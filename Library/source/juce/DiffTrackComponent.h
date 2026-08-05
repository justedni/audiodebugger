// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "juce/ChannelComponent.h"

namespace AudioDebug {

class AudioDebuggerUI;

class DiffTrackComponent : public TrackComponent
{
public:
    explicit DiffTrackComponent(AudioDebuggerUI& debugger);
    ~DiffTrackComponent() override;

    int getGlobalChanId() const override { return -1; }
    int computeTrackHeight(int globalTrackHeight) const override;
    void paint(juce::Graphics& g) override;

    static constexpr int ROW_HEIGHT = 14;
    static constexpr int MIN_HEIGHT = 18;
    static constexpr int MAX_HEIGHT = 96;

private:
    juce::Rectangle<float> getRowBounds(int outputIndex) const;

    AudioDebuggerUI& m_debugger;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DiffTrackComponent)
};

} // namespace AudioDebug
