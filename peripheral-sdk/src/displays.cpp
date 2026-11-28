// SPDX-License-Identifier: LGPL-2.1-only
#include "emu51/displays.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace {
constexpr uint32_t io=E51P_INPUT|E51P_OUTPUT;
#define PIN(n,d,w,u) {sizeof(e51p_endpoint_desc),n,E51P_DIGITAL,d,w,0,u,0}
const e51p_endpoint_desc raw7[]{PIN("segments",E51P_INPUT,8,0),PIN("commons",E51P_INPUT,8,0),PIN("colon",E51P_INPUT,2,0)};
const e51p_endpoint_desc raw14[]{PIN("segments",E51P_INPUT,15,0),PIN("commons",E51P_INPUT,8,0),PIN("colon",E51P_INPUT,2,0)};
const e51p_endpoint_desc raw16[]{PIN("segments",E51P_INPUT,17,0),PIN("commons",E51P_INPUT,8,0),PIN("colon",E51P_INPUT,2,0)};
const e51p_endpoint_desc barPins[]{PIN("segments",E51P_INPUT,16,0)};
const e51p_endpoint_desc cdPins[]{PIN("BCD",E51P_INPUT,4,0),PIN("LE",E51P_INPUT,1,0),PIN("LT",E51P_INPUT,1,1),PIN("BI",E51P_INPUT,1,1),PIN("segments",E51P_OUTPUT,7,0)};
const e51p_endpoint_desc lsPins[]{PIN("BCD",E51P_INPUT,4,0),PIN("LT",E51P_INPUT,1,1),PIN("BI",E51P_INPUT,1,1),PIN("RBI",E51P_INPUT,1,1),PIN("segments",E51P_OUTPUT,7,127),PIN("RBO",E51P_OUTPUT,1,1)};
const e51p_endpoint_desc maxPins[]{PIN("DIN",E51P_INPUT,1,0),PIN("CLK",E51P_INPUT,1,0),PIN("LOAD",E51P_INPUT,1,1),PIN("DOUT",E51P_OUTPUT,1,0),PIN("segments",E51P_OUTPUT,8,0),PIN("commons",E51P_OUTPUT,8,0)};
const e51p_endpoint_desc htPins[]{PIN("SCL",E51P_INPUT,1,1),PIN("SDA",io,1,1),PIN("segments",E51P_OUTPUT,16,0),PIN("commons",E51P_OUTPUT,8,0),
    PIN("keys0",E51P_INPUT,13,8191),PIN("keys1",E51P_INPUT,13,8191),PIN("keys2",E51P_INPUT,13,8191),PIN("INT",E51P_OUTPUT,1,1)};
const e51p_endpoint_desc tmPins[]{PIN("CLK",E51P_INPUT,1,1),PIN("DIO",io,1,1),PIN("segments",E51P_OUTPUT,8,0),PIN("commons",E51P_OUTPUT,8,0),PIN("keys0",E51P_INPUT,8,255),PIN("keys1",E51P_INPUT,8,255)};
#undef PIN
constexpr uint8_t font[]{0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f,0x40,0x79,0x76,0x38,0x73,0};
constexpr uint8_t lsFont[]{0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7c,0x07,0x7f,0x67,0x58,0x4c,0x62,0x69,0x78,0};
uint8_t fromMax(uint8_t value) { uint8_t result=value&128; for (unsigned bit=0;bit<7;++bit) if (value&(1u<<(6-bit))) result|=uint8_t(1u<<bit); return result; }
}
struct e51p_display {
    const e51p_api* api=nullptr; e51p_context* context=nullptr;
    e51p_display_config cfg{}; std::array<e51p_endpoint,8> pins{};
    uint64_t hz=0, lastTime=0;
    std::array<uint32_t,8> instant{}, unknown{};
    std::array<std::array<double,17>,8> history{};
    std::array<double,2> colonHistory{}; uint32_t colonMask=0;
    uint8_t bcd=0;
    std::array<uint8_t,16> ram{};
    uint16_t shift=0; bool oldClock=false, oldLoad=true, serialOut=false;
    uint8_t decode=0,intensity=0,limit=0; bool enabled=false,test=false,oscillator=false;
    uint8_t blink=0, intMode=0, pointer=0, command=0; bool fixed=false;
    uint64_t heldKeys=0, latchedKeys=0, previousKeys=0, keyTime=0;
    bool keyFlag=false; uint8_t keyReadMask=0;
    // Serial byte engine: I2C MSB-first or TM1637's address-less LSB-first bus.
    bool oldScl=true,oldSda=true,sdaLow=false,ack=false,masterAck=false;
    unsigned phase=0,nextPhase=0,bits=0,ackStage=0; uint8_t rx=0,tx=0;
    bool tm() const { return cfg.kind==E51P_DISPLAY_TM1637; }
    bool ht() const { return cfg.kind==E51P_DISPLAY_HT16K33; }
    bool max() const { return cfg.kind==E51P_DISPLAY_MAX7219||cfg.kind==E51P_DISPLAY_MAX7221; }
    bool raw() const { return cfg.kind<=E51P_DISPLAY_BAR; }
    uint64_t now() const { uint64_t time=0,rate=0; api->clock(context,&time,&rate); return time; }
    unsigned digits() const { return cfg.kind==E51P_DISPLAY_BAR||cfg.kind==E51P_DISPLAY_CD4511||cfg.kind==E51P_DISPLAY_LS47 ? 1 : cfg.digits; }
    unsigned segments() const { return cfg.kind==E51P_DISPLAY_14SEG ? 15 : cfg.kind==E51P_DISPLAY_16SEG ? 17 :
        cfg.kind==E51P_DISPLAY_BAR ? cfg.bar_segments : ht() ? 16 : 8; }
    uint32_t sample(unsigned index,uint32_t& invalid) const {
        e51p_digital_state value{};
        const auto status=api->sample(context,pins[index],&value);
        invalid=status==E51P_OK||status==E51P_CONTENTION ? value.floating|value.contention : UINT32_MAX;
        return value.value;
    }
    bool high(unsigned index,bool fallback=false) const { uint32_t invalid=0; const auto value=sample(index,invalid); return invalid&1 ? fallback : (value&1)!=0; }
    void drive(unsigned index,uint32_t value,uint32_t mask,uint32_t drain=0) { (void)api->drive(context,pins[index],value,mask,drain); }
    void integrate(uint64_t time) {
        if (time==lastTime) return;
        const double decay=std::exp(-double(time-lastTime)/(double(hz)*cfg.persistence_ms/1000));
        for (unsigned d=0;d<digits();++d) for (unsigned s=0;s<segments();++s)
            history[d][s]=history[d][s]*decay+((instant[d]&(1u<<s)) ? 1-decay : 0);
        for (unsigned i=0;i<2;++i) colonHistory[i]=colonHistory[i]*decay+((colonMask&(1u<<i)) ? 1-decay : 0);
        lastTime=time;
    }
    void rawChanged() {
        integrate(now());
        uint32_t invalid=0, commonInvalid=0;
        auto value=sample(0,invalid); const auto mask=(1u<<segments())-1;
        if (cfg.common_anode) value=~value;
        value&=mask&~invalid;
        const auto commons=cfg.kind==E51P_DISPLAY_BAR ? 0u : sample(1,commonInvalid);
        for (unsigned d=0;d<digits();++d) {
            const bool active=cfg.kind==E51P_DISPLAY_BAR||(!(commonInvalid&(1u<<d))&&bool(commons&(1u<<d))==bool(cfg.common_anode));
            instant[d]=active ? value : 0;
            unknown[d]=cfg.kind==E51P_DISPLAY_BAR ? invalid&mask : (commonInvalid&(1u<<d)) ? mask : active ? invalid&mask : 0;
        }
        colonMask=0;
        if (cfg.kind!=E51P_DISPLAY_BAR && cfg.colon) {
            uint32_t bad=0; auto colon=sample(2,bad);
            if (cfg.common_anode) colon=~colon;
            const auto digit=std::min(1u,digits()-1);
            if (!(commonInvalid&(1u<<digit))&&bool(commons&(1u<<digit))==bool(cfg.common_anode)) colonMask=colon&3&~bad;
        }
    }
    void bcdChanged() {
        integrate(now()); uint32_t invalid=0; const auto value=sample(0,invalid)&15;
        uint32_t lit=0; bool blank=false;
        if (cfg.kind==E51P_DISPLAY_CD4511) {
            if (!high(1)&&!(invalid&15)) bcd=static_cast<uint8_t>(value);
            lit=!high(2,true) ? 127 : !high(3,true) ? 0 : bcd<10 ? font[bcd] : 0;
            drive(4,lit,127);
        } else {
            blank=!high(2,true)||(high(1,true)&&!high(3,true)&&value==0);
            lit=!high(2,true) ? 0 : !high(1,true) ? 127 : blank ? 0 : lsFont[value];
            drive(4,(~lit)&127,127,127); drive(5,blank ? 0 : 1,1,1);
        }
        instant[0]=lit; unknown[0]=invalid&15 ? 127 : 0;
    }
    uint64_t keys() const {
        uint64_t result=heldKeys;
        if (ht()) for (unsigned col=0;col<3;++col) { uint32_t bad=0; const auto data=sample(4+col,bad); result|=uint64_t((~data)&~bad&8191)<<(13*col); }
        if (tm()) for (unsigned col=0;col<2;++col) { uint32_t bad=0; const auto data=sample(4+col,bad); result|=uint64_t((~data)&~bad&255)<<(8*col); }
        return result;
    }
    void interruptOutput() { if (ht()) { const bool active=keyFlag&&(intMode&1); drive(7,(intMode&2) ? active : !active,1,(intMode&2) ? 0 : 1); } }
    void scanKeys(uint64_t time) {
        if (!ht()||!oscillator) return;
        const auto current=keys();
        if (current!=previousKeys) { previousKeys=current; keyTime=time; }
        // Functional two-scan debounce, using a fixed 20 ms simulated interval.
        const uint64_t debounce=std::max<uint64_t>(1,hz/50);
        if (current && time-keyTime>=debounce && !keyFlag) { latchedKeys=current; keyFlag=true; interruptOutput(); }
    }
    bool visible(uint64_t time) const {
        if (max()) return test||enabled;
        if (tm()) return enabled;
        if (ht()) {
            if (!oscillator||!enabled) return false;
            if (!blink) return true;
            const long double halfPeriod=static_cast<long double>(hz)*(blink==1 ? 0.25L : blink==2 ? 0.5L : 1.0L);
            return std::fmod(static_cast<long double>(time),2*halfPeriod)<halfPeriod;
        }
        return true;
    }
    unsigned scanCount() const { return max() ? test ? 8 : unsigned(limit)+1 : ht() ? 8 : 6; }
    double duty() const {
        if (max()) return cfg.kind==E51P_DISPLAY_MAX7219 ? double(2*(test ? 15 : intensity)+1)/32 : double(std::min(15u,unsigned(test ? 16 : intensity)+1))/16;
        if (ht()) return double(intensity+1)/16;
        constexpr unsigned widths[]{1,2,4,10,11,12,13,14}; return double(widths[intensity&7])/16;
    }
    uint32_t digitMask(unsigned digit) const {
        if (max()) return test ? 255 : (decode&(1u<<digit)) ? uint32_t(font[ram[digit]&15]|(ram[digit]&128)) : fromMax(ram[digit]);
        if (ht()) return uint32_t(ram[2*digit])|(uint32_t(ram[2*digit+1])<<8);
        return ram[digit];
    }
    void scanOutput(uint64_t time) {
        const unsigned count=scanCount();
        const long double slots=static_cast<long double>(time)*800*count/static_cast<long double>(hz);
        const unsigned digit=static_cast<unsigned>(std::fmod(std::floor(slots),static_cast<long double>(count)));
        const bool on=visible(time) && (slots-std::floor(slots))<duty();
        const uint32_t mask=on ? digitMask(digit) : 0;
        if (max()) { drive(4,mask,255); drive(5,on ? 255u^(1u<<digit) : 255,255); }
        else { const uint32_t width=ht() ? 65535 : 255; drive(2,tm() ? (~mask)&width : mask,width); drive(3,tm() ? (on ? 1u<<digit : 0) : (on ? 255u^(1u<<digit) : 255),255); }
    }
    void maxChanged() {
        const bool clock=high(1), load=high(2,true);
        const bool selected=cfg.kind==E51P_DISPLAY_MAX7219||!load||(load&&!oldLoad&&clock&&!oldClock);
        if (clock&&!oldClock&&selected) { shift=uint16_t((shift<<1)|unsigned(high(0))); serialOut=(shift&0x8000)!=0; }
        if (!clock&&oldClock&&selected) drive(3,serialOut,1);
        if (load&&!oldLoad) {
            const auto address=(shift>>8)&15, data=shift&255;
            if (address>=1&&address<=8) ram[address-1]=static_cast<uint8_t>(data);
            else if (address==9) decode=static_cast<uint8_t>(data);
            else if (address==10) intensity=data&15;
            else if (address==11) limit=data&7;
            else if (address==12) enabled=data&1;
            else if (address==15) test=data&1;
            scanOutput(now());
        }
        oldClock=clock; oldLoad=load;
    }
    void serialDrive(bool low) { sdaLow=low; drive(1,low ? 0 : 1,1,1); }
    void htCommand(uint8_t value) {
        if (value<=15 || (value>=0x40&&value<=0x45) || value==0x60) { pointer=value; keyReadMask=0; }
        else if ((value&0xf0)==0x20) oscillator=value&1;
        else if ((value&0xf0)==0x80) { enabled=value&1; blink=(value>>1)&3; }
        else if ((value&0xf0)==0xe0) intensity=value&15;
        else if ((value&0xf0)==0xa0) { intMode=(value>>1)&3; interruptOutput(); }
        scanOutput(now());
    }
    uint8_t readByte() const {
        if (tm()) {
            const auto mask=keys();
            for (unsigned i=0;i<16;++i) if (mask&(uint64_t(1)<<i)) return static_cast<uint8_t>((i<8 ? 0xf7 : 0xef)-(i&7));
            return 255;
        }
        if (pointer<16) return ram[pointer];
        if (pointer==0x60) return keyFlag ? 1 : 0;
        if (pointer>=0x40&&pointer<=0x45) return static_cast<uint8_t>((latchedKeys>>(13*((pointer-0x40)/2)))>>((pointer&1)*8));
        return 0;
    }
    void nextByte() {
        if (pointer<16) pointer=(pointer+1)&15;
        else if (pointer>=0x40&&pointer<=0x45) {
            keyReadMask|=uint8_t(1u<<(pointer-0x40)); pointer=pointer==0x45 ? 0x40 : uint8_t(pointer+1);
            if (keyReadMask==63) { keyFlag=false; latchedKeys=0; keyReadMask=0; keyTime=now(); interruptOutput(); }
        }
    }
    void received() {
        ack=true; nextPhase=3;
        if (ht()&&phase==1) { ack=(unsigned(rx)>>1)==cfg.i2c_address; nextPhase=ack ? ((rx&1) ? 4 : 2) : 0; }
        else if (ht()&&phase==2) htCommand(rx);
        else if (ht()&&phase==3) { if (pointer<16) { ram[pointer]=rx; pointer=(pointer+1)&15; scanOutput(now()); } }
        else if (tm()) {
            if (phase==2) {
                command=rx;
                if ((rx&0xc0)==0x40) { fixed=rx&4; nextPhase=(rx==0x42) ? 4 : 0; }
                else if ((rx&0xc0)==0xc0) { pointer=rx&7; nextPhase=3; }
                else if ((rx&0xc0)==0x80) { enabled=rx&8; intensity=rx&7; nextPhase=0; scanOutput(now()); }
                else nextPhase=0;
            } else if (phase==3) { if (pointer<6) ram[pointer]=rx; if (!fixed&&pointer<6) ++pointer; scanOutput(now()); }
        }
        ackStage=1; bits=0; rx=0;
    }
    bool txLow() const { return !(tx&(tm() ? 1u<<bits : 128u>>bits)); }
    void serialChanged() {
        const bool scl=high(0,true), sda=high(1,true);
        if (scl&&oldScl&&!sdaLow) {
            if (oldSda&&!sda) { phase=ht() ? 1 : 2; bits=ackStage=0; rx=0; }
            else if (!oldSda&&sda) { phase=0; ackStage=0; }
        }
        if (!oldScl&&scl) {
            if (ackStage==2) ackStage=3;
            else if (phase==4) { if (++bits==8) { phase=tm() ? 7 : 5; if (ht()) nextByte(); } }
            else if (phase==5) { masterAck=!sda; phase=6; }
            else if (phase>=1&&phase<=3&&!ackStage) {
                if (tm()) rx=uint8_t(rx|(unsigned(sda)<<bits)); else rx=uint8_t((rx<<1)|unsigned(sda));
                if (++bits==8) received();
            }
        }
        if (oldScl&&!scl) {
            if (ackStage==1) { ackStage=2; serialDrive(ack); }
            else if (ackStage==3) { ackStage=0; phase=nextPhase; bits=0; tx=readByte(); serialDrive(phase==4&&txLow()); }
            else if (phase==4) serialDrive(txLow());
            else if (phase==5) serialDrive(false);
            else if (phase==6) { bits=0; phase=masterAck ? 4 : 0; tx=readByte(); serialDrive(phase==4&&txLow()); }
            else if (phase==7) { phase=0; serialDrive(false); }
        }
        oldScl=scl; oldSda=sda;
        if (ht()) scanKeys(now());
    }
    static void changed(void* user,e51p_context*,e51p_endpoint,e51p_digital_state,uint64_t) {
        auto& s=*static_cast<e51p_display*>(user);
        if (s.raw()) s.rawChanged(); else if (s.max()) s.maxChanged(); else if (s.ht()||s.tm()) s.serialChanged(); else s.bcdChanged();
    }
    static void advance(void* user,e51p_context*,uint64_t,uint64_t to) {
        auto& s=*static_cast<e51p_display*>(user);
        if (s.max()||s.ht()||s.tm()) { s.scanKeys(to); s.scanOutput(to); }
    }
    static void reset(void* user,e51p_context*) {
        auto& s=*static_cast<e51p_display*>(user);
        s.lastTime=0; s.instant.fill(0); s.unknown.fill(0); for (auto& row:s.history) row.fill(0); s.colonHistory.fill(0); s.colonMask=0;
        s.ram.fill(0); s.shift=0; s.oldClock=false; s.oldLoad=true; s.serialOut=false; s.decode=s.intensity=s.limit=0;
        s.enabled=s.test=s.oscillator=false; s.blink=s.intMode=s.pointer=s.command=0; s.fixed=false; s.bcd=0;
        s.latchedKeys=s.previousKeys=s.keyTime=0; s.keyFlag=false; s.keyReadMask=0;
        s.oldScl=s.oldSda=true; s.sdaLow=s.ack=s.masterAck=false; s.phase=s.nextPhase=s.bits=s.ackStage=0; s.rx=s.tx=0;
        if (s.max()) { s.drive(3,0,1); s.scanOutput(0); }
        else if (s.ht()||s.tm()) { s.serialDrive(false); s.scanOutput(0); if (s.ht()) s.interruptOutput(); }
        else if (!s.raw()) { s.drive(4,s.cfg.kind==E51P_DISPLAY_LS47 ? 127 : 0,127,s.cfg.kind==E51P_DISPLAY_LS47 ? 127 : 0); if (s.cfg.kind==E51P_DISPLAY_LS47) s.drive(5,1,1,1); }
    }
};
extern "C" {
e51p_display_config e51p_display_config_init(uint32_t kind) { return {sizeof(e51p_display_config),kind,
    kind==E51P_DISPLAY_MAX7219||kind==E51P_DISPLAY_MAX7221||kind==E51P_DISPLAY_HT16K33 ? 8u : 4u,10,0,0,20,0x70}; }
const char* e51p_display_type_id(uint32_t kind) {
    static const char* ids[]{"emu51.seven-segment","emu51.fourteen-segment","emu51.sixteen-segment","emu51.bargraph",
        "emu51.cd4511","emu51.74ls47","emu51.max7219","emu51.max7221","emu51.ht16k33","emu51.tm1637"};
    return kind<E51P_DISPLAY_COUNT ? ids[kind] : nullptr;
}
const char* e51p_display_name(uint32_t kind) {
    static const char* names[]{"7-segment LED display","14-segment LED display","16-segment LED display","LED bar graph",
        "CD4511 BCD decoder","74LS47 BCD decoder","MAX7219","MAX7221","HT16K33","TM1637"};
    return kind<E51P_DISPLAY_COUNT ? names[kind] : nullptr;
}
const e51p_endpoint_desc* e51p_display_pins(uint32_t kind,uint32_t* count) {
    if (!count) return nullptr;
    switch (kind) {
    case E51P_DISPLAY_7SEG: *count=3; return raw7;
    case E51P_DISPLAY_14SEG: *count=3; return raw14;
    case E51P_DISPLAY_16SEG: *count=3; return raw16;
    case E51P_DISPLAY_BAR: *count=1; return barPins;
    case E51P_DISPLAY_CD4511: *count=5; return cdPins;
    case E51P_DISPLAY_LS47: *count=6; return lsPins;
    case E51P_DISPLAY_MAX7219: case E51P_DISPLAY_MAX7221: *count=6; return maxPins;
    case E51P_DISPLAY_HT16K33: *count=8; return htPins;
    case E51P_DISPLAY_TM1637: *count=6; return tmPins;
    default: *count=0; return nullptr;
    }
}
e51p_status e51p_add_display(const e51p_api* api,e51p_context* c,const char* id,
    const e51p_display_config* cfg,e51p_display** result,e51p_endpoint* pins,uint32_t capacity) {
    if (!cfg||cfg->struct_size<sizeof(*cfg)) return E51P_INVALID_ARGUMENT;
    uint32_t count=0; const auto* desc=e51p_display_pins(cfg->kind,&count);
    if (!api||api->abi_version!=E51P_ABI_VERSION||api->struct_size<sizeof(*api)||!c||!cfg||cfg->struct_size<sizeof(*cfg)||!desc||!result||!pins||capacity<count||
        cfg->digits<1||cfg->digits>8||cfg->bar_segments<1||cfg->bar_segments>16||cfg->common_anode<0||cfg->common_anode>1||cfg->colon<0||cfg->colon>1||
        !std::isfinite(cfg->persistence_ms)||cfg->persistence_ms<1||cfg->persistence_ms>1000||cfg->i2c_address<0x70||cfg->i2c_address>0x77) return E51P_INVALID_ARGUMENT;
    auto state=std::unique_ptr<e51p_display>(new(std::nothrow) e51p_display); if (!state) return E51P_NO_MEMORY;
    state->api=api; state->context=c; state->cfg=*cfg; state->pins.fill(E51P_INVALID_ID);
    uint64_t time=0; if (api->clock(c,&time,&state->hz)!=E51P_OK) return E51P_INVALID_STATE;
    if (state->tm()&&state->cfg.digits>6) return E51P_INVALID_ARGUMENT;
    e51p_device_desc device{}; device.struct_size=sizeof(device); device.instance_id=id; device.user=state.get();
    device.destroy=[](void* p) { delete static_cast<e51p_display*>(p); }; device.reset=e51p_display::reset;
    device.digital_changed=e51p_display::changed; device.advance=e51p_display::advance;
    e51p_device handle; auto status=api->add_device(c,&device,&handle); if (status!=E51P_OK) return status;
    auto* owned=state.release();
    for (uint32_t i=0;i<count;++i) { status=api->add_endpoint(c,handle,&desc[i],&owned->pins[i]); if (status!=E51P_OK) return status; }
    std::copy_n(owned->pins.begin(),count,pins); *result=owned; return E51P_OK;
}
e51p_status e51p_display_snapshot(const e51p_display* s,e51p_segment_snapshot* out) {
    if (!s||!out||out->struct_size<sizeof(*out)) return E51P_INVALID_ARGUMENT;
    e51p_segment_snapshot result{}; result.struct_size=sizeof(result); result.digits=s->digits(); result.segments=s->segments(); result.keys=(s->ht()||s->tm()) ? s->keys() : 0;
    const auto time=s->now();
    if (s->raw()||s->cfg.kind==E51P_DISPLAY_CD4511||s->cfg.kind==E51P_DISPLAY_LS47) {
        const double decay=std::exp(-double(time-s->lastTime)/(double(s->hz)*s->cfg.persistence_ms/1000));
        for (unsigned d=0;d<result.digits;++d) {
            result.instantaneous[d]=result.programmed[d]=s->instant[d]; result.undefined[d]=s->unknown[d];
            for (unsigned segment=0;segment<result.segments;++segment) result.brightness[d][segment]=float(s->history[d][segment]*decay+((s->instant[d]&(1u<<segment)) ? 1-decay : 0));
        }
        result.colon=s->colonMask;
        for (unsigned i=0;i<2;++i) result.colon_brightness[i]=float(s->colonHistory[i]*decay+((s->colonMask&(1u<<i)) ? 1-decay : 0));
    } else if (s->visible(time)) {
        const long double slots=static_cast<long double>(time)*800*s->scanCount()/static_cast<long double>(s->hz);
        const auto scan=static_cast<unsigned>(std::fmod(std::floor(slots),static_cast<long double>(s->scanCount())));
        const bool active=slots-std::floor(slots)<s->duty();
        for (unsigned d=0;d<result.digits&&d<s->scanCount();++d) {
            const auto mask=s->digitMask(d); result.programmed[d]=mask;
            result.instantaneous[d]=active&&d==scan ? mask : 0;
            for (unsigned segment=0;segment<result.segments;++segment) result.brightness[d][segment]=(mask&(1u<<segment)) ? float(s->duty()/s->scanCount()) : 0;
        }
        if (s->cfg.colon&&result.digits>1) {
            const auto dp=result.segments-1;
            result.colon=(result.instantaneous[1]&(1u<<dp)) ? 3 : 0;
            result.colon_brightness[0]=result.colon_brightness[1]=result.brightness[1][dp];
            result.instantaneous[1]&=~(1u<<dp); result.programmed[1]&=~(1u<<dp); result.brightness[1][dp]=0;
        }
    }
    *out=result; return E51P_OK;
}
e51p_status e51p_display_set_keys(e51p_display* s,uint64_t mask) {
    if (!s||(!s->ht()&&!s->tm())||mask>=(uint64_t(1)<<(s->ht() ? 39 : 16))) return E51P_INVALID_ARGUMENT;
    s->heldKeys=mask; s->scanKeys(s->now()); return E51P_OK;
}
}
