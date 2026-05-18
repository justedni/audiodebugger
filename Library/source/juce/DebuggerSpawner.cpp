#include "juce/DebuggerSpawner.h"

#include "core/AudioDebug.h"
#include "juce/AudioDebuggerUI.h"

namespace AudioDebug {

DebuggerSpawner::DebuggerSpawner(AudioDebug::DebugProcessor* processor, const juce::String& tracePrefix)
	: m_buttonReport("Debugger")
{
	addAndMakeVisible(m_buttonReport);

    auto path = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(tracePrefix + ".rpt");
    AudioDebug::AudioDebugger::get()->setCaptureName(path.getFullPathName().toStdString(), false);

	m_reportViewer.reset(new AudioDebug::AudioDebuggerUI(processor));
	m_buttonReport.onClick = [this] { openReport(); };
}

DebuggerSpawner::~DebuggerSpawner()
{
	if (m_reportWindow)
		m_reportWindow.deleteAndZero();
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
