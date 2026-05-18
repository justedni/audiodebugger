#include "core/ReportFile.h"

#include <assert.h>
#include <chrono>
#include <thread>

namespace AudioDebug {

ReadBlock::ReadBlock()
    : type(BlockType::INVALID)
    , block_id(0)
    , checksum_valid(false)
{}

ReadBlock::ReadBlock(BlockType t, uint32_t id, std::vector<uint8_t> d, bool valid)
    : type(t)
    , block_id(id)
    , data(std::move(d))
    , checksum_valid(valid)
{
}

std::vector<int32_t> ReadBlock::as_samples(EBitDepth bitDepth) const
{


    switch (bitDepth)
    {
    case EBitDepth::Int24:
    {
        std::vector<int32_t> samples(data.size() / 3);

        for (size_t i = 0; i < samples.size(); ++i)
        {
            int32_t sample = static_cast<int32_t>(
                data[i * 3] |
                (data[i * 3 + 1] << 8) |
                (data[i * 3 + 2] << 16)
                );

            if (sample & 0x800000) {
                sample |= 0xFF000000;
            }

            samples[i] = sample;
        }

        return samples;
        break;
    }
    default:
    case EBitDepth::Int32:
    {
        std::vector<int32_t> samples(data.size() / sizeof(int32_t));
        std::memcpy(samples.data(), data.data(), data.size());
        return samples;
    }
    }
}

ReportFile::ReportFile(EFileSlot slot, const std::string& filePath)
    : m_slot(slot)
    , m_filePath(filePath)
{
}

void ReportFile::initChanStates(bool bHasInput, uint8_t inChannels, uint8_t outChannels, uint32_t blockSize)
{
    if (bHasInput)
    {
        m_chanStateIn.reset(new ChannelReaderState());
        m_chanStateIn->init(inChannels, blockSize);
    }

    m_chanStateOut.reset(new ChannelReaderState());
    m_chanStateOut->init(outChannels, blockSize);

    m_customBlocks.clear();
    m_customBlockSamplescount.store(0);
}

bool ReportFile::openFile(bool liveMode)
{
    m_liveMode = liveMode;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_audioInBlocks.clear();
        m_audioOutBlocks.clear();
        m_customBlocks.clear();
    }


    auto flags = std::ios::binary | std::ios::in;

    if (!liveMode)
        flags |= std::ios::ate;

    m_file.open(m_filePath, flags);
    if (!m_file.is_open())
        return false;

    if (!liveMode)
    {
        auto ffileSize = m_file.tellg();
        m_file.seekg(0, std::ios::beg);
    }

    m_file.read(reinterpret_cast<char*>(&m_header), sizeof(AudioDebug::FileHeader));
    if (m_file.gcount() != sizeof(AudioDebug::FileHeader))
    {
        m_file.close();
        return false;
    }

    if (m_header.magic != ARPT_MAGIC)
    {
        m_file.close();
        return false;
    }

    return true;
}

int64_t ReportFile::getFileOffset()
{
    return m_file.tellg();
}

void ReportFile::seekToDataStart()
{
    m_file.clear();
    m_file.seekg(sizeof(AudioDebug::FileHeader), std::ios::beg);

    m_simulatedPos = sizeof(AudioDebug::FileHeader);
}

void ReportFile::seekTo(uint64_t offset)
{
    m_file.clear();
    m_file.seekg(offset, std::ios::beg);
}

void ReportFile::clearFlags()
{
    m_file.clear();
}

bool ReportFile::findOffsetForSample(const std::vector<BlockEntry>& blocks, int64_t sampleNumber, uint64_t& out_offset, int64_t& out_blockStart)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = std::upper_bound(blocks.begin(), blocks.end(), sampleNumber,
        [](int64_t sample, const BlockEntry& entry) {
            return sample < entry.sampleStart;
        }
    );

    if (it != blocks.begin())
    {
        --it;
        out_offset = it->fileOffset;
        out_blockStart = it->sampleStart;
        return true;
    }

    out_blockStart = 0;
    return false; // Error case
}

bool ReportFile::findOffsetForCustomBlock(int64_t sampleNumber, uint64_t& out_offset, int64_t& out_blockStart)
{
    return findOffsetForSample(m_customBlocks, sampleNumber, out_offset, out_blockStart);
}

//int ReportFile::getBlockIndexForSample(int64_t sampleNumber)
//{
//    auto it = std::upper_bound(
//        audioBlocks.begin(),
//        audioBlocks.end(),
//        sampleNumber,
//        [](int64_t sample, const AudioBlockEntry& entry) {
//        return sample < entry.sampleStart;
//    }
//    );
//
//    if (it != audioBlocks.begin()) {
//        return std::distance(audioBlocks.begin(), it - 1);
//    }
//
//    return -1;
//}

bool ReportFile::peekNextBlockHeader(BlockHeader& out_header)
{
    auto pos = m_file.tellg();

    BlockHeader header;
    m_file.read(reinterpret_cast<char*>(&header), sizeof(BlockHeader));

    m_file.seekg(pos);
    m_file.clear();

    if (m_file.gcount() == sizeof(BlockHeader))
    {
        out_header = header;
        return true;
    }

    return false;
}

bool ReportFile::readFileBlock(AudioDebug::ReadBlock& out_block, uint32_t& out_blockSize)
{
    using namespace AudioDebug;

    if (!m_file.is_open()) {
        return false;
    }

    std::streampos current_pos = m_file.tellg();

    auto seekAndClear = [&]()
    {
        if (m_liveMode)
        {
            m_file.seekg(current_pos);
            m_file.clear();
        }
    };

    BlockHeader header;
    m_file.read(reinterpret_cast<char*>(&header), sizeof(BlockHeader));

    if (m_file.gcount() != sizeof(BlockHeader))
    {
        seekAndClear();
        return false;
    }

    BlockType type = static_cast<BlockType>(header.block_type);
    if (!isBlockValid(type))
    {
        seekAndClear();
        return false;
    }

    std::vector<uint8_t> data(header.data_size);
    m_file.read(reinterpret_cast<char*>(data.data()), header.data_size);

    if (static_cast<uint32_t>(m_file.gcount()) != header.data_size)
    {
        seekAndClear();
        return false;
    }

    uint32_t computed_checksum = calculateChecksum(data);
    bool checksum_valid = (computed_checksum == header.checksum);

    out_block = ReadBlock(type, header.block_id, std::move(data), checksum_valid);
    out_blockSize = header.data_size;

    BlockHeader nextHeader;
    if (peekNextBlockHeader(nextHeader) && nextHeader.block_type != 0)
    {
        isBlockValid(static_cast<BlockType>(nextHeader.block_type));
    }

    return true;
}

EProcessRet ReportFile::processOneBlock(ReadBlock& block, EProcessType processType, bool bStopAtEOF, int32_t& out_audioSamplesProcessed)
{
    out_audioSamplesProcessed = 0;

    uint64_t blockOffset = getFileOffset();

    uint32_t data_size = 0;
    bool bSuccess = readFileBlock(block, data_size);
    if (!bSuccess)
    {
        if (!bStopAtEOF)
        {
            clearFlags();
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(50ms);

            return EProcessRet::Sleep;
        }
        else
        {
            // Stop at EOF
            return EProcessRet::Stop;
        }
    }
    else
    {
        auto& header = getHeader();

        if (block.type == BlockType::AUDIOIN)
        {
            processAudioBlock(block, processType, blockOffset, header.in_channels, out_audioSamplesProcessed);
        }
        else if (block.type == BlockType::AUDIOOUT)
        {
            processAudioBlock(block, processType, blockOffset, header.out_channels, out_audioSamplesProcessed);
        }
        else if (block.type == BlockType::METADATA)
        {
            auto* data = block.data.data();
            int remainingData = block.data.size();
            size_t offset = 0;

            while (remainingData > 0)
            {
                uint32_t block_read = 0;
                auto marker = AudioDebug::Metadata::readMarker(data + offset, block_read);

                if (processType != EProcessType::DryRun)
                {
                    onMarker(std::move(marker));
                }

                remainingData -= block_read;
                offset += block_read;
            }
        }
        else if (block.type == BlockType::MIDI)
        {
            if (processType != EProcessType::DryRun)
            {
                std::vector<RawMidiMessage> result = AudioDebugger::readAllMidiEvents(block.data);
                int actualTimestamp = block.block_id * header.block_size;

                onMidi(actualTimestamp, std::move(result));
            }
        }
        else
        {
            processCustomBlock(block, processType, blockOffset);
        }

        return EProcessRet::Success;
    }
}


void ReportFile::simulateReadBlock(const ReadBlock& block, EProcessType processType)
{
    uint64_t blockOffset = m_simulatedPos;

    int32_t unusedProcessedSamples = 0;
    auto& header = getHeader();

    if (block.type == BlockType::AUDIOIN)
    {
        processAudioBlock(block, processType, blockOffset, header.in_channels, unusedProcessedSamples);
    }
    else if (block.type == BlockType::AUDIOOUT)
    {
        processAudioBlock(block, processType, blockOffset, header.out_channels, unusedProcessedSamples);
    }
    else if (block.type == BlockType::METADATA)
    {
        auto* data = block.data.data();
        int remainingData = block.data.size();
        size_t offset = 0;

        while (remainingData > 0)
        {
            uint32_t block_read = 0;
            auto marker = AudioDebug::Metadata::readMarker(data + offset, block_read);

            if (processType != EProcessType::DryRun)
            {
                onMarker(std::move(marker));
            }

            remainingData -= block_read;
            offset += block_read;
        }
    }
    else if (block.type == BlockType::MIDI)
    {
        if (processType != EProcessType::DryRun)
        {
            std::vector<RawMidiMessage> result = AudioDebugger::readAllMidiEvents(block.data);
            int actualTimestamp = block.block_id * header.block_size;

            onMidi(actualTimestamp, std::move(result));
        }
    }
    else
    {
        processCustomBlock(block, processType, blockOffset);
    }

    m_simulatedPos += sizeof(BlockHeader);
    m_simulatedPos += block.data.size();
}

void ReportFile::processAudioBlock(const ReadBlock& block, EProcessType processType, uint64_t blockOffset, int numChannels, int32_t& out_audioSamplesProcessed)
{
    if (block.data.empty())
        return;

    assert(block.type == BlockType::AUDIOIN || block.type == BlockType::AUDIOOUT);
    const bool bIsOut = (block.type == BlockType::AUDIOOUT);
    ChannelReaderState& state = bIsOut ? *m_chanStateOut.get() : *m_chanStateIn.get();
    auto& blocks = bIsOut ? m_audioOutBlocks : m_audioInBlocks;
    const auto& header = getHeader();

    int64_t startSample = state.samplesCount.load();

    auto samples = block.as_samples((EBitDepth)header.bit_depth);

    int remainingSamples = samples.size();
    const int numberOfSamplesPerBlock = remainingSamples / (numChannels);
    auto it = samples.begin();
    int outSampleCount = 0;

    assert(numberOfSamplesPerBlock <= state.m_tempBuffer[0].size());

    while (remainingSamples > 0)
    {
        assert(it != samples.end());

        for (int iChan = 0; iChan < numChannels; iChan++)
        {
            int32_t sampleInt = *it;
            float sample = AudioDebugger::intToFloat(sampleInt, header.bit_depth);

            state.m_tempBuffer[iChan][outSampleCount] = sample;

            it++;
            remainingSamples--;
        }

        if (processType != EProcessType::DryRun)
        {
            int globalChanId = 0;
            if (m_slot == EFileSlot::Rpt)
            {
                globalChanId = bIsOut ? header.in_channels : 0;
            }
            else
            {
                assert(m_slot == EFileSlot::Gen);
                globalChanId = header.in_channels + header.out_channels;
            }

            for (int iChan = 0; iChan < numChannels; iChan++)
            {
                float value = state.m_tempBuffer[iChan][outSampleCount];
                onAudioBlock(globalChanId + iChan, value, startSample, state.m_tempBuffer[0].size());
            }
        }

        outSampleCount++;
    }

    while (outSampleCount < state.m_tempBuffer[0].size())
    {
        for (int iChan = 0; iChan < numChannels; iChan++)
        {
            state.m_tempBuffer[iChan][outSampleCount] = 0.0f;
        }

        outSampleCount++;
    }

    if (processType != EProcessType::DryRun)
    {
        //addFileOffset(slot, startSample, blockOffset);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            blocks.push_back({ startSample, blockOffset });
        }

        state.samplesCount.store(startSample + numberOfSamplesPerBlock);
    }

    out_audioSamplesProcessed = numberOfSamplesPerBlock;
}


bool ReportFile::processCustomBlock(const ReadBlock& block, EProcessType processType, uint64_t blockOffset)
{
    assert(block.type == AudioDebug::BlockType::CUSTOM);

    if (processType != AudioDebug::EProcessType::DryRun)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        int64_t startSample = m_customBlockSamplescount.load();

        m_customBlocks.push_back({ startSample, blockOffset });
        m_customBlockSamplescount.store(startSample + getHeader().block_size);
    }

    return true;
}

std::vector<int32_t> ReportFile::as_samples(const ReadBlock& block) const
{
    return block.as_samples((EBitDepth)getHeader().bit_depth);
}

void ReportFile::findSamplesFromBlock(BlockType targetBlockType, int64_t startSample, int64_t numSamples, uint8_t numChannels, uint8_t outChanOffset, std::vector<std::vector<float>>& outSamples)
{
    uint64_t out_fileOffset = 0;
    int64_t out_blockStart = 0;

    const auto& header = getHeader();

    assert(targetBlockType == BlockType::AUDIOIN || targetBlockType == BlockType::AUDIOOUT);
    const std::vector<BlockEntry>& blocks = (targetBlockType == BlockType::AUDIOOUT) ? m_audioOutBlocks : m_audioInBlocks;
    bool bFound = findOffsetForSample(blocks, startSample, out_fileOffset, out_blockStart);
    assert(bFound);
    if (bFound)
    {
        assert(out_fileOffset > 0);
        seekTo(out_fileOffset);

        int64_t samplesToPush = numSamples;
        int64_t outputOffset = 0;

        int offsetAtFirstBlock = (startSample - out_blockStart);
        bool bFirstBlock = true;

        AudioDebug::ReadBlock block;

        while (samplesToPush > 0)
        {
            uint32_t data_size = 0;
            if (!readFileBlock(block, data_size))
            {
                break;
            }

            if (block.type == targetBlockType)
            {
                auto samples = block.as_samples((EBitDepth)header.bit_depth);
                int remainingSamples = samples.size();
                auto it = samples.begin();

                int frameCount = 0;

                while (remainingSamples > 0)
                {
                    assert(it != samples.end());

                    bool bAddSamples = !bFirstBlock || (frameCount >= offsetAtFirstBlock);

                    for (int chan = 0; chan < numChannels; chan++)
                    {
                        int32_t sampleInt = *it;
                        float sample = AudioDebugger::intToFloat(sampleInt, header.bit_depth);
                        int outChan = chan + outChanOffset;

                        if (bAddSamples)
                            outSamples[outChan][outputOffset] = sample;

                        it++;
                        remainingSamples--;
                    }

                    if (bAddSamples)
                    {
                        outputOffset++;
                        samplesToPush--;
                    }

                    frameCount++;

                    if (samplesToPush <= 0)
                        break;
                }

                bFirstBlock = false;
            }
        }
    }
}

int64_t ReportFile::getOutStateSampleCount() const
{
    return m_chanStateOut->samplesCount.load();
}

const std::vector<std::vector<float>>& ReportFile::getInTempBuffer() const
{
    return m_chanStateIn->m_tempBuffer;
}

const std::vector<std::vector<float>>& ReportFile::getOutTempBuffer() const
{
    return m_chanStateOut->m_tempBuffer;
}

bool isBlockValid(BlockType type)
{
    switch (type)
    {
    case BlockType::AUDIOIN:
    case BlockType::AUDIOOUT:
    case BlockType::METADATA:
    case BlockType::MIDI:
    case BlockType::CUSTOM:
        return true;
    default:
        assert(false);
        return false;
    }
}

void writeFileHeader(std::ostream& os, const FileHeader& h)
{
    int16_t internalCount = 0;

    auto writeInt = [&](auto& val)
    {
        os.write(reinterpret_cast<const char*>(&val), sizeof(uint32_t));
        internalCount += sizeof(uint32_t);
    };

    auto writeInt8 = [&](auto& val)
    {
        os.write(reinterpret_cast<const char*>(&val), sizeof(uint8_t));
        internalCount += sizeof(uint8_t);
    };

    writeInt(h.magic);
    writeInt(h.version);
    writeInt(h.sample_rate);
    writeInt(h.block_size);
    writeInt(h.blocks_count);
    writeInt(h.in_channels);
    writeInt(h.out_channels);
    writeInt8(h.bit_depth);
    writeInt8(h.reserved1[0]);
    writeInt8(h.reserved1[1]);
    writeInt8(h.reserved1[2]);
    writeInt(h.reserved2[0]);
    writeInt(h.reserved2[1]);

    assert(internalCount == sizeof(FileHeader));
}

void writeBlockHeader(std::ostream& os, const BlockHeader& h)
{
    int16_t internalCount = 0;

    auto writeInt = [&](auto& val)
    {
        os.write(reinterpret_cast<const char*>(&val), sizeof(uint32_t));
        internalCount += sizeof(uint32_t);
    };

    writeInt(h.block_type);
    writeInt(h.data_size);
    writeInt(h.block_id);
    writeInt(h.checksum);

    assert(internalCount == sizeof(BlockHeader));
}

uint32_t calculateChecksum(const std::vector<uint8_t>& data)
{
    uint32_t sum = 0;
    for (uint8_t byte : data) {
        sum += byte;
    }
    return sum;
}

} // namespace AudioDebug
