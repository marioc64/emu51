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
        check(app->sourceLines_.at(2).file == fs::weakly_canonical(source).string() && app->sourceLines_.at(2).line == 2,
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
            app->sourceLines_.at(0x103).file == fs::weakly_canonical(root / "debug.inc").string() &&
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

    static void test_symbolic_debugging(const fs::path& root)
    {
        const fs::path source = root / "symbols.asm";
        const fs::path included = root / "symbols.inc";
        const std::string text = "counter data 30h\nflag bit 7\nready bit 90h\nlimit equ 1234h\n"
            "org 100h\nstart: mov a,#1\nloop: inc a\nsjmp loop\n$include(symbols.inc)\nend\n";
        write(source, text);
        write(included, "helper: ret\n");
        auto app = create();
        app->requestProgramLoadFromPath(source, true);
        check(app->resolveDebugAddress("  LoOp ") == 0x102 &&
            app->resolveDebugAddress("counter") == 0x30 && app->resolveDebugAddress("helper") == 0x105,
            "File loading preserves case-insensitive symbols from the source and includes");
        check(app->resolveDebugAddress("0x1234") == 0x1234 && app->resolveDebugAddress("ffff") == 0xffff &&
            !app->resolveDebugAddress("10000") && !app->resolveDebugAddress("missing") &&
            !app->resolveDebugAddress("loop+1"), "Symbol lookup preserves bounded hex input and rejects unknown expressions");
        check(app->labelAtAddress(0x102) == "LOOP" && app->labelAtAddress(0x30).empty(),
            "Disassembly labels use program labels rather than data symbols at the same numeric address");
        app->cpu_.SFR[0x30] = 0xab;
        app->cpu_.SFR[0x20] = 0x80;
        app->cpu_.SFR[0x90] = 1;
        check(app->symbolValue("COUNTER") == 0xab && app->symbolValue("FLAG") == 1 &&
            app->symbolValue("READY") == 1 && app->symbolValue("LIMIT") == 0x1234 &&
            app->symbolValue("START") == 0x74 && !app->symbolValue("MISSING"),
            "Symbol watches distinguish RAM, bit RAM, SFR bits, constants and program bytes");
        app->instructionExecuting_ = true;
        app->watchpoints_ = {{Emu51ImguiApp::WatchSpace::Program, 0x100, true, false, true}};
        (void)app->symbolValue("START");
        check(app->pendingWatchpointStop_.empty(), "Named value inspection does not trigger watchpoints");
        app->instructionExecuting_ = false;
        app->watchpoints_.clear();
        app->trackAssemblyEditorChanges();
        check(app->editorMatchesLoadedSource_ && app->editorLineAddress(6) == 0x100 &&
            app->editorLineAddress(7) == 0x102 && !app->editorLineAddress(5),
            "Source gutter resolves instruction starts and ignores directives");
        app->toggleEditorBreakpoint(7);
        app->cpu_.PC = 0x100; app->toggleRun(); app->runSteps(100);
        check(!app->running_ && app->cpu_.PC == 0x102 && app->cpu_.Acc == 1,
            "An editor gutter breakpoint stops at the corresponding loaded instruction");
        app->toggleEditorBreakpoint(7);
        app->assemblyEditor_->SetCursor(7, 0);
        app->runToEditorCursor(); app->runSteps(100);
        check(!app->running_ && app->cpu_.PC == 0x103,
            "Run to editor cursor uses the instruction start rather than an operand byte");

        app->assemblyEditor_->SetText("; inserted line\n" + text);
        app->trackAssemblyEditorChanges();
        check(!app->editorMatchesLoadedSource_ && !app->editorLineAddress(7),
            "Editing source disables stale gutter locations immediately");
        check(app->saveAssemblyEditorFileToPath(source), "Changed source saves successfully");
        app->runToEditorCursor();
        check(!app->running_ && !app->runToAddress_ && !app->editorMatchesLoadedSource_,
            "Saving changed source does not make old debug locations valid again");
        app->showSource(0x102);
        check(app->statusMessage_.find("differs") != std::string::npos,
            "Show source explains a mismatch instead of navigating to an obsolete line");
        app->compileAndLoadAssemblyEditor();
        check(app->editorMatchesLoadedSource_ && app->editorLineAddress(8) == 0x102,
            "Compile and load replaces snapshots and enables current source locations");

        write(included, "; changed on disk\nhelper: ret\n");
        app->showSource(0x105);
        check(!app->editorMatchesLoadedSource_ && app->statusMessage_.find("differs") != std::string::npos,
            "Included files changed on disk cannot silently reuse loaded source locations");
        write(included, "helper: ret\n");
        check(app->loadAssemblyFileIntoEditor(included), "Original include can be reopened");
        app->trackAssemblyEditorChanges();
        check(app->editorLineAddress(1) == 0x105, "Matching include files support source gutter breakpoints");
        check(app->loadAssemblyFileIntoEditor(source), "Main source can be reopened");
        app->trackAssemblyEditorChanges();
        app->cpu_.writeProg(0x101, 2);
        check(!app->sourceLines_.contains(0x100) && !app->sourceLines_.contains(0x101) &&
            !app->editorLineAddress(7) && app->editorLineAddress(8) == 0x102,
            "Editing an operand invalidates its whole instruction while preserving unaffected locations");
        app->symbolWatches_.insert("COUNTER");
        const auto previous = app->resolveDebugAddress("loop");
        check(!app->loadProgram((root / "absent.asm").string().c_str(), true, true) &&
            app->resolveDebugAddress("loop") == previous,
            "Failed program preparation preserves loaded symbols");
        const fs::path binary = root / "symbols.bin";
        write(binary, std::string(1, '\0'));
        check(app->loadProgram(binary.string().c_str(), false, false) && app->loadedSymbols_.empty() &&
            app->sourceAddresses_.empty() && app->loadedSources_.empty() && !app->symbolValue("COUNTER") &&
            app->symbolWatches_.contains("COUNTER"),
            "Overlaying a binary clears stale debug metadata and leaves watches visibly unresolved");

        edit(*app, {}, "org 200h\nnew_label: nop\nend\n");
        app->compileAndLoadAssemblyEditor();
        check(app->resolveDebugAddress("new_label") == 0x200 && app->editorMatchesLoadedSource_ &&
            app->editorLineAddress(2) == 0x200 && !app->resolveDebugAddress("loop"),
            "Unsaved editor builds carry their own symbols and source snapshots");
        app->clearMemory();
        check(app->loadedSymbols_.empty() && !app->editorLineAddress(2),
            "Clearing program memory clears symbolic debug metadata");
        write(source, "\xef\xbb\xbf\torg 300h\r\n\tstart: nop\r\nend\r\n");
        app->unsavedChangesPrompt_ = [] { return Choice::Discard; };
        app->requestProgramLoadFromPath(source, true);
        app->trackAssemblyEditorChanges();
        check(app->editorMatchesLoadedSource_ && app->editorLineAddress(2) == 0x300,
            "BOM, CRLF and tab normalization do not cause false source mismatches");
        edit(*app, {}, "org 0ffffh\nboundary: mov a,#1\nend\n");
        app->compileAndLoadAssemblyEditor();
        check(app->editorLineAddress(2) == 0xffff,
            "A wrapped instruction maps its source line to the opcode rather than its lower-address operand");
        app->cpu_.writeProg(0, 2);
        check(!app->editorLineAddress(2) && !app->sourceLines_.contains(0xffff),
            "Operand edits invalidate wrapped instructions as well");
        edit(*app, {}, "face: nop\nend\n");
        app->compileAndLoadAssemblyEditor();
        check(app->resolveDebugAddress("face") == 0 && app->resolveDebugAddress("0xface") == 0xface,
            "Symbol names take precedence over bare hex; explicit hex remains unambiguous");
    }

    static void test_project_sessions(const fs::path& root)
    {
        const auto folder = root / "project folder";
        fs::create_directory(folder);
        const auto source = folder / "main.asm";
        const auto binary = folder / "overlay.bin";
        const auto editor = folder / "edit.asm";
        const auto project = folder / "demo.emu51";
        write(source, "org 100h\nbase: mov a,#1\nend\n");
        write(binary, std::string(1, static_cast<char>(0xa5)));
        const std::string built = "counter data 30h\norg 200h\nentry: inc counter\nret\nend\n";
        const std::string draft = "; draft with quotes \" and backslash \\\n" + built;
        write(editor, built);
        auto app = create();
        app->setUnifiedMode(false);
        app->requestProgramLoadFromPath(source, true);
        check(app->loadProgram(binary.string().c_str(), false, false, 0x400), "Project binary overlay loads");
        edit(*app, editor, built);
        app->compileAssemblyEditor();
        app->loadCompiledAssemblyEditorOutput();
        edit(*app, editor, draft);
        app->breakpoints_ = {0x100, 0x200};
        app->watchpoints_ = {{Emu51ImguiApp::WatchSpace::Data, 0x4567, true, true, false},
            {Emu51ImguiApp::WatchSpace::Internal, 0x30, false, true, true}};
        app->symbolWatches_ = {"COUNTER", "MISSING"};
        app->cycleExact_ = false; app->crystalMHz_ = 11.0592; app->stepsPerFrame_ = 123;
        app->debuggerOpen_ = true; app->traceEnabled_ = true;
        app->assemblyEditor_->SetCursor(3, 2);
        app->cpu_.PC = 0x1234; app->running_ = true; app->instructionCount_ = 42;
        check(app->saveProjectToPath(project) && app->running_ && app->cpu_.PC == 0x1234 &&
            app->assemblyEditorState_.dirty && read(editor) == built,
            "Saving a project captures drafts without rewriting source files or stopping execution");
        std::string error;
        auto decoded = emu51LoadProject(project, error);
        check(decoded && decoded->programs.size() == 3 && decoded->programs[1].binaryAddress == 0x400 &&
            decoded->programs[2].fromEditor && decoded->programs[2].sourceText == built,
            "Projects retain file, binary overlay and editor build recipes in load order");
        const auto document = read(project);
        check(document.find("\"path\":\"main.asm\"") != std::string::npos &&
            document.find(folder.generic_string()) == std::string::npos,
            "Project files store portable relative paths");
        check(!app->saveProjectToPath(source) && read(source).find("org 100h") == 0,
            "Saving a project cannot overwrite a referenced program source");

        const auto moved = root / "relocated project";
        fs::rename(folder, moved);
        auto restored = create();
        check(restored->openProjectFromPath(moved / "demo.emu51"), "Projects reopen after moving their folder");
        check(!restored->unifiedMode_ && !restored->running_ && restored->cpu_.PC == 0 &&
            restored->instructionCount_ == 0 && restored->cycleCount_ == 0 && restored->cpu_.readProg(0x100) == 0x74 &&
            restored->cpu_.readProg(0x400) == 0xa5 && restored->cpu_.readProg(0x200) == 0x05 &&
            restored->cpu_.readData(0x100) == 0,
            "Project restoration rebuilds overlays in the selected memory mode and resets execution paused");
        check(!restored->cycleExact_ && restored->crystalMHz_ == 11.0592 && restored->stepsPerFrame_ == 123 &&
            restored->breakpoints_ == app->breakpoints_ && restored->watchpoints_.size() == 2 &&
            !restored->watchpoints_[0].enabled && restored->watchpoints_[0].read && restored->watchpoints_[0].write &&
            restored->symbolWatches_ == app->symbolWatches_ && restored->debuggerOpen_ && restored->traceEnabled_,
            "Project execution settings, breakpoints, watches and watchpoint flags round-trip");
        int line = 0, column = 0;
        restored->assemblyEditor_->GetMainCursor(line, column);
        check(restored->assemblyEditor_->GetText() == draft && restored->assemblyEditorState_.dirty &&
            restored->assemblyEditorState_.sourcePath == fs::weakly_canonical(moved / "edit.asm").string() && line == 3 && column == 2 &&
            restored->resolveDebugAddress("entry") == 0x200 && !restored->editorMatchesLoadedSource_,
            "Projects restore the editor draft and cursor independently of the last loaded assembly");
        check(restored->loadedProgramPath_ == fs::weakly_canonical(moved / "main.asm").string(),
            "Projects preserve the original reload target separately from overlays");

        restored->unsavedChangesPrompt_ = [] { return Choice::Cancel; };
        restored->cpu_.PC = 0x2345; restored->running_ = true; restored->cycleCount_ = 99;
        const auto memory = restored->romMemory_;
        const auto previousPath = restored->projectPath_;
        check(!restored->openProjectFromPath(moved / "demo.emu51") && restored->running_ &&
            restored->cpu_.PC == 0x2345 && restored->cycleCount_ == 99 && restored->romMemory_ == memory,
            "Canceling the unsaved-source guard preserves the live project");
        restored->projectPathPicker_ = [](bool) { return fs::path{}; };
        restored->saveProjectAs(); restored->requestOpenProject();
        check(restored->projectPath_ == previousPath && restored->running_,
            "Canceling either project picker leaves the current project selected");
        fs::rename(moved / "overlay.bin", moved / "missing.bin");
        check(!restored->openProjectFromPath(moved / "demo.emu51") && restored->romMemory_ == memory &&
            restored->cpu_.PC == 0x2345 && restored->running_ && restored->projectPath_ == previousPath,
            "Missing project inputs cannot reset the current CPU, memory or project path");
        fs::rename(moved / "missing.bin", moved / "overlay.bin");
        for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
                {"\"version\":1", "\"version\":99"}, {"\"crystal_mhz\":11.0592", "\"crystal_mhz\":-1"},
                {"\"binary_address\":1024", "\"binary_address\":65536"},
                {"\"space\":1", "\"space\":3"}, {"\"breakpoints\":[256,512]", "\"breakpoints\":[-1]"},
                {"\"version\":1", "\"version\":1,\"version\":1"}}) {
            std::string malformed = document;
            const auto at = malformed.find(from);
            check(at != std::string::npos, "Invalid-project fixture locates its target field");
            if (at == std::string::npos) continue;
            malformed.replace(at, from.size(), to);
            write(moved / "bad.emu51", malformed);
            check(!restored->openProjectFromPath(moved / "bad.emu51") && restored->cpu_.PC == 0x2345 &&
                restored->running_ && restored->romMemory_ == memory && restored->projectPath_ == previousPath,
                "Invalid versions, settings, addresses, watchpoints and duplicate keys cannot partially restore a project");
        }
        decoded = emu51LoadProject(moved / "demo.emu51", error);
        check(decoded.has_value(), "Moved project remains readable after validation errors");
        if (decoded) {
            decoded->editor.text.assign(1024 * 1024 + 1, 'x');
            check(!emu51SaveProject(moved / "demo.emu51", *decoded, error) && read(moved / "demo.emu51") == document,
                "Oversized project text fails without truncating the existing project file");
        }
        restored->unsavedChangesPrompt_ = [] { return Choice::Discard; };
        check(restored->openProjectFromPath(moved / "demo.emu51"), "A project remains usable after canceled and failed opens");

        auto untitled = create();
        untitled->untitledSourceBaseDirectory_ = moved.string();
        write(moved / "local.inc", "value equ 42\n");
        edit(*untitled, {}, "$include(local.inc)\nentry: mov a,#value\nend\n");
        untitled->compileAndLoadAssemblyEditor();
        check(untitled->saveProjectToPath(moved / "untitled.emu51"), "Untitled builds can be saved in projects");
        auto recovered = create();
        check(recovered->openProjectFromPath(moved / "untitled.emu51") && recovered->assemblyEditorState_.dirty &&
            recovered->assemblyEditorState_.sourcePath.empty() && recovered->editorMatchesLoadedSource_ &&
            recovered->cpu_.readProg(1) == 42,
            "Untitled editor builds restore symbols and relative include resolution");
        recovered->compileAndLoadAssemblyEditor();
        check(recovered->cpu_.readProg(1) == 42, "Restored untitled source can be rebuilt with its original include directory");

        auto clean = create();
        clean->requestProgramLoadFromPath(moved / "main.asm", true);
        int pickers = 0;
        clean->projectPathPicker_ = [&](bool save) {
            check(save, "First Save project requests a save picker");
            ++pickers;
            return moved / "clean.EMU51";
        };
        clean->saveProject();
        clean->stepsPerFrame_ = 321;
        clean->saveProject();
        check(pickers == 1 && fs::is_regular_file(moved / "clean.EMU51"),
            "Save project remembers its path and subsequent saves do not open a picker");
        write(moved / "main.asm", "org 100h\nbase: mov a,#9\nend\n");
        auto current = create();
        current->requestProgramLoadFromPath(moved / "clean.EMU51", true);
        check(current->cpu_.readProg(0x101) == 9 && current->stepsPerFrame_ == 321 &&
            !current->assemblyEditorState_.dirty && current->assemblyEditor_->GetText().find("#9") != std::string::npos,
            "Clean editor files and program inputs reopen from disk; project extensions are case-insensitive");
        edit(*current, moved / "main.asm", "org 100h\nbase: mov a,#7\nend\n");
        current->unsavedChangesPrompt_ = [] { return Choice::Save; };
        check(current->openProjectFromPath(moved / "clean.EMU51") && current->cpu_.readProg(0x101) == 7 &&
            current->assemblyEditor_->GetText().find("#7") != std::string::npos && !current->assemblyEditorState_.dirty,
            "Saving the old editor during project opening revalidates inputs before applying them");
        edit(*current, {}, "unsaved source\n");
        current->savePathPicker_ = [] { return fs::path{}; };
        current->cpu_.PC = 0x3456;
        check(!current->openProjectFromPath(moved / "clean.EMU51") && current->cpu_.PC == 0x3456 &&
            current->assemblyEditor_->GetText() == "unsaved source\n",
            "Canceling Save As in the unsaved guard also cancels project replacement");
    }

    static void test_recent_projects(const fs::path& root)
    {
        auto app = create();
        check(app->recentProjectFileCount() == 0 && app->recentProjectFilePath(-1).empty() &&
            app->recentProjectFilePath(0).empty(), "New sessions have an empty, bounds-checked recent projects list");
        for (int i = 0; i < 12; ++i)
            check(app->saveProjectToPath(root / ("recent-" + std::to_string(i) + ".emu51")),
                "Saving projects populates recent projects");
        check(app->recentProjectFileCount() == 10 &&
            fs::path(app->recentProjectFilePath(0)).filename() == "recent-11.emu51" &&
            fs::path(app->recentProjectFilePath(9)).filename() == "recent-2.emu51",
            "Recent projects retain the ten most recently used entries");
        const auto selected = app->recentProjectFilePath(3);
        app->openRecentProjectFile(3);
        check(app->projectPath_ == selected && app->recentProjectFilePath(0) == selected && app->recentProjectFileCount() == 10,
            "Opening a recent project selects it and moves its existing entry to the front");
        check(app->saveProjectToPath(selected) && app->recentProjectFilePath(0) == selected && app->recentProjectFileCount() == 10,
            "Saving the same project deduplicates its recent entry");
        std::ostringstream stored;
        app->saveRecentProjectPaths(stored);
        auto other = create();
        std::istringstream input(stored.str());
        other->loadRecentProjectPaths(input);
        check(other->recentProjectPaths_ == app->recentProjectPaths_, "Recent projects survive persistence round-trips");
        std::istringstream duplicates("\n" + selected + "\r\n" + selected + "\n" + stored.str());
        other->loadRecentProjectPaths(duplicates);
        check(other->recentProjectPaths_ == app->recentProjectPaths_,
            "Loading recent projects ignores blank and duplicate entries and accepts CRLF");
        const auto before = app->recentProjectPaths_;
        app->cpu_.PC = 0x1234;
        app->openRecentProjectFile(-1); app->openRecentProjectFile(10);
        check(app->recentProjectPaths_ == before && app->cpu_.PC == 0x1234,
            "Invalid recent-project indices do not change the live session");
        check(!app->openProjectFromPath(root / "missing-recent.emu51") && app->recentProjectPaths_ == before,
            "Failed project opens do not add to or reorder recent projects");
        check(!app->saveProjectToPath(root / "absent-folder" / "failed.emu51") && app->recentProjectPaths_ == before,
            "Failed saves do not add to recent projects");
        fs::remove(before[1]);
        app->openRecentProjectFile(1);
        check(app->cpu_.PC == 0x1234 && app->recentProjectPaths_ == before,
            "A missing recent project leaves the list and current machine intact");
        edit(*app, {}, "unsaved\n");
        app->unsavedChangesPrompt_ = [] { return Choice::Cancel; };
        app->openRecentProjectFile(2);
        check(app->cpu_.PC == 0x1234 && app->recentProjectPaths_ == before && app->assemblyEditorState_.dirty,
            "Recent project opening uses the unsaved-source guard and canceled opens preserve recent ordering");
        app->rememberRecentProgramPath(root / "program.hex");
        const auto programs = app->recentProgramPaths_;
        app->clearRecentProjectFiles();
        std::ostringstream cleared;
        app->saveRecentProjectPaths(cleared);
        check(app->recentProjectFileCount() == 0 && cleared.str().empty() && app->recentProgramPaths_ == programs &&
            app->projectPath_ == selected && app->assemblyEditorState_.dirty,
            "Clear recent projects persists an empty list without touching program recents, the current project or editor");
    }

    static void test_serial_terminal(const fs::path& root)
    {
        auto app = create();
        const auto sample = fs::path(EMU51_REPO_ROOT) / "progs/asm/demo/serial_echo.asm";
        check(app->loadProgram(sample.string().c_str(), true, true), "Serial echo example assembles and loads");
        app->showSerialTerminal();
        app->crystalMHz_ = 11.0592;
        std::snprintf(app->serialTerminalInput_, sizeof(app->serialTerminalInput_), "Hi");
        check(app->sendSerialTerminalInput() && !app->running_ && app->cpu_.pendingSerialReceiveCount() == 2 &&
            app->serialTerminalInput_[0] == '\0', "Sending serial input queues bytes without starting execution");
        app->toggleRun(); app->runSteps(10000);
        app->stopForDebugger("test");
        check(app->serialTerminalDisplay() == ">Hi" && app->cpu_.serialReceiveOverruns() == 0 &&
            app->cpu_.serialTransmitOverwrites() == 0,
            "Interrupt-driven UART echo sends its greeting and echoes queued text without data loss");
        app->serialHexInput_ = true;
        std::snprintf(app->serialTerminalInput_, sizeof(app->serialTerminalInput_), "00 0xFF 0A");
        check(app->sendSerialTerminalInput(), "Terminal accepts hexadecimal input, including NUL and FF");
        app->toggleRun(); app->runSteps(10000); app->stopForDebugger("test");
        check(app->serialTerminalDisplay() == ">Hi\\x00\\xFF\n", "Text output escapes binary bytes without corrupting the terminal");
        app->serialHexOutput_ = true;
        check(app->serialTerminalDisplay() == "3E 48 69 00 FF 0A ", "Hexadecimal output preserves every transmitted byte");
        std::snprintf(app->serialTerminalInput_, sizeof(app->serialTerminalInput_), "41 ZZ");
        const auto queued = app->cpu_.pendingSerialReceiveCount();
        check(!app->sendSerialTerminalInput() && app->cpu_.pendingSerialReceiveCount() == queued &&
            std::string(app->serialTerminalInput_) == "41 ZZ", "Malformed hex input cannot enqueue a partial message");
        app->serialHexInput_ = false; app->serialLineEnding_ = 3;
        std::snprintf(app->serialTerminalInput_, sizeof(app->serialTerminalInput_), "X");
        check(app->sendSerialTerminalInput() && app->cpu_.pendingSerialReceiveCount() == 3,
            "Selected CRLF ending is appended to queued input");
        app->cpu_.clearSerialReceiveQueue();
        for (std::size_t i = 0; i < emu51::kSerialQueueCapacity; ++i) app->cpu_.queueSerialReceive(0);
        std::snprintf(app->serialTerminalInput_, sizeof(app->serialTerminalInput_), "Y");
        check(!app->sendSerialTerminalInput() && app->cpu_.pendingSerialReceiveCount() == emu51::kSerialQueueCapacity,
            "Full serial input queues reject whole messages without overflow");
        app->resetCpuState(true);
        check(!app->cpu_.pendingSerialReceiveCount() && app->serialTerminalOutput_.size() == 6,
            "CPU reset clears pending UART state while preserving the terminal transcript");

        const auto project = root / "serial.emu51";
        check(app->saveProjectToPath(project), "Project saves serial terminal visibility");
        auto restored = create();
        check(restored->openProjectFromPath(project) && restored->serialTerminalOpen_ &&
            restored->serialTerminalOutput_.empty() && !restored->cpu_.pendingSerialReceiveCount(),
            "Projects restore terminal visibility without replaying runtime serial traffic");
        std::string old = read(project);
        const std::string field = "\"serial_terminal_open\":true,";
        const auto at = old.find(field);
        check(at != std::string::npos, "Project serial visibility field is present");
        if (at != std::string::npos) {
            old.erase(at, field.size()); write(root / "old-serial.emu51", old);
            check(restored->openProjectFromPath(root / "old-serial.emu51") && !restored->serialTerminalOpen_,
                "Projects created before UART support default to a closed terminal");
        }
        restored->showSerialTerminal();
        ImGui::NewFrame(); restored->drawSerialTerminal(); ImGui::Render();
    }

    static void test_serial_file_send(const fs::path& root)
    {
        auto app = create();
        const auto sample = fs::path(EMU51_REPO_ROOT) / "progs/asm/demo/serial_echo.asm";
        check(app->loadProgram(sample.string().c_str(), true, true), "File-send test loads the serial echo example");
        app->toggleRun(); app->runSteps(10000); app->stopForDebugger("test");
        // Speed up the example's timer for a large-file regression while
        // retaining its real interrupt-driven receive/transmit handshake.
        app->cpu_.TH1 = app->cpu_.TL1 = 0xff;
        std::string payload;
        for (std::size_t i = 0; i < emu51::kSerialQueueCapacity + 513; ++i)
            payload.push_back(static_cast<char>(i & 255));
        const auto path = root / "serial input.bin";
        write(path, payload);
        app->serialHexInput_ = true; app->serialLineEnding_ = 3;
        app->serialFilePathPicker_ = [&] { return path; };
        const auto beforeOutput = app->serialOutputGeneration_;
        const auto beforeInstructions = app->instructionCount_;
        app->requestSerialFileSend();
        check(app->serialFileStream_.is_open() && app->serialFileQueued_ == emu51::kSerialQueueCapacity &&
            app->cpu_.pendingSerialReceiveCount() == emu51::kSerialQueueCapacity && !app->running_ &&
            app->instructionCount_ == beforeInstructions,
            "Choosing a large file queues bounded input without starting execution");
        const auto beforePath = app->serialFilePath_;
        check(!app->startSerialFileSend(root / "other.bin") && app->serialFilePath_ == beforePath,
            "An active file transfer cannot be replaced silently");
        std::snprintf(app->serialTerminalInput_, sizeof(app->serialTerminalInput_), "41");
        check(!app->sendSerialTerminalInput() && std::string(app->serialTerminalInput_) == "41",
            "Text/hex messages cannot interleave with an active file transfer");
        app->toggleRun(); app->runSteps(900000); app->stopForDebugger("test");
        check(!app->serialFileStream_.is_open() && app->serialFileQueued_ == payload.size() &&
            !app->cpu_.pendingSerialReceiveCount() && app->serialOutputGeneration_ - beforeOutput == payload.size() &&
            app->cpu_.serialReceiveOverruns() == 0 && app->cpu_.serialTransmitOverwrites() == 0,
            "Large file transfers refill the queue while the hidden terminal's firmware echoes every byte");
        bool matches = app->serialTerminalOutput_.size() == emu51::kSerialQueueCapacity;
        for (std::size_t i = 0; matches && i < app->serialTerminalOutput_.size(); ++i)
            matches = app->serialTerminalOutput_[i].value == static_cast<unsigned char>(payload[payload.size() - emu51::kSerialQueueCapacity + i]);
        check(matches, "File sending preserves binary bytes and order without hex parsing or appended CRLF");

        const auto completedPath = app->serialFilePath_;
        const auto completedCount = app->serialFileQueued_;
        app->serialFilePathPicker_ = [] { return fs::path{}; };
        app->requestSerialFileSend();
        check(app->serialFilePath_ == completedPath && app->serialFileQueued_ == completedCount,
            "Canceling the file picker preserves transfer history");
        check(!app->startSerialFileSend(root / "missing.bin") && !app->startSerialFileSend(root) &&
            app->serialFilePath_ == completedPath && !app->cpu_.pendingSerialReceiveCount(),
            "Missing files and directories cannot mutate serial input or the last transfer");
        write(root / "empty.bin", "");
        check(app->startSerialFileSend(root / "empty.bin") && !app->serialFileStream_.is_open() &&
            app->serialFileQueued_ == 0 && !app->cpu_.pendingSerialReceiveCount(), "Empty files complete without injecting bytes");

        check(app->startSerialFileSend(path), "A second file transfer starts after completion");
        app->running_ = true;
        app->cancelSerialFileSend();
        app->serviceSerialFileSend();
        check(!app->serialFileStream_.is_open() && !app->cpu_.pendingSerialReceiveCount() && app->running_,
            "Cancel stops file reading and clears pending UART input without stopping the CPU");
        app->stopForDebugger("test");
        check(app->startSerialFileSend(path), "A canceled file can be selected again");
        app->resetCpuState(true);
        app->serviceSerialFileSend();
        check(!app->serialFileStream_.is_open() && !app->cpu_.pendingSerialReceiveCount(),
            "CPU reset cancels file transfers so they cannot inject data into the reset machine");

        check(app->startSerialFileSend(path), "A file can be opened after reset");
        app->serialFileStream_.setstate(std::ios::badbit);
        app->cpu_.clearSerialReceiveQueue();
        app->serviceSerialFileSend();
        check(!app->serialFileStream_.is_open() && !app->cpu_.pendingSerialReceiveCount() &&
            app->serialTerminalStatus_.find("read failed") != std::string::npos,
            "Mid-transfer read errors stop the transfer without injecting an incomplete chunk");
        check(app->startSerialFileSend(path), "File opening recovers from stream errors");
        check(app->loadProgram(sample.string().c_str(), true, true) && !app->serialFileStream_.is_open() &&
            app->serialFilePath_.empty() && !app->cpu_.pendingSerialReceiveCount(),
            "Opening a new program cancels transfers and clears file progress");
        app->showSerialTerminal();
        check(app->startSerialFileSend(path), "Terminal rendering test has an active file transfer");
        ImGui::NewFrame(); app->drawSerialTerminal(); ImGui::Render();
    }

    static void test_serial_file_endings(const fs::path& root)
    {
        // A CR at the end of the first CRLF-mode source chunk, mixed endings,
        // an embedded NUL, worst-case expansion and a final lone CR.
        std::string payload(2047, 'A');
        payload += "\r\nB\nC\r";
        payload.append("D\0E", 3);
        payload += std::string(4096, '\n');
        payload.append("\0tail", 5);
        payload += '\r';
        const auto path = root / "mixed endings.txt";
        write(path, payload);
        for (int mode = 0; mode <= 3; ++mode) {
            auto app = create();
            const auto sample = fs::path(EMU51_REPO_ROOT) / "progs/asm/demo/serial_echo.asm";
            check(app->loadProgram(sample.string().c_str(), true, true), "File-ending test loads UART echo firmware");
            app->running_ = true; app->runSteps(10000);
            app->cpu_.TH1 = app->cpu_.TL1 = 0xff;
            app->serialFileLineEnding_ = mode;
            app->serialHexInput_ = true; app->serialLineEnding_ = 2;
            const auto before = app->serialOutputGeneration_;
            check(app->startSerialFileSend(path), "Text or binary file transfer starts");
            // Changing the control cannot alter a transfer already in progress.
            app->serialFileLineEnding_ = (mode + 1) % 4;
            const std::string ending = mode == 1 ? "\n" : mode == 2 ? "\r" : "\r\n";
            std::string expected = payload;
            if (mode) {
                expected = std::string(2047, 'A') + ending + "B" + ending + "C" + ending;
                expected.append("D\0E", 3);
                for (unsigned i = 0; i < 4096; ++i) expected += ending;
                expected.append("\0tail", 5);
                expected += ending;
            }
            unsigned remaining = 3000000;
            while (remaining-- && app->serialOutputGeneration_ - before < expected.size()) app->executeInstruction();
            app->runSteps(1000);
            check(!app->serialFileStream_.is_open() && app->serialFileQueued_ == payload.size() &&
                !app->cpu_.pendingSerialReceiveCount() && app->serialOutputGeneration_ - before == expected.size() &&
                app->cpu_.serialReceiveOverruns() == 0 && app->cpu_.serialTransmitOverwrites() == 0,
                "All ending modes stream within queue bounds and preserve source progress without data loss");
            bool matches = app->serialTerminalOutput_.size() == emu51::kSerialQueueCapacity;
            for (std::size_t i = 0; matches && i < app->serialTerminalOutput_.size(); ++i)
                matches = app->serialTerminalOutput_[i].value == static_cast<unsigned char>(
                    expected[expected.size() - emu51::kSerialQueueCapacity + i]);
            check(matches, "File ending conversion coalesces CRLF across chunks and preserves other bytes and order");
            check(read(path) == payload, "Sending text never rewrites the source file");
            app->serialFileLineEnding_ = 4;
            check(!app->startSerialFileSend(path) && !app->cpu_.pendingSerialReceiveCount(),
                "Invalid file ending settings do not inject input");
        }
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
    Emu51ImguiAppTestAccess::test_symbolic_debugging(root);
    Emu51ImguiAppTestAccess::test_project_sessions(root);
    Emu51ImguiAppTestAccess::test_recent_projects(root);
    Emu51ImguiAppTestAccess::test_serial_terminal(root);
    Emu51ImguiAppTestAccess::test_serial_file_send(root);
    Emu51ImguiAppTestAccess::test_serial_file_endings(root);
    Emu51ImguiAppTestAccess::test_execution_servicing();
    Emu51ImguiAppTestAccess::test_recovery_integration(root);
    fs::remove_all(root);
    ImGui::DestroyContext();
    if (failures)
        return 1;
    std::puts("emu51 application workflow tests passed");
    return 0;
}
