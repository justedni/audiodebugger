// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/AudioDebugger.h"

#include "core/FileWriter.h"
#include "core/RptFileFormat.h"
#include "core/AudioDebug.h"
#include "core/AudioDebugInterface.h"

#include <sstream>
#include <chrono>
#include <iomanip>

namespace AudioDebug {

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

} // namespace AudioDebug
