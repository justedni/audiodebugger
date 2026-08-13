// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "core/AudioDebug.h"

namespace AudioDebug {

struct FileHeader;
class FileWriter;
class DebugProcessor;
enum class BlockType : uint32_t;

namespace FileWriterConfig
{
    constexpr size_t queueCapacity = 64;
    constexpr size_t maxBlockBytes = 128 * 1024 * 2; // Note: this is high on purpose because custom data can be very big
    constexpr std::chrono::milliseconds flushInterval{ 250 };
}

class BlockAccumulator
{
private:
public:
    BlockAccumulator(BlockType type) noexcept
        : m_type(type)
    {
        std::memset(m_buffer, 0, FileWriterConfig::maxBlockBytes);
    }

    BlockWriter& get() noexcept
    {
        if (!m_open)
        {
            m_writer = BlockWriter(m_buffer, FileWriterConfig::maxBlockBytes);
            m_open = true;
        }
        return m_writer;
    }

    bool isOpen()  const noexcept { return m_open; }
    BlockType type() const noexcept { return m_type; }
    const uint8_t* buffer() const noexcept { return m_buffer; }

    size_t close(bool& out_overflowed) noexcept
    {
        if (!m_open)
            return 0;

        m_open = false;
        out_overflowed = m_writer.overflowed();
        return out_overflowed ? 0 : m_writer.bytesWritten();
    }

private:
    const BlockType m_type;
    BlockWriter m_writer;
    uint8_t m_buffer[FileWriterConfig::maxBlockBytes];
    bool m_open = false;
};


class AudioDebugger
{
public:
    AudioDebugger();
    ~AudioDebugger();

    AudioDebugger(AudioDebugger& other) = delete;
    void operator=(const AudioDebugger&) = delete;

    void initDebugProcessor(double sampleRate, int samplesPerBlock, int numInputs, int numOutputs, uint8_t bitDepth);

    void waitUntilWriterHasFinished();
    void resetWriter();
    void setAutoWriteOutputFile(bool bEnable);
    void setRptFileMaxLength(int numSecs);

    bool getAsyncWriterStatusChanged();
    bool getAsyncWriterGotDisconnected();
    const std::string& getLastFileName() const;

    void prepare(DebugProcessor* processor);
    void terminate();

    void setCaptureName(const std::string& fileName, bool bAppendDate);

    using SampleAccFunc = AudioDebug::SampleAccFunc;
    using BlockAddedFunc = std::function<void(uint32_t, const uint8_t*, size_t)>;

    void writeInputFrame(int numSamples, int numChannels, const float* const* buffer);
    void writeOutputFrame(int numSamples, int numChannels, const float* const* buffer);
    void writeInputFrame(int numSamples, const SampleAccFunc f);
    void writeOutputFrame(int numSamples, const SampleAccFunc& f);
    BlockWriter& midiEventBlock() noexcept { return m_midiAccum.get(); }
    BlockWriter& metadataBlock() noexcept { return m_metadataAccum.get(); }
    BlockWriter& customBlock()   noexcept { return m_customAccum.get(); }

    int32_t getCurrentBufferTimestamp() const;
    int32_t getSampleIdInCurrentBuffer(int numSamples, int sampleId, bool lookback, int latencySamples) const;

    void setSampleToDebug(int32_t sampleId) { m_sampleToDebug = sampleId; }
    int32_t getSampleToDebug() const { return m_sampleToDebug; }

    uint32_t getInstanceId() const noexcept { return m_instanceId; }

    EBitDepth getBitDepth() const { return m_bitDepth; }
    uint8_t getNumInputs() const { return m_numInputs; }
    uint8_t getNumOutputs() const { return m_numOutputs; }
    void setNumInputsOutputs(uint8_t in_numInputs, uint8_t in_numOutputs);

    static int32_t floatToInt(float sample, uint8_t bitDepth);
    static float intToFloat(int32_t sample, uint8_t bitDepth);

    void startRegenerate(const FileHeader& header, DebugProcessor* processor, std::string& out_genFileName);
    void endRegenerate();
    bool isRegenerating() const { return m_bIsRegenerating; }

    void setBlockAddedCallback(BlockAddedFunc func);

    void serviceFromMessageThread();
    bool isCapturing() const;

private:
    friend class ScopedDebugBlock;

    void flushAccumulator_(BlockAccumulator& accum);
    void resetWriter_();
    void checkMaxLengthReached_();
    void waitUntilWriterHasFinished_();
    void savePluginState(DebugProcessor* processor);

    bool beginFrame(uint32_t numSamples);
    void endFrame();

    const uint32_t m_instanceId;

    bool m_autoWriteOutputFile = false;
    int m_rptFileMaxLengthSecs = 0;

    mutable std::mutex m_writerMutex;
    std::unique_ptr<FileWriter> m_debugWriter;

    std::atomic<bool> m_maxLengthReached{ false };

    BlockAccumulator m_midiAccum;
    BlockAccumulator m_metadataAccum;
    BlockAccumulator m_customAccum;

    std::string m_debugFilepath;
    bool m_debugWriterStatusChanged = false;
    bool m_debugWriterDisconnected = false;

    int32_t m_sampleToDebug = -1;
    uint64_t m_debugBufferWritePos = 0;
    uint32_t m_debugBlockCount = 0;
    bool m_bIsRegenerating = false;

    EBitDepth m_bitDepth = EBitDepth::Int32;
    uint8_t m_numInputs = 4;
    uint8_t m_numOutputs = 2;
    int m_sampleRate = 0;
    int32_t m_maxSamplesPerBlock = 0;
};

} // namespace AudioDebug
