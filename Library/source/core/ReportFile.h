// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/AudioDebug.h"

#include <string>
#include <array>
#include <fstream>
#include <mutex>

namespace AudioDebug {

enum EBitDepth : uint8_t;

enum class BlockType : uint32_t
{
    INVALID     = 0,
    AUDIOIN     = 0x49445541,   // 'AUDI'
    AUDIOOUT    = 0x4F445541,   // 'AUDO'
    METADATA    = 0x4154454d,   // 'META'
    MIDI        = 0x4944494d,   // 'MIDI'
    CUSTOM      = 0x4d545343,   // 'CSTM'
};

bool isBlockValid(BlockType type);

#define ARPT_MAGIC 0x54505241

#ifdef __GNUC__
#define PACK( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#endif

#ifdef _MSC_VER
#define PACK( __Declaration__ ) __pragma(pack(push, 1)) \
__Declaration__ \
__pragma(pack(pop))
#endif


PACK(struct FileHeader
{
    uint32_t magic = ARPT_MAGIC;
    uint32_t version = 1;
    uint32_t sample_rate = 0;
    uint32_t block_size = 0;
    uint32_t blocks_count = 0;
    uint32_t in_channels = 0;
    uint32_t out_channels = 0;
    uint8_t bit_depth = 0;
    uint8_t reserved1[3];
    uint32_t reserved2[2];
});

PACK(struct BlockHeader
{
    uint32_t block_type = 0;
    uint32_t data_size = 0;
    uint32_t block_id = 0;
    uint32_t checksum = 0;
});


struct ReadBlock
{
    BlockType type;
    uint32_t block_id;
    std::vector<uint8_t> data;
    bool checksum_valid;

    ReadBlock();
    ReadBlock(BlockType t, uint32_t id, std::vector<uint8_t> d, bool valid);

    std::vector<int32_t> as_samples(EBitDepth bitDepth) const;

    std::string as_string() const {
        return std::string(data.begin(), data.end());
    }
};

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

class ReportFile
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
    const FileHeader& getHeader() const { return m_header; }

    bool openFile(bool liveMode);
    void initChanStates(bool bHasInput, uint8_t inChannels, uint8_t outChannels, uint32_t blockSize);
    int64_t getFileOffset();
    void seekToDataStart();
    void seekTo(uint64_t offset);
    bool peekNextBlockHeader(BlockHeader& out_header);
    bool readFileBlock(ReadBlock& out_block, uint32_t& out_blockSize);
    void clearFlags();

    EProcessRet processOneBlock(ReadBlock& block, EProcessType processType, bool bStopAtEOF, int32_t& audioSamplesProcessed);
    void simulateReadBlock(const ReadBlock& block, EProcessType processType);

    void processAudioBlock(const ReadBlock& block, EProcessType processType, uint64_t blockOffset, int numChannels, int32_t& out_audioSamplesProcessed);
    void findSamplesFromBlock(BlockType targetBlockType, int64_t startSample, int64_t numSamples, uint8_t numChannels, uint8_t outChanOffset, std::vector<std::vector<float>>& outSamples);

    bool findOffsetForCustomBlock(int64_t sampleNumber, uint64_t& out_offset, int64_t& out_blockStart);
    bool findOffsetForSample(const std::vector<BlockEntry>& blocks, int64_t sampleNumber, uint64_t& out_offset, int64_t& out_blockStart);

    bool processCustomBlock(const ReadBlock& block, EProcessType processType, uint64_t blockOffset);

    std::vector<int32_t> as_samples(const ReadBlock& block) const;

    int64_t getOutStateSampleCount() const;
    const std::vector<std::vector<float>>& getInTempBuffer() const;
    const std::vector<std::vector<float>>& getOutTempBuffer() const;

    AudioBlockCallback onAudioBlock;
    MarkerCallback onMarker;
    MidiCallback onMidi;

private:
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
};


void writeFileHeader(std::ostream& os, const FileHeader& header);
void writeBlockHeader(std::ostream& os, const BlockHeader& header);

uint32_t calculateChecksum(const std::vector<uint8_t>& data);

} // namespace AudioDebug
