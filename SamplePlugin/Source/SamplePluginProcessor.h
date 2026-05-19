// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "juce/JuceDebugProcessor.h"

namespace AudioDebug {
struct RawMidiMessage;
}

class SamplePluginEditor;

class SamplePluginProcessor : public AudioDebug::JuceDebugProcessor
{
public:
    SamplePluginProcessor();
    ~SamplePluginProcessor() override;

    //AudioDebug::JuceDebugProcessor interface
    void processBlockRegenerate(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    //juce::AudioProcessor interface
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int index) override {}
    const juce::String getProgramName(int index) override { return juce::String(""); }
    void changeProgramName(int index, const juce::String& newName) override {}

    void getStateInformation(juce::MemoryBlock& destData) override {}
    void setStateInformation(const void* data, int sizeInBytes) override {}

private:
    void processInternal(int numSamples, juce::MidiBuffer& midiMessages, float* const* data);

    bool m_isPlaying = false;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplePluginProcessor)
};
