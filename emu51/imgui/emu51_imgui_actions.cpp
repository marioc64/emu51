// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_actions.h"

#include <cstdio>

namespace {

struct ActionEntry {
	Emu51Action id;
	Emu51ShortcutBinding shortcut;
	const char* keyLabel; // displayed key name; "F9", "S", "B", ...
	bool (*enabled)(const Emu51ImguiMenuTarget&);
	void (*invoke)(Emu51ImguiMenuTarget&, const ImGuiIO&);
};

bool alwaysEnabled(const Emu51ImguiMenuTarget&) { return true; }
bool canSave(const Emu51ImguiMenuTarget& t)     { return t.canSaveAssemblyEditorFile(); }
bool hasSource(const Emu51ImguiMenuTarget& t)   { return t.hasAssemblyEditorSource(); }
bool hasBuildOutput(const Emu51ImguiMenuTarget& t) { return t.hasCompiledAssemblyEditorOutput(); }

constexpr ActionEntry kActions[] = {
	{Emu51Action::FileSave,            {ImGuiKey_S,  Emu51ShortcutModifier::Primary},      "S",  canSave,        [](Emu51ImguiMenuTarget& t, const ImGuiIO&) { t.saveAssemblyEditorFile(); }},
	{Emu51Action::FileSaveAs,          {ImGuiKey_S,  Emu51ShortcutModifier::PrimaryShift}, "S",  canSave,        [](Emu51ImguiMenuTarget& t, const ImGuiIO&) { t.saveAssemblyEditorFileAs(); }},
	{Emu51Action::BuildCompile,        {ImGuiKey_B,  Emu51ShortcutModifier::Primary},      "B",  hasSource,      [](Emu51ImguiMenuTarget& t, const ImGuiIO&) { t.compileAssemblyEditor(); }},
	{Emu51Action::BuildLoad,           {ImGuiKey_L,  Emu51ShortcutModifier::PrimaryShift}, "L",  hasBuildOutput, [](Emu51ImguiMenuTarget& t, const ImGuiIO&) { t.loadCompiledAssemblyEditorOutput(); }},
	{Emu51Action::BuildCompileAndLoad, {ImGuiKey_B,  Emu51ShortcutModifier::PrimaryShift}, "B",  hasSource,      [](Emu51ImguiMenuTarget& t, const ImGuiIO&) { t.compileAndLoadAssemblyEditor(); }},
	{Emu51Action::ExecToggleRun,       {ImGuiKey_F9, Emu51ShortcutModifier::None},         "F9", alwaysEnabled,  [](Emu51ImguiMenuTarget& t, const ImGuiIO&) { t.toggleRun(); }},
	{Emu51Action::ExecReset,           {ImGuiKey_R,  Emu51ShortcutModifier::Primary},      "R",  alwaysEnabled,  [](Emu51ImguiMenuTarget& t, const ImGuiIO&) { t.resetCpuState(false); }},
	{Emu51Action::ExecResetStop,       {ImGuiKey_R,  Emu51ShortcutModifier::PrimaryShift}, "R",  alwaysEnabled,  [](Emu51ImguiMenuTarget& t, const ImGuiIO&) { t.resetCpuState(true); }},
	{Emu51Action::ExecStepIn,          {ImGuiKey_F7, Emu51ShortcutModifier::None},         "F7", alwaysEnabled,  [](Emu51ImguiMenuTarget& t, const ImGuiIO&) { t.stepIn(); }},
	{Emu51Action::ExecStepOver,        {ImGuiKey_F8, Emu51ShortcutModifier::None},         "F8", alwaysEnabled,  [](Emu51ImguiMenuTarget& t, const ImGuiIO&) { t.stepOver(); }},
	{Emu51Action::ExecStepOut,         {ImGuiKey_F8, Emu51ShortcutModifier::Shift},        "F8", alwaysEnabled,  [](Emu51ImguiMenuTarget& t, const ImGuiIO&) { t.stepOut(); }},
};

const ActionEntry* findAction(Emu51Action id)
{
	for (const ActionEntry& entry : kActions) {
		if (entry.id == id)
			return &entry;
	}
	return nullptr;
}

bool modifierMatches(const ImGuiIO& io, Emu51ShortcutModifier modifier)
{
#if defined(__APPLE__)
	const bool primary = io.KeySuper && !io.KeyCtrl;
#else
	const bool primary = io.KeyCtrl && !io.KeySuper;
#endif
	switch (modifier) {
	case Emu51ShortcutModifier::None:
		return !io.KeyCtrl && !io.KeyAlt && !io.KeySuper && !io.KeyShift;
	case Emu51ShortcutModifier::Primary:
		return primary && !io.KeyAlt && !io.KeyShift;
	case Emu51ShortcutModifier::PrimaryShift:
		return primary && !io.KeyAlt && io.KeyShift;
	case Emu51ShortcutModifier::Shift:
		return !io.KeyCtrl && !io.KeyAlt && !io.KeySuper && io.KeyShift;
	}
	return false;
}

} // namespace

const char* emu51ShortcutLabel(Emu51Action action)
{
	const ActionEntry* entry = findAction(action);
	if (entry == nullptr || entry->shortcut.key == ImGuiKey_None)
		return "";

	thread_local char buffer[40];
#if defined(__APPLE__)
	const char* primary = "Cmd";
#else
	const char* primary = "Ctrl";
#endif
	switch (entry->shortcut.modifier) {
	case Emu51ShortcutModifier::None:
		std::snprintf(buffer, sizeof(buffer), "%s", entry->keyLabel);
		break;
	case Emu51ShortcutModifier::Primary:
		std::snprintf(buffer, sizeof(buffer), "%s+%s", primary, entry->keyLabel);
		break;
	case Emu51ShortcutModifier::PrimaryShift:
		std::snprintf(buffer, sizeof(buffer), "%s+Shift+%s", primary, entry->keyLabel);
		break;
	case Emu51ShortcutModifier::Shift:
		std::snprintf(buffer, sizeof(buffer), "Shift+%s", entry->keyLabel);
		break;
	}
	return buffer;
}

void emu51HandleImguiShortcuts(Emu51ImguiMenuTarget& target)
{
	const ImGuiIO& io = ImGui::GetIO();
	for (const ActionEntry& entry : kActions) {
		if (entry.shortcut.key == ImGuiKey_None)
			continue;
		if (!modifierMatches(io, entry.shortcut.modifier))
			continue;
		if (!ImGui::IsKeyPressed(entry.shortcut.key, false))
			continue;
		if (entry.enabled != nullptr && !entry.enabled(target))
			continue;
		entry.invoke(target, io);
	}
}
