// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>

#include "core/BlockWriter.h"

namespace AudioDebug {

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

using SampleAccFunc = std::function<void(SampleWriter&)>;

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

} // namespace Metadata

} // namespace AudioDebug
