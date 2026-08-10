// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DemoProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Demo {

namespace {

constexpr int kMaxMidiEventsPerBlock = 64;

int noteName(int noteNumber, char* out, size_t outSize)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    const int octave = noteNumber / 12 - 1;
    const int pitch  = noteNumber % 12;

    return std::snprintf(out, outSize, "%s%d", names[pitch], octave);
}

} // namespace

DemoProcessor::DemoProcessor(int numInputChannels, int numOutputChannels)
    : m_numInputChannels(numInputChannels)
    , m_numOutputChannels(numOutputChannels)
{
    m_regenerateMidi.reserve(kMaxMidiEventsPerBlock);
}

DemoProcessor::~DemoProcessor() = default;

void DemoProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    m_sampleRate      = sampleRate;
    m_samplesPerBlock = samplesPerBlock;

    if (!isRegenerating())
    {
        startDebugCapture(sampleRate,
            samplesPerBlock,
            m_numInputChannels,
            m_numOutputChannels + kNumDebugChannels,
            AudioDebug::EBitDepth::Int32);
    }

    const int numChannels = std::max(m_numInputChannels, m_numOutputChannels);
    m_dsp.prepare(sampleRate, samplesPerBlock, numChannels);

    m_blockIndex = 0;
}

void DemoProcessor::releaseResources()
{
    m_dsp.reset();
}

void DemoProcessor::processBlock(int numSamples, float* const* buffers,  const std::vector<MidiEvent>& midiEvents)
{
    m_currentNumSamples = numSamples;
    m_currentBuffers    = buffers;

    AudioDebug::ScopedDebugBlock debugBlock(*this, numSamples);
    if (debugBlock)
    {
        debugBlock.writeInput(numSamples, m_numInputChannels, buffers);

        for (const auto& e : midiEvents)
        {
            if (e.numBytes <= 0 || e.numBytes > 3)
                continue;

            debugBlock.pushMidiEvent(e.samplePosition, e.numBytes, e.data);
        }
    }

    const int numChannels = std::max(m_numInputChannels, m_numOutputChannels);
    m_dsp.process(numSamples, numChannels, buffers, midiEvents);

    if (debugBlock)
    {
        const float* chans[4] = { buffers[0], buffers[1],  m_dsp.getLfoTrace(), m_dsp.getCutoffTrace() };
        debugBlock.writeOutput(numSamples, 4, chans);
    }

    ++m_blockIndex;
}

void DemoProcessor::prepareToPlayHook(double sampleRate, int samplesPerBlock)
{
    prepareToPlay(sampleRate, samplesPerBlock);
}

void DemoProcessor::releaseResourcesHook()
{
    releaseResources();
}

void DemoProcessor::processBlockHook(int numSamples,
                                     float** inputs,
                                     const std::vector<AudioDebug::RawMidiMessage>& midiMessages)
{
    m_regenerateMidi.clear();

    for (const auto& msg : midiMessages)
    {
        MidiEvent e;
        e.samplePosition = static_cast<int>(msg.position);
        e.numBytes = std::min<int>(msg.dataSize, 3);
        std::memcpy(e.data, msg.data, static_cast<size_t>(e.numBytes));

        m_regenerateMidi.push_back(e);
    }

    processBlock(numSamples, inputs, m_regenerateMidi);
}

} // namespace Demo
