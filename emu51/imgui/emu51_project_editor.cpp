// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"
#include "emu51_imgui_asm_editor.h"
#include "emu51_imgui_paths.h"
#include "emu51_editor_files.h"
#include "TextEditor.h"
#include "portable-file-dialogs.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>

void Emu51ImguiApp::syncProjectUnit()
{
    if (activeProjectUnit_.empty()) return;
    trackAssemblyEditorChanges();
    const auto unit = std::find_if(projectUnits_.begin(),projectUnits_.end(),[&](const auto& u) { return u.id == activeProjectUnit_; });
    if (unit == projectUnits_.end()) return;
    if (!assemblyEditorState_.sourcePath.empty()) unit->path = assemblyEditorState_.sourcePath;
    auto& e = unit->editor;
    e.present = true; e.open = assemblyEditorState_.open; e.draft = unit->embedded || assemblyEditorState_.dirty;
    e.path = assemblyEditorState_.sourcePath; e.name = assemblyEditorState_.sourceName;
    e.baseDirectory = assemblyEditorBaseDirectory().string();
    e.text = e.draft ? observedAssemblyEditorText_ : std::string{};
    assemblyEditor_->GetMainCursor(e.line,e.column);
}
bool Emu51ImguiApp::switchProjectUnit(const std::string& id)
{
    if (activeProjectUnit_ == id) return true;
    auto next = unitEditors_.find(id);
    if (next == unitEditors_.end()) {
        const auto unit = std::find_if(projectUnits_.begin(),projectUnits_.end(),[&](const auto& u) { return u.id == id; });
        if (unit == projectUnits_.end()) return false;
        std::string text = unit->editor.text;
        if (!unit->embedded && !unit->editor.draft) {
            const auto path = unit->path;
            const bool read = withoutExecutionLock([&] {
                std::ifstream input(path,std::ios::binary|std::ios::ate);
                if (!input || input.tellg() < 0 || input.tellg() > 1024*1024) return false;
                text.resize(static_cast<std::size_t>(input.tellg())); input.seekg(0);
                if (!text.empty() && !input.read(text.data(),static_cast<std::streamsize>(text.size()))) return false;
                return input.peek() == std::char_traits<char>::eof() && !input.bad();
            });
            if (!read) { statusMessage_ = "Cannot open project source: "+path; return false; }
        }
        UnitEditor buffer;
        buffer.editor = std::make_unique<TextEditor>(); emu51ConfigureAsmEditor(*buffer.editor);
        buffer.editor->SetText(text); buffer.editor->SetCursor(unit->editor.line,unit->editor.column);
        buffer.observed = buffer.editor->GetText();
        buffer.state.sourcePath = unit->editor.path;
        if (buffer.state.sourcePath.empty() && !unit->embedded) buffer.state.sourcePath = unit->path;
        buffer.state.sourceName = unit->editor.name.empty() ? std::filesystem::path(unit->path).filename().string() : unit->editor.name;
        buffer.state.dirty = unit->editor.draft; buffer.state.open = true;
        if (const auto build = lastUnitBuilds_.find(id); build != lastUnitBuilds_.end()) {
            buffer.diagnostics = build->second.diagnostics;
            if (buffer.diagnostics.empty()) buffer.state.compiledImage = build->second.image;
        }
        buffer.baseDirectory = unit->baseDirectory.empty() ? std::filesystem::path(unit->path).parent_path().string() : unit->baseDirectory;
        if (savedProjectUnits_.contains(id)) buffer.savedInProject = buffer.observed;
        if (persistState_) buffer.recovery = Emu51EditorRecovery::create(emu51GetEditorRecoveryDirectory());
        next = unitEditors_.emplace(id,std::move(buffer)).first;
    }
    syncProjectUnit();
    saveEditorRecovery();
    const auto swap = [&](UnitEditor& buffer) {
        std::swap(assemblyEditor_,buffer.editor); std::swap(assemblyEditorState_,buffer.state);
        std::swap(editorDiagnostics_,buffer.diagnostics); std::swap(observedAssemblyEditorText_,buffer.observed);
        std::swap(untitledSourceBaseDirectory_,buffer.baseDirectory); std::swap(projectSavedEditorText_,buffer.savedInProject);
        std::swap(editorRecovery_,buffer.recovery);
    };
    swap(unitEditors_[activeProjectUnit_]);
    swap(next->second);
    activeProjectUnit_ = id;
    recoverySavePending_ = false;
    editorDebugSourceName_.clear(); editorDebugSourceKey_.clear();
    configureEditorDebugger();
    assemblyEditor_->SetTransactionCallback([this](std::vector<TextEditor::Change>&) { auto guard = access(); markAssemblyEditorChanged(); });
    updateEditorSourceMatch();
    return true;
}
bool Emu51ImguiApp::confirmProjectEditors()
{
    if (projectUnits_.empty()) return confirmAssemblyEditorChanges();
    const auto original = activeProjectUnit_;
    std::vector<std::string> ids;
    for (const auto& u : projectUnits_) ids.push_back(u.id);
    for (const auto& id : ids) {
        if (!switchProjectUnit(id) || !confirmAssemblyEditorChanges()) { (void)switchProjectUnit(original); return false; }
    }
    (void)switchProjectUnit(original);
    return true;
}
Emu51Project Emu51ImguiApp::captureBuildProject()
{
    syncProjectUnit();
    Emu51Project p; p.formatVersion = 2;
    if (peripherals_) p.peripherals = peripherals_->config();
    p.units = projectUnits_; p.mappings = projectMappings_; p.activeUnit = activeProjectUnit_;
    const auto base = projectPath_.empty() ? std::filesystem::current_path() : std::filesystem::path(projectPath_).parent_path();
    for (auto& unit : p.units) for (auto& path : unit.includeDirectories)
        if (!std::filesystem::path(path).is_absolute()) path = (base/path).lexically_normal().string();
    p.execution = {cycleExact_,crystalMHz_,stepsPerFrame_}; p.unified = unifiedMode_; p.cpuModel = cpu_.model();
    return p;
}
bool Emu51ImguiApp::buildProjectUnits(bool load, bool powerCycle)
{
    auto project = captureBuildProject();
    auto build = withoutExecutionLock([&] { return emu51BuildProject(project); });
    for (const auto& unit : build.units) {
        lastUnitBuilds_.insert_or_assign(unit.id,unit);
        if (unit.id == activeProjectUnit_) {
            editorDiagnostics_ = unit.diagnostics;
            if (unit.diagnostics.empty()) assemblyEditorState_.compiledImage = unit.image;
        } else if (auto it = unitEditors_.find(unit.id); it != unitEditors_.end()) {
            it->second.diagnostics = unit.diagnostics;
            if (unit.diagnostics.empty()) it->second.state.compiledImage = unit.image;
        }
    }
    if (!build.success()) { statusMessage_ = build.error; setAssemblyEditorBuildStatus(false,"Project build failed",build.error); return false; }
    if (load) {
        try {
            std::unique_ptr<Emu51PeripheralSession> candidate;
            auto* target = peripherals_.get();
            if (powerCycle) {
                candidate = withoutExecutionLock([&] { return Emu51PeripheralSession::create(project.peripherals,crystalMHz_); });
                target = candidate.get();
            }
            std::string error;
            if (!target || !emu51ProgramProject(build,*target,error)) throw std::runtime_error(error.empty() ? "Apply a circuit memory device first" : error);
            if (powerCycle) { stopForDebugger("power cycle"); installPeripheralSession(std::move(candidate)); resetCpuState(true); }
            installProjectSources(build);
            invalidateInstructionCache();
        } catch (const std::exception& e) { statusMessage_ = e.what(); return false; }
    }
    statusMessage_ = load ? (powerCycle ? "Power cycle completed from initialization and program recipes" : "Project programmed; selectors unchanged") : "All project build units compiled and validated";
    setAssemblyEditorBuildStatus(true,load ? "Project loaded" : "Project built",statusMessage_);
    return true;
}
bool Emu51ImguiApp::addProjectUnit(const std::filesystem::path& path, bool embedded)
{
    if (projectUnits_.size() >= 32) { statusMessage_ = "Projects support 32 build units"; return false; }
    for (const auto& u : projectUnits_) if (!embedded && std::filesystem::path(u.path) == path) return switchProjectUnit(u.id);
    unsigned suffix = 1; std::string id;
    do { id = "source"+std::to_string(suffix++); } while (std::any_of(projectUnits_.begin(),projectUnits_.end(),[&](const auto& u) { return u.id == id; }));
    Emu51AssemblyUnit unit; unit.id = id; unit.path = std::filesystem::weakly_canonical(std::filesystem::absolute(path)).string();
    unit.embedded = embedded; unit.baseDirectory = std::filesystem::path(unit.path).parent_path().string();
    unit.editor = {true,true,embedded,embedded ? "" : unit.path,path.filename().string(),unit.baseDirectory,embedded ? "; New project source\norg 0\nend\n" : "",0,0};
    projectUnits_.push_back(std::move(unit));
    if (!switchProjectUnit(id)) { projectUnits_.pop_back(); return false; }
    projectTreeOpen_ = true;
    return true;
}
bool Emu51ImguiApp::initializeCircuitProject()
{
    if (peripherals_ && peripherals_->circuitBus()) return true;
    Emu51Project project;
    if (peripherals_) project.peripherals = peripherals_->config();
    project.unified = unifiedMode_; project.execution = {cycleExact_,crystalMHz_,stepsPerFrame_};
    std::string error;
    if (!emu51MigrateProject(project,error)) { statusMessage_ = error; return false; }
    try {
        auto candidate = withoutExecutionLock([&] { return Emu51PeripheralSession::create(project.peripherals,crystalMHz_); });
        for (const auto& m : candidate->memories()) {
            const auto& bytes = unifiedMode_ ? unifiedMemory_ : (m.id.starts_with("system_code") ? romMemory_ : ramMemory_);
            if (!candidate->programMemory(m.id,0,std::vector<std::uint8_t>(bytes.begin(),bytes.end()))) throw std::runtime_error("Cannot migrate backing memory");
        }
        stopForDebugger("memory circuit created"); installPeripheralSession(std::move(candidate));
        peripheralDraft_ = peripherals_->config(); resetCpuState(true);
        return true;
    } catch (const std::exception& e) { statusMessage_ = e.what(); return false; }
}
bool Emu51ImguiApp::removeProjectUnit(std::string id)
{
    if (!switchProjectUnit(id)) return false;
    const auto saved = projectSavedEditorText_; projectSavedEditorText_.reset();
    if (!confirmAssemblyEditorChanges()) { projectSavedEditorText_ = saved; return false; }
    std::string next;
    for (const auto& unit : projectUnits_) if (unit.id != id) { next = unit.id; break; }
    if (!next.empty()) {
        if (!switchProjectUnit(next)) { projectSavedEditorText_ = saved; return false; }
        if (auto old = unitEditors_.find(id); old != unitEditors_.end() && old->second.recovery)
            withoutExecutionLock([&] { old->second.recovery->clear(); });
    } else {
        clearEditorRecovery();
        activeProjectUnit_.clear();
        assemblyEditor_ = std::make_unique<TextEditor>(); emu51ConfigureAsmEditor(*assemblyEditor_);
        assemblyEditorState_ = {}; observedAssemblyEditorText_ = assemblyEditor_->GetText();
        editorDiagnostics_.clear(); projectSavedEditorText_.reset();
        assemblyEditor_->SetTransactionCallback([this](std::vector<TextEditor::Change>&) { auto guard = access(); markAssemblyEditorChanged(); });
        configureEditorDebugger();
    }
    unitEditors_.erase(id); savedProjectUnits_.erase(id); lastUnitBuilds_.erase(id);
    std::erase_if(projectUnits_,[&](const auto& unit) { return unit.id == id; });
    std::erase_if(projectMappings_,[&](const auto& row) { return row.unit == id; });
    statusMessage_ = "Source removed from project; file preserved";
    return true;
}
void Emu51ImguiApp::drawProjectTree()
{
    if (ImGui::Button(projectTreeOpen_ ? "Hide project files" : "Project files")) projectTreeOpen_ = !projectTreeOpen_;
    if (!projectTreeOpen_) return;
    if ((!peripherals_ || !peripherals_->circuitBus()) && ImGui::Button("Create memory circuit from current machine")) initializeCircuitProject();
    if (ImGui::Button("Add source...")) {
        const auto paths = withoutExecutionLock([] { return pfd::open_file("Add project source",{}, {"Assembly","*.asm","All files","*"}).result(); });
        if (!paths.empty()) addProjectUnit(paths.front());
    }
    ImGui::SameLine();
    if (ImGui::Button("New source")) addProjectUnit((projectPath_.empty() ? std::filesystem::current_path() : std::filesystem::path(projectPath_).parent_path()) / ("untitled"+std::to_string(projectUnits_.size()+1)+".asm"),true);
    if (ImGui::BeginChild("project-files",{0,100},ImGuiChildFlags_Borders)) {
        for (const bool includes : {false,true}) if (ImGui::TreeNodeEx(includes ? "Include files" : "Build units",ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto& unit : projectUnits_) if (unit.includeOnly == includes) {
                const auto label = unit.id+"  "+std::filesystem::path(unit.path).filename().string();
                if (ImGui::Selectable(label.c_str(),unit.id == activeProjectUnit_)) switchProjectUnit(unit.id);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",unit.path.c_str());
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
    if (ImGui::Button("Build project")) buildProjectUnits(false);
    ImGui::SameLine(); if (ImGui::Button("Build and load")) buildProjectUnits(true);
    ImGui::SameLine(); if (ImGui::Button("Power cycle")) buildProjectUnits(true,true);
    if (!projectUnits_.empty()) {
        const auto current = std::find_if(projectUnits_.begin(),projectUnits_.end(),[&](const auto& u) { return u.id == activeProjectUnit_; });
        if (current != projectUnits_.end()) {
            ImGui::Checkbox("Include file (not a build unit)",&current->includeOnly);
            ImGui::Checkbox("Build only (no programming)",&current->buildOnly);
            if (ImGui::TreeNode("Include directories")) {
                for (std::size_t i = 0; i < current->includeDirectories.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i)); char path[4096]; std::snprintf(path,sizeof(path),"%s",current->includeDirectories[i].c_str());
                    if (ImGui::InputText("Directory",path,sizeof(path))) current->includeDirectories[i] = path;
                    ImGui::PopID();
                }
                if (current->includeDirectories.size() < 32 && ImGui::SmallButton("Add include directory")) current->includeDirectories.emplace_back();
                ImGui::TreePop();
            }
            if (assemblyEditorState_.compiledImage && ImGui::TreeNode("Listing and symbols")) {
                const auto& image = *assemblyEditorState_.compiledImage;
                ImGui::BeginChild("unit-listing",{0,120},ImGuiChildFlags_Borders);
                ImGuiListClipper clipper; clipper.Begin(static_cast<int>(image.bytes.size()));
                while (clipper.Step()) {
                    auto it = image.bytes.begin(); std::advance(it,clipper.DisplayStart);
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row,++it) {
                        const auto [address,byte] = *it;
                        const auto source = image.sourceLines.find(address);
                        ImGui::Text("%04X  %02X  %s:%zu",address,byte,source == image.sourceLines.end() ? "" : source->second.file.c_str(),source == image.sourceLines.end() ? 0 : source->second.line);
                    }
                }
                ImGui::EndChild();
                for (const auto& [name,symbol] : image.symbols) ImGui::Text("%s = %04X",name.c_str(),symbol.value);
                ImGui::TreePop();
            }
        }
        if (!activeProjectUnit_.empty() && ImGui::SmallButton("Remove file from project")) {
            const auto id = activeProjectUnit_; (void)removeProjectUnit(id);
        }
        if (ImGui::TreeNode("Program load mappings")) {
            if (projectMappings_.size() < 256 && ImGui::Button("Add mapping")) {
                unsigned order = 0; for (const auto& row : projectMappings_) order = std::max(order,row.order+1);
                const auto target = peripherals_ && !peripherals_->memories().empty() ? peripherals_->memories().front().id : "";
                projectMappings_.push_back({"mapping"+std::to_string(order+1),activeProjectUnit_,target,0,65536,0,order,false});
            }
            for (std::size_t i = 0; i < projectMappings_.size();) {
                auto& row = projectMappings_[i]; ImGui::PushID(row.id.c_str()); ImGui::SeparatorText(row.id.c_str());
                if (ImGui::BeginCombo("Source unit",row.unit.c_str())) {
                    for (const auto& unit : projectUnits_) if (ImGui::Selectable(unit.id.c_str(),row.unit == unit.id)) row.unit = unit.id;
                    ImGui::EndCombo();
                }
                ImGui::InputScalar("Source start (hex)",ImGuiDataType_U32,&row.sourceStart,nullptr,nullptr,"%04X",ImGuiInputTextFlags_CharsHexadecimal);
                auto inclusiveEnd = row.sourceEnd ? row.sourceEnd-1 : 0;
                if (ImGui::InputScalar("Source end (hex, inclusive)",ImGuiDataType_U32,&inclusiveEnd,nullptr,nullptr,"%04X",ImGuiInputTextFlags_CharsHexadecimal)) row.sourceEnd = inclusiveEnd < 65536 ? inclusiveEnd+1 : 0;
                if (ImGui::BeginCombo("Memory device",row.device.c_str())) {
                    if (peripherals_) for (const auto& memory : peripherals_->memories()) if (ImGui::Selectable(memory.id.c_str(),memory.id == row.device)) row.device = memory.id;
                    ImGui::EndCombo();
                }
                ImGui::InputScalar("Physical destination (hex)",ImGuiDataType_U64,&row.destination,nullptr,nullptr,"%llX",ImGuiInputTextFlags_CharsHexadecimal);
                ImGui::InputScalar("Order",ImGuiDataType_U32,&row.order);
                ImGui::Checkbox("Allow ordered overlay",&row.overlay);
                const bool remove = ImGui::SmallButton("Remove mapping"); ImGui::PopID();
                if (remove) projectMappings_.erase(projectMappings_.begin()+static_cast<std::ptrdiff_t>(i)); else ++i;
            }
            ImGui::TreePop();
        }
    }
}
