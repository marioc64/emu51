// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"
#include "emu51_imgui_input.h"
#include "TextEditor.h"

#include <algorithm>
#include <cstdio>
#include <limits>

void Emu51ImguiApp::showDebugger() { debuggerOpen_ = true; }

void Emu51ImguiApp::toggleBreakpoint(std::uint16_t address)
{
    if (!breakpoints_.erase(address))
        breakpoints_.insert(address);
}

void Emu51ImguiApp::stopForDebugger(const std::string& reason)
{
    running_ = false;
    stepTask_.reset();
    runToAddress_.reset();
    skipBreakpoint_.reset();
    statusMessage_ = reason;
    cycleTiming_.reset();
}

void Emu51ImguiApp::runToAddress(std::uint16_t address)
{
    stepTask_.reset();
    runToAddress_ = address;
    skipBreakpoint_ = cpu_.PC;
    running_ = true;
    cycleTiming_.reset();
    statusMessage_ = "running to cursor";
}

bool Emu51ImguiApp::stopBeforeInstruction()
{
    if (runToAddress_ && cpu_.PC == *runToAddress_) {
        stopForDebugger("reached cursor");
        return true;
    }
    const bool skip = skipBreakpoint_ && *skipBreakpoint_ == cpu_.PC;
    skipBreakpoint_.reset();
    if (!skip && breakpoints_.contains(cpu_.PC)) {
        char reason[48];
        std::snprintf(reason, sizeof(reason), "breakpoint at %04X", cpu_.PC);
        stopForDebugger(reason);
        return true;
    }
    return false;
}

void Emu51ImguiApp::noteMemoryAccess(WatchSpace space, std::uint16_t address, bool write)
{
    if (!instructionExecuting_ || !pendingWatchpointStop_.empty())
        return;
    for (const auto& watch : watchpoints_) {
        if (!watch.enabled || watch.space != space || watch.address != address ||
            !(write ? watch.write : watch.read))
            continue;
        char reason[96];
        std::snprintf(reason, sizeof(reason), "%s %s watchpoint at %04X",
            space == WatchSpace::Program ? "program" : "data", write ? "write" : "read", address);
        pendingWatchpointStop_ = reason;
        return;
    }
}

void Emu51ImguiApp::setLoadedSourceMap(const emu51_asm::AssemblyImage& image)
{
    // Replacing an image invalidates old source locations, including overlay
    // loads whose unchanged bytes may belong to a different source version.
    if (stepTask_)
        stopForDebugger("step canceled by program load");
    sourceLines_ = image.sourceLines;
}

void Emu51ImguiApp::runToEditorCursor()
{
    trackAssemblyEditorChanges();
    if (!assemblyEditor_ || assemblyEditorState_.dirty) {
        statusMessage_ = "save, compile and load the current source before running to its cursor";
        return;
    }
    int line = 0, column = 0;
    assemblyEditor_->GetMainCursor(line, column);
    const auto& path = assemblyEditorState_.sourcePath;
    for (const auto& [address, source] : sourceLines_) {
        if ((source.file == path || source.file == assemblyEditorState_.sourceName) &&
            source.line == static_cast<std::size_t>(line + 1)) {
            runToAddress(address);
            return;
        }
    }
    statusMessage_ = "no loaded instruction at the editor cursor";
}

void Emu51ImguiApp::showSource(std::uint16_t address)
{
    const auto found = sourceLines_.find(address);
    if (found == sourceLines_.end())
        return;
    // Copy before opening another editor buffer; loading does not change CPU.
    const auto source = found->second;
    if (source.file != assemblyEditorState_.sourcePath && source.file != assemblyEditorState_.sourceName) {
        if (!confirmAssemblyEditorChanges() || !loadAssemblyFileIntoEditor(source.file))
            return;
    }
    if (source.line == 0 || source.line > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return;
    assemblyEditorState_.open = true;
    assemblyEditor_->SetCursor(static_cast<int>(source.line - 1), 0);
    assemblyEditor_->SetFocus();
}

void Emu51ImguiApp::drawDebugger()
{
    if (!debuggerOpen_)
        return;
    ImGui::SetNextWindowSize(ImVec2(620, 510), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Debugger", &debuggerOpen_)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("%s", statusMessage_.c_str());
    ImGui::SetNextItemWidth(85);
    ImGui::InputText("Address", debugAddress_, sizeof(debugAddress_), ImGuiInputTextFlags_CharsHexadecimal);
    const auto address = emu51ParseHexInput(debugAddress_, 0xffff);
    ImGui::BeginDisabled(!address);
    if (ImGui::Button("Toggle breakpoint"))
        toggleBreakpoint(static_cast<std::uint16_t>(*address));
    ImGui::SameLine();
    if (ImGui::Button("Run to address"))
        runToAddress(static_cast<std::uint16_t>(*address));
    ImGui::EndDisabled();
    if (ImGui::Button("Run to editor cursor"))
        runToEditorCursor();
    ImGui::SameLine();
    if (ImGui::Button("Show PC source"))
        showSource(cpu_.PC);
    ImGui::SameLine();
    if (ImGui::Button("Pause"))
        stopForDebugger("paused");

    if (ImGui::CollapsingHeader("Breakpoints", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::optional<std::uint16_t> remove;
        for (const auto breakpoint : breakpoints_) {
            ImGui::PushID(breakpoint);
            ImGui::Text("%04X", breakpoint);
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) remove = breakpoint;
            ImGui::PopID();
        }
        if (remove) breakpoints_.erase(*remove);
    }
    if (ImGui::CollapsingHeader("Watchpoints", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Combo("Space", &watchSpace_, "Internal RAM/SFR changes\0External data\0Program memory\0");
        ImGui::BeginDisabled(watchSpace_ == 0);
        ImGui::Checkbox("Reads", &watchRead_);
        ImGui::SameLine();
        ImGui::Checkbox("Writes", &watchWrite_);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!address || (watchSpace_ == 0 && *address > 255) ||
            (watchSpace_ != 0 && !watchRead_ && !watchWrite_));
        if (ImGui::Button("Add watchpoint"))
            watchpoints_.push_back({static_cast<WatchSpace>(watchSpace_), static_cast<std::uint16_t>(*address),
                watchSpace_ != 0 && watchRead_, watchSpace_ == 0 || watchWrite_, true});
        ImGui::EndDisabled();
        for (std::size_t i = 0; i < watchpoints_.size();) {
            auto& watch = watchpoints_[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::Checkbox("##enabled", &watch.enabled);
            ImGui::SameLine();
            ImGui::Text("%s %04X %s%s", watch.space == WatchSpace::Internal ? "Internal changes" :
                (watch.space == WatchSpace::Data ? "Data" : "Program"), watch.address,
                watch.read ? "R" : "", watch.write ? "W" : "");
            ImGui::SameLine();
            const bool remove = ImGui::SmallButton("Remove");
            ImGui::PopID();
            if (remove) watchpoints_.erase(watchpoints_.begin() + static_cast<std::ptrdiff_t>(i));
            else ++i;
        }
    }
    ImGui::Separator();
    ImGui::Checkbox("Record instruction trace", &traceEnabled_);
    ImGui::SameLine();
    if (ImGui::Button("Clear trace")) trace_.clear();
    ImGui::TextDisabled("Last %zu instructions (maximum %zu). Source locations refer to the last assembly.", trace_.size(), kTraceCapacity);
    if (ImGui::BeginTable("trace", 5, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg,
        ImVec2(0, std::max(100.0f, ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupColumn("PC"); ImGui::TableSetupColumn("Instruction");
        ImGui::TableSetupColumn("Next PC"); ImGui::TableSetupColumn("Cycles");
        ImGui::TableSetupColumn("A / PSW / SP"); ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(trace_.size()));
        while (clipper.Step()) for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const auto& entry = trace_[static_cast<std::size_t>(row)];
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::Text("%04X", entry.pc); ImGui::TableNextColumn();
            ImGui::Text("%02X %02X %02X", entry.bytes[0], entry.bytes[1], entry.bytes[2]);
            ImGui::TableNextColumn(); ImGui::Text("%04X", entry.nextPc);
            ImGui::TableNextColumn(); ImGui::Text("%u", entry.cycles);
            ImGui::TableNextColumn(); ImGui::Text("%02X / %02X / %02X", entry.acc, entry.psw, entry.sp);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
