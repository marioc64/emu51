// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"
#include "emu51_imgui_actions.h"
#include "emu51_editor_files.h"
#include "emu51_segment_display.h"
#include "TextEditor.h"
#include "imgui_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>

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
    static std::string stateJson(const Emu51ImguiApp& app)
    {
        std::string json;
        check(emu51EncodeUiState(app.captureUiState(),json), "UI state encodes with Kason");
        return json;
    }
    static void restoreState(Emu51ImguiApp& app, const std::string& json)
    {
        Emu51UiState state;
        if (emu51DecodeUiState(json,state)) app.applyUiState(state);
        else check(false,"UI state decodes with Kason");
    }
    static std::unique_ptr<Emu51ImguiApp> create()
    {
        return std::unique_ptr<Emu51ImguiApp>(new Emu51ImguiApp(false, false));
    }
    static void edit(Emu51ImguiApp& app, const fs::path& path, const std::string& source)
    {
        app.assemblyEditor_->SetText(source);
        app.assemblyEditorState_.sourcePath = path.string();
        app.assemblyEditorState_.open = true;
        app.markAssemblyEditorChanged();
    }
    static bool containsLegacyGraph(const Emu51PeripheralConfig& actual, const Emu51PeripheralConfig& expected)
    {
        return std::all_of(expected.devices.begin(),expected.devices.end(),[&](const auto& d) { return std::find(actual.devices.begin(),actual.devices.end(),d) != actual.devices.end(); }) &&
            std::all_of(expected.digital.begin(),expected.digital.end(),[&](const auto& w) { return std::find(actual.digital.begin(),actual.digital.end(),w) != actual.digital.end(); }) &&
            std::all_of(expected.analog.begin(),expected.analog.end(),[&](const auto& w) { return std::find(actual.analog.begin(),actual.analog.end(),w) != actual.analog.end(); });
    }
    static auto& audioDevice(auto& config)
    {
        const auto found = std::find_if(config.devices.begin(),config.devices.end(),[](const auto& d) { return d.kind == Emu51PeripheralKind::Audio; });
        check(found != config.devices.end(),"Audio fixture contains its audio device");
        return found == config.devices.end() ? config.devices.front() : *found;
    }
    static void openAudioExample(Emu51ImguiApp& app, int example)
    {
        const auto path = fs::path(EMU51_REPO_ROOT)/"progs/asm/demo"/
            (example == 3 ? "microphone_covox.emu51" : "covox_duet.emu51");
        check(app.openProjectFromPath(path), "Open a portable audio example through the project loader");
        app.compileAssemblyEditor();
        check(app.hasCompiledAssemblyEditorOutput(),("Portable audio source compiles: "+app.statusMessage_).c_str());
    }
    static void configureAdcBoardFixture(Emu51ImguiApp& app)
    {
        check(app.applyPeripheralConfig(emu51PeripheralPreset(2),12), "Configure the ADC workflow fixture");
        app.assemblyEditorState_.sourceName = "adc-workflow.asm";
        app.assemblyEditorState_.open = true;
        app.assemblyEditor_->SetText(R"(; ADC0804 -> 8255 -> Covox. Inspect IRAM 31h and dac.out.
org 0
    mov DPTR,#8003h
    mov A,#098h       ; A input, B output, C upper input, lower output
    movx @DPTR,A
    mov DPTR,#8002h
convert:
    mov A,#004h       ; CS=0, WR=0, RD=1: reset converter
    movx @DPTR,A
    mov A,#006h       ; WR=1: sample input and start conversion
    movx @DPTR,A
wait_adc:
    jb 0B2h,wait_adc  ; ADC INTR -> P3.2
    mov A,#002h       ; RD=0: enable data
    movx @DPTR,A
    mov DPTR,#8000h
    movx A,@DPTR
    mov 031h,A
    mov DPTR,#8001h
    movx @DPTR,A      ; output sampled byte on DAC
    mov DPTR,#8002h
    sjmp convert
end
)");
        app.markAssemblyEditorChanged();
        app.compileAndLoadAssemblyEditor();
    }
#ifdef EMU51_GUI_HOST_AUDIO
    static bool startNullAudio(Emu51ImguiApp& app)
    {
        if (!app.peripherals_) return false;
        app.stopForDebugger("opening NULL audio fixture");
        std::string error;
        if (!app.peripherals_->enumerateAudio(app.audioCaptureDevices_,app.audioPlaybackDevices_,error,true) ||
            !app.peripherals_->openAudio(-1,-1,0,-1,error)) return false;
        app.cycleExact_ = true;
        app.toggleRun();
        app.syncPeripheralAudio();
        return app.running_ && app.peripherals_->audioOpen();
    }
#endif
    static void test_peripheral_editor(const fs::path& root)
    {
        Emu51PeripheralEditor editor;
        auto config = emu51PeripheralPreset(4);
        config.digital.clear(); config.analog.clear();
        std::string message;
        auto& io = ImGui::GetIO();
        io.DisplaySize = {1280,840}; io.DeltaTime = 1.0f/60;
        unsigned char* pixels; int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        const auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({20,20}); ImGui::SetNextWindowSize({1220,780});
            ImGui::Begin("Circuit interaction test",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize);
            editor.draw(config,Emu51Model::i8052,message);
            ImGui::End(); ImGui::Render();
        };
        frame(); frame();
        check(editor.blocks_.size() == 3 && editor.blocks_[0].title == "Intel 8052",
            "Circuit canvas includes the actual CPU model and every device");
        const auto blockSize = [&](const std::string& id) {
            const auto block = std::find_if(editor.blocks_.begin(),editor.blocks_.end(),[&](const auto& b) { return b.id == id; });
            return block == editor.blocks_.end() ? ImVec2{} : block->size;
        };
        const auto cpuSize = blockSize("cpu"), audioSize = blockSize("audio"), dacSize = blockSize("covox");
        check(cpuSize.x > 0 && cpuSize.x <= 500 && cpuSize.y < 700 &&
            audioSize.x > 0 && audioSize.x < dacSize.x && audioSize.y < dacSize.y,
            "The CPU is compact and a single-pin device takes less space than a multi-pin device");
        check(editor.pin("cpu.P0") && editor.pin("cpu.P0")->bits.size() == 8 && editor.pin("cpu.P3")->bits.size() == 8 &&
            editor.pin("cpu.ADDR") && editor.pin("cpu.DATA") && editor.pin("cpu.PSEN") &&
            editor.pin("covox.out") && editor.pin("audio.playback"),
            "Canvas exposes every CPU port bit, functional bus and analog audio pins");
        if (const auto* address = editor.pin("cpu.ADDR"); address && address->bits.size() == 16) {
            bool aligned = !address->left;
            for (int i = 0; i < 8; ++i)
                aligned &= address->bits[i].x == address->bits[i+8].x &&
                    address->bits[i].y == address->bits[0].y &&
                    address->bits[i+8].y == address->bits[8].y &&
                    address->bits[i].y < address->bits[i+8].y;
            check(aligned,"The CPU address bus uses two aligned rows of eight output bits");
        } else check(false,"The CPU address bus exposes all sixteen bits");
        check(editor.zoom_ == 0.5f,"Opening a graph keeps its compact default scale");
        editor.fit_ = true;
        editor.readableFit_ = false;
        frame();
        const auto original = config;
        const auto cpuPosition = editor.blocks_[0].position;
        const auto header = editor.screen({cpuPosition.x+35,cpuPosition.y+20});
        io.AddMousePosEvent(header.x,header.y); frame();
        io.AddMouseButtonEvent(0,true); frame();
        io.AddMousePosEvent(header.x-25,header.y+30); frame();
        check(editor.interacting(),"Paused circuit drag requests animation frames");
        io.AddMouseButtonEvent(0,false); frame();
        const auto cpu = std::find_if(editor.blocks_.begin(),editor.blocks_.end(),[](const auto& b) { return b.id == "cpu"; });
        check(cpu != editor.blocks_.end() && cpu->position.x < cpuPosition.x-20 && cpu->position.y > cpuPosition.y+20 && config == original,
            "Actual header dragging moves only presentation, without changing the circuit");
        const auto drag = [&](const std::string& a, int ab, const std::string& b, int bb) {
            const auto start = editor.anchor(*editor.pin(a),ab), end = editor.anchor(*editor.pin(b),bb);
            io.AddMousePosEvent(start.x,start.y); frame();
            io.AddMouseButtonEvent(0,true); frame();
            io.AddMousePosEvent(end.x,end.y); frame();
            io.AddMouseButtonEvent(0,false); frame();
        };
        drag("cpu.P1",-1,"covox.data",-1);
        check(config.digital.size() == 1 && config.digital[0].a == "cpu.P1" && config.digital[0].width == 8,
            "Dragging whole-port connectors creates an eight-bit digital wire");
        drag("audio.playback",-1,"covox.out",-1);
        check(config.analog.size() == 1 && config.analog[0].from == "covox.out" && config.analog[0].to == "audio.playback",
            "Dragging analog input to output normalizes signal direction");
        const auto connected = config;
        check(!editor.connect(config,{"covox.out",-1},{"cpu.P0",-1},message) && config == connected,
            "Analog-to-digital links are rejected without altering the draft");
        check(!editor.connect(config,{"covox.out",-1},{"audio.playback",-1},message) && config == connected,
            "An occupied analog input cannot receive a second source");
        config.digital.clear();
        drag("cpu.P1",2,"covox.data",5);
        check(config.digital.size() == 1 && config.digital[0].aBit == 2 && config.digital[0].bBit == 5 && config.digital[0].width == 1,
            "Dragging numbered connectors creates the selected bit permutation");
        // A port slice still uses the same SDK topology and validation path.
        config.digital = {{"cpu.P1","covox.data",2,3,3}};
        auto session = Emu51PeripheralSession::create(config,12);
        check(session->config().digital[0].width == 3,"Graph edits support multi-bit port slices");
        frame();
        if (!editor.routes_.empty() && editor.routes_.front().size() >= 2) {
            const auto& route = editor.routes_.front();
            std::size_t longest = 1;
            const auto length = [&](std::size_t i) {
                return std::abs(route[i].x-route[i-1].x)+std::abs(route[i].y-route[i-1].y);
            };
            for (std::size_t i = 2; i < route.size(); ++i)
                if (length(i) > length(longest)) longest = i;
            const auto at = editor.screen({(route[longest-1].x+route[longest].x)/2,(route[longest-1].y+route[longest].y)/2});
            io.AddMousePosEvent(at.x,at.y); frame();
            io.AddMouseButtonEvent(0,true); frame(); io.AddMouseButtonEvent(0,false); frame();
            check(editor.wire_.kind == 1 && editor.wire_.index == 0,"Clicking a routed segment selects its wire");
        } else check(false,"A port slice has a visible route");
        const auto beforeZoom = editor.zoom_;
        const auto zoomAt = editor.screen({100,100});
        io.AddMousePosEvent(zoomAt.x,zoomAt.y); frame();
        const auto beforeWorld = editor.world(io.MousePos);
        io.AddMouseWheelEvent(0,1); frame();
        const auto afterWorld = editor.world(io.MousePos);
        check(editor.zoom_ > beforeZoom && std::abs(beforeWorld.x-afterWorld.x) < 0.01f && std::abs(beforeWorld.y-afterWorld.y) < 0.01f,
            "Canvas zoom preserves the world point under the pointer");
        const auto oldPan = editor.pan_;
        const ImVec2 blank = {editor.origin_.x+editor.canvasSize_.x-10,editor.origin_.y+editor.canvasSize_.y-10};
        io.AddMousePosEvent(blank.x,blank.y); frame(); io.AddMouseButtonEvent(2,true); frame();
        io.AddMousePosEvent(blank.x-35,blank.y-20); frame(); io.AddMouseButtonEvent(2,false); frame();
        check(editor.pan_.x < oldPan.x-30 && editor.pan_.y < oldPan.y-15,"Middle dragging pans the paused canvas");

        const auto openCanvasMenu = [&]() -> ImGuiWindow* {
            const auto at = ImVec2(editor.origin_.x+editor.canvasSize_.x-25,editor.origin_.y+25);
            io.AddMousePosEvent(at.x,at.y); frame();
            io.AddMouseButtonEvent(1,true); frame(); io.AddMouseButtonEvent(1,false); frame(); frame();
            check(!GImGui->OpenPopupStack.empty(),"Right-click opens the graph popup while paused");
            return GImGui->OpenPopupStack.empty() ? nullptr : GImGui->OpenPopupStack.back().Window;
        };
        const auto openSubmenu = [&](const char* label) -> ImGuiWindow* {
            auto* popup = openCanvasMenu();
            if (!popup) return nullptr;
            ImGui::ActivateItemByID(popup->GetID(label)); frame(); frame();
            check(GImGui->OpenPopupStack.Size == 2,"Graph popup opens its requested submenu");
            return GImGui->OpenPopupStack.Size == 2 ? GImGui->OpenPopupStack.back().Window : nullptr;
        };
        const auto beforeRightPan = editor.pan_;
        const ImVec2 panAt = {editor.origin_.x+editor.canvasSize_.x-40,editor.origin_.y+editor.canvasSize_.y-40};
        io.AddMousePosEvent(panAt.x,panAt.y); frame(); io.AddMouseButtonEvent(1,true); frame();
        io.AddMousePosEvent(panAt.x-35,panAt.y-20); frame(); io.AddMouseButtonEvent(1,false); frame(); frame();
        check(editor.pan_.x < beforeRightPan.x-30 && editor.pan_.y < beforeRightPan.y-15 && GImGui->OpenPopupStack.empty(),
            "Right dragging pans without opening the context menu on release");
        const float inspectorCanvasWidth = editor.canvasSize_.x;
        if (auto* popup = openCanvasMenu()) { ImGui::ActivateItemByID(popup->GetID("Inspector")); frame(); frame(); }
        check(!editor.inspectorVisible() && editor.canvasSize_.x > inspectorCanvasWidth+250,
            "Popup inspector toggle collapses the panel and gives its width to the canvas");
        if (auto* popup = openCanvasMenu()) { ImGui::ActivateItemByID(popup->GetID("Inspector")); frame(); frame(); }
        check(editor.inspectorVisible(),"Popup restores the inspector");
        const auto deviceCount = config.devices.size();
        if (auto* submenu = openSubmenu("Add")) {
            const auto at = editor.popupPosition_;
            ImGui::ActivateItemByID(submenu->GetID("Voltage source")); frame(); frame();
            check(config.devices.size() == deviceCount+1 && config.devices.back().kind == Emu51PeripheralKind::Voltage,
                "Add submenu creates the selected device type");
            const auto id = config.devices.back().id;
            const auto block = std::find_if(editor.blocks_.begin(),editor.blocks_.end(),[&](const auto& b) { return b.id == id; });
            check(block != editor.blocks_.end() && std::abs(block->position.x-at.x) < 0.01f && std::abs(block->position.y-at.y) < 0.01f,
                "New blocks use the original right-click world position");
            editor.removeDevice(config,id); frame();
        }
        if (auto* submenu = openSubmenu("Add")) {
            ImGui::ActivateItemByID(submenu->GetID("Bit Inverter")); frame(); frame();
            const auto& inverter = config.devices.back();
            const auto id = inverter.id;
            const auto* input = editor.pin(id+".IN");
            const auto* output = editor.pin(id+".OUT");
            check(config.devices.size() == deviceCount+1 && inverter.kind == Emu51PeripheralKind::Inverter &&
                input && input->left && input->descriptor.width == 1 && output && !output->left && output->descriptor.width == 1,
                "Graph adds an inverter with a one-bit input and a one-bit output");
            if (config.devices.size() == deviceCount+1) editor.removeDevice(config,id);
            frame();
        }
        if (auto* submenu = openSubmenu("Fit")) {
            ImGui::ActivateItemByID(submenu->GetID("Actual size (100%)")); frame(); frame();
            check(editor.zoom_ == 1,"Fit submenu can restore actual size");
        }
        editor.selected_ = "cpu";
        if (auto* submenu = openSubmenu("Fit")) {
            ImGui::ActivateItemByID(submenu->GetID("Selected block")); frame(); frame();
            const auto block = std::find_if(editor.blocks_.begin(),editor.blocks_.end(),[](const auto& b) { return b.id == "cpu"; });
            const auto center = editor.screen({block->position.x+block->size.x/2,block->position.y+block->size.y/2});
            check(std::abs(center.x-editor.origin_.x-editor.canvasSize_.x/2) < 1 &&
                std::abs(center.y-editor.origin_.y-editor.canvasSize_.y/2) < 1,"Selected-block fit centers that block");
        }
        if (auto* submenu = openSubmenu("Fit")) {
            ImGui::ActivateItemByID(submenu->GetID("All blocks")); frame(); frame();
            for (const auto& b : editor.blocks_) {
                const auto lo = editor.screen(b.position), hi = editor.screen({b.position.x+b.size.x,b.position.y+b.size.y});
                check(lo.x >= editor.origin_.x && hi.x <= editor.origin_.x+editor.canvasSize_.x &&
                    lo.y >= editor.origin_.y && hi.y <= editor.origin_.y+editor.canvasSize_.y,"Fit all keeps every block in view");
            }
        }
        auto* editorWindow = ImGui::FindWindowByName("Circuit interaction test");
        const auto beforeRename = config;
        editor.selected_ = "covox"; editor.wire_ = {}; frame();
        ImGuiWindow* inspector = nullptr;
        for (auto* window : GImGui->Windows)
            if (window->ParentWindow == editorWindow && std::strstr(window->Name,"circuit-inspector")) inspector = window;
        check(inspector != nullptr,"Selected block exposes its inspector");
        if (inspector) {
            ImGui::ActivateItemByID(inspector->GetID("##block-name"));
            GImGui->NavNextActivateFlags |= ImGuiActivateFlags_PreferInput;
            frame();
            io.AddInputCharactersUTF8("Lead Covox"); frame();
            io.AddKeyEvent(ImGuiKey_Enter,true); frame(); io.AddKeyEvent(ImGuiKey_Enter,false); frame();
            const auto label = std::find_if(editor.blocks_.begin(),editor.blocks_.end(),[](const auto& b) { return b.id == "covox"; });
            check(label != editor.blocks_.end() && label->title == "Lead Covox" && config == beforeRename,
                "Typing a block name updates the visible title");
        }
        const auto namedConfig = config;
        check(editor.rename("cpu","Main CPU") && !editor.rename("cpu","bad\nname") && !editor.rename("cpu",std::string(129,'x')) && config == namedConfig,
            "CPU naming is presentation only and rejects invalid labels");

        const auto namedPositions = editor.positions(config);
        if (auto* popup = openCanvasMenu()) { ImGui::ActivateItemByID(popup->GetID("Arrange blocks")); frame(); frame(); }
        const auto arrangedBlock = [&](const char* id) -> const auto& {
            return *std::find_if(editor.blocks_.begin(),editor.blocks_.end(),[&](const auto& block) { return block.id == id; });
        };
        check(arrangedBlock("cpu").position.x < arrangedBlock("covox").position.x &&
            arrangedBlock("covox").position.x < arrangedBlock("audio").position.x,
            "Arrange follows the connected CPU, DAC and audio inputs from left to right");
        for (const auto& old : namedPositions) {
            const auto current = std::find_if(editor.positions_.begin(),editor.positions_.end(),[&](const auto& p) { return p.id == old.id; });
            check(current != editor.positions_.end() && current->name == old.name,"Arranging blocks preserves user names");
        }
        editor.restore(namedPositions); frame();

        Emu51PeripheralEditor routing;
        auto circuit = emu51PeripheralPreset(1);
        Emu51PeripheralDevice obstacle;
        obstacle.id = "obstacle";
        obstacle.kind = Emu51PeripheralKind::AddressDecoder;
        for (int i = 0; i < 8; ++i) obstacle.decoderOutputs.push_back({"CS"+std::to_string(i)});
        circuit.devices.push_back(obstacle);
        routing.restore({{"cpu",0,0},{"obstacle",350,0},{"dac",700,0}});
        routing.rebuild(circuit,Emu51Model::i8052);
        const auto links = routing.connections(circuit);
        const auto lead = [&](const char* name, int bit = -1) {
            const auto* p = routing.pin(name);
            return p ? routing.routeEnd(*p,bit).leadLength : 0.0f;
        };
        check(lead("obstacle.CS0") > 0 && lead("obstacle.CS0") == lead("obstacle.CS7") &&
            lead("obstacle.CS0") < lead("obstacle.CS1") && lead("obstacle.CS1") < lead("obstacle.CS3") &&
            lead("obstacle.CS3") == lead("obstacle.CS4"),
            "Connectors fan out symmetrically with shorter leads at the top and bottom");
        check(lead("cpu.P2",0) > lead("cpu.P0",-1),"Numbered bits use their own escape position for the lead");
        if (links.size() == 1) {
            auto& target = *std::find_if(routing.blocks_.begin(),routing.blocks_.end(),[](const auto& b) { return b.id == "dac"; });
            auto& blocker = *std::find_if(routing.blocks_.begin(),routing.blocks_.end(),[](const auto& b) { return b.id == "obstacle"; });
            const auto& link = links.front();
            const float alignment = routing.routeEnd(*link.from,link.fromBit).edge.y-routing.routeEnd(*link.to,link.toBit).edge.y;
            routing.moveBlock(target,{target.position.x,target.position.y+alignment});
            routing.updateRoutes(links);
            const auto& detour = routing.routes_.front();
            const auto& curved = routing.roundedRoutes_.front();
            bool clear = detour.size() >= 4;
            for (std::size_t i = 1; i < detour.size(); ++i) {
                const auto a = detour[i-1], b = detour[i];
                clear &= a.x == b.x || a.y == b.y;
                for (const auto& block : routing.blocks_) {
                    const auto lo = block.position;
                    const ImVec2 hi{lo.x+block.size.x,lo.y+block.size.y};
                    clear &= std::max(a.x,b.x) <= lo.x || std::min(a.x,b.x) >= hi.x ||
                        std::max(a.y,b.y) <= lo.y || std::min(a.y,b.y) >= hi.y;
                }
            }
            check(clear,"An aligned wire detours around every obstructing block using orthogonal segments");
            if (curved.size() >= 4) {
                const auto a = curved.front(), next = curved[1], b = curved.back(), previous = curved[curved.size()-2];
                check(next.y == a.y && next.x-a.x >= lead(link.from->descriptor.name.c_str(),link.fromBit)-16 &&
                    previous.y == b.y && b.x-previous.x >= lead(link.to->descriptor.name.c_str(),link.toBit)-16,
                    "Rounded wires preserve the preferred clearance at both box edges");
                bool turns = false, outside = true;
                for (std::size_t i = 1; i < curved.size(); ++i) {
                    const auto p = curved[i-1], q = curved[i];
                    turns |= p.x != q.x && p.y != q.y;
                    for (const auto& block : routing.blocks_) {
                        const auto lo = block.position;
                        const ImVec2 hi{lo.x+block.size.x,lo.y+block.size.y};
                        outside &= std::max(p.x,q.x) <= lo.x || std::min(p.x,q.x) >= hi.x ||
                            std::max(p.y,q.y) <= lo.y || std::min(p.y,q.y) >= hi.y;
                    }
                }
                check(turns && outside,"Rounded bends stay outside every box");
            } else check(false,"The detour renders rounded corners");
            routing.moveBlock(blocker,{blocker.position.x,1000});
            routing.updateRoutes(links);
            check(routing.routes_.front().size() == 2,"Moving an obstacle out of the way restores a straight route");

            const auto originalTarget = target.position;
            const auto source = routing.routeEnd(*link.from,link.fromBit).edge;
            routing.moveBlock(target,{source.x+96,originalTarget.y+120});
            routing.updateRoutes(links);
            const auto& narrow = routing.roundedRoutes_.front();
            check(narrow.size() >= 4 && narrow[1].y == narrow.front().y && narrow[1].x-narrow.front().x >= 16 &&
                narrow[narrow.size()-2].y == narrow.back().y && narrow.back().x-narrow[narrow.size()-2].x >= 16,
                "Facing connectors fit straight leads and rounded turns in a narrow channel");
            routing.moveBlock(target,originalTarget);
        } else check(false,"The routing fixture has its digital wire");

        auto dac = circuit.devices.front();
        dac.id = "second_dac";
        circuit.devices.push_back(dac);
        auto branch = circuit.digital.front();
        branch.b = "second_dac.data";
        circuit.digital.push_back(branch);
        auto layout = routing.positions(circuit);
        layout.push_back({"second_dac",700,220});
        routing.restore(layout);
        routing.rebuild(circuit,Emu51Model::i8052);
        routing.updateRoutes(routing.connections(circuit));
        if (routing.routes_.size() == 2 && routing.routes_[0].size() == 2 && routing.routes_[1].size() >= 3) {
            const auto& straight = routing.routes_[0];
            const auto& branchRoute = routing.routes_[1];
            float shared = 0;
            for (std::size_t i = 1; i < branchRoute.size(); ++i) {
                const auto a = branchRoute[i-1], b = branchRoute[i];
                if (a.y == straight[0].y && b.y == a.y)
                    shared += std::max(0.0f,std::min(std::max(a.x,b.x),straight[1].x)-std::max(std::min(a.x,b.x),straight[0].x));
            }
            check(shared < (straight[1].x-straight[0].x)*0.1f,"Fan-out wires leave a shared port on separate tracks");
        } else check(false,"The routing fixture shows both fan-out branches");

        std::string boardError;
        const auto board = emu51LoadProject(fs::path(EMU51_REPO_ROOT)/"progs/boards/avt2250/avt2250.emu51",boardError);
        if (board) {
            routing.restore(board->peripheralLayout);
            routing.rebuild(board->peripherals,Emu51Model::i8051);
            routing.arrange(board->peripherals);
            bool bitsStayOnSide = true;
            for (const auto& block : routing.blocks_) for (const auto& pin : block.pins) {
                const float midpoint = block.position.x+block.size.x/2;
                for (std::size_t i = 0; i < pin.bits.size(); ++i) {
                    const auto end = routing.routeEnd(pin,static_cast<int>(i));
                    const float edge = block.position.x+(pin.left ? 0 : block.size.x);
                    bitsStayOnSide &= (pin.left ? end.port.x+12.5f < midpoint : end.port.x-12.5f > midpoint) &&
                        end.edge.x == edge && end.edge.y > end.port.y+10.5f && end.edge.y < end.port.y+29;
                }
            }
            check(bitsStayOnSide,"Bit buttons stay in their own half and leads drop below them before reaching the correct edge");
            routing.updateRoutes(routing.connections(board->peripherals));
            check(!routing.routes_.empty() && std::all_of(routing.routes_.begin(),routing.routes_.end(),
                [](const auto& route) { return route.size() >= 2; }),"Every connection in the arranged AVT-2250 has a route");
        } else check(false,("Load the public AVT-2250 circuit for routing: "+boardError).c_str());

        auto app = create();
        check(app->applyPeripheralConfig(config,12),"Canvas circuit applies through the normal transactional path");
        app->peripheralEditor_ = editor;
        app->cpu_.PC = 0x1234;
        const auto savedLayout = editor.positions(config);
        const auto path = root/"circuit-layout.emu51";
        check(app->saveProjectToPath(path),"Projects save graphical block positions");
        auto restored = create();
        check(restored->openProjectFromPath(path) && restored->peripheralEditor_.positions(config) == savedLayout,
            "Project reopening restores CPU and device positions");
        check(app->cpu_.PC == 0x1234,"Saving graphical layout never resets the CPU");
        std::string error;
        auto project = emu51LoadProject(path,error);
        check(project && project->peripheralLayout == savedLayout,"Layout serialization preserves fractional coordinates");
        if (project) {
            const auto validBytes = read(path);
            project->peripheralLayout.push_back(project->peripheralLayout.front());
            check(!emu51SaveProject(path,*project,error) && read(path) == validBytes,"Duplicate block layouts fail atomically");
            project->peripheralLayout = {{"missing",0,0}};
            check(!emu51SaveProject(path,*project,error),"Layout IDs must refer to the CPU or a configured device");
            project->peripheralLayout = {{"cpu",100001,0}};
            check(!emu51SaveProject(path,*project,error),"Out-of-range canvas coordinates are rejected");
            auto old = validBytes;
            const auto begin = old.find("\"peripheral_layout\"");
            const auto end = old.find(']',begin);
            check(begin != std::string::npos && end != std::string::npos,"Project layout is an optional JSON array");
            if (begin != std::string::npos && end != std::string::npos) {
                old.erase(begin,old.find(',',end)-begin+1); write(path,old);
                const auto legacy = emu51LoadProject(path,error);
                check(legacy && legacy->peripheralLayout.empty() && legacy->peripherals == config,
                    "Existing projects without graphical layout still load their circuits");
            }
        }
        editor.removeDevice(config,"covox");
        check(config.digital.empty() && config.analog.empty() && editor.positions(config).size() == 2,
            "Removing a block removes attached analog/digital wires and its saved position");
        editor.removeDevice(config,"cpu");
        editor.rebuild(config,Emu51Model::i8051);
        check(editor.pin("cpu.P0") != nullptr,"The CPU block cannot be removed");
        for (int preset = 1; preset <= 4; ++preset) {
            const auto c = emu51PeripheralPreset(preset); editor.restore({}); editor.rebuild(c,Emu51Model::i8051);
            const auto pins = Emu51PeripheralSession::describe(c,true);
            for (const auto& p : pins) check(editor.pin(p.name) != nullptr,"Every described endpoint appears in every preset");
            if (preset == 2) check(editor.pin("ppi.bus") && editor.pin("ppi.RESET") && editor.pin("adc.plus") && editor.pin("adc.minus"),
                "Bus, reset and both analog ADC inputs remain visible");
        }
        io.AddMousePosEvent(-100,-100);
    }
    static void test_peripheral_window()
    {
        using PaneKey = Emu51ImguiApp::PaneKey;
        auto app = create(); app->peripheralsOpen_ = true;
        app->peripheralDraft_ = emu51PeripheralPreset(2);
        app->peripheralMessage_ = "Graph status belongs in the popup, not above the canvas";
        auto& io = ImGui::GetIO(); io.DisplaySize = {1280,840}; io.DeltaTime = 1.0f/60;
        const PaneKey pane{PaneKey::Kind::Peripherals,0};
        const auto frame = [&] {
            ImGui::NewFrame(); app->updateDragSnapState(); app->updateSnappedPaneLayout(); app->drawPeripherals(); ImGui::Render();
        };
        frame(); frame();
        auto* window = ImGui::FindWindowByName("Peripherals");
        check(window != nullptr,"Peripherals registers its own dockable window");
        if (!window) return;
        const auto* tabs = GImGui->TabBars.GetByKey(window->GetID("peripheral-tabs"));
        check(tabs && std::abs(tabs->BarRect.Min.y-window->TitleBarRect().Min.y) < 1 &&
            tabs->BarRect.Max.y <= window->TitleBarRect().Max.y &&
            tabs->BarRect.Min.x > window->Pos.x+ImGui::CalcTextSize("Peripherals").x,
            "Peripherals tabs share the title bar to the right of the window name");
        ImGuiWindow* canvasWindow = nullptr;
        for (auto* child : GImGui->Windows)
            if (child->ParentWindow == window && std::strstr(child->Name,"circuit-area")) canvasWindow = child;
        check(tabs && canvasWindow && std::abs(canvasWindow->Pos.y-window->DC.CursorStartPos.y) < 1 &&
            canvasWindow->Pos.y-tabs->BarRect.Max.y <= ImGui::GetStyle().WindowPadding.y+1,
            "Canvas uses the full content height below the integrated title bar");
        const auto click = [&](ImVec2 at) {
            io.AddMousePosEvent(at.x,at.y); frame(); io.AddMouseButtonEvent(0,true); frame();
            io.AddMouseButtonEvent(0,false); frame(); frame();
        };
        const auto chooseTab = [&](int index) {
            const auto& tab = tabs->Tabs[index];
            click({tabs->BarRect.Min.x+tab.Offset-tabs->ScrollingAnim+tab.Width/2,
                (tabs->BarRect.Min.y+tabs->BarRect.Max.y)/2});
            check(tabs->SelectedTabId == tab.ID,"Title-bar tabs select their corresponding content");
        };
        const auto floatingPosition = window->Pos;
        ImGuiWindow* inspector = nullptr;
        for (auto* child : GImGui->Windows)
            if (child->ParentWindow == window && std::strstr(child->Name,"circuit-inspector")) inspector = child;
        check(inspector != nullptr,"Graph opens with an Inspector header");
        if (inspector) {
            const float canvasWidth = app->peripheralEditor_.canvasSize_.x;
            const float font = ImGui::GetFontSize();
            const auto& style = ImGui::GetStyle();
            ImGui::SetWindowPos(window,{io.DisplaySize.x-window->Size.x+72,floatingPosition.y});
            frame(); frame();
            const float inspectorRight = std::min(inspector->WorkRect.Max.x,inspector->ClipRect.Max.x-style.FramePadding.x);
            check(inspectorRight < inspector->WorkRect.Max.x,"The floating Inspector is partially clipped by the viewport");
            app->layoutStateDirty_ = false;
            click({inspectorRight-font/2,inspector->DC.CursorStartPos.y+font/2});
            check(!app->peripheralEditor_.inspectorVisible() && app->peripheralEditor_.canvasSize_.x > canvasWidth &&
                app->layoutStateDirty_,"The Inspector header close button expands the graph and saves the visibility change");
            chooseTab(1);
            const float inspectorEnd = std::min(window->OuterRectClipped.Max.x-style.FramePadding.x,
                window->TitleBarRect().Max.x-font-ImGui::CalcTextSize("Apply").x-3*style.FramePadding.x-3*style.ItemInnerSpacing.x
                -(style.WindowMenuButtonPosition == ImGuiDir_Right ? font+style.ItemInnerSpacing.x : 0));
            const float inspectorWidth = ImGui::CalcTextSize("Inspector").x+2*style.FramePadding.x;
            app->layoutStateDirty_ = false;
            click({inspectorEnd-inspectorWidth/2,window->TitleBarRect().GetCenter().y});
            check(app->peripheralEditor_.inspectorVisible() && tabs->SelectedTabId == tabs->Tabs[0].ID &&
                app->layoutStateDirty_,"The title-bar Inspector button restores the pane and selects Graph");
            ImGui::SetWindowPos(window,floatingPosition); frame(); frame();
        }
        for (int tab : {1,2,3,0}) chooseTab(tab);
        check(window->Pos.x == floatingPosition.x && window->Pos.y == floatingPosition.y && !window->Collapsed && !app->dragSnapActive_,
            "Clicking title-bar tabs neither moves nor collapses or docks the window");
        const auto draft = app->peripheralDraft_;
        app->peripheralDraft_.digital.resize(64,draft.digital.front());
        chooseTab(1);
        ImGui::SetScrollY(window,120); frame(); frame();
        check(window->Scroll.y > 0 && std::abs(tabs->BarRect.Min.y-window->TitleBarRect().Min.y) < 1,
            "Scrolling tab content leaves the integrated tab bar fixed in the title");
        chooseTab(0); app->peripheralDraft_ = draft; frame(); frame();
        PaneKey registered;
        check(app->paneFromWindowId(window->ID,&registered) && registered == pane,
            "Peripherals participates in the same title drag docking path as other panes");
        const auto title = ImVec2(window->Pos.x+50,window->TitleBarRect().GetCenter().y);
        io.AddMousePosEvent(title.x,title.y); frame(); io.AddMouseButtonEvent(0,true); frame();
        io.AddMousePosEvent(80,420); frame(); io.AddMouseButtonEvent(0,false); frame(); frame();
        check(app->snapTargetForPane(pane) == Emu51ImguiSnapTarget::Left && window->Size.x < 780,
            "Actual title-bar dragging docks Peripherals and removes the old oversized minimum");
        check(app->hasActivePane() && app->activePane_ == pane,"Docked Peripherals is available to the Window snap commands");
        check(tabs->ScrollButtonEnabled,"Narrow docked title bars expose tab scrolling controls");
        const float arrowWidth = ImGui::GetFontSize()-2;
        for (int i = 0; i < 3; ++i) {
            click({tabs->BarRect.Max.x+1+arrowWidth*1.5f,tabs->BarRect.GetCenter().y});
        }
        check(tabs->SelectedTabId == tabs->Tabs[3].ID && !window->Collapsed,
            "Title-bar arrows reach the Live tab in a narrow dock");
        for (int i = 0; i < 3; ++i) click({tabs->BarRect.Max.x+1+arrowWidth*0.5f,tabs->BarRect.GetCenter().y});
        check(tabs->SelectedTabId == tabs->Tabs[0].ID,"Title-bar arrows return to Graph without hitting Close");
        app->snapActivePane(Emu51ImguiSnapTarget::Right); frame();
        check(app->snapTargetForPane(pane) == Emu51ImguiSnapTarget::Right && window->Pos.x > 600,
            "Window snap commands reposition Peripherals");
        app->peripheralEditor_.setInspectorVisible(false);
        const auto saved = stateJson(*app);
        auto restored = create(); restoreState(*restored,saved);
        check(restored->snapTargetForPane(pane) == Emu51ImguiSnapTarget::Right && !restored->peripheralEditor_.inspectorVisible(),
            "Docking and inspector visibility survive layout serialization");
        app->snapActivePane(Emu51ImguiSnapTarget::None); frame();
        check(window->Size.x >= 1000 && !(window->Flags & ImGuiWindowFlags_NoResize),
            "Undocking restores the floating size and resize handles");
        // The menu Float command keeps the dock's origin; move the restored,
        // wider window fully on-screen before testing its actual close button.
        ImGui::SetWindowPos(window,ImVec2(20,20)); frame();
        click({window->TitleBarRect().Max.x-ImGui::GetStyle().FramePadding.x-ImGui::GetFontSize()/2,
            window->TitleBarRect().GetCenter().y});
        check(!app->peripheralsOpen_ && !app->paneExists(pane),"The close button beside the title-bar tabs still closes Peripherals");
        io.AddMousePosEvent(-100,-100);
        const PaneKey registers{PaneKey::Kind::Registers,0};
        app->setPaneSnapTarget(registers,Emu51ImguiSnapTarget::Left);
        const auto registersFrame = [&] {
            ImGui::NewFrame(); app->updateSnappedPaneLayout(); app->drawRegisters(); ImGui::Render();
        };
        registersFrame(); registersFrame();
        auto* registersWindow = ImGui::FindWindowByName("Registers");
        check(registersWindow && app->registersVisible(),"Registers starts visible and retains docking support");
        if (registersWindow) {
            ImGui::ActivateItemByID(registersWindow->GetID("#CLOSE")); registersFrame(); registersFrame();
            check(!app->registersVisible() && !app->paneExists(registers) && app->snappedPaneRects_.empty(),
                "Closing Registers hides the pane and releases its docked space");
            const auto closed = stateJson(*app);
            auto reloaded = create(); restoreState(*reloaded,closed);
            check(!reloaded->registersVisible(),"Registers visibility survives application layout persistence");
            app->setRegistersVisible(true); registersFrame();
            check(app->registersVisible() && !app->snappedPaneRects_.empty() && GImGui->NavWindow == registersWindow,
                "Window menu visibility command reopens and focuses Registers in its previous dock");
        }
    }
    static void test_peripheral_title_apply(const fs::path& root)
    {
        using PaneKey = Emu51ImguiApp::PaneKey;
        auto app = create(); app->peripheralsOpen_ = true;
        const auto config = emu51PeripheralPreset(1);
        check(app->applyPeripheralConfig(config,12),"Title-bar Apply fixture starts with a DAC circuit");
        auto& io = ImGui::GetIO(); io.DisplaySize = {1280,840}; io.DeltaTime = 1.0f/60;
        const auto frame = [&] {
            ImGui::NewFrame(); app->updateDragSnapState(); app->updateSnappedPaneLayout(); app->drawPeripherals(); ImGui::Render();
        };
        frame(); frame();
        auto* window = ImGui::FindWindowByName("Peripherals");
        check(window != nullptr,"Apply fixture has a Peripherals title bar");
        if (!window) return;
        ImGui::SetWindowPos(window,{20,20}); frame();
        const auto clickApply = [&] {
            const auto title = window->TitleBarRect();
            const auto& style = ImGui::GetStyle();
            const float end = title.Max.x-style.FramePadding.x-ImGui::GetFontSize()-2*style.ItemInnerSpacing.x;
            const float width = ImGui::CalcTextSize("Apply").x+2*style.FramePadding.x;
            io.AddMousePosEvent(end-width/2,title.GetCenter().y); frame();
            io.AddMouseButtonEvent(0,true); frame(); io.AddMouseButtonEvent(0,false); frame(); frame();
        };
        app->cpu_.PC = 0x1234;
        clickApply();
        check(app->cpu_.PC == 0x1234,"Apply is disabled when the graph has no pending changes");
        app->peripheralEditor_.rename("dac","Output DAC");
        const auto layout = app->peripheralEditor_.positions(config);
        auto changed = config;
        changed.digital.front().a = "cpu.P2"; // Use a port with pull-ups for the DAC.
        changed.devices.front().referenceVolts = 3.3; // Inspector device setting.
        app->peripheralDraft_ = changed; app->running_ = true;
        const auto path = root/"title-applied.emu51";
        check(app->saveProjectToPath(path),"Save before Apply succeeds without activating draft edits");
        std::string error;
        const auto beforeApply = emu51LoadProject(path,error);
        check(beforeApply && beforeApply->peripherals == config && app->cpu_.PC == 0x1234 && app->running_,
            "Unapplied wiring and inspector changes do not leak into Save Project or reset CPU");
        clickApply();
        check(app->peripherals_->config() == changed && app->peripheralDraft_ == changed &&
            !app->running_ && app->cpu_.PC == 0 && app->peripheralEditor_.positions(changed) == layout,
            "Clicking title-bar Apply activates wiring and inspector settings, resets/pauses CPU and keeps block presentation");
        check(!window->Collapsed && app->snapTargetForPane({PaneKey::Kind::Peripherals,0}) == Emu51ImguiSnapTarget::None,
            "Clicking Apply neither collapses nor docks the window");
        app->cpu_.writeDirect(0xa0,128);
        double voltage = 0;
        check(app->peripherals_->sampleAnalog("dac.out",voltage) && std::abs(voltage-1.65) < 1e-6,
            "Applied wiring and reference voltage actually drive the DAC from P2");
        app->cpu_.PC = 0x2468;
        clickApply();
        check(app->cpu_.PC == 0x2468,"Successful Apply disables itself until another configuration change");
        check(app->saveProjectToPath(path),"Save Project accepts the title-applied circuit");
        const auto project = emu51LoadProject(path,error);
        check(project && project->peripherals == changed && project->peripheralLayout == layout,
            "Save Project includes applied edits and retains names and positions");

        app->setPaneSnapTarget({PaneKey::Kind::Peripherals,0},Emu51ImguiSnapTarget::Right); frame(); frame();
        auto* original = app->peripherals_.get();
        app->peripheralDraft_.digital.front().width = 9;
        app->cpu_.PC = 0x4321; app->running_ = true;
        clickApply();
        check(app->peripherals_.get() == original && app->cpu_.PC == 0x4321 && app->running_ &&
            app->peripheralDraft_ != changed && app->peripheralMessage_ == app->statusMessage_,
            "Docked title-bar Apply rejects invalid edits without replacing the circuit or stopping CPU");
        check(!GImGui->OpenPopupStack.empty(),"Invalid Apply visibly explains its validation error");
        if (!GImGui->OpenPopupStack.empty()) {
            auto* popup = GImGui->OpenPopupStack.back().Window;
            ImGui::ActivateItemByID(popup->GetID("Close")); frame(); frame();
        }
        check(GImGui->OpenPopupStack.empty(),"The Apply validation error can be dismissed with Close");
        app->peripheralDraft_ = {};
        clickApply();
        check(!app->peripherals_ && !app->running_ && app->cpu_.PC == 0 && app->peripheralsOpen_,
            "Apply in a narrow dock can disconnect the graph without hitting Close or the tab arrows");
        auto* tabs = GImGui->TabBars.GetByKey(window->GetID("peripheral-tabs"));
        tabs->NextSelectedTabId = ImHashStr("Live",0,tabs->ID); frame(); frame();
        app->peripheralDraft_ = config;
        clickApply();
        check(app->peripherals_ && app->peripherals_->config() == config &&
            tabs->SelectedTabId == ImHashStr("Live",0,tabs->ID),
            "Apply is available from Live and renders the newly activated session immediately");
        io.AddMousePosEvent(-100,-100);
    }
    static void test_live_labels(const fs::path& root)
    {
        auto app = create();
        check(app->applyPeripheralConfig(emu51PeripheralPreset(1),12),"Create Live label project fixture");
        auto& io = ImGui::GetIO(); io.DisplaySize = {900,650}; io.DeltaTime = 1.0f/60;
        const auto frame = [&] {
            ImGui::NewFrame(); ImGui::SetNextWindowPos({20,20}); ImGui::SetNextWindowSize({840,590});
            ImGui::Begin("Live labels regression",nullptr,ImGuiWindowFlags_NoSavedSettings);
            app->drawPeripheralLive(app->peripherals_->config());
            ImGui::End(); ImGui::Render();
        };
        const auto click = [&](ImVec2 at,int button) {
            io.AddMousePosEvent(at.x,at.y); frame();
            io.AddMouseButtonEvent(button,true); frame();
            io.AddMouseButtonEvent(button,false); frame(); frame();
        };
        const auto popup = [&](const char* id) -> ImGuiWindow* {
            for (auto* child : GImGui->Windows) {
                if (!(child->Flags&ImGuiWindowFlags_ChildWindow) || !std::strstr(child->Name,"live-canvas")) continue;
                for (const auto& item : GImGui->OpenPopupStack)
                    if (item.PopupId==child->GetID(id)) return item.Window;
            }
            return nullptr;
        };
        frame(); frame();
        const ImVec2 at{190,190};
        click(at,ImGuiMouseButton_Right);
        check(!popup("live-label-menu"),"Right click cannot edit labels outside Edit layout");
        app->peripheralLive_.editing_ = true; frame(); frame();
        const auto choose = [&](const char* item) {
            auto* menu = popup("live-label-menu");
            check(menu!=nullptr,"Right click opens the Live label menu");
            if (menu) { ImGui::ActivateItemByID(menu->GetID(item)); frame(); frame(); }
            return menu!=nullptr;
        };
        const auto text = [&](const char* label) {
            check(popup("live-label-text")!=nullptr,"Label menu action opens a text field");
            io.AddInputCharactersUTF8(label); frame();
            io.AddKeyEvent(ImGuiKey_Enter,true); frame();
            io.AddKeyEvent(ImGuiKey_Enter,false); frame();
        };
        click(at,ImGuiMouseButton_Right);
        check(app->peripheralLive_.interacting(),"Paused label menu keeps redraws active");
        if (!choose("Add label")) return;
        text("ADC input");
        auto labels = app->peripheralLive_.labels();
        check(labels.size()==1 && labels.front().text=="ADC input","Add places typed text on the canvas");
        if (labels.size()!=1) return;
        frame(); frame();
        const ImVec2 inside{at.x+10,at.y+9};
        click(inside,ImGuiMouseButton_Right);
        if (!choose("Edit label")) return;
        text("Analog input");
        labels = app->peripheralLive_.labels();
        check(labels.size()==1 && labels.front().text=="Analog input","Edit replaces the existing label text");
        if (labels.size()!=1) return;
        io.AddMousePosEvent(inside.x,inside.y); frame(); frame();
        io.AddMouseButtonEvent(0,true); frame();
        io.AddMousePosEvent(inside.x+48,inside.y+25); frame();
        check(app->peripheralLive_.interacting(),"Dragging a label keeps paused redraws active");
        io.AddMouseButtonEvent(0,false); frame(); frame();
        const auto moved = app->peripheralLive_.labels();
        check(moved[0].x>labels[0].x+40 && moved[0].y>labels[0].y+20,"Labels can be repositioned in Edit layout");

        const auto path = root/"live-labels.emu51";
        auto restored = create();
        check(app->saveProjectToPath(path) && restored->openProjectFromPath(path) &&
            restored->peripheralLive_.labels()==moved && !restored->peripheralLive_.editing(),
            "Live label text and position round-trip, with editing locked after opening");
        std::string error;
        if (auto project = emu51LoadProject(path,error); project && !project->peripheralLiveLabels.empty()) {
            project->peripheralLiveLabels[0].text = "\n";
            check(!emu51SaveProject(root/"bad-live-label.emu51",*project,error),"Invalid Live label text is rejected");
        }
        click({inside.x+48,inside.y+25},ImGuiMouseButton_Right);
        if (!choose("Remove label")) return;
        check(app->peripheralLive_.labels().empty(),"Remove label deletes the label under the cursor");
    }

    static void test_live_device_layout(const fs::path& root)
    {
        Emu51PeripheralConfig config;
        config.devices = {{"switch",Emu51PeripheralKind::Switch},{"button",Emu51PeripheralKind::Button},
            {"source",Emu51PeripheralKind::Digital},{"display",Emu51PeripheralKind::Display7},{"audio",Emu51PeripheralKind::Audio}};
        config.devices.back().audioDirections = E51P_AUDIO_CAPTURE | E51P_AUDIO_PLAYBACK;
        for (const auto& device : std::vector<Emu51PeripheralDevice>{{"ppi",Emu51PeripheralKind::I8255},
            {"adc",Emu51PeripheralKind::Adc0804},{"dac",Emu51PeripheralKind::Dac},{"shift",Emu51PeripheralKind::Shift595},
            {"eeprom",Emu51PeripheralKind::Eeprom},{"ay",Emu51PeripheralKind::Ay38910,0x8100}})
            config.devices.push_back(device);
        auto app = create();
        check(app->applyPeripheralConfig(config,12),"Live layout fixture configures interactive devices, display and audio");
        app->peripheralsOpen_ = true;
        app->peripheralLive_.restore({{"cpu",12,12,320,300},{"switch",350,12,280,180},
            {"button",650,12,280,180},{"source",350,208,280,180},{"display",650,208,340,300},{"audio",1000,12,520,680}});
        auto& io = ImGui::GetIO(); io.DisplaySize = {1280,840}; io.DeltaTime = 1.0f/60;
        const auto frame = [&] {
            ImGui::NewFrame(); ImGui::SetNextWindowPos({20,20}); ImGui::SetNextWindowSize({1200,780});
            ImGui::Begin("Live layout regression",nullptr,ImGuiWindowFlags_NoSavedSettings);
            app->drawPeripheralLive(app->peripherals_->config()); ImGui::End(); ImGui::Render();
        };
        frame(); frame();
        auto* window = ImGui::FindWindowByName("Live layout regression");
        ImGuiWindow* canvas = nullptr;
        for (auto* child : GImGui->Windows) if (child->ParentWindow == window && std::strstr(child->Name,"live-canvas")) canvas = child;
        check(canvas != nullptr,"Live devices use a scrollable layout canvas");
        if (!canvas) return;
        const auto box = [&](const char* id) -> ImGuiWindow* {
            const auto childId = ImHashStr("box",0,ImHashStr(id,0,canvas->ID));
            for (auto* child : GImGui->Windows) if (child->ParentWindow == canvas && child->ChildId == childId) return child;
            return nullptr;
        };
        for (const auto* id : {"switch","button","source","display","audio"})
            check(box(id) != nullptr,"Front-panel devices and selected utility controls have Live boxes");
        for (const auto* id : {"cpu","ppi","adc","dac","shift","eeprom","ay"})
            check(box(id) == nullptr,"Live does not create CPU or bare IC boxes");
        const auto controls = [&](const char* id) -> ImGuiWindow* {
            for (auto* child : GImGui->Windows)
                if (child->ParentWindow == box(id) && std::strstr(child->Name,"controls")) return child;
            return nullptr;
        };
        const auto click = [&](ImVec2 at) {
            io.AddMousePosEvent(at.x,at.y); frame(); io.AddMouseButtonEvent(0,true); frame();
            io.AddMouseButtonEvent(0,false); frame(); frame();
        };
        const auto drag = [&](ImVec2 at,ImVec2 by,bool shouldAnimate) {
            io.AddMousePosEvent(at.x,at.y); frame(); io.AddMouseButtonEvent(0,true); frame();
            io.AddMousePosEvent(at.x+by.x,at.y+by.y); frame();
            if (shouldAnimate) check(app->shouldAnimate(),"Paused Live dragging/resizing requests continuous redraws");
            io.AddMouseButtonEvent(0,false); frame(); frame();
        };
        auto* switchControls = controls("switch");
        check(switchControls != nullptr,"Device controls belong to their own box");
        if (!switchControls || !box("switch")) return;
        click({switchControls->DC.CursorStartPos.x+5,switchControls->DC.CursorStartPos.y+5});
        check(app->peripherals_->pressed("switch"),"A switch remains operable in locked layout mode");
        const auto locked = app->peripheralLive_.layout(app->peripherals_->config());
        drag({box("switch")->Pos.x+50,box("switch")->Pos.y+12},{30,25},false);
        check(app->peripheralLive_.layout(app->peripherals_->config()) == locked,"Headers cannot move device boxes outside edit mode");
        click({window->DC.CursorStartPos.x+6,window->DC.CursorStartPos.y+6});
        check(app->peripheralLive_.editing(),"Edit layout is enabled by the visible checkbox");
        click({switchControls->DC.CursorStartPos.x+5,switchControls->DC.CursorStartPos.y+5});
        check(app->peripherals_->pressed("switch"),"Layout edit mode disables device controls instead of toggling hardware");
        app->cpu_.PC = 0x1234;
        const auto active = app->peripherals_->config();
        drag({box("switch")->Pos.x+50,box("switch")->Pos.y+12},{35,25},true);
        const auto moved = app->peripheralLive_.layout(active);
        const auto geometry = [](const auto& boxes) {
            return *std::find_if(boxes.begin(),boxes.end(),[](const auto& b) { return b.id == "switch"; });
        };
        check(geometry(moved).x > geometry(locked).x+30 && geometry(moved).y > geometry(locked).y+20,"Dragging a header updates the Live device position");
        auto* switchBox = box("switch");
        drag({switchBox->Pos.x+switchBox->Size.x-9,switchBox->Pos.y+switchBox->Size.y-9},{45,35},true);
        const auto resized = app->peripheralLive_.layout(active);
        check(geometry(resized).width > geometry(moved).width+40 && geometry(resized).height > geometry(moved).height+30,
            "The corner handle resizes a device box");
        check(app->cpu_.PC == 0x1234 && app->peripherals_->config() == active && app->peripheralDraft_ == active,
            "Live layout edits do not reset CPU or change the circuit draft");
        const auto projectPath = root/"live-layout.emu51";
        check(app->saveProjectToPath(projectPath),"Save Project stores the Live layout");
        auto restored = create();
        check(restored->openProjectFromPath(projectPath) && restored->peripheralLive_.layout(active) == resized &&
            !restored->peripheralLive_.editing(),"Live positions and sizes round-trip through project JSON; edit mode starts locked");
        std::string error;
        auto project = emu51LoadProject(projectPath,error);
        check(project && project->peripheralLiveLayout == resized,"Kason restores all Live boxes independently of Graph layout");
        if (project) {
            const auto bytes = read(projectPath);
            for (int invalid = 0; invalid < 5; ++invalid) {
                project->peripheralLiveLayout = resized;
                if (invalid == 0) project->peripheralLiveLayout.push_back(resized.front());
                if (invalid == 1) project->peripheralLiveLayout.front().id = "missing";
                if (invalid == 2) project->peripheralLiveLayout.front().x = -1;
                if (invalid == 3) project->peripheralLiveLayout.front().width = 0;
                if (invalid == 4) project->peripheralLiveLayout.front().height = 4097;
                check(!emu51SaveProject(projectPath,*project,error) && read(projectPath) == bytes,
                    "Invalid Live layout IDs, duplicates, positions and sizes cannot overwrite a project");
            }
            auto invalidJson = bytes;
            const auto begin = invalidJson.find("\"peripheral_live_layout\"");
            const auto width = invalidJson.find("\"width\":",begin)+8;
            const auto end = invalidJson.find(',',width);
            invalidJson.replace(width,end-width,"0");
            const auto invalidPath = root/"invalid-live-layout.emu51";
            write(invalidPath,invalidJson);
            restored->cpu_.PC = 0xbeef; auto* originalSession = restored->peripherals_.get();
            check(!restored->openProjectFromPath(invalidPath) && restored->cpu_.PC == 0xbeef &&
                restored->peripherals_.get() == originalSession && restored->peripheralLive_.layout(active) == resized,
                "An invalid Live layout from JSON cannot partially replace the running project");
        }
        click({window->DC.CursorStartPos.x+6,window->DC.CursorStartPos.y+6});
        check(!app->peripheralLive_.editing(),"Leaving edit mode locks the layout again");
        const auto* buttonControls = controls("button");
        if (buttonControls) {
            const auto at = buttonControls->DC.CursorStartPos;
            io.AddMousePosEvent(at.x+120,at.y+10); frame(); io.AddMouseButtonEvent(0,true); frame();
            check(app->peripherals_->pressed("button"),"Momentary buttons work inside their own box");
            app->peripheralLive_.editing_ = true; frame();
            check(!app->peripherals_->pressed("button"),"Entering layout mode releases held momentary buttons");
            io.AddMouseButtonEvent(0,false); frame();
        }
        app->peripheralLive_.restore({{"audio",12,12,360,400},{"cpu",650,12,350,300}});
        frame(); frame();
        auto* audioControls = controls("audio");
        check(audioControls != nullptr,"All audio options are hosted in the audio device's own box");
        if (audioControls) {
            auto* tabs = GImGui->TabBars.GetByKey(audioControls->GetID("audio-options"));
            check(tabs && tabs->Tabs.Size == 2,"The audio box contains Signals and Devices tabs");
            if (tabs) for (const auto* tab : {"Devices","Signals"}) {
                tabs->NextSelectedTabId = ImHashStr(tab,0,tabs->ID); frame(); frame();
                check(audioControls->DC.CursorMaxPos.x <= audioControls->InnerRect.Max.x+1,
                    "Audio controls, gain, scopes and statistics fit horizontally inside the minimum-width box");
            }
        }
        check(!app->peripherals_->audioOpen(),"Rendering and rearranging Live boxes never opens physical audio devices");
        // Compact also upgrades the older, monitor-sized saved layouts.
        click({window->DC.CursorStartPos.x+6,window->DC.CursorStartPos.y+6});
        ImGui::FocusWindow(window); ImGui::ActivateItemByID(window->GetID("Compact")); frame(); frame();
        const auto compact = app->peripheralLive_.layout(app->peripherals_->config());
        check(compact.size() == 5 && std::none_of(compact.begin(),compact.end(),[](const auto& b) { return b.id == "cpu"; }),
            "Default and saved Live layouts contain only front-panel devices, audio and adjustable sources");
        for (const auto& b : compact) {
            if (b.id == "switch" || b.id == "button") check(b.width <= 160 && b.height <= 90,"Physical controls have compact default boxes");
            if (b.id == "display") check(b.height <= 150,"Segment displays do not reserve space for internal signals");
            if (b.id == "audio") check(b.width <= 420 && b.height <= 400,"Audio uses a compact default box");
        }
        const auto compactPath = root/"compact-front-panel.emu51";
        auto compactRestored = create();
        check(app->saveProjectToPath(compactPath) && compactRestored->openProjectFromPath(compactPath) &&
            compactRestored->peripheralLive_.layout(app->peripherals_->config()) == compact,
            "Compact boxes smaller than the old monitor limits survive project save/reload");
        io.AddMousePosEvent(-100,-100);
    }
    static void test_live_content_scaling_and_key_modes(const fs::path& root)
    {
        Emu51PeripheralConfig config;
        config.devices = {{"lcd",Emu51PeripheralKind::Lcd},{"segments",Emu51PeripheralKind::Display7},
            {"bar",Emu51PeripheralKind::Bargraph},{"led",Emu51PeripheralKind::Led},{"switch",Emu51PeripheralKind::Switch},
            {"button",Emu51PeripheralKind::Button},{"source",Emu51PeripheralKind::Voltage},{"digital",Emu51PeripheralKind::Digital},
            {"audio",Emu51PeripheralKind::Audio},{"keypad",Emu51PeripheralKind::Keypad}};
        config.devices[8].audioDirections = E51P_AUDIO_CAPTURE | E51P_AUDIO_PLAYBACK;
        auto app = create(); app->peripheralsOpen_ = true;
        check(app->applyPeripheralConfig(config,12),"Configure every visible device type for scaling and keypad regression");
        auto& io = ImGui::GetIO(); io.DisplaySize = {1280,900}; io.DeltaTime = 1.0f/60;
        io.AddMousePosEvent(-100,-100);
        const auto frame = [&](bool show = true) {
            ImGui::NewFrame(); ImGui::SetNextWindowPos({10,10}); ImGui::SetNextWindowSize({1240,860});
            ImGui::Begin("Live content regression",nullptr,ImGuiWindowFlags_NoSavedSettings);
            if (show) app->drawPeripheralLive(app->peripherals_->config());
            else { app->peripheralsOpen_ = false; app->drawPeripherals(); }
            ImGui::End(); ImGui::Render();
        };
        const auto place = [&](const char* id,float width,float height) {
            std::vector<Emu51PeripheralLiveBox> boxes;
            for (const auto& d : config.devices) boxes.push_back({d.id,d.id == id ? 12.0 : 2000.0,d.id == id ? 12.0 : 2000.0,width,height});
            app->peripheralLive_.restore(boxes); frame(); frame();
        };
        const auto controls = [&](const char* id) -> ImGuiWindow* {
            auto* window = ImGui::FindWindowByName("Live content regression");
            for (auto* canvas : GImGui->Windows) if (canvas->ParentWindow == window && std::strstr(canvas->Name,"live-canvas")) {
                const auto childId = ImHashStr("box",0,ImHashStr(id,0,canvas->ID));
                for (auto* box : GImGui->Windows) if (box->ParentWindow == canvas && box->ChildId == childId)
                    for (auto* child : GImGui->Windows) if (child->ParentWindow == box && std::strstr(child->Name,"controls")) return child;
            }
            return nullptr;
        };
        const auto coloredBounds = [&](const char* id,ImU32 color) {
            ImRect bounds({FLT_MAX,FLT_MAX},{-FLT_MAX,-FLT_MAX});
            auto* child = controls(id);
            if (child) for (const auto& vertex : child->DrawList->VtxBuffer) if (vertex.col == color) bounds.Add(vertex.pos);
            check(!bounds.IsInverted(),"A component renders its visible content inside its own box");
            return bounds;
        };
        const float header = ImGui::GetTextLineHeight()+8;
        struct Example { const char* id; float width,height; ImU32 color; };
        const Example examples[] = {
            {"lcd",248,94,IM_COL32(24,55,28,255)}, {"segments",264,150,IM_COL32(12,14,18,255)},
            {"bar",204,100,IM_COL32(12,14,18,255)}, {"led",96,64,IM_COL32(55,55,55,255)},
            {"switch",140,64,ImGui::GetColorU32(ImGuiCol_FrameBg)}, {"button",140,72,ImGui::GetColorU32(ImGuiCol_Button)},
            {"source",180,64,ImGui::GetColorU32(ImGuiCol_FrameBg)}, {"digital",140,64,ImGui::GetColorU32(ImGuiCol_FrameBg)},
            {"audio",420,320,ImGui::GetColorU32(ImGuiCol_FrameBg)}, {"keypad",196,226,ImGui::GetColorU32(ImGuiCol_Button)}
        };
        for (const auto& example : examples) {
            place(example.id,example.width,example.height);
            const auto small = coloredBounds(example.id,example.color);
            // Double the usable content area; title bars intentionally keep their size.
            place(example.id,(example.width-12)*2+12,(example.height-header-12)*2+header+12);
            const auto large = coloredBounds(example.id,example.color);
            if (!small.IsInverted() && !large.IsInverted()) {
                if (!(large.GetWidth() > small.GetWidth()*1.7f && large.GetHeight() > small.GetHeight()*1.7f))
                    std::fprintf(stderr,"Scaling %s: %.1fx%.1f -> %.1fx%.1f\n",example.id,small.GetWidth(),small.GetHeight(),large.GetWidth(),large.GetHeight());
                check(large.GetWidth() > small.GetWidth()*1.7f && large.GetHeight() > small.GetHeight()*1.7f,
                    "Resizing grows actual display geometry, controls and hit targets in both dimensions");
                const auto* child = controls(example.id);
                if (large.Max.x > child->InnerRect.Max.x+1 || large.Max.y > child->InnerRect.Max.y+1)
                    std::fprintf(stderr,"Overflow %s: %.1f,%.1f beyond %.1f,%.1f\n",example.id,large.Max.x,large.Max.y,child->InnerRect.Max.x,child->InnerRect.Max.y);
                check(large.Max.x <= child->InnerRect.Max.x+1 && large.Max.y <= child->InnerRect.Max.y+1,
                    "Scaled component content fits inside its enlarged box");
            }
        }
        place("keypad",380,420);
        auto* keypad = controls("keypad");
        check(keypad != nullptr,"A large keypad has visible controls");
        if (!keypad) return;
        const auto click = [&](ImVec2 at) {
            io.AddMousePosEvent(at.x,at.y); frame(); io.AddMouseButtonEvent(0,true); frame();
            io.AddMouseButtonEvent(0,false); frame(); frame();
        };
        // The bottom-right key must fill its enlarged grid cell, beyond the old fixed grid.
        click({keypad->InnerRect.Max.x-10,keypad->InnerRect.Max.y-70});
        check(app->peripherals_->keys("keypad") == (1u<<15),"Enlarged keypad hit regions match the rendered bottom-right key");
        click({keypad->InnerRect.Max.x-10,keypad->InnerRect.Max.y-70});
        check(app->peripherals_->keys("keypad") == 0,"Toggle mode releases a key on a second click");
        const auto chooseMode = [&](bool momentary) {
            click({keypad->DC.CursorStartPos.x+20,keypad->DC.CursorStartPos.y+10});
            check(!GImGui->OpenPopupStack.empty(),"Keypad mode is selectable from its visible combo");
            if (!GImGui->OpenPopupStack.empty()) {
                auto* popup = GImGui->OpenPopupStack.back().Window;
                click({popup->DC.CursorStartPos.x+10,
                    (momentary ? popup->DC.CursorPosPrevLine.y : popup->DC.CursorStartPos.y)+5});
            }
            check(app->peripheralLive_.momentaryKeys("keypad") == momentary,"Keypad mode selection takes effect");
        };
        chooseMode(true);
        const auto firstKey = [&] {
            const float row = keypad->DC.PrevLineSize.y;
            return ImVec2(keypad->DC.CursorStartPos.x+10,keypad->DC.CursorStartPos.y+row+15);
        };
        const auto press = [&] {
            const auto at = firstKey(); io.AddMousePosEvent(at.x,at.y); frame(); io.AddMouseButtonEvent(0,true); frame();
            check(app->peripherals_->keys("keypad") == 1,"Momentary mode presses a key immediately on mouse down");
            frame(); check(app->peripherals_->keys("keypad") == 1,"Rendering a held key keeps the same contact pressed");
        };
        press();
        io.AddMouseButtonEvent(0,false); frame();
        check(app->peripherals_->keys("keypad") == 0,"Momentary mode releases on mouse up without a second click");
        press();
        const auto projectPath = root/"keypad-mode.emu51";
        auto restored = create();
        check(app->saveProjectToPath(projectPath) && restored->openProjectFromPath(projectPath) &&
            restored->peripheralLive_.momentaryKeys("keypad") && restored->peripherals_->keys("keypad") == 0,
            "Projects retain keypad mode and scaled box geometry without restoring held contacts");
        auto legacyJson = read(projectPath);
        const std::string modeField = ",\"momentary_keys\":true";
        const auto modeAt = legacyJson.find(modeField);
        check(modeAt != std::string::npos,"Saved keypad mode is present in project JSON");
        if (modeAt != std::string::npos) {
            legacyJson.erase(modeAt,modeField.size());
            const auto legacyPath = root/"legacy-keypad-mode.emu51";
            write(legacyPath,legacyJson);
            auto legacy = create();
            check(legacy->openProjectFromPath(legacyPath) && !legacy->peripheralLive_.momentaryKeys("keypad"),
                "Older project layouts without a keypad mode keep the existing toggle behavior");
        }
        app->peripheralLive_.editing_ = true; frame();
        check(app->peripherals_->keys("keypad") == 0,"Entering layout edit mode releases momentary keys");
        io.AddMouseButtonEvent(0,false); app->peripheralLive_.editing_ = false; frame();
        press(); frame(false);
        check(app->peripherals_->keys("keypad") == 0,"Hiding Live releases momentary keys");
        io.AddMouseButtonEvent(0,false); frame();
        press(); io.AddFocusEvent(false); frame();
        check(app->peripherals_->keys("keypad") == 0,"Losing application focus releases momentary keys");
        io.AddFocusEvent(true); io.AddMouseButtonEvent(0,false); frame();
        chooseMode(false);
        click(firstKey());
        check(app->peripherals_->keys("keypad") == 1,"Toggle mode latches a key after a complete click");
        frame(false);
        check(app->peripherals_->keys("keypad") == 1,"Intentional toggle-mode holds survive hiding Live");
        io.AddMousePosEvent(-100,-100);
    }
    static void test_segment_colon_alignment()
    {
        using Vertex = std::tuple<float,float,ImU32>;
        auto& io = ImGui::GetIO(); io.DisplaySize = {1280,840}; io.DeltaTime = 1.0f/60;
        // Inspect actual draw geometry, so the test does not duplicate the layout formula.
        const auto render = [&](const e51p_segment_snapshot& state,int style,bool instantaneous,bool colon,float width) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({10,10}); ImGui::SetNextWindowSize({width,400});
            ImGui::Begin("segment-geometry",nullptr,ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            auto* draw = ImGui::GetWindowDrawList(); const int start = draw->VtxBuffer.Size;
            emu51DrawSegmentDisplay(state,style,0,instantaneous,colon);
            std::vector<Vertex> vertices;
            for (int i = start; i < draw->VtxBuffer.Size; ++i) {
                const auto& vertex = draw->VtxBuffer[i];
                vertices.emplace_back(vertex.pos.x,vertex.pos.y,vertex.col);
            }
            ImGui::End(); ImGui::Render();
            std::sort(vertices.begin(),vertices.end()); return vertices;
        };
        const auto difference = [](const auto& a,const auto& b) {
            std::vector<Vertex> result;
            std::set_difference(a.begin(),a.end(),b.begin(),b.end(),std::back_inserter(result));
            return result;
        };
        const auto bounds = [](const auto& vertices) {
            ImRect result({FLT_MAX,FLT_MAX},{-FLT_MAX,-FLT_MAX});
            for (const auto& vertex : vertices) result.Add({std::get<0>(vertex),std::get<1>(vertex)});
            return result;
        };
        for (int style : {7,14,16}) for (unsigned digits : {4u,8u})
            for (bool instantaneous : {false,true}) for (float width : {320.0f,640.0f}) {
                e51p_segment_snapshot state{}; state.digits = digits; state.segments = style+1;
                const auto baseline = render(state,style,instantaneous,false,width);
                auto second = state, third = state;
                const unsigned right = style == 16 ? 2 : 1, left = style == 16 ? 7 : 5;
                second.instantaneous[1] = 1u<<right; second.brightness[1][right] = 1.0f/digits;
                third.instantaneous[2] = 1u<<left; third.brightness[2][left] = 1.0f/digits;
                const auto rightVertices = difference(render(second,style,instantaneous,false,width),baseline);
                const auto leftVertices = difference(render(third,style,instantaneous,false,width),baseline);
                state.colon = 3; state.colon_brightness[0] = state.colon_brightness[1] = 1.0f/digits;
                const auto colonVertices = difference(render(state,style,instantaneous,true,width),baseline);
                check(!rightVertices.empty() && !leftVertices.empty() && !colonVertices.empty(),
                    "Rendered digit edges and both colon dots are present");
                if (rightVertices.empty() || leftVertices.empty() || colonVertices.empty()) continue;
                const auto secondEdge = bounds(rightVertices).Max.x, thirdEdge = bounds(leftVertices).Min.x;
                const auto colonBounds = bounds(colonVertices);
                const float middleY = colonBounds.GetCenter().y;
                for (bool upper : {false,true}) {
                    std::vector<Vertex> dot;
                    for (const auto& vertex : colonVertices)
                        if ((std::get<1>(vertex) < middleY) == upper) dot.push_back(vertex);
                    check(!dot.empty(),"Each colon dot renders separately");
                    if (!dot.empty()) check(std::abs(bounds(dot).GetCenter().x-(secondEdge+thirdEdge)/2) < .05f,
                        "Each colon dot is centered between the rendered second and third digit edges");
                }
                check(colonBounds.Min.x > secondEdge && colonBounds.Max.x < thirdEdge,
                    "Colon dots have clearance from both adjacent digits");
            }
    }
    static void test_peripheral_workflows(const fs::path& root)
    {
        auto app = create();
        auto dac = emu51PeripheralPreset(1);
        check(app->applyPeripheralConfig(dac, 12), "Desktop accepts GPIO DAC graph");
        app->cpu_.writeDirect(0x90, 128);
        double voltage = -1;
        check(app->peripherals_->sampleAnalog("dac.out", voltage) && voltage == 2.5,
            "Desktop CPU latch drives the DAC without executing an instruction");
        auto* original = app->peripherals_.get();
        app->cpu_.PC = 0x1234;
        auto invalid = dac; invalid.digital[0].width = 9;
        check(!app->applyPeripheralConfig(invalid, 12) && app->peripherals_.get() == original && app->cpu_.PC == 0x1234,
            "Invalid bit slices preserve the active graph and CPU");
        invalid = dac; invalid.digital[0].a = "missing.pin";
        check(!app->applyPeripheralConfig(invalid, 12) && app->peripherals_.get() == original,
            "Unknown pins cannot replace the active graph");
        configureAdcBoardFixture(*app);
        check(app->hasCompiledAssemblyEditorOutput() && app->projectPrograms_.size() == 1 && !app->running_,
            "ADC fixture configures the board and compiles/loads real firmware while paused");
        for (unsigned i = 0; i != 300; ++i) app->executeInstruction();
        check(app->cpu_.IRAM[0x31] == 160 && app->peripherals_->sampleAnalog("dac.out", voltage) && voltage == 3.125,
            "Desktop firmware reads ADC through MOVX/8255 and updates DAC");
        check(app->unifiedMemory_[0x8000] == 0 && app->unifiedMemory_[0x8001] == 0,
            "Mapped XDATA bypasses backing program/data memory");
        const auto time = app->peripherals_->ticks();
        (void)app->cpu_.readData(0x8000);
        (void)app->cpu_.readData(0x8002);
        (void)app->cpu_.readData(0x8003); // unsupported control-register peek must remain passive
        check(app->peripherals_->ticks() == time && app->peripherals_->status() == E51P_OK,
            "Debugger peeks never advance time or poison the running graph");
        check(app->peripherals_->setSource("input", 1.25), "Live voltage source accepts a new input");
        for (unsigned i = 0; i != 300; ++i) app->executeInstruction();
        check(app->cpu_.IRAM[0x31] == 64 && app->peripherals_->sampleAnalog("dac.out", voltage) && voltage == 1.25,
            "Live input changes reach the next conversion without resetting CPU");
        const auto projectPath = root / "peripherals.emu51";
        check(app->saveProjectToPath(projectPath), "Peripheral project saves wiring, source value and embedded firmware");
        auto restored = create();
        check(restored->openProjectFromPath(projectPath) && restored->peripherals_ && !restored->running_,
            "Project restores a paused peripheral graph");
        for (unsigned i = 0; i != 300; ++i) restored->executeInstruction();
        check(restored->cpu_.IRAM[0x31] == 64, "Restored embedded firmware uses the saved voltage source");
        restored->resetCpuState(true);
        check(restored->peripherals_->ticks() == 0 && restored->peripherals_->sampleAnalog("dac.out", voltage) && voltage == 0,
            "CPU reset resets peripheral time and the DAC");
        restored->peripherals_->writeData(0x8003, 0xa0); // unimplemented 8255 mode
        restored->running_ = true;
        restored->executeInstruction();
        check(!restored->running_ && restored->statusMessage_.find("peripheral error") != std::string::npos,
            "Fatal device errors stop desktop execution");
        restored->resetCpuState(true);
        check(restored->peripherals_->status() == E51P_OK, "Reset clears sticky peripheral bus errors");
        std::string error;
        auto project = emu51LoadProject(projectPath, error);
        project->peripherals.digital[0].a = "unknown.pin";
        const auto brokenPath = root / "broken-peripherals.emu51";
        check(emu51SaveProject(brokenPath, *project, error), "Write project with structurally valid but unavailable pin");
        original = restored->peripherals_.get(); restored->cpu_.PC = 0x4321;
        check(!restored->openProjectFromPath(brokenPath) && restored->peripherals_.get() == original && restored->cpu_.PC == 0x4321,
            "Invalid project wiring is rejected before resetting the live session");
        restored->unsavedChangesPrompt_ = [] { return Choice::Discard; };
        const auto emptyPath = root / "no-peripherals.emu51";
        check(emu51SaveProject(emptyPath, Emu51Project{}, error) && restored->openProjectFromPath(emptyPath) && !restored->peripherals_,
            "Opening a project without peripherals detaches the previous graph");
#ifdef EMU51_GUI_HOST_AUDIO
        auto audio = emu51PeripheralPreset(1);
        audio.devices.push_back({"sound", Emu51PeripheralKind::Audio});
        audio.analog.push_back({"dac.out", "sound.playback"});
        check(restored->applyPeripheralConfig(audio, 12) && !restored->peripherals_->audioOpen(),
            "Applying an audio graph never opens host devices");
        std::vector<std::string> capture, playback;
        check(restored->peripherals_->enumerateAudio(capture, playback, error, true) &&
            restored->peripherals_->openAudio(-1, -1, 0, -1, error), "Explicit NULL audio host opens without physical devices");
        check(restored->peripherals_->syncAudio(true, error), "Start NULL host for full-scale Covox check");
        for (const auto code : {0u, 64u, 128u, 192u, 255u}) {
            restored->cpu_.writeDirect(0x90, static_cast<std::uint8_t>(code));
            restored->peripherals_->gpio()->advance(32); // crosses a 48 kHz boundary at 12 MHz
            e51p_audio_scope_frame frame{}; uint32_t count = 0, rate = 0;
            check(restored->peripherals_->audioScope(&frame, 1, count, rate) && count == 1 && rate == 48000 &&
                frame.output == (static_cast<int>(code) - 128) / 128.0f,
                "Desktop Covox uses full-scale offset-binary playback without hidden attenuation");
        }
        check(restored->peripherals_->syncAudio(false, error), "Stop full-scale NULL playback fixture");
        const auto gainTime = restored->peripherals_->ticks();
        restored->cpu_.PC = 0x2345;
        check(restored->peripherals_->setInputGainDb(24) && !restored->peripherals_->setInputGainDb(61) &&
            !restored->peripherals_->setInputGainDb(-21) && restored->peripherals_->ticks() == gainTime &&
            restored->cpu_.PC == 0x2345 && restored->peripherals_->audioOpen(),
            "Live input gain changes preserve CPU, graph time and the open host");
        restored->toggleRun(); restored->syncPeripheralAudio();
        for (unsigned i = 0; i != 100; ++i) restored->executeInstruction();
        restored->toggleRun();
        restored->resetCpuState(true);
        check(restored->peripherals_->audioOpen() && restored->peripherals_->ticks() == 0,
            "Pause/reset retain an explicitly opened audio host safely");
        check(restored->saveProjectToPath(root / "audio.emu51"), "Audio project saves");
        auto reopened = create();
        check(reopened->openProjectFromPath(root / "audio.emu51") && !reopened->peripherals_->audioOpen(),
            "Loading an audio project never reopens physical devices");
        check(reopened->peripherals_->config().devices.back().inputGainDb == 24,
            "Live microphone gain survives project save/load and CPU reset");
        check(restored->applyPeripheralConfig({}, 12) && !restored->peripherals_,
            "Replacing an open audio graph closes the host and detaches CPU safely");
#endif
    }
    static void test_background_execution(const fs::path& root)
    {
        using Clock = Emu51CycleTiming::Clock;
        {
            Emu51ExecutionWorker worker;
            std::atomic<unsigned> batches{0};
            std::atomic<bool> requestUi{false};
            worker.start([&] {
                const unsigned batch = ++batches;
                if (batch == 1) {
                    requestUi.wait(false);
                    // Give the UI time to queue access while this batch owns the model.
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                } else std::this_thread::sleep_for(std::chrono::milliseconds(2));
                return batch < 32;
            });
            requestUi.store(true);
            requestUi.notify_one();
            while (!batches.load()) std::this_thread::yield();
            {
                const auto guard = worker.access(false);
                check(batches.load() <= 2,"A busy worker hands the model to the waiting UI between batches");
            }
            worker.stop();
        }
        auto app = create();
        openAudioExample(*app,4);
        auto config = emu51PeripheralPreset(4); config.devices.back().audioBufferMs = 32;
        // Keep CPU load low so this tests scheduling/ownership even under TSAN,
        // rather than the host's ability to emulate a 24.576 MHz CPU in real time.
        check(app->applyPeripheralConfig(config, 0.12), "Worker fixture configures a 32 ms audio buffer");
        check(app->startBackgroundExecution(), "Background execution worker starts");
        {
            auto guard = app->access();
#ifdef EMU51_GUI_HOST_AUDIO
            check(startNullAudio(*app), "Worker fixture starts NULL playback");
#else
            app->toggleRun();
#endif
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::uint64_t firstCycles = 0;
        e51p_audio_stats before{};
        {
            auto guard = app->access(false);
            firstCycles = app->cycleCount_;
            check(app->running_ && firstCycles > 0, "Worker executes without frontend tick or rendering");
            app->peripherals_->audioStats(before);
        }
        // Simulate a blocked event loop or GPU submission: no UI service at all.
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        {
            auto guard = app->access(false);
            e51p_audio_stats after{}; app->peripherals_->audioStats(after);
            check(app->cycleCount_ > firstCycles + 2000, "CPU/peripherals advance throughout a blocked UI loop");
#ifdef EMU51_GUI_HOST_AUDIO
            check(after.playback_used > before.playback_used + 10000 && after.playback_misses == before.playback_misses,
                "32 ms buffer remains fed during a 350 ms UI stall");
#endif
        }
        // Rendering the editor is a real UI hot path. Its decorator simulates
        // slow highlighting/layout inside Render(), not merely a sleep between frames.
        bool delayed = false; std::uint64_t renderProgress = 0;
        {
            auto guard = app->access();
            app->peripheralsOpen_ = true;
            app->assemblyEditor_->SetLineDecorator(-2.0f, [&](TextEditor::Decorator&) {
                if (delayed) return;
                delayed = true;
                std::uint64_t start = 0;
                { auto read = app->access(false); start = app->cycleCount_; }
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                { auto read = app->access(false); renderProgress = app->cycleCount_ - start; }
            });
        }
        auto& io = ImGui::GetIO(); io.DisplaySize = ImVec2(1280, 800); io.DeltaTime = 1.0f / 60;
        unsigned char* pixels; int width, height; io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ImGui::NewFrame(); app->access()->draw(); ImGui::Render();
        check(delayed && renderProgress > 1000, "Slow editor rendering releases the model and cannot stall audio generation");
        {
            auto guard = app->access();
            app->configureEditorDebugger();
            app->toggleRun();
            check(!app->running_, "Pause synchronizes with worker at a batch boundary");
            firstCycles = app->cycleCount_;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        {
            auto guard = app->access();
            check(app->cycleCount_ == firstCycles, "Paused worker consumes no CPU instructions");
            app->resetCpuState(true); app->stepIn();
            check(!app->running_ && app->instructionCount_ == 1, "Reset and single-step remain synchronized while worker exists");
            app->resetCpuState(true);
            // Breakpoint stop must run on the worker without touching editor/ImGui.
            app->cpu_.writeProg(0,0); app->cpu_.writeProg(1,0); app->breakpoints_.insert(1);
            app->toggleRun();
        }
        const auto waitStopped = [&] {
            const auto end = Clock::now() + std::chrono::seconds(2);
            while (Clock::now() < end) {
                { auto read = app->access(false); if (!app->running_) return true; }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return false;
        };
        check(waitStopped(), "Worker breakpoint wakes/stops execution");
        {
            auto guard = app->access(false);
            check(app->executionRedrawPending_.load() && app->shouldAnimate(), "Worker debugger stop retains a final UI redraw");
        }
        ImGui::NewFrame(); app->access()->draw(); ImGui::Render();
        {
            auto guard = app->access();
            check(!app->executionRedrawPending_.load(), "Final stopped frame consumes the worker redraw request");
            check(app->cpu_.PC == 1 && app->statusMessage_.find("breakpoint") != std::string::npos,
                "Breakpoint reports the correct synchronized program counter");
            app->breakpoints_.clear(); app->resetCpuState(true);
            app->cpu_.writeProg(0,0x75); app->cpu_.writeProg(1,0x90); app->cpu_.writeProg(2,0x55);
            app->watchpoints_.push_back({Emu51ImguiApp::WatchSpace::Internal, 0x90, false, true, true});
            app->toggleRun();
        }
        check(waitStopped(), "Worker watchpoint interrupts execution");
        {
            auto guard = app->access();
            check(app->cpu_.readLatch(0x90) == 0x55 && app->statusMessage_.find("watchpoint") != std::string::npos,
                "Worker preserves watchpoint semantics and diagnostic state");
            app->watchpoints_.clear();
            app->resetCpuState(true);
            // Restore the actual music project under a running worker.
            app->unsavedChangesPrompt_ = [] { return Choice::Discard; };
            check(app->openProjectFromPath(fs::path(EMU51_REPO_ROOT) / "progs/asm/demo/covox_duet.emu51"),
                "Project reload safely replaces the worker's machine and peripheral graph");
            config = emu51PeripheralPreset(4); config.devices.back().audioBufferMs = 32;
            check(app->applyPeripheralConfig(config, 0.12), "Worker fixture restores low CPU workload");
#ifdef EMU51_GUI_HOST_AUDIO
            check(startNullAudio(*app), "Reloaded worker opens playback explicitly");
#else
            app->toggleRun();
#endif
            const auto cycles = app->cycleCount_, ticks = app->peripherals_->ticks();
            const auto pc = app->cpu_.PC;
            check(app->applyPeripheralAudioBuffer(64) && app->cycleCount_ == cycles && app->peripherals_->ticks() == ticks && app->cpu_.PC == pc,
                "Buffer reconfiguration hands simulation ownership to UI without resetting the player");
            check(app->saveProjectToPath(root / "worker.emu51"), "Project save snapshots worker-owned configuration safely");
            // A modal path picker releases even nested access guards. The
            // cancel path leaves the worker producing frames throughout.
            app->projectPathPicker_ = [&](bool) {
                std::uint64_t start = 0;
                { auto read = app->access(false); start = app->cycleCount_; }
                std::this_thread::sleep_for(std::chrono::milliseconds(1200));
                { auto read = app->access(false); check(app->cycleCount_ > start + 8000, "Worker continues during a native/modal file dialog longer than the old timing-rebase threshold"); }
                return fs::path{};
            };
            { auto nested = app->access(); app->requestOpenProject(); }
            // stop() must not join while retaining this outer UI guard.
            app->stopBackgroundExecution();
            check(!app->running_ && !app->executionWorker_.started(), "Worker stops and joins safely under nested UI access");
        }
        check(app->startBackgroundExecution(), "Worker can restart after a joined shutdown");
        { auto guard = app->access(); app->toggleRun(); }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        app->stopBackgroundExecution();
        { auto guard = app->access(false); check(!app->running_, "Restarted worker shuts down before application destruction"); }
    }
    static void test_audio_buffer_setting(const fs::path& root)
    {
        auto app = create(); openAudioExample(*app,4);
        check(app->applyPeripheralConfig(emu51PeripheralPreset(4), 0.12), "Buffer fixture uses a low CPU workload");
        for (unsigned i = 0; i < 1000; ++i) app->executeInstruction();
        check(app->peripherals_->setInputGainDb(12), "Buffer fixture sets a retained gain");
#ifdef EMU51_GUI_HOST_AUDIO
        check(startNullAudio(*app), "Live buffer fixture opens NULL playback");
#else
        app->toggleRun();
#endif
        const auto ticks = app->peripherals_->ticks(), cycles = app->cycleCount_;
        const auto pc = app->cpu_.PC;
        std::array<std::uint8_t, 128> iram{};
        std::copy_n(app->cpu_.IRAM, iram.size(), iram.begin());
        check(!app->applyPeripheralAudioBuffer(9) && !app->applyPeripheralAudioBuffer(2001) && app->running_,
            "Out-of-range buffer values do not disturb a running player");
        for (const int size : {10, 512, 2000, 0, 512}) {
            check(app->applyPeripheralAudioBuffer(size) && app->running_ && app->cpu_.PC == pc &&
                app->cycleCount_ == cycles && app->peripherals_->ticks() == ticks &&
                std::equal(iram.begin(), iram.end(), app->cpu_.IRAM), "Live buffer changes preserve CPU and player position");
            check(app->peripherals_->audioLatencyMs() == (size ? size : 128) &&
                audioDevice(app->peripherals_->config()).audioBufferMs == size &&
                audioDevice(app->peripheralDraft_).audioBufferMs == size &&
                audioDevice(app->peripherals_->config()).inputGainDb == 12,
                "Applied buffer controls real latency, saved graph and draft while retaining gain");
#ifdef EMU51_GUI_HOST_AUDIO
            check(app->peripherals_->audioOpen() && app->peripherals_->audioRunning(), "Buffer resize reopens and resumes the same NULL host selection");
#endif
        }
#ifdef EMU51_GUI_HOST_AUDIO
        e51p_audio_stats before{}; app->peripherals_->audioStats(before);
        const auto end = Emu51CycleTiming::Clock::now() + std::chrono::milliseconds(650);
        while (Emu51CycleTiming::Clock::now() < end) { app->tick(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        e51p_audio_stats after{}; app->peripherals_->audioStats(after);
        check(after.playback_used > before.playback_used && !after.captured_frames,
            "Resized 512 ms queue delivers actual playback callback data");
#endif
        app->stopForDebugger("buffer test paused");
        check(app->saveProjectToPath(root / "audio-buffer.emu51"), "Project saves audio buffer size");
        auto restored = create();
        check(restored->openProjectFromPath(root / "audio-buffer.emu51") &&
            audioDevice(restored->peripherals_->config()).audioBufferMs == 512 && restored->audioBufferMs_ == 512 &&
            restored->peripherals_->audioLatencyMs() == 512 && !restored->peripherals_->audioOpen(),
            "Project reload restores buffer setting and allocates matching queues without opening hardware");
        auto invalid = restored->peripherals_->config(); audioDevice(invalid).audioBufferMs = -1;
        auto* original = restored->peripherals_.get();
        check(!restored->applyPeripheralConfig(invalid, 0.12) && restored->peripherals_.get() == original,
            "Graph validation rejects invalid stored audio buffering transactionally");
    }
    static void test_audio_host_pause_recovery()
    {
#ifdef EMU51_GUI_HOST_AUDIO
        using Clock = Emu51CycleTiming::Clock;
        auto app = create(); openAudioExample(*app,4);
        // This tests host audio scheduling, not synthesizer pitch. Keep CPU work
        // low so the real callback checks remain valid under ASan/slow CI too.
        check(app->applyPeripheralConfig(emu51PeripheralPreset(4), 0.12), "Slow-clock audio recovery fixture configures");
        check(startNullAudio(*app), "Playback recovery fixture starts a NULL host");
        const auto waitForPlayback = [&](uint64_t used) {
            e51p_audio_stats stats{};
            const auto deadline = Clock::now() + std::chrono::seconds(2);
            do {
                app->tick();
                if (app->peripherals_->audioStats(stats) &&
                    (stats.playback_used >= used + 480 || stats.status != E51P_OK))
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (Clock::now() < deadline);
            return stats;
        };
        const auto before = waitForPlayback(0);
        check(app->peripherals_->audioLatencyMs() == 128 && before.playback_used >= 480 &&
            before.status == E51P_OK && !before.captured_frames,
            "Playback-only GUI reserves 128 ms without opening capture");
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        // The SDK reserve test measures exact underruns with controlled clocks.
        // Here the real callback must keep delivering samples across UI gaps.
        const auto afterShort = waitForPlayback(before.playback_used);
        check(afterShort.status == E51P_OK && afterShort.playback_used >= before.playback_used + 480,
            "Playback continues across an 80 ms UI service gap");
        const auto cycles = app->cycleCount_, ticks = app->peripherals_->ticks();
        const auto pc = app->cpu_.PC;
        std::array<std::uint8_t, 36> player{};
        std::copy_n(app->cpu_.IRAM + 0x30, player.size(), player.begin());
        // The native callback continues for >1 s while the UI cannot execute.
        // Pacing intentionally discards this interval; audio must discard it too.
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        e51p_audio_stats duringGap{}; app->peripherals_->audioStats(duringGap);
        check(duringGap.playback_misses > afterShort.playback_misses, "Long UI gap exhausts queued playback");
        app->tick();
        e51p_audio_scope_frame frame{}; uint32_t count = 0, rate = 0;
        check(app->running_ && app->peripherals_->audioOpen() && app->peripherals_->audioRunning() &&
            app->cycleCount_ == cycles && app->peripherals_->ticks() == ticks && app->cpu_.PC == pc &&
            std::equal(player.begin(), player.end(), app->cpu_.IRAM + 0x30) &&
            app->peripherals_->audioScope(&frame, 1, count, rate) && count == 0,
            "Host pause starts a new audio epoch while preserving CPU, player state and open devices");
        const auto recovered = waitForPlayback(duringGap.playback_used);
        check(recovered.status == E51P_OK && recovered.playback_used >= duringGap.playback_used + 480 &&
            recovered.playback_misses >= duringGap.playback_misses,
            "Playback resumes after host-clock rebase, retaining cumulative diagnostics");
        const auto stable = waitForPlayback(recovered.playback_used);
        check(stable.status == E51P_OK && stable.playback_used >= recovered.playback_used + 480,
            "Recovered playback remains synchronized instead of missing every future sample");
        app->stopForDebugger("audio recovery test complete");
#endif
    }
    static void test_covox_duet(const fs::path& root)
    {
        auto app = create();
        openAudioExample(*app,4);
        check(app->hasCompiledAssemblyEditorOutput() && app->crystalMHz_ == 24.576 && app->cycleExact_ && !app->running_ &&
            app->peripherals_ && containsLegacyGraph(app->peripherals_->config(),emu51PeripheralPreset(4)) && !app->peripherals_->audioOpen(),
            "Covox duet loads playback-only wiring and timer firmware at 24.576 MHz, paused with no microphone");
        for (unsigned i = 0; i < 30000; ++i) app->executeInstruction();
        check(app->cpu_.IRAM[0x30] == 1 && app->cpu_.IRAM[0x33] == 1 &&
            app->cpu_.IRAM[0x3c] == 60 && app->cpu_.IRAM[0x44] == 52 && app->cpu_.IRAM[0x3d] == 1 && app->cpu_.IRAM[0x45] == 1 &&
            (app->cpu_.IRAM[0x35] || app->cpu_.IRAM[0x36]),
            "Desktop executes real audio/music interrupts and exposes first-chord player state in IRAM");
        const auto file = root / "covox_duet.emu51";
        check(app->saveProjectToPath(file), "Duet project saves embedded firmware and playback-only graph");
        auto restored = create();
        check(restored->openProjectFromPath(file) && restored->peripherals_ && !restored->running_ &&
            !restored->peripherals_->audioOpen() && restored->crystalMHz_ == 24.576 &&
            containsLegacyGraph(restored->peripherals_->config(),emu51PeripheralPreset(4)) && restored->hasAssemblyEditorSource(),
            "Duet project restores wiring, clock and source without opening audio");
        auto packaged = create();
        check(packaged->openProjectFromPath(fs::path(EMU51_REPO_ROOT) / "progs/asm/demo/covox_duet.emu51") &&
            containsLegacyGraph(packaged->peripherals_->config(),emu51PeripheralPreset(4)), "Portable Covox duet example opens");
        for (unsigned i = 0; i < 30000; ++i) packaged->executeInstruction();
        check(packaged->cpu_.IRAM[0x33] == 1 && packaged->cpu_.IRAM[0x3c] == 60,
            "Packaged duet's embedded source drives the real timer sequencer");
#ifdef EMU51_GUI_HOST_AUDIO
        check(startNullAudio(*restored) && restored->running_ && restored->peripherals_->audioOpen(),
            "Explicit host opening and Run start playback-only NULL audio");
        restored->toggleRun();
        check(!restored->running_ && !restored->peripherals_->audioRunning(), "Duet audio follows Pause");
#endif
    }
    static void test_microphone_covox(const fs::path& root)
    {
        const auto initializeEcho = [&](Emu51ImguiApp& target) {
            unsigned steps = 0;
            while (!target.cpu_.IRAM[0x32] && steps++ < 1000000) target.executeInstruction();
            check(target.cpu_.IRAM[0x32] == 1, "Echo delay line initializes before sampling");
        };
        auto app = create();
        openAudioExample(*app,3);
        check(app->hasCompiledAssemblyEditorOutput() && app->crystalMHz_ == 3 && app->cycleExact_ && !app->running_ &&
            app->peripherals_ && containsLegacyGraph(app->peripherals_->config(),emu51PeripheralPreset(3)) && !app->peripherals_->audioOpen(),
            "Microphone demo loads direct ADC/P0 and P1/Covox firmware at 3 MHz without opening devices");
        initializeEcho(*app);
        for (unsigned i = 0; i < 500; ++i) app->executeInstruction();
        double volts = -1;
        check(app->cpu_.IRAM[0x31] == 128 && app->cpu_.readLatch(0x80) == 255 &&
            app->peripherals_->sampleAnalog("covox.out", volts) && volts == 2.5,
            "Paused capture supplies centered silence through ADC, P0, real firmware, P1 and Covox");
        auto analogTest = emu51PeripheralPreset(3);
        analogTest.devices.push_back({"input", Emu51PeripheralKind::Voltage});
        analogTest.analog[0].from = "input.out";
        check(app->applyPeripheralConfig(analogTest, 3), "Apply a deterministic analog source to the microphone board for GPIO tests");
        initializeEcho(*app);
        for (const auto code : {0u, 64u, 128u, 160u, 255u}) {
            const auto voltage = 5.0 * code / 256.0;
            check(app->peripherals_->setSource("input", voltage), "Set ADC test voltage");
            for (unsigned i = 0; i < 500; ++i) app->executeInstruction();
            const auto mixed = (code >> 1) + 64; // These first slots still contain initialized silence.
            check(app->cpu_.IRAM[0x30] == code && app->cpu_.IRAM[0x31] == mixed && app->cpu_.readLatch(0x90) == mixed &&
                app->cpu_.readLatch(0x80) == 255 && app->peripherals_->sampleAnalog("covox.out", volts) && volts == 5.0 * mixed / 256.0,
                "Real ADC/P0 samples are half-mixed with delay bytes before reaching P1/Covox");
        }
        app->unsavedChangesPrompt_ = [] { return Choice::Discard; };
        openAudioExample(*app,3);
        const auto file = root / "microphone_covox.emu51";
        check(app->saveProjectToPath(file), "Microphone example saves a portable project");
        auto restored = create();
        check(restored->openProjectFromPath(file) && containsLegacyGraph(restored->peripherals_->config(),emu51PeripheralPreset(3)) &&
            restored->crystalMHz_ == 3 && restored->hasAssemblyEditorSource() && !restored->peripherals_->audioOpen(),
            "Microphone project restores embedded source and wiring with physical devices closed");
        const auto sample = fs::path(EMU51_REPO_ROOT) / "progs/asm/demo/microphone_covox.emu51";
        auto packaged = create();
        check(packaged->openProjectFromPath(sample), "Redistributable microphone example project loads");
        initializeEcho(*packaged);
        for (unsigned i = 0; i < 500; ++i) packaged->executeInstruction();
        check(packaged->cpu_.IRAM[0x31] == 128, "Packaged project's embedded firmware runs through direct ports");
#ifdef EMU51_GUI_HOST_AUDIO
        check(startNullAudio(*restored) && restored->running_ && restored->peripherals_->audioOpen(),
            "Explicit host opening and Run start cycle-paced NULL audio execution");
        restored->toggleRun();
        check(!restored->running_ && restored->peripherals_->audioOpen(), "Pausing keeps the open microphone host silent");
        restored->resetCpuState(true);
        check(restored->peripherals_->ticks() == 0, "Microphone host safely follows CPU/context reset");
        // Device initialization may block between Run and the first CPU tick.
        // That wall time must not become emulation backlog in the new audio epoch.
        restored->toggleRun();
        restored->cycleTiming_.reset(Emu51CycleTiming::Clock::now() - std::chrono::milliseconds(750));
        const auto beforeRestart = restored->cycleCount_;
        restored->tickAt(Emu51CycleTiming::Clock::now());
        check(!restored->cycleTiming_.cyclesDue() && restored->cycleCount_ == beforeRestart,
            "Audio resume begins with a fresh CPU pacing interval, without initialization backlog");
        restored->toggleRun();
#endif
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
    static void test_unedited_projects_close_without_prompt(const fs::path& root)
    {
        const auto sourcePath = root/"project-source.asm", projectPath = root/"saved-editor.emu51";
        const std::string source = "\xef\xbb\xbf\tnop\r\n\tend\r\n";
        write(sourcePath,source);
        for (int kind = 0; kind < 3; ++kind) {
            Emu51Project project;
            project.editor.present = project.editor.open = true;
            project.editor.path = kind == 2 ? "" : sourcePath.string();
            project.editor.draft = kind != 0;
            project.editor.text = kind ? "\tmov a,#42h\r\n\tend\r\n" : "";
            std::string error;
            check(emu51SaveProject(projectPath,project,error),"Write a project with referenced or embedded assembly");
            auto app = create(); int prompts = 0;
            app->unsavedChangesPrompt_ = [&] { ++prompts; return Choice::Cancel; };
            check(app->openProjectFromPath(projectPath),"Open a saved project editor buffer");
            const auto original = app->assemblyEditor_->GetText();
            for (int frame = 0; frame < 2; ++frame) {
                ImGui::NewFrame(); app->drawAssemblyEditor(); ImGui::Render();
            }
            check(!app->recoverySavePending_,"Loading saved project text does not schedule unsaved recovery");
            check(app->confirmAssemblyEditorChanges() && prompts == 0,
                "Unedited referenced, named embedded and untitled embedded project source needs no save prompt");
            check(app->openProjectFromPath(projectPath) && prompts == 0,
                "Reopening a saved project does not prompt for unchanged assembly");
            app->assemblyEditor_->ReplaceSectionText(0,0,0,0,"; actual edit\n");
            check(!app->confirmAssemblyEditorChanges() && prompts == 1 && app->assemblyEditorState_.dirty,
                "An actual edit after project open still requires save/discard/cancel");
            app->assemblyEditor_->Undo();
            check(app->assemblyEditor_->GetText() == original && app->confirmAssemblyEditorChanges() && prompts == 1,
                "Undo back to the saved project buffer removes the unnecessary save prompt");
            app->assemblyEditor_->Redo();
            check(app->saveProjectToPath(projectPath) && app->confirmAssemblyEditorChanges() && prompts == 1,
                "Save Project protects the current embedded source without requiring a separate ASM save");
            check(read(sourcePath) == source,"Saving a project does not overwrite its referenced ASM file");
            auto reopened = create();
            reopened->unsavedChangesPrompt_ = [&] { ++prompts; return Choice::Cancel; };
            check(reopened->openProjectFromPath(projectPath) &&
                reopened->assemblyEditor_->GetText() == app->assemblyEditor_->GetText() && reopened->prepareToQuit() && prompts == 1,
                "Project-saved edits survive reopening and close without another save prompt");
            app->requestQuit();
            check(app->consumeQuitRequested() && prompts == 1,"Closing an unmodified saved project succeeds without prompting");
            // Opening another source must retire the saved project's buffer baseline.
            check(app->loadAssemblyFileIntoEditor(sourcePath),"Open a standalone source after the saved project");
            app->assemblyEditor_->SetText(original); app->trackAssemblyEditorChanges();
            app->assemblyEditor_->ReplaceSectionText(0,0,0,0,"; actual edit\n");
            check(!app->confirmAssemblyEditorChanges() && prompts == 2,
                "An old project snapshot cannot suppress prompts for a different source buffer");
        }
    }
    static void test_project_saved_buffer_failures(const fs::path& root)
    {
        const auto source = root/"guard-source.asm", projectPath = root/"guard-project.emu51";
        write(source,"nop\nend\n");
        Emu51Project project; project.editor.present = project.editor.open = project.editor.draft = true;
        project.editor.path = source.string(); project.editor.text = "mov a,#42h\nend\n";
        std::string error;
        check(emu51SaveProject(projectPath,project,error),"Create a saved embedded editor fixture");
        const auto savedProject = read(projectPath);
        auto app = create(); int prompts = 0;
        app->unsavedChangesPrompt_ = [&] { ++prompts; return Choice::Cancel; };
        check(app->openProjectFromPath(projectPath),"Open embedded editor for failed-save regression");
        app->assemblyEditor_->ReplaceSectionText(0,0,0,0,"; new edit\n");
        const auto edited = app->assemblyEditor_->GetText();
        check(!app->saveProjectToPath(root/"absent-directory"/"failed.emu51") &&
            !app->confirmAssemblyEditorChanges() && prompts == 1 && read(projectPath) == savedProject &&
            app->assemblyEditor_->GetText() == edited,
            "Failed Save Project does not mark edits saved, damage the previous project, or bypass the save prompt");
        check(!app->openProjectFromPath(root/"absent-project.emu51") &&
            !app->confirmAssemblyEditorChanges() && prompts == 2 && app->assemblyEditor_->GetText() == edited,
            "Failed Open Project preserves unsaved assembly and its save guard");
        check(app->saveProjectToPath(projectPath) && app->confirmAssemblyEditorChanges() && prompts == 2,
            "A successful project save after a failed one protects the editor buffer");
        check(app->saveAssemblyEditorFileToPath(source),"Save the embedded buffer to its ASM file");
        app->assemblyEditor_->Undo();
        check(!app->confirmAssemblyEditorChanges() && prompts == 3,
            "Undo after an ASM save prompts even if an older version was embedded in a project");
        app->assemblyEditor_->Redo();
        check(!app->confirmAssemblyEditorChanges() && prompts == 4,
            "Redo does not reuse the retired project baseline after an ASM save");
    }
#ifdef __APPLE__
    static void test_native_project_picker(const fs::path& root)
    {
        // Replace only the dialog subprocess. Exercise the real PFD backend and
        // compile its AppleScript, supplying selections without opening any UI.
        const auto bin = root/"picker-bin", directory = root/"project folder with spaces";
        fs::create_directories(bin); fs::create_directories(directory);
        const auto projectPath = directory/"picker project.emu51", script = root/"picker.applescript";
        Emu51Project project; project.editor.present = project.editor.open = project.editor.draft = true;
        project.editor.text = "nop\nend\n"; project.peripherals = emu51PeripheralPreset(1);
        std::string error;
        check(emu51SaveProject(projectPath,project,error),"Write the native picker project fixture");
        write(bin/"osascript",R"SH(#!/bin/sh
printf '%s\n' "$2" > "$EMU51_TEST_PICKER_SCRIPT"
/usr/bin/osacompile -o "$EMU51_TEST_PICKER_SCRIPT.scpt" -e "$2" >/dev/null 2>&1 || exit 1
if [ "$EMU51_TEST_PICKER_CANCEL" != 1 ]; then
    printf '%s\n' "$EMU51_TEST_PICKER_SELECTION"
fi
)SH");
        fs::permissions(bin/"osascript",fs::perms::owner_all);
        struct Environment {
            std::vector<std::pair<std::string,std::optional<std::string>>> saved;
            void set(const char* name,const std::string& value) {
                const auto* old = std::getenv(name);
                saved.emplace_back(name,old ? std::optional<std::string>(old) : std::nullopt);
                check(setenv(name,value.c_str(),1) == 0,"Set isolated native picker test environment");
            }
            ~Environment() {
                for (auto i = saved.rbegin(); i != saved.rend(); ++i) {
                    if (i->second) setenv(i->first.c_str(),i->second->c_str(),1);
                    else unsetenv(i->first.c_str());
                }
            }
        } environment;
        const auto* previousPath = std::getenv("PATH");
        environment.set("PATH",bin.string()+":"+(previousPath ? previousPath : "/usr/bin:/bin"));
        environment.set("EMU51_TEST_PICKER_SCRIPT",script.string());
        environment.set("EMU51_TEST_PICKER_SELECTION",projectPath.string());
        environment.set("EMU51_TEST_PICKER_CANCEL","0");
        auto app = create(); int prompts = 0;
        app->unsavedChangesPrompt_ = [&] { ++prompts; return Choice::Cancel; };
        app->requestOpenProject();
        check(app->projectPath_ == fs::weakly_canonical(projectPath).string() && app->peripherals_,
            "First Open Project compiles its native dialog script and loads the selection");
        for (int repeat = 0; repeat < 2; ++repeat) {
            app->cpu_.PC = 0x1234;
            app->requestOpenProject();
            check(app->cpu_.PC == 0 && app->projectPath_ == fs::weakly_canonical(projectPath).string() && prompts == 0,
                "Repeated Open Project compiles valid AppleScript and reloads an unchanged embedded buffer without prompting");
            const auto generated = read(script);
            check(generated.find("default location") != std::string::npos &&
                generated.find("default name") == std::string::npos && generated.find(directory.string()) != std::string::npos,
                "Native Open Project starts in the project folder, including paths with spaces");
        }
        environment.set("EMU51_TEST_PICKER_CANCEL","1");
        const auto currentPath = app->projectPath_; auto* session = app->peripherals_.get();
        app->cpu_.PC = 0x5678; app->requestOpenProject();
        check(app->projectPath_ == currentPath && app->peripherals_.get() == session && app->cpu_.PC == 0x5678 && prompts == 0,
            "Canceling the native picker leaves the project, circuit and CPU untouched");
        app->projectPath_ = (root/"missing folder"/"gone.emu51").string();
        check(app->chooseProjectPath(false).empty() && read(script).find("default name") == std::string::npos &&
            read(script).find("default location") == std::string::npos,
            "A missing previous project folder falls back to a valid dialog instead of an invalid filename default");
    }
#endif
#ifdef __linux__
    static void test_linux_project_picker(const fs::path& root)
    {
        const auto directory = root/"project folder with spaces";
        fs::create_directory(directory);
        const auto projectPath = directory/"picker project.emu51";
        Emu51Project project; project.editor.present = project.editor.open = project.editor.draft = true;
        project.editor.text = "nop\nend\n";
        std::string error;
        check(emu51SaveProject(projectPath,project,error),"Write the Linux picker project fixture");
        auto app = create();
        app->projectPath_ = (root/"previous.emu51").string();
        auto& io = ImGui::GetIO(); io.DisplaySize = {1000,800}; io.DeltaTime = 1.f/60;
        io.Fonts->Build();
        const auto frame = [&] {
            ImGui::NewFrame(); ImGui::Begin("Linux project picker test");
            app->drawOpenProjectBrowser(); ImGui::End(); ImGui::Render();
        };
        app->requestOpenProject(); frame(); frame();
        auto* popup = ImGui::FindWindowByName("Open Project");
        check(popup && popup->Active && app->projectBrowser_.directory == root,
            "Open Project displays the Linux browser in the previous project's folder");
        if (!popup || !popup->Active) return;
        const auto open = [&](const std::string& path) {
            auto& input = app->projectBrowser_.path;
            std::snprintf(input.data(),input.size(),"%s",path.c_str());
            ImGui::ActivateItemByID(popup->GetID("Open")); frame();
        };
        open(directory.filename().string());
        check(app->projectBrowser_.directory == directory && app->projectBrowser_.entries.size() == 1 &&
            app->projectBrowser_.entries.front().name == projectPath.filename(),"Browse a directory with spaces and list its project");
        open("missing.emu51");
        check(!app->projectBrowser_.error.empty() && app->projectPath_ == (root/"previous.emu51").string(),
            "A failed load keeps the picker open with an error and preserves the current project");
        open(projectPath.filename().string()); frame();
        check(app->projectPath_ == fs::weakly_canonical(projectPath).string() && GImGui->OpenPopupStack.empty(),
            "Selecting a Linux project loads it and closes the browser");
        app->requestOpenProject(); frame(); frame();
        check(app->projectBrowser_.directory == directory,"Reopening starts in the selected project's folder");
        app->cpu_.PC = 0x1234;
        ImGui::ActivateItemByID(popup->GetID("Cancel")); frame(); frame();
        check(GImGui->OpenPopupStack.empty() && app->cpu_.PC == 0x1234,"Cancel closes the Linux browser without resetting the machine");
    }
#endif
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

    static void test_lab_peripheral_examples()
    {
        auto app = create();
        const auto examples = fs::path(EMU51_REPO_ROOT)/"progs/asm/demo";
        const auto run = [&](uint64_t ticks) {
            const auto target = app->peripherals_->ticks()+ticks;
            while (app->peripherals_->ticks()<target && app->peripherals_->status()==E51P_OK) app->executeInstruction();
            check(app->peripherals_->status()==E51P_OK,"Lab demo runs without peripheral errors");
        };
        const bool stepper = app->openProjectFromPath(examples/"stepper_control.emu51");
        check(stepper,"Open and assemble the ULN2003 stepper demo");
        if (!stepper) return;
        run(1000000);
        e51p_stepper_snapshot forward{}, reverse{};
        check(app->peripherals_->stepperState("motor",forward) && forward.valid &&
            forward.direction==1 && forward.position_half_steps>0,"Stepper firmware advances the motor through ULN2003");
        check(app->peripherals_->setPressed("reverse",true) && app->peripherals_->setPressed("half",true),
            "Live switches select direction and half steps");
        run(550000);
        check(app->peripherals_->stepperState("motor",reverse) && reverse.valid && reverse.direction==-1 &&
            reverse.position_half_steps<forward.position_half_steps &&
            reverse.invalid_transitions==forward.invalid_transitions,"Firmware reverses the shaft without an invalid coil transition");

        auto& io=ImGui::GetIO(); io.DisplaySize={1280,900}; io.DeltaTime=1.0f/60;
        ImGui::NewFrame(); app->drawPeripherals(); ImGui::Render();
        auto* window = ImGui::FindWindowByName("Peripherals");
        auto* tabs = window ? GImGui->TabBars.GetByKey(window->GetID("peripheral-tabs")) : nullptr;
        check(tabs!=nullptr,"Lab project opens the Peripherals window");
        if (tabs) {
            tabs->NextSelectedTabId = ImHashStr("Live",0,tabs->ID);
            const auto tick = app->peripherals_->ticks();
            for (int i=0; i<3; ++i) { ImGui::NewFrame(); app->drawPeripherals(); ImGui::Render(); }
            check(app->peripherals_->ticks()==tick,"Stepper drawing preserves paused simulation time");
        }

        const bool adc = app->openProjectFromPath(examples/"adc0809_scan.emu51");
        check(adc,"Open and assemble the ADC0809 eight-channel scan");
        if (!adc) return;
        for (unsigned i = 0; i < 8; ++i)
            check(app->peripherals_->setSource("channel"+std::to_string(i),i*0.625),"Set a known ADC test voltage");
        run(20000);
        check(app->cpu_.IRAM[0x28]>0,"ADC0809 firmware completed a full scan");
        for (unsigned i=0; i<8; ++i)
            check(app->cpu_.IRAM[0x20+i] == i*32,"Firmware reads each separate analog channel");
        check(app->peripherals_->setSource("channel3",5),"Adjust ADC channel 3 voltage");
        run(20000);
        check(app->cpu_.IRAM[0x23]==255 && app->cpu_.IRAM[0x24]==128,
            "A live voltage update changes its channel and leaves the next channel independent");
    }

    static void test_serial_peripheral_examples(const fs::path& root)
    {
        const auto source = fs::path(EMU51_REPO_ROOT)/"progs/asm/demo";
        const auto directory = root/"serial-examples";
        fs::create_directories(directory);
        fs::copy_file(source/"serial_i2c.inc",directory/"serial_i2c.inc");
        for (const std::string name : {"oled_rtc","mcp23017_keypad","w25q64_flash"})
            for (const auto* ext : {".asm",".emu51"}) fs::copy_file(source/(name+ext),directory/(name+ext));
        auto app = create();
        const auto open = [&](const std::string& name) {
            const bool opened = app->openProjectFromPath(directory/(name+".emu51"));
            if (!opened) std::fprintf(stderr,"Serial demo %s: %s\n",name.c_str(),app->statusMessage_.c_str());
            check(opened,"Serial example opens and assembles through the project loader");
            return opened;
        };
        const auto run = [&](uint64_t duration) {
            const auto target = app->peripherals_->ticks()+duration;
            for (unsigned i = 0; i < duration/12+1 && app->peripherals_->ticks()<target; ++i)
                app->executeInstruction();
            check(app->peripherals_->ticks()>=target,"Serial example advances emulated time");
        };
        std::array<e51p_logic_sample,E51P_LOGIC_CAPACITY> samples{};
        std::array<e51p_logic_event,E51P_LOGIC_CAPACITY> events{};
        e51p_logic_status capture{};
        const auto decode = [&](unsigned protocol) {
            auto* analyzer = app->peripherals_->serialDevice("capture");
            check(e51p_logic_read(analyzer,&capture,samples.data(),samples.size())==E51P_OK && capture.count>1,
                "Logic analyzer captures firmware bus edges");
            return e51p_logic_decode(samples.data(),capture.count,protocol,events.data(),events.size());
        };
        if (!open("oled_rtc")) return;
        run(800000);
        auto* oled = app->peripherals_->serialDevice("oled");
        auto* rtc = app->peripherals_->serialDevice("rtc");
        e51p_oled_snapshot screen{};
        e51p_rtc_snapshot clock{};
        check(app->cpu_.IRAM[0x23]==0x55 && app->cpu_.IRAM[0x28]==0x58 &&
            app->cpu_.IRAM[0x29]==0x59 && app->cpu_.IRAM[0x2a]==0x23,
            "Firmware addresses the RTC separately and reads three BCD bytes using a repeated START");
        check(e51p_oled_state(oled,&screen)==E51P_OK && (screen.pixels[0][0]&1) &&
            screen.pixels[23][0]==255, "OLED firmware draws text and a plot into visible RAM");
        const auto i2cEvents = decode(E51P_LOGIC_I2C);
        check(std::any_of(events.begin(),events.begin()+i2cEvents,[](const auto& e) {
            return e.kind==E51P_LOGIC_ADDRESS && e.a==0x78 && e.flags;
        }),"I2C analyzer decodes the OLED write address and ACK");
        auto graph = app->peripherals_->config();
        check(app->saveProjectToPath(directory/"roundtrip.emu51"),"Serial device topology and settings save");
        if (!open("roundtrip")) return;
        check(app->peripherals_->config()==graph,"Serial device settings round trip");
        run(24800000);
        rtc = app->peripherals_->serialDevice("rtc");
        check(e51p_rtc_state(rtc,&clock)==E51P_OK && clock.year==2000 && clock.month==3 &&
            clock.day==1 && clock.hour==0 && clock.minute==0 && clock.second==0,
            "RTC advances across leap day and midnight using emulated time");
        const auto ticks = app->peripherals_->ticks();
        auto& io = ImGui::GetIO(); io.DisplaySize = {1280,900}; io.DeltaTime = 1.0f/60;
        app->peripheralsOpen_ = true;
        ImGui::NewFrame(); app->drawPeripherals(); ImGui::Render();
        auto* window = ImGui::FindWindowByName("Peripherals");
        auto* tabs = window ? GImGui->TabBars.GetByKey(window->GetID("peripheral-tabs")) : nullptr;
        check(tabs!=nullptr,"Serial devices load into the Peripherals window");
        if (tabs) {
            tabs->NextSelectedTabId = ImHashStr("Live",0,tabs->ID);
            for (int i = 0; i < 3; ++i) { ImGui::NewFrame(); app->drawPeripherals(); ImGui::Render(); }
        }
        check(app->peripherals_->ticks()==ticks,"Rendering serial snapshots while paused leaves simulated time unchanged");

        if (!open("mcp23017_keypad")) return;
        run(100000);
        check(app->cpu_.IRAM[0x23]==0x55,"Firmware configures the GPIO expander");
        auto* analyzer = app->peripherals_->serialDevice("capture");
        check(e51p_logic_arm(analyzer,2,0)==E51P_OK,"Analyzer arms on falling interrupt edge");
        check(app->peripherals_->setKeys("keypad",1u<<9),"Press row 2 column 1");
        run(100000);
        bool defined = false;
        check(app->cpu_.IRAM[0x26]==2 && app->peripherals_->ledState("led",defined) && defined,
            "Expander interrupts the CPU, firmware scans the correct row/column, and lights the LED");
        check(e51p_logic_stop(analyzer)==E51P_OK &&
            e51p_logic_read(analyzer,&capture,samples.data(),samples.size())==E51P_OK &&
            capture.triggered && !capture.armed && capture.trigger_tick<=capture.end_tick,
            "Edge-triggered capture records the interrupt timestamp and stops");
        app->peripherals_->setKeys("keypad",0);
        run(100000);
        check(app->cpu_.IRAM[0x26]==0 && !app->peripherals_->ledState("led",defined) && defined,
            "Key release triggers another scan and turns off the LED");

        if (!open("w25q64_flash")) return;
        run(1500000);
        const auto* iram = app->cpu_.IRAM;
        check(iram[0x23]==0x55 && iram[0x20]==0xef && iram[0x21]==0x40 && iram[0x22]==0x17 &&
            std::memcmp(iram+0x28,"EMU51",5)==0,"SPI firmware erases, programs and reads flash and the JEDEC ID");
        const auto spiEvents = decode(E51P_LOGIC_SPI);
        check(std::any_of(events.begin(),events.begin()+spiEvents,[](const auto& e) {
            return e.kind==E51P_LOGIC_BYTE && e.flags==3 && e.b==0xef;
        }),"SPI analyzer decodes valid bytes and observes the manufacturer's ID");
        check(app->flushPeripheralStorage(),"Committed SPI flash image saves");
        const auto image = read(directory/"w25q64_flash.bin");
        check(image.size()==E51P_FLASH_SIZE && image.compare(0,5,"EMU51")==0,"Flash image contains the programmed bytes");
        if (!open("w25q64_flash")) return;
        std::vector<uint8_t> exported(E51P_FLASH_SIZE);
        check(e51p_flash_export(app->peripherals_->serialDevice("flash"),exported.data(),E51P_FLASH_SIZE)==E51P_OK &&
            std::memcmp(exported.data(),"EMU51",5)==0,"Flash contents survive reopening before firmware runs");
    }

    static void test_segment_examples(const fs::path& root)
    {
        const auto source=fs::path(EMU51_REPO_ROOT)/"progs/asm/demo";
        const auto directory=root/"segment-examples";fs::create_directories(directory);
        for (const auto* name : {"seven_segment_clock","segment_decoders","max7219_counter","segment_gallery","ht16k33_alpha","tm1637_counter"})
            for (const auto* ext : {".asm",".emu51"}) fs::copy_file(source/(std::string(name)+ext),directory/(std::string(name)+ext));
        auto app=create();
        const auto open=[&](const char* name) {
            const bool opened=app->openProjectFromPath(directory/(std::string(name)+".emu51"));
            if (!opened) std::fprintf(stderr,"Segment demo %s: %s\n",name,app->statusMessage_.c_str());
            check(opened,"Segment example loads through normal project validation and firmware assembly");return opened;
        };
        const auto run=[&](uint64_t ticks) {
            const auto target=app->peripherals_->ticks()+ticks;
            while (app->peripherals_->ticks()<target) app->executeInstruction();
            check(app->peripherals_->status()==E51P_OK,"Display firmware runs without contention or protocol faults");
        };
        const auto view=[&](const char* id) {e51p_segment_snapshot state{};check(app->peripherals_->displayState(id,state),"Display exposes a passive live snapshot");return state;};
        if (!open("seven_segment_clock")) return;
        run(13200000);
        auto clock=view("clock");
        check(app->cpu_.IRAM[0x33]==1&&clock.digits==4&&clock.colon_brightness[0]>.15f,
            "Timer-autoreload firmware counts real simulated seconds and blinks its colon");
        for (unsigned digit=0;digit<4;++digit) check(clock.brightness[digit][1]>.2f&&clock.brightness[digit][1]<.3f,
            "Each clock digit accumulates its quarter-duty refresh independent of GUI frames");
        if (!open("segment_decoders")) return;
        run(240000);
        check(view("cd4511").programmed[0]==0x3f&&view("ls47").programmed[0]==0x3f&&
            view("cathode").brightness[0][0]>.5f&&view("anode").brightness[0][0]>.5f,
            "BCD firmware drives both common-cathode and common-anode displays through real decoder pins");
        if (!open("max7219_counter")) return;
        run(300000);
        auto first=view("max7219"),second=view("max7221"),wired=view("wired_digits");
        check(first.programmed[1]==0x5b&&second.programmed[1]==0x07&&first.brightness[1][0]>second.brightness[1][0],
            "Firmware addresses two chained MAX chips, uses Code-B, and programs separate brightness levels");
        check(wired.brightness[1][0]>.05f,"MAX physical segment/common outputs also illuminate a separately wired display");
        if (!open("ht16k33_alpha")) return;
        run(300000);
        auto alpha=view("ht16k33");
        check(alpha.programmed[0]==0xf9&&alpha.programmed[1]==0x536&&view("alpha").brightness[1][8]>.04f,
            "I2C firmware fills the HT16K33 character RAM and illuminates 14-segment diagonals");
        check(app->peripherals_->setDisplayKeys("ht16k33",uint64_t(1)<<38),"Live HT key matrix accepts key 39");run(500000);
        check(app->cpu_.IRAM[0x26]>0&&app->cpu_.IRAM[0x25]==0x10,"8051 polls HT INT and reads all six key RAM bytes");
        if (!open("tm1637_counter")) return;
        run(300000);
        auto tm=view("tm1637");
        check(tm.programmed[0]==6&&tm.programmed[1]==0x5b&&tm.colon_brightness[0]>0,
            "TM firmware uses address-less LSB-first commands and the colon's DP mapping");
        check(app->peripherals_->setDisplayKeys("tm1637",uint64_t(1)<<10),"TM key input accepts K2/SG3");run(1200000);
        check(app->cpu_.IRAM[0x21]==0xed,"TM key code reaches firmware through DIO");
        if (!open("segment_gallery")) return;
        run(60000);
        check(view("seven").segments==8&&view("fourteen").segments==15&&view("sixteen").segments==17&&view("bar").segments==10,
            "Gallery loads every raw display geometry, decimal point and bar width");
        Emu51PeripheralEditor editor;editor.rebuild(app->peripherals_->config(),Emu51Model::i8051);
        const auto* wide=editor.pin("sixteen.segments");
        check(wide&&wide->bits.size()==17&&wide->bits.back().y>wide->bits.front().y,"Graph wraps all seventeen segment connectors into visible rows");
        for (const auto& block : editor.blocks_) for (const auto& pin : block.pins) for (const auto& bit : pin.bits)
            check(bit.x>block.position.x&&bit.x<block.position.x+block.size.x&&bit.y>block.position.y&&bit.y<block.position.y+block.size.y,
                "Every wide-port bit remains inside its block");
        const auto config=app->peripherals_->config();
        check(app->saveProjectToPath(directory/"roundtrip.emu51"),"Segment configuration saves with the project");
        auto restored=create();check(restored->openProjectFromPath(directory/"roundtrip.emu51")&&restored->peripherals_->config()==config,
            "All new digit, geometry, polarity, color and persistence settings round-trip through Kason");
        auto& io=ImGui::GetIO();io.DisplaySize={1280,900};io.DeltaTime=1.0f/60;
        for (unsigned i=0;i<3;++i) {ImGui::NewFrame();restored->drawPeripherals();ImGui::Render();}
        auto* window=ImGui::FindWindowByName("Peripherals");auto* tabs=GImGui->TabBars.GetByKey(window->GetID("peripheral-tabs"));
        tabs->NextSelectedTabId=ImHashStr("Live",0,tabs->ID);
        for (unsigned i=0;i<10000;++i) restored->executeInstruction();
        e51p_segment_snapshot before{},after{};
        check(restored->peripherals_->displayState("sixteen",before),"Paused renderer fixture has a display snapshot");
        for (unsigned i=0;i<3;++i) {ImGui::NewFrame();restored->drawPeripherals();ImGui::Render();}
        check(restored->peripherals_->displayState("sixteen",after),"Rendered display snapshot remains readable");
        check(!std::memcmp(&before,&after,sizeof(before)),"Rendering display snapshots while paused does not decay or advance the simulation");
    }

    static void test_component_examples(const fs::path& root)
    {
        const auto source=fs::path(EMU51_REPO_ROOT)/"progs/asm/demo";
        const auto directory=root/"component-examples"; fs::create_directories(directory);
        for (const auto* name : {"front_panel","shift_registers","eeprom_counter","ay_chords"})
            for (const auto* extension : {".asm",".emu51"})
                fs::copy_file(source/(std::string(name)+extension),directory/(std::string(name)+extension));
        auto app=create();
        const auto run=[&](uint64_t ticks) {
            const auto target=app->peripherals_->ticks()+ticks;
            while (app->peripherals_->ticks()<target) app->executeInstruction();
            check(app->peripherals_->status()==E51P_OK,"Example executes without floating bus errors, contention or oscillation");
        };
        const bool frontOpened=app->openProjectFromPath(directory/"front_panel.emu51");
        if (!frontOpened) std::fprintf(stderr,"Front panel: %s\n",app->statusMessage_.c_str());
        check(frontOpened,"Front-panel example opens through the normal project loader");
        if (!app->peripherals_) return;
        run(600000);
        e51p_lcd_snapshot lcd{};
        check(app->peripherals_->lcdState("lcd",lcd) && lcd.display_on &&
            std::string(reinterpret_cast<char*>(lcd.cells),15)=="emu51 I/O panel",
            "Real 8051 firmware initializes and prints to the LCD");
        check(app->peripherals_->setKeys("keypad",1u<<9),"Live keypad accepts a pressed key");
        run(200000);
        check(app->cpu_.IRAM[0x20]=='8' && app->peripherals_->lcdState("lcd",lcd) && lcd.cells[16+5]=='8',
            "Firmware scans matrix rows and shows the selected key on the LCD");
        check(app->peripherals_->setPressed("button",true),"Live pushbutton closes its contact"); run(200000);
        bool defined=false;
        check(app->peripherals_->ledState("led",defined) && defined &&
            app->peripherals_->lcdState("lcd",lcd) && lcd.cells[16+13]=='1',
            "Bouncing pushbutton reaches the CPU and updates both the LED and LCD");
        check(app->openProjectFromPath(directory/"shift_registers.emu51"),"Shift-register example opens"); run(100000);
        e51p_digital_state state{};
        check(app->cpu_.IRAM[0x20]==0x96 && app->peripherals_->sampleDigital("shift_out.Q",state) && state.value==0x96 &&
            app->peripherals_->sampleDigital("chained_out.Q",state) && state.value==0xa5,
            "8051 shifts parallel switches through a 165 and two chained 595s");
        check(app->peripherals_->setPressed("switch_0",false),"Live switch changes a shift-register input"); run(100000);
        check(app->cpu_.IRAM[0x20]==0x97,"Firmware sees live switch changes without restarting");
        check(app->openProjectFromPath(directory/"eeprom_counter.emu51"),"EEPROM counter example opens"); run(1200000);
        check(app->cpu_.IRAM[0x20]==255 && app->cpu_.IRAM[0x21]==0 && app->cpu_.IRAM[0x22]==0 && !app->cpu_.IRAM[0x23],
            "Bit-banged firmware reads, programs, ACK-polls and verifies EEPROM");
        check(app->flushPeripheralStorage() && read(directory/"eeprom_counter.bin").size()==256 &&
            read(directory/"eeprom_counter.bin")[0x2a]==0,"Committed EEPROM data saves to its project-relative image");
        app->resetCpuState(true); run(1200000);
        check(app->cpu_.IRAM[0x20]==0 && app->cpu_.IRAM[0x22]==1,"CPU reset retains nonvolatile EEPROM contents");
        check(app->openProjectFromPath(directory/"eeprom_counter.emu51"),"EEPROM project can be reopened before a separate flush"); run(1200000);
        check(app->cpu_.IRAM[0x20]==1 && app->cpu_.IRAM[0x22]==2,"Session replacement carries the latest committed EEPROM bytes");
        auto config=app->peripherals_->config();
        std::string error;
        auto project=emu51LoadProject(directory/"eeprom_counter.emu51",error);
        check(bool(project),"EEPROM fixture decodes");
        if (project) {
            project->peripherals.devices[0].storagePath=(directory/"canceled.bin").string();
            check(emu51SaveProject(directory/"canceled.emu51",*project,error),"Alternate EEPROM fixture saves");
            edit(*app,{},"unsaved\n"); app->unsavedChangesPrompt_=[] { return Choice::Cancel; };
            check(!app->openProjectFromPath(directory/"canceled.emu51") && !fs::exists(directory/"canceled.bin"),
                "A canceled project open never creates the candidate EEPROM image");
            app->unsavedChangesPrompt_=[] { return Choice::Discard; };
        }
        check(app->openProjectFromPath(directory/"ay_chords.emu51"),"AY chord example opens without audio hardware"); run(3000000);
        double voltage=0;
        check(app->cpu_.IRAM[0x32]>=2 && app->peripherals_->sampleAnalog("ay.out",voltage) && voltage>=0.1 && voltage<=4.9 &&
            !app->peripherals_->audioOpen(),"Timer ISR sequences three AY voices through XDATA while host audio remains explicitly controlled");
#ifdef EMU51_GUI_HOST_AUDIO
        check(startNullAudio(*app),"AY demo opens the host's NULL playback backend");
        const auto until=Emu51CycleTiming::Clock::now()+std::chrono::milliseconds(250);
        while (Emu51CycleTiming::Clock::now()<until) { app->tick(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        e51p_audio_stats audio{};
        std::array<e51p_audio_scope_frame,512> waveform{}; uint32_t count=0, rate=0;
        check(app->peripherals_->audioStats(audio) && audio.playback_used>0 &&
            app->peripherals_->audioScope(waveform.data(),512,count,rate) && rate==48000 && count>0 &&
            std::any_of(waveform.begin(),waveform.begin()+count,[](const auto& frame) { return std::abs(frame.output)>0.05f; }),
            "AY voltage reaches non-silent 48 kHz host playback and the output scope");
        app->stopForDebugger("AY audio verified");
#endif
        config=app->peripherals_->config();
        config.devices[0].clockHz=1789772; config.devices[0].activeLow=false;
        check(app->applyPeripheralConfig(config,12),"Alternate AY clock is accepted");
        check(app->saveProjectToPath(directory/"roundtrip.emu51"),"Expanded peripheral settings save in project JSON");
        auto restored=create(); check(restored->openProjectFromPath(directory/"roundtrip.emu51") &&
            restored->peripherals_->config()==config,"New device kinds, clocks, polarity and wiring round-trip through Kason");
        config.devices[0].wiredBus = false; // Exercise legacy fixed-window validation.
        config.devices.push_back({"overlap",Emu51PeripheralKind::I8255,0x9000});
        check(!app->applyPeripheralConfig(config,12),"Legacy fixed AY and 8255 XDATA windows cannot overlap");
        // Render Live with a complete LCD/keypad/button circuit as well as Graph.
        check(restored->openProjectFromPath(directory/"front_panel.emu51"),"Front panel reloads for GUI rendering");
        auto& io=ImGui::GetIO(); io.DisplaySize={1280,840}; io.DeltaTime=1.0f/60;
        for (unsigned i=0;i<3;++i) { ImGui::NewFrame(); restored->drawPeripherals(); ImGui::Render(); }
        auto* window=ImGui::FindWindowByName("Peripherals");
        auto* tabs=GImGui->TabBars.GetByKey(window->GetID("peripheral-tabs"));
        tabs->NextSelectedTabId=ImHashStr("Live",0,tabs->ID);
        for (unsigned i=0;i<3;++i) { ImGui::NewFrame(); restored->drawPeripherals(); ImGui::Render(); }
        check(tabs->SelectedTabId==ImHashStr("Live",0,tabs->ID),"New front-panel controls render in the title-bar Live tab");
    }

    static void test_ui_state(const fs::path& root)
    {
        const auto directory = root/"state-migration";
        fs::create_directories(directory);
        const auto layout = directory/"imgui_layout_state.txt";
        const auto programs = directory/"recent_programs.txt";
        const auto projects = directory/"recent_projects.txt";
        const auto path = directory/"state.json";
        const auto program = (fs::canonical(root)/"program with spaces.hex").string();
        const auto project = (fs::canonical(root)/"project with spaces.emu51").string();
        write(layout,"prefs 0.3 0.25 0.2 0.4\nregisters_visible 0\nperipheral_inspector 0\n"
            "pane registers 0 left 1 240 400\npane disassembly 7 right 0.5 300 500 0\n"
            "pane disassembly 8 none 1 250 350\n");
        write(programs,program+"\r\n"+program+"\n");
        write(projects,project+"\r\n"+project+"\n");
        const auto oldLayout = read(layout), oldPrograms = read(programs), oldProjects = read(projects);
        auto app = create(); app->persistState_ = true;
        app->loadState(path);
        check(fs::exists(path) && app->disassemblyViews_.size() == 2 && app->disassemblyViews_[0].id == 7 &&
            !app->disassemblyViews_[0].followPc && app->disassemblyViews_[1].followPc &&
            !app->registersVisible() && !app->peripheralEditor_.inspectorVisible() &&
            app->recentProgramPaths_ == std::vector<std::string>{program} &&
            app->recentProjectPaths_ == std::vector<std::string>{project},
            "First launch imports all three legacy files into state.json, preserving flags, order and CRLF paths");
        const Emu51ImguiApp::PaneKey pane{Emu51ImguiApp::PaneKey::Kind::Disassembly,7};
        check(app->snapTargetForPane(pane) == Emu51ImguiSnapTarget::Right &&
            app->findFloatingPaneState(pane)->size.y == 500 && app->snapLayoutPreferences_.leftWidthFraction == 0.3f,
            "Legacy migration preserves docking preferences, pane sizes and IDs");
        app->disassemblyViews_[0].followPc = true;
        app->registersOpen_ = true;
        const auto escaped = (fs::canonical(root)/fs::path("quote\"-backslash\\-żółw.hex")).string();
        app->rememberRecentProgramPath(escaped);
        Emu51UiState stored;
        check(emu51LoadUiState(path,stored) == Emu51UiStateLoad::Loaded && stored.registersVisible &&
            stored.recentPrograms.front() == escaped && stored.recentProjects == std::vector<std::string>{project},
            "Saving program history writes a complete snapshot and escapes Unicode, quotes and backslashes");
        app->clearRecentProjectFiles();
        check(emu51LoadUiState(path,stored) == Emu51UiStateLoad::Loaded && stored.recentProjects.empty() &&
            stored.recentPrograms.front() == escaped &&
            std::any_of(stored.panes.begin(),stored.panes.end(),[](const auto& saved) {
                return saved.kind == "disassembly" && saved.id == 7 && saved.followPc;
            }),
            "Clearing project history preserves program history and current layout in the same file");
        auto restored = create(); restored->loadState(path);
        check(restored->registersVisible() && restored->recentProjectPaths_.empty() &&
            restored->recentProgramPaths_ == app->recentProgramPaths_ && restored->disassemblyViews_[0].followPc,
            "Existing state.json takes precedence over stale legacy files across executions");
        check(read(layout) == oldLayout && read(programs) == oldPrograms && read(projects) == oldProjects,
            "Migration and subsequent saves leave legacy files unchanged");
        for (auto& view : app->disassemblyViews_) view.open = false;
        check(app->saveState(),"A layout with every disassembly window closed is saved");
        restored->loadState(path);
        check(restored->disassemblyViews_.empty(),"Closed disassembly windows remain closed after restart");
        app->snapLayoutPreferences_.leftWidthFraction = 0.12f;
        app->snapLayoutPreferences_.rightWidthFraction = 0.70f;
        check(app->saveState(),"Layout boundary fractions survive float-to-JSON conversion");
        const auto valid = read(path);
        for (const std::string& invalid : {std::string("{"),
            std::string("{\"version\":2,\"layout\":{\"panes\":[]}}"),
            std::string("{\"version\":1,\"version\":1,\"layout\":{\"panes\":[]}}"),
            std::string("{\"version\":1,\"layout\":{\"left_width\":-1,\"panes\":[]}}"),
            std::string("{\"version\":1,\"layout\":{\"panes\":[{\"kind\":\"disassembly\",\"id\":2147483647}]}}")}) {
            stored.recentPrograms = {"sentinel"};
            check(!emu51DecodeUiState(invalid,stored) && stored.recentPrograms == std::vector<std::string>{"sentinel"},
                "Malformed, duplicate, unsupported or invalid state is rejected transactionally");
        }
        write(path,"{broken");
        auto blocked = create(); blocked->persistState_ = true; blocked->loadState(path);
        blocked->rememberRecentProgramPath(root/"another.hex");
        check(blocked->statePersistenceBlocked_ && !blocked->saveState() && read(path) == "{broken" &&
            blocked->recentProjectPaths_.empty(),
            "Invalid existing JSON is preserved and never replaced by defaults or stale legacy history");
        blocked.reset();
        write(path,valid);
        check(emu51LoadUiState(path,stored) == Emu51UiStateLoad::Loaded,"Original valid state remains readable");
        const auto snapshot = stateJson(*app);
        app->statePath_ = directory/"missing-parent"/"state.json";
        check(!app->saveState() && read(path) == valid && stateJson(*app) == snapshot,
            "Failed atomic saves retain existing state and the in-memory snapshot");
        app->persistState_ = false;
    }

    static void test_disassembly_follow_pc_persistence(const fs::path& root)
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
        const auto saved = stateJson(*app);

        auto restored = create();
        restoreState(*restored,saved);
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
        const auto changed = stateJson(*restored);
        auto reopened = create();
        restoreState(*reopened,changed);
        check(!reopened->disassemblyViews_[0].followPc && reopened->disassemblyViews_[1].followPc,
            "Both enabling and disabling Follow PC survive another save and reload");

        for (const std::string flag : {"", " invalid", " 2"}) {
            auto legacy = create();
            const auto directory = root/"legacy-follow"; fs::create_directories(directory);
            write(directory/"imgui_layout_state.txt","pane disassembly 1 none 1 230 400" + flag + "\n");
            Emu51UiState imported;
            check(emu51ImportLegacyUiState(directory,imported),"Legacy layout imports");
            legacy->applyUiState(imported);
            check(legacy->disassemblyViews_.size() == 1 && legacy->disassemblyViews_[0].followPc,
                "Older layouts and invalid flags retain the default Follow PC behavior");
        }
    }

    static void test_disassembly_title_control()
    {
        auto app = create();
        app->disassemblyViews_.clear(); app->addDisassemblyView(0,true); app->addDisassemblyView(0,false);
        auto& first = app->disassemblyViews_[0]; auto& second = app->disassemblyViews_[1];
        const Emu51ImguiApp::PaneKey pane{Emu51ImguiApp::PaneKey::Kind::Disassembly,first.id};
        auto& io = ImGui::GetIO(); io.DisplaySize = {1280,840}; io.DeltaTime = 1.0f/60;
        const auto frame = [&] {
            ImGui::NewFrame(); app->updateDragSnapState(); app->updateSnappedPaneLayout();
            for (std::size_t i = 0; i < app->disassemblyViews_.size(); ++i) {
                auto& view = app->disassemblyViews_[i]; if (!view.open) continue;
                ImGui::SetNextWindowPos({20+static_cast<float>(i)*380,20},ImGuiCond_Once);
                app->drawDisassemblyView(view);
            }
            ImGui::Render();
        };
        frame(); frame();
        const auto name = [](int id) { return "###disassembly_"+std::to_string(id); };
        auto* firstWindow = ImGui::FindWindowByName(name(first.id).c_str());
        auto* secondWindow = ImGui::FindWindowByName(name(second.id).c_str());
        check(firstWindow && secondWindow,"Each disassembly window retains a stable identity");
        if (!firstWindow || !secondWindow) return;
        check(firstWindow->MenuBarHeight == 0 && !(firstWindow->Flags & ImGuiWindowFlags_MenuBar),
            "Disassembly no longer spends a content row on the Follow PC menu bar");
        ImGuiWindow* content = nullptr;
        for (auto* child : GImGui->Windows)
            if (child->ParentWindow == firstWindow && std::strstr(child->Name,"disassembly_view")) content = child;
        check(content && std::abs(content->Pos.y-firstWindow->DC.CursorStartPos.y) < 1,
            "Disassembly rows start immediately below the title bar");
        const auto click = [&](ImVec2 at) {
            io.AddMousePosEvent(at.x,at.y); frame(); io.AddMouseButtonEvent(0,true); frame();
            io.AddMouseButtonEvent(0,false); frame(); frame();
        };
        const auto checkbox = [](ImGuiWindow* window, bool label) {
            const auto& style = ImGui::GetStyle();
            const auto title = window->TitleBarRect();
            const float right = title.Max.x-window->WindowBorderSize-style.FramePadding.x-
                ImGui::GetFontSize()-2*style.ItemInnerSpacing.x;
            const float width = ImGui::GetFrameHeight()+(label ? style.ItemInnerSpacing.x+ImGui::CalcTextSize("Follow PC").x : 0);
            return ImVec2(right-width+ImGui::GetFrameHeight()/2,title.GetCenter().y);
        };
        app->layoutStateDirty_ = false;
        const auto originalPosition = firstWindow->Pos;
        click(checkbox(firstWindow,true));
        check(!first.followPc && !second.followPc && app->layoutStateDirty_ && !firstWindow->Collapsed &&
            firstWindow->Pos.x == originalPosition.x && firstWindow->Pos.y == originalPosition.y,
            "Title checkbox toggles only its own view without moving or collapsing the window");
        app->running_ = true; app->cpu_.PC = 0x1234;
        click(checkbox(secondWindow,true));
        check(!first.followPc && second.followPc && app->running_ && app->cpu_.PC == 0x1234,
            "Follow PC can be enabled from the title without changing CPU execution");
        app->running_ = false;
        ImGui::SetWindowSize(firstWindow,{170,350}); frame();
        click(checkbox(firstWindow,false));
        check(first.followPc && first.open,"Compact title checkbox stays usable in narrow windows without hitting Close");
        ImGui::SetWindowCollapsed(firstWindow,true); frame();
        click(checkbox(firstWindow,false));
        check(!first.followPc && firstWindow->Collapsed,"Follow PC remains usable in a collapsed title bar");
        ImGui::SetWindowCollapsed(firstWindow,false); frame();
        const auto title = ImVec2(firstWindow->Pos.x+40,firstWindow->TitleBarRect().GetCenter().y);
        io.AddMousePosEvent(title.x,title.y); frame(); io.AddMouseButtonEvent(0,true); frame();
        io.AddMousePosEvent(80,420); frame(); io.AddMouseButtonEvent(0,false); frame(); frame();
        check(app->snapTargetForPane(pane) == Emu51ImguiSnapTarget::Left,"Dragging the disassembly caption still docks the window");
        click({firstWindow->TitleBarRect().Max.x-ImGui::GetStyle().FramePadding.x-ImGui::GetFontSize()/2,
            firstWindow->TitleBarRect().GetCenter().y});
        check(!first.open && second.open,"Close remains independent from each view's title checkbox");
        io.AddMousePosEvent(-100,-100);
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
        app->cpu_.IRAM[0x30] = 0xab;
        app->cpu_.IRAM[0x20] = 0x80;
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

    static void test_cpu_models(const fs::path& root)
    {
        auto app = create();
        check(app->cpu_.model() == Emu51Model::i8051 && app->clampMonitorAddress(app->internalRamMonitor_, 255) == 127,
            "New sessions default to 8051 with a 128-byte internal RAM monitor");
        app->unifiedMemory_[0] = 0x76; app->unifiedMemory_[1] = 0x42; // MOV @R0,#42h
        app->running_ = true; app->cpu_.PC = 99; app->cpu_.queueSerialReceive(1);
        app->setCpuModel(Emu51Model::i8052);
        check(!app->running_ && app->cpu_.PC == 0 && app->unifiedMemory_[0] == 0x76 &&
            app->cpu_.pendingSerialReceiveCount() == 0 && app->clampMonitorAddress(app->internalRamMonitor_, 255) == 255,
            "Changing CPU resets and pauses without clearing loaded program memory");
        app->cpu_.Ri(0, 0x90);
        app->watchpoints_ = {{Emu51ImguiApp::WatchSpace::Iram, 0x90, false, true, true}};
        app->running_ = true; app->executeInstruction();
        check(!app->running_ && app->readMonitorByte(app->internalRamMonitor_, 0x90) == 0x42 && app->cpu_.P1 == 0xff &&
            !app->pendingWatchpointStop_.empty(), "Upper RAM monitor and RAM change watchpoints are separate from SFRs");
        app->cpu_.IRAM[0x90] = 0;
        app->watchpoints_ = {{Emu51ImguiApp::WatchSpace::Internal, 0x90, false, true, true}};
        app->cpu_.PC = 0; app->running_ = true; app->executeInstruction();
        check(app->running_ && app->pendingWatchpointStop_.empty(), "A direct SFR watch does not react to upper RAM writes");
        app->watchpoints_ = {{Emu51ImguiApp::WatchSpace::Iram, 0x90, false, true, true}};
        const auto path = root / "cpu-model.emu51";
        check(app->saveProjectToPath(path), "CPU model project saves");
        auto restored = create();
        check(restored->openProjectFromPath(path) && restored->cpu_.model() == Emu51Model::i8052 &&
            restored->watchpoints_.size() == 1 && restored->watchpoints_[0].space == Emu51ImguiApp::WatchSpace::Iram,
            "CPU model and upper RAM watchpoints survive project restoration");
        std::string document = read(path);
        const std::string field = "\"cpu_model\":\"8052\",";
        const auto at = document.find(field);
        check(at != std::string::npos, "Project stores a stable CPU model ID");
        if (at != std::string::npos) {
            document.erase(at, field.size());
            write(root / "old-cpu.emu51", document);
            check(restored->openProjectFromPath(root / "old-cpu.emu51") && restored->cpu_.model() == Emu51Model::i8051,
                "Projects without a CPU model retain the 8051 default");
            document.insert(at, "\"cpu_model\":\"unknown\",");
            write(root / "bad-cpu.emu51", document);
            restored->cpu_.PC = 123; restored->running_ = true;
            check(!restored->openProjectFromPath(root / "bad-cpu.emu51") && restored->cpu_.PC == 123 && restored->running_,
                "Unknown CPU models fail validation before changing live state");
        }
        app->internalRamMonitor_.start = 0xf0; app->setCpuModel(Emu51Model::i8051);
        check(app->internalRamMonitor_.start == 0x7f, "Switching to 8051 clamps the internal RAM monitor");
        app->requestExecutionSettings();
        check(app->executionSettingsDialog_.cpuModel == 0, "Execution settings display the active CPU model");
    }

    static void test_project_save_shortcuts(const fs::path& root)
    {
        const auto folder = root / "save-shortcuts";
        fs::create_directory(folder);
        const auto source = folder / "source.asm";
        const auto include = folder / "defs.inc";
        const auto clean = folder / "clean.asm";
        const auto embedded = folder / "embedded.asm";
        const auto project = folder / "session.emu51";
        write(source, "nop\n");
        write(include, "value equ 1\n");
        write(clean, "; clean\r\n");
        auto app = create();
        check(app->addProjectUnit(include, false), "Add include to the project editor");
        edit(*app, include, "value equ 2\n");
        check(app->addProjectUnit(clean, false, false) && app->addProjectUnit(embedded, true),
            "Add clean and embedded project sources");
        edit(*app, {}, "; embedded project source\n");
        check(app->addProjectUnit(source, false), "Add an active project source");
        const auto active = app->activeProjectUnit_;
        edit(*app, source, "inc a\n");
        app->projectPathPicker_ = [&](bool save) { return save ? project : fs::path{}; };

        auto& io = ImGui::GetIO();
        io.DisplaySize = {1280, 900};
#if defined(__APPLE__)
        const auto primary = io.ConfigMacOSXBehaviors ? ImGuiMod_Ctrl : ImGuiMod_Super;
#else
        const auto primary = ImGuiMod_Ctrl;
#endif
        const auto shortcut = [&](bool shift) {
            io.AddKeyEvent(primary, true);
            io.AddKeyEvent(ImGuiMod_Shift, shift);
            io.AddKeyEvent(ImGuiKey_S, true);
            ImGui::NewFrame();
            emu51HandleImguiShortcuts(*app);
            ImGui::EndFrame();
            io.AddKeyEvent(ImGuiKey_S, false);
            io.AddKeyEvent(ImGuiMod_Shift, false);
            io.AddKeyEvent(primary, false);
            ImGui::NewFrame();
            ImGui::EndFrame();
        };
        shortcut(true);
        check(read(source) == "inc a\n" && read(include) == "value equ 1\n" && !fs::exists(project),
            "Shift plus the save shortcut writes only the current source");
        edit(*app, source, "dec a\n");
        shortcut(false);
        std::string error;
        const auto saved = emu51LoadProject(project, error);
        check(read(source) == "dec a\n" && read(include) == "value equ 2\n" && read(clean) == "; clean\r\n" &&
            app->activeProjectUnit_ == active && !app->assemblyEditorState_.dirty && saved && saved->units.size() == 4 &&
            !saved->units[0].editor.draft && saved->units[2].editor.draft &&
            saved->units[2].editor.text == "; embedded project source\n" && !fs::exists(embedded),
            "Save Project writes dirty source and include files and embeds sources without a disk file");

        const auto previousDocument = read(project);
        edit(*app, source, "ret\n");
        app->stepsPerFrame_ = 321;
        fs::remove(source);
        fs::create_directory(source);
        app->saveProject();
        check(read(project) == previousDocument && app->assemblyEditorState_.dirty &&
            app->statusMessage_.find(source.string()) != std::string::npos,
            "A failed source write leaves the project file intact and the source dirty");
        fs::remove(source);
        app->saveProject();
        check(read(source) == "ret\n" && read(project) != previousDocument && !app->assemblyEditorState_.dirty,
            "Saving again after a source write failure persists both the source and project");
    }

    static void test_collapsible_edge_docks(const fs::path& root)
    {
        using Pane = Emu51ImguiApp::PaneKey;
        using Target = Emu51ImguiSnapTarget;
        auto app = create();
        app->registersOpen_ = app->serialTerminalOpen_ = app->projectPanelOpen_ = app->peripheralsOpen_ = true;
        const Pane registers{Pane::Kind::Registers, 0};
        const Pane serial{Pane::Kind::SerialTerminal, 0};
        const Pane project{Pane::Kind::Project, 0};
        const Pane peripherals{Pane::Kind::Peripherals, 0};
        const Pane center{Pane::Kind::Disassembly, app->disassemblyViews_.front().id};
        app->setPaneSnapTarget(registers, Target::Left);
        app->setPaneSnapTarget(serial, Target::Right);
        app->setPaneSnapTarget(project, Target::Top);
        app->setPaneSnapTarget(peripherals, Target::Bottom);
        app->setPaneSnapTarget(center, Target::Center);
        app->snapLayoutPreferences_.leftWidthFraction = 0.31f;
        auto& io = ImGui::GetIO();
        io.DisplaySize = {1280, 840};
        const auto frame = [&] {
            ImGui::NewFrame();
            app->contentTop_ = emu51DrawImguiToolbar(*app);
            app->updateSnappedPaneLayout();
            app->drawRegisters();
            app->drawDisassemblyViews();
            app->drawSnapSplitters();
            ImGui::Render();
        };
        frame(); frame();
        const auto* toolbar = ImGui::FindWindowByName("ExecutionToolbar");
        check(toolbar != nullptr && app->snappedPaneRects_.size() == 5, "The toolbar and all five docks start visible");
        if (!toolbar) return;
        const auto clickDock = [&](int index) {
            const auto& style = ImGui::GetStyle();
            const float size = ImGui::GetFrameHeight();
            const float x = toolbar->Pos.x + toolbar->Size.x - style.WindowPadding.x -
                size * 0.5f - (4 - index) * (size + style.ItemSpacing.x);
            const float y = toolbar->Pos.y + style.WindowPadding.y + size * 0.5f;
            io.AddMousePosEvent(x, y); frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true); frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false); frame();
        };
        const Target edges[]{Target::Left, Target::Top, Target::Right, Target::Bottom};
        for (int i = 0; i < 4; ++i) {
            clickDock(i);
            for (int j = 0; j < 4; ++j)
                check(app->edgeDockCollapsed(edges[j]) == (i == j), "A right-aligned toolbar icon toggles only its matching dock");
            check(app->snappedPaneRects_.size() == 4, "The collapsed dock frees its space");
            clickDock(i);
            check(!app->edgeDockCollapsed(edges[i]) && app->snappedPaneRects_.size() == 5,
                "Clicking a dock icon again expands its dock");
        }
        check(app->snapLayoutPreferences_.leftWidthFraction == 0.31f, "Toggling a dock preserves its size");
        clickDock(0);
        clickDock(4);
        check(app->snappedPaneRects_.size() == 1 && app->snappedPaneRects_.front().pane == center &&
            app->snappedPaneRects_.front().width > 1250 && app->snappedPaneRects_.front().height > 760 &&
            app->snapSplitters_.empty(),
            "The all-edges toolbar icon collapses the remaining docks and gives the center the full workspace");
        const auto savedProject = root / "collapsed-docks.emu51";
        check(app->saveProjectToPath(savedProject), "Save a project with collapsed docks");
        auto restored = create();
        check(restored->openProjectFromPath(savedProject) && restored->collapsedEdgeDocks_ == app->collapsedEdgeDocks_ &&
            restored->snapTargetForPane(serial) == Target::Right,
            "Reopening a project preserves collapsed docks and their pane assignments");
        clickDock(4);
        check(app->snappedPaneRects_.size() == 5, "Clicking the all-edges icon again restores the original layout");
        io.DisplaySize = {420, 840};
        frame(); frame();
        clickDock(4);
        check(app->snappedPaneRects_.size() == 1, "The dock icons remain reachable in a narrow toolbar");
        clickDock(4);
        io.DisplaySize = {1280, 840};
        io.AddMousePosEvent(-100, -100);
    }

    static void test_project_workspace(const fs::path& root)
    {
        using Pane = Emu51ImguiApp::PaneKey;
        using MemoryKind = Emu51ImguiApp::MonitorView::MemoryKind;
        const auto path = root / "workspace.emu51";
        auto app = create();
        app->registersOpen_ = false;
        app->peripheralsOpen_ = true;
        app->projectPanelOpen_ = true;
        app->internalRamMonitor_.open = false;
        app->serialTerminalOpen_ = true;
        app->disassemblyViews_.clear();
        app->monitorViews_.clear();
        app->addDisassemblyView(0x3456, false);
        app->addMonitorView(MemoryKind::ExternalProgram, 0x1230);
        const Pane disassembly{Pane::Kind::Disassembly, app->disassemblyViews_.front().id};
        const Pane monitor{Pane::Kind::Monitor, app->monitorViews_.front().id};
        const Pane serial{Pane::Kind::SerialTerminal, 0};
        app->setPaneSnapTarget(disassembly, Emu51ImguiSnapTarget::Right);
        app->findPaneSnapAssignment(disassembly)->share = 0.75f;
        app->setPaneSnapTarget(monitor, Emu51ImguiSnapTarget::Bottom);
        app->snapLayoutPreferences_.bottomHeightFraction = 0.34f;
        auto& floating = app->ensureFloatingPaneState(serial);
        floating.position = ImVec2(75, 86);
        floating.size = ImVec2(615, 458);
        floating.hasSize = true;
        floating.collapsed = true;
        app->projectPathPicker_ = [&](bool) { return path; };
        app->saveProject();

        auto restored = create();
        restored->debuggerOpen_ = true;
        restored->recentProgramPaths_ = {(root / "recent.hex").string()};
        restored->addMonitorView(MemoryKind::ExternalData, 0x0);
        check(restored->openProjectFromPath(path) && !restored->registersOpen_ && restored->peripheralsOpen_ &&
            restored->projectPanelOpen_ && !restored->debuggerOpen_ && !restored->internalRamMonitor_.open &&
            restored->serialTerminalOpen_ && restored->recentProgramPaths_.size() == 1,
            "Opening a project restores its tool visibility and keeps application history");
        const auto* right = restored->findPaneSnapAssignment(disassembly);
        check(right && right->target == Emu51ImguiSnapTarget::Right && right->share == 0.75f &&
            restored->snapTargetForPane(monitor) == Emu51ImguiSnapTarget::Bottom &&
            restored->snapLayoutPreferences_.bottomHeightFraction == 0.34f &&
            restored->disassemblyViews_.size() == 1 && !restored->disassemblyViews_.front().followPc &&
            restored->disassemblyViews_.front().start == 0x3456 && restored->monitorViews_.size() == 1 &&
            restored->monitorViews_.front().start == 0x1230 && restored->monitorViews_.front().memoryKind == MemoryKind::ExternalProgram,
            "Restoring a project recreates its snap proportions, disassembly and memory monitor views");
        ImGui::NewFrame();
        restored->drawSerialTerminal();
        const auto* window = ImGui::FindWindowByName("Serial Terminal");
        check(window && window->Pos.x == 75 && window->Pos.y == 86 && window->Collapsed &&
            window->SizeFull.x == 615 && window->SizeFull.y == 458,
            "The first frame after opening restores floating window placement and collapse state");
        ImGui::EndFrame();
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

        restored->assemblyEditor_->ReplaceSectionText(0,0,0,0,"; unsaved after project open\n");
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
                {"\"version\":2", "\"version\":99"}, {"\"crystal_mhz\":11.0592", "\"crystal_mhz\":-1"},
                {"\"binary_address\":1024", "\"binary_address\":65536"},
                {"\"space\":1", "\"space\":4"}, {"\"breakpoints\":[256,512]", "\"breakpoints\":[-1]"},
                {"\"version\":2", "\"version\":2,\"version\":2"}}) {
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
        const auto stored = stateJson(*app);
        auto other = create();
        restoreState(*other,stored);
        check(other->recentProjectPaths_ == app->recentProjectPaths_, "Recent projects survive persistence round-trips");
        auto duplicateState = app->captureUiState();
        duplicateState.recentProjects = {"",selected,selected};
        duplicateState.recentProjects.insert(duplicateState.recentProjects.end(),app->recentProjectPaths_.begin(),app->recentProjectPaths_.end());
        other->applyUiState(duplicateState);
        check(other->recentProjectPaths_ == app->recentProjectPaths_,
            "Loading recent projects ignores blank and duplicate entries");
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
        Emu51UiState cleared;
        check(emu51DecodeUiState(stateJson(*app),cleared),"Cleared history decodes");
        check(app->recentProjectFileCount() == 0 && cleared.recentProjects.empty() && app->recentProgramPaths_ == programs &&
            app->projectPath_ == selected && app->assemblyEditorState_.dirty,
            "Clear recent projects persists an empty list without touching program recents, the current project or editor");
    }

    static void test_hardware_projects(const fs::path& root)
    {
        auto app = create();
        check(!app->hardware_ && !*app->hardwareModuleName(), "A standard session has no hardware module or menu item");
        const auto path = root / "hardware.emu51";
        Emu51Project project;
        project.hardwareModule = "unavailable-board";
        std::string error;
        check(emu51SaveProject(path, project, error), "Projects can retain a local hardware requirement");
        app->cpu_.PC = 0x1234;
        app->running_ = true;
        check(!app->openProjectFromPath(path) && app->cpu_.PC == 0x1234 && app->running_ &&
            app->statusMessage_.find("unavailable-board") != std::string::npos,
            "Unavailable project hardware is reported before changing the live machine");
        project.hardwareModule.clear();
        check(emu51SaveProject(path, project, error), "Generic projects save an empty hardware requirement");
        auto json = read(path);
        const std::string field = "\"hardware_module\":\"\",";
        const auto position = json.find(field);
        check(position != std::string::npos, "Project hardware field is present");
        if (position != std::string::npos) json.erase(position, field.size());
        write(path, json);
        check(app->openProjectFromPath(path) && !app->running_, "Older projects without a hardware field still open");
    }

    static void test_serial_terminal_docking(const fs::path& root)
    {
        using Pane = Emu51ImguiApp::PaneKey;
        const Pane pane{Pane::Kind::SerialTerminal, 0};
        auto app = create();
        app->showSerialTerminal();
        auto& io = ImGui::GetIO();
        io.DisplaySize = {1280, 840};
        const auto frame = [&](Emu51ImguiApp& terminalApp) {
            ImGui::NewFrame();
            terminalApp.updateDragSnapState();
            terminalApp.updateSnappedPaneLayout();
            terminalApp.drawSerialTerminal();
            ImGui::Render();
        };
        frame(*app);
        auto* window = ImGui::FindWindowByName("Serial Terminal");
        check(window != nullptr, "The Serial Terminal has a dockable window");
        if (!window) return;
        ImGui::SetWindowCollapsed(window, false);
        ImGui::SetWindowPos(window, {100, 100});
        frame(*app);
        const auto floatingWidth = window->Size.x;
        io.AddMousePosEvent(window->Pos.x + 60, window->TitleBarRect().GetCenter().y);
        frame(*app);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame(*app);
        io.AddMousePosEvent(1200, 420);
        frame(*app);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame(*app);
        frame(*app);
        check(app->snapTargetForPane(pane) == Emu51ImguiSnapTarget::Right &&
            window->Size.x < floatingWidth && (window->Flags & ImGuiWindowFlags_NoResize) &&
            (window->Flags & ImGuiWindowFlags_HorizontalScrollbar),
            "Title-bar dragging docks the terminal and keeps controls reachable in a narrow dock");
        check(app->hasActivePane() && app->activePane_ == pane, "The docked terminal participates in Window snap commands");

        const auto project = root / "docked-terminal.emu51";
        app->projectPathPicker_ = [&](bool) { return project; };
        app->saveProject();
        auto restored = create();
        check(restored->openProjectFromPath(project) && restored->serialTerminalOpen_ &&
            restored->snapTargetForPane(pane) == Emu51ImguiSnapTarget::Right,
            "Reopening a project restores the terminal in its saved dock");
        frame(*restored);
        ImGui::ActivateItemByID(window->GetID("#CLOSE"));
        frame(*restored);
        frame(*restored);
        check(!restored->serialTerminalOpen_ && restored->snappedPaneRects_.empty(),
            "Closing a docked terminal frees its dock space");
        restored->showSerialTerminal();
        frame(*restored);
        check(!restored->snappedPaneRects_.empty(), "Showing the terminal again uses its previous dock");
        restored->snapPane(pane, Emu51ImguiSnapTarget::None);
        io.AddMousePosEvent(-100, -100);
        frame(*restored);
        check(window->Size.x == floatingWidth && !(window->Flags & ImGuiWindowFlags_NoResize),
            "Floating the terminal restores its previous size and resize handles");
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

    static void test_tinybasic(const fs::path& root)
    {
        auto app = create();
        const auto demo = fs::path(EMU51_REPO_ROOT) / "progs/tinybasic";
        check(app->openProjectFromPath(demo / "tinybasic.emu51"), "TinyBASIC demo project loads with the built-in assembler");
        check(app->cpu_.model() == Emu51Model::i8052 && !app->unifiedMode_ && !app->running_ &&
            app->serialTerminalOpen_ && app->crystalMHz_ == 11.0592 && app->serialLineEnding_ == 2 &&
            app->serialFileLineEnding_ == 2 && app->serialFileLineDelayMs_ == 20,
            "TinyBASIC project restores its CPU, split memory, UART clock, CR endings and upload delay");
        const auto upstream = emu51_asm::prepare_program_file(fs::path(EMU51_REPO_ROOT) / "third_party/tb51/tb51.hex");
        bool same = upstream.result.success && upstream.image.bytes.size() == 3202;
        for (const auto& [address, byte] : upstream.image.bytes) same = same && app->cpu_.readProg(address) == byte;
        check(same, "TinyBASIC demo retains every byte of the published interpreter image");
        const auto ready = [&] {
            // Upstream C_IN polls RI at 8050h when waiting at a BASIC prompt.
            return !app->serialFileStream_.is_open() && !app->cpu_.pendingSerialReceiveCount() &&
                !app->cpu_.serialTransmitBusy() && app->cpu_.PC == 0x8050 && app->serialTerminalDisplay().ends_with("\n>");
        };
        const auto runUntilReady = [&] {
            unsigned remaining = 1000000;
            do { app->executeInstruction(); } while (--remaining && !ready());
            check(ready(), "TinyBASIC completes the UART command and returns to its prompt within the instruction budget");
        };
        runUntilReady();
        check(app->serialTerminalDisplay().find("MCS-51 TINY BASIC") != std::string::npos && app->cpu_.SP >= 0x80,
            "TinyBASIC boots, prints its banner over UART and uses upper internal RAM for the stack");
        const auto command = [&](const char* text) {
            app->serialTerminalOutput_.clear();
            std::snprintf(app->serialTerminalInput_, sizeof(app->serialTerminalInput_), "%s", text);
            check(app->sendSerialTerminalInput(), "TinyBASIC command queues through the serial terminal");
            runUntilReady();
            return app->serialTerminalDisplay();
        };
        check(command("print (2+3)*4").find("\n20\nOK\n>") != std::string::npos,
            "TinyBASIC evaluates expressions and accepts lowercase UART input");
        app->serialTerminalOpen_ = false;
        app->serialTerminalOutput_.clear();
        check(app->startSerialFileSend(demo / "squares.bas"), "TinyBASIC BASIC program starts streaming through Send file");
        // Controls are snapshotted for a transfer already in progress.
        app->serialFileLineDelayMs_ = 0;
        runUntilReady();
        app->serialFileLineDelayMs_ = 20;
        const auto listing = command("LIST");
        check(listing.find("10 FOR I=1 TO 5") != std::string::npos && listing.find("20 PRINT I,I*I") != std::string::npos &&
            listing.find("30 NEXT I") != std::string::npos && listing.find("40 END") != std::string::npos,
            "TinyBASIC stores and lists all numbered lines sent from the LF-ended BASIC file");
        const auto result = command("RUN");
        check(result == "RUN\n1       1\n2       4\n3       9\n4       16\n5       25\nOK\n>",
            "TinyBASIC runs every iteration of a stored FOR/NEXT program and prints the expected squares");
        app->serialTerminalOutput_.clear();
        std::snprintf(app->serialTerminalInput_, sizeof(app->serialTerminalInput_), "INPUT A");
        check(app->sendSerialTerminalInput(), "TinyBASIC INPUT command sends");
        for (unsigned i = 0; i < 100000 && !app->serialTerminalDisplay().ends_with(": "); ++i) app->executeInstruction();
        check(app->serialTerminalDisplay().ends_with(": "), "TinyBASIC INPUT requests a value through UART");
        command("7");
        check(command("PRINT A*6").find("\n42\nOK\n>") != std::string::npos,
            "TinyBASIC reads a UART numeric input, stores the variable and uses it in an expression");
        check(app->cpu_.serialReceiveOverruns() == 0 && app->cpu_.serialTransmitOverwrites() == 0 && app->cpu_.serialOutputDrops() == 0,
            "TinyBASIC command, upload, LIST and RUN complete without UART data loss");
        command("NEW"); command("10 GOTO 10");
        std::snprintf(app->serialTerminalInput_, sizeof(app->serialTerminalInput_), "RUN");
        check(app->sendSerialTerminalInput(), "TinyBASIC infinite loop starts through UART");
        for (unsigned i = 0; i < 30000; ++i) app->executeInstruction();
        check(!ready(), "TinyBASIC keeps executing an infinite stored program before Ctrl-C");
        app->serialHexInput_ = true; app->serialLineEnding_ = 0;
        std::snprintf(app->serialTerminalInput_, sizeof(app->serialTerminalInput_), "03");
        check(app->sendSerialTerminalInput(), "Ctrl-C queues as a raw UART control byte");
        runUntilReady();
        check(app->serialTerminalDisplay().find("WHAT?") != std::string::npos,
            "TinyBASIC Ctrl-C aborts execution and returns to the command prompt");
        app->serialHexInput_ = false; app->serialLineEnding_ = 2;
        check(app->saveProjectToPath(root / "tinybasic-roundtrip.emu51"), "TinyBASIC UART settings save in a project");
        auto restored = create();
        check(restored->openProjectFromPath(root / "tinybasic-roundtrip.emu51") && restored->serialLineEnding_ == 2 &&
            restored->serialFileLineEnding_ == 2 && restored->serialFileLineDelayMs_ == 20,
            "Serial ending and file pacing preferences survive project round trips");
        const auto saved = read(root / "tinybasic-roundtrip.emu51");
        for (const auto& [field, invalid] : std::vector<std::pair<std::string, std::string>>{
                {"\"serial_file_line_delay_ms\":20", "\"serial_file_line_delay_ms\":10001"},
                {"\"serial_file_ending\":2", "\"serial_file_ending\":4"},
                {"\"serial_input_ending\":2", "\"serial_input_ending\":-1"}}) {
            auto malformed = saved;
            const auto at = malformed.find(field);
            check(at != std::string::npos, "UART project validation fixture locates its field");
            if (at == std::string::npos) continue;
            malformed.replace(at, field.size(), invalid);
            write(root / "invalid-uart.emu51", malformed);
            restored->cpu_.PC = 123; restored->running_ = true;
            check(!restored->openProjectFromPath(root / "invalid-uart.emu51") && restored->cpu_.PC == 123 && restored->running_,
                "Invalid UART settings cannot partially restore a project");
        }
        auto legacy = saved;
        for (const char* field : {"\"serial_file_line_delay_ms\":20,", "\"serial_file_ending\":2,", "\"serial_input_ending\":2,"}) {
            const auto at = legacy.find(field);
            if (at != std::string::npos) legacy.erase(at, std::strlen(field));
        }
        write(root / "legacy-uart.emu51", legacy);
        check(restored->openProjectFromPath(root / "legacy-uart.emu51") && restored->serialLineEnding_ == 0 &&
            restored->serialFileLineEnding_ == 0 && restored->serialFileLineDelayMs_ == 0,
            "Older projects default to unchanged file endings and continuous sending");
    }

    static void test_serial_file_pacing(const fs::path& root)
    {
        const auto path = root / "paced.txt";
        const std::string payload = "A\r\nB\nC\rD";
        write(path, payload);
        for (int mode = 0; mode <= 3; ++mode) {
            auto app = create();
            const auto sample = fs::path(EMU51_REPO_ROOT) / "progs/asm/demo/serial_echo.asm";
            check(app->loadProgram(sample.string().c_str(), true, true), "Paced transfer loads echo firmware");
            app->running_ = true; app->runSteps(10000); app->serialTerminalOutput_.clear();
            app->cpu_.TH1 = app->cpu_.TL1 = 0xff;
            app->serialFileLineDelayMs_ = 1; app->serialFileLineEnding_ = mode;
            check(app->startSerialFileSend(path) && app->serialFileQueued_ == 3,
                "Paced transfers initially queue one complete CRLF source line");
            app->serialFileLineDelayMs_ = 0;
            unsigned budget = 10000;
            while (budget-- && !app->serialFileResumeCycle_) app->executeInstruction();
            check(app->serialFileResumeCycle_ > app->cycleCount_ && app->serialFileQueued_ == 3 &&
                !app->cpu_.pendingSerialReceiveCount(), "Line delay starts after the preceding UART line has drained");
            app->cycleCount_ = app->serialFileResumeCycle_ - 1;
            app->serviceSerialFileSend();
            check(app->serialFileQueued_ == 3, "Pacing cannot queue the next line before its emulated deadline");
            app->cycleCount_ = app->serialFileResumeCycle_;
            app->serviceSerialFileSend();
            check(app->serialFileQueued_ == 5, "Pacing queues the next LF source line exactly at its deadline");
            app->runSteps(50000);
            std::string expected = payload;
            if (mode) {
                const std::string ending = mode == 1 ? "\n" : mode == 2 ? "\r" : "\r\n";
                expected = "A" + ending + "B" + ending + "C" + ending + "D";
            }
            std::string received;
            for (const auto& byte : app->serialTerminalOutput_) received += static_cast<char>(byte.value);
            check(!app->serialFileStream_.is_open() && !app->cpu_.pendingSerialReceiveCount() && received == expected &&
                app->serialFileQueued_ == payload.size(), "Paced file input preserves raw/mixed endings, normalizes text and retains a final unterminated line");
            check(app->cpu_.serialReceiveOverruns() == 0 && app->cpu_.serialTransmitOverwrites() == 0,
                "Paced file transfers do not lose data");
            for (int invalid : {-1, 10001}) {
                app->serialFileLineDelayMs_ = invalid;
                check(!app->startSerialFileSend(path) && !app->cpu_.pendingSerialReceiveCount(),
                    "Invalid file line delays cannot inject input");
            }
            app->serialFileLineDelayMs_ = 20;
            check(app->startSerialFileSend(path), "Paced transfer can restart after validation errors");
            app->resetCpuState(true);
            app->serviceSerialFileSend();
            check(!app->serialFileStream_.is_open() && !app->cpu_.pendingSerialReceiveCount(),
                "CPU reset cancels paced file transfers");
        }
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
        app->cpu_.setModel(Emu51Model::i8052);
        app->cpu_.PC = 0x20; app->cpu_.SP = 0; app->cpu_.IRAM[0] = 0x12; app->cpu_.IRAM[255] = 0x34;
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

int main(int argc, char** argv)
{
    const bool workerOnly = argc == 2 && std::string(argv[1]) == "--worker-only";
#if defined(__APPLE__) || defined(__linux__)
    const bool pickerOnly = argc == 2 && std::string(argv[1]) == "--project-picker-only";
#endif
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    const fs::path root = fs::temp_directory_path() /
        ("emu51-ui-tests-" + std::to_string(Emu51CycleTiming::Clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    if (workerOnly) {
        Emu51ImguiAppTestAccess::test_background_execution(root);
#ifdef __APPLE__
    } else if (pickerOnly) {
        Emu51ImguiAppTestAccess::test_native_project_picker(root);
#elif defined(__linux__)
    } else if (pickerOnly) {
        Emu51ImguiAppTestAccess::test_linux_project_picker(root);
#endif
    } else {
    Emu51ImguiAppTestAccess::test_peripheral_editor(root);
    Emu51ImguiAppTestAccess::test_peripheral_window();
    Emu51ImguiAppTestAccess::test_peripheral_title_apply(root);
    Emu51ImguiAppTestAccess::test_live_device_layout(root);
    Emu51ImguiAppTestAccess::test_live_labels(root);
    Emu51ImguiAppTestAccess::test_live_content_scaling_and_key_modes(root);
    Emu51ImguiAppTestAccess::test_segment_colon_alignment();
    Emu51ImguiAppTestAccess::test_peripheral_workflows(root);
    Emu51ImguiAppTestAccess::test_microphone_covox(root);
    Emu51ImguiAppTestAccess::test_covox_duet(root);
    Emu51ImguiAppTestAccess::test_audio_host_pause_recovery();
    Emu51ImguiAppTestAccess::test_audio_buffer_setting(root);
    Emu51ImguiAppTestAccess::test_unedited_sources_close_without_prompt(root);
    Emu51ImguiAppTestAccess::test_unedited_projects_close_without_prompt(root);
    Emu51ImguiAppTestAccess::test_project_saved_buffer_failures(root);
    Emu51ImguiAppTestAccess::test_unsaved_changes(root);
    Emu51ImguiAppTestAccess::test_transactional_loading(root);
    Emu51ImguiAppTestAccess::test_undo_redo_after_save(root);
    Emu51ImguiAppTestAccess::test_component_examples(root);
    Emu51ImguiAppTestAccess::test_segment_examples(root);
    Emu51ImguiAppTestAccess::test_serial_peripheral_examples(root);
    Emu51ImguiAppTestAccess::test_lab_peripheral_examples();
    Emu51ImguiAppTestAccess::test_ui_state(root);
    Emu51ImguiAppTestAccess::test_disassembly_follow_pc_persistence(root);
    Emu51ImguiAppTestAccess::test_disassembly_title_control();
    Emu51ImguiAppTestAccess::test_in_memory_compilation();
    Emu51ImguiAppTestAccess::test_debugger(root);
    Emu51ImguiAppTestAccess::test_symbolic_debugging(root);
    Emu51ImguiAppTestAccess::test_cpu_models(root);
    Emu51ImguiAppTestAccess::test_project_sessions(root);
    Emu51ImguiAppTestAccess::test_project_save_shortcuts(root);
    Emu51ImguiAppTestAccess::test_project_workspace(root);
    Emu51ImguiAppTestAccess::test_collapsible_edge_docks(root);
    Emu51ImguiAppTestAccess::test_recent_projects(root);
    Emu51ImguiAppTestAccess::test_hardware_projects(root);
    Emu51ImguiAppTestAccess::test_serial_terminal(root);
    Emu51ImguiAppTestAccess::test_serial_terminal_docking(root);
    Emu51ImguiAppTestAccess::test_serial_file_send(root);
    Emu51ImguiAppTestAccess::test_serial_file_endings(root);
    Emu51ImguiAppTestAccess::test_tinybasic(root);
    Emu51ImguiAppTestAccess::test_serial_file_pacing(root);
    Emu51ImguiAppTestAccess::test_execution_servicing();
    Emu51ImguiAppTestAccess::test_recovery_integration(root);
    }
    fs::remove_all(root);
    ImGui::DestroyContext();
    if (failures)
        return 1;
    std::puts("emu51 application workflow tests passed");
    return 0;
}
