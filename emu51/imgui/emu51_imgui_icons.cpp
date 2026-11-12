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
	}
	return kCodiconDebugStart;
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
	ImGui::GetWindowDrawList()->AddText(centeredIconPosition(rect, glyph), ImGui::GetColorU32(ImGuiCol_Text), glyph.text);
	return clicked;
}
