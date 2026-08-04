// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "juce/ChannelComponent.h"

#include "juce/MipmapBuilder.h"
#include "juce/Mipmaps.h"
#include "juce/AudioDebuggerUI.h"
#include "juce/WaveformOverview.h"

namespace AudioDebug {

static float clampF(float n, float lower, float upper)
{
    return std::max(lower, std::min(n, upper));
}

ChannelComponent::ChannelComponent(AudioDebuggerUI& debugger, int globalChanId)
    : m_debugger(debugger)
    , m_globalChanId(globalChanId)
{
    setInterceptsMouseClicks(false, false);
    setOpaque(false);
}

ChannelComponent::~ChannelComponent() = default;

void ChannelComponent::paint(juce::Graphics& g)
{
    if (m_debugger.getViewportLengthSamples() <= 0)
        return;

    if (m_debugger.shouldPaintDetailedUI())
    {
        paintWaveformFromRawSamples(g);

        if (m_debugger.shouldPaintExtraDetailedUI())
        {
            paintSampleValues(g);
        }
    }
    else
    {
        paintWaveformFromMipmap(g);
    }

    paintCentreLine(g);
}

void ChannelComponent::paintWaveformFromMipmap(juce::Graphics& g)
{
    auto* overview = m_debugger.getOverview();
    auto* builder = overview ? overview->getMipMapBuilder() : nullptr;
    if (!builder)
        return;

    const auto clip = g.getClipBounds();
    const float width = (float)getWidth();

    if (width <= 0.0f || clip.isEmpty())
        return;

    builder->withLock([&]()
    {
        const double samplesPerPixel = m_debugger.getSamplesPerPixel();
        const int bestLevel = builder->selectBestLevel(samplesPerPixel, 0);
        const auto& mipmapData = builder->getLevel(bestLevel, m_globalChanId);

        const int64_t viewportStartSample = m_debugger.getViewportStartSample();
        const int64_t viewportLengthSamples = m_debugger.getViewportLengthSamples();

        const int clipLeft = std::max(0, clip.getX() - 1);
        const int clipRight = std::min((int)width, clip.getRight() + 1);

        const int64_t firstSample = m_debugger.pixelToSample((float)clipLeft, width);
        const int64_t lastSample = m_debugger.pixelToSample((float)clipRight, width);

        int startPoint = (int)(firstSample / mipmapData.samplesPerPoint);
        int endPoint = (int)((lastSample + mipmapData.samplesPerPoint - 1) / mipmapData.samplesPerPoint);

        startPoint = std::max(startPoint, 0);
        endPoint = std::min(endPoint, (int)mipmapData.data.size());

        if (startPoint >= endPoint)
            return;

        g.setColour(m_debugger.getWaveformColour((uint8_t)m_globalChanId));

        const float height = (float)getHeight();
        const float centreY = height / 2.0f;
        const float scale   = (height / 2.2f) * (float)m_debugger.getVerticalZoom();

        for (int point = startPoint; point < endPoint; ++point)
        {
            const int64_t pointSample = (int64_t)point * mipmapData.samplesPerPoint;
            const float x = m_debugger.sampleToPixel(pointSample, width);

            if (x < (float)clipLeft - 1.0f || x > (float)clipRight + 1.0f)
                continue;

            const auto& mm = mipmapData.data[point];

            float y1 = centreY - (mm.max * scale);
            float y2 = centreY - (mm.min * scale);
            y1 = clampF(y1, 0.0f, height);
            y2 = clampF(y2, 0.0f, height);

            g.drawLine(x, y1, x, y2, 1.0f);
        }
    });
}

void ChannelComponent::paintWaveformFromRawSamples(juce::Graphics& g)
{
    juce::ScopedLock lock(m_debugger.getCacheLock());

    m_debugger.updateSampleCache_();

    const auto* chanSamples = m_debugger.getCachedSamplesForChannel(m_globalChanId);
    if (!chanSamples || chanSamples->empty())
        return;

    const int64_t viewportStartSample = m_debugger.getViewportStartSample();
    const int64_t viewportLengthSamples = m_debugger.getViewportLengthSamples();
    const int64_t cacheStart    = m_debugger.getCacheStartSample();

    const float height = (float)getHeight();
    const float width = (float)getWidth();
    const float centreY = height / 2.0f;
    const float scale = (height / 2.2f) * (float)m_debugger.getVerticalZoom();

    const bool bDrawRects = m_debugger.getOption(EDefaultOptions::SamplesAsRects);
    const double samplesPerPixel = m_debugger.getSamplesPerPixel();
    const bool bDisplayAsPath = true;// (samplesPerPixel < 2.0);

    g.setColour(m_debugger.getWaveformColour((uint8_t)m_globalChanId));

    juce::Path waveformPath;
    bool firstPoint = true;
    float lastY = 0.0f;

    for (int64_t i = 0; i < (int64_t)chanSamples->size(); ++i)
    {
        const int64_t sampleIndex = cacheStart + i;
        const float x = (float)((sampleIndex - viewportStartSample) / (double)viewportLengthSamples * width);

        if (x < -10.0f || x > width + 10.0f)
            continue;

        float y = centreY - ((*chanSamples)[i] * scale);
        y = clampF(y, 0.0f, height);

        if (bDisplayAsPath)
        {
            if (firstPoint)
            {
                waveformPath.startNewSubPath(x, y);
                firstPoint = false;
            }
            else
            {
                if (bDrawRects)
                    waveformPath.lineTo(x, lastY);
                waveformPath.lineTo(x, y);
            }
        }
        else
        {
            firstPoint = false;
            g.drawVerticalLine((int)x, lastY, y);
        }

        if (bDrawRects)
            lastY = y;
    }

    if (bDisplayAsPath)
        g.strokePath(waveformPath, juce::PathStrokeType(1.0f));
}

void ChannelComponent::paintSampleValues(juce::Graphics& g)
{
    if (!m_debugger.getOption(EDefaultOptions::SampleValues))
        return;

    auto* lnf = &m_debugger.getLookAndFeel();

    juce::ScopedLock lock(m_debugger.getCacheLock());

    const auto* chanSamples = m_debugger.getCachedSamplesForChannel(m_globalChanId);
    if (!chanSamples || chanSamples->empty())
        return;

    const auto bitDepth = m_debugger.getBitDepth();

    const int64_t viewportStart = m_debugger.getViewportStartSample();
    const int64_t viewportLen   = m_debugger.getViewportLengthSamples();
    const int64_t cacheStart    = m_debugger.getCacheStartSample();
    const float width = (float)getWidth();
    const float centreY = getHeight() / 2.0f;
    const double pixelsPerSample = width / (double)viewportLen;

    float fontSize = m_debugger.getFontSize(pixelsPerSample);
    g.setFont(juce::Font(fontSize));

    const float halfWidth = (float)((1.0 / (double)viewportLen) * width) / 2.0f;

    for (int64_t i = 0; i < (int64_t)chanSamples->size(); ++i)
    {
        const int64_t sampleIndex = cacheStart + i;
        if (sampleIndex < viewportStart || sampleIndex >= viewportStart + viewportLen)
            continue;

        const float x = (float)((sampleIndex - viewportStart) / (double)viewportLen * width) + halfWidth;
        if (x < -50.0f || x > width + 50.0f)
            continue;

        const juce::String valueText = m_debugger.getSampleValueString((*chanSamples)[i], bitDepth);

        const int textWidth  = g.getCurrentFont().getStringWidth(valueText);
        const int textHeight = (int)fontSize + 2;

        const float textY = centreY - 15.0f;
        const juce::Rectangle<float> textBounds(x - textWidth / 2.0f, textY, (float)textWidth, (float)textHeight);

        g.setColour(lnf->findColour(AudioDebuggerLookAndFeel::markerOutlineColourId).withAlpha(0.6f));
        g.fillRect(textBounds);

        g.setColour(m_debugger.getWaveformColour((uint8_t)m_globalChanId));
        g.drawText(valueText, textBounds.toNearestInt(), juce::Justification::centred, false);
    }
}

void ChannelComponent::paintCentreLine(juce::Graphics& g)
{
    auto* lnf = &m_debugger.getLookAndFeel();
    const auto colour = lnf->findColour(AudioDebuggerLookAndFeel::waveformSeparatorColourId);
    g.setColour(colour.withAlpha(0.2f));
    g.drawHorizontalLine(getHeight() / 2, 0.0f, (float)getWidth());
}

} // namespace AudioDebug
