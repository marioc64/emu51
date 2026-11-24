// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "emu51_project.h"
#include "assembler.h"
#include <map>
class Emu51PeripheralSession;
struct Emu51BuiltUnit {
    std::string id;
    emu51_asm::AssemblyImage image;
    std::vector<emu51_asm::Diagnostic> diagnostics;
};
struct Emu51ProgrammedByte {
    std::uint8_t value;
    std::string unit, mapping;
    std::uint16_t source;
    bool overlay;
};
struct Emu51ProjectBuild {
    std::vector<Emu51BuiltUnit> units;
    std::map<std::pair<std::string,std::uint64_t>,Emu51ProgrammedByte> writes;
    std::string error;
    bool success() const { return error.empty(); }
};
// Preparation never changes live memory, selectors or CPU state.
Emu51ProjectBuild emu51BuildProject(const Emu51Project&);
bool emu51PlanProjectLoads(const Emu51Project&, Emu51ProjectBuild&);
bool emu51ProgramProject(const Emu51ProjectBuild&, Emu51PeripheralSession&, std::string& error);
bool emu51MigrateProject(Emu51Project&, std::string& error);
