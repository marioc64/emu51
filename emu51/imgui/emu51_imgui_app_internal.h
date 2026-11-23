// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// Helpers and constants shared between the emu51_imgui_app.cpp source families
// (app.cpp, dialogs.cpp, layout.cpp). Each header function is `inline` so this
// file can be included by multiple translation units without ODR issues.

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "emu51_imgui_app.h"
#include "emu51_imgui_menu.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace emu51_imgui_internal {

inline constexpr int    kMinStepsPerFrame            = Emu51ExecutionSettings::kMinStepsPerFrame;
inline constexpr int    kMaxExecutionStepsPerAction  = Emu51ExecutionSettings::kMaxStepsPerFrame;
inline constexpr std::size_t kMaxRecentProgramFiles  = 10;
inline constexpr float  kSnapGap                     = 8.0f;
inline constexpr float  kSnapMargin                  = 8.0f;
inline constexpr float  kSnapMinCenterWidth          = 220.0f;
inline constexpr float  kSnapMinCenterHeight         = 140.0f;

inline std::string normalizedProgramPath(const std::filesystem::path& path)
{
	if (path.empty())
		return {};

	std::error_code error;
	std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
	if (error)
		normalized = std::filesystem::absolute(path, error);
	if (error)
		normalized = path;
	return normalized.lexically_normal().string();
}

inline bool isTemporaryAssemblyEditorOutputPath(const std::filesystem::path& path)
{
	const std::string filename = path.filename().string();
	constexpr std::string_view prefix = "emu51-editor-";
	return path.extension() == ".hex" &&
		filename.size() > prefix.size() &&
		filename.compare(0, prefix.size(), prefix) == 0;
}

inline int clampStepsPerFrame(int value)
{
	return std::clamp(value, kMinStepsPerFrame, kMaxExecutionStepsPerAction);
}

inline const char* fillRangeTitle(bool unifiedMode, bool fillRom)
{
	if (unifiedMode)
		return "Fill memory range";
	return fillRom ? "Fill program memory range" : "Fill data memory range";
}

inline int snapTargetIndex(Emu51ImguiSnapTarget target)
{
	switch (target) {
	case Emu51ImguiSnapTarget::Left:   return 0;
	case Emu51ImguiSnapTarget::Right:  return 1;
	case Emu51ImguiSnapTarget::Top:    return 2;
	case Emu51ImguiSnapTarget::Bottom: return 3;
	case Emu51ImguiSnapTarget::Center: return 4;
	case Emu51ImguiSnapTarget::None:
	default:                           return -1;
	}
}

inline const char* snapTargetLabel(Emu51ImguiSnapTarget target)
{
	switch (target) {
	case Emu51ImguiSnapTarget::Left:   return "left";
	case Emu51ImguiSnapTarget::Right:  return "right";
	case Emu51ImguiSnapTarget::Top:    return "top";
	case Emu51ImguiSnapTarget::Bottom: return "bottom";
	case Emu51ImguiSnapTarget::Center: return "center";
	case Emu51ImguiSnapTarget::None:
	default:                           return "floating";
	}
}

inline const char* snapTargetPersistedLabel(Emu51ImguiSnapTarget target)
{
	switch (target) {
	case Emu51ImguiSnapTarget::None:   return "none";
	case Emu51ImguiSnapTarget::Left:   return "left";
	case Emu51ImguiSnapTarget::Right:  return "right";
	case Emu51ImguiSnapTarget::Top:    return "top";
	case Emu51ImguiSnapTarget::Bottom: return "bottom";
	case Emu51ImguiSnapTarget::Center: return "center";
	default:                           return "none";
	}
}

inline bool parseSnapTarget(const std::string& text, Emu51ImguiSnapTarget* target)
{
	if (text == "none")        *target = Emu51ImguiSnapTarget::None;
	else if (text == "left")   *target = Emu51ImguiSnapTarget::Left;
	else if (text == "right")  *target = Emu51ImguiSnapTarget::Right;
	else if (text == "top")    *target = Emu51ImguiSnapTarget::Top;
	else if (text == "bottom") *target = Emu51ImguiSnapTarget::Bottom;
	else if (text == "center") *target = Emu51ImguiSnapTarget::Center;
	else return false;
	return true;
}

inline const char* paneKindLabel(Emu51ImguiApp::PaneKey::Kind kind)
{
	switch (kind) {
	case Emu51ImguiApp::PaneKey::Kind::Registers:          return "registers";
	case Emu51ImguiApp::PaneKey::Kind::Peripherals:        return "peripherals";
	case Emu51ImguiApp::PaneKey::Kind::AssemblyEditor:     return "assembly_editor";
	case Emu51ImguiApp::PaneKey::Kind::Disassembly:        return "disassembly";
	case Emu51ImguiApp::PaneKey::Kind::InternalRamMonitor: return "internal_ram";
	case Emu51ImguiApp::PaneKey::Kind::Monitor:            return "monitor";
	default:                                               return "registers";
	}
}

inline bool parsePaneKind(const std::string& text, Emu51ImguiApp::PaneKey::Kind* kind)
{
	if (text == "registers")            *kind = Emu51ImguiApp::PaneKey::Kind::Registers;
	else if (text == "peripherals")     *kind = Emu51ImguiApp::PaneKey::Kind::Peripherals;
	else if (text == "assembly_editor") *kind = Emu51ImguiApp::PaneKey::Kind::AssemblyEditor;
	else if (text == "disassembly")     *kind = Emu51ImguiApp::PaneKey::Kind::Disassembly;
	else if (text == "internal_ram")    *kind = Emu51ImguiApp::PaneKey::Kind::InternalRamMonitor;
	else if (text == "monitor")         *kind = Emu51ImguiApp::PaneKey::Kind::Monitor;
	else return false;
	return true;
}

inline float clampedSlotSpan(float available, float preferred, float minRemaining)
{
	return std::max(0.0f, std::min(preferred, std::max(0.0f, available - minRemaining)));
}

inline float clampSlotFraction(float value)
{
	return std::clamp(value, 0.12f, 0.70f);
}

inline ImVec2 childRegionSizeWithResizeGripClearance()
{
	const ImVec2 available = ImGui::GetContentRegionAvail();
	const ImGuiStyle& style = ImGui::GetStyle();
	const float gripClearance = std::max(style.GrabMinSize, ImGui::GetFontSize()) + style.FramePadding.y;
	return ImVec2(
		std::max(0.0f, available.x - gripClearance),
		std::max(0.0f, available.y - gripClearance));
}

struct SnapOverlayTarget {
	Emu51ImguiSnapTarget target = Emu51ImguiSnapTarget::None;
	ImRect rect;
};

inline ImRect paneSnapWorkRect(ImGuiViewport* viewport, float contentTop)
{
	return ImRect(
		ImVec2(viewport->Pos.x + kSnapMargin, contentTop + kSnapMargin),
		ImVec2(viewport->Pos.x + viewport->Size.x - kSnapMargin, viewport->Pos.y + viewport->Size.y - kSnapMargin));
}

inline std::array<SnapOverlayTarget, 5> buildSnapOverlayTargets(ImGuiViewport* viewport, float contentTop)
{
	const ImRect workRect = paneSnapWorkRect(viewport, contentTop);
	const ImVec2 edgeSize(88.0f, 88.0f);
	const ImVec2 centerSize(96.0f, 96.0f);
	const float inset = 24.0f;
	const ImVec2 center = workRect.GetCenter();

	std::array<SnapOverlayTarget, 5> targets = {{
		{Emu51ImguiSnapTarget::Left, ImRect(
			ImVec2(workRect.Min.x + inset, center.y - edgeSize.y * 0.5f),
			ImVec2(workRect.Min.x + inset + edgeSize.x, center.y + edgeSize.y * 0.5f))},
		{Emu51ImguiSnapTarget::Right, ImRect(
			ImVec2(workRect.Max.x - inset - edgeSize.x, center.y - edgeSize.y * 0.5f),
			ImVec2(workRect.Max.x - inset, center.y + edgeSize.y * 0.5f))},
		{Emu51ImguiSnapTarget::Top, ImRect(
			ImVec2(center.x - edgeSize.x * 0.5f, workRect.Min.y + inset),
			ImVec2(center.x + edgeSize.x * 0.5f, workRect.Min.y + inset + edgeSize.y))},
		{Emu51ImguiSnapTarget::Bottom, ImRect(
			ImVec2(center.x - edgeSize.x * 0.5f, workRect.Max.y - inset - edgeSize.y),
			ImVec2(center.x + edgeSize.x * 0.5f, workRect.Max.y - inset))},
		{Emu51ImguiSnapTarget::Center, ImRect(
			ImVec2(center.x - centerSize.x * 0.5f, center.y - centerSize.y * 0.5f),
			ImVec2(center.x + centerSize.x * 0.5f, center.y + centerSize.y * 0.5f))},
	}};
	return targets;
}

inline Emu51ImguiSnapTarget hitTestSnapOverlayTarget(
	const std::array<SnapOverlayTarget, 5>& targets, const ImVec2& mousePos)
{
	for (const SnapOverlayTarget& target : targets) {
		if (target.rect.Contains(mousePos))
			return target.target;
	}
	return Emu51ImguiSnapTarget::None;
}

} // namespace emu51_imgui_internal
