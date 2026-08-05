// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace AudioDebug {

enum EBitDepth : uint8_t;

enum class BlockType : uint32_t
{
    INVALID = 0,
    AUDIOIN = 0x49445541,   // 'AUDI'
    AUDIOOUT = 0x4F445541,   // 'AUDO'
    METADATA = 0x4154454d,   // 'META'
    MIDI = 0x4944494d,   // 'MIDI'
    CUSTOM = 0x4d545343,   // 'CSTM'
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

struct SampleCache
{
    int64_t startSample;
    int64_t numSamples;
    std::vector<float> samples;

    bool contains(int64_t sample) const {
        return sample >= startSample && sample < (startSample + numSamples);
    }

    bool isValid() const {
        return numSamples > 0 && !samples.empty();
    }

    void clear() {
        samples.clear();
        numSamples = 0;
    }
};

class FileReader
{
public:
    virtual const FileHeader& getHeader() const = 0;
    virtual void seekTo(uint64_t offset) = 0;
    virtual bool readFileBlock(ReadBlock& out_block, uint32_t& out_blockSize) = 0;
    virtual bool findOffsetForCustomBlock(int64_t sampleNumber, uint64_t& out_offset, int64_t& out_blockStart) = 0;
};

} // namespace AudioDebug
