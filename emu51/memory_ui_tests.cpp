// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"
#include "emu51_editor_files.h"
#include "TextEditor.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace {
namespace fs = std::filesystem;
void check(bool ok, const std::string& what) { if (!ok) { std::fprintf(stderr,"[FAIL] %s\n",what.c_str()); std::exit(1); } }
std::string read(const fs::path& path) { std::ifstream input(path,std::ios::binary); return {std::istreambuf_iterator<char>(input),{}}; }
}
struct Emu51ImguiAppTestAccess {
    static std::unique_ptr<Emu51ImguiApp> create() { return std::unique_ptr<Emu51ImguiApp>(new Emu51ImguiApp(false,false)); }
    static void run(const fs::path& root) {
        const auto demos = fs::path(EMU51_REPO_ROOT)/"progs/asm/demo";
        auto app = create();
        check(app->openProjectFromPath(demos/"banked_rom.emu51"),"Open banked project: "+app->statusMessage_);
        check(app->projectUnits_.size() == 3 && app->projectMappings_.size() == 3,"Project installs independent build units and mappings");
        auto* storage = app->peripherals_->memory("program_rom");
        check(e51p_memory_capacity(storage) == 524288 && app->cpu_.readLatch(0x90) == 255,"512 KiB ROM and reset selector state");
        check(app->readProgramByte(0) == 0x75 && fs::path(app->sourceLines_.at(0).file).filename() == "banked_boot.asm","Boot source follows physical region 15");
        app->executeInstruction();
        check(app->cpu_.PC == 3 && app->cpu_.readLatch(0x90) == 254,"Firmware changes selector without reset");
        check(app->readProgramByte(3) == 0x74 && fs::path(app->sourceLines_.at(3).file).filename() == "banked_application.asm","Bank switch changes disassembly/source identity");
        app->executeInstruction(); check(app->cpu_.Acc == 0x42,"CPU fetches instructions from selected physical bank");
        check(app->peripherals_->programMemory("program_rom",14*32768+4,{0x99}),"Modify an instruction operand physically");
        (void)app->readProgramByte(3);
        check(!app->sourceLines_.contains(3),"Changed operand invalidates the instruction's physical source line");
        check(app->peripherals_->programMemory("program_rom",14*32768+4,{0x42}),"Restore programmed instruction");
        (void)app->readProgramByte(3); check(app->sourceLines_.contains(3),"Matching physical bytes restore source identity");
        for (unsigned i = 0; i < 10; ++i) app->executeInstruction();
        uint8_t byte = 0; e51p_memory_read(app->peripherals_->memory("work_ram"),0,&byte);
        check(byte == 0x42,"MOVX writes wired external RAM");
        check(app->peripherals_->readData(0x8000,byte,true) && byte == 0x42 &&
            app->peripherals_->readData(0,byte,true) && byte == 255,"Inverted A15 selects RAM only in the upper XDATA half");
        const auto selector = app->cpu_.readLatch(0x90);
        const auto ticks = app->peripherals_->ticks();
        const auto snapshot = app->peripherals_->snapshotMemory("program_rom",15*32768,3);
        check(app->peripherals_->programMemory("program_rom",15*32768,snapshot) && app->cpu_.readLatch(0x90) == selector && app->peripherals_->ticks() == ticks,"Inactive-region binary programming preserves selectors and time");
        app->cpu_.writeData(0,0x99);
        e51p_memory_read(storage,14*32768+3,&byte); check(byte == 0x74,"CPU writes cannot alter ROM");

        check(app->switchProjectUnit("boot"),"Select boot editor");
        const auto originalBoot = app->assemblyEditor_->GetText();
        auto* bootEditor = app->assemblyEditor_.get();
        app->assemblyEditor_->ReplaceSectionText(0,0,0,0,"; boot draft\n"); app->trackAssemblyEditorChanges();
        check(app->switchProjectUnit("application"),"Switch to application without discarding boot draft");
        auto* applicationEditor = app->assemblyEditor_.get();
        const auto originalApplication = app->assemblyEditor_->GetText();
        app->assemblyEditor_->ReplaceSectionText(0,0,0,0,"; application draft\n"); app->trackAssemblyEditorChanges();
        check(app->switchProjectUnit("boot") && app->assemblyEditor_.get() == bootEditor && app->assemblyEditor_->GetText().starts_with("; boot draft"),"Separate TextEditor and undo state survive switches");
        app->assemblyEditor_->Undo(); app->trackAssemblyEditorChanges();
        check(app->assemblyEditor_->GetText() == originalBoot,"Boot undo does not alter application buffer");
        check(app->switchProjectUnit("application") && app->assemblyEditor_.get() == applicationEditor && app->assemblyEditor_->GetText().starts_with("; application draft"),"Application draft remains independent");
        app->assemblyEditor_->Undo(); app->trackAssemblyEditorChanges();
        check(app->assemblyEditor_->GetText() == originalApplication,"Application retains its own undo history");
        app->assemblyEditor_->ReplaceSectionText(0,0,0,0,"; saved in project only\n"); app->trackAssemblyEditorChanges();
        const auto sourceOnDisk = read(demos/"banked_application.asm");
        check(app->saveProjectToPath(root/"edited.emu51"),"Save all editor drafts: "+app->statusMessage_);
        check(read(demos/"banked_application.asm") == sourceOnDisk && app->peripherals_->memory("program_rom") == storage,"Save Project preserves source files and memory storage");
        auto reopened = create();
        check(reopened->openProjectFromPath(root/"edited.emu51"),"Reload saved project buffers: "+reopened->statusMessage_);
        check(reopened->activeProjectUnit_ == "application" && reopened->assemblyEditor_->GetText().starts_with("; saved in project only"),"Active unit and embedded draft round trip");
        check(reopened->switchProjectUnit("boot") && reopened->assemblyEditor_->GetText() == originalBoot,"Inactive unit also round trips");

        auto config = app->peripherals_->config();
        for (auto& d : config.devices) if (d.kind == Emu51PeripheralKind::Clock) d.clockHz = 24000000;
        check(app->applyPeripheralConfig(config,12) && app->crystalMHz_ == 24 && app->peripherals_->memory("program_rom") == storage,"Clock Apply uses connected source and retains storage identity");
        e51p_memory_read(app->peripherals_->memory("work_ram"),0,&byte); check(byte == 0x42,"Clock Apply preserves RAM contents");
        check(app->cpu_.PC == 0 && !app->running_ && app->cpu_.readLatch(0x90) == 255,"Apply resets/pauses CPU and resets selector ports without clearing memory");
        auto invalid = config;
        Emu51PeripheralDevice collision{"collision",Emu51PeripheralKind::AddressDecoder};
        collision.decoderOutputs = {{"A","",true,{0}},{"B","",true,{0}}};
        invalid.devices.push_back(collision);
        auto* active = app->peripherals_.get(); app->cpu_.PC = 0x1234;
        check(!app->applyPeripheralConfig(invalid,24) && app->peripherals_.get() == active && app->cpu_.PC == 0x1234,"Decoder collision cannot replace a live machine");
        std::string error; auto bad = emu51LoadProject(root/"edited.emu51",error); check(bool(bad),error);
        bad->units[0].editor.draft = true; bad->units[0].editor.text = "invalid assembly\n";
        check(emu51SaveProject(root/"broken.emu51",*bad,error),error);
        app->running_ = true;
        check(!app->openProjectFromPath(root/"broken.emu51") && app->peripherals_.get() == active && app->cpu_.PC == 0x1234 && app->running_,"Failed required build preserves current CPU, running state and storage");
        app->running_ = false;
        for (auto& d : config.devices) if (d.id == "program_rom") d.memoryImages = {{(root/"missing.bin").string(),0x50000,1}};
        check(app->applyPeripheralConfig(config,24),"Initialization recipe can change without reloading unchanged storage");
        active = app->peripherals_.get();
        check(!app->buildProjectUnits(true,true) && app->peripherals_.get() == active,"Failed power-on image validation preserves existing machine");
        check(emu51WriteTextFileAtomically(root/"init.bin",std::string(1,static_cast<char>(0xa5))),"Write power-on fixture");
        for (auto& d : config.devices) if (d.id == "program_rom") d.memoryImages = {{(root/"init.bin").string(),0x50000,1}};
        check(app->applyPeripheralConfig(config,24) && app->buildProjectUnits(true,true),"Explicit power cycle reloads fills, images and program rows");
        e51p_memory_read(app->peripherals_->memory("program_rom"),0x50000,&byte); check(byte == 0xa5,"Power cycle applies binary image");
        e51p_memory_read(app->peripherals_->memory("work_ram"),0,&byte); check(byte == 0,"Power cycle reconstructs RAM instead of retaining runtime writes");
        check(app->readProgramByte(0) == 0x75,"Assembly programming follows binary initialization");
        const auto init = read(root/"init.bin");
        check(!app->saveProjectToPath(root/"init.bin") && read(root/"init.bin") == init,"Save Project cannot overwrite an initialization image");

        // Exercise new inspector, project, logical and physical debugger UI paths.
        app->assemblyEditorState_.open = true; app->debuggerOpen_ = true; app->peripheralsOpen_ = true;
        app->peripheralEditor_.selected_ = "program_rom";
        auto& io = ImGui::GetIO(); io.DisplaySize = {1600,1000}; io.DeltaTime = 1.0f/60;
        for (unsigned frame = 0; frame < 2; ++frame) {
            ImGui::NewFrame(); app->drawPeripherals(); app->drawAssemblyEditor(); app->drawDebugger(); ImGui::Render();
        }
        app->peripheralEditor_.selected_ = "select";
        ImGui::NewFrame(); app->drawPeripherals(); ImGui::Render();
        ImGui::NewFrame(); ImGui::Begin("Physical inspector test"); ImGui::SetNextItemOpen(true);
        app->drawPhysicalMemory(); ImGui::End(); ImGui::Render();
        auto floating = app->peripherals_->config();
        for (auto& wire : floating.digital) if (wire.a == "cpu.P1" && wire.b == "program_rom.ADDR") wire.a = "cpu.P0";
        check(app->applyPeripheralConfig(floating,24),"Floating-selector circuit can be inspected");
        const auto before = app->peripherals_->ticks();
        check(app->readProgramByte(0) == 255 && app->peripherals_->ticks() == before && app->peripherals_->status() == E51P_OK,"Passive unresolved read does not poison execution or advance time");
        app->running_ = true; app->executeInstruction();
        check(!app->running_ && app->statusMessage_.find("address") != std::string::npos,"P0 without pull-ups reports a runtime bus fault");

        auto bus = create(); check(bus->openProjectFromPath(demos/"decoder_bus.emu51"),"Open shared bus example");
        bus->peripheralsOpen_ = true;
        bus->peripheralEditor_.selected_ = "select";
        ImGui::NewFrame(); bus->drawPeripherals(); ImGui::Render();
        for (unsigned i = 0; i < 20; ++i) bus->executeInstruction();
        bool defined = false; check(bus->peripherals_->ledState("led",defined) && defined,"Decoder-selected 8255 drives real LED peripheral");
        auto shared = create(); check(shared->openProjectFromPath(demos/"shared_ram.emu51"),"Open shared CODE/XDATA example");
        for (unsigned i = 0; i < 20; ++i) shared->executeInstruction();
        check(shared->cpu_.Acc == 0x55 && shared->readProgramByte(0x100) == 0x55,"Shared bytes are visible through both CPU spaces and debugger");
        shared->unsavedChangesPrompt_ = [] { return Emu51ImguiApp::UnsavedChoice::Discard; };
        const auto sources = shared->projectUnits_;
        for (const auto& unit : sources) check(shared->removeProjectUnit(unit.id),"Remove source, including final unit");
        check(shared->projectUnits_.empty() && shared->projectMappings_.empty() && shared->activeProjectUnit_.empty(),"Removing final unit leaves a usable empty project");
        check(shared->buildProjectUnits(true,true),"Memory-only circuit can power-cycle without assembly units");
        shared->assemblyEditor_->SetText("org 0\ndb 99h\nend\n"); shared->markAssemblyEditorChanged();
        check(shared->compileAssemblyEditorImage(true) && !shared->loadCompiledAssemblyEditorOutput(true,false),"Standalone editor loading into a custom circuit requires an explicit target");
        check(shared->projectUnits_.size() == 1 && shared->projectMappings_.size() == 1 && shared->projectMappings_.front().device.empty(),"Unplaced editor output becomes a reviewable mapping draft");
        check(shared->readProgramByte(0) == 0,"Unplaced output preserves live storage");
        auto plain = create(); check(plain->initializeCircuitProject(),"Create default memory circuit");
        plain->assemblyEditor_->SetText("org 0\nmov a,#33h\nend\n"); plain->markAssemblyEditorChanged();
        check(plain->compileAssemblyEditorImage(true) && plain->loadCompiledAssemblyEditorOutput(true,false) && plain->readProgramByte(1) == 0x33,
            "Standalone editor loading into default memory creates an explicit programming row");
    }
};
int main() {
    ImGui::CreateContext(); ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().Fonts->AddFontDefault(); ImGui::GetIO().Fonts->Build();
    const auto root = fs::temp_directory_path()/("emu51-memory-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root); Emu51ImguiAppTestAccess::run(root); fs::remove_all(root);
    ImGui::DestroyContext(); std::puts("Memory circuit UI, independent editors, physical sources, clocks and lifecycle passed.");
}
