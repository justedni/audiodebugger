// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>
#include <string>
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

	// Abstract interface
	virtual int getLatencySamples(int samplesPerBlock) const { (void)samplesPerBlock; return 0; }
	virtual void prepareToPlayHook(double sampleRate, int samplesPerBlock) = 0;
	virtual void releaseResourcesHook() {}
	virtual void processBlockHook(int numSamples, float** inputs, const std::vector<RawMidiMessage>& midiMessages) = 0;
	virtual void getStateInformationHook(std::vector<uint8_t>& out_data) { (void)out_data; }
	virtual void setStateInformationHook(const uint8_t* data, size_t size) { (void)data; (void)size; }
	virtual uint32_t getStateId() const { return 0; }
	virtual DebugParameterBridge* getParameterBridge() { return nullptr; }

	void setCapturePath(const std::string& fullPath);
	void initDebug(double sampleRate, int samplesPerBlock, int numInputs, int numOutputs, uint8_t bitDepth);
	void startDebugCapture();
	void terminateDebugCapture();
	bool isRegenerating() const;
	void setDebugInOutCount(uint8_t in_numInputs, uint8_t in_numOutputs);

	class DebuggerAccess;
private:
	friend class DebuggerAccess;

	AudioDebugger& getDebugger() noexcept { return *m_debugger; }
	const AudioDebugger& getDebugger() const noexcept { return *m_debugger; }

	std::unique_ptr<AudioDebugger> m_debugger;
};

} // namespace AudioDebug
