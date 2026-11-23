// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_peripheral_live.h"
#include "imgui_internal.h"
#include <algorithm>

bool Emu51PeripheralLive::momentaryKeys(const std::string& id) const
{
    const auto found = std::find_if(boxes_.begin(),boxes_.end(),[&](const auto& box) { return box.id == id; });
    return found != boxes_.end() && found->momentaryKeys;
}
void Emu51PeripheralLive::setMomentaryKeys(const std::string& id,bool momentary)
{
    const auto found = std::find_if(boxes_.begin(),boxes_.end(),[&](const auto& box) { return box.id == id; });
    if (found != boxes_.end()) found->momentaryKeys = momentary;
}
bool Emu51PeripheralLive::visible(Emu51PeripheralKind kind)
{
    return emu51PeripheralDisplayKind(kind) >= 0 || kind == Emu51PeripheralKind::Led ||
        kind == Emu51PeripheralKind::Switch || kind == Emu51PeripheralKind::Button ||
        kind == Emu51PeripheralKind::Lcd || kind == Emu51PeripheralKind::Keypad ||
        kind == Emu51PeripheralKind::Audio || kind == Emu51PeripheralKind::Voltage || kind == Emu51PeripheralKind::Digital;
}
std::vector<Emu51PeripheralLiveBox> Emu51PeripheralLive::layout(const Emu51PeripheralConfig& config) const
{
    std::vector<Emu51PeripheralLiveBox> result;
    for (const auto& box : boxes_) if (
        std::any_of(config.devices.begin(),config.devices.end(),[&](const auto& d) { return d.id == box.id && visible(d.kind); })) result.push_back(box);
    return result;
}
void Emu51PeripheralLive::arrange(float width)
{
    double x = 12, y = 12, row = 0;
    for (auto& box : boxes_) {
        if (x > 12 && x+box.width+12 > width) { x = 12; y += row+10; row = 0; }
        box.x = x; box.y = y; x += box.width+10; row = std::max(row,box.height);
    }
}
void Emu51PeripheralLive::draw(const std::vector<Device>& devices, const std::function<void(const std::string&,float)>& content)
{
    interacting_ = false;
    ImGui::Checkbox("Edit layout",&editing_);
    if (editing_) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Arrange")) arrange(ImGui::GetWindowContentRegionMax().x);
        ImGui::SameLine();
        if (ImGui::SmallButton("Compact")) {
            for (auto& box : boxes_) for (const auto& device : devices) if (box.id == device.id) {
                box.width = device.size.x; box.height = device.size.y;
            }
            arrange(ImGui::GetWindowContentRegionMax().x);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Use compact device sizes and arrange the front panel.");
    }
    std::erase_if(boxes_,[&](const auto& box) {
        return std::none_of(devices.begin(),devices.end(),[&](const auto& d) { return d.id == box.id; });
    });
    if (devices.empty()) { ImGui::TextDisabled("No front-panel devices in this circuit."); return; }
    const bool initial = boxes_.empty();
    for (const auto& device : devices) {
        auto found = std::find_if(boxes_.begin(),boxes_.end(),[&](const auto& box) { return box.id == device.id; });
        if (found == boxes_.end()) {
            double bottom = 12;
            for (const auto& box : boxes_) bottom = std::max(bottom,box.y+box.height+16);
            boxes_.push_back({device.id,12,bottom,device.size.x,device.size.y});
        }
    }
    if (initial) arrange(ImGui::GetContentRegionAvail().x);
    ImGui::BeginChild("live-canvas",{0,0},ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar);
    const auto origin = ImGui::GetCursorScreenPos();
    const float header = ImGui::GetTextLineHeight()+8;
    ImVec2 extent{0,0};
    std::string raise;
    for (auto& box : boxes_) {
        const auto& device = *std::find_if(devices.begin(),devices.end(),[&](const auto& d) { return d.id == box.id; });
        box.width = std::max(box.width,static_cast<double>(device.minimum.x));
        box.height = std::max(box.height,static_cast<double>(device.minimum.y));
        const ImVec2 at{origin.x+static_cast<float>(box.x),origin.y+static_cast<float>(box.y)};
        ImGui::SetCursorScreenPos(at); ImGui::PushID(box.id.c_str());
        ImGui::PushStyleColor(ImGuiCol_ChildBg,ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{4,4});
        const bool visible = ImGui::BeginChild("box",{static_cast<float>(box.width),static_cast<float>(box.height)},ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (visible) {
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(at,{at.x+static_cast<float>(box.width),at.y+header},ImGui::GetColorU32(ImGuiCol_Header),4);
            const auto titleAt = ImVec2(at.x+6,at.y+4);
            draw->PushClipRect(at,{at.x+static_cast<float>(box.width)-8,at.y+header},true);
            draw->AddText(titleAt,ImGui::GetColorU32(ImGuiCol_Text),device.title.c_str());
            draw->PopClipRect();
            ImGui::SetCursorScreenPos(at);
            ImGui::InvisibleButton("move",{static_cast<float>(box.width),header});
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",device.type.c_str());
            if (ImGui::IsItemActivated()) raise = box.id;
            if (editing_) {
                if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                if (ImGui::IsItemActive()) {
                    interacting_ = true;
                    const auto delta = ImGui::GetIO().MouseDelta;
                    box.x = std::clamp(box.x+delta.x,0.0,100000.0);
                    box.y = std::clamp(box.y+delta.y,0.0,100000.0);
                }
            }
            ImGui::SetCursorScreenPos({at.x+6,at.y+header+6});
            if (ImGui::BeginChild("controls",{static_cast<float>(box.width)-12,static_cast<float>(box.height)-header-12},
                ImGuiChildFlags_None)) {
                const float scale = std::clamp(std::min((static_cast<float>(box.width)-12)/std::max(1.0f,device.size.x-12),
                    (static_cast<float>(box.height)-header-12)/std::max(1.0f,device.size.y-header-12)),.35f,16.0f);
                ImGui::PushFont(nullptr,ImGui::GetFontSize()*scale);
                const auto style = ImGui::GetStyle();
                const auto scaled = [scale](ImVec2 v) { return ImVec2(v.x*scale,v.y*scale); };
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,scaled(style.FramePadding));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,scaled(style.ItemSpacing));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,scaled(style.ItemInnerSpacing));
                ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize,style.GrabMinSize*scale);
                ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing,style.IndentSpacing*scale);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,style.FrameRounding*scale);
                ImGui::PushTextWrapPos(0);
                ImGui::BeginDisabled(editing_); content(box.id,scale); ImGui::EndDisabled();
                ImGui::PopTextWrapPos();
                ImGui::PopStyleVar(6);
                ImGui::PopFont();
            }
            ImGui::EndChild();
            if (editing_) {
                const ImVec2 grip{at.x+static_cast<float>(box.width)-18,at.y+static_cast<float>(box.height)-18};
                ImGui::SetCursorScreenPos(grip);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
                // A small overlay keeps the resize handle above the controls child.
                if (ImGui::BeginChild("grip",{18,18},ImGuiChildFlags_None,ImGuiWindowFlags_NoBackground |
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                    ImGui::InvisibleButton("resize",{18,18});
                    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                    if (ImGui::IsItemActive()) {
                        interacting_ = true;
                        const auto delta = ImGui::GetIO().MouseDelta;
                        box.width = std::clamp(box.width+delta.x,static_cast<double>(device.minimum.x),4096.0);
                        box.height = std::clamp(box.height+delta.y,static_cast<double>(device.minimum.y),4096.0);
                    }
                    ImGui::GetWindowDrawList()->AddLine({grip.x+4,grip.y+15},{grip.x+15,grip.y+4},ImGui::GetColorU32(ImGuiCol_TextDisabled),2);
                }
                ImGui::EndChild(); ImGui::PopStyleVar();
            }
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) raise = box.id;
        }
        ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor(); ImGui::PopID();
        extent.x = std::max(extent.x,static_cast<float>(box.x+box.width+16));
        extent.y = std::max(extent.y,static_cast<float>(box.y+box.height+16));
    }
    ImGui::SetCursorScreenPos(origin); ImGui::Dummy(extent);
    ImGui::EndChild();
    // Keep the selected box above overlapping neighbours on subsequent frames.
    if (!raise.empty()) {
        const auto found = std::find_if(boxes_.begin(),boxes_.end(),[&](const auto& box) { return box.id == raise; });
        if (found != boxes_.end()) std::rotate(found,found+1,boxes_.end());
    }
}
