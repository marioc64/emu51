// SPDX-License-Identifier: LGPL-2.1-only
#include "emu51/displays.h"
#include "emu51/registry.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
namespace {
void check(bool yes,const char* message) { if (!yes) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); } }
void ok(e51p_status status) { if (status!=E51P_OK) { std::fprintf(stderr,"SDK status %u\n",status); std::exit(1); } }
struct Graph {
    const e51p_api* api=nullptr; e51p_context* c=nullptr; e51p_device host{}; uint64_t time=0; unsigned count=0;
    Graph() { ok(e51p_get_api(E51P_ABI_VERSION,&api)); ok(api->create(1000000,&c)); e51p_device_desc d{}; d.struct_size=sizeof(d);d.instance_id="host";ok(api->add_device(c,&d,&host)); }
    ~Graph() { api->destroy(c); }
    e51p_endpoint pin(unsigned width=1) { const auto name="p"+std::to_string(count++); e51p_endpoint_desc d{sizeof(d),name.c_str(),E51P_DIGITAL,E51P_INPUT|E51P_OUTPUT,width,0,0,0}; e51p_endpoint p;ok(api->add_endpoint(c,host,&d,&p));return p; }
    e51p_display* add(unsigned kind,std::array<e51p_endpoint,8>& pins,const char* id="display",e51p_display_config* settings=nullptr) { auto cfg=e51p_display_config_init(kind);e51p_display* out=nullptr;ok(e51p_add_display(api,c,id,settings ? settings : &cfg,&out,pins.data(),8));return out; }
    void wire(e51p_endpoint a,e51p_endpoint b,unsigned width=1) { ok(api->connect_digital(c,a,0,b,0,width)); }
    void drive(e51p_endpoint p,uint32_t value,uint32_t mask=1,uint32_t drain=0) { ok(api->drive(c,p,value,mask,drain)); }
    uint32_t sample(e51p_endpoint p) { e51p_digital_state s{};ok(api->sample(c,p,&s));check(!s.floating&&!s.contention,"Bus input is defined");return s.value; }
    void advance(uint64_t ticks) { time+=ticks;ok(api->advance_to(c,time)); }
    e51p_segment_snapshot view(e51p_display* d) { e51p_segment_snapshot s{};s.struct_size=sizeof(s);ok(e51p_display_snapshot(d,&s));return s; }
};
void rawDisplays() {
    for (unsigned kind : {E51P_DISPLAY_7SEG,E51P_DISPLAY_14SEG,E51P_DISPLAY_16SEG,E51P_DISPLAY_BAR}) for (int anode : {0,1}) {
        Graph g; std::array<e51p_endpoint,8> pins{}; auto cfg=e51p_display_config_init(kind);cfg.common_anode=anode;
        auto* d=g.add(kind,pins,"display",&cfg);const unsigned width=kind==E51P_DISPLAY_7SEG ? 8 : kind==E51P_DISPLAY_14SEG ? 15 : kind==E51P_DISPLAY_16SEG ? 17 : 16;
        const auto segments=g.pin(width),commons=g.pin(8);g.wire(segments,pins[0],width);if (kind!=E51P_DISPLAY_BAR) g.wire(commons,pins[1],8);
        ok(g.api->start(g.c));const uint32_t mask=(1u<<width)-1;g.drive(segments,anode ? 0 : mask,mask);g.drive(commons,anode ? 1 : 254,255);g.advance(20000);
        auto s=g.view(d);const unsigned visible=kind==E51P_DISPLAY_BAR ? 10 : width;
        check(s.segments==visible&&s.instantaneous[0]==((1u<<visible)-1),"Raw segment widths and both electrical polarities are modeled");
        check(std::abs(s.brightness[0][visible-1]-float(1-std::exp(-1)))<1e-6,"Persistence integrates emulated segment on-time");
        const auto again=g.view(d);check(!std::memcmp(&s,&again,sizeof(s)),"Passive snapshots freeze exactly while simulation time is paused");
        ok(g.api->reset(g.c));g.time=0;s=g.view(d);check(s.brightness[0][0]==0,"Reset clears display persistence");
    }
    Graph g;std::array<e51p_endpoint,8> p{};auto cfg=e51p_display_config_init(E51P_DISPLAY_7SEG);cfg.colon=1;
    auto* d=g.add(E51P_DISPLAY_7SEG,p,"scan",&cfg);const auto seg=g.pin(8),com=g.pin(8),colon=g.pin(2);g.wire(seg,p[0],8);g.wire(com,p[1],8);g.wire(colon,p[2],2);
    ok(g.api->start(g.c));g.drive(seg,127,255);g.drive(colon,3,3);
    for (unsigned frame=0;frame<50;++frame) for (unsigned digit=0;digit<4;++digit) { g.drive(com,255u^(1u<<digit),255);g.advance(1000); }
    auto s=g.view(d);for (unsigned digit=0;digit<4;++digit) check(s.brightness[digit][0]>0.22f&&s.brightness[digit][0]<0.28f,"Multiplexed digits retain their quarter-duty brightness");
    check(s.colon_brightness[0]>0.22f&&s.colon_brightness[1]>0.22f,"Colon LEDs follow digit one's common");
    g.drive(com,255,255);g.advance(200000);s=g.view(d);check(s.brightness[0][0]<0.0001f,"Unrefreshed display segments decay on the emulated timeline");
}
void decoders() {
    for (unsigned kind : {E51P_DISPLAY_CD4511,E51P_DISPLAY_LS47}) {
        Graph g;std::array<e51p_endpoint,8> p{};auto* d=g.add(kind,p);const auto bcd=g.pin(4),control=g.pin(3);g.wire(bcd,p[0],4);
        for (unsigned bit=0;bit<3;++bit) ok(g.api->connect_digital(g.c,control,bit,p[bit+1],0,1));
        ok(g.api->start(g.c));g.drive(control,kind==E51P_DISPLAY_CD4511 ? 6 : 7,7);g.drive(bcd,3,15);
        check(g.view(d).programmed[0]==0x4f,"BCD decoders display decimal three");
        if (kind==E51P_DISPLAY_CD4511) {
            g.drive(control,7,7);g.drive(bcd,5,15);check(g.view(d).programmed[0]==0x4f,"4511 LE holds its previously latched digit");
            g.drive(control,6,7);check(g.view(d).programmed[0]==0x6d,"4511 transparent mode accepts new BCD data");
            g.drive(control,2,7);check(!g.view(d).programmed[0],"4511 blanking disables segments");g.drive(control,0,7);check(g.view(d).programmed[0]==127,"4511 lamp test overrides blanking");
            g.drive(control,6,7);g.drive(bcd,10,15);check(!g.view(d).programmed[0],"4511 blanks non-decimal BCD values");
        } else {
            g.drive(bcd,0,15);g.drive(control,3,7);check(!g.view(d).programmed[0]&&!g.sample(p[5]),"LS47 ripple blanking suppresses zero and asserts RBO");
            g.drive(control,2,7);check(g.view(d).programmed[0]==127&&g.sample(p[5]),"LS47 lamp test lights all segments and releases ripple blanking");
            g.drive(control,0,7);check(!g.view(d).programmed[0],"LS47 explicit blanking overrides lamp test");
            g.drive(control,7,7);g.drive(bcd,6,15);check(g.sample(p[4])==(127u^0x7c),"LS47 uses active-low open-collector segment outputs and its tailless six font");
        }
    }
}
struct Max {
    Graph g;std::array<e51p_endpoint,8> p{},q{};e51p_display* first; e51p_display* second;
    e51p_endpoint din,clk,load;
    Max(unsigned kind) : first(g.add(kind,p,"first")),second(g.add(kind,q,"second")),din(g.pin()),clk(g.pin()),load(g.pin()) {
        g.wire(din,p[0]);g.wire(p[3],q[0]);for (const auto& pins : {p,q}) {g.wire(clk,pins[1]);g.wire(load,pins[2]);}ok(g.api->start(g.c));g.drive(clk,0);g.drive(load,1);
    }
    void words(uint16_t a,uint16_t b,bool selected=true) { if (selected) g.drive(load,0);const uint32_t stream=(uint32_t(b)<<16)|a;
        for (int bit=31;bit>=0;--bit) { g.drive(din,(stream>>bit)&1);g.drive(clk,1);g.drive(clk,0); }
        if (!selected) g.drive(load,0);g.drive(load,1);
    }
    void both(unsigned reg,unsigned value) { words(uint16_t((reg<<8)|value),uint16_t((reg<<8)|value)); }
};
void maxControllers() {
    for (unsigned kind : {E51P_DISPLAY_MAX7219,E51P_DISPLAY_MAX7221}) {
        Max m(kind);m.both(1,0x7f);m.words(0x0c01,0x0c01,false);
        check((m.g.view(m.first).brightness[0][0]>0)==(kind==E51P_DISPLAY_MAX7219),"MAX7221 gates shifting by CS; MAX7219 shifts independently of LOAD");
        m.both(12,1);m.both(11,7);m.both(10,15);m.both(9,255);m.words(0x0182,0x0105);
        auto a=m.g.view(m.first), b=m.g.view(m.second);
        check(a.programmed[0]==0xdb&&b.programmed[0]==0x6d,"Chained MAX devices latch distinct words with Code-B decode and decimal point");
        const float duty=kind==E51P_DISPLAY_MAX7219 ? 31.0f/256 : 15.0f/128;
        check(std::abs(a.brightness[0][0]-duty)<1e-6,"MAX intensity duty cycles differ correctly between 7219 and 7221");
        m.words(0,0x0106);check(m.g.view(m.first).programmed[0]==0xdb&&m.g.view(m.second).programmed[0]==0x7d,"MAX no-op preserves other chips during a chained write");
        m.both(12,0);check(!m.g.view(m.first).programmed[0],"MAX shutdown blanks without discarding digit RAM");m.both(15,1);a=m.g.view(m.first);check(a.programmed[7]==255,"MAX display test overrides shutdown, decode and scan limit");
        m.both(15,0);m.both(12,1);m.both(9,0);m.both(1,0x40);check(m.g.view(m.first).programmed[0]==1,"MAX native bit 6 maps to segment A");
    }
}
struct Serial {
    Graph g;std::array<e51p_endpoint,8> p{};e51p_display* d;bool tm; e51p_endpoint clk,data;
    Serial(bool titan) : d(g.add(titan ? E51P_DISPLAY_TM1637 : E51P_DISPLAY_HT16K33,p)),tm(titan),clk(g.pin()),data(g.pin()) {
        g.wire(clk,p[0]);g.wire(data,p[1]);ok(g.api->start(g.c));sda(1);scl(1);
    }
    void sda(bool high) {g.drive(data,high,1,1);g.advance(5);}
    void scl(bool high) {g.drive(clk,high,1,1);g.advance(5);}
    void start() {sda(1);scl(1);sda(0);scl(0);}
    void stop() {sda(0);scl(1);sda(1);}
    bool send(uint8_t value) {for (unsigned i=0;i<8;++i) {sda((value>>(tm ? i : 7-i))&1);scl(1);scl(0);}sda(1);scl(1);const bool ack=!g.sample(data);scl(0);return ack;}
    uint8_t read(bool ack=false) {uint8_t value=0;sda(1);for (unsigned i=0;i<8;++i) {scl(1);value=uint8_t(value|(g.sample(data)<<(tm ? i : 7-i)));scl(0);}if (!tm) {sda(!ack);scl(1);scl(0);sda(1);}return value;}
    void command(uint8_t value) {start();if (!tm) check(send(0xe0),"HT16K33 acknowledges its I2C write address");check(send(value),"Controller command is acknowledged");stop();}
};
void htController() {
    Serial s(false);s.start();check(!s.send(0xe2),"HT16K33 ignores another I2C address");s.stop();s.command(0x21);s.command(0x81);s.command(0xef);
    s.start();check(s.send(0xe0)&&s.send(0),"HT display RAM pointer accepts zero");for (unsigned i=0;i<16;++i) check(s.send(uint8_t(i+1)),"HT RAM accepts auto-increment writes");s.stop();
    auto frame=s.g.view(s.d);check(frame.programmed[0]==0x0201&&frame.programmed[7]==0x100f,"HT16K33 maps all 16 RAM bytes to eight 16-bit commons");
    s.start();check(s.send(0xe0)&&s.send(15),"HT read pointer selects last RAM byte");s.start();check(s.send(0xe1),"Repeated START selects HT read mode");check(s.read(true)==16&&s.read(false)==1,"HT display RAM reads wrap at 0Fh");s.stop();
    s.command(0x83);s.g.advance(260000);check(!s.g.view(s.d).programmed[0],"HT blink uses simulated time");s.command(0x81);s.command(0xe0);check(std::abs(s.g.view(s.d).brightness[0][0]-1.0f/128)<1e-6,"HT dimming has sixteen duty settings");
    s.command(0xa2);ok(e51p_display_set_keys(s.d,uint64_t(1)<<38));s.g.advance(21000);check(!s.g.sample(s.p[7]),"HT debounced keys assert active-low INT");
    s.start();check(s.send(0xe0)&&s.send(0x40),"HT key RAM pointer accepts 40h");s.start();check(s.send(0xe1),"HT key read address ACK");
    std::array<uint8_t,6> key{};for (unsigned i=0;i<6;++i) key[i]=s.read(i!=5);s.stop();check(key[5]==0x10&&!key[0]&&s.g.sample(s.p[7]),"Reading all six HT key bytes returns the 39th key and clears INT");
    s.command(0x20);check(!s.g.view(s.d).programmed[0],"HT oscillator standby blanks the display");s.command(0x21);check(s.g.view(s.d).programmed[0]==0x0201,"HT standby retains display RAM");
}
void tmController() {
    Serial s(true);s.command(0x40);s.start();check(s.send(0xc0),"TM address command ACK");for (uint8_t value : {1,2,4,8,16,32,64}) check(s.send(value),"TM display data ACK");s.stop();s.command(0x8f);
    auto frame=s.g.view(s.d);check(frame.programmed[0]==1&&frame.programmed[3]==8,"TM LSB-first burst preserves display data and ignores writes past digit five");
    check(std::abs(frame.brightness[0][0]-14.0f/96)<1e-6,"TM maximum duty is 14/16 across six scan positions");
    s.command(0x44);s.start();check(s.send(0xc1)&&s.send(3)&&s.send(7),"TM fixed-address data ACK");s.stop();check(s.g.view(s.d).programmed[1]==7,"TM fixed-address mode overwrites the selected digit");
    ok(e51p_display_set_keys(s.d,uint64_t(1)<<10));s.start();check(s.send(0x42),"TM key-read command ACK");check(s.read()==0xed,"TM key data uses the documented K2/SG3 code");s.stop();
    s.command(0x80);check(!s.g.view(s.d).programmed[0],"TM display-off retains RAM");s.command(0x88);check(s.g.view(s.d).programmed[1]==7,"TM display-on restores RAM contents");
}
void registry() {e51p_registry* r=nullptr;ok(e51p_registry_create(E51P_REGISTRY_ABI_VERSION,&r));ok(e51p_registry_add_builtins(r));for (unsigned kind=0;kind<E51P_DISPLAY_COUNT;++kind) {
    Graph g;const e51p_factory_desc* f=nullptr;ok(e51p_registry_find(r,e51p_display_type_id(kind),&f));std::array<e51p_endpoint,8> pins{};ok(e51p_registry_instantiate(r,f->type_id,g.api,g.c,"device",nullptr,0,pins.data(),8));ok(g.api->start(g.c));check(f->pin_count>0,"Display factory exposes all functional pins");}e51p_registry_destroy(r);}
}
int main() {rawDisplays();decoders();maxControllers();htController();tmController();registry();std::puts("Segment display and controller protocol tests passed");}
