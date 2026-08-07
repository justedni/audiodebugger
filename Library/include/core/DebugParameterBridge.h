// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace AudioDebug {

class DebugParameterBridge
{
public:
	virtual ~DebugParameterBridge() {}

	virtual void setFloatParam(uint8_t id, float value) = 0;
	virtual float getFloatParam(uint8_t id) const = 0;
	virtual void setBoolParam(uint8_t id, bool bEnabled) = 0;
	virtual bool getBoolParam(uint8_t id) const = 0;
};

} // namespace AudioDebug
