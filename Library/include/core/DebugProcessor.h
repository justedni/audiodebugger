// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>
#include <memory>

namespace AudioDebug {

struct RawMidiMessage;
class AudioDebugger;
class DebugParameterBridge;

class DebugProcessor
{
public:
	DebugProcessor();
	virtual ~DebugProcessor();

	AudioDebugger&       getDebugger()       noexcept { return *m_debugger; }
	const AudioDebugger& getDebugger() const noexcept { return *m_debugger; }

	// Abstract interface
	virtual int getLatencySamples(int samplesPerBlock) const { (void)samplesPerBlock; return 0; }
	virtual void prepareToPlayHook(double sampleRate, int samplesPerBlock) = 0;
	virtual void releaseResourcesHook() {}
	virtual void processBlockHook(int numSamples, float** inputs, const std::vector<RawMidiMessage>& midiMessages) = 0;
	virtual void getStateInformationHook(std::vector<uint8_t>& out_data) { (void)out_data; }
	virtual void setStateInformationHook(const uint8_t* data, size_t size) { (void)data; (void)size; }
	virtual uint32_t getStateId() const { return 0; }
	virtual DebugParameterBridge* getParameterBridge() { return nullptr; }

private:
	std::unique_ptr<AudioDebugger> m_debugger;
};

} // namespace AudioDebug
