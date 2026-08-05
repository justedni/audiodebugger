// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "juce/MidiTrackComponent.h"

#include "juce/AudioDebuggerUI.h"
#include "juce/WaveformOverview.h"
#include "juce/MarkerStore.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace AudioDebug {

MidiTrackComponent::MidiTrackComponent(AudioDebuggerUI& debugger)
    : m_debugger(debugger)
{
    setInterceptsMouseClicks(false, false);
    setOpaque(false);
}

MidiTrackComponent::~MidiTrackComponent() = default;

void MidiTrackComponent::rebuildNoteCache()
{
    m_notes.clear();
    m_otherMessages.clear();
    m_cacheValid = false;

    auto* overview = m_debugger.getOverview();
    if (!overview || !overview->getMarkers())
        return;

    const auto& allMidi = overview->getMarkers()->getAllMidi();

    std::map<int, size_t> heldNotes;

    int lowest = 127;
    int highest = 0;

    for (const auto& [timestamp, messages] : allMidi)
    {
        for (const auto& msg : messages)
        {
            const auto juceMsg = juce::MidiMessage(msg.data, static_cast<int>(msg.dataSize));
            const int64_t absPosition = (int64_t)timestamp + msg.position;

            if (juceMsg.isNoteOn())
            {
                const int key = juceMsg.getNoteNumber() * 16 + juceMsg.getChannel();

                auto held = heldNotes.find(key);
                if (held != heldNotes.end() && m_notes[held->second].endSample < 0)
                    m_notes[held->second].endSample = absPosition;

                NoteEvent note;
                note.startSample = absPosition;
                note.endSample = -1;
                note.noteNumber = juceMsg.getNoteNumber();
                note.velocity = juceMsg.getVelocity();
                note.channel = juceMsg.getChannel();

                heldNotes[key] = m_notes.size();
                m_notes.push_back(note);

                lowest = std::min(lowest, note.noteNumber);
                highest = std::max(highest, note.noteNumber);
            }
            else if (juceMsg.isNoteOff())
            {
                const int key = juceMsg.getNoteNumber() * 16 + juceMsg.getChannel();

                auto held = heldNotes.find(key);
                if (held != heldNotes.end() && m_notes[held->second].endSample < 0)
                {
                    m_notes[held->second].endSample = absPosition;
                    heldNotes.erase(held);
                }
            }
            else
            {
                juce::String label;

                if (juceMsg.isController())
                    label = juce::String::formatted("CC%d=%d", juceMsg.getControllerNumber(), juceMsg.getControllerValue());
                else if (juceMsg.isPitchWheel())
                    label = juce::String::formatted("PB %d", juceMsg.getPitchWheelValue());
                else if (juceMsg.isProgramChange())
                    label = juce::String::formatted("PC %d", juceMsg.getProgramChangeNumber());
                else if (juceMsg.isAllNotesOff() || juceMsg.isAllSoundOff())
                    label = "AllOff";

                if (label.isNotEmpty())
                    m_otherMessages.emplace_back(absPosition, label);
            }
        }
    }

    if (lowest <= highest)
    {
        m_lowestNote = std::max(0, lowest - 2);
        m_highestNote = std::min(127, highest + 2);

        if (m_highestNote - m_lowestNote < 12)
        {
            const int centre = (m_lowestNote + m_highestNote) / 2;
            m_lowestNote = std::max(0, centre - 6);
            m_highestNote = std::min(127, centre + 6);
        }
    }

    m_cacheValid = true;
}

float MidiTrackComponent::sampleToX(int64_t sample) const
{
    const int64_t viewportStart = m_debugger.getViewportStartSample();
    const int64_t viewportLength = m_debugger.getViewportLengthSamples();

    return (float)((sample - viewportStart) / (double)viewportLength * getWidth());
}

float MidiTrackComponent::noteToY(int noteNumber) const
{
    const int span = std::max(1, m_highestNote - m_lowestNote);
    const float usable = (float)getHeight() - 12.0f;
    const float ratio = (float)(noteNumber - m_lowestNote) / (float)span;

    return usable - (ratio * usable);
}

void MidiTrackComponent::paint(juce::Graphics& g)
{
    if (!m_debugger.getOverview() || m_debugger.getViewportLengthSamples() <= 0)
        return;

    if (!m_cacheValid)
        rebuildNoteCache();

    paintBackground(g);
    paintNotes(g);
    paintOtherMessages(g);
}

void MidiTrackComponent::paintBackground(juce::Graphics& g)
{
    auto* lnf = &m_debugger.getLookAndFeel();
    const auto separator = lnf->findColour(AudioDebuggerLookAndFeel::waveformSeparatorColourId);

    g.setColour(separator.withAlpha(0.12f));

    for (int note = m_lowestNote; note <= m_highestNote; ++note)
    {
        if (note % 12 != 0)
            continue;

        const float y = noteToY(note);
        g.drawHorizontalLine((int)y, 0.0f, (float)getWidth());
    }

    g.setColour(separator.withAlpha(0.2f));
    g.drawHorizontalLine(getHeight() - 12, 0.0f, (float)getWidth());
}

void MidiTrackComponent::paintNotes(juce::Graphics& g)
{
    if (m_notes.empty())
        return;

    const int64_t viewportStart = m_debugger.getViewportStartSample();
    const int64_t viewportLength = m_debugger.getViewportLengthSamples();
    const int64_t viewportEnd = viewportStart + viewportLength;

    const int span = std::max(1, m_highestNote - m_lowestNote);
    const float noteHeight = std::max(10.0f, ((float)getHeight() - 12.0f) / (float)span);

    const bool showLabels = (getWidth() / (double)viewportLength) > 0.0005;

    g.setFont(juce::Font(10.0f));

    for (const auto& note : m_notes)
    {
        const int64_t endSample = (note.endSample < 0) ? viewportEnd : note.endSample;

        if (endSample < viewportStart || note.startSample > viewportEnd)
            continue;

        float x1 = sampleToX(note.startSample);
        float x2 = sampleToX(endSample);

        if (x2 - x1 < 2.0f)
            x2 = x1 + 2.0f;

        x1 = std::max(-1.0f, x1);
        x2 = std::min((float)getWidth() + 1.0f, x2);

        const float y = noteToY(note.noteNumber);

        const float hue = std::fmod(0.55f + (note.channel - 1) * 0.13f, 1.0f);
        const float alpha = 0.45f + 0.55f * (note.velocity / 127.0f);
        const auto colour = juce::Colour::fromHSV(hue, 0.65f, 0.95f, alpha);

        g.setColour(colour);
        g.fillRoundedRectangle(x1, y - noteHeight * 0.5f, x2 - x1, noteHeight, 1.5f);

        if (note.endSample < 0)
        {
            g.setColour(juce::Colours::red.withAlpha(0.7f));
            g.drawLine(x2 - 1.0f, y - noteHeight, x2 - 1.0f, y + noteHeight, 1.0f);
        }

        if (showLabels && (x2 - x1) > 28.0f && noteHeight > 8.0f)
        {
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.drawText(juce::MidiMessage::getMidiNoteName(note.noteNumber, true, true, 3),
                juce::Rectangle<float>(x1 + 3.0f, y - noteHeight * 0.5f, x2 - x1 - 4.0f, noteHeight).toNearestInt(),
                juce::Justification::centredLeft, false);
        }
    }
}

void MidiTrackComponent::paintOtherMessages(juce::Graphics& g)
{
    if (m_otherMessages.empty())
        return;

    const int64_t viewportStart = m_debugger.getViewportStartSample();
    const int64_t viewportEnd = viewportStart + m_debugger.getViewportLengthSamples();

    const float stripY = (float)getHeight() - 11.0f;
    const bool showLabels = m_debugger.shouldPaintDetailedUI();

    g.setFont(juce::Font(9.0f));

    float lastLabelX = -1000.0f;

    for (const auto& [sample, label] : m_otherMessages)
    {
        if (sample < viewportStart || sample > viewportEnd)
            continue;

        const float x = sampleToX(sample);

        g.setColour(juce::Colours::yellow.withAlpha(0.7f));
        g.fillRect(x, stripY, 2.0f, 10.0f);

        if (showLabels && (x - lastLabelX) > 40.0f)
        {
            g.setColour(juce::Colours::yellow.withAlpha(0.9f));
            g.drawText(label, (int)x + 3, (int)stripY, 60, 10, juce::Justification::centredLeft, false);
            lastLabelX = x;
        }
    }
}

} // namespace AudioDebug
