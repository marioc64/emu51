// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "emu51_execution_settings.h"
#include "emu51_peripheral_config.h"
#include "../core/emu51_model.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

struct Emu51ProjectProgram {
    std::string path;
    std::uint16_t binaryAddress = 0;
    bool fromEditor = false;
    std::string sourceText;
};

struct Emu51ProjectWatchpoint {
    // 0: direct RAM/SFR changes, 1: external data, 2: program, 3: internal RAM changes.
    int space = 0;
    std::uint16_t address = 0;
    bool read = false, write = true, enabled = true;
};

struct Emu51ProjectEditor {
    bool present = false, open = false, draft = false;
    std::string path, name = "untitled.asm", baseDirectory, text;
    int line = 0, column = 0;
};

struct Emu51Project {
    bool unified = true, debuggerOpen = false, traceEnabled = false;
    bool serialTerminalOpen = false;
    int serialLineEnding = 0, serialFileLineEnding = 0, serialFileLineDelayMs = 0;
    Emu51Model cpuModel = Emu51Model::i8051;
    std::string hardwareModule;
    Emu51PeripheralConfig peripherals;
    std::vector<Emu51PeripheralBlockPosition> peripheralLayout;
    std::vector<Emu51PeripheralLiveBox> peripheralLiveLayout;
    Emu51ExecutionSettings execution;
    std::vector<Emu51ProjectProgram> programs;
    std::string reloadPath;
    std::uint16_t reloadBinaryAddress = 0;
    Emu51ProjectEditor editor;
    std::set<std::uint16_t> breakpoints;
    std::vector<Emu51ProjectWatchpoint> watchpoints;
    std::set<std::string> watches;
};

// Decode/validate before applying anything. Paths returned by load are absolute;
// save stores relative paths where possible. Writes use atomic replacement.
std::optional<Emu51Project> emu51LoadProject(const std::filesystem::path& path, std::string& error);
bool emu51SaveProject(const std::filesystem::path& path, const Emu51Project& project, std::string& error);
