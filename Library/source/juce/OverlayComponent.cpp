// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "juce/OverlayComponent.h"

#include "juce/AudioDebuggerUI.h"
#include "juce/WaveformOverview.h"

#include "juce/DiffDetector.h"
#include "juce/MarkerStore.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace AudioDebug {

OverlayComponent::OverlayComponent(AudioDebuggerUI & debugger)
    : m_debugger(debugger)
{
    setInterceptsMouseClicks(false, false);
    setOpaque(false);
}

OverlayComponent::~OverlayComponent() = default;

void OverlayComponent::paint(juce::Graphics& g)
{
    if (!m_debugger.getOverview() || m_debugger.getViewportLengthSamples() <= 0)
        return;

    if (m_debugger.shouldPaintDetailedUI())
        paintDetailedDifferences(g);
    else
        paintMipmapDifferences(g);

    paintWaveformSeparators(g);

    m_debugger.paintUICustom(g);

    paintMarkers(g);
    paintMidiMessages(g);
}

void OverlayComponent::paintWaveformSeparators(juce::Graphics& g)
{
    const auto& chanData = m_debugger.getChannelData();
    const int totalChannels = m_debugger.getNumInputs() + m_debugger.getNumOutputs() * 2;

    g.setColour(juce::Colours::grey);
    for (int i = 0; i < totalChannels; ++i)
    {
        const auto& b = chanData.bounds[i];
        g.drawHorizontalLine((int)b.maxValue, 0.0f, (float)getWidth());
    }
}

void OverlayComponent::paintMipmapDifferences(juce::Graphics& g)
{
    auto* diffDetector = m_debugger.getDiffDetector();
    if (!diffDetector)
        return;

    const int64_t viewportStartSample = m_debugger.getViewportStartSample();
    const int64_t viewportLengthSamples = m_debugger.getViewportLengthSamples();

    auto differences = diffDetector->getInRange(viewportStartSample, viewportStartSample + viewportLengthSamples);

    if (differences.empty())
        return;

    int totalDiffSamples = 0;

    const auto& chanData = m_debugger.getChannelData();
    const uint8_t numInputs = m_debugger.getNumInputs();
    const uint8_t numOutputs = m_debugger.getNumOutputs();

    const float channelHeight = chanData.channelHeight;
    const auto& refBounds = chanData.bounds[numInputs + numOutputs];
    const float height = chanData.metadataHeight / 2;

    for (const auto& diff : differences)
    {
        float startX = (float)((diff.startSample - viewportStartSample) / (double)viewportLengthSamples * getWidth());
        float endX = (float)((diff.endSample - viewportStartSample) / (double)viewportLengthSamples * getWidth());

        if (endX - startX < 2.0f) {
            endX = startX + 2.0f;
        }

        startX = std::max(0.0f, startX);
        endX = std::min((float)getWidth(), endX);

        const float alpha = 0.3f + (0.5f * std::min(1.0f, diff.maxDifference * 10.0f));
        const float startY = refBounds.metaY + (diff.channel * height);

        g.setColour(juce::Colours::orange.withAlpha(alpha));
        g.fillRect(startX, startY, endX - startX, height);

        totalDiffSamples += (int)(diff.endSample - diff.startSample + 1);
    }

    int64_t diffCount = 0;
    for (auto& diff : differences) {
        diffCount += (diff.endSample - diff.startSample + 1);
    }

    if (totalDiffSamples > 0)
    {
        g.setColour(juce::Colours::orange);
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(juce::String(totalDiffSamples) + " differences",
            10, refBounds.metaY, 200, 20,
            juce::Justification::left);
    }
}

void OverlayComponent::paintDetailedDifferences(juce::Graphics& g)
{
    auto* diffDetector = m_debugger.getDiffDetector();
    if (!diffDetector)
        return;

    const int64_t viewportStartSample = m_debugger.getViewportStartSample();
    const int64_t viewportLengthSamples = m_debugger.getViewportLengthSamples();

    auto differences = diffDetector->getInRange(viewportStartSample, viewportStartSample + viewportLengthSamples);

    if (differences.empty())
        return;

    const auto& chanData = m_debugger.getChannelData();
    const float channelHeight = chanData.channelHeight;
    const uint8_t numInputs = m_debugger.getNumInputs();
    const uint8_t numOutputs = m_debugger.getNumOutputs();

    const float height = chanData.metadataHeight / 2;
    const float sampleWidth = std::max(2.0f, (1.0f / viewportLengthSamples * getWidth()));
    const int64_t highlightedDiff = m_debugger.getHighlightedDiff();

    int totalDiffSamples = 0;
    for (const auto& diff : differences)
    {
        const auto& outBounds = chanData.bounds[numInputs + diff.channel];
        const auto& reftBounds = chanData.bounds[numInputs + numOutputs + diff.channel];

        for (int64_t sample = diff.startSample; sample <= diff.endSample; ++sample)
        {
            if (sample < viewportStartSample || sample > viewportStartSample + viewportLengthSamples)
                continue;

            float x = (float)((sample - viewportStartSample) / (double)viewportLengthSamples * getWidth());

            auto colour = juce::Colours::orange;
            if (highlightedDiff >= diff.startSample && highlightedDiff <= diff.endSample)
                colour = juce::Colours::red;

            g.setColour(colour.withAlpha(0.5f));
            g.fillRect(x, outBounds.minValue, sampleWidth, channelHeight);
            g.fillRect(x, reftBounds.minValue, sampleWidth, channelHeight);
        }

        totalDiffSamples += (int)(diff.endSample - diff.startSample + 1);
    }

    g.setColour(juce::Colours::orange);
    g.setFont(juce::Font(11.0f, juce::Font::bold));

    const auto& refBounds = chanData.bounds[numInputs + numOutputs];

    g.drawText(juce::String(totalDiffSamples) + " differences",
        10, refBounds.metaY, 200, 20,
        juce::Justification::left);
}


void OverlayComponent::paintMarkers(juce::Graphics& g)
{
    auto* overview = m_debugger.getOverview();
    if (!overview)
        return;

    const int64_t viewportStartSample = m_debugger.getViewportStartSample();
    const int64_t viewportLengthSamples = m_debugger.getViewportLengthSamples();

    auto markers = overview->getMarkers()->getMarkersInRange(viewportStartSample, viewportStartSample + viewportLengthSamples);

    if (markers.empty())
        return;

    auto* lnf = &m_debugger.getLookAndFeel();

    const float MIN_MARKER_SPACING_PIXELS = 20.0f;
    double samplesPerPixel = viewportLengthSamples / (double)getWidth();
    int64_t minSampleSpacing = (int64_t)(MIN_MARKER_SPACING_PIXELS * samplesPerPixel);

    const auto& chanData = m_debugger.getChannelData();
    const float channelHeight = chanData.channelHeight;
    const uint8_t numInputs = m_debugger.getNumInputs();
    const uint8_t numOutputs = m_debugger.getNumOutputs();

    std::vector<MarkerPair> startMarkers;
    std::vector<MarkerPair> regularMarkers;
    std::vector<MarkerPair> customMarkers;

    int64_t lastBufferSample = -minSampleSpacing * 2;

    for (const auto& m : markers)
    {
        auto& marker = m.main;
        switch (marker.magic)
        {
        case Metadata::MARKER_BUFFER_START:
        {
            if (marker.samplePosition - lastBufferSample >= minSampleSpacing) {
                startMarkers.push_back(m);
            }

            lastBufferSample = marker.samplePosition;
            break;
        }
        case Metadata::MARKER_TEXT:
        {
            regularMarkers.push_back(m);
            break;
        }
        case Metadata::MARKER_CUSTOM:
        {
            customMarkers.push_back(m);
            break;
        }
        default:
            jassert(false); // Unhandled
        }
    }

    const bool bPaintStartMarkers = m_debugger.getOption(EDefaultOptions::BufferStartMarkers);
    const bool bPaintCustomMarkers = m_debugger.getOption(EDefaultOptions::CustomMarkers);

    if (bPaintStartMarkers || bPaintCustomMarkers)
    {
        bool showDensity = samplesPerPixel > 10000.0;

        if (!regularMarkers.empty() && bPaintCustomMarkers)
        {
            if (showDensity)
            {
                const int NUM_BUCKETS = 50;
                std::vector<int> bucketCounts(NUM_BUCKETS, 0);

                for (const auto& marker : regularMarkers)
                {
                    int64_t offsetSample = marker.main.samplePosition - viewportStartSample;
                    int bucketIndex = (int)((offsetSample * NUM_BUCKETS) / viewportLengthSamples);

                    if (bucketIndex >= 0 && bucketIndex < NUM_BUCKETS) {
                        bucketCounts[bucketIndex]++;
                    }
                }

                float bucketWidth = getWidth() / (float)NUM_BUCKETS;
                int maxCount = *std::max_element(bucketCounts.begin(), bucketCounts.end());

                if (maxCount > 0) {
                    for (int i = 0; i < NUM_BUCKETS; ++i) {
                        if (bucketCounts[i] > 0) {
                            float x = i * bucketWidth;
                            float alpha = 0.3f + (0.7f * bucketCounts[i] / (float)maxCount);

                            g.setColour(juce::Colours::yellow.withAlpha(alpha));
                            g.fillRect(x, 0.0f, bucketWidth, 5.0f);
                        }
                    }
                }

                g.setColour(lnf->findColour(AudioDebuggerLookAndFeel::markerLabelColourId).withAlpha(0.8f));
                g.setFont(11.0f);
                g.drawText(juce::String(regularMarkers.size()) + " markers", getWidth() - 100, 10, 90, 20, juce::Justification::centredRight);
            }
            else
            {
                std::vector<std::vector<const MarkerPair*>> markerGroups;
                std::vector<int> timestamps;
                std::vector<const MarkerPair*> currentGroup;

                for (const auto& m : regularMarkers)
                {
                    if (samplesPerPixel < 1000.0) // TEMP
                    {
                        if (currentGroup.empty() || currentGroup.back()->main.chanId == m.main.chanId && currentGroup.back()->main.samplePosition == m.main.samplePosition)
                        {
                            currentGroup.push_back(&m);
                        }
                        else
                        {
                            if (!currentGroup.empty()) {
                                markerGroups.push_back(currentGroup);
                            }
                            currentGroup.clear();
                            currentGroup.push_back(&m);
                        }

                        if (std::find(timestamps.begin(), timestamps.end(), m.main.samplePosition) == timestamps.end())
                            timestamps.push_back(m.main.samplePosition);
                    }
                }

                if (!currentGroup.empty()) {
                    markerGroups.push_back(currentGroup);
                }

                for (const auto& group : markerGroups)
                {
                    if (group.empty())
                        continue;

                    const auto& samplePosition = group[0]->main.samplePosition;
                    const auto& colour = group[0]->main.colorId;
                    const auto& chanId = group[0]->main.chanId;

                    float x = (float)((samplePosition - viewportStartSample) / (double)viewportLengthSamples * getWidth());

                    auto groupColor = m_debugger.colorIdToJuceColour(colour);

                    const auto& chanBounds = chanData.bounds[chanId];
                    const float lineStart = chanBounds.minValue;

                    switch (group[0]->main.paintType)
                    {
                    case EMarkerPaintType::FullLine:
                    {
                        const float lineEnd = lineStart  + chanData.bounds[chanId + 1].maxValue - chanBounds.minValue;

                        g.setColour(groupColor);
                        g.drawLine(x, lineStart, x, lineEnd, 2.0f);

                        juce::Path triangle;
                        triangle.addTriangle(x, lineStart, x - 5.0f, lineStart + 10.0f, x + 5.0f, lineStart + 10.0f);
                        g.fillPath(triangle);
                        break;
                    }
                    case EMarkerPaintType::NoLine:
                    {
                        // Don't draw vertical line
                        break;
                    }
                    }

                    if (samplesPerPixel < 1000.0)
                    {
                        g.setFont(11.0f);
                        float labelY = 12.0f;
                        constexpr float labelSpacing = 2.0f;

                        for (const auto* marker : group)
                        {
                            const auto& text = marker->main.getText();
                            if (text.empty())
                                continue;

                            juce::String label = text;

                            if (marker->bHasRegen)
                            {
                                const auto& regenText = marker->regen.getText();
                                if (text != regenText)
                                {
                                    label += " -> ";
                                    label += juce::String(regenText);
                                }
                            }

                            int labelWidth = g.getCurrentFont().getStringWidth(label) + 8;
                            int labelHeight = 16;

                            juce::Rectangle<int> labelBounds((int)x + 3, lineStart + labelY, labelWidth, labelHeight);

                            g.setColour(lnf->findColour(AudioDebuggerLookAndFeel::markerOutlineColourId).withAlpha(0.7f));
                            g.fillRect(labelBounds);

                            auto juceColor = m_debugger.colorIdToJuceColour(marker->main.colorId);
                            g.setColour(juceColor);
                            g.drawRect(labelBounds, 1);

                            g.setColour(lnf->findColour(AudioDebuggerLookAndFeel::markerLabelColourId));
                            g.drawText(label, labelBounds.reduced(2), juce::Justification::centredLeft, false);

                            labelY += labelHeight + labelSpacing;
                        }
                    }
                }

                const float lastInputY = chanData.bounds[numInputs - 1].maxValue;
                for (auto& timestamp : timestamps)
                {
                    float x = (float)((timestamp - viewportStartSample) / (double)viewportLengthSamples * getWidth());

                    g.setColour(juce::Colours::red);
                    g.drawText(juce::String(timestamp), x, lastInputY, 60, 20, juce::Justification::centredLeft);
                }
            }
        }

        if (bPaintStartMarkers)
        {
            for (const auto& marker : startMarkers)
            {
                float x = (float)((marker.main.samplePosition - viewportStartSample) /
                    (double)viewportLengthSamples * getWidth());

                auto juceColor = m_debugger.colorIdToJuceColour(marker.main.colorId);
                g.setColour(juceColor);

                const float dashLength = 8.0f;
                const float gapLength = 4.0f;
                float y = 0.0f;

                while (y < getHeight())
                {
                    float y2 = std::min(y + dashLength, (float)getHeight());
                    g.drawLine(x, y, x, y2, 0.5f);
                    y = y2 + gapLength;
                }

                g.drawText(juce::String(marker.main.samplePosition), x, getHeight() - 20, 60, 20, juce::Justification::centredLeft);
            }
        }
    }

    m_debugger.paintCustomMarkers(g, customMarkers);
}

void OverlayComponent::paintMidiMessages(juce::Graphics& g)
{
    auto* overview = m_debugger.getOverview();
    if (!overview)
        return;

    const int64_t viewportStartSample = m_debugger.getViewportStartSample();
    const int64_t viewportLengthSamples = m_debugger.getViewportLengthSamples();

    auto timestamps = overview->getMarkers()->getAllMidi();

    for (auto& [timestamp, messages] : timestamps)
    {
        for (auto& msg : messages)
        {
            auto juceMsg = juce::MidiMessage(msg.data, static_cast<int>(msg.dataSize));

            if (juceMsg.isNoteOn() || juceMsg.isNoteOff())
            {
                int absPosition = timestamp + msg.position;
                float x = (float)((absPosition - viewportStartSample) / (double)viewportLengthSamples * getWidth());

                g.setColour(juce::Colours::red);
                g.drawRect(x, 0.0f, 100.0f, 20.0f);

                juce::String txt;

                if (juceMsg.isNoteOn())
                    txt = juce::String::formatted("NoteOn %d Vel=%d", juceMsg.getNoteNumber(), juceMsg.getVelocity());
                else if (juceMsg.isNoteOff())
                    txt = juce::String::formatted("NoteOff %d", juceMsg.getNoteNumber());

                int textWidth = g.getCurrentFont().getStringWidth(txt);
                g.drawText(txt, x, 0.0f, textWidth, 20, juce::Justification::centredLeft);
            }
        }
    }
}

} // namespace AudioDebug
