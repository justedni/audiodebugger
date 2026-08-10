// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DemoProcessor.h"
#include "TestSignal.h"

#include "core/AudioDebug.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

namespace Config {

constexpr double      kSampleRate        = 48000.0;
constexpr int         kNumInputChannels  = 2;
constexpr int         kNumOutputChannels = 2;
constexpr double      kDurationSeconds   = 12.0;

constexpr int         kMaxBlockSize      = 512;
constexpr int         kMinBlockSize      = 32;

constexpr bool        kVariableBlockSize = true;
constexpr uint32_t    kBlockSizeSeed     = 0x5EEDBEEFu;

constexpr bool        kSimulateRealtime  = true;

constexpr const char* kCaptureName       = "ConsoleSample";

constexpr int         kMaxCaptureSeconds = 60;

} // namespace Config

//==============================================================================

namespace {

void sliceMidiForBlock(const std::vector<Demo::MidiEvent>& timeline,
                       size_t& cursor,
                       int64_t blockStart,
                       int numSamples,
                       std::vector<Demo::MidiEvent>& out)
{
    out.clear();

    const int64_t blockEnd = blockStart + numSamples;

    while (cursor < timeline.size()
           && timeline[cursor].samplePosition < blockEnd)
    {
        Demo::MidiEvent e = timeline[cursor];
        e.samplePosition  = static_cast<int>(e.samplePosition - blockStart);
        out.push_back(e);
        ++cursor;
    }
}

class BlockSizer
{
public:
    explicit BlockSizer(uint32_t seed) : m_state(seed) {}

    int next()
    {
        if (!Config::kVariableBlockSize)
            return Config::kMaxBlockSize;

        m_state = m_state * 1664525u + 1013904223u;

        const int range = Config::kMaxBlockSize - Config::kMinBlockSize + 1;
        return Config::kMinBlockSize + static_cast<int>((m_state >> 16) % static_cast<uint32_t>(range));
    }

private:
    uint32_t m_state;
};

} // namespace

int main()
{
    using namespace Config;

    const int numChannels  = std::max(kNumInputChannels, kNumOutputChannels);
    const int totalSamples = static_cast<int>(kDurationSeconds * kSampleRate);

    std::printf("ConsoleSample: plain C++ demo for AudioDebugger library\n");
    if (kVariableBlockSize)
        std::printf("  %.0f Hz, block size %d..%d (variable), %d in / %d out, %.1f s\n\n",
                    kSampleRate, kMinBlockSize, kMaxBlockSize,
                    kNumInputChannels, kNumOutputChannels, kDurationSeconds);
    else
        std::printf("  %.0f Hz, %d samples/block (fixed), %d in / %d out, %.1f s\n\n",
                    kSampleRate, kMaxBlockSize,
                    kNumInputChannels, kNumOutputChannels, kDurationSeconds);

    std::vector<std::vector<float>> channelData(static_cast<size_t>(numChannels), std::vector<float>(kMaxBlockSize, 0.0f));

    std::vector<float*> channelPtrs(static_cast<size_t>(numChannels));
    for (int ch = 0; ch < numChannels; ++ch)
        channelPtrs[static_cast<size_t>(ch)] = channelData[static_cast<size_t>(ch)].data();

    Demo::DemoProcessor processor(kNumInputChannels, kNumOutputChannels);

    processor.setCapturePath(kCaptureName, true);
    processor.setDebugCaptureEnabled(true);
    processor.prepareToPlay(kSampleRate, kMaxBlockSize);

    Demo::TestSignal signal(kSampleRate, numChannels);
    signal.reset();

    const auto midiTimeline = Demo::TestSignal::makeMidiSequence(kSampleRate, totalSamples);
    size_t midiCursor = 0;

    std::vector<Demo::MidiEvent> blockMidi;
    blockMidi.reserve(64);

    BlockSizer blockSizer(kBlockSizeSeed);

    int64_t samplePosition = 0;
    int     blockCount     = 0;
    int     smallestBlock  = kMaxBlockSize;
    int     largestBlock   = 0;
    double  nextReportTime = 0.0;

    while (samplePosition < totalSamples)
    {
        const auto blockStartTime = std::chrono::steady_clock::now();

        int numSamples = blockSizer.next();
        numSamples = std::min<int>(numSamples, static_cast<int>(totalSamples - samplePosition));

        smallestBlock = std::min(smallestBlock, numSamples);
        largestBlock  = std::max(largestBlock,  numSamples);

        signal.fill(numSamples, channelPtrs.data());

        sliceMidiForBlock(midiTimeline, midiCursor, samplePosition, numSamples, blockMidi);

        processor.processBlock(numSamples, channelPtrs.data(), blockMidi);

        samplePosition += numSamples;
        ++blockCount;

        const double elapsed = static_cast<double>(samplePosition) / kSampleRate;
        if (elapsed >= nextReportTime)
        {
            std::printf("  %6.2f s   block %4d   (%d samples)\n",
                        elapsed, blockCount, numSamples);
            nextReportTime += 1.0;
        }

        if (kSimulateRealtime)
            std::this_thread::sleep_until(
                blockStartTime + std::chrono::duration<double>(numSamples / kSampleRate));
    }

    processor.terminateDebugCapture();
    processor.releaseResources();

    return 0;
}
