// SPDX-License-Identifier: LGPL-2.1-only
#include "emu51/serial_devices.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <vector>

e51p_status e51p_set_device_pulls(e51p_context*, e51p_endpoint, uint32_t, uint32_t) noexcept;

namespace {
constexpr auto input = E51P_INPUT;
constexpr auto output = E51P_OUTPUT;
constexpr auto io = input | output;
constexpr e51p_endpoint_desc pin(const char* name, unsigned direction, unsigned width = 1, unsigned pull = 0) {
    return {sizeof(e51p_endpoint_desc), name, E51P_DIGITAL, direction, width, 0, pull, 0};
}
constexpr e51p_endpoint_desc i2cPins[]{pin("SCL", input, 1, 1), pin("SDA", io, 1, 1)};
constexpr e51p_endpoint_desc rtcPins[]{i2cPins[0], i2cPins[1], pin("SQW", output, 1, 1)};
constexpr e51p_endpoint_desc gpioPins[]{i2cPins[0], i2cPins[1], pin("A",io,8), pin("B",io,8),
    pin("INTA",output,1,1), pin("INTB",output,1,1), pin("RESET",input,1,1)};
constexpr e51p_endpoint_desc flashPins[]{pin("CS",input,1,1), pin("SCK",input), pin("MOSI",input), pin("MISO",output)};
constexpr e51p_endpoint_desc logicPins[]{pin("IN",input,8)};

constexpr unsigned bcd(unsigned value) { return (value/10)*16+value%10; }
constexpr unsigned decimal(unsigned value) { return (value>>4)*10+(value&15); }
unsigned monthDays(unsigned year, unsigned month) {
    constexpr unsigned days[]{31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month-1]+unsigned(month==2 && year%4==0);
}
struct Rtc {
    std::array<uint8_t,64> reg{};
    unsigned day = 0, seconds = 0, weekday = 7;
    uint64_t fraction = 0;
    bool initialized = false;
    void date(unsigned& year, unsigned& month, unsigned& date) const {
        year = 0; month = 1; date = day;
        while (date >= 365u+unsigned(year%4==0)) {
            date -= 365u+unsigned(year%4==0);
            ++year;
        }
        while (date >= monthDays(year,month)) date -= monthDays(year,month++);
        ++date;
    }
    void update() {
        unsigned year, month, dateOfMonth;
        date(year,month,dateOfMonth);
        const unsigned hour = seconds/3600;
        reg[0] = static_cast<uint8_t>((reg[0]&128)|bcd(seconds%60));
        reg[1] = static_cast<uint8_t>(bcd(seconds/60%60));
        reg[2] = static_cast<uint8_t>((reg[2]&64) ? (64|(hour>=12 ? 32 : 0)|bcd((hour+11)%12+1)) : bcd(hour));
        reg[3] = static_cast<uint8_t>(weekday);
        reg[4] = static_cast<uint8_t>(bcd(dateOfMonth));
        reg[5] = static_cast<uint8_t>(bcd(month));
        reg[6] = static_cast<uint8_t>(bcd(year));
    }
    void write(unsigned index, uint8_t value) {
        if (index >= 7) { reg[index] = index == 7 ? value & 0x93 : value; return; }
        reg[index] = value;
        if (index == 0) fraction = 0;
        const unsigned hour = (reg[2]&64) ? decimal(reg[2]&31)%12+((reg[2]&32) ? 12 : 0) : decimal(reg[2]&63)%24;
        seconds = hour*3600+(decimal(reg[1]&127)%60)*60+decimal(reg[0]&127)%60;
        weekday = std::clamp<unsigned>(reg[3]&7,1,7);
        const unsigned year = decimal(reg[6])%100, month = std::clamp(decimal(reg[5]&31),1u,12u);
        day = year*365+(year+3)/4;
        for (unsigned m = 1; m < month; ++m) day += monthDays(year,m);
        day += std::clamp(decimal(reg[4]&63),1u,monthDays(year,month))-1;
    }
    void advance(uint64_t delta, uint64_t hz) {
        if (reg[0]&128) return;
        uint64_t whole = delta/hz;
        const auto tail = delta%hz;
        if (tail >= hz-fraction) { fraction = tail-(hz-fraction); ++whole; }
        else fraction += tail;
        if (!whole) return;
        const auto partial = seconds+whole%86400;
        const auto days = whole/86400+partial/86400;
        day = static_cast<unsigned>((day+days)%36525);
        weekday = static_cast<unsigned>((weekday-1+days)%7+1);
        seconds = static_cast<unsigned>(partial%86400);
        update();
    }
};
struct Oled {
    std::array<uint8_t,1024> ram{};
    std::array<uint8_t,6> args{};
    unsigned col = 0, page = 0, firstCol = 0, lastCol = 127, firstPage = 0, lastPage = 7;
    unsigned mode = 2, command = 0, remaining = 0, length = 0, line = 0, offset = 0, mux = 64;
    unsigned contrast = 127, control = 0;
    bool wantsControl = true, on = false, invert = false, all = false, remap = false, reverse = false;
    bool scroll = false, left = false, diagonal = false;
    unsigned scrollFirst = 0, scrollLast = 7, scrollRate = 0, scrollVertical = 0, scrollTop = 0, scrollRows = 64;
    uint64_t scrollTick = 0;

    void apply(unsigned value, uint64_t now) {
        switch (value) {
        case 0x20: mode = args[0]&3; break;
        case 0x21: firstCol = args[0]&127; lastCol = std::max(firstCol,unsigned(args[1]&127)); col = firstCol; break;
        case 0x22: firstPage = args[0]&7; lastPage = std::max(firstPage,unsigned(args[1]&7)); page = firstPage; break;
        case 0x26: case 0x27: case 0x29: case 0x2a:
            left = value==0x27 || value==0x2a;
            diagonal = value==0x29 || value==0x2a;
            scrollFirst = args[1]&7; scrollLast = args[3]&7;
            scrollRate = args[2]&7; scrollVertical = diagonal ? args[4]&63 : 0;
            break;
        case 0x2e: scroll = false; break;
        case 0x2f: if (!scroll) scrollTick = now; scroll = true; break;
        case 0x81: contrast = args[0]; break;
        case 0xa0: case 0xa1: remap = value&1; break;
        case 0xa3: scrollTop = args[0]&63; scrollRows = std::clamp(unsigned(args[1]),1u,64-scrollTop); break;
        case 0xa4: case 0xa5: all = value&1; break;
        case 0xa6: case 0xa7: invert = value&1; break;
        case 0xa8: mux = (args[0]&63)+1; break;
        case 0xae: case 0xaf: on = value&1; break;
        case 0xc0: case 0xc8: reverse = value&8; break;
        case 0xd3: offset = args[0]&63; break;
        default:
            if (value <= 0x0f) col = (col&112)|value;
            else if ((value&0xf0)==0x10) col = (col&15)|((value&7)<<4);
            else if ((value&0xc0)==0x40) line = value&63;
            else if (value>=0xb0 && value<=0xb7) page = value&7;
            break;
        }
    }
    void write(uint8_t value, uint64_t now) {
        if (wantsControl) { control = value; wantsControl = false; return; }
        wantsControl = (control&128)!=0;
        if (control&64) {
            ram[page*128+col] = value;
            if (mode==0) {
                if (col >= lastCol) { col = firstCol; page = page >= lastPage ? firstPage : page+1; }
                else ++col;
            } else if (mode==1) {
                if (page >= lastPage) { page = firstPage; col = col >= lastCol ? firstCol : col+1; }
                else ++page;
            } else col = (col+1)&127;
            return;
        }
        if (remaining) {
            args[length-remaining--] = value;
            if (!remaining) apply(command,now);
            return;
        }
        command = value;
        switch (value) {
        case 0x26: case 0x27: length = 6; break;
        case 0x29: case 0x2a: length = 5; break;
        case 0x21: case 0x22: case 0xa3: length = 2; break;
        case 0x20: case 0x81: case 0x8d: case 0xa8: case 0xd3: case 0xd5: case 0xd9: case 0xda: case 0xdb:
            length = 1; break;
        default: length = 0; apply(value,now); break;
        }
        remaining = length;
    }
    void snapshot(e51p_oled_snapshot& out, uint64_t tick, uint64_t hz) const {
        out = {};
        out.contrast = static_cast<uint8_t>(contrast);
        if (!on) return;
        constexpr unsigned frames[]{5,64,128,256,3,4,25,2};
        const auto scrollSteps = (static_cast<long double>(tick-scrollTick)/static_cast<long double>(hz))*60/frames[scrollRate];
        const auto step = scroll ? static_cast<unsigned>(std::fmod(scrollSteps,128*scrollRows)) : 0;
        for (unsigned y = 0; y < mux; ++y) {
            unsigned row = ((reverse ? mux-1-y : y)+line+64-offset)&63;
            if (scrollVertical && row>=scrollTop && row<scrollTop+scrollRows)
                row = scrollTop+static_cast<unsigned>((row-scrollTop+scrollRows-(step%scrollRows)*scrollVertical%scrollRows)%scrollRows);
            for (unsigned x = 0; x < 128; ++x) {
                unsigned column = remap ? 127-x : x;
                if (scroll && row/8>=scrollFirst && row/8<=scrollLast)
                    column = (column+(left ? step%128 : 128-step%128))&127;
                if ((all || ((ram[(row/8)*128+column]>>(row&7))&1)) != invert)
                    out.pixels[y][x/8] |= static_cast<uint8_t>(1u<<(x&7));
            }
        }
    }
};
struct Expander {
    enum { Direction, Polarity, Enable, Compare, Control, Config, Pull, Flags, Capture, Gpio, Latch };
    std::array<std::array<uint8_t,2>,11> reg{};
    std::array<uint8_t,2> previous{};
    bool resetHeld = false;
    Expander() { reg[Direction].fill(255); }
};
struct Flash {
    std::vector<uint8_t> data;
    std::array<uint8_t,256> page{};
    bool selected = false, clock = false, wel = false, reading = false, resetEnabled = false;
    uint8_t cmd = 0, rx = 0, tx = 255;
    unsigned bytes = 0, bits = 0, address = 0, programmed = 0;
    unsigned pending = 0, pendingAddress = 0;
    uint64_t revision = 0, busyStart = 0, busyDuration = 0;

    void exchange(uint8_t value) {
        tx = 255; reading = false;
        if (!bytes) {
            cmd = pending && value!=0x05 && value!=0x35 && value!=0x15 ? 0 : value;
            if (cmd==0x66) resetEnabled = true;
            else if (cmd!=0x99) resetEnabled = false;
        }
        switch (cmd) {
        case 0x05: reading = true; tx = static_cast<uint8_t>((pending ? 1 : 0)|(wel ? 2 : 0)); break;
        case 0x35: case 0x15: reading = true; tx = 0; break;
        case 0x9f: {
            constexpr uint8_t id[]{0xef,0x40,0x17};
            reading = true;
            if (bytes<3) tx = id[bytes];
            break;
        }
        case 0x90: if (bytes>=3) { reading = true; tx = ((address+bytes-3)&1) ? 0x16 : 0xef; } break;
        default: break;
        }
        if (cmd==0x03 || cmd==0x0b || cmd==0x02 || cmd==0x20 || cmd==0x52 || cmd==0xd8 || cmd==0x90) {
            if (bytes>=1 && bytes<=3) address = (address<<8)|value;
            if ((cmd==0x03 && bytes>=3) || (cmd==0x0b && bytes>=4)) {
                reading = true;
                tx = data[(address+bytes-(cmd==0x03 ? 3 : 4))&(E51P_FLASH_SIZE-1)];
            }
            if (cmd==0x02 && bytes>=4 && wel) {
                page[(address+programmed)&255] = value;
                ++programmed;
            }
        }
        ++bytes;
    }
    void finish(uint64_t now, uint64_t hz) {
        if (pending || bits || !bytes) return;
        if (cmd==0x06 && bytes==1) wel = true;
        if (cmd==0x04 && bytes==1) wel = false;
        if (cmd==0x99 && resetEnabled && bytes==1) { wel = false; resetEnabled = false; }
        if (!wel) return;
        uint64_t milliseconds = 0;
        if (cmd==0x02 && programmed) milliseconds = 3;
        if (cmd==0x20 && bytes==4) milliseconds = 45;
        if ((cmd==0x52 || cmd==0xd8) && bytes==4) milliseconds = 150;
        if ((cmd==0xc7 || cmd==0x60) && bytes==1) milliseconds = 2000;
        if (!milliseconds) return;
        pending = cmd; pendingAddress = address&(E51P_FLASH_SIZE-1); wel = false;
        const auto fractionalTicks = ((hz%1000)*milliseconds)/1000;
        constexpr auto limit = std::numeric_limits<uint64_t>::max();
        busyStart = now;
        busyDuration = hz/1000 > (limit-fractionalTicks)/milliseconds ? limit :
            std::max<uint64_t>(1,(hz/1000)*milliseconds+fractionalTicks);
    }
    void commit() {
        if (!pending) return;
        if (pending==0x02) {
            const unsigned base = pendingAddress&~255u;
            for (unsigned i = 0; i < page.size(); ++i) data[base+i] &= page[i];
        } else {
            const unsigned size = pending==0x20 ? 4096 : pending==0x52 ? 32768 : pending==0xd8 ? 65536 : E51P_FLASH_SIZE;
            std::fill_n(data.begin()+(pendingAddress&~(size-1)),size,uint8_t{0xFF});
        }
        pending = 0;
        ++revision;
    }
};
struct Analyzer {
    std::vector<e51p_logic_sample> samples;
    unsigned first = 0, count = 0;
    int channel = -1;
    bool rising = true, armed = false, triggered = false;
    uint64_t triggerTick = 0, endTick = 0;
    void push(e51p_logic_sample sample) {
        const unsigned limit = triggered ? E51P_LOGIC_CAPACITY : E51P_LOGIC_CAPACITY/4;
        if (count==limit) { first = (first+1)%E51P_LOGIC_CAPACITY; --count; }
        samples[(first+count++)%E51P_LOGIC_CAPACITY] = sample;
        if (count==E51P_LOGIC_CAPACITY) { armed = false; endTick = sample.tick; }
    }
    void changed(e51p_logic_sample sample) {
        if (!armed) return;
        if (!triggered && count) {
            const auto last = samples[(first+count-1)%E51P_LOGIC_CAPACITY].pins;
            const uint32_t bit = 1u<<channel;
            if (!((last.floating|last.contention|sample.pins.floating|sample.pins.contention)&bit) &&
                ((last.value^sample.pins.value)&bit) && ((sample.pins.value&bit)!=0)==rising) {
                triggered = true; triggerTick = sample.tick;
            }
        }
        push(sample);
    }
};
}

struct e51p_serial_device {
    enum class Phase { Idle, Address, Receive, AckOut, Send, AckIn };
    const e51p_api* api;
    e51p_context* context;
    e51p_serial_config config;
    std::array<e51p_endpoint,7> pins{};
    uint64_t hz = 0;
    Oled oled;
    Rtc rtc;
    Expander expander;
    Flash flash;
    Analyzer logic;
    Phase phase = Phase::Idle, afterAck = Phase::Idle;
    unsigned bits = 0, pointer = 0;
    int clearInterruptPort = -1;
    uint8_t rx = 0, tx = 0;
    std::array<uint8_t,7> rtcRead{};
    bool oldScl = true, oldSda = true, ack = false, sdaLow = false, wantsPointer = true;

    uint64_t now() const {
        uint64_t tick = 0, rate = 0;
        api->clock(context,&tick,&rate);
        return tick;
    }
    e51p_digital_state sample(unsigned index) const {
        e51p_digital_state value{};
        api->sample(context,pins[index],&value);
        return value;
    }
    bool high(unsigned index) const { return (sample(index).value&1)!=0; }
    void drive(unsigned index, unsigned value, unsigned enable, unsigned open = 0) {
        api->drive(context,pins[index],value,enable,open);
    }
    void sda(bool low) {
        sdaLow = low;
        drive(1,low ? 0 : 1,1,1);
    }
    void squareWave() {
        const auto control = rtc.reg[7];
        bool level = (control&128)!=0;
        if ((control&16) && !(rtc.reg[0]&128)) {
            constexpr unsigned rates[]{1,4096,8192,32768};
            const auto halfCycles = static_cast<long double>(rtc.fraction)*rates[control&3]*2/static_cast<long double>(hz);
            level = (static_cast<unsigned>(halfCycles)&1)==0;
        }
        drive(2,level,1,1);
    }
    void expanderOutputs() {
        auto& r = expander.reg;
        for (unsigned port = 0; port < 2; ++port) {
            drive(2+port,r[Expander::Latch][port],255u^r[Expander::Direction][port]);
            e51p_set_device_pulls(context,pins[2+port],r[Expander::Pull][port]&r[Expander::Direction][port],0);
            const bool mirrored = (r[Expander::Config][0]&64)!=0;
            const bool pending = r[Expander::Flags][port] || (mirrored && r[Expander::Flags][port^1]);
            const bool openDrain = (r[Expander::Config][0]&4)!=0;
            const bool activeHigh = (r[Expander::Config][0]&2)!=0 && !openDrain;
            drive(4+port,pending==activeHigh,1,openDrain);
        }
    }
    unsigned gpioValue(unsigned port) const {
        const auto& r = expander.reg;
        return (sample(2+port).value^(r[Expander::Polarity][port]&r[Expander::Direction][port]))&255;
    }
    void gpioChanged(unsigned port) {
        auto& r = expander.reg;
        const auto value = sample(2+port).value&255;
        const auto previous = expander.previous[port];
        expander.previous[port] = static_cast<uint8_t>(value);
        const auto compared = (previous&~r[Expander::Control][port])|(r[Expander::Compare][port]&r[Expander::Control][port]);
        const auto flags = (value^compared)&r[Expander::Enable][port]&r[Expander::Direction][port];
        if (flags && !r[Expander::Flags][port]) {
            r[Expander::Capture][port] = static_cast<uint8_t>(gpioValue(port));
            r[Expander::Flags][port] = static_cast<uint8_t>(flags);
            expanderOutputs();
        }
    }
    unsigned gpioRegister() const {
        if (expander.reg[Expander::Config][0]&128)
            return (pointer&15)<=10 ? (pointer&15)*2+(pointer>>4) : 255;
        return pointer<=21 ? pointer : 255;
    }
    void gpioNext() {
        const unsigned cfg = expander.reg[Expander::Config][0];
        if (cfg&32) { if (!(cfg&128) && pointer<=21) pointer ^= 1; return; }
        if (cfg&128) pointer = (pointer&15)>=10 ? pointer&16 : pointer+1;
        else pointer = pointer>=21 ? 0 : pointer+1;
    }
    uint8_t readByte() {
        if (config.kind==E51P_DS1307) {
            const auto r = pointer < rtcRead.size() ? rtcRead[pointer] : rtc.reg[pointer];
            pointer = (pointer+1)&63;
            return r;
        }
        if (config.kind!=E51P_MCP23017) return 255;
        const auto index = gpioRegister(), port = index&1;
        gpioNext();
        if (index==255) return 0;
        auto& r = expander.reg;
        const auto reg = index>>1;
        const auto value = reg==Expander::Gpio ? gpioValue(port) : r[reg][port];
        if (reg==Expander::Gpio || reg==Expander::Capture) clearInterruptPort = static_cast<int>(port);
        return static_cast<uint8_t>(value);
    }
    void receive(uint8_t value) {
        if (config.kind==E51P_SSD1306) { oled.write(value,now()); return; }
        if (wantsPointer) {
            pointer = value&(config.kind==E51P_DS1307 ? 63 : 31);
            wantsPointer = false;
            return;
        }
        if (config.kind==E51P_DS1307) {
            rtc.write(pointer,value);
            pointer = (pointer+1)&63;
            squareWave();
            return;
        }
        const auto index = gpioRegister(), port = index&1, reg = index>>1;
        if (index!=255 && reg!=Expander::Flags && reg!=Expander::Capture) {
            auto& r = expander.reg;
            if (reg==Expander::Config) r[reg].fill(value&0xfe);
            else r[reg==Expander::Gpio ? unsigned{Expander::Latch} : reg][port] = value;
            expanderOutputs();
            if (reg==Expander::Enable || reg==Expander::Control || reg==Expander::Compare) gpioChanged(port);
        }
        gpioNext();
    }
    void i2cChanged() {
        const bool scl = high(0), data = high(1);
        if (scl && oldScl && !sdaLow && oldSda!=data) {
            phase = data ? Phase::Idle : Phase::Address;
            bits = 0; rx = 0; clearInterruptPort = -1;
            if (!data && config.kind==E51P_DS1307) std::copy_n(rtc.reg.begin(),rtcRead.size(),rtcRead.begin());
        }
        if (scl && !oldScl) {
            switch (phase) {
            case Phase::Address: case Phase::Receive:
                rx = static_cast<uint8_t>((rx<<1)|unsigned(data));
                if (++bits==8) {
                    if (phase==Phase::Address) {
                        ack = (unsigned(rx)>>1)==config.address && !expander.resetHeld &&
                            (!(rx&1) || config.kind!=E51P_SSD1306);
                        afterAck = !ack ? Phase::Idle : (rx&1) ? Phase::Send : Phase::Receive;
                        wantsPointer = true; oled.wantsControl = true;
                    } else { receive(rx); ack = true; afterAck = Phase::Receive; }
                    phase = Phase::AckOut; bits = 0; rx = 0;
                }
                break;
            case Phase::AckIn: ack = !data; ++bits; break;
            case Phase::AckOut: ++bits; break;
            case Phase::Send:
                if (++bits==8 && clearInterruptPort>=0) {
                    const auto port = static_cast<unsigned>(clearInterruptPort);
                    clearInterruptPort = -1;
                    expander.reg[Expander::Flags][port] = 0;
                    expander.previous[port] = static_cast<uint8_t>(sample(2+port).value);
                    gpioChanged(port);
                    expanderOutputs();
                }
                break;
            default: break;
            }
        }
        if (!scl && oldScl) {
            switch (phase) {
            case Phase::AckOut:
                if (!bits) sda(ack);
                else {
                    phase = afterAck; bits = 0;
                    if (phase==Phase::Send) { tx = readByte(); sda(!(tx&128)); }
                    else sda(false);
                }
                break;
            case Phase::Send:
                if (bits==8) { phase = Phase::AckIn; bits = 0; sda(false); }
                else sda(!(tx&(128u>>bits)));
                break;
            case Phase::AckIn:
                if (bits) {
                    phase = ack ? Phase::Send : Phase::Idle;
                    bits = 0;
                    if (ack) { tx = readByte(); sda(!(tx&128)); }
                }
                break;
            default: break;
            }
        }
        oldScl = scl; oldSda = data;
    }
    void spiChanged(uint64_t tick) {
        const bool select = !high(0), clock = high(1);
        if (select!=flash.selected) {
            if (!select) { flash.finish(tick,hz); drive(3,0,0); }
            else {
                flash.cmd = flash.rx = 0; flash.tx = 255;
                flash.bytes = flash.bits = flash.address = flash.programmed = 0; flash.reading = false;
                if (!flash.pending) flash.page.fill(255);
            }
        } else if (select && clock!=flash.clock) {
            if (clock) {
                flash.rx = static_cast<uint8_t>((flash.rx<<1)|unsigned(high(2)));
                if (++flash.bits==8) {
                    flash.exchange(flash.rx);
                    flash.rx = 0; flash.bits = 0;
                }
            } else if (flash.reading) drive(3,(flash.tx>>(7-flash.bits))&1,1);
        }
        flash.selected = select; flash.clock = clock;
    }
    void arm(int channel, bool rising) {
        logic.first = logic.count = 0;
        logic.channel = channel; logic.rising = rising; logic.armed = true;
        logic.triggered = channel<0; logic.triggerTick = now();
        logic.push({logic.triggerTick,sample(0)});
    }
    static void reset(void* user, e51p_context*) {
        auto& s = *static_cast<e51p_serial_device*>(user);
        s.phase = Phase::Idle; s.oldScl = s.oldSda = true;
        s.bits = s.pointer = 0; s.clearInterruptPort = -1;
        if (s.config.kind<=E51P_MCP23017) s.sda(false);
        switch (s.config.kind) {
        case E51P_SSD1306: s.oled = {}; break;
        case E51P_DS1307:
            if (!s.rtc.initialized) {
                s.rtc.day = s.config.rtc_day; s.rtc.seconds = s.config.rtc_second;
                s.rtc.weekday = (s.config.rtc_day+6)%7+1;
                s.rtc.update(); s.rtc.initialized = true;
            }
            s.squareWave();
            break;
        case E51P_MCP23017: s.expander = {}; s.expanderOutputs(); break;
        case E51P_W25Q64:
            s.flash.commit();
            s.flash.selected = s.flash.wel = s.flash.resetEnabled = false;
            s.drive(3,0,0);
            break;
        case E51P_LOGIC_ANALYZER: s.arm(s.logic.channel,s.logic.rising); break;
        default: break;
        }
    }
    static void advance(void* user, e51p_context*, uint64_t from, uint64_t to) {
        auto& s = *static_cast<e51p_serial_device*>(user);
        if (s.config.kind==E51P_DS1307) {
            s.rtc.advance(to-from,s.hz);
            if (s.rtc.reg[7]&16) s.squareWave();
        }
        if (s.config.kind==E51P_W25Q64 && s.flash.pending && to-s.flash.busyStart>=s.flash.busyDuration) s.flash.commit();
    }
    static void changed(void* user, e51p_context*, e51p_endpoint endpoint, e51p_digital_state state, uint64_t tick) {
        auto& s = *static_cast<e51p_serial_device*>(user);
        switch (s.config.kind) {
        case E51P_MCP23017:
            if (endpoint==s.pins[6]) {
                const bool held = !s.high(6);
                if (held && !s.expander.resetHeld) {
                    s.expander = {}; s.phase = Phase::Idle; s.sda(false); s.expanderOutputs();
                }
                s.expander.resetHeld = held;
            }
            if (endpoint==s.pins[2] || endpoint==s.pins[3]) {
                s.gpioChanged(unsigned(endpoint==s.pins[3]));
                break;
            }
            if (endpoint!=s.pins[0] && endpoint!=s.pins[1]) break;
            [[fallthrough]];
        case E51P_SSD1306: case E51P_DS1307:
            if (endpoint==s.pins[0] || endpoint==s.pins[1]) s.i2cChanged();
            break;
        case E51P_W25Q64:
            if (endpoint==s.pins[0] || endpoint==s.pins[1]) s.spiChanged(tick);
            break;
        case E51P_LOGIC_ANALYZER: s.logic.changed({tick,state}); break;
        default: break;
        }
    }
};

e51p_serial_config e51p_serial_config_init(uint32_t kind) {
    return {kind,kind==E51P_SSD1306 ? 0x3cu : kind==E51P_DS1307 ? 0x68u : 0x20u,0,0};
}
const char* e51p_serial_type_id(uint32_t kind) {
    constexpr const char* ids[]{"emu51.ssd1306","emu51.ds1307","emu51.mcp23017","emu51.w25q64","emu51.logic-analyzer"};
    return kind < E51P_SERIAL_DEVICE_COUNT ? ids[kind] : nullptr;
}
const e51p_endpoint_desc* e51p_serial_pins(uint32_t kind, uint32_t* count) {
    if (!count) return nullptr;
    switch (kind) {
    case E51P_SSD1306: *count = 2; return i2cPins;
    case E51P_DS1307: *count = 3; return rtcPins;
    case E51P_MCP23017: *count = 7; return gpioPins;
    case E51P_W25Q64: *count = 4; return flashPins;
    case E51P_LOGIC_ANALYZER: *count = 1; return logicPins;
    default: *count = 0; return nullptr;
    }
}
e51p_status e51p_add_serial_device(const e51p_api* api, e51p_context* context, const char* id,
    const e51p_serial_config* cfg, e51p_serial_device** result, e51p_endpoint* pins, uint32_t capacity) {
    uint32_t count = 0;
    const auto* metadata = cfg ? e51p_serial_pins(cfg->kind,&count) : nullptr;
    if (!api || api->abi_version!=E51P_ABI_VERSION || api->struct_size<sizeof(*api) || !context ||
        !metadata || !result || !pins || capacity<count ||
        (cfg->kind==E51P_SSD1306 && cfg->address!=0x3c && cfg->address!=0x3d) ||
        (cfg->kind==E51P_DS1307 && (cfg->address!=0x68 || cfg->rtc_day>=36525 || cfg->rtc_second>=86400)) ||
        (cfg->kind==E51P_MCP23017 && (cfg->address<0x20 || cfg->address>0x27))) return E51P_INVALID_ARGUMENT;
    std::unique_ptr<e51p_serial_device> state;
    try {
        state = std::make_unique<e51p_serial_device>();
        if (cfg->kind==E51P_W25Q64) state->flash.data.resize(E51P_FLASH_SIZE,255);
        if (cfg->kind==E51P_LOGIC_ANALYZER) state->logic.samples.resize(E51P_LOGIC_CAPACITY);
    } catch (const std::bad_alloc&) { return E51P_NO_MEMORY; }
    state->api = api; state->context = context; state->config = *cfg;
    uint64_t tick = 0;
    if (api->clock(context,&tick,&state->hz)!=E51P_OK) return E51P_INVALID_STATE;
    e51p_device_desc desc{};
    desc.struct_size = sizeof(desc); desc.instance_id = id; desc.user = state.get();
    desc.destroy = [](void* user) { delete static_cast<e51p_serial_device*>(user); };
    desc.digital_changed = e51p_serial_device::changed;
    desc.reset = e51p_serial_device::reset;
    if (cfg->kind==E51P_DS1307 || cfg->kind==E51P_W25Q64) desc.advance = e51p_serial_device::advance;
    e51p_device device;
    auto status = api->add_device(context,&desc,&device);
    if (status!=E51P_OK) return status;
    auto* owned = state.release();
    for (unsigned i = 0; i < count; ++i) {
        status = api->add_endpoint(context,device,&metadata[i],&owned->pins[i]);
        if (status!=E51P_OK) return status;
    }
    std::copy_n(owned->pins.begin(),count,pins);
    *result = owned;
    return E51P_OK;
}
e51p_status e51p_oled_state(const e51p_serial_device* s, e51p_oled_snapshot* out) {
    if (!s || s->config.kind!=E51P_SSD1306 || !out) return E51P_INVALID_ARGUMENT;
    s->oled.snapshot(*out,s->now(),s->hz);
    return E51P_OK;
}
e51p_status e51p_rtc_state(const e51p_serial_device* s, e51p_rtc_snapshot* out) {
    if (!s || s->config.kind!=E51P_DS1307 || !out) return E51P_INVALID_ARGUMENT;
    unsigned year, month, day;
    s->rtc.date(year,month,day);
    const auto seconds = s->rtc.seconds;
    *out = {2000+year,month,day,seconds/3600,seconds/60%60,seconds%60,(s->rtc.reg[0]&128)!=0};
    return E51P_OK;
}
e51p_status e51p_flash_import(e51p_serial_device* s, const uint8_t* bytes, uint32_t size) {
    if (!s || s->config.kind!=E51P_W25Q64 || !bytes || size!=E51P_FLASH_SIZE) return E51P_INVALID_ARGUMENT;
    if (s->flash.pending || s->flash.selected) return E51P_INVALID_STATE;
    std::copy_n(bytes,size,s->flash.data.begin());
    ++s->flash.revision;
    return E51P_OK;
}
e51p_status e51p_flash_export(const e51p_serial_device* s, uint8_t* bytes, uint32_t size) {
    if (!s || s->config.kind!=E51P_W25Q64 || !bytes || size!=E51P_FLASH_SIZE) return E51P_INVALID_ARGUMENT;
    std::copy_n(s->flash.data.begin(),size,bytes);
    return E51P_OK;
}
uint64_t e51p_flash_revision(const e51p_serial_device* s) {
    return s && s->config.kind==E51P_W25Q64 ? s->flash.revision : 0;
}
e51p_status e51p_logic_arm(e51p_serial_device* s, int channel, int rising) {
    if (!s || s->config.kind!=E51P_LOGIC_ANALYZER || channel < -1 || channel > 7 || (rising!=0 && rising!=1)) return E51P_INVALID_ARGUMENT;
    s->arm(channel,rising!=0);
    return E51P_OK;
}
e51p_status e51p_logic_stop(e51p_serial_device* s) {
    if (!s || s->config.kind!=E51P_LOGIC_ANALYZER) return E51P_INVALID_ARGUMENT;
    if (s->logic.armed) s->logic.endTick = s->now();
    s->logic.armed = false;
    return E51P_OK;
}
e51p_status e51p_logic_read(const e51p_serial_device* s, e51p_logic_status* out,
    e51p_logic_sample* samples, uint32_t capacity) {
    if (!s || s->config.kind!=E51P_LOGIC_ANALYZER || !out || (!samples && capacity)) return E51P_INVALID_ARGUMENT;
    const auto& l = s->logic;
    *out = {l.count,l.armed ? s->now() : l.endTick,l.triggerTick,s->hz,l.armed,l.triggered};
    for (unsigned i = 0; i < std::min(l.count,capacity); ++i)
        samples[i] = l.samples[(l.first+i)%E51P_LOGIC_CAPACITY];
    return E51P_OK;
}

uint32_t e51p_logic_decode(const e51p_logic_sample* samples, uint32_t count, uint32_t protocol,
    e51p_logic_event* out, uint32_t capacity) {
    if (!samples || !out || protocol>E51P_LOGIC_SPI) return 0;
    const bool i2c = protocol==E51P_LOGIC_I2C;
    unsigned written = 0, bits = 0, a = 0, b = 0, valid = 3;
    bool active = false, address = false;
    for (unsigned i = 1; i < count && written < capacity; ++i) {
        const auto& prev = samples[i-1].pins;
        const auto& now = samples[i].pins;
        const unsigned bad = now.floating|now.contention;
        const unsigned unstable = bad|prev.floating|prev.contention;
        const unsigned changed = prev.value^now.value;
        const bool scl = (now.value&1)!=0;
        const bool startStop = i2c ? scl && (prev.value&1) && !(unstable&3) && (changed&2) :
            !(unstable&8) && (changed&8);
        const auto tick = samples[i].tick;
        if (startStop) {
            active = !(now.value&(i2c ? 2u : 8u));
            const auto kind = active ? E51P_LOGIC_BEGIN : E51P_LOGIC_END;
            out[written++] = {tick,static_cast<uint32_t>(kind),0,0,0};
            address = active && i2c;
            bits = a = b = 0; valid = 3;
        } else if (active && (bad&(i2c ? 3u : 9u))) {
            active = false;
        } else if (active && scl && (changed&1) && !(unstable&1)) {
            if (i2c && bits==8) {
                const auto kind = address ? E51P_LOGIC_ADDRESS : E51P_LOGIC_BYTE;
                out[written++] = {tick,static_cast<uint32_t>(kind),a,0,unsigned(!(now.value&2))};
                address = false;
                bits = a = 0;
            } else {
                a = (a<<1)|((now.value>>1)&1);
                b = (b<<1)|((now.value>>2)&1);
                valid &= ~(bad>>1);
                ++bits;
                if (!i2c && bits==8) {
                    out[written++] = {tick,E51P_LOGIC_BYTE,a,b,valid};
                    bits = a = b = 0; valid = 3;
                }
            }
        }
    }
    return written;
}
