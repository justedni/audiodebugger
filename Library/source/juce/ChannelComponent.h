// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace AudioDebug {

class AudioDebuggerUI;
class WaveformOverview;

class ChannelComponent : public juce::Component
{
public:
    ChannelComponent(AudioDebuggerUI& debugger, int globalChanId);
    ~ChannelComponent() override;

    void paint(juce::Graphics& g) override;

    int getGlobalChanId() const { return m_globalChanId; }

private:
    void paintWaveformFromMipmap(juce::Graphics& g);
    void paintWaveformFromRawSamples(juce::Graphics& g);
    void paintSampleValues(juce::Graphics& g);
    void paintCentreLine(juce::Graphics& g);

    AudioDebuggerUI& m_debugger;
    const int m_globalChanId;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelComponent)
};

} // namespace AudioDebug
