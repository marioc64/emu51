// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "emu51_peripheral_session.h"
#include "emu51_model.h"
#include "imgui.h"
#include <string>
#include <vector>

// UI-thread-owned circuit canvas. It edits a draft, never a live SDK context.
class Emu51PeripheralEditor {
public:
    void draw(Emu51PeripheralConfig&, Emu51Model, std::string& message);
    void restore(const std::vector<Emu51PeripheralBlockPosition>&);
    std::vector<Emu51PeripheralBlockPosition> positions(const Emu51PeripheralConfig&) const;
    bool interacting() const { return interacting_; }
    bool inspectorVisible() const { return inspectorVisible_; }
    void setInspectorVisible(bool visible) { inspectorVisible_ = visible; }
    void suspend() {
        interacting_ = false; draggingBlock_.clear(); draggingPin_ = {}; wire_ = {};
        rightGesture_ = rightPanned_ = false;
    }
private:
    friend struct Emu51ImguiAppTestAccess;
    struct Pin {
        Emu51PeripheralPin descriptor;
        ImVec2 position;
        bool left = false;
        std::vector<ImVec2> bits;
    };
    struct Block {
        std::string id, title, subtitle;
        ImVec2 position, size;
        std::vector<Pin> pins;
    };
    struct Connector { std::string name; int bit = -1; };
    struct WireSelection { int kind = 0, index = -1; }; // 1=digital, 2=analog, 3=bus
    std::vector<Emu51PeripheralBlockPosition> positions_;
    std::vector<Block> blocks_;
    std::string selected_, draggingBlock_;
    WireSelection wire_;
    Connector draggingPin_;
    ImVec2 pan_{24, 24}, origin_{}, canvasSize_{}, blockDragOffset_{};
    ImVec2 popupPosition_{};
    float zoom_ = 0.85f;
    bool fit_ = true, interacting_ = false;
    bool inspectorVisible_ = true;
    bool rightGesture_ = false, rightPanned_ = false;
    std::string fitBlock_;
    void rebuild(const Emu51PeripheralConfig&, Emu51Model);
    void canvas(Emu51PeripheralConfig&, std::string&);
    void inspector(Emu51PeripheralConfig&);
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
