// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"
#include "emu51_imgui_app_internal.h"
#include "emu51_imgui_input.h"
#include "TextEditor.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>

using emu51_imgui_internal::normalizedProgramPath;

namespace {
std::string debugName(const char* text)
{
    std::string name = text ? text : "";
    const auto first = name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    name = name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return name;
}

const char* symbolKindName(emu51_asm::SymbolKind kind)
{
    switch (kind) {
    case emu51_asm::SymbolKind::label: return "Label / program byte";
    case emu51_asm::SymbolKind::equ: return "Constant";
    case emu51_asm::SymbolKind::data: return "Internal RAM/SFR";
    case emu51_asm::SymbolKind::bit: return "Bit";
    }
    return "";
}
}

void Emu51ImguiApp::showDebugger() { debuggerOpen_ = true; }

std::optional<std::uint16_t> Emu51ImguiApp::resolveDebugAddress(const char* text) const
{
    const auto name = debugName(text);
    if (const auto symbol = loadedSymbols_.find(name); symbol != loadedSymbols_.end())
        return symbol->second.value;
    if (const auto address = emu51ParseHexInput(name.c_str(), 0xffff))
        return static_cast<std::uint16_t>(*address);
    return std::nullopt;
}

std::string Emu51ImguiApp::labelAtAddress(std::uint16_t address) const
{
    const auto label = loadedLabels_.find(address);
    return label == loadedLabels_.end() ? std::string{} : label->second;
}

std::optional<unsigned> Emu51ImguiApp::symbolValue(const std::string& name) const
{
    const auto found = loadedSymbols_.find(name);
    if (found == loadedSymbols_.end())
        return std::nullopt;
    const auto& symbol = found->second;
    switch (symbol.kind) {
    case emu51_asm::SymbolKind::label: return readProgramByte(symbol.value);
    case emu51_asm::SymbolKind::equ: return symbol.value;
    case emu51_asm::SymbolKind::data:
        if (symbol.value <= 0xff) return cpu_.readDirect(static_cast<std::uint8_t>(symbol.value));
        break;
    case emu51_asm::SymbolKind::bit:
        if (symbol.value <= 0xff) {
            const unsigned byte = symbol.value < 0x80 ? 0x20 + symbol.value / 8 : symbol.value & 0xf8;
            return (cpu_.readDirect(static_cast<std::uint8_t>(byte)) >> (symbol.value & 7)) & 1u;
        }
        break;
    }
    return std::nullopt;
}

void Emu51ImguiApp::clearLoadedDebugInfo()
{
    sourceLines_.clear();
    loadedSymbols_.clear();
    loadedLabels_.clear();
    loadedSources_.clear();
    sourceAddresses_.clear();
    sourceLineBytes_.clear();
    editorMatchesLoadedSource_ = false;
}

void Emu51ImguiApp::invalidateSourceLocation(std::uint16_t address)
{
    const auto found = sourceLines_.find(address);
    if (found == sourceLines_.end())
        return;
    // An operand edit invalidates its instruction's source entry too. All bytes
    // from one source line lose the mapping together, including wrapped output.
    const auto source = found->second;
    const SourceKey key{source.file, source.line};
    sourceAddresses_.erase(key);
    const auto bytes = sourceLineBytes_.find(key);
    if (bytes != sourceLineBytes_.end()) {
        for (const auto byte : bytes->second) sourceLines_.erase(byte);
        sourceLineBytes_.erase(bytes);
    }
}

std::string Emu51ImguiApp::editorSourceKey() const
{
    return editorDebugSourceKey_;
}

void Emu51ImguiApp::updateEditorSourceMatch()
{
    const std::string name = assemblyEditorState_.sourcePath.empty()
        ? (assemblyEditorBaseDirectory() / assemblyEditorState_.sourceName).string() : assemblyEditorState_.sourcePath;
    if (name != editorDebugSourceName_) {
        editorDebugSourceName_ = name;
        editorDebugSourceKey_ = normalizedProgramPath(name);
    }
    const auto source = loadedSources_.find(editorSourceKey());
    editorMatchesLoadedSource_ = source != loadedSources_.end() &&
        source->second == observedAssemblyEditorText_;
}

std::optional<std::uint16_t> Emu51ImguiApp::editorLineAddress(std::size_t line) const
{
    if (!editorMatchesLoadedSource_)
        return std::nullopt;
    const auto found = sourceAddresses_.find({editorSourceKey(), line});
    return found == sourceAddresses_.end() ? std::nullopt : std::optional(found->second);
}

void Emu51ImguiApp::toggleEditorBreakpoint(std::size_t line)
{
    trackAssemblyEditorChanges();
    if (const auto address = editorLineAddress(line))
        toggleBreakpoint(*address);
    else
        statusMessage_ = "no matching loaded instruction; compile and load the current source";
}

void Emu51ImguiApp::configureEditorDebugger()
{
    assemblyEditor_->SetLineDecorator(-2.0f, [this](TextEditor::Decorator& decoration) {
        auto guard = access(false);
        const auto address = editorLineAddress(static_cast<std::size_t>(decoration.line + 1));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(!address);
        const bool clicked = ImGui::InvisibleButton("breakpoint", ImVec2(decoration.width, decoration.height));
        ImGui::EndDisabled();
        if (address) {
            const bool enabled = breakpoints_.contains(*address);
            const ImVec2 center(origin.x + decoration.width * 0.5f, origin.y + decoration.height * 0.5f);
            const float radius = std::min(decoration.width, decoration.height) * 0.3f;
            if (enabled)
                ImGui::GetWindowDrawList()->AddCircleFilled(center, radius, IM_COL32(230, 75, 75, 255));
            else if (ImGui::IsItemHovered())
                ImGui::GetWindowDrawList()->AddCircle(center, radius, IM_COL32(230, 75, 75, 255));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s breakpoint at %04X", enabled ? "Remove" : "Add", *address);
            if (clicked)
                toggleEditorBreakpoint(static_cast<std::size_t>(decoration.line + 1));
        }
    });
    assemblyEditor_->SetLineNumberContextMenuCallback([this](int line) {
        auto guard = access();
        trackAssemblyEditorChanges();
        const auto address = editorLineAddress(static_cast<std::size_t>(line + 1));
        if (ImGui::MenuItem("Breakpoint", nullptr, address && breakpoints_.contains(*address), address.has_value()))
            toggleEditorBreakpoint(static_cast<std::size_t>(line + 1));
        if (ImGui::MenuItem("Run to line", nullptr, false, address.has_value()))
            runToAddress(*address);
    });
}

void Emu51ImguiApp::toggleBreakpoint(std::uint16_t address)
{
    if (!breakpoints_.erase(address))
        breakpoints_.insert(address);
}

void Emu51ImguiApp::stopForDebugger(const std::string& reason)
{
    running_ = false;
    std::string audioError;
    if (peripherals_ && !peripherals_->syncAudio(false, audioError))
        peripheralMessage_ = audioError + "; reopen audio devices to retry";
    stepTask_.reset();
    runToAddress_.reset();
    skipBreakpoint_.reset();
    statusMessage_ = audioError.empty() ? reason : reason + "; " + peripheralMessage_;
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
        const auto label = labelAtAddress(cpu_.PC);
        stopForDebugger(label.empty() ? std::string(reason) : std::string(reason) + " (" + label + ")");
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
    clearLoadedDebugInfo();
    sourceLines_ = image.sourceLines;
    std::map<std::string, std::string> normalizedPaths;
    const auto sourcePath = [&](const std::string& file) -> const std::string& {
        auto [found, inserted] = normalizedPaths.try_emplace(file);
        if (inserted) found->second = normalizedProgramPath(file);
        return found->second;
    };
    for (auto& [address, source] : sourceLines_) {
        source.file = sourcePath(source.file);
        sourceLineBytes_[{source.file, source.line}].push_back(address);
    }
    loadedSymbols_ = image.symbols;
    for (const auto& [name, symbol] : loadedSymbols_) {
        if (symbol.kind == emu51_asm::SymbolKind::label) {
            auto& labels = loadedLabels_[symbol.value];
            if (!labels.empty()) labels += ", ";
            labels += name;
        }
    }
    TextEditor normalized;
    normalized.SetTabSize(assemblyEditor_->GetTabSize());
    normalized.SetInsertSpacesOnTabs(assemblyEditor_->IsInsertSpacesOnTabs());
    for (const auto& [file, text] : image.sources) {
        normalized.SetText(text);
        loadedSources_[sourcePath(file)] = normalized.GetText();
    }
    for (const auto address : image.instructionAddresses) {
        const auto source = sourceLines_.find(address);
        if (source != sourceLines_.end())
            sourceAddresses_.try_emplace(SourceKey{source->second.file, source->second.line}, address);
    }
    trackAssemblyEditorChanges();
}

void Emu51ImguiApp::runToEditorCursor()
{
    trackAssemblyEditorChanges();
    if (!assemblyEditor_) {
        return;
    }
    int line = 0, column = 0;
    assemblyEditor_->GetMainCursor(line, column);
    if (const auto address = editorLineAddress(static_cast<std::size_t>(line + 1)))
        runToAddress(*address);
    else
        statusMessage_ = "no matching loaded instruction; compile and load the current source";
}

void Emu51ImguiApp::showSource(std::uint16_t address)
{
    const auto found = sourceLines_.find(address);
    if (found == sourceLines_.end())
        return;
    // Copy before opening another editor buffer; loading does not change CPU.
    const auto source = found->second;
    trackAssemblyEditorChanges();
    if (source.file != editorSourceKey()) {
        if (!confirmAssemblyEditorChanges() || !loadAssemblyFileIntoEditor(source.file))
            return;
    }
    trackAssemblyEditorChanges();
    if (!editorMatchesLoadedSource_) {
        statusMessage_ = "source differs from the loaded program; compile and load before source navigation";
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
    trackAssemblyEditorChanges();
    ImGui::SetNextItemWidth(220);
    ImGui::InputText("Address or symbol", debugAddress_, sizeof(debugAddress_));
    const auto address = resolveDebugAddress(debugAddress_);
    if (address) {
        ImGui::SameLine();
        ImGui::TextDisabled("= %04X", *address);
    } else {
        ImGui::TextDisabled("Enter a loaded symbol or hexadecimal address.");
    }
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
            ImGui::Text("%04X %s", breakpoint, labelAtAddress(breakpoint).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) remove = breakpoint;
            ImGui::PopID();
        }
        if (remove) breakpoints_.erase(*remove);
    }
    if (ImGui::CollapsingHeader("Symbols")) {
        ImGui::InputText("Filter", symbolFilter_, sizeof(symbolFilter_));
        const auto filter = debugName(symbolFilter_);
        ImGui::TextDisabled("Select an address to use it above. Watches inspect values without stopping execution.");
        if (ImGui::BeginTable("symbols", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg,
            ImVec2(0, 160))) {
            ImGui::TableSetupColumn("Name"); ImGui::TableSetupColumn("Address / constant");
            ImGui::TableSetupColumn("Kind"); ImGui::TableSetupColumn("Watch");
            ImGui::TableHeadersRow();
            for (const auto& [name, symbol] : loadedSymbols_) {
                if (!filter.empty() && name.find(filter) == std::string::npos) continue;
                ImGui::PushID(name.c_str());
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(name.c_str());
                ImGui::TableNextColumn();
                char value[8];
                std::snprintf(value, sizeof(value), "%04X", symbol.value);
                if (ImGui::Selectable(value))
                    std::snprintf(debugAddress_, sizeof(debugAddress_), "%s", name.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(symbolKindName(symbol.kind));
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(symbolWatches_.contains(name));
                if (ImGui::SmallButton("Watch")) symbolWatches_.insert(name);
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (loadedSymbols_.empty()) ImGui::TextDisabled("Compile and load assembly to populate symbols.");
    }
    if (ImGui::CollapsingHeader("Watches", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto name = debugName(debugAddress_);
        ImGui::BeginDisabled(!loadedSymbols_.contains(name) || symbolWatches_.contains(name));
        if (ImGui::Button("Watch symbol")) symbolWatches_.insert(name);
        ImGui::EndDisabled();
        std::optional<std::string> remove;
        for (const auto& watch : symbolWatches_) {
            ImGui::PushID(watch.c_str());
            const auto value = symbolValue(watch);
            const auto symbol = loadedSymbols_.find(watch);
            if (value)
                ImGui::Text("%s = %04X (%u) [%s]", watch.c_str(), *value, *value,
                    symbolKindName(symbol->second.kind));
            else
                ImGui::TextDisabled("%s: unavailable in the loaded program", watch.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) remove = watch;
            ImGui::PopID();
        }
        if (remove) symbolWatches_.erase(*remove);
    }
    if (ImGui::CollapsingHeader("Watchpoints", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Combo("Space", &watchSpace_, "Direct RAM/SFR changes\0External data\0Program memory\0Internal RAM changes\0");
        const bool internalSpace = watchSpace_ == 0 || watchSpace_ == 3;
        ImGui::BeginDisabled(internalSpace);
        ImGui::Checkbox("Reads", &watchRead_);
        ImGui::SameLine();
        ImGui::Checkbox("Writes", &watchWrite_);
        ImGui::EndDisabled();
        const auto symbol = loadedSymbols_.find(debugName(debugAddress_));
        const bool bitSymbol = symbol != loadedSymbols_.end() && symbol->second.kind == emu51_asm::SymbolKind::bit;
        if (bitSymbol) ImGui::TextDisabled("Use Watches for a bit value, or enter its byte address for a watchpoint.");
        ImGui::BeginDisabled(!address || bitSymbol || (internalSpace && *address > (watchSpace_ == 3 ? cpu_.modelInfo().internalRamBytes - 1 : 255)) ||
            (!internalSpace && !watchRead_ && !watchWrite_));
        if (ImGui::Button("Add watchpoint"))
            watchpoints_.push_back({static_cast<WatchSpace>(watchSpace_), static_cast<std::uint16_t>(*address),
                !internalSpace && watchRead_, internalSpace || watchWrite_, true});
        ImGui::EndDisabled();
        for (std::size_t i = 0; i < watchpoints_.size();) {
            auto& watch = watchpoints_[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::Checkbox("##enabled", &watch.enabled);
            ImGui::SameLine();
            ImGui::Text("%s %04X %s%s", watch.space == WatchSpace::Internal ? "Internal changes" :
                (watch.space == WatchSpace::Iram ? "RAM changes" : (watch.space == WatchSpace::Data ? "Data" : "Program")), watch.address,
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
