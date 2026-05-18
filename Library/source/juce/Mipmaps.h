#pragma once

#include <vector>
#include <array>
#include <atomic>

namespace AudioDebug {

struct MinMaxPair
{
    float min;
    float max;

    MinMaxPair() : min(0.0f), max(0.0f) {}
    MinMaxPair(float mn, float mx) : min(mn), max(mx) {}
};

struct MipmapLevel
{
    static std::vector<int> getMipmapLevelsForMode(bool liveMode, int64_t totalSamples);

    std::vector<MinMaxPair> data;
    const int samplesPerPoint;
    std::atomic<int> numPointsReady = 0;

    MipmapLevel(int inSamplesPerPoint, int totalSamples);
    ~MipmapLevel();

    MipmapLevel(MipmapLevel&& other) noexcept;

    void clear()
    {
        data.clear();
        state.reset();
    }

    struct State
    {
        State();

        void reset();
        void updateMinMaxFromSample(float sample);
        void updateFrame(MipmapLevel& level, int64_t startSample, int numSamples);
        void endProcess(MipmapLevel& level);

        int currentPoint = 0;
        float currentMin;
        float currentMax;

        int samplesInCurrentPoint = 0;
    };

    State state;
};

} // namespace AudioDebug
