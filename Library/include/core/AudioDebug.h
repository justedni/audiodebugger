// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <mutex>
#include <functional>
#include <variant>
#include <assert.h>

#ifndef ensure
#ifdef _DEBUG
#define ensure(cond) if (!(cond)) { __debugbreak(); }
#else
#define ensure(cond) assert(cond)
#endif
#endif

namespace AudioDebug {

class AudioDebugger;
class DebugProcessor;


enum EMarkerColor : uint8_t
{
    Black = 0,
    PlainGrey,
    DottedGrey,
    Red,
    Blue,
    Green
};

enum EMarkerPaintType : uint8_t
{
    FullLine = 0,
    NoLine
};

enum EBitDepth : uint8_t
{
    Int24,
    Int32,
    //Int8,  // TODO
    //Int16, // TODO
    Count
};

struct RawMidiMessage
{
    uint32_t position = 0;
    uint8_t dataSize = 0;
    uint8_t data[3] = { 0, 0, 0 };
};

class SampleWriter
{
public:
    SampleWriter(uint8_t* dest, size_t maxBytes, uint8_t bitDepth) noexcept;

    void push(int32_t sample) noexcept;
    void push(float sample) noexcept;

    size_t bytesWritten() const noexcept { return m_pos; }

private:
    uint8_t* m_dest;
    size_t m_pos;
    size_t m_maxBytes;
    uint8_t m_bitDepth;
};

using SampleAccFunc = std::function<void(SampleWriter&)>;


class BlockWriter
{
public:
    BlockWriter();
    ~BlockWriter();

    BlockWriter(uint8_t* buffer, size_t capacity, uint8_t bitDepth = 0) noexcept;

    BlockWriter(BlockWriter&& other) noexcept { moveFrom(other); }
    BlockWriter& operator=(BlockWriter&& other) noexcept;

    BlockWriter(const BlockWriter&) = delete;
    BlockWriter& operator=(const BlockWriter&) = delete;

    explicit operator bool() const noexcept { return m_data != nullptr; }

    size_t bytesWritten() const noexcept { return m_cursor; }
    size_t capacity()     const noexcept { return m_capacity; }
    size_t remaining()    const noexcept { return m_capacity - m_cursor; }

    bool overflowed() const noexcept { return m_overflow; }

    bool append(const void* data, size_t numBytes) noexcept;

    template <class T>
    bool append(const T& value) noexcept
    {
        static_assert(std::is_trivially_copyable<T>::value, "append<T> requires a trivially copyable type");
        return append(&value, sizeof(T));
    }

    uint8_t* reserve(size_t numBytes) noexcept;

    template <class Fn>
    bool writeSamples(const Fn& f) noexcept
    {
        if (m_data == nullptr || m_overflow)
            return false;

        SampleWriter writer(m_data + m_cursor, m_capacity - m_cursor, m_bitDepth);
        f(writer);
        m_cursor += writer.bytesWritten();
        return true;
    }

    bool commit() noexcept;

private:
    friend class FileWriter;

    BlockWriter(FileWriter* owner, uint8_t* data, size_t capacity, uint8_t bitDepth) noexcept
        : m_owner(owner), m_data(data), m_capacity(capacity), m_bitDepth(bitDepth) {}

    void moveFrom(BlockWriter& other) noexcept;
    void clear() noexcept;

    FileWriter* m_owner = nullptr;
    uint8_t* m_data  = nullptr;
    size_t  m_capacity = 0;
    size_t m_cursor = 0;
    uint8_t m_bitDepth = 0;
    bool m_overflow = false;
};


class ScopedDebugBlock
{
public:
    ScopedDebugBlock(DebugProcessor& processor, uint32_t numSamples);
    ~ScopedDebugBlock();

    ScopedDebugBlock(const ScopedDebugBlock&) = delete;
    ScopedDebugBlock& operator=(const ScopedDebugBlock&) = delete;
    ScopedDebugBlock(ScopedDebugBlock&&) = delete;
    ScopedDebugBlock& operator=(ScopedDebugBlock&&) = delete;

    explicit operator bool() const noexcept { return m_bActive; }

    void writeInput(int numSamples, int numChannels, const float* const* buffer);
    void writeInput(int numSamples, const SampleAccFunc& f);
    void writeOutput(int numSamples, int numChannels, const float* const* buffer);
    void writeOutput(int numSamples, const SampleAccFunc& f);

    void pushMidiEvent(int position, int dataSize, const uint8_t* data);

    BlockWriter& metadata() noexcept;
    BlockWriter& custom() noexcept;

    int32_t getCurrentBufferTimestamp() const;
    const AudioDebugger& getDebugger() const { return m_debugger; }

private:
    AudioDebugger& m_debugger;
    bool m_bActive = false;
};


struct Marker
{
    int32_t magic;
    int64_t samplePosition;
    uint8_t chanId;
    uint8_t paintType;
    uint8_t colorId;

    std::variant<std::string, std::vector<uint8_t>> data;

    Marker()
        : magic(0)
        , samplePosition(0)
        , chanId(0)
        , colorId(0)
    {}

    Marker(int32_t mag, int64_t pos, std::string&& t, uint8_t cid, uint8_t ptype, uint8_t col)
        : magic(mag), samplePosition(pos), chanId(cid), paintType(ptype), data(std::move(t)), colorId(col)
    {}

    Marker(int32_t mag, int64_t pos, std::vector<uint8_t>&& d, uint8_t cid, uint8_t ptype, uint8_t col)
        : magic(mag), samplePosition(pos), chanId(cid), paintType(ptype), data(std::move(d)), colorId(col)
    {}

    bool operator<(const Marker& other) const {
        if (samplePosition != other.samplePosition) {
            return samplePosition < other.samplePosition;
        }
        return chanId < other.chanId;
    }

    const std::string& getText() const { return std::get<std::string>(data); }
    const std::vector<uint8_t>& getBinary() const { return std::get<std::vector<uint8_t>>(data); }
};

struct MarkerPair
{
    MarkerPair(Marker&& m)
        : main(std::move(m))
    {
    }

    Marker main;
    bool bHasRegen = false;
    Marker regen;
};

namespace Metadata {

    enum EMetadataType : uint32_t
    {
        INVALID = 0,
        MARKER_BUFFER_START = 0x54534b4d,   // "MKST"
        MARKER_TEXT = 0x524b524d,           // "MRKR"
        MARKER_CUSTOM = 0x434b524d,         // "MRKC"
    };

    constexpr bool isValidMarker(int magic)
    {
        return (magic == MARKER_BUFFER_START)
            || (magic == MARKER_TEXT)
            || (magic == MARKER_CUSTOM);
    }

    uint32_t add_uint8(BlockWriter& block, uint8_t num);
    uint32_t add_int8(BlockWriter& block, int8_t num);
    uint32_t add_uint16(BlockWriter& block, uint16_t num);
    uint32_t add_int16(BlockWriter& block, int16_t num);
    uint32_t add_uint32(BlockWriter& block, uint32_t num);
    uint32_t add_uint64(BlockWriter& block, uint64_t num);
    uint32_t add_int32(BlockWriter& block, int32_t num);
    uint32_t add_float(BlockWriter& block, float num);
    uint32_t add_double(BlockWriter& block, double num);
    uint32_t add_string(BlockWriter& block, const char* text, size_t textSize);

    void addMarkerText(BlockWriter& block, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color, const char* text, size_t textSize);
    void addMarkerCustom(BlockWriter& block, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color, const std::vector<uint8_t>& data);
    void addMarkerCustomHeaderOnly(BlockWriter& block, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color);
    void addBufferStartMarker(BlockWriter& block, int timestamp);
    void addMarkerRegular(BlockWriter& block, int magic, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color, const char* text, size_t textSize);
    void addMidiEvent(BlockWriter& block, int timestamp, int dataSize, const uint8_t* data);


    uint32_t read_uint8(const uint8_t* data, uint8_t& out_num);
    uint32_t read_int8(const uint8_t* data, int8_t& out_num);
    uint32_t read_uint16(const uint8_t* data, uint16_t& out_num);
    uint32_t read_int16(const uint8_t* data, int16_t& out_num);
    uint32_t read_uint32(const uint8_t* data, uint32_t& out_num);
    uint32_t read_uint64(const uint8_t* data, uint64_t& out_num);
    uint32_t read_int32(const uint8_t* data, int32_t& out_num);
    uint32_t read_float(const uint8_t* data, float& out_num);
    uint32_t read_double(const uint8_t* data, double& out_num);

    Marker readMarker(const uint8_t* data, uint32_t& out_readSize);
    std::vector<RawMidiMessage> readAllMidiEvents(const std::vector<uint8_t>& data);

} // namespace Metadata



} // namespace AudioDebug
