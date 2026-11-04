// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/AudioDebug.h"
#include "core/AudioDebugInterface.h"

namespace AudioDebug {

struct RawMidiMessage;

class ScopedJuceDebugBlock
{
public:
	ScopedJuceDebugBlock(DebugProcessor& processor,
						 juce::AudioBuffer<float>& buffer,
						 const juce::MidiBuffer& midiMessages,
						 int numInputChannels,
						 int numOutputChannels) noexcept
		: m_block(processor, buffer.getNumSamples())
		, m_buffer(buffer)
		, m_numInputChannels(numInputChannels)
		, m_numOutputChannels(numOutputChannels)
	{
		if (!m_block)
			return;

		m_block.writeInput(m_buffer.getNumSamples(),
						   m_numInputChannels,
						   m_buffer.getArrayOfWritePointers());

		for (const auto metadata : midiMessages)
		{
			// RawMidiMessage carries at most 3 bytes, so anything longer
			// (SysEx, but also any 4+ byte message) cannot be represented.
			if (metadata.numBytes <= 0 || metadata.numBytes > 3)
				continue;

			m_block.pushMidiEvent(metadata.samplePosition, metadata.numBytes, metadata.data);
		}
	}

	~ScopedJuceDebugBlock()
	{
		if (m_block)
		{
			m_block.writeOutput(m_buffer.getNumSamples(),
								m_numOutputChannels,
								m_buffer.getArrayOfWritePointers());
		}

		// m_block is declared first, so it is destroyed last: it commits the
		// MIDI queue and closes the block after the output push above.
	}

	ScopedJuceDebugBlock(const ScopedJuceDebugBlock&)            = delete;
	ScopedJuceDebugBlock& operator=(const ScopedJuceDebugBlock&) = delete;
	ScopedJuceDebugBlock(ScopedJuceDebugBlock&&)                 = delete;
	ScopedJuceDebugBlock& operator=(ScopedJuceDebugBlock&&)      = delete;

	int32_t getCurrentBufferTimestamp() const;

	explicit operator bool() const noexcept { return static_cast<bool>(m_block); }
	bool isActive() { return operator bool(); }

	ScopedDebugBlock& raw() noexcept { return m_block; }

private:
	ScopedDebugBlock m_block;
	juce::AudioBuffer<float>& m_buffer;
	int m_numInputChannels;
	int m_numOutputChannels;
};

class JuceDebugProcessor : public juce::AudioProcessor
						 , public AudioDebug::DebugProcessor
{
public:
	explicit JuceDebugProcessor(const juce::AudioProcessor::BusesProperties& ioLayouts);
	~JuceDebugProcessor() override;

	virtual void prepareToPlayImpl(double sampleRate, int samplesPerBlock) { (void)sampleRate; (void)samplesPerBlock; }
	virtual void releaseResourcesImpl() {}
	virtual void processBlockImpl(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages, ScopedJuceDebugBlock* scope) = 0;

	virtual void onCaptureStarted(juce::AudioBuffer<float>& buffer) {}

	// juce::AudioProcessor
	void prepareToPlay(double sampleRate, int samplesPerBlock) final;
	void releaseResources() final;
	void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) final;

protected:
	// AudioDebug::DebugProcessor
	void prepareToPlayHook(double sampleRate, int samplesPerBlock) final;
	void releaseResourcesHook() final;
	void processBlockHook(int numSamples, float** inputs, const std::vector<RawMidiMessage>& midiMessages) final;
	void getStateInformationHook(std::vector<uint8_t>& out_data) final;
	void setStateInformationHook(const uint8_t* data, size_t size) final;

private:
	juce::MidiBuffer m_regenerateMidiMessages;
	bool m_isPlaying = false;
};

} // namespace AudioDebug
