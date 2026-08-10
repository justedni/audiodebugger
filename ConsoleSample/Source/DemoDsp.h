// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <vector>

namespace Demo {

struct MidiEvent
{
    int     samplePosition = 0;
    int     numBytes       = 0;
    uint8_t data[3]        = { 0, 0, 0 };
};


class TremoloFilter
{
public:
    TremoloFilter() = default;

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void process(int numSamples, int numChannels, float* const* buffers,  const std::vector<MidiEvent>& midiEvents);

    void setDepthScale(float scale) noexcept { m_depthScale = scale; }
    void setCutoffScale(float scale) noexcept { m_cutoffScale = scale; }
    void setTremoloBypassed(bool b) noexcept { m_tremoloBypassed = b; }

    const float* getLfoTrace() const noexcept { return m_lfoTrace.data(); }
    const float* getCutoffTrace() const noexcept { return m_cutoffTrace.data(); }

    float getCutoffHz() const noexcept { return m_cutoffHz; }
    int getHeldNote() const noexcept { return m_heldNote; }
    float getLfoRateHz() const noexcept { return m_lfoRateHz; }

private:
    void handleMidiEvent(const MidiEvent& e);
    void setCutoffFromNote(int noteNumber);

    double m_sampleRate = 48000.0;

    std::vector<float> m_lpState;
    float m_cutoffHz        = 2000.0f;
    float m_targetCutoffHz  = 2000.0f;
    float m_cutoffSmoothing = 0.0f;

    float m_lfoPhase     = 0.0f;
    float m_lfoRateHz    = 4.0f;
    float m_lfoDepthMidi = 0.6f;

    float m_depthScale = 1.0f;
    float m_cutoffScale = 1.0f;
    bool m_tremoloBypassed = false;

    int m_heldNote = -1;

    std::vector<float> m_lfoTrace;
    std::vector<float> m_cutoffTrace;
};

} // namespace Demo
