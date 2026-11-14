// SPDX-License-Identifier: GPL-2.0-only
#include "emu51.h"

#include <array>
#include <cstdio>

namespace {
// Independent expectations from the Atmel 8051 Hardware Manual, section 1.
// https://www.microchip.com/content/dam/mchp/documents/OTH/ProductDocuments/UserGuides/doc4316.pdf
int failures;
unsigned long cases;
void check(bool condition, const char* message)
{
    ++cases;
    if (!condition) {
        if (failures < 12) std::fprintf(stderr, "[FAIL] 8051 validation: %s\n", message);
        ++failures;
    }
}
unsigned parity(unsigned byte)
{
    unsigned ones = 0;
    for (unsigned bit = 0; bit < 8; ++bit) ones += (byte >> bit) & 1;
    return ones % 2;
}
int signedByte(unsigned value) { return value < 128 ? static_cast<int>(value) : static_cast<int>(value) - 256; }
struct Machine {
    std::array<std::uint8_t, 65536> program{}, data{};
    emu51 cpu{data.data(), program.data()};
    bool table;
    explicit Machine(bool dispatch) : table(dispatch) { cpu.setModel(Emu51Model::i8051); }
    int step(std::uint8_t op, std::uint8_t first = 0x30, std::uint8_t second = 0x30) {
        cpu.PC = 0x1000; program[0x1000] = op; program[0x1001] = first; program[0x1002] = second;
        return table ? cpu.process_table() : cpu.process();
    }
};

void arithmetic(bool table)
{
    Machine m(table);
    for (const auto op : {0x24, 0x34, 0x94}) {
        for (unsigned a = 0; a < 256; ++a) for (unsigned b = 0; b < 256; ++b) for (unsigned carry = 0; carry < 2; ++carry) {
            m.cpu.Acc = static_cast<std::uint8_t>(a);
            m.cpu.PSW = static_cast<std::uint8_t>(0x66 | (carry << 7)); // stale AC/OV, user flags preserved
            const int inputCarry = op == 0x24 ? 0 : static_cast<int>(carry);
            const bool subtract = op == 0x94;
            const int unsignedResult = subtract ? static_cast<int>(a) - static_cast<int>(b) - inputCarry
                                                : static_cast<int>(a + b) + inputCarry;
            const int signedResult = subtract ? signedByte(a) - signedByte(b) - inputCarry
                                              : signedByte(a) + signedByte(b) + inputCarry;
            const auto result = static_cast<std::uint8_t>(unsignedResult);
            const bool cy = subtract ? unsignedResult < 0 : unsignedResult > 255;
            const bool ac = subtract ? static_cast<int>(a % 16) - static_cast<int>(b % 16) - inputCarry < 0
                                     : static_cast<int>(a % 16 + b % 16) + inputCarry > 15;
            const bool ov = signedResult < -128 || signedResult > 127;
            const unsigned psw = 0x22 | (cy ? 0x80 : 0) | (ac ? 0x40 : 0) | (ov ? 4 : 0) | parity(result);
            const int cycles = m.step(static_cast<std::uint8_t>(op), static_cast<std::uint8_t>(b));
            check(m.cpu.Acc == result && m.cpu.PSW == psw && cycles == 1 && m.cpu.PC == 0x1002,
                "Exhaustive ADD/ADDC/SUBB result, CY, AC, OV, parity, preserved flags and timing");
        }
    }
    for (unsigned a = 0; a < 256; ++a) for (unsigned b = 0; b < 256; ++b) {
        m.cpu.Acc = static_cast<std::uint8_t>(a); m.cpu.B = static_cast<std::uint8_t>(b); m.cpu.PSW = 0xe6;
        const unsigned product = a * b;
        const unsigned expected = 0x62 | (product > 255 ? 4 : 0) | parity(product & 255);
        const int multiplyCycles = m.step(0xa4);
        check(m.cpu.Acc == (product & 255) && m.cpu.B == product / 256 && m.cpu.PSW == expected && multiplyCycles == 4,
            "Exhaustive MUL AB product, flags and timing");
        m.cpu.Acc = static_cast<std::uint8_t>(a); m.cpu.B = static_cast<std::uint8_t>(b); m.cpu.PSW = 0xe6;
        const int divideCycles = m.step(0x84);
        if (b) check(m.cpu.Acc == a / b && m.cpu.B == a % b && m.cpu.PSW == (0x62 | parity(a / b)) && divideCycles == 4,
            "Exhaustive nonzero DIV AB quotient, remainder, flags and timing");
        else check(!(m.cpu.PSW & bit7) && (m.cpu.PSW & bit2) && divideCycles == 4,
            "DIV by zero sets OV and clears CY without asserting undefined A/B results");
    }
}

void instructionCycles(bool table)
{
    // Machine cycles for the classic 12-clock core, indexed by opcode nibble.
    // Reserved A5 is omitted from hardware conformance checks.
    constexpr unsigned char cycles[16][16] = {
        {1,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1},
        {2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1},
        {2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1},
        {2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1},
        {2,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1},
        {2,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1},
        {2,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1},
        {2,2,2,2,1,2,1,1,1,1,1,1,1,1,1,1},
        {2,2,2,2,4,2,2,2,2,2,2,2,2,2,2,2},
        {2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1},
        {2,2,1,2,4,0,2,2,2,2,2,2,2,2,2,2},
        {2,2,1,1,2,2,2,2,2,2,2,2,2,2,2,2},
        {2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
        {2,2,1,1,1,2,1,1,2,2,2,2,2,2,2,2},
        {2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1},
        {2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1},
    };
    Machine m(table);
    for (unsigned op = 0; op < 256; ++op) {
        if (op == 0xa5) continue;
        m.cpu.init(); m.cpu.Ri(0, 0x40); m.cpu.Ri(1, 0x41);
        check(m.step(static_cast<std::uint8_t>(op)) == cycles[op / 16][op % 16],
            "Every defined opcode has an independently specified classic machine-cycle cost");
    }
}

void stack(bool table)
{
    Machine m(table);
    for (unsigned sp = 1; sp < 128; ++sp) for (const unsigned value : {0u, 7u, 0x20u, 0x7fu, 0xffu}) {
        m.cpu.SP = static_cast<std::uint8_t>(sp); m.cpu.IRAM[sp] = static_cast<std::uint8_t>(value);
        const int cycles = m.step(0xd0, 0x81); // POP SP: decrement before writing the destination.
        check(m.cpu.SP == value && cycles == 2, "POP SP retains the popped value, including zero and FFh");
    }
    for (unsigned sp = 0; sp < 127; ++sp) {
        m.cpu.SP = static_cast<std::uint8_t>(sp);
        m.step(0xc0, 0x81); // PUSH SP: increment before reading the source.
        check(m.cpu.SP == sp + 1 && m.cpu.IRAM[sp + 1] == sp + 1, "PUSH SP stores the incremented pointer");
    }
    m.cpu.SP = 0x7f; m.cpu.P0 = 0x5a; m.cpu.P1 = 0xa5;
    m.step(0xc0, 0x90);
    check(m.cpu.SP == 0x80 && m.cpu.P0 == 0x5a && m.cpu.P1 == 0xa5 && m.cpu.IRAM[0x80] == 0,
        "8051 stack overflow into absent upper RAM cannot write a port latch");
}

void memory(bool table)
{
    Machine m(table);
    for (unsigned address = 0x80; address < 256; ++address) {
        m.cpu.init(); m.cpu.Ri(0, static_cast<std::uint8_t>(address));
        const auto before = m.cpu.SFR[address];
        m.step(0x76, 0x12);
        check(m.cpu.SFR[address] == before && m.cpu.IRAM[address] == 0 && m.cpu.readIndirect(static_cast<std::uint8_t>(address)) == 0xff,
            "Every absent upper RAM address is isolated from SFRs on 8051");
    }
    for (unsigned bank = 0; bank < 4; ++bank) for (unsigned reg = 0; reg < 8; ++reg) {
        m.cpu.init(); m.step(0x75, 0xd0, static_cast<std::uint8_t>(bank << 3));
        m.step(static_cast<std::uint8_t>(0x78 + reg), 0x5a);
        check(m.cpu.IRAM[bank * 8 + reg] == 0x5a && m.cpu.Ri(static_cast<int>(reg)) == 0x5a,
            "All four register banks use lower RAM after software PSW selection");
    }
    for (unsigned page = 0; page < 256; ++page) for (unsigned pointer = 0; pointer < 2; ++pointer) {
        m.cpu.init(); m.cpu.P2 = static_cast<std::uint8_t>(page); m.cpu.Ri(static_cast<int>(pointer), 0x42); m.cpu.Acc = 0xa5;
        const unsigned address = page * 256 + 0x42;
        m.data[address] = 0; m.step(static_cast<std::uint8_t>(0xf2 + pointer));
        check(m.data[address] == 0xa5 && m.cpu.P2 == page, "MOVX @Ri,A uses the P2 latch for all 256 external pages");
        m.cpu.Acc = 0; m.step(static_cast<std::uint8_t>(0xe2 + pointer));
        check(m.cpu.Acc == 0xa5 && m.cpu.P2 == page, "MOVX A,@Ri reads the selected external page");
    }
}
} // namespace

int emu51_validation_tests()
{
    failures = 0; cases = 0;
    for (const bool table : {false, true}) {
        arithmetic(table); instructionCycles(table); stack(table); memory(table);
    }
    std::printf("8051 independent validation: %lu cases, %d failures\n", cases, failures);
    return failures;
}
