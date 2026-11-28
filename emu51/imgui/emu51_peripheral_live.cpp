// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_peripheral_live.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cstdio>

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
    return emu51PeripheralDisplayKind(kind) >= 0 || kind == Emu51PeripheralKind::Ssd1306 ||
        kind == Emu51PeripheralKind::Stepper || kind == Emu51PeripheralKind::Ds1307 || kind == Emu51PeripheralKind::LogicAnalyzer || kind == Emu51PeripheralKind::Led ||
        kind == Emu51PeripheralKind::Switch || kind == Emu51PeripheralKind::Button ||
        kind == Emu51PeripheralKind::Lcd || kind == Emu51PeripheralKind::Keypad || kind == Emu51PeripheralKind::KeyMatrix ||
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
void Emu51PeripheralLive::restore(const std::vector<Emu51PeripheralLiveBox>& boxes,
    const std::vector<Emu51PeripheralLiveLabel>& labels)
{
    boxes_ = boxes;
    labels_ = labels;
    labelTarget_ = -1;
    editing_ = false;
    suspend();
}

void Emu51PeripheralLive::drawLabels(ImVec2 origin,ImVec2& extent)
{
    const auto mouse = ImGui::GetMousePos();
    const ImVec2 padding{6,4};
    int hit = -1;
    auto* draw = ImGui::GetWindowDrawList();
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        auto& label = labels_[i];
        const ImVec2 at{origin.x+static_cast<float>(label.x),origin.y+static_cast<float>(label.y)};
        const auto text = ImGui::CalcTextSize(label.text.c_str());
        const ImVec2 size{text.x+2*padding.x,text.y+2*padding.y};
        const ImVec2 end{at.x+size.x,at.y+size.y};
        if (editing_) {
            if (ImRect(at,end).Contains(mouse)) hit = static_cast<int>(i);
            ImGui::SetCursorScreenPos(at);
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("label",size);
            const bool hovered = ImGui::IsItemHovered();
            if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (ImGui::IsItemActive()) {
                interacting_ = true;
                const auto delta = ImGui::GetIO().MouseDelta;
                label.x = std::clamp(label.x+delta.x,0.0,100000.0);
                label.y = std::clamp(label.y+delta.y,0.0,100000.0);
            }
            ImGui::PopID();
            draw->AddRectFilled(at,end,ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg),3);
        }
        draw->AddText({at.x+padding.x,at.y+padding.y},ImGui::GetColorU32(ImGuiCol_Text),label.text.c_str());
        extent.x = std::max(extent.x,static_cast<float>(label.x)+size.x+16);
        extent.y = std::max(extent.y,static_cast<float>(label.y)+size.y+16);
    }

    if (!editing_) return;
    if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        labelPosition_ = {std::clamp(mouse.x-origin.x,0.0f,100000.0f),std::clamp(mouse.y-origin.y,0.0f,100000.0f)};
        labelTarget_ = hit;
        ImGui::OpenPopup("live-label-menu");
    }
    const bool existing = labelTarget_ >= 0 && static_cast<std::size_t>(labelTarget_) < labels_.size();
    bool enterText = false;
    if (ImGui::BeginPopup("live-label-menu")) {
        interacting_ = true;
        if (existing) {
            enterText = ImGui::MenuItem("Edit label");
            if (ImGui::MenuItem("Remove label")) {
                labels_.erase(labels_.begin()+labelTarget_);
                labelTarget_ = -1;
            }
        } else {
            enterText = ImGui::MenuItem("Add label",nullptr,false,labels_.size() < Emu51PeripheralLiveLabel::maxCount);
        }
        ImGui::EndPopup();
    }
    if (enterText) {
        const auto text = existing ? labels_[labelTarget_].text.c_str() : "";
        std::snprintf(labelText_,sizeof(labelText_),"%s",text);
        ImGui::OpenPopup("live-label-text");
    }
    if (ImGui::BeginPopup("live-label-text")) {
        interacting_ = true;
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputTextWithHint("##text","Label text",labelText_,sizeof(labelText_),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        Emu51PeripheralLiveLabel label{labelText_,labelPosition_.x,labelPosition_.y};
        const bool valid = emu51ValidLiveLabel(label);
        ImGui::BeginDisabled(!valid);
        const bool confirm = ImGui::Button(existing ? "Apply" : "Add");
        ImGui::EndDisabled();
        if (valid && (enter || confirm)) {
            if (existing) labels_[labelTarget_].text = std::move(label.text);
            else labels_.push_back(std::move(label));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
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
    if (devices.empty() && labels_.empty() && !editing_) { ImGui::TextDisabled("No front-panel devices in this circuit."); return; }
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
    drawLabels(origin,extent);
    ImGui::SetCursorScreenPos(origin); ImGui::Dummy(extent);
    ImGui::EndChild();
    // Keep the selected box above overlapping neighbours on subsequent frames.
    if (!raise.empty()) {
        const auto found = std::find_if(boxes_.begin(),boxes_.end(),[&](const auto& box) { return box.id == raise; });
        if (found != boxes_.end()) std::rotate(found,found+1,boxes_.end());
    }
}
