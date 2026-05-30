// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <mutex>
#include <functional>
#include <variant>

namespace AudioDebug {

class FileWriter;
class DebugProcessor;
struct FileHeader;

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

enum EBitDepth : uint8_t
{
    Int24,
    Int32
    //Int8,  // TODO
    //Int16, // TODO
};

struct RawMidiMessage
{
    uint32_t position = 0;
    uint8_t dataSize = 0;
    uint8_t data[3] = { 0, 0, 0 };
};

class SampleWriter
{
public:
    SampleWriter(uint8_t* dest, size_t maxBytes, uint8_t bitDepth) noexcept;

    void push(int32_t sample) noexcept;
    void push(float sample) noexcept;

    size_t bytesWritten() const noexcept { return m_pos; }

private:
    uint8_t* m_dest;
    size_t m_pos;
    size_t m_maxBytes;
    uint8_t m_bitDepth;
};

class AudioDebugger
{
public:
    AudioDebugger();
    ~AudioDebugger();

    AudioDebugger(AudioDebugger& other) = delete;
    void operator=(const AudioDebugger&) = delete;

    static AudioDebugger* get();

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

    void pushAllInputSamples(int numSamples, int numChannels, float* const* buffer);
    void pushAllOutputSamples(int numSamples, int numChannels, float* const* buffer);
    void addInDebugSamples(int numSamples, const SampleAccFunc f);
    void addOutDebugSamples(int numSamples, const SampleAccFunc& f);
    void addMetadata(const std::vector<uint8_t>& meta_data);
    void addCustomData(const std::vector<uint8_t>& meta_data);
    void pushMidiEvent(int position, int dataSize, const uint8_t* data);
    void writeAllMidiEvents();
    void sendBlockEnd();

    static std::vector<RawMidiMessage> readAllMidiEvents(const std::vector<uint8_t>& data);

    int32_t getCurrentWrittenBufferSize() const;
    int32_t getSampleIdInCurrentBuffer(int numSamples, int sampleId, bool lookback, int latencySamples) const;
    void setSampleToDebug(int32_t sampleId) { m_sampleToDebug = sampleId; }

    EBitDepth getBitDepth() const { return m_bitDepth; }
    uint8_t getNumInputs() const { return m_numInputs; }
    uint8_t getNumOutputs() const { return m_numOutputs; }
    void setNumInputsOutputs(uint8_t in_numInputs, uint8_t in_numOutputs);

    static int32_t floatToInt(float sample, uint8_t bitDepth);
    static float intToFloat(int32_t sample, uint8_t bitDepth);

    void startRegenerate(const FileHeader& header, std::string& out_genFileName);
    void endRegenerate();
    bool isRegenerating() const { return m_bIsRegenerating; }

    void setBlockAddedCallback(BlockAddedFunc func);

protected:
    friend class DebugProcessor;
    friend class AudioDebuggerUI;
    static void create();
    static void release();

private:
    void resetWriter_();
    void checkMaxLengthReached_();

    static std::unique_ptr<AudioDebugger> m_singleton;
    static std::mutex m_mutex;

    bool m_autoWriteOutputFile = false;
    int m_rptFileMaxLengthSecs = 0;

    std::mutex m_writerMutex;
    std::unique_ptr<FileWriter> m_debugWriter;

    static constexpr int kMidiBufferSize = 512 * 8;
    uint8_t midiBuffer[kMidiBufferSize];
    int midiBufferSize = 0;

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


class Metadata
{
public:
    enum EMetadataType : uint32_t
    {
        INVALID = 0,
        MARKER_BUFFER_START = 0x54534b4d,   // "MKST"
        MARKER_TEXT = 0x524b524d,           // "MRKR"
        MARKER_CUSTOM = 0x434b524d,         // "MRKC"
    };

    static bool isValidMarker(int magic)
    {
        return (magic == MARKER_BUFFER_START)
            || (magic == MARKER_TEXT)
            || (magic == MARKER_CUSTOM);
    }

    static uint32_t add_uint8(std::vector<uint8_t>& buffer, uint8_t num);
    static uint32_t add_int8(std::vector<uint8_t>& buffer, int8_t num);
    static uint32_t add_uint16(std::vector<uint8_t>& buffer, uint16_t num);
    static uint32_t add_int16(std::vector<uint8_t>& buffer, int16_t num);
    static uint32_t add_uint32(std::vector<uint8_t>& buffer, uint32_t num);
    static uint32_t add_uint64(std::vector<uint8_t>& buffer, uint64_t num);
    static uint32_t add_int32(std::vector<uint8_t>& buffer, int32_t num);
    static uint32_t add_float(std::vector<uint8_t>& buffer, float num);
    static uint32_t add_double(std::vector<uint8_t>& buffer, double num);
    static uint32_t add_string(std::vector<uint8_t>& buffer, const char* text, size_t textSize);

    static uint32_t read_uint8(const uint8_t* data, uint8_t& out_num);
    static uint32_t read_int8(const uint8_t* data, int8_t& out_num);
    static uint32_t read_uint16(const uint8_t* data, uint16_t& out_num);
    static uint32_t read_int16(const uint8_t* data, int16_t& out_num);
    static uint32_t read_uint32(const uint8_t* data, uint32_t& out_num);
    static uint32_t read_uint64(const uint8_t* data, uint64_t& out_num);
    static uint32_t read_int32(const uint8_t* data, int32_t& out_num);
    static uint32_t read_float(const uint8_t* data, float& out_num);
    static uint32_t read_double(const uint8_t* data, double& out_num);

    static void addMarkerText(std::vector<uint8_t>& meta_data, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color, const char* text, size_t textSize);
    static void addMarkerCustom(std::vector<uint8_t>& meta_data, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color, const std::vector<uint8_t>& data);
    static void addBufferStartMarker(std::vector<uint8_t>& meta_data, int timestamp);

    static Marker readMarker(const uint8_t* data, uint32_t& out_readSize);

private:
    static void addMarkerRegular(std::vector<uint8_t>& meta_data, int magic, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color, const char* text, size_t textSize);
};

} // namespace AudioDebug
