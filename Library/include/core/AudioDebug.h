// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <mutex>
#include <functional>
#include <variant>

#include "core/BlockWriter.h"

namespace AudioDebug {

class FileWriter;
class DebugProcessor;
struct FileHeader;
enum class BlockType : uint32_t;

enum EMarkerColor : uint8_t
{
    Black = 0,
    PlainGrey,
    DottedGrey,
    Red,
    Blue,
    Green
};

enum EMarkerPaintType : uint8_t
{
    FullLine = 0,
    NoLine
};

struct Marker
{
    int32_t magic;
    int64_t samplePosition;
    uint8_t chanId;
    uint8_t paintType;
    uint8_t colorId;

    std::variant<std::string, std::vector<uint8_t>> data;

    Marker()
        : magic(0)
        , samplePosition(0)
        , chanId(0)
        , colorId(0)
    {}

    Marker(int32_t mag, int64_t pos, std::string&& t, uint8_t cid, uint8_t ptype, uint8_t col)
        : magic(mag), samplePosition(pos), chanId(cid), paintType(ptype), data(std::move(t)), colorId(col)
    {}

    Marker(int32_t mag, int64_t pos, std::vector<uint8_t>&& d, uint8_t cid, uint8_t ptype, uint8_t col)
        : magic(mag), samplePosition(pos), chanId(cid), paintType(ptype), data(std::move(d)), colorId(col)
    {}

    bool operator<(const Marker& other) const {
        if (samplePosition != other.samplePosition) {
            return samplePosition < other.samplePosition;
        }
        return chanId < other.chanId;
    }

    const std::string& getText() const { return std::get<std::string>(data); }
    const std::vector<uint8_t>& getBinary() const { return std::get<std::vector<uint8_t>>(data); }
};

struct MarkerPair
{
    MarkerPair(Marker&& m)
        : main(std::move(m))
    {
    }

    Marker main;
    bool bHasRegen = false;
    Marker regen;
};

enum EBitDepth : uint8_t
{
    Int24,
    Int32,
    //Int8,  // TODO
    //Int16, // TODO
    Count
};

struct RawMidiMessage
{
    uint32_t position = 0;
    uint8_t dataSize = 0;
    uint8_t data[3] = { 0, 0, 0 };
};

class AudioDebugger
{
public:
    AudioDebugger();
    ~AudioDebugger();

    AudioDebugger(AudioDebugger& other) = delete;
    void operator=(const AudioDebugger&) = delete;

    void initDebugProcessor(double sampleRate, int samplesPerBlock, int numInputs, int numOutputs, EBitDepth bitDepth);

    void waitUntilWriterHasFinished();
    void resetWriter();
    void setAutoWriteOutputFile(bool bEnable);
    void setRptFileMaxLength(int numSecs);

    bool getAsyncWriterStatusChanged();
    bool getAsyncWriterGotDisconnected();
    const std::string& getLastFileName() const;

    void forceBreakSample(uint16_t currentSampleId);

    void prepare(DebugProcessor* processor);
    void terminate();

    void setCaptureName(const std::string& fileName, bool bAppendDate);

    using SampleAccFunc = std::function<void(SampleWriter&)>;
    using BlockAddedFunc = std::function<void(uint32_t, const uint8_t*, size_t)>;

    void pushAllInputSamples(int numSamples, int numChannels, const float* const* buffer);
    void pushAllOutputSamples(int numSamples, int numChannels, const float* const* buffer);
    void pushInDebugSamples(int numSamples, const SampleAccFunc f);
    void pushOutDebugSamples(int numSamples, const SampleAccFunc& f);
    BlockWriter& midiEventBlock() noexcept { return m_midiAccum.get(); }
    BlockWriter& metadataBlock() noexcept { return m_metadataAccum.get(); }
    BlockWriter& customBlock()   noexcept { return m_customAccum.get(); }

    int32_t getCurrentBufferTimestamp() const;
    int32_t getSampleIdInCurrentBuffer(int numSamples, int sampleId, bool lookback, int latencySamples) const;
    void setSampleToDebug(int32_t sampleId) { m_sampleToDebug = sampleId; }

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

class ScopedDebugBlock
{
public:
    ScopedDebugBlock(AudioDebugger& debugger, uint32_t numSamples);
    ~ScopedDebugBlock();

    ScopedDebugBlock(const ScopedDebugBlock&)            = delete;
    ScopedDebugBlock& operator=(const ScopedDebugBlock&) = delete;
    ScopedDebugBlock(ScopedDebugBlock&&)                 = delete;
    ScopedDebugBlock& operator=(ScopedDebugBlock&&)      = delete;

    explicit operator bool() const noexcept { return m_bActive; }

    void writeInput(int numSamples, int numChannels, const float* const* buffer)
    {
        if (m_bActive)
            m_debugger.pushAllInputSamples(numSamples, numChannels, buffer);
    }

    void writeInput(int numSamples, const AudioDebugger::SampleAccFunc& f)
    {
        if (m_bActive)
            m_debugger.pushInDebugSamples(numSamples, f);
    }

    void writeOutput(int numSamples, int numChannels, const float* const* buffer)
    {
        if (m_bActive)
            m_debugger.pushAllOutputSamples(numSamples, numChannels, buffer);
    }

    void writeOutput(int numSamples, const AudioDebugger::SampleAccFunc& f)
    {
        if (m_bActive)
            m_debugger.pushOutDebugSamples(numSamples, f);
    }

    void pushMidiEvent(int position, int dataSize, const uint8_t* data);

    BlockWriter& metadata() noexcept
    {
        return m_bActive ? m_debugger.metadataBlock() : m_nullBlock;
    }

    BlockWriter& custom() noexcept
    {
        return m_bActive ? m_debugger.customBlock() : m_nullBlock;
    }

private:
    AudioDebugger& m_debugger;
    bool m_bActive = false;
    BlockWriter m_nullBlock;
};

namespace Metadata {

    enum EMetadataType : uint32_t
    {
        INVALID = 0,
        MARKER_BUFFER_START = 0x54534b4d,   // "MKST"
        MARKER_TEXT = 0x524b524d,           // "MRKR"
        MARKER_CUSTOM = 0x434b524d,         // "MRKC"
    };

    constexpr bool isValidMarker(int magic)
    {
        return (magic == MARKER_BUFFER_START)
            || (magic == MARKER_TEXT)
            || (magic == MARKER_CUSTOM);
    }

    uint32_t add_uint8(BlockWriter& block, uint8_t num);
    uint32_t add_int8(BlockWriter& block, int8_t num);
    uint32_t add_uint16(BlockWriter& block, uint16_t num);
    uint32_t add_int16(BlockWriter& block, int16_t num);
    uint32_t add_uint32(BlockWriter& block, uint32_t num);
    uint32_t add_uint64(BlockWriter& block, uint64_t num);
    uint32_t add_int32(BlockWriter& block, int32_t num);
    uint32_t add_float(BlockWriter& block, float num);
    uint32_t add_double(BlockWriter& block, double num);
    uint32_t add_string(BlockWriter& block, const char* text, size_t textSize);

    void addMarkerText(BlockWriter& block, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color, const char* text, size_t textSize);
    void addMarkerCustom(BlockWriter& block, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color, const std::vector<uint8_t>& data);
    void addMarkerCustomHeaderOnly(BlockWriter& block, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color);
    void addBufferStartMarker(BlockWriter& block, int timestamp);
    void addMarkerRegular(BlockWriter& block, int magic, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color, const char* text, size_t textSize);
    void addMidiEvent(BlockWriter& block, int timestamp, int dataSize, const uint8_t* data);


    uint32_t read_uint8(const uint8_t* data, uint8_t& out_num);
    uint32_t read_int8(const uint8_t* data, int8_t& out_num);
    uint32_t read_uint16(const uint8_t* data, uint16_t& out_num);
    uint32_t read_int16(const uint8_t* data, int16_t& out_num);
    uint32_t read_uint32(const uint8_t* data, uint32_t& out_num);
    uint32_t read_uint64(const uint8_t* data, uint64_t& out_num);
    uint32_t read_int32(const uint8_t* data, int32_t& out_num);
    uint32_t read_float(const uint8_t* data, float& out_num);
    uint32_t read_double(const uint8_t* data, double& out_num);

    Marker readMarker(const uint8_t* data, uint32_t& out_readSize);
    std::vector<RawMidiMessage> readAllMidiEvents(const std::vector<uint8_t>& data);

} // namespace Metadata

} // namespace AudioDebug
