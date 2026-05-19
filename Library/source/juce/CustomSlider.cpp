// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "juce/CustomSlider.h"

namespace AudioDebug {

CustomSlider::CustomSlider(uint8_t in_id, uint8_t in_pos, juce::Component* editor,
    juce::Slider::Listener* listener, juce::String l,
    double min, double max, double step, int width)
    : id(in_id)
    , pos(in_pos)
    , labelStr(l)
{
    slider.setRange(min, max, step);
    slider.setNumDecimalPlacesToDisplay(0);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, width, 20);
    slider.addListener(listener);
    editor->addAndMakeVisible(slider);
}

CustomSlider::CustomSlider(uint8_t in_id, uint8_t in_pos, juce::String l)
    : id(in_id)
    , pos(in_pos)
    , labelStr(l)
{
}

void CustomSlider::setEnabled(bool shouldEnable)
{
    slider.setEnabled(shouldEnable);
    bEnabled = shouldEnable;
}

void CustomSlider::updateValue(float value)
{
    slider.setValue(value, juce::NotificationType::dontSendNotification);
}

void CustomSlider::setCustomDecimals(int decimalPlacesToDisplay)
{
    bCustomDecimals = true;
    customDecimals = decimalPlacesToDisplay;
    slider.setNumDecimalPlacesToDisplay(decimalPlacesToDisplay);
}

} // namespace AudioDebug
