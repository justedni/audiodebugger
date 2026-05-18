#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace AudioDebug {

class WaveformPopup;
class AudioDebuggerUI;
class DebugProcessor;

class DebuggerSpawner : public juce::Component
{
public:
	DebuggerSpawner(AudioDebug::DebugProcessor* processor, const juce::String& tracePrefix);
	~DebuggerSpawner();

	void resized() override;

private:
	void openReport();

	juce::TextButton m_buttonReport;
	std::unique_ptr<AudioDebug::AudioDebuggerUI> m_reportViewer;
	juce::Component::SafePointer<AudioDebug::WaveformPopup> m_reportWindow;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DebuggerSpawner)
};

} // namespace AudioDebug
