// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace AudioDebug {

class MainMenuModel;
class DebugProcessor;
class ControlPanel;
class CustomSlider;
class AutoConnectButton;
struct Marker;

struct FileHeader;
class ReportFile;

class WaveformOverview;
struct MarkerPair;

class MainComponent;
class ChannelComponent;
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
                      , public juce::Slider::Listener
                      , public juce::Timer
{
public:
    AudioDebuggerUI(DebugProcessor* processor);
    virtual ~AudioDebuggerUI();

    // Virtual interface (customizable)
    virtual void paintUICustom(juce::Graphics& g) {}
    virtual void paintCustomMarkers(juce::Graphics& g, const std::vector<MarkerPair>& markers) {}
    virtual void updateControlPanelValues() {}
    virtual void controlPanelResized(int x, int y, int width) {}
    virtual void setSliderFromDebugPanel(uint8_t id, float value) {}
    virtual void setButtonFromDebugPanel(uint8_t id, bool bEnabled) {}
    virtual void invalidateCustomCache_() {}
    virtual void refreshChannelBounds();
    virtual juce::Colour getWaveformColour(uint8_t channelId) const;
    virtual juce::String getSampleValueString(float value, uint8_t bitDepth) const;

    // ------------------------------
    void initialize();
    void terminate();

    // ------------------------
    void addControlPanelOption(uint8_t id, const std::string& displayName, bool defaultEnabled);
    void addControlPanelButton(const juce::String& name);
    void addControlPanelSlider(int id, const CustomSlider& srcSlider, juce::LookAndFeel* lookAndFeel);
    void addComponentToControlPanel(juce::Component* component);

    void updateParamsValues();

    void paint(juce::Graphics& g) override;

    void resized() override;
    void sliderValueChanged(juce::Slider* slider) override;
    void scrollBarMoved(double newRangeStart);

    int64_t pixelToSample(float x, float componentWidth) const;
    float sampleToPixel(int64_t sample, float componentWidth) const;

    void timerCallback() override;

    void autoConnectToLastFile();
    void triggerCaptureEnd();

    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& e) override;

    void setNewStartSample(int64_t newStartSample);
    bool isSampleWithinVisibleRange(int64_t sampleId);

    void regenerateOutput(int32_t sampleItToDebug);
    void importbinReport();
    void importgenFile();
    void refreshGenFile();
    void exportbinReport();
    void setAutoWriteRptFile(bool bEnable);
    void setRptFileMaxLength(int numSeconds);
    void setUIDarkMode(bool bEnable);

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

    int64_t getViewportStartSample() const { return viewportStartSample; }
    int64_t getViewportLengthSamples() const { return viewportLengthSamples; }
    double getVerticalZoom() const { return zoomVSlider->getValue(); }
    double getSamplesPerPixel() const { return viewportLengthSamples / (double)getWidth(); }
    const std::vector<float>* getCachedSamplesForChannel(int chanId) const;
    int64_t getCacheStartSample() const { return sampleCache.startSample; }
    juce::CriticalSection& getCacheLock() const { return cacheLock; }
    uint8_t getNumInputs() const { return m_numInputs; }
    uint8_t getNumOutputs() const { return m_numOutputs; }
    int64_t getHighlightedDiff() const { return highlightedDiff; }

protected:
    bool loadAudioFile(const juce::File& file, bool liveMode);
    bool shouldPaintDetailedUI() const;
    bool shouldPaintExtraDetailedUI() const;

    juce::Colour colorIdToJuceColour(uint32_t id) const;

    DebugProcessor* m_processor = nullptr;
    uint8_t m_numInputs = 0;
    uint8_t m_numOutputs = 2;

    std::unique_ptr<AudioDebuggerLookAndFeel> m_lookAndFeel;
    std::unique_ptr<juce::FileChooser> m_fileChooser;

    std::unique_ptr<juce::Slider> zoomVSlider;

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


    struct ChannelBounds {
        float minValue = 0.0f;
        float maxValue = 0.0f;
        float middle = 0.0f;
        float metaY = 0.0f;
    };

    struct ChannelData
    {
        std::vector<ChannelBounds> bounds;
        float channelHeight;
        float metadataHeight;
    };

    ChannelData m_chanBounds;

    int getChannelHeight() const;
public:
    const ChannelData& getChannelData() const { return m_chanBounds; }

    struct SampleCache
    {
        int64_t startSample;
        int64_t numSamples;
        std::vector<std::vector<float>> samples; // Per-channel

        bool contains(int64_t sample) const {
            return sample >= startSample && sample < (startSample + numSamples);
        }

        bool isValid() const {
            return numSamples > 0 && !samples.empty();
        }

        void clear() {
            samples.clear();
            numSamples = 0;
        }
    };

    double getMaxZoom() const { return MAX_ZOOM; }
    void setZoomLevel(double newZoom);

    void differenceThresholdChanged(int threshold);

    float getFontSize(double pixelsPerSample) const;

    DiffDetector* getDiffDetector() const;
protected:
    void setGenerationButtonsEnabled(bool bEnable);

    void updateVisibleRange();
    void updateSampleCache_();

    int64_t calculateViewportLength(double zoom) const;
    double calculateZoomFromViewportLength(int64_t viewportLength) const;

    static constexpr double MIN_ZOOM = 1.0;
    static constexpr double MAX_ZOOM = 1000000.0;
    static constexpr int64_t MIN_VIEWPORT_SAMPLES = 16;
    static constexpr double RAW_SAMPLE_THRESHOLD = 16.0;

    std::unique_ptr<WaveformOverview> overview;

    mutable juce::CriticalSection cacheLock;
    SampleCache sampleCache;

    bool m_isLoading = false;
    double zoomLevel = 0.0;
    double viewportCenter = 0.0;
    int64_t viewportStartSample = 0;
    int64_t viewportLengthSamples = 0;
    bool isDragging = false;
    int32_t m_repaintedRegenSamples = 0;

    juce::Point<int> lastMousePos;

    int64_t highlightedDiff = 0;

    std::unique_ptr<ControlPanel> m_controlPanel;

    std::function<void(int64_t)> onMouseSampleChanged;

    juce::File m_lastSelectedFile;
    MainComponent* m_parentComponent = nullptr;

    std::vector<std::unique_ptr<ChannelComponent>> m_channels;
    std::unique_ptr<OverlayComponent> m_overlay;

    friend class ChannelComponent;
    friend class OverlayComponent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioDebuggerUI)
};

enum EMenuItemID : uint8_t
{
    NEW = 1,
    IMPORT_RPT,
    IMPORT_GEN,
    EXPORT,
    DARKMODE,
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
