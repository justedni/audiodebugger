// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace AudioDebug {

class OptionPanel : public juce::Component
{
public:
    OptionPanel(const juce::String& title);
    ~OptionPanel();

    void resized() override;

    void addElement(juce::Component* comp);

    void setExpanded(bool shouldBeExpanded);
    int getPreferredHeight() const;

private:
    std::unique_ptr<juce::LookAndFeel> m_lookAndFeel;
    juce::ToggleButton m_toggleButton;
    juce::Component m_content;
    bool m_expanded = true;
    int m_headerHeight = 24;
    int m_contentHeight = 0;
};

} // namespace AudioDebug
