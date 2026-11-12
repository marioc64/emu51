// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_draw.h"

#include <cstdio>

#include "imgui.h"
#include "imgui_internal.h"

namespace {

constexpr ImU32 kContentRowEvenColor = IM_COL32(20, 20, 20, 255);
constexpr ImU32 kContentRowOddColor = IM_COL32(34, 34, 34, 255);
constexpr ImU32 kPcMarkerColor = IM_COL32(255, 96, 96, 255);
constexpr ImU32 kDptrMarkerColor = IM_COL32(96, 160, 255, 255);

void drawAddressMarkerRect(const ImRect& rect, std::uint16_t address, std::uint16_t pc, std::uint16_t dptr)
{
	const bool markPc = address == pc;
	const bool markDptr = address == dptr;
	if (!markPc && !markDptr)
		return;

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImRect outer = rect;
	outer.Expand(ImVec2(2.0f, 1.0f));
	if (markPc)
		drawList->AddRect(outer.Min, outer.Max, kPcMarkerColor, 2.0f, 0, 1.5f);
	if (markDptr) {
		ImRect inner = outer;
		if (markPc)
			inner.Expand(ImVec2(-2.0f, -2.0f));
		drawList->AddRect(inner.Min, inner.Max, kDptrMarkerColor, 2.0f, 0, 1.5f);
	}
}

}  // namespace

void emu51DrawAlternatingContentRowBackground(int rowIndex, float width, float height)
{
	if (width <= 0.0f || height <= 0.0f)
		return;

	const ImVec2 cursor = ImGui::GetCursorScreenPos();
	const ImVec2 min(cursor.x, cursor.y - 2.0f);
	const ImVec2 max(min.x + width, min.y + height);
	const ImU32 color = (rowIndex % 2 == 0) ? kContentRowEvenColor : kContentRowOddColor;
	ImGui::GetWindowDrawList()->AddRectFilled(min, max, color);
}

void emu51DrawMarkedHexByte(std::uint8_t value, std::uint16_t address, std::uint16_t pc, std::uint16_t dptr)
{
	char text[3];
	std::snprintf(text, sizeof(text), "%02X", value);
	ImGui::TextUnformatted(text);
	drawAddressMarkerRect(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), address, pc, dptr);
}
