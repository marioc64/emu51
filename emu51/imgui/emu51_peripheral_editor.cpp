// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_peripheral_editor.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr float blockWidth = 320, headerHeight = 58, rowHeight = 52;
constexpr ImU32 digitalColor = IM_COL32(94, 198, 241, 255);
constexpr ImU32 analogColor = IM_COL32(245, 188, 91, 255);
constexpr ImU32 busColor = IM_COL32(185, 151, 246, 255);
ImVec2 plus(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
ImVec2 minus(ImVec2 a, ImVec2 b) { return {a.x - b.x, a.y - b.y}; }
ImVec2 scale(ImVec2 a, float s) { return {a.x * s, a.y * s}; }
bool contains(ImVec2 p, ImVec2 lo, ImVec2 hi) { return p.x >= lo.x && p.y >= lo.y && p.x <= hi.x && p.y <= hi.y; }
float distanceSquared(ImVec2 a, ImVec2 b) { const auto d = minus(a, b); return d.x*d.x + d.y*d.y; }
float segmentDistance(ImVec2 p, ImVec2 a, ImVec2 b)
{
    const auto d = minus(b, a), q = minus(p, a);
    const float length = d.x*d.x + d.y*d.y;
    const float t = length > 0 ? std::clamp((q.x*d.x + q.y*d.y) / length, 0.0f, 1.0f) : 0;
    return std::sqrt(distanceSquared(p, plus(a, scale(d, t))));
}
ImVec2 bezier(ImVec2 a, ImVec2 c, ImVec2 d, ImVec2 b, float t)
{
    const float u = 1-t;
    return plus(plus(scale(a,u*u*u), scale(c,3*u*u*t)), plus(scale(d,3*u*t*t),scale(b,t*t*t)));
}
ImU32 color(unsigned kind) { return kind == E51P_DIGITAL ? digitalColor : kind == E51P_ANALOG ? analogColor : busColor; }
const char* direction(const Emu51PeripheralPin& p)
{
    if (p.kind == E51P_BUS_INITIATOR || p.kind == E51P_BUS_TARGET) return "BUS";
    return p.direction == (E51P_INPUT|E51P_OUTPUT) ? "I/O" : p.direction & E51P_OUTPUT ? "OUT" : "IN";
}
std::string range(const std::string& name, int bit, int width)
{
    return name + "[" + (width > 1 ? std::to_string(bit+width-1) + ":" : "") + std::to_string(bit) + "]";
}
}

void Emu51PeripheralEditor::restore(const std::vector<Emu51PeripheralBlockPosition>& positions)
{
    positions_ = positions;
    selected_.clear(); wire_ = {}; draggingBlock_.clear(); draggingPin_ = {};
    fitBlock_.clear(); rightGesture_ = rightPanned_ = false;
    pan_ = {24,24}; fit_ = true;
}
std::vector<Emu51PeripheralBlockPosition> Emu51PeripheralEditor::positions(const Emu51PeripheralConfig& c) const
{
    auto result = positions_;
    std::erase_if(result, [&](const auto& p) {
        return p.id != "cpu" && std::none_of(c.devices.begin(), c.devices.end(), [&](const auto& d) { return d.id == p.id; });
    });
    return result;
}
void Emu51PeripheralEditor::rebuild(const Emu51PeripheralConfig& config, Emu51Model model)
{
    blocks_.clear();
    const auto descriptors = Emu51PeripheralSession::describe(config, true);
    const auto add = [&](const std::string& id, const std::string& title, const std::string& subtitle) {
        auto found = std::find_if(positions_.begin(), positions_.end(), [&](const auto& p) { return p.id == id; });
        if (found == positions_.end()) {
            const auto i = blocks_.size();
            positions_.push_back({id, static_cast<double>(i % 3) * 400, static_cast<double>(i / 3) * 420 + (i % 3) * 28});
            found = positions_.end()-1;
        }
        Block b; b.id = id; b.title = found->name.empty() ? title : found->name; b.subtitle = subtitle;
        b.position = {static_cast<float>(found->x), static_cast<float>(found->y)};
        float left = 0, right = 0;
        for (const auto& d : descriptors) {
            if (!d.name.starts_with(id + ".")) continue;
            Pin p; p.descriptor = d;
            p.left = d.kind == E51P_BUS_TARGET || (d.kind != E51P_BUS_INITIATOR && !(d.direction & E51P_OUTPUT));
            auto& column = p.left ? left : right;
            const float row=column;
            const unsigned bitRows=d.kind==E51P_DIGITAL && d.width>1 ? (d.width+7)/8 : 1;
            column+=rowHeight+18*(bitRows-1);
            p.position = plus(b.position, {p.left ? 0 : blockWidth, headerHeight + row + 14});
            if (d.kind == E51P_DIGITAL && d.width > 1)
                for (unsigned bit = 0; bit < d.width; ++bit)
                    p.bits.push_back(plus(b.position, {(p.left ? 19.0f : 198.0f) + 14*(bit%8), headerHeight + row + 35 + 18*(bit/8)}));
            b.pins.push_back(std::move(p));
        }
        b.size = {blockWidth, headerHeight + std::max(left,right) + 10};
        blocks_.push_back(std::move(b));
    };
    add("cpu", std::string("Intel ") + emu51ModelInfo(model).id,
        std::string("CPU ") + emu51ModelInfo(model).id + "  /  P0-P3  /  XDATA");
    for (const auto& d : config.devices) {
        if (const int size = emu51PeripheralBusSize(d.kind)) {
            char subtitle[96]; std::snprintf(subtitle,sizeof(subtitle),"%s / XDATA %04X-%04X",emu51PeripheralKindName(d.kind),d.address,d.address+size-1);
            add(d.id,d.id,subtitle);
        } else add(d.id,d.id,emu51PeripheralKindName(d.kind));
    }
    std::erase_if(positions_, [&](const auto& p) { return std::none_of(blocks_.begin(), blocks_.end(), [&](const auto& b) { return b.id == p.id; }); });
    // Selected blocks stay above intersecting blocks and their connectors.
    std::stable_sort(blocks_.begin(), blocks_.end(), [&](const auto& a, const auto& b) { return (a.id == selected_) < (b.id == selected_); });
}
const Emu51PeripheralEditor::Pin* Emu51PeripheralEditor::pin(const std::string& name) const
{
    for (const auto& b : blocks_) for (const auto& p : b.pins) if (p.descriptor.name == name) return &p;
    return nullptr;
}
ImVec2 Emu51PeripheralEditor::screen(ImVec2 p) const { return plus(origin_, plus(pan_, scale(p,zoom_))); }
ImVec2 Emu51PeripheralEditor::world(ImVec2 p) const { return scale(minus(minus(p,origin_),pan_),1/zoom_); }
ImVec2 Emu51PeripheralEditor::anchor(const Pin& p, int bit) const
{
    return screen(bit >= 0 && bit < static_cast<int>(p.bits.size()) ? p.bits[bit] : p.position);
}
void Emu51PeripheralEditor::fitView()
{
    if (blocks_.empty()) return;
    ImVec2 lo = blocks_[0].position, hi = plus(lo, blocks_[0].size);
    const auto focus = std::find_if(blocks_.begin(),blocks_.end(),[&](const auto& b) { return b.id == fitBlock_; });
    if (focus != blocks_.end()) { lo = focus->position; hi = plus(lo,focus->size); }
    else for (const auto& b : blocks_) {
        lo.x = std::min(lo.x,b.position.x); lo.y = std::min(lo.y,b.position.y);
        hi.x = std::max(hi.x,b.position.x+b.size.x); hi.y = std::max(hi.y,b.position.y+b.size.y);
    }
    zoom_ = std::clamp(std::min((canvasSize_.x-64)/(hi.x-lo.x), (canvasSize_.y-64)/(hi.y-lo.y)),0.25f,1.25f);
    pan_ = { (canvasSize_.x-(hi.x-lo.x)*zoom_)/2-lo.x*zoom_, (canvasSize_.y-(hi.y-lo.y)*zoom_)/2-lo.y*zoom_ };
    fit_ = false;
    fitBlock_.clear();
}
bool Emu51PeripheralEditor::connect(Emu51PeripheralConfig& config, const Connector& a, const Connector& b, std::string& message)
{
    const auto* p = pin(a.name); const auto* q = pin(b.name);
    if (!p || !q || a.name == b.name) { message = "Choose two different connectors."; return false; }
    if ((p->descriptor.kind == E51P_BUS_INITIATOR && q->descriptor.kind == E51P_BUS_TARGET) ||
        (q->descriptor.kind == E51P_BUS_INITIATOR && p->descriptor.kind == E51P_BUS_TARGET)) {
        const auto& target = p->descriptor.kind == E51P_BUS_TARGET ? a.name : b.name;
        const auto id = target.substr(0,target.find('.'));
        selected_ = id; wire_ = {};
        message = "Device buses map to CPU XDATA automatically. Set the base address in the block inspector.";
        return true;
    }
    if (p->descriptor.kind != q->descriptor.kind || (p->descriptor.kind != E51P_DIGITAL && p->descriptor.kind != E51P_ANALOG)) {
        message = "Connect digital to digital, analog output to analog input, or XDATA to an 8255 bus."; return false;
    }
    if (p->descriptor.kind == E51P_ANALOG) {
        const bool forward = (p->descriptor.direction & E51P_OUTPUT) && (q->descriptor.direction & E51P_INPUT);
        const bool reverse = (q->descriptor.direction & E51P_OUTPUT) && (p->descriptor.direction & E51P_INPUT);
        if (!forward && !reverse) { message = "An analog wire needs one output and one input."; return false; }
        const auto& from = forward ? a.name : b.name;
        const auto& to = forward ? b.name : a.name;
        if (std::any_of(config.analog.begin(),config.analog.end(),[&](const auto& w) { return w.to == to; })) {
            message = "This analog input is already connected. Select its wire and remove it first."; return false;
        }
        if (config.analog.size() >= 64) { message = "The graph supports 64 analog wires."; return false; }
        config.analog.push_back({from,to}); wire_ = {2,static_cast<int>(config.analog.size()-1)};
    } else {
        if (!((p->descriptor.direction|q->descriptor.direction) & E51P_OUTPUT)) {
            message = "Two input-only pins cannot drive a digital net."; return false;
        }
        const int ab = std::max(0,a.bit), bb = std::max(0,b.bit);
        const int width = a.bit >= 0 || b.bit >= 0 ? 1 : static_cast<int>(std::min(p->descriptor.width,q->descriptor.width));
        if (ab + width > static_cast<int>(p->descriptor.width) || bb + width > static_cast<int>(q->descriptor.width)) {
            message = "The selected bits are outside the port."; return false;
        }
        if (std::any_of(config.digital.begin(),config.digital.end(),[&](const auto& w) {
            return w.width == width && ((w.a == a.name && w.b == b.name && w.aBit == ab && w.bBit == bb) ||
                (w.b == a.name && w.a == b.name && w.bBit == ab && w.aBit == bb));
        })) { message = "These digital connectors are already wired."; return false; }
        if (config.digital.size() >= 128) { message = "The graph supports 128 digital wires."; return false; }
        config.digital.push_back({a.name,b.name,ab,bb,width}); wire_ = {1,static_cast<int>(config.digital.size()-1)};
    }
    selected_.clear(); message = "Draft wire added."; return true;
}
void Emu51PeripheralEditor::removeDevice(Emu51PeripheralConfig& config, const std::string& id)
{
    if (id == "cpu") return;
    const auto prefix = id + ".";
    std::erase_if(config.digital,[&](const auto& w) { return w.a.starts_with(prefix) || w.b.starts_with(prefix); });
    std::erase_if(config.analog,[&](const auto& w) { return w.from.starts_with(prefix) || w.to.starts_with(prefix); });
    std::erase_if(config.devices,[&](const auto& d) { return d.id == id; });
    std::erase_if(positions_,[&](const auto& p) { return p.id == id; });
    selected_.clear(); wire_ = {};
}
bool Emu51PeripheralEditor::rename(const std::string& id, const std::string& name)
{
    if (name.size() > 128 || std::any_of(name.begin(),name.end(),[](unsigned char ch) { return ch < 32; })) return false;
    const auto found = std::find_if(positions_.begin(),positions_.end(),[&](const auto& p) { return p.id == id; });
    if (found == positions_.end()) return false;
    found->name = name;
    return true;
}

void Emu51PeripheralEditor::draw(Emu51PeripheralConfig& config, Emu51Model model, std::string& message)
{
    interacting_ = false;
    rebuild(config,model);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const bool showInspector = inspectorVisible_;
    const bool sideBySide = available.x >= 600;
    const float canvasWidth = showInspector && sideBySide ? available.x-268 : available.x;
    const float canvasHeight = showInspector && !sideBySide ? std::max(80.0f,available.y*0.6f-8) : std::max(80.0f,available.y);
    ImGui::BeginChild("circuit-area",ImVec2(std::max(80.0f,canvasWidth),canvasHeight),ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    canvas(config,message);
    ImGui::EndChild();
    if (showInspector) {
        if (sideBySide) ImGui::SameLine();
        ImGui::BeginChild("circuit-inspector",ImVec2(0,sideBySide ? canvasHeight : std::max(80.0f,ImGui::GetContentRegionAvail().y)),ImGuiChildFlags_Borders);
        inspector(config);
        ImGui::EndChild();
    }
}

void Emu51PeripheralEditor::canvas(Emu51PeripheralConfig& config, std::string& message)
{
    auto& io = ImGui::GetIO();
    origin_ = ImGui::GetCursorScreenPos(); canvasSize_ = ImGui::GetContentRegionAvail();
    canvasSize_.x = std::max(1.0f,canvasSize_.x); canvasSize_.y = std::max(1.0f,canvasSize_.y);
    if (fit_) fitView();
    ImGui::InvisibleButton("##circuit",canvasSize_,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle|ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        rightGesture_ = true; rightPanned_ = false;
    }
    if (hovered && io.MouseWheel != 0) {
        const auto at = world(io.MousePos);
        zoom_ = std::clamp(zoom_*std::pow(1.15f,io.MouseWheel),0.25f,2.0f);
        pan_ = minus(minus(io.MousePos,origin_),scale(at,zoom_)); interacting_ = true;
    }
    if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle,0) || ImGui::IsMouseDragging(ImGuiMouseButton_Right,3))) {
        pan_ = plus(pan_,io.MouseDelta); interacting_ = true;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) rightPanned_ = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { draggingBlock_.clear(); draggingPin_ = {}; }
    if (!draggingBlock_.empty() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const auto p = minus(world(io.MousePos),blockDragOffset_);
        for (auto& saved : positions_) if (saved.id == draggingBlock_) {
            const auto clamped = ImVec2(std::clamp(p.x,-100000.0f,100000.0f),std::clamp(p.y,-100000.0f,100000.0f));
            const auto old = std::find_if(blocks_.begin(),blocks_.end(),[&](const auto& b) { return b.id == draggingBlock_; });
            if (old != blocks_.end()) {
                const auto delta = minus(clamped,old->position); old->position = clamped;
                for (auto& pin : old->pins) { pin.position = plus(pin.position,delta); for (auto& bit : pin.bits) bit = plus(bit,delta); }
            }
            saved.x = clamped.x; saved.y = clamped.y;
        }
        interacting_ = true;
    }
    auto* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin_,plus(origin_,canvasSize_),true);
    dl->AddRectFilled(origin_,plus(origin_,canvasSize_),IM_COL32(21,26,35,255));
    const float grid = 32*zoom_;
    for (float x = std::fmod(pan_.x,grid); x < canvasSize_.x; x+=grid)
        dl->AddLine(plus(origin_,{x,0}),plus(origin_,{x,canvasSize_.y}),IM_COL32(36,43,54,255));
    for (float y = std::fmod(pan_.y,grid); y < canvasSize_.y; y+=grid)
        dl->AddLine(plus(origin_,{0,y}),plus(origin_,{canvasSize_.x,y}),IM_COL32(36,43,54,255));
    Connector hoverPin; std::string hoverBlock; bool header = false;
    if (hovered) for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
        const auto& b = *it;
        for (const auto& p : b.pins) {
            const float radius = std::max(6.0f,7*zoom_);
            if (distanceSquared(io.MousePos,anchor(p)) <= radius*radius) hoverPin = {p.descriptor.name,-1};
            for (std::size_t bit = 0; bit < p.bits.size(); ++bit)
                if (distanceSquared(io.MousePos,screen(p.bits[bit])) <= radius*radius) hoverPin = {p.descriptor.name,static_cast<int>(bit)};
        }
        if (!hoverPin.name.empty() || contains(io.MousePos,screen(b.position),screen(plus(b.position,b.size)))) {
            hoverBlock = b.id; header = io.MousePos.y < screen(plus(b.position,{0,headerHeight})).y; break;
        }
    }
    WireSelection hoverWire;
    float closest = 7;
    const auto text = [&](ImVec2 p, const std::string& s, ImU32 c, float size = 13.0f) {
        dl->AddText(ImGui::GetFont(),size*zoom_,screen(p),c,s.c_str());
    };
    const auto link = [&](const Pin* a, int ab, const Pin* b, int bb, ImU32 c, const std::string& label, WireSelection selection) {
        if (!a || !b) return;
        const auto start = anchor(*a,ab), end = anchor(*b,bb);
        // Bound backward loops so Fit all keeps return paths inside the canvas.
        const float bend = std::clamp(std::abs(end.x-start.x)*0.45f,50.0f*zoom_,100.0f*zoom_);
        const auto c1 = plus(start,{a->left ? -bend : bend,0}), c2 = plus(end,{b->left ? -bend : bend,0});
        const bool selected = wire_.kind == selection.kind && wire_.index == selection.index;
        dl->AddBezierCubic(start,c1,c2,end,selected ? IM_COL32(250,250,255,255) : c,selected ? 3.5f : 2.0f);
        if (selection.kind != 1) {
            const auto tip = bezier(start,c1,c2,end,0.73f), behind = bezier(start,c1,c2,end,0.70f);
            const auto v = minus(tip,behind);
            const float length = std::sqrt(v.x*v.x+v.y*v.y);
            if (length > 0) {
                const auto unit = scale(v,1/length), base = minus(tip,scale(unit,8*zoom_));
                const ImVec2 side{-unit.y*4*zoom_,unit.x*4*zoom_};
                dl->AddTriangleFilled(tip,plus(base,side),minus(base,side),c);
            }
        }
        if (hovered && hoverBlock.empty()) {
            auto previous = start;
            for (int i = 1; i <= 32; ++i) {
                const auto current = bezier(start,c1,c2,end,static_cast<float>(i)/32);
                const float dist = segmentDistance(io.MousePos,previous,current);
                if (dist < closest) { closest = dist; hoverWire = selection; }
                previous = current;
            }
        }
        const float fontSize = 11*zoom_;
        const auto extent = ImGui::GetFont()->CalcTextSizeA(fontSize,FLT_MAX,0,label.c_str());
        for (const float t : {0.5f,0.3f,0.7f}) {
            const auto mid = bezier(start,c1,c2,end,t);
            const auto lo = minus(mid,{extent.x/2+4,extent.y/2+3}), hi = plus(lo,{extent.x+8,extent.y+6});
            const bool overlaps = std::any_of(blocks_.begin(),blocks_.end(),[&](const auto& block) {
                const auto a = screen(block.position), b = screen(plus(block.position,block.size));
                return lo.x < b.x+4 && hi.x > a.x-4 && lo.y < b.y+4 && hi.y > a.y-4;
            });
            if (overlaps) continue;
            dl->AddRectFilled(lo,hi,IM_COL32(21,26,35,235),3);
            dl->AddText(ImGui::GetFont(),fontSize,plus(lo,{4,3}),c,label.c_str());
            break;
        }
    };
    for (std::size_t i = 0; i < config.digital.size(); ++i) {
        const auto& w = config.digital[i];
        const auto* a = pin(w.a); const auto* b = pin(w.b);
        const bool whole = a && b && w.aBit == 0 && w.bBit == 0 && w.width == static_cast<int>(a->descriptor.width) && w.width == static_cast<int>(b->descriptor.width);
        link(a,whole ? -1 : w.aBit,b,whole ? -1 : w.bBit,digitalColor,
            std::to_string(w.width) + (w.width == 1 ? " bit" : " bits"),{1,static_cast<int>(i)});
    }
    for (std::size_t i = 0; i < config.analog.size(); ++i) {
        const auto& w = config.analog[i]; link(pin(w.from),-1,pin(w.to),-1,analogColor,"analog",{2,static_cast<int>(i)});
    }
    for (std::size_t i = 0; i < config.devices.size(); ++i) {
        const auto& d = config.devices[i];
        const int size = emu51PeripheralBusSize(d.kind);
        if (!size) continue;
        char label[64]; std::snprintf(label,sizeof(label),"XDATA %04X-%04X",d.address,d.address+size-1);
        link(pin("cpu.xdata"),-1,pin(d.id+".bus"),-1,busColor,label,{3,static_cast<int>(i)});
    }
    for (const auto& b : blocks_) {
        const auto lo = screen(b.position), hi = screen(plus(b.position,b.size));
        dl->AddRectFilled(plus(lo,{3,4}),plus(hi,{3,4}),IM_COL32(0,0,0,65),6*zoom_);
        dl->AddRectFilled(lo,hi,IM_COL32(39,46,59,255),6*zoom_);
        dl->AddRectFilled(lo,screen(plus(b.position,{b.size.x,headerHeight})),b.id == "cpu" ? IM_COL32(43,65,87,255) : IM_COL32(52,64,83,255),6*zoom_);
        dl->AddRect(lo,hi,b.id == selected_ ? IM_COL32(239,243,255,255) : IM_COL32(83,102,126,255),6*zoom_,0,b.id == selected_ ? 2 : 1);
        // Keep long IDs inside the block title instead of covering neighbours.
        dl->PushClipRect(plus(lo,{8*zoom_,0}),minus(hi,{8*zoom_,0}),true);
        text(plus(b.position,{13,9}),b.title,IM_COL32(242,245,250,255),17);
        text(plus(b.position,{13,34}),b.subtitle,IM_COL32(179,194,213,255),12);
        dl->PopClipRect();
        for (const auto& p : b.pins) {
            const auto c = color(p.descriptor.kind);
            const auto shortName = p.descriptor.name.substr(p.descriptor.name.find('.')+1);
            const auto label = shortName + (p.descriptor.width > 1 && p.descriptor.kind == E51P_DIGITAL ? " ["+std::to_string(p.descriptor.width-1)+":0]" : "") + "  " + direction(p.descriptor);
            const auto extent = ImGui::GetFont()->CalcTextSizeA(12*zoom_,FLT_MAX,0,label.c_str());
            const auto at = plus(p.position,{p.left ? 14.0f : -14.0f-extent.x/zoom_,-7});
            text(at,label,c,12);
            if (p.descriptor.kind == E51P_ANALOG) dl->AddRectFilled(minus(anchor(p),{5*zoom_,5*zoom_}),plus(anchor(p),{5*zoom_,5*zoom_}),c,2);
            else dl->AddCircleFilled(anchor(p),5*zoom_,c);
            for (std::size_t bit = 0; bit < p.bits.size(); ++bit) {
                const auto center = screen(p.bits[bit]);
                dl->AddRectFilled(minus(center,{6*zoom_,7*zoom_}),plus(center,{6*zoom_,7*zoom_}),IM_COL32(27,35,46,255),2);
                dl->AddRect(minus(center,{6*zoom_,7*zoom_}),plus(center,{6*zoom_,7*zoom_}),c,2);
                text(plus(p.bits[bit],{bit<10 ? -3.0f : -5.0f,-6}),std::to_string(bit),c,bit<10 ? 10 : 9);
            }
        }
    }
    if (!hoverPin.name.empty()) {
        const auto* p = pin(hoverPin.name);
        dl->AddCircle(anchor(*p,hoverPin.bit),9*zoom_,IM_COL32(255,255,255,255),0,2);
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(hoverPin.name.c_str());
        ImGui::Text("%s | %s | %u bit%s",p->descriptor.kind == E51P_ANALOG ? "Analog" : p->descriptor.kind == E51P_DIGITAL ? "Digital" : "XDATA bus",direction(p->descriptor),p->descriptor.width,p->descriptor.width == 1 ? "" : "s");
        if (hoverPin.bit >= 0) ImGui::Text("Bit %d",hoverPin.bit);
        ImGui::EndTooltip(); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    } else if (header && hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (!hoverPin.name.empty()) draggingPin_ = hoverPin;
        else if (!hoverBlock.empty()) {
            selected_ = hoverBlock; wire_ = {};
            if (header) {
                draggingBlock_ = hoverBlock;
                const auto b = std::find_if(blocks_.begin(),blocks_.end(),[&](const auto& block) { return block.id == hoverBlock; });
                blockDragOffset_ = minus(world(io.MousePos),b->position);
            }
        } else { wire_ = hoverWire; selected_.clear(); }
    }
    if (!draggingPin_.name.empty()) {
        if (const auto* p = pin(draggingPin_.name)) {
            const auto start = anchor(*p,draggingPin_.bit);
            const auto end = io.MousePos;
            dl->AddBezierCubic(start,plus(start,{p->left ? -70.0f : 70.0f,0}),minus(end,{70,0}),end,color(p->descriptor.kind),2.5f);
        }
        interacting_ = true;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (!draggingPin_.name.empty() && !hoverPin.name.empty()) connect(config,draggingPin_,hoverPin,message);
        draggingPin_ = {}; draggingBlock_.clear();
    }
    if (ImGui::IsItemActive()) interacting_ = true;
    dl->PopClipRect();
    if (rightGesture_ && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        const bool moved = rightPanned_ || io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] >= 9 ||
            distanceSquared(io.MousePos,io.MouseClickedPos[ImGuiMouseButton_Right]) >= 9;
        if (hovered && !moved) {
            popupPosition_ = world(io.MousePos);
            ImGui::OpenPopup("graph-context");
        }
        rightGesture_ = false; rightPanned_ = false;
    }
    contextMenu(config,message);
}

void Emu51PeripheralEditor::contextMenu(Emu51PeripheralConfig& config, const std::string& message)
{
    if (!ImGui::BeginPopup("graph-context")) return;
    interacting_ = true; // Keep hover-driven submenus responsive while paused.
    if (ImGui::BeginMenu("Add",config.devices.size() < 32)) {
        const char* kinds[] = {"8255", "ADC0804", "Covox / DAC", "Voltage source", "Digital source", "Host audio",
            "LED", "Switch", "Pushbutton", "HD44780 LCD", "4x4 matrix keypad", "74HC595", "74HC165", "24C02 EEPROM", "AY-3-8910", "7-segment LED display", "14-segment LED display", "16-segment LED display", "LED bar graph",
            "CD4511 BCD decoder", "74LS47 BCD decoder", "MAX7219", "MAX7221", "HT16K33", "TM1637"};
        const bool hasAudio = std::any_of(config.devices.begin(),config.devices.end(),[](const auto& d) { return d.kind == Emu51PeripheralKind::Audio; });
        for (int kind = 0; kind <= static_cast<int>(Emu51PeripheralKind::Tm1637); ++kind) {
            if (!ImGui::MenuItem(kinds[kind],nullptr,false,kind != static_cast<int>(Emu51PeripheralKind::Audio) || !hasAudio)) continue;
            int suffix = 1; std::string id;
            do { id = "device" + std::to_string(suffix++); } while (std::any_of(config.devices.begin(),config.devices.end(),[&](const auto& d) { return d.id == id; }));
            Emu51PeripheralDevice device; device.id = id; device.kind = static_cast<Emu51PeripheralKind>(kind);
            if (const int size = emu51PeripheralBusSize(device.kind)) {
                device.address = device.kind == Emu51PeripheralKind::Ay38910 ? 0x9000 : 0x8000;
                while (device.address <= 65536-size && std::any_of(config.devices.begin(),config.devices.end(),[&](const auto& d) {
                    return emu51PeripheralBusSize(d.kind) && device.address < d.address+emu51PeripheralBusSize(d.kind) && d.address < device.address+size;
                })) device.address += size;
            }
            if (device.kind==Emu51PeripheralKind::Max7219 || device.kind==Emu51PeripheralKind::Max7221 || device.kind==Emu51PeripheralKind::Ht16k33) device.displayDigits=8;
            if (device.kind==Emu51PeripheralKind::Ht16k33 || device.kind==Emu51PeripheralKind::Display14) device.displayStyle=14;
            if (device.kind==Emu51PeripheralKind::Display16) device.displayStyle=16;
            config.devices.push_back(device);
            positions_.push_back({id,std::clamp(popupPosition_.x,-100000.0f,100000.0f),std::clamp(popupPosition_.y,-100000.0f,100000.0f)});
            selected_ = id; wire_ = {};
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Fit")) {
        if (ImGui::MenuItem("All blocks")) { fitBlock_.clear(); fit_ = true; }
        if (ImGui::MenuItem("Selected block",nullptr,false,!selected_.empty())) { fitBlock_ = selected_; fit_ = true; }
        if (ImGui::MenuItem("Actual size (100%)")) {
            const auto center = scale(canvasSize_,0.5f), at = world(plus(origin_,center));
            zoom_ = 1; pan_ = minus(center,at); fit_ = false; fitBlock_.clear();
        }
        ImGui::Separator(); ImGui::TextDisabled("Zoom: %.0f%%",zoom_*100);
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Arrange blocks")) {
        // Rearrangement preserves user labels and stable endpoint IDs.
        for (std::size_t i = 0; i <= config.devices.size(); ++i) {
            const std::string id = i ? config.devices[i-1].id : "cpu";
            const auto position = std::find_if(positions_.begin(),positions_.end(),[&](const auto& p) { return p.id == id; });
            if (position == positions_.end()) continue;
            position->x = static_cast<double>(i%3)*400;
            position->y = static_cast<double>(i/3)*420+(i%3)*28;
        }
        fitBlock_.clear(); fit_ = true;
    }
    ImGui::Separator();
    ImGui::MenuItem("Inspector",nullptr,&inspectorVisible_);
    if (ImGui::BeginMenu("Controls")) {
        ImGui::TextUnformatted("Drag headers to move blocks.\nDrag connectors to wire.\nWheel to zoom.\nMiddle/right drag to pan.");
        ImGui::EndMenu();
    }
    if (!message.empty()) {
        ImGui::SeparatorText("Status");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+320);
        ImGui::TextUnformatted(message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndPopup();
}

void Emu51PeripheralEditor::inspector(Emu51PeripheralConfig& config)
{
    ImGui::TextUnformatted("Inspector"); ImGui::Separator();
    if (wire_.kind == 1 && wire_.index >= 0 && wire_.index < static_cast<int>(config.digital.size())) {
        auto& w = config.digital[wire_.index];
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(digitalColor),"Digital wire");
        ImGui::TextWrapped("%s",range(w.a,w.aBit,w.width).c_str());
        ImGui::TextWrapped("%s",range(w.b,w.bBit,w.width).c_str());
        const auto* a = pin(w.a); const auto* b = pin(w.b);
        if (a && b) {
            const int maxA = static_cast<int>(a->descriptor.width), maxB = static_cast<int>(b->descriptor.width);
            ImGui::SetNextItemWidth(85);
            ImGui::InputInt("First bit A",&w.aBit,0);
            w.aBit = std::clamp(w.aBit,0,maxA-1);
            ImGui::SetNextItemWidth(85);
            ImGui::InputInt("First bit B",&w.bBit,0);
            w.bBit = std::clamp(w.bBit,0,maxB-1);
            ImGui::SetNextItemWidth(85);
            ImGui::InputInt("Width",&w.width,0);
            w.width = std::clamp(w.width,1,std::min(maxA-w.aBit,maxB-w.bBit));
        }
        ImGui::TextWrapped("Use the first-bit offsets and width for a slice or permutation.");
        if (ImGui::Button("Remove wire")) { config.digital.erase(config.digital.begin()+wire_.index); wire_ = {}; }
    } else if (wire_.kind == 2 && wire_.index >= 0 && wire_.index < static_cast<int>(config.analog.size())) {
        const auto w = config.analog[wire_.index];
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(analogColor),"Analog wire");
        ImGui::TextWrapped("Output: %s",w.from.c_str()); ImGui::TextWrapped("Input: %s",w.to.c_str());
        if (ImGui::Button("Remove wire")) { config.analog.erase(config.analog.begin()+wire_.index); wire_ = {}; }
    } else {
        std::string id = selected_;
        if (wire_.kind == 3 && wire_.index >= 0 && wire_.index < static_cast<int>(config.devices.size())) id = config.devices[wire_.index].id;
        const auto position = std::find_if(positions_.begin(),positions_.end(),[&](const auto& p) { return p.id == id; });
        if (position != positions_.end()) {
            ImGui::TextDisabled("ID: %s",id.c_str());
            ImGui::TextUnformatted("Block name");
            char name[129]{};
            std::memcpy(name,position->name.data(),std::min(position->name.size(),sizeof(name)-1));
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##block-name","Automatic name",name,sizeof(name))) (void)rename(id,name);
        }
        if (id == "cpu") {
            ImGui::TextUnformatted("CPU");
            ImGui::TextWrapped("P0-P3: four bidirectional 8-bit ports. Each numbered connector is one bit.");
            ImGui::TextWrapped("P3 alternate inputs/outputs: RXD, TXD, INT0, INT1, T0, T1, WR, RD. 8052 also uses P1.0/P1.1 for T2/T2EX.");
            ImGui::TextWrapped("XDATA: 16-bit address, 8-bit data bus. 8255 blocks map four addresses here.");
            ImGui::TextDisabled("Choose CPU model in Execution settings.");
        } else {
            const auto found = std::find_if(config.devices.begin(),config.devices.end(),[&](const auto& d) { return d.id == id; });
            if (found != config.devices.end()) {
                auto& d = *found;
                ImGui::TextWrapped("%s",d.id.c_str()); ImGui::TextWrapped("%s",emu51PeripheralKindName(d.kind)); ImGui::Separator();
                ImGui::PushItemWidth(110);
                if (d.kind == Emu51PeripheralKind::I8255) {
                    ImGui::InputInt("XDATA base",&d.address,0,0,ImGuiInputTextFlags_CharsHexadecimal);
                    ImGui::TextWrapped("Four registers: A, B, C, control. RESET is active high. The XDATA bus connection is automatic.");
                }
                if (d.kind == Emu51PeripheralKind::Led || d.kind == Emu51PeripheralKind::Switch || d.kind == Emu51PeripheralKind::Button)
                    ImGui::Checkbox("Active low",&d.activeLow);
                if (d.kind == Emu51PeripheralKind::Switch) ImGui::Checkbox("Initially closed",&d.initiallyPressed);
                if (d.kind == Emu51PeripheralKind::Button) ImGui::InputInt("Bounce (ms)",&d.bounceMs,1,5);
                if (d.kind == Emu51PeripheralKind::Lcd) {
                    ImGui::InputInt("Columns (1..20)",&d.columns,0);
                    ImGui::InputInt("Rows (1, 2 or 4)",&d.rows,0);
                    ImGui::TextWrapped("4-bit mode uses data bits 4..7. RS, RW and E follow the HD44780 interface.");
                }
                if (d.kind == Emu51PeripheralKind::Keypad)
                    ImGui::TextWrapped("Drive rows low to scan; columns are open-drain outputs with pull-ups. Hold keys in Live.");
                if (d.kind == Emu51PeripheralKind::Eeprom) {
                    ImGui::InputInt("I2C address (50..57)",&d.i2cAddress,0,0,ImGuiInputTextFlags_CharsHexadecimal);
                    char path[4096]; std::snprintf(path,sizeof(path),"%s",d.storagePath.c_str());
                    if (ImGui::InputText("Image file",path,sizeof(path))) d.storagePath = path;
                    ImGui::TextWrapped("Optional 256-byte image. Committed bytes save on Pause/exit. SDA needs open-drain master drive; WP high protects writes.");
                }
                if (d.kind == Emu51PeripheralKind::Ay38910) {
                    ImGui::InputInt("XDATA base",&d.address,0,0,ImGuiInputTextFlags_CharsHexadecimal);
                    ImGui::InputDouble("Chip clock (Hz)",&d.clockHz,1000,100000,"%.0f");
                    ImGui::TextWrapped("XDATA +0 selects a register, +1 reads/writes it. Alternatively use DA/BDIR/BC1 with BC2 high. RESET is active low. Connect out to host audio playback.");
                }
                if (emu51PeripheralDisplayKind(d.kind)>=0) {
                    if (d.kind!=Emu51PeripheralKind::Bargraph && d.kind!=Emu51PeripheralKind::Cd4511 && d.kind!=Emu51PeripheralKind::Ls47)
                        ImGui::InputInt("Digits",&d.displayDigits,1,2);
                    if (d.kind==Emu51PeripheralKind::Bargraph) ImGui::InputInt("Segments (1..16)",&d.barSegments,1,4);
                    if (d.kind<=Emu51PeripheralKind::Bargraph) {
                        ImGui::Checkbox(d.kind==Emu51PeripheralKind::Bargraph ? "Active low" : "Common anode",&d.commonAnode);
                        ImGui::InputDouble("Persistence (ms)",&d.persistenceMs,5,20,"%.0f");
                    }
                    if (d.kind!=Emu51PeripheralKind::Bargraph && d.kind!=Emu51PeripheralKind::Cd4511 && d.kind!=Emu51PeripheralKind::Ls47)
                        ImGui::Checkbox("Colon",&d.displayColon);
                    ImGui::Combo("Color",&d.displayColor,"Red\0Green\0Amber\0Blue\0White\0");
                    if (d.kind==Emu51PeripheralKind::Ht16k33) {
                        ImGui::InputInt("I2C address (70..77)",&d.displayAddress,0,0,ImGuiInputTextFlags_CharsHexadecimal);
                        int style=d.displayStyle==14 ? 1 : d.displayStyle==16 ? 2 : 0;
                        if (ImGui::Combo("Preview geometry",&style,"7 segment\0" "14 segment\0" "16 segment\0")) d.displayStyle=style==1 ? 14 : style==2 ? 16 : 7;
                    }
                    ImGui::TextWrapped("Outputs are visible in Live. Raw displays measure emulated segment on-time; CPU Pause freezes the result.");
                }
                if (d.kind == Emu51PeripheralKind::Adc0804 || d.kind == Emu51PeripheralKind::Dac)
                    ImGui::InputDouble("Reference (V)",&d.referenceVolts,0,0,"%.6g");
                if (d.kind == Emu51PeripheralKind::Adc0804) ImGui::InputInt("Conversion ticks",&d.conversionTicks,0);
                if (d.kind == Emu51PeripheralKind::Voltage) ImGui::InputDouble("Voltage (V)",&d.voltage,0.1,1,"%.6g");
                if (d.kind == Emu51PeripheralKind::Digital) ImGui::InputInt("Value (hex)",&d.digitalValue,0,0,ImGuiInputTextFlags_CharsHexadecimal);
                if (d.kind == Emu51PeripheralKind::Audio) {
                    int choice = d.audioDirections-1;
                    if (ImGui::Combo("Directions",&choice,"Capture\0Playback\0Capture and playback\0")) d.audioDirections = choice+1;
                    ImGui::TextWrapped("48 kHz mono. Capture is an analog output; playback is an analog input. Open audio devices and adjust gain/buffering in Live.");
                }
                ImGui::PopItemWidth();
                if (ImGui::Button("Remove block and wires")) removeDevice(config,id);
            } else {
                ImGui::TextWrapped("Select a block or wire to edit its properties.");
                ImGui::TextWrapped("Drag a whole-port circle to connect a byte, or a numbered bit to connect one signal. Select the wire to set a bit range.");
            }
        }
    }
    ImGui::Separator();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(digitalColor),"Digital  /  circles and bits");
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(analogColor),"Analog  /  squares");
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(busColor),"XDATA bus  /  address mapping");
    ImGui::TextWrapped("IN / OUT / I/O indicate signal direction. Unconnected ports remain visible.");
}
