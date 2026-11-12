// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "imgui.h"
#include "emu51.h"

namespace SegmentDisplay {

ImVec2 Size(float scale = 1.0f);
float Advance(float scale = 1.0f);
void Digit(std::uint8_t value, float scale = 1.0f);

}  // namespace SegmentDisplay
