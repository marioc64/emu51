// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_project_build.h"
#include <algorithm>
#include <stdexcept>

bool emu51MigrateProject(Emu51Project& project, std::string& error)
{
    error.clear();
    if (!project.hardwareModule.empty() || emu51CircuitBus(project.peripherals)) return true;
    try {
        // Validate fixed legacy windows before replacing them with chip-select
        // wiring, which otherwise makes old overlapping addresses disappear.
        emu51ValidatePeripheralConfig(project.peripherals);
        auto next = project;
        auto& c = next.peripherals;
        const auto unique = [&](const std::string& base) {
            auto name = base; unsigned suffix = 1;
            while (std::any_of(c.devices.begin(),c.devices.end(),[&](const auto& d) { return d.id == name; })) name = base+std::to_string(suffix++);
            return name;
        };
        unsigned shift = 16;
        std::vector<std::pair<std::string,int>> io;
        for (auto& d : c.devices) if (const int size = emu51PeripheralBusSize(d.kind)) {
            if (d.address % size) throw std::runtime_error("Legacy device "+d.id+" has an unaligned MMIO window; explicit register address wiring is required");
            shift = std::min(shift,size == 4 ? 2u : 1u);
            d.wiredBus = true; io.emplace_back(d.id,d.address);
        }
        Emu51PeripheralDevice code;
        code.id = unique(next.unified ? "system_memory" : "system_code"); code.kind = Emu51PeripheralKind::Ram;
        c.devices.push_back(code);
        auto dataId = code.id;
        if (!next.unified) {
            Emu51PeripheralDevice data; data.id = unique("system_data"); data.kind = Emu51PeripheralKind::Ram;
            dataId = data.id; c.devices.push_back(data);
        }
        const auto wire = [&](const std::string& a, const std::string& b, int aBit = 0, int bBit = 0, int width = 1) {
            c.digital.push_back({a,b,aBit,bBit,width});
        };
        const auto selectId = unique("system_select");
        const bool splitWithoutIo = !next.unified && io.empty();
        if (splitWithoutIo) {
            c.devices.push_back({selectId,Emu51PeripheralKind::Inverter});
            wire("cpu.PSEN",code.id+".CS");
            wire("cpu.PSEN",selectId+".IN");
            wire(selectId+".OUT",dataId+".CS");
        } else {
            Emu51PeripheralDevice decode{selectId,Emu51PeripheralKind::AddressDecoder};
            decode.decoderBits = static_cast<int>(17-shift);
            decode.decoderOutputs.push_back({"CODE",next.unified ? "/CS_MEMORY" : "/CS_CODE",true,{}});
            if (!next.unified) decode.decoderOutputs.push_back({"DATA","/CS_DATA",true,{}});
            const auto memoryOutputs = decode.decoderOutputs.size();
            for (std::size_t i = 0; i < io.size(); ++i) decode.decoderOutputs.push_back({"IO"+std::to_string(i),"/CS_"+io[i].first,true,{}});
            const unsigned addressValues = 1u << (16-shift);
            for (unsigned value = 0; value < 2*addressValues; ++value) {
                const bool xdata = value >= addressValues;
                const unsigned address = (value % addressValues) << shift;
                std::size_t output = !next.unified && xdata ? 1 : 0;
                if (xdata) for (std::size_t i = 0; i < io.size(); ++i) {
                    const auto device = std::find_if(c.devices.begin(),c.devices.end(),[&](const auto& d) { return d.id == io[i].first; });
                    if (address >= static_cast<unsigned>(io[i].second) && address < static_cast<unsigned>(io[i].second+emu51PeripheralBusSize(device->kind)))
                        output = memoryOutputs+i;
                }
                decode.decoderOutputs[output].values.push_back(value);
            }
            c.devices.push_back(std::move(decode));
            if (shift < 16) wire("cpu.ADDR",selectId+".IN",static_cast<int>(shift),0,static_cast<int>(16-shift));
            wire("cpu.PSEN",selectId+".IN",0,static_cast<int>(16-shift));
            wire(selectId+".CODE",code.id+".CS");
            if (!next.unified) wire(selectId+".DATA",dataId+".CS");
        }
        wire("cpu.ADDR",code.id+".ADDR",0,0,16); wire("cpu.DATA",code.id+".DATA",0,0,8);
        if (next.unified) {
            Emu51PeripheralDevice read; read.id = unique("system_read"); read.kind = Emu51PeripheralKind::AddressDecoder;
            read.decoderBits = 2; read.decoderOutputs = {{"READ","/OE",true,{0,1,2}}};
            wire("cpu.PSEN",read.id+".IN"); wire("cpu.RD",read.id+".IN",0,1);
            wire(read.id+".READ",code.id+".OE"); wire("cpu.WR",code.id+".WE");
            c.devices.push_back(std::move(read));
        } else {
            wire("cpu.PSEN",code.id+".OE");
            wire("cpu.RD",dataId+".OE"); wire("cpu.WR",dataId+".WE");
            wire("cpu.ADDR",dataId+".ADDR",0,0,16); wire("cpu.DATA",dataId+".DATA",0,0,8);
        }
        for (std::size_t i = 0; i < io.size(); ++i) {
            const auto& id = io[i].first;
            const auto d = std::find_if(c.devices.begin(),c.devices.end(),[&](const auto& device) { return device.id == id; });
            wire(selectId+".IO"+std::to_string(i),id+".CS"); wire("cpu.RD",id+".OE"); wire("cpu.WR",id+".WE");
            wire("cpu.DATA",id+".DATA",0,0,8); wire("cpu.ADDR",id+".ADDR",0,0,emu51PeripheralBusSize(d->kind) == 4 ? 2 : 1);
        }
        Emu51PeripheralDevice clock; clock.id = unique("system_clock"); clock.kind = Emu51PeripheralKind::Clock;
        clock.clockHz = next.execution.crystalMHz*1000000.0; wire(clock.id+".OUT","cpu.CLK"); c.devices.push_back(clock);
        c.circuitBus = true;
        if (next.units.empty()) {
            for (std::size_t i = 0; i < next.programs.size(); ++i) {
                const auto& program = next.programs[i];
                Emu51AssemblyUnit unit; unit.id = "program"+std::to_string(i+1); unit.path = program.path;
                unit.embedded = program.fromEditor; unit.binaryAddress = program.binaryAddress;
                unit.editor = {true,true,program.fromEditor,program.path,std::filesystem::path(program.path).filename().string(),
                    std::filesystem::path(program.path).parent_path().string(),program.sourceText,0,0};
                next.mappings.push_back({"load"+std::to_string(i+1),unit.id,code.id,0,65536,0,static_cast<uint32_t>(i),true});
                next.units.push_back(std::move(unit));
            }
            if (!next.units.empty()) next.activeUnit = next.units.front().id;
            if (next.editor.present && !next.units.empty()) {
                const auto& editor = next.editor;
                const bool untitled = editor.path.empty();
                const auto editorSource = untitled ? std::filesystem::path(editor.baseDirectory)/editor.name :
                    std::filesystem::path(editor.path);
                const auto existing = std::find_if(next.units.begin(),next.units.end(),[&](const auto& u) {
                    if (std::filesystem::path(u.path).lexically_normal() != editorSource.lexically_normal()) return false;
                    // A legacy project can store the same embedded source in
                    // both its loaded program and its unnamed editor buffer.
                    return !untitled || (u.embedded && u.editor.text == editor.text);
                });
                if (existing != next.units.end()) {
                    existing->editor = editor; existing->embedded = editor.path.empty();
                    if (!editor.baseDirectory.empty()) existing->baseDirectory = editor.baseDirectory;
                    next.activeUnit = existing->id;
                } else {
                    // An unloaded editor is retained without making its possibly
                    // incomplete draft a required program image during migration.
                    Emu51AssemblyUnit unit; unit.id = "editor";
                    unit.path = editorSource.string();
                    unit.baseDirectory = editor.baseDirectory; unit.embedded = editor.path.empty();
                    unit.includeOnly = true; unit.editor = editor;
                    next.activeUnit = unit.id; next.units.push_back(std::move(unit));
                }
            }
        }
        next.formatVersion = 2;
        emu51ValidatePeripheralConfig(c);
        project = std::move(next);
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
