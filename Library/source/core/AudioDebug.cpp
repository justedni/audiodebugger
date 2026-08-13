// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/AudioDebug.h"

#include "core/DebuggerAccess.h"
#include "core/FileWriter.h"

#include "core/AudioDebugInterface.h"

namespace AudioDebug {

SampleWriter::SampleWriter(uint8_t* dest, size_t maxBytes, uint8_t bitDepth) noexcept
	: m_dest(dest)
	, m_pos(0)
	, m_maxBytes(maxBytes)
	, m_bitDepth(bitDepth)
{
}


BlockWriter::BlockWriter() = default;

BlockWriter::BlockWriter(uint8_t* buffer, size_t capacity, uint8_t bitDepth) noexcept
    : m_owner(nullptr), m_data(buffer), m_capacity(capacity), m_bitDepth(bitDepth)
{
}

BlockWriter::~BlockWriter()
{
    if (m_owner != nullptr)
    {
        FileWriter* owner = m_owner;
        clear();
        owner->abandonBlock();
    }
}

BlockWriter& BlockWriter::operator=(BlockWriter&& other) noexcept
{
    if (this != &other)
    {
        if (m_owner != nullptr)
        {
            FileWriter* owner = m_owner;
            clear();
            owner->abandonBlock();
        }
        moveFrom(other);
    }
    return *this;
}

bool BlockWriter::append(const void* data, size_t numBytes) noexcept
{
    if (uint8_t* dst = reserve(numBytes))
    {
        std::memcpy(dst, data, numBytes);
        return true;
    }
    assert(false);
    return false;
}

uint8_t* BlockWriter::reserve(size_t numBytes) noexcept
{
	if (m_data == nullptr)
		return nullptr;

	if (m_overflow || numBytes > m_capacity - m_cursor)
	{
		m_overflow = true;
		return nullptr;
	}

	uint8_t* dst = m_data + m_cursor;
	m_cursor += numBytes;
	return dst;
}

void BlockWriter::moveFrom(BlockWriter& other) noexcept
{
    m_owner = other.m_owner;
    m_data = other.m_data;
    m_capacity = other.m_capacity;
    m_cursor = other.m_cursor;
    m_bitDepth = other.m_bitDepth;
    m_overflow = other.m_overflow;
    other.clear();
}

void BlockWriter::clear() noexcept
{
    m_owner = nullptr;
    m_data = nullptr;
    m_capacity = 0;
    m_cursor = 0;
    m_bitDepth = 0;
    m_overflow = false;
}

bool BlockWriter::commit() noexcept
{
    if (m_owner == nullptr)
        return false;

    FileWriter* owner = m_owner;
    const bool   ok = !m_overflow;
    const size_t bytes = m_cursor;
    clear();

    return owner->finishBlock(ok, bytes);
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

void SampleWriter::push(float sample) noexcept
{
	int32_t value = AudioDebugger::floatToInt(sample, (EBitDepth)m_bitDepth);
	push(value);
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
		m_debugger.writeInputFrame(numSamples, numChannels, buffer);
}

void ScopedDebugBlock::writeInput(int numSamples, const AudioDebugger::SampleAccFunc& f)
{
	if (m_bActive)
		m_debugger.writeInputFrame(numSamples, f);
}

void ScopedDebugBlock::writeOutput(int numSamples, int numChannels, const float* const* buffer)
{
	if (m_bActive)
		m_debugger.writeOutputFrame(numSamples, numChannels, buffer);
}

void ScopedDebugBlock::writeOutput(int numSamples, const AudioDebugger::SampleAccFunc& f)
{
	if (m_bActive)
		m_debugger.writeOutputFrame(numSamples, f);
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
