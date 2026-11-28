// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "emu51_peripheral_config.h"
#include "imgui.h"
#include <functional>
#include <vector>

// Presentation only. Device controls remain owned by the application/session.
class Emu51PeripheralLive {
public:
    struct Device {
        std::string id, title, type;
        ImVec2 size{220,100}, minimum{80,48};
    };
    static bool visible(Emu51PeripheralKind kind);
    void draw(const std::vector<Device>&, const std::function<void(const std::string&,float)>&);
    bool momentaryKeys(const std::string&) const;
    void setMomentaryKeys(const std::string&,bool);
    std::vector<Emu51PeripheralLiveBox> layout(const Emu51PeripheralConfig&) const;
    const std::vector<Emu51PeripheralLiveLabel>& labels() const { return labels_; }
    void restore(const std::vector<Emu51PeripheralLiveBox>&,const std::vector<Emu51PeripheralLiveLabel>& = {});
    bool interacting() const { return interacting_; }
    bool editing() const { return editing_; }
    void suspend() { interacting_ = false; }
private:
    friend struct Emu51ImguiAppTestAccess;
    std::vector<Emu51PeripheralLiveBox> boxes_;
    std::vector<Emu51PeripheralLiveLabel> labels_;
    char labelText_[Emu51PeripheralLiveLabel::textCapacity]{};
    ImVec2 labelPosition_{};
    int labelTarget_ = -1;
    void drawLabels(ImVec2 origin,ImVec2& extent);
    bool editing_ = false, interacting_ = false;
    void arrange(float width);
};
