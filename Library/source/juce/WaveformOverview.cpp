#include "WaveformOverview.h"

#include "core/AudioDebug.h"
#include "core/DebugProcessor.h"
#include "core/FileWriter.h"
#include "juce/Mipmaps.h"
#include "juce/RingBuffer.h"

#include "juce/DiffDetector.h"
#include "juce/MarkerStore.h"
#include "juce/MipmapBuilder.h"

namespace AudioDebug {

WaveformOverview::WaveformOverview()
    : totalSamples(0)
    , sampleRate(0)
    , bitsPerSample(0)
    , m_diffDetector(new DiffDetector())
    , m_mipmaps(new MipmapBuilder())
    , m_markers(new MarkerStore())
{
}

WaveformOverview::~WaveformOverview()
{
    if (m_backgroundThread) {
        m_backgroundThread->stopThread(5000);
    }

    stopLiveMonitoring();

    m_mipmaps->clear();
}

DiffDetector* WaveformOverview::getDiffDetector() const { return m_diffDetector.get(); }
MipmapBuilder* WaveformOverview::getMipMapBuilder() const { return m_mipmaps.get(); }
MarkerStore* WaveformOverview::getMarkers() const { return m_markers.get(); }

ReportFile* WaveformOverview::getRptFile() const { return m_rptFile.get(); }
ReportFile* WaveformOverview::getGenFile() const { return m_genFile.get(); }

void WaveformOverview::startLiveMonitoring()
{
    shouldStopLiveMonitoring.store(false);

    class LiveMonitorThread : public juce::Thread {
    public:
        LiveMonitorThread(WaveformOverview* owner)
            : juce::Thread("LiveAudioMonitor")
            , overview(owner)
        {}

        void run() override
        {
            overview->updateLiveMonitoringThread();
        }

    private:
        WaveformOverview* overview;
    };

    liveMonitorThread = std::make_unique<LiveMonitorThread>(this);
    liveMonitorThread->startThread();
}

bool WaveformOverview::getLiveRefreshRequired()
{
    if (m_bLiveRefreshNeeded.load())
    {
        m_bLiveRefreshNeeded.store(false);
        return true;
    }

    return false;
}

void WaveformOverview::updateLiveMonitoringThread()
{
    ReadBlock block;

    while (!shouldStopLiveMonitoring.load())
    {
        int32_t processedThisBlock = 0;
        auto ret = m_rptFile->processOneBlock(block, EProcessType::Normal, false, processedThisBlock);
        jassert(ret != EProcessRet::Stop); // In live mode, we never break, until shouldStopLiveMonitoring is requested

        if (ret == EProcessRet::Success && block.type == BlockType::AUDIOOUT)
        {
            m_bLiveRefreshNeeded.store(true);
        }

        totalSamples = m_rptFile->getOutStateSampleCount();
    }
}

void WaveformOverview::stopLiveMonitoring()
{
    shouldStopLiveMonitoring.store(true);

    if (liveMonitorThread) {
        liveMonitorThread->stopThread(2000);
        liveMonitorThread.reset();
    }
}

void WaveformOverview::setRptCallbacks()
{
    m_rptFile->onMarker = [this](Marker&& m) {
        m_markers->addMarker(std::move(m));
    };
    m_rptFile->onMidi = [this](int64_t ts, std::vector<RawMidiMessage> msgs) {
        m_markers->addMidi(ts, std::move(msgs));
    };

    m_rptFile->onAudioBlock = [this](auto globalChanId, float value, int64_t blockStartSample, int blockSize) {
        m_mipmaps->pushFrame(globalChanId, value, blockStartSample, blockSize);
    };

}

void WaveformOverview::setGenCallbacks()
{
    m_genFile->onMarker = [this](Marker&& m) {
        m_markers->updateRegenMarker(std::move(m));
    };

    m_genFile->onAudioBlock = [this](auto globalChanId, float value, int64_t blockStartSample, int blockSize) {
        m_mipmaps->pushFrame(globalChanId, value, blockStartSample, blockSize);
    };

}

bool WaveformOverview::openRptFile(const juce::String& filePath, double viewportZoomLevel, float viewportWidth, bool liveMode, int& out_numInChans)
{
    m_liveMode = liveMode;
    disableRefBlocks = false;
    m_bIsLoading = true;
    m_loadingProgress = 0;

    m_rptFile.reset(new ReportFile(ReportFile::EFileSlot::Rpt, filePath.toStdString()));
    auto ret = m_rptFile->openFile(m_liveMode);
    if (!ret)
        return false;

    setRptCallbacks();

    auto& header = m_rptFile->getHeader();

    out_numInChans = header.in_channels;

    if (liveMode)
    {
        totalSamples = 0; // Unknown at this stage
        sampleRate = header.sample_rate;
    }
    else
    {
        totalSamples = header.blocks_count * header.block_size;
        sampleRate = header.sample_rate;
    }

    bitsPerSample = 32;

    m_mipmaps->clear();
    m_markers->clear();
    m_diffDetector->clear();

    m_mipmaps->init(header.in_channels, header.out_channels, liveMode, totalSamples);

    m_rptFile->initChanStates(true, header.in_channels, header.out_channels, header.block_size);

    if (liveMode)
    {
        startLiveMonitoring();
    }
    else
    {
        startMipmapBackgroundGeneration();
    }

    return true;
}

void WaveformOverview::startMipmapBackgroundGeneration()
{
    class GenerationThread : public juce::Thread {
    public:
        GenerationThread(WaveformOverview* owner)
            : juce::Thread("WaveformGeneration")
            , overview(owner)
        {}

        void run() override
        {
            overview->generateMipmapLevelsInBackground();
        }

    private:
        WaveformOverview* overview;
    };

    m_backgroundThread = std::make_unique<GenerationThread>(this);
    m_backgroundThread->startThread();
}

void WaveformOverview::startOpenGenFile(const juce::String& genFilePath)
{
    class GenFileThread : public juce::Thread {
    public:
        GenFileThread(WaveformOverview* owner, const juce::String& filePath)
            : juce::Thread("RegenerationThread")
            , overview(owner)
            , genFilePath(filePath)
        {}

        void run() override
        {
            overview->openGenFile(genFilePath);
        }

    private:
        WaveformOverview* overview;
        const juce::String genFilePath;
    };

    m_backgroundThread = std::make_unique<GenFileThread>(this, genFilePath);
    m_backgroundThread->startThread();
}

void WaveformOverview::startOutputRegeneration(DebugProcessor* processor)
{
    class RegenerationThread : public juce::Thread {
    public:
        RegenerationThread(WaveformOverview* owner, DebugProcessor* proc)
            : juce::Thread("RegenerationThread")
            , overview(owner)
            , processor(proc)
        {}

        void run() override
        {
            overview->updateRegenerateThread(processor);
        }

    private:
        WaveformOverview* overview;
        DebugProcessor* processor;
    };

    m_backgroundThread = std::make_unique<RegenerationThread>(this, processor);
    m_backgroundThread->startThread();
}

void WaveformOverview::reopenSameGenFile()
{
    auto& currFilePath = m_genFile->getFilePath();
    if (!currFilePath.empty())
        startOpenGenFile(currFilePath);
}

void WaveformOverview::openGenFile(const juce::String& filePath)
{
    m_genFile.reset(new ReportFile(ReportFile::EFileSlot::Gen, filePath.toStdString()));
    bool bOpened = m_genFile->openFile(false);
    jassert(bOpened);

    setGenCallbacks();

    disableRefBlocks = true;
    m_bIsLoading = true;
    m_loadingProgress = 0;

    m_liveMode = false;

    m_rptFile->seekToDataStart();

    m_diffDetector->clear();

    auto& header = m_rptFile->getHeader();

    m_genFile->seekToDataStart();
    m_genFile->initChanStates(false, header.in_channels, header.out_channels, header.block_size);

    m_mipmaps->clearRegenChannels();

    bool rptFileEnded = false;
    bool genFileEnded = false;
    int64_t samplesProcessed = 0;
    ReadBlock block;
    ReadBlock genBlock;

    while (true)
    {
        block.type = BlockType::INVALID;
        while (block.type != BlockType::AUDIOOUT && !rptFileEnded)
        {
            int32_t processedThisBlock = 0;
            if (m_rptFile->processOneBlock(block, EProcessType::DryRun, true, processedThisBlock) == EProcessRet::Stop)
            {
                rptFileEnded = true;
            }
        }

        genBlock.type = BlockType::INVALID;
        while (genBlock.type != BlockType::AUDIOOUT && !genFileEnded)
        {
            int32_t genProcessedThisBlock = 0;
            if (m_genFile->processOneBlock(genBlock, EProcessType::Normal, true, genProcessedThisBlock) == EProcessRet::Stop)
            {
                genFileEnded = true;
            }
        }

        const auto& leftBuffer = m_rptFile->getOutTempBuffer();
        const auto& rightBuffer = m_genFile->getOutTempBuffer();
        for (int ch = 0; ch < header.out_channels; ch++)
            m_diffDetector->detectDifferences(leftBuffer[ch], rightBuffer[ch], ch, header.bit_depth);

        samplesProcessed += header.block_size;

        uint8_t progress = std::round((samplesProcessed / (float)totalSamples) * 100);
        m_loadingProgress = progress;

        if (rptFileEnded && genFileEnded)
        {
            break;
        }
    }

    m_mipmaps->finaliseRef();

    m_bIsLoading = false;
    m_loadingProgress = 100;
}

void WaveformOverview::updateRegenerateThread(DebugProcessor* processor)
{
    disableRefBlocks = true;
    m_bIsLoading = true;
    m_loadingProgress = 0;
    m_regenSamplesProcessed = 0;

    auto* pDebugger = AudioDebug::AudioDebugger::get();
    //pDebugger->setSampleToDebug(sampleItToDebug);

    ReadBlock block;
    ReadBlock genBlock;

    m_markers->initRegen();

    m_rptFile->seekToDataStart();

    auto& header = m_rptFile->getHeader();
    const int numInChannels = header.in_channels;
    const int numOutChannels = header.out_channels;

    std::vector<float*> tempInPtrs;
    auto& tempIn = m_rptFile->getInTempBuffer();
    tempInPtrs.reserve(tempIn.size());
    for (const auto& buff : tempIn)
        tempInPtrs.push_back(const_cast<float*>(buff.data()));

    std::vector<std::vector<float>> outChannels (numOutChannels, std::vector<float>(header.block_size));
    std::vector<float*> tempOutPtrs(numOutChannels);
    for (size_t i = 0; i < numOutChannels; ++i)
        tempOutPtrs[i] = outChannels[i].data();

    float** outBuffer = tempOutPtrs.data();

    std::string genFileName;
    pDebugger->startRegenerate(header, genFileName);

    processor->prepareToPlayHook(header.sample_rate, header.block_size);

    ReportFile* genFile = nullptr;
    {
        auto file = juce::File::getCurrentWorkingDirectory().getChildFile(genFileName);
        if (file.existsAsFile())
            genFile = new ReportFile(ReportFile::EFileSlot::Gen, file.getFullPathName().toStdString());
    }

    const int latencySamples = processor->getLatencySamples(header.block_size);

    AudioRingBuffer* refOutChannelsCache[2] = { nullptr, nullptr };
    for (int i = 0; i < 2; i++)
    {
        refOutChannelsCache[i] = new AudioRingBuffer(header.block_size, header.block_size + latencySamples);
    }

    m_genFile.reset(genFile);
    bool bOpened = genFile ? m_genFile->openFile(true) : false;
    if (!bOpened)
        return;

    setGenCallbacks();

    bool previousLiveMode = m_liveMode;
    m_liveMode = false;

    bool rptFileEnded = false;

    int64_t samplesProcessed = 0;
    int64_t consumedLatency = latencySamples;

    m_diffDetector->clear();

    m_genFile->seekToDataStart();
    m_genFile->initChanStates(false, header.in_channels, header.out_channels, header.block_size);

    m_mipmaps->clearRegenChannels();

    pDebugger->setBlockAddedCallback([this](auto type, const auto* bytes, auto size)
    {
        ReadBlock tempBlock;
        tempBlock.type = static_cast<BlockType>(type);
        tempBlock.data.resize(size);
        // TODO: can be done better than copying the whole thing
        std::memcpy(tempBlock.data.data(), bytes, size);

        m_genFile->simulateReadBlock(tempBlock, EProcessType::Normal);

        if (tempBlock.type == BlockType::AUDIOOUT)
        {
            m_bLiveRefreshNeeded.store(true);
        }
    });


    int currentBlockId = 0;

    std::vector<RawMidiMessage> localMidiMessages;

    bool bAudioInRead = false;
    int numberOfSamplesPerBlock = 0;
    bool bAudioOutRead = false;

    auto generateAndDiff = [&]()
    {
        processor->processBlockHook(numberOfSamplesPerBlock, tempInPtrs.data(), localMidiMessages, outBuffer);

        samplesProcessed += numberOfSamplesPerBlock;

        if (consumedLatency <= 0)
        {
            auto& regeneratedBuffer = outChannels;
            for (int chan = 0; chan < numOutChannels; chan++)
            {
                const auto& originalBuffer = refOutChannelsCache[chan]->readToTemp(numberOfSamplesPerBlock, -latencySamples);
                m_diffDetector->detectDifferences(originalBuffer, regeneratedBuffer[chan], chan, header.bit_depth);
            }

            m_regenSamplesProcessed += numberOfSamplesPerBlock;
        }
        else
        {
            consumedLatency -= numberOfSamplesPerBlock;
        }
    };

    while (true)
    {
        BlockHeader nextHeader;
        if (m_rptFile->peekNextBlockHeader(nextHeader) && nextHeader.block_id != currentBlockId)
        {
            generateAndDiff();

            localMidiMessages.clear();
            bAudioInRead = false;
            bAudioOutRead = false;
            currentBlockId = nextHeader.block_id;
        }

        int32_t processedThisBlock = 0;
        if (m_rptFile->processOneBlock(block, EProcessType::DryRun, true, processedThisBlock) == EProcessRet::Stop)
        {
            rptFileEnded = true;
            generateAndDiff();
        }

        if (block.type == BlockType::AUDIOIN)
        {
            bAudioInRead = true;
            numberOfSamplesPerBlock = m_rptFile->as_samples(block).size() / numInChannels;
        }
        else if (block.type == BlockType::MIDI)
        {
            localMidiMessages = AudioDebugger::readAllMidiEvents(block.data);
        }
        else if (block.type == BlockType::AUDIOOUT)
        {
            bAudioOutRead = true;

            for (int i = 0; i < numOutChannels; i++)
            {
                const auto& sourceBuff = m_rptFile->getOutTempBuffer()[i];
                refOutChannelsCache[i]->push(sourceBuff.data(), sourceBuff.size());
            }
        }

        uint8_t progress = std::round((samplesProcessed / (float)totalSamples) * 100);
        m_loadingProgress = progress;

        if (rptFileEnded)
        {
            pDebugger->waitUntilWriterHasFinished();
            break;
        }
    }

    m_mipmaps->finaliseRef();

    m_bIsLoading = false;
    m_loadingProgress = 100;

    m_liveMode = previousLiveMode;

    pDebugger->endRegenerate();

    for (int i = 0; i < 2; i++)
    {
        delete refOutChannelsCache[i];
    }
}

void WaveformOverview::generateMipmapLevelsInBackground()
{
    m_bIsLoading = true;
    m_loadingProgress = 0;

    {
        int64_t samplesProcessed = 0;

        using namespace AudioDebug;
        ReadBlock block;

        m_markers->clear();

        m_rptFile->seekToDataStart();

        while (true)
        {
            int32_t processedThisBlock = 0;

            if (m_rptFile->processOneBlock(block, EProcessType::Normal, true, processedThisBlock) == EProcessRet::Stop)
                break;

            if (block.type == BlockType::AUDIOOUT)
            {
                samplesProcessed += processedThisBlock;
            }

            uint8_t progress = std::round((samplesProcessed / (float)totalSamples) * 100);
            m_loadingProgress = progress;
        }

        m_mipmaps->finaliseAll();
    }

    m_bIsLoading = false;
    m_loadingProgress = 100;
}

bool WaveformOverview::getSamplesFromFile(int64_t startSample, int64_t numSamples, std::vector<std::vector<float>>& outSamples)
{
    if (startSample < 0 || startSample >= totalSamples)
        return false;

    numSamples = std::min(numSamples, totalSamples - startSample);

    const auto& header = m_rptFile->getHeader();

    int totalChannels = header.in_channels + (header.out_channels * 2);
    outSamples.resize(totalChannels);
    for (int ch = 0; ch < totalChannels; ++ch)
    {
        outSamples[ch].resize((size_t)numSamples);
    }

    m_rptFile->findSamplesFromBlock(BlockType::AUDIOIN, startSample, numSamples, header.in_channels, 0, outSamples);
    m_rptFile->findSamplesFromBlock(BlockType::AUDIOOUT, startSample, numSamples, header.out_channels, header.in_channels, outSamples);

    const uint8_t outChanOffset = header.in_channels + header.out_channels;

    if (m_genFile)
        m_genFile->findSamplesFromBlock(BlockType::AUDIOOUT, startSample,  numSamples, header.out_channels, outChanOffset, outSamples);

    return true;
}

} // namespace AudioDebug
