// SPDX-License-Identifier: GPL-2.0-only
#include "emu51.h"

#include <array>
#include <cstdio>
#include <initializer_list>

namespace {
int failures;
void check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "[FAIL] model: %s\n", message); ++failures; }
}
struct Machine {
    std::array<std::uint8_t, 65536> memory{};
    emu51 cpu{memory.data()};
    bool table;
    explicit Machine(bool useTable, Emu51Model model = Emu51Model::i8052) : table(useTable) { cpu.setModel(model); }
    int step() { return table ? cpu.process_table() : cpu.process(); }
    void cycles(unsigned count) { for (unsigned i = 0; i < count; ++i) step(); }
    void instruction(std::initializer_list<std::uint8_t> bytes) {
        cpu.PC = 0x1000;
        unsigned pos = cpu.PC;
        for (const auto byte : bytes) memory[pos++] = byte;
        step();
    }
    void timer2(unsigned control, unsigned reload = 0, unsigned value = 0) {
        cpu.writeDirect(0xca, static_cast<std::uint8_t>(reload));
        cpu.writeDirect(0xcb, static_cast<std::uint8_t>(reload >> 8));
        cpu.writeDirect(0xcc, static_cast<std::uint8_t>(value));
        cpu.writeDirect(0xcd, static_cast<std::uint8_t>(value >> 8));
        cpu.writeDirect(0xc8, static_cast<std::uint8_t>(control));
    }
    unsigned timerValue() const { return (cpu.readDirect(0xcd) << 8) | cpu.readDirect(0xcc); }
    unsigned captureValue() const { return (cpu.readDirect(0xcb) << 8) | cpu.readDirect(0xca); }
};

void memory(bool table)
{
    for (const auto model : {Emu51Model::i8051, Emu51Model::i8052}) {
        Machine m(table, model);
        m.instruction({0x78, 0x90}); // MOV R0,#90h
        m.instruction({0x76, 0x12}); // MOV @R0,#12h
        check(m.cpu.P1 == 0xff, "Indirect upper RAM cannot write P1 on either model");
        check(m.cpu.readIndirect(0x90) == (model == Emu51Model::i8052 ? 0x12 : 0xff),
            "Only 8052 implements upper internal RAM");
        m.instruction({0x75, 0x90, 0x34}); // MOV P1,#34h
        check(m.cpu.P1 == 0x34 && m.cpu.readIndirect(0x90) != 0x34,
            "Direct SFR writes are isolated from upper RAM");
        m.instruction({0xd2, 0x07}); // SETB RAM bit 7
        check(m.cpu.IRAM[0x20] == 0x80 && m.cpu.readDirect(0x20) == 0x80,
            "Bit-addressable RAM shares lower internal RAM");
        m.cpu.writeDirect(0xc8, 0xff);
        check(m.cpu.readDirect(0xc8) == 0xff, "Timer 2 register reads and absent open bus are deterministic");
        m.instruction({0xc2, 0xca}); // CLR TR2
        check(m.cpu.readDirect(0xc8) == (model == Emu51Model::i8052 ? 0xfb : 0xff),
            "Timer 2 bit writes are enabled only for 8052");
        m.cpu.writeDirect(0xc9, 0xaa); m.cpu.writeDirect(0xf8, 0);
        check(!m.cpu.hasSfr(0xc9) && m.cpu.readDirect(0xc9) == 0xff && m.cpu.readDirect(0xf8) == 0xff,
            "Classic models do not implement derivative T2MOD or reserved SFRs");
        m.cpu.init();
        check(m.cpu.model() == model && m.cpu.P1 == 0xff && m.cpu.SP == 7 &&
            m.cpu.IRAM[0x20] == 0 && m.cpu.IRAM[0x90] == 0,
            "Reset preserves model and resets both memory regions");
    }
    Machine m(table);
    m.cpu.SP = 0x7f;
    m.cpu.writeDirect(0x30, 0xa5);
    m.instruction({0xc0, 0x30}); // PUSH 30h across upper RAM boundary
    check(m.cpu.SP == 0x80 && m.cpu.IRAM[0x80] == 0xa5 && m.cpu.P0 == 0xff,
        "PUSH crosses 7Fh into upper RAM without touching P0");
    m.instruction({0xd0, 0x31});
    check(m.cpu.SP == 0x7f && m.cpu.readDirect(0x31) == 0xa5, "POP reads upper RAM");
    m.instruction({0x12, 0x20, 0x00});
    check(m.cpu.SP == 0x81 && m.cpu.IRAM[0x80] == 3 && m.cpu.IRAM[0x81] == 0x10,
        "LCALL stores return address in upper RAM independently of SP SFR");
    m.instruction({0x22});
    check(m.cpu.SP == 0x7f && m.cpu.PC == 0x1003, "RET restores an upper RAM return address");
    m.cpu.SP = 0xff; m.cpu.writeDirect(0x30, 0x5a);
    m.instruction({0xc0, 0x30});
    check(m.cpu.SP == 0 && m.cpu.IRAM[0] == 0x5a, "Stack wraps at FFh on 8052");
    m.cpu.setModel(Emu51Model::i8051);
    check(m.cpu.modelInfo().internalRamBytes == 128 && m.cpu.PC == 0 && m.cpu.IRAM[0x80] == 0,
        "Changing model resets CPU and clears hidden upper RAM");
}

void indirectOperands(bool table)
{
    struct Vector { unsigned opcode, accumulator, result, ram; };
    // Initial indirect byte A5h, direct byte 30h=33h; CY initially clear.
    constexpr Vector vectors[] = {
        {0x06, 0x3c, 0x3c, 0xa6}, {0x16, 0x3c, 0x3c, 0xa4},
        {0x26, 1, 0xa6, 0xa5}, {0x36, 1, 0xa6, 0xa5},
        {0x46, 0x0f, 0xaf, 0xa5}, {0x56, 0x0f, 5, 0xa5},
        {0x66, 0x0f, 0xaa, 0xa5}, {0x96, 0xb0, 0x0b, 0xa5},
        {0xa6, 0x3c, 0x3c, 0x33}, {0xb6, 0x3c, 0x3c, 0xa5},
        {0xc6, 0x3c, 0xa5, 0x3c}, {0xd6, 0x3c, 0x35, 0xac},
        {0xe6, 0x3c, 0xa5, 0xa5}, {0xf6, 0x3c, 0x3c, 0x3c},
    };
    for (const unsigned index : {0u, 1u}) {
        for (const auto& v : vectors) {
            Machine m(table);
            m.cpu.Ri(static_cast<int>(index), 0x90);
            m.cpu.IRAM[0x90] = 0xa5; m.cpu.Acc = static_cast<std::uint8_t>(v.accumulator);
            m.cpu.writeDirect(0x30, 0x33);
            m.instruction({static_cast<std::uint8_t>(v.opcode + index),
                static_cast<std::uint8_t>(v.opcode == 0xb6 ? 0xa5 : 0x30), 0x20});
            check(m.cpu.Acc == v.result && m.cpu.IRAM[0x90] == v.ram && m.cpu.P1 == 0xff,
                "Indirect arithmetic, logic, exchange and move operands use upper RAM for R0 and R1");
        }
        Machine m(table); m.cpu.Ri(static_cast<int>(index), 0x90); m.cpu.IRAM[0x90] = 0xa5;
        m.instruction({static_cast<std::uint8_t>(0x86 + index), 0x31});
        check(m.cpu.readDirect(0x31) == 0xa5 && m.cpu.P1 == 0xff,
            "MOV direct,@Ri reads upper RAM through either pointer");
    }
}

void timers(bool table)
{
    Machine stopped(table);
    stopped.timer2(0, 0x1234, 0xfffe); stopped.cycles(4);
    check(stopped.timerValue() == 0xfffe, "TR2 gates the timer");
    Machine reload(table);
    reload.timer2(4, 0xfffe, 0xffff);
    reload.step();
    check(reload.timerValue() == 0xfffe && (reload.cpu.readDirect(0xc8) & bit7),
        "Auto-reload overflow reloads RCAP2 and sets TF2");
    reload.cpu.writeDirect(0xc8, 4); reload.cycles(2);
    check(reload.timerValue() == 0xfffe && (reload.cpu.readDirect(0xc8) & bit7),
        "Auto-reload repeats and sticky flags do not prevent counting");
    Machine capture(table);
    capture.timer2(5, 0xabcd, 0xffff); capture.step();
    check(capture.timerValue() == 0 && capture.captureValue() == 0xabcd && (capture.cpu.readDirect(0xc8) & bit7),
        "Capture mode overflow wraps without reloading");
    capture.timer2(0x0d, 0, 0x1234); capture.cpu.setTimer2Inputs(true, false); capture.step();
    check(capture.captureValue() == 0x1235 && capture.timerValue() == 0x1235 && (capture.cpu.readDirect(0xc8) & bit6),
        "T2EX falling edge captures the counter and sets EXF2");
    capture.cpu.writeDirect(0xc8, 0x0d); capture.step();
    check(capture.captureValue() == 0x1235 && !(capture.cpu.readDirect(0xc8) & bit6),
        "A held-low T2EX does not retrigger");
    Machine externalReload(table);
    externalReload.timer2(0x0c, 0x4567, 0x1111);
    externalReload.cpu.setTimer2Inputs(true, false); externalReload.step();
    check(externalReload.timerValue() == 0x4567 && (externalReload.cpu.readDirect(0xc8) & bit6),
        "T2EX falling edge reloads in auto-reload mode");
    Machine counter(table);
    counter.timer2(6, 0x1000, 0xfffe); counter.cycles(4);
    check(counter.timerValue() == 0xfffe, "Counter mode ignores machine-cycle clocks");
    counter.cpu.setTimer2Inputs(false, true); counter.step(); counter.cycles(3);
    check(counter.timerValue() == 0xffff, "A T2 falling edge increments once");
    counter.cpu.setTimer2Inputs(true, true); counter.step();
    counter.cpu.setTimer2Inputs(false, true); counter.step();
    check(counter.timerValue() == 0x1000 && (counter.cpu.readDirect(0xc8) & bit7),
        "External counter overflow auto-reloads");
    Machine baud(table);
    baud.timer2(0x3d, 0xffff, 0xffff); baud.memory[0] = 0xa4; // MUL = 4 machine cycles
    baud.step();
    check(baud.timerValue() == 0xffff && !(baud.cpu.readDirect(0xc8) & bit7),
        "Baud mode reloads six times per machine cycle and suppresses TF2");
    baud.cpu.setTimer2Inputs(true, false); baud.step();
    check((baud.cpu.readDirect(0xc8) & bit6) && baud.captureValue() == 0xffff,
        "Baud mode T2EX sets EXF2 without capture or external reload");
    Machine masked(table, Emu51Model::i8051);
    masked.cpu.SFR[0xc8] = 0xff; masked.cpu.IE = 0xa0;
    check(masked.step() == 1 && masked.cpu.PC == 1, "8051 cannot clock or interrupt from Timer 2");
}

void interrupts(bool table)
{
    Machine m(table);
    m.cpu.SP = 0x7f; m.cpu.IE = 0xa0;
    m.timer2(4, 0, 0xffff);
    check(m.step() == 3 && m.cpu.PC == 0x2b && m.cpu.SP == 0x81 && m.cpu.IRAM[0x80] == 1 &&
        m.cpu.IRAM[0x81] == 0 && m.timerValue() == 2,
        "Timer 2 interrupts use vector 2Bh, upper RAM stack and two entry cycles");
    check(m.cpu.readDirect(0xc8) & bit7, "Timer 2 flags are not cleared by vectoring");
    m.memory[0x2b] = 0x32; // RETI without clearing TF2
    check(m.step() == 4 && m.cpu.PC == 0x2b, "Uncleared TF2 retriggers after RETI");
    m.cpu.writeDirect(0xc8, 0); m.step();
    check(m.cpu.PC == 1 && m.cpu.SP == 0x7f, "Cleared Timer 2 flags permit return");
    Machine flags(table);
    flags.cpu.IE = 0xa0; flags.cpu.writeDirect(0xc8, bit6);
    check(flags.step() == 3 && flags.cpu.PC == 0x2b, "EXF2 alone can request a Timer 2 interrupt");
    Machine priority(table);
    priority.cpu.IE = 0xa2; priority.cpu.TCON = bit5; priority.cpu.writeDirect(0xc8, bit7);
    priority.step();
    check(priority.cpu.PC == 0xb, "Timer 0 wins equal-priority polling before Timer 2");
    priority.cpu.IP = bit5; priority.step();
    check(priority.cpu.PC == 0x2b, "High-priority Timer 2 preempts a low-priority interrupt");
}

void uart(bool table)
{
    // RCAP=FFFA makes one Timer 2 overflow per machine cycle (oscillator/2).
    // SMOD must not affect Timer 2's fixed divide-by-16 UART clock.
    for (const bool smod : {false, true}) {
        Machine m(table);
        m.timer2(0x34, 0xfffa, 0xfffa); m.cpu.writeDirect(0x98, 0x50);
        m.cpu.PCON = smod ? bit7 : 0;
        m.cpu.writeDirect(0x99, 0x41); m.cpu.queueSerialReceive(0x55);
        m.cycles(143); check(!(m.cpu.SCON & 3), "Timer 2 UART flags are not early");
        m.step(); check((m.cpu.SCON & bit1) && !(m.cpu.SCON & bit0), "Timer 2 TI is set at bit nine");
        m.cycles(7); check(!(m.cpu.SCON & bit0), "Timer 2 RI waits for mid-stop-bit sample");
        m.step(); check((m.cpu.SCON & bit0) && m.cpu.SBUF == 0x55, "Timer 2 RX samples at 152 cycles regardless of SMOD");
        m.cycles(8); const auto output = m.cpu.takeSerialTransmit();
        check(output && output->value == 0x41 && !(m.cpu.readDirect(0xc8) & bit7),
            "Timer 2 transmits a complete frame without TF2 interrupts");
    }
    for (const bool txTimer2 : {false, true}) {
        Machine m(table);
        m.timer2(txTimer2 ? 0x14 : 0x24, 0xfffa, 0xfffa);
        m.cpu.TMOD = 0x20; m.cpu.TH1 = m.cpu.TL1 = 0xff; m.cpu.TCON = bit6;
        m.cpu.writeDirect(0x98, 0x50); m.cpu.writeDirect(0x99, 0x42); m.cpu.queueSerialReceive(0x24);
        m.cycles(160);
        check(m.cpu.takeSerialTransmit().has_value() == txTimer2 && bool(m.cpu.SCON & bit0) == !txTimer2,
            "TCLK and RCLK select independent transmit and receive clocks");
        m.cycles(160);
        if (!txTimer2) check(m.cpu.takeSerialTransmit().has_value(), "Timer 1 still clocks TX when RCLK selects Timer 2");
        else check((m.cpu.SCON & bit0) && m.cpu.SBUF == 0x24, "Timer 1 still clocks RX when TCLK selects Timer 2");
    }
    Machine rapid(table);
    rapid.timer2(0x34, 0xffff, 0xffff); rapid.cpu.writeDirect(0x98, 0x50);
    rapid.cpu.writeDirect(0x99, 0xa5); rapid.cpu.queueSerialReceive(0x5a);
    rapid.memory[0] = 0xa4; rapid.step(); rapid.cycles(22); // 26 cycles => 156 overflows
    check((rapid.cpu.SCON & 3) == 3 && rapid.cpu.SBUF == 0x5a && !rapid.cpu.takeSerialTransmit(),
        "All Timer 2 overflows within multi-cycle instructions clock UART flags");
    rapid.step(); check(rapid.cpu.takeSerialTransmit().has_value(), "Multiple overflows finish UART frame at the next boundary");
    Machine nine(table);
    nine.timer2(0x34, 0xfffa, 0xfffa); nine.cpu.writeDirect(0x98, 0xd8);
    nine.cpu.writeDirect(0x99, 0x33); nine.cpu.queueSerialReceive(0x44, true);
    nine.cycles(167);
    check((nine.cpu.SCON & bit1) && !(nine.cpu.SCON & bit0), "Timer 2 mode 3 RI waits through the ninth bit");
    nine.step();
    check((nine.cpu.SCON & (bit0 | bit2)) == (bit0 | bit2) && nine.cpu.SBUF == 0x44,
        "Timer 2 mode 3 receives the ninth bit");
    nine.cycles(8); const auto ninth = nine.cpu.takeSerialTransmit();
    check(ninth && ninth->value == 0x33 && ninth->ninth, "Timer 2 mode 3 transmits an eleven-bit frame");
    Machine stopped(table);
    stopped.timer2(0x30, 0xfffa, 0xfffa); stopped.cpu.writeDirect(0x98, 0x50);
    stopped.cpu.writeDirect(0x99, 1); stopped.cpu.queueSerialReceive(2); stopped.cycles(400);
    check(!(stopped.cpu.SCON & 3) && stopped.cpu.serialTransmitBusy(), "TR2 stops Timer 2 UART clocks");
}
} // namespace

int emu51_model_tests()
{
    failures = 0;
    for (const bool table : {false, true}) { memory(table); indirectOperands(table); timers(table); interrupts(table); uart(table); }
    return failures;
}
