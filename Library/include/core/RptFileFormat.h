// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace AudioDebug {

enum EBitDepth : uint8_t;

enum ERptFileVersion : uint8_t
{
    Legacy = 1,
    WithFrames = 2,
    VERSION_COUNT
};

constexpr uint32_t kCurrentFormatVersion = ERptFileVersion::VERSION_COUNT - 1;

enum class BlockType : uint32_t
{
    INVALID = 0,
    AUDIOIN = 0x49445541,   // 'AUDI'
    AUDIOOUT = 0x4F445541,   // 'AUDO'
    METADATA = 0x4154454d,   // 'META'
    MIDI = 0x4944494d,   // 'MIDI'
    CUSTOM = 0x4d545343,   // 'CSTM'
    FRAME = 0x454d5246,   // 'FRME'  (v2+)
    STATE = 0x54415453,   // 'STAT'  (v2+)
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

enum EFileFlags : uint8_t
{
    FileFlag_None = 0,
    FileFlag_HasState = 1 << 0,
};


PACK(struct FileHeader
{
    uint32_t magic = ARPT_MAGIC;
    uint32_t version = kCurrentFormatVersion;
    uint32_t sample_rate = 0;
    uint32_t block_size = 0;
    uint32_t blocks_count = 0;
    uint32_t in_channels = 0;
    uint32_t out_channels = 0;
    uint8_t bit_depth = 0;
    uint8_t flags = 0;
    uint8_t reserved1[2] = {};
    uint64_t total_samples = 0;
});
static_assert(sizeof(FileHeader) == 40);


PACK(struct BlockHeader
{
    uint32_t block_type = 0;
    uint32_t data_size = 0;
    uint32_t block_id = 0;
    uint32_t checksum = 0;
});
static_assert(sizeof(BlockHeader) == 16);


PACK(struct FrameInfo
{
    uint32_t num_samples = 0;
    uint32_t reserved = 0;
    int64_t start_sample = 0;
});
static_assert(sizeof(FrameInfo) == 16);

PACK(struct StateHeader
{
    uint32_t plugin_id = 0;
    uint32_t reserved[3] = {};
});
static_assert(sizeof(StateHeader) == 16);


struct PluginState
{
    uint32_t plugin_id = 0;
    std::vector<uint8_t> data;

    bool isValid() const { return !data.empty(); }
};

inline bool fileUsesFrames(const FileHeader& header) noexcept
{
    return (header.version >= ERptFileVersion::WithFrames);
}

inline bool fileHasPluginState(const FileHeader& header) noexcept
{
    return header.version >= ERptFileVersion::WithFrames
        && (header.flags & FileFlag_HasState) != 0;
}

inline bool readFrameInfo(const uint8_t* data, size_t size, FrameInfo& out_info) noexcept
{
    if (data == nullptr || size < sizeof(FrameInfo))
        return false;

    std::memcpy(&out_info, data, sizeof(FrameInfo));
    return true;
}


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
    virtual uint64_t getTotalSamples() const = 0;
    virtual bool readPluginState(PluginState& out_state) { (void)out_state; return false; }
};

} // namespace AudioDebug
