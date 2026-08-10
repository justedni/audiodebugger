// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "DemoDsp.h"

#include "core/AudioDebug.h"
#include "core/AudioDebugInterface.h"

#include <atomic>
#include <vector>

namespace Demo {

class DemoProcessor : public AudioDebug::DebugProcessor
{
public:
    enum SliderId : uint8_t
    {
        TremoloDepthScale = 0,   // 0 .. 2, default 1
        CutoffScale       = 1,   // 0.25 .. 4, default 1
        NumSliders
    };

    enum ButtonId : uint8_t
    {
        BypassTremolo = 0,
        NumButtons
    };

    static constexpr int kNumDebugChannels = 2;

    DemoProcessor(int numInputChannels, int numOutputChannels);
    ~DemoProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();

    void processBlock(int numSamples, float* const* buffers, const std::vector<MidiEvent>& midiEvents);

    int getNumInputChannels()  const noexcept { return m_numInputChannels; }
    int getNumOutputChannels() const noexcept { return m_numOutputChannels; }

private:
    void prepareToPlayHook(double sampleRate, int samplesPerBlock) override;
    void releaseResourcesHook() override;
    void processBlockHook(int numSamples, float** inputs, const std::vector<AudioDebug::RawMidiMessage>& midiMessages) override;

    TremoloFilter m_dsp;

    const int m_numInputChannels;
    const int m_numOutputChannels;

    double m_sampleRate = 48000.0;
    int m_samplesPerBlock = 0;
    int m_blockIndex = 0;

    int m_currentNumSamples = 0;
    float* const* m_currentBuffers    = nullptr;

    std::atomic<float> m_depthScale{ 1.0f };
    std::atomic<float> m_cutoffScale{ 1.0f };

    std::vector<MidiEvent> m_regenerateMidi;
};

} // namespace Demo
