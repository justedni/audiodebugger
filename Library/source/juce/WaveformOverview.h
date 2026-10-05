// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ReportFile.h"

#include <fstream>

namespace AudioDebug {

class FileReader;
class ReadBlock;
struct Marker;
enum ESampleSlot : uint8_t;
enum class BlockType : uint32_t;

struct MipMapContainer;
struct MipmapLevel;
enum MipmapChan : uint8_t;
class ReportFile;

class DebugProcessor;

class DiffDetector;
class MipmapBuilder;
class MarkerStore;
class ChannelComponent;

class WaveformOverview
{
public:
    WaveformOverview(DebugProcessor* processor);
    ~WaveformOverview();

    void startLiveMonitoring();
    void stopLiveMonitoring();
    void updateLiveMonitoringThread();
    bool isLiveMode() const { return m_liveMode; }
    bool isLiveMonitoring() const { return !shouldStopLiveMonitoring.load(); }

    void startOpenGenFile(const juce::String& genFilePath);
    void startOutputRegeneration();

    bool openRptFile(const juce::String& filePath, double viewportZoomLevel, float viewportWidth, bool liveMode);
    void openGenFile(const juce::String& filePath);
    void reopenSameGenFile();

    void updateSampleCache(int64_t viewportStartSample, int64_t viewportLengthSamples, std::vector<std::unique_ptr<ChannelComponent>>& channels);

    bool isLoading() const { return m_bIsLoading.load(); }
    uint8_t getLoadingProgress() const { return m_loadingProgress.load(); }
    uint32_t getRegenSamplesProcessed() const { return m_regenSamplesProcessed.load(); }

    int64_t getTotalSamples() const { return totalSamples; }
    int getSampleRate() const { return sampleRate; }
    double getLengthInSeconds() const { return totalSamples / (double)sampleRate; }

    DiffDetector* getDiffDetector() const;
    MipmapBuilder* getMipMapBuilder() const;
    MarkerStore* getMarkers() const;

    ReportFile* getRptFile() const;
    ReportFile* getGenFile() const;

    bool getLiveRefreshRequired();
private:
    void setRptCallbacks();
    void setGenCallbacks();

    void startMipmapBackgroundGeneration();
    void generateMipmapLevelsInBackground();

    bool m_liveMode = false;
    std::atomic<bool> shouldStopLiveMonitoring{ true };
    std::unique_ptr<juce::Thread> liveMonitorThread;

protected:
    int64_t totalSamples;
    int sampleRate;
    int bitsPerSample;

    std::unique_ptr<ReportFile> m_rptFile;
    std::unique_ptr<ReportFile> m_genFile;

private:
    DebugProcessor* m_processor = nullptr;

    bool disableRefBlocks = false;

    std::unique_ptr<juce::Thread> m_backgroundThread;
    std::atomic<uint8_t> m_loadingProgress = 0;
    std::atomic<uint32_t> m_regenSamplesProcessed = 0;
    std::atomic<bool> m_bIsLoading = false;
    std::atomic<bool> m_bLiveRefreshNeeded = false;

    std::unique_ptr<DiffDetector> m_diffDetector;
    std::unique_ptr<MipmapBuilder> m_mipmaps;
    std::unique_ptr<MarkerStore> m_markers;

    void updateRegenerateThread();
};

} // namespace AudioDebug
