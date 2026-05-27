// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

namespace AudioDebug {

struct RawMidiMessage;

class DebugProcessor
{
public:
	DebugProcessor();
	virtual ~DebugProcessor();

	// Abstract interface
	virtual int getLatencySamples(int samplesPerBlock) const { return 0; }
	virtual void prepareToPlayHook(double sampleRate, int samplesPerBlock) {}
	virtual void processBlockHook(int numSamples, float** inputs, const std::vector<RawMidiMessage>& midiMessages) {}

	virtual void setSliderParam(uint8_t id, float value) {}
	virtual float getSliderParam(uint8_t id) const { return 0.0f; }
	virtual void setButtonEnabled(uint8_t id, bool bEnabled) {}
	virtual bool getButtonEnabled(uint8_t id) const { return false; }
};

} // namespace AudioDebug
