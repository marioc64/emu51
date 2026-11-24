// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"
#include "emu51_imgui_app_internal.h"
#include "emu51_imgui_input.h"
#include "imgui.h"

#include <cstdint>

namespace {
using emu51_imgui_internal::clampStepsPerFrame;
using emu51_imgui_internal::fillRangeTitle;
} // namespace

void Emu51ImguiApp::drawExecutionSettingsDialog()
{
	if (executionSettingsDialog_.open) {
		ImGui::OpenPopup("Execution settings");
		executionSettingsDialog_.open = false;
	}

	if (!ImGui::BeginPopupModal("Execution settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

    ImGui::SetNextItemWidth(ImGui::CalcTextSize("8052 (256 bytes RAM, Timer 2)").x + ImGui::GetFrameHeight() * 1.5f);
    ImGui::Combo("CPU model", &executionSettingsDialog_.cpuModel, "8051 (128 bytes RAM)\0" "8052 (256 bytes RAM, Timer 2)\0");
    if (executionSettingsDialog_.cpuModel != static_cast<int>(cpu_.model()))
        ImGui::TextWrapped("Changing the CPU model resets and pauses execution. Loaded program memory is preserved.");
    ImGui::TextDisabled("CPU model is saved with the project.");
    if (peripherals_) ImGui::TextWrapped("Changing the crystal frequency also resets the peripheral graph and closes audio devices.");
    ImGui::TextDisabled("Select a matching $MOD51 or $MOD52 in assembly sources.");
	ImGui::Text("Run mode setting");
	const ImGuiStyle& style = ImGui::GetStyle();
	const float inputOffset = ImGui::GetCursorPosX() + ImGui::GetFrameHeight() +
		style.ItemInnerSpacing.x + ImGui::CalcTextSize("Instructions per frame").x + style.ItemSpacing.x;
	if (ImGui::RadioButton("Instructions per frame", !executionSettingsDialog_.cycleExact))
		executionSettingsDialog_.cycleExact = false;
	ImGui::SameLine(inputOffset);
	ImGui::BeginDisabled(executionSettingsDialog_.cycleExact);
	ImGui::SetNextItemWidth(140.0f);
	ImGui::InputInt("##instructions_per_frame", &executionSettingsDialog_.stepsPerFrame, 1, 100);
	executionSettingsDialog_.stepsPerFrame = clampStepsPerFrame(executionSettingsDialog_.stepsPerFrame);
	ImGui::EndDisabled();

	if (!executionSettingsDialog_.cycleExact)
		ImGui::TextDisabled("One logical frame is 1/60 second, independent of display refresh.");
	if (ImGui::RadioButton("Cycle exact", executionSettingsDialog_.cycleExact))
		executionSettingsDialog_.cycleExact = true;
	ImGui::SameLine(inputOffset);
	const bool graphClock = peripherals_ && std::any_of(peripherals_->config().devices.begin(),peripherals_->config().devices.end(),[](const auto& d) { return d.kind == Emu51PeripheralKind::Clock; });
	if (graphClock) executionSettingsDialog_.crystalMHz = crystalMHz_;
	ImGui::BeginDisabled(!executionSettingsDialog_.cycleExact || graphClock);
	ImGui::SetNextItemWidth(140.0f);
	ImGui::InputDouble("MHz", &executionSettingsDialog_.crystalMHz, 0.0, 0.0, "%.9g");
	ImGui::EndDisabled();
	if (graphClock) ImGui::TextDisabled("Oscillator frequency is configured in the Clock block inspector.");

	const bool validMHz = Emu51CycleTiming::validCrystalMHz(executionSettingsDialog_.crystalMHz);
	const bool validSettings = !executionSettingsDialog_.cycleExact || validMHz;
	if (!validSettings)
		ImGui::TextDisabled("Enter a frequency from 0.000001 to 1000 MHz.");

	ImGui::BeginDisabled(!validSettings);
    if (ImGui::Button("Apply")) {
        executionSettingsDialog_.applyError.clear();
        const bool changedClock = peripherals_ && validMHz && executionSettingsDialog_.crystalMHz != crystalMHz_;
        if (changedClock && !applyPeripheralConfig(peripherals_->config(), executionSettingsDialog_.crystalMHz)) {
            executionSettingsDialog_.applyError = peripheralMessage_;
        } else {
            setCpuModel(static_cast<Emu51Model>(executionSettingsDialog_.cpuModel));
            stepsPerFrame_ = clampStepsPerFrame(executionSettingsDialog_.stepsPerFrame);
            cycleExact_ = executionSettingsDialog_.cycleExact;
            if (validMHz) crystalMHz_ = executionSettingsDialog_.crystalMHz;
            cycleTiming_.reset();
            statusMessage_ = cycleExact_ ? "updated cycle exact timing" : "updated instructions per frame";
            executionSettingsDialog_.saveFailed = !saveExecutionSettings();
            if (!executionSettingsDialog_.saveFailed) ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		ImGui::CloseCurrentPopup();
    if (!executionSettingsDialog_.applyError.empty()) {
        ImGui::TextWrapped("%s", executionSettingsDialog_.applyError.c_str());
    }
	if (executionSettingsDialog_.saveFailed)
		ImGui::TextUnformatted("Settings applied, but could not save configuration.");

	ImGui::EndPopup();
}

void Emu51ImguiApp::drawBinaryLoadDialog()
{
	if (binaryLoadDialog_.open) {
		ImGui::OpenPopup("Load binary program");
		binaryLoadDialog_.open = false;
		binaryLoadDialog_.focusAddressInput = true;
	}

	if (!ImGui::BeginPopupModal("Load binary program", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	ImGui::Text("Load binary at address");
	ImGui::SetNextItemWidth(100.0f);
	if (binaryLoadDialog_.focusAddressInput) {
		ImGui::SetKeyboardFocusHere();
		binaryLoadDialog_.focusAddressInput = false;
	}
	const bool submitted = ImGui::InputText(
		"Address",
		binaryLoadDialog_.address,
		sizeof(binaryLoadDialog_.address),
		ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
	if (ImGui::Button("Load") || submitted) {
		const auto parsed = emu51ParseHexInput(binaryLoadDialog_.address, 0xffff);
		if (!parsed) {
			statusMessage_ = "invalid load address";
		} else if (loadProgram(
				binaryLoadDialog_.path.c_str(),
				binaryLoadDialog_.resetCpuBeforeLoad,
				binaryLoadDialog_.resetCpuBeforeLoad,
				static_cast<std::uint16_t>(*parsed))) {
			binaryLoadDialog_.path.clear();
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		binaryLoadDialog_.path.clear();
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Emu51ImguiApp::drawSetProgramCounterDialog()
{
	if (setProgramCounterDialog_.open) {
		ImGui::OpenPopup("Set program counter");
		setProgramCounterDialog_.open = false;
	}

	if (!ImGui::BeginPopupModal("Set program counter", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	ImGui::Text("Set PC to address");
	ImGui::SetNextItemWidth(100.0f);
	if (setProgramCounterDialog_.focusInput) {
		ImGui::SetKeyboardFocusHere();
		setProgramCounterDialog_.focusInput = false;
	}
	const bool submitted = ImGui::InputText(
		"PC",
		setProgramCounterDialog_.value,
		sizeof(setProgramCounterDialog_.value),
		ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
	if (!running_ && (ImGui::Button("Apply") || submitted)) {
		const auto parsed = emu51ParseHexInput(setProgramCounterDialog_.value, 0xffff);
		if (!parsed) {
			statusMessage_ = "invalid pc value";
		} else {
			cpu_.PC = static_cast<std::uint16_t>(*parsed);
			statusMessage_ = "pc updated";
			updateDisassemblyFollowRange();
			ImGui::CloseCurrentPopup();
		}
	}
	if (running_) {
		ImGui::TextDisabled("Stop execution to change PC.");
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		ImGui::CloseCurrentPopup();

	ImGui::EndPopup();
}

void Emu51ImguiApp::drawFillRangeDialog()
{
	if (fillRangeDialog_.open) {
		ImGui::OpenPopup(fillRangeTitle(unifiedMode_, fillRangeDialog_.fillRom));
		fillRangeDialog_.open = false;
	}

	const char* popupTitle = fillRangeTitle(unifiedMode_, fillRangeDialog_.fillRom);
	if (!ImGui::BeginPopupModal(popupTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	ImGui::SetNextItemWidth(100.0f);
	ImGui::InputText("Start", fillRangeDialog_.start, sizeof(fillRangeDialog_.start), ImGuiInputTextFlags_CharsHexadecimal);
	ImGui::SetNextItemWidth(100.0f);
	ImGui::InputText("End", fillRangeDialog_.end, sizeof(fillRangeDialog_.end), ImGuiInputTextFlags_CharsHexadecimal);
	ImGui::SetNextItemWidth(60.0f);
	ImGui::InputText("Value", fillRangeDialog_.value, sizeof(fillRangeDialog_.value), ImGuiInputTextFlags_CharsHexadecimal);

	if (ImGui::Button("Apply")) {
		const auto parsedStart = emu51ParseHexInput(fillRangeDialog_.start, 0xffff);
		const auto parsedEnd = emu51ParseHexInput(fillRangeDialog_.end, 0xffff);
		const auto parsedValue = emu51ParseHexInput(fillRangeDialog_.value, 0xff);
		if (!parsedStart || !parsedEnd || !parsedValue) {
			statusMessage_ = "invalid fill range input";
		} else {
			fillRange(
				fillRangeDialog_.fillRom,
				static_cast<std::uint16_t>(*parsedStart),
				static_cast<std::uint16_t>(*parsedEnd),
				static_cast<std::uint8_t>(*parsedValue));
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		ImGui::CloseCurrentPopup();

	ImGui::EndPopup();
}
