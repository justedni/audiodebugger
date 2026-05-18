#include "DiffDetector.h"

#include "core/AudioDebug.h"

namespace AudioDebug {

void DiffDetector::clear()
{
    juce::ScopedLock lock(m_lock);
    m_differences.clear();

    m_inRange[0] = m_inRange[1] = false;
    m_rangeStart[0] = m_rangeStart[1] = 0;
    m_rangeMaxDiff[0] = m_rangeMaxDiff[1] = 0;
    m_samplesProcessed[0] = m_samplesProcessed[1] = 0;
}

void DiffDetector::detectDifferences(const std::vector<float>& outSamples, const std::vector<float>& refSamples, int ch, uint8_t bitDepth)
{
    jassert(outSamples.size() == refSamples.size());

    const int samplesToRead = static_cast<int>(outSamples.size());
    for (int i = 0; i < samplesToRead; ++i)
    {
        bool hasDifference = false;
        int32_t maxDiffAtSample = 0;

        const int32_t mainSample = AudioDebugger::floatToInt(outSamples[i], bitDepth);
        const int32_t refSample = AudioDebugger::floatToInt(refSamples[i], bitDepth);
        const int32_t diff = diffValues(mainSample, refSample);

        if (diff > m_threshold) {
            hasDifference = true;
            maxDiffAtSample = diff;
        }

        int64_t currentSample = m_samplesProcessed[ch] + i;

        if (hasDifference) {
            if (!m_inRange[ch]) {
                m_inRange[ch] = true;
                m_rangeStart[ch] = currentSample;
                m_rangeMaxDiff[ch] = maxDiffAtSample;
            }
            else {
                m_rangeMaxDiff[ch] = std::max(m_rangeMaxDiff[ch], maxDiffAtSample);
            }
        }
        else {
            if (m_inRange[ch]) {
                juce::ScopedLock lock(m_lock);
                m_differences.emplace_back(ch, m_rangeStart[ch], currentSample - 1, m_rangeMaxDiff[ch]);
                m_inRange[ch] = false;
            }
        }
    }

    m_samplesProcessed[ch] += samplesToRead;

    if (m_inRange[ch]) {
        int64_t lastSample = m_samplesProcessed[ch] - 1;
        m_differences.emplace_back(ch, m_rangeStart[ch], lastSample, m_rangeMaxDiff[ch]);
        m_inRange[ch] = false;
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
