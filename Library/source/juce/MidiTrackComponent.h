// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "juce/ChannelComponent.h"

#include <map>

namespace AudioDebug {

class AudioDebuggerUI;

class MidiTrackComponent : public TrackComponent
{
public:
    explicit MidiTrackComponent(AudioDebuggerUI& debugger);
    ~MidiTrackComponent() override;

    int getGlobalChanId() const override { return -1; }

    int computeTrackHeight(int globalTrackHeight) const override
    {
        return juce::jlimit(MIN_HEIGHT, MAX_HEIGHT, globalTrackHeight / 2);
    }

    void paint(juce::Graphics& g) override;

    void invalidateCache() { m_cacheValid = false; }

    bool hasEvents() const { return !m_notes.empty(); }

    static constexpr int MIN_HEIGHT = 40;
    static constexpr int MAX_HEIGHT = 120;

private:
    struct NoteEvent
    {
        int64_t startSample = 0;
        int64_t endSample = -1;
        int noteNumber = 0;
        int velocity = 0;
        int channel = 1;
    };

    void rebuildNoteCache();

    float sampleToX(int64_t sample) const;
    float noteToY(int noteNumber) const;

    void paintBackground(juce::Graphics& g);
    void paintNotes(juce::Graphics& g);
    void paintOtherMessages(juce::Graphics& g);

    AudioDebuggerUI& m_debugger;

    std::vector<NoteEvent> m_notes;
    std::vector<std::pair<int64_t, juce::String>> m_otherMessages;

    int m_lowestNote = 36;
    int m_highestNote = 84;

    bool m_cacheValid = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiTrackComponent)
};

} // namespace AudioDebug
