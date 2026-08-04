// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "juce/JuceDebugProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>

class SamplePluginEditor;

class SamplePluginProcessor : public AudioDebug::JuceDebugProcessor
{
public:
    SamplePluginProcessor();
    ~SamplePluginProcessor() override;

    //AudioDebug::JuceDebugProcessor interface
    void prepareToPlayImpl(double sampleRate, int samplesPerBlock) override;
    void processBlockImpl(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages, AudioDebug::ScopedJuceDebugBlock* scope) override;

    //juce::AudioProcessor interface
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram([[maybe_unused]] int index) override {}
    const juce::String getProgramName([[maybe_unused]] int index) override { return juce::String(""); }
    void changeProgramName([[maybe_unused]] int index, [[maybe_unused]] const juce::String& newName) override {}

    void getStateInformation([[maybe_unused]] juce::MemoryBlock& destData) override {}
    void setStateInformation([[maybe_unused]] const void* data, [[maybe_unused]] int sizeInBytes) override {}

private:
    juce::AudioFormatManager m_formatManager;
    juce::Synthesiser m_synth;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplePluginProcessor)
};
