// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"
#include "emu51_imgui_app_internal.h"
#include "emu51_imgui_paths.h"
#include "emu51_editor_files.h"
#include "TextEditor.h"
#include "emu51_imgui_asm_editor.h"
#include "portable-file-dialogs.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {
namespace fs = std::filesystem;
using emu51_imgui_internal::normalizedProgramPath;
constexpr std::size_t kMaxRecentProjects = 10;

bool readEditorSource(const fs::path& path, std::string& text)
{
    std::ifstream input(path, std::ios::binary);
    std::error_code error;
    if (!input || !fs::is_regular_file(path, error)) return false;
    text.clear();
    char chunk[4096];
    while (input.read(chunk, sizeof(chunk)) || input.gcount()) {
        text.append(chunk, static_cast<std::size_t>(input.gcount()));
        if (text.size() > 1024 * 1024) return false;
    }
    return !input.bad() && input.eof();
}
}

fs::path Emu51ImguiApp::chooseProjectPath(bool save)
{
    if (projectPathPicker_) return withoutExecutionLock([&] { return projectPathPicker_(save); });
    if (!pfd::settings::available()) {
        statusMessage_ = "file dialog unavailable";
        return {};
    }
    const std::vector<std::string> filters{"emu51 projects", "*.emu51", "All files", "*"};
    if (save) {
        fs::path initial = projectPath_.empty() ? fs::path("project.emu51") : fs::path(projectPath_);
        if (projectPath_.empty() && !loadedProgramPath_.empty()) {
            initial = loadedProgramPath_;
            initial.replace_extension(".emu51");
        }
        auto selected = withoutExecutionLock([&] { return fs::path(pfd::save_file("Save project", initial.string(), filters).result()); });
        if (!selected.empty() && !selected.has_extension()) selected += ".emu51";
        return selected;
    }
    // The macOS backend treats a filename as "default name", which is invalid
    // for choose file. Open dialogs need a directory, including on repeat opens.
    auto initial = fs::path(projectPath_).parent_path();
    std::error_code error;
    if (!fs::is_directory(initial,error)) initial.clear();
    const auto selected = withoutExecutionLock([&] { return pfd::open_file("Open project", initial.string(), filters).result(); });
    return selected.empty() ? fs::path{} : fs::path(selected.front());
}

void Emu51ImguiApp::requestOpenProject()
{
#ifdef __linux__
    if (!projectPathPicker_) {
        projectBrowser_.open = true;
        return;
    }
#endif
    const auto path = chooseProjectPath(false);
    resetTimingAfterUiWork();
    if (!path.empty()) openProjectFromPath(path);
}

#ifdef __linux__
void Emu51ImguiApp::browseProjectDirectory(const fs::path& path)
{
    auto& browser = projectBrowser_;
    std::error_code error;
    std::vector<ProjectBrowser::Entry> entries;
    withoutExecutionLock([&] {
        for (fs::directory_iterator it(path, fs::directory_options::none, error), end; !error && it != end; it.increment(error)) {
            std::error_code typeError;
            const bool directory = it->is_directory(typeError);
            if (directory || (it->path().extension() == ".emu51" && it->is_regular_file(typeError)))
                entries.push_back({it->path().filename(), directory});
        }
    });
    if (error) {
        browser.error = "Cannot list " + path.string() + ": " + error.message();
        return;
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.directory != b.directory) return a.directory;
        return a.name < b.name;
    });
    browser.directory = path.lexically_normal();
    browser.entries = std::move(entries);
    browser.path[0] = '\0';
    browser.error.clear();
}

void Emu51ImguiApp::drawOpenProjectBrowser()
{
    auto& browser = projectBrowser_;
    if (std::exchange(browser.open, false)) {
        browser = {};
        browser.directory = "/";
        std::error_code error;
        auto initial = fs::path(projectPath_).parent_path();
        if (!fs::is_directory(initial, error)) initial = fs::current_path(error);
        browseProjectDirectory(error ? fs::path("/") : initial);
        ImGui::OpenPopup("Open Project");
    }

    ImGui::SetNextWindowSize({640, 450}, ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal("Open Project", nullptr, ImGuiWindowFlags_NoSavedSettings)) return;

    std::optional<fs::path> directory;
    ImGui::BeginDisabled(browser.directory == browser.directory.root_path());
    if (ImGui::Button("Up")) directory = browser.directory.parent_path();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextWrapped("%s", browser.directory.c_str());

    bool submit = false;
    const float controlsHeight = 3 * ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("projects", {0, std::max(80.0f, ImGui::GetContentRegionAvail().y - controlsHeight)}, ImGuiChildFlags_Borders)) {
        for (const auto& entry : browser.entries) {
            const auto label = entry.name.string() + (entry.directory ? "/" : "");
            ImGui::PushID(label.c_str());
            if (ImGui::Selectable(label.c_str(), !entry.directory && entry.name == browser.path.data(),
                    ImGuiSelectableFlags_AllowDoubleClick)) {
                if (entry.directory) directory = browser.directory / entry.name;
                else {
                    std::snprintf(browser.path.data(), browser.path.size(), "%s", entry.name.c_str());
                    submit = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::SetNextItemWidth(-1);
    submit |= ImGui::InputTextWithHint("##project-path", "Project filename or path", browser.path.data(), browser.path.size(),
        ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("Open") || submit) {
        if (browser.path[0] != '\0') {
            const fs::path path = browser.directory / browser.path.data();
            std::error_code error;
            if (fs::is_directory(path, error)) directory = path;
            else {
                const bool opened = openProjectFromPath(path);
                resetTimingAfterUiWork();
                if (opened) ImGui::CloseCurrentPopup();
                else browser.error = statusMessage_;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    if (directory) browseProjectDirectory(*directory);
    if (!browser.error.empty()) ImGui::TextWrapped("%s", browser.error.c_str());
    ImGui::EndPopup();
}
#endif

void Emu51ImguiApp::saveProject()
{
    if (projectPath_.empty()) saveProjectAs();
    else saveProjectToPath(projectPath_, true);
}

void Emu51ImguiApp::saveProjectAs()
{
    const auto path = chooseProjectPath(true);
    resetTimingAfterUiWork();
    if (!path.empty()) saveProjectToPath(path, true);
}

int Emu51ImguiApp::recentProjectFileCount() const
{
    return static_cast<int>(recentProjectPaths_.size());
}

const std::string& Emu51ImguiApp::recentProjectFilePath(int index) const
{
    static const std::string empty;
    return index < 0 || static_cast<std::size_t>(index) >= recentProjectPaths_.size()
        ? empty : recentProjectPaths_[static_cast<std::size_t>(index)];
}

void Emu51ImguiApp::openRecentProjectFile(int index)
{
    const fs::path path = recentProjectFilePath(index);
    if (!path.empty()) openProjectFromPath(path);
}

void Emu51ImguiApp::clearRecentProjectFiles()
{
    recentProjectPaths_.clear();
    saveState();
}

void Emu51ImguiApp::rememberRecentProjectPath(const fs::path& path)
{
    const auto normalized = normalizedProgramPath(path);
    if (normalized.empty()) return;
    std::erase(recentProjectPaths_, normalized);
    recentProjectPaths_.insert(recentProjectPaths_.begin(), normalized);
    if (recentProjectPaths_.size() > kMaxRecentProjects) recentProjectPaths_.resize(kMaxRecentProjects);
    saveState();
}

bool Emu51ImguiApp::saveProjectSources()
{
    const auto active = std::find_if(projectUnits_.begin(), projectUnits_.end(),
        [&](const auto& unit) { return unit.id == activeProjectUnit_; });
    const fs::path activePath = assemblyEditorState_.sourcePath;
    if (assemblyEditorState_.dirty && !activePath.empty() &&
        (active == projectUnits_.end() || !active->embedded)) {
        if (!saveAssemblyEditorFileToPath(activePath)) {
            statusMessage_ = "Cannot save project source: " + activePath.string();
            return false;
        }
        syncProjectUnit();
    }
    for (auto& unit : projectUnits_) {
        if (unit.id == activeProjectUnit_ || unit.embedded || !unit.editor.draft ||
            !isEditableProjectUnit(unit)) continue;
        const auto& source = unit.editor.path.empty() ? unit.path : unit.editor.path;
        if (source.empty()) continue;
        const auto buffer = unitEditors_.find(unit.id);
        const auto& text = buffer == unitEditors_.end() ? unit.editor.text : buffer->second.observed;
        if (!withoutExecutionLock([&] { return emu51WriteTextFileAtomically(source, text); })) {
            statusMessage_ = "Cannot save project source: " + source;
            return false;
        }
        unit.editor.draft = false;
        unit.editor.text.clear();
        if (buffer != unitEditors_.end()) {
            buffer->second.state.dirty = false;
            if (auto* recovery = buffer->second.recovery.get())
                withoutExecutionLock([recovery] { recovery->clear(); });
        }
    }
    return true;
}

bool Emu51ImguiApp::saveProjectToPath(const fs::path& path, bool saveFiles)
{
    syncProjectUnit();
    trackAssemblyEditorChanges();
    const auto target = normalizedProgramPath(path);
    if (path.empty() || (!assemblyEditorState_.sourcePath.empty() &&
        target == normalizedProgramPath(assemblyEditorState_.sourcePath)) ||
        (!loadedProgramPath_.empty() && target == normalizedProgramPath(loadedProgramPath_)) ||
        loadedSources_.contains(target)) {
        statusMessage_ = "choose a project path different from the source file";
        return false;
    }
    for (const auto& program : projectPrograms_) {
        if (target == normalizedProgramPath(program.path)) {
            statusMessage_ = "choose a project path different from the program files";
            return false;
        }
    }
    for (const auto& unit : projectUnits_) if (target == normalizedProgramPath(unit.path) ||
        (!unit.editor.path.empty() && target == normalizedProgramPath(unit.editor.path))) {
        statusMessage_ = "choose a project path different from every project source/include file"; return false;
    }
    if (peripherals_) for (const auto& device : peripherals_->config().devices)
    {
        if (!device.storagePath.empty() && target == normalizedProgramPath(device.storagePath)) {
            statusMessage_ = "choose a project path different from the EEPROM image";
            return false;
        }
        for (const auto& image : device.memoryImages) if (target == normalizedProgramPath(image.path)) {
            statusMessage_ = "choose a project path different from binary initialization images"; return false;
        }
    }
    if (saveFiles && (!saveProjectSources() || !flushPeripheralStorage())) return false;
    Emu51Project project;
    project.units = projectUnits_; project.mappings = projectMappings_; project.activeUnit = activeProjectUnit_;
    const auto sourceBase = projectPath_.empty() ? fs::current_path() : fs::path(projectPath_).parent_path();
    for (auto& unit : project.units) for (auto& include : unit.includeDirectories)
        if (!fs::path(include).is_absolute()) include = (sourceBase/include).lexically_normal().string();
    if (peripherals_) project.peripherals = peripherals_->config();
    project.peripheralLayout = peripheralEditor_.positions(project.peripherals);
    project.peripheralLiveLayout = peripheralLive_.layout(project.peripherals);
    project.peripheralLiveLabels = peripheralLive_.labels();
    project.workspace = captureUiLayout();
    project.cpuModel = cpu_.model();
    project.unified = unifiedMode_;
    project.execution = {cycleExact_, crystalMHz_, stepsPerFrame_};
    if (projectUnits_.empty()) project.programs = projectPrograms_;
    project.reloadPath = loadedProgramPath_;
    project.reloadBinaryAddress = loadedProgramBinaryAddress_;
    project.breakpoints = breakpoints_;
    project.watches = symbolWatches_;
    project.debuggerOpen = debuggerOpen_;
    project.traceEnabled = traceEnabled_;
    project.serialTerminalOpen = serialTerminalOpen_;
    project.serialLineEnding = serialLineEnding_;
    project.serialFileLineEnding = serialFileLineEnding_;
    project.serialFileLineDelayMs = serialFileLineDelayMs_;
    if (hardware_ && hardware_->enabled()) project.hardwareModule = hardware_->id();
    for (const auto& watch : watchpoints_)
        project.watchpoints.push_back({static_cast<int>(watch.space), watch.address, watch.read, watch.write, watch.enabled});
    auto& editor = project.editor;
    editor.present = !assemblyEditorState_.sourcePath.empty() || assemblyEditorState_.dirty || hasAssemblyEditorSource();
    if (editor.present && observedAssemblyEditorText_.size() > 1024 * 1024) {
        statusMessage_ = "project editor buffers are limited to 1 MiB";
        return false;
    }
    editor.open = assemblyEditorState_.open;
    editor.path = assemblyEditorState_.sourcePath;
    editor.name = assemblyEditorState_.sourceName;
    editor.baseDirectory = assemblyEditorBaseDirectory().empty() ? fs::current_path().string() : assemblyEditorBaseDirectory().string();
    editor.draft = editor.present && (assemblyEditorState_.dirty || editor.path.empty());
    if (editor.draft) editor.text = observedAssemblyEditorText_;
    assemblyEditor_->GetMainCursor(editor.line, editor.column);
    std::string error;
    if (!withoutExecutionLock([&] { return emu51SaveProject(path, project, error); })) {
        statusMessage_ = error;
        return false;
    }
    projectPath_ = target;
    projectSavedEditorText_ = observedAssemblyEditorText_;
    savedProjectUnits_.clear();
    for (const auto& unit : projectUnits_) savedProjectUnits_.insert(unit.id);
    for (auto& [id,buffer] : unitEditors_) {
        if (!id.empty()) buffer.savedInProject = buffer.observed;
        if (auto* recovery = buffer.recovery.get()) withoutExecutionLock([recovery] { recovery->clear(); });
    }
    clearEditorRecovery();
    rememberRecentProjectPath(projectPath_);
    statusMessage_ = "saved project " + fs::path(projectPath_).filename().string();
    return true;
}

bool Emu51ImguiApp::openProjectFromPath(const fs::path& path)
{
    std::string error;
    auto project = withoutExecutionLock([&] { return emu51LoadProject(path, error); });
    if (!project) { statusMessage_ = error; return false; }
    if (project->formatVersion == 1 && !emu51MigrateProject(*project,error)) { statusMessage_ = error; return false; }
    if (!project->hardwareModule.empty() && (!hardware_ || project->hardwareModule != hardware_->id())) {
        statusMessage_ = "project requires unavailable hardware module: " + project->hardwareModule;
        return false;
    }
    std::unique_ptr<Emu51PeripheralSession> peripheralCandidate;
    try {
        if (!project->peripherals.devices.empty() && project->units.empty())
            peripheralCandidate = withoutExecutionLock([&] { return Emu51PeripheralSession::create(project->peripherals, project->execution.crystalMHz); });
    } catch (const std::exception& e) { statusMessage_ = e.what(); return false; }
    std::map<std::string,UnitEditor> candidateEditors;
    Emu51ProjectBuild preparedBuild;
    std::vector<emu51_asm::AssemblyImage> images;
    std::string editorText;
    const auto prepare = [&]() {
        images.clear();
        if (!project->units.empty()) {
            preparedBuild = withoutExecutionLock([&] { return emu51BuildProject(*project); });
            if (!preparedBuild.success()) { statusMessage_ = preparedBuild.error; return false; }
            candidateEditors.clear();
            for (const auto& unit : project->units) {
                if (!isEditableProjectUnit(unit)) continue;
                std::string text = unit.editor.text;
                if (!unit.embedded && !unit.editor.draft) {
                    const auto built = std::find_if(preparedBuild.units.begin(),preparedBuild.units.end(),[&](const auto& b) { return b.id == unit.id; });
                    const auto source = built == preparedBuild.units.end() ? std::map<std::string,std::string>::const_iterator{} : built->image.sources.find(unit.path);
                    if (built != preparedBuild.units.end() && source != built->image.sources.end()) text = source->second;
                    else if (!withoutExecutionLock([&] { return readEditorSource(unit.path,text); })) { statusMessage_ = "Cannot read project editor source: "+unit.path; return false; }
                }
                UnitEditor buffer; buffer.editor = std::make_unique<TextEditor>(); emu51ConfigureAsmEditor(*buffer.editor);
                buffer.editor->SetText(text); buffer.editor->SetCursor(unit.editor.line,unit.editor.column);
                buffer.observed = buffer.editor->GetText(); buffer.savedInProject = buffer.observed;
                buffer.state.sourcePath = unit.editor.path.empty() && !unit.embedded ? unit.path : unit.editor.path;
                buffer.state.sourceName = unit.editor.present ? unit.editor.name : fs::path(unit.path).filename().string();
                buffer.state.dirty = unit.editor.draft;
                buffer.baseDirectory = unit.baseDirectory.empty() ? fs::path(unit.path).parent_path().string() : unit.baseDirectory;
                const auto built = std::find_if(preparedBuild.units.begin(),preparedBuild.units.end(),[&](const auto& b) { return b.id == unit.id; });
                if (built != preparedBuild.units.end()) { buffer.state.compiledImage = built->image; buffer.diagnostics = built->diagnostics; }
                if (persistState_) buffer.recovery = Emu51EditorRecovery::create(emu51GetEditorRecoveryDirectory());
                candidateEditors.emplace(unit.id,std::move(buffer));
            }
            try {
                peripheralCandidate = withoutExecutionLock([&] { return Emu51PeripheralSession::create(project->peripherals,project->execution.crystalMHz); });
                if (!emu51ProgramProject(preparedBuild,*peripheralCandidate,error)) { statusMessage_ = error; return false; }
            } catch (const std::exception& e) { statusMessage_ = e.what(); return false; }
        }
        for (const auto& program : project->programs) {
            if (!project->units.empty()) break;
            if (program.fromEditor) {
                emu51_asm::Assembler assembler;
                auto compiled = withoutExecutionLock([&] { return assembler.assemble_string(program.path, program.sourceText, fs::path(program.path).parent_path()); });
                if (!compiled.success()) {
                    const auto& diagnostic = compiled.diagnostics.front();
                    statusMessage_ = diagnostic.file + ":" + std::to_string(diagnostic.line) + ": " + diagnostic.message;
                    return false;
                }
                images.push_back(std::move(compiled.image));
            } else {
                auto prepared = withoutExecutionLock([&] { return emu51_asm::prepare_program_file(program.path, {program.binaryAddress}); });
                if (!prepared.result.success) {
                    statusMessage_ = program.path + ": " + prepared.result.message;
                    return false;
                }
                images.push_back(std::move(prepared.image));
            }
        }
        const auto& editor = project->editor;
        editorText = editor.draft ? editor.text : std::string{};
        if (editor.present && !editor.draft && !withoutExecutionLock([&] { return readEditorSource(editor.path, editorText); })) {
            statusMessage_ = "unable to open project source: " + editor.path;
            return false;
        }
        return true;
    };
    if (!prepare()) return false;
    trackAssemblyEditorChanges();
    const bool dirty = assemblyEditorState_.dirty;
    if (!confirmProjectEditors()) return false;
    // Saving the old buffer may change one of this project's inputs. Recheck it
    // before resetting the machine; choosing Discard has not changed any files.
    if (((dirty && !assemblyEditorState_.dirty) || !projectUnits_.empty()) && !prepare()) return false;

    installPeripheralSession(std::move(peripheralCandidate));
    peripheralDraft_ = project->peripherals;
    peripheralEditor_.restore(project->peripheralLayout);
    peripheralLive_.restore(project->peripheralLiveLayout,project->peripheralLiveLabels);
    peripheralsOpen_ = peripheralsOpen_ || !peripheralDraft_.devices.empty();
    cpu_.setModel(project->cpuModel);
    resetMachine();
    internalRamMonitor_.start = clampMonitorAddress(internalRamMonitor_, internalRamMonitor_.start);
    if (hardware_) hardware_->setEnabled(!project->hardwareModule.empty());
    unifiedMode_ = project->unified;
    cycleExact_ = project->execution.cycleExact;
    crystalMHz_ = peripherals_ ? peripherals_->crystalMHz() : project->execution.crystalMHz;
    stepsPerFrame_ = project->execution.stepsPerFrame;
    for (const auto& image : images) {
        emu51_asm::apply_program_image(cpu_, image);
        setLoadedSourceMap(image);
    }
    if (!project->units.empty()) installProjectSources(preparedBuild);
    projectPrograms_ = project->programs;
    for (auto& [id,buffer] : unitEditors_) {
        (void)id;
        if (auto* recovery = buffer.recovery.get()) withoutExecutionLock([recovery] { recovery->clear(); });
    }
    unitEditors_ = std::move(candidateEditors); activeProjectUnit_.clear(); savedProjectUnits_.clear();
    lastUnitBuilds_.clear(); for (const auto& unit : preparedBuild.units) lastUnitBuilds_.emplace(unit.id,unit);
    projectUnits_ = project->units; projectMappings_ = project->mappings;
    for (const auto& unit : projectUnits_) savedProjectUnits_.insert(unit.id);
    selectedProjectFile_ = project->activeUnit.empty() && !projectUnits_.empty() ? projectUnits_.front().id : project->activeUnit;
    projectPanelMessage_.clear();
    pendingAssemblyTab_.clear();
    if (!projectUnits_.empty()) setProjectPanelVisible(true);
    loadedProgramPath_ = project->reloadPath;
    loadedProgramBinaryAddress_ = project->reloadBinaryAddress;
    breakpoints_ = project->breakpoints;
    watchpoints_.clear();
    for (const auto& watch : project->watchpoints)
        watchpoints_.push_back({static_cast<WatchSpace>(watch.space), watch.address, watch.read, watch.write, watch.enabled});
    symbolWatches_ = project->watches;
    debuggerOpen_ = project->debuggerOpen;
    traceEnabled_ = project->traceEnabled;
    serialTerminalOpen_ = project->serialTerminalOpen;
    serialLineEnding_ = project->serialLineEnding;
    serialFileLineEnding_ = project->serialFileLineEnding;
    serialFileLineDelayMs_ = project->serialFileLineDelayMs;
    pendingWatchpointStop_.clear();
    const auto& editor = project->editor;
    assemblyEditorState_ = {};
    assemblyEditorState_.sourcePath = editor.path;
    assemblyEditorState_.sourceName = editor.name;
    assemblyEditorState_.open = editor.open || (!editor.present &&
        std::any_of(projectUnits_.begin(), projectUnits_.end(),
            [](const auto& unit) { return unit.editor.open && isEditableProjectUnit(unit); }));
    untitledSourceBaseDirectory_ = editor.path.empty() ? editor.baseDirectory : std::string{};
    assemblyEditor_->SetText(editorText);
    assemblyEditor_->ClearMarkers();
    observedAssemblyEditorText_ = assemblyEditor_->GetText();
    editorDiagnostics_.clear();
    if (editor.present && editor.draft) markAssemblyEditorChanged();
    projectSavedEditorText_ = observedAssemblyEditorText_;
    clearEditorRecovery();
    assemblyEditor_->SetCursor(editor.line, editor.column);
    updateEditorSourceMatch();
    if (unitEditors_.contains(project->activeUnit)) {
        const auto unit = std::find_if(projectUnits_.begin(), projectUnits_.end(),
            [&](const auto& item) { return item.id == project->activeUnit; });
        if (!unit->editor.present) (void)openProjectUnit(unit->id);
        else (void)switchProjectUnit(unit->id);
    }
    auto openUnit = std::find_if(projectUnits_.begin(), projectUnits_.end(),
        [&](const auto& unit) { return unit.id == activeProjectUnit_ && unit.editor.open; });
    if (openUnit == projectUnits_.end()) {
        openUnit = std::find_if(projectUnits_.begin(), projectUnits_.end(),
            [](const auto& unit) { return unit.editor.open && isEditableProjectUnit(unit); });
        if (openUnit != projectUnits_.end()) (void)switchProjectUnit(openUnit->id);
    }
    if (openUnit != projectUnits_.end()) pendingAssemblyTab_ = openUnit->id;
    if (project->workspace) applyUiLayout(*project->workspace, true);
    binaryLoadDialog_.open = fillRangeDialog_.open = setProgramCounterDialog_.open = executionSettingsDialog_.open = false;
    projectPath_ = normalizedProgramPath(path);
    rememberRecentProjectPath(projectPath_);
    quitApproved_ = false;
    invalidateInstructionCache();
    updateDisassemblyFollowRange();
    cycleTiming_.reset();
    statusMessage_ = "opened project " + fs::path(projectPath_).filename().string() + " (paused)";
    return true;
}
