# Audio Debugger

WIP tool to debug audio plugins, with trace recording, output regeneration, close sample inspection, customizable interface and functionalities

## Compile and link
Please take a look at the CMakeLists.txt examples in the code. The library expects to find a "JUCE" folder at the root (next to the audiodebugger folder) that contains the Juce framework. Then in the CMakeLists, simply link with `AudioDebuggerLib` using `target_link_libraries`.

## Integration - Juce plugin (fast)
Integration example can be found in the [SamplePlugin](https://github.com/justedni/audiodebugger/tree/main/SamplePlugin) subfolder

### Editor
Simply use `AudioDebug::DebuggerSpawner`: a wrapper that encapsulates a button that spawns the AudioDebugger
- add a `std::unique_ptr<AudioDebug::DebuggerSpawner> spawner;` to your Editor .h
- call `spawner.reset(new AudioDebug::DebuggerSpawner(&p, "sample_plugin")); addAndMakeVisible(spawner.get());` in the Editor's constructor in the .cpp
- make sure to call `spawner->setBounds(0, 0, 80, 20);` in your Editor's `resized()`

### Processor
Make your main processor inherit from `AudioDebug::JuceDebugProcessor` (instead of `juce::AudioProcessor`)
Then you need to implement `processBlockRegenerate`, most of the time this will simply call your internal processBlock():
```
void VirtualJVProcessor::processBlockRegenerate(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    processBlock(buffer, midiMessages);
}
```

## Integration - Pure C++ plugin

### GUI
Since you don't have a Juce Editor, you cannot integrate the AudioDebugger directly inside the plugin. However, you can still compile the Standalone app that allows you to open .rpt files written by your audio processor (either offline or in real time).

### Audio Processor
Your main processor class needs to inherit from `AudioDebug::DebugProcessor`
You need to implement two functions:
```
void prepareToPlayHook(double sampleRate, int samplesPerBlock, int numChannelsIn) override
{
    // For a Juce plugin, this would simply call prepareToPlay
    prepareToPlay(sampleRate, samplesPerBlock);
}
```

```
void processBlockHook(int numSamples, float** inputs, int32_t numChannelsIn, const std::vector<AudioDebug::RawMidiMessage>& inMessages, float** outputs) override
{
    // Call your internal process function here (input and output buffers are separate, unlike in Juce processors)
    processInternal(numSamples, inMessages, inputs, outputs);
}
```

In the regular prepare method, make sure to initialize:
```
    AudioDebug::AudioDebugger::get()->initDebugProcessor(
        std::round(sampleRate),
        samplesPerBlock,
        getTotalNumInputChannels(),
        getTotalNumOutputChannels(),
        AudioDebug::EBitDepth::Int32);
```
## Log data
In your process function, you can push the following types of data to the .rpt trace file:
(see the SamplePlugin for detailed usage)
- Input samples: `pDebugger->pushAllInputSamples()` or `pDebugger->addInDebugSamples()`
- Output samples: `pDebugger->pushAllOutputSamples()` or `pDebugger->addOutDebugSamples()`
- Midi Events: `pDebugger->pushMidiEvent()` and then `pDebugger->writeAllMidiEvents()`
- Markers and custom data: use `pDebugger->addMetadata` and the `AudioDebug::Metadata` helpers
