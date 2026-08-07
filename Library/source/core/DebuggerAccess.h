// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/DebugProcessor.h"
#include "core/AudioDebugger.h"

namespace AudioDebug {

class DebugProcessor::DebuggerAccess
{
public:
    static AudioDebugger& get(DebugProcessor& p)       { return p.getDebugger(); }
    static const AudioDebugger& get(const DebugProcessor& p) { return p.getDebugger(); }
};

} // namespace AudioDebug
