// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "juce/DebuggerSpawner.h"

#include "core/AudioDebugger.h"
#include "core/AudioDebugInterface.h"
#include "juce/AudioDebuggerUI.h"

namespace AudioDebug {

DebuggerSpawner::DebuggerSpawner(DebugProcessor* processor, const juce::String& tracePrefix)
    : m_buttonReport("Debugger")
{
    m_reportViewer.reset(new AudioDebuggerUI(processor));

    init(tracePrefix);
}

DebuggerSpawner::DebuggerSpawner(AudioDebuggerUI* debugger, const juce::String& tracePrefix)
    : m_buttonReport("Debugger")
{
    m_reportViewer.reset(debugger);

    init(tracePrefix);
}

DebuggerSpawner::~DebuggerSpawner()
{
	if (m_reportWindow)
		m_reportWindow.deleteAndZero();
}

void DebuggerSpawner::init(const juce::String& tracePrefix)
{
    addAndMakeVisible(m_buttonReport);

    auto path = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(tracePrefix + ".rpt");
    if (auto* debugger = m_reportViewer->getDebugger())
        debugger->setCaptureName(path.getFullPathName().toStdString(), false);

    m_buttonReport.onClick = [this] { openReport(); };
}

void DebuggerSpawner::resized()
{
    m_buttonReport.setBounds(getLocalBounds());
}

void DebuggerSpawner::openReport()
{
    if (!m_reportWindow)
    {
        auto onClose = [this]() {
            m_reportWindow.deleteAndZero();
        };

        m_reportWindow = new AudioDebug::WaveformPopup(
            "Report Viewer",
            m_reportViewer.get(),
            std::move(onClose)
        );
    }

    m_reportWindow->toFront(true);
}


} // namespace AudioDebug
