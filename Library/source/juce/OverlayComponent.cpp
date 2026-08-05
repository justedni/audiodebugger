// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "juce/OverlayComponent.h"

#include "juce/AudioDebuggerUI.h"
#include "juce/WaveformOverview.h"

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
    {
        paintStatusText(g);
        return;
    }

    paintMarkers(g);

    m_debugger.paintOverlayCustom(g);

    paintStatusText(g);
}

void OverlayComponent::paintStatusText(juce::Graphics& g)
{
    auto* lnf = &m_debugger.getLookAndFeel();

    if (!m_debugger.getOverview())
    {
        g.setColour(lnf->findColour(AudioDebuggerLookAndFeel::markerLabelColourId));
        g.drawText("No audio file loaded", getLocalBounds(), juce::Justification::centred);
        return;
    }

    if (!m_debugger.isLoadingFile())
        return;

    auto currentFont = g.getCurrentFont();
    g.setFont(juce::Font(24.0f));

    const juce::String progressText =
        juce::String(m_debugger.getOverview()->getLoadingProgress()) + "% - Generating waveform...";

    const int textWidth  = g.getCurrentFont().getStringWidth(progressText);
    const int textHeight = (int)g.getCurrentFont().getHeight();

    juce::Rectangle<int> textBounds((getWidth() - textWidth) / 2 - 10,
                                    (getHeight() - textHeight) / 2 - 10,
                                    textWidth + 20,
                                    textHeight + 20);

    g.setColour(juce::Colours::black);
    g.fillRect(textBounds);

    g.setColour(juce::Colours::white);
    g.drawText(progressText, getLocalBounds().reduced(10), juce::Justification::centred);

    g.setFont(currentFont);
}

void OverlayComponent::paintMarkers(juce::Graphics& g)
{
    auto* overview = m_debugger.getOverview();
    if (!overview)
        return;

    const int64_t viewportStartSample = m_debugger.getViewportStartSample();
    const int64_t viewportLengthSamples = m_debugger.getViewportLengthSamples();

    jassert(overview);
    auto markers = overview->getMarkers()->getMarkersInRange(viewportStartSample, viewportStartSample + viewportLengthSamples);

    if (markers.empty())
        return;

    auto* lnf = &m_debugger.getLookAndFeel();

    const float MIN_MARKER_SPACING_PIXELS = 20.0f;
    double samplesPerPixel = viewportLengthSamples / (double)getWidth();
    int64_t minSampleSpacing = (int64_t)(MIN_MARKER_SPACING_PIXELS * samplesPerPixel);

    const uint8_t numInputs = m_debugger.getNumInputs();

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

                    const auto chanArea = m_debugger.getChannelArea(chanId);
                    if (chanArea.isEmpty())
                        continue;

                    const float lineStart = (float)chanArea.getY();

                    switch (group[0]->main.paintType)
                    {
                    case EMarkerPaintType::FullLine:
                    {
                        float lineEnd = (float)chanArea.getBottom();

                        const auto nextArea = m_debugger.getChannelArea(chanId + 1);
                        if (!nextArea.isEmpty() && nextArea.getBottom() > chanArea.getBottom())
                            lineEnd = (float)nextArea.getBottom();

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

                            juce::Rectangle<int> labelBounds((int)x + 3, (int)(lineStart + labelY), labelWidth, labelHeight);

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

                float lastInputY = 0.0f;
                for (int i = (int)numInputs - 1; i >= 0; --i)
                {
                    const auto area = m_debugger.getChannelArea(m_debugger.getInputChanId(i));
                    if (!area.isEmpty())
                    {
                        lastInputY = (float)area.getBottom();
                        break;
                    }
                }

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

} // namespace AudioDebug
