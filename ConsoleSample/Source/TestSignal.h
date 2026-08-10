// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "DemoDsp.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace Demo {

class TestSignal
{
public:
    TestSignal(double sampleRate, int numChannels)
        : m_sampleRate(sampleRate)
        , m_numChannels(numChannels)
    {
    }

    static constexpr double kFirstBurstTime = 1.4;
    static constexpr double kBurstPeriod    = 3.0;
    static constexpr double kBurstLength    = 0.08;

    void reset()
    {
        m_samplePos  = 0;
        m_sweepPhase = 0.0;
        m_rngState   = 0x13579BDFu;
    }

    void fill(int numSamples, float* const* buffers)
    {
        constexpr float kTwoPi = 6.28318530717958647692f;

        for (int i = 0; i < numSamples; ++i)
        {
            const double t = static_cast<double>(m_samplePos) / m_sampleRate;

            const double sweepT = std::fmod(t, 6.0);
            const double freq   = 80.0 * std::pow(3000.0 / 80.0, sweepT / 6.0);

            m_sweepPhase += freq / m_sampleRate;
            if (m_sweepPhase >= 1.0)
                m_sweepPhase -= 1.0;

            const float sweep = static_cast<float>(0.35 * std::sin(kTwoPi * m_sweepPhase));

            const bool inBurst = t >= kFirstBurstTime  && std::fmod(t - kFirstBurstTime, kBurstPeriod) < kBurstLength;

            const float noise = inBurst ? 0.25f * nextNoise() : 0.0f;

            const float mono = sweep + noise;

            for (int ch = 0; ch < m_numChannels; ++ch)
            {
                const float channelGain = (ch == 0) ? 1.0f : 0.8f;
                buffers[ch][i] = mono * channelGain;
            }

            ++m_samplePos;
        }
    }

    static std::vector<MidiEvent> makeMidiSequence(double sampleRate, int totalSamples)
    {
        std::vector<MidiEvent> events;

        const int   notes[]   = { 48, 55, 60, 67, 72, 60, 52, 64 };
        const int   numNotes  = static_cast<int>(sizeof(notes) / sizeof(notes[0]));
        const double noteLenS = 0.75;

        int noteIndex = 0;
        for (double t = 0.25; t < static_cast<double>(totalSamples) / sampleRate - noteLenS; t += 1.0)
        {
            const int note     = notes[noteIndex % numNotes];
            const int velocity = 64 + (noteIndex % 4) * 20;   // 64, 84, 104, 124

            const int onPos  = static_cast<int>(t * sampleRate);
            const int offPos = static_cast<int>((t + noteLenS) * sampleRate);

            events.push_back(makeEvent(onPos,  0x90, note, velocity));
            events.push_back(makeEvent(offPos, 0x80, note, 0));

            const int ccPos   = static_cast<int>((t + noteLenS * 0.5) * sampleRate);
            const int ccValue = 20 + (noteIndex % 5) * 25;
            events.push_back(makeEvent(ccPos, 0xB0, 1, ccValue));

            ++noteIndex;
        }

        return events;
    }

private:
    static MidiEvent makeEvent(int pos, uint8_t status, int d1, int d2)
    {
        MidiEvent e;
        e.samplePosition = pos;
        e.numBytes       = 3;
        e.data[0]        = status;   // channel 1
        e.data[1]        = static_cast<uint8_t>(d1);
        e.data[2]        = static_cast<uint8_t>(d2);
        return e;
    }

    float nextNoise()
    {
        m_rngState = m_rngState * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(m_rngState)) / 2147483648.0f;
    }

    double m_sampleRate;
    int m_numChannels;
    int64_t m_samplePos = 0;
    double m_sweepPhase = 0.0;
    uint32_t m_rngState = 0x13579BDFu;
};

} // namespace Demo
