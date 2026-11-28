// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "imgui.h"

enum class Emu51ImguiIcon {
	Run,
	Pause,
	StepOver,
	StepIn,
	StepOut,
	DockLeft,
	DockTop,
	DockRight,
	DockBottom,
	DockAll,
};

bool emu51ImguiIconButton(const char* id, Emu51ImguiIcon icon, const ImVec2& size);
