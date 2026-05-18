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
