#include "juce/MipmapBuilder.h"

#include "juce/Mipmaps.h"
#include "core/ReportFile.h"

namespace AudioDebug {

void MipmapBuilder::init(int numInChannels, int numOutChannels, bool liveMode, int64_t totalSamples)
{
    juce::ScopedLock lock(m_lock);

    m_numInChannels = numInChannels;
    m_numOutChannels = numOutChannels;

    m_samplesPerPoint = MipmapLevel::getMipmapLevelsForMode(liveMode, totalSamples);

    m_channels.clear();

    int totalChannels = numInChannels + (numOutChannels * 2);
    for (int i = 0; i < totalChannels; i++)
    {
        m_channels.emplace_back(m_samplesPerPoint, totalSamples);
    }
}

void MipmapBuilder::clear()
{
    juce::ScopedLock lock(m_lock);

    for (auto& chan : m_channels)
    {
        chan.levels.clear();
    }
}

void MipmapBuilder::clearRegenChannels()
{
    juce::ScopedLock lock(m_lock);

    int start = m_numInChannels + m_numOutChannels;
    for (int i = start; i < m_channels.size(); i++)
    {
        for (auto& level : m_channels[i].levels)
        {
            level.clear();
        }
    }
}

void MipmapBuilder::pushFrame(int globalChanId,
    float value,
    int64_t blockStartSample,
    int blockSize)
{
    juce::ScopedLock lock(m_lock);

    jassert(globalChanId >= 0 && globalChanId < m_channels.size());
    auto& channel = m_channels[globalChanId];

    for (auto& level : channel.levels)
    {
        level.state.updateMinMaxFromSample(value);
        level.state.updateFrame(level, blockStartSample, blockSize);
    }
}

void MipmapBuilder::finaliseRef()
{
    juce::ScopedLock lock(m_lock);

    int start = m_numInChannels + m_numOutChannels;
    for (int i = start; i < m_channels.size(); i++)
    {
        for (auto& level : m_channels[i].levels)
        {
            level.state.endProcess(level);
        }
    }
}

void MipmapBuilder::finaliseAll()
{
    juce::ScopedLock lock(m_lock);

    for (int i = 0; i < m_channels.size(); i++)
    {
        for (auto& level : m_channels[i].levels)
        {
            level.state.endProcess(level);
        }
    }
}

const MipmapLevel& MipmapBuilder::getLevel(int level, int globalChanId) const
{
    jassert(globalChanId >= 0 && globalChanId < m_channels.size());
    auto& channel = m_channels[globalChanId];
    jassert(level >= 0 && level < channel.levels.size());
    return channel.levels[level];
}

void MipmapBuilder::withLock(std::function<void()> f) const
{
    juce::ScopedLock lock(m_lock);
    f();
}

int MipmapBuilder::selectBestLevel(double samplesPerPixel, int globalChanId) const
{
    if (m_channels.empty())
        return 0;

    jassert(globalChanId >= 0 && globalChanId < m_channels.size());
    auto& channel = m_channels[globalChanId];

    if (channel.levels.empty())
        return 0;

    int bestLevel = 0;

    for (int i = 0; i < channel.levels.size(); ++i) {
        if (getLevel(i, globalChanId).samplesPerPoint <= samplesPerPixel * 2) {
            bestLevel = i;
        }
        else {
            break;
        }
    }

    return bestLevel;
}

} // namespace AudioDebug
