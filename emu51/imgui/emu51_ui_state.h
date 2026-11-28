// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct Emu51UiPaneState {
    std::string kind;
    int id = 0;
    std::string target = "none";
    double share = 1, width = 0, height = 0;
    bool followPc = true;
    bool open = true, positioned = false;
    double x = 0, y = 0;
    int memoryKind = 0, address = 0;
    bool collapsed = false;
};

struct Emu51UiLayout {
    double leftWidth = 0.24, rightWidth = 0.24, topHeight = 0.22, bottomHeight = 0.22;
    bool registersVisible = true, peripheralInspector = true;
    std::vector<Emu51UiPaneState> panes{{"disassembly", 1}};
    std::array<bool, 4> collapsedEdgeDocks{}; // Left, right, top, bottom.
};

struct Emu51UiState : Emu51UiLayout {
    std::vector<std::string> recentPrograms, recentProjects;
};

enum class Emu51UiStateLoad { Missing, Loaded, Invalid };
bool emu51DecodeUiState(std::string_view json, Emu51UiState& state);
bool emu51EncodeUiState(const Emu51UiState& state, std::string& json);
Emu51UiStateLoad emu51LoadUiState(const std::filesystem::path& path, Emu51UiState& state);
// Read-only compatibility importer; legacy files are retained after migration.
bool emu51ImportLegacyUiState(const std::filesystem::path& directory, Emu51UiState& state);
