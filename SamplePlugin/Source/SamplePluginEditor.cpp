// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SamplePluginEditor.h"
#include "SamplePluginProcessor.h"

#include "juce/DebuggerSpawner.h"

SamplePluginEditor::SamplePluginEditor(SamplePluginProcessor& p)
    : AudioProcessorEditor(&p)
    , processor(p)
{
    m_debuggerSpawner.reset(new AudioDebug::DebuggerSpawner(&p, "sample_plugin"));
    addAndMakeVisible(m_debuggerSpawner.get());

    setSize(800, 600);
}

SamplePluginEditor::~SamplePluginEditor()
{
}

void SamplePluginEditor::resized()
{
    m_debuggerSpawner->setBounds(0, 0, 80, 20);
}
