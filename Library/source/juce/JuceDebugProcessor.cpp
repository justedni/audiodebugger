// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "juce/JuceDebugProcessor.h"

#include "core/AudioDebugger.h"

#ifdef _DEBUG
namespace AudioThreadGuard
{
    inline std::atomic<std::thread::id> audioThreadId{};
    inline std::atomic<bool> isSet{ false };

    inline void registerAudioThread() noexcept
    {
        bool expected = false;
        if (isSet.compare_exchange_strong(expected, true))
            audioThreadId.store(std::this_thread::get_id(), std::memory_order_release);
    }

    inline void ensureNotCalledFromAudioThread() noexcept
    {
        bool bIsAudioThread = isSet.load(std::memory_order_acquire)
            && std::this_thread::get_id() == audioThreadId.load(std::memory_order_relaxed);

        if (bIsAudioThread)
        {
            // Assert or break here
            bool bBreakHere = true;
        }
    }
}

void* operator new(size_t size)
{
    AudioThreadGuard::ensureNotCalledFromAudioThread();

    void* ptr = std::malloc(size);
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

void* operator new[](size_t size)
{
    AudioThreadGuard::ensureNotCalledFromAudioThread();

    void* ptr = std::malloc(size);
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    AudioThreadGuard::ensureNotCalledFromAudioThread();

    return std::malloc(size == 0 ? 1 : size);
}
#endif

namespace AudioDebug {

JuceDebugProcessor::JuceDebugProcessor(const juce::AudioProcessor::BusesProperties& ioLayouts)
    : juce::AudioProcessor(ioLayouts)
{
}

JuceDebugProcessor::~JuceDebugProcessor() = default;

void JuceDebugProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    m_isPlaying = false;

    initDebug(
        std::round(sampleRate),
        samplesPerBlock,
        getTotalNumInputChannels(),
        getTotalNumOutputChannels(),
        AudioDebug::EBitDepth::Int32);

    prepareToPlayImpl(sampleRate, samplesPerBlock);
}

void JuceDebugProcessor::releaseResources()
{
    releaseResourcesImpl();
}

void JuceDebugProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    if (isRegenerating())
        return;

#ifdef _DEBUG
    AudioThreadGuard::registerAudioThread();
#endif

    float* const* data = buffer.getArrayOfWritePointers();

    buffer.clear();

    if (auto* procPlayHead = getPlayHead())
    {
        auto pos = procPlayHead->getPosition();
        if (pos.hasValue())
        {
            auto isPlaying = pos->getIsPlaying();

            // Useful toggle for MIDI synths to only send data when DAW is playing
            if (m_isPlaying != isPlaying)
            {
                if (isPlaying) {
                    startDebugCapture();
                }

                m_isPlaying = isPlaying;
            }
        }
    }

    if (m_isPlaying)
    {
        ScopedJuceDebugBlock scope(*this,
            buffer,
            midiMessages,
            getTotalNumInputChannels(),
            getTotalNumOutputChannels());

        // Please don't move out of the if statement! The ScopedJuceDebugBlock uses RAII
        // (renderBlock needs to be called before the block goes out of scope)
        processBlockImpl(buffer, midiMessages, &scope);
    }
    else
    {
        // Render audio without recording debug messages
        processBlockImpl(buffer, midiMessages, nullptr);
    }
}

void JuceDebugProcessor::prepareToPlayHook(double sampleRate, int samplesPerBlock)
{
    prepareToPlay(sampleRate, samplesPerBlock);
}

void JuceDebugProcessor::releaseResourcesHook()
{
    releaseResources();
}

void JuceDebugProcessor::processBlockHook(int numSamples, float** inputs, const std::vector<RawMidiMessage>& midiMessages)
{
    juce::AudioBuffer<float> tempBuffer(inputs, getTotalNumInputChannels(), numSamples);

    juce::MidiBuffer juceMidiMessages;
    for (auto& msg : midiMessages)
    {
        juceMidiMessages.addEvent(msg.data, static_cast<int>(msg.dataSize), msg.position);
    }

    ScopedJuceDebugBlock scope(*this,
        tempBuffer,
        juceMidiMessages,
        getTotalNumInputChannels(),
        getTotalNumOutputChannels());

    processBlockImpl(tempBuffer, juceMidiMessages, &scope);
}

void JuceDebugProcessor::getStateInformationHook(std::vector<uint8_t>& out_data)
{
    juce::MemoryBlock mb;
    getStateInformation(mb);

    const auto* bytes = static_cast<const uint8_t*>(mb.getData());
    out_data.assign(bytes, bytes + mb.getSize());
}

void JuceDebugProcessor::setStateInformationHook(const uint8_t* data, size_t size)
{
    setStateInformation(data, static_cast<int>(size));
}

int32_t ScopedJuceDebugBlock::getCurrentBufferTimestamp() const
{
    return m_block.getDebugger().getCurrentBufferTimestamp();
}

} // namespace AudioDebug
