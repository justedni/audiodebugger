// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/DebugProcessor.h"

#include "core/AudioDebug.h"

namespace AudioDebug {

DebugProcessor::DebugProcessor()
	: m_debugger(std::make_unique<AudioDebugger>())
{
}

DebugProcessor::~DebugProcessor() = default;

} // namespace AudioDebug
