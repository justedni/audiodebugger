// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/AudioDebug.h"

#include "core/FileWriter.h"

#include <assert.h>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "core/DebugProcessor.h"

namespace AudioDebug {

std::unique_ptr<AudioDebugger> AudioDebugger::m_singleton;
std::mutex AudioDebugger::m_mutex;


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

	assert(false);
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

	assert(false);
	return 0;
}

void SampleWriter::push(float sample) noexcept
{
	int32_t value = AudioDebugger::floatToInt(sample, (EBitDepth)m_bitDepth);
	push(value);
}

AudioDebugger::AudioDebugger()
{
}

AudioDebugger::~AudioDebugger()
{
	resetWriter();
}

void AudioDebugger::waitUntilWriterHasFinished()
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
		waitUntilWriterHasFinished();

		m_debugWriter.reset();
	}

	m_debugBufferWritePos = 0;
}

AudioDebugger* AudioDebugger::get()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_singleton.get();
}

void AudioDebugger::create()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_singleton == nullptr)
		m_singleton = std::make_unique<AudioDebugger>();
}

void AudioDebugger::release()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_singleton.get())
		m_singleton.reset();
}

void AudioDebugger::initDebugProcessor(double sampleRate, int samplesPerBlock, int numInputs, int numOutputs, EBitDepth bitDepth)
{
	m_numInputs = numInputs;
	m_numOutputs = numOutputs;
	m_sampleRate = sampleRate;
	m_maxSamplesPerBlock = samplesPerBlock;
	m_bitDepth = bitDepth;
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

void AudioDebugger::prepare(DebugProcessor* processor)
{
	m_debugBufferWritePos = 0;
	m_debugBlockCount = 0;

	if (!m_bIsRegenerating)
	{
		FileHeader header;
		header.version = 1;
		header.sample_rate = m_sampleRate;
		header.block_size = m_maxSamplesPerBlock;

		header.in_channels = m_numInputs;
		header.out_channels = m_numOutputs;
		header.bit_depth = m_bitDepth;

		header.blocks_count = 0;

		if (m_autoWriteOutputFile)
		{
			std::lock_guard<std::mutex> lock(m_writerMutex);

			m_debugWriter.reset(new FileWriter(m_debugFilepath, header));
			m_debugWriterStatusChanged = true;

			if (!m_debugWriter->start())
			{
				assert(false);
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
	if (bAppendDate)
		m_debugFilepath = generateLogFilename(fileName);
	else
		m_debugFilepath = fileName;

	static const std::string ext = ".rpt";
	auto hasExt = (m_debugFilepath.compare(m_debugFilepath.size() - ext.size(), ext.size(), ext) == 0);

	if (!hasExt)
		m_debugFilepath.append(ext);
}

void AudioDebugger::pushMidiEvent(int position, int dataSize, const uint8_t* data)
{
	if (midiBufferSize + 4 + 1 + dataSize > kMidiBufferSize)
	{
		assert(false); // queue full
		return;
	}

	midiBuffer[midiBufferSize++] = static_cast<uint8_t>(position & 0xFF);
	midiBuffer[midiBufferSize++] = static_cast<uint8_t>((position >> 8) & 0xFF);
	midiBuffer[midiBufferSize++] = static_cast<uint8_t>((position >> 16) & 0xFF);
	midiBuffer[midiBufferSize++] = static_cast<uint8_t>((position >> 24) & 0xFF);

	midiBuffer[midiBufferSize++] = dataSize;
	std::memcpy(midiBuffer + midiBufferSize, data, dataSize);
	midiBufferSize += dataSize;
}

void AudioDebugger::writeAllMidiEvents()
{
	std::lock_guard<std::mutex> lock(m_writerMutex);

	if (!m_bIsRegenerating && m_debugWriter && midiBufferSize > 0)
	{
		std::vector<uint8_t> buffer(midiBuffer, midiBuffer + midiBufferSize);
		m_debugWriter->writeBlock(BlockType::MIDI, m_debugBlockCount, std::move(buffer));
	}

	midiBufferSize = 0;
}

std::vector<RawMidiMessage> AudioDebugger::readAllMidiEvents(const std::vector<uint8_t>& data)
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

void AudioDebugger::setBlockAddedCallback(BlockAddedFunc func)
{
	m_debugWriter->setBlockAddedCallback(func);
}

void AudioDebugger::pushAllInputSamples(int numSamples, int numChannels, float* const* buffer)
{
	addInDebugSamples(numSamples, [&](auto& io)
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

void AudioDebugger::pushAllOutputSamples(int numSamples, int numChannels, float* const* buffer)
{
	addOutDebugSamples(numSamples, [&](auto& io)
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

void AudioDebugger::addInDebugSamples(int numSamples, const SampleAccFunc f)
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

void AudioDebugger::addOutDebugSamples(int numSamples, const SampleAccFunc& f)
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

void AudioDebugger::addCustomData(const std::vector<uint8_t>& meta_data)
{
	std::lock_guard<std::mutex> lock(m_writerMutex);

	if (m_debugWriter) // Regenerating mode can also write custom blocks
	{
		m_debugWriter->writeBlock(BlockType::CUSTOM, m_debugBlockCount, meta_data);
	}
}

void AudioDebugger::addMetadata(const std::vector<uint8_t>& meta_data)
{
	std::lock_guard<std::mutex> lock(m_writerMutex);

	if (m_debugWriter)
	{
		m_debugWriter->writeBlock(BlockType::METADATA, m_debugBlockCount, meta_data);
	}
}

void AudioDebugger::sendBlockEnd()
{
	m_debugBlockCount++;
	midiBufferSize = 0;
}

void AudioDebugger::checkMaxLengthReached_()
{
	if (m_debugWriter && m_rptFileMaxLengthSecs >= 0)
	{
		float elapsed = (m_debugBufferWritePos / (float)m_debugWriter->getCurrentHeader().sample_rate);
		if (elapsed >= (float)m_rptFileMaxLengthSecs)
		{
			resetWriter_();
			m_autoWriteOutputFile = false;
			m_debugWriterDisconnected = true;
		}
	}
}

void AudioDebugger::startRegenerate(const FileHeader& header, std::string& out_genFileName)
{
	m_bIsRegenerating = true;

	std::lock_guard<std::mutex> lock(m_writerMutex);

	FileHeader newHeader = FileHeader(header);
	newHeader.blocks_count = 0;

	m_numInputs = header.in_channels;
	m_numOutputs = header.out_channels;

	out_genFileName = m_debugFilepath + ".gen";
	m_debugWriter.reset(new FileWriter(out_genFileName, newHeader));

	if (!m_debugWriter->start())
	{
		assert(false);
		m_debugWriter.reset(nullptr);
	}
}

void AudioDebugger::endRegenerate()
{
	m_bIsRegenerating = false;
	m_debugWriter.reset();
}

int32_t AudioDebugger::getCurrentWrittenBufferSize() const
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


template<typename T>
uint32_t add_num(std::vector<uint8_t>& buffer, T num)
{
	buffer.insert(buffer.end(),
		reinterpret_cast<const uint8_t*>(&num),
		reinterpret_cast<const uint8_t*>(&num) + sizeof(T));

	return sizeof(T);
}

uint32_t Metadata::add_uint8(std::vector<uint8_t>& buffer, uint8_t num) { return add_num(buffer, num); }
uint32_t Metadata::add_int8(std::vector<uint8_t>& buffer, int8_t num) { return add_num(buffer, num); }
uint32_t Metadata::add_uint16(std::vector<uint8_t>& buffer, uint16_t num) { return add_num(buffer, num); }
uint32_t Metadata::add_int16(std::vector<uint8_t>& buffer, int16_t num) { return add_num(buffer, num); }
uint32_t Metadata::add_uint32(std::vector<uint8_t>& buffer, uint32_t num) { return add_num(buffer, num); }
uint32_t Metadata::add_uint64(std::vector<uint8_t>& buffer, uint64_t num) { return add_num(buffer, num); }
uint32_t Metadata::add_int32(std::vector<uint8_t>& buffer, int32_t num) { return add_num(buffer, num); }
uint32_t Metadata::add_float(std::vector<uint8_t>& buffer, float num) { return add_num(buffer, num); }
uint32_t Metadata::add_double(std::vector<uint8_t>& buffer, double num) { return add_num(buffer, num); }

uint32_t Metadata::add_string(std::vector<uint8_t>& buffer, const char* text, size_t textSize)
{
	Metadata::add_uint32(buffer, textSize);

	buffer.insert(buffer.end(), text, text + textSize);

	return textSize + sizeof(uint32_t);
}

template<typename T>
uint32_t read_number(const uint8_t* data, T& out_num)
{
	std::memcpy(&out_num, data, sizeof(T));
	return sizeof(T);
}

uint32_t Metadata::read_uint8(const uint8_t* data, uint8_t& out_num) { return read_number(data, out_num); }
uint32_t Metadata::read_int8(const uint8_t* data, int8_t& out_num) { return read_number(data, out_num); }
uint32_t Metadata::read_uint16(const uint8_t* data, uint16_t& out_num) { return read_number(data, out_num); }
uint32_t Metadata::read_int16(const uint8_t* data, int16_t& out_num) { return read_number(data, out_num); }
uint32_t Metadata::read_uint32(const uint8_t* data, uint32_t& out_num) { return read_number(data, out_num); }
uint32_t Metadata::read_uint64(const uint8_t* data, uint64_t& out_num) { return read_number(data, out_num); }
uint32_t Metadata::read_int32(const uint8_t* data, int32_t& out_num) { return read_number(data, out_num); }
uint32_t Metadata::read_float(const uint8_t* data, float& out_num) { return read_number(data, out_num); }
uint32_t Metadata::read_double(const uint8_t* data, double& out_num) { return read_number(data, out_num); }

void Metadata::addMarkerRegular(std::vector<uint8_t>& meta_data, int magic, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color, const char* text, size_t textSize)
{
	Metadata::add_uint32(meta_data, magic);
	Metadata::add_uint32(meta_data, timestamp);
	Metadata::add_int8(meta_data, chanId);
	Metadata::add_uint8(meta_data, paintType);
	Metadata::add_uint8(meta_data, color);

	switch (magic)
	{
	case MARKER_BUFFER_START:
		// No text
		break;
	case MARKER_TEXT:
		Metadata::add_string(meta_data, text, textSize);
		break;
	default:
		assert(false); // Unhandled
	}
}

void Metadata::addMarkerCustom(std::vector<uint8_t>& meta_data, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color, const std::vector<uint8_t>& inData)
{
	Metadata::add_uint32(meta_data, EMetadataType::MARKER_CUSTOM);
	Metadata::add_uint32(meta_data, timestamp);
	Metadata::add_int8(meta_data, chanId);
	Metadata::add_uint8(meta_data, paintType);
	Metadata::add_uint8(meta_data, color);

	uint32_t data_len = static_cast<uint32_t>(inData.size());
	Metadata::add_uint32(meta_data, data_len);

	meta_data.insert(meta_data.end(), inData.begin(), inData.end());
}

Marker Metadata::readMarker(const uint8_t* data, uint32_t& out_readSize)
{
	int offset = 0;

	uint32_t magic_val, timestamp = 0;
	uint8_t paintType = 0, colorId = 0;
	int8_t chanId = 0;
	offset += Metadata::read_uint32(data + offset, magic_val);

	if (!isValidMarker(magic_val))
	{
		assert(false);
		return Marker{};
	}

	offset += Metadata::read_uint32(data + offset, timestamp);
	offset += Metadata::read_int8(data + offset, chanId);
	offset += Metadata::read_uint8(data + offset, paintType);
	offset += Metadata::read_uint8(data + offset, colorId);

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
		offset += Metadata::read_uint32(data + offset, str_len);

		auto text = std::string(data + offset, data + offset + str_len);
		offset += str_len;

		out_readSize = offset;
		return Marker(magic_val, timestamp, std::move(text), chanId, paintType, colorId);
		break;
	}
	case MARKER_CUSTOM:
	{
		uint32_t data_len;
		offset += Metadata::read_uint32(data + offset, data_len);

		auto customData = std::vector<uint8_t>(data + offset, data + offset + data_len);
		offset += data_len;

		out_readSize = offset;
		return Marker(magic_val, timestamp, std::move(customData), chanId, paintType, colorId);
		break;
	}
	default:
		assert(false); // Unhandled
		return Marker{};
	}
}

void Metadata::addMarkerText(std::vector<uint8_t>& meta_data, int timestamp, int8_t chanId, uint8_t paintType, uint8_t color, const char* text, size_t textSize)
{
	addMarkerRegular(meta_data, EMetadataType::MARKER_TEXT, timestamp, chanId, paintType, color, text, textSize);
}

void Metadata::addBufferStartMarker(std::vector<uint8_t>& meta_data, int timestamp)
{
	addMarkerRegular(meta_data, EMetadataType::MARKER_BUFFER_START, timestamp, -1, 0, EMarkerColor::DottedGrey, "", 0);
}


} // namespace AudioDebug
