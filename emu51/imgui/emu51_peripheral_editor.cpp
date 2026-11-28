// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_peripheral_editor.h"
#include "imgui_internal.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <queue>
#include <utility>

namespace {
constexpr float titleSize = 34, pinFontSize = 24;
constexpr float headerPadding = 8, subtitleTop = 38, headerHeight = subtitleTop+pinFontSize+2;
constexpr float pinPadding = 9, rowHeight = pinFontSize+4, pinColumnGap = 8;
constexpr ImVec2 bitHalf{12.5f,10.5f};
constexpr unsigned bitsPerRow = 8;
constexpr float bitStep = 27, bitInset = bitHalf.x+5, bitLaneGap = 2, bitLeadGap = 3;
constexpr float bitRowHeight = 2*bitHalf.y+bitLeadGap+(bitsPerRow-1)*bitLaneGap+3;
// Project layout coordinates keep their original units.
constexpr float layoutScale = 4;
constexpr float columnGap = 184, blockGap = 64;
constexpr float minLead = 40, maxLead = 96, leadStep = 8;
constexpr float clearance = 24, cornerRadius = 16, laneGap = 20;
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
struct Bounds { ImVec2 lo, hi; };
bool intersects(Bounds a, Bounds b)
{
    return a.lo.x < b.hi.x && a.hi.x > b.lo.x && a.lo.y < b.hi.y && a.hi.y > b.lo.y;
}
bool samePoint(ImVec2 a, ImVec2 b) { return a.x == b.x && a.y == b.y; }
void appendPoint(std::vector<ImVec2>& path, ImVec2 p)
{
    if (!path.empty() && samePoint(path.back(),p)) return;
    if (path.size() >= 2) {
        const auto a = minus(path.back(),path[path.size()-2]), b = minus(p,path.back());
        if ((a.x == 0 && b.x == 0 && a.y*b.y > 0) || (a.y == 0 && b.y == 0 && a.x*b.x > 0)) {
            path.back() = p;
            return;
        }
    }
    path.push_back(p);
}
float manhattan(ImVec2 a, ImVec2 b) { return std::abs(a.x-b.x)+std::abs(a.y-b.y); }

struct Terminal {
    ImVec2 edge, lead;
    int outward; // -1 for left, +1 for right, 0 for a free cursor
};
using TerminalPair = std::pair<std::optional<Terminal>,std::optional<Terminal>>;

std::optional<Terminal> terminal(ImVec2 edge, bool left, const std::vector<Bounds>& boxes, float maxLength)
{
    const int outward = left ? -1 : 1;
    float available = maxLength;
    for (const auto& box : boxes) {
        if (edge.y <= box.lo.y-clearance || edge.y >= box.hi.y+clearance) continue;
        const float near = left ? edge.x-box.hi.x : box.lo.x-edge.x;
        const float far = left ? edge.x-box.lo.x : box.hi.x-edge.x;
        if (far <= 0) continue; // Includes the box that owns this connector.
        available = std::min(available,near-clearance);
    }
    if (available < clearance) return std::nullopt;
    return Terminal{edge,plus(edge,{static_cast<float>(outward)*available,0}),outward};
}

std::vector<ImVec2> roundCorners(const std::vector<ImVec2>& route)
{
    if (route.size() < 3) return route;
    std::vector<ImVec2> curve{route.front()};
    for (std::size_t i = 1; i+1 < route.size(); ++i) {
        const auto before = route[i-1], corner = route[i], after = route[i+1];
        const float incoming = manhattan(before,corner), outgoing = manhattan(corner,after);
        // The entire quadratic stays within its control triangle. Limiting
        // that triangle to less than the obstacle clearance keeps it outside boxes.
        const float radius = std::min({cornerRadius,incoming/3,outgoing/3});
        const auto a = plus(corner,scale(minus(before,corner),radius/incoming));
        const auto b = plus(corner,scale(minus(after,corner),radius/outgoing));
        curve.push_back(a);
        for (int step = 1; step <= 5; ++step) {
            const float t = static_cast<float>(step)/5.0f, u = 1-t;
            curve.push_back(plus(plus(scale(a,u*u),scale(corner,2*u*t)),scale(b,t*t)));
        }
    }
    curve.push_back(route.back());
    return curve;
}

TerminalPair routedTerminals(ImVec2 a, bool aLeft, float aLength,
    ImVec2 b, bool bLeft, float bLength, const std::vector<Bounds>& boxes)
{
    const float gap = (b.x-a.x)*(aLeft ? -1.0f : 1.0f);
    if (aLeft != bLeft && gap >= 2*clearance && gap < aLength+bLength) {
        // Share the tight space above the minimum clearance in proportion to
        // each terminal's preferred length.
        const float fraction = (gap-2*clearance)/(aLength+bLength-2*clearance);
        aLength = clearance+(aLength-clearance)*fraction;
        bLength = gap-aLength;
    }
    return {terminal(a,aLeft,boxes,aLength),terminal(b,bLeft,boxes,bLength)};
}

ImVec2 pointAlong(const std::vector<ImVec2>& path, float fraction)
{
    float length = 0;
    for (std::size_t i = 1; i < path.size(); ++i) length += manhattan(path[i-1],path[i]);
    float at = length*fraction;
    for (std::size_t i = 1; i < path.size(); ++i) {
        const float segment = manhattan(path[i-1],path[i]);
        if (at <= segment) return plus(path[i-1],scale(minus(path[i],path[i-1]),at/segment));
        at -= segment;
    }
    return path.back();
}

// An orthogonal grid whose rails follow box edges and connector positions.
// Subdividing common rails lets later wires pay for exactly the distance shared.
class WireRouter {
public:
    WireRouter(const std::vector<Bounds>& boxes, const std::vector<ImVec2>& ports)
    {
        for (const auto p : ports) { xs_.push_back(p.x); ys_.push_back(p.y); }
        for (const auto& box : boxes) {
            const auto lo = minus(box.lo,{clearance,clearance});
            const auto hi = plus(box.hi,{clearance,clearance});
            xs_.insert(xs_.end(),{lo.x,box.lo.x-maxLead-laneGap,box.lo.x-maxLead-2*laneGap,
                hi.x,box.hi.x+maxLead+laneGap,box.hi.x+maxLead+2*laneGap});
            ys_.insert(ys_.end(),{lo.y-2*laneGap,lo.y-laneGap,lo.y,hi.y,hi.y+laneGap,hi.y+2*laneGap});
        }
        for (auto* axis : {&xs_,&ys_}) {
            std::sort(axis->begin(),axis->end());
            axis->erase(std::unique(axis->begin(),axis->end()),axis->end());
        }
        columns_ = static_cast<int>(xs_.size());
        rows_ = static_cast<int>(ys_.size());
        edges_.resize(columns_*rows_);
        for (const auto& box : boxes) {
            const auto lo = minus(box.lo,{clearance,clearance});
            const auto hi = plus(box.hi,{clearance,clearance});
            const int l = index(xs_,lo.x), r = index(xs_,hi.x), t = index(ys_,lo.y), b = index(ys_,hi.y);
            for (int y = t+1; y < b; ++y)
                for (int x = l; x < r; ++x) edges_[y*columns_+x][0] = -1;
            for (int y = t; y < b; ++y)
                for (int x = l+1; x < r; ++x) edges_[y*columns_+x][1] = -1;
        }
    }

    std::vector<ImVec2> route(const Terminal& from, const Terminal& to)
    {
        const int start = vertex(from.lead), finish = vertex(to.lead);
        if (start == finish && from.outward == to.outward) return {};
        struct Candidate {
            float estimate, cost;
            int state;
            bool operator<(const Candidate& other) const
            {
                if (estimate != other.estimate) return estimate > other.estimate;
                if (cost != other.cost) return cost < other.cost;
                return state > other.state;
            }
        };
        constexpr float bendCost = 24;
        std::priority_queue<Candidate> open;
        costs_.assign(edges_.size()*2,FLT_MAX);
        previous_.resize(costs_.size());
        costs_[start*2] = 0;
        previous_[start*2] = -1;
        open.push({manhattan(from.lead,to.lead),0,start*2});
        int last = -1;
        while (!open.empty()) {
            const auto current = open.top();
            open.pop();
            if (current.cost != costs_[current.state]) continue;
            const int here = current.state/2;
            if (here == finish) { last = current.state; break; }
            const int x = here%columns_, y = here/columns_;
            const int neighbors[] = {x > 0 ? here-1 : -1, x+1 < columns_ ? here+1 : -1,
                y > 0 ? here-columns_ : -1, y+1 < rows_ ? here+columns_ : -1};
            for (int i = 0; i < 4; ++i) {
                const int next = neighbors[i], axis = i/2;
                if (next < 0) continue;
                const int horizontal = axis == 0 ? (i == 0 ? -1 : 1) : 0;
                if (here == start && horizontal == -from.outward) continue;
                if (next == finish && to.outward && horizontal == to.outward) continue;
                const int usage = edges_[std::min(here,next)][axis];
                if (usage < 0) continue;
                const auto point = coordinates(next);
                const float length = manhattan(coordinates(here),point);
                float cost = current.cost + length*static_cast<float>(1+3*usage);
                if (axis != current.state%2) cost += bendCost;
                const int perpendicular = axis == 0 ? columns_ : 1;
                if ((axis == 0 ? next/columns_ > 0 : next%columns_ > 0) &&
                    edges_[next-perpendicular][1-axis] > 0 && edges_[next][1-axis] > 0) cost += 12;
                if (to.outward && next == finish && axis == 1) cost += bendCost;
                const int state = next*2+axis;
                if (cost >= costs_[state]) continue;
                costs_[state] = cost;
                previous_[state] = current.state;
                open.push({cost+manhattan(point,to.lead),cost,state});
            }
        }
        std::vector<ImVec2> result;
        for (int state = last; state >= 0; state = previous_[state]) {
            const int here = state/2, prior = previous_[state];
            if (prior >= 0) ++edges_[std::min(here,prior/2)][state%2];
            appendPoint(result,coordinates(here));
        }
        if (result.empty()) return {};
        std::vector<ImVec2> path{from.edge};
        for (auto it = result.rbegin(); it != result.rend(); ++it) appendPoint(path,*it);
        appendPoint(path,to.edge);
        return path;
    }

private:
    static int index(const std::vector<float>& axis, float at)
    {
        return static_cast<int>(std::lower_bound(axis.begin(),axis.end(),at)-axis.begin());
    }
    int vertex(ImVec2 p) const { return index(ys_,p.y)*columns_+index(xs_,p.x); }
    ImVec2 coordinates(int vertex) const { return {xs_[vertex%columns_],ys_[vertex/columns_]}; }
    std::vector<float> xs_, ys_, costs_;
    std::vector<int> previous_;
    std::vector<std::array<int,2>> edges_;
    int columns_, rows_;
};

ImU32 color(unsigned kind) { return kind == E51P_DIGITAL ? digitalColor : kind == E51P_ANALOG ? analogColor : busColor; }
const char* direction(const Emu51PeripheralPin& p)
{
    if (p.kind == E51P_BUS_INITIATOR || p.kind == E51P_BUS_TARGET) return "BUS";
    return p.direction == (E51P_INPUT|E51P_OUTPUT) ? "I/O" : p.direction & E51P_OUTPUT ? "OUT" : "IN";
}
std::string pinLabel(const Emu51PeripheralPin& pin, const Emu51PeripheralDevice* device)
{
    auto name = pin.name.substr(pin.name.find('.')+1);
    if (!device && (name == "PSEN" || name == "RD" || name == "WR")) name = "/"+name;
    if (device && device->kind == Emu51PeripheralKind::AddressDecoder) {
        const auto output = std::find_if(device->decoderOutputs.begin(),device->decoderOutputs.end(),[&](const auto& o) { return o.id == name; });
        if (output != device->decoderOutputs.end()) name = output->name.empty() ? (output->activeLow ? "/" : "")+output->id : output->name;
    } else if (device && ((name == "CS" && device->csLow) || (name == "OE" && device->oeLow) || (name == "WE" && device->weLow))) {
        name = "/"+name;
    }
    if (pin.width > 1 && pin.kind == E51P_DIGITAL) name += " ["+std::to_string(pin.width-1)+":0]";
    return name + "  " + direction(pin);
}

float layoutTextWidth(ImFont* font, float size, const std::string& text)
{
    // Keep world geometry independent of the zoom and use the font's base
    // metrics so a layout calculation does not create a new atlas size.
    const float base = font->LegacySize;
    return font->CalcTextSizeA(base,FLT_MAX,0,text.c_str()).x*(size/base);
}

unsigned bitsInRow(unsigned width, unsigned bit)
{
    return std::min(bitsPerRow,width-bit/bitsPerRow*bitsPerRow);
}

float bitEscapeY(ImVec2 port, unsigned bit, unsigned width, bool left)
{
    const unsigned index = bit%bitsPerRow;
    const unsigned lane = left ? index : bitsInRow(width,bit)-1-index;
    return port.y+bitHalf.y+bitLeadGap+static_cast<float>(lane)*bitLaneGap;
}

std::string range(const std::string& name, int bit, int width)
{
    return name + "[" + (width > 1 ? std::to_string(bit+width-1) + ":" : "") + std::to_string(bit) + "]";
}
}

void Emu51PeripheralEditor::restore(const std::vector<Emu51PeripheralBlockPosition>& positions)
{
    positions_ = positions;
    decoderText_.clear(); decoderErrors_.clear(); decoderNewValues_.clear(); memoryRequest_.reset();
    selected_.clear(); wire_ = {}; draggingBlock_.clear(); draggingPin_ = {};
    fitBlock_.clear(); rightGesture_ = rightPanned_ = false;
    pan_ = {24,24}; fit_ = readableFit_ = true;
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
    const bool unplaced = positions_.empty();
    std::vector<std::size_t> added;
    const auto descriptors = Emu51PeripheralSession::describe(config, true);
    auto* font = ImGui::GetFont();
    const auto add = [&](const std::string& id, const std::string& title, const std::string& subtitle,
        const Emu51PeripheralDevice* device) {
        auto found = std::find_if(positions_.begin(), positions_.end(), [&](const auto& p) { return p.id == id; });
        if (found == positions_.end()) {
            added.push_back(blocks_.size());
            positions_.push_back({id});
            found = std::prev(positions_.end());
        }
        Block b; b.id = id; b.title = found->name.empty() ? title : found->name; b.subtitle = subtitle;
        b.position = {static_cast<float>(found->x)*layoutScale, static_cast<float>(found->y)*layoutScale};
        std::array<float,2> heights{};
        float halfWidthForBits = 0;
        for (const auto& d : descriptors) {
            if (!d.name.starts_with(id + ".")) continue;
            Pin p; p.descriptor = d;
            p.label = pinLabel(d,device);
            p.left = d.kind == E51P_BUS_TARGET || (d.kind != E51P_BUS_INITIATOR && !(d.direction & E51P_OUTPUT));
            const int side = p.left ? 0 : 1;
            const float row = headerHeight+heights[side];
            const unsigned bitRows = d.kind == E51P_DIGITAL && d.width > 1 ? (d.width+bitsPerRow-1)/bitsPerRow : 0;
            heights[side] += rowHeight+bitRowHeight*static_cast<float>(bitRows);
            p.position = {0,row+rowHeight/2};
            const float labelWidth = std::min(235.0f,pinPadding+layoutTextWidth(font,pinFontSize,p.label)+4);
            auto& width = b.pinWidths[side];
            width = std::max(width,labelWidth);
            if (bitRows) {
                const float bitWidth = bitInset+static_cast<float>(bitsInRow(d.width,0)-1)*bitStep+bitHalf.x;
                halfWidthForBits = std::max(halfWidthForBits,bitWidth);
                for (unsigned bit = 0; bit < d.width; ++bit) {
                    const unsigned index = bit%bitsPerRow;
                    const unsigned column = p.left ? index : bitsInRow(d.width,bit)-1-index;
                    const float x = (p.left ? 1.0f : -1.0f)*(bitInset+bitStep*static_cast<float>(column));
                    p.bits.push_back({x,row+rowHeight+bitHalf.y+bitRowHeight*static_cast<float>(bit/bitsPerRow)});
                }
            }
            b.pins.push_back(std::move(p));
        }
        const float heading = std::min(384.0f,std::max(layoutTextWidth(font,titleSize,b.title),layoutTextWidth(font,pinFontSize,b.subtitle)));
        const float labels = b.pinWidths[0] > 0 && b.pinWidths[1] > 0 ? 2*std::max(b.pinWidths[0],b.pinWidths[1]) :
            b.pinWidths[0]+b.pinWidths[1];
        b.size = {std::max(heading+2*headerPadding,std::max(labels,2*halfWidthForBits)+pinColumnGap),
            headerHeight+std::max(heights[0],heights[1])+3};
        // The first and last connectors on each side set the lead length
        // profile. Include numbered bit tails where they leave the block.
        std::array<float,2> first{FLT_MAX,FLT_MAX}, last{-FLT_MAX,-FLT_MAX};
        for (const auto& p : b.pins) {
            const int side = p.left ? 0 : 1;
            first[side] = std::min(first[side],p.position.y);
            last[side] = std::max(last[side],p.position.y);
            for (unsigned bit = 0; bit < p.bits.size(); ++bit)
                last[side] = std::max(last[side],bitEscapeY(p.bits[bit],bit,p.descriptor.width,p.left));
        }
        for (auto& p : b.pins) {
            const int side = p.left ? 0 : 1;
            p.leadHalfSpan = (last[side]-first[side])/2;
            p.leadCenterOffset = first[side]+p.leadHalfSpan-p.position.y;
            const auto edge = plus(b.position,{p.left ? 0 : b.size.x,0});
            p.position = plus(edge,p.position);
            for (auto& bit : p.bits) bit = plus(edge,bit);
        }
        blocks_.push_back(std::move(b));
    };
    add("cpu",std::string("Intel ")+emu51ModelInfo(model).id,"CPU / CODE / XDATA",nullptr);
    for (const auto& d : config.devices) {
        if (const int size = emu51PeripheralBusSize(d.kind); size && !d.wiredBus) {
            char subtitle[96]; std::snprintf(subtitle,sizeof(subtitle),"%s / XDATA %04X-%04X",emu51PeripheralKindName(d.kind),d.address,d.address+size-1);
            add(d.id,d.id,subtitle,&d);
        } else add(d.id,d.id,emu51PeripheralKindName(d.kind),&d);
    }
    std::erase_if(positions_, [&](const auto& p) { return std::none_of(blocks_.begin(), blocks_.end(), [&](const auto& b) { return b.id == p.id; }); });
    if (unplaced) {
        arrange(config);
    } else if (!added.empty()) {
        ImVec2 edge{-FLT_MAX,FLT_MAX};
        for (std::size_t i = 0; i < blocks_.size(); ++i) {
            if (std::find(added.begin(),added.end(),i) != added.end()) continue;
            edge.x = std::max(edge.x,blocks_[i].position.x+blocks_[i].size.x);
            edge.y = std::min(edge.y,blocks_[i].position.y);
        }
        if (edge.x == -FLT_MAX) edge = {-columnGap,0};
        for (const auto i : added) {
            moveBlock(blocks_[i],{edge.x+columnGap,edge.y});
            edge.y += blocks_[i].size.y+blockGap;
        }
    }
    // Selected blocks stay above intersecting blocks and their connectors.
    std::stable_sort(blocks_.begin(), blocks_.end(), [&](const auto& a, const auto& b) { return (a.id == selected_) < (b.id == selected_); });
}

void Emu51PeripheralEditor::moveBlock(Block& block, ImVec2 at)
{
    const auto saved = std::find_if(positions_.begin(),positions_.end(),[&](const auto& p) { return p.id == block.id; });
    const float limit = 100000*layoutScale;
    at = {std::clamp(at.x,-limit,limit),std::clamp(at.y,-limit,limit)};
    saved->x = at.x/layoutScale;
    saved->y = at.y/layoutScale;
    for (auto& pin : block.pins) {
        pin.position = {at.x+(pin.left ? 0 : block.size.x),at.y+(pin.position.y-block.position.y)};
        for (auto& bit : pin.bits) bit = plus(at,minus(bit,block.position));
    }
    block.position = at;
}

std::vector<Emu51PeripheralEditor::Connection> Emu51PeripheralEditor::connections(const Emu51PeripheralConfig& config) const
{
    std::vector<Connection> result;
    const auto add = [&](const Pin* a, int ab, const Pin* b, int bb, int kind, std::size_t i) {
        if (a && b) result.push_back({a,b,ab,bb,{kind,static_cast<int>(i)}});
    };
    for (std::size_t i = 0; i < config.digital.size(); ++i) {
        const auto& wire = config.digital[i];
        const auto* a = pin(wire.a);
        const auto* b = pin(wire.b);
        const bool whole = a && b && wire.aBit == 0 && wire.bBit == 0 &&
            wire.width == static_cast<int>(a->descriptor.width) && wire.width == static_cast<int>(b->descriptor.width);
        add(a,whole ? -1 : wire.aBit,b,whole ? -1 : wire.bBit,1,i);
    }
    for (std::size_t i = 0; i < config.analog.size(); ++i) {
        const auto& wire = config.analog[i];
        add(pin(wire.from),-1,pin(wire.to),-1,2,i);
    }
    for (std::size_t i = 0; i < config.devices.size(); ++i) {
        const auto& device = config.devices[i];
        if (emu51PeripheralBusSize(device.kind) && !device.wiredBus)
            add(pin("cpu.xdata"),-1,pin(device.id+".bus"),-1,3,i);
    }
    return result;
}

Emu51PeripheralEditor::RouteEnd Emu51PeripheralEditor::routeEnd(const Pin& pin, int bit) const
{
    const bool numbered = bit >= 0 && bit < static_cast<int>(pin.bits.size());
    const auto port = numbered ? pin.bits[bit] : pin.position;
    const ImVec2 edge{pin.position.x,numbered ? bitEscapeY(port,static_cast<unsigned>(bit),pin.descriptor.width,pin.left) : port.y};
    const float centerDistance = std::abs((edge.y-pin.position.y)-pin.leadCenterOffset);
    const float weight = pin.leadHalfSpan > 0 ? std::max(0.0f,1-centerDistance/pin.leadHalfSpan) : 0;
    // Reuse a small set of routing rails for nearby connectors.
    const float length = minLead+leadStep*std::round(weight*(maxLead-minLead)/leadStep);
    return {port,edge,pin.left,length};
}

void Emu51PeripheralEditor::arrange(const Emu51PeripheralConfig& config)
{
    if (blocks_.empty()) return;
    // Stable IDs keep selection and draw order from changing the arrangement.
    std::sort(blocks_.begin(),blocks_.end(),[](const auto& a, const auto& b) {
        if ((a.id == "cpu") != (b.id == "cpu")) return a.id == "cpu";
        return a.id < b.id;
    });
    const int count = static_cast<int>(blocks_.size());
    struct Neighbor { int node, offset; float pinDelta; };
    std::map<const Pin*,int> owners;
    std::vector<std::vector<Neighbor>> neighbors(count);
    for (int i = 0; i < count; ++i)
        for (const auto& pin : blocks_[i].pins) owners.emplace(&pin,i);
    for (const auto& wire : connections(config)) {
        const int a = owners.at(wire.from), b = owners.at(wire.to);
        if (a == b) continue;
        const int offset = wire.from->left == wire.to->left ? 0 : wire.from->left ? -1 : 1;
        const float delta = (routeEnd(*wire.from,wire.fromBit).edge.y-blocks_[a].position.y)-
            (routeEnd(*wire.to,wire.toBit).edge.y-blocks_[b].position.y);
        neighbors[a].push_back({b,offset,delta});
        neighbors[b].push_back({a,-offset,-delta});
    }
    const bool hasThroughBlock = std::any_of(neighbors.begin(),neighbors.end(),[](const auto& links) {
        return std::any_of(links.begin(),links.end(),[](const auto& link) { return link.offset < 0; }) &&
            std::any_of(links.begin(),links.end(),[](const auto& link) { return link.offset > 0; });
    });
    const int columnCount = std::min({count,5,std::max(hasThroughBlock ? 3 : 1,static_cast<int>(std::ceil(std::sqrt(count))))});
    std::vector<std::vector<int>> columns(columnCount);
    std::vector<int> column(count,-1), placedNeighbors(count);
    std::vector<float> heights(columnCount), tops(count), desired(count);
    for (int placed = 0; placed < count; ++placed) {
        int next = -1;
        for (int i = 0; i < count; ++i)
            if (column[i] < 0 && (next < 0 || placedNeighbors[i] > placedNeighbors[next] ||
                (placedNeighbors[i] == placedNeighbors[next] && neighbors[i].size() > neighbors[next].size()))) next = i;
        int best = 0;
        float bestCost = FLT_MAX;
        for (int col = 0; col < columnCount; ++col) {
            float cost = heights[col]*0.12f;
            for (const auto& neighbor : neighbors[next]) {
                const int other = column[neighbor.node];
                if (other < 0) {
                    if (col+neighbor.offset < 0 || col+neighbor.offset >= columnCount) cost += 50;
                    continue;
                }
                const int distance = other-col;
                cost += static_cast<float>(140*std::abs(distance-neighbor.offset));
                if (neighbor.offset && distance*neighbor.offset <= 0) cost += 180;
            }
            if (cost < bestCost) { bestCost = cost; best = col; }
        }
        column[next] = best;
        columns[best].push_back(next);
        tops[next] = heights[best];
        heights[best] += blocks_[next].size.y+blockGap;
        for (const auto& neighbor : neighbors[next]) ++placedNeighbors[neighbor.node];
    }

    // Use connected pin heights to order and align each column, with a fixed
    // height budget so alignment cannot make the whole drawing grow on each pass.
    const float heightLimit = *std::max_element(heights.begin(),heights.end())+blockGap;
    for (int pass = 0; pass < 4; ++pass) {
        for (int i = 0; i < columnCount; ++i) {
            const int col = pass%2 ? columnCount-1-i : i;
            auto& nodes = columns[col];
            for (const int node : nodes) {
                std::vector<float> aligned;
                for (const auto& neighbor : neighbors[node])
                    if (column[neighbor.node] != col) aligned.push_back(tops[neighbor.node]-neighbor.pinDelta);
                if (aligned.empty()) { desired[node] = tops[node]; continue; }
                const auto middle = aligned.begin()+std::ssize(aligned)/2;
                std::nth_element(aligned.begin(),middle,aligned.end());
                desired[node] = *middle;
            }
            std::stable_sort(nodes.begin(),nodes.end(),[&](int a, int b) { return desired[a] < desired[b]; });
            float top = 0, remaining = heights[col];
            for (const int node : nodes) {
                tops[node] = std::clamp(desired[node],top,std::max(top,heightLimit-remaining));
                const float advance = blocks_[node].size.y+blockGap;
                top = tops[node]+advance;
                remaining -= advance;
            }
        }
    }
    float x = 0;
    for (const auto& nodes : columns) {
        if (nodes.empty()) continue;
        float width = 0;
        for (const int node : nodes) {
            moveBlock(blocks_[node],{x,tops[node]});
            width = std::max(width,blocks_[node].size.x);
        }
        x += width+columnGap;
    }
}

void Emu51PeripheralEditor::updateRoutes(const std::vector<Connection>& wires)
{
    std::vector<float> geometry{static_cast<float>(blocks_.size())};
    std::vector<Bounds> boxes;
    for (const auto& block : blocks_) {
        const auto hi = plus(block.position,block.size);
        boxes.push_back({block.position,hi});
    }
    std::sort(boxes.begin(),boxes.end(),[](const auto& a, const auto& b) {
        return std::array{a.lo.x,a.lo.y,a.hi.x,a.hi.y} < std::array{b.lo.x,b.lo.y,b.hi.x,b.hi.y};
    });
    for (const auto& box : boxes) geometry.insert(geometry.end(),{box.lo.x,box.lo.y,box.hi.x,box.hi.y});
    for (const auto& wire : wires) {
        for (const auto end : {routeEnd(*wire.from,wire.fromBit),routeEnd(*wire.to,wire.toBit)}) {
            geometry.insert(geometry.end(),{end.edge.x,end.edge.y,end.left ? -1.0f : 1.0f,end.leadLength});
        }
    }
    if (geometry == routeGeometry_) return;
    routeGeometry_ = std::move(geometry);
    routes_.clear();
    routes_.resize(wires.size());
    roundedRoutes_.clear();
    roundedRoutes_.resize(wires.size());
    if (wires.empty()) return;
    std::vector<TerminalPair> terminals;
    std::vector<ImVec2> ports;
    for (const auto& wire : wires) {
        const auto a = routeEnd(*wire.from,wire.fromBit), b = routeEnd(*wire.to,wire.toBit);
        const auto ends = routedTerminals(a.edge,a.left,a.leadLength,b.edge,b.left,b.leadLength,boxes);
        terminals.push_back(ends);
        if (!ends.first || !ends.second) continue;
        ports.push_back(ends.first->lead);
        ports.push_back(ends.second->lead);
    }
    if (ports.empty()) return;
    WireRouter router(boxes,ports);
    for (std::size_t i = 0; i < wires.size(); ++i) {
        const auto& [a,b] = terminals[i];
        if (!a || !b) continue;
        routes_[i] = router.route(*a,*b);
        roundedRoutes_[i] = roundCorners(routes_[i]);
    }
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
    else {
        const auto include = [&](ImVec2 p) {
            lo.x = std::min(lo.x,p.x); lo.y = std::min(lo.y,p.y);
            hi.x = std::max(hi.x,p.x); hi.y = std::max(hi.y,p.y);
        };
        for (const auto& b : blocks_) { include(b.position); include(plus(b.position,b.size)); }
        for (const auto& route : routes_) for (const auto p : route) include(p);
    }
    const auto margin = ImVec2(std::min(64.0f,canvasSize_.x*0.2f),std::min(64.0f,canvasSize_.y*0.2f));
    zoom_ = std::min({(canvasSize_.x-margin.x)/(hi.x-lo.x),(canvasSize_.y-margin.y)/(hi.y-lo.y),1.25f});
    constexpr float readableZoom = 0.5f;
    if (readableFit_) {
        zoom_ = readableZoom;
        const auto cpu = std::find_if(blocks_.begin(),blocks_.end(),[](const auto& block) { return block.id == "cpu"; });
        const auto reference = cpu != blocks_.end() ? cpu->position : lo;
        const auto frameAxis = [&](float start, float end, float view, float padding, float near) {
            const float first = padding/2-start*zoom_, last = view-padding/2-end*zoom_;
            return last < first ? std::clamp(padding/2-near*zoom_,last,first) : (first+last)/2;
        };
        pan_ = {frameAxis(lo.x,hi.x,canvasSize_.x,margin.x,reference.x),
            frameAxis(lo.y,hi.y,canvasSize_.y,margin.y,reference.y)};
    } else {
        pan_ = { (canvasSize_.x-(hi.x-lo.x)*zoom_)/2-lo.x*zoom_, (canvasSize_.y-(hi.y-lo.y)*zoom_)/2-lo.y*zoom_ };
    }
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
    std::erase_if(decoderText_,[&](const auto& p) { return p.first.starts_with(id+"."); });
    std::erase_if(decoderErrors_,[&](const auto& p) { return p.first.starts_with(id+"."); });
    std::erase_if(decoderNewValues_,[&](const auto& p) { return p.first.starts_with(id+"."); });
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
    if (showInspector && inspectorVisible_) {
        if (sideBySide) ImGui::SameLine();
        ImGui::BeginChild("circuit-inspector",ImVec2(0,sideBySide ? canvasHeight : std::max(80.0f,ImGui::GetContentRegionAvail().y)),ImGuiChildFlags_Borders);
        inspector(config);
        interacting_ |= ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImGui::EndChild();
    }
}

void Emu51PeripheralEditor::canvas(Emu51PeripheralConfig& config, std::string& message)
{
    auto& io = ImGui::GetIO();
    origin_ = ImGui::GetCursorScreenPos(); canvasSize_ = ImGui::GetContentRegionAvail();
    canvasSize_.x = std::max(1.0f,canvasSize_.x); canvasSize_.y = std::max(1.0f,canvasSize_.y);
    const auto wires = connections(config);
    updateRoutes(wires);
    if (fit_) fitView();
    ImGui::InvisibleButton("##circuit",canvasSize_,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle|ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        rightGesture_ = true; rightPanned_ = false;
    }
    if (hovered && io.MouseWheel != 0) {
        const auto at = world(io.MousePos);
        const float minimum = std::min(0.25f,zoom_);
        zoom_ = std::clamp(zoom_*std::pow(1.15f,io.MouseWheel),minimum,2.0f);
        pan_ = minus(minus(io.MousePos,origin_),scale(at,zoom_)); interacting_ = true;
    }
    if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle,0) || ImGui::IsMouseDragging(ImGuiMouseButton_Right,3))) {
        pan_ = plus(pan_,io.MouseDelta); interacting_ = true;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) rightPanned_ = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { draggingBlock_.clear(); draggingPin_ = {}; }
    if (!draggingBlock_.empty() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const auto block = std::find_if(blocks_.begin(),blocks_.end(),[&](const auto& b) { return b.id == draggingBlock_; });
        if (block != blocks_.end()) {
            moveBlock(*block,minus(world(io.MousePos),blockDragOffset_));
            updateRoutes(wires);
        }
        interacting_ = true;
    }
    auto* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin_,plus(origin_,canvasSize_),true);
    dl->AddRectFilled(origin_,plus(origin_,canvasSize_),IM_COL32(21,26,35,255));
    const float grid = std::max(8.0f,32*zoom_);
    const float gridStartX = std::fmod(pan_.x,grid);
    const float gridStartY = std::fmod(pan_.y,grid);
    for (unsigned column = 0; ; ++column) {
        const float x = gridStartX + static_cast<float>(column)*grid;
        if (!(x < canvasSize_.x)) break;
        dl->AddLine(plus(origin_,{x,0}),plus(origin_,{x,canvasSize_.y}),IM_COL32(36,43,54,255));
    }
    for (unsigned row = 0; ; ++row) {
        const float y = gridStartY + static_cast<float>(row)*grid;
        if (!(y < canvasSize_.y)) break;
        dl->AddLine(plus(origin_,{0,y}),plus(origin_,{canvasSize_.x,y}),IM_COL32(36,43,54,255));
    }
    Connector hoverPin; std::string hoverBlock; bool header = false;
    if (hovered) for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
        const auto& b = *it;
        float closestPin = FLT_MAX;
        for (const auto& p : b.pins) {
            const auto hit = [&](ImVec2 center, ImVec2 half, int bit) {
                const float distance = distanceSquared(io.MousePos,center);
                if (distance < closestPin && contains(io.MousePos,minus(center,half),plus(center,half))) {
                    hoverPin = {p.descriptor.name,bit};
                    closestPin = distance;
                }
            };
            const float radius = std::max(6.0f,7*zoom_);
            hit(anchor(p),{radius,radius},-1);
            for (std::size_t bit = 0; bit < p.bits.size(); ++bit)
                hit(screen(p.bits[bit]),{std::max(6.0f,bitHalf.x*zoom_),std::max(6.0f,bitHalf.y*zoom_)},static_cast<int>(bit));
        }
        if (!hoverPin.name.empty() || contains(io.MousePos,screen(b.position),screen(plus(b.position,b.size)))) {
            hoverBlock = b.id; header = io.MousePos.y < screen(plus(b.position,{0,headerHeight})).y; break;
        }
    }
    WireSelection hoverWire;
    float closest = 7;
    const auto text = [&](ImVec2 p, const std::string& s, ImU32 c, float size) {
        dl->AddText(ImGui::GetFont(),size*zoom_,screen(p),c,s.c_str());
    };
    const auto stroke = [&](const std::vector<ImVec2>& route, ImU32 c, float thickness) {
        if (route.size() < 2) return;
        dl->PathClear();
        for (const auto p : route) dl->PathLineTo(screen(p));
        dl->PathStroke(c,0,thickness);
    };
    const auto drawBitTail = [&](const RouteEnd& end, ImU32 c, float thickness) {
        if (samePoint(end.port,end.edge)) return;
        dl->PathLineTo(screen({end.port.x,end.port.y+bitHalf.y}));
        dl->PathLineTo(screen({end.port.x,end.edge.y}));
        dl->PathLineTo(screen(end.edge));
        dl->PathStroke(c,0,std::min(thickness,bitLaneGap*zoom_));
    };
    struct BitTail { RouteEnd end; ImU32 color; float thickness; };
    std::map<const Pin*,std::vector<BitTail>> bitTails;
    const auto wireColor = [](int kind) { return kind == 1 ? digitalColor : kind == 2 ? analogColor : busColor; };
    for (std::size_t i = 0; i < wires.size(); ++i) {
        const auto& route = routes_[i];
        if (route.size() < 2) continue;
        const auto& wire = wires[i];
        const auto c = wireColor(wire.selection.kind);
        const bool selected = wire_.kind == wire.selection.kind && wire_.index == wire.selection.index;
        const auto lineColor = selected ? IM_COL32(250,250,255,255) : c;
        const float thickness = selected ? 3.5f : std::clamp(4*zoom_,1.0f,2.0f);
        const auto& rounded = roundedRoutes_[i];
        stroke(rounded,lineColor,thickness);
        for (const auto& [pin,bit] : {std::pair{wire.from,wire.fromBit},std::pair{wire.to,wire.toBit}}) {
            const auto end = routeEnd(*pin,bit);
            if (!samePoint(end.port,end.edge)) bitTails[pin].push_back({end,lineColor,thickness});
        }
        if (wire.selection.kind != 1) {
            const auto tip = pointAlong(route,0.76f);
            // Keep the arrow on a straight leg, even when the nominal tip is near a bend.
            for (std::size_t j = 1; j < route.size(); ++j) {
                if (manhattan(route[j-1],tip)+manhattan(tip,route[j]) > manhattan(route[j-1],route[j])+0.001f) continue;
                const auto delta = minus(route[j],route[j-1]);
                const float length = manhattan(route[j-1],route[j]);
                const auto unit = scale(delta,1/length);
                const float arrowSize = std::min(20.0f,length/3);
                const float trim = std::min(cornerRadius,length/4);
                const auto head = plus(route[j-1],scale(unit,std::clamp(manhattan(route[j-1],tip),trim+arrowSize,length-trim)));
                const auto base = minus(head,scale(unit,arrowSize));
                const ImVec2 side{-unit.y*arrowSize/2,unit.x*arrowSize/2};
                dl->AddTriangleFilled(screen(head),screen(plus(base,side)),screen(minus(base,side)),lineColor);
                break;
            }
        }
        if (hovered && hoverBlock.empty()) {
            for (std::size_t j = 1; j < rounded.size(); ++j) {
                const float dist = segmentDistance(io.MousePos,screen(rounded[j-1]),screen(rounded[j]));
                if (dist < closest) { closest = dist; hoverWire = wire.selection; }
            }
        }
    }
    std::vector<Bounds> labelBounds;
    for (std::size_t i = 0; i < wires.size(); ++i) {
        if (routes_[i].size() < 2) continue;
        const auto [kind,index] = wires[i].selection;
        std::string label = "analog";
        if (kind == 1) {
            const int width = config.digital[index].width;
            label = std::to_string(width) + (width == 1 ? " bit" : " bits");
        } else if (kind == 3) {
            const auto& device = config.devices[index];
            char range[64];
            std::snprintf(range,sizeof(range),"XDATA %04X-%04X",device.address,device.address+emu51PeripheralBusSize(device.kind)-1);
            label = range;
        }
        const bool selected = wire_.kind == kind && wire_.index == index;
        if (!selected && 18*zoom_ < 8) continue;
        const float fontSize = selected ? std::max(10.0f,18*zoom_) : 18*zoom_;
        const auto extent = ImGui::GetFont()->CalcTextSizeA(fontSize,FLT_MAX,0,label.c_str());
        const ImVec2 half{extent.x/2+4,extent.y/2+3};
        for (const float fraction : {0.5f,0.34f,0.66f,0.2f,0.8f}) {
            const auto middle = screen(pointAlong(roundedRoutes_[i],fraction));
            const Bounds bounds{minus(middle,half),plus(middle,half)};
            if (std::any_of(blocks_.begin(),blocks_.end(),[&](const auto& block) {
                const ImVec2 margin{4,4};
                return intersects(bounds,{minus(screen(block.position),margin),plus(screen(plus(block.position,block.size)),margin)});
            }) || std::any_of(labelBounds.begin(),labelBounds.end(),[&](const auto& b) { return intersects(bounds,b); })) continue;
            labelBounds.push_back(bounds);
            dl->AddRectFilled(bounds.lo,bounds.hi,IM_COL32(21,26,35,235),3);
            dl->AddText(ImGui::GetFont(),fontSize,plus(bounds.lo,{4,3}),wireColor(kind),label.c_str());
            break;
        }
    }
    for (const auto& b : blocks_) {
        const auto lo = screen(b.position), hi = screen(plus(b.position,b.size));
        dl->AddRectFilled(plus(lo,{3,4}),plus(hi,{3,4}),IM_COL32(0,0,0,65),6*zoom_);
        dl->AddRectFilled(lo,hi,IM_COL32(39,46,59,255),6*zoom_);
        dl->AddRectFilled(lo,screen(plus(b.position,{b.size.x,headerHeight})),b.id == "cpu" ? IM_COL32(43,65,87,255) : IM_COL32(52,64,83,255),6*zoom_);
        dl->AddRect(lo,hi,b.id == selected_ ? IM_COL32(239,243,255,255) : IM_COL32(83,102,126,255),6*zoom_,0,b.id == selected_ ? 2 : 1);
        // Keep long IDs inside the block title instead of covering neighbours.
        const float headerInset = headerPadding*zoom_;
        dl->PushClipRect(plus(lo,{headerInset,0}),minus(hi,{headerInset,0}),true);
        text(plus(b.position,{headerPadding,2}),b.title,IM_COL32(242,245,250,255),titleSize);
        text(plus(b.position,{headerPadding,subtitleTop}),b.subtitle,IM_COL32(179,194,213,255),pinFontSize);
        dl->PopClipRect();
        if (header && hoverBlock == b.id) {
            const float available = b.size.x-2*headerPadding;
            auto* font = ImGui::GetFont();
            if (layoutTextWidth(font,titleSize,b.title) > available || layoutTextWidth(font,pinFontSize,b.subtitle) > available) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(b.title.c_str());
                ImGui::TextUnformatted(b.subtitle.c_str());
                ImGui::EndTooltip();
            }
        }
        for (const auto& p : b.pins) {
            if (const auto tails = bitTails.find(&p); tails != bitTails.end()) {
                for (const auto& tail : tails->second) drawBitTail(tail.end,tail.color,tail.thickness);
            }
            const auto c = color(p.descriptor.kind);
            const auto extent = ImGui::GetFont()->CalcTextSizeA(pinFontSize*zoom_,FLT_MAX,0,p.label.c_str());
            const auto at = plus(p.position,{p.left ? pinPadding : -pinPadding-extent.x/zoom_,-pinFontSize/2});
            dl->PushClipRect({p.left ? lo.x : hi.x-b.pinWidths[1]*zoom_,lo.y},
                {p.left ? lo.x+b.pinWidths[0]*zoom_ : hi.x,hi.y},true);
            text(at,p.label,c,pinFontSize);
            dl->PopClipRect();
            const auto center = anchor(p);
            const auto connectorHalf = ImVec2(6*zoom_,6*zoom_);
            if (p.descriptor.kind == E51P_ANALOG) dl->AddRectFilled(minus(center,connectorHalf),plus(center,connectorHalf),c,2);
            else dl->AddCircleFilled(center,connectorHalf.x,c);
            for (std::size_t bit = 0; bit < p.bits.size(); ++bit) {
                const auto number = std::to_string(bit);
                const float size = bit < 10 ? 20 : 18;
                const auto numberExtent = ImGui::GetFont()->CalcTextSizeA(size*zoom_,FLT_MAX,0,number.c_str());
                const auto bitCenter = screen(p.bits[bit]);
                const auto half = scale(bitHalf,zoom_);
                dl->AddRectFilled(minus(bitCenter,half),plus(bitCenter,half),IM_COL32(27,35,46,255),2);
                dl->AddRect(minus(bitCenter,half),plus(bitCenter,half),c,2);
                dl->AddText(ImGui::GetFont(),size*zoom_,minus(bitCenter,scale(numberExtent,0.5f)),c,number.c_str());
            }
        }
    }
    if (!hoverPin.name.empty()) {
        const auto* p = pin(hoverPin.name);
        const auto center = anchor(*p,hoverPin.bit);
        if (hoverPin.bit < 0) dl->AddCircle(center,9*zoom_,IM_COL32(255,255,255,255),0,2);
        else {
            const auto half = scale(bitHalf,zoom_);
            dl->AddRect(minus(center,half),plus(center,half),IM_COL32(255,255,255,255),2,0,2);
        }
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(hoverPin.name.c_str());
        ImGui::TextUnformatted(p->label.c_str());
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
        if (const auto* p = hovered ? pin(draggingPin_.name) : nullptr) {
            const auto a = routeEnd(*p,draggingPin_.bit);
            const auto* target = pin(hoverPin.name);
            const auto cursor = world(io.MousePos);
            const RouteEnd b = target ? routeEnd(*target,hoverPin.bit) : RouteEnd{cursor,cursor,false};
            std::vector<Bounds> boxes;
            for (const auto& block : blocks_) boxes.push_back({block.position,plus(block.position,block.size)});
            if (target != p && (target || hoverBlock.empty())) {
                const auto [start,end] = target ? routedTerminals(a.edge,a.left,a.leadLength,b.edge,b.left,b.leadLength,boxes) :
                    std::pair{terminal(a.edge,a.left,boxes,a.leadLength),std::optional{Terminal{cursor,cursor,0}}};
                const auto route = start && end ?
                    WireRouter(boxes,{start->lead,end->lead}).route(*start,*end) : std::vector<ImVec2>{};
                if (!route.empty()) {
                    const auto c = color(p->descriptor.kind);
                    stroke(roundCorners(route),c,2.5f);
                    drawBitTail(a,c,2.5f);
                    drawBitTail(b,c,2.5f);
                }
            }
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
            "CD4511 BCD decoder", "74LS47 BCD decoder", "MAX7219", "MAX7221", "HT16K33", "TM1637",
            "RAM", "ROM", "Clock", "Address Decoder", "8-bit bus output latch", "8-bit bus input buffer", "Configurable key matrix",
            "Bit Inverter", "SSD1306 OLED", "DS1307 real-time clock", "MCP23017 GPIO expander", "W25Q64 SPI flash", "Logic analyzer", "24LC256 EEPROM", "ULN2003", "Four-phase stepper", "ADC0809"};
        const bool hasAudio = std::any_of(config.devices.begin(),config.devices.end(),[](const auto& d) { return d.kind == Emu51PeripheralKind::Audio; });
        for (int kind = 0; kind <= static_cast<int>(Emu51PeripheralKind::Adc0809); ++kind) {
            if (!ImGui::MenuItem(kinds[kind],nullptr,false,kind != static_cast<int>(Emu51PeripheralKind::Audio) || !hasAudio)) continue;
            int suffix = 1; std::string id;
            do { id = "device" + std::to_string(suffix++); } while (std::any_of(config.devices.begin(),config.devices.end(),[&](const auto& d) { return d.id == id; }));
            Emu51PeripheralDevice device; device.id = id; device.kind = static_cast<Emu51PeripheralKind>(kind);
            device.wiredBus = emu51PeripheralBusSize(device.kind) && emu51CircuitBus(config);
            if (device.kind == Emu51PeripheralKind::BusLatch || device.kind == Emu51PeripheralKind::BusBuffer) device.wiredBus = true;
            if (device.kind == Emu51PeripheralKind::KeyMatrix) { device.rows = 8; device.columns = 2; }
            if (device.kind == Emu51PeripheralKind::Rom) device.memoryFill = 255;
            if (device.kind == Emu51PeripheralKind::Clock) device.clockHz = 12000000;
            if (device.kind == Emu51PeripheralKind::Adc0809) device.conversionTicks = 1200;
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
            positions_.push_back({id,std::clamp(popupPosition_.x/layoutScale,-100000.0f,100000.0f),
                std::clamp(popupPosition_.y/layoutScale,-100000.0f,100000.0f)});
            selected_ = id; wire_ = {};
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Fit")) {
        if (ImGui::MenuItem("All blocks")) { fitBlock_.clear(); fit_ = true; readableFit_ = false; }
        if (ImGui::MenuItem("Selected block",nullptr,false,!selected_.empty())) {
            fitBlock_ = selected_; fit_ = true; readableFit_ = false;
        }
        if (ImGui::MenuItem("Actual size (100%)")) {
            const auto center = scale(canvasSize_,0.5f), at = world(plus(origin_,center));
            zoom_ = 1; pan_ = minus(center,at); fit_ = false; fitBlock_.clear();
        }
        ImGui::Separator(); ImGui::TextDisabled("Zoom: %.0f%%",zoom_*100);
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Arrange blocks")) {
        arrange(config);
        fitBlock_.clear(); fit_ = readableFit_ = true;
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
    const auto header = ImGui::GetCursorScreenPos();
    const float closeSize = ImGui::GetFontSize();
    const float right = std::min(header.x+ImGui::GetContentRegionAvail().x,
        ImGui::GetCurrentWindowRead()->ClipRect.Max.x-ImGui::GetStyle().FramePadding.x);
    ImGui::TextUnformatted("Inspector");
    if (right >= header.x+closeSize) {
        if (ImGui::CloseButton(ImGui::GetID("##close-inspector"),{right-closeSize,header.y})) {
            inspectorVisible_ = false;
            interacting_ = true;
            return;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide Inspector");
    }
    ImGui::Separator();
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
            ImGui::TextWrapped("ADDR: 16 address bits. DATA: 8 data bits. /PSEN reads CODE, /RD reads XDATA, /WR writes XDATA. CLK accepts a Clock source. These are functional transaction signals.");
            ImGui::TextDisabled("Choose CPU model in Execution settings.");
        } else {
            const auto found = std::find_if(config.devices.begin(),config.devices.end(),[&](const auto& d) { return d.id == id; });
            if (found != config.devices.end()) {
                auto& d = *found;
                ImGui::TextWrapped("%s",d.id.c_str()); ImGui::TextWrapped("%s",emu51PeripheralKindName(d.kind)); ImGui::Separator();
                ImGui::PushItemWidth(110);
                if (emu51PeripheralBusSize(d.kind)) ImGui::Checkbox("Wire address/data/control pins",&d.wiredBus);
                if (d.kind == Emu51PeripheralKind::BusLatch) {
                    ImGui::TextWrapped("Writes store DATA in Q and its complement Q_n. CS defines address aliases. Leave bus OE inactive: this output latch cannot drive a bus read.");
                    ImGui::InputDouble("Pulse duration (ms)",&d.pulseMs,1,10,"%.3f");
                    ImGui::TextWrapped("Each write retriggers PULSE/PULSE_n. Zero disables the pulse; stored Q is retained after expiry.");
                }
                if (d.kind == Emu51PeripheralKind::BusBuffer)
                    ImGui::TextWrapped("Reads sample IN with weak pull-ups. CS defines address aliases. Leave WE inactive: the buffer has no writable state.");
                if (d.kind == Emu51PeripheralKind::KeyMatrix || d.kind == Emu51PeripheralKind::Keypad) keypadInspector(d);
                if (d.kind == Emu51PeripheralKind::I8255) {
                    if (!d.wiredBus) ImGui::InputInt("XDATA base",&d.address,0,0,ImGuiInputTextFlags_CharsHexadecimal);
                    ImGui::TextWrapped("Four registers selected by ADDR bits 0/1: A, B, C, control. RESET is active high.");
                }
                if (d.kind == Emu51PeripheralKind::Ram || d.kind == Emu51PeripheralKind::Rom) {
                    ImGui::InputScalar("Capacity (bytes)",ImGuiDataType_U64,&d.memoryCapacity);
                    ImGui::InputInt("Power-on fill (hex)",&d.memoryFill,0,0,ImGuiInputTextFlags_CharsHexadecimal);
                    ImGui::TextWrapped("One continuous physical byte array. Higher address pins can be driven by GPIO, 8255 ports or latches.");
                    if (ImGui::TreeNode("Power-on binary images")) {
                        for (std::size_t i = 0; i < d.memoryImages.size();) {
                            auto& image = d.memoryImages[i]; ImGui::PushID(static_cast<int>(i));
                            char path[4096]; std::snprintf(path,sizeof(path),"%s",image.path.c_str());
                            if (ImGui::InputText("Image path",path,sizeof(path))) image.path = path;
                            ImGui::InputScalar("Physical offset",ImGuiDataType_U64,&image.offset);
                            ImGui::InputScalar("Image length",ImGuiDataType_U64,&image.length);
                            const bool remove = ImGui::SmallButton("Remove image"); ImGui::PopID();
                            if (remove) d.memoryImages.erase(d.memoryImages.begin()+static_cast<std::ptrdiff_t>(i)); else ++i;
                        }
                        if (d.memoryImages.size() < 32 && ImGui::SmallButton("Add initialization image")) d.memoryImages.push_back({"",0,d.memoryCapacity});
                        ImGui::TextWrapped("Applied on power cycle. Save Project does not overwrite image files.");
                        ImGui::TreePop();
                    }
                    auto& range = memoryRanges_[d.id];
                    ImGui::Checkbox("Whole device",&range.whole);
                    if (!range.whole) {
                        ImGui::InputScalar("Physical offset",ImGuiDataType_U64,&range.offset);
                        ImGui::InputScalar("Length (bytes)",ImGuiDataType_U64,&range.length);
                    }
                    const auto offset = range.whole ? 0 : range.offset, length = range.whole ? d.memoryCapacity : range.length;
                    const bool valid = length && offset <= d.memoryCapacity && length <= d.memoryCapacity-offset;
                    const bool active = std::find(activeMemories_.begin(),activeMemories_.end(),d.id) != activeMemories_.end();
                    ImGui::BeginDisabled(!valid || !active);
                    if (ImGui::Button("Load binary...")) memoryRequest_ = MemoryRequest{d.id,false,offset,length};
                    if (ImGui::Button("Save binary...")) memoryRequest_ = MemoryRequest{d.id,true,offset,length};
                    ImGui::EndDisabled();
                    if (!valid) ImGui::TextWrapped("Choose a nonempty range inside the physical capacity.");
                    if (!active) ImGui::TextWrapped("Apply before using binary Load/Save. New/type/capacity changes recreate this storage; wiring/Clock edits preserve unchanged chips.");
                    ImGui::TextWrapped("Load/Save uses physical storage without changing selector pins.");
                }
                if (d.kind == Emu51PeripheralKind::Clock) {
                    ImGui::InputDouble("Oscillator (Hz)",&d.clockHz,1000000,12000000,"%.0f");
                    ImGui::TextWrapped("Connect OUT to cpu.CLK. Apply resets and pauses the CPU, preserving unchanged external memory.");
                }
                if (d.kind == Emu51PeripheralKind::Inverter)
                    ImGui::TextWrapped("OUT is the inverse of the single digital bit at IN.");
                if (d.kind == Emu51PeripheralKind::AddressDecoder) {
                    ImGui::InputInt("Input bits (1..32)",&d.decoderBits,0);
                    ImGui::TextWrapped("IN bit 0 is the least significant bit. Each input value may activate exactly one output.");
                    for (std::size_t i = 0; i < d.decoderOutputs.size();) {
                        auto& output = d.decoderOutputs[i];
                        const auto key = d.id+"."+output.id;
                        ImGui::PushID(output.id.c_str()); ImGui::SeparatorText(output.id.c_str());
                        char name[129]; std::snprintf(name,sizeof(name),"%s",output.name.c_str());
                        if (ImGui::InputText("Output name",name,sizeof(name))) output.name = name;
                        ImGui::Checkbox("Active low (/CS)",&output.activeLow);
                        auto [it,inserted] = decoderText_.try_emplace(key);
                        auto& buffer = it->second;
                        if (inserted) {
                            std::string text;
                            for (auto value : output.values) { char token[16]; std::snprintf(token,sizeof(token),"%Xh ",value); text += token; }
                            buffer.assign(text.begin(),text.end()); buffer.resize(text.size()+4096,0);
                        }
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::InputTextMultiline("##input-values",buffer.data(),buffer.size(),{-1,90})) {
                            std::vector<std::uint32_t> values; std::string error;
                            if (emu51ParseDecoderValues(buffer.data(),values,error)) { output.values = std::move(values); decoderErrors_.erase(key); }
                            else decoderErrors_[key] = error;
                        }
                        ImGui::TextDisabled("Values: decimal or hexadecimal (0x10, 10h).");
                        auto& newValue = decoderNewValues_[key];
                        ImGui::InputScalar("New value (hex)",ImGuiDataType_U32,&newValue,nullptr,nullptr,"%X",ImGuiInputTextFlags_CharsHexadecimal);
                        ImGui::BeginDisabled(decoderErrors_.contains(key));
                        if (ImGui::SmallButton("Add value")) {
                            output.values.push_back(newValue); decoderText_.erase(key);
                        }
                        ImGui::EndDisabled();
                        if (ImGui::TreeNode("Assigned values and bits")) {
                            ImGui::BeginChild("value-bits",{0,120},ImGuiChildFlags_Borders);
                            ImGuiListClipper clipper; clipper.Begin(static_cast<int>(output.values.size()));
                            std::optional<std::size_t> erase;
                            while (clipper.Step()) for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                                const auto value = output.values[static_cast<std::size_t>(row)];
                                std::string bits;
                                for (int bit = std::clamp(d.decoderBits,1,32)-1; bit >= 0; --bit) bits += (value >> bit)&1 ? '1' : '0';
                                ImGui::PushID(row); ImGui::Text("%Xh  %s",value,bits.c_str()); ImGui::SameLine();
                                ImGui::BeginDisabled(decoderErrors_.contains(key));
                                if (ImGui::SmallButton("Remove")) erase = static_cast<std::size_t>(row);
                                ImGui::EndDisabled(); ImGui::PopID();
                            }
                            if (erase) { output.values.erase(output.values.begin()+static_cast<std::ptrdiff_t>(*erase)); decoderText_.erase(key); }
                            ImGui::EndChild(); ImGui::TreePop();
                        }
                        const bool remove = ImGui::SmallButton("Remove output"); ImGui::PopID();
                        if (remove) {
                            const auto endpoint = d.id+"."+output.id;
                            std::erase_if(config.digital,[&](const auto& w) { return w.a == endpoint || w.b == endpoint; });
                            decoderText_.erase(key); decoderErrors_.erase(key);
                            decoderNewValues_.erase(key);
                            d.decoderOutputs.erase(d.decoderOutputs.begin()+static_cast<std::ptrdiff_t>(i));
                        } else ++i;
                    }
                    if (d.decoderOutputs.size() < 32 && ImGui::Button("Add output")) {
                        unsigned suffix = 0; std::string name;
                        do { name = "CS"+std::to_string(suffix++); } while (std::any_of(d.decoderOutputs.begin(),d.decoderOutputs.end(),[&](const auto& o) { return o.id == name; }));
                        d.decoderOutputs.push_back({name,"/"+name,true,{}});
                    }
                    auto error = emu51DecoderError(d);
                    if (error.empty()) error = validationError();
                    if (!error.empty()) ImGui::TextWrapped("%s",error.c_str());
                }
                if (d.wiredBus || d.kind == Emu51PeripheralKind::Ram || d.kind == Emu51PeripheralKind::Rom) {
                    ImGui::Checkbox("CS active low",&d.csLow); ImGui::Checkbox("OE active low",&d.oeLow);
                    if (d.kind != Emu51PeripheralKind::Rom) ImGui::Checkbox("WE active low",&d.weLow);
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
                if (const auto imageSize = emu51PeripheralEepromSize(d.kind)) {
                    ImGui::InputInt("I2C address (50..57)",&d.i2cAddress,0,0,ImGuiInputTextFlags_CharsHexadecimal);
                    char path[4096]; std::snprintf(path,sizeof(path),"%s",d.storagePath.c_str());
                    if (ImGui::InputText("Image file",path,sizeof(path))) d.storagePath = path;
                    ImGui::TextWrapped("Optional %u-byte image. Committed bytes save on Pause or exit. Release SDA for ACK/read cycles. WP high protects writes.",imageSize);
                }
                if (d.kind == Emu51PeripheralKind::Ssd1306 || d.kind == Emu51PeripheralKind::Mcp23017) {
                    int address = d.serialAddress ? d.serialAddress : d.kind == Emu51PeripheralKind::Ssd1306 ? 0x3c : 0x20;
                    if (ImGui::InputInt("I2C address",&address,0,0,ImGuiInputTextFlags_CharsHexadecimal)) d.serialAddress = address;
                }
                if (d.kind == Emu51PeripheralKind::Ds1307) {
                    ImGui::TextUnformatted("I2C address 68h");
                    ImGui::InputInt("Days since 2000-01-01",&d.rtcDay);
                    ImGui::InputInt("Seconds since midnight",&d.rtcSecond);
                }
                if (d.kind == Emu51PeripheralKind::Adc0809) {
                    ImGui::InputInt("Conversion ticks",&d.conversionTicks,1,12);
                    ImGui::InputDouble("Full scale (V)",&d.referenceVolts,0.1,1.0,"%.3f");
                    ImGui::TextWrapped("Pulse ALE to latch ADDR, then pulse START. EOC goes high when conversion completes. OE high drives DATA.");
                }
                if (d.kind == Emu51PeripheralKind::Uln2003)
                    ImGui::TextWrapped("A high input sinks the corresponding output. Other outputs are open.");
                if (d.kind == Emu51PeripheralKind::Stepper)
                    ImGui::TextWrapped("Wire four active-low coil drivers in A, B, C, D order. The shaft uses 200 full steps per turn.");
                if (d.kind == Emu51PeripheralKind::W25q64) {
                    char path[4096]; std::snprintf(path,sizeof(path),"%s",d.storagePath.c_str());
                    if (ImGui::InputText("Image file",path,sizeof(path))) d.storagePath = path;
                    ImGui::TextWrapped("Optional 8 MiB image. Committed bytes save on Pause or exit.");
                }
                if (d.kind == Emu51PeripheralKind::Ay38910) {
                    if (!d.wiredBus) ImGui::InputInt("XDATA base",&d.address,0,0,ImGuiInputTextFlags_CharsHexadecimal);
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
void Emu51PeripheralEditor::keypadInspector(Emu51PeripheralDevice& d)
{
    if (d.kind == Emu51PeripheralKind::KeyMatrix) {
        ImGui::InputInt("Electrical scan rows (1..8)",&d.rows,0);
        ImGui::InputInt("Electrical columns (1..4)",&d.columns,0);
        ImGui::InputInt("Independent keys (0..8)",&d.directKeys,0);
        ImGui::TextWrapped("Scanned contacts use bit = row * columns + column. Independent contacts follow them and drive direct output bits without scanning.");
    }
    const int count = emu51KeyCount(d);
    if (count < 1 || count > 32) { ImGui::TextWrapped("Choose electrical dimensions and independent keys totaling at most 32 contacts."); return; }
    ImGui::SeparatorText("Keypad appearance");
    ImGui::TextWrapped("Layout positions select existing contacts. Rearranging or renaming keys preserves their electrical codes. Empty positions are not clickable.");
    bool custom = d.keyLayoutColumns != 0 || d.keyLayoutRows != 0 || !d.keyLayout.empty();
    const auto reset = [&] {
        d.keyLayoutColumns = 4; d.keyLayoutRows = (count+3)/4;
        d.keyLayout.assign(static_cast<std::size_t>(d.keyLayoutColumns*d.keyLayoutRows),-1);
        for (int key = 0; key < count; ++key) d.keyLayout[static_cast<std::size_t>(key)] = key;
    };
    if (ImGui::Checkbox("Custom key layout",&custom)) {
        if (custom) reset(); else { d.keyLayout.clear(); d.keyLayoutColumns = d.keyLayoutRows = 0; }
    }
    if (custom) {
        int columns = d.keyLayoutColumns, rows = d.keyLayoutRows;
        bool changed = ImGui::InputInt("Layout columns (1..8)",&columns,0);
        changed |= ImGui::InputInt("Layout rows (1..8)",&rows,0);
        if (changed) {
            columns = std::clamp(columns,1,8); rows = std::clamp(rows,1,8);
            std::vector<int> layout(static_cast<std::size_t>(columns*rows),-1);
            for (int row = 0; row < std::min(rows,d.keyLayoutRows); ++row)
                for (int col = 0; col < std::min(columns,d.keyLayoutColumns); ++col) {
                    const auto old = static_cast<std::size_t>(row*d.keyLayoutColumns+col);
                    if (old < d.keyLayout.size()) layout[static_cast<std::size_t>(row*columns+col)] = d.keyLayout[old];
                }
            d.keyLayout = std::move(layout); d.keyLayoutColumns = columns; d.keyLayoutRows = rows;
        }
        const bool fits = d.keyLayoutColumns >= 1 && d.keyLayoutColumns <= 8 && d.keyLayoutRows >= 1 && d.keyLayoutRows <= 8 &&
            d.keyLayoutColumns*d.keyLayoutRows >= count;
        ImGui::BeginDisabled(!fits);
        if (ImGui::SmallButton("Place all contacts in order")) {
            d.keyLayout.assign(static_cast<std::size_t>(d.keyLayoutColumns*d.keyLayoutRows),-1);
            for (int key = 0; key < count; ++key) d.keyLayout[static_cast<std::size_t>(key)] = key;
        }
        ImGui::EndDisabled();
        if (d.keyLayoutColumns >= 1 && d.keyLayoutColumns <= 8 && d.keyLayoutRows >= 1 && d.keyLayoutRows <= 8 &&
            d.keyLayout.size() == static_cast<std::size_t>(d.keyLayoutColumns*d.keyLayoutRows) &&
            ImGui::BeginTable("key-layout",d.keyLayoutColumns,ImGuiTableFlags_Borders|ImGuiTableFlags_SizingStretchSame)) {
            for (std::size_t cell = 0; cell < d.keyLayout.size(); ++cell) {
                ImGui::TableNextColumn(); ImGui::PushID(static_cast<int>(cell)); ImGui::SetNextItemWidth(-1);
                const int assigned = d.keyLayout[cell];
                const auto preview = assigned < 0 ? std::string("Empty") : emu51KeyLabel(d,static_cast<unsigned>(assigned));
                if (ImGui::BeginCombo("##contact",preview.c_str())) {
                    for (int key = -1; key < count; ++key) {
                        const auto label = key < 0 ? std::string("Empty") : emu51KeyLabel(d,static_cast<unsigned>(key))+" ["+std::to_string(key)+"]";
                        if (ImGui::Selectable(label.c_str(),assigned == key)) {
                            if (key >= 0) {
                                const auto other = std::find(d.keyLayout.begin(),d.keyLayout.end(),key);
                                if (other != d.keyLayout.end()) *other = assigned;
                            }
                            d.keyLayout[cell] = key;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Row %d, column %d. Selecting an already placed contact swaps the positions.",
                    static_cast<int>(cell)/d.keyLayoutColumns+1,static_cast<int>(cell)%d.keyLayoutColumns+1);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    if (ImGui::TreeNode("Key labels and function legends")) {
        ImGui::TextWrapped("Labels and legends change the display; firmware determines each key's function.");
        for (int key = 0; key < count; ++key) {
            ImGui::PushID(key); ImGui::Text("Contact %d%s",key,d.kind == Emu51PeripheralKind::KeyMatrix && key >= d.rows*d.columns ? " (independent)" : "");
            char label[64], legend[64]; std::snprintf(label,sizeof(label),"%s",emu51KeyLabel(d,static_cast<unsigned>(key)).c_str());
            std::snprintf(legend,sizeof(legend),"%s",static_cast<std::size_t>(key) < d.keyLegends.size() ? d.keyLegends[static_cast<std::size_t>(key)].c_str() : "");
            if (ImGui::InputText("Label",label,sizeof(label))) { d.keyLabels.resize(static_cast<std::size_t>(count)); d.keyLabels[static_cast<std::size_t>(key)] = label; }
            if (ImGui::InputText("Legend",legend,sizeof(legend))) { d.keyLegends.resize(static_cast<std::size_t>(count)); d.keyLegends[static_cast<std::size_t>(key)] = legend; }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}

std::string Emu51PeripheralEditor::validationError() const
{
    return decoderErrors_.empty() ? std::string{} : decoderErrors_.begin()->second;
}
