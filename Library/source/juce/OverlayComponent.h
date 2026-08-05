// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace AudioDebug {

class AudioDebuggerUI;
struct MarkerPair;

class OverlayComponent : public juce::Component
{
public:
    explicit OverlayComponent(AudioDebuggerUI& debugger);
    ~OverlayComponent() override;

    void paint(juce::Graphics& g) override;

private:
    void paintMarkers(juce::Graphics& g);

    void paintStatusText(juce::Graphics& g);

    AudioDebuggerUI& m_debugger;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OverlayComponent)
};

} // namespace AudioDebug
