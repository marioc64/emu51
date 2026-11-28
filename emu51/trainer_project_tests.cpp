// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"
#include "emu51_editor_files.h"
#include "imgui_internal.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace {
namespace fs = std::filesystem;
void check(bool condition, const std::string& message) {
    if (!condition) { std::fprintf(stderr,"[FAIL] %s\n",message.c_str()); std::exit(1); }
}
}
struct Emu51ImguiAppTestAccess {
    static std::unique_ptr<Emu51ImguiApp> create() { return std::unique_ptr<Emu51ImguiApp>(new Emu51ImguiApp(false,false)); }
    static void runFor(Emu51ImguiApp& app, uint64_t cycles) {
        const auto until = app.cycleCount_+cycles;
        while (app.cycleCount_ < until) {
            app.executeInstruction();
            check(app.peripherals_->status() == E51P_OK,"Trainer circuit executes without bus faults: "+app.peripherals_->busError());
        }
    }
    static bool runUntil(Emu51ImguiApp& app, uint16_t pc, uint64_t cycles) {
        const auto until = app.cycleCount_+cycles;
        while (app.cpu_.PC != pc && app.cycleCount_ < until) app.executeInstruction();
        return app.cpu_.PC == pc;
    }
    static void press(Emu51ImguiApp& app, unsigned key, bool down) {
        const auto bit = 1u<<(key < 16 ? 2*(key%8)+key/8 : key);
        const auto mask = app.peripherals_->keys("keypad");
        check(app.peripherals_->setKeys("keypad",down ? mask|bit : mask&~bit),"Set keypad contact");
    }
    static void circuit(const fs::path& root) {
        std::string error;
        auto project = emu51LoadProject(fs::path(EMU51_REPO_ROOT)/"progs/boards/avt2250/avt2250.emu51",error);
        check(bool(project),"Public ROM-free circuit parses: "+error);
        check(project->hardwareModule.empty() && project->peripherals.devices.size() == 12,"Circuit uses ordinary public peripherals and one complete keypad");
        const auto keypad = std::find_if(project->peripherals.devices.begin(),project->peripherals.devices.end(),[](const auto& d) { return d.id == "keypad"; });
        check(keypad != project->peripherals.devices.end() && keypad->keyLayoutColumns == 5 && keypad->keyLayoutRows == 4 &&
            keypad->keyLayout.size() == 20 && std::count(keypad->keyLayout.begin(),keypad->keyLayout.end(),-1) == 2 &&
            keypad->directKeys == 2 && keypad->keyLabels[16] == "M" && keypad->keyLabels[17] == "OK" &&
            keypad->keyLayout[15] == 16 && keypad->keyLayout[16] == 0 && keypad->keyLayout[17] == 17 &&
            keypad->keyLayout[18] == -1 && keypad->keyLayout[19] == -1,"AVT panel has five columns, four rows and bottom row M / 0 / OK / empty / empty");
        const auto originalPath = project->peripherals.devices.front().memoryImages.front().path;
        check(fs::path(originalPath).filename() == "monitor.bin","Public project references a user-provided image");
        emu51_asm::Assembler assembler;
        const auto assembled = assembler.assemble_string("synthetic.asm",
            "org 0\nmov dptr,#4001h\nmov a,#3\nmovx @dptr,a\nmov dptr,#4000h\nmov a,#4fh\nmovx @dptr,a\n"
            "mov dptr,#8000h\nmov a,#55h\nmovx @dptr,a\nmovx a,@dptr\nmov 20h,a\n"
            "mov dptr,#6000h\nmovx a,@dptr\nmov 21h,a\nidle: sjmp idle\nend\n");
        check(assembled.success(),"Assemble original synthetic bus exercise");
        std::string binary(8192,0);
        for (const auto& [address,value] : assembled.image.bytes) binary[address] = static_cast<char>(value);
        check(emu51WriteTextFileAtomically(root/"monitor.bin",binary),"Write synthetic ROM fixture");
        project->peripherals.devices.front().memoryImages.front().path = fs::weakly_canonical(root/"monitor.bin").string();
        check(emu51SaveProject(root/"trainer.emu51",*project,error),"Save circuit with synthetic image: "+error);
        auto app = create(); check(app->openProjectFromPath(root/"trainer.emu51"),"Open standard trainer project: "+app->statusMessage_);
        check(!app->hardware_ && app->crystalMHz_ == 11.0592,"No hardware module; graph Clock is authoritative");
        const auto routing = app->peripherals_->routingRevision(false);
        press(*app,3,true);
        check(app->peripherals_->routingRevision(false) == routing,"Keyboard data changes do not invalidate CODE routing caches");
        runFor(*app,100);
        check(app->cpu_.IRAM[0x20] == 0x55 && app->cpu_.IRAM[0x21] == 0xfe,"CPU MOVX reaches shared RAM and scanned keys");
        check(app->readProgramByte(0x8000) == 0x55,"RAM is executable CODE as well as writable XDATA");
        e51p_segment_snapshot display{};
        check(app->peripherals_->displayState("display",display) && display.instantaneous[3] == 0x4f,"Latched segment and digit signals drive the standard display");
        for (unsigned key = 0; key < 18; ++key) {
            app->peripherals_->setKeys("keypad",0);
            press(*app,key,true);
            for (unsigned scan = 0; scan < 8; ++scan) {
                app->cpu_.writeData(0x5fff,static_cast<uint8_t>(scan)); app->cpu_.writeData(0x5ffe,0);
                const auto value = app->cpu_.readData(0x7fff);
                const auto expected = static_cast<uint8_t>(255u & ~(key < 16 ? (key%8 == scan ? 1u<<(key/8) : 0u) : 1u<<(key-14)));
                check(value == expected,"Each of 18 keys respects scan position and MMIO aliases");
            }
        }
        press(*app,17,false); app->peripherals_->setKeys("keypad",(1u<<6)|(1u<<7)); press(*app,16,true); press(*app,17,true);
        app->cpu_.writeData(0x4001,3); app->cpu_.writeData(0x4000,0xff);
        check(app->cpu_.readData(0x6000) == 0xf0,"Four keyboard input bits can be asserted simultaneously");
        const auto time = app->peripherals_->ticks();
        for (unsigned i = 0; i < 20; ++i) check(app->cpu_.readData(0x6000) == 0xf0,"Passive keyboard buffer read");
        check(app->peripherals_->ticks() == time,"Repeated debugger reads preserve time and keys");
        check(app->cpu_.readData(0x4000) == 255 && app->cpu_.readData(0x2000) == 255,"Output-only and unmapped reads return open bus");
        const auto firstByte = app->readProgramByte(0); app->cpu_.writeData(0,0x99);
        check(app->readProgramByte(0) == firstByte,"Ordinary writes cannot alter EPROM");
        check(app->peripherals_->programMemory("program_ram",0x1000,{0x74,0x42,0x80,0xfe}),"Explicitly program executable RAM");
        app->cpu_.PC = 0x9000; app->executeInstruction(); check(app->cpu_.Acc == 0x42,"CPU fetches and executes code in shared RAM");
        runFor(*app,40000); app->peripherals_->displayState("display",display);
        check(std::all_of(std::begin(display.instantaneous),std::end(display.instantaneous),[](auto v) { return v == 0; }),"Scan watchdog blanks the display after writes stop");
        app->cpu_.writeData(0x4000,0x80); app->peripherals_->displayState("display",display);
        check(display.instantaneous[3] == 0x80,"A committed segment write retriggers the display watchdog");
        app->resetCpuState(true);
        check(app->readProgramByte(0x8000) == 0x55 && app->readProgramByte(0) == firstByte,"CPU reset preserves RAM and ROM");
        check(app->buildProjectUnits(true,true) && app->readProgramByte(0x8000) == 0,"Memory-only power cycle restores the ROM recipe and clears RAM");
        const auto restored = emu51LoadProject(root/"trainer.emu51",error);
        check(restored && restored->peripherals == project->peripherals,"Key labels, pulse timing and wiring round trip");
        auto slowConfig = project->peripherals;
        for (auto& d : slowConfig.devices) if (d.kind == Emu51PeripheralKind::Clock) d.clockHz = 1;
        auto slow = Emu51PeripheralSession::create(slowConfig,12); slow->writeData(0x4000,0x33);
        e51p_digital_state pulse{};
        check(slow->sampleDigital("segments.PULSE",pulse) && pulse.value == 1,"Positive pulse duration rounds up to one tick at low Clock rates");
        auto invalid = *project; invalid.peripherals.devices.front().memoryImages.front().path = (root/"missing.bin").string();
        check(emu51SaveProject(root/"missing.emu51",invalid,error),error);
        auto* session = app->peripherals_.get(); app->cpu_.PC = 0x1234;
        check(!app->openProjectFromPath(root/"missing.emu51") && app->peripherals_.get() == session && app->cpu_.PC == 0x1234,
            "Missing user ROM preserves the existing machine");
        // Exercise the ordinary graph inspector and Live controls, including the matrix.
        app->peripheralsOpen_ = true;
        auto& io = ImGui::GetIO(); io.DisplaySize = {1500,950}; io.DeltaTime = 1.0f/60;
        app->peripheralEditor_.selected_ = "keypad";
        for (unsigned frame = 0; frame < 2; ++frame) { ImGui::NewFrame(); app->drawPeripherals(); ImGui::Render(); }
        auto* window = ImGui::FindWindowByName("Peripherals");
        auto* tabs = GImGui->TabBars.GetByKey(window->GetID("peripheral-tabs"));
        tabs->NextSelectedTabId = ImHashStr("Live",0,tabs->ID);
        for (unsigned frame = 0; frame < 2; ++frame) { ImGui::NewFrame(); app->drawPeripherals(); ImGui::Render(); }
        layoutInteraction(*app,root);
    }
    static void layoutInteraction(Emu51ImguiApp& app, const fs::path& root) {
        const auto& devices = app.peripherals_->config().devices;
        const auto keypad = *std::find_if(devices.begin(),devices.end(),[](const auto& d) { return d.id == "keypad"; });
        auto& io = ImGui::GetIO(); ImVec2 origin{},available{};
        const auto frame = [&] {
            ImGui::NewFrame(); ImGui::SetNextWindowPos({20,20}); ImGui::SetNextWindowSize({850,600});
            ImGui::Begin("Key layout interaction",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize);
            origin = ImGui::GetCursorScreenPos(); available = ImGui::GetContentRegionAvail();
            app.drawPeripheralKeys(keypad,static_cast<unsigned>(emu51KeyCount(keypad)),static_cast<unsigned>(keypad.columns));
            ImGui::End(); ImGui::Render();
        };
        app.peripheralLive_.setMomentaryKeys("keypad",false); app.peripherals_->setKeys("keypad",0);
        io.AddMousePosEvent(-100,-100); frame(); frame();
        const auto center = [&](unsigned cell) {
            const auto spacing = ImGui::GetStyle().ItemSpacing;
            const auto line = ImGui::GetFrameHeightWithSpacing();
            const float w = (available.x-4*spacing.x)/5, h = (available.y-2*line-3*spacing.y)/4;
            return ImVec2{origin.x+(cell%5)*(w+spacing.x)+w/2,origin.y+line+(cell/5)*(h+spacing.y)+h/2};
        };
        for (unsigned cell = 0; cell < 20; ++cell) {
            app.peripherals_->setKeys("keypad",0);
            const auto at = center(cell); io.AddMousePosEvent(at.x,at.y); frame();
            io.AddMouseButtonEvent(0,true); frame(); io.AddMouseButtonEvent(0,false); frame();
            const int contact = keypad.keyLayout[cell];
            check(app.peripherals_->keys("keypad") == (contact < 0 ? 0u : 1u<<contact),
                "Rendered panel position "+std::to_string(cell)+" selects its assigned contact or remains empty");
        }
        app.peripherals_->setKeys("keypad",0); app.peripheralLive_.setMomentaryKeys("keypad",true); frame();
        for (const unsigned cell : {15u,17u}) {
            const auto at = center(cell); io.AddMousePosEvent(at.x,at.y); frame();
            io.AddMouseButtonEvent(0,true); frame();
            check(app.cpu_.readData(0x6000) == (cell == 17 ? 0xf7 : 0xfb),"M and OK work as independent momentary contacts in the same panel");
            io.AddMouseButtonEvent(0,false); frame(); check(app.peripherals_->keys("keypad") == 0,"Momentary panel contacts release on mouse up");
        }
        auto edited = app.peripherals_->config();
        auto& edit = *std::find_if(edited.devices.begin(),edited.devices.end(),[](const auto& d) { return d.id == "keypad"; });
        std::swap(edit.keyLayout[15],edit.keyLayout[17]); edit.keyLabels[16] = "Monitor key"; edit.keyLegends[16] = "Return";
        check(app.applyPeripheralConfig(edited,app.crystalMHz_),"Apply edited labels and visual layout");
        app.peripherals_->setKeys("keypad",1u<<16);
        check(app.cpu_.readData(0x6000) == 0xfb,"Rearranging and renaming preserves the electrical key code");
        check(app.saveProjectToPath(root/"edited-layout.emu51"),"Save edited keypad appearance");
        std::string error; const auto saved = emu51LoadProject(root/"edited-layout.emu51",error);
        check(saved && saved->peripherals == edited,"Edited dimensions, holes, contacts, labels and legends round trip");
        const auto reject = [&](auto change) {
            auto invalid = *saved; auto& d = *std::find_if(invalid.peripherals.devices.begin(),invalid.peripherals.devices.end(),[](const auto& v) { return v.id == "keypad"; });
            change(d); check(!emu51SaveProject(root/"bad-layout.emu51",invalid,error),"Invalid keypad layout is rejected before saving");
        };
        reject([](auto& d) { d.keyLayout[0] = d.keyLayout[1]; });
        reject([](auto& d) { d.keyLayout[0] = 18; });
        reject([](auto& d) { d.keyLayout[0] = -2; });
        reject([](auto& d) { d.keyLayout.pop_back(); });
        reject([](auto& d) { d.keyLabels[0] = std::string(64,'x'); });
        Emu51PeripheralDevice sparse; sparse.id = "sparse"; sparse.kind = Emu51PeripheralKind::KeyMatrix; sparse.rows = 5; sparse.columns = 4;
        sparse.keyLayoutColumns = 5; sparse.keyLayoutRows = 4; sparse.keyLayout.assign(20,-1);
        for (int i = 0; i < 18; ++i) sparse.keyLayout[static_cast<std::size_t>(i)] = i;
        Emu51PeripheralConfig sparseConfig; sparseConfig.devices.push_back(sparse); emu51ValidatePeripheralConfig(sparseConfig);
        check(emu51KeyCount(sparse) == 20,"A 5x4 electrical matrix can leave two panel contacts unused");
    }
    static void paulmon2(const fs::path& board, bool persistent) {
        auto app = create();
        const auto path = board/(persistent ? "paulmon2_eeprom.emu51" : "paulmon2.emu51");
        const bool opened = app->openProjectFromPath(path);
        check(opened,"Open the bundled PAULMON2 project: "+app->statusMessage_);
        check(app->cpu_.model()==Emu51Model::i8052 && app->serialTerminalOpen_ && app->crystalMHz_==11.0592,
            "Project restores the CPU, terminal and crystal");
        const auto rom = emu51_asm::prepare_program_file(board/"upstream/pm21_2.hex");
        check(rom.result.success && !rom.image.bytes.empty(),"The upstream monitor HEX is readable");
        for (const auto& [addr,byte] : rom.image.bytes)
            check(app->readProgramByte(addr)==byte,"The loaded monitor retains the published firmware bytes");

        const auto wait = [&](const std::string& text) {
            const auto until = app->cycleCount_+1200000;
            while (app->cycleCount_ < until) {
                runFor(*app,256);
                if (!app->cpu_.serialTransmitBusy() && app->serialTerminalDisplay().find(text)!=std::string::npos) return;
            }
            check(false,"PAULMON2 expected "+text+" in terminal output: "+app->serialTerminalDisplay());
        };
        const auto send = [&](const char* input) {
            app->serialTerminalOutput_.clear();
            std::snprintf(app->serialTerminalInput_,sizeof(app->serialTerminalInput_),"%s",input);
            check(app->sendSerialTerminalInput(),"Queue monitor input from the serial terminal");
        };

        wait("Loc:2000 >");
        check(app->cpu_.TH1==0xfa && app->cpu_.readData(0x2000)==0xff,
            "Unmodified monitor boots at 9600 baud with empty shared program RAM");
        send("D");
        wait("or ESC to abort\n\n");
        runFor(*app,2048);
        check(app->startSerialFileSend(board/"hello.hex"),"Send the sample Intel HEX over the simulated UART");
        wait("Download completed");
        wait("Loc:2000 >");
        check(app->serialTerminalDisplay().find("No errors detected")!=std::string::npos &&
            !app->serialFileStream_.is_open() && app->cpu_.serialReceiveOverruns()==0,
            "The real monitor accepts the transfer without checksum errors or receive overruns: "+app->serialTerminalDisplay()+
            " overruns="+std::to_string(app->cpu_.serialReceiveOverruns()));
        const auto demo = emu51_asm::prepare_program_file(board/"hello.hex");
        check(demo.result.success && !demo.image.bytes.empty(),"The sample Intel HEX is readable");
        for (const auto& [addr,byte] : demo.image.bytes)
            check(app->readProgramByte(addr)==byte && app->cpu_.readData(addr)==byte,
                "Monitor writes executable RAM through XDATA");

        send("J");
        wait("ESC to quit: ");
        runFor(*app,2048);
        send("2000");
        wait("Hello from RAM. Send a key to return.");
        bool defined = false;
        check(app->peripherals_->ledState("led",defined) && defined,"The uploaded program drives the P1.0 LED");
        send("X");
        wait("Loc:2000 >");
        check(!app->peripherals_->ledState("led",defined) && defined,"The uploaded program returns to the monitor");
        app->resetCpuState(true);
        check(app->readProgramByte(0x2000)==demo.image.bytes.at(0x2000),"CPU reset preserves downloaded RAM");
        if (persistent) {
            wait("Loc:2000 >");
            send("B");
            wait("Slot saved.");
            wait("Loc:2000 >");
            check(app->flushPeripheralStorage(),"Save EEPROM through the desktop image path");
            const auto image = Emu51PeripheralSession::readBinary(board/"program_slot.bin",32768);
            check(image[0]==0xa5 && image[0x100]==demo.image.bytes.at(0x2000),"The 32 KiB image contains the committed program slot");
        }
        check(app->buildProjectUnits(true,true) && app->readProgramByte(0x2000)==0xff &&
            app->readProgramByte(0x3f00)==0xa5,"Power cycle clears downloaded RAM and restores the baud initializer");
        if (persistent) {
            app->serialTerminalOutput_.clear();
            wait("Slot restored.");
            wait("Loc:2000 >");
            for (const auto& [addr,byte] : demo.image.bytes)
                check(app->readProgramByte(addr)==byte,"Power-up restores the saved program to executable RAM");
            app.reset(); app = create();
            check(app->openProjectFromPath(path),"Reopen the EEPROM trainer using its saved image");
            wait("Slot restored.");
            wait("Loc:2000 >");
            for (const auto& [addr,byte] : demo.image.bytes)
                check(app->readProgramByte(addr)==byte,"A fresh session loads the persisted program");
        }
    }
    static void monitor(const fs::path& path, const fs::path& serialInput) {
        auto app = create(); check(app->openProjectFromPath(path),"Open locally supplied monitor project: "+app->statusMessage_);
        runFor(*app,50000);
        e51p_segment_snapshot display{}; check(app->peripherals_->displayState("display",display),"Read standard display snapshot");
        constexpr std::array<uint8_t,8> greeting{0,0x40,0x76,0x79,0x38,0x38,0x3f,0};
        for (unsigned digit = 0; digit < 8; ++digit) for (unsigned segment = 0; segment < 8; ++segment)
            check((display.brightness[digit][segment] > 0.008) == bool(greeting[digit]&(1u<<segment)),"Unmodified monitor scans its HELLO greeting through public devices");
        runFor(*app,1500000);
        for (unsigned key = 0; key < 18; ++key) {
            app->cpu_.IRAM[0x76] = 0; press(*app,key,true);
            const unsigned expected = key < 16 ? static_cast<unsigned>("0123456789ABCDEF"[key]) : key == 16 ? 'M' : 13;
            const auto until = app->cycleCount_+50000;
            while (app->cycleCount_ < until && app->cpu_.IRAM[0x76] != expected) app->executeInstruction();
            check(app->cpu_.IRAM[0x76] == expected,"Monitor ISR decodes key "+std::to_string(key));
            if (key == 16) check(runUntil(*app,0x0110,10000),"M key returns through the actual monitor interrupt handler");
            press(*app,key,false); runFor(*app,50000);
        }
        if (!serialInput.empty()) {
            app->resetCpuState(true); runFor(*app,1600000);
            press(*app,7,true); check(runUntil(*app,0x031d,100000),"Monitor Load key enters its UART loader"); press(*app,7,false);
            runFor(*app,1000); app->serialFileLineEnding_ = 3;
            check(app->startSerialFileSend(serialInput),"Queue locally supplied HEX through normal serial input");
            check(runUntil(*app,0x06ef,5000000),"Original ROM accepts the CRLF-normalized stream and returns after EOF");
            const auto prepared = emu51_asm::prepare_program_file(serialInput);
            check(prepared.result.success,"Parse locally supplied HEX for comparison");
            for (const auto& [address,value] : prepared.image.bytes)
                check(app->readProgramByte(address) == value,"UART-loaded RAM byte matches its original HEX address");
            check(app->cpu_.serialReceiveOverruns() == 0 && !app->serialFileStream_.is_open(),"UART load completes without receive overruns");
        }
    }
};
int main(int argc, char** argv) {
    ImGui::CreateContext(); ImGui::GetIO().IniFilename = nullptr; ImGui::GetIO().Fonts->AddFontDefault(); ImGui::GetIO().Fonts->Build();
    const auto root = fs::temp_directory_path()/("emu51-trainer-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root); Emu51ImguiAppTestAccess::circuit(root);
    const auto programs = fs::path(EMU51_REPO_ROOT)/"progs";
    const auto board = programs/"boards/paulmon2", copy = root/"progs/boards/paulmon2";
    Emu51ImguiAppTestAccess::paulmon2(board,false);
    fs::create_directories(copy.parent_path());
    fs::copy(board,copy,fs::copy_options::recursive);
    fs::create_directories(root/"progs/asm/demo");
    fs::copy_file(programs/"asm/demo/serial_i2c.inc",root/"progs/asm/demo/serial_i2c.inc");
    Emu51ImguiAppTestAccess::paulmon2(copy,true);
    if ((argc == 3 || argc == 5) && std::string(argv[1]) == "--monitor-project") {
        check(argc != 5 || std::string(argv[3]) == "--serial-input","Expected --serial-input HEX");
        Emu51ImguiAppTestAccess::monitor(argv[2],argc == 5 ? fs::path(argv[4]) : fs::path{});
    } else check(argc == 1,"Usage: emu51_trainer_tests [--monitor-project PROJECT [--serial-input HEX]]");
    fs::remove_all(root); ImGui::DestroyContext(); std::puts("Public trainer circuits, ROM recipes, serial upload and lifecycle passed.");
}
