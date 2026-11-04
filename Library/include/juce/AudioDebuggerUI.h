// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace AudioDebug {

class MainMenuModel;
class AudioDebugger;
class DebugProcessor;
class ControlPanel;
class CustomSlider;
class AutoConnectButton;
struct Marker;

struct FileHeader;
struct SampleCache;
class FileReader;
class ReportFile;

class WaveformOverview;
struct MarkerPair;

class MainComponent;
class ChannelList;
class ChannelComponent;
class CollapsibleChannel;
class MidiTrackComponent;
class DiffTrackComponent;
class OverlayComponent;
class DiffDetector;
class MipmapBuilder;

enum EDefaultOptions : uint8_t
{
    BufferStartMarkers = 0,
    CustomMarkers,
    SamplesAsRects,
    SampleValues,
};


class AudioDebuggerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    enum ColourIds
    {
        backgroundColourId = 0x1000001,
        waveformSeparatorColourId,
        waveformOutColourId,
        markerLabelColourId,
        markerOutlineColourId
    };

    AudioDebuggerLookAndFeel();
    ~AudioDebuggerLookAndFeel() override {}

    bool areScrollbarButtonsVisible() override { return true; }

    void setDarkMode(bool bEnable);
};

class AudioDebuggerUI : public juce::Component
                      , public juce::Timer
{
public:
    AudioDebuggerUI(DebugProcessor* processor);
    virtual ~AudioDebuggerUI();

    virtual void paintTrackCustom(juce::Graphics& g, ChannelComponent& track) {}
    virtual void paintOverlayCustom(juce::Graphics& g) {}
    virtual void paintCustomMarkers(juce::Graphics& g, const std::vector<MarkerPair>& markers) {}

    virtual void onChannelLayoutChanged() {}

    virtual void updateControlPanelValues() {}
    virtual void controlPanelResized(int x, int y, int width) {}
    virtual void setSliderFromDebugPanel(uint8_t id, float value) {}
    virtual void setButtonFromDebugPanel(uint8_t id, bool bEnabled) {}
    virtual void setOptionFromDebugPanel(uint8_t id, bool bEnabled) {}
    virtual void invalidateCustomCache_() {}
    virtual juce::Colour getWaveformColour(uint8_t channelId) const;

    virtual juce::String getSampleValueString(float value, uint8_t bitDepth) const;
    virtual int32_t getSampleValueForDiff(float value, uint8_t bitDepth) const;

    virtual float getFloatParam(uint8_t id) const { return 0.0f; }
    virtual bool getBoolParam(uint8_t id) const { return false; }
    virtual bool getBoolOption(uint8_t id) const { return false; }

    void refreshChannelBounds();

    // ------------------------------
    void initialize();
    void terminate();

    // ------------------------
    void addControlPanelOption(uint8_t id, const std::string& displayName, bool defaultEnabled, bool defaultVisible = true);
    void addControlPanelButton(const juce::String& name, bool defaultVisible);
    void addControlPanelSlider(int id, const CustomSlider& srcSlider, juce::LookAndFeel* lookAndFeel);
    void addComponentToControlPanel(juce::Component* component);

    void updateParamsValues();

    void paint(juce::Graphics& g) override;

    void resized() override;
    void horizontalScrollBarMoved(double newRangeStart);
    void verticalScrollBarMoved(double newRangeStart);
    void syncVerticalScrollbar();

    int64_t pixelToSample(float x, float componentWidth) const;
    float sampleToPixel(int64_t sample, float componentWidth) const;

    void timerCallback() override;

    void autoConnectToLastFile();
    void triggerCaptureEnd();

    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& e) override;

    void setNewStartSample(int64_t newStartSample);
    bool isSampleWithinVisibleRange(int64_t sampleId);

    void regenerateOutput(int32_t sampleItToDebug);
    void importbinReport();
    void importgenFile();
    void refreshGenFile();
    void exportbinReport();
    void clearCurrentSession();
    void restoreFileState(bool bIsGen);
    void setAutoWriteRptFile(bool bEnable);
    void setRptFileMaxLength(int numSeconds);
    void setUIDarkMode(bool bEnable);
    void setShowRegenerateTracks(bool bShow);

    enum EDiffToGo : uint8_t { Previous, Next };
    void goToDiff(EDiffToGo type);

    bool getOption(uint8_t id) const;

    void setSliderValue(uint8_t id, float value);
    void setButtonEnabled(uint8_t id, bool bEnabled);

    int64_t getSampleAtPosition(int x) const;

    float getCurrentMousePos() const { return currentMousePos.getX(); }

    bool isRegenerating() const { return m_isRegenerating; }
    bool isLoadingFile() const;

    WaveformOverview* getOverview() const { return overview.get(); }
    void setParentComponentPtr(MainComponent* ptr) { m_parentComponent = ptr; }

    ControlPanel* getControlPanel() const;
    DebugProcessor* getProcessor() const { return m_processor; }

    AudioDebugger* getDebugger() const;

    int64_t getViewportStartSample() const { return viewportStartSample; }
    int64_t getViewportLengthSamples() const { return viewportLengthSamples; }
    double getGlobalVerticalZoom() const;
    double getSamplesPerPixel() const { return viewportLengthSamples / (double)getWidth(); }
    uint8_t getNumInputs() const { return m_numInputs; }
    uint8_t getNumOutputs() const { return m_numOutputs; }
    int64_t getHighlightedDiff() const { return highlightedDiff; }

protected:
    bool loadAudioFile(const juce::File& file, bool liveMode);
    void clearScreen();
    bool shouldPaintDetailedUI() const;
    bool shouldPaintExtraDetailedUI() const;

    juce::Colour colorIdToJuceColour(uint32_t id) const;

    DebugProcessor* m_processor = nullptr;
    uint8_t m_numInputs = 0;
    uint8_t m_numOutputs = 2;

    std::unique_ptr<AudioDebuggerLookAndFeel> m_lookAndFeel;
    std::unique_ptr<juce::FileChooser> m_fileChooser;

    float m_percentageLoaded = 0.0f;
    bool m_isRegenerating = false;

    juce::Point<float> currentMousePos;

    template<typename T>
    int32_t average_vect(AudioDebuggerUI* pThis, const T& data, int start, int end, int latencySamples = 0)
    {
        int count = end - start;
        if (count <= 0) {
            return 0;
        }

        int val = 0;
        int maxEnd = std::min(end, (int)data.size());
        for (int i = start; i < maxEnd; i++)
        {
            val += data[i];
        }

        return std::round(val / count);
    }


public:
    int getTotalChannels() const { return m_numInputs + m_numOutputs * 2; }

    int64_t getTotalSamples() const;
    int getSampleRate() const;
    FileReader* getRptFile() const;
    FileReader* getGenFile() const;

    int getInputChanId (int index) const { return index; }
    int getOutputChanId(int index) const { return m_numInputs + index; }
    int getRegenChanId (int index) const { return m_numInputs + m_numOutputs + index; }

    bool isRegenChanId(int globalChanId) const { return globalChanId >= m_numInputs + m_numOutputs; }
    int chanIdToOutputIndex(int globalChanId) const;

    ChannelComponent* getChannel(int globalChanId) const;

    const SampleCache* getSampleCache(int globalChanId) const;
    const juce::CriticalSection* getSampleCacheLock(int globalChanId) const;

    CollapsibleChannel* getCollapsibleChannel(int globalChanId) const;
    bool isChannelExpanded(int globalChanId) const;

    juce::Rectangle<int> getChannelArea(int globalChanId) const;
    juce::Rectangle<int> getChannelHeaderArea(int globalChanId) const;
    float getChannelCentreY(int globalChanId) const;

    void setTrackHeight(int heightPx);
    int getTrackHeight() const { return m_trackHeight; }

    static constexpr int MIN_TRACK_HEIGHT = 40;
    static constexpr int MAX_TRACK_HEIGHT = 400;
    static constexpr int DEFAULT_TRACK_HEIGHT = 120;
    static constexpr int TRACK_HEIGHT_STEP = 5;

    void channelLayoutChanged();
    void ensureSampleCache();

    double getMaxZoom() const { return MAX_ZOOM; }
    void setZoomLevel(double newZoom);

    void differenceThresholdChanged(int threshold);

    float getFontSize(double pixelsPerSample) const;

    DiffDetector* getDiffDetector() const;
protected:
    void setGenerationButtonsEnabled(bool bEnable);
    void onBackgroundLoadStarted();

    void autoFitTrackHeight();
    void updateMidiTrackVisibility();

    void updateVisibleRange();

    int64_t calculateViewportLength(double zoom) const;
    double calculateZoomFromViewportLength(int64_t viewportLength) const;

    static constexpr double MIN_ZOOM = 1.0;
    static constexpr double MAX_ZOOM = 1000000.0;
    static constexpr int64_t MIN_VIEWPORT_SAMPLES = 16;
    static constexpr double RAW_SAMPLE_THRESHOLD = 16.0;

    bool m_isLoading = false;
    double zoomLevel = 0.0;
    double viewportCenter = 0.0;
    int64_t viewportStartSample = 0;
    int64_t viewportLengthSamples = 0;
    bool isDragging = false;
    int32_t m_repaintedRegenSamples = 0;

    juce::Point<int> lastMousePos;
    int64_t m_sampleAtCursor = 0;

    int64_t highlightedDiff = 0;

    std::unique_ptr<ControlPanel> m_controlPanel;

    std::function<void(int64_t)> onMouseSampleChanged;

    juce::File m_lastSelectedFile;
    MainComponent* m_parentComponent = nullptr;

    std::vector<std::unique_ptr<ChannelComponent>> m_channels;
    std::unique_ptr<MidiTrackComponent> m_midiTrack;
    CollapsibleChannel* m_midiChannel = nullptr;
    std::unique_ptr<DiffTrackComponent> m_diffTrack;
    CollapsibleChannel* m_diffChannel = nullptr;
    std::vector<CollapsibleChannel*> m_regenChannels;

    int m_trackHeight = DEFAULT_TRACK_HEIGHT;

    std::unique_ptr<ChannelList> m_channelList;
    std::unique_ptr<juce::Viewport> m_viewport;
    std::unique_ptr<OverlayComponent> m_overlay;

    friend class ChannelComponent;
    friend class OverlayComponent;
    friend class MidiTrackComponent;
    friend class DiffTrackComponent;

private:
    std::unique_ptr<WaveformOverview> overview;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioDebuggerUI)
};

enum EMenuItemID : uint8_t
{
    NEW = 1,
    IMPORT_RPT,
    IMPORT_GEN,
    EXPORT,
    CLEAR_SESSION,
    DARKMODE,
    ALWAYSONTOP,
    SHOWREGENERATE,
    SET_RPT_STATE,
    SET_GEN_STATE,
};

class WaveformPopup : public juce::DocumentWindow
{
public:
    WaveformPopup(const juce::String& name, AudioDebuggerUI* audioDebugger, std::function<void()> onCloseCallback = {});
    ~WaveformPopup();

    void closeButtonPressed() override;
    AudioDebuggerUI* getAudioDebugger() const { return m_audioDebugger; }

private:
    std::unique_ptr<MainComponent> m_mainComponent;
    AudioDebuggerUI* m_audioDebugger = nullptr;
    std::function<void()> m_onCloseCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformPopup)
};


} // namespace AudioDebug
