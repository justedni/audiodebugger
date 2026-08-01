// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace AudioDebug {

struct MipMapContainer;
struct MipmapLevel;

enum ESampleSlot : uint8_t;

class MipmapBuilder
{
public:
    void init(int numInChannels, int numOutChannels, bool liveMode, int64_t totalSamples);
    void clear();
    void clearRegenChannels();

    void pushFrame(int globalChanId,
        float value,
        int64_t blockStartSample,
        int blockSize);

    void finaliseRef();
    void finaliseAll();

    const MipmapLevel& getLevel(int level, int globalChanId) const;
    int getNumLevels() const { return m_samplesPerPoint.size(); }
    int selectBestLevel(double samplesPerPixel, int globalChanId) const;

    void withLock(std::function<void()> f) const;

private:
    mutable juce::CriticalSection m_lock;

    struct Channel
    {
        Channel(const std::vector<int>& samplesPerPoint, int64_t totalSamples)
        {
            int numLevels = samplesPerPoint.size();
            levels.reserve(numLevels);
            for (size_t i = 0; i < numLevels; ++i)
                levels.emplace_back(samplesPerPoint[i], totalSamples);
        }

        std::vector<MipmapLevel> levels;
    };

    std::vector<Channel> m_channels;
    std::vector<int> m_samplesPerPoint;

    int m_numInChannels = 0;
    int m_numOutChannels = 0;
};

} // namespace AudioDebug
