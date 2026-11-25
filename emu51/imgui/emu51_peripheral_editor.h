// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "emu51_peripheral_session.h"
#include "emu51_model.h"
#include "imgui.h"
#include <array>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <utility>

// UI-thread-owned circuit canvas. It edits a draft, never a live SDK context.
class Emu51PeripheralEditor {
public:
    void draw(Emu51PeripheralConfig&, Emu51Model, std::string& message);
    void restore(const std::vector<Emu51PeripheralBlockPosition>&);
    std::vector<Emu51PeripheralBlockPosition> positions(const Emu51PeripheralConfig&) const;
    bool interacting() const { return interacting_; }
    bool inspectorVisible() const { return inspectorVisible_; }
    void setInspectorVisible(bool visible) { inspectorVisible_ = visible; }
    struct MemoryRequest { std::string id; bool save; std::uint64_t offset, length; };
    std::optional<MemoryRequest> takeMemoryRequest() { return std::exchange(memoryRequest_,std::nullopt); }
    void setActiveMemory(const std::vector<std::string>& ids) { activeMemories_ = ids; }
    std::string validationError() const;
    void suspend() {
        interacting_ = false; draggingBlock_.clear(); draggingPin_ = {}; wire_ = {};
        rightGesture_ = rightPanned_ = false;
    }
private:
    friend struct Emu51ImguiAppTestAccess;
    struct Pin {
        Emu51PeripheralPin descriptor;
        std::string label;
        ImVec2 position;
        bool left = false;
        std::vector<ImVec2> bits;
    };
    struct Block {
        std::string id, title, subtitle;
        ImVec2 position, size;
        std::array<float,2> pinWidths{};
        std::vector<Pin> pins;
    };
    struct Connector { std::string name; int bit = -1; };
    struct WireSelection { int kind = 0, index = -1; }; // 1=digital, 2=analog, 3=bus
    struct Connection {
        const Pin *from, *to;
        int fromBit, toBit;
        WireSelection selection;
    };
    struct RouteEnd {
        ImVec2 port, edge;
        bool left;
    };
    std::vector<Emu51PeripheralBlockPosition> positions_;
    std::vector<Block> blocks_;
    std::vector<float> routeGeometry_;
    std::vector<std::vector<ImVec2>> routes_, roundedRoutes_;
    std::string selected_, draggingBlock_;
    WireSelection wire_;
    Connector draggingPin_;
    ImVec2 pan_{24, 24}, origin_{}, canvasSize_{}, blockDragOffset_{};
    ImVec2 popupPosition_{};
    float zoom_ = 0.85f;
    bool fit_ = true, readableFit_ = true, interacting_ = false;
    bool inspectorVisible_ = true;
    bool rightGesture_ = false, rightPanned_ = false;
    std::string fitBlock_;
    std::map<std::string,std::vector<char>> decoderText_;
    std::map<std::string,std::string> decoderErrors_;
    std::map<std::string,std::uint32_t> decoderNewValues_;
    struct MemoryRange { bool whole = true; std::uint64_t offset = 0, length = 0; };
    std::map<std::string,MemoryRange> memoryRanges_;
    std::vector<std::string> activeMemories_;
    std::optional<MemoryRequest> memoryRequest_;
    void rebuild(const Emu51PeripheralConfig&, Emu51Model);
    void moveBlock(Block&, ImVec2);
    void arrange(const Emu51PeripheralConfig&);
    std::vector<Connection> connections(const Emu51PeripheralConfig&) const;
    RouteEnd routeEnd(const Pin&, int bit) const;
    void updateRoutes(const std::vector<Connection>&);
    void canvas(Emu51PeripheralConfig&, std::string&);
    void inspector(Emu51PeripheralConfig&);
    void keypadInspector(Emu51PeripheralDevice&);
    void contextMenu(Emu51PeripheralConfig&, const std::string& message);
    void fitView();
    const Pin* pin(const std::string&) const;
    ImVec2 screen(ImVec2) const;
    ImVec2 world(ImVec2) const;
    ImVec2 anchor(const Pin&, int bit = -1) const;
    bool connect(Emu51PeripheralConfig&, const Connector&, const Connector&, std::string&);
    void removeDevice(Emu51PeripheralConfig&, const std::string&);
    bool rename(const std::string& id, const std::string& name);
};
