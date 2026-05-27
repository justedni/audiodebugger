// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DiffDetector.h"

#include "core/AudioDebug.h"

namespace AudioDebug {

void DiffDetector::startDetection(int numChannels)
{
    juce::ScopedLock lock(m_lock);
    m_differences.clear();

    m_states.clear();
    m_states.resize(numChannels);
}

void DiffDetector::detectDifferences(const std::vector<float>& leftSamples, const std::vector<float>& rightSamples, int ch, uint8_t bitDepth)
{
    jassert(leftSamples.size() == rightSamples.size());

    jassert(ch < m_states.size());
    auto& state = m_states[ch];

    const int samplesToRead = static_cast<int>(leftSamples.size());
    for (int i = 0; i < samplesToRead; ++i)
    {
        bool hasDifference = false;
        int32_t maxDiffAtSample = 0;

        const int32_t leftSample = AudioDebugger::floatToInt(leftSamples[i], bitDepth);
        const int32_t rightSample = AudioDebugger::floatToInt(rightSamples[i], bitDepth);
        const int32_t diff = diffValues(leftSample, rightSample);

        if (diff > m_threshold) {
            hasDifference = true;
            maxDiffAtSample = diff;
        }

        int64_t currentSample = state.samplesProcessed + i;

        if (hasDifference) {
            if (!state.inRange) {
                state.inRange = true;
                state.rangeStart = currentSample;
                state.rangeMaxDiff = maxDiffAtSample;
            }
            else {
                state.rangeMaxDiff = std::max(state.rangeMaxDiff, maxDiffAtSample);
            }
        }
        else {
            if (state.inRange) {
                juce::ScopedLock lock(m_lock);
                m_differences.emplace_back(ch, state.rangeStart, currentSample - 1, state.rangeMaxDiff);
                state.inRange = false;
            }
        }
    }

    state.samplesProcessed += samplesToRead;

    if (state.inRange) {
        int64_t lastSample = state.samplesProcessed - 1;
        m_differences.emplace_back(ch, state.rangeStart, lastSample, state.rangeMaxDiff);
        state.inRange = false;
    }
}

int32_t DiffDetector::diffValues(int32_t a, int32_t b) const
{
    const int32_t maskedA = static_cast<int32_t>(static_cast<uint32_t>(a) & m_mask);
    const int32_t maskedB = static_cast<int32_t>(static_cast<uint32_t>(b) & m_mask);
    return std::abs(maskedA - maskedB);
}

void DiffDetector::setMaskPreset(MaskPreset preset)
{
    m_preset = preset;
    m_mask = maskForPreset(preset);
}

uint32_t DiffDetector::maskForPreset(MaskPreset preset)
{
    switch (preset)
    {
    case MaskPreset::Full:      return 0xFFFFFFFFu;
    case MaskPreset::Bits16:    return 0xFFFFFF00u;
    }
    return 0xFFFFFFFFu;
}

std::vector<DifferenceRange> DiffDetector::getInRange(int64_t startSample, int64_t endSample) const
{
    juce::ScopedLock lock(m_lock);

    std::vector<DifferenceRange> result;

    for (const auto& diff : m_differences) {
        if (diff.overlaps(startSample, endSample)) {
            result.push_back(diff);
        }
    }

    return result;
}

} // namespace AudioDebug
