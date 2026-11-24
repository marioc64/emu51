// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_project_build.h"
#include "emu51_peripheral_session.h"
#include <algorithm>
#include <set>
#include <stdexcept>

Emu51ProjectBuild emu51BuildProject(const Emu51Project& project)
{
    Emu51ProjectBuild build;
    try {
        std::set<std::string> ids;
        std::map<std::filesystem::path,std::string> drafts;
        for (const auto& unit : project.units) if (unit.embedded || unit.editor.draft) {
            const auto path = std::filesystem::absolute(unit.path).lexically_normal();
            const auto [it,inserted] = drafts.emplace(path,unit.editor.text);
            if (!inserted && it->second != unit.editor.text) throw std::runtime_error("Conflicting editor drafts for "+unit.path);
        }
        for (const auto& unit : project.units) {
            if (unit.id.empty() || !ids.insert(unit.id).second) throw std::runtime_error("Assembly unit IDs must be nonempty and unique");
            if (unit.includeOnly) continue;
            Emu51BuiltUnit result; result.id = unit.id;
            if (emu51_asm::detect_program_file_kind(unit.path) == emu51_asm::ProgramFileKind::assembly_source || unit.embedded) {
                emu51_asm::AssembleOptions options;
                options.source_overrides = drafts;
                for (const auto& path : unit.includeDirectories) options.include_directories.emplace_back(path);
                emu51_asm::Assembler assembler(options);
                auto compiled = unit.embedded || unit.editor.draft ?
                    assembler.assemble_string(unit.path,unit.editor.text,unit.baseDirectory.empty() ? std::filesystem::path(unit.path).parent_path() : std::filesystem::path(unit.baseDirectory)) :
                    assembler.assemble_file(unit.path);
                result.diagnostics = std::move(compiled.diagnostics);
                result.image = std::move(compiled.image);
                build.units.push_back(std::move(result));
                if (!build.units.back().diagnostics.empty()) {
                    const auto& diagnostic = build.units.back().diagnostics.front();
                    throw std::runtime_error(unit.id + ": " + diagnostic.file + ":" + std::to_string(diagnostic.line) + ": " + diagnostic.message);
                }
            } else {
                auto prepared = emu51_asm::prepare_program_file(unit.path,{unit.binaryAddress});
                if (!prepared.result.success) throw std::runtime_error(unit.id + ": " + prepared.result.message);
                result.image = std::move(prepared.image); build.units.push_back(std::move(result));
            }
        }
        emu51PlanProjectLoads(project,build);
    } catch (const std::exception& e) { build.error = e.what(); build.writes.clear(); }
    return build;
}
bool emu51PlanProjectLoads(const Emu51Project& project, Emu51ProjectBuild& build)
{
    build.writes.clear(); build.error.clear();
    try {
        std::vector<Emu51ProgramMapping> rows = project.mappings;
        std::sort(rows.begin(),rows.end(),[](const auto& a, const auto& b) { return a.order < b.order; });
        std::set<std::string> ids;
        std::set<std::uint32_t> orders;
        std::map<std::string,std::set<std::uint16_t>> covered;
        for (const auto& row : rows) {
            if (row.id.empty() || !ids.insert(row.id).second || !orders.insert(row.order).second || row.sourceStart >= row.sourceEnd || row.sourceEnd > 65536)
                throw std::runtime_error("Invalid load mapping ID, order or source interval");
            const auto unit = std::find_if(build.units.begin(),build.units.end(),[&](const auto& u) { return u.id == row.unit; });
            const auto device = std::find_if(project.peripherals.devices.begin(),project.peripherals.devices.end(),[&](const auto& d) { return d.id == row.device; });
            if (unit == build.units.end()) throw std::runtime_error(row.id + ": unknown source unit " + row.unit);
            if (device == project.peripherals.devices.end() || (device->kind != Emu51PeripheralKind::Ram && device->kind != Emu51PeripheralKind::Rom))
                throw std::runtime_error(row.id + ": target must be a RAM or ROM device");
            if (row.destination > device->memoryCapacity || row.sourceEnd-row.sourceStart > device->memoryCapacity-row.destination)
                throw std::runtime_error(row.id + ": destination interval exceeds " + row.device);
            for (auto it = unit->image.bytes.lower_bound(static_cast<std::uint16_t>(row.sourceStart));
                it != unit->image.bytes.end() && it->first < row.sourceEnd; ++it) {
                const auto destination = row.destination + (it->first-row.sourceStart);
                const auto key = std::pair(row.device,destination);
                const auto previous = build.writes.find(key);
                if (previous != build.writes.end() && (!row.overlay || !previous->second.overlay))
                    throw std::runtime_error(row.id + ": destination collision with " + previous->second.mapping + " in " + row.device + " at offset " + std::to_string(destination));
                build.writes.insert_or_assign(key,Emu51ProgrammedByte{it->second,row.unit,row.id,it->first,row.overlay});
                covered[row.unit].insert(it->first);
            }
        }
        for (const auto& unit : project.units) if (!unit.buildOnly && !unit.includeOnly) {
            const auto compiled = std::find_if(build.units.begin(),build.units.end(),[&](const auto& b) { return b.id == unit.id; });
            if (compiled == build.units.end()) throw std::runtime_error("Unit has no build result: " + unit.id);
            for (const auto& [address,value] : compiled->image.bytes) {
                (void)value;
                if (!covered[unit.id].contains(address)) throw std::runtime_error(unit.id + ": unmapped emitted byte at source address " + std::to_string(address));
            }
        }
        return true;
    } catch (const std::exception& e) { build.error = e.what(); build.writes.clear(); return false; }
}
bool emu51ProgramProject(const Emu51ProjectBuild& build, Emu51PeripheralSession& session, std::string& error)
{
    error = build.error;
    if (!error.empty()) return false;
    for (const auto& [key,byte] : build.writes) {
        (void)byte;
        const auto* m = session.memory(key.first);
        if (!m || key.second >= e51p_memory_capacity(m)) { error = "Memory target changed before programming: " + key.first; return false; }
    }
    // No allocation, disk access or fallible device callbacks after preflight.
    for (const auto& [key,byte] : build.writes)
        (void)e51p_memory_program(session.memory(key.first),key.second,&byte.value,1);
    return true;
}
