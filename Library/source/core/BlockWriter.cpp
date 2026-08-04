// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/BlockWriter.h"

#include "core/FileWriter.h"

#include <assert.h>

namespace AudioDebug {

    BlockWriter::BlockWriter() = default;

BlockWriter::BlockWriter(uint8_t* buffer, size_t capacity, uint8_t bitDepth) noexcept
    : m_owner(nullptr), m_data(buffer), m_capacity(capacity), m_bitDepth(bitDepth)
{
}

BlockWriter::~BlockWriter()
{
    if (m_owner != nullptr)
    {
        FileWriter* owner = m_owner;
        clear();
        owner->abandonBlock();
    }
}

BlockWriter& BlockWriter::operator=(BlockWriter&& other) noexcept
{
    if (this != &other)
    {
        if (m_owner != nullptr)
        {
            FileWriter* owner = m_owner;
            clear();
            owner->abandonBlock();
        }
        moveFrom(other);
    }
    return *this;
}

bool BlockWriter::append(const void* data, size_t numBytes) noexcept
{
    if (uint8_t* dst = reserve(numBytes))
    {
        std::memcpy(dst, data, numBytes);
        return true;
    }
    assert(false);
    return false;
}

void BlockWriter::moveFrom(BlockWriter& other) noexcept
{
    m_owner = other.m_owner;
    m_data = other.m_data;
    m_capacity = other.m_capacity;
    m_cursor = other.m_cursor;
    m_bitDepth = other.m_bitDepth;
    m_overflow = other.m_overflow;
    other.clear();
}

void BlockWriter::clear() noexcept
{
    m_owner = nullptr;
    m_data = nullptr;
    m_capacity = 0;
    m_cursor = 0;
    m_bitDepth = 0;
    m_overflow = false;
}

bool BlockWriter::commit() noexcept
{
    if (m_owner == nullptr)
        return false;

    FileWriter* owner = m_owner;
    const bool   ok = !m_overflow;
    const size_t bytes = m_cursor;
    clear();

    return owner->finishBlock(ok, bytes);
}

} // namespace AudioDebug
