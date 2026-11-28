// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "emu51_ui_state.h"
#include "kason_schema.h"

namespace emu51_ui_layout {

struct StoredPane {
    char kind[32], target[16];
    int id, followPc, open, positioned, memoryKind, address, collapsed;
    double share, width, height, x, y;
};

struct StoredLayout {
    double leftWidth, rightWidth, topHeight, bottomHeight;
    int registersVisible, peripheralInspector;
    int leftCollapsed, rightCollapsed, topCollapsed, bottomCollapsed;
    StoredPane panes[1024];
    std::size_t paneCount;
};

const kason_schema* schema();
bool decode(const StoredLayout& stored, Emu51UiLayout& layout);
bool encode(const Emu51UiLayout& layout, StoredLayout& stored);

} // namespace emu51_ui_layout
