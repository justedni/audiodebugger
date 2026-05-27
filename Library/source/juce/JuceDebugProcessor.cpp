// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "juce/JuceDebugProcessor.h"

#include "core/AudioDebug.h"

namespace AudioDebug {

JuceDebugProcessor::JuceDebugProcessor(const juce::AudioProcessor::BusesProperties& ioLayouts)
    : juce::AudioProcessor (ioLayouts)
{
}

JuceDebugProcessor::~JuceDebugProcessor()
{
}

void JuceDebugProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    AudioDebug::AudioDebugger::get()->initDebugProcessor(
        std::round(sampleRate),
        samplesPerBlock,
        getTotalNumInputChannels(),
        getTotalNumOutputChannels(),
        AudioDebug::EBitDepth::Int32);
}

void JuceDebugProcessor::prepareToPlayHook(double sampleRate, int samplesPerBlock)
{
    prepareToPlay(sampleRate, samplesPerBlock);
}
void JuceDebugProcessor::processBlockHook(int numSamples, float** inputs, const std::vector<RawMidiMessage>& midiMessages)
{
    const int numInputs = AudioDebug::AudioDebugger::get()->getNumInputs();
    juce::AudioBuffer<float> tempBuffer(inputs, numInputs, numSamples);

    juce::MidiBuffer juceMidiMessages;
    for (auto& msg : midiMessages)
    {
        juceMidiMessages.addEvent(msg.data, static_cast<int>(msg.dataSize), msg.position);
    }

    processBlockRegenerate(tempBuffer, juceMidiMessages);
}

} // namespace AudioDebug
