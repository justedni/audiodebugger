// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "juce/ControlPanel.h"

#include "core/AudioDebugInterface.h"
#include "juce/CustomSlider.h"
#include "juce/OptionPanel.h"
#include "juce/AudioDebuggerUI.h"

#include <assert.h>

namespace AudioDebug {

ControlPanel::ControlPanel(AudioDebuggerUI* parent)
    : m_parent(parent)
{
    m_optionPanel.reset(new OptionPanel("Options"));
    addAndMakeVisible(m_optionPanel.get());

    m_paramsPanel.reset(new OptionPanel("Parameters"));
    addAndMakeVisible(m_paramsPanel.get());

    m_buttonComputeOutput.reset(new juce::TextButton("Regenerate Output"));
    addAndMakeVisible(m_buttonComputeOutput.get());
    m_buttonComputeOutput->onClick = [this] { m_parent->regenerateOutput(-1); };

    setGenerationButtonsEnabled(false);
}

ControlPanel::~ControlPanel()
{
    setPanelLookAndFeel(nullptr);

    for (auto* slider : m_sliders)
    {
        slider->getSlider().setLookAndFeel(nullptr);
        delete slider;
    }
    m_sliders.clear();
}

void ControlPanel::setPanelLookAndFeel(juce::LookAndFeel* lookAndFeel)
{
    setLookAndFeel(lookAndFeel);
    m_buttonComputeOutput->setLookAndFeel(lookAndFeel);
}

void ControlPanel::setGenerationButtonsEnabled(bool bEnable)
{
    m_buttonComputeOutput->setEnabled(bEnable);
}

void ControlPanel::addOption(uint8_t id, const std::string& displayName, bool defaultEnabled, bool defaultVisible)
{
    bool bExists = (m_toggleOptions.find(id) != m_toggleOptions.end());
    assert(!bExists);
    if (bExists)
        return;

    m_toggleOptions[id] = {};
    auto& button = m_toggleOptions[id].button;
    button->setButtonText(displayName);
    button->setToggleState(defaultEnabled, juce::NotificationType::dontSendNotification);
    button->addListener(this);
    m_optionPanel->addElement(button.get(), defaultVisible);

    m_toggleOptions[id].bVisible = defaultVisible;
}

bool ControlPanel::isOptionEnabled(uint8_t id) const
{
    assert(m_toggleOptions.find(id) != m_toggleOptions.end());
    return m_toggleOptions.at(id).button->getToggleState();
}

void ControlPanel::addSlider(int id, const CustomSlider& srcSlider, juce::LookAndFeel* lookAndFeel)
{
    m_sliders.push_back(new CustomSlider(id, srcSlider.getPos(), srcSlider.getLabel()));
    auto& s = m_sliders.back()->getSlider();
    const auto& ref = srcSlider.getSlider();

    s.setLookAndFeel(lookAndFeel);
    s.setRange(ref.getMinimum(), ref.getMaximum(), ref.getInterval());
    if (srcSlider.hasCustomDecimals())
        s.setNumDecimalPlacesToDisplay(srcSlider.getCustomDecimalsNum());
    else
        s.setNumDecimalPlacesToDisplay(0);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
    s.addListener(this);
    m_paramsPanel->addElement(&s, true);
}

void ControlPanel::addButton(const juce::String& name, bool defaultVisible)
{
    std::unique_ptr<juce::ToggleButton> button(new juce::ToggleButton());
    button->setButtonText(name);
    m_buttons.push_back(std::move(button));

    auto& btn = m_buttons.back();
    btn.get()->addListener(this);
    m_paramsPanel->addElement(btn.get(), true);
}

void ControlPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::lightgrey.withAlpha(0.8f));
}

void ControlPanel::resized()
{
    auto bounds = getLocalBounds();
    int preferredHeight = m_optionPanel->getPreferredHeight();
    m_optionPanel->setBounds(bounds.removeFromTop(preferredHeight));

    int x = 0, y = preferredHeight;
    int width = getWidth();

    {
        int groupY = 0;
        for (auto& toggle : m_toggleOptions)
        {
            if (!toggle.second.bVisible)
                continue;

            toggle.second.button->setBounds(x, groupY, width, 20);
            groupY += 20;
        }
    }

    y += 20;

    preferredHeight = m_paramsPanel->getPreferredHeight();
    m_paramsPanel->setBounds(bounds.removeFromTop(preferredHeight));

    {
        int count = 0;
        int groupY = 0;
        for (auto& button : m_buttons)
        {
            button->setBounds(0, groupY, width, 20);

            for (auto* slider : m_sliders)
            {
                if (slider->getPos() == count)
                {
                    slider->setLabelY(groupY);
                    groupY += 20;
                    slider->getSlider().setBounds(x, groupY, width, 20);
                }
            }

            groupY += 20;
            count++;
        }
    }

    y += preferredHeight;
    m_parent->controlPanelResized(x, y, width);

    m_buttonComputeOutput->setBounds(x + 2, getHeight() - 42, width - 4, 40);
}

void ControlPanel::sliderValueChanged(juce::Slider* slider)
{
    auto val = slider->getValue();

    for (auto* s : m_sliders)
    {
        if (&s->getSlider() == slider)
        {
            m_parent->setSliderFromDebugPanel(s->getId(), val);

            break;
        }
    }

    m_parent->repaint();
}

void ControlPanel::buttonClicked(juce::Button* button)
{
    auto state = button->getToggleState();

    bool bHandled = false;
    int id = 0;
    for (auto& toggle : m_toggleOptions)
    {
        if (button == toggle.second.button.get())
        {
            m_parent->setOptionFromDebugPanel(toggle.first, state);
            bHandled = true;
            break;
        }
        id++;
    }

    if (!bHandled)
    {
        uint8_t i = 0;
        for (auto& btn : m_buttons)
        {
            if (button == btn.get())
            {
                m_parent->setButtonFromDebugPanel(i, state);
                bHandled = true;
                break;
            }
            i++;
        }
    }

    m_parent->repaint();
}

void ControlPanel::updateControls()
{
    for (auto* slider : m_sliders)
    {
        int val = m_parent->getFloatParam(slider->getId());
        slider->updateValue(val);
    }

    uint8_t i = 0;
    for (auto& btn : m_buttons)
    {
        bool bIsEnabled = m_parent->getBoolParam(i);
        m_buttons[i]->setToggleState(bIsEnabled, juce::NotificationType::dontSendNotification);
        i++;
    }
}

void ControlPanel::setSliderValue(uint8_t id, float value)
{
    auto found = std::find_if(m_sliders.begin(), m_sliders.end(), [&](auto* s) { return s->getId() == id; });

    jassert(found != m_sliders.end());
    (*found)->getSlider().setValue(value, juce::NotificationType::dontSendNotification);
}

void ControlPanel::setButtonEnabled(uint8_t id, bool bEnabled)
{
    assert(id >= 0 && id < m_buttons.size());
    m_buttons[id]->setToggleState(bEnabled, juce::NotificationType::dontSendNotification);
}

} // namespace AudioDebug
