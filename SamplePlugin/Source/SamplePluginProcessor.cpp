// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SamplePluginProcessor.h"
#include "SamplePluginEditor.h"

#include "core/AudioDebug.h"

SamplePluginProcessor::SamplePluginProcessor()
    : AudioDebug::JuceDebugProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

SamplePluginProcessor::~SamplePluginProcessor()
{
}

bool SamplePluginProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void SamplePluginProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midiMessages)
{
    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    float* const* data = buffer.getArrayOfWritePointers();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
    {
        buffer.clear(i, 0, buffer.getNumSamples());
    }

    auto* playHead = getPlayHead();
    juce::AudioPlayHead::CurrentPositionInfo info;
    playHead->getCurrentPosition(info);

    // Useful toggle for MIDI synths to only send data when DAW is playing
    if (m_isPlaying != info.isPlaying)
    {
        auto* pDebugger = AudioDebug::AudioDebugger::get();
        if (info.isPlaying && pDebugger) {
            pDebugger->prepare(this);
        }

        m_isPlaying = info.isPlaying;
    }

    processInternal(buffer.getNumSamples(), midiMessages, data);
}

void SamplePluginProcessor::processInternal(int numSamples, juce::MidiBuffer& midiMessages, float* const* data)
{
    auto* pDebugger = AudioDebug::AudioDebugger::get();
    if (pDebugger && m_isPlaying)
    {
        // Push Midi events to Debugger
        {
            for (const auto metadata : midiMessages)
            {
                auto message = metadata.getMessage();
                if (!message.isSysEx())
                {
                    pDebugger->pushMidiEvent(metadata.samplePosition, message.getRawDataSize(), message.getRawData());
                }
            }

            pDebugger->writeAllMidiEvents();
        }

        // Push Input Data
        pDebugger->pushAllInputSamples(numSamples, getTotalNumInputChannels(), data);

        // Push Output Data
        pDebugger->pushAllOutputSamples(numSamples, getTotalNumOutputChannels(), data);

        // Add metadata marker for Start of Buffer
        std::vector<uint8_t> meta_data;
        AudioDebug::Metadata::addBufferStartMarker(meta_data, pDebugger->getCurrentWrittenBufferSize());
        pDebugger->addMetadata(meta_data);

        // Signal end of block (required call)
        pDebugger->sendBlockEnd();
    }
}

void SamplePluginProcessor::processBlockRegenerate(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    processInternal(buffer.getNumSamples(), midiMessages, buffer.getArrayOfWritePointers());
}

juce::AudioProcessorEditor* SamplePluginProcessor::createEditor()
{
    return new SamplePluginEditor(*this);
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
    return new SamplePluginProcessor();
}
