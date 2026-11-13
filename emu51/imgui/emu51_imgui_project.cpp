// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"
#include "emu51_imgui_app_internal.h"
#include "emu51_imgui_paths.h"
#include "emu51_editor_files.h"
#include "TextEditor.h"
#include "portable-file-dialogs.h"

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
    if (projectPathPicker_) return projectPathPicker_(save);
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
        auto selected = fs::path(pfd::save_file("Save project", initial.string(), filters).result());
        if (!selected.empty() && !selected.has_extension()) selected += ".emu51";
        return selected;
    }
    const auto selected = pfd::open_file("Open project", projectPath_, filters).result();
    return selected.empty() ? fs::path{} : fs::path(selected.front());
}

void Emu51ImguiApp::requestOpenProject()
{
    const auto path = chooseProjectPath(false);
    cycleTiming_.reset();
    if (!path.empty()) openProjectFromPath(path);
}

void Emu51ImguiApp::saveProject()
{
    if (projectPath_.empty()) saveProjectAs();
    else saveProjectToPath(projectPath_);
}

void Emu51ImguiApp::saveProjectAs()
{
    const auto path = chooseProjectPath(true);
    cycleTiming_.reset();
    if (!path.empty()) saveProjectToPath(path);
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
    saveRecentProjectPaths();
}

void Emu51ImguiApp::loadRecentProjectPaths()
{
    const auto path = emu51GetRecentProjectsPath();
    if (path.empty()) return;
    std::ifstream input(path);
    if (input) loadRecentProjectPaths(input);
}

void Emu51ImguiApp::loadRecentProjectPaths(std::istream& input)
{
    recentProjectPaths_.clear();
    std::string line;
    while (recentProjectPaths_.size() < kMaxRecentProjects && std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto path = normalizedProgramPath(line);
        if (!path.empty() && std::find(recentProjectPaths_.begin(), recentProjectPaths_.end(), path) == recentProjectPaths_.end())
            recentProjectPaths_.push_back(path);
    }
}

void Emu51ImguiApp::saveRecentProjectPaths(std::ostream& output) const
{
    for (const auto& path : recentProjectPaths_) output << path << '\n';
}

void Emu51ImguiApp::saveRecentProjectPaths() const
{
    if (!persistState_) return;
    const auto path = emu51GetRecentProjectsPath();
    if (path.empty()) return;
    std::ostringstream output;
    saveRecentProjectPaths(output);
    emu51WriteTextFileAtomically(path, output.str());
}

void Emu51ImguiApp::rememberRecentProjectPath(const fs::path& path)
{
    const auto normalized = normalizedProgramPath(path);
    if (normalized.empty()) return;
    std::erase(recentProjectPaths_, normalized);
    recentProjectPaths_.insert(recentProjectPaths_.begin(), normalized);
    if (recentProjectPaths_.size() > kMaxRecentProjects) recentProjectPaths_.resize(kMaxRecentProjects);
    saveRecentProjectPaths();
}

bool Emu51ImguiApp::saveProjectToPath(const fs::path& path)
{
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
    Emu51Project project;
    project.unified = unifiedMode_;
    project.execution = {cycleExact_, crystalMHz_, stepsPerFrame_};
    project.programs = projectPrograms_;
    project.reloadPath = loadedProgramPath_;
    project.reloadBinaryAddress = loadedProgramBinaryAddress_;
    project.breakpoints = breakpoints_;
    project.watches = symbolWatches_;
    project.debuggerOpen = debuggerOpen_;
    project.traceEnabled = traceEnabled_;
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
    if (editor.draft) editor.text = assemblyEditor_->GetText();
    assemblyEditor_->GetMainCursor(editor.line, editor.column);
    std::string error;
    if (!emu51SaveProject(path, project, error)) {
        statusMessage_ = error;
        return false;
    }
    projectPath_ = target;
    rememberRecentProjectPath(projectPath_);
    statusMessage_ = "saved project " + fs::path(projectPath_).filename().string();
    return true;
}

bool Emu51ImguiApp::openProjectFromPath(const fs::path& path)
{
    std::string error;
    const auto project = emu51LoadProject(path, error);
    if (!project) { statusMessage_ = error; return false; }
    std::vector<emu51_asm::AssemblyImage> images;
    std::string editorText;
    const auto prepare = [&]() {
        images.clear();
        for (const auto& program : project->programs) {
            if (program.fromEditor) {
                emu51_asm::Assembler assembler;
                auto compiled = assembler.assemble_string(program.path, program.sourceText, fs::path(program.path).parent_path());
                if (!compiled.success()) {
                    const auto& diagnostic = compiled.diagnostics.front();
                    statusMessage_ = diagnostic.file + ":" + std::to_string(diagnostic.line) + ": " + diagnostic.message;
                    return false;
                }
                images.push_back(std::move(compiled.image));
            } else {
                auto prepared = emu51_asm::prepare_program_file(program.path, {program.binaryAddress});
                if (!prepared.result.success) {
                    statusMessage_ = program.path + ": " + prepared.result.message;
                    return false;
                }
                images.push_back(std::move(prepared.image));
            }
        }
        const auto& editor = project->editor;
        editorText = editor.draft ? editor.text : std::string{};
        if (editor.present && !editor.draft && !readEditorSource(editor.path, editorText)) {
            statusMessage_ = "unable to open project source: " + editor.path;
            return false;
        }
        return true;
    };
    if (!prepare()) return false;
    trackAssemblyEditorChanges();
    const bool dirty = assemblyEditorState_.dirty;
    if (!confirmAssemblyEditorChanges()) return false;
    // Saving the old buffer may change one of this project's inputs. Recheck it
    // before resetting the machine; choosing Discard has not changed any files.
    if (dirty && !assemblyEditorState_.dirty && !prepare()) return false;

    resetMachine();
    unifiedMode_ = project->unified;
    cycleExact_ = project->execution.cycleExact;
    crystalMHz_ = project->execution.crystalMHz;
    stepsPerFrame_ = project->execution.stepsPerFrame;
    for (const auto& image : images) {
        emu51_asm::apply_program_image(cpu_, image);
        setLoadedSourceMap(image);
    }
    projectPrograms_ = project->programs;
    loadedProgramPath_ = project->reloadPath;
    loadedProgramBinaryAddress_ = project->reloadBinaryAddress;
    breakpoints_ = project->breakpoints;
    watchpoints_.clear();
    for (const auto& watch : project->watchpoints)
        watchpoints_.push_back({static_cast<WatchSpace>(watch.space), watch.address, watch.read, watch.write, watch.enabled});
    symbolWatches_ = project->watches;
    debuggerOpen_ = project->debuggerOpen;
    traceEnabled_ = project->traceEnabled;
    pendingWatchpointStop_.clear();
    const auto& editor = project->editor;
    assemblyEditorState_ = {};
    assemblyEditorState_.sourcePath = editor.path;
    assemblyEditorState_.sourceName = editor.name;
    assemblyEditorState_.open = editor.open;
    untitledSourceBaseDirectory_ = editor.path.empty() ? editor.baseDirectory : std::string{};
    assemblyEditor_->SetText(editorText);
    assemblyEditor_->ClearMarkers();
    observedAssemblyEditorText_ = assemblyEditor_->GetText();
    editorDiagnostics_.clear();
    clearEditorRecovery();
    if (editor.present && editor.draft) markAssemblyEditorChanged();
    assemblyEditor_->SetCursor(editor.line, editor.column);
    updateEditorSourceMatch();
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
