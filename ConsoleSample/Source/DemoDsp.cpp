// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DemoDsp.h"

#include <algorithm>
#include <cmath>

namespace Demo {

namespace {
constexpr float kPi        = 3.14159265358979323846f;
constexpr float kTwoPi     = 2.0f * kPi;
constexpr float kMinCutoff = 120.0f;
constexpr float kMaxCutoff = 12000.0f;
}

void TremoloFilter::prepare(double sampleRate, int maxBlockSize, int numChannels)
{
    m_sampleRate = sampleRate;

    m_lpState.assign(static_cast<size_t>(std::max(numChannels, 1)), 0.0f);

    m_lfoTrace.assign(static_cast<size_t>(std::max(maxBlockSize, 1)), 0.0f);
    m_cutoffTrace.assign(static_cast<size_t>(std::max(maxBlockSize, 1)), 0.0f);

    const float tau = 0.020f;
    m_cutoffSmoothing = 1.0f - std::exp(-1.0f / (tau * static_cast<float>(sampleRate)));

    reset();
}

void TremoloFilter::reset()
{
    std::fill(m_lpState.begin(), m_lpState.end(), 0.0f);
    m_lfoPhase = 0.0f;
    m_heldNote = -1;
    m_cutoffHz = 2000.0f;
    m_targetCutoffHz = 2000.0f;
}

void TremoloFilter::setCutoffFromNote(int noteNumber)
{
    const float hz = 440.0f * std::pow(2.0f, (static_cast<float>(noteNumber) - 69.0f) / 12.0f);
    m_targetCutoffHz = std::clamp(hz * 4.0f, kMinCutoff, kMaxCutoff);
}

void TremoloFilter::handleMidiEvent(const MidiEvent& e)
{
    if (e.numBytes < 2)
        return;

    const uint8_t status = static_cast<uint8_t>(e.data[0] & 0xF0);
    const int data1 = static_cast<int>(e.data[1]);
    const int data2 = e.numBytes >= 3 ? static_cast<int>(e.data[2]) : 0;

    switch (status)
    {
    case 0x90: // NoteOn
    {
        if (data2 > 0)
        {
            m_heldNote = data1;
            setCutoffFromNote(data1);

            m_lfoRateHz = 1.0f + (static_cast<float>(data2) / 127.0f) * 11.0f;
            break;
        }
    }
    case 0x80: // NoteOff
    {
        if (data1 == m_heldNote)
            m_heldNote = -1;
        break;
    }
    case 0xB0: // CC 1 -> tremolo depth
    {
        if (data1 == 1)
            m_lfoDepthMidi = static_cast<float>(data2) / 127.0f;
        break;
    }
    default:
        break;
    }
}

void TremoloFilter::process(int numSamples, int numChannels, float* const* buffers, const std::vector<MidiEvent>& midiEvents)
{
    const float phaseInc = m_lfoRateHz / static_cast<float>(m_sampleRate);
    const float depth = m_tremoloBypassed ? 0.0f : std::clamp(m_lfoDepthMidi * m_depthScale, 0.0f, 1.0f);

    size_t nextEvent = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        while (nextEvent < midiEvents.size()
               && midiEvents[nextEvent].samplePosition <= i)
        {
            handleMidiEvent(midiEvents[nextEvent]);
            ++nextEvent;
        }

        m_cutoffHz += (m_targetCutoffHz - m_cutoffHz) * m_cutoffSmoothing;

        const float effectiveCutoff = std::clamp(m_cutoffHz * m_cutoffScale, kMinCutoff, kMaxCutoff);

        const float g = 1.0f - std::exp(-kTwoPi * effectiveCutoff / static_cast<float>(m_sampleRate));

        const float lfo  = 0.5f * (1.0f + std::sin(kTwoPi * m_lfoPhase));
        const float gain = 1.0f - depth * (1.0f - lfo);

        m_lfoPhase += phaseInc;
        if (m_lfoPhase >= 1.0f)
            m_lfoPhase -= 1.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float& state = m_lpState[static_cast<size_t>(ch)];
            state += g * (buffers[ch][i] - state);
            buffers[ch][i] = state * gain;
        }

        m_lfoTrace[static_cast<size_t>(i)] = gain;

        m_cutoffTrace[static_cast<size_t>(i)] = (effectiveCutoff - kMinCutoff) / (kMaxCutoff - kMinCutoff);
    }

    for (; nextEvent < midiEvents.size(); ++nextEvent)
        handleMidiEvent(midiEvents[nextEvent]);
}

} // namespace Demo
