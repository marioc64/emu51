// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_icons.h"

#include <algorithm>

#include "imgui_internal.h"

namespace {

struct IconGlyph {
	const char* text = "";
	ImWchar codepoint = 0;
};

constexpr IconGlyph kCodiconDebugPause{"\xee\xab\x91", 0xead1};
constexpr IconGlyph kCodiconDebugStart{"\xee\xab\x93", 0xead3};
constexpr IconGlyph kCodiconDebugStepInto{"\xee\xab\x94", 0xead4};
constexpr IconGlyph kCodiconDebugStepOut{"\xee\xab\x95", 0xead5};
constexpr IconGlyph kCodiconDebugStepOver{"\xee\xab\x96", 0xead6};

IconGlyph iconGlyph(Emu51ImguiIcon icon)
{
	switch (icon) {
	case Emu51ImguiIcon::Run:
		return kCodiconDebugStart;
	case Emu51ImguiIcon::Pause:
		return kCodiconDebugPause;
	case Emu51ImguiIcon::StepOver:
		return kCodiconDebugStepOver;
	case Emu51ImguiIcon::StepIn:
		return kCodiconDebugStepInto;
	case Emu51ImguiIcon::StepOut:
		return kCodiconDebugStepOut;
	default:
		return {};
	}
}

void drawDockIcon(const ImRect& button, Emu51ImguiIcon icon)
{
	const auto center = button.GetCenter();
	const float side = IM_TRUNC(std::min(button.GetWidth(), button.GetHeight()) * 0.32f);
	const float marker = std::max(2.0f, IM_TRUNC(side * 0.5f));
	const ImVec2 min(IM_ROUND(center.x - side), IM_ROUND(center.y - side));
	const ImVec2 max(min.x + side * 2, min.y + side * 2);
	auto* draw = ImGui::GetWindowDrawList();
	const auto color = ImGui::GetColorU32(ImGuiCol_Text);
	draw->AddRect(min, max, color);
	if (icon == Emu51ImguiIcon::DockLeft || icon == Emu51ImguiIcon::DockAll)
		draw->AddRectFilled(min, {min.x + marker, max.y}, color);
	if (icon == Emu51ImguiIcon::DockTop || icon == Emu51ImguiIcon::DockAll)
		draw->AddRectFilled(min, {max.x, min.y + marker}, color);
	if (icon == Emu51ImguiIcon::DockRight || icon == Emu51ImguiIcon::DockAll)
		draw->AddRectFilled({max.x - marker, min.y}, max, color);
	if (icon == Emu51ImguiIcon::DockBottom || icon == Emu51ImguiIcon::DockAll)
		draw->AddRectFilled({min.x, max.y - marker}, max, color);
}

ImVec2 centeredIconPosition(const ImRect& rect, const IconGlyph& icon)
{
	ImFontBaked* baked = ImGui::GetFontBaked();
	const ImFontGlyph* glyph = baked != nullptr ? baked->FindGlyph(icon.codepoint) : nullptr;
	if (glyph != nullptr && glyph->Visible) {
		const float scale = ImGui::GetFontSize() / baked->Size;
		return ImVec2(
			rect.GetCenter().x - (glyph->X0 + glyph->X1) * scale * 0.5f,
			rect.GetCenter().y - (glyph->Y0 + glyph->Y1) * scale * 0.5f + 1.0f);
	}

	const ImVec2 textSize = ImGui::CalcTextSize(icon.text);
	return ImVec2(
		rect.Min.x + std::max(0.0f, rect.GetWidth() - textSize.x) * 0.5f,
		rect.Min.y + std::max(0.0f, rect.GetHeight() - textSize.y) * 0.5f);
}

}  // namespace

bool emu51ImguiIconButton(const char* id, Emu51ImguiIcon icon, const ImVec2& size)
{
	const bool clicked = ImGui::Button(id, size);
	const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
	const IconGlyph glyph = iconGlyph(icon);
	if (glyph.codepoint)
		ImGui::GetWindowDrawList()->AddText(centeredIconPosition(rect, glyph), ImGui::GetColorU32(ImGuiCol_Text), glyph.text);
	else
		drawDockIcon(rect, icon);
	return clicked;
}
