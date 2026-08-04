// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace AudioDebug {

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

} // namespace AudioDebug
