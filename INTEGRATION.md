# Integrating Audio Debugger

This guide shows how to add Audio Debugger to your own processor. For an overview, build instructions and the viewer, see the [README](README.md).

- [Adding the library to your project](#adding-the-library-to-your-project)
- [JUCE plugin](#juce-plugin)
- [Plain C++ (no JUCE)](#plain-c-no-juce)
- [What you can log](#what-you-can-log)
- [Debug this sample](#debug-this-sample)
- [Regression testing](#regression-testing)
- [Real-time safety](#real-time-safety)

## Adding the library to your project

Only the `Library/` folder is needed. It builds two static libraries:

- `AudioDebugCore`: capture, file format and diff. Plain C++17, no dependencies.
- `AudioDebugJuce`: viewer UI and JUCE helpers. Depends on `AudioDebugCore` and JUCE.

Add JUCE first (if you use it), then the repository, and link the target you need:

```cmake
add_subdirectory(JUCE)
add_subdirectory(audiodebugger)

# JUCE plugin: viewer + JUCE helpers (pulls in AudioDebugCore)
target_link_libraries(MyPlugin PRIVATE AudioDebugJuce)

# Plain C++ processor, capture only (no JUCE needed)
target_link_libraries(MyProcessor PRIVATE AudioDebugCore)
```

To build only the core without JUCE, configure with `-DAUDIODEBUG_BUILD_JUCE=OFF`. When Audio Debugger isn't the top-level project, the samples aren't built unless you set `-DAUDIODEBUGGER_BUILD_SAMPLES=ON`.

## JUCE plugin

Full example: [SamplePlugin](SamplePlugin).

### Processor

Inherit from `AudioDebug::JuceDebugProcessor` instead of `juce::AudioProcessor`. It implements `prepareToPlay` and `processBlock` for you (they are `final`), and calls your `...Impl` methods instead:

```cpp
#include "juce/JuceDebugProcessor.h"

class MyPluginProcessor : public AudioDebug::JuceDebugProcessor
{
public:
    MyPluginProcessor()
        : AudioDebug::JuceDebugProcessor(BusesProperties()
            .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {}

    void prepareToPlayImpl(double sampleRate, int samplesPerBlock) override;
    void processBlockImpl(juce::AudioBuffer<float>& buffer,
                          juce::MidiBuffer& midiMessages,
                          AudioDebug::ScopedJuceDebugBlock* scope) override;

    // Optional: identifies your plugin's state blob, stored in the trace
    uint32_t getStateId() const override;

    // ... the rest of the usual juce::AudioProcessor overrides
};
```

A capture starts when the host transport starts playing. While it is recording, the base class records the input buffer and incoming MIDI before calling `processBlockImpl`, and the output buffer after it returns. `scope` is non-null only while a capture is running. You can use it to add markers:

```cpp
void MyPluginProcessor::processBlockImpl(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midiMessages,
                                         AudioDebug::ScopedJuceDebugBlock* scope)
{
    render(buffer, midiMessages);

    if (scope && scope->isActive())
    {
        static constexpr const char* text = "My marker";
        const int32_t pos = scope->getCurrentBufferTimestamp() + sampleOffsetInBlock;

        AudioDebug::Metadata::addMarkerText(scope->raw().metadata(), pos,
                                            0, AudioDebug::FullLine,
                                            AudioDebug::Red, text, strlen(text));
    }
}
```

`processBlockImpl` is also what runs during regeneration, so the same code path produces both the original trace and the regenerated one. To do something when a capture starts (reset a counter, add a first marker...), override `onCaptureStarted(juce::AudioBuffer<float>&)`.

### Editor

`AudioDebug::DebuggerSpawner` is a button that opens the debugger window:

```cpp
// MyPluginEditor.h
#include "juce/DebuggerSpawner.h"

std::unique_ptr<AudioDebug::DebuggerSpawner> m_debuggerSpawner;

// MyPluginEditor.cpp, constructor
m_debuggerSpawner = std::make_unique<AudioDebug::DebuggerSpawner>(&processor, "my_plugin");
addAndMakeVisible(m_debuggerSpawner.get());

// MyPluginEditor.cpp, resized()
m_debuggerSpawner->setBounds(0, 0, 80, 20);
```

The second argument is the trace name prefix (see [Trace files](README.md#trace-files)). If you have subclassed `AudioDebug::AudioDebuggerUI` to add your own options, sliders or buttons to the control panel, pass a pointer to it instead of the processor. The spawner takes ownership.

## Plain C++ (no JUCE)

Full example: [ConsoleSample](ConsoleSample).

Without a JUCE editor, the debugger can't run inside your plugin. You can still write `.rpt` traces from your processor and open them in the [Standalone](Standalone) viewer, live or after the fact.

Inherit from `AudioDebug::DebugProcessor` and implement the two hooks used by regeneration:

```cpp
#include "core/AudioDebug.h"
#include "core/AudioDebugInterface.h"

class MyProcessor : public AudioDebug::DebugProcessor
{
    // Called before a regeneration: prepare your DSP as usual
    void prepareToPlayHook(double sampleRate, int samplesPerBlock) override
    {
        prepareToPlay(sampleRate, samplesPerBlock);
    }

    // Called for each recorded block during a regeneration.
    // `inputs` is processed in place (same layout as your normal process call).
    void processBlockHook(int numSamples, float** inputs,
                          const std::vector<AudioDebug::RawMidiMessage>& midi) override
    {
        processBlock(numSamples, inputs, convertMidi(midi));
    }
};
```

Set up the capture once, then start it from your prepare method:

```cpp
processor.setCapturePath("my_capture", /*bAppendDate*/ true);
processor.setDebugCaptureEnabled(true);

void MyProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    if (!isRegenerating())
        startDebugCapture(sampleRate, samplesPerBlock,
                          numInputs, numOutputs, AudioDebug::EBitDepth::Int32);
    // ... prepare your DSP
}
```

In your process function, open a `AudioDebug::ScopedDebugBlock` for the block, write input and MIDI, process, then write the output. The block is committed when the scope ends:

```cpp
void MyProcessor::processBlock(int numSamples, float* const* buffers, const std::vector<MidiEvent>& midi)
{
    AudioDebug::ScopedDebugBlock debugBlock(*this, numSamples);
    if (debugBlock)
    {
        debugBlock.writeInput(numSamples, numInputs, buffers);
        for (const auto& e : midi)
            debugBlock.pushMidiEvent(e.samplePosition, e.numBytes, e.data);
    }

    process(numSamples, buffers, midi);

    if (debugBlock)
        debugBlock.writeOutput(numSamples, numOutputs, buffers);
}
```

Call `terminateDebugCapture()` when you stop processing.

## What you can log

From a `ScopedDebugBlock` (or `scope->raw()` in a JUCE plugin):

| Data | API |
|---|---|
| Input samples | `writeInput(numSamples, numChannels, buffers)` or `writeInput(numSamples, accessor)` |
| Output samples | `writeOutput(...)`, same two overloads |
| MIDI events (up to 3 bytes) | `pushMidiEvent(position, dataSize, data)` |
| Text markers | `Metadata::addMarkerText(block.metadata(), ...)` |
| Binary markers / custom data | `Metadata::addMarkerCustom(...)`, or `Metadata::add_*` helpers on `block.metadata()` / `block.custom()` |

**Extra debug channels:** you can write more output channels than your plugin really has. For example, the ConsoleSample declares 2 extra outputs and records its LFO and filter cutoff next to the audio, so internal signals show up as tracks in the viewer and take part in the diff.

## Debug this sample

In the viewer, right-click a sample and choose *Debug this sample*. The trace is regenerated, and when it reaches that sample, `debugBreakSampleId()` fires in your code. To use it, call it from your process loop with the index of the sample within the current block:

```cpp
for (int i = 0; i < numSamples; ++i)
{
    debugBreakSampleId(i);        // put a breakpoint inside this function...
    // debugBreakSampleId(i, true); // ...or force a break automatically
    // process sample i
}
```

This only does something during a regeneration, so it costs nothing in normal playback. It currently uses MSVC-specific pragmas and `__debugbreak`.

## Regression testing

`core/UnitTests.h` provides `UnitTests::UnitTestProcessor`. Subclass it, create your processor in `createProcessor()`, and call `processUnitTestFromRpt()` with a reference trace. It regenerates the trace with your current code and fills `getDifferences()` with every range where the output changed. An empty list means the output is still bit-identical.

This part of the API is still experimental.

## Real-time safety

Inside your process callback, recording is designed to be real-time safe:

- `ScopedDebugBlock`, `writeInput` / `writeOutput`, `pushMidiEvent` and the `Metadata::add*` helpers write into pre-allocated memory and hand finished blocks to the writer thread through a lock-free queue. Disk I/O happens on a background thread (flushed every 250 ms).
- Prefer the `const float* const*` overloads of `writeInput` / `writeOutput`. The accessor overloads take a `std::function`, which can allocate if your lambda captures a lot.
- Each audio block has a 256 KB budget for its data. If you write more than that, the extra data is dropped (`BlockWriter::overflowed()`). The audio thread never waits.
- In Debug builds, the JUCE layer flags heap allocations made on the audio thread.

Starting and stopping a capture (`startDebugCapture`, `terminateDebugCapture`) opens files and allocates memory, so call them from `prepareToPlay` or another non-real-time thread.
