// SPDX-License-Identifier: LGPL-2.1-only
#include "emu51/serial_devices.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {
void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr,"[FAIL] %s\n",what);
        std::exit(1);
    }
}
class Circuit {
public:
    const e51p_api* api = nullptr;
    e51p_context* context = nullptr;
    e51p_serial_device* chip = nullptr;
    std::array<e51p_endpoint,7> pins{}, master{};
    uint64_t tick = 0;
    Circuit(unsigned kind, unsigned outputs) {
        check(e51p_get_api(E51P_ABI_VERSION,&api)==E51P_OK,"Load SDK");
        check(api->create(1000000,&context)==E51P_OK,"Create 1 MHz circuit");
        const auto cfg = e51p_serial_config_init(kind);
        check(e51p_add_serial_device(api,context,"chip",&cfg,&chip,pins.data(),static_cast<uint32_t>(pins.size()))==E51P_OK,"Add chip");
        e51p_device_desc desc{};
        desc.struct_size = sizeof(desc); desc.instance_id = "master";
        e51p_device device;
        check(api->add_device(context,&desc,&device)==E51P_OK,"Add master");
        uint32_t count = 0;
        e51p_serial_pins(kind,&count);
        constexpr const char* names[]{"p0","p1","p2","p3","p4","p5","p6"};
        for (unsigned i = 0; i < count; ++i) {
            e51p_endpoint_desc port{sizeof(port),names[i],E51P_DIGITAL,
                static_cast<uint32_t>(outputs&(1u<<i) ? E51P_INPUT|E51P_OUTPUT : E51P_INPUT),1,0,0,0};
            check(api->add_endpoint(context,device,&port,&master[i])==E51P_OK,"Add master pin");
            check(api->connect_digital(context,master[i],0,pins[i],0,1)==E51P_OK,"Wire master pin");
        }
        check(api->start(context)==E51P_OK,"Start circuit");
    }
    ~Circuit() { api->destroy(context); }
    void set(unsigned pin, bool high, bool openDrain = false) {
        check(api->drive(context,master[pin],high,1,openDrain)==E51P_OK,"Drive master pin");
    }
    e51p_digital_state state(unsigned pin) const {
        e51p_digital_state state{};
        check(api->sample(context,master[pin],&state)==E51P_OK,"Read pin state");
        return state;
    }
    void advance(uint64_t delta) {
        tick += delta;
        check(api->advance_to(context,tick)==E51P_OK,"Advance circuit");
    }
    unsigned exchange(unsigned tx) {
        unsigned rx = 0;
        for (unsigned i = 0; i < 8; ++i) {
            set(2,(tx&(0x80u>>i))!=0);
            set(1,true);
            rx = (rx<<1)|state(3).value;
            set(1,false);
        }
        return rx;
    }
    void spi(std::initializer_list<unsigned> tx) {
        set(0,false);
        for (const auto byte : tx) exchange(byte);
        set(0,true);
    }
    unsigned spiRead(std::initializer_list<unsigned> tx) {
        set(0,false);
        for (const auto byte : tx) exchange(byte);
        const auto result = exchange(0);
        set(0,true);
        return result;
    }
    void start() { set(1,true,true); set(0,true,true); set(1,false,true); set(0,false,true); }
    void stop() { set(1,false,true); set(0,true,true); set(1,true,true); }
    void send(unsigned byte) {
        for (unsigned i = 0; i < 8; ++i) {
            set(1,(byte&(0x80u>>i))!=0,true);
            set(0,true,true); set(0,false,true);
        }
        set(1,true,true); set(0,true,true);
        check(!state(1).value,"I2C slave ACKs");
        set(0,false,true);
    }
};
void flash() {
    Circuit c(E51P_W25Q64,7);
    c.set(1,false);
    c.spi({2,0,0,0xff,0x00});
    check(c.spiRead({3,0,0,0xff})==0xff,"Program requires WEL");
    c.spi({6});
    check(c.spiRead({5})==2,"Write enable sets WEL");
    c.spi({2,0,0,0xff,0xa5,0x5a});
    check(c.spiRead({5})==1 && c.state(3).floating,"Busy clears WEL and CS releases MISO");
    check(!e51p_flash_revision(c.chip),"Pending writes are not exported");
    c.advance(3000);
    check(c.spiRead({3,0,0,0xff})==0xa5 && c.spiRead({3,0,0,0})==0x5a &&
        c.spiRead({3,0,1,0})==0xff,"Page program wraps within 256 bytes");
    c.spi({6}); c.spi({2,0,0,0xff,0xf0}); c.advance(3000);
    check(c.spiRead({0x0b,0,0,0xff,0})==0xa0,"Program can only clear bits and fast read skips its dummy byte");
    c.spi({6}); c.spi({0x20,0,0,0}); c.advance(45000);
    check(c.spiRead({3,0,0,0xff})==0xff,"Sector erase restores all bits");
}
void rtc() {
    Circuit c(E51P_DS1307,3);
    c.start(); c.send(0xd0); c.send(5); c.send(2); c.stop();
    c.start(); c.send(0xd0); c.send(0);
    for (auto value : {0xd9u,0x59u,0x71u,3u,0x31u,0x03u,0x24u,0x10u}) c.send(value);
    c.stop();
    c.advance(3000000);
    e51p_rtc_snapshot time{};
    check(e51p_rtc_state(c.chip,&time)==E51P_OK && time.halted && time.year==2024 &&
        time.month==3 && time.day==31 && time.hour==23 && time.minute==59 && time.second==59,
        "RTC supports halt, 12-hour BCD, and a date written before its month");
    c.start(); c.send(0xd0); c.send(0); c.send(0x59); c.stop();
    check(c.state(2).value==1,"SQW releases on the first half of the configured second");
    c.advance(500000);
    check(!c.state(2).value,"SQW asserts on the second half");
    c.advance(500000);
    check(e51p_rtc_state(c.chip,&time)==E51P_OK && !time.halted &&
        time.month==4 && time.day==1 && !time.hour && !time.minute && !time.second,
        "RTC resumes and carries to a new month");
    check(c.api->reset(c.context)==E51P_OK &&
        e51p_rtc_state(c.chip,&time)==E51P_OK && time.month==4 && time.day==1,"RTC survives logical reset");
}
void pretrigger() {
    Circuit c(E51P_LOGIC_ANALYZER,1);
    c.set(0,false);
    check(e51p_logic_arm(c.chip,1,1)==E51P_OK,"Arm on a disconnected channel");
    for (unsigned i = 0; i < 1100; ++i) { c.advance(1); c.set(0,(i&1)==0); }
    std::array<e51p_logic_sample,E51P_LOGIC_CAPACITY> samples{};
    e51p_logic_status status{};
    check(e51p_logic_read(c.chip,&status,samples.data(),samples.size())==E51P_OK &&
        status.armed && !status.triggered && status.count==1024 && samples.front().tick==77 &&
        samples[1023].tick==1100,"Pre-trigger ring retains the latest 1024 transitions");
    check(e51p_logic_arm(c.chip,0,1)==E51P_OK,"Arm on a connected channel");
    c.advance(1); c.set(0,true);
    check(e51p_logic_read(c.chip,&status,nullptr,0)==E51P_OK && status.triggered &&
        status.trigger_tick==1101,"Trigger uses the exact rising edge timestamp");
}
}

int main() {
    flash();
    rtc();
    pretrigger();
}
