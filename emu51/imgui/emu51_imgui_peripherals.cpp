// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"
#include "emu51_editor_files.h"
#include "emu51_segment_display.h"
#include "imgui_internal.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <exception>
#include <stdexcept>
#include <tuple>
#include "portable-file-dialogs.h"

namespace {
int peripheralTitleTabs(bool pending, bool& apply, bool valid = true)
{
    auto* window = ImGui::GetCurrentWindow();
    auto& dc = window->DC;
    const auto& style = ImGui::GetStyle();
    const auto title = window->TitleBarRect();
    const float font = ImGui::GetFontSize();
    const float collapseLeft = style.WindowMenuButtonPosition == ImGuiDir_Left ? font+2*style.ItemInnerSpacing.x : 0;
    const float collapseRight = style.WindowMenuButtonPosition == ImGuiDir_Right ? font+style.ItemInnerSpacing.x : 0;
    const float start = title.Min.x+style.FramePadding.x+collapseLeft+
        ImGui::CalcTextSize("Peripherals").x+2*style.ItemInnerSpacing.x;
    const float buttonEnd = title.Max.x-style.FramePadding.x-font-2*style.ItemInnerSpacing.x-collapseRight;
    const float buttonWidth = ImGui::CalcTextSize("Apply").x+2*style.FramePadding.x;
    const float buttonStart = buttonEnd-buttonWidth;
    const float end = buttonStart-style.ItemInnerSpacing.x;
    const ImGuiID id = window->GetID("peripheral-tabs");
    auto* tabs = GImGui->TabBars.GetOrAddByKey(id);
    tabs->ID = id;
    const char* labels[] = {"Graph", "Digital wiring", "Analog wiring", "Live"};
    int selected = 0;
    for (int i = 0; i < 4; ++i)
        if (tabs->SelectedTabId == ImHashStr(labels[i],0,id)) selected = i;
    // Submit controls in the decoration. Restore content layout so
    // the active pane starts immediately below the title bar, even when scrolled.
    const auto saved = std::make_tuple(dc.CursorPos,dc.CursorPosPrevLine,dc.CursorMaxPos,dc.IdealMaxPos,
        dc.CurrLineSize,dc.PrevLineSize,dc.CurrLineTextBaseOffset,dc.PrevLineTextBaseOffset,
        dc.IsSameLine,dc.IsSetPos,dc.NavLayerCurrent);
    dc.NavLayerCurrent = ImGuiNavLayer_Menu;
    ImRect buttonBounds(ImVec2(buttonStart,title.Min.y),ImVec2(buttonEnd,title.Max.y));
    auto buttonClip = buttonBounds; buttonClip.ClipWith(window->OuterRectClipped);
    ImGui::PushClipRect(buttonClip.Min,buttonClip.Max,false);
    dc.CursorPos = buttonBounds.Min;
    ImGui::BeginDisabled(!pending || !valid);
    apply = ImGui::Button("Apply##peripherals",buttonBounds.GetSize());
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(pending ? "Apply graph and device settings; reset and pause the CPU." : "No pending graph or device settings. Names and positions are saved with the project.");
    ImGui::PopClipRect();
    // Prioritize Apply and the native close/collapse controls in narrow docks.
    // Retain the selected tab until there is room to draw its headers again.
    if (end > start) {
        ImRect bounds(ImVec2(start,title.Min.y),ImVec2(end,title.Max.y));
        auto clip = bounds; clip.ClipWith(window->OuterRectClipped);
        ImGui::PushClipRect(clip.Min,clip.Max,false);
        ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize,0);
        tabs->SeparatorMinX = start; tabs->SeparatorMaxX = end;
        ImGuiTabBarFlags flags = ImGuiTabBarFlags_FittingPolicyScroll;
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) flags |= ImGuiTabBarFlags_IsFocused;
        if (ImGui::BeginTabBarEx(tabs,bounds,flags)) {
            for (int i = 0; i < 4; ++i) if (ImGui::BeginTabItem(labels[i])) {
                selected = i;
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::PopStyleVar();
        ImGui::PopClipRect();
    }
    std::tie(dc.CursorPos,dc.CursorPosPrevLine,dc.CursorMaxPos,dc.IdealMaxPos,
        dc.CurrLineSize,dc.PrevLineSize,dc.CurrLineTextBaseOffset,dc.PrevLineTextBaseOffset,
        dc.IsSameLine,dc.IsSetPos,dc.NavLayerCurrent) = saved;
    return selected;
}

void drawLcd(const e51p_lcd_snapshot& lcd,float scale)
{
    const float cellW=14*scale, cellH=23*scale, pixel=2*scale;
    const auto origin=ImGui::GetCursorScreenPos();
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin,{origin.x+lcd.columns*cellW+12*scale,origin.y+lcd.rows*cellH+12*scale},IM_COL32(24,55,28,255),4*scale);
    const auto ink=IM_COL32(168,236,134,255);
    for (unsigned row=0; row<lcd.rows; ++row) for (unsigned col=0; col<lcd.columns; ++col) {
        const auto index=row*lcd.columns+col;
        const auto at=ImVec2(origin.x+6*scale+col*cellW,origin.y+6*scale+row*cellH);
        const auto code=lcd.cells[index];
        if (lcd.display_on) {
            if (code<16) {
                for (unsigned y=0; y<8; ++y) for (unsigned x=0; x<5; ++x)
                    if (lcd.cgram[(code&7)*8+y]&(16u>>x))
                        draw->AddRectFilled({at.x+x*pixel,at.y+y*pixel},{at.x+(x+1)*pixel-.3f*scale,at.y+(y+1)*pixel-.3f*scale},ink);
            } else {
                char text[]{code>=32 && code<127 ? static_cast<char>(code) : '?',0};
                draw->AddText(ImGui::GetFont(),18*scale,at,ink,text);
            }
            if (lcd.cursor_cell==static_cast<int>(index)) {
                if (lcd.cursor_on) draw->AddLine({at.x,at.y+18*scale},{at.x+10*scale,at.y+18*scale},ink,2*scale);
                if (lcd.blink_on && std::fmod(ImGui::GetTime(),1.0)<.5)
                    draw->AddRect(at,{at.x+10*scale,at.y+18*scale},ink);
            }
        }
    }
    ImGui::Dummy({lcd.columns*cellW+12*scale,lcd.rows*cellH+12*scale});
}

bool pinCombo(const char* label, std::string& selected, const std::vector<Emu51PeripheralPin>& pins,
    unsigned kind, unsigned direction = 0)
{
    bool changed = false;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo(label, selected.empty() ? "Select pin" : selected.c_str())) {
        for (const auto& p : pins) {
            if (p.kind != kind || (direction && p.direction != direction)) continue;
            if (ImGui::Selectable(p.name.c_str(), selected == p.name)) { selected = p.name; changed = true; }
        }
        ImGui::EndCombo();
    }
    return changed;
}
void audioDeviceCombo(const char* label, int& index, const std::vector<std::string>& names)
{
    const auto preview = index >= 0 && index < static_cast<int>(names.size()) ? names[static_cast<std::size_t>(index)].c_str() : "System default";
    if (ImGui::BeginCombo(label, preview)) {
        if (ImGui::Selectable("System default", index == -1)) index = -1;
        for (std::size_t i = 0; i < names.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(names[i].c_str(), index == static_cast<int>(i))) index = static_cast<int>(i);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}
}

void Emu51ImguiApp::installPeripheralSession(std::unique_ptr<Emu51PeripheralSession> session)
{
    if (session) {
        if (peripherals_) session->carryStorageFrom(*peripherals_);
        session->activateStorage();
    }
    cpu_.setPortIo(nullptr);
    peripherals_ = std::move(session); // closes the old host before destroying its graph
    liveAudioMessage_.clear(); liveHeldKeypads_.clear(); liveKeypadsThisFrame_.clear();
    audioCaptureDevices_.clear(); audioPlaybackDevices_.clear();
    audioCaptureDevice_ = audioPlaybackDevice_ = -1;
    if (peripherals_) cpu_.setPortIo(peripherals_->gpio());
    audioBufferMs_ = 0;
    if (peripherals_) for (const auto& d : peripherals_->config().devices)
        if (d.kind == Emu51PeripheralKind::Audio) audioBufferMs_ = d.audioBufferMs;
}
bool Emu51ImguiApp::applyPeripheralConfig(const Emu51PeripheralConfig& config, double crystalMHz)
{
    // config may refer to the active session, which will be replaced below.
    auto next = config;
    const auto base = projectPath_.empty() ? std::filesystem::current_path() : std::filesystem::path(projectPath_).parent_path();
    for (auto& device : next.devices) for (auto& image : device.memoryImages)
        if (!std::filesystem::path(image.path).is_absolute() && !image.path.empty()) image.path = (base/image.path).lexically_normal().string();
    std::unique_ptr<Emu51PeripheralSession> candidate;
    try {
        if (!Emu51CycleTiming::validCrystalMHz(crystalMHz)) throw std::runtime_error("Invalid crystal frequency");
        if (peripherals_ && peripherals_->circuitBus() && !emu51CircuitBus(next) && !next.devices.empty()) {
            Emu51Project compatible; compatible.peripherals = next; compatible.unified = unifiedMode_;
            compatible.execution = {cycleExact_,crystalMHz,stepsPerFrame_};
            std::string error;
            if (!emu51MigrateProject(compatible,error)) throw std::runtime_error(error);
            next = std::move(compatible.peripherals);
        }
        emu51ValidatePeripheralConfig(next);
        if (!next.devices.empty()) candidate = withoutExecutionLock([&] { return Emu51PeripheralSession::create(next, crystalMHz,peripherals_.get()); });
    } catch (const std::exception& e) {
        peripheralMessage_ = statusMessage_ = e.what(); return false;
    }
    stopForDebugger("applying peripherals");
    installPeripheralSession(std::move(candidate));
    if (peripherals_ && hardware_) hardware_->setEnabled(false);
    peripheralDraft_ = next;
    crystalMHz_ = peripherals_ ? peripherals_->crystalMHz() : crystalMHz;
    resetCpuState(true);
    peripheralMessage_ = statusMessage_ = peripherals_ ? "Peripheral graph applied; CPU reset and paused" : "Peripherals disconnected; CPU reset and paused";
    return true;
}
void Emu51ImguiApp::resetCpuAndPeripherals()
{
    if (peripherals_) {
        std::string error;
        if (!peripherals_->syncAudio(false, error)) peripheralMessage_ = error + "; reopen audio devices to retry";
    }
    cpu_.init();
    if (peripherals_) peripherals_->resetStatus();
}
bool Emu51ImguiApp::syncPeripheralAudio()
{
    if (!peripherals_) return false;
    if (hardware_ && hardware_->enabled()) {
        stopForDebugger("hardware module enabled; peripherals disconnected");
        installPeripheralSession(nullptr);
        peripheralMessage_ = statusMessage_;
        return false;
    }
    const bool wasRunning = peripherals_->audioRunning();
    std::string error;
    if (!peripherals_->syncAudio(running_ && cycleExact_ && !stepTask_, error)) {
        stopForDebugger(error);
        peripheralMessage_ = error;
        return false;
    }
    const bool resumed = !wasRunning && peripherals_->audioRunning();
    // Begin CPU pacing after blocking device startup, in the same fresh epoch
    // as capture/playback. Initialization is not emulated execution time.
    if (resumed) cycleTiming_.reset();
    return resumed;
}
bool Emu51ImguiApp::applyPeripheralAudioBuffer(int milliseconds)
{
    if (!peripherals_ || !emu51ValidAudioBufferMs(milliseconds)) {
        peripheralMessage_ = "Audio buffer must be 10..2000 ms, or 0 for automatic.";
        return false;
    }
    const bool resume = running_;
    stopForDebugger("changing audio buffer");
    std::string error;
    const bool applied = peripherals_->setAudioBufferMs(milliseconds, error);
    // A resize can succeed even if reopening a device fails. Keep the editable
    // graph in agreement with the actual saved configuration in either case.
    for (const auto& d : peripherals_->config().devices) if (d.kind == Emu51PeripheralKind::Audio) {
        audioBufferMs_ = d.audioBufferMs;
        for (auto& draft : peripheralDraft_.devices) if (draft.id == d.id && draft.kind == d.kind)
            draft.audioBufferMs = d.audioBufferMs;
    }
    if (resume) { toggleRun(); syncPeripheralAudio(); }
    peripheralMessage_ = applied ? "Audio buffer applied at the current CPU position." : error;
    return applied;
}
bool Emu51ImguiApp::flushPeripheralStorage()
{
    if (!peripherals_) return true;
    std::string error;
    const bool saved = peripherals_->flushStorage(error,[this](const auto& path,auto bytes) {
        return withoutExecutionLock([&] { return emu51WriteTextFileAtomically(path,bytes); });
    });
    if (!saved) peripheralMessage_ = statusMessage_ = error;
    return saved;
}
void Emu51ImguiApp::drawPeripherals()
{
    if (!running_) (void)flushPeripheralStorage();
    const auto releaseButtons = [&] {
        if (peripherals_) for (const auto& d : peripherals_->config().devices)
            if (d.kind == Emu51PeripheralKind::Button) (void)peripherals_->setPressed(d.id,false);
    };
    if (!peripheralsOpen_) { releaseLiveKeypads(); releaseButtons(); peripheralEditor_.suspend(); peripheralLive_.suspend(); return; }
    const PaneKey pane{PaneKey::Kind::Peripherals, 0};
    ImGuiWindowFlags flags = 0;
    applyWindowPlacement(pane, ImVec2(1180, 780), &flags);
    if (snapTargetForPane(pane) == Emu51ImguiSnapTarget::None)
        ImGui::SetNextWindowSizeConstraints(ImVec2(480, 320), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign,ImVec2(0,0.5f));
    const bool visible = ImGui::Begin("Peripherals", &peripheralsOpen_, flags);
    ImGui::PopStyleVar();
    if (!peripheralsOpen_) executionRedrawPending_.store(true,std::memory_order_release);
    registerPaneWindow(pane);
    drawPaneSnapContextMenu(pane);
    if (!visible || !peripheralsOpen_) { releaseLiveKeypads(); releaseButtons(); peripheralEditor_.suspend(); peripheralLive_.suspend(); ImGui::End(); return; }
    captureFloatingPaneState(pane);
    notePaneActivity(pane, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
    bool graphVisible = false;
    bool apply = false;
    std::string draftError = peripheralEditor_.validationError();
    try { emu51ValidatePeripheralConfig(peripheralDraft_); }
    catch (const std::exception& e) { draftError = e.what(); }
    const int selectedTab = peripheralTitleTabs(
        peripheralDraft_ != (peripherals_ ? peripherals_->config() : Emu51PeripheralConfig{}),apply,draftError.empty());
    if (!draftError.empty()) ImGui::TextWrapped("%s",draftError.c_str());
    if (apply) {
        if (!applyPeripheralConfig(peripheralDraft_,crystalMHz_)) ImGui::OpenPopup("Cannot apply peripherals");
        executionRedrawPending_.store(true,std::memory_order_release);
    }
    if (ImGui::BeginPopup("Cannot apply peripherals")) {
        ImGui::TextUnformatted("Cannot apply peripherals");
        ImGui::Separator();
        ImGui::TextWrapped("%s",peripheralMessage_.c_str());
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    const auto active = peripherals_ ? peripherals_->config() : Emu51PeripheralConfig{};
    const auto pins = Emu51PeripheralSession::describe(peripheralDraft_);
    if (selectedTab != 3) { releaseLiveKeypads(); releaseButtons(); }
    {
        if (selectedTab == 0) {
            graphVisible = true;
            const auto model = cpu_.model();
            const bool inspectorVisible = peripheralEditor_.inspectorVisible();
            const auto previousMessage = peripheralMessage_;
            std::string message = previousMessage;
            std::vector<std::string> activeMemory;
            if (peripherals_) for (const auto& m : peripherals_->memories())
                if (std::any_of(peripheralDraft_.devices.begin(),peripheralDraft_.devices.end(),[&](const auto& d) {
                    return d.id == m.id && d.memoryCapacity == e51p_memory_capacity(m.storage.get()) &&
                        ((d.kind == Emu51PeripheralKind::Rom) == (e51p_memory_kind(m.storage.get()) == E51P_ROM));
                })) activeMemory.push_back(m.id);
            peripheralEditor_.setActiveMemory(activeMemory);
            withoutExecutionLock([&] { peripheralEditor_.draw(peripheralDraft_, model, message); });
            if (const auto request = peripheralEditor_.takeMemoryRequest()) {
                try {
                    const auto id = request->id;
                    const auto selected = withoutExecutionLock([&]() -> std::string {
                        const std::vector<std::string> filters{"Binary images","*.bin","All files","*"};
                        if (request->save) return pfd::save_file("Save physical memory",id+".bin",filters).result();
                        const auto paths = pfd::open_file("Load physical memory",{},filters).result();
                        return paths.empty() ? std::string{} : paths.front();
                    });
                    if (!selected.empty() && peripherals_ && peripherals_->memory(id)) {
                        if (request->save) {
                            const auto bytes = peripherals_->snapshotMemory(id,request->offset,request->length);
                            const std::string_view data(reinterpret_cast<const char*>(bytes.data()),bytes.size());
                            if (!withoutExecutionLock([&] { return emu51WriteTextFileAtomically(selected,data); })) throw std::runtime_error("Cannot save binary; original file preserved");
                        } else {
                            const auto bytes = withoutExecutionLock([&] { return Emu51PeripheralSession::readBinary(selected,request->length); });
                            if (!peripherals_->programMemory(id,request->offset,bytes)) throw std::runtime_error("Physical memory range is no longer valid");
                            invalidateInstructionCache();
                        }
                        peripheralMessage_ = statusMessage_ = request->save ? "Physical memory saved; selectors unchanged" : "Physical memory programmed; selectors unchanged";
                    }
                } catch (const std::exception& e) { peripheralMessage_ = statusMessage_ = e.what(); }
            }
            if (message != previousMessage) peripheralMessage_ = std::move(message);
            if (inspectorVisible != peripheralEditor_.inspectorVisible()) markLayoutStateDirty();
        }
        if (selectedTab == 1) {
            ImGui::TextWrapped("Connections are bidirectional. Bit numbers start at 0. Use several rows for permutations or groups across ports.");
            ImGui::BeginDisabled(peripheralDraft_.digital.size() >= 128);
            if (ImGui::Button("Add digital wire")) peripheralDraft_.digital.push_back({});
            ImGui::EndDisabled();
            if (ImGui::BeginTable("digital-wires", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Pin A", 0, 3); ImGui::TableSetupColumn("Bit A");
                ImGui::TableSetupColumn("Pin B", 0, 3); ImGui::TableSetupColumn("Bit B");
                ImGui::TableSetupColumn("Width"); ImGui::TableSetupColumn("Remove"); ImGui::TableHeadersRow();
                for (std::size_t i = 0; i < peripheralDraft_.digital.size();) {
                    auto& w = peripheralDraft_.digital[i]; ImGui::PushID(static_cast<int>(i)); ImGui::TableNextRow();
                    ImGui::TableNextColumn(); pinCombo("##a", w.a, pins, E51P_DIGITAL);
                    ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##a-bit", &w.aBit, 0);
                    ImGui::TableNextColumn(); pinCombo("##b", w.b, pins, E51P_DIGITAL);
                    ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##b-bit", &w.bBit, 0);
                    ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##width", &w.width, 0);
                    ImGui::TableNextColumn(); const bool remove = ImGui::SmallButton("Remove"); ImGui::PopID();
                    if (remove) peripheralDraft_.digital.erase(peripheralDraft_.digital.begin() + static_cast<std::ptrdiff_t>(i)); else ++i;
                }
                ImGui::EndTable();
            }
        }
        if (selectedTab == 2) {
            ImGui::TextWrapped("Voltages in volts. One output can feed many inputs; each input has one source.");
            ImGui::BeginDisabled(peripheralDraft_.analog.size() >= 64);
            if (ImGui::Button("Add analog wire")) peripheralDraft_.analog.push_back({});
            ImGui::EndDisabled();
            if (ImGui::BeginTable("analog-wires", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Output", 0, 3); ImGui::TableSetupColumn("Input", 0, 3); ImGui::TableSetupColumn("Remove"); ImGui::TableHeadersRow();
                for (std::size_t i = 0; i < peripheralDraft_.analog.size();) {
                    auto& w = peripheralDraft_.analog[i]; ImGui::PushID(static_cast<int>(i)); ImGui::TableNextRow();
                    ImGui::TableNextColumn(); pinCombo("##from", w.from, pins, E51P_ANALOG, E51P_OUTPUT);
                    ImGui::TableNextColumn(); pinCombo("##to", w.to, pins, E51P_ANALOG, E51P_INPUT);
                    ImGui::TableNextColumn(); const bool remove = ImGui::SmallButton("Remove"); ImGui::PopID();
                    if (remove) peripheralDraft_.analog.erase(peripheralDraft_.analog.begin() + static_cast<std::ptrdiff_t>(i)); else ++i;
                }
                ImGui::EndTable();
            }
        }
        if (selectedTab == 3) drawPeripheralLive(active);
    }
    if (!graphVisible) peripheralEditor_.suspend();
    if (selectedTab != 3) peripheralLive_.suspend();
    ImGui::End();
}

void Emu51ImguiApp::releaseLiveKeypads()
{
    if (peripherals_) for (const auto& id : liveHeldKeypads_) {
        (void)peripherals_->setKeys(id,0); (void)peripherals_->setDisplayKeys(id,0);
    }
    liveHeldKeypads_.clear(); liveKeypadsThisFrame_.clear();
}
void Emu51ImguiApp::drawPeripheralKeys(const Emu51PeripheralDevice& d,unsigned count,unsigned columns)
{
    const bool controller = d.kind != Emu51PeripheralKind::Keypad && d.kind != Emu51PeripheralKind::KeyMatrix;
    uint64_t mask = controller ? peripherals_->displayKeys(d.id) : peripherals_->keys(d.id);
    bool momentary = peripheralLive_.momentaryKeys(d.id);
    int mode = momentary ? 1 : 0;
    ImGui::PushID("key-input");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##key-mode",&mode,"Toggle\0While pressed\0")) {
        momentary = mode == 1; peripheralLive_.setMomentaryKeys(d.id,momentary); mask = 0;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle: click to hold, click again to release. While pressed: release with the mouse button.");
    const auto previous = mask;
    if (momentary) mask = 0;
    const bool custom = !controller && !d.keyLayout.empty();
    if (custom) columns = static_cast<unsigned>(d.keyLayoutColumns);
    const unsigned slots = custom ? static_cast<unsigned>(d.keyLayout.size()) : count;
    const unsigned rows = (slots+columns-1)/columns;
    const auto available = ImGui::GetContentRegionAvail(), spacing = ImGui::GetStyle().ItemSpacing;
    const ImVec2 keySize{std::max(4.0f,(available.x-(columns-1)*spacing.x)/columns),
        std::max(4.0f,(available.y-ImGui::GetFrameHeightWithSpacing()-(rows-1)*spacing.y)/rows)};
    for (unsigned slot = 0; slot < slots; ++slot) {
        const int assigned = custom ? d.keyLayout[slot] : static_cast<int>(slot);
        if (assigned < 0) {
            ImGui::Dummy(keySize);
            if ((slot+1)%columns && slot+1<slots) ImGui::SameLine();
            continue;
        }
        const auto key = static_cast<unsigned>(assigned);
        ImGui::PushID(static_cast<int>(slot));
        const bool held = (previous&(uint64_t(1)<<key)) != 0;
        if (held) ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.15f,.5f,.25f,1));
        auto label = emu51KeyLabel(d,key);
        if (key < d.keyLegends.size() && !d.keyLegends[key].empty()) label += "\n"+d.keyLegends[key];
        const bool clicked = ImGui::Button("##contact",keySize);
        const bool active = ImGui::IsItemActive();
        const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        const auto textSize = ImGui::CalcTextSize(label.c_str(),nullptr,false);
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(min,max,true);
        draw->AddText({min.x+(max.x-min.x-textSize.x)/2,min.y+(max.y-min.y-textSize.y)/2},ImGui::GetColorU32(ImGuiCol_Text),label.c_str());
        draw->PopClipRect();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nContact %u",label.c_str(),key);
        if (momentary) {
            if (active) mask |= uint64_t(1)<<key;
        } else if (clicked) mask ^= uint64_t(1)<<key;
        if (held) ImGui::PopStyleColor();
        ImGui::PopID();
        if ((slot+1)%columns && slot+1<slots) ImGui::SameLine();
    }
    if (ImGui::Button("Release all",{ImGui::GetContentRegionAvail().x,ImGui::GetFrameHeight()})) mask = 0;
    if (controller) (void)peripherals_->setDisplayKeys(d.id,mask);
    else (void)peripherals_->setKeys(d.id,static_cast<unsigned>(mask));
    if (momentary && mask) liveKeypadsThisFrame_.insert(d.id);
    ImGui::PopID();
}
void Emu51ImguiApp::drawPeripheralLiveDevice(const Emu51PeripheralDevice& d,float scale)
{
    ImGui::PushID(d.id.c_str());
    if (emu51PeripheralDisplayKind(d.kind)>=0) {
        e51p_segment_snapshot display{};
        if (peripherals_->displayState(d.id,display)) {
            const int style=d.kind==Emu51PeripheralKind::Bargraph ? 0 : d.kind==Emu51PeripheralKind::Display14 ? 14 :
                d.kind==Emu51PeripheralKind::Display16 ? 16 : d.kind==Emu51PeripheralKind::Ht16k33 ? d.displayStyle : 7;
            emu51DrawSegmentDisplay(display,style,d.displayColor,false,d.displayColon,scale);
            if ((d.kind==Emu51PeripheralKind::Ht16k33 || d.kind==Emu51PeripheralKind::Tm1637) && ImGui::TreeNode("Keys")) {
                const unsigned count=d.kind==Emu51PeripheralKind::Ht16k33 ? 39 : 16;
                const unsigned columns=std::max(1u,std::min(13u,static_cast<unsigned>(std::max(30.0f,ImGui::GetContentRegionAvail().x)/(36*scale))));
                drawPeripheralKeys(d,count,columns);
                ImGui::TreePop();
            }
        }
    } else if (d.kind == Emu51PeripheralKind::Led) {
        bool defined=false; const bool on=peripherals_->ledState(d.id,defined);
        const auto at = ImGui::GetCursorScreenPos();
        const auto available = ImGui::GetContentRegionAvail();
        const float radius = std::max(2.0f,(std::min(available.x,available.y)-4*scale)/2);
        const ImVec2 center{at.x+available.x/2,at.y+available.y/2};
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddCircleFilled(center,radius+2*scale,IM_COL32(55,55,55,255));
        draw->AddCircleFilled(center,radius,!defined ? IM_COL32(180,115,20,255) : on ? IM_COL32(255,55,35,255) : IM_COL32(55,18,14,255));
        ImGui::Dummy(available);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",!defined ? "Undefined input" : on ? "On" : "Off");
    } else if (d.kind == Emu51PeripheralKind::Switch) {
        bool pressed=peripherals_->pressed(d.id);
        if (ImGui::Checkbox("On",&pressed)) {
            (void)peripherals_->setPressed(d.id,pressed);
            for (auto& draft : peripheralDraft_.devices) if (draft.id==d.id) draft.initiallyPressed=pressed;
        }
    } else if (d.kind == Emu51PeripheralKind::Lcd) {
        e51p_lcd_snapshot lcd{};
        if (peripherals_->lcdState(d.id,lcd)) drawLcd(lcd,scale);
    } else if (d.kind == Emu51PeripheralKind::Keypad || d.kind == Emu51PeripheralKind::KeyMatrix) {
        drawPeripheralKeys(d,static_cast<unsigned>(emu51KeyCount(d)),
            d.kind == Emu51PeripheralKind::Keypad ? 4 : static_cast<unsigned>(d.columns));
    }
    ImGui::PopID();
    if (d.kind == Emu51PeripheralKind::Voltage || d.kind == Emu51PeripheralKind::Digital) {
        ImGui::PushID(d.id.c_str()); ImGui::SetNextItemWidth(-1);
        double voltage = d.voltage; int value = d.digitalValue;
        const bool changed = d.kind == Emu51PeripheralKind::Voltage ? ImGui::InputDouble("##voltage", &voltage, 0.1, 1, "%.6g V") :
            ImGui::InputInt("##value", &value, 0, 0, ImGuiInputTextFlags_CharsHexadecimal);
        if (changed && peripherals_->setSource(d.id, d.kind == Emu51PeripheralKind::Voltage ? voltage : value)) {
            for (auto& draft : peripheralDraft_.devices) if (draft.id == d.id && draft.kind == d.kind) {
                draft.voltage = voltage; draft.digitalValue = value;
            }
        }
        ImGui::PopID();
    }
    if (d.kind == Emu51PeripheralKind::Audio) drawPeripheralAudio(d,scale);
}
void Emu51ImguiApp::drawPeripheralAudio(const Emu51PeripheralDevice& d,float scale)
{
    if (!ImGui::BeginTabBar("audio-options")) return;
    if (ImGui::BeginTabItem("Signals")) {
        if (d.audioDirections & E51P_AUDIO_CAPTURE) {
            float db = static_cast<float>(d.inputGainDb);
            ImGui::SetNextItemWidth(-90*scale);
            if (ImGui::SliderFloat("Mic gain", &db, -20, 60, "%+.1f dB", ImGuiSliderFlags_AlwaysClamp) &&
                peripherals_->setInputGainDb(db)) {
                for (auto& draft : peripheralDraft_.devices)
                    if (draft.id == d.id && draft.kind == d.kind) draft.inputGainDb = db;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Microphone gain before ADC conversion; adjust while running.");
        }
        int& span = audioScopeSpan_;
        float& outputZoom = audioScopeZoom_;
        ImGui::SetNextItemWidth(100*scale);
        ImGui::Combo("Window", &span, "10 ms\0" "20 ms\0" "40 ms\0" "80 ms\0");
        std::array<e51p_audio_scope_frame, 4096> samples{};
        uint32_t count = 0, rate = 0;
        const uint32_t wanted = 480u << span; // desktop bridge: 48 kHz
        if (peripherals_->audioScope(samples.data(), wanted, count, rate)) {
            float inputPeak = 0;
            if (d.audioDirections & E51P_AUDIO_CAPTURE) for (uint32_t i = 0; i < count; ++i)
                inputPeak = std::max(inputPeak,std::abs(samples[i].adc));
            const unsigned channels = ((d.audioDirections & E51P_AUDIO_CAPTURE) ? 1u : 0u)+((d.audioDirections & E51P_AUDIO_PLAYBACK) ? 1u : 0u);
            const float reserved = channels*ImGui::GetTextLineHeightWithSpacing()+
                ((d.audioDirections & E51P_AUDIO_PLAYBACK) ? ImGui::GetFrameHeightWithSpacing() : 0)+
                (inputPeak >= 1 ? ImGui::GetTextLineHeightWithSpacing() : 0)+3*ImGui::GetStyle().ItemSpacing.y;
            const float plotHeight = std::max(24*scale,(ImGui::GetContentRegionAvail().y-reserved)/std::max(1u,channels));
            const auto plot = [&](const char* label, int channel, float zoom) {
                std::array<float, 4096> values{};
                float peak = 0;
                for (uint32_t i = 0; i < count; ++i) {
                    const float value = channel == 1 ? samples[i].adc : samples[i].output;
                    values[i] = value * zoom; peak = std::max(peak, std::abs(value));
                }
                ImGui::Text("%s  %.1f%%", label, 100 * peak);
                ImGui::PushID(label);
                ImGui::PlotLines("##waveform", values.data(), static_cast<int>(count), 0,
                    count ? nullptr : "No audio", -1, 1, ImVec2(-1, plotHeight));
                ImGui::PopID();
            };
            if (d.audioDirections & E51P_AUDIO_CAPTURE) {
                if (inputPeak >= 1) ImGui::TextColored({1,.4f,.2f,1},"Input clipping");
                plot("Input", 1, 1);
            }
            if (d.audioDirections & E51P_AUDIO_PLAYBACK) {
                ImGui::SetNextItemWidth(120*scale);
                ImGui::SliderFloat("Scope zoom", &outputZoom, 1, 100, "%.0fx", ImGuiSliderFlags_Logarithmic);
                plot("Output", 2, outputZoom);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scope zoom changes only the display.");
            }
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Devices")) {
        ImGui::TextUnformatted(peripherals_->audioRunning() ? "Running" : peripherals_->audioOpen() ? "Paused" : "Closed");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Audio follows Run/Pause in Cycle exact mode.");
        if (ImGui::Button("Refresh devices")) {
            try {
                if (peripherals_->enumerateAudio(audioCaptureDevices_,audioPlaybackDevices_,liveAudioMessage_))
                    liveAudioMessage_ = "Devices refreshed";
            } catch (const std::exception& e) { liveAudioMessage_ = e.what(); }
        }
        ImGui::BeginDisabled(peripherals_->audioOpen());
        if (d.audioDirections & E51P_AUDIO_CAPTURE) {
            ImGui::SetNextItemWidth(-64*scale);
            audioDeviceCombo("Input",audioCaptureDevice_,audioCaptureDevices_);
        }
        if (d.audioDirections & E51P_AUDIO_PLAYBACK) {
            ImGui::SetNextItemWidth(-64*scale);
            audioDeviceCombo("Output",audioPlaybackDevice_,audioPlaybackDevices_);
        }
        if (ImGui::CollapsingHeader("Channels")) {
            if (d.audioDirections & E51P_AUDIO_CAPTURE) {
                ImGui::SetNextItemWidth(90*scale); ImGui::InputInt("Input (0-based)",&audioCaptureChannel_,0);
            }
            if (d.audioDirections & E51P_AUDIO_PLAYBACK) {
                ImGui::SetNextItemWidth(90*scale); ImGui::InputInt("Output (-1 = all)",&audioPlaybackChannel_,0);
            }
        }
        if (ImGui::Button("Open")) {
            if (peripherals_->openAudio(audioCaptureDevice_,audioPlaybackDevice_,audioCaptureChannel_,audioPlaybackChannel_,liveAudioMessage_))
                liveAudioMessage_ = "Opened; press Run to start audio";
        }
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(!peripherals_->audioOpen());
        if (ImGui::Button("Close")) { peripherals_->closeAudio(); liveAudioMessage_.clear(); }
        ImGui::EndDisabled();
        if (!liveAudioMessage_.empty()) ImGui::TextWrapped("%s",liveAudioMessage_.c_str());
        if (ImGui::CollapsingHeader("Buffer")) {
            ImGui::SetNextItemWidth(-50*scale); ImGui::InputInt("ms",&audioBufferMs_,32,128);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("10..2000 ms; 0 = automatic. Saved with the project.");
            ImGui::BeginDisabled(!emu51ValidAudioBufferMs(audioBufferMs_) || audioBufferMs_ == d.audioBufferMs);
            if (ImGui::Button("Apply buffer")) {
                (void)applyPeripheralAudioBuffer(audioBufferMs_); liveAudioMessage_ = peripheralMessage_;
            }
            ImGui::EndDisabled();
            ImGui::Text("Current: %.1f ms",peripherals_->audioLatencyMs());
        }
        if (ImGui::CollapsingHeader("Diagnostics")) {
            e51p_audio_stats stats{};
            if (peripherals_->audioStats(stats)) {
                ImGui::Text("Frames in / out: %llu / %llu",static_cast<unsigned long long>(stats.capture_used),static_cast<unsigned long long>(stats.playback_used));
                ImGui::Text("Misses in / out: %llu / %llu",static_cast<unsigned long long>(stats.capture_misses),static_cast<unsigned long long>(stats.playback_misses));
                ImGui::Text("Dropped in / out: %llu / %llu",static_cast<unsigned long long>(stats.capture_dropped),static_cast<unsigned long long>(stats.playback_dropped));
            }
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
void Emu51ImguiApp::drawPeripheralLive(const Emu51PeripheralConfig& active)
{
    std::vector<Emu51PeripheralLive::Device> devices;
    const auto names = peripheralEditor_.positions(active);
    const auto title = [&](const std::string& id) {
        const auto found = std::find_if(names.begin(),names.end(),[&](const auto& p) { return p.id == id; });
        return found != names.end() && !found->name.empty() ? found->name : id;
    };
    for (const auto& d : active.devices) {
        if (!Emu51PeripheralLive::visible(d.kind)) continue;
        ImVec2 size{180,64}, minimum{160,64};
        if (d.kind == Emu51PeripheralKind::Digital) { size = {140,64}; minimum = {120,64}; }
        else if (d.kind == Emu51PeripheralKind::Led) { size = {96,64}; minimum = {80,64}; }
        else if (d.kind == Emu51PeripheralKind::Switch) { size = {140,64}; minimum = {100,64}; }
        else if (d.kind == Emu51PeripheralKind::Button) { size = {140,72}; minimum = {100,72}; }
        else if (d.kind == Emu51PeripheralKind::Audio) { size = {420,320}; minimum = {360,240}; }
        else if (d.kind == Emu51PeripheralKind::Lcd) {
            size = {std::max(120.0f,static_cast<float>(d.columns*14+24)),static_cast<float>(d.rows*23+48)};
            minimum = {100,60};
        } else if (d.kind == Emu51PeripheralKind::Keypad || d.kind == Emu51PeripheralKind::KeyMatrix) {
            const auto columns = d.keyLayoutColumns ? d.keyLayoutColumns : d.kind == Emu51PeripheralKind::Keypad ? 4 : d.columns;
            const auto rows = d.keyLayoutRows ? d.keyLayoutRows : (emu51KeyCount(d)+columns-1)/columns;
            const bool legends = std::any_of(d.keyLegends.begin(),d.keyLegends.end(),[](const auto& s) { return !s.empty(); });
            size = d.kind == Emu51PeripheralKind::Keypad && d.keyLayout.empty() && !legends ? ImVec2{196,226} :
                ImVec2{std::max(196.0f,columns*64.0f+20),rows*(legends ? 58.0f : 38.0f)+86};
            minimum = {120,140};
        }
        else if (emu51PeripheralDisplayKind(d.kind) >= 0) {
            size = {std::max(160.0f,d.displayDigits*58.0f+32),150}; minimum = {100,80};
            if (d.kind == Emu51PeripheralKind::Ht16k33 || d.kind == Emu51PeripheralKind::Tm1637) size.y = 180;
            if (d.kind == Emu51PeripheralKind::Bargraph) { size = {std::max(160.0f,d.barSegments*18.0f+24),100}; minimum = {140,90}; }
        }
        devices.push_back({d.id,title(d.id),emu51PeripheralKindName(d.kind),size,minimum});
    }
    std::string heldButton;
    liveKeypadsThisFrame_.clear();
    peripheralLive_.draw(devices,[&](const std::string& id,float scale) {
        const auto found = std::find_if(active.devices.begin(),active.devices.end(),[&](const auto& d) { return d.id == id; });
        if (found == active.devices.end() || !peripherals_) return;
        if (found->kind == Emu51PeripheralKind::Button) {
            ImGui::Button("Hold",ImGui::GetContentRegionAvail());
            if (ImGui::IsItemActive()) heldButton = id;
            (void)peripherals_->setPressed(id,ImGui::IsItemActive());
        } else drawPeripheralLiveDevice(*found,scale);
    });
    for (const auto& id : liveHeldKeypads_) if (!liveKeypadsThisFrame_.contains(id) && peripherals_) {
        (void)peripherals_->setKeys(id,0); (void)peripherals_->setDisplayKeys(id,0);
    }
    liveHeldKeypads_ = liveKeypadsThisFrame_;
    if (peripherals_) for (const auto& d : active.devices)
        if (d.kind == Emu51PeripheralKind::Button && d.id != heldButton) (void)peripherals_->setPressed(d.id,false);
}
