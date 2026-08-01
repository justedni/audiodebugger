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
	virtual int getLatencySamples(int samplesPerBlock) const { (void)samplesPerBlock; return 0; }
	virtual void prepareToPlayHook(double sampleRate, int samplesPerBlock) { (void)sampleRate; (void)samplesPerBlock; }
	virtual void releaseResourcesHook() {}
	virtual void processBlockHook(int numSamples, float** inputs, const std::vector<RawMidiMessage>& midiMessages) { (void)numSamples; (void)inputs; (void)midiMessages; }

	virtual void setSliderParam(uint8_t id, float value) { (void)id; (void)value; }
	virtual float getSliderParam(uint8_t id) const { (void)id; return 0.0f; }
	virtual void setButtonEnabled(uint8_t id, bool bEnabled) { (void)id; (void)bEnabled; }
	virtual bool getButtonEnabled(uint8_t id) const { (void)id; return false; }
};

} // namespace AudioDebug
