// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_core/juce_core.h>

#include "core/AudioDebug.h"
#include "core/AudioDebugInterface.h"
#include "juce/AudioDebuggerUI.h"

namespace ProjectInfo
{
    const char* const  projectName = "AudioDebugger";
    const char* const  companyName = "";
    const char* const  versionString = "1.0.0";
    const int          versionNumber = 0x10000;
}


class DebugProcessorNone : public AudioDebug::DebugProcessor
{
public:
    DebugProcessorNone()
        : DebugProcessor()
    {}

    virtual ~DebugProcessorNone() {}
};


class AudioDebuggerApplication  : public juce::JUCEApplication
{
public:
    AudioDebuggerApplication() {}

    const juce::String getApplicationName() override       { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String& commandLine) override
    {
        m_dummyProcessor.reset(new DebugProcessorNone());

        m_reportViewer.reset(new AudioDebug::AudioDebuggerUI(m_dummyProcessor.get()));
        m_reportViewer->setSize(600, 400);

        auto onClose = [this]() {
            JUCEApplication::getInstance()->systemRequestedQuit();
        };

        m_mainWindow.reset(new AudioDebug::WaveformPopup(
            getApplicationName(),
            m_reportViewer.get(),
            std::move(onClose)
        ));
    }

    void shutdown() override
    {
        m_mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
    }

private:
    std::unique_ptr<DebugProcessorNone> m_dummyProcessor;
    std::unique_ptr<AudioDebug::AudioDebuggerUI> m_reportViewer;
    std::unique_ptr<AudioDebug::WaveformPopup> m_mainWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioDebuggerApplication)
};

START_JUCE_APPLICATION (AudioDebuggerApplication)
