// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MarkerStore.h"

namespace AudioDebug {

void MarkerStore::clear()
{
	juce::ScopedLock lock(m_lock);
	m_markers.clear();
	m_midi.clear();
}

void MarkerStore::addMarker(Marker&& marker)
{
	juce::ScopedLock lock(m_lock);
	m_markers[marker.samplePosition].emplace_back(std::move(marker));
}

void MarkerStore::addMidi(int64_t timestamp, std::vector<RawMidiMessage> msgs)
{
	juce::ScopedLock lock(m_lock);
	m_midi[timestamp] = std::move(msgs);
}

void MarkerStore::initRegen()
{
    juce::ScopedLock lock(m_lock);
    for (auto& t : m_markers)
    {
        for (auto& m : t.second)
        {
            m.bHasRegen = false;
        }
    }
}

void MarkerStore::updateRegenMarker(Marker&& regenMarker)
{
    juce::ScopedLock lock(m_lock);

    auto it = m_markers.find(regenMarker.samplePosition);

    if (it == m_markers.end())
        return;

    auto& markersAtPosition = it->second;

    std::vector<MarkerPair*> existingMarkers;
    for (auto& m : markersAtPosition)
    {
        if (m.main.magic == regenMarker.magic && m.main.chanId == regenMarker.chanId)
        {
            existingMarkers.push_back(&m);
        }
    }

    if (existingMarkers.size() == 1)
    {
        auto& rptMarker = existingMarkers[0];
        if ((rptMarker->main.data != regenMarker.data) || regenMarker.chanId == 0xff)
        {
            rptMarker->bHasRegen = true;
            rptMarker->regen = std::move(regenMarker);
        }
    }
}


std::vector<MarkerPair> MarkerStore::getMarkersInRange(int64_t start, int64_t end) const
{
    juce::ScopedLock lock(m_lock);

    std::vector<MarkerPair> result;

    if (m_markers.empty())
        return result;

    auto startIt = m_markers.lower_bound(start);
    auto endIt = m_markers.upper_bound(end);

    for (auto it = startIt; it != endIt; ++it)
    {
        const auto& markersAtPosition = it->second;
        result.insert(result.end(), markersAtPosition.begin(), markersAtPosition.end());
    }

    return result;
}

std::map<int64_t, std::vector<RawMidiMessage>> MarkerStore::getAllMidi() const
{
    juce::ScopedLock lock(m_lock);
    return m_midi;
}

} // namespace AudioDebug
