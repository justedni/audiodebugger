#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/DebugProcessor.h"

namespace AudioDebug {
struct RawMidiMessage;
}

class SamplePluginEditor;

class SamplePluginProcessor : public juce::AudioProcessor
                            , public AudioDebug::DebugProcessor
{
public:
    SamplePluginProcessor();
    ~SamplePluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

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

    //AudioDebug::DebugProcessor interface
public:
    void prepareToPlayHook(double sampleRate, int samplesPerBlock, int numChannelsIn) override;
    void processBlockHook(int numSamples, float** inputs, int32_t numChannelsIn, const std::vector<AudioDebug::RawMidiMessage>& inMessages, float** outputs) override;

private:
    void processInternal(int numSamples, juce::MidiBuffer& midiMessages, float* const* data);

    bool m_isPlaying = false;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplePluginProcessor)
};
