// SPDX-License-Identifier: GPL-2.0-only
#include "emu51.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

namespace {
int failures;
void check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "[FAIL] serial: %s\n", message); ++failures; }
}
struct Machine {
    std::array<std::uint8_t, 65536> memory{};
    emu51 cpu{memory.data()};
    bool table;
    explicit Machine(bool useTable) : table(useTable) {}
    int step() { return table ? cpu.process_table() : cpu.process(); }
    void cycles(unsigned count) { for (unsigned i = 0; i < count; ++i) step(); }
    void timerUart(std::uint8_t scon = 0x50) {
        cpu.writeDirect(0x98, scon);
        cpu.TMOD = 0x20; cpu.TH1 = cpu.TL1 = 0xff; cpu.TCON = 0x40;
    }
};

void timing(bool table)
{
    // Timer 1 overflows once per machine cycle; 32 overflows per bit. Mode 1
    // sets TI at bit 9, samples RX at bit 9.5, and finishes a ten-bit frame.
    Machine m(table);
    m.timerUart(); m.cpu.SBUF = 0x12; m.cpu.writeDirect(0x99, 0x41);
    check(m.cpu.SBUF == 0x12, "SBUF transmit writes do not change the receive buffer");
    m.cycles(287);
    check(!(m.cpu.SCON & bit1), "Mode 1 TI stays clear for 287 overflow clocks");
    m.cycles(1);
    check((m.cpu.SCON & bit1) && !m.cpu.takeSerialTransmit(), "TI is set at the start of the stop bit, before complete output");
    // Writing the same byte after TI must preserve the first stop bit.
    m.cpu.SCON &= ~bit1; m.cpu.writeDirect(0x99, 0x41);
    m.cycles(32);
    const auto first = m.cpu.takeSerialTransmit();
    check(first && first->value == 0x41 && m.cpu.serialTransmitBusy(), "The next SBUF write waits for the first stop bit");
    m.cycles(320);
    const auto second = m.cpu.takeSerialTransmit();
    check(second && second->value == 0x41 && !m.cpu.serialTransmitBusy(), "Equal consecutive SBUF writes send two frames");

    Machine receive(table);
    receive.timerUart(); receive.cpu.queueSerialReceive(0x55); receive.cpu.queueSerialReceive(0xaa);
    receive.cycles(303);
    check(!(receive.cpu.SCON & bit0), "Mode 1 RI stays clear before the middle of the stop bit");
    receive.cycles(1);
    check((receive.cpu.SCON & (bit0 | bit2)) == (bit0 | bit2) && receive.cpu.readDirect(0x99) == 0x55,
        "Mode 1 receives a byte and stop bit at 304 overflow clocks");
    check((receive.cpu.SCON & bit0) && receive.cpu.readDirect(0x99) == 0x55, "Reading SBUF does not clear RI");
    receive.cycles(320);
    check(receive.cpu.SBUF == 0x55 && receive.cpu.serialReceiveOverruns() == 1,
        "An uncleared RI preserves unread data and counts the next received frame as lost");

    Machine doubled(table);
    doubled.timerUart(); doubled.cpu.PCON = bit7;
    doubled.cpu.writeDirect(0x99, 0x42); doubled.cpu.queueSerialReceive(0x24);
    doubled.cycles(143); check(!(doubled.cpu.SCON & bit1), "SMOD TI is not early");
    doubled.cycles(1); check(doubled.cpu.SCON & bit1, "SMOD doubles the Timer 1 UART baud rate");
    doubled.cycles(7); check(!(doubled.cpu.SCON & bit0), "SMOD RX is not early");
    doubled.cycles(1); check(doubled.cpu.SCON & bit0, "SMOD receives at 152 overflow clocks");
    doubled.cycles(8); check(doubled.cpu.takeSerialTransmit().has_value(), "SMOD frame completes at 160 overflow clocks");

    Machine halted(table);
    halted.timerUart(); halted.cpu.TCON = 0;
    halted.cpu.writeDirect(0x99, 0x11); halted.cpu.queueSerialReceive(0x22);
    halted.cycles(1000);
    check(!(halted.cpu.SCON & 3) && halted.cpu.pendingSerialReceiveCount() == 1, "Stopped Timer 1 stops both serial directions");
    halted.cpu.TCON = 0x40; halted.cpu.SCON &= ~bit4;
    halted.cycles(320);
    check(halted.cpu.takeSerialTransmit().has_value() && !(halted.cpu.SCON & bit0), "REN disables reception without disabling transmission");
    halted.cpu.SCON |= bit4; halted.cycles(304);
    check(halted.cpu.SBUF == 0x22 && (halted.cpu.SCON & bit0), "Host input waits until REN is enabled");

    Machine multi(table);
    multi.timerUart(); multi.memory[0] = 0xa4; // MUL AB takes four machine cycles.
    multi.cpu.writeDirect(0x99, 0x77);
    multi.step(); multi.cycles(316);
    const auto byte = multi.cpu.takeSerialTransmit();
    check(byte && byte->value == 0x77, "Every mode 2 Timer 1 overflow within a multi-cycle instruction clocks the UART");
    for (const auto tmod : {0x60, 0xa0, 0x23, 0x30}) {
        Machine gated(table);
        gated.timerUart(); gated.cpu.TMOD = static_cast<std::uint8_t>(tmod); gated.cpu.P3 &= ~bit3;
        gated.cpu.writeDirect(0x99, 0x33); gated.cycles(1000);
        check(!(gated.cpu.SCON & bit1) && !gated.cpu.takeSerialTransmit(),
            "Counter input, a low GATE pin, and split/stopped timer modes do not fabricate UART clocks");
    }
}

void modes(bool table)
{
    Machine shift(table);
    shift.cpu.writeDirect(0x98, 0x10);
    shift.cpu.writeDirect(0x99, 0xa5); shift.cpu.queueSerialReceive(0x5a);
    shift.cycles(7); check(!(shift.cpu.SCON & 3), "Mode 0 flags remain clear for seven shift clocks");
    shift.cycles(1);
    const auto output = shift.cpu.takeSerialTransmit();
    check(output && output->value == 0xa5 && shift.cpu.SBUF == 0x5a && (shift.cpu.SCON & 3) == 3,
        "Mode 0 sends and receives eight data bits at one bit per machine cycle");
    shift.cpu.queueSerialReceive(0x33); shift.cycles(20);
    check(shift.cpu.pendingSerialReceiveCount() == 1 && shift.cpu.SBUF == 0x5a, "Mode 0 waits for RI to be cleared before receiving again");
    shift.cpu.SCON &= ~bit0; shift.cycles(8);
    check(shift.cpu.SBUF == 0x33, "Mode 0 resumes reception after software clears RI");

    for (bool smod : {false, true}) {
        Machine fixed(table);
        fixed.cpu.writeDirect(0x98, 0x98); // Mode 2, REN and TB8.
        fixed.cpu.PCON = smod ? bit7 : 0;
        fixed.cpu.writeDirect(0x99, 0x66); fixed.cpu.queueSerialReceive(0x44, true);
        const unsigned txFlag = smod ? 27 : 54;
        fixed.cycles(txFlag - 1); check(!(fixed.cpu.SCON & bit1), "Mode 2 TI is not early");
        fixed.cycles(1); check(fixed.cpu.SCON & bit1, "Mode 2 uses the oscillator /64 or /32 baud clock");
        const unsigned rxFlag = smod ? 28 : 56;
        fixed.cycles(rxFlag - txFlag);
        check(fixed.cpu.SBUF == 0x44 && (fixed.cpu.SCON & 5) == 5, "Mode 2 receives the ninth bit into RB8");
        fixed.cycles((smod ? 30 : 59) - rxFlag);
        const auto sent = fixed.cpu.takeSerialTransmit();
        check(sent && sent->ninth && sent->value == 0x66, "Mode 2 sends the latched TB8 in an eleven-bit frame");
    }

    Machine ninth(table);
    ninth.timerUart(0xf8); // Mode 3, SM2, REN, TB8.
    ninth.cpu.writeDirect(0x99, 0x99); ninth.cpu.SCON &= ~bit3;
    ninth.cpu.queueSerialReceive(0x10, false); ninth.cpu.queueSerialReceive(0x20, true);
    ninth.cycles(319); check(!(ninth.cpu.SCON & bit1), "Mode 3 TI is not early");
    ninth.cycles(1); check(ninth.cpu.SCON & bit1, "Mode 3 TI accounts for the ninth data bit");
    ninth.cycles(16); check(!(ninth.cpu.SCON & bit0), "SM2 rejects a mode 3 frame with a clear ninth bit");
    ninth.cycles(16);
    const auto sent = ninth.cpu.takeSerialTransmit();
    check(sent && sent->ninth, "Changing TB8 after the write does not change the byte in flight");
    ninth.cycles(336);
    check(ninth.cpu.SBUF == 0x20 && (ninth.cpu.SCON & 5) == 5, "SM2 accepts a mode 3 frame with a set ninth bit");

    Machine stop(table);
    stop.timerUart(0x70); stop.cpu.queueSerialReceive(0x11, false, false); stop.cycles(320);
    check(!(stop.cpu.SCON & bit0), "Mode 1 SM2 rejects an invalid stop bit");
    stop.cpu.SCON &= ~bit5; stop.cpu.queueSerialReceive(0x22, false, false); stop.cycles(304);
    check(stop.cpu.SBUF == 0x22 && (stop.cpu.SCON & bit0) && !(stop.cpu.SCON & bit2), "Mode 1 without SM2 reports an invalid stop bit in RB8");
}

void directWrites(bool table)
{
    struct Instruction { std::vector<std::uint8_t> bytes; std::uint8_t rx = 0x55, acc = 0x55; };
    std::vector<Instruction> instructions = {
        {{0x75,0x99,0x55}}, {{0xf5,0x99}}, {{0x85,0x30,0x99}}, {{0x86,0x99}}, {{0x87,0x99}},
        {{0xc5,0x99}}, {{0xd0,0x99}}, {{0x05,0x99},0x54}, {{0x15,0x99},0x56},
        {{0x42,0x99},0x55,0}, {{0x43,0x99,0}}, {{0x52,0x99},0x55,0xff}, {{0x53,0x99,0xff}},
        {{0x62,0x99},0x55,0}, {{0x63,0x99,0}}, {{0xd5,0x99,0},0x56},
    };
    for (unsigned op = 0x88; op <= 0x8f; ++op) instructions.push_back({{static_cast<std::uint8_t>(op),0x99}});
    for (const auto& instruction : instructions) {
        Machine m(table);
        std::copy(instruction.bytes.begin(), instruction.bytes.end(), m.memory.begin());
        m.cpu.SBUF = instruction.rx; m.cpu.Acc = instruction.acc;
        for (int i = 0; i < 8; ++i) m.cpu.Ri(i, 0x55);
        m.cpu.IRAM[0x30] = 0x55;
        if (instruction.bytes[0] == 0x86) m.cpu.Ri(0, 0x30);
        if (instruction.bytes[0] == 0x87) m.cpu.Ri(1, 0x30);
        if (instruction.bytes[0] == 0xd0) m.cpu.SP = 0x30;
        const int cost = m.step();
        m.cycles(8 - cost);
        const auto byte = m.cpu.takeSerialTransmit();
        check(byte && byte->value == 0x55 && m.cpu.SBUF == instruction.rx, "Every direct SBUF destination triggers TX while preserving RX, including same-value RMW writes");
    }
}

void interruptsAndReset(bool table)
{
    Machine m(table);
    m.cpu.IE = 0x90; m.cpu.SCON = 2; m.cpu.PC = 0x100;
    check(m.step() == 3 && m.cpu.PC == 0x23 && m.cpu.SP == 9 && (m.cpu.SCON & bit1),
        "EA/ES route a pending serial request to 0023 without clearing TI");
    m.memory[0x23] = 0x32; // RETI
    m.cpu.SCON = 0; m.step();
    check(m.cpu.PC == 0x101 && m.cpu.SP == 7, "Software flag clearing and RETI return from the serial interrupt");
    m.cpu.IE = 0x92; m.cpu.IP = 0; m.cpu.TCON = 0x20; m.cpu.SCON = 2;
    m.step(); check(m.cpu.PC == 0xb, "Timer 0 wins equal-priority arbitration ahead of serial");
    m.cpu.IP = 0x10; m.step();
    check(m.cpu.PC == 0x23 && m.cpu.SP == 11, "High-priority serial can preempt a low-priority timer handler");

    Machine entry(table);
    entry.cpu.writeDirect(0x99, 0x71); entry.cycles(5);
    entry.cpu.IE = 0x82; entry.cpu.TCON = 0x20;
    check(entry.step() == 3, "Timer interrupt entry consumes its two extra cycles");
    const auto byte = entry.cpu.takeSerialTransmit();
    check(byte && byte->value == 0x71 && (entry.cpu.SCON & bit1), "UART clocks continue during interrupt entry");

    Machine reset(table);
    for (std::size_t i = 0; i < emu51::kSerialQueueCapacity; ++i) check(reset.cpu.queueSerialReceive(0), "Serial host queue accepts its documented capacity");
    check(!reset.cpu.queueSerialReceive(0), "Serial host input is bounded");
    reset.cpu.writeDirect(0x99, 0x12); reset.cpu.writeDirect(0x99, 0x34);
    check(reset.cpu.serialTransmitOverwrites() == 1, "Premature SBUF writes are reported");
    reset.cpu.init();
    check(!reset.cpu.serialTransmitBusy() && !reset.cpu.takeSerialTransmit() && !reset.cpu.pendingSerialReceiveCount() &&
        !reset.cpu.serialTransmitOverwrites() && !reset.cpu.serialReceiveOverruns(), "Reset clears pending serial frames, buffers and counters");
    for (std::size_t i = 0; i < emu51::kSerialQueueCapacity + 1; ++i) {
        reset.cpu.writeDirect(0x99, static_cast<std::uint8_t>(i)); reset.cycles(8);
    }
    const auto oldest = reset.cpu.takeSerialTransmit();
    check(oldest && oldest->value == 1 && reset.cpu.serialOutputDrops() == 1,
        "A stalled serial consumer retains only the newest bounded output and reports discarded bytes");
    reset.cpu.writeDirect(0x98, 0x40); reset.cpu.writeDirect(0x99, 0x12);
    reset.cpu.writeDirect(0x98, 0x80);
    check(!reset.cpu.serialTransmitBusy(), "Changing serial modes aborts an in-flight frame");
    reset.cpu.writeDirect(0x99, 0x34); reset.cycles(40); reset.cpu.PCON |= bit7;
    reset.cycles(1);
    check(!reset.cpu.serialTransmitBusy(), "Changing SMOD during a frame cannot underflow its remaining clock count");
}
} // namespace

int emu51_serial_tests()
{
    failures = 0;
    for (bool table : {false, true}) {
        timing(table); modes(table); directWrites(table); interruptsAndReset(table);
    }
    return failures;
}
