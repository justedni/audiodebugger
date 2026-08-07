// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/AudioDebug.h"
#include "core/RptFileFormat.h"

#include <string>
#include <array>
#include <fstream>
#include <mutex>

namespace AudioDebug {

struct ChannelReaderState
{
    std::vector<std::vector<float>> m_tempBuffer;
    std::atomic<int64_t> samplesCount;
    int numChannels = 0;

    void init(int numChans, int blockSize)
    {
        numChannels = numChans;

        m_tempBuffer.resize(numChans);
        for (int iChan = 0; iChan < numChans; iChan++) {
            m_tempBuffer[iChan].resize(blockSize);
        }

        samplesCount.store(0);
    }

    void ensureCapacity(int numSamples)
    {
        for (auto& chan : m_tempBuffer)
        {
            if (static_cast<int>(chan.size()) < numSamples)
                chan.resize(numSamples);
        }
    }

    void resetCount()
    {
        samplesCount.store(0);
    }
};

enum EProcessType : uint8_t { Normal, DryRun };

using AudioBlockCallback = std::function<void(int globalChanId,
    float value,
    int64_t blockStartSample,
    int blockSize)>;

using MarkerCallback = std::function<void(Marker&&)>;
using MidiCallback = std::function<void(int64_t timestamp, std::vector<RawMidiMessage>)>;

enum EProcessRet : uint8_t { Success, Sleep, Stop };


class ReportFile : public FileReader
{
public:
    enum EFileSlot : uint8_t
    {
        Rpt = 0,
        Gen
    };

    ReportFile(EFileSlot slot, const std::string& filePath);

    struct BlockEntry
    {
        int64_t sampleStart;
        uint64_t fileOffset;

        bool operator<(int64_t sample) const {
            return sampleStart < sample;
        }
    };

    const std::string& getFilePath() const { return m_filePath; }
    const FileHeader& getHeader() const override { return m_header; }

    bool openFile(bool liveMode);
    void initChanStates(bool bHasInput, uint8_t inChannels, uint8_t outChannels, uint32_t blockSize);
    int64_t getFileOffset();
    void seekToDataStart();
    void seekTo(uint64_t offset) override;
    bool peekNextBlockHeader(BlockHeader& out_header);
    bool readFileBlock(ReadBlock& out_block, uint32_t& out_blockSize) override;
    void clearFlags();

    EProcessRet processOneBlock(ReadBlock& block, EProcessType processType, bool bStopAtEOF, int32_t& audioSamplesProcessed);
    void simulateReadBlock(const ReadBlock& block, EProcessType processType);

    void processAudioBlock(const ReadBlock& block, EProcessType processType, uint64_t blockOffset, int numChannels, int32_t& out_audioSamplesProcessed);

    void fillSampleCache(int64_t startSample, int64_t numSamples, std::vector<SampleCache*> out_cache);

    bool findOffsetForCustomBlock(int64_t sampleNumber, uint64_t& out_offset, int64_t& out_blockStart) override;
    bool findOffsetForSample(const std::vector<BlockEntry>& blocks, int64_t sampleNumber, uint64_t& out_offset, int64_t& out_blockStart);

    bool processCustomBlock(const ReadBlock& block, EProcessType processType, uint64_t blockOffset);

    std::vector<int32_t> as_samples(const ReadBlock& block) const;

    int64_t getOutStateSampleCount() const;
    const std::vector<std::vector<float>>& getInTempBuffer() const;
    const std::vector<std::vector<float>>& getOutTempBuffer() const;

    bool usesFrames() const { return m_usesFrames; }
    uint32_t getFrameSampleCount() const;
    int64_t getFrameStartSample(uint32_t blockId) const;
    uint64_t getTotalSamples() const override;

    bool hasPluginState() const { return m_stateBlockSize > 0; }
    bool readPluginState(PluginState& out_state) override;

    uint64_t getDataStartOffset() const { return m_dataStartOffset; }

    AudioBlockCallback onAudioBlock;
    MarkerCallback onMarker;
    MidiCallback onMidi;

private:
    void fillSampleCacheFromBlock(BlockType targetBlockType, int64_t startSample, int64_t numSamples, uint8_t numChannels, uint8_t outChanOffset, std::vector<SampleCache*> out_cache);
    void fixLegacyBitDepth();
    void fixLegacyHeaderFields();
    void locatePluginStateBlock();
    void resetFrameState();

    void dispatchBlock(const ReadBlock& block, EProcessType processType, uint64_t blockOffset, int32_t& out_audioSamplesProcessed);

    void processFrameBlock(const ReadBlock& block);
    void processMetadataBlock(const ReadBlock& block, EProcessType processType);
    void processMidiBlock(const ReadBlock& block, EProcessType processType);

    const EFileSlot m_slot;
    const std::string m_filePath;
    std::ifstream m_file;
    uint64_t m_simulatedPos = 0;
    bool m_liveMode = false;
    FileHeader m_header;

    mutable std::mutex m_mutex;

    std::vector<BlockEntry> m_audioInBlocks;
    std::vector<BlockEntry> m_audioOutBlocks;
    std::vector<BlockEntry> m_customBlocks;

    std::unique_ptr<ChannelReaderState> m_chanStateIn;
    std::unique_ptr<ChannelReaderState> m_chanStateOut;
    std::atomic<int64_t> m_customBlockSamplescount;

    uint64_t m_dataStartOffset = sizeof(FileHeader);
    uint64_t m_stateBlockOffset = 0;
    uint32_t m_stateBlockSize = 0;

    bool m_usesFrames = false;
    uint32_t m_currentFrameId = 0;
    std::atomic<uint32_t> m_currentFrameSamples{ 0 };
    std::atomic<int64_t> m_currentFrameStart{ 0 };
};


void writeFileHeader(std::ostream& os, const FileHeader& header);
void writeBlockHeader(std::ostream& os, const BlockHeader& header);

uint32_t calculateChecksum(const uint8_t* data, size_t size);
uint32_t calculateChecksum(const std::vector<uint8_t>& data);

} // namespace AudioDebug
