// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/DiffDetector.h"

#include "core/AudioDebugger.h"

#include <assert.h>

namespace AudioDebug {

void DiffDetector::startDetection(int numChannels)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_differences.clear();

    m_states.clear();
    m_states.resize(numChannels);
}

void DiffDetector::detectDifferences(const std::vector<float>& leftSamples, const std::vector<float>& rightSamples, int ch, uint8_t bitDepth, std::function<int32_t(float, uint8_t)> funcGetVal)
{
    assert(leftSamples.size() == rightSamples.size());

    assert(ch < m_states.size());
    auto& state = m_states[ch];

    const int samplesToRead = static_cast<int>(leftSamples.size());
    for (int i = 0; i < samplesToRead; ++i)
    {
        bool hasDifference = false;
        int32_t maxDiffAtSample = 0;

        const int32_t leftSample = funcGetVal(leftSamples[i], bitDepth);
        const int32_t rightSample = funcGetVal(rightSamples[i], bitDepth);
        const int32_t diff = std::abs(leftSample - rightSample);

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
                std::lock_guard<std::mutex> lock(m_mutex);
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

std::vector<DifferenceRange> DiffDetector::getInRange(int64_t startSample, int64_t endSample) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<DifferenceRange> result;

    for (const auto& diff : m_differences) {
        if (diff.overlaps(startSample, endSample)) {
            result.push_back(diff);
        }
    }

    return result;
}

} // namespace AudioDebug
