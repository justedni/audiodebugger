// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SamplePluginProcessor.h"
#include "SamplePluginEditor.h"

#include "core/AudioDebug.h"

#include <BinaryData.h>

SamplePluginProcessor::SamplePluginProcessor()
    : AudioDebug::JuceDebugProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    m_formatManager.registerBasicFormats();
    m_synth.addVoice(new juce::SamplerVoice());
}

SamplePluginProcessor::~SamplePluginProcessor()
{
}

bool SamplePluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void SamplePluginProcessor::prepareToPlayImpl(double sampleRate, int samplesPerBlock)
{
    (void)samplesPerBlock;

    m_synth.setCurrentPlaybackSampleRate(sampleRate);
    m_synth.clearSounds();

    auto stream = std::make_unique<juce::MemoryInputStream>(BinaryData::StStrings_64_wav,
        static_cast<size_t>(BinaryData::StStrings_64_wavSize),
        false);

    std::unique_ptr<juce::AudioFormatReader> reader(m_formatManager.createReaderFor(std::move(stream)));

    if (reader == nullptr)
    {
        jassertfalse;
        return;
    }

    juce::BigInteger allNotes;
    allNotes.setRange(0, 128, true);

    static constexpr int kRootMidiNote = 64;

    m_synth.addSound(new juce::SamplerSound("sample",
        *reader,
        allNotes,
        kRootMidiNote,
        0.0,
        0.1,
        10.0));
}

void SamplePluginProcessor::processBlockImpl(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages, AudioDebug::ScopedJuceDebugBlock* scope)
{
    m_synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());

    if (scope)
    {
        for (const auto& midi : midiMessages)
        {
            const auto& msg = midi.getMessage();
            if (msg.isNoteOn())
            {
                int32_t pos = AudioDebug::AudioDebugger::get()->getCurrentBufferTimestamp() + (int32_t)std::round(msg.getTimeStamp());
                auto& metadata = scope->raw().metadata();
                constexpr char* text("NoteOn marker");
                AudioDebug::Metadata::addMarkerText(metadata, pos, 0, 0, 0, text, strlen(text));
            }
        }
    }
}

juce::AudioProcessorEditor* SamplePluginProcessor::createEditor()
{
    return new SamplePluginEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SamplePluginProcessor();
}
