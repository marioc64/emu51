// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_seg.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr ImU32 kSegmentDisplayOffColor = IM_COL32(48, 48, 48, 255);
constexpr ImU32 kSegmentDisplayOnColor = IM_COL32(0, 230, 0, 255);
constexpr int kSegmentBitA = 0;
constexpr int kSegmentBitB = 1;
constexpr int kSegmentBitC = 2;
constexpr int kSegmentBitD = 3;
constexpr int kSegmentBitE = 4;
constexpr int kSegmentBitF = 5;
constexpr int kSegmentBitG = 6;
constexpr int kSegmentBitDp = 7;

}  // namespace

namespace SegmentDisplay {

ImVec2 Size(float scale)
{
	return ImVec2(6.0f * scale, 12.0f * scale);
}

float Advance(float scale)
{
	const ImVec2 size = Size(scale);
	//const float thickness = 5.0f * scale;
	const float margin = 3.0f * scale;
	const float dotRadius =  1.0f;
	return size.x + dotRadius + 2.0f * margin;
}

void Digit(std::uint8_t value, float scale)
{
	// Common direct-wired MCU layout: bit0=a, bit1=b, bit2=c, bit3=d, bit4=e, bit5=f, bit6=g, bit7=dp.
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImDrawListFlags savedFlags = drawList->Flags;
	//drawList->Flags |= ImDrawListFlags_AntiAliasedFill;
	//drawList->Flags |= ImDrawListFlags_AntiAliasedLines;
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 size = Size(scale);
	const float margin = 3.0f * scale;
	const float left = origin.x + margin;
	const float right = left + size.x;
	const float top = origin.y;
	const float mid = top + size.y * 0.5f;
	const float bottom = top + size.y;
	const float shift = 1.0f * scale;
	const float dotRadius = 1.0f;

	const auto segmentColor = [value](int bit) {
		return (value & (1u << bit)) != 0 ? kSegmentDisplayOnColor : kSegmentDisplayOffColor;
	};
	const auto drawHorizontalSegment = [&](int bit, float y) {
		drawList->AddLine(
			ImVec2(left + shift, y),
			ImVec2(right - shift + 0.01f, y),
			segmentColor(bit),
			1.0f);
	};
	const auto drawVerticalSegment = [&](int bit, float x, float y0, float y1) {		
		drawList->AddLine(
			ImVec2(x, y0 + shift),
			ImVec2(x, y1 - shift + 0.01f),
			segmentColor(bit),
			1.0f);
	};

	drawHorizontalSegment(kSegmentBitA, top);
	drawVerticalSegment(kSegmentBitF, left, top, mid);
	drawVerticalSegment(kSegmentBitB, right, top, mid);
	drawHorizontalSegment(kSegmentBitG, mid);
	drawVerticalSegment(kSegmentBitE, left, mid, bottom);
	drawVerticalSegment(kSegmentBitC, right, mid, bottom);
	drawHorizontalSegment(kSegmentBitD, bottom);
	drawList->AddCircleFilled(
		ImVec2(right + margin + dotRadius / 2.0f, bottom),
		dotRadius * scale,
		segmentColor(kSegmentBitDp),
		8);

	drawList->Flags = savedFlags;
	ImGui::Dummy(ImVec2(size.x + dotRadius * 2.0f + margin, size.y));
}

}  // namespace SegmentDisplay
