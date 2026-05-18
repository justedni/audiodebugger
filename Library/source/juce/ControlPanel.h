#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace AudioDebug {

class DebugProcessor;
class CustomSlider;
class OptionPanel;
class AudioDebuggerUI;

class ControlPanel : public juce::Component
    , public juce::Slider::Listener
    , public juce::Button::Listener
{
public:
    ControlPanel(AudioDebuggerUI* parent, DebugProcessor* processor);
    ~ControlPanel();

    void addOption(uint8_t id, const std::string& displayName, bool defaultEnabled);
    bool isOptionEnabled(uint8_t id) const;

    void addSlider(int id, const CustomSlider& srcSlider, juce::LookAndFeel* lookAndFeel);
    void addButton(const juce::String& name);
    void setPanelLookAndFeel(juce::LookAndFeel* lookAndFeel);

    void updateControls();

    void paint(juce::Graphics& g) override;
    void resized() final;
    void sliderValueChanged(juce::Slider* slider) override;
    void buttonClicked(juce::Button* button) override;

    void setSliderValue(uint8_t id, float value);
    void setButtonEnabled(uint8_t id, bool bEnabled);

    void setGenerationButtonsEnabled(bool bEnabled);
protected:
    std::map<uint8_t, std::unique_ptr<juce::ToggleButton>> m_toggleOptions;

    std::unique_ptr<juce::TextButton> m_buttonComputeOutput;
    std::vector<CustomSlider*> m_sliders;
    std::vector<std::unique_ptr<juce::ToggleButton>> m_buttons;

    std::unique_ptr<OptionPanel> m_optionPanel;
    std::unique_ptr<OptionPanel> m_paramsPanel;

    AudioDebuggerUI* m_parent = nullptr;
    DebugProcessor* m_processor = nullptr;
};

} // namespace AudioDebug
