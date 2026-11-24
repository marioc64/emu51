// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "emu51/wired_bus.h"
#include "emu51/devices.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
void check(bool ok, const char* what) { if (!ok) { std::fprintf(stderr,"[FAIL] %s\n",what); std::exit(1); } }
void requireStatus(e51p_status s, const char* operation, int line) {
    if (s != E51P_OK) { std::fprintf(stderr,"[FAIL] line %d: %s returned %u\n",line,operation,s); std::exit(1); }
}
#define require(operation) requireStatus((operation),#operation,__LINE__)
e51p_memory* memory(uint32_t kind, uint64_t bytes, uint8_t fill) {
    const e51p_memory_config config{sizeof(config),kind,bytes,fill};
    e51p_memory* m = nullptr; require(e51p_memory_create(&config,&m)); return m;
}
void storage() {
    auto* m = memory(E51P_ROM,524288,255);
    uint8_t v = 0; require(e51p_memory_read(m,0x7ffff,&v)); check(v == 255,"ROM initialization");
    const uint8_t data[]{0x11,0x22,0x33};
    require(e51p_memory_program(m,0x70001,data,3));
    check(e51p_memory_write(m,0x70001,0) == E51P_UNSUPPORTED,"ROM bus protection");
    check(e51p_memory_program(m,0x7ffff,data,3) == E51P_INVALID_ARGUMENT,"Range rejection");
    check(e51p_memory_fill(m,UINT64_MAX,3,0) == E51P_INVALID_ARGUMENT,"Offset overflow rejection");
    uint8_t out[3]{}; require(e51p_memory_copy(m,0x70001,out,3));
    check(out[0] == 0x11 && out[1] == 0x22 && out[2] == 0x33,"Physical snapshot above 64 KiB");
    check(e51p_memory_revision(m) == 1,"Rejected operations do not change revision");
    require(e51p_memory_program(m,524288,nullptr,0));
    check(e51p_memory_address_bits(m) == 19,"Physical address pin count");
    e51p_memory_retain(m); e51p_memory_release(m); e51p_memory_release(m);
    m = memory(E51P_RAM,3,0); check(e51p_memory_address_bits(m) == 2,"Nonpower capacity pin count");
    check(e51p_memory_write(m,3,42) == E51P_INVALID_ARGUMENT,"Nonpower address bounds");
    require(e51p_memory_write(m,2,42)); e51p_memory_release(m);
}
void decoder() {
    const uint32_t a[]{0,1,2}, b[]{3,7};
    e51p_decoder_output out[]{{sizeof(out[0]),"ROM",1,a,3},{sizeof(out[0]),"IO",0,b,2}};
    e51p_decoder* d = nullptr; require(e51p_decoder_create(3,out,2,&d));
    uint32_t selected; e51p_digital_state level{};
    require(e51p_decoder_select(d,{7,0,0},&selected)); check(selected == 1,"Multiple keys per output");
    require(e51p_decoder_level(d,0,{2,0,0},&level)); check(level.value == 0,"Active low selection");
    require(e51p_decoder_level(d,1,{7,0,0},&level)); check(level.value == 1,"Active high selection");
    check(e51p_decoder_select(d,{6,0,0},&selected) == E51P_NOT_FOUND && selected == E51P_INVALID_ID,"Unassigned key");
    check(e51p_decoder_select(d,{0,1,0},&selected) == E51P_INVALID_STATE,"Floating decoder input");
    check(e51p_decoder_select(d,{0,0,1},&selected) == E51P_INVALID_STATE,"Contended decoder input");
    e51p_decoder_destroy(d);
    const uint32_t duplicate[]{1,1}; out[0].values = duplicate; out[0].value_count = 2;
    check(e51p_decoder_create(3,out,2,&d) == E51P_CONFLICT && !d,"Same-output duplicate rejected");
    out[0].values = b;
    check(e51p_decoder_create(3,out,2,&d) == E51P_CONFLICT,"Cross-output collision rejected");
    const uint32_t outside[]{8}; out[0].values = outside; out[0].value_count = 1;
    check(e51p_decoder_create(3,out,2,&d) == E51P_INVALID_ARGUMENT,"Decoder width range checked");
    const uint32_t wide[]{UINT32_MAX}; out[0].values = wide;
    require(e51p_decoder_create(32,out,1,&d));
    require(e51p_decoder_select(d,{UINT32_MAX,0,0},&selected));
    e51p_decoder_destroy(d);
}
void liveDecoder() {
    const e51p_api* api; require(e51p_get_api(E51P_ABI_VERSION,&api));
    e51p_context* c; require(api->create(1,&c));
    const uint32_t keys[]{1,3};
    const e51p_decoder_output output{sizeof(output),"CS",1,keys,2};
    e51p_decoder* model; require(e51p_decoder_create(2,&output,1,&model));
    e51p_endpoint input,cs; require(e51p_decoder_attach(api,c,"decode",model,&input,&cs,1));
    e51p_decoder_destroy(model); // The context owns its own reference.
    e51p_device_desc d{}; d.struct_size = sizeof(d); d.instance_id = "source";
    e51p_device source; require(api->add_device(c,&d,&source));
    const e51p_endpoint_desc desc{sizeof(desc),"OUT",E51P_DIGITAL,E51P_OUTPUT,2,0,0,0};
    e51p_endpoint drive; require(api->add_endpoint(c,source,&desc,&drive));
    require(api->connect_digital(c,drive,0,input,0,2)); require(api->start(c));
    e51p_digital_state state{}; require(api->sample(c,cs,&state));
    check(state.floating == 1,"Unresolved live decoder releases output");
    require(api->drive(c,drive,1,3,0)); require(api->sample(c,cs,&state));
    check(state.value == 0 && !state.floating,"GPIO drives ordinary decoder output active");
    require(api->drive(c,drive,2,3,0)); require(api->sample(c,cs,&state));
    check(state.value == 1,"Unassigned live input deactivates output");
    e51p_snapshot* snapshot; require(e51p_snapshot_create(c,&snapshot)); require(e51p_snapshot_begin(snapshot));
    require(e51p_snapshot_drive(snapshot,drive,3,3,0,0));
    require(e51p_snapshot_sample(snapshot,input,&state)); check(state.value == 3,"Virtual input override");
    require(api->sample(c,input,&state)); check(state.value == 2,"Virtual override never drives live input");
    e51p_snapshot_destroy(snapshot); api->destroy(c);
}
void electrical() {
    const e51p_api* api; require(e51p_get_api(E51P_ABI_VERSION,&api));
    e51p_context* c; require(api->create(12,&c));
    e51p_wired_bus* bus; e51p_initiator_pins cpu{};
    require(e51p_wired_bus_create(api,c,"cpu",2,&bus,&cpu));
    auto* a = memory(E51P_RAM,3,0); auto* b = memory(E51P_RAM,4,0);
    e51p_bus_pins ap{},bp{}; uint32_t ai,bi;
    require(e51p_wired_bus_add_memory(bus,"a",a,0,0,0,&ap,&ai));
    require(e51p_wired_bus_add_memory(bus,"b",b,0,0,0,&bp,&bi));
    auto* disabled = memory(E51P_ROM,1,255); e51p_bus_pins dp{}; uint32_t di;
    require(e51p_wired_bus_add_memory(bus,"disabled",disabled,1,0,0,&dp,&di));
    const uint32_t unknownKey[]{0}; const e51p_decoder_output unknownOutput{sizeof(unknownOutput),"CS",1,unknownKey,1};
    e51p_endpoint unknownIn,unknownCs;
    require(e51p_wired_bus_add_decoder(bus,"unknown-selector",1,&unknownOutput,1,&unknownIn,&unknownCs));
    require(api->connect_digital(c,unknownCs,0,dp.cs,0,1));
    const uint32_t key[]{0}; const e51p_decoder_output out{sizeof(out),"ENABLE",0,key,1};
    e51p_endpoint rd,oe,wr,we;
    require(e51p_wired_bus_add_decoder(bus,"read",1,&out,1,&rd,&oe));
    require(e51p_wired_bus_add_decoder(bus,"write",1,&out,1,&wr,&we));
    const auto wire = [&](e51p_endpoint x,unsigned xb,e51p_endpoint y,unsigned yb,unsigned n = 1) { require(api->connect_digital(c,x,xb,y,yb,n)); };
    wire(cpu.data_read_n,0,rd,0); wire(cpu.data_write_n,0,wr,0);
    for (const auto& p : {ap,bp}) {
        wire(cpu.address,0,p.address,1); wire(cpu.address,1,p.address,0);
        wire(cpu.data,0,p.data,0,8); wire(oe,0,p.oe,0); wire(we,0,p.we,0);
    }
    e51p_device_desc d{}; d.struct_size = sizeof(d); d.instance_id = "controls";
    e51p_device source; require(api->add_device(c,&d,&source));
    const e51p_endpoint_desc desc{sizeof(desc),"SELECT",E51P_DIGITAL,E51P_OUTPUT,2,0,0,0};
    e51p_endpoint select,conflict,outputEnable; require(api->add_endpoint(c,source,&desc,&select));
    const e51p_endpoint_desc bit{sizeof(bit),"ADDRESS",E51P_DIGITAL,E51P_OUTPUT,1,0,0,0};
    require(api->add_endpoint(c,source,&bit,&conflict)); wire(conflict,0,ap.address,0);
    const e51p_endpoint_desc enableDesc{sizeof(enableDesc),"ENABLE",E51P_DIGITAL,E51P_OUTPUT,1,0,0,0};
    require(api->add_endpoint(c,source,&enableDesc,&outputEnable)); wire(outputEnable,0,dp.oe,0);
    wire(select,0,ap.cs,0); wire(select,1,bp.cs,0);
    require(api->start(c)); require(e51p_wired_bus_compile(bus));
    require(api->drive(c,select,1,3,0));
    e51p_bus_result result{};
    require(e51p_wired_bus_access(bus,E51P_DATA_WRITE,1,0x55,0,&result));
    uint8_t value; require(e51p_memory_read(a,2,&value)); check(value == 0x55,"Permuted address bits and active-high controls");
    require(e51p_wired_bus_access(bus,E51P_DATA_READ,1,0,1,&result)); check(result.value == 0x55,"Permuted read routing");
    require(api->drive(c,select,3,3,0));
    check(e51p_wired_bus_access(bus,E51P_DATA_READ,1,0,0,&result) == E51P_CONTENTION,"Independent chip selects still diagnose competing readers");
    check(e51p_wired_bus_access(bus,E51P_DATA_WRITE,3,0xaa,0,&result) == E51P_INVALID_ARGUMENT,"Selected nonpower address rejected");
    require(e51p_memory_read(b,3,&value)); check(value == 0,"Failed broadcast has no partial write");
    require(e51p_wired_bus_access(bus,E51P_DATA_WRITE,0,0x66,0,&result));
    require(e51p_memory_read(a,0,&value)); check(value == 0x66,"Broadcast RAM write first receiver");
    require(e51p_memory_read(b,0,&value)); check(value == 0x66,"Broadcast RAM write second receiver");
    require(api->drive(c,select,1,3,0)); require(api->drive(c,conflict,1,1,0));
    check(e51p_wired_bus_access(bus,E51P_DATA_WRITE,0,0x77,0,&result) == E51P_CONTENTION,"Contended address suppresses transaction");
    require(e51p_memory_read(a,0,&value)); check(value == 0x66,"Contended write preserves storage");
    require(api->drive(c,conflict,0,0,0)); require(api->drive(c,select,0,3,0));
    check(e51p_wired_bus_access(bus,E51P_DATA_READ,0,0,0,&result) == E51P_NOT_FOUND && result.value == 255,"Unmapped open bus is FF");
    require(api->drive(c,outputEnable,1,1,0));
    check(e51p_wired_bus_access(bus,E51P_DATA_READ,0,0,0,&result) == E51P_INVALID_STATE,"Unresolved CS faults only when read or write is enabled");
    e51p_wired_bus_destroy(bus); api->destroy(c); e51p_memory_release(a); e51p_memory_release(b); e51p_memory_release(disabled);

    require(api->create(12,&c)); require(e51p_wired_bus_create(api,c,"cpu",1,&bus,&cpu));
    e51p_endpoint in,output;
    const e51p_decoder_output empty{sizeof(empty),"CS",0,nullptr,0};
    require(e51p_wired_bus_add_decoder(bus,"feedback",1,&empty,1,&in,&output));
    require(api->connect_digital(c,in,0,output,0,1)); require(api->start(c));
    check(e51p_wired_bus_compile(bus) == E51P_CONFLICT,"Combinational feedback rejected when compiling routing");
    e51p_wired_bus_destroy(bus); api->destroy(c);
}
void circuit() {
    const e51p_api* api; require(e51p_get_api(E51P_ABI_VERSION,&api));
    e51p_context* c; require(api->create(12000000,&c));
    e51p_wired_bus* bus; e51p_initiator_pins cpu{};
    require(e51p_wired_bus_create(api,c,"functional",16,&bus,&cpu));
    auto* rom = memory(E51P_ROM,524288,255); auto* ram = memory(E51P_RAM,16384,0);
    e51p_bus_pins rp{},mp{},ip{}; uint32_t ri,mi,ii;
    require(e51p_wired_bus_add_memory(bus,"rom",rom,1,1,1,&rp,&ri));
    require(e51p_wired_bus_add_memory(bus,"ram",ram,1,1,1,&mp,&mi));
    e51p_i8255_endpoints ppi{}; ppi.struct_size = sizeof(ppi);
    require(e51p_add_i8255_mode0(api,c,"ppi-model",&ppi));
    require(e51p_wired_bus_add_target(bus,"ppi",ppi.bus,2,1,1,1,&ip,&ii));
    const uint32_t rv[]{0,1}, mv[]{2}, iv[]{3};
    const e51p_decoder_output outputs[]{{sizeof(outputs[0]),"ROM",1,rv,2},
        {sizeof(outputs[0]),"RAM",1,mv,1},{sizeof(outputs[0]),"IO",1,iv,1}};
    e51p_endpoint input, selected[3]; require(e51p_wired_bus_add_decoder(bus,"decode",2,outputs,3,&input,selected));
    const auto wire = [&](e51p_endpoint a, unsigned abit, e51p_endpoint b, unsigned bbit, unsigned width) {
        require(api->connect_digital(c,a,abit,b,bbit,width));
    };
    wire(cpu.address,14,input,0,2);
    wire(selected[0],0,rp.cs,0,1); wire(selected[1],0,mp.cs,0,1); wire(selected[2],0,ip.cs,0,1);
    wire(cpu.address,0,rp.address,0,15); wire(cpu.address,0,mp.address,0,14); wire(cpu.address,0,ip.address,0,2);
    wire(cpu.data,0,rp.data,0,8); wire(cpu.data,0,mp.data,0,8); wire(cpu.data,0,ip.data,0,8);
    wire(cpu.code_read_n,0,rp.oe,0,1); wire(cpu.data_read_n,0,ip.oe,0,1); wire(cpu.data_write_n,0,ip.we,0,1);
    wire(cpu.data_write_n,0,mp.we,0,1);
    // RAM is readable from CODE and XDATA using another ordinary decoder.
    const uint32_t read_keys[]{0,1,2};
    const e51p_decoder_output read_output{sizeof(read_output),"READ",1,read_keys,3};
    e51p_endpoint controls, read_pin; require(e51p_wired_bus_add_decoder(bus,"read-enable",2,&read_output,1,&controls,&read_pin));
    wire(cpu.code_read_n,0,controls,0,1); wire(cpu.data_read_n,0,controls,1,1); wire(read_pin,0,mp.oe,0,1);
    e51p_device_desc desc{}; desc.struct_size = sizeof(desc); desc.instance_id = "bank-selector";
    e51p_device source; require(api->add_device(c,&desc,&source));
    const e51p_endpoint_desc bank_desc{sizeof(bank_desc),"bits",E51P_DIGITAL,E51P_OUTPUT,4,0,0,0};
    e51p_endpoint bank; require(api->add_endpoint(c,source,&bank_desc,&bank)); wire(bank,0,rp.address,15,4);
    require(api->start(c)); require(api->drive(c,bank,0,15,0)); require(e51p_wired_bus_compile(bus));
    const uint8_t code0 = 0x12, code9 = 0x90;
    require(e51p_memory_program(rom,0x123,&code0,1)); require(e51p_memory_program(rom,9*32768+0x123,&code9,1));
    const auto access = [&](uint32_t kind, uint32_t address, uint32_t value = 0, bool peek = false) {
        e51p_bus_result result{};
        const auto status = e51p_wired_bus_access(bus,kind,address,value,peek,&result);
        if (status != E51P_OK) std::fprintf(stderr,"bus: %u %s\n",status,e51p_wired_bus_error(bus));
        require(status); return result;
    };
    check(access(E51P_CODE_READ,0x123).value == code0,"ROM read through wired decoder");
    require(api->drive(c,bank,9,15,0));
    check(access(E51P_CODE_READ,0x123).value == code9,"GPIO high bits change physical region");
    check(access(E51P_CODE_READ,0x123).physical_address == 9*32768+0x123,"Physical source identity");
    access(E51P_DATA_WRITE,0x8005,0x5a);
    check(access(E51P_CODE_READ,0x8005).value == 0x5a && access(E51P_DATA_READ,0x8005).value == 0x5a,"Shared CODE/XDATA RAM");
    access(E51P_DATA_WRITE,0xc003,0x80); access(E51P_DATA_WRITE,0xc001,0x69);
    check(access(E51P_DATA_READ,0xc001).value == 0x69,"Decoder selects nonmemory 8255");
    check(access(E51P_DATA_READ,0xfffd).value == 0x69,"Register aliases follow local address pins");
    e51p_bus_result result{};
    check(e51p_wired_bus_access(bus,E51P_DATA_WRITE,0xc003,0xe4,0,&result) == E51P_UNSUPPORTED,"Unsupported peripheral write rejected before commit");
    e51p_digital_state before{},after{};
    require(api->sample(c,cpu.address,&before));
    uint64_t time,hz; require(api->clock(c,&time,&hz));
    for (unsigned i = 0; i < 20; ++i) access(E51P_CODE_READ,0x123,0,true);
    require(api->sample(c,cpu.address,&after));
    uint64_t after_time; require(api->clock(c,&after_time,&hz));
    check(before.value == after.value && before.floating == after.floating && before.contention == after.contention && time == after_time,"Debugger peeks leave live pins and time unchanged");
    require(api->reset(c)); require(api->drive(c,bank,0,15,0));
    check(access(E51P_CODE_READ,0x8005).value == 0x5a,"Context/CPU reset preserves external RAM");
    e51p_wired_bus_destroy(bus); api->destroy(c); e51p_memory_release(rom); e51p_memory_release(ram);
}
}
int main() { storage(); decoder(); liveDecoder(); electrical(); circuit(); std::puts("Memory, decoder, wired peripherals, banking and passive inspection passed."); }
