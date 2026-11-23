// SPDX-License-Identifier: LGPL-2.1-only
#include "emu51/components.h"
#include "emu51/registry.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
void check(bool value, const char* message) { if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); } }
void ok(e51p_status status) { if (status != E51P_OK) { std::fprintf(stderr,"SDK status %u\n",status); std::exit(1); } }
struct Graph {
    const e51p_api* api = nullptr; e51p_context* c = nullptr; e51p_device host{};
    uint64_t time = 0; unsigned counter = 0;
    Graph() { ok(e51p_get_api(E51P_ABI_VERSION,&api)); ok(api->create(1000000,&c));
        e51p_device_desc d{}; d.struct_size=sizeof(d); d.instance_id="host"; ok(api->add_device(c,&d,&host)); }
    ~Graph() { api->destroy(c); }
    e51p_endpoint pin(unsigned width=1, bool bus=false) {
        const auto name="p"+std::to_string(counter++);
        e51p_endpoint_desc d{sizeof(d),name.c_str(),bus ? E51P_BUS_INITIATOR : E51P_DIGITAL,
            bus ? 0u : uint32_t(E51P_INPUT|E51P_OUTPUT),width,bus ? 16u : 0u,0,0};
        e51p_endpoint p; ok(api->add_endpoint(c,host,&d,&p)); return p;
    }
    void wire(e51p_endpoint a, e51p_endpoint b, unsigned width=1) { ok(api->connect_digital(c,a,0,b,0,width)); }
    void drive(e51p_endpoint p, uint32_t value, uint32_t mask=1, uint32_t drain=0) { ok(api->drive(c,p,value,mask,drain)); }
    uint32_t sample(e51p_endpoint p, uint32_t mask=UINT32_MAX) { e51p_digital_state s{}; ok(api->sample(c,p,&s)); check(!((s.floating|s.contention)&mask),"Input is defined"); return s.value; }
    void advance(uint64_t ticks) { time+=ticks; ok(api->advance_to(c,time)); }
    e51p_component* add(unsigned kind, std::array<e51p_endpoint,16>& pins, e51p_component_config* config=nullptr, const char* id="component") {
        auto defaults=e51p_component_config_init(kind); e51p_component* result=nullptr;
        ok(e51p_add_component(api,c,id,config ? config : &defaults,&result,pins.data(),16)); return result;
    }
};
void frontPanel() {
    Graph g; std::array<e51p_endpoint,16> button{}, led{}, keys{};
    auto cfg=e51p_component_config_init(E51P_COMPONENT_BUTTON); cfg.bounce_ticks=5000;
    auto* b=g.add(E51P_COMPONENT_BUTTON,button,&cfg,"button");
    auto* l=g.add(E51P_COMPONENT_LED,led,nullptr,"led"); g.wire(button[0],led[0]);
    auto* k=g.add(E51P_COMPONENT_KEYPAD,keys,nullptr,"keys"); const auto rows=g.pin(4); g.wire(rows,keys[0],4);
    ok(g.api->start(g.c)); int on=9, defined=0;
    ok(e51p_component_get_led(l,&on,&defined)); check(defined&&!on,"Active-low LED starts off");
    ok(e51p_component_set_pressed(b,1)); ok(e51p_component_get_led(l,&on,&defined)); check(on,"Button drives an LED");
    g.advance(1000); ok(e51p_component_get_led(l,&on,&defined)); check(!on,"Button bounce uses simulation time");
    g.advance(4000); ok(e51p_component_get_led(l,&on,&defined)); check(on,"Bounce settles at the requested contact");
    ok(e51p_component_set_pressed(b,0)); g.advance(5000); check(g.sample(button[0])==1,"Released button leaves the pull-up");
    ok(e51p_component_set_keys(k,(1u<<1)|(1u<<11)));
    g.drive(rows,14,15); check(g.sample(keys[1])==13,"Keypad row zero returns column one");
    g.drive(rows,11,15); check(g.sample(keys[1])==7,"Keypad row two returns column three");
    g.drive(rows,15,15); check(g.sample(keys[1])==15,"Unselected rows release all columns");
}
void shiftRegisters() {
    Graph g; std::array<e51p_endpoint,16> a{}, b{}, in{};
    g.add(E51P_COMPONENT_595,a,nullptr,"a"); g.add(E51P_COMPONENT_595,b,nullptr,"b"); g.add(E51P_COMPONENT_165,in,nullptr,"in");
    const auto serial=g.pin(), clock=g.pin(), latch=g.pin(), enable=g.pin(), clear=g.pin();
    g.wire(serial,a[0]); g.wire(a[6],b[0]);
    for (const auto& p : {a,b}) { g.wire(clock,p[1]); g.wire(latch,p[2]); g.wire(enable,p[4]); g.wire(clear,p[3]); }
    const auto input=g.pin(8), inputClock=g.pin(), load=g.pin(), inhibit=g.pin();
    g.wire(input,in[0],8); g.wire(inputClock,in[2]); g.wire(inhibit,in[3]); g.wire(load,in[4]);
    ok(g.api->start(g.c)); g.drive(clock,0); g.drive(latch,0); g.drive(clear,1); g.drive(enable,0);
    const uint32_t word=0xa35c;
    for (int bit=15; bit>=0; --bit) { g.drive(serial,(word>>bit)&1); g.drive(clock,1); g.drive(clock,0); }
    check(g.sample(a[5])==0,"595 data stays hidden until the storage clock");
    g.drive(latch,1); g.drive(latch,0);
    check(g.sample(a[5])==0x5c && g.sample(b[5])==0xa3,"Cascaded 595s shift MSB first and latch both bytes");
    g.drive(clear,0); check(g.sample(a[5])==0x5c && g.sample(a[6])==0,"595 clear affects shift state, not the output latch");
    g.drive(enable,1); e51p_digital_state state{}; ok(g.api->sample(g.c,a[5],&state)); check(state.floating==255,"595 OE tri-states its outputs");
    g.drive(input,0x96,255); g.drive(inputClock,0); g.drive(inhibit,0); g.drive(load,0); g.drive(load,1);
    uint32_t result=0;
    for (int bit=0; bit<8; ++bit) { result=(result<<1)|g.sample(in[5]); check(g.sample(in[6])==(g.sample(in[5])^1),"165 complemented serial output"); g.drive(inputClock,1); g.drive(inputClock,0); }
    check(result==0x96,"165 parallel load is read MSB first");
}
struct Lcd {
    Graph g; std::array<e51p_endpoint,16> p{}; e51p_component* handle;
    e51p_endpoint data,rs,rw,enable; bool four=false;
    Lcd() : handle(g.add(E51P_COMPONENT_LCD,p)),data(g.pin(8)),rs(g.pin()),rw(g.pin()),enable(g.pin()) {
        g.wire(data,p[0],8); g.wire(rs,p[1]); g.wire(rw,p[2]); g.wire(enable,p[3]); ok(g.api->start(g.c));
        g.drive(enable,0); g.drive(rw,0); g.drive(rs,0);
    }
    void pulse(uint8_t value) { g.drive(data,value,255); g.drive(enable,1); g.drive(enable,0); }
    void write(uint8_t value, bool character=false, bool wait=true) {
        g.drive(rs,character); g.drive(rw,0);
        if (four) { pulse(value&240); pulse(uint8_t(value<<4)); } else pulse(value);
        if (wait) g.advance(value==1||value==2 ? 1600 : 40);
    }
    uint8_t read(bool character=false) {
        g.drive(rs,character); g.drive(data,0,0); g.drive(rw,1); g.drive(enable,1);
        uint8_t value=uint8_t(g.sample(data,four ? 240 : 255)); g.drive(enable,0);
        if (four) { g.drive(enable,1); value=uint8_t((value&240)|(g.sample(data,four ? 240 : 255)>>4)); g.drive(enable,0); }
        g.drive(rw,0); return value;
    }
};
void lcd() {
    Lcd l; l.write(0x38); l.write(0x0f); l.write(0x80); l.write('H',true); l.write('i',true);
    e51p_lcd_snapshot state{}; state.struct_size=sizeof(state); ok(e51p_component_get_lcd(l.handle,&state));
    check(state.display_on && state.cells[0]=='H' && state.cells[1]=='i' && state.cursor_cell==2,"8-bit LCD displays written characters and cursor");
    l.write(1,false,false); check(l.read()&128,"LCD exposes busy during clear");
    l.write('X',true,false); l.g.advance(1600); l.write(0x80); check(l.read(true)==' ',"LCD ignores writes while busy");
    l.write(0x20); l.four=true; l.write(0x28); l.write(0x80); l.write('4',true); l.write('b',true);
    l.write(0x80); check(l.read(true)=='4' && l.read(true)=='b',"4-bit reads and writes use two high-first nibbles");
    l.write(0x40); for (unsigned i=0;i<8;++i) l.write(uint8_t(i),true);
    l.write(0x80); l.write(0,true); ok(e51p_component_get_lcd(l.handle,&state));
    check(state.cells[0]==0 && state.cgram[7]==7,"LCD custom glyph RAM is available to the host renderer");
    l.write(0x18); ok(e51p_component_get_lcd(l.handle,&state)); check(state.cells[0]=='b',"LCD display shift changes the visible window");
}
struct I2c {
    Graph g; std::array<e51p_endpoint,16> pins{}; e51p_component* device;
    e51p_endpoint scl,sda,wp;
    I2c() : device(g.add(E51P_COMPONENT_EEPROM,pins)),scl(g.pin()),sda(g.pin()),wp(g.pin()) {
        g.wire(scl,pins[0]); g.wire(sda,pins[1]); g.wire(wp,pins[2]);
        std::array<uint8_t,256> image{}; image.fill(255); image[0x40]=0x96; image[0x41]=0x35;
        ok(e51p_component_eeprom_import(device,image.data(),256));
        ok(g.api->start(g.c)); lineSda(1); lineScl(1); g.drive(wp,0);
    }
    void lineSda(bool high) { g.drive(sda,high,1,1); g.advance(5); }
    void lineScl(bool high) { g.drive(scl,high,1,1); g.advance(5); }
    void start() { lineSda(1); lineScl(1); lineSda(0); lineScl(0); }
    void stop() { lineSda(0); lineScl(1); lineSda(1); }
    bool send(uint8_t value) {
        for (int i=7;i>=0;--i) { lineSda((value>>i)&1); lineScl(1); lineScl(0); }
        lineSda(1); lineScl(1); const bool ack=!g.sample(sda); lineScl(0); return ack;
    }
    uint8_t read(bool ack) {
        lineSda(1); uint8_t value=0;
        for (int i=0;i<8;++i) { lineScl(1); value=uint8_t((value<<1)|g.sample(sda)); lineScl(0); }
        lineSda(!ack); lineScl(1); lineScl(0); lineSda(1); return value;
    }
};
void eeprom() {
    I2c i; i.start(); check(!i.send(0xa2),"Other I2C addresses are not acknowledged"); i.stop();
    i.start(); check(i.send(0xa0)&&i.send(0x40),"EEPROM accepts address and word address");
    i.start(); check(i.send(0xa1),"Repeated START selects EEPROM read");
    check(i.read(true)==0x96 && i.read(false)==0x35,"Random and sequential reads honor master ACK/NACK"); i.stop();
    i.start(); check(i.send(0xa0)&&i.send(7)&&i.send(0x12)&&i.send(0x34),"EEPROM page write bytes are acknowledged"); i.stop();
    i.start(); check(!i.send(0xa0),"EEPROM NACKs during the write cycle"); i.stop(); i.g.advance(5000);
    std::array<uint8_t,256> image{}; ok(e51p_component_eeprom_export(i.device,image.data(),256));
    check(image[7]==0x12 && image[0]==0x34 && image[8]==255,"Eight-byte EEPROM pages wrap without crossing page boundaries");
    i.g.drive(i.wp,1); i.start(); check(i.send(0xa0)&&i.send(0)&&i.send(0x77),"Write-protected EEPROM still ACKs"); i.stop(); i.g.advance(5000);
    ok(e51p_component_eeprom_export(i.device,image.data(),256)); check(image[0]==0x34,"WP prevents programming at STOP");
    ok(i.g.api->reset(i.g.c)); ok(e51p_component_eeprom_export(i.device,image.data(),256)); check(image[7]==0x12,"EEPROM survives CPU/graph reset");
    i.g.time=0; i.g.advance(1);
    check(e51p_component_eeprom_import(i.device,image.data(),256)==E51P_INVALID_STATE,"A running EEPROM cannot be replaced behind the simulated bus");
}
void ay() {
    Graph g; std::array<e51p_endpoint,16> p{}; g.add(E51P_COMPONENT_AY,p);
    const auto bus=g.pin(8,true), data=g.pin(8), control=g.pin(2);
    g.wire(data,p[1],8);
    ok(g.api->connect_digital(g.c,control,0,p[2],0,1));
    ok(g.api->connect_digital(g.c,control,1,p[3],0,1));
    ok(g.api->map_bus(g.c,bus,0x9000,2,p[0],0)); ok(g.api->start(g.c));
    const auto write=[&](unsigned reg,unsigned value) { ok(g.api->bus_write(g.c,bus,0x9000,reg)); ok(g.api->bus_write(g.c,bus,0x9001,value)); };
    double out=0; ok(g.api->analog_read(g.c,p[8],&out)); check(out==2.5,"AY silence is centered for host audio");
    write(0,125); write(1,0); write(7,0x3e); write(8,15);
    unsigned edges=0; bool high=false;
    for (unsigned i=0;i<1000;++i) { g.advance(10); ok(g.api->analog_read(g.c,p[8],&out)); check(out>=0.1&&out<=4.9,"AY output stays within audio span"); const bool next=out>2.5; if (next!=high) ++edges; high=next; }
    check(edges>=19&&edges<=21,"2 MHz / (16*125) produces a 1 kHz tone");
    write(8,0); g.advance(100); ok(g.api->analog_read(g.c,p[8],&out)); check(out==2.5,"AY volume zero mutes output");
    write(14,0x5a); write(7,0x7f); check(g.sample(p[6])==0x5a,"AY register 7 controls its output ports");
    write(1,255); uint32_t value=0; ok(g.api->bus_read(g.c,bus,0x9001,E51P_PEEK,&value)); check(value==15,"AY register masks and passive register reads");
    write(8,16); write(11,1); write(12,0); write(13,9); g.advance(3000); ok(g.api->analog_read(g.c,p[8],&out)); check(out==2.5,"AY one-shot envelope holds at zero");
    g.drive(control,0,3); g.drive(data,1,255); g.drive(control,3,3); g.drive(control,0,3);
    g.drive(data,0xab,255); g.drive(control,1,3); g.drive(control,0,3); g.drive(data,0,0);
    g.drive(control,2,3); check(g.sample(data)==11,"Native AY control pins latch, write and read the same register bank"); g.drive(control,0,3);
    write(7,0x37); write(8,15); write(9,0); write(10,0); write(6,7);
    bool low=false, highNoise=false;
    for (unsigned i=0;i<1000;++i) { g.advance(10); ok(g.api->analog_read(g.c,p[8],&out)); low |= out<2.5; highNoise |= out>2.5; }
    check(low&&highNoise,"AY noise generator feeds the per-channel mixer");
    write(7,0x3f); write(8,16); write(11,1); write(12,0);
    for (unsigned shape=0;shape<16;++shape) {
        write(13,shape); double minimum=5, maximum=0;
        for (unsigned step=0;step<32;++step) { g.advance(128); ok(g.api->analog_read(g.c,p[8],&out)); minimum=std::min(minimum,out); maximum=std::max(maximum,out); }
        if (!(shape&8)||(shape&1)) {
            const bool full=(shape&8) && ((shape&4) ? !(shape&2) : (shape&2));
            check(std::abs(out-(full ? 3.3 : 2.5))<1e-8,"AY envelope hold/alternate/attack behavior matches each one-shot shape");
        } else check(maximum-minimum>0.79,"Each repeating AY envelope shape continues through its full range");
    }
    write(8,15); write(9,15); write(10,15); ok(g.api->analog_read(g.c,p[8],&out));
    check(out<=4.9&&out>=0.1,"Three full-volume AY channels remain inside the host audio span");
}
void registry() {
    e51p_registry* r=nullptr; ok(e51p_registry_create(E51P_REGISTRY_ABI_VERSION,&r)); ok(e51p_registry_add_builtins(r));
    const char* ids[]{"emu51.led","emu51.switch","emu51.button","emu51.hd44780","emu51.keypad4x4","emu51.74hc595","emu51.74hc165","emu51.24c02","emu51.ay38910"};
    for (const auto* id : ids) {
        Graph g; const e51p_factory_desc* desc=nullptr; ok(e51p_registry_find(r,id,&desc)); std::array<e51p_endpoint,16> pins{};
        ok(e51p_registry_instantiate(r,id,g.api,g.c,"device",nullptr,0,pins.data(),16)); ok(g.api->start(g.c));
        check(desc->pin_count>0,"Each new factory exposes its modeled pins");
    }
    e51p_registry_destroy(r);
}
}
int main() { std::puts("Front panel"); frontPanel(); std::puts("Shift registers"); shiftRegisters(); std::puts("LCD"); lcd(); std::puts("EEPROM"); eeprom(); std::puts("AY"); ay(); registry(); std::puts("Component protocol tests passed"); }
