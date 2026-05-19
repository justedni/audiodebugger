// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace AudioDebug {

struct DifferenceRange
{
    uint8_t channel;
    int64_t startSample;
    int64_t endSample;
    float maxDifference;

    DifferenceRange() : channel(0), startSample(0), endSample(0), maxDifference(0.0f) {}
    DifferenceRange(uint8_t chan, int64_t start, int64_t end, float maxDiff)
        : channel(chan), startSample(start), endSample(end), maxDifference(maxDiff) {}

    bool operator<(const DifferenceRange& other) const {
        return startSample < other.startSample;
    }

    bool overlaps(int64_t viewStart, int64_t viewEnd) const {
        return !(endSample < viewStart || startSample > viewEnd);
    }
};

class DiffDetector
{
public:
    enum MaskPreset : uint8_t
    {
        Full,
        Bits16,
        Count
    };

    void clear();
    const std::vector<DifferenceRange>& getAll() const { return m_differences; }
    std::vector<DifferenceRange> getInRange(int64_t startSample, int64_t endSample) const;

    void detectDifferences(const std::vector<float>& outSamples, const std::vector<float>& refSamples, int ch, uint8_t bitDepth);

    void setMaskPreset(MaskPreset preset);
    MaskPreset getMaskPreset() const { return m_preset; }

    void setThreshold(int threshold) { m_threshold = threshold; }
    int getThreshold() const { return m_threshold; }

private:
    int32_t diffValues(int32_t a, int32_t b) const;

    uint32_t maskForPreset(MaskPreset preset);

    std::vector<DifferenceRange> m_differences;
    mutable juce::CriticalSection m_lock;

    bool m_inRange[2] = { false, false };
    int64_t m_rangeStart[2] = { 0, 0 };
    int32_t m_rangeMaxDiff[2] = { 0, 0 };
    int64_t m_samplesProcessed[2] = { 0, 0 };

    int m_threshold = 0;

    uint32_t m_mask = 0xFFFFFFFFu;
    MaskPreset m_preset = MaskPreset::Full;
};

} // namespace AudioDebug
