// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"
#include "emu51_editor_files.h"
#include "TextEditor.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace {
namespace fs = std::filesystem;
int failures = 0;
void check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        ++failures;
    }
}
std::string read(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}
void write(const fs::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary);
    output << text;
}
} // namespace

struct Emu51ImguiAppTestAccess {
    using Choice = Emu51ImguiApp::UnsavedChoice;
    static std::unique_ptr<Emu51ImguiApp> create()
    {
        return std::unique_ptr<Emu51ImguiApp>(new Emu51ImguiApp(false));
    }
    static void edit(Emu51ImguiApp& app, const fs::path& path, const std::string& source)
    {
        app.assemblyEditor_->SetText(source);
        app.assemblyEditorState_.sourcePath = path.string();
        app.assemblyEditorState_.open = true;
        app.markAssemblyEditorChanged();
    }
    static void test_unedited_sources_close_without_prompt(const fs::path& root)
    {
        const std::string sources[] = {
            "mov a,#42h\nend\n",
            "mov a,#42h\r\nend\r\n",
            "\tmov a,#42h\n\tend\n",
            "\tmov a,#42h\r\n\tend\r\n",
            "\xef\xbb\xbf\tmov a,#42h\r\n\tend\r\n",
            "mov a,#42h\nend",
            "",
        };
        for (const auto& source : sources) {
            const fs::path path = root / "unedited.asm";
            write(path, source);
            auto app = create();
            int prompts = 0;
            app->unsavedChangesPrompt_ = [&] { ++prompts; return Choice::Cancel; };
            app->requestProgramLoadFromPath(path, true);
            // Match the first rendered frame and subsequent quit-time checks.
            app->trackAssemblyEditorChanges();
            app->trackAssemblyEditorChanges();
            check(!app->assemblyEditorState_.dirty && !app->recoverySavePending_,
                "Loading source with normalized whitespace does not mark it dirty or schedule recovery");
            app->requestQuit();
            check(app->consumeQuitRequested() && prompts == 0,
                "Unedited LF/CRLF, tabbed, BOM, empty and unterminated sources close without prompting");
            check(read(path) == source, "Closing unedited source preserves its original bytes");
        }

        const fs::path sample = fs::path(EMU51_REPO_ROOT) / "progs/asm/demo/mtask2.asm";
        const std::string original = read(sample);
        auto app = create();
        int prompts = 0;
        app->unsavedChangesPrompt_ = [&] { ++prompts; return Choice::Cancel; };
        app->requestProgramLoadFromPath(sample, true);
        check(app->hasLoadedProgram(), "Repository MTask2 sample opens and assembles successfully");
        app->trackAssemblyEditorChanges();
        app->requestQuit();
        check(app->consumeQuitRequested() && prompts == 0,
            "Repository MTask2 sample closes without prompting when unedited");
        check(read(sample) == original, "Opening and closing MTask2 does not rewrite the sample");
    }
    static void test_unsaved_changes(const fs::path& root)
    {
        const fs::path original = root / "original.asm";
        const fs::path next = root / "next.asm";
        const std::string edits = "mov a,#42h\nend\n";
        write(original, "nop\nend\n");
        write(next, "mov a,#12h\nend\n");
        auto app = create();
        edit(*app, original, edits);
        int prompts = 0;
        app->unsavedChangesPrompt_ = [&] { ++prompts; return Choice::Cancel; };
        app->requestProgramLoadFromPath(next, true);
        check(app->assemblyEditor_->GetText() == edits && app->assemblyEditorState_.dirty,
            "Cancel opening another source preserves the dirty editor");
        app->requestQuit();
        check(!app->consumeQuitRequested() && prompts == 2,
            "Cancel prevents quitting and opening through the same guard");
        check(read(original) == "nop\nend\n", "Cancel does not save over the original");

        edit(*app, {}, edits);
        app->unsavedChangesPrompt_ = [] { return Choice::Save; };
        app->savePathPicker_ = [] { return fs::path{}; };
        app->requestProgramLoadFromPath(next, true);
        app->requestQuit();
        check(!app->consumeQuitRequested() && app->assemblyEditor_->GetText() == edits &&
            app->assemblyEditorState_.dirty, "Canceling Save As cancels the dependent action");

        app->savePathPicker_ = [&] { return root / "missing-directory" / "source.asm"; };
        check(!app->prepareToQuit() && app->assemblyEditorState_.dirty,
            "A failed save keeps the editor dirty and prevents quitting");

        app->savePathPicker_ = [&] { return original; };
        app->requestProgramLoadFromPath(next, true);
        check(read(original) == edits, "Save before opening preserves all edits on disk");
        check(app->assemblyEditor_->GetText() == read(next) && !app->assemblyEditorState_.dirty,
            "Opening proceeds only after the save succeeds");

        edit(*app, original, edits);
        app->unsavedChangesPrompt_ = [&] { ++prompts; return Choice::Discard; };
        const int previousPrompts = prompts;
        app->requestProgramLoadFromPath(root / "missing.asm", true);
        check(app->assemblyEditor_->GetText() == edits && app->assemblyEditorState_.dirty,
            "A failed source read preserves edits even after choosing Discard");
        check(prompts == previousPrompts + 1, "Source replacement asks before discarding");
        fs::create_directory(root / "directory.asm");
        app->requestProgramLoadFromPath(root / "directory.asm", true);
        check(app->assemblyEditor_->GetText() == edits && app->assemblyEditorState_.dirty,
            "Opening a directory cannot replace the editor buffer with empty text");
        app->requestProgramLoadFromPath(next, true);
        check(app->assemblyEditor_->GetText() == read(next), "Discard allows source replacement");

        edit(*app, original, edits);
        app->setAssemblyEditorVisible(false);
        app->unsavedChangesPrompt_ = [] { return Choice::Cancel; };
        check(!app->prepareToQuit(), "Hidden editor buffers are still protected on quit");
        app->unsavedChangesPrompt_ = [&] { ++prompts; return Choice::Save; };
        const int beforeQuit = prompts;
        app->requestQuit();
        check(app->consumeQuitRequested() && !app->assemblyEditorState_.dirty && read(original) == edits,
            "Save and quit saves a hidden dirty buffer");
        check(app->prepareToQuit() && prompts == beforeQuit + 1,
            "Native termination after an approved quit does not prompt twice");

        auto discardApp = create();
        edit(*discardApp, original, "unsaved\n");
        discardApp->unsavedChangesPrompt_ = [] { return Choice::Discard; };
        discardApp->requestQuit();
        check(discardApp->consumeQuitRequested() && read(original) == edits,
            "Discard and quit leaves the saved source unchanged");
    }

    static void test_transactional_loading(const fs::path& root)
    {
        auto app = create();
        const fs::path valid = root / "valid.asm";
        write(valid, "org 100h\nmov a,#42h\nend\n");
        check(app->loadProgram(valid.string().c_str(), true, true), "Valid assembly loads");
        app->loadedProgramBinaryAddress_ = 0x100;
        app->ramMemory_.fill(0x11);
        app->romMemory_.fill(0x22);
        app->unifiedMemory_.fill(0x33);
        app->cpu_.PC = 0x1234;
        app->cpu_.Acc = 0x56;
        app->cpu_.SP = 0x37;
        app->running_ = true;
        app->instructionCount_ = 42;
        app->cycleCount_ = 123;
        const auto ram = app->ramMemory_;
        const auto rom = app->romMemory_;
        const auto unified = app->unifiedMemory_;
        std::array<std::uint8_t, 256> sfr;
        std::copy(std::begin(app->cpu_.SFR), std::end(app->cpu_.SFR), sfr.begin());
        const auto loadedPath = app->loadedProgramPath_;
        const auto recent = app->recentProgramPaths_;
        const fs::path badHex = root / "bad.hex";
        const fs::path badAsm = root / "bad.asm";
        const fs::path badBin = root / "bad.bin";
        write(badHex, ":0100000042BD\n:bad-record\n");
        write(badAsm, "unknown_instruction\nend\n");
        write(badBin, std::string(65537, 'x'));
        for (const auto& path : {badHex, badAsm, badBin, root / "absent.hex"}) {
            check(!app->loadProgram(path.string().c_str(), true, true), "Invalid input fails to load");
            check(app->ramMemory_ == ram && app->romMemory_ == rom && app->unifiedMemory_ == unified,
                "Failed Open preserves every memory space");
            check(std::equal(sfr.begin(), sfr.end(), std::begin(app->cpu_.SFR)) &&
                app->cpu_.PC == 0x1234 && app->running_ && app->instructionCount_ == 42 && app->cycleCount_ == 123,
                "Failed Open preserves CPU, execution state and counters");
            check(app->loadedProgramPath_ == loadedPath && app->loadedProgramBinaryAddress_ == 0x100 &&
                app->recentProgramPaths_ == recent, "Failed Open preserves reload and recent-file metadata");
        }
        write(badBin, "12");
        check(!app->loadProgram(badBin.string().c_str(), true, true, 0xffff) && app->unifiedMemory_ == unified,
            "A binary that does not fit cannot reset the machine");
        app->reloadProgram();
        check(app->cpu_.PC == 0 && !app->running_ && app->instructionCount_ == 0 && app->cycleCount_ == 0 &&
            app->unifiedMemory_[0x100] == 0x74 && app->unifiedMemory_[0x101] == 0x42 &&
            app->unifiedMemory_[0x200] == 0, "Successful reload still resets before committing the staged image");
        app->cpu_.PC = 0x2468;
        app->cpu_.Acc = 0x99;
        app->unifiedMemory_[0x200] = 0xab;
        app->running_ = true;
        check(app->loadProgram(valid.string().c_str(), false, false) && app->cpu_.PC == 0x2468 &&
            app->cpu_.Acc == 0x99 && app->running_ && app->unifiedMemory_[0x200] == 0xab,
            "Load without reset preserves CPU state and bytes outside the image");
        app->setUnifiedMode(false);
        app->ramMemory_[0x100] = 0xde;
        check(app->loadProgram(valid.string().c_str(), false, false) && app->romMemory_[0x100] == 0x74 &&
            app->ramMemory_[0x100] == 0xde, "Staged loads respect separate program and data spaces");
    }

    static void test_undo_redo_after_save(const fs::path& root)
    {
        auto app = create();
        const fs::path path = root / "undo.asm";
        write(path, "nop\n");
        check(app->loadAssemblyFileIntoEditor(path), "Undo test source opens");
        app->assemblyEditor_->ReplaceSectionText(0, 0, 0, 3, "inc a");
        check(app->saveAssemblyEditorFileToPath(path) && !app->assemblyEditorState_.dirty,
            "Edited text is saved before Undo");
        app->unsavedChangesPrompt_ = [] { return Choice::Cancel; };
        app->assemblyEditor_->Undo();
        check(!app->prepareToQuit() && app->assemblyEditorState_.dirty &&
            app->assemblyEditor_->GetText() == "nop\n", "Undo immediately after Save cannot bypass the quit guard");
        check(app->saveAssemblyEditorFileToPath(path), "Undone text is saved before Redo");
        app->assemblyEditor_->Redo();
        check(!app->prepareToQuit() && app->assemblyEditorState_.dirty &&
            app->assemblyEditor_->GetText() == "inc a\n", "Redo immediately after Save cannot bypass the quit guard");
    }

    static void test_disassembly_follow_pc_persistence()
    {
        auto app = create();
        app->disassemblyViews_.front().open = false;
        app->addDisassemblyView(0, true);
        app->addDisassemblyView(0, false);
        const int followingId = app->disassemblyViews_[1].id;
        const int fixedId = app->disassemblyViews_[2].id;
        const Emu51ImguiApp::PaneKey pane{Emu51ImguiApp::PaneKey::Kind::Disassembly, fixedId};
        app->setPaneSnapTarget(pane, Emu51ImguiSnapTarget::Left);
        auto& floating = app->ensureFloatingPaneState(pane);
        floating.size = ImVec2(300, 400);
        floating.hasSize = true;
        std::ostringstream saved;
        app->saveLayoutState(saved);

        auto restored = create();
        std::istringstream input(saved.str());
        restored->loadLayoutState(input);
        check(restored->disassemblyViews_.size() == 2 &&
            restored->disassemblyViews_[0].id == followingId && restored->disassemblyViews_[0].followPc &&
            restored->disassemblyViews_[1].id == fixedId && !restored->disassemblyViews_[1].followPc,
            "Layout restores each open disassembly window's Follow PC setting by ID");
        const auto* restoredFloating = restored->findFloatingPaneState(pane);
        check(restored->snapTargetForPane(pane) == Emu51ImguiSnapTarget::Left &&
            restoredFloating && restoredFloating->size.x == 300 && restoredFloating->size.y == 400,
            "Follow PC persistence preserves the existing pane placement and size");
        restored->addDisassemblyView(0, true);
        check(restored->disassemblyViews_.back().id > fixedId,
            "New disassembly windows do not reuse restored IDs");
        restored->disassemblyViews_[0].followPc = false;
        restored->disassemblyViews_[1].followPc = true;
        std::ostringstream changed;
        restored->saveLayoutState(changed);
        auto reopened = create();
        std::istringstream changedInput(changed.str());
        reopened->loadLayoutState(changedInput);
        check(!reopened->disassemblyViews_[0].followPc && reopened->disassemblyViews_[1].followPc,
            "Both enabling and disabling Follow PC survive another save and reload");

        for (const std::string flag : {"", " invalid", " 2"}) {
            auto legacy = create();
            std::istringstream legacyInput("pane disassembly 1 none 1 230 400" + flag + "\n");
            legacy->loadLayoutState(legacyInput);
            check(legacy->disassemblyViews_.size() == 1 && legacy->disassemblyViews_[0].followPc,
                "Older layouts and invalid flags retain the default Follow PC behavior");
        }
    }

    static void test_in_memory_compilation()
    {
        auto first = create();
        auto second = create();
        edit(*first, {}, "org 100h\nmov a,#11h\nend\n");
        edit(*second, {}, "org 100h\nmov a,#22h\nend\n");
        first->compileAssemblyEditor();
        second->compileAssemblyEditor();
        check(first->hasCompiledAssemblyEditorOutput() && second->hasCompiledAssemblyEditorOutput(),
            "Two untitled editors compile independently");
        first->cpu_.PC = 0x1234;
        first->unifiedMemory_[0x200] = 0xab;
        first->loadCompiledAssemblyEditorOutput();
        second->loadCompiledAssemblyEditorOutput();
        check(first->unifiedMemory_[0x101] == 0x11 && second->unifiedMemory_[0x101] == 0x22,
            "Compiled output cannot be overwritten by another editor instance");
        check(first->cpu_.PC == 0x1234 && first->unifiedMemory_[0x200] == 0xab,
            "Loading an in-memory image preserves the running machine outside the image");
        edit(*first, {}, "org 100h\nmov a,#33h\nend\n");
        first->loadCompiledAssemblyEditorOutput();
        check(!first->hasCompiledAssemblyEditorOutput() && first->unifiedMemory_[0x101] == 0x11,
            "Edited source cannot load a stale image");
        first->compileAndLoadAssemblyEditor();
        check(first->unifiedMemory_[0x101] == 0x33 && first->unifiedMemory_[0x200] == 0 && first->cpu_.PC == 0,
            "Compile and load resets only after successful assembly");
        first->unifiedMemory_[0x200] = 0xcd;
        edit(*first, {}, "invalid_instruction\n");
        first->compileAndLoadAssemblyEditor();
        check(!first->hasCompiledAssemblyEditorOutput() && first->unifiedMemory_[0x200] == 0xcd,
            "Compile errors invalidate the image without resetting memory");
    }

    static void test_debugger(const fs::path& root)
    {
        auto app = create();
        const fs::path source = root / "debug.asm";
        write(source, "mov a,#1\nloop: inc a\nsjmp loop\nend\n");
        check(app->loadProgram(source.string().c_str(), true, true), "Debugger source loads");
        check(app->sourceLines_.at(2).file == source.string() && app->sourceLines_.at(2).line == 2,
            "Assembler emits address-to-source mappings");
        app->toggleBreakpoint(2);
        app->toggleRun();
        app->runSteps(100);
        check(!app->running_ && app->cpu_.PC == 2 && app->cpu_.Acc == 1 && app->instructionCount_ == 1,
            "A breakpoint stops before executing its instruction");
        app->toggleRun(); app->runSteps(100);
        check(!app->running_ && app->cpu_.PC == 2 && app->cpu_.Acc == 2,
            "Resume skips the stopped breakpoint once and stops on its next visit");
        app->runToAddress(3); app->runSteps(100);
        check(!app->running_ && app->cpu_.PC == 3 && !app->runToAddress_,
            "Run to cursor stops before its target and clears the temporary target");

        app->breakpoints_.clear(); app->unifiedMemory_.fill(0); app->cpu_.PC = 0;
        app->traceEnabled_ = true; app->running_ = true;
        app->runSteps(static_cast<int>(app->kTraceCapacity + 10));
        check(app->trace_.size() == app->kTraceCapacity && app->trace_.front().pc == 10 &&
            app->trace_.back().cycles == 1, "Instruction traces retain only the bounded most recent entries");

        const std::array<std::uint8_t, 9> program{0x90,0x40,0x00,0x74,0x55,0xf0,0xe0,0x80,0xfe};
        std::copy(program.begin(), program.end(), app->unifiedMemory_.begin());
        app->cpu_.PC = 0;
        app->watchpoints_ = {{Emu51ImguiApp::WatchSpace::Data, 0x4000, false, true, true}};
        app->running_ = true; app->runSteps(100);
        check(!app->running_ && app->cpu_.PC == 6 && app->unifiedMemory_[0x4000] == 0x55,
            "Data write watchpoints stop after the triggering instruction");
        app->watchpoints_ = {{Emu51ImguiApp::WatchSpace::Data, 0x4000, true, false, true}};
        app->toggleRun(); app->runSteps(100);
        check(!app->running_ && app->cpu_.PC == 7 && app->cpu_.Acc == 0x55,
            "Data read watchpoints stop after MOVX reads");
        app->watchpoints_ = {{Emu51ImguiApp::WatchSpace::Internal, 0xe0, false, true, true}};
        app->cpu_.PC = 3; app->cpu_.Acc = 0;
        app->running_ = true; app->runSteps(100);
        check(!app->running_ && app->cpu_.PC == 5 && app->cpu_.Acc == 0x55,
            "Internal RAM/SFR watchpoints detect value changes");
        app->watchpoints_ = {{Emu51ImguiApp::WatchSpace::Program, 4, true, false, true}};
        app->cpu_.PC = 3; app->running_ = true; app->pendingWatchpointStop_.clear();
        app->cpu_.readProg(4);
        check(app->pendingWatchpointStop_.empty(), "UI/program inspection does not trigger watchpoints");
        app->runSteps(100);
        check(!app->running_ && app->cpu_.PC == 5,
            "Program reads during execution trigger program watchpoints");

        write(root / "debug.inc", "entry: inc a\nret\n");
        write(source, "org 100h\nlcall entry\n$include(debug.inc)\nend\n");
        check(app->loadProgram(source.string().c_str(), true, true) &&
            app->sourceLines_.at(0x103).file == (root / "debug.inc").string() &&
            app->sourceLines_.at(0x103).line == 1, "Source mappings preserve included file locations");
        app->cpu_.writeProg(0x103, 0);
        check(!app->sourceLines_.contains(0x103), "Program writes invalidate modified source locations");
        app->stepIn();
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280, 800);
        io.DeltaTime = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        io.Fonts->Build();
        ImGui::NewFrame();
        app->showDebugger();
        app->drawDebugger();
        app->drawDisassemblyView(app->disassemblyViews_.front());
        ImGui::Render();
    }

    static void test_execution_servicing()
    {
        using Clock = Emu51CycleTiming::Clock;
        const auto start = Clock::time_point(std::chrono::seconds(1000));
        for (int services : {30, 60, 240}) {
            auto app = create();
            app->cycleExact_ = false; app->stepsPerFrame_ = 10; app->running_ = true;
            app->cycleTiming_.reset(start);
            for (int i = 1; i <= services; ++i)
                app->tickAt(start + std::chrono::nanoseconds(1'000'000'000LL * i / services));
            check(app->instructionCount_ == 600,
                "Instruction pacing is independent of redraw and service frequency");
        }
        auto app = create();
        app->running_ = true; app->cycleTiming_.reset(start);
        app->tickAt(start + std::chrono::milliseconds(1));
        check(app->cycleCount_ == 1000, "Cycle-exact execution can be serviced without rendering");
        app->stopForDebugger("test");
        app->cpu_.PC = 0;
        app->unifiedMemory_[0] = 0x12; app->unifiedMemory_[1] = 0; app->unifiedMemory_[2] = 0x10;
        app->unifiedMemory_[0x10] = 0x22;
        const auto before = app->instructionCount_;
        app->stepOver();
        check(app->running_ && app->stepTask_ && app->instructionCount_ == before,
            "Step Over starts a task without blocking the input handler");
        app->tick();
        check(!app->running_ && !app->stepTask_ && app->cpu_.PC == 3 && app->cpu_.SP == 7,
            "Step Over completes when its return address and stack depth match");
        app->cpu_.PC = 0; app->unifiedMemory_[0x10] = 0x80; app->unifiedMemory_[0x11] = 0xfe;
        app->stepOver(); app->tick();
        check(app->running_ && app->stepTask_ && app->stepTask_->instructions > 0 &&
            app->stepTask_->instructions < 1000000, "A non-returning call yields before the global instruction limit");
        app->toggleRun();
        check(!app->running_ && !app->stepTask_, "Pause cancels a pending step task");
        app->cpu_.PC = 0; app->cpu_.SP = 7; app->toggleBreakpoint(0x10);
        app->stepOver(); app->tick();
        check(!app->running_ && !app->stepTask_ && app->cpu_.PC == 0x10,
            "Breakpoints interrupt and cancel Step Over tasks");
        app->breakpoints_.clear(); app->cpu_.PC = 0x20; app->unifiedMemory_[0x20] = 0x22;
        app->cpu_.SP = 0; app->cpu_.SFR[0] = 0x12; app->cpu_.SFR[255] = 0x34;
        app->stepOut(); app->tick();
        check(!app->running_ && !app->stepTask_ && app->cpu_.PC == 0x1234 && app->cpu_.SP == 0xfe,
            "Step Out handles a wrapped stack using its return address and depth");
    }

    static void test_recovery_integration(const fs::path& root)
    {
        auto app = create();
        app->editorRecovery_ = Emu51EditorRecovery::create(root / "recovery");
        app->persistState_ = true;
        // Do not write actual user preferences from this test instance.
        const fs::path directory = app->editorRecovery_->directory();
        edit(*app, root / "recovered.asm", "mov a,#55h\n");
        app->saveEditorRecovery();
        const auto draft = app->editorRecovery_->load();
        check(draft && draft->text == app->assemblyEditor_->GetText(), "Dirty editor produces a recovery draft");
        app->unsavedChangesPrompt_ = [] { return Choice::Cancel; };
        check(!app->prepareToQuit() && fs::exists(directory / "draft"), "Cancel keeps the recovery draft");
        app->unsavedChangesPrompt_ = [] { return Choice::Save; };
        check(app->prepareToQuit() && !fs::exists(directory / "draft"), "Successful save removes the recovery draft");
        app->persistState_ = false;

        const fs::path recoveryRoot = root / "abandoned-recovery";
        auto abandoned = Emu51EditorRecovery::create(recoveryRoot);
        const fs::path oldDirectory = abandoned->directory();
        check(abandoned->save({(root / "old.asm").string(), "old.asm", "\tmov a,#66h\r\n"}),
            "Abandoned recovery draft is written");
        abandoned.reset();
        auto active = Emu51EditorRecovery::create(recoveryRoot);
        check(active->save({"", "active.asm", "active buffer"}), "Active session draft is written");
        auto fresh = create();
        fresh->editorRecovery_ = Emu51EditorRecovery::create(recoveryRoot);
        int prompts = 0;
        fresh->recoveryPrompt_ = [&](const std::string& name) {
            check(name == "old.asm", "Recovery skips drafts owned by active sessions");
            ++prompts;
            return Emu51ImguiApp::RecoveryChoice::Keep;
        };
        fresh->recoverAssemblyEditor(recoveryRoot);
        check(prompts == 1 && !fresh->assemblyEditorState_.dirty && fs::exists(oldDirectory / "draft"),
            "Cancel recovery keeps the abandoned draft without replacing the editor");
        fresh->recoveryPrompt_ = [](const std::string&) { return Emu51ImguiApp::RecoveryChoice::Restore; };
        fresh->recoverAssemblyEditor(recoveryRoot);
        check(fresh->assemblyEditor_->GetText() == "    mov a,#66h\n" && fresh->assemblyEditorState_.dirty &&
            fresh->assemblyEditorState_.sourcePath == (root / "old.asm").string() &&
            fresh->editorRecovery_->directory() == oldDirectory, "Recovery restores text, source path and dirty state");
        fresh->trackAssemblyEditorChanges();
        check(!fresh->recoverySavePending_, "Normalizing a restored draft does not count as a new edit");
        fresh->unsavedChangesPrompt_ = [] { return Choice::Discard; };
        check(fresh->prepareToQuit() && !fs::exists(oldDirectory / "draft"),
            "Discarding a recovered buffer removes its draft");

        auto unwanted = Emu51EditorRecovery::create(recoveryRoot);
        const fs::path unwantedDirectory = unwanted->directory();
        unwanted->save({"", "unwanted.asm", "unwanted"});
        unwanted.reset();
        auto discard = create();
        discard->recoveryPrompt_ = [](const std::string&) { return Emu51ImguiApp::RecoveryChoice::Discard; };
        discard->recoverAssemblyEditor(recoveryRoot);
        check(!fs::exists(unwantedDirectory / "draft") && !discard->assemblyEditorState_.dirty,
            "Discard recovery deletes an abandoned draft without loading it");
        active->clear();
    }
};

int main()
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    const fs::path root = fs::temp_directory_path() /
        ("emu51-ui-tests-" + std::to_string(Emu51CycleTiming::Clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    Emu51ImguiAppTestAccess::test_unedited_sources_close_without_prompt(root);
    Emu51ImguiAppTestAccess::test_unsaved_changes(root);
    Emu51ImguiAppTestAccess::test_transactional_loading(root);
    Emu51ImguiAppTestAccess::test_undo_redo_after_save(root);
    Emu51ImguiAppTestAccess::test_disassembly_follow_pc_persistence();
    Emu51ImguiAppTestAccess::test_in_memory_compilation();
    Emu51ImguiAppTestAccess::test_debugger(root);
    Emu51ImguiAppTestAccess::test_execution_servicing();
    Emu51ImguiAppTestAccess::test_recovery_integration(root);
    fs::remove_all(root);
    ImGui::DestroyContext();
    if (failures)
        return 1;
    std::puts("emu51 application workflow tests passed");
    return 0;
}
