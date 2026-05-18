#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace AudioDebug {

class CustomSlider
{
public:
    CustomSlider(uint8_t in_id, uint8_t in_pos, juce::Component* editor,
        juce::Slider::Listener* listener,
        juce::String l,
        double min, double max, double step, int width
    );

    CustomSlider(uint8_t in_id, uint8_t in_pos, juce::String l);

    void setEnabled(bool shouldEnable);
    bool isEnabled() const { return bEnabled; }

    uint8_t getId() const { return id; }
    uint8_t getPos() const { return pos; }
    juce::String getLabel() const { return labelStr; }
    int getLabelXOffset() const { return labelXOffset; }
    void setLabelXOffset(int x) { labelXOffset = x; }
    int getLabelY() const { return labelY; }
    void setLabelY(int y) { labelY = y; }
    juce::Slider& getSlider() { return slider; }
    const juce::Slider& getSlider() const { return slider; }
    void updateValue(float value);

    void setCustomDecimals(int decimalPlacesToDisplay);
    bool hasCustomDecimals() const { return bCustomDecimals; }
    int getCustomDecimalsNum() const { return customDecimals; }

protected:
    const uint8_t id;
    const uint8_t pos;
    juce::Slider slider;
    int labelY = 0;
    int labelXOffset = 0;
    juce::String labelStr;
    bool bEnabled = true;

    bool bCustomDecimals = false;
    int customDecimals = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CustomSlider)
};

} // namespace AudioDebug
