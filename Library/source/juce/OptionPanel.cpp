// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OptionPanel.h"

namespace AudioDebug {

class OptionPanelButtonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        const bool expanded = button.getToggleState();

        auto bgColour = button.findColour(juce::TextButton::buttonColourId);
        if (shouldDrawButtonAsDown) 
            bgColour = bgColour.darker(0.2f);
        else if (shouldDrawButtonAsHighlighted)
            bgColour = bgColour.brighter(0.1f);

        g.setColour(bgColour);
        g.fillRoundedRectangle(bounds.reduced(1.0f), 3.0f);

        const float triangleSize = bounds.getHeight() * 0.35f;
        const float triangleX = bounds.getX() + 8.0f;
        const float triangleY = bounds.getCentreY();

        juce::Path triangle;
        if (expanded)
        {
            triangle.addTriangle(triangleX, triangleY - triangleSize * 0.4f,
                triangleX + triangleSize, triangleY - triangleSize * 0.4f,
                triangleX + triangleSize * 0.5f, triangleY + triangleSize * 0.5f);
        }
        else
        {
            triangle.addTriangle(triangleX, triangleY - triangleSize * 0.5f,
                triangleX, triangleY + triangleSize * 0.5f,
                triangleX + triangleSize * 0.9f, triangleY);
        }

        g.setColour(button.findColour(juce::ToggleButton::textColourId));
        g.fillPath(triangle);

        const float textOffsetX = triangleX + triangleSize + 8.0f;
        auto textBounds = bounds.withLeft(textOffsetX);

        g.drawText(button.getButtonText(), textBounds.toNearestInt(), juce::Justification::centredLeft, true);
    }
};


OptionPanel::OptionPanel(const juce::String& title)
    : m_lookAndFeel(new OptionPanelButtonLookAndFeel())
{
    m_toggleButton.setLookAndFeel(m_lookAndFeel.get());
    m_toggleButton.setButtonText(title);
    m_toggleButton.setClickingTogglesState(true);
    m_toggleButton.setToggleState(true, juce::dontSendNotification);
    m_toggleButton.onClick = [this] { setExpanded(m_toggleButton.getToggleState()); };
    addAndMakeVisible(m_toggleButton);

    addAndMakeVisible(m_content);
}

OptionPanel::~OptionPanel()
{
    m_toggleButton.setLookAndFeel(nullptr);
}

void OptionPanel::setExpanded(bool shouldBeExpanded)
{
    m_expanded = shouldBeExpanded;
    m_content.setVisible(m_expanded);

    if (auto* parent = getParentComponent())
        parent->resized();
}

int OptionPanel::getPreferredHeight() const
{
    return m_headerHeight + (m_expanded ? m_contentHeight : 0);
}

void OptionPanel::resized()
{
    auto bounds = getLocalBounds();
    m_toggleButton.setBounds(bounds.removeFromTop(m_headerHeight));
    m_content.setBounds(bounds);
}

void OptionPanel::addElement(juce::Component* comp)
{
    m_content.addAndMakeVisible(comp);
    m_contentHeight += 20;
}

} // namespace AudioDebug
