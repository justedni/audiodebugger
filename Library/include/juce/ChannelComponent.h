// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/RptFileFormat.h"

namespace AudioDebug {

class AudioDebuggerUI;


class TrackComponent : public juce::Component
{
public:
    ~TrackComponent() override = default;

    virtual int getGlobalChanId() const { return -1; }
    virtual int computeTrackHeight(int globalTrackHeight) const { return globalTrackHeight; }
};

class ChannelHeaderButton : public juce::Button
{
public:
    ChannelHeaderButton(const juce::String& name, juce::Colour accent);

    void setAccentColour(juce::Colour c) { m_accent = c; repaint(); }
    void paintButton(juce::Graphics& g, bool shouldDrawHighlighted, bool shouldDrawDown) override;

private:
    juce::Colour m_accent;
};


class ChannelComponent : public TrackComponent
{
public:
    ChannelComponent(AudioDebuggerUI& debugger, int globalChanId);
    ~ChannelComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    int getGlobalChanId() const override { return m_globalChanId; }

    void clearCache();

    bool checkCacheUpdateRequired(int64_t viewportStartSample, int64_t viewportLengthSamples) const;
    void prepareCacheUpdateIfRequired(int64_t viewportStartSample, int64_t viewportLengthSamples, int64_t cacheStart, int64_t cacheLength);
    void lockCache();
    void unlockCache();
    SampleCache* getSampleCache() { return &sampleCache; }
    const SampleCache* getSampleCache() const { return &sampleCache; }
    const juce::CriticalSection& getCacheLock() const { return cacheLock; }

private:
    double getVerticalZoom() const { return zoomVSlider.getValue(); }

    void paintDifferences(juce::Graphics& g);
    void paintWaveformFromMipmap(juce::Graphics& g);
    void paintWaveformFromRawSamples(juce::Graphics& g);
    void paintSampleValues(juce::Graphics& g);
    void paintCentreLine(juce::Graphics& g);

    AudioDebuggerUI& m_debugger;
    const int m_globalChanId;

    mutable juce::CriticalSection cacheLock;
    SampleCache sampleCache;

    juce::Slider zoomVSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelComponent)
};

class CollapsibleChannel : public juce::Component
{
public:
    CollapsibleChannel(juce::String name, TrackComponent* track, juce::Colour accent);

    void setExpanded(bool shouldExpand);
    bool isExpanded() const { return expanded; }

    TrackComponent* getTrack() const { return track; }
    int getGlobalChanId() const { return track != nullptr ? track->getGlobalChanId() : -1; }

    void setTrackHeight(int h);
    int getTrackHeight() const { return trackHeight; }

    int getPreferredHeight() const;

    static constexpr int getHeaderHeight()   { return headerHeight; }
    static constexpr int getMinTrackHeight() { return minTrackHeight; }

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void()> onToggled;

private:
    ChannelHeaderButton header;
    TrackComponent* track = nullptr;
    juce::Colour accentColour;
    bool expanded = true;
    int trackHeight = 120;

    static constexpr int headerHeight = 22;
    static constexpr int minTrackHeight = 40;
};

} // namespace AudioDebug
