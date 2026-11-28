// SPDX-License-Identifier: LGPL-2.1-only
#include "emu51/components.h"
#include "emu51/snapshot.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace {
constexpr uint32_t io = E51P_INPUT | E51P_OUTPUT;
#define PIN(n,d,w,u,l) {sizeof(e51p_endpoint_desc),n,E51P_DIGITAL,d,w,0,u,l}
const e51p_endpoint_desc ledPins[] = {PIN("in",E51P_INPUT,1,0,0)};
const e51p_endpoint_desc switchPins[] = {PIN("out",E51P_OUTPUT,1,1,0)};
const e51p_endpoint_desc lcdPins[] = {PIN("data",io,8,0,0),PIN("RS",E51P_INPUT,1,0,0),
    PIN("RW",E51P_INPUT,1,0,0),PIN("E",E51P_INPUT,1,0,0)};
const e51p_endpoint_desc keypadPins[] = {PIN("rows",E51P_INPUT,4,15,0),PIN("columns",E51P_OUTPUT,4,15,0)};
const e51p_endpoint_desc matrixPins[] = {PIN("rows",E51P_INPUT,8,255,0),PIN("columns",E51P_OUTPUT,4,15,0),PIN("direct",E51P_OUTPUT,8,255,0)};
const e51p_endpoint_desc latchPins[] = {{sizeof(e51p_endpoint_desc),"bus",E51P_BUS_TARGET,0,8,1,0,0},
    PIN("Q",E51P_OUTPUT,8,0,0),PIN("Q_n",E51P_OUTPUT,8,0,0),
    PIN("PULSE",E51P_OUTPUT,1,0,0),PIN("PULSE_n",E51P_OUTPUT,1,0,0)};
const e51p_endpoint_desc bufferPins[] = {{sizeof(e51p_endpoint_desc),"bus",E51P_BUS_TARGET,0,8,1,0,0},
    PIN("IN",E51P_INPUT,8,255,0)};
const e51p_endpoint_desc shift595Pins[] = {PIN("SER",E51P_INPUT,1,0,0),PIN("SRCLK",E51P_INPUT,1,0,0),
    PIN("RCLK",E51P_INPUT,1,0,0),PIN("SRCLR",E51P_INPUT,1,1,0),PIN("OE",E51P_INPUT,1,0,0),
    PIN("Q",E51P_OUTPUT,8,0,0),PIN("QH",E51P_OUTPUT,1,0,0)};
const e51p_endpoint_desc shift165Pins[] = {PIN("D",E51P_INPUT,8,0,0),PIN("SER",E51P_INPUT,1,0,0),
    PIN("CLK",E51P_INPUT,1,0,0),PIN("INH",E51P_INPUT,1,0,0),PIN("LOAD",E51P_INPUT,1,1,0),
    PIN("QH",E51P_OUTPUT,1,0,0),PIN("QH_n",E51P_OUTPUT,1,0,0)};
const e51p_endpoint_desc eepromPins[] = {PIN("SCL",E51P_INPUT,1,1,0),PIN("SDA",io,1,1,0),PIN("WP",E51P_INPUT,1,0,0)};
const e51p_endpoint_desc ulnPins[] = {PIN("IN",E51P_INPUT,7,0,0),PIN("OUT",E51P_OUTPUT,7,0,0)};
const e51p_endpoint_desc stepperPins[] = {PIN("COILS",E51P_INPUT,4,15,0)};
#define AIN(n) {sizeof(e51p_endpoint_desc),n,E51P_ANALOG,E51P_INPUT,1,0,0,0}
const e51p_endpoint_desc adc0809Pins[] = {PIN("ADDR",E51P_INPUT,3,0,0),PIN("ALE",E51P_INPUT,1,0,0),
    PIN("START",E51P_INPUT,1,0,0),PIN("OE",E51P_INPUT,1,0,0),PIN("EOC",E51P_OUTPUT,1,0,0),
    PIN("DATA",E51P_OUTPUT,8,0,0),AIN("IN0"),AIN("IN1"),AIN("IN2"),AIN("IN3"),AIN("IN4"),AIN("IN5"),AIN("IN6"),AIN("IN7")};
#undef AIN
const e51p_endpoint_desc ayPins[] = {{sizeof(e51p_endpoint_desc),"bus",E51P_BUS_TARGET,0,8,1,0,0},
    PIN("DA",io,8,0,0),PIN("BDIR",E51P_INPUT,1,0,0),PIN("BC1",E51P_INPUT,1,0,0),
    PIN("BC2",E51P_INPUT,1,1,0),PIN("RESET",E51P_INPUT,1,1,0),PIN("A",io,8,255,0),PIN("B",io,8,255,0),
    {sizeof(e51p_endpoint_desc),"out",E51P_ANALOG,E51P_OUTPUT,1,0,0,0},
    {sizeof(e51p_endpoint_desc),"channelA",E51P_ANALOG,E51P_OUTPUT,1,0,0,0},
    {sizeof(e51p_endpoint_desc),"channelB",E51P_ANALOG,E51P_OUTPUT,1,0,0,0},
    {sizeof(e51p_endpoint_desc),"channelC",E51P_ANALOG,E51P_OUTPUT,1,0,0,0}};
#undef PIN
}

struct e51p_component {
    const e51p_api* api = nullptr;
    e51p_context* context = nullptr;
    e51p_component_config config{};
    std::array<e51p_endpoint,16> pins{};
    uint64_t hz = 0;
    bool started = false;
    bool pressed = false, contact = false;
    uint64_t bounceStart = 0, bounceEnd = 0;
    uint32_t keys = 0, shift = 0, latch = 0;
    uint32_t directKeys = 0;
    uint32_t lastClock = 0, lastLatch = 0;
    uint64_t pulseTicks = 0, pulseStart = 0;
    bool pulseActive = false;
    // HD44780 state; DDRAM implements the two 40-character controller lines.
    std::array<uint8_t,128> ddram{};
    std::array<uint8_t,64> cgram{};
    uint8_t address = 0, highNibble = 0;
    bool fourBit = false, secondNibble = false, cgramSelected = false;
    bool display = false, cursor = false, blink = false, increment = true, entryShift = false;
    int displayShift = 0;
    uint64_t busyUntil = 0;
    bool lastE = false, readSecond = false;
    // I2C: address, word address, data, read phases and ACK half cycles.
    std::vector<uint8_t> memory;
    std::array<uint8_t,64> page{};
    uint64_t pageMask = 0, storageRevision = 0;
    unsigned pageBase = 0, pointer = 0, wordHigh = 0, wordBytes = 0;
    uint8_t rx = 0;
    int phase = 0, bits = 0, ackStage = 0, nextPhase = 0;
    bool selected = false, ack = false, sdaLow = false, lastScl = true, lastSda = true, masterAck = false;
    uint64_t writeUntil = 0;
    // Four-phase stepper uses one ring of both wave and paired-coil states.
    e51p_stepper_snapshot motor{};
    int previousPhase = -1;
    bool motorDefined = false;
    // ADC0809 pin latches and timed conversion.
    uint64_t conversionTicks = 1200, adcRemaining = 0;
    double adcSpan = 5;
    uint32_t adcChannel = 0, adcPending = 0, adcValue = 0;
    bool adcAddressKnown = false, adcPendingValid = false, adcValid = false;
    bool adcEoc = true, adcAle = false, adcStart = false, adcArmed = false, adcInitialized = false;
    // AY-3-8910: the master clock is independent of the CPU oscillator.
    std::array<uint8_t,16> registers{};
    uint8_t ayAddress = 0, lastMode = 0;
    std::array<unsigned,3> toneCount{{1,1,1}};
    std::array<bool,3> toneHigh{};
    unsigned noiseCount = 1, envelopeCount = 1, noise = 0x1ffff;
    int envelopeStep = 15, envelopeAttack = 0;
    bool envelopeHold = false;
    double clockRemainder = 0;

    uint64_t now() const { uint64_t time = 0, rate = 0; api->clock(context,&time,&rate); return time; }
    uint64_t micros(unsigned us) const { return std::max<uint64_t>(1,(hz/1000000)*us+((hz%1000000)*us+999999)/1000000); }
    bool sample(unsigned i, uint32_t& value, uint32_t mask = 1) const {
        e51p_digital_state state{};
        if (api->sample(context,pins[i],&state) != E51P_OK || ((state.floating|state.contention)&mask)) return false;
        value = state.value; return true;
    }
    bool high(unsigned i, bool fallback = false) const { uint32_t value = 0; return sample(i,value) ? (value&1) != 0 : fallback; }
    void drive(unsigned i, uint32_t value, uint32_t mask, uint32_t drain = 0) { (void)api->drive(context,pins[i],value,mask,drain); }
    void contactOutput() { drive(0,config.active_low ? !contact : contact,config.active_low ? (contact ? 1u : 0u) : 1u,config.active_low ? 1u : 0u); }
    void keypadOutput() {
        const bool matrix = config.kind == E51P_COMPONENT_MATRIX;
        const unsigned rowCount = matrix ? config.rows : 4, columnCount = matrix ? config.columns : 4;
        const unsigned rowMask = (1u<<rowCount)-1, columnMask = (1u<<columnCount)-1;
        uint32_t rows = rowMask, columns = 15;
        if (sample(0,rows,rowMask)) for (unsigned r = 0; r < rowCount; ++r)
            if (!(rows&(1u<<r))) columns &= ~((keys>>(columnCount*r))&columnMask);
        drive(1,columns,15,15);
        if (matrix) {
            uint32_t direct = 255;
            for (unsigned key = 0; key < directKeys; ++key)
                if (keys & (UINT32_C(1)<<(rowCount*columnCount+key))) direct &= ~(1u<<key);
            drive(2,direct,255,255);
        }
    }
    void latchOutput() { drive(1,latch,255); drive(2,latch^255,255); }
    void pulseOutput() { drive(3,pulseActive,1); drive(4,!pulseActive,1); }
    void shifts() {
        if (config.kind == E51P_COMPONENT_595) {
            const bool clock = high(1), store = high(2);
            const auto previous = shift;
            if (!high(3,true)) shift = 0;
            else if (clock && !lastClock) shift = ((shift<<1)|unsigned(high(0)))&255;
            // Both clocks high on the same edge latch the preceding shift state.
            if (store && !lastLatch) latch = clock && !lastClock ? previous : shift;
            lastClock = clock; lastLatch = store;
            drive(5,latch,high(4) ? 0u : 255u); drive(6,(shift>>7)&1,1);
        } else {
            const bool clock = high(2) || high(3);
            if (!high(4,true)) { uint32_t data = 0; if (sample(0,data,255)) shift = data&255; }
            else if (clock && !lastClock) shift = ((shift<<1)|unsigned(high(1)))&255;
            lastClock = clock;
            drive(5,(shift>>7)&1,1); drive(6,((shift>>7)&1)^1,1);
        }
    }
    void lcdNext() {
        if (cgramSelected) address = static_cast<uint8_t>((int(address)+(increment ? 1 : 63))%64);
        else if (increment) address = address == 0x27 ? 0x40 : address == 0x67 ? 0 : (address+1)&127;
        else address = address == 0 ? 0x67 : address == 0x40 ? 0x27 : (address-1)&127;
    }
    uint8_t lcdRead(bool data) const { return data ? (cgramSelected ? cgram[address&63] : ddram[address&127]) :
        static_cast<uint8_t>((now()<busyUntil ? 128 : 0)|address); }
    void lcdWrite(uint8_t value, bool data) {
        if (now() < busyUntil) return;
        unsigned duration = 37;
        if (data) {
            if (cgramSelected) cgram[address&63] = value&31; else ddram[address&127] = value;
            lcdNext();
            if (entryShift && !cgramSelected) displayShift = (displayShift+(increment ? 1 : 39))%40;
        } else if (value&128) { cgramSelected = false; address = value&127; }
        else if (value&64) { cgramSelected = true; address = value&63; }
        else if (value&32) { fourBit = !(value&16); secondNibble = readSecond = false; }
        else if (value&16) {
            if (value&8) displayShift = (displayShift+((value&4) ? 39 : 1))%40;
            else address = static_cast<uint8_t>((address+((value&4) ? 1 : 127))&127);
        } else if (value&8) { display = value&4; cursor = value&2; blink = value&1; }
        else if (value&4) { increment = value&2; entryShift = value&1; }
        else if (value&3) {
            if (value&1) { ddram.fill(' '); increment = true; entryShift = false; }
            address = 0; cgramSelected = false; displayShift = 0; duration = 1520;
        }
        busyUntil = now()+micros(duration);
    }
    void lcdChanged() {
        const bool enable = high(3), reading = high(2), data = high(1);
        if (enable && reading) {
            const uint8_t value = lcdRead(data);
            drive(0,fourBit ? (readSecond ? uint32_t(value)<<4 : value)&240 : value,fourBit ? 240 : 255);
        } else drive(0,0,0);
        if (!enable && lastE) {
            if (reading) {
                if (!fourBit || readSecond) { if (data && now() >= busyUntil) lcdNext(); readSecond = false; }
                else readSecond = true;
            } else {
                uint32_t value = 0;
                if (sample(0,value,fourBit ? 240 : 255)) {
                    if (!fourBit) lcdWrite(static_cast<uint8_t>(value),data);
                    else if (!secondNibble) { highNibble = value&240; secondNibble = true; }
                    else { secondNibble = false; lcdWrite(static_cast<uint8_t>(highNibble|(value>>4)),data); }
                }
            }
        }
        lastE = enable;
    }
    bool wideEeprom() const { return config.kind == E51P_COMPONENT_24LC256; }
    unsigned pageSize() const { return wideEeprom() ? 64 : 8; }
    void eepromDrive(bool low) { sdaLow = low; drive(1,low ? 0 : 1,1,1); }
    void eepromCommit() {
        for (unsigned i = 0; i < pageSize(); ++i) {
            if (!(pageMask&(UINT64_C(1)<<i))) continue;
            auto& byte = memory[pageBase+i];
            if (byte != page[i]) { byte = page[i]; ++storageRevision; }
        }
        pageMask = 0; writeUntil = 0;
    }
    void received() {
        ack = false; nextPhase = 0;
        if (phase == 1) {
            selected = (rx>>1) == config.i2c_address && now() >= writeUntil;
            ack = selected; nextPhase = selected ? ((rx&1) ? 4 : 2) : 0;
            wordBytes = 0;
        } else if (phase == 2) {
            ack = true;
            if (wideEeprom() && wordBytes++ == 0) { wordHigh = rx&127u; nextPhase = 2; }
            else { pointer = (wideEeprom() ? wordHigh<<8 : 0)|rx; nextPhase = 3; }
        } else if (phase == 3) {
            ack = true; nextPhase = 3;
            const unsigned offsetMask = pageSize()-1;
            if (!pageMask) pageBase = pointer&~offsetMask;
            page[pointer&offsetMask] = rx;
            pageMask |= UINT64_C(1)<<(pointer&offsetMask);
            pointer = pageBase|((pointer+1)&offsetMask);
        }
        ackStage = 1; bits = 0; rx = 0;
    }
    void eepromChanged() {
        const bool scl = high(0,true), sda = high(1,true);
        // Ignore transitions caused by our own open-drain ACK/data driver.
        if (scl && lastScl && !sdaLow) {
            if (lastSda && !sda) {
                if (wideEeprom() && !writeUntil) pageMask = 0;
                phase = 1; bits = 0; ackStage = 0; rx = 0; selected = false;
            }
            if (!lastSda && sda) {
                phase = 0; ackStage = 0;
                if (pageMask && !writeUntil) {
                    if (high(2)) pageMask = 0;
                    else writeUntil = now()+micros(5000);
                }
            }
        }
        if (!lastScl && scl) {
            if (ackStage == 2) ackStage = 3;
            else if (phase == 4) { if (++bits == 8) phase = 5; }
            else if (phase == 5) { masterAck = !sda; phase = 6; }
            else if (phase >= 1 && phase <= 3 && !ackStage) {
                rx = static_cast<uint8_t>((rx<<1)|unsigned(sda));
                if (++bits == 8) received();
            }
        }
        if (lastScl && !scl) {
            if (ackStage == 1) { ackStage = 2; eepromDrive(ack); }
            else if (ackStage == 3) {
                ackStage = 0; phase = nextPhase; bits = 0;
                eepromDrive(phase == 4 && !(memory[pointer]&128));
            } else if (phase == 4) eepromDrive(!(memory[pointer]&(128u>>bits)));
            else if (phase == 5) eepromDrive(false);
            else if (phase == 6) {
                pointer = (pointer+1)&(static_cast<unsigned>(memory.size())-1);
                bits = 0; phase = masterAck ? 4 : 0;
                eepromDrive(phase == 4 && !(memory[pointer]&128));
            }
        }
        lastScl = scl; lastSda = sda;
    }
    void ulnChanged() {
        e51p_digital_state input{};
        (void)api->sample(context,pins[0],&input);
        drive(1,0,input.value&~(input.floating|input.contention)&127u,127);
    }
    void stepperChanged() {
        constexpr int phases[]{-1,0,2,1,4,-1,3,-1,6,7,-1,-1,5,-1,-1,-1};
        uint32_t levels = 0;
        const bool defined = sample(0,levels,15);
        const unsigned coils = defined ? (~levels)&15u : 0;
        if (coils==motor.coils && defined==motorDefined) return;
        motorDefined = defined;
        motor.coils = coils;
        const int phase = phases[coils];
        motor.valid = defined && (!coils || phase>=0);
        if (phase<0) {
            if (!motor.valid && motor.invalid_transitions<UINT32_MAX) ++motor.invalid_transitions;
            previousPhase = -1; motor.direction = 0;
            return;
        }
        if (previousPhase>=0) {
            int step = (phase-previousPhase+8)%8;
            if (step>4) step -= 8;
            if (step!=0 && std::abs(step)<=2) {
                constexpr int turn = E51P_STEPPER_HALF_STEPS_PER_TURN;
                motor.position_half_steps = static_cast<unsigned>((static_cast<int>(motor.position_half_steps)+step+turn)%turn);
                motor.direction = step>0 ? 1 : -1;
            } else if (step) {
                if (motor.invalid_transitions<UINT32_MAX) ++motor.invalid_transitions;
                motor.direction = 0;
            }
        }
        previousPhase = phase;
    }
    void adcOutputs() {
        drive(4,adcEoc,1);
        drive(5,adcValue,adcValid && high(3) ? 255 : 0);
    }
    void adcChanged() {
        const bool ale = high(1), start = high(2);
        if (!adcInitialized) {
            adcInitialized = true; adcAle = ale; adcStart = start;
            adcOutputs();
            return;
        }
        if (ale && !adcAle) adcAddressKnown = sample(0,adcChannel,7);
        if (start && !adcStart) {
            adcArmed = true;
            adcRemaining = 0; adcEoc = false;
        } else if (!start && adcStart && adcArmed) {
            adcArmed = false;
            double volts = 0;
            adcPendingValid = adcAddressKnown && api->analog_read(context,pins[6+adcChannel],&volts)==E51P_OK;
            adcPending = adcPendingValid ? static_cast<unsigned>(std::clamp(volts/adcSpan*256+0.5,0.0,255.0)) : 0;
            adcRemaining = conversionTicks;
        }
        adcAle = ale; adcStart = start;
        adcOutputs();
    }
    void ayPorts() {
        for (unsigned port = 0; port < 2; ++port)
            drive(6+port,registers[14+port],(registers[7]&(64u<<port)) ? 255 : 0);
    }
    uint8_t ayRead() const {
        if (ayAddress >= 14 && !(registers[7]&(64u<<(ayAddress-14)))) {
            uint32_t data = 255; sample(6+ayAddress-14,data,255); return static_cast<uint8_t>(data);
        }
        return registers[ayAddress];
    }
    void ayWrite(uint8_t value) {
        static constexpr uint8_t masks[]{255,15,255,15,255,15,31,255,31,31,31,255,255,15,255,255};
        registers[ayAddress] = value&masks[ayAddress];
        if (ayAddress == 13) {
            envelopeStep = 15; envelopeAttack = (value&4) ? 15 : 0; envelopeHold = false;
            envelopeCount = 32*std::max(1u,unsigned(registers[11])+(unsigned(registers[12])<<8));
        }
        if (ayAddress == 7 || ayAddress >= 14) ayPorts();
        ayOutput();
    }
    void ayChanged() {
        if (!high(5,true)) { registers.fill(0); ayAddress = 0; ayPorts(); drive(1,0,0); ayOutput(); return; }
        const uint8_t mode = high(4,true) ? static_cast<uint8_t>((high(2) ? 2 : 0)|(high(3) ? 1 : 0)) : 0;
        if (mode != lastMode) {
            uint32_t value = 0;
            if ((mode == 2 || mode == 3) && sample(1,value,255)) {
                if (mode == 3) ayAddress = value&15; else ayWrite(static_cast<uint8_t>(value));
            }
            lastMode = mode;
        }
        drive(1,mode == 1 ? ayRead() : 0,mode == 1 ? 255 : 0);
    }
    void ayTick() {
        for (unsigned channel = 0; channel < 3; ++channel) {
            const unsigned period = std::max(1u,unsigned(registers[channel*2])+(unsigned(registers[channel*2+1])<<8));
            if (--toneCount[channel] == 0) { toneCount[channel] = period; toneHigh[channel] = !toneHigh[channel]; }
        }
        if (--noiseCount == 0) {
            noiseCount = 2*std::max(1u,unsigned(registers[6]));
            noise = (noise>>1)|(((noise^(noise>>3))&1)<<16);
        }
        if (--envelopeCount == 0) {
            envelopeCount = 32*std::max(1u,unsigned(registers[11])+(unsigned(registers[12])<<8));
            if (!envelopeHold && --envelopeStep < 0) {
                const auto shape = registers[13];
                if (!(shape&8)) { envelopeStep = 0; envelopeAttack = 0; envelopeHold = true; }
                else if (shape&1) { if (shape&2) envelopeAttack ^= 15; envelopeStep = 0; envelopeHold = true; }
                else { if (shape&2) envelopeAttack ^= 15; envelopeStep = 15; }
            }
        }
    }
    void ayOutput() {
        static const auto amplitudes = [] {
            std::array<double,16> levels{};
            for (unsigned i=1;i<16;++i) levels[i]=std::pow(10.0,(double(i)-15)*3.0/20.0);
            return levels;
        }();
        double mix = 0;
        for (unsigned channel = 0; channel < 3; ++channel) {
            const unsigned volume = (registers[8+channel]&16) ? unsigned(envelopeStep^envelopeAttack) : registers[8+channel]&15;
            const double level = amplitudes[volume];
            const bool gate = (toneHigh[channel] || (registers[7]&(1u<<channel))) &&
                ((noise&1) || (registers[7]&(8u<<channel)));
            const double sampleValue = level*(gate ? 1 : -1);
            mix += sampleValue;
            (void)api->analog_write(context,pins[9+channel],2.5+2.4*sampleValue);
        }
        (void)api->analog_write(context,pins[8],std::clamp(2.5+0.8*mix,0.1,4.9));
    }
    static void changed(void* user, e51p_context*, e51p_endpoint endpoint, e51p_digital_state, uint64_t) {
        auto& self = *static_cast<e51p_component*>(user);
        switch (self.config.kind) {
        case E51P_COMPONENT_LCD: self.lcdChanged(); break;
        case E51P_COMPONENT_KEYPAD: case E51P_COMPONENT_MATRIX: self.keypadOutput(); break;
        case E51P_COMPONENT_595: case E51P_COMPONENT_165: self.shifts(); break;
        case E51P_COMPONENT_EEPROM: case E51P_COMPONENT_24LC256: self.eepromChanged(); break;
        case E51P_COMPONENT_ULN2003: if (endpoint==self.pins[0]) self.ulnChanged(); break;
        case E51P_COMPONENT_STEPPER: self.stepperChanged(); break;
        case E51P_COMPONENT_ADC0809:
            if (endpoint==self.pins[1] || endpoint==self.pins[2] || endpoint==self.pins[3]) self.adcChanged();
            break;
        case E51P_COMPONENT_AY: self.ayChanged(); break;
        default: break;
        }
    }
    static void reset(void* user, e51p_context*) {
        auto& s = *static_cast<e51p_component*>(user); s.started = true;
        s.bounceStart = s.bounceEnd = 0; s.contact = s.pressed;
        s.shift = s.latch = s.lastClock = s.lastLatch = 0;
        s.ddram.fill(' '); s.cgram.fill(0); s.address = s.highNibble = 0;
        s.fourBit = s.secondNibble = s.cgramSelected = s.display = s.cursor = s.blink = s.entryShift = false;
        s.increment = true; s.displayShift = 0; s.busyUntil = 0; s.lastE = s.readSecond = false;
        if (s.writeUntil) s.eepromCommit();
        s.pageMask = 0;
        s.phase = s.bits = s.ackStage = 0; s.pointer = 0; s.writeUntil = 0;
        s.sdaLow = false; s.lastScl = s.lastSda = true;
        s.registers.fill(0); s.ayAddress = s.lastMode = 0; s.toneCount.fill(1); s.toneHigh.fill(false);
        s.noise = 0x1ffff; s.noiseCount = s.envelopeCount = 1; s.envelopeStep = 15; s.envelopeAttack = 0;
        s.envelopeHold = false; s.clockRemainder = 0;
        switch (s.config.kind) {
        case E51P_COMPONENT_SWITCH: case E51P_COMPONENT_BUTTON: s.contactOutput(); break;
        case E51P_COMPONENT_KEYPAD: case E51P_COMPONENT_MATRIX: s.keypadOutput(); break;
        case E51P_COMPONENT_BUS_LATCH: s.pulseActive = false; s.latchOutput(); s.pulseOutput(); break;
        case E51P_COMPONENT_LCD: s.drive(0,0,0); break;
        case E51P_COMPONENT_595: s.drive(5,0,255); s.drive(6,0,1); break;
        case E51P_COMPONENT_165: s.drive(5,0,1); s.drive(6,1,1); break;
        case E51P_COMPONENT_EEPROM: case E51P_COMPONENT_24LC256: s.drive(1,1,1,1); break;
        case E51P_COMPONENT_STEPPER:
            s.motor = {}; s.previousPhase = -1; s.motorDefined = false; break;
        case E51P_COMPONENT_ADC0809:
            s.adcAddressKnown = s.adcValid = s.adcAle = s.adcStart = s.adcArmed = s.adcInitialized = false;
            s.adcRemaining = 0; s.adcEoc = true; s.adcValue = 0; s.adcOutputs(); break;
        case E51P_COMPONENT_AY: s.ayPorts(); s.drive(1,0,0); s.ayOutput(); break;
        default: break;
        }
    }
    static void advance(void* user, e51p_context*, uint64_t from, uint64_t to) {
        auto& s = *static_cast<e51p_component*>(user);
        if (s.config.kind == E51P_COMPONENT_BUS_LATCH && s.pulseActive && to-s.pulseStart >= s.pulseTicks) {
            s.pulseActive = false; s.pulseOutput();
        }
        if (s.bounceEnd) {
            if (to >= s.bounceEnd) { s.contact = s.pressed; s.bounceEnd = 0; }
            else s.contact = s.pressed ^ (((to-s.bounceStart)/std::max<uint64_t>(1,s.hz/1000))&1);
            s.contactOutput();
        }
        if (s.writeUntil && to >= s.writeUntil) s.eepromCommit();
        if (s.adcRemaining) {
            if (to-from < s.adcRemaining) s.adcRemaining -= to-from;
            else {
                s.adcRemaining = 0; s.adcEoc = true;
                s.adcValid = s.adcPendingValid; s.adcValue = s.adcPending; s.adcOutputs();
            }
        }
        if (s.config.kind == E51P_COMPONENT_AY) {
            const double clocks = s.clockRemainder+double(to-from)*s.config.clock_hz/(8*double(s.hz));
            const auto count = static_cast<uint64_t>(clocks); s.clockRemainder = clocks-double(count);
            for (uint64_t i = 0; i < count; ++i) s.ayTick();
            s.ayOutput();
        }
    }
    static e51p_status busRead(void* user, e51p_context*, e51p_endpoint, uint32_t address, uint32_t, uint32_t* value) {
        auto& s = *static_cast<e51p_component*>(user);
        if (address > 1) return E51P_INVALID_ARGUMENT;
        if (s.config.kind == E51P_COMPONENT_BUS_LATCH) return E51P_UNSUPPORTED;
        if (s.config.kind == E51P_COMPONENT_BUS_BUFFER) {
            e51p_digital_state input{};
            const auto status = s.api->sample(s.context,s.pins[1],&input);
            if (status != E51P_OK) return status;
            if (input.contention) return E51P_CONTENTION;
            if (input.floating) return E51P_INVALID_STATE;
            *value = input.value; return E51P_OK;
        }
        *value = address ? s.ayRead() : s.ayAddress; return E51P_OK;
    }
    static e51p_status busWrite(void* user, e51p_context*, e51p_endpoint, uint32_t address, uint32_t value) {
        auto& s = *static_cast<e51p_component*>(user);
        if (address > 1 || value > 255) return E51P_INVALID_ARGUMENT;
        if (s.config.kind == E51P_COMPONENT_BUS_BUFFER) return E51P_UNSUPPORTED;
        if (s.config.kind == E51P_COMPONENT_BUS_LATCH) {
            s.latch = value; s.latchOutput(); s.pulseStart = s.now(); s.pulseActive = s.pulseTicks != 0; s.pulseOutput();
            return E51P_OK;
        }
        if (address) s.ayWrite(static_cast<uint8_t>(value)); else s.ayAddress = value&15;
        return E51P_OK;
    }
};

extern "C" {
e51p_component_config e51p_component_config_init(uint32_t kind) {
    if (kind == E51P_COMPONENT_MATRIX) return {sizeof(e51p_component_config),kind,1,0,0,2,8,0x50,2000000};
    return {sizeof(e51p_component_config),kind,1,0,0,16,2,0x50,2000000};
}
const e51p_endpoint_desc* e51p_component_pins(uint32_t kind, uint32_t* count) {
    if (!count) return nullptr;
    switch (kind) {
    case E51P_COMPONENT_LED: *count = 1; return ledPins;
    case E51P_COMPONENT_SWITCH: case E51P_COMPONENT_BUTTON: *count = 1; return switchPins;
    case E51P_COMPONENT_LCD: *count = 4; return lcdPins;
    case E51P_COMPONENT_KEYPAD: *count = 2; return keypadPins;
    case E51P_COMPONENT_MATRIX: *count = 3; return matrixPins;
    case E51P_COMPONENT_BUS_LATCH: *count = 5; return latchPins;
    case E51P_COMPONENT_BUS_BUFFER: *count = 2; return bufferPins;
    case E51P_COMPONENT_595: *count = 7; return shift595Pins;
    case E51P_COMPONENT_165: *count = 7; return shift165Pins;
    case E51P_COMPONENT_EEPROM: case E51P_COMPONENT_24LC256: *count = 3; return eepromPins;
    case E51P_COMPONENT_ULN2003: *count = 2; return ulnPins;
    case E51P_COMPONENT_STEPPER: *count = 1; return stepperPins;
    case E51P_COMPONENT_ADC0809: *count = 14; return adc0809Pins;
    case E51P_COMPONENT_AY: *count = 12; return ayPins;
    default: *count = 0; return nullptr;
    }
}
e51p_status e51p_add_component(const e51p_api* api, e51p_context* context, const char* id,
    const e51p_component_config* config, e51p_component** result, e51p_endpoint* pins, uint32_t capacity) {
    uint32_t count = 0;
    const auto* metadata = config ? e51p_component_pins(config->kind,&count) : nullptr;
    if (!api || api->abi_version != E51P_ABI_VERSION || api->struct_size < sizeof(*api) || !context || !config ||
        config->struct_size < sizeof(*config) || !result || !pins || capacity < count || !metadata ||
        (config->active_low != 0 && config->active_low != 1) || (config->initially_pressed != 0 && config->initially_pressed != 1) ||
        (config->kind == E51P_COMPONENT_MATRIX ? (config->columns < 1 || config->columns > 4 || config->rows < 1 || config->rows > 8) :
        (config->columns < 1 || config->columns > 20 || (config->rows != 1 && config->rows != 2 && config->rows != 4))) ||
        config->columns*config->rows > 80 || config->i2c_address < 0x50 || config->i2c_address > 0x57 ||
        !std::isfinite(config->clock_hz) || config->clock_hz < 1000 || config->clock_hz > 4000000) return E51P_INVALID_ARGUMENT;
    auto state = std::unique_ptr<e51p_component>(new(std::nothrow) e51p_component);
    if (!state) return E51P_NO_MEMORY;
    state->api = api; state->context = context; state->config = *config; state->pressed = config->initially_pressed != 0;
    state->pins.fill(E51P_INVALID_ID);
    try {
        if (config->kind == E51P_COMPONENT_EEPROM || state->wideEeprom())
            state->memory.resize(state->wideEeprom() ? 32768 : 256,255);
    } catch (const std::bad_alloc&) { return E51P_NO_MEMORY; }
    uint64_t time = 0;
    if (api->clock(context,&time,&state->hz) != E51P_OK) return E51P_INVALID_STATE;
    e51p_device_desc desc{}; desc.struct_size = sizeof(desc); desc.instance_id = id; desc.user = state.get();
    desc.destroy = [](void* user) { delete static_cast<e51p_component*>(user); };
    desc.reset = e51p_component::reset; desc.advance = e51p_component::advance; desc.digital_changed = e51p_component::changed;
    if (config->kind == E51P_COMPONENT_AY || config->kind == E51P_COMPONENT_BUS_LATCH || config->kind == E51P_COMPONENT_BUS_BUFFER) {
        desc.bus_read = e51p_component::busRead; desc.bus_write = e51p_component::busWrite;
    }
    e51p_device device;
    auto status = api->add_device(context,&desc,&device);
    if (status != E51P_OK) return status;
    auto* owned = state.release();
    for (uint32_t i = 0; i < count; ++i) {
        auto pin = metadata[i];
        if (config->kind == E51P_COMPONENT_SWITCH || config->kind == E51P_COMPONENT_BUTTON) {
            pin.pull_up = config->active_low ? 1 : 0; pin.pull_down = config->active_low ? 0 : 1;
        }
        status = api->add_endpoint(context,device,&pin,&owned->pins[i]);
        if (status != E51P_OK) return status;
    }
    if (config->kind == E51P_COMPONENT_AY || config->kind == E51P_COMPONENT_BUS_LATCH || config->kind == E51P_COMPONENT_BUS_BUFFER) {
        status = e51p_target_set_write_validator(context,owned->pins[0],
            [](void* user, uint32_t address, uint32_t value) -> e51p_status {
                if (static_cast<e51p_component*>(user)->config.kind == E51P_COMPONENT_BUS_BUFFER) return E51P_UNSUPPORTED;
                return address < 2 && value < 256 ? E51P_OK : E51P_INVALID_ARGUMENT;
            },owned);
        if (status != E51P_OK) return status;
    }
    std::copy_n(owned->pins.begin(),count,pins); *result = owned;
    return E51P_OK;
}
e51p_status e51p_component_set_pressed(e51p_component* s, int pressed) {
    if (!s || (pressed != 0 && pressed != 1) || (s->config.kind != E51P_COMPONENT_SWITCH && s->config.kind != E51P_COMPONENT_BUTTON))
        return E51P_INVALID_ARGUMENT;
    if (s->pressed == bool(pressed)) return E51P_OK;
    s->pressed = pressed != 0;
    if (s->config.kind == E51P_COMPONENT_BUTTON && s->config.bounce_ticks) {
        s->bounceStart = s->now(); s->bounceEnd = s->bounceStart+s->config.bounce_ticks;
    } else s->bounceEnd = 0;
    s->contact = s->pressed; s->contactOutput(); return E51P_OK;
}
e51p_status e51p_component_set_keys(e51p_component* s, uint32_t mask) {
    if (!s || (s->config.kind != E51P_COMPONENT_KEYPAD && s->config.kind != E51P_COMPONENT_MATRIX)) return E51P_INVALID_ARGUMENT;
    const unsigned count = s->config.kind == E51P_COMPONENT_MATRIX ? s->config.rows*s->config.columns+s->directKeys : 16;
    if (count < 32 && mask >= (1u<<count)) return E51P_INVALID_ARGUMENT;
    s->keys = mask; s->keypadOutput(); return E51P_OK;
}
e51p_status e51p_component_set_latch_pulse_ticks(e51p_component* s, uint64_t ticks) {
    if (!s || s->config.kind != E51P_COMPONENT_BUS_LATCH) return E51P_INVALID_ARGUMENT;
    if (s->started) return E51P_INVALID_STATE;
    s->pulseTicks = ticks; return E51P_OK;
}
e51p_status e51p_component_set_matrix_direct_keys(e51p_component* s, uint32_t count) {
    if (!s || s->config.kind != E51P_COMPONENT_MATRIX || count > 8 || count+s->config.rows*s->config.columns > 32) return E51P_INVALID_ARGUMENT;
    if (s->started) return E51P_INVALID_STATE;
    s->directKeys = count; return E51P_OK;
}
e51p_status e51p_component_get_led(const e51p_component* s, int* on, int* defined) {
    if (!s || s->config.kind != E51P_COMPONENT_LED || !on || !defined) return E51P_INVALID_ARGUMENT;
    uint32_t value = 0; *defined = s->sample(0,value); *on = *defined && ((value&1) != unsigned(s->config.active_low)); return E51P_OK;
}
e51p_status e51p_component_get_lcd(const e51p_component* s, e51p_lcd_snapshot* out) {
    if (!s || s->config.kind != E51P_COMPONENT_LCD || !out || out->struct_size < sizeof(*out)) return E51P_INVALID_ARGUMENT;
    e51p_lcd_snapshot result{}; result.struct_size = sizeof(result); result.columns = s->config.columns; result.rows = s->config.rows;
    result.display_on = s->display; result.cursor_on = s->cursor; result.blink_on = s->blink; result.cursor_cell = -1;
    std::copy(s->cgram.begin(),s->cgram.end(),result.cgram);
    for (unsigned r = 0; r < result.rows; ++r) for (unsigned c = 0; c < result.columns; ++c) {
        const unsigned base = (r&1)*64+(r>=2 ? result.columns : 0);
        const unsigned address = (base&64)|(((base&63)+c+unsigned(s->displayShift))%40);
        const auto cell = r*result.columns+c;
        result.cells[cell] = s->ddram[address];
        if (!s->cgramSelected && address == s->address) result.cursor_cell = static_cast<int>(cell);
    }
    *out = result; return E51P_OK;
}
e51p_status e51p_component_get_stepper(const e51p_component* s, e51p_stepper_snapshot* out) {
    if (!s || s->config.kind!=E51P_COMPONENT_STEPPER || !out) return E51P_INVALID_ARGUMENT;
    *out = s->motor;
    return E51P_OK;
}
e51p_status e51p_component_set_adc0809(e51p_component* s, uint64_t ticks, double span) {
    if (!s || s->config.kind!=E51P_COMPONENT_ADC0809 || !ticks || !std::isfinite(span) || span<=0) return E51P_INVALID_ARGUMENT;
    if (s->started) return E51P_INVALID_STATE;
    s->conversionTicks = ticks; s->adcSpan = span;
    return E51P_OK;
}
e51p_status e51p_component_eeprom_import(e51p_component* s, const uint8_t* bytes, uint32_t size) {
    if (!s || s->memory.empty() || !bytes || size != s->memory.size()) return E51P_INVALID_ARGUMENT;
    if (s->started && s->now() != 0) return E51P_INVALID_STATE;
    if (!std::equal(s->memory.begin(),s->memory.end(),bytes)) {
        std::copy_n(bytes,size,s->memory.begin());
        ++s->storageRevision;
    }
    return E51P_OK;
}
e51p_status e51p_component_eeprom_export(const e51p_component* s, uint8_t* bytes, uint32_t size) {
    if (!s || s->memory.empty() || !bytes || size != s->memory.size()) return E51P_INVALID_ARGUMENT;
    std::copy(s->memory.begin(),s->memory.end(),bytes); return E51P_OK;
}
uint64_t e51p_component_eeprom_revision(const e51p_component* s) { return s ? s->storageRevision : 0; }
}
