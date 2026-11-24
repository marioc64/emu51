// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "emu51_imgui_draw.h"
#include "emu51_imgui_input.h"
#include "emu51_imgui_seg.h"
#include "imgui.h"

namespace {

namespace SD = SegmentDisplay;

constexpr float kMonitorKeyRepeatDelay = 0.35f;
constexpr float kMonitorKeyRepeatRate = 0.06f;
constexpr float kMonitorKeyRepeatMaxFrameDelta = 0.05f;
constexpr int kMonitorBytesPerGroup = 4;
constexpr float kMonitorColumnGap = 20.0f;
constexpr float kMonitorGroupGap = 10.0f;
constexpr float kMonitorAsciiGroupGap = 4.0f;
constexpr float kMonitorSegmentGroupGap = 4.0f;
constexpr float kMonitorSegmentDigitSpacing = 2.0f;
constexpr float kMonitorSegmentScale = 1.0f;

char monitorAscii(std::uint8_t value)
{
	return value >= 0x20 && value <= 0x7e ? static_cast<char>(value) : '.';
}

float monitorAddressColumnWidth(int addressDigits)
{
	return ImGui::CalcTextSize(addressDigits == 2 ? "FF:" : "FFFF:").x;
}

float monitorDataGroupWidth()
{
	return ImGui::CalcTextSize("FF FF FF FF").x;
}

float monitorAsciiGroupWidth()
{
	return ImGui::CalcTextSize("....").x;
}

float monitorSegmentGroupWidth()
{
	const float digitWidth = SD::Advance(kMonitorSegmentScale);
	return digitWidth * static_cast<float>(kMonitorBytesPerGroup) +
		kMonitorSegmentDigitSpacing * static_cast<float>(kMonitorBytesPerGroup - 1);
}

float monitorDataColumnWidth(int groupsPerRow)
{
	return monitorDataGroupWidth() * static_cast<float>(groupsPerRow) +
		kMonitorGroupGap * static_cast<float>(std::max(0, groupsPerRow - 1));
}

float monitorAsciiColumnWidth(int groupsPerRow)
{
	return monitorAsciiGroupWidth() * static_cast<float>(groupsPerRow) +
		kMonitorAsciiGroupGap * static_cast<float>(std::max(0, groupsPerRow - 1));
}

float monitorSegmentColumnWidth(int groupsPerRow)
{
	return monitorSegmentGroupWidth() * static_cast<float>(groupsPerRow) +
		kMonitorSegmentGroupGap * static_cast<float>(std::max(0, groupsPerRow - 1));
}

float monitorRowWidth(int addressDigits, int groupsPerRow)
{
	return monitorAddressColumnWidth(addressDigits) +
		kMonitorColumnGap +
		monitorDataColumnWidth(groupsPerRow) +
		kMonitorColumnGap +
		monitorAsciiColumnWidth(groupsPerRow) +
		kMonitorColumnGap +
		monitorSegmentColumnWidth(groupsPerRow);
}

int monitorGroupsPerRowForWidth(float availableWidth, int addressDigits)
{
	int groupsPerRow = 1;
	for (int candidate = 2; ; ++candidate) {
		if (monitorRowWidth(addressDigits, candidate) > availableWidth)
			break;
		groupsPerRow = candidate;
	}
	return groupsPerRow;
}

void formatMonitorAsciiGroup(
	const std::array<std::uint8_t, kMonitorBytesPerGroup>& values,
	const std::array<bool, kMonitorBytesPerGroup>& present,
	char* buffer,
	size_t bufferSize)
{
	if (bufferSize == 0)
		return;

	size_t cursor = 0;
	for (int i = 0; i < kMonitorBytesPerGroup && cursor + 1 < bufferSize; ++i)
		buffer[cursor++] = present[static_cast<size_t>(i)] ? monitorAscii(values[static_cast<size_t>(i)]) : ' ';
	buffer[std::min(cursor, bufferSize - 1)] = '\0';
}

}  // namespace

void Emu51ImguiApp::drawMonitorViews()
{
	if (internalRamMonitor_.open)
		drawMonitorView(internalRamMonitor_);

	for (auto it = monitorViews_.begin(); it != monitorViews_.end(); ) {
		drawMonitorView(*it);
		if (!it->open) {
			const PaneKey pane{PaneKey::Kind::Monitor, it->id};
			removePaneSnapTarget(pane);
			if (hasActivePane_ && activePane_ == pane)
				hasActivePane_ = false;
			it = monitorViews_.erase(it);
			continue;
		}
		++it;
	}
}

void Emu51ImguiApp::drawMonitorView(MonitorView& view)
{
	char title[64];
	char popupId[64];
	const PaneKey pane{
		view.memoryKind == MonitorView::MemoryKind::InternalRam ? PaneKey::Kind::InternalRamMonitor : PaneKey::Kind::Monitor,
		view.memoryKind == MonitorView::MemoryKind::InternalRam ? 0 : view.id
	};
	const int addressDigits = monitorAddressDigits(view);
	const std::uint16_t maxAddress = clampMonitorAddress(view, Emu51ImguiApp::kMemoryBytes);
	const char* name = monitorName(view);
	if (addressDigits == 2)
		std::snprintf(title, sizeof(title), "%s @ %02X###monitor_%d", name, view.start & 0xff, view.id);
	else
		std::snprintf(title, sizeof(title), "%s @ %04X###monitor_%d", name, view.start, view.id);
	std::snprintf(popupId, sizeof(popupId), "Goto###monitor_goto_%d", view.id);
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoNavInputs;
	applyWindowPlacement(pane, ImVec2(430.0f, 400.0f), &flags);
	ImGui::Begin(title, &view.open, flags);
	registerPaneWindow(pane);
	drawPaneSnapContextMenu(pane);

	const ImVec2 childSize = ImGui::GetContentRegionAvail();
	ImGui::BeginChild("monitor_view", childSize, false,
		ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs);
	ImGui::SetItemDefaultFocus();

	const float childHeight = ImGui::GetContentRegionAvail().y;
	const ImGuiStyle& style = ImGui::GetStyle();
	const float textRowHeight = ImGui::GetTextLineHeight();
	const float segmentRowHeight = SD::Size(kMonitorSegmentScale).y;
	const float rowHeight = std::max(textRowHeight, segmentRowHeight);
	const float rowAdvance = rowHeight + style.ItemSpacing.y;
	const float availableWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x);
	const int groupsPerRow = monitorGroupsPerRowForWidth(availableWidth, addressDigits);
	const int bytesPerRow = groupsPerRow * kMonitorBytesPerGroup;
	const int visibleRows = std::max(1, static_cast<int>(std::floor((childHeight + style.ItemSpacing.y) / rowAdvance)));
	const float addressWidth = monitorAddressColumnWidth(addressDigits);
	const float dataColumnX = addressWidth + kMonitorColumnGap;
	const float dataWidth = monitorDataColumnWidth(groupsPerRow);
	const float asciiColumnX = dataColumnX + dataWidth + kMonitorColumnGap;
	const float asciiWidth = monitorAsciiColumnWidth(groupsPerRow);
	const float segmentColumnX = asciiColumnX + asciiWidth + kMonitorColumnGap;
	const float rowWidth = segmentColumnX + monitorSegmentColumnWidth(groupsPerRow);
	const float byteTextWidth = ImGui::CalcTextSize("00").x;
	const float byteTextGap = ImGui::CalcTextSize(" ").x;
	const bool markExternalAddresses = view.memoryKind != MonitorView::MemoryKind::InternalRam;

	const bool monitorActive =
		ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
		ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	notePaneActivity(pane, monitorActive);
	captureFloatingPaneState(pane);

	if (monitorActive && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
		if (addressDigits == 2)
			std::snprintf(view.gotoAddress, sizeof(view.gotoAddress), "%02X", view.start & 0xff);
		else
			std::snprintf(view.gotoAddress, sizeof(view.gotoAddress), "%04X", view.start);
		view.openGotoPopup = true;
	}

	if (monitorActive) {
		const ImGuiIO& io = ImGui::GetIO();
		const bool upPressed = ImGui::IsKeyPressed(ImGuiKey_UpArrow, false);
		const bool downPressed = ImGui::IsKeyPressed(ImGuiKey_DownArrow, false);
		const bool upHeld = ImGui::IsKeyDown(ImGuiKey_UpArrow);
		const bool downHeld = ImGui::IsKeyDown(ImGuiKey_DownArrow);
		int scrollDirection = 0;
		bool immediateStep = false;
		if (upPressed != downPressed) {
			scrollDirection = upPressed ? -1 : 1;
			immediateStep = true;
		} else if (upHeld != downHeld) {
			scrollDirection = upHeld ? -1 : 1;
		} else if (upHeld && downHeld) {
			scrollDirection = view.keyScrollDirection;
		}
		if (scrollDirection != 0) {
			virtualScrollActive_ = true;
			const auto scrollOneRow = [this, &view, bytesPerRow, scrollDirection]() {
				view.start = clampMonitorAddress(view, static_cast<int>(view.start) + scrollDirection * bytesPerRow);
			};

			if (immediateStep || view.keyScrollDirection != scrollDirection) {
				view.keyScrollDirection = scrollDirection;
				view.keyScrollRepeatTimer = -kMonitorKeyRepeatDelay;
				scrollOneRow();
			} else {
				view.keyScrollRepeatTimer += std::min(io.DeltaTime, kMonitorKeyRepeatMaxFrameDelta);
				if (view.keyScrollRepeatTimer >= 0.0f) {
					scrollOneRow();
					view.keyScrollRepeatTimer = std::min(0.0f, view.keyScrollRepeatTimer - kMonitorKeyRepeatRate);
				}
			}
		} else {
			view.keyScrollDirection = 0;
			view.keyScrollRepeatTimer = 0.0f;
		}
	} else {
		view.keyScrollDirection = 0;
		view.keyScrollRepeatTimer = 0.0f;
	}

	if (view.openGotoPopup) {
		ImGui::OpenPopup(popupId);
		view.openGotoPopup = false;
	}
	if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("Go to address");
		ImGui::SetNextItemWidth(100.0f);
		ImGui::SetKeyboardFocusHere();
		if (ImGui::InputText("##monitor_goto_address", view.gotoAddress, sizeof(view.gotoAddress),
				ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
			if (const auto parsed = emu51ParseHexInput(view.gotoAddress, 0xffff)) {
				view.start = clampMonitorAddress(view, static_cast<int>(*parsed));
				ImGui::CloseCurrentPopup();
			} else {
				statusMessage_ = "invalid address";
			}
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	for (int row = 0; row < visibleRows; ++row) {
		const int rowAddress = static_cast<int>(view.start) + row * bytesPerRow;
		if (rowAddress > maxAddress)
			break;

		char address[6];
		char asciiText[16];
		if (addressDigits == 2)
			std::snprintf(address, sizeof(address), "%02X:", rowAddress & 0xff);
		else
			std::snprintf(address, sizeof(address), "%04X:", rowAddress);

		emu51DrawAlternatingContentRowBackground(row, rowWidth, rowAdvance);
		const float rowStartX = ImGui::GetCursorPosX();
		ImGui::TextUnformatted(address);
		ImGui::SameLine(rowStartX + dataColumnX);

		for (int group = 0; group < groupsPerRow; ++group) {
			if (group > 0)
				ImGui::SameLine(0.0f, kMonitorGroupGap);
			for (int byteIndex = 0; byteIndex < kMonitorBytesPerGroup; ++byteIndex) {
				if (byteIndex > 0)
					ImGui::SameLine(0.0f, byteTextGap);
				const int addressOffset = group * kMonitorBytesPerGroup + byteIndex;
				const int cellAddress = rowAddress + addressOffset;
				if (cellAddress <= maxAddress) {
					const std::uint16_t byteAddress = static_cast<std::uint16_t>(cellAddress);
					const std::uint8_t value = readMonitorByte(view, byteAddress);
					if (markExternalAddresses)
						emu51DrawMarkedHexByte(value, byteAddress, cpu_.PC, cpu_.DPTR());
					else
						ImGui::Text("%02X", value);
				}
				else {
					ImGui::Dummy(ImVec2(byteTextWidth, rowHeight));
				}
			}
		}

		ImGui::SameLine(rowStartX + asciiColumnX);
		for (int group = 0; group < groupsPerRow; ++group) {
			if (group > 0)
				ImGui::SameLine(0.0f, kMonitorAsciiGroupGap);
			std::array<std::uint8_t, kMonitorBytesPerGroup> values{};
			std::array<bool, kMonitorBytesPerGroup> present{};
			for (int byteIndex = 0; byteIndex < kMonitorBytesPerGroup; ++byteIndex) {
				const int addressOffset = group * kMonitorBytesPerGroup + byteIndex;
				const int cellAddress = rowAddress + addressOffset;
				const bool hasByte = cellAddress <= maxAddress;
				present[static_cast<size_t>(byteIndex)] = hasByte;
				if (hasByte)
					values[static_cast<size_t>(byteIndex)] = readMonitorByte(view, static_cast<std::uint16_t>(cellAddress));
			}
			formatMonitorAsciiGroup(values, present, asciiText, sizeof(asciiText));
			ImGui::TextUnformatted(asciiText);
		}

		ImGui::SameLine(rowStartX + segmentColumnX);
		for (int byteIndex = 0; byteIndex < bytesPerRow; ++byteIndex) {
			if (byteIndex > 0) {
				ImGui::SameLine(
					0.0f,
					byteIndex % kMonitorBytesPerGroup == 0 ? kMonitorSegmentGroupGap : kMonitorSegmentDigitSpacing);
			}

			const int cellAddress = rowAddress + byteIndex;
			if (cellAddress <= maxAddress)
				SD::Digit(readMonitorByte(view, static_cast<std::uint16_t>(cellAddress)), kMonitorSegmentScale);
			else
				ImGui::Dummy(ImVec2(SD::Advance(kMonitorSegmentScale), segmentRowHeight));
		}
	}

	ImGui::EndChild();
	ImGui::End();
}

std::uint16_t Emu51ImguiApp::clampMonitorAddress(const MonitorView& view, int address) const
{
	const int maxAddress = view.memoryKind == MonitorView::MemoryKind::InternalRam ? static_cast<int>(cpu_.modelInfo().internalRamBytes) - 1 : (Emu51ImguiApp::kMemoryBytes - 1);
	return static_cast<std::uint16_t>(std::clamp(address, 0, maxAddress));
}

std::uint8_t Emu51ImguiApp::readMonitorByte(const MonitorView& view, std::uint16_t address)
{
	switch (view.memoryKind) {
	case MonitorView::MemoryKind::InternalRam:
		return cpu_.readIndirect(static_cast<std::uint8_t>(address));
	case MonitorView::MemoryKind::ExternalProgram:
		if (peripherals_ && peripherals_->circuitBus()) return readProgramByte(address);
		return unifiedMode_ ? cpu_.readData(address) : cpu_.readProg(address);
	case MonitorView::MemoryKind::ExternalData:
	default:
		return cpu_.readData(address);
	}
}

const char* Emu51ImguiApp::monitorName(const MonitorView& view) const
{
	switch (view.memoryKind) {
	case MonitorView::MemoryKind::InternalRam:
		return "Internal RAM";
	case MonitorView::MemoryKind::ExternalProgram:
		return unifiedMode_ ? "External memory" : "Program memory";
	case MonitorView::MemoryKind::ExternalData:
	default:
		return unifiedMode_ ? "External memory" : "Data memory";
	}
}

int Emu51ImguiApp::monitorAddressDigits(const MonitorView& view) const
{
	return view.memoryKind == MonitorView::MemoryKind::InternalRam ? 2 : 4;
}
