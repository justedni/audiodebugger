// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Mipmaps.h"

#include <assert.h>

namespace AudioDebug {

std::vector<int> MipmapLevel::getMipmapLevelsForMode(bool liveMode, int64_t totalSamples)
{
    if (liveMode)
    {
        return { 32, 64, 256, 1024, 4096, 16384 };
    }

    if (totalSamples <= 0)
    {
        return { 256, 1024, 4096, 16384 };
    }

    if (totalSamples < 163840)
    {
        return { 32, 128, 1024 };
    }

    std::vector<int> levels = { 256, 1024, 4096, 16384 };

    if (totalSamples > 16384 * 1000) {
        levels.push_back(65536);
    }
    if (totalSamples > 65536 * 1000) {
        levels.push_back(262144);
    }
    if (totalSamples > 262144 * 1000) {
        levels.push_back(1048576);
    }

    return levels;
}

MipmapLevel::MipmapLevel(int inSamplesPerPoint, int totalSamples)
    : samplesPerPoint(inSamplesPerPoint)
    , numPointsReady(0)
    , state()
{
    int numPoints = (int)((totalSamples + inSamplesPerPoint - 1) / inSamplesPerPoint);

    data.reserve(numPoints);
}

MipmapLevel::~MipmapLevel()
{
}

MipmapLevel::MipmapLevel(MipmapLevel&& other) noexcept
    : data(std::move(other.data))
    , samplesPerPoint(other.samplesPerPoint)
    , numPointsReady(other.numPointsReady.load())
    , state(std::move(other.state))
{}

MipmapLevel::State::State()
{
    reset();
}

void MipmapLevel::State::reset()
{
    currentPoint = 0;
    samplesInCurrentPoint = 0;

    currentMin = 1.0f;
    currentMax = -1.0f;
}

void MipmapLevel::State::updateMinMaxFromSample(float sample)
{
    currentMin = std::min(currentMin, sample);
    currentMax = std::max(currentMax, sample);
}

void MipmapLevel::State::updateFrame(MipmapLevel& level, int64_t startSample, int numSamples)
{
    const auto& samplesPerPoint = level.samplesPerPoint;

    samplesInCurrentPoint++;

    if (samplesInCurrentPoint >= samplesPerPoint)
    {
        if (currentPoint >= level.data.size())
        {
            const int startPoint = (int)(startSample / level.samplesPerPoint);
            const int endPoint = (int)((startSample + numSamples) / level.samplesPerPoint);

            if (endPoint >= (int)level.data.size()) {
                level.data.resize(endPoint + 1);
            }
        }
        assert(currentPoint < level.data.size());
        level.data[currentPoint] = MinMaxPair(currentMin, currentMax);
        level.numPointsReady = currentPoint + 1;

        currentPoint++;

        currentMin = 1.0f;
        currentMax = -1.0f;

        samplesInCurrentPoint = 0;
    }
}

void MipmapLevel::State::endProcess(MipmapLevel& level)
{
    const int numPoints = (int)level.data.size();

    if (samplesInCurrentPoint > 0 && currentPoint < numPoints)
    {
        level.data[currentPoint] = MinMaxPair(currentMin, currentMax);
        level.numPointsReady = currentPoint + 1;
    }
}

} // namespace AudioDebug
