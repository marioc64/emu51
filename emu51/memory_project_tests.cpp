// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_project_build.h"
#include "emu51_peripheral_session.h"
#include "emu51_editor_files.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace {
void check(bool ok, const std::string& what) { if (!ok) { std::fprintf(stderr,"[FAIL] %s\n",what.c_str()); std::exit(1); } }
Emu51AssemblyUnit source(const std::string& id, const std::filesystem::path& path, const std::string& text) {
    Emu51AssemblyUnit unit;
    unit.id = id; unit.path = path.string(); unit.embedded = true;
    unit.editor = {true,true,true,path.string(),path.filename().string(),path.parent_path().string(),text,0,0};
    return unit;
}
}
int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("emu51-memory-project-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    Emu51Project project;
    Emu51PeripheralDevice rom; rom.id = "program_rom"; rom.kind = Emu51PeripheralKind::Rom; rom.memoryCapacity = 524288; rom.memoryFill = 255;
    Emu51PeripheralDevice ram; ram.id = "work_ram"; ram.kind = Emu51PeripheralKind::Ram;
    Emu51PeripheralDevice decoder; decoder.id = "decode"; decoder.kind = Emu51PeripheralKind::AddressDecoder;
    decoder.decoderBits = 32; decoder.decoderOutputs = {{"CS0","/CS_ROM",true,{0,4,UINT32_MAX}},{"CS1","/CS_IO",false,{1,3}}};
    Emu51PeripheralDevice clock; clock.id = "clock"; clock.kind = Emu51PeripheralKind::Clock; clock.clockHz = 12000000;
    project.peripherals.devices = {rom,ram,decoder,clock};
    project.peripherals.digital.push_back({"clock.OUT","cpu.CLK",0,0,1});
    project.units = {source("monitor",root/"monitor.asm","org 0\ndb 12h\norg 10h\ndb 34h\nend\n"),
        source("application",root/"application.asm","org 8000h\nmov a,#42h\nend\n"),
        source("tables",root/"tables.asm","org 4000h\ndb 56h,78h\nend\n")};
    project.activeUnit = "application";
    project.mappings = {{"boot","monitor","program_rom",0,0x2000,0,0,false},
        {"app","application","program_rom",0x8000,65536,0x10000,1,false},
        {"data","tables","work_ram",0x4000,0x4800,0x2000,2,false}};
    auto build = emu51BuildProject(project);
    check(build.success(),"Build explicit mappings: "+build.error);
    check(build.writes.size() == 6,"Only emitted bytes are programmed");
    check(build.units[1].image.bytes.contains(0x8000) && !build.units[1].image.bytes.contains(0),"ORG stays in source address space");
    auto session = Emu51PeripheralSession::create(project.peripherals,12);
    std::string error;
    check(emu51ProgramProject(build,*session,error),error);
    uint8_t value;
    e51p_memory_read(session->memory("program_rom"),0x10000,&value); check(value == 0x74,"Source 8000 maps to physical 10000");
    e51p_memory_read(session->memory("program_rom"),0x10001,&value); check(value == 0x42,"Instruction operand retains encoding");
    e51p_memory_read(session->memory("program_rom"),1,&value); check(value == 255,"Sparse gaps retain fill");
    e51p_memory_read(session->memory("work_ram"),0x2001,&value); check(value == 0x78,"Multiple target devices");
    auto invalid = project;
    invalid.mappings[1].destination = UINT64_MAX;
    check(!emu51PlanProjectLoads(invalid,build) && build.writes.empty(),"Capacity/overflow failure leaves no partial write set");
    invalid = project; invalid.mappings.push_back({"collision","application","program_rom",0x8000,65536,0x10000,3,false});
    build = emu51BuildProject(invalid); check(!build.success(),"Destination overlap rejected");
    invalid.mappings[1].overlay = invalid.mappings[3].overlay = true;
    build = emu51BuildProject(invalid); check(build.success(),"Explicit ordered overlays accepted: "+build.error);
    invalid = project; invalid.mappings.pop_back();
    check(!emu51BuildProject(invalid).success(),"Unmapped emitted bytes rejected");
    invalid = project; invalid.units[1].editor.text = "this is not assembly\n";
    build = emu51BuildProject(invalid); check(!build.success() && build.writes.empty(),"Assembly failure does not program earlier units");
    e51p_memory_read(session->memory("program_rom"),0x10001,&value); check(value == 0x42,"Failed preparations preserve active machine");

    const auto chip = session->snapshotMemory("program_rom",0x10000,2);
    check(emu51WriteTextFileAtomically(root/"image.bin",std::string_view(reinterpret_cast<const char*>(chip.data()),chip.size())),"Write binary snapshot");
    check(Emu51PeripheralSession::readBinary(root/"image.bin",2) == chip,"Binary round trip");
    bool rejected = false; try { (void)Emu51PeripheralSession::readBinary(root/"image.bin",3); } catch (...) { rejected = true; }
    check(rejected,"Exact binary length checked before loading");
    auto changed = project.peripherals; changed.devices[0].memoryImages = {{(root/"missing.bin").string(),0,2}};
    auto preserved = Emu51PeripheralSession::create(changed,24,session.get());
    check(preserved->memory("program_rom") == session->memory("program_rom"),"Apply retains storage identity and does not reread changed initialization recipe");
    check(e51p_memory_revision(preserved->memory("program_rom")) == e51p_memory_revision(session->memory("program_rom")),"Apply retains revision");
    project.peripherals.devices[0].memoryImages = {{(root/"image.bin").string(),0x20000,2}};
    check(emu51SaveProject(root/"machine.emu51",project,error),"Save version 2: "+error);
    const auto loaded = emu51LoadProject(root/"machine.emu51",error);
    check(bool(loaded),"Reload version 2: "+error);
    check(loaded->units.size() == 3 && loaded->activeUnit == "application" && loaded->mappings.size() == 3,"Units, active editor and mapping rows round trip");
    check(loaded->peripherals.devices[2].decoderOutputs == decoder.decoderOutputs,"32-bit decoder assignments and output names round trip");
    check(loaded->peripherals.devices[0].memoryImages[0].path == fs::weakly_canonical(root/"image.bin").string(),"Initialization paths resolve relative to project");
    auto power = Emu51PeripheralSession::create(loaded->peripherals,12);
    e51p_memory_read(power->memory("program_rom"),0x20001,&value); check(value == 0x42,"Power-on binary initialization");
    invalid = *loaded; invalid.peripherals.devices[2].decoderOutputs[1].values.push_back(4);
    check(!emu51SaveProject(root/"bad.emu51",invalid,error),"Persistence rejects decoder collisions");

    Emu51Project inversion;
    Emu51PeripheralDevice memory{"memory",Emu51PeripheralKind::Rom};
    memory.memoryCapacity = 2;
    Emu51PeripheralDevice led{"led",Emu51PeripheralKind::Led};
    led.activeLow = false;
    inversion.peripherals.devices = {memory,led,clock,{"source",Emu51PeripheralKind::Digital},
        {"gpio_not",Emu51PeripheralKind::Inverter},{"address_not",Emu51PeripheralKind::Inverter}};
    inversion.peripherals.digital = {{"clock.OUT","cpu.CLK"},{"source.out","gpio_not.IN",3,0,1},{"gpio_not.OUT","led.in"},
        {"cpu.ADDR","address_not.IN"},{"address_not.OUT","memory.ADDR"},
        {"cpu.PSEN","memory.CS"},{"cpu.PSEN","memory.OE"},{"cpu.DATA","memory.DATA",0,0,8}};
    check(emu51SaveProject(root/"inverter.emu51",inversion,error),"Save inverter circuit: "+error);
    const auto invertedProject = emu51LoadProject(root/"inverter.emu51",error);
    check(invertedProject && invertedProject->peripherals == inversion.peripherals,"Inverter devices and wiring round trip");
    auto inverted = Emu51PeripheralSession::create(invertedProject->peripherals,12);
    bool defined = false;
    check(inverted->ledState("led",defined) && defined,"Inverter drives high for a low input");
    check(inverted->setSource("source",8) && !inverted->ledState("led",defined) && defined,"Inverter drives low for a high input");
    check(inverted->setSource("source",0) && inverted->ledState("led",defined) && defined,"Inverter follows a falling input without advancing time");
    check(inverted->programMemory("memory",0,{0x42,0x43}),"Program two bytes behind an inverted address line");
    check(inverted->readProgram(0,value,true) && value == 0x43 && inverted->readProgram(1,value,true) && value == 0x42,
        "Functional CPU bus reads evaluate the inverter with each virtual address");

    auto includes = project;
    includes.units = {source("main",root/"main.asm","$include(values.inc)\norg 1234h\ndb VALUE\nend\n"),
        source("include",root/"values.inc","VALUE equ 5ah\n")};
    includes.units[1].includeOnly = true;
    includes.mappings = {{"include-test","main","program_rom",0x1234,0x1235,0x30000,0,false}};
    const auto includeBuild = emu51BuildProject(includes);
    check(includeBuild.success() && includeBuild.units.size() == 1 && includeBuild.writes.at({"program_rom",0x30000}).value == 0x5a,
        "Independent unsaved include draft participates without becoming a separate binary: "+includeBuild.error);

    for (bool unified : {false,true}) {
        Emu51Project old; old.unified = unified; old.execution.crystalMHz = 11.0592;
        Emu51PeripheralDevice ppi; ppi.id = "ppi"; ppi.kind = Emu51PeripheralKind::I8255; ppi.address = 0x8000;
        old.peripherals.devices.push_back(ppi);
        check(emu51MigrateProject(old,error),"Migrate legacy memory/MMIO graph: "+error);
        check(old.formatVersion == 2 && old.peripherals.circuitBus && emu51PeripheralClockMHz(old.peripherals,12) == 11.0592,
            "Migration promotes old clock to authoritative graph source");
        auto migrated = Emu51PeripheralSession::create(old.peripherals,12);
        const auto codeId = unified ? "system_memory" : "system_code";
        check(e51p_memory_kind(migrated->memory(codeId)) == E51P_RAM,"Legacy writable CODE semantics preserved");
        check(migrated->programMemory(codeId,0x8000,{0x42}),"Program CODE behind an MMIO address");
        check(migrated->readProgram(0x8000,value,true) && value == 0x42,"MMIO exclusion only applies to XDATA");
        check(migrated->writeData(0x8003,0x80),"Migrated 8255 control window accepts write");
        check(migrated->writeData(0x8001,0x77),"Migrated 8255 data window accepts write");
        check(migrated->readData(0x8001,value,true) && value == 0x77,"MMIO wins over fallback storage");
        uint8_t stored; e51p_memory_read(migrated->memory(codeId),0x8001,&stored);
        check(stored == 0,"Peripheral writes do not touch fallback memory");
        check(migrated->writeData(0x1234,0x55),"Migrated fallback RAM accepts XDATA write");
        migrated->readProgram(0x1234,value,true); check(value == (unified ? 0x55 : 0),"Split/unified alias semantics survive migration");
    }
    Emu51Project split;
    split.unified = false;
    check(emu51MigrateProject(split,error),"Migrate separate CODE and XDATA with no I/O: "+error);
    check(split.peripherals.devices.size() == 4 && split.peripherals.devices[2].kind == Emu51PeripheralKind::Inverter,
        "Legacy split memory uses a bit inverter for the complementary chip select");
    auto splitMemory = Emu51PeripheralSession::create(split.peripherals,12);
    check(splitMemory->programMemory("system_code",0,{0x42}) && splitMemory->writeData(0,0x55) &&
        splitMemory->readProgram(0,value,true) && value == 0x42 && splitMemory->readData(0,value,true) && value == 0x55,
        "Inverted PSEN preserves independent CODE and XDATA storage");

    Emu51Project overlap;
    Emu51PeripheralDevice first; first.id = "first"; first.kind = Emu51PeripheralKind::I8255; first.address = 0x8000;
    auto second = first; second.id = "second";
    overlap.peripherals.devices = {first,second};
    check(!emu51MigrateProject(overlap,error) && !overlap.peripherals.circuitBus && overlap.peripherals.devices.size() == 2,
        "Ambiguous legacy overlap fails transactionally");
    overlap.peripherals.devices.pop_back(); overlap.peripherals.devices[0].address = 0x8001;
    check(!emu51MigrateProject(overlap,error) && !overlap.peripherals.circuitBus,"Unaligned legacy register window requires explicit wiring");
    Emu51Project draft;
    draft.programs.push_back({(root/"loaded.asm").string(),0,true,"org 0\ndb 42h\nend\n"});
    draft.editor = {true,true,true,(root/"unfinished.asm").string(),"unfinished.asm",root.string(),"unfinished source",4,2};
    check(emu51MigrateProject(draft,error) && draft.units.size() == 2 && draft.units.back().includeOnly && draft.activeUnit == "editor" &&
        draft.units.back().editor.text == "unfinished source" && draft.units.back().editor.line == 4 && emu51BuildProject(draft).success(),
        "Migration retains an unloaded editor draft without requiring it to assemble");
    fs::remove_all(root);
    std::puts("Physical programming, sparse build units, collision checks, binary files and project persistence passed.");
}
