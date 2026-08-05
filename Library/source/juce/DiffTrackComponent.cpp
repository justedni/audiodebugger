// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "juce/DiffTrackComponent.h"

#include "juce/AudioDebuggerUI.h"
#include "juce/WaveformOverview.h"

#include "core/DiffDetector.h"

namespace AudioDebug {

DiffTrackComponent::DiffTrackComponent(AudioDebuggerUI& debugger)
    : m_debugger(debugger)
{
    setInterceptsMouseClicks(false, false);
    setOpaque(false);
}

DiffTrackComponent::~DiffTrackComponent() = default;

int DiffTrackComponent::computeTrackHeight(int globalTrackHeight) const
{
    juce::ignoreUnused(globalTrackHeight);

    const int numOutputs = std::max(1, (int)m_debugger.getNumOutputs());
    return juce::jlimit(MIN_HEIGHT, MAX_HEIGHT, numOutputs * ROW_HEIGHT);
}

juce::Rectangle<float> DiffTrackComponent::getRowBounds(int outputIndex) const
{
    const int numOutputs = std::max(1, (int)m_debugger.getNumOutputs());
    const float rowHeight = (float)getHeight() / (float)numOutputs;

    return { 0.0f, outputIndex * rowHeight, (float)getWidth(), rowHeight };
}

void DiffTrackComponent::paint(juce::Graphics& g)
{
    auto* diffDetector = m_debugger.getDiffDetector();
    if (!diffDetector || m_debugger.getViewportLengthSamples() <= 0)
        return;

    auto* lnf = &m_debugger.getLookAndFeel();

    const int64_t viewportStartSample = m_debugger.getViewportStartSample();
    const int64_t viewportLengthSamples = m_debugger.getViewportLengthSamples();
    const int numOutputs = std::max(1, (int)m_debugger.getNumOutputs());

    g.setColour(lnf->findColour(AudioDebuggerLookAndFeel::waveformSeparatorColourId).withAlpha(0.12f));
    for (int out = 1; out < numOutputs; ++out)
        g.drawHorizontalLine((int)getRowBounds(out).getY(), 0.0f, (float)getWidth());

    auto differences = diffDetector->getInRange(viewportStartSample, viewportStartSample + viewportLengthSamples);

    if (differences.empty())
    {
        g.setColour(lnf->findColour(AudioDebuggerLookAndFeel::markerLabelColourId).withAlpha(0.35f));
        g.setFont(juce::Font(10.0f));
        g.drawText("No differences in view", 6, 0, 200, getHeight(), juce::Justification::centredLeft, false);
        return;
    }

    const int64_t highlightedDiff = m_debugger.getHighlightedDiff();
    const float width = (float)getWidth();

    int64_t totalDiffSamples = 0;

    for (const auto& diff : differences)
    {
        if (diff.channel < 0 || diff.channel >= numOutputs)
            continue;

        const auto row = getRowBounds(diff.channel);

        float startX = (float)((diff.startSample - viewportStartSample) / (double)viewportLengthSamples * width);
        float endX = (float)((diff.endSample + 1 - viewportStartSample) / (double)viewportLengthSamples * width);

        if (endX - startX < 2.0f)
            endX = startX + 2.0f;

        startX = std::max(0.0f, startX);
        endX = std::min(width, endX);

        const bool bHighlighted = (highlightedDiff >= diff.startSample && highlightedDiff <= diff.endSample);
        const float alpha = 0.35f + (0.55f * std::min(1.0f, diff.maxDifference * 10.0f));

        g.setColour((bHighlighted ? juce::Colours::red : juce::Colours::orange).withAlpha(alpha));
        g.fillRect(startX, row.getY() + 1.0f, endX - startX, row.getHeight() - 2.0f);

        totalDiffSamples += (diff.endSample - diff.startSample + 1);
    }

    if (totalDiffSamples > 0)
    {
        const juce::String text = juce::String(totalDiffSamples) + " differences";

        g.setFont(juce::Font(10.0f, juce::Font::bold));
        const int textWidth = g.getCurrentFont().getStringWidth(text) + 8;

        juce::Rectangle<int> labelBounds(getWidth() - textWidth - 6, 1, textWidth, std::min(14, getHeight() - 2));

        g.setColour(lnf->findColour(AudioDebuggerLookAndFeel::markerOutlineColourId).withAlpha(0.7f));
        g.fillRect(labelBounds);

        g.setColour(juce::Colours::orange);
        g.drawText(text, labelBounds, juce::Justification::centred, false);
    }
}

} // namespace AudioDebug
