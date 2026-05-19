// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

#include "core/AudioDebug.h"

namespace AudioDebug {

struct MarkerPair
{
    MarkerPair(Marker&& m)
        : main(std::move(m))
    {}

    Marker main;
    bool bHasRegen = false;
    Marker regen;
};

class MarkerStore
{
public:
    void clear();

    void addMarker(Marker&& marker);
    void addMidi(int64_t timestamp, std::vector<RawMidiMessage> msgs);

    void initRegen();
    void updateRegenMarker(Marker&& regenMarker);

    std::vector<MarkerPair> getMarkersInRange(int64_t start, int64_t end) const;
    std::map<int64_t, std::vector<RawMidiMessage>> getAllMidi() const;

private:
    mutable juce::CriticalSection m_lock;
    std::map<int64_t, std::vector<MarkerPair>> m_markers;
    std::map<int64_t, std::vector<RawMidiMessage>> m_midi;
};

} // namespace AudioDebug
