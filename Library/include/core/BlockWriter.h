// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <iostream>
#include <chrono>

#include "core/SampleWriter.h"

namespace AudioDebug {

enum class BlockType : uint32_t;

namespace FileWriterConfig
{
    constexpr size_t queueCapacity = 32;
    constexpr size_t maxBlockBytes = 128 * 1024 * 2; // Note: this is high on purpose because custom data can be very big
    constexpr std::chrono::milliseconds flushInterval{ 250 };
}

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

    uint8_t* reserve(size_t numBytes) noexcept
    {
        if (m_data == nullptr)
            return nullptr;

        if (m_overflow || numBytes > m_capacity - m_cursor)
        {
            m_overflow = true;
            return nullptr;
        }

        uint8_t* dst = m_data + m_cursor;
        m_cursor += numBytes;
        return dst;
    }

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

class BlockAccumulator
{
private:
public:
    BlockAccumulator(BlockType type) noexcept
        : m_type(type)
    {
        std::memset(m_buffer, 0, FileWriterConfig::maxBlockBytes);
    }

    BlockWriter& get() noexcept
    {
        if (!m_open)
        {
            m_writer = BlockWriter(m_buffer, FileWriterConfig::maxBlockBytes);
            m_open = true;
        }
        return m_writer;
    }

    bool isOpen()  const noexcept { return m_open; }
    BlockType type() const noexcept { return m_type; }
    const uint8_t* buffer() const noexcept { return m_buffer; }

    size_t close(bool& out_overflowed) noexcept
    {
        if (!m_open)
            return 0;

        m_open = false;
        out_overflowed = m_writer.overflowed();
        return out_overflowed ? 0 : m_writer.bytesWritten();
    }

private:
    const BlockType m_type;
    BlockWriter m_writer;
    uint8_t m_buffer[FileWriterConfig::maxBlockBytes];
    bool m_open = false;
};


} // namespace AudioDebug
