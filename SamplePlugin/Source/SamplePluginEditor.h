#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class SamplePluginProcessor;

namespace AudioDebug {
    class DebuggerSpawner;
}

class SamplePluginEditor : public juce::AudioProcessorEditor
{
public:
    SamplePluginEditor(SamplePluginProcessor&);
    ~SamplePluginEditor() override;

    void resized() override;

private:
    SamplePluginProcessor& processor;

    std::unique_ptr<AudioDebug::DebuggerSpawner> m_debuggerSpawner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplePluginEditor)
};
