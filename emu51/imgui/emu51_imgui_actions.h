// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "emu51_imgui_menu.h"
#include "imgui.h"

// One declarative registry of every keyboard-dispatched action in the ImGui
// frontend. Menu drawing pulls per-platform shortcut labels from here, and the
// single shortcut handler walks the same table; that way adding or moving a
// binding is a one-place edit.

enum class Emu51Action {
	FileSave,
	FileSaveAs,
	BuildCompile,
	BuildLoad,
	BuildCompileAndLoad,
	ExecToggleRun,
	ExecReset,
	ExecResetStop,
	ExecStepIn,
	ExecStepOver,
	ExecStepOut,
};

enum class Emu51ShortcutModifier : unsigned char {
	None,           // bare key (e.g. F9)
	Primary,        // Cmd on macOS, Ctrl on Linux / Windows
	PrimaryShift,   // primary + Shift
	Shift,          // Shift only
};

struct Emu51ShortcutBinding {
	ImGuiKey key = ImGuiKey_None;
	Emu51ShortcutModifier modifier = Emu51ShortcutModifier::None;
};

// Returns a static, platform-correct label for a binding ("Ctrl+S", "F9", ...).
// Empty string for bindings with no key.
const char* emu51ShortcutLabel(Emu51Action action);

// Walks the action table and invokes any binding whose shortcut combo is
// pressed this frame. Used by the GLFW frontend. macOS uses Cocoa menus for
// shortcut dispatch and does not call this.
void emu51HandleImguiShortcuts(Emu51ImguiMenuTarget& target);
