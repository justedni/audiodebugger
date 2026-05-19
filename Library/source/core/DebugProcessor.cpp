// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/DebugProcessor.h"
#include "core/AudioDebug.h"

namespace AudioDebug {

DebugProcessor::DebugProcessor()
{
	AudioDebug::AudioDebugger::create();
}

DebugProcessor::~DebugProcessor()
{
	AudioDebug::AudioDebugger::release();
}

} // namespace AudioDebug
