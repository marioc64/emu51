// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"
#include <algorithm>

bool Emu51ImguiApp::fillCircuitMemory(bool code, std::uint16_t start, std::uint16_t end, std::uint8_t value, bool skipUnmapped)
{
    if (!peripherals_ || !peripherals_->circuitBus()) return false;
    std::set<std::pair<std::string,std::uint64_t>> writes;
    for (unsigned address = start; address <= end; ++address) {
        Emu51PhysicalAddress physical;
        if (!peripherals_->resolve(code,static_cast<std::uint16_t>(address),physical)) {
            if (skipUnmapped) continue;
            statusMessage_ = "Fill range includes an unmapped, peripheral or invalid address; memory unchanged";
            return false;
        }
        writes.emplace(physical.device,physical.offset);
    }
    for (const auto& [id,offset] : writes) (void)e51p_memory_program(peripherals_->memory(id),offset,&value,1);
    invalidateInstructionCache();
    return true;
}
void Emu51ImguiApp::drawPhysicalMemory()
{
    if (!peripherals_ || peripherals_->memories().empty()) return;
    Emu51PhysicalAddress pc;
    if (peripherals_->resolve(true,cpu_.PC,pc)) ImGui::Text("PC -> %s + %llXh",pc.device.c_str(),static_cast<unsigned long long>(pc.offset));
    if (!ImGui::CollapsingHeader("Physical chip memory")) return;
    if (!peripherals_->memory(physicalMemoryDevice_)) physicalMemoryDevice_ = peripherals_->memories().front().id;
    if (ImGui::BeginCombo("Chip",physicalMemoryDevice_.c_str())) {
        for (const auto& m : peripherals_->memories()) if (ImGui::Selectable(m.id.c_str(),m.id == physicalMemoryDevice_)) {
            physicalMemoryDevice_ = m.id; physicalMemoryOffset_ = 0;
        }
        ImGui::EndCombo();
    }
    auto* memory = peripherals_->memory(physicalMemoryDevice_);
    const auto capacity = e51p_memory_capacity(memory);
    ImGui::Text("%s, %llu physical bytes",e51p_memory_kind(memory) == E51P_ROM ? "ROM" : "RAM",static_cast<unsigned long long>(capacity));
    ImGui::InputScalar("Offset (hex)",ImGuiDataType_U64,&physicalMemoryOffset_,nullptr,nullptr,"%llX",ImGuiInputTextFlags_CharsHexadecimal);
    if (physicalMemoryOffset_ >= capacity) { ImGui::TextDisabled("Offset is outside this chip."); return; }
    const auto bytes = peripherals_->snapshotMemory(physicalMemoryDevice_,physicalMemoryOffset_,std::min<std::uint64_t>(256,capacity-physicalMemoryOffset_));
    ImGui::BeginChild("physical-bytes",{0,180},ImGuiChildFlags_Borders);
    for (std::size_t row = 0; row < bytes.size(); row += 16) {
        ImGui::Text("%08llX:",static_cast<unsigned long long>(physicalMemoryOffset_+row));
        for (std::size_t col = 0; col < 16 && row+col < bytes.size(); ++col) { ImGui::SameLine(); ImGui::Text("%02X",bytes[row+col]); }
    }
    ImGui::EndChild();
    ImGui::InputScalar("Programming length",ImGuiDataType_U64,&physicalMemoryLength_);
    ImGui::InputInt("Fill byte (hex)",&physicalMemoryFill_,0,0,ImGuiInputTextFlags_CharsHexadecimal);
    const bool valid = physicalMemoryLength_ && physicalMemoryLength_ <= capacity-physicalMemoryOffset_ && physicalMemoryFill_ >= 0 && physicalMemoryFill_ <= 255;
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Program physical range")) {
        (void)e51p_memory_fill(memory,physicalMemoryOffset_,physicalMemoryLength_,static_cast<std::uint8_t>(physicalMemoryFill_));
        invalidateInstructionCache();
        statusMessage_ = "Physical range programmed; selectors unchanged";
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("This is explicit chip programming, including ROM. It does not change a selected bank. Binary Load/Save is in the chip inspector.");
}
