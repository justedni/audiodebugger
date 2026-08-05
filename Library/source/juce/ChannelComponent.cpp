// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "juce/ChannelComponent.h"

#include "juce/MipmapBuilder.h"
#include "juce/Mipmaps.h"
#include "juce/AudioDebuggerUI.h"
#include "juce/WaveformOverview.h"
#include "core/ReportFile.h"
#include "core/DiffDetector.h"

namespace AudioDebug {

static float clampF(float n, float lower, float upper)
{
    return std::max(lower, std::min(n, upper));
}

ChannelComponent::ChannelComponent(AudioDebuggerUI& debugger, int globalChanId)
    : m_debugger(debugger)
    , m_globalChanId(globalChanId)
    , zoomVSlider(juce::Slider(juce::Slider::SliderStyle::LinearVertical, juce::Slider::TextBoxBelow))
{
    zoomVSlider.setRange(0.0, 2.0, 0.1);
    zoomVSlider.setValue(1.0);
    zoomVSlider.setNumDecimalPlacesToDisplay(0);
    zoomVSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 30, 20);
    zoomVSlider.onValueChange = [this]() { repaint(); };
    zoomVSlider.setColour(juce::Slider::backgroundColourId, juce::Colours::grey.withAlpha(0.5f));
    addAndMakeVisible(&zoomVSlider);

    setInterceptsMouseClicks(false, true);
    setOpaque(false);
}

ChannelComponent::~ChannelComponent() = default;

void ChannelComponent::resized()
{
    const int sliderHeight = juce::jlimit(24, 80, getHeight() - 8);
    zoomVSlider.setBounds(getWidth() - 20,
                          (getHeight() - sliderHeight) / 2,
                          20,
                          sliderHeight);
}

void ChannelComponent::paint(juce::Graphics& g)
{
    if (m_debugger.getViewportLengthSamples() <= 0)
        return;

    m_debugger.ensureSampleCache();

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

    paintDifferences(g);

    m_debugger.paintTrackCustom(g, *this);

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
        const float scale = (height / 2.2f) * m_debugger.getGlobalVerticalZoom() * getVerticalZoom();

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

void ChannelComponent::clearCache()
{
    juce::ScopedLock lock(cacheLock);
    sampleCache.clear();
}

bool ChannelComponent::checkCacheUpdateRequired(int64_t viewportStartSample, int64_t viewportLengthSamples) const
{
    if (!sampleCache.isValid() ||
        !sampleCache.contains(viewportStartSample) ||
        !sampleCache.contains(viewportStartSample + viewportLengthSamples - 1))
    {
        return true;
    }

    return false;
}

void ChannelComponent::prepareCacheUpdateIfRequired(int64_t viewportStartSample, int64_t viewportLengthSamples, int64_t cacheStart, int64_t cacheLength)
{
    juce::ScopedLock lock(cacheLock);

    if (checkCacheUpdateRequired(viewportStartSample, viewportLengthSamples))
    {
        sampleCache.clear();
        sampleCache.startSample = cacheStart;
        sampleCache.numSamples = cacheLength;

        sampleCache.samples.resize((size_t)cacheLength);
    }
}

void ChannelComponent::lockCache()
{
    cacheLock.enter();
}
void ChannelComponent::unlockCache()
{
    cacheLock.exit();
}

void ChannelComponent::paintWaveformFromRawSamples(juce::Graphics& g)
{
    juce::ScopedLock lock(cacheLock);

    if (!sampleCache.isValid() || sampleCache.samples.empty())
        return;

    const auto& chanSamples = sampleCache.samples;

    const int64_t viewportStartSample = m_debugger.getViewportStartSample();
    const int64_t viewportLengthSamples = m_debugger.getViewportLengthSamples();

    const float height = (float)getHeight();
    const float width = (float)getWidth();
    const float centreY = height / 2.0f;
    const float scale = (height / 2.2f) * m_debugger.getGlobalVerticalZoom() * getVerticalZoom();

    const bool bDrawRects = m_debugger.getOption(EDefaultOptions::SamplesAsRects);
    const double samplesPerPixel = m_debugger.getSamplesPerPixel();
    const bool bDisplayAsPath = true;// (samplesPerPixel < 2.0);

    g.setColour(m_debugger.getWaveformColour((uint8_t)m_globalChanId));

    juce::Path waveformPath;
    bool firstPoint = true;
    float lastY = 0.0f;

    for (int64_t i = 0; i < (int64_t)chanSamples.size(); ++i)
    {
        const int64_t sampleIndex = sampleCache.startSample + i;
        const float x = (float)((sampleIndex - viewportStartSample) / (double)viewportLengthSamples * width);

        if (x < -10.0f || x > width + 10.0f)
            continue;

        float y = centreY - (chanSamples[i] * scale);
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

    juce::ScopedLock lock(cacheLock);

    if (!sampleCache.isValid() || sampleCache.samples.empty())
        return;

    const auto& chanSamples = sampleCache.samples;

    const auto bitDepth = AudioDebugger::get()->getBitDepth();

    const int64_t viewportStart = m_debugger.getViewportStartSample();
    const int64_t viewportLen   = m_debugger.getViewportLengthSamples();
    const float width = (float)getWidth();
    const float centreY = getHeight() / 2.0f;
    const double pixelsPerSample = width / (double)viewportLen;

    float fontSize = m_debugger.getFontSize(pixelsPerSample);
    g.setFont(juce::Font(fontSize));

    const float halfWidth = (float)((1.0 / (double)viewportLen) * width) / 2.0f;

    for (int64_t i = 0; i < (int64_t)chanSamples.size(); ++i)
    {
        const int64_t sampleIndex = sampleCache.startSample + i;
        if (sampleIndex < viewportStart || sampleIndex >= viewportStart + viewportLen)
            continue;

        const float x = (float)((sampleIndex - viewportStart) / (double)viewportLen * width) + halfWidth;
        if (x < -50.0f || x > width + 50.0f)
            continue;

        const juce::String valueText = m_debugger.getSampleValueString(chanSamples[i], bitDepth);

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

void ChannelComponent::paintDifferences(juce::Graphics& g)
{
    auto* diffDetector = m_debugger.getDiffDetector();
    if (!diffDetector)
        return;

    const int outputIndex = m_debugger.chanIdToOutputIndex(m_globalChanId);
    if (outputIndex < 0)
        return;

    const int64_t viewportStartSample = m_debugger.getViewportStartSample();
    const int64_t viewportLengthSamples = m_debugger.getViewportLengthSamples();

    auto differences = diffDetector->getInRange(viewportStartSample, viewportStartSample + viewportLengthSamples);
    if (differences.empty())
        return;

    const int64_t highlightedDiff = m_debugger.getHighlightedDiff();
    const float width = (float)getWidth();
    const float height = (float)getHeight();

    for (const auto& diff : differences)
    {
        if (diff.channel != outputIndex)
            continue;

        float startX = (float)((diff.startSample - viewportStartSample) / (double)viewportLengthSamples * width);
        float endX = (float)((diff.endSample + 1 - viewportStartSample) / (double)viewportLengthSamples * width);

        if (endX - startX < 2.0f)
            endX = startX + 2.0f;

        startX = std::max(0.0f, startX);
        endX = std::min(width, endX);

        const bool bHighlighted = (highlightedDiff >= diff.startSample && highlightedDiff <= diff.endSample);

        g.setColour((bHighlighted ? juce::Colours::red : juce::Colours::orange).withAlpha(0.25f));
        g.fillRect(startX, 0.0f, endX - startX, height);
    }
}

void ChannelComponent::paintCentreLine(juce::Graphics& g)
{
    auto* lnf = &m_debugger.getLookAndFeel();
    const auto colour = lnf->findColour(AudioDebuggerLookAndFeel::waveformSeparatorColourId);
    g.setColour(colour.withAlpha(0.2f));
    g.drawHorizontalLine(getHeight() / 2, 0.0f, (float)getWidth());
}

//------------------------------------------------------------
ChannelHeaderButton::ChannelHeaderButton(const juce::String& name, juce::Colour accent)
    : juce::Button(name)
    , m_accent(accent)
{
    setButtonText(name);
    setClickingTogglesState(true);
    setToggleState(true, juce::dontSendNotification);
}

void ChannelHeaderButton::paintButton(juce::Graphics& g, bool shouldDrawHighlighted, bool shouldDrawDown)
{
    const bool isExpanded = getToggleState();

    auto r = getLocalBounds().toFloat().reduced(1.0f);

    auto body = m_accent.withMultipliedSaturation(0.7f).withMultipliedBrightness(isExpanded ? 0.45f : 0.28f);

    if (shouldDrawHighlighted)
        body = body.brighter(0.25f);
    if (shouldDrawDown)
        body = body.brighter(0.45f);

    g.setColour(body);
    g.fillRoundedRectangle(r, 3.0f);

    g.setColour(m_accent.withAlpha(shouldDrawHighlighted ? 0.9f : 0.6f));
    g.drawRoundedRectangle(r, 3.0f, 1.0f);

    g.setColour(m_accent);
    g.fillRect(r.getX() + 1.0f, r.getY() + 1.0f, 3.0f, r.getHeight() - 2.0f);

    const float cx = r.getX() + 14.0f;
    const float cy = r.getCentreY();
    constexpr float s = 4.0f;

    juce::Path triangle;
    if (isExpanded)
        triangle.addTriangle(cx - s, cy - s * 0.6f, cx + s, cy - s * 0.6f, cx, cy + s * 0.8f);
    else
        triangle.addTriangle(cx - s * 0.6f, cy - s, cx - s * 0.6f, cy + s, cx + s * 0.8f, cy);

    g.setColour(juce::Colours::white.withAlpha(isExpanded ? 0.95f : 0.65f));
    g.fillPath(triangle);

    auto textArea = r.withTrimmedLeft(24.0f).withTrimmedRight(4.0f).toNearestInt();
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.setColour(juce::Colours::white.withAlpha(isExpanded ? 0.95f : 0.6f));
    g.drawText(getButtonText(), textArea, juce::Justification::centredLeft, true);
}



CollapsibleChannel::CollapsibleChannel(juce::String name, TrackComponent* tr, juce::Colour accent)
    : header(name, accent)
    , track(tr)
    , accentColour(accent)
{
    jassert(track != nullptr);

    header.onClick = [this]
    {
        setExpanded(header.getToggleState());
    };
    addAndMakeVisible(header);
    addAndMakeVisible(*track);

    setInterceptsMouseClicks(false, true);
}

void CollapsibleChannel::setExpanded(bool shouldExpand)
{
    if (expanded == shouldExpand)
        return;

    expanded = shouldExpand;
    track->setVisible(expanded);
    header.setToggleState(expanded, juce::dontSendNotification);
    header.repaint();

    if (onToggled)
        onToggled();
    else if (auto* p = getParentComponent())
        p->resized();
}

void CollapsibleChannel::setTrackHeight(int h)
{
    trackHeight = std::max(minTrackHeight, h);
}

int CollapsibleChannel::getPreferredHeight() const
{
    if (!expanded)
        return headerHeight;

    const int h = (track != nullptr) ? track->computeTrackHeight(trackHeight) : trackHeight;
    return headerHeight + std::max(minTrackHeight, h);
}

void CollapsibleChannel::paint(juce::Graphics& g)
{
    g.setColour(accentColour.withAlpha(0.25f));
    g.drawRect(getLocalBounds(), 1);
}

void CollapsibleChannel::resized()
{
    auto r = getLocalBounds();
    header.setBounds(r.removeFromTop(headerHeight));

    if (expanded)
        track->setBounds(r);
    else
        track->setBounds(r.getX(), r.getY(), r.getWidth(), 0);
}

} // namespace AudioDebug
