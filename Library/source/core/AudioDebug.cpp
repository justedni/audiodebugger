// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/AudioDebug.h"

#include "core/DebuggerAccess.h"
#include "core/FileWriter.h"
#include "core/BlockWriter.h"

#include <assert.h>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "core/DebugProcessor.h"

#ifdef _DEBUG
#define ensure(cond) if (!(cond)) { __debugbreak(); }
#else
#define ensure(cond) assert(cond)
#endif

namespace AudioDebug {

SampleWriter::SampleWriter(uint8_t* dest, size_t maxBytes, uint8_t bitDepth) noexcept
	: m_dest(dest)
	, m_pos(0)
	, m_maxBytes(maxBytes)
	, m_bitDepth(bitDepth)
{
}

void SampleWriter::push(int32_t sample) noexcept
{
	switch (m_bitDepth)
	{
	case EBitDepth::Int24:
	{
		assert(m_pos + 3 <= m_maxBytes);
		m_dest[m_pos++] = static_cast<uint8_t>(sample & 0xFF);
		m_dest[m_pos++] = static_cast<uint8_t>((sample >> 8) & 0xFF);
		m_dest[m_pos++] = static_cast<uint8_t>((sample >> 16) & 0xFF);
		break;
	}
	case EBitDepth::Int32:
	{
		assert(m_pos + 4 <= m_maxBytes);
		m_dest[m_pos++] = static_cast<uint8_t>(sample & 0xFF);
		m_dest[m_pos++] = static_cast<uint8_t>((sample >> 8) & 0xFF);
		m_dest[m_pos++] = static_cast<uint8_t>((sample >> 16) & 0xFF);
		m_dest[m_pos++] = static_cast<uint8_t>((sample >> 24) & 0xFF);
		break;
	}
	default:
		ensure(false);
	}
}

static constexpr float l24To32bits = 8388608.0f; // 2^23 (24 bits encoding, 1 bit for sign)
static constexpr double l32To32bits = 2147483648.0; // 2^31

int32_t AudioDebugger::floatToInt(float sample, uint8_t bitDepth)
{
	switch (bitDepth)
	{
	case EBitDepth::Int24:
	{
		return std::round(sample * l24To32bits);
	}
	case EBitDepth::Int32:
	{
		return (int32_t)std::round(sample * l32To32bits);
	}
	}

	ensure(false);
	return 0;
}

float AudioDebugger::intToFloat(int32_t sample, uint8_t bitDepth)
{
	switch (bitDepth)
	{
	case EBitDepth::Int24:
	{
		return sample / l24To32bits;
	}
	case EBitDepth::Int32:
	{
		return sample / l32To32bits;
	}
	}

	ensure(false);
	return 0;
}

void SampleWriter::push(float sample) noexcept
{
	int32_t value = AudioDebugger::floatToInt(sample, (EBitDepth)m_bitDepth);
	push(value);
}

namespace {

std::mutex g_instanceIdMutex;
std::vector<bool> g_instanceIdsInUse;

uint32_t acquireInstanceId()
{
	std::lock_guard<std::mutex> lock(g_instanceIdMutex);

	for (size_t i = 0; i < g_instanceIdsInUse.size(); ++i)
	{
		if (!g_instanceIdsInUse[i])
		{
			g_instanceIdsInUse[i] = true;
			return static_cast<uint32_t>(i);
		}
	}

	g_instanceIdsInUse.push_back(true);
	return static_cast<uint32_t>(g_instanceIdsInUse.size() - 1);
}

void releaseInstanceId(uint32_t id)
{
	std::lock_guard<std::mutex> lock(g_instanceIdMutex);

	if (id < g_instanceIdsInUse.size())
		g_instanceIdsInUse[id] = false;
}

} // namespace

AudioDebugger::AudioDebugger()
	: m_instanceId(acquireInstanceId())
	, m_midiAccum{ BlockType::MIDI }
	, m_metadataAccum{ BlockType::METADATA }
	, m_customAccum{ BlockType::CUSTOM }
{
}

AudioDebugger::~AudioDebugger()
{
	resetWriter();

	releaseInstanceId(m_instanceId);
}

void AudioDebugger::waitUntilWriterHasFinished()
{
	std::lock_guard<std::mutex> lock(m_writerMutex);
	waitUntilWriterHasFinished_();
}

void AudioDebugger::waitUntilWriterHasFinished_()
{
	if (m_debugWriter)
	{
		while (m_debugWriter->hasPendingWrites()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
	}
}

void AudioDebugger::resetWriter()
{
	std::lock_guard<std::mutex> lock(m_writerMutex);
	resetWriter_();
}

void AudioDebugger::resetWriter_()
{
	if (m_debugWriter)
	{
		waitUntilWriterHasFinished_();

		m_debugWriter.reset();
	}

	m_debugBufferWritePos = 0;
}

bool AudioDebugger::isCapturing() const
{
	std::lock_guard<std::mutex> lock(m_writerMutex);
	return m_debugWriter != nullptr;
}

void AudioDebugger::initDebugProcessor(double sampleRate, int samplesPerBlock, int numInputs, int numOutputs, uint8_t bitDepth)
{
	m_numInputs = numInputs;
	m_numOutputs = numOutputs;
	m_sampleRate = sampleRate;
	m_maxSamplesPerBlock = samplesPerBlock;
	m_bitDepth = static_cast<EBitDepth>(bitDepth);
}

void AudioDebugger::setAutoWriteOutputFile(bool bEnable)
{
	m_autoWriteOutputFile = bEnable;
}

void AudioDebugger::setRptFileMaxLength(int numSecs)
{
	m_rptFileMaxLengthSecs = numSecs;
}

bool AudioDebugger::getAsyncWriterStatusChanged()
{
	if (m_debugWriterStatusChanged)
	{
		m_debugWriterStatusChanged = false;
		return true;
	}

	return false;
}

bool AudioDebugger::getAsyncWriterGotDisconnected()
{
	if (m_debugWriterDisconnected)
	{
		m_debugWriterDisconnected = false;
		return true;
	}

	return false;
}

const std::string& AudioDebugger::getLastFileName() const
{
	return m_debugFilepath;
}

void AudioDebugger::savePluginState(DebugProcessor* processor)
{
	std::vector<uint8_t> state;
	processor->getStateInformationHook(state);
	if (!state.empty())
	{
		m_debugWriter->setPluginState(state.data(), state.size(),
			processor->getStateId());
	}
}

void AudioDebugger::prepare(DebugProcessor* processor)
{
	m_debugBufferWritePos = 0;
	m_debugBlockCount = 0;

	if (!m_bIsRegenerating)
	{
		FileHeader header;
		header.sample_rate = m_sampleRate;
		header.block_size = m_maxSamplesPerBlock;

		header.in_channels = m_numInputs;
		header.out_channels = m_numOutputs;
		header.bit_depth = m_bitDepth;

		if (m_autoWriteOutputFile)
		{
			std::lock_guard<std::mutex> lock(m_writerMutex);

			m_debugWriter.reset(new FileWriter(m_debugFilepath, header));

			savePluginState(processor);

			m_debugWriterStatusChanged = true;

			if (!m_debugWriter->start())
			{
				ensure(false);
				m_debugWriter.reset(nullptr);
			}
		}
	}
}

void AudioDebugger::terminate()
{
	m_debugBufferWritePos = 0;
	m_debugBlockCount = 0;
}

std::string generateLogFilename(const std::string& fileName)
{
	auto now = std::chrono::system_clock::now();
	auto time = std::chrono::system_clock::to_time_t(now);
	std::tm timeinfo;

#ifdef _WIN32
	localtime_s(&timeinfo, &time);
#else
	localtime_r(&time, &timeinfo);
#endif

	std::ostringstream oss;
	oss << fileName << "_"
		<< std::put_time(&timeinfo, "%Y%m%d_%H%M%S")
		<< ".rpt";

	return oss.str();
}

void AudioDebugger::setCaptureName(const std::string& fileName, bool bAppendDate)
{
	static const std::string ext = ".rpt";

	std::string name = fileName;
	if (name.size() >= ext.size()
		&& name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
	{
		name.erase(name.size() - ext.size());
	}

	if (m_instanceId != 0)
		name += "_" + std::to_string(m_instanceId);

	if (bAppendDate)
		m_debugFilepath = generateLogFilename(name);
	else
		m_debugFilepath = name + ext;
}

void AudioDebugger::setBlockAddedCallback(BlockAddedFunc func)
{
	if (m_debugWriter)
		m_debugWriter->setBlockAddedCallback(func);
}

bool AudioDebugger::beginFrame(uint32_t numSamples)
{
	if (!m_debugWriter)
		return false;

	m_debugWriter->beginFrame(numSamples);
	return true;
}

void AudioDebugger::pushAllInputSamples(int numSamples, int numChannels, const float* const* buffer)
{
	pushInDebugSamples(numSamples, [&](auto& io)
	{
		for (int32_t i = 0; i < numSamples; i++)
		{
			for (uint8_t chan = 0; chan < numChannels; chan++)
			{
				io.push(buffer[chan][i]);
			}
		}
	});
}

void AudioDebugger::pushAllOutputSamples(int numSamples, int numChannels, const float* const* buffer)
{
	pushOutDebugSamples(numSamples, [&](auto& io)
	{
		for (int32_t i = 0; i < numSamples; i++)
		{
			for (uint8_t chan = 0; chan < numChannels; chan++)
			{
				io.push(buffer[chan][i]);
			}
		}
	});
}

void AudioDebugger::pushInDebugSamples(int numSamples, const SampleAccFunc f)
{
	std::lock_guard<std::mutex> lock(m_writerMutex);

	if (!m_bIsRegenerating)
	{
		checkMaxLengthReached_();
	}

	if (!m_bIsRegenerating && m_debugWriter)
	{
		m_debugWriter->writeAudioBlockDirect(BlockType::AUDIOIN, m_debugBlockCount, numSamples * m_numInputs, f);
	}

	//m_debugBufferWritePos += numSamples;
}

void AudioDebugger::pushOutDebugSamples(int numSamples, const SampleAccFunc& f)
{
	std::lock_guard<std::mutex> lock(m_writerMutex);

	if (!m_bIsRegenerating)
	{
		checkMaxLengthReached_();
	}

	if (m_debugWriter) // Output is written to a different Writer in Regenerating mode
	{
		m_debugWriter->writeAudioBlockDirect(BlockType::AUDIOOUT, m_debugBlockCount, numSamples * m_numOutputs, f);
	}

	m_debugBufferWritePos += numSamples;
}

void AudioDebugger::flushAccumulator_(BlockAccumulator& accum)
{
	bool overflowed = false;
	const size_t size = accum.close(overflowed);

	if (overflowed || size == 0)
		return;

	if (auto block = m_debugWriter->beginBlock(accum.type(), m_debugBlockCount))
	{
		block.append(accum.buffer(), size);
		block.commit();
	}
}

void AudioDebugger::endFrame()
{
	if (!m_debugWriter)
		return;

	std::lock_guard<std::mutex> lock(m_writerMutex);

	if (!m_bIsRegenerating)
	{
		flushAccumulator_(m_midiAccum);
	}

	flushAccumulator_(m_metadataAccum);
	flushAccumulator_(m_customAccum);

	m_debugWriter->endFrame();

	m_debugBlockCount++;
}

void AudioDebugger::checkMaxLengthReached_()
{
	if (m_debugWriter && m_rptFileMaxLengthSecs >= 0)
	{
		float elapsed = (m_debugBufferWritePos / (float)m_debugWriter->getCurrentHeader().sample_rate);
		if (elapsed >= (float)m_rptFileMaxLengthSecs)
		{
			m_maxLengthReached.store(true, std::memory_order_relaxed);
		}
	}
}

void AudioDebugger::serviceFromMessageThread()
{
	if (m_maxLengthReached.exchange(false))
	{
		resetWriter();
		m_autoWriteOutputFile = false;
		m_debugWriterDisconnected = true;
	}
}

void AudioDebugger::startRegenerate(const FileHeader& header, DebugProcessor* processor, std::string& out_genFileName)
{
	m_bIsRegenerating = true;

	m_debugBufferWritePos = 0;
	m_debugBlockCount = 0;

	std::lock_guard<std::mutex> lock(m_writerMutex);

	FileHeader newHeader = FileHeader(header);
	newHeader.blocks_count = 0;
	newHeader.total_samples = 0;

	m_numInputs = header.in_channels;
	m_numOutputs = header.out_channels;

	out_genFileName = m_debugFilepath + ".gen";
	m_debugWriter.reset(new FileWriter(out_genFileName, newHeader));

	savePluginState(processor);

	if (!m_debugWriter->start())
	{
		ensure(false);
		m_debugWriter.reset(nullptr);
	}
}

void AudioDebugger::endRegenerate()
{
	m_bIsRegenerating = false;
	m_debugWriter.reset();
}

int32_t AudioDebugger::getCurrentBufferTimestamp() const
{
	return m_debugBufferWritePos;
}

void AudioDebugger::setNumInputsOutputs(uint8_t in_numInputs, uint8_t in_numOutputs)
{
	m_numInputs = in_numInputs;
	m_numOutputs = in_numOutputs;
}

int32_t AudioDebugger::getSampleIdInCurrentBuffer(int numSamples, int sampleId, bool lookback, int latencySamples) const
{
	return m_debugBufferWritePos + sampleId;
}


void AudioDebugger::forceBreakSample(uint16_t currentSampleId)
{
	if (m_sampleToDebug == currentSampleId)
	{
		__debugbreak();
	}
}


namespace Metadata {

template <typename T>
bool add_num(BlockWriter& block, T num)
{
	static_assert(std::is_trivially_copyable<T>::value, "add_num requires a trivially copyable type");
	return block.append(&num, sizeof(T));
}

uint32_t add_uint8(BlockWriter& block, uint8_t num) { return add_num(block, num); }
uint32_t add_int8(BlockWriter& block, int8_t num) { return add_num(block, num); }
uint32_t add_uint16(BlockWriter& block, uint16_t num) { return add_num(block, num); }
uint32_t add_int16(BlockWriter& block, int16_t num) { return add_num(block, num); }
uint32_t add_uint32(BlockWriter& block, uint32_t num) { return add_num(block, num); }
uint32_t add_uint64(BlockWriter& block, uint64_t num) { return add_num(block, num); }
uint32_t add_int32(BlockWriter& block, int32_t num) { return add_num(block, num); }
uint32_t add_float(BlockWriter& block, float num) { return add_num(block, num); }
uint32_t add_double(BlockWriter& block, double num) { return add_num(block, num); }

uint32_t add_string(BlockWriter& block, const char* text, size_t textSize)
{
	if (textSize > UINT32_MAX)
		return 0;

	const uint32_t len = static_cast<uint32_t>(textSize);

	uint8_t* dst = block.reserve(sizeof(uint32_t) + textSize);
	if (dst == nullptr)
		return 0;

	std::memcpy(dst, &len, sizeof(uint32_t));

	if (textSize > 0)
		std::memcpy(dst + sizeof(uint32_t), text, textSize);

	return textSize + sizeof(uint32_t);;
}

template<typename T>
uint32_t read_number(const uint8_t* data, T& out_num)
{
	std::memcpy(&out_num, data, sizeof(T));
	return sizeof(T);
}

uint32_t read_uint8(const uint8_t* data, uint8_t& out_num) { return read_number(data, out_num); }
uint32_t read_int8(const uint8_t* data, int8_t& out_num) { return read_number(data, out_num); }
uint32_t read_uint16(const uint8_t* data, uint16_t& out_num) { return read_number(data, out_num); }
uint32_t read_int16(const uint8_t* data, int16_t& out_num) { return read_number(data, out_num); }
uint32_t read_uint32(const uint8_t* data, uint32_t& out_num) { return read_number(data, out_num); }
uint32_t read_uint64(const uint8_t* data, uint64_t& out_num) { return read_number(data, out_num); }
uint32_t read_int32(const uint8_t* data, int32_t& out_num) { return read_number(data, out_num); }
uint32_t read_float(const uint8_t* data, float& out_num) { return read_number(data, out_num); }
uint32_t read_double(const uint8_t* data, double& out_num) { return read_number(data, out_num); }

void addMarkerRegular(BlockWriter& block, int magic, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color, const char* text, size_t textSize)
{
	add_uint32(block, magic);
	add_uint32(block, timestamp);
	add_int8(block, chanId);
	add_uint8(block, paintType);
	add_uint8(block, color);

	switch (magic)
	{
	case MARKER_BUFFER_START:
		// No text
		break;
	case MARKER_TEXT:
		add_string(block, text, textSize);
		break;
	default:
		ensure(false); // Unhandled
	}
}


void addMidiEvent(BlockWriter& block, int timestamp, int dataSize, const uint8_t* data)
{
	uint32_t totalSize = sizeof(uint32_t) + sizeof(uint8_t) + dataSize;
	uint8_t* dst = block.reserve(totalSize);
	if (dst == nullptr)
		return;

	std::memcpy(dst, &timestamp, sizeof(uint32_t));
	std::memcpy(dst + sizeof(uint32_t), &dataSize, sizeof(uint8_t));
	std::memcpy(dst + sizeof(uint32_t) + sizeof(uint8_t), data, dataSize);
}

std::vector<RawMidiMessage> readAllMidiEvents(const std::vector<uint8_t>& data)
{
	std::vector<RawMidiMessage> result;
	int i = 0;
	while (i + 5 <= static_cast<int>(data.size()))
	{
		RawMidiMessage msg{};
		msg.position = data[i]
			| (data[i + 1] << 8)
			| (data[i + 2] << 16)
			| (data[i + 3] << 24);
		msg.dataSize = data[i + 4];
		i += 5;
		if (i + msg.dataSize > static_cast<int>(data.size()))
			break;
		std::memcpy(msg.data, data.data() + i, msg.dataSize);
		i += msg.dataSize;
		result.push_back(msg);
	}

	return result;
}

void addMarkerCustom(BlockWriter& block, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color, const std::vector<uint8_t>& inData)
{
	addMarkerCustomHeaderOnly(block, timestamp, chanId, paintType, color);

	uint32_t data_len = static_cast<uint32_t>(inData.size());
	add_uint32(block, data_len);

	uint8_t* dst = block.reserve(data_len);
	assert(dst);
	if (dst)
	{
		std::memcpy(dst, inData.data(), data_len);
	}
}

void addMarkerCustomHeaderOnly(BlockWriter& block, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color)
{
	add_uint32(block, EMetadataType::MARKER_CUSTOM);
	add_uint32(block, timestamp);
	add_int8(block, chanId);
	add_uint8(block, paintType);
	add_uint8(block, color);
}

Marker readMarker(const uint8_t* data, uint32_t& out_readSize)
{
	int offset = 0;

	uint32_t magic_val, timestamp = 0;
	uint8_t paintType = 0, colorId = 0;
	int8_t chanId = 0;
	offset += read_uint32(data + offset, magic_val);

	if (!isValidMarker(magic_val))
	{
		ensure(false);
		return Marker{};
	}

	offset += read_uint32(data + offset, timestamp);
	offset += read_int8(data + offset, chanId);
	offset += read_uint8(data + offset, paintType);
	offset += read_uint8(data + offset, colorId);

	switch (magic_val)
	{
	case MARKER_BUFFER_START:
	{
		out_readSize = offset;
		return Marker(magic_val, timestamp, "", chanId, paintType, colorId);
	}
	case MARKER_TEXT:
	{
		uint32_t str_len;
		offset += read_uint32(data + offset, str_len);

		auto text = std::string(data + offset, data + offset + str_len);
		offset += str_len;

		out_readSize = offset;
		return Marker(magic_val, timestamp, std::move(text), chanId, paintType, colorId);
		break;
	}
	case MARKER_CUSTOM:
	{
		uint32_t data_len;
		offset += read_uint32(data + offset, data_len);

		auto customData = std::vector<uint8_t>(data + offset, data + offset + data_len);
		offset += data_len;

		out_readSize = offset;
		return Marker(magic_val, timestamp, std::move(customData), chanId, paintType, colorId);
		break;
	}
	default:
		ensure(false); // Unhandled
		return Marker{};
	}
}

void addMarkerText(BlockWriter& block, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color, const char* text, size_t textSize)
{
	addMarkerRegular(block, EMetadataType::MARKER_TEXT, timestamp, chanId, paintType, color, text, textSize);
}

void addBufferStartMarker(BlockWriter& block, int timestamp)
{
	addMarkerRegular(block, EMetadataType::MARKER_BUFFER_START, timestamp, -1, 0, EMarkerColor::DottedGrey, "", 0);
}

} // namespace Metadata

ScopedDebugBlock::ScopedDebugBlock(DebugProcessor& processor, uint32_t numSamples)
	: m_debugger(DebugProcessor::DebuggerAccess::get(processor))
{
	m_bActive = m_debugger.beginFrame(numSamples);
	if (m_bActive)
	{
		auto& metadataBlock = m_debugger.metadataBlock();
		auto timestamp = m_debugger.getCurrentBufferTimestamp();
		Metadata::addBufferStartMarker(metadataBlock, timestamp);
	}
}

ScopedDebugBlock::~ScopedDebugBlock()
{
	if (m_bActive)
	{
		m_debugger.endFrame();
	}
}

void ScopedDebugBlock::writeInput(int numSamples, int numChannels, const float* const* buffer)
{
	if (m_bActive)
		m_debugger.pushAllInputSamples(numSamples, numChannels, buffer);
}

void ScopedDebugBlock::writeInput(int numSamples, const AudioDebugger::SampleAccFunc& f)
{
	if (m_bActive)
		m_debugger.pushInDebugSamples(numSamples, f);
}

void ScopedDebugBlock::writeOutput(int numSamples, int numChannels, const float* const* buffer)
{
	if (m_bActive)
		m_debugger.pushAllOutputSamples(numSamples, numChannels, buffer);
}

void ScopedDebugBlock::writeOutput(int numSamples, const AudioDebugger::SampleAccFunc& f)
{
	if (m_bActive)
		m_debugger.pushOutDebugSamples(numSamples, f);
}

void ScopedDebugBlock::pushMidiEvent(int position, int dataSize, const uint8_t* data)
{
	if (m_bActive)
	{
		auto& midiBlock = m_debugger.midiEventBlock();
		Metadata::addMidiEvent(midiBlock, position, dataSize, data);
	}
}

static BlockWriter m_nullBlock{};

BlockWriter& ScopedDebugBlock::metadata() noexcept
{
	return m_bActive ? m_debugger.metadataBlock() : m_nullBlock;
}

BlockWriter& ScopedDebugBlock::custom() noexcept
{
	return m_bActive ? m_debugger.customBlock() : m_nullBlock;
}

int32_t ScopedDebugBlock::getCurrentBufferTimestamp() const
{
	return m_debugger.getCurrentBufferTimestamp();
}

} // namespace AudioDebug
