// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_menu.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>

#include "emu51_imgui_actions.h"
#include "emu51_imgui_icons.h"
#include "imgui.h"

namespace {

// Edit-menu shortcuts (Cut/Copy/Paste/Undo/Redo/SelectAll) are handled
// internally by ImGuiColorTextEdit, so we don't put them in the action
// registry; we only render their hint text here.
const char* editShortcutLabel(const char* key)
{
#if defined(__APPLE__)
	static thread_local char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "Cmd+%s", key);
	return buffer;
#else
	static thread_local char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "Ctrl+%s", key);
	return buffer;
#endif
}

const char* editRedoShortcutLabel()
{
#if defined(__APPLE__)
	static thread_local char buffer[40];
	std::snprintf(buffer, sizeof(buffer), "Cmd+Shift+Z");
	return buffer;
#else
	return editShortcutLabel("Y");
#endif
}

std::string recentProgramLabel(const std::string& path, int index)
{
	const std::filesystem::path filePath(path);
	const std::string filename = filePath.filename().empty() ? path : filePath.filename().string();
	return std::to_string(index + 1) + " " + filename + "###recent_program_" + std::to_string(index);
}

void drawRunButton(Emu51ImguiMenuTarget& target)
{
	const bool wasRunning = target.running();
	const ImVec2 buttonSize(ImGui::GetFrameHeight() * 1.35f, ImGui::GetFrameHeight());
	if (wasRunning) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
	}
	const Emu51ImguiIcon icon = wasRunning ? Emu51ImguiIcon::Pause : Emu51ImguiIcon::Run;
	if (emu51ImguiIconButton("##run", icon, buttonSize))
		target.toggleRun();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		ImGui::SetTooltip("Run/Pause (F9)");
	if (wasRunning)
		ImGui::PopStyleColor(3);
}

struct DockControl {
	Emu51ImguiSnapTarget edge;
	Emu51ImguiIcon icon;
	const char* id;
	const char* name;
};

constexpr std::array kDockControls{
	DockControl{Emu51ImguiSnapTarget::Left, Emu51ImguiIcon::DockLeft, "##dock_left", "left dock"},
	DockControl{Emu51ImguiSnapTarget::Top, Emu51ImguiIcon::DockTop, "##dock_top", "top dock"},
	DockControl{Emu51ImguiSnapTarget::Right, Emu51ImguiIcon::DockRight, "##dock_right", "right dock"},
	DockControl{Emu51ImguiSnapTarget::Bottom, Emu51ImguiIcon::DockBottom, "##dock_bottom", "bottom dock"},
};

bool dockToggle(const char* id, Emu51ImguiIcon icon, const char* name, bool collapsed, const ImVec2& size)
{
	if (collapsed)
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	const bool clicked = emu51ImguiIconButton(id, icon, size);
	if (collapsed)
		ImGui::PopStyleColor();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		ImGui::SetTooltip("%s %s", collapsed ? "Expand" : "Collapse", name);
	return clicked;
}

}  // namespace

void emu51DrawImguiMainMenuBar(Emu51ImguiMenuTarget& target)
{
	if (!ImGui::BeginMainMenuBar())
		return;

	if (ImGui::BeginMenu("File")) {
		if (ImGui::MenuItem("Open project...")) target.requestOpenProject();
		const int recentProjectCount = target.recentProjectFileCount();
		if (ImGui::BeginMenu("Open Recent Projects", recentProjectCount > 0)) {
			for (int i = 0; i < recentProjectCount; ++i) {
				const std::string path = target.recentProjectFilePath(i);
				const auto filename = std::filesystem::path(path).filename().string();
				const auto label = std::to_string(i + 1) + " " + filename + "###recent_project_" + std::to_string(i);
				if (ImGui::MenuItem(label.c_str())) target.openRecentProjectFile(i);
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", path.c_str());
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Clear Menu")) target.clearRecentProjectFiles();
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Save project", emu51ShortcutLabel(Emu51Action::ProjectSave))) target.saveProject();
		if (ImGui::MenuItem("Save project as...")) target.saveProjectAs();
		if (ImGui::MenuItem("Add file...")) target.requestAddProjectFiles();
		if (ImGui::MenuItem("New ASM File")) target.newProjectAssemblyFile();
		ImGui::Separator();
		if (ImGui::MenuItem("Open program..."))
			target.requestOpenProgram();
		if (ImGui::MenuItem("Load program..."))
			target.requestLoadProgram();
		if (ImGui::MenuItem("Reload program", nullptr, false, target.hasLoadedProgram()))
			target.reloadProgram();
		const int recentCount = target.recentProgramFileCount();
		if (ImGui::BeginMenu("Open Recent", recentCount > 0)) {
			for (int i = 0; i < recentCount; ++i) {
				const std::string& path = target.recentProgramFilePath(i);
				const std::string label = recentProgramLabel(path, i);
				if (ImGui::MenuItem(label.c_str()))
					target.openRecentProgramFile(i);
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
					ImGui::SetTooltip("%s", path.c_str());
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Clear Menu"))
				target.clearRecentProgramFiles();
			ImGui::EndMenu();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Save source", emu51ShortcutLabel(Emu51Action::SourceSave), false, target.canSaveAssemblyEditorFile()))
			target.saveAssemblyEditorFile();
		if (ImGui::MenuItem("Save source as...", nullptr, false, target.canSaveAssemblyEditorFile()))
			target.saveAssemblyEditorFileAs();
		ImGui::Separator();
		if (ImGui::MenuItem("Quit"))
			target.requestQuit();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit")) {
		if (ImGui::MenuItem("Undo", editShortcutLabel("Z"), false, target.canUndoAssemblyEditorText()))
			target.undoAssemblyEditorText();
		if (ImGui::MenuItem("Redo", editRedoShortcutLabel(), false, target.canRedoAssemblyEditorText()))
			target.redoAssemblyEditorText();
		ImGui::Separator();
		const bool canUseTextCommands = target.canUseAssemblyEditorTextCommands();
		if (ImGui::MenuItem("Cut", editShortcutLabel("X"), false, canUseTextCommands))
			target.cutAssemblyEditorText();
		if (ImGui::MenuItem("Copy", editShortcutLabel("C"), false, canUseTextCommands))
			target.copyAssemblyEditorText();
		if (ImGui::MenuItem("Paste", editShortcutLabel("V"), false, canUseTextCommands))
			target.pasteAssemblyEditorText();
		if (ImGui::MenuItem("Select All", editShortcutLabel("A"), false, canUseTextCommands))
			target.selectAllAssemblyEditorText();
		ImGui::Separator();
		if (ImGui::MenuItem("Format Code", nullptr, false, target.canFormatAssemblyEditorText()))
			target.formatAssemblyEditorText();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Memory")) {
		const bool unifiedMode = target.unifiedMode();
		if (ImGui::MenuItem("Unified mode", nullptr, unifiedMode))
			target.setUnifiedMode(!target.unifiedMode());
		ImGui::Separator();
		if (unifiedMode) {
			if (ImGui::MenuItem("Clear memory"))
				target.clearAllMemory();
		}
		else {
			if (ImGui::MenuItem("Clear data memory"))
				target.clearRamOnly();
			if (ImGui::MenuItem("Clear program memory"))
				target.clearRomOnly();
		}
		ImGui::Separator();
		if (unifiedMode) {
			if (ImGui::MenuItem("Fill memory range"))
				target.requestFillRamRange();
		}
		else {
			if (ImGui::MenuItem("Fill data memory range"))
				target.requestFillRamRange();
			if (ImGui::MenuItem("Fill program memory range"))
				target.requestFillRomRange();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Build")) {
		if (ImGui::MenuItem("Compile", emu51ShortcutLabel(Emu51Action::BuildCompile), false, target.hasAssemblyEditorSource()))
			target.compileAssemblyEditor();
		if (ImGui::MenuItem("Load", emu51ShortcutLabel(Emu51Action::BuildLoad), false, target.hasCompiledAssemblyEditorOutput()))
			target.loadCompiledAssemblyEditorOutput();
		if (ImGui::MenuItem("Compile and Load", emu51ShortcutLabel(Emu51Action::BuildCompileAndLoad), false, target.hasAssemblyEditorSource()))
			target.compileAndLoadAssemblyEditor();
		ImGui::Separator();
		const bool hasProjectFiles = target.hasProjectFiles();
		if (ImGui::MenuItem("Build Project", nullptr, false, hasProjectFiles))
			target.performProjectBuild(Emu51ProjectBuildAction::Build);
		if (ImGui::MenuItem("Build and Load", nullptr, false, hasProjectFiles))
			target.performProjectBuild(Emu51ProjectBuildAction::BuildAndLoad);
		if (ImGui::MenuItem("Power Cycle"))
			target.performProjectBuild(Emu51ProjectBuildAction::PowerCycle);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Execution")) {
		if (ImGui::MenuItem("Serial terminal")) target.showSerialTerminal();
		if (ImGui::MenuItem("Run", emu51ShortcutLabel(Emu51Action::ExecToggleRun), target.running()))
			target.toggleRun();
		if (ImGui::MenuItem("Reset", emu51ShortcutLabel(Emu51Action::ExecReset))) {
			target.resetCpuState(ImGui::GetIO().KeyShift);
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("Reset CPU state. Hold Shift while selecting to stop execution.");
		if (ImGui::MenuItem("Set PC...", nullptr, false, !target.running()))
			target.requestSetProgramCounter();
		if (ImGui::MenuItem("Step over", emu51ShortcutLabel(Emu51Action::ExecStepOver)))
			target.stepOver();
		if (ImGui::MenuItem("Step in", emu51ShortcutLabel(Emu51Action::ExecStepIn)))
			target.stepIn();
		if (ImGui::MenuItem("Step out", emu51ShortcutLabel(Emu51Action::ExecStepOut)))
			target.stepOut();
		ImGui::Separator();
		if (ImGui::MenuItem("Settings..."))
			target.requestExecutionSettings();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Window")) {
        const bool registersVisible = target.registersVisible();
        if (ImGui::MenuItem("Registers", nullptr, registersVisible)) target.setRegistersVisible(!registersVisible);
		if (ImGui::MenuItem("Peripherals")) target.showPeripherals();
		if (*target.hardwareModuleName() && ImGui::MenuItem(target.hardwareModuleName())) target.showHardwareModule();
		const bool internalRamVisible = target.internalRamMonitorVisible();
		if (ImGui::MenuItem("Internal RAM Monitor", nullptr, internalRamVisible))
			target.setInternalRamMonitorVisible(!internalRamVisible);
		const bool assemblyEditorVisible = target.assemblyEditorVisible();
		if (ImGui::MenuItem("Assembly Editor", nullptr, assemblyEditorVisible))
			target.setAssemblyEditorVisible(!assemblyEditorVisible);
		const bool projectVisible = target.projectPanelVisible();
		if (ImGui::MenuItem("Project", nullptr, projectVisible))
			target.setProjectPanelVisible(!projectVisible);
		if (ImGui::MenuItem("New Disassembly View"))
			target.createDisassemblyView();
		if (target.unifiedMode()) {
			if (ImGui::MenuItem("New External Memory Monitor"))
				target.createDataMemoryMonitor();
		}
		else {
			if (ImGui::MenuItem("New Data Memory Monitor"))
				target.createDataMemoryMonitor();
			if (ImGui::MenuItem("New Program Memory Monitor"))
				target.createProgramMemoryMonitor();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Collapse All Edge Docks"))
			target.setAllEdgeDocksCollapsed(true);
		if (ImGui::MenuItem("Expand All Edge Docks"))
			target.setAllEdgeDocksCollapsed(false);
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();
}

float emu51DrawImguiToolbar(Emu51ImguiMenuTarget& target)
{
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float topOffset = viewport->Pos.y + ImGui::GetFrameHeight();

	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, topOffset), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 0.0f), ImGuiCond_Always);
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;

	if (!ImGui::Begin("ExecutionToolbar", nullptr, flags)) {
		const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
		ImGui::End();
		return bottom;
	}

	drawRunButton(target);
	ImGui::SameLine();
	if (ImGui::Button("Debugger"))
		target.showDebugger();
	ImGui::SameLine();
	const ImVec2 buttonSize(ImGui::GetFrameHeight() * 1.35f, ImGui::GetFrameHeight());
	if (emu51ImguiIconButton("##step_over", Emu51ImguiIcon::StepOver, buttonSize))
		target.stepOver();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		ImGui::SetTooltip("Step over (F8)");
	ImGui::SameLine();
	if (emu51ImguiIconButton("##step_in", Emu51ImguiIcon::StepIn, buttonSize))
		target.stepIn();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		ImGui::SetTooltip("Step in (F7)");
	ImGui::SameLine();
	if (emu51ImguiIconButton("##step_out", Emu51ImguiIcon::StepOut, buttonSize))
		target.stepOut();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		ImGui::SetTooltip("Step out (Shift+F8)");
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float left = ImGui::GetCursorPosX();
	const float right = left + ImGui::GetContentRegionAvail().x;
	const float dockCount = static_cast<float>(kDockControls.size() + 1);
	const float dockWidth = std::clamp((right - left - (dockCount - 1) * spacing) / dockCount,
		buttonSize.y * 0.75f, buttonSize.y);
	const float dockStart = std::max(left, right - dockCount * dockWidth - (dockCount - 1) * spacing);
	const float executionEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
	const bool docksFitInline = executionEnd + spacing <= dockStart;
	const auto timing = target.executionTimingStatus();
	const bool timingFitsInline = docksFitInline && executionEnd + 4 * spacing + ImGui::CalcTextSize(timing.c_str()).x <= dockStart;
	if (timingFitsInline) {
		ImGui::SameLine(0.0f, 3 * spacing);
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", timing.c_str());
	}
	if (docksFitInline)
		ImGui::SameLine(dockStart);
	else
		ImGui::SetCursorPosX(dockStart);
	const ImVec2 dockButtonSize(dockWidth, buttonSize.y);
	bool allCollapsed = true;
	for (const auto& control : kDockControls) {
		const bool collapsed = target.edgeDockCollapsed(control.edge);
		if (dockToggle(control.id, control.icon, control.name, collapsed, dockButtonSize))
			target.setEdgeDockCollapsed(control.edge, !collapsed);
		allCollapsed &= target.edgeDockCollapsed(control.edge);
		ImGui::SameLine();
	}
	if (dockToggle("##dock_all", Emu51ImguiIcon::DockAll, "all edge docks", allCollapsed, dockButtonSize))
		target.setAllEdgeDocksCollapsed(!allCollapsed);
	if (!timingFitsInline) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("%s", timing.c_str());
		ImGui::PopStyleColor();
	}

	const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
	ImGui::End();
	return bottom;
}

