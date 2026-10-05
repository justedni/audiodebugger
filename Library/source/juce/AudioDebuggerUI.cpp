// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "juce/AudioDebuggerUI.h"

#include "core/DebuggerAccess.h"
#include "core/AudioDebugInterface.h"
#include "core/ReportFile.h"

#include "juce/CustomSlider.h"
#include "juce/ControlPanel.h"
#include "core/DiffDetector.h"
#include "juce/MarkerStore.h"
#include "juce/MipmapBuilder.h"
#include "juce/Mipmaps.h"
#include "juce/WaveformOverview.h"
#include "juce/ChannelComponent.h"
#include "juce/OverlayComponent.h"
#include "juce/MidiTrackComponent.h"
#include "juce/DiffTrackComponent.h"

#include <assert.h>

namespace AudioDebug {

class AutoConnectButton : public juce::TextButton
{
public:
    enum State : uint8_t
    {
        Disconnected = 0,
        Armed,
        Connected
    };

    AutoConnectButton(AudioDebuggerUI* audioDebuggerUI)
        : m_audioDebugger(audioDebuggerUI)
    {
        updateButtonText();

        onClick = [this]()
        {
            switch (m_state)
            {
            case State::Disconnected:
            {
                m_state = State::Armed;
                break;
            }
            case State::Armed: juce::Colours::orange;
            {
                if (auto* debugger = m_audioDebugger->getDebugger())
                    debugger->resetWriter();
                m_state = State::Disconnected;
                break;
            }
            case State::Connected: juce::Colours::red;
            {
                m_audioDebugger->triggerCaptureEnd();
                if (auto* debugger = m_audioDebugger->getDebugger())
                    debugger->resetWriter();
                m_state = State::Disconnected;
                break;
            }
            }

            updateButtonText();
            repaint();
        };
    }

    ~AutoConnectButton()
    {
    }

    bool isArmed() const { return m_state == State::Armed; }
    bool isConnected() const { return m_state == State::Connected; }
    void setConnected()
    {
        m_state = State::Connected;
        updateButtonText();
        repaint();
    }
    void setDisconnected()
    {
        m_state = State::Disconnected;
        updateButtonText();
        repaint();
    }

    void updateButtonText()
    {
        juce::String text;
        switch (m_state)
        {
        case State::Disconnected: text = "Disconnected"; break;
        case State::Armed:  text = "Connect: waiting"; break;
        case State::Connected:  text = "Connected"; break;
        }

        setButtonText(text);
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = getLocalBounds().toFloat();

        juce::Colour backgroundColor;
        switch (m_state)
        {
        case State::Disconnected: backgroundColor = getLookAndFeel().findColour(juce::TextButton::buttonColourId); break;
        case State::Armed: backgroundColor = juce::Colours::orange; break;
        case State::Connected: backgroundColor = juce::Colours::red; break;
        }

        if (shouldDrawButtonAsHighlighted)
            backgroundColor = backgroundColor.brighter(0.1f);

        g.setColour(backgroundColor);
        g.fillRoundedRectangle(bounds, 4.0f);

        g.setColour(juce::Colours::black);
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

        g.setColour(getToggleState() ? juce::Colours::white : juce::Colours::black);
        g.setFont(14.0f);
        g.drawText(getButtonText(), bounds, juce::Justification::centred);
    }

private:
    State m_state = Armed;
    AudioDebuggerUI* m_audioDebugger = nullptr;
};

class MainMenuModel : public juce::MenuBarModel
{
public:
    MainMenuModel(AudioDebuggerUI* parent);

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int menuIndex) override;

    bool getDarkModeEnabled() const { return m_optionDarkMode; }
private:
    AudioDebuggerUI* m_parent = nullptr;
    bool m_optionDarkMode = false;
    bool m_optionAlwaysOnTop = false;
};

juce::Colour AudioDebuggerUI::colorIdToJuceColour(uint32_t id) const
{
    auto juceColor = juce::Colours::black;
    switch (id)
    {
    case Black: juceColor = juce::Colours::black; break;
    case PlainGrey: juceColor = juce::Colours::grey; break;
    case DottedGrey:juceColor = juce::Colours::grey; break;
    case Red: juceColor = juce::Colours::red; break;
    case Blue: juceColor = juce::Colours::blue; break;
    case Green:juceColor = juce::Colours::green; break;
    default: assert(false);
    }

    return juceColor;
}

class MainComponent : public juce::Component
                    , public juce::ScrollBar::Listener
{
public:
    MainComponent(AudioDebuggerUI* audioDebugger)
        : m_audioDebugger(audioDebugger)
    {
        m_menuBarModel = std::make_unique<MainMenuModel>(audioDebugger);
        m_menuBar = std::make_unique<juce::MenuBarComponent>(m_menuBarModel.get());
        addAndMakeVisible(m_menuBar.get());

        m_autoConnectButton = std::make_unique<AutoConnectButton>(audioDebugger);
        addAndMakeVisible(m_autoConnectButton.get());

        m_autoWriteButton.setButtonText("Write");
        m_autoWriteButton.setToggleState(true, juce::dontSendNotification);
        addAndMakeVisible(m_autoWriteButton);
        m_autoWriteButton.onClick = [this]()
        {
            auto state = m_autoWriteButton.getToggleState();
            m_audioDebugger->setAutoWriteRptFile(state);
        };

        m_secondsLabel.setText("secs", juce::dontSendNotification);
        addAndMakeVisible(m_secondsLabel);

        m_writeLengthSlider.setRange(-1.0, 3600.0, 1.0);
        m_writeLengthSlider.setValue(30.0);
        m_writeLengthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        addAndMakeVisible(m_writeLengthSlider);
        m_writeLengthSlider.onValueChange = [this]
        {
            int value = std::round(m_writeLengthSlider.getValue());
            m_audioDebugger->setRptFileMaxLength(value);
        };

        m_scrollbar.reset(new juce::ScrollBar(false));
        m_scrollbar->setAutoHide(false);
        m_scrollbar->addListener(this);
        addAndMakeVisible(m_scrollbar.get());

        m_controlPanel = audioDebugger->getControlPanel();
        addAndMakeVisible(m_controlPanel);

        addAndMakeVisible(m_audioDebugger);
        m_audioDebugger->setParentComponentPtr(this);

        m_sampleRateLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        m_sampleRateLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(m_sampleRateLabel);

        m_sampleIdLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        m_sampleIdLabel.setJustificationType(juce::Justification::centredLeft);
        m_sampleIdLabel.setText("Sample: ---", juce::dontSendNotification);
        addAndMakeVisible(m_sampleIdLabel);

        zoomSlider.reset(new juce::Slider());
        zoomSlider->setRange(1.0, audioDebugger->getMaxZoom(), 0.0);
        zoomSlider->setValue(1.0);
        zoomSlider->setSkewFactorFromMidPoint(100.0);
        zoomSlider->setTextBoxStyle(juce::Slider::NoTextBox, false, 100, 30);
        //zoomSlider->setLookAndFeel(m_lookAndFeel.get());
        zoomSlider->onValueChange = [this] { m_audioDebugger->setZoomLevel(zoomSlider->getValue()); };
        addAndMakeVisible(zoomSlider.get());

        addAndMakeVisible(m_zoomLabel);
        m_zoomLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        m_zoomLabel.setJustificationType(juce::Justification::centredLeft);
        m_zoomLabel.setText("H-Zoom:", juce::dontSendNotification);

        addAndMakeVisible(m_zoomText);
        m_zoomText.setFont(juce::Font(14.0f, juce::Font::bold));
        m_zoomText.setJustificationType(juce::Justification::centredLeft);
        m_zoomText.setText("------", juce::dontSendNotification);

        addAndMakeVisible(m_trackHeightLabel);
        m_trackHeightLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        m_trackHeightLabel.setJustificationType(juce::Justification::centredRight);
        m_trackHeightLabel.setText("Track H:", juce::dontSendNotification);

        addAndMakeVisible(m_trackHeightSlider);
        m_trackHeightSlider.setRange((double)AudioDebuggerUI::MIN_TRACK_HEIGHT,
                                     (double)AudioDebuggerUI::MAX_TRACK_HEIGHT, 5.0);
        m_trackHeightSlider.setValue((double)audioDebugger->getTrackHeight(), juce::dontSendNotification);
        m_trackHeightSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
        m_trackHeightSlider.onValueChange = [this]
        {
            m_audioDebugger->setTrackHeight((int)std::round(m_trackHeightSlider.getValue()));
        };

        addAndMakeVisible(m_diffThresholdLabel);
        m_diffThresholdLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        m_diffThresholdLabel.setText("Diff - Threshold:", juce::dontSendNotification);

        addAndMakeVisible(m_diffThresholdSlider);
        m_diffThresholdSlider.setRange(0.0, 50, 1.0);
        m_diffThresholdSlider.setValue(0);
        m_diffThresholdSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        m_diffThresholdSlider.onValueChange = [this] { m_audioDebugger->differenceThresholdChanged(m_diffThresholdSlider.getValue()); };

        m_diffMode.addItem("Full", DiffDetector::MaskPreset::Full + 1);
        m_diffMode.addItem("16-bit", DiffDetector::MaskPreset::Bits16 + 1);
        m_diffMode.onChange = [this]
        {
            if (auto* diffDetector = m_audioDebugger->getDiffDetector())
            {
                const int idx = m_diffMode.getSelectedId() - 1;
                if (idx >= 0 && idx <= (uint8_t)DiffDetector::MaskPreset::Count)
                {
                    diffDetector->setMaskPreset((DiffDetector::MaskPreset)idx);
                }
            }
        };
        addAndMakeVisible(m_diffMode);

        m_buttonPreviousDiff.reset(new juce::TextButton("Previous"));
        addAndMakeVisible(m_buttonPreviousDiff.get());
        m_buttonPreviousDiff->onClick = [this] { m_audioDebugger->goToDiff(AudioDebuggerUI::EDiffToGo::Previous); };

        m_buttonNextDiff.reset(new juce::TextButton("Next"));
        addAndMakeVisible(m_buttonNextDiff.get());
        m_buttonNextDiff->onClick = [this] { m_audioDebugger->goToDiff(AudioDebuggerUI::EDiffToGo::Next); };

        m_buttonDiffRefresh.reset(new juce::TextButton("Refresh"));
        addAndMakeVisible(m_buttonDiffRefresh.get());
        m_buttonDiffRefresh->onClick = [this] { m_audioDebugger->refreshGenFile(); };

        if (auto* overview = m_audioDebugger->getOverview())
            if (auto* rptFile = overview->getRptFile())
                updateSampleRateDisplay(rptFile->getHeader().sample_rate);

        setSize(600, 400);
    }

    ~MainComponent()
    {
        m_menuBar->setModel(nullptr);
    }

    void init()
    {
        zoomSlider->setValue(1.0, juce::dontSendNotification);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        auto menuBarBounds = bounds.removeFromTop(24);
        auto scrollBarArea = bounds.removeFromBottom(18);
        auto bottomBarBounds = bounds.removeFromBottom(20);
        auto panelArea = bounds.removeFromRight(150);

        m_menuBar->setBounds(menuBarBounds);
        m_autoConnectButton->setBounds(menuBarBounds.removeFromRight(120).reduced(2));
        m_secondsLabel.setBounds(menuBarBounds.removeFromRight(30));
        m_writeLengthSlider.setBounds(menuBarBounds.removeFromRight(140).reduced(2));
        m_autoWriteButton.setBounds(menuBarBounds.removeFromRight(60).reduced(2));

        m_scrollbar->setBounds(scrollBarArea);
        m_controlPanel->setBounds(panelArea);

        m_zoomLabel.setBounds(bottomBarBounds.removeFromLeft(56));
        zoomSlider->setBounds(bottomBarBounds.removeFromLeft(300));
        m_zoomText.setBounds(bottomBarBounds.removeFromLeft(340));
        bottomBarBounds.removeFromLeft(20);
        m_trackHeightLabel.setBounds(bottomBarBounds.removeFromLeft(60));
        m_trackHeightSlider.setBounds(bottomBarBounds.removeFromLeft(130));
        bottomBarBounds.removeFromLeft(30);
        m_diffThresholdLabel.setBounds(bottomBarBounds.removeFromLeft(100));
        m_diffThresholdSlider.setBounds(bottomBarBounds.removeFromLeft(100));
        m_diffMode.setBounds(bottomBarBounds.removeFromLeft(80));
        m_buttonPreviousDiff->setBounds(bottomBarBounds.removeFromLeft(50));
        m_buttonNextDiff->setBounds(bottomBarBounds.removeFromLeft(50));
        m_buttonDiffRefresh->setBounds(bottomBarBounds.removeFromLeft(70));

        m_sampleIdLabel.setBounds(bottomBarBounds.removeFromRight(120));
        m_sampleRateLabel.setBounds(bottomBarBounds.removeFromRight(100));

        m_audioDebugger->setBounds(bounds);
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colours::white);
        const int lineY = getHeight() - 18;
        g.drawLine(0, lineY, getWidth(), lineY);
    }

    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override
    {
        if (scrollBar == m_scrollbar.get())
        {
            m_audioDebugger->scrollBarMoved(newRangeStart);

            m_scrollbar->setCurrentRangeStart((double)newRangeStart, juce::dontSendNotification);

            repaint();
        }
    }

    void setZoomLevel(double value)
    {
        zoomSlider->setValue(value, juce::dontSendNotification);
    }

    void setZoomText(double zoomLevel, double verticalZoom, bool bIsUsingMipmaps, int mipmapLevel, double samplesPerPixel)
    {
        juce::String zoomLevelText;
        if (zoomLevel < 10.0)
            zoomLevelText = juce::String(zoomLevel, 1) + "x";
        else if (zoomLevel < 1000.0)
            zoomLevelText = juce::String((int)zoomLevel) + "x";
        else if (zoomLevel < 10000.0)
            zoomLevelText = juce::String(zoomLevel / 1000.0, 1) + "k";
        else
            zoomLevelText = juce::String((int)(zoomLevel / 1000.0)) + "k";

        juce::String info = zoomLevelText;
        info += " | V-Zoom: " + juce::String(verticalZoom, 1) + "x";
        if (bIsUsingMipmaps)
        {
            info += " | Mipmap level: " + juce::String(mipmapLevel);
            info += " | Samples per pixel: " + juce::String(samplesPerPixel);
        }
        else
        {
            info += " | Raw Samples";
        }

        m_zoomText.setText(info, juce::dontSendNotification);
    }

    void setGenerationButtonsEnabled(bool bEnable)
    {
        m_buttonPreviousDiff->setEnabled(bEnable);
        m_buttonNextDiff->setEnabled(bEnable);
        m_buttonDiffRefresh->setEnabled(bEnable);
    }

    void updateSampleDisplay(int64_t sample)
    {
        if (sample >= 0)
        {
            juce::String sampleText = juce::String(sample);

            juce::String formatted;
            int count = 0;
            for (int i = sampleText.length() - 1; i >= 0; --i) {
                if (count > 0 && count % 3 == 0)
                    formatted = " " + formatted;
                formatted = juce::String::charToString(sampleText[i]) + formatted;
                count++;
            }

            m_sampleIdLabel.setText("Sample: " + formatted, juce::dontSendNotification);
        }
        else {
            m_sampleIdLabel.setText("Sample: ---", juce::dontSendNotification);
        }
    }

    void updateSampleRateDisplay(int32_t rate)
    {
        m_sampleRateLabel.setText(juce::String::formatted("Rate: %dkHz", (int32_t)std::round(rate / 1000)), juce::dontSendNotification);
    }

    MainMenuModel* getMenuBarModel() const { return m_menuBarModel.get(); }
    AutoConnectButton* getAutoConnectButton() const { return m_autoConnectButton.get(); }
    juce::ScrollBar* getScrollbar() const { return m_scrollbar.get(); }
    ControlPanel* getControlPanel() const { return m_controlPanel; }

    int getDiffThreshold() const { return m_diffThresholdSlider.getValue(); }

    bool getAutoWriteRptFile() const { return m_autoWriteButton.getToggleState(); }
    void setAutoWriteRptFile(bool bSet) { m_autoWriteButton.setToggleState(bSet, juce::dontSendNotification); }

    int getRptFileMaxLength() const { return std::round(m_writeLengthSlider.getValue()); }
    void setRptFileMaxLength(int numSeconds) { m_writeLengthSlider.setValue(static_cast<double>(numSeconds), juce::dontSendNotification); }

private:
    AudioDebuggerUI* m_audioDebugger = nullptr;

    std::unique_ptr<juce::MenuBarComponent> m_menuBar;
    std::unique_ptr<MainMenuModel> m_menuBarModel;
    juce::Slider m_writeLengthSlider;
    juce::ToggleButton m_autoWriteButton;
    juce::Label m_secondsLabel;
    std::unique_ptr<AutoConnectButton> m_autoConnectButton;

    juce::Label m_zoomLabel;
    std::unique_ptr<juce::Slider> zoomSlider;
    juce::Label m_zoomText;
    juce::Label m_sampleRateLabel;
    juce::Label m_sampleIdLabel;

    juce::Label m_trackHeightLabel;
    juce::Slider m_trackHeightSlider;

    juce::Label m_diffThresholdLabel;
    juce::Slider m_diffThresholdSlider;
    juce::ComboBox m_diffMode;
    std::unique_ptr<juce::TextButton> m_buttonPreviousDiff;
    std::unique_ptr<juce::TextButton> m_buttonNextDiff;
    std::unique_ptr<juce::TextButton> m_buttonDiffRefresh;

    std::unique_ptr<juce::ScrollBar> m_scrollbar;
    ControlPanel* m_controlPanel = nullptr;
};


class ChannelViewport : public juce::Viewport
{
public:
    std::function<void()> onVisibleAreaChanged;

    void visibleAreaChanged(const juce::Rectangle<int>&) override
    {
        if (onVisibleAreaChanged)
            onVisibleAreaChanged();
    }
};


class ChannelList : public juce::Component
{
public:
    explicit ChannelList(AudioDebuggerUI& owner)
        : m_owner(owner)
    {
        setInterceptsMouseClicks(false, true);
    }

    void clear()
    {
        m_channels.clear();
        setSize(getWidth(), 0);
    }

    void addTrack(juce::String name, TrackComponent* track, juce::Colour accent)
    {
        auto cc = std::make_unique<CollapsibleChannel>(name, track, accent);
        cc->setTrackHeight(m_trackHeight);

        auto* raw = cc.get();
        cc->onToggled = [this, raw] { onTrackToggled(raw); };

        addAndMakeVisible(*cc);
        m_channels.push_back(std::move(cc));
        resized();
    }

    CollapsibleChannel* findByChanId(int globalChanId) const
    {
        if (globalChanId < 0)
            return nullptr;

        for (auto& c : m_channels)
            if (c->getGlobalChanId() == globalChanId)
                return c.get();

        return nullptr;
    }

    void setTrackHeight(int h)
    {
        if (m_trackHeight == h)
            return;

        m_trackHeight = h;

        for (auto& c : m_channels)
            c->setTrackHeight(h);

        resized();
    }

    void resized() override
    {
        const int w = getWidth();

        int y = 0;
        for (auto& c : m_channels)
        {
            const int h = c->getPreferredHeight();
            c->setBounds(0, y, w, h);
            y += h;
        }

        setSize(w, y);
    }

    void mouseMove(const juce::MouseEvent& e) override { m_owner.mouseMove(e.getEventRelativeTo(&m_owner)); }
    void mouseDown(const juce::MouseEvent& e) override { m_owner.mouseDown(e.getEventRelativeTo(&m_owner)); }
    void mouseDrag(const juce::MouseEvent& e) override { m_owner.mouseDrag(e.getEventRelativeTo(&m_owner)); }
    void mouseUp  (const juce::MouseEvent& e) override { m_owner.mouseUp  (e.getEventRelativeTo(&m_owner)); }
    void mouseExit(const juce::MouseEvent& e) override { m_owner.mouseExit(e.getEventRelativeTo(&m_owner)); }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (e.mods.isAltDown())
        {
            juce::Component::mouseWheelMove(e, wheel);
            return;
        }

        m_owner.mouseWheelMove(e.getEventRelativeTo(&m_owner), wheel);
    }

private:
    void onTrackToggled(CollapsibleChannel* toggled)
    {
        auto* viewport = findParentComponentOfClass<juce::Viewport>();

        const int yBefore = (viewport != nullptr)
            ? viewport->getLocalArea(toggled, toggled->getLocalBounds()).getY()
            : 0;

        resized();

        if (viewport != nullptr)
        {
            const int yAfter = viewport->getLocalArea(toggled, toggled->getLocalBounds()).getY();

            if (yAfter != yBefore)
            {
                viewport->setViewPosition(viewport->getViewPositionX(),
                    std::max(0, viewport->getViewPositionY() + (yAfter - yBefore)));
            }
        }

        m_owner.channelLayoutChanged();
    }

    AudioDebuggerUI& m_owner;
    std::vector<std::unique_ptr<CollapsibleChannel>> m_channels;
    int m_trackHeight = AudioDebuggerUI::DEFAULT_TRACK_HEIGHT;
};


//-----------------------------------------------------------------------------
AudioDebuggerUI::AudioDebuggerUI(DebugProcessor* processor)
    : m_processor(processor)
{
    m_controlPanel.reset(new ControlPanel(this, processor ? processor->getParameterInterface() : nullptr));

    m_lookAndFeel.reset(new AudioDebuggerLookAndFeel());
    setLookAndFeel(m_lookAndFeel.get());
    m_controlPanel->setPanelLookAndFeel(m_lookAndFeel.get());

    m_controlPanel->addOption(EDefaultOptions::BufferStartMarkers, "Buffer start markers", true, true);
    m_controlPanel->addOption(EDefaultOptions::CustomMarkers, "Custom markers", true, true);
    m_controlPanel->addOption(EDefaultOptions::SamplesAsRects, "Samples as rectangles", true, true);
    m_controlPanel->addOption(EDefaultOptions::SampleValues, "Sample values", true, true);

    zoomVSlider.reset(new juce::Slider(juce::Slider::SliderStyle::LinearVertical, juce::Slider::TextBoxBelow));
    zoomVSlider->setRange(1, 40, 0);
    zoomVSlider->setValue(1);
    zoomVSlider->setNumDecimalPlacesToDisplay(0);
    zoomVSlider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 30, 20);
    zoomVSlider->addListener(this);
    zoomVSlider->setLookAndFeel(m_lookAndFeel.get());
    zoomVSlider->setColour(juce::Slider::backgroundColourId, juce::Colours::grey.withAlpha(0.5f));
    addAndMakeVisible(zoomVSlider.get());

    if (auto* debugger = getDebugger())
    {
        m_numInputs = debugger->getNumInputs();
        m_numOutputs = debugger->getNumOutputs();
    }

    m_channelList.reset(new ChannelList(*this));

    auto viewport = std::make_unique<ChannelViewport>();
    viewport->onVisibleAreaChanged = [this] { channelLayoutChanged(); };
    viewport->setViewedComponent(m_channelList.get(), false);
    viewport->setScrollBarsShown(true, false);
    m_viewport = std::move(viewport);
    addAndMakeVisible(m_viewport.get());

    m_overlay = std::make_unique<OverlayComponent>(*this);
    addAndMakeVisible(m_overlay.get());
}

AudioDebuggerUI::~AudioDebuggerUI()
{
    if (m_channelList)
        m_channelList->clear();

    m_midiTrack.reset();
    m_diffTrack.reset();

    setLookAndFeel(nullptr);
}

void AudioDebuggerUI::initialize()
{
    auto* menuBarModel = m_parentComponent->getMenuBarModel();
    setAutoWriteRptFile(m_parentComponent->getAutoWriteRptFile());
    setRptFileMaxLength(m_parentComponent->getRptFileMaxLength());
    setUIDarkMode(menuBarModel->getDarkModeEnabled());

    getControlPanel()->updateControls();
    updateControlPanelValues();

    onMouseSampleChanged = [this](int64_t sample) {
        m_parentComponent->updateSampleDisplay(sample);
    };

    startTimerHz(60);
}

void AudioDebuggerUI::terminate()
{
    stopTimer();
}

int64_t AudioDebuggerUI::pixelToSample(float x, float componentWidth) const
{
    if (componentWidth <= 0.0f)
        return viewportStartSample;

    return viewportStartSample + (int64_t)((x / componentWidth) * (double)viewportLengthSamples);
}

float AudioDebuggerUI::sampleToPixel(int64_t sample, float componentWidth) const
{
    if (viewportLengthSamples <= 0)
        return 0.0f;

    return (float)((sample - viewportStartSample) / (double)viewportLengthSamples * componentWidth);
}

DiffDetector* AudioDebuggerUI::getDiffDetector() const
{
    return overview ? overview->getDiffDetector() : nullptr;
}

void AudioDebuggerUI::triggerCaptureEnd()
{
    if (overview)
        overview->stopLiveMonitoring();
    setAutoWriteRptFile(false);
}

ControlPanel* AudioDebuggerUI::getControlPanel() const { return m_controlPanel.get(); }

AudioDebugger* AudioDebuggerUI::getDebugger() const
{
    assert(m_processor);
    if (m_processor)
        return &DebugProcessor::DebuggerAccess::get(*m_processor);

    return nullptr;
}

void AudioDebuggerUI::addControlPanelOption(uint8_t id, const std::string& displayName, bool defaultEnabled, bool defaultVisible)
{
    getControlPanel()->addOption(id, displayName, defaultEnabled, defaultVisible);
}

void AudioDebuggerUI::addControlPanelButton(const juce::String& name)
{
    getControlPanel()->addButton(name);
}

void AudioDebuggerUI::addControlPanelSlider(int id, const CustomSlider& srcSlider, juce::LookAndFeel* lookAndFeel)
{
    getControlPanel()->addSlider(id, srcSlider, lookAndFeel);
}

void AudioDebuggerUI::addComponentToControlPanel(juce::Component* component)
{
    getControlPanel()->addAndMakeVisible(component);
}

void AudioDebuggerUI::updateParamsValues()
{
    getControlPanel()->updateControls();
    updateControlPanelValues();
}

std::pair<int, int> getMinMax(short* buffer, int start, int end)
{
    int count = end - start;
    if (count <= 0) {
        return { 0, 0 };
    }

    int min = 0, max = 0;
    for (int i = start; i < end; i++)
    {
        if (i == start || buffer[i] > max)
            max = buffer[i];
        if (i == start || buffer[i] < min)
            min = buffer[i];
    }

    return { min, max };
}

float linear_interpolation(float value, float inMin, float inMax, float outMin, float outMax)
{
    float normalized = (value - inMin) / (inMax - inMin);
    return outMin + normalized * (outMax - outMin);
}

bool almostEqual(int a, int b, int margin = 1)
{
    return std::abs(a - b) <= margin;
}

int64_t AudioDebuggerUI::getSampleAtPosition(int x) const
{
    if (!overview || getWidth() <= 0 || overview->getTotalSamples() <= 0)
        return -1;

    double ratio = x / (double)getWidth();
    int64_t sample = viewportStartSample + (int64_t)(viewportLengthSamples * ratio);

    return juce::jlimit((int64_t)0, overview->getTotalSamples() - 1, sample);
}

void AudioDebuggerUI::refreshChannelBounds()
{
    if (m_channelList)
    {
        m_channelList->setTrackHeight(m_trackHeight);
        m_channelList->resized();
    }

    channelLayoutChanged();
}

void AudioDebuggerUI::setTrackHeight(int heightPx)
{
    heightPx = juce::jlimit(MIN_TRACK_HEIGHT, MAX_TRACK_HEIGHT, heightPx);

    if (m_trackHeight == heightPx)
        return;

    m_trackHeight = heightPx;

    if (m_channelList)
        m_channelList->setTrackHeight(m_trackHeight);

    channelLayoutChanged();
}

int AudioDebuggerUI::chanIdToOutputIndex(int globalChanId) const
{
    if (globalChanId < m_numInputs)
        return -1;

    if (globalChanId < m_numInputs + m_numOutputs)
        return globalChanId - m_numInputs;

    return globalChanId - (m_numInputs + m_numOutputs);
}

int64_t AudioDebuggerUI::getTotalSamples() const { return overview->getTotalSamples(); }
int AudioDebuggerUI::getSampleRate() const { return overview->getSampleRate(); }
FileReader* AudioDebuggerUI::getRptFile() const  { return overview->getRptFile(); }
FileReader* AudioDebuggerUI::getGenFile() const  { return overview->getGenFile(); }


ChannelComponent* AudioDebuggerUI::getChannel(int globalChanId) const
{
    if (globalChanId < 0 || globalChanId >= (int)m_channels.size())
        return nullptr;

    return m_channels[(size_t)globalChanId].get();
}

CollapsibleChannel* AudioDebuggerUI::getCollapsibleChannel(int globalChanId) const
{
    return m_channelList ? m_channelList->findByChanId(globalChanId) : nullptr;
}

bool AudioDebuggerUI::isChannelExpanded(int globalChanId) const
{
    auto* cc = getCollapsibleChannel(globalChanId);
    return cc != nullptr && cc->isExpanded();
}

juce::Rectangle<int> AudioDebuggerUI::getChannelArea(int globalChanId) const
{
    auto* cc = getCollapsibleChannel(globalChanId);
    if (cc == nullptr || !cc->isExpanded())
        return {};

    auto* ch = cc->getTrack();
    if (ch == nullptr || ch->getHeight() <= 0)
        return {};

    const auto area = getLocalArea(ch, ch->getLocalBounds());
    return area.getIntersection(m_viewport ? m_viewport->getBounds() : getLocalBounds());
}

juce::Rectangle<int> AudioDebuggerUI::getChannelHeaderArea(int globalChanId) const
{
    auto* cc = getCollapsibleChannel(globalChanId);
    if (cc == nullptr)
        return {};

    auto header = cc->getLocalBounds().withHeight(CollapsibleChannel::getHeaderHeight());
    const auto area = getLocalArea(cc, header);

    return area.getIntersection(m_viewport ? m_viewport->getBounds() : getLocalBounds());
}

float AudioDebuggerUI::getChannelCentreY(int globalChanId) const
{
    const auto area = getChannelArea(globalChanId);
    return area.isEmpty() ? 0.0f : (float)area.getCentreY();
}

void AudioDebuggerUI::channelLayoutChanged()
{
    onChannelLayoutChanged();

    if (m_overlay)
        m_overlay->repaint();
}

const SampleCache* AudioDebuggerUI::getSampleCache(int globalChanId) const
{
    auto* ch = getChannel(globalChanId);
    return ch != nullptr ? ch->getSampleCache() : nullptr;
}

const juce::CriticalSection* AudioDebuggerUI::getSampleCacheLock(int globalChanId) const
{
    auto* ch = getChannel(globalChanId);
    return ch != nullptr ? &ch->getCacheLock() : nullptr;
}

void AudioDebuggerUI::ensureSampleCache()
{
    if (overview && viewportLengthSamples > 0 && shouldPaintDetailedUI())
        overview->updateSampleCache(viewportStartSample, viewportLengthSamples, m_channels);
}

juce::Colour AudioDebuggerUI::getWaveformColour(uint8_t channelId) const
{
    if (channelId < m_numInputs)
        return juce::Colours::blue;
    else if (channelId < m_numInputs + m_numOutputs)
        return juce::Colours::grey;
    else
        return m_lookAndFeel->findColour(AudioDebuggerLookAndFeel::waveformOutColourId);
}

juce::String AudioDebuggerUI::getSampleValueString(float value, uint8_t bitDepth) const
{
    return juce::String(AudioDebugger::floatToInt(value, bitDepth));
}

float AudioDebuggerUI::getFontSize(double pixelsPerSample) const
{
    float fontSize = 9.0f;
    if (pixelsPerSample >= 50.0) {
        fontSize = 11.0f;
    }
    else if (pixelsPerSample >= 30.0) {
        fontSize = 10.0f;
    }
    else if (pixelsPerSample >= 15.0) {
        fontSize = 9.0f;
    }
    else {
        fontSize = 8.0f;
    }

    return fontSize;
}

void AudioDebuggerUI::paint(juce::Graphics& g)
{
    auto* lnf = m_lookAndFeel.get();
    g.fillAll(lnf->findColour(AudioDebuggerLookAndFeel::backgroundColourId));

    const auto textColour = lnf->findColour(AudioDebuggerLookAndFeel::markerLabelColourId);

    juce::ignoreUnused(textColour);

    if (!overview || viewportLengthSamples <= 0)
        return;

    {
        double samplesPerPixel = viewportLengthSamples / (double)getWidth();
        const double verticalZoom = zoomVSlider->getValue();
        const int mipmapLevel = overview->getMipMapBuilder()->selectBestLevel(samplesPerPixel, 0);
        const bool bIsUsingMipmaps = (samplesPerPixel > RAW_SAMPLE_THRESHOLD);
        m_parentComponent->setZoomText(zoomLevel, verticalZoom, bIsUsingMipmaps, mipmapLevel, samplesPerPixel);
    }

    ensureSampleCache();
}

bool AudioDebuggerUI::shouldPaintDetailedUI() const
{
    double samplesPerPixel = viewportLengthSamples / (double)getWidth();
    double pixelsPerSample = getWidth() / (double)viewportLengthSamples;

    return (samplesPerPixel <= RAW_SAMPLE_THRESHOLD && !overview->isLiveMonitoring() && !isLoadingFile());
}

bool AudioDebuggerUI::shouldPaintExtraDetailedUI() const
{
    double pixelsPerSample = getWidth() / (double)viewportLengthSamples;
    static constexpr double SAMPLE_VALUE_DISPLAY_THRESHOLD = 20.0;

    return (pixelsPerSample >= SAMPLE_VALUE_DISPLAY_THRESHOLD);
}

void AudioDebuggerUI::resized()
{
    auto bounds = getLocalBounds();

    zoomVSlider->setBounds(getWidth() - 20, getHeight() - 100, 20, 100);

    if (m_viewport)
    {
        m_viewport->setBounds(bounds);

        if (m_channelList)
        {
            m_channelList->setSize(m_viewport->getMaximumVisibleWidth(),
                                   m_channelList->getHeight());
        }
    }

    if (m_overlay)
    {
        m_overlay->setBounds(bounds);
        m_overlay->toFront(false);
    }
}

void AudioDebuggerUI::sliderValueChanged(juce::Slider* slider)
{
    if (slider == zoomVSlider.get())
    {
        repaint();
    }
}

void AudioDebuggerUI::scrollBarMoved(double newRangeStart)
{
    if (!overview)
        return;

    int64_t currentLength = viewportLengthSamples;

    viewportStartSample = (int64_t)newRangeStart;

    viewportStartSample = juce::jlimit((int64_t)0,
        overview->getTotalSamples() - currentLength,
        viewportStartSample);

    viewportCenter = (viewportStartSample + currentLength / 2) /
        (double)overview->getTotalSamples();

    repaint();
}

void AudioDebuggerUI::autoConnectToLastFile()
{

}

void AudioDebuggerUI::timerCallback()
{
    auto* pDebugger = getDebugger();
    if (!pDebugger)
        return;

    pDebugger->serviceFromMessageThread();

    // Use the following code to auto-load a file if required
    auto* autoConnectButton = m_parentComponent->getAutoConnectButton();
    const bool bAutoConnect = autoConnectButton->isArmed();
    const bool bIsConnected = autoConnectButton->isConnected();
    if (bAutoConnect && pDebugger->getAsyncWriterStatusChanged())
    {
        const auto& filename = pDebugger->getLastFileName();
        if (!filename.empty())
        {
            auto fullPath = juce::File::getCurrentWorkingDirectory().getChildFile(filename);
            if (fullPath.existsAsFile())
            {
                loadAudioFile(fullPath, true);
                autoConnectButton->setConnected();
            }
        }
    }
    else if (bIsConnected && pDebugger->getAsyncWriterGotDisconnected())
    {
        autoConnectButton->setDisconnected();
        triggerCaptureEnd();
    }

    if (overview)
    {
        bool overviewLoading = (overview->isLoading());
        if (overviewLoading != m_isLoading)
        {
            repaint();

            if (!overviewLoading)
            {
                setGenerationButtonsEnabled(true);
            }

            m_isLoading = overviewLoading;
            m_repaintedRegenSamples = 0;
        }
        // Below can be enabled for real-time refresh but is a bit slow
        else if (overview->getLiveRefreshRequired())
        {
            if (m_midiTrack)
                m_midiTrack->invalidateCache();

            if (overview->isLiveMode())
            {
                updateVisibleRange();
                repaint();
            }
            else
            {
                const float width = (float)getWidth();
                int32_t lastRepaint = m_repaintedRegenSamples;
                int32_t endRepaint = overview->getRegenSamplesProcessed();

                const float x1 = sampleToPixel(lastRepaint, width);
                const float x2 = sampleToPixel(endRepaint, width);
                const int left = std::max(0, (int)std::floor(x1) - 1);
                const int right = std::min((int)width, (int)std::ceil(x2) + 1);

                if (right > left)
                {
                    for (int out = 0; out < (int)m_numOutputs; ++out)
                    {
                        const int chanId = getRegenChanId(out);

                        if (auto* channel = getChannel(chanId))
                            if (isChannelExpanded(chanId))
                                channel->repaint(left, 0, right - left, channel->getHeight());
                    }

                    if (m_diffTrack)
                        m_diffTrack->repaint(left, 0, right - left, m_diffTrack->getHeight());
                }

                m_repaintedRegenSamples = endRepaint;
            }
        }
    }
}

void AudioDebuggerUI::mouseMove(const juce::MouseEvent& event)
{
    currentMousePos = event.position;

    m_sampleAtCursor = getSampleAtPosition(event.getPosition().x);

    if (onMouseSampleChanged)
    {
        onMouseSampleChanged(m_sampleAtCursor);
    }
}

void AudioDebuggerUI::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (overview)
    {
        auto modifiers = event.mods;

        if (modifiers.isCtrlDown())
        {
            double mouseRatio = event.getPosition().x / (double)getWidth();
            int64_t sampleUnderMouse = viewportStartSample + (int64_t)(viewportLengthSamples * mouseRatio);

            double coeff = 1.0;
            const bool zoomingOut = (wheel.deltaY < 0);

            if (zoomLevel < 10.0) {
                coeff = 0.3;
            }
            else if (zoomLevel < 100.0) {
                coeff = 0.5;
            }
            else if (zoomLevel < 1000.0) {
                coeff = 0.8;
            }
            else if (zoomLevel < 10000.0) {
                coeff = 1.5;
            }
            else {
                coeff = 3.0;
            }

            double zoomFactor = 1.0 + (wheel.deltaY * coeff);
            zoomFactor = juce::jmax(zoomFactor, 0.7);

            double oldZoom = zoomLevel;
            setZoomLevel(zoomLevel * zoomFactor);

            m_parentComponent->setZoomLevel(zoomLevel);

            if (oldZoom != zoomLevel)
            {
                viewportCenter = sampleUnderMouse / (double)overview->getTotalSamples();
                updateVisibleRange();

                int64_t currentSampleAtMouse = viewportStartSample + (int64_t)(viewportLengthSamples * mouseRatio);
                int64_t sampleDiff = sampleUnderMouse - currentSampleAtMouse;

                viewportStartSample += sampleDiff;
                viewportStartSample = juce::jlimit((int64_t)0, overview->getTotalSamples() - viewportLengthSamples, viewportStartSample);

                viewportCenter = (viewportStartSample + viewportLengthSamples / 2) / (double)overview->getTotalSamples();

                repaint();
            }
        }
        else if (modifiers.isShiftDown())
        {
            auto currZoomVal = zoomVSlider->getValue();
            zoomVSlider->setValue(currZoomVal + wheel.deltaY, juce::NotificationType::dontSendNotification);
            repaint();
        }
        else
        {
            double scrollAmount = wheel.deltaY * viewportLengthSamples * 0.1;

            int64_t newStartSample = viewportStartSample - (int64_t)scrollAmount;

            if (newStartSample == viewportStartSample)
            {
                if (scrollAmount < 0.0) scrollAmount = -1.0;
                else if (scrollAmount > 0.0) scrollAmount = 1.0;

                newStartSample = viewportStartSample - (int64_t)scrollAmount;
            }

            newStartSample = juce::jlimit((int64_t)0,
                overview->getTotalSamples() - viewportLengthSamples,
                newStartSample);

            viewportStartSample = newStartSample;
            viewportCenter = (viewportStartSample + viewportLengthSamples / 2) /
                (double)overview->getTotalSamples();

            repaint();

            m_parentComponent->getScrollbar()->setCurrentRange((double)viewportStartSample, (double)(viewportLengthSamples), juce::dontSendNotification);
        }
    }
}

void AudioDebuggerUI::mouseDrag(const juce::MouseEvent& e)
{
    if (!isDragging || !overview)
        return;

    int dx = e.getPosition().x - lastMousePos.x;
    lastMousePos = e.getPosition();

    double pixelsPerSample = getWidth() / (double)viewportLengthSamples;
    double samplesDelta = -dx / pixelsPerSample;

    int64_t newStartSample = viewportStartSample + (int64_t)samplesDelta;

    setNewStartSample(newStartSample);

    repaint();
}

void AudioDebuggerUI::setNewStartSample(int64_t newStartSample)
{
    newStartSample = juce::jlimit((int64_t)0,
        overview->getTotalSamples() - viewportLengthSamples,
        newStartSample);

    viewportStartSample = newStartSample;
    viewportCenter = (viewportStartSample + viewportLengthSamples / 2) /
        (double)overview->getTotalSamples();

    m_parentComponent->getScrollbar()->setCurrentRangeStart((double)viewportStartSample, juce::dontSendNotification);
}

bool AudioDebuggerUI::isSampleWithinVisibleRange(int64_t sampleId)
{
    return (sampleId >= viewportStartSample && sampleId <= viewportStartSample + viewportLengthSamples);
}

void AudioDebuggerUI::setZoomLevel(double newZoom) {
    zoomLevel = juce::jlimit(MIN_ZOOM, MAX_ZOOM, newZoom);
    updateVisibleRange();
    repaint();
}

void AudioDebuggerUI::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isRightButtonDown())
    {
        juce::PopupMenu menu;

        menu.addItem(1, "Copy sample ID to clipboard");
        menu.addItem(2, "Debug this sample");

        menu.showMenuAsync(juce::PopupMenu::Options(),
            [&](int result)
            {
                if (result == 1)
                {
                    juce::SystemClipboard::copyTextToClipboard(juce::String(m_sampleAtCursor));
                }
                else if (result == 2)
                {
                    regenerateOutput(m_sampleAtCursor);
                }
            }
        );
    }
    else if (event.mods.isLeftButtonDown())
    {
        lastMousePos = event.getPosition();
        highlightedDiff = getSampleAtPosition(lastMousePos.x);
        isDragging = true;
    }
}

void AudioDebuggerUI::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    isDragging = false;
}

void AudioDebuggerUI::mouseExit(const juce::MouseEvent& e)
{
    if (onMouseSampleChanged) {
        onMouseSampleChanged(-1);
    }
}

void AudioDebuggerUI::differenceThresholdChanged(int threshold)
{
    if (auto* diffDetector = getDiffDetector())
    {
        diffDetector->setThreshold(threshold);
    }
}

bool AudioDebuggerUI::getOption(uint8_t id) const
{
    return getControlPanel()->isOptionEnabled(id);
}

void AudioDebuggerUI::goToDiff(EDiffToGo type)
{
    auto* diffDetector = getDiffDetector();
    if (!diffDetector)
        return;

    auto differences = diffDetector->getAll();

    if (differences.empty())
        return;

    switch (type)
    {
    case EDiffToGo::Previous:
    {
        for (auto it = differences.rbegin(); it != differences.rend(); ++it)
        {
            auto& diff = *it;
            if (diff.endSample < highlightedDiff)
            {
                highlightedDiff = diff.endSample;
                break;
            }
        }
        break;
    }
    case EDiffToGo::Next:
    {
        for (const auto& diff : differences)
        {
            if (diff.startSample > highlightedDiff)
            {
                highlightedDiff = diff.startSample;
                break;
            }
        }

        break;
    }
    default:
        jassert(false);
    }

    if (!isSampleWithinVisibleRange(highlightedDiff))
    {
        int64_t newStart = highlightedDiff - (viewportLengthSamples / 8);
        setNewStartSample(newStart);
    }

    repaint();
}

void AudioDebuggerUI::setGenerationButtonsEnabled(bool bEnable)
{
    getControlPanel()->setGenerationButtonsEnabled(bEnable);
    m_parentComponent->setGenerationButtonsEnabled(bEnable);
}


void AudioDebuggerUI::regenerateOutput(int32_t sampleItToDebug)
{
    auto* autoConnectButton = m_parentComponent->getAutoConnectButton();
    if (autoConnectButton->isConnected())
    {
        autoConnectButton->setDisconnected();
        triggerCaptureEnd();
    }

    {
        for (auto& channel : m_channels)
        {
            channel->clearCache();
        }

        if (m_midiTrack)
            m_midiTrack->invalidateCache();

        invalidateCustomCache_();
    }


    if (auto* debugger = getDebugger())
    {
        debugger->setSampleToDebug(sampleItToDebug);
    }

    setGenerationButtonsEnabled(false);
    overview->startOutputRegeneration();
}

bool AudioDebuggerUI::loadAudioFile(const juce::File& file, bool liveMode)
{
    if (!file.existsAsFile())
    {
        overview.reset();
        setGenerationButtonsEnabled(false);
        return false;
    }

    DiffDetector::MaskPreset diffMaskPreset = DiffDetector::MaskPreset::Full;

    if (auto* oldDiffDetector = getDiffDetector()) {
        diffMaskPreset = oldDiffDetector->getMaskPreset();
    }

    overview.reset(new WaveformOverview(m_processor));

    zoomLevel = 1.0;

    m_parentComponent->init();
    zoomVSlider->setValue(1.0, juce::dontSendNotification);
    viewportCenter = 0.5;

    auto* diffDetector = overview->getDiffDetector();
    diffDetector->setMaskPreset(diffMaskPreset);
    diffDetector->setThreshold(m_parentComponent->getDiffThreshold());

    bool bRet = overview->openRptFile(file.getFullPathName(), zoomLevel, getWidth(), liveMode);

    if (!bRet) {
        overview.reset();
        setGenerationButtonsEnabled(false);
        return false;
    }

    setGenerationButtonsEnabled(true);

    auto& header = overview->getRptFile()->getHeader();
    m_numInputs = header.in_channels;
    m_numOutputs = header.out_channels;

    m_channelList->clear();
    m_midiTrack.reset();
    m_diffTrack.reset();

    const int totalChannels = getTotalChannels();
    m_channels.clear();
    m_channels.reserve(totalChannels);
    for (int i = 0; i < totalChannels; ++i)
    {
        m_channels.push_back(std::make_unique<ChannelComponent>(*this, i));
    }

    m_midiTrack = std::make_unique<MidiTrackComponent>(*this);
    m_channelList->addTrack("MIDI", m_midiTrack.get(), juce::Colours::mediumpurple);

    for (int i = 0; i < m_numInputs; i++)
    {
        const int chanId = getInputChanId(i);
        m_channelList->addTrack(juce::String::formatted("In %d", i + 1),
                                m_channels[(size_t)chanId].get(),
                                getWaveformColour((uint8_t)chanId));
    }
    for (int i = 0; i < m_numOutputs; i++)
    {
        const int chanId = getOutputChanId(i);
        m_channelList->addTrack(juce::String::formatted("Out %d", i + 1),
                                m_channels[(size_t)chanId].get(),
                                getWaveformColour((uint8_t)chanId));
    }

    m_diffTrack = std::make_unique<DiffTrackComponent>(*this);
    m_channelList->addTrack("Differences", m_diffTrack.get(), juce::Colours::orange);

    for (int i = 0; i < m_numOutputs; i++)
    {
        const int chanId = getRegenChanId(i);
        m_channelList->addTrack(juce::String::formatted("Regen Out %d", i + 1),
                                m_channels[(size_t)chanId].get(),
                                getWaveformColour((uint8_t)chanId));
    }

    m_parentComponent->updateSampleRateDisplay(header.sample_rate);

    if (auto* debugger = getDebugger())
    {
        debugger->setNumInputsOutputs(m_numInputs, m_numOutputs);
    }

    resized();
    refreshChannelBounds();

    updateVisibleRange();
    repaint();

    return true;
}

bool AudioDebuggerUI::isLoadingFile() const
{
    return (!overview->isLiveMode() && overview->isLoading());
}

int64_t AudioDebuggerUI::calculateViewportLength(double zoom) const
{
    if (!overview)
        return 0;

    int64_t totalSamples = overview->getTotalSamples();

    double normalizedZoom = zoom / MIN_ZOOM;

    double logMin = std::log(MIN_VIEWPORT_SAMPLES);
    double logMax = std::log(totalSamples);
    double logZoom = std::log(normalizedZoom);
    double maxLogZoom = std::log(MAX_ZOOM / MIN_ZOOM);

    double t = logZoom / maxLogZoom;
    double logViewportLength = logMax - t * (logMax - logMin);

    int64_t viewportLength = (int64_t)std::exp(logViewportLength);

    viewportLength = std::max(MIN_VIEWPORT_SAMPLES, viewportLength);
    viewportLength = std::min(totalSamples, viewportLength);

    return viewportLength;
}

double AudioDebuggerUI::calculateZoomFromViewportLength(int64_t viewportLength) const
{
    if (!overview)
        return MIN_ZOOM;

    int64_t totalSamples = overview->getTotalSamples();

    viewportLength = std::max(MIN_VIEWPORT_SAMPLES, viewportLength);
    viewportLength = std::min(totalSamples, viewportLength);

    double logMin = std::log(MIN_VIEWPORT_SAMPLES);
    double logMax = std::log(totalSamples);
    double logViewportLength = std::log(viewportLength);

    double t = (logMax - logViewportLength) / (logMax - logMin);
    double maxLogZoom = std::log(MAX_ZOOM / MIN_ZOOM);
    double logZoom = t * maxLogZoom;

    double zoom = std::exp(logZoom) * MIN_ZOOM;

    return juce::jlimit(MIN_ZOOM, MAX_ZOOM, zoom);
}

void AudioDebuggerUI::updateVisibleRange()
{
    if (!overview)
        return;

    int64_t totalSamples = overview->getTotalSamples();

    viewportLengthSamples = calculateViewportLength(zoomLevel);

    int64_t centerSample = (int64_t)(totalSamples * viewportCenter);
    viewportStartSample = centerSample - (viewportLengthSamples / 2);

    // Clamp to valid range
    viewportStartSample = std::max((int64_t)0, viewportStartSample);
    if (viewportStartSample + viewportLengthSamples > totalSamples) {
        viewportStartSample = totalSamples - viewportLengthSamples;
    }
    viewportStartSample = std::max((int64_t)0, viewportStartSample);

    auto* scrollbar = m_parentComponent->getScrollbar();
    scrollbar->setRangeLimits(0.0, (double)totalSamples, juce::dontSendNotification);
    scrollbar->setCurrentRange((double)viewportStartSample, (double)(viewportLengthSamples), juce::dontSendNotification);
}


void AudioDebuggerUI::importbinReport()
{
    m_fileChooser = std::make_unique<juce::FileChooser>("Select a previously exported bin report",
        juce::File(),
        "*.rpt");

    auto folderChooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    m_fileChooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser& chooser)
    {
        auto selected = chooser.getResult();
        if (selected.existsAsFile())
        {
            m_lastSelectedFile = selected;
            loadAudioFile(selected, false);
        }
    });
}

void AudioDebuggerUI::importgenFile()
{
    m_fileChooser = std::make_unique<juce::FileChooser>("Select a regenerated report",
        juce::File(),
        "*.gen");

    auto folderChooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    m_fileChooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser& chooser)
    {
        auto selected = chooser.getResult();
        if (selected.existsAsFile())
        {
            overview->startOpenGenFile(selected.getFullPathName());
        }
    });
}

void AudioDebuggerUI::refreshGenFile()
{
    overview->reopenSameGenFile();
}

void AudioDebuggerUI::exportbinReport()
{
    juce::File sourceFile;
    if (auto* debugger = getDebugger())
    {
        const auto& filename = debugger->getLastFileName();
        if (!filename.empty())
        {
            sourceFile = juce::File::getCurrentWorkingDirectory().getChildFile(filename);
        }
    }

    if (!sourceFile.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Session file not found",
            "There is no current session to export!",
            "OK"
        );
        return;
    }

    m_fileChooser = std::make_unique<juce::FileChooser>("Export current report view to a bin file",
        juce::File(),
        "*.rpt");

    auto folderChooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;

    m_fileChooser->launchAsync(folderChooserFlags, [this, sourceFile](const juce::FileChooser& chooser)
    {
        auto result = chooser.getResult();
        if (result.getFileName().isNotEmpty())
        {
            bool bSuccess = sourceFile.copyFileTo(result.getFullPathName());
            if (!bSuccess)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Error",
                    "There was an error while creating the file.",
                    "OK"
                );
            }
        }
    });
}

void AudioDebuggerUI::restoreFileState(bool bIsGen)
{
    auto* file = bIsGen ? overview->getGenFile() : overview->getRptFile();
    if (file && file->hasPluginState())
    {
        AudioDebug::PluginState state;
        if (file->readPluginState(state))
        {
            const uint32_t expectedId = m_processor->getStateId();
            const bool bSamePlugin = (expectedId == 0 || state.plugin_id == 0 || state.plugin_id == expectedId);

            if (bSamePlugin)
                m_processor->setStateInformationHook(state.data.data(), state.data.size());
            else
                assert(false); // trace was recorded from a different plugin
        }
    }
}

void AudioDebuggerUI::setAutoWriteRptFile(bool bEnable)
{
    m_parentComponent->setAutoWriteRptFile(bEnable);
    if (auto* debugger = getDebugger())
        debugger->setAutoWriteOutputFile(bEnable);
}

void AudioDebuggerUI::setRptFileMaxLength(int numSeconds)
{
    m_parentComponent->setRptFileMaxLength(numSeconds);
    if (auto* debugger = getDebugger())
        debugger->setRptFileMaxLength(numSeconds);
}

void AudioDebuggerUI::setUIDarkMode(bool bEnable)
{
    m_lookAndFeel->setDarkMode(bEnable);
    repaint();
}

void AudioDebuggerUI::setSliderValue(uint8_t id, float value)
{
    getControlPanel()->setSliderValue(id, value);
}

void AudioDebuggerUI::setButtonEnabled(uint8_t id, bool bEnabled)
{
    getControlPanel()->setButtonEnabled(id, bEnabled);
}

//-----------------------------------------------------------------------------
MainMenuModel::MainMenuModel(AudioDebuggerUI* parent)
    : m_parent(parent)
{
}

juce::StringArray MainMenuModel::getMenuBarNames()
{
    return { "File", "Options", "Plugin"};
}

juce::PopupMenu MainMenuModel::getMenuForIndex(int menuIndex, const juce::String& menuName)
{
    juce::PopupMenu menu;

    if (menuIndex == 0) // File menu
    {
        menu.addItem(IMPORT_RPT, "Import standard .rpt report");
        menu.addItem(IMPORT_GEN, "Import Unit Test .gen report");
        menu.addItem(EXPORT, "Export .rpt");
        menu.addSeparator();
    }
    else if (menuIndex == 1)
    {
        menu.addItem(DARKMODE, "Dark mode", true, m_optionDarkMode);
        menu.addItem(ALWAYSONTOP, "Always on top", true, m_optionAlwaysOnTop);
    }
    else if (menuIndex == 2)
    {
        menu.addItem(SET_RPT_STATE, "Restore state from .rpt file");
        menu.addItem(SET_GEN_STATE, "Restore state from .gen file");
    }

    return menu;
}

void MainMenuModel::menuItemSelected(int menuItemID, int menuIndex)
{
    switch (menuItemID)
    {
    case IMPORT_RPT:
    {
        m_parent->importbinReport();
        break;
    }
    case IMPORT_GEN:
    {
        m_parent->importgenFile();
        break;
    }
    case EXPORT:
    {
        m_parent->exportbinReport();
        break;
    }
    case DARKMODE:
    {
        m_optionDarkMode = !m_optionDarkMode;
        m_parent->setUIDarkMode(m_optionDarkMode);
        menuItemsChanged();
        break;
    }
    case ALWAYSONTOP:
    {
        m_optionAlwaysOnTop = !m_optionAlwaysOnTop;
        if (auto* top = m_parent->getTopLevelComponent())
            top->setAlwaysOnTop(m_optionAlwaysOnTop);
        break;
    }
    case SET_RPT_STATE:
    {
        m_parent->restoreFileState(false);
        break;
    }
    case SET_GEN_STATE:
    {
        m_parent->restoreFileState(true);
        break;
    }
    }
}

//-----------------------------------------------------------------------------
AudioDebuggerLookAndFeel::AudioDebuggerLookAndFeel()
{
    setDarkMode(true);
}

void AudioDebuggerLookAndFeel::setDarkMode(bool bEnable)
{
    if (bEnable)
    {
        setColour(juce::Label::textColourId, juce::Colours::white);

        setColour(backgroundColourId, juce::Colour(0xff1a1a1a));
        setColour(waveformSeparatorColourId, juce::Colours::white);
        setColour(waveformOutColourId, juce::Colours::white);
        setColour(markerLabelColourId, juce::Colours::white);
        setColour(markerOutlineColourId, juce::Colours::black);
    }
    else
    {
        setColour(juce::Label::textColourId, juce::Colours::black);

        setColour(backgroundColourId, juce::Colours::white);
        setColour(waveformSeparatorColourId, juce::Colours::black);
        setColour(waveformOutColourId, juce::Colours::black);
        setColour(markerLabelColourId, juce::Colours::black);
        setColour(markerOutlineColourId, juce::Colours::white);
    }

    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::black);
    setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::white);
    setColour(juce::Slider::backgroundColourId, juce::Colours::white);
    setColour(juce::ToggleButton::textColourId, juce::Colours::black);
    setColour(juce::ToggleButton::tickColourId, juce::Colours::black);
}

//-----------------------------------------------------------------------------
WaveformPopup::WaveformPopup(const juce::String& name, AudioDebuggerUI* audioDebugger, std::function<void()> onCloseCallback)
    : DocumentWindow(name, juce::Colours::darkgrey, DocumentWindow::allButtons)
    , m_onCloseCallback(std::move(onCloseCallback))
    , m_audioDebugger(audioDebugger)
{
    setUsingNativeTitleBar(false);

    m_mainComponent.reset(new MainComponent(audioDebugger));

    assert(m_audioDebugger);
    m_audioDebugger->initialize();

    centreWithSize(800, 600);
    setContentNonOwned(m_mainComponent.get(), true);
    setResizable(true, true);

    setVisible(true);
}

WaveformPopup::~WaveformPopup()
{
    setMenuBar(nullptr);

    m_audioDebugger->terminate();
}

void WaveformPopup::closeButtonPressed()
{
    setVisible(false);

    if (m_onCloseCallback)
        m_onCloseCallback();
}

} // namespace AudioDebug
