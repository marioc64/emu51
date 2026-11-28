// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"
#include "emu51_imgui_asm_editor.h"
#include "emu51_imgui_paths.h"
#include "emu51_editor_files.h"
#include "TextEditor.h"
#include "imgui_internal.h"
#include "portable-file-dialogs.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <tuple>

bool Emu51ImguiApp::isEditableProjectUnit(const Emu51AssemblyUnit& unit)
{
    return unit.embedded || unit.includeOnly ||
        emu51_asm::detect_program_file_kind(unit.path) == emu51_asm::ProgramFileKind::assembly_source;
}

void Emu51ImguiApp::syncProjectUnit()
{
    if (activeProjectUnit_.empty()) return;
    trackAssemblyEditorChanges();
    const auto unit = std::find_if(projectUnits_.begin(),projectUnits_.end(),[&](const auto& u) { return u.id == activeProjectUnit_; });
    if (unit == projectUnits_.end()) return;
    if (!assemblyEditorState_.sourcePath.empty()) unit->path = assemblyEditorState_.sourcePath;
    auto& e = unit->editor;
    e.present = true; e.draft = unit->embedded || assemblyEditorState_.dirty;
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
        if (unit == projectUnits_.end() || !isEditableProjectUnit(*unit)) return false;
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
        buffer.state.dirty = unit->editor.draft;
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
    const bool visible = assemblyEditorState_.open;
    const auto swap = [&](UnitEditor& buffer) {
        std::swap(assemblyEditor_,buffer.editor); std::swap(assemblyEditorState_,buffer.state);
        std::swap(editorDiagnostics_,buffer.diagnostics); std::swap(observedAssemblyEditorText_,buffer.observed);
        std::swap(untitledSourceBaseDirectory_,buffer.baseDirectory); std::swap(projectSavedEditorText_,buffer.savedInProject);
        std::swap(editorRecovery_,buffer.recovery);
    };
    swap(unitEditors_[activeProjectUnit_]);
    swap(next->second);
    assemblyEditorState_.open = visible;
    activeProjectUnit_ = id;
    recoverySavePending_ = false;
    editorDebugSourceName_.clear(); editorDebugSourceKey_.clear();
    configureEditorDebugger();
    assemblyEditor_->SetTransactionCallback([this](std::vector<TextEditor::Change>&) { auto guard = access(); markAssemblyEditorChanged(); });
    updateEditorSourceMatch();
    return true;
}

bool Emu51ImguiApp::openProjectUnit(const std::string& id)
{
    const auto unit = std::find_if(projectUnits_.begin(), projectUnits_.end(), [&](const auto& item) { return item.id == id; });
    if (unit == projectUnits_.end() || !isEditableProjectUnit(*unit) || !switchProjectUnit(id)) return false;
    unit->editor.open = true;
    pendingAssemblyTab_ = id;
    setAssemblyEditorVisible(true);
    return true;
}

void Emu51ImguiApp::closeProjectUnitTab(const std::string& id)
{
    const auto unit = std::find_if(projectUnits_.begin(), projectUnits_.end(), [&](const auto& item) { return item.id == id; });
    if (unit == projectUnits_.end()) return;
    unit->editor.open = false;
    if (activeProjectUnit_ == id) {
        syncProjectUnit();
        saveEditorRecovery();
        const auto next = std::find_if(projectUnits_.begin(), projectUnits_.end(),
            [](const auto& item) { return item.editor.open && isEditableProjectUnit(item); });
        if (next != projectUnits_.end()) (void)openProjectUnit(next->id);
    }
    executionRedrawPending_.store(true, std::memory_order_release);
}

bool Emu51ImguiApp::confirmProjectEditors()
{
    if (projectUnits_.empty()) return confirmAssemblyEditorChanges();
    const auto original = activeProjectUnit_;
    std::vector<std::string> ids;
    for (const auto& u : projectUnits_)
        if (u.id == original || unitEditors_.contains(u.id)) ids.push_back(u.id);
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
    if (!build.success()) {
        projectPanelMessage_ = statusMessage_ = build.error;
        return false;
    }
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
        } catch (const std::exception& e) {
            projectPanelMessage_ = statusMessage_ = e.what();
            return false;
        }
    }
    statusMessage_ = load ? (powerCycle ? "Power cycle completed from initialization and program recipes" : "Project programmed; selectors unchanged") : "All project build units compiled and validated";
    projectPanelMessage_ = statusMessage_;
    return true;
}
bool Emu51ImguiApp::addProjectUnit(const std::filesystem::path& path, bool embedded, bool openEditor)
{
    std::error_code error;
    if (!embedded && !std::filesystem::is_regular_file(path, error)) {
        statusMessage_ = "Cannot open project file: " + path.string();
        return false;
    }
    const auto canonical = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
    for (const auto& unit : projectUnits_) if (!embedded && unit.path == canonical.string()) {
        selectedProjectFile_ = unit.id;
        setProjectPanelVisible(true);
        return !openEditor || !isEditableProjectUnit(unit) || openProjectUnit(unit.id);
    }
    if (projectUnits_.size() >= 32) { statusMessage_ = "Projects support 32 build units"; return false; }
    unsigned suffix = 1; std::string id;
    do { id = "source"+std::to_string(suffix++); } while (std::any_of(projectUnits_.begin(),projectUnits_.end(),[&](const auto& u) { return u.id == id; }));
    Emu51AssemblyUnit unit; unit.id = id; unit.path = canonical.string();
    unit.embedded = embedded; unit.baseDirectory = std::filesystem::path(unit.path).parent_path().string();
    unit.includeOnly = path.extension() == ".inc" || path.extension() == ".INC";
    if (isEditableProjectUnit(unit))
        unit.editor = {true,false,embedded,embedded ? "" : unit.path,path.filename().string(),unit.baseDirectory,embedded ? "; New project source\norg 0\nend\n" : "",0,0};
    projectUnits_.push_back(std::move(unit));
    if (openEditor && isEditableProjectUnit(projectUnits_.back()) && !openProjectUnit(id)) {
        projectUnits_.pop_back();
        return false;
    }
    selectedProjectFile_ = id;
    setProjectPanelVisible(true);
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
    const auto original = activeProjectUnit_;
    if (id == original || unitEditors_.contains(id)) {
        if (!switchProjectUnit(id)) return false;
        const auto saved = projectSavedEditorText_;
        projectSavedEditorText_.reset();
        if (!confirmAssemblyEditorChanges()) {
            projectSavedEditorText_ = saved;
            (void)switchProjectUnit(original);
            return false;
        }
        std::string next = original == id ? "" : original;
        if (next.empty()) {
            for (const auto& unit : projectUnits_) {
                if (unit.id != id && unit.editor.open && isEditableProjectUnit(unit)) {
                    next = unit.id;
                    break;
                }
            }
        }
        if (!next.empty() || unitEditors_.contains("")) {
            if (!switchProjectUnit(next)) { projectSavedEditorText_ = saved; return false; }
            if (auto old = unitEditors_.find(id); old != unitEditors_.end() && old->second.recovery) {
                auto* recovery = old->second.recovery.get();
                withoutExecutionLock([recovery] { recovery->clear(); });
            }
        } else {
            clearEditorRecovery();
            activeProjectUnit_.clear();
            const bool visible = assemblyEditorState_.open;
            assemblyEditor_ = std::make_unique<TextEditor>();
            emu51ConfigureAsmEditor(*assemblyEditor_);
            assemblyEditorState_ = {};
            assemblyEditorState_.open = visible;
            observedAssemblyEditorText_ = assemblyEditor_->GetText();
            editorDiagnostics_.clear();
            projectSavedEditorText_.reset();
            assemblyEditor_->SetTransactionCallback([this](std::vector<TextEditor::Change>&) { auto guard = access(); markAssemblyEditorChanged(); });
            configureEditorDebugger();
        }
    }
    unitEditors_.erase(id);
    savedProjectUnits_.erase(id);
    lastUnitBuilds_.erase(id);
    std::erase_if(projectUnits_,[&](const auto& unit) { return unit.id == id; });
    std::erase_if(projectMappings_,[&](const auto& row) { return row.unit == id; });
    if (selectedProjectFile_ == id)
        selectedProjectFile_ = projectUnits_.empty() ? "" : projectUnits_.front().id;
    statusMessage_ = "Source removed from project; file preserved";
    return true;
}

void Emu51ImguiApp::setProjectPanelVisible(bool visible)
{
    if (projectPanelOpen_ == visible) return;
    projectPanelOpen_ = visible;
    if (!visible) {
        const PaneKey pane{PaneKey::Kind::Project, 0};
        removePaneSnapTarget(pane);
        if (hasActivePane_ && activePane_ == pane) hasActivePane_ = false;
    }
    executionRedrawPending_.store(true, std::memory_order_release);
}

void Emu51ImguiApp::drawProjectPanel()
{
    const PaneKey pane{PaneKey::Kind::Project, 0};
    if (!projectPanelOpen_ || paneHiddenByCollapsedDock(pane)) return;
    ImGuiWindowFlags flags = 0;
    applyWindowPlacement(pane, {270, 540}, &flags);
    bool open = true;
    const bool visible = ImGui::Begin("Project###project_panel", &open, flags);
    capturePanePlacement(pane);
    registerPaneWindow(pane);
    drawPaneSnapContextMenu(pane);
    if (visible) {
        notePaneActivity(pane, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
        drawProjectFiles();
    }
    ImGui::End();
    if (!open) setProjectPanelVisible(false);
}

void Emu51ImguiApp::requestAddProjectFiles()
{
    const auto base = projectPath_.empty() ? std::string{} :
        std::filesystem::path(projectPath_).parent_path().string();
    const auto paths = withoutExecutionLock([&] {
        return pfd::open_file("Add project file", base,
            {"Project files", "*.asm *.s03 *.hex *.ihx *.ihex *.bin *.inc", "All files", "*"},
            pfd::opt::multiselect).result();
    });
    if (!paths.empty()) setProjectPanelVisible(true);
    for (const auto& path : paths)
        if (!addProjectUnit(path, false, false)) projectPanelMessage_ = statusMessage_;
    resetTimingAfterUiWork();
}

void Emu51ImguiApp::newProjectAssemblyFile()
{
    const auto directory = projectPath_.empty() ? std::filesystem::current_path() :
        std::filesystem::path(projectPath_).parent_path();
    unsigned suffix = 1;
    auto path = directory / "untitled1.asm";
    while (std::filesystem::exists(path) || std::any_of(projectUnits_.begin(), projectUnits_.end(),
        [&](const auto& unit) { return unit.path == path.string(); }))
        path = directory / ("untitled" + std::to_string(++suffix) + ".asm");
    setProjectPanelVisible(true);
    if (!addProjectUnit(path, true)) projectPanelMessage_ = statusMessage_;
}

void Emu51ImguiApp::performProjectBuild(Emu51ProjectBuildAction action)
{
    const bool powerCycle = action == Emu51ProjectBuildAction::PowerCycle;
    if (!powerCycle && !hasProjectFiles()) return;
    if (!buildProjectUnits(action != Emu51ProjectBuildAction::Build, powerCycle))
        setProjectPanelVisible(true);
    resetTimingAfterUiWork();
}

void Emu51ImguiApp::drawProjectFiles()
{
    if (!peripherals_ || !peripherals_->circuitBus())
        if (ImGui::Button("Create memory circuit") && !initializeCircuitProject()) projectPanelMessage_ = statusMessage_;

    if (!projectPanelMessage_.empty()) ImGui::TextWrapped("%s", projectPanelMessage_.c_str());

    if (projectUnits_.empty()) {
        ImGui::TextDisabled("No project files");
        return;
    }

    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    const float padding = 2 * ImGui::GetStyle().WindowPadding.y;
    const float listHeight = std::min(rowHeight * static_cast<float>(projectUnits_.size()) + padding,
        std::max(2 * rowHeight + padding, ImGui::GetContentRegionAvail().y * 0.4f));
    if (ImGui::BeginChild("project-file-list", {0, listHeight}, ImGuiChildFlags_Borders)) {
        for (const auto& unit : projectUnits_) {
            ImGui::PushID(unit.id.c_str());
            const auto label = std::filesystem::path(unit.path).filename().string() + "###file";
            if (ImGui::Selectable(label.c_str(), selectedProjectFile_ == unit.id,
                    ImGuiSelectableFlags_AllowDoubleClick, {ImGui::GetContentRegionAvail().x, 0})) {
                selectedProjectFile_ = unit.id;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && isEditableProjectUnit(unit))
                    (void)openProjectUnit(unit.id);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", unit.path.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    auto current = std::find_if(projectUnits_.begin(), projectUnits_.end(),
        [&](const auto& unit) { return unit.id == selectedProjectFile_; });
    if (current == projectUnits_.end()) return;
    const auto& id = current->id;
    ImGui::SeparatorText("File properties");
    if (isEditableProjectUnit(*current)) {
        if (ImGui::Button("Open in ASM Editor") && !openProjectUnit(id)) projectPanelMessage_ = statusMessage_;
    }
    if (ImGui::Button("Remove from project")) {
        (void)removeProjectUnit(id);
        return;
    }
    ImGui::PushID(id.c_str());
    ImGui::PushItemWidth(-1.0f);
    const auto inputScalar = [](const char* label, ImGuiDataType type, void* value, const char* format = nullptr) {
        ImGui::TextWrapped("%s", label);
        ImGui::PushID(label);
        const bool changed = ImGui::InputScalar("##value", type, value, nullptr, nullptr, format,
            format ? ImGuiInputTextFlags_CharsHexadecimal : ImGuiInputTextFlags_None);
        ImGui::PopID();
        return changed;
    };
    const auto fileKind = emu51_asm::detect_program_file_kind(current->path);
    const bool assemblySource = current->embedded || fileKind == emu51_asm::ProgramFileKind::assembly_source;
    if (isEditableProjectUnit(*current)) ImGui::Checkbox("Include file", &current->includeOnly);
    if (assemblySource && !current->includeOnly) ImGui::Checkbox("Build only", &current->buildOnly);
    if (!current->includeOnly && !current->embedded && fileKind == emu51_asm::ProgramFileKind::raw_binary)
        inputScalar("Binary source address (hex)", ImGuiDataType_U16, &current->binaryAddress, "%04X");

    const bool hasMappings = std::any_of(projectMappings_.begin(), projectMappings_.end(),
        [&](const auto& row) { return row.unit == id; });
    if (!current->includeOnly || hasMappings) {
        ImGui::SeparatorText("Load mappings");
        if (!current->includeOnly && projectMappings_.size() < 256 && ImGui::SmallButton("Add mapping")) {
            unsigned order = 0;
            for (const auto& row : projectMappings_) order = std::max(order, row.order + 1);
            unsigned suffix = 1;
            std::string mappingId;
            do { mappingId = "mapping" + std::to_string(suffix++); }
            while (std::any_of(projectMappings_.begin(), projectMappings_.end(),
                [&](const auto& row) { return row.id == mappingId; }));
            Emu51ProgramMapping mapping{mappingId, id, "", 0, 65536, 0, order, false};
            if (peripherals_ && !peripherals_->memories().empty()) {
                const auto& memory = peripherals_->memories().front();
                mapping.device = memory.id;
                mapping.sourceEnd = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(65536, e51p_memory_capacity(memory.storage.get())));
            }
            // Find the file's emitted address interval using its current build options and drafts.
            auto fileProject = captureBuildProject();
            fileProject.mappings.clear();
            for (auto& unit : fileProject.units) {
                unit.includeOnly = unit.id != id;
                unit.buildOnly = true;
            }
            const auto fileBuild = withoutExecutionLock([&] { return emu51BuildProject(fileProject); });
            if (fileBuild.success() && !fileBuild.units.empty() && !fileBuild.units.front().image.bytes.empty()) {
                const auto& bytes = fileBuild.units.front().image.bytes;
                mapping.sourceStart = bytes.begin()->first;
                mapping.sourceEnd = static_cast<std::uint32_t>(bytes.rbegin()->first) + 1;
            }
            projectMappings_.push_back(std::move(mapping));
        }
        for (auto row = projectMappings_.begin(); row != projectMappings_.end();) {
            if (row->unit != id) { ++row; continue; }
            ImGui::PushID(row->id.c_str());
            ImGui::Separator();
            ImGui::TextUnformatted("Memory chip");
            if (ImGui::BeginCombo("##memory-chip", row->device.empty() ? "Choose chip" : row->device.c_str())) {
                if (peripherals_) for (const auto& memory : peripherals_->memories())
                    if (ImGui::Selectable(memory.id.c_str(), memory.id == row->device)) row->device = memory.id;
                ImGui::EndCombo();
            }
            inputScalar("Physical address (hex)", ImGuiDataType_U64, &row->destination, "%llX");
            if (ImGui::TreeNode("Source range and order")) {
                inputScalar("Source start (hex)", ImGuiDataType_U32, &row->sourceStart, "%04X");
                auto end = row->sourceEnd ? row->sourceEnd - 1 : 0;
                if (inputScalar("Source end (incl., hex)", ImGuiDataType_U32, &end, "%04X"))
                    row->sourceEnd = std::min(end, 65535u) + 1;
                inputScalar("Order", ImGuiDataType_U32, &row->order);
                ImGui::Checkbox("Allow ordered overlay", &row->overlay);
                ImGui::TreePop();
            }
            const bool remove = ImGui::SmallButton("Remove mapping");
            ImGui::PopID();
            if (remove) row = projectMappings_.erase(row);
            else ++row;
        }
    }
    if (assemblySource && !current->includeOnly && ImGui::TreeNode("Include directories")) {
        for (std::size_t i = 0; i < current->includeDirectories.size();) {
            ImGui::PushID(static_cast<int>(i));
            char path[4096];
            std::snprintf(path, sizeof(path), "%s", current->includeDirectories[i].c_str());
            if (ImGui::InputText("##directory", path, sizeof(path))) current->includeDirectories[i] = path;
            const bool remove = ImGui::SmallButton("Remove directory");
            ImGui::PopID();
            if (remove) current->includeDirectories.erase(current->includeDirectories.begin() + static_cast<std::ptrdiff_t>(i));
            else ++i;
        }
        if (current->includeDirectories.size() < 32 && ImGui::SmallButton("Add include directory"))
            current->includeDirectories.emplace_back();
        ImGui::TreePop();
    }
    const AssemblyEditorState* editor = id == activeProjectUnit_ ? &assemblyEditorState_ : nullptr;
    if (const auto it = unitEditors_.find(id); !editor && it != unitEditors_.end()) editor = &it->second.state;
    if (editor && editor->compiledImage && ImGui::TreeNode("Listing and symbols")) {
        const auto& image = *editor->compiledImage;
        ImGui::BeginChild("unit-listing", {0, 120}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(image.bytes.size()));
        while (clipper.Step()) {
            auto it = image.bytes.begin();
            std::advance(it, clipper.DisplayStart);
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row, ++it) {
                const auto [address, byte] = *it;
                const auto source = image.sourceLines.find(address);
                ImGui::Text("%04X  %02X  %s:%zu", address, byte,
                    source == image.sourceLines.end() ? "" : source->second.file.c_str(),
                    source == image.sourceLines.end() ? 0 : source->second.line);
            }
        }
        ImGui::EndChild();
        for (const auto& [name, symbol] : image.symbols) ImGui::TextWrapped("%s = %04X", name.c_str(), symbol.value);
        ImGui::TreePop();
    }
    ImGui::PopItemWidth();
    ImGui::PopID();
}

void Emu51ImguiApp::drawAssemblyEditorTabs()
{
    auto* window = ImGui::GetCurrentWindow();
    auto& dc = window->DC;
    const auto& style = ImGui::GetStyle();
    const auto title = window->TitleBarRect();
    const float buttonSpace = ImGui::GetFontSize() + style.ItemInnerSpacing.x;
    const float start = title.Min.x + style.FramePadding.x + ImGui::CalcTextSize("Assembly Editor").x +
        2 * style.ItemInnerSpacing.x + (style.WindowMenuButtonPosition == ImGuiDir_Left ? buttonSpace : 0);
    const float end = title.Max.x - style.FramePadding.x - buttonSpace -
        (style.WindowMenuButtonPosition == ImGuiDir_Right ? buttonSpace : 0);
    if (end <= start + ImGui::GetFrameHeight()) return;

    const auto saved = std::make_tuple(dc.CursorPos, dc.CursorPosPrevLine, dc.CursorMaxPos, dc.IdealMaxPos,
        dc.CurrLineSize, dc.PrevLineSize, dc.CurrLineTextBaseOffset, dc.PrevLineTextBaseOffset,
        dc.IsSameLine, dc.IsSetPos, dc.NavLayerCurrent);
    dc.NavLayerCurrent = ImGuiNavLayer_Menu;
    const auto barId = window->GetID("assembly-file-tabs");
    auto* bar = GImGui->TabBars.GetOrAddByKey(barId);
    bar->ID = barId;
    const ImRect bounds({start, title.Min.y}, {end, title.Max.y});
    auto clip = bounds;
    clip.ClipWith(window->OuterRectClipped);
    ImGui::PushClipRect(clip.Min, clip.Max, false);
    ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize, 0);
    bar->SeparatorMinX = start;
    bar->SeparatorMaxX = end;
    std::string selected;
    std::string closed;
    ImGuiTabBarFlags flags = ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton;
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) flags |= ImGuiTabBarFlags_IsFocused;
    if (ImGui::BeginTabBarEx(bar, bounds, flags)) {
        const auto requested = std::exchange(pendingAssemblyTab_, {});
        if (projectUnits_.empty() && ImGui::BeginTabItem(
                (assemblyEditorState_.sourceName + "###standalone").c_str(), nullptr,
                assemblyEditorState_.dirty ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None))
            ImGui::EndTabItem();
        for (const auto& unit : projectUnits_) {
            if (!unit.editor.open || !isEditableProjectUnit(unit)) continue;
            const bool active = unit.id == activeProjectUnit_;
            const auto it = unitEditors_.find(unit.id);
            const bool dirty = active ? assemblyEditorState_.dirty : it != unitEditors_.end() && it->second.state.dirty;
            auto label = std::filesystem::path(unit.path).filename().string() + "###" + unit.id;
            bool open = true;
            ImGuiTabItemFlags tabFlags = dirty ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
            if (unit.id == requested) tabFlags |= ImGuiTabItemFlags_SetSelected;
            if (ImGui::BeginTabItem(label.c_str(), &open, tabFlags)) {
                if (!active && requested.empty() && bar->SelectedTabId == ImHashStr(label.c_str(), 0, barId))
                    selected = unit.id;
                ImGui::EndTabItem();
            }
            if (!open) closed = unit.id;
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar();
    ImGui::PopClipRect();
    std::tie(dc.CursorPos, dc.CursorPosPrevLine, dc.CursorMaxPos, dc.IdealMaxPos,
        dc.CurrLineSize, dc.PrevLineSize, dc.CurrLineTextBaseOffset, dc.PrevLineTextBaseOffset,
        dc.IsSameLine, dc.IsSetPos, dc.NavLayerCurrent) = saved;
    if (!selected.empty() && selected != closed) (void)switchProjectUnit(selected);
    if (!closed.empty()) closeProjectUnitTab(closed);
}
