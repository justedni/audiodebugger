// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

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

    resetFrameState();
}

void ReportFile::resetFrameState()
{
    m_currentFrameId = 0;
    m_currentFrameSamples.store(0);
    m_currentFrameStart.store(0);
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

    resetFrameState();

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

    fixLegacyBitDepth();
    fixLegacyHeaderFields();

    m_usesFrames = fileUsesFrames(m_header);

    return true;
}

void ReportFile::fixLegacyBitDepth()
{
    if (m_header.bit_depth >= (int)EBitDepth::Count)
    {
        m_header.bit_depth = EBitDepth::Int24;
    }
}

void ReportFile::fixLegacyHeaderFields()
{
    // v1 files were written with the reserved bytes uninitialized, so whatever is
    // sitting in `flags` and `total_samples` is garbage. Clear it so nothing
    // downstream can accidentally trust it.
    if (m_header.version < ERptFileVersion::WithFrames)
    {
        m_header.total_samples = 0;
    }

    m_header.reserved1[0] = 0;
    m_header.reserved1[1] = 0;
}

uint32_t ReportFile::getFrameSampleCount() const
{
    if (m_usesFrames)
    {
        const uint32_t frameSamples = m_currentFrameSamples.load();
        if (frameSamples > 0)
            return frameSamples;
    }

    return m_header.block_size;
}

int64_t ReportFile::getFrameStartSample(uint32_t blockId) const
{
    if (m_usesFrames)
        return m_currentFrameStart.load();

    // v1: positions can only be derived from the fixed block size.
    return static_cast<int64_t>(blockId) * static_cast<int64_t>(m_header.block_size);
}

uint64_t ReportFile::getTotalSamples() const
{
    if (m_header.version >= ERptFileVersion::WithFrames)
        return m_header.total_samples;

    return static_cast<uint64_t>(m_header.blocks_count) * static_cast<uint64_t>(m_header.block_size);
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

    resetFrameState();
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

bool ReportFile::peekNextBlockHeader(BlockHeader& out_header)
{
    auto pos = m_file.tellg();

    BlockHeader header;
    m_file.read(reinterpret_cast<char*>(&header), sizeof(BlockHeader));
    const bool complete = (m_file.gcount() == sizeof(BlockHeader));

    m_file.clear();
    m_file.seekg(pos);

    if (complete)
        out_header = header;

    return complete;
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
            m_file.clear();
            m_file.seekg(current_pos);
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
        // TODO: this should raise a "critical error", not silently fail
        // The file is potentially corrupted. Re-trying later won't help
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

    return true;
}

void ReportFile::dispatchBlock(const ReadBlock& block, EProcessType processType, uint64_t blockOffset, int32_t& out_audioSamplesProcessed)
{
    const auto& header = getHeader();

    switch (block.type)
    {
    case BlockType::FRAME:
        processFrameBlock(block);
        break;

    case BlockType::AUDIOIN:
        processAudioBlock(block, processType, blockOffset, header.in_channels, out_audioSamplesProcessed);
        break;

    case BlockType::AUDIOOUT:
        processAudioBlock(block, processType, blockOffset, header.out_channels, out_audioSamplesProcessed);
        break;

    case BlockType::METADATA:
        processMetadataBlock(block, processType);
        break;

    case BlockType::MIDI:
        processMidiBlock(block, processType);
        break;

    default:
        processCustomBlock(block, processType, blockOffset);
        break;
    }
}

void ReportFile::processFrameBlock(const ReadBlock& block)
{
    FrameInfo info;
    if (!readFrameInfo(block.data.data(), block.data.size(), info))
    {
        assert(false && "malformed FRAME block");
        return;
    }

    m_currentFrameId = block.block_id;
    m_currentFrameSamples.store(info.num_samples);
    m_currentFrameStart.store(info.start_sample);
}

void ReportFile::processMetadataBlock(const ReadBlock& block, EProcessType processType)
{
    auto* data = block.data.data();
    int remainingData = static_cast<int>(block.data.size());
    size_t offset = 0;

    while (remainingData > 0)
    {
        uint32_t block_read = 0;
        auto marker = AudioDebug::Metadata::readMarker(data + offset, block_read);

        if (block_read == 0)
        {
            assert(false && "marker parser made no progress - malformed METADATA block");
            break;
        }

        if (processType != EProcessType::DryRun)
        {
            onMarker(std::move(marker));
        }

        remainingData -= block_read;
        offset += block_read;
    }
}

void ReportFile::processMidiBlock(const ReadBlock& block, EProcessType processType)
{
    if (processType == EProcessType::DryRun)
        return;

    std::vector<RawMidiMessage> result = Metadata::readAllMidiEvents(block.data);

    const int64_t actualTimestamp = getFrameStartSample(block.block_id);
    onMidi(actualTimestamp, std::move(result));
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

    dispatchBlock(block, processType, blockOffset, out_audioSamplesProcessed);

    return EProcessRet::Success;
}


void ReportFile::simulateReadBlock(const ReadBlock& block, EProcessType processType)
{
    uint64_t blockOffset = m_simulatedPos;

    int32_t unusedProcessedSamples = 0;
    dispatchBlock(block, processType, blockOffset, unusedProcessedSamples);

    m_simulatedPos += sizeof(BlockHeader);
    m_simulatedPos += block.data.size();
}

void ReportFile::processAudioBlock(const ReadBlock& block, EProcessType processType, uint64_t blockOffset, int numChannels, int32_t& out_audioSamplesProcessed)
{
    if (block.data.empty() || numChannels <= 0)
        return;

    assert(block.type == BlockType::AUDIOIN || block.type == BlockType::AUDIOOUT);
    const bool bIsOut = (block.type == BlockType::AUDIOOUT);
    ChannelReaderState& state = bIsOut ? *m_chanStateOut.get() : *m_chanStateIn.get();
    auto& blocks = bIsOut ? m_audioOutBlocks : m_audioInBlocks;
    const auto& header = getHeader();

    auto samples = block.as_samples((EBitDepth)header.bit_depth);

    int remainingSamples = static_cast<int>(samples.size());
    const int numberOfSamplesPerBlock = remainingSamples / numChannels;

    assert(!m_usesFrames
        || m_currentFrameSamples.load() == 0
        || static_cast<uint32_t>(numberOfSamplesPerBlock) == m_currentFrameSamples.load());

    const int64_t startSample = m_usesFrames ? m_currentFrameStart.load()
                                             : state.samplesCount.load();

    state.ensureCapacity(numberOfSamplesPerBlock);

    auto it = samples.begin();
    int outSampleCount = 0;

    while (remainingSamples >= numChannels)
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

                onAudioBlock(globalChanId + iChan, value, startSample, numberOfSamplesPerBlock);
            }
        }

        outSampleCount++;
    }

    while (outSampleCount < static_cast<int>(state.m_tempBuffer[0].size()))
    {
        for (int iChan = 0; iChan < numChannels; iChan++)
        {
            state.m_tempBuffer[iChan][outSampleCount] = 0.0f;
        }

        outSampleCount++;
    }

    if (processType != EProcessType::DryRun)
    {
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

        const int64_t startSample = m_usesFrames ? m_currentFrameStart.load()
                                                 : m_customBlockSamplescount.load();

        m_customBlocks.push_back({ startSample, blockOffset });
        m_customBlockSamplescount.store(startSample + getFrameSampleCount());
    }

    return true;
}

std::vector<int32_t> ReportFile::as_samples(const ReadBlock& block) const
{
    return block.as_samples((EBitDepth)getHeader().bit_depth);
}

void ReportFile::fillSampleCacheFromBlock(BlockType targetBlockType, int64_t startSample, int64_t numSamples, uint8_t numChannels, uint8_t outChanOffset, std::vector<SampleCache*> out_cache)
{
    uint64_t out_fileOffset = 0;
    int64_t out_blockStart = 0;

    const auto& header = getHeader();

    assert(targetBlockType == BlockType::AUDIOIN || targetBlockType == BlockType::AUDIOOUT);
    const std::vector<BlockEntry>& blocks = (targetBlockType == BlockType::AUDIOOUT) ? m_audioOutBlocks : m_audioInBlocks;
    if (blocks.empty())
        return;

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
                int remainingSamples = static_cast<int>(samples.size());
                auto it = samples.begin();

                int frameCount = 0;

                while (remainingSamples >= numChannels)
                {
                    assert(it != samples.end());

                    bool bAddSamples = !bFirstBlock || (frameCount >= offsetAtFirstBlock);

                    for (int chan = 0; chan < numChannels; chan++)
                    {
                        int32_t sampleInt = *it;
                        float sample = AudioDebugger::intToFloat(sampleInt, header.bit_depth);
                        int outChan = chan + outChanOffset;

                        if (bAddSamples)
                            out_cache[outChan]->samples[outputOffset] = sample;

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

void ReportFile::fillSampleCache(int64_t startSample, int64_t numSamples, std::vector<SampleCache*> out_cache)
{
    if (m_slot == EFileSlot::Rpt)
    {
        fillSampleCacheFromBlock(BlockType::AUDIOIN, startSample, numSamples, m_header.in_channels, 0, out_cache);
        fillSampleCacheFromBlock(BlockType::AUDIOOUT, startSample, numSamples, m_header.out_channels, m_header.in_channels, out_cache);
    }
    else if (m_slot == EFileSlot::Gen)
    {
        fillSampleCacheFromBlock(BlockType::AUDIOOUT, startSample, numSamples, m_header.out_channels, 0, out_cache);
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
    case BlockType::FRAME:
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

    auto writeInt64 = [&](auto& val)
    {
        os.write(reinterpret_cast<const char*>(&val), sizeof(uint64_t));
        internalCount += sizeof(uint64_t);
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
    writeInt64(h.total_samples);

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

namespace {

    struct Crc32Table
    {
        uint32_t values[256];

        Crc32Table() noexcept
        {
            const uint32_t kPolynomial = 0xEDB88320u;

            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1u) ? (kPolynomial ^ (c >> 1)) : (c >> 1);

                values[i] = c;
            }
        }
    };

}

uint32_t calculateChecksum(const uint8_t* data, size_t size)
{
    static const Crc32Table table;

    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i)
        crc = table.values[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);

    return crc ^ 0xFFFFFFFFu;
}

uint32_t calculateChecksum(const std::vector<uint8_t>& data)
{
    return calculateChecksum(data.data(), data.size());
}
} // namespace AudioDebug
