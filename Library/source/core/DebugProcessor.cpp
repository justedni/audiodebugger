// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/AudioDebugInterface.h"

#include "core/AudioDebugger.h"

namespace AudioDebug {

DebugProcessor::DebugProcessor()
	: m_debugger(std::make_unique<AudioDebugger>())
{
}

DebugProcessor::~DebugProcessor() = default;

void DebugProcessor::setCapturePath(const std::string& fullPath, bool bAppendDate)
{
	m_debugger->setCaptureName(fullPath, bAppendDate);
}

void DebugProcessor::setDebugCaptureEnabled(bool bEnabled)
{
	m_debugger->setAutoWriteOutputFile(bEnabled);
}

void DebugProcessor::startDebugCapture(double sampleRate, int samplesPerBlock, int numInputs, int numOutputs, uint8_t bitDepth)
{
	m_debugger->initDebugProcessor(sampleRate, samplesPerBlock, numInputs, numOutputs, bitDepth);
	m_debugger->prepare(this);
}

void DebugProcessor::terminateDebugCapture()
{
	m_debugger->terminate();
}

bool DebugProcessor::isRegenerating() const
{
	return m_debugger->isRegenerating();
}

void DebugProcessor::setDebugInOutCount(uint8_t in_numInputs, uint8_t in_numOutputs)
{
	m_debugger->setNumInputsOutputs(in_numInputs, in_numOutputs);
}

int DebugProcessor::getDebugInChannels() const { return m_debugger->getNumInputs(); }
int DebugProcessor::getDebugOutChannels() const { return m_debugger->getNumOutputs(); }

#pragma optimize("", off)
void DebugProcessor::debugBreakSampleId(uint32_t sampleId, bool bForceBreak)
{
	if (isRegenerating())
	{
		const int32_t sampleToDebug = m_debugger->getSampleToDebug();
		if ((m_debugger->getCurrentBufferTimestamp() + sampleId) == sampleToDebug)
		{
			if (bForceBreak)
			{
				__debugbreak();
			}
			else
			{
				bool bAddBreakPointHere = true;
			}
		}
	}
}
#pragma optimize("", on)

} // namespace AudioDebug
