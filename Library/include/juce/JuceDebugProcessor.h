// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/DebugProcessor.h"

namespace AudioDebug {

struct RawMidiMessage;

class JuceDebugProcessor : public juce::AudioProcessor
						 , public AudioDebug::DebugProcessor
{
public:
	JuceDebugProcessor(const juce::AudioProcessor::BusesProperties& ioLayouts);
	virtual ~JuceDebugProcessor();

	virtual void processBlockRegenerate(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) = 0;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;

	void prepareToPlayHook(double sampleRate, int samplesPerBlock) override;
	void processBlockHook(int numSamples, float** inputs, const std::vector<RawMidiMessage>& midiMessages) override;
};

} // namespace AudioDebug
