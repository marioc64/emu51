// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstdarg>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>

#if !defined(_WIN32)
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <sstream>
#include <string>
#include <vector>

#include "assembler.h"
#include "emu51_editor_files.h"
#include "emu51.h"
#include "imgui/emu51_imgui_input.h"
#include "imgui/emu51_cycle_timing.h"
#include "imgui/emu51_execution_settings.h"

namespace {

struct Fixture {
    std::uint8_t mem[0x10000];
    emu51 cpu;

    Fixture() : mem{0}, cpu(mem) {
        std::memset(mem, 0, sizeof(mem));
        cpu.init();
        cpu.IE = 0x00;
        cpu.TCON = 0x00;
        cpu.TMOD = 0x00;
    }

    void place(std::uint16_t pc, std::uint8_t op, std::uint8_t b1 = 0, std::uint8_t b2 = 0) {
        mem[pc] = op;
        mem[pc + 1] = b1;
        mem[pc + 2] = b2;
        cpu.PC = pc;
    }

    int step() { return cpu.process(); }
};

int g_failures = 0;
std::array<bool, 256> g_seen{};

void fail(const char* test, const char* fmt, ...) {
    std::fprintf(stderr, "[FAIL] %s: ", test);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fprintf(stderr, "\n");
    ++g_failures;
}

void mark(std::uint8_t op) {
    g_seen[op] = true;
}

void check(bool cond, const char* test, const char* fmt, ...) {
    if (cond) return;
    std::fprintf(stderr, "[FAIL] %s: ", test);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fprintf(stderr, "\n");
    ++g_failures;
}

std::string intel_hex_record(std::uint8_t type, std::uint16_t address, const std::vector<std::uint8_t>& data) {
    char byte[3];
    std::string line = ":";
    auto append_byte = [&line, &byte](std::uint8_t value) {
        std::snprintf(byte, sizeof(byte), "%02X", value);
        line += byte;
    };

    unsigned sum = static_cast<unsigned>(data.size()) +
                   static_cast<unsigned>(address >> 8) +
                   static_cast<unsigned>(address & 0xffu) +
                   type;
    append_byte(static_cast<std::uint8_t>(data.size()));
    append_byte(static_cast<std::uint8_t>(address >> 8));
    append_byte(static_cast<std::uint8_t>(address & 0xffu));
    append_byte(type);
    for (std::uint8_t value : data) {
        append_byte(value);
        sum += value;
    }
    append_byte(static_cast<std::uint8_t>((0x100u - (sum & 0xffu)) & 0xffu));
    line += '\n';
    return line;
}

void test_dptr_roundtrip() {
    Fixture f;
    f.cpu.DPTR(0x1234);
    check(f.cpu.DPL == 0x34 && f.cpu.DPH == 0x12 && f.cpu.DPTR() == 0x1234,
          "dptr_roundtrip",
          "DPL=%02x DPH=%02x DPTR=%04x",
          f.cpu.DPL, f.cpu.DPH, f.cpu.DPTR());
}

void test_parity_flag_odd_is_set() {
    Fixture f;
    mark(0x00);
    f.cpu.Acc = 0x01; // odd parity
    f.place(0x0000, 0x00);
    f.step();
    check((f.cpu.PSW & 0x01) != 0,
          "parity_odd_set",
          "PSW=%02x expected P=1 for Acc=%02x", f.cpu.PSW, f.cpu.Acc);
}

void test_a5_advances_pc() {
    Fixture f;
    mark(0xa5);
    f.place(0x0100, 0xa5);
    f.step();
    check(f.cpu.PC == 0x0101,
          "opcode_a5_progress",
          "PC=%04x expected 0101", f.cpu.PC);
}

void test_ajmp_acall_page_behavior() {
    static const std::uint8_t ajmp_ops[] = {0x01, 0x21, 0x41, 0x61, 0x81, 0xa1, 0xc1, 0xe1};
    static const std::uint8_t acall_ops[] = {0x11, 0x31, 0x51, 0x71, 0x91, 0xb1, 0xd1, 0xf1};

    for (int i = 0; i < 8; i++) {
        {
            Fixture f;
            const std::uint16_t pc = 0x8a34;
            const std::uint8_t imm = 0x5a;
            mark(ajmp_ops[i]);
            f.place(pc, ajmp_ops[i], imm);
            f.step();
            const std::uint16_t expected = static_cast<std::uint16_t>(((pc + 2) & 0xf800) | (i << 8) | imm);
            check(f.cpu.PC == expected,
                  "ajmp_addr11",
                  "op=%02x got=%04x expected=%04x", ajmp_ops[i], f.cpu.PC, expected);
        }
        {
            Fixture f;
            const std::uint16_t pc = 0x9234;
            const std::uint8_t imm = 0x6b;
            mark(acall_ops[i]);
            f.place(pc, acall_ops[i], imm);
            f.cpu.SP = 0x20;
            f.step();
            const std::uint16_t expected = static_cast<std::uint16_t>(((pc + 2) & 0xf800) | (i << 8) | imm);
            check(f.cpu.PC == expected,
                  "acall_addr11_pc",
                  "op=%02x got=%04x expected=%04x", acall_ops[i], f.cpu.PC, expected);
            check(f.cpu.SP == 0x22,
                  "acall_addr11_sp",
                  "op=%02x SP=%02x expected=22", acall_ops[i], f.cpu.SP);
            check(f.cpu.IRAM[0x21] == static_cast<std::uint8_t>((pc + 2) & 0xff) &&
                      f.cpu.IRAM[0x22] == static_cast<std::uint8_t>((pc + 2) >> 8),
                  "acall_addr11_stack",
                  "op=%02x ret=%02x%02x expected=%04x",
                  acall_ops[i], f.cpu.IRAM[0x22], f.cpu.IRAM[0x21], pc + 2);
        }
    }

    mark(0x02);
    {
        Fixture f;
        f.place(0x0000, 0x02, 0x12, 0x34);
        f.step();
        check(f.cpu.PC == 0x1234, "ljmp", "PC=%04x expected 1234", f.cpu.PC);
    }

    mark(0x12);
    mark(0x22);
    mark(0x32);
    {
        Fixture f;
        f.place(0x0400, 0x12, 0x34, 0x56);
        f.cpu.SP = 0x07;
        f.step();
        check(f.cpu.PC == 0x3456, "lcall_pc", "PC=%04x", f.cpu.PC);
        check(f.cpu.SP == 0x09, "lcall_sp", "SP=%02x", f.cpu.SP);

        f.place(0x3456, 0x22);
        f.step();
        check(f.cpu.PC == 0x0403 && f.cpu.SP == 0x07, "ret", "PC=%04x SP=%02x", f.cpu.PC, f.cpu.SP);

        f.cpu.SP = 0x09;
        f.cpu.IRAM[0x08] = 0xaa;
        f.cpu.IRAM[0x09] = 0xbb;
        f.place(0x0500, 0x32);
        f.step();
        check(f.cpu.PC == 0xbbaa && f.cpu.SP == 0x07, "reti", "PC=%04x SP=%02x", f.cpu.PC, f.cpu.SP);
    }
}

void test_movc_reads_program_memory() {
    mark(0x83);
    mark(0x93);

    {
        Fixture f;
        f.cpu.Acc = 0x02;
        f.place(0x0100, 0x83);
        f.mem[0x0103] = 0x5a; // PC+1 + A
        f.mem[0x0002] = 0x00;
        f.step();
        check(f.cpu.Acc == 0x5a,
              "movc_a_pc",
              "Acc=%02x expected 5a (from code memory PC+1+A)", f.cpu.Acc);
    }

    {
        Fixture f;
        f.cpu.Acc = 0x03;
        f.cpu.DPTR(0x2000);
        f.place(0x0000, 0x93);
        f.mem[0x2003] = 0xc7;
        f.step();
        check(f.cpu.Acc == 0xc7,
              "movc_a_dptr",
              "Acc=%02x expected c7 (from code memory DPTR+A)", f.cpu.Acc);
    }
}

void test_xchd_keeps_upper_nibble() {
    mark(0xd6);
    mark(0xd7);

    {
        Fixture f;
        f.cpu.Ri(0, 0x30);
        f.cpu.Acc = 0xab;
        f.cpu.IRAM[0x30] = 0xcd;
        f.place(0x0000, 0xd6);
        f.step();
        check(f.cpu.Acc == 0xad && f.cpu.IRAM[0x30] == 0xcb,
              "xchd_r0",
              "Acc=%02x mem=%02x expected Acc=ad mem=cb", f.cpu.Acc, f.cpu.IRAM[0x30]);
    }

    {
        Fixture f;
        f.cpu.Ri(1, 0x31);
        f.cpu.Acc = 0x41;
        f.cpu.IRAM[0x31] = 0x9e;
        f.place(0x0000, 0xd7);
        f.step();
        check(f.cpu.Acc == 0x4e && f.cpu.IRAM[0x31] == 0x91,
              "xchd_r1",
              "Acc=%02x mem=%02x expected Acc=4e mem=91", f.cpu.Acc, f.cpu.IRAM[0x31]);
    }
}

void test_movx_and_mov_coverage() {
    static const std::uint8_t mov_a_r[] = {0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xef};
    static const std::uint8_t mov_r_a[] = {0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff};
    static const std::uint8_t mov_r_imm[] = {0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f};
    static const std::uint8_t mov_dir_r[] = {0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f};
    static const std::uint8_t mov_r_dir[] = {0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf};

    mark(0x74); mark(0x75); mark(0x85); mark(0x86); mark(0x87);
    mark(0xe5); mark(0xe6); mark(0xe7); mark(0xf5); mark(0xf6); mark(0xf7);
    mark(0x76); mark(0x77); mark(0x90); mark(0x92); mark(0xa2);
    mark(0xe0); mark(0xe2); mark(0xe3); mark(0xf0); mark(0xf2); mark(0xf3);

    for (int i = 0; i < 8; i++) {
        mark(mov_a_r[i]);
        mark(mov_r_a[i]);
        mark(mov_r_imm[i]);
        mark(mov_dir_r[i]);
        mark(mov_r_dir[i]);
    }

    {
        Fixture f;
        f.place(0x0000, 0x74, 0x55);
        f.step();
        check(f.cpu.Acc == 0x55 && f.cpu.PC == 0x0002, "mov_a_imm", "Acc=%02x PC=%04x", f.cpu.Acc, f.cpu.PC);
    }

    {
        Fixture f;
        f.place(0x0000, 0x90, 0x12, 0x34);
        f.step();
        check(f.cpu.DPTR() == 0x1234 && f.cpu.PC == 0x0003, "mov_dptr_imm", "DPTR=%04x PC=%04x", f.cpu.DPTR(), f.cpu.PC);
    }

    {
        Fixture f;
        f.cpu.DPTR(0x2000);
        f.cpu.Acc = 0x66;
        f.place(0x0000, 0xf0);
        f.step();
        check(f.mem[0x2000] == 0x66, "movx_dptr_a", "mem[2000]=%02x", f.mem[0x2000]);
        f.place(0x0001, 0xe0);
        f.mem[0x2000] = 0x88;
        f.step();
        check(f.cpu.Acc == 0x88, "movx_a_dptr", "Acc=%02x", f.cpu.Acc);
    }

    // MOVX @Ri uses low byte from Ri and high address byte from P2.
    {
        Fixture f;
        f.cpu.P2 = 0x12;
        f.cpu.Ri(0, 0x34);
        f.cpu.Acc = 0xab;
        f.place(0x0100, 0xf2); // MOVX @R0,A
        f.step();
        check(f.mem[0x1234] == 0xab,
              "movx_r0_a_uses_p2_high",
              "mem[1234]=%02x expected ab", f.mem[0x1234]);
    }

    {
        Fixture f;
        f.cpu.P2 = 0x56;
        f.cpu.Ri(1, 0x78);
        f.mem[0x5678] = 0xc3;
        f.place(0x0100, 0xe3); // MOVX A,@R1
        f.step();
        check(f.cpu.Acc == 0xc3,
              "movx_a_r1_uses_p2_high",
              "Acc=%02x expected c3", f.cpu.Acc);
    }
}

void test_mov_direct_imm_and_mov_bit_c_regressions() {
    mark(0x75);
    mark(0x92);

    // Regression: MOV direct,#imm must fetch immediate from operand+1 with defined sequencing.
    {
        Fixture f;
        f.place(0x0000, 0x75, 0x22, 0x5a);
        f.cpu.IRAM[0x22] = 0x00;
        f.step();
        check(f.cpu.IRAM[0x22] == 0x5a && f.cpu.PC == 0x0003,
              "mov_direct_imm_sequence",
              "IRAM[22]=%02x PC=%04x expected 5a/0003", f.cpu.IRAM[0x22], f.cpu.PC);
    }

    // Regression: MOV bit,C must use the same decoded bit address for both set and clear paths.
    {
        Fixture f;
        f.cpu.P1 = 0xff;
        f.cpu.PSW &= static_cast<std::uint8_t>(~0x80); // C=0
        f.place(0x0100, 0x92, 0x90); // MOV P1.0,C
        f.step();
        check(f.cpu.P1 == 0xfe && f.cpu.PC == 0x0102,
              "mov_bit_c_clear_path",
              "P1=%02x PC=%04x expected fe/0102", f.cpu.P1, f.cpu.PC);
    }
}

void test_address_space_and_program_memory_edges() {
    std::array<std::uint8_t, 0x10000> data{};
    std::array<std::uint8_t, 0x10000> prog{};
    functional_address_space space(
        [&data](std::uint16_t addr) -> std::uint8_t { return data[addr]; },
        [&data](std::uint16_t addr, std::uint8_t value) { data[addr] = value; },
        [&prog](std::uint16_t addr) -> std::uint8_t { return prog[addr]; },
        [&prog](std::uint16_t addr, std::uint8_t value) { prog[addr] = value; });
    emu51 cpu(space);

    cpu.writeData(0x1234, 0x11);
    cpu.writeProg(0x1234, 0x22);
    check(data[0x1234] == 0x11 && prog[0x1234] == 0x22,
          "functional_address_space_split_writes",
          "data=%02x prog=%02x expected 11/22", data[0x1234], prog[0x1234]);

    prog[0x0100] = 0x74; // MOV A,#imm
    prog[0x0101] = 0x5a;
    data[0x0100] = 0x00;
    data[0x0101] = 0x00;
    cpu.PC = 0x0100;
    cpu.process();
    check(cpu.Acc == 0x5a && cpu.PC == 0x0102,
          "functional_address_space_fetches_program",
          "Acc=%02x PC=%04x expected 5a/0102", cpu.Acc, cpu.PC);

    emu51 split_cpu(false);
    check(split_cpu.readData(0x0000) == 0x00 &&
              split_cpu.readData(0xffff) == 0x00 &&
              split_cpu.readProg(0x0000) == 0x00 &&
              split_cpu.readProg(0xffff) == 0x00,
          "owned_address_space_zero_initialized",
          "owned memory should start at zero");
}

void test_arithmetic_and_logic_coverage() {
    static const std::uint8_t add_ops[] = {0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f};
    static const std::uint8_t addc_ops[] = {0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f};
    static const std::uint8_t subb_ops[] = {0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f};
    static const std::uint8_t orl_a_ops[] = {0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f};
    static const std::uint8_t anl_a_ops[] = {0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f};
    static const std::uint8_t xrl_a_ops[] = {0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f};

    mark(0x42); mark(0x43); mark(0x52); mark(0x53); mark(0x62); mark(0x63);
    mark(0x72); mark(0x82); mark(0xa0); mark(0xb0);
    mark(0x84); mark(0xa4); mark(0xd4);

    for (std::uint8_t op : add_ops) mark(op);
    for (std::uint8_t op : addc_ops) mark(op);
    for (std::uint8_t op : subb_ops) mark(op);
    for (std::uint8_t op : orl_a_ops) mark(op);
    for (std::uint8_t op : anl_a_ops) mark(op);
    for (std::uint8_t op : xrl_a_ops) mark(op);

    {
        Fixture f;
        f.cpu.Acc = 0xff;
        f.place(0x0000, 0x24, 0x01);
        f.step();
        check(f.cpu.Acc == 0x00 && (f.cpu.PSW & 0x80), "add_flags", "Acc=%02x PSW=%02x", f.cpu.Acc, f.cpu.PSW);
    }

    {
        Fixture f;
        f.cpu.Acc = 0x00;
        f.cpu.PSW |= 0x80; // C=1
        f.place(0x0000, 0x34, 0x00);
        f.step();
        check(f.cpu.Acc == 0x01, "addc_with_carry", "Acc=%02x", f.cpu.Acc);
    }

    {
        Fixture f;
        f.cpu.Acc = 0x00;
        f.cpu.PSW |= 0x80; // borrow in
        f.place(0x0000, 0x94, 0x00);
        f.step();
        check(f.cpu.Acc == 0xff && (f.cpu.PSW & 0x80), "subb_borrow", "Acc=%02x PSW=%02x", f.cpu.Acc, f.cpu.PSW);
    }
}

void test_arithmetic_flag_edge_cases() {
    mark(0x24);
    mark(0x34);
    mark(0x94);

    {
        Fixture f;
        f.cpu.Acc = 0x7f;
        f.place(0x0000, 0x24, 0x01);
        f.step();
        check(f.cpu.Acc == 0x80 &&
                  (f.cpu.PSW & 0x80) == 0 &&
                  (f.cpu.PSW & 0x40) != 0 &&
                  (f.cpu.PSW & 0x04) != 0,
              "add_signed_overflow_no_carry",
              "Acc=%02x PSW=%02x expected Acc=80 C=0 AC=1 OV=1", f.cpu.Acc, f.cpu.PSW);
    }

    {
        Fixture f;
        f.cpu.Acc = 0x80;
        f.place(0x0000, 0x24, 0x80);
        f.step();
        check(f.cpu.Acc == 0x00 &&
                  (f.cpu.PSW & 0x80) != 0 &&
                  (f.cpu.PSW & 0x40) == 0 &&
                  (f.cpu.PSW & 0x04) != 0,
              "add_negative_overflow_with_carry",
              "Acc=%02x PSW=%02x expected Acc=00 C=1 AC=0 OV=1", f.cpu.Acc, f.cpu.PSW);
    }

    {
        Fixture f;
        f.cpu.Acc = 0x00;
        f.cpu.PSW |= 0x80; // C=1
        f.place(0x0000, 0x34, 0x0f);
        f.step();
        check(f.cpu.Acc == 0x10 &&
                  (f.cpu.PSW & 0x80) == 0 &&
                  (f.cpu.PSW & 0x40) != 0 &&
                  (f.cpu.PSW & 0x04) == 0,
              "addc_aux_carry_from_carry_in",
              "Acc=%02x PSW=%02x expected Acc=10 C=0 AC=1 OV=0", f.cpu.Acc, f.cpu.PSW);
    }

    {
        Fixture f;
        f.cpu.Acc = 0xff;
        f.cpu.PSW |= 0x80; // C=1
        f.place(0x0000, 0x34, 0x00);
        f.step();
        check(f.cpu.Acc == 0x00 &&
                  (f.cpu.PSW & 0x80) != 0 &&
                  (f.cpu.PSW & 0x40) != 0 &&
                  (f.cpu.PSW & 0x04) == 0,
              "addc_full_byte_carry",
              "Acc=%02x PSW=%02x expected Acc=00 C=1 AC=1 OV=0", f.cpu.Acc, f.cpu.PSW);
    }

    {
        Fixture f;
        f.cpu.Acc = 0x80;
        f.place(0x0000, 0x94, 0x01);
        f.step();
        check(f.cpu.Acc == 0x7f &&
                  (f.cpu.PSW & 0x80) == 0 &&
                  (f.cpu.PSW & 0x40) != 0 &&
                  (f.cpu.PSW & 0x04) != 0,
              "subb_signed_overflow_no_borrow",
              "Acc=%02x PSW=%02x expected Acc=7f C=0 AC=1 OV=1", f.cpu.Acc, f.cpu.PSW);
    }
}

void test_subb_and_mul_div_flag_rules() {
    mark(0x94);
    mark(0x84);
    mark(0xa4);

    // SUBB sets C only when borrow from bit 7 is required.
    {
        Fixture f;
        f.cpu.Acc = 0xff;
        f.cpu.PSW &= static_cast<std::uint8_t>(~0x80); // C=0
        f.place(0x0000, 0x94, 0x01); // FF - 01 -> FE, no borrow
        f.step();
        check(f.cpu.Acc == 0xfe && (f.cpu.PSW & 0x80) == 0,
              "subb_no_borrow_clears_c",
              "Acc=%02x PSW=%02x expected Acc=fe C=0", f.cpu.Acc, f.cpu.PSW);
    }

    // DIV AB clears OV when divisor is non-zero.
    {
        Fixture f;
        f.cpu.PSW |= 0x04; // pre-set OV
        f.cpu.Acc = 10;
        f.cpu.B = 3;
        f.place(0x0000, 0x84);
        f.step();
        check((f.cpu.PSW & 0x04) == 0,
              "div_nonzero_clears_ov",
              "PSW=%02x expected OV=0", f.cpu.PSW);
    }

    // MUL AB always clears C.
    {
        Fixture f;
        f.cpu.PSW |= 0x80; // pre-set C
        f.cpu.Acc = 2;
        f.cpu.B = 3;
        f.place(0x0000, 0xa4);
        f.step();
        check((f.cpu.PSW & 0x80) == 0,
              "mul_always_clears_c",
              "PSW=%02x expected C=0", f.cpu.PSW);
    }
}

void test_div_by_zero_sets_ov_and_clears_c() {
    mark(0x84);

    Fixture f;
    f.cpu.Acc = 0x55;
    f.cpu.B = 0x00;
    f.cpu.PSW &= static_cast<std::uint8_t>(~0x04); // clear OV
    f.cpu.PSW |= 0x80;                      // pre-set C to verify clear
    f.place(0x0000, 0x84);
    f.step();
    check((f.cpu.PSW & 0x04) != 0,
          "div_by_zero_sets_ov",
          "PSW=%02x expected OV=1", f.cpu.PSW);
    check((f.cpu.PSW & 0x80) == 0,
          "div_always_clears_c",
          "PSW=%02x expected C=0", f.cpu.PSW);
}

void test_da_a_known_vectors() {
    mark(0xd4);

    // 0x99 + 1 -> 0x9a; DA -> 0x00, C=1.
    {
        Fixture f;
        f.cpu.Acc = 0x99;
        f.place(0x0000, 0x24, 0x01);
        f.step();
        check(f.cpu.Acc == 0x9a && (f.cpu.PSW & 0x80) == 0,
              "da_a_setup_after_add",
              "Acc=%02x PSW=%02x expected Acc=9a C=0", f.cpu.Acc, f.cpu.PSW);
        f.place(0x0002, 0xd4);
        f.step();
        check(f.cpu.Acc == 0x00 && (f.cpu.PSW & 0x80) != 0,
              "da_a_full_decimal_overflow_sets_c",
              "Acc=%02x PSW=%02x expected Acc=00 C=1", f.cpu.Acc, f.cpu.PSW);
    }

    // Lower nibble > 9, no carry-in: 0x1c -> 0x22, C unchanged.
    {
        Fixture f;
        f.cpu.Acc = 0x1c;
        f.cpu.PSW &= static_cast<std::uint8_t>(~0x80); // C=0
        f.cpu.PSW &= static_cast<std::uint8_t>(~0x40); // AC=0
        f.place(0x0000, 0xd4);
        f.step();
        check(f.cpu.Acc == 0x22 && (f.cpu.PSW & 0x80) == 0,
              "da_a_lower_only_no_carry",
              "Acc=%02x PSW=%02x expected Acc=22 C=0", f.cpu.Acc, f.cpu.PSW);
    }

    // Upper nibble > 9, no carry-in: 0xa5 -> 0x05, C set.
    {
        Fixture f;
        f.cpu.Acc = 0xa5;
        f.cpu.PSW &= static_cast<std::uint8_t>(~0x80); // C=0
        f.cpu.PSW &= static_cast<std::uint8_t>(~0x40); // AC=0
        f.place(0x0000, 0xd4);
        f.step();
        check(f.cpu.Acc == 0x05 && (f.cpu.PSW & 0x80) != 0,
              "da_a_upper_only_sets_c",
              "Acc=%02x PSW=%02x expected Acc=05 C=1", f.cpu.Acc, f.cpu.PSW);
    }

    // Carry-in forces upper correction even when upper nibble <= 9; C must remain set.
    {
        Fixture f;
        f.cpu.Acc = 0x10;
        f.cpu.PSW |= 0x80;                       // C=1
        f.cpu.PSW &= static_cast<std::uint8_t>(~0x40);  // AC=0
        f.place(0x0000, 0xd4);
        f.step();
        check(f.cpu.Acc == 0x70 && (f.cpu.PSW & 0x80) != 0,
              "da_a_preserves_carry_in",
              "Acc=%02x PSW=%02x expected Acc=70 C=1", f.cpu.Acc, f.cpu.PSW);
    }
}

void test_da_a_all_bcd_sums() {
    Fixture f;
    const auto bcd = [](int value) {
        return static_cast<std::uint8_t>(((value / 10) << 4) | (value % 10));
    };
    for (int lhs = 0; lhs < 100; ++lhs) {
        for (int rhs = 0; rhs < 100; ++rhs) {
            for (int carry = 0; carry <= 1; ++carry) {
                f.cpu.init();
                f.cpu.Acc = bcd(lhs);
                f.cpu.PSW = carry ? bit7 : 0;
                f.place(0, 0x34, bcd(rhs), 0xd4); // ADDC A,#rhs; DA A
                f.step();
                f.step();
                const int sum = lhs + rhs + carry;
                if (f.cpu.Acc != bcd(sum % 100) || !!(f.cpu.PSW & bit7) != (sum >= 100)) {
                    fail("da_a_all_bcd_sums",
                         "%d + %d + %d: Acc=%02x C=%d expected Acc=%02x C=%d",
                         lhs, rhs, carry, f.cpu.Acc, !!(f.cpu.PSW & bit7),
                         bcd(sum % 100), sum >= 100);
                    return;
                }
            }
        }
    }
}

void test_bit_address_mapping_and_cpl_roundtrip() {
    mark(0xb2); // CPL bit
    mark(0xd2); // SETB bit
    mark(0xc2); // CLR bit

    // bit 0x10 maps to IRAM[0x22].bit0 (middle of iram bit-addressable range).
    {
        Fixture f;
        f.cpu.IRAM[0x22] = 0x00;
        f.place(0x0000, 0xd2, 0x10);
        f.step();
        check(f.cpu.IRAM[0x22] == 0x01,
              "bit_iram_mid_addr_setb",
              "IRAM[22]=%02x expected 01", f.cpu.IRAM[0x22]);

        f.place(0x0002, 0xb2, 0x10);
        f.step();
        check(f.cpu.IRAM[0x22] == 0x00,
              "bit_iram_mid_addr_cpl_clears",
              "IRAM[22]=%02x expected 00", f.cpu.IRAM[0x22]);

        f.place(0x0004, 0xb2, 0x10);
        f.step();
        check(f.cpu.IRAM[0x22] == 0x01,
              "bit_iram_mid_addr_cpl_sets",
              "IRAM[22]=%02x expected 01", f.cpu.IRAM[0x22]);
    }

    // bit 0xa8 maps to SFR[0xa8].bit0 (IE.0/EX0).
    {
        Fixture f;
        f.cpu.IE = 0x00;
        f.place(0x0000, 0xd2, 0xa8);
        f.step();
        check(f.cpu.IE == 0x01,
              "bit_sfr_ie_setb",
              "IE=%02x expected 01", f.cpu.IE);

        f.place(0x0002, 0xb2, 0xa8);
        f.step();
        check(f.cpu.IE == 0x00,
              "bit_sfr_ie_cpl_clears",
              "IE=%02x expected 00", f.cpu.IE);
    }

    // bit 0x8b maps to SFR[0x88].bit3 (TCON.IT1) -- non-aligned bit within an SFR byte.
    {
        Fixture f;
        f.cpu.TCON = 0xff;
        f.place(0x0000, 0xc2, 0x8b);
        f.step();
        check((f.cpu.TCON & 0x08) == 0 && (f.cpu.TCON & ~0x08) == 0xf7,
              "bit_sfr_tcon_clr_keeps_others",
              "TCON=%02x expected f7", f.cpu.TCON);
    }
}

void test_inc_dec_rotate_swap_clr_cpl_setb_coverage() {
    static const std::uint8_t inc_ops[] = {0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0xa3};
    static const std::uint8_t dec_ops[] = {0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f};

    for (std::uint8_t op : inc_ops) mark(op);
    for (std::uint8_t op : dec_ops) mark(op);

    mark(0x03); mark(0x13); mark(0x23); mark(0x33);
    mark(0xc4); mark(0xe4); mark(0xf4);
    mark(0xc2); mark(0xc3); mark(0xd2); mark(0xd3); mark(0xb2); mark(0xb3);

    {
        Fixture f;
        f.cpu.Acc = 0x81;
        f.place(0x0000, 0x03);
        f.step();
        check(f.cpu.Acc == 0xc0, "rr", "Acc=%02x", f.cpu.Acc);

        f.place(0x0001, 0x23);
        f.step();
        check(f.cpu.Acc == 0x81, "rl", "Acc=%02x", f.cpu.Acc);

        f.cpu.PSW &= ~0x80;
        f.place(0x0002, 0x33);
        f.step();
        check(f.cpu.Acc == 0x02 && (f.cpu.PSW & 0x80), "rlc", "Acc=%02x PSW=%02x", f.cpu.Acc, f.cpu.PSW);

        f.place(0x0003, 0x13);
        f.step();
        check(f.cpu.Acc == 0x81, "rrc", "Acc=%02x", f.cpu.Acc);

        f.place(0x0004, 0xc4);
        f.step();
        check(f.cpu.Acc == 0x18, "swap", "Acc=%02x", f.cpu.Acc);
    }
}

void test_branch_coverage() {
    mark(0x10); mark(0x20); mark(0x30);
    mark(0x40); mark(0x50); mark(0x60); mark(0x70); mark(0x80);
    mark(0x73);

    {
        Fixture f;
        f.cpu.Acc = 0x00;
        f.place(0x0100, 0x60, 0x05);
        f.step();
        check(f.cpu.PC == 0x0107, "jz_taken", "PC=%04x", f.cpu.PC);

        f.cpu.Acc = 0x01;
        f.place(0x0200, 0x60, 0x05);
        f.step();
        check(f.cpu.PC == 0x0202, "jz_not_taken", "PC=%04x", f.cpu.PC);
    }

    {
        Fixture f;
        f.place(0x0300, 0x80, static_cast<std::uint8_t>(-2));
        f.step();
        check(f.cpu.PC == 0x0300, "sjmp", "PC=%04x", f.cpu.PC);
    }

    {
        Fixture f;
        f.cpu.DPTR(0x1234);
        f.cpu.Acc = 0x10;
        f.place(0x0000, 0x73);
        f.step();
        check(f.cpu.PC == 0x1244, "jmp_a_dptr", "PC=%04x", f.cpu.PC);
    }
}

void test_pc_stack_bit_and_branch_wrap_edges() {
    mark(0x00);
    mark(0x80);
    mark(0xc0);
    mark(0xd0);
    mark(0xd2);
    mark(0xc2);

    {
        Fixture f;
        f.mem[0xffff] = 0x00; // NOP wraps PC from FFFF to 0000.
        f.cpu.PC = 0xffff;
        f.step();
        check(f.cpu.PC == 0x0000,
              "pc_wrap_after_single_byte_instruction",
              "PC=%04x expected 0000", f.cpu.PC);
    }

    {
        Fixture f;
        f.mem[0xfffe] = 0x80; // SJMP +1 from FFFE lands at 0001.
        f.mem[0xffff] = 0x01;
        f.cpu.PC = 0xfffe;
        f.step();
        check(f.cpu.PC == 0x0001,
              "relative_branch_wrap_forward",
              "PC=%04x expected 0001", f.cpu.PC);
    }

    {
        Fixture f;
        f.cpu.SP = 0xff;
        f.cpu.IRAM[0x20] = 0xa5;
        f.place(0x0100, 0xc0, 0x20); // PUSH 20h wraps SP to 00h.
        f.step();
        check(f.cpu.SP == 0x00 && f.cpu.IRAM[0x00] == 0xa5,
              "push_wraps_stack_pointer",
              "SP=%02x IRAM[00]=%02x expected 00/a5", f.cpu.SP, f.cpu.IRAM[0x00]);

        f.cpu.IRAM[0x21] = 0x00;
        f.place(0x0102, 0xd0, 0x21); // POP 21h reads IRAM[00], then SP wraps to FFh.
        f.step();
        check(f.cpu.SP == 0xff && f.cpu.IRAM[0x21] == 0xa5,
              "pop_wraps_stack_pointer",
              "SP=%02x IRAM[21]=%02x expected ff/a5", f.cpu.SP, f.cpu.IRAM[0x21]);
    }

    {
        Fixture f;
        f.place(0x0200, 0xd2, 0x00); // SETB bit 00h -> 20h.0
        f.step();
        check((f.cpu.IRAM[0x20] & 0x01) != 0,
              "setb_lowest_internal_bit",
              "IRAM[20]=%02x expected bit0 set", f.cpu.IRAM[0x20]);

        f.place(0x0202, 0xd2, 0x7f); // SETB bit 7Fh -> 2Fh.7
        f.step();
        check((f.cpu.IRAM[0x2f] & 0x80) != 0,
              "setb_highest_internal_bit",
              "SFR[2f]=%02x expected bit7 set", f.cpu.IRAM[0x2f]);

        f.cpu.P0 = 0x00;
        f.place(0x0204, 0xd2, 0x80); // SETB P0.0
        f.step();
        check((f.cpu.P0 & 0x01) != 0,
              "setb_lowest_sfr_bit",
              "P0=%02x expected bit0 set", f.cpu.P0);

        f.cpu.B = 0xff;
        f.place(0x0206, 0xc2, 0xf7); // CLR B.7
        f.step();
        check((f.cpu.B & 0x80) == 0,
              "clr_highest_implemented_sfr_bit",
              "B=%02x expected bit7 clear", f.cpu.B);
    }
}

void test_cjne_djnz_coverage() {
    static const std::uint8_t cjne_ops[] = {0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf};
    static const std::uint8_t djnz_ops[] = {0xd5,0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf};

    for (std::uint8_t op : cjne_ops) mark(op);
    for (std::uint8_t op : djnz_ops) mark(op);

    {
        Fixture f;
        f.cpu.Acc = 0x10;
        f.place(0x0000, 0xb4, 0x11, 0x05);
        f.step();
        check(f.cpu.PC == 0x0008, "cjne_taken", "PC=%04x", f.cpu.PC);
    }

    {
        Fixture f;
        f.cpu.Ri(0, 0x02);
        f.place(0x0000, 0xd8, 0x05);
        f.step();
        check(f.cpu.Ri(0) == 0x01 && f.cpu.PC == 0x0007,
              "djnz_r0_taken",
              "R0=%02x PC=%04x", f.cpu.Ri(0), f.cpu.PC);
    }
}

void test_stack_xch_coverage() {
    mark(0xc0); mark(0xd0);
    static const std::uint8_t xch_ops[] = {0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf};
    for (std::uint8_t op : xch_ops) mark(op);

    {
        Fixture f;
        f.cpu.SP = 0x07;
        f.cpu.IRAM[0x20] = 0x42;
        f.place(0x0000, 0xc0, 0x20);
        f.step();
        check(f.cpu.SP == 0x08 && f.cpu.IRAM[0x08] == 0x42, "push", "SP=%02x top=%02x", f.cpu.SP, f.cpu.IRAM[0x08]);

        f.cpu.IRAM[0x20] = 0x00;
        f.place(0x0002, 0xd0, 0x20);
        f.step();
        check(f.cpu.SP == 0x07 && f.cpu.IRAM[0x20] == 0x42, "pop", "SP=%02x val=%02x", f.cpu.SP, f.cpu.IRAM[0x20]);
    }
}

void test_mov_indirect_single_cycle_timing() {
    for (const std::uint8_t op : std::array<std::uint8_t, 4>{0x76, 0x77, 0xf6, 0xf7}) {
        mark(op);
        Fixture f;
        const int reg = op & 1;
        f.cpu.Ri(reg, 0x40);
        f.cpu.Acc = 0x5a;
        f.cpu.TMOD = 0x11; // Both timers in mode 1.
        f.cpu.TCON = 0x50; // TR0 | TR1
        f.place(0x0200, op, 0xa5);

        const int cycles = f.step();
        const bool immediate = op == 0x76 || op == 0x77;
        check(cycles == 1 && f.cpu.TL0 == 1 && f.cpu.TL1 == 1,
              "mov_indirect_single_cycle", "op=%02x cycles=%d TL0=%02x TL1=%02x",
              op, cycles, f.cpu.TL0, f.cpu.TL1);
        check(f.cpu.IRAM[0x40] == (immediate ? 0xa5 : 0x5a) &&
                  f.cpu.PC == (immediate ? 0x0202 : 0x0201),
              "mov_indirect_result", "op=%02x value=%02x PC=%04x",
              op, f.cpu.IRAM[0x40], f.cpu.PC);
    }
}

void test_interrupt_entry_cycles() {
    struct InterruptCase {
        std::uint8_t enable;
        std::uint8_t pending;
        std::uint16_t vector;
    };
    const InterruptCase sources[] = {
        {0x81, bit1, 0x0003}, // EX0
        {0x82, bit5, 0x000b}, // Timer 0
        {0x84, bit3, 0x0013}, // EX1
        {0x88, bit7, 0x001b}, // Timer 1
    };
    // NOP, SJMP, and MUL cover one-, two-, and four-cycle instructions.
    for (const std::uint8_t op : std::array<std::uint8_t, 3>{0x00, 0x80, 0xa4}) {
        const int instructionCycles = op == 0x00 ? 1 : (op == 0x80 ? 2 : 4);
        for (const InterruptCase& source : sources) {
            Fixture f;
            f.cpu.IE = source.enable;
            f.cpu.TMOD = 0x11;
            f.cpu.TCON = bit0 | bit2 | bit4 | bit6 | source.pending;
            f.cpu.TL0 = 0x10;
            f.cpu.TL1 = 0x20;
            f.place(0x0200, op); // SJMP +0 also returns to the next instruction.

            const int cycles = f.step();
            check(cycles == instructionCycles + 2 &&
                      f.cpu.TL0 == 0x10 + cycles && f.cpu.TL1 == 0x20 + cycles,
                  "interrupt_entry_cycles", "op=%02x vector=%04x cycles=%d TL0=%02x TL1=%02x",
                  op, source.vector, cycles, f.cpu.TL0, f.cpu.TL1);
            check(f.cpu.PC == source.vector && f.cpu.SP == 0x09 &&
                      (f.cpu.TCON & source.pending) == 0,
                  "interrupt_entry_state", "op=%02x vector=%04x PC=%04x SP=%02x TCON=%02x",
                  op, source.vector, f.cpu.PC, f.cpu.SP, f.cpu.TCON);

            const int nextCycles = f.step(); // NOP at the interrupt vector.
            check(nextCycles == 1 && f.cpu.TL0 == 0x11 + cycles && f.cpu.TL1 == 0x21 + cycles,
                  "interrupt_entry_charged_once", "vector=%04x cycles=%d TL0=%02x TL1=%02x",
                  source.vector, nextCycles, f.cpu.TL0, f.cpu.TL1);
        }
    }

    for (const std::uint8_t enable : std::array<std::uint8_t, 3>{0x00, 0x01, 0x80}) {
        Fixture f;
        f.cpu.IE = enable; // No enables, global disabled, or source disabled.
        f.cpu.TMOD = 0x11;
        f.cpu.TCON = bit0 | bit1 | bit4 | bit6;
        f.place(0x0200, 0x00);
        const int cycles = f.step();
        check(cycles == 1 && f.cpu.TL0 == 1 && f.cpu.TL1 == 1 &&
                  f.cpu.PC == 0x0201 && f.cpu.SP == 0x07,
              "masked_interrupt_no_entry_cycles", "IE=%02x cycles=%d TL0=%02x TL1=%02x PC=%04x SP=%02x",
              enable, cycles, f.cpu.TL0, f.cpu.TL1, f.cpu.PC, f.cpu.SP);
    }
}

void test_timer_overflow_during_interrupt_entry() {
    Fixture f;
    f.cpu.IE = 0x83; // EA | EX0 | ET0
    f.cpu.IP = bit1; // Timer 0 can preempt EX0.
    f.cpu.TMOD = 0x01;
    f.cpu.TCON = bit0 | bit4;
    f.cpu.TH0 = 0xff;
    f.cpu.TL0 = 0xfe;
    f.place(0x0200, 0x00);
    f.cpu.triggerExternalInterrupt0();

    const int entryCycles = f.step();
    check(entryCycles == 3 && f.cpu.PC == 0x0003 && f.cpu.SP == 0x09 &&
              f.cpu.TH0 == 0 && f.cpu.TL0 == 1 && (f.cpu.TCON & bit5) != 0,
          "timer_overflow_during_entry", "cycles=%d PC=%04x SP=%02x TH0=%02x TL0=%02x TCON=%02x",
          entryCycles, f.cpu.PC, f.cpu.SP, f.cpu.TH0, f.cpu.TL0, f.cpu.TCON);

    // Entry advances timers without recursively polling another interrupt.
    const int preemptCycles = f.step();
    check(preemptCycles == 3 && f.cpu.PC == 0x000b && f.cpu.SP == 0x0b &&
              f.cpu.TL0 == 4 && (f.cpu.TCON & bit5) == 0,
          "entry_overflow_serviced_next_step", "cycles=%d PC=%04x SP=%02x TL0=%02x TCON=%02x",
          preemptCycles, f.cpu.PC, f.cpu.SP, f.cpu.TL0, f.cpu.TCON);
}

void test_timer_mode2_reload_timing() {
    // Compare batched instruction timing with an independent per-cycle model
    // for every reload/counter pair, including reload periods of 1, 2, and 3.
    for (const std::uint8_t op : std::array<std::uint8_t, 3>{0x00, 0x80, 0xa4}) {
        Fixture f;
        f.cpu.TMOD = 0x22; // Both timers in mode 2.
        const int expectedCycles = op == 0x00 ? 1 : (op == 0x80 ? 2 : 4);
        for (unsigned reload = 0; reload < 256; ++reload) {
            for (unsigned initial = 0; initial < 256; ++initial) {
                unsigned expected = initial;
                bool overflow = false;
                for (int cycle = 0; cycle < expectedCycles; ++cycle) {
                    if (expected == 0xff) {
                        expected = reload;
                        overflow = true;
                    } else {
                        ++expected;
                    }
                }
                f.cpu.TH0 = f.cpu.TH1 = static_cast<std::uint8_t>(reload);
                f.cpu.TL0 = f.cpu.TL1 = static_cast<std::uint8_t>(initial);
                f.cpu.TCON = 0x50;
                f.place(0x0200, op);
                const int cycles = f.step();
                if (cycles != expectedCycles || f.cpu.TL0 != expected || f.cpu.TL1 != expected ||
                    f.cpu.TH0 != reload || f.cpu.TH1 != reload ||
                    (f.cpu.TCON & 0xa0) != (overflow ? 0xa0 : 0)) {
                    fail("timer_mode2_reload_timing",
                         "op=%02x TH=%02x initial=%02x cycles=%d TL0=%02x TL1=%02x expected=%02x TCON=%02x",
                         op, reload, initial, cycles, f.cpu.TL0, f.cpu.TL1, expected, f.cpu.TCON);
                    return;
                }
            }
        }
    }
}

void test_timer_mode2_reload_during_interrupt_entry() {
    Fixture f;
    f.cpu.IE = 0x82; // EA | ET0
    f.cpu.TMOD = 0x22;
    f.cpu.TCON = 0x50;
    f.cpu.TH0 = f.cpu.TL0 = 0xff; // Overflow every machine cycle.
    f.cpu.TH1 = 0xfe;
    f.cpu.TL1 = 0xff;
    f.place(0x0200, 0x00);

    const int entryCycles = f.step();
    check(entryCycles == 3 && f.cpu.PC == 0x000b && f.cpu.SP == 0x09 &&
              f.cpu.TL0 == 0xff && f.cpu.TL1 == 0xfe && (f.cpu.TCON & bit5) != 0,
          "mode2_reload_during_entry", "cycles=%d PC=%04x SP=%02x TL0=%02x TL1=%02x TCON=%02x",
          entryCycles, f.cpu.PC, f.cpu.SP, f.cpu.TL0, f.cpu.TL1, f.cpu.TCON);

    const int nextCycles = f.step();
    check(nextCycles == 1 && f.cpu.PC == 0x000c && f.cpu.SP == 0x09 &&
              f.cpu.TL0 == 0xff && f.cpu.TL1 == 0xff && (f.cpu.TCON & bit5) != 0,
          "mode2_pending_interrupt_not_reentrant", "cycles=%d PC=%04x SP=%02x TL0=%02x TL1=%02x TCON=%02x",
          nextCycles, f.cpu.PC, f.cpu.SP, f.cpu.TL0, f.cpu.TL1, f.cpu.TCON);
}

void test_external_interrupt_trigger_and_reti() {
    mark(0x00);
    mark(0x32);

    Fixture f;
    f.cpu.IE = 0x81; // EA | EX0
    f.cpu.TCON |= bit0; // IT0=1 edge mode for this test
    f.cpu.SP = 0x07;
    f.place(0x0200, 0x00); // NOP
    f.mem[0x0003] = 0x32;   // RETI at EX0 vector

    f.cpu.triggerExternalInterrupt0();
    f.step();
    check(f.cpu.PC == 0x0003, "ext0_vector", "PC=%04x expected 0003", f.cpu.PC);
    check(f.cpu.SP == 0x09, "ext0_stack_push", "SP=%02x expected 09", f.cpu.SP);
    check((f.cpu.TCON & 0x02) == 0, "ext0_flag_cleared", "TCON=%02x expected IE0 cleared", f.cpu.TCON);

    f.step(); // RETI
    check(f.cpu.PC == 0x0201 && f.cpu.SP == 0x07,
          "ext0_reti_return",
          "PC=%04x SP=%02x expected PC=0201 SP=07", f.cpu.PC, f.cpu.SP);
}

void test_tf0_persists_until_serviced() {
    mark(0x00);

    Fixture f;
    f.cpu.TMOD = 0x02;      // T0 mode 2 auto-reload
    f.cpu.TCON = 0x10;      // TR0=1
    f.cpu.TH0 = 0x00;
    f.cpu.TL0 = 0xff;       // overflow on next cycle
    f.cpu.IE = 0x80;        // EA=1, ET0=0 (masked)
    f.place(0x0100, 0x00);
    f.step();
    check((f.cpu.TCON & 0x20) != 0, "tf0_latched_masked", "TCON=%02x expected TF0=1", f.cpu.TCON);

    f.cpu.IE = 0x82;        // EA | ET0
    f.place(0x0101, 0x00);
    f.step();
    check(f.cpu.PC == 0x000b, "tf0_serviced_when_enabled", "PC=%04x expected 000b", f.cpu.PC);
    check((f.cpu.TCON & 0x20) == 0, "tf0_cleared_on_vector", "TCON=%02x expected TF0=0", f.cpu.TCON);
}

void test_interrupt_priority_and_preemption() {
    mark(0x00);

    // High-priority T0 should win over low-priority EX0.
    {
        Fixture f;
        f.cpu.IE = 0x83;    // EA | EX0 | ET0
        f.cpu.IP = 0x02;    // PT0 high, PX0 low
        f.cpu.TCON = 0x20;  // TF0 pending
        f.place(0x0300, 0x00);
        f.cpu.triggerExternalInterrupt0();
        f.step();
        check(f.cpu.PC == 0x000b, "priority_high_wins", "PC=%04x expected 000b", f.cpu.PC);
    }

    // High-priority interrupt can preempt low-priority ISR.
    {
        Fixture f;
        f.cpu.IE = 0x83;    // EA | EX0 | ET0
        f.cpu.IP = 0x02;    // PT0 high, PX0 low
        f.cpu.SP = 0x07;
        f.mem[0x0003] = 0x00; // low-priority ISR body starts with NOP

        f.place(0x0400, 0x00);
        f.cpu.triggerExternalInterrupt0();
        f.step();
        check(f.cpu.PC == 0x0003, "low_vector_first", "PC=%04x expected 0003", f.cpu.PC);

        f.cpu.TCON |= 0x20; // TF0 pending while in low ISR
        f.step();           // execute NOP in low ISR, then arbitration
        check(f.cpu.PC == 0x000b, "high_preempts_low", "PC=%04x expected 000b", f.cpu.PC);
    }
}

void test_timer_gate_and_counter_bits() {
    mark(0x00);

    // GATE0: timer runs only when INT0 pin is high.
    {
        Fixture f;
        f.cpu.TMOD = 0x09; // GATE0=1, C/T0=0, mode1
        f.cpu.TCON = 0x10; // TR0=1
        f.cpu.TH0 = 0x12;
        f.cpu.TL0 = 0x34;
        f.cpu.P3 &= static_cast<std::uint8_t>(~bit2); // INT0 low
        f.place(0x0100, 0x00);
        f.step();
        check(f.cpu.TH0 == 0x12 && f.cpu.TL0 == 0x34,
              "gate0_blocks_when_int0_low",
              "TH0=%02x TL0=%02x", f.cpu.TH0, f.cpu.TL0);

        f.cpu.P3 |= bit2; // INT0 high
        f.place(0x0101, 0x00);
        f.step();
        check(f.cpu.TL0 == 0x35,
              "gate0_allows_when_int0_high",
              "TL0=%02x expected 35", f.cpu.TL0);
    }

    // C/T0=1: no automatic machine-cycle increment (requires external pulses).
    {
        Fixture f;
        f.cpu.TMOD = 0x05; // GATE0=0, C/T0=1, mode1
        f.cpu.TCON = 0x10; // TR0=1
        f.cpu.TH0 = 0x56;
        f.cpu.TL0 = 0x78;
        f.place(0x0200, 0x00);
        f.step();
        check(f.cpu.TH0 == 0x56 && f.cpu.TL0 == 0x78,
              "ct0_no_auto_increment",
              "TH0=%02x TL0=%02x", f.cpu.TH0, f.cpu.TL0);
    }
}

void test_timer_modes_0_and_3() {
    mark(0x00);

    // Mode0: 13-bit overflow sets TF0.
    {
        Fixture f;
        f.cpu.TMOD = 0x00; // T0 mode0
        f.cpu.TCON = 0x10; // TR0=1
        f.cpu.TH0 = 0xff;
        f.cpu.TL0 = 0x1f;
        f.place(0x0300, 0x00);
        f.step();
        check(f.cpu.TH0 == 0x00 && (f.cpu.TL0 & 0x1f) == 0x00 && (f.cpu.TCON & 0x20),
              "mode0_overflow_tf0",
              "TH0=%02x TL0=%02x TCON=%02x", f.cpu.TH0, f.cpu.TL0, f.cpu.TCON);
    }

    // Mode3: TL0 uses TR0/TF0 and TH0 uses TR1/TF1 independently.
    {
        Fixture f;
        f.cpu.TMOD = 0x03; // T0 mode3
        f.cpu.TCON = 0x50; // TR1=1, TR0=1
        f.cpu.TL0 = 0xff;
        f.cpu.TH0 = 0xff;
        f.cpu.TL1 = 0x44;
        f.cpu.TH1 = 0x55;
        f.place(0x0400, 0x00);
        f.step();
        check(f.cpu.TL0 == 0x00 && f.cpu.TH0 == 0x00,
              "mode3_split_counters_increment",
              "TL0=%02x TH0=%02x", f.cpu.TL0, f.cpu.TH0);
        check((f.cpu.TCON & 0x20) && (f.cpu.TCON & 0x80),
              "mode3_tf0_tf1_set",
              "TCON=%02x expected TF0+TF1 set", f.cpu.TCON);
        check(f.cpu.TL1 == 0x44 && f.cpu.TH1 == 0x55,
              "mode3_timer1_halted",
              "TL1=%02x TH1=%02x", f.cpu.TL1, f.cpu.TH1);
    }

    // Mode3: TH0 should still count when only TR1=1.
    {
        Fixture f;
        f.cpu.TMOD = 0x03; // T0 mode3
        f.cpu.TCON = 0x40; // TR1=1, TR0=0
        f.cpu.TH0 = 0x10;
        f.cpu.TL0 = 0x20;
        f.place(0x0410, 0x00);
        f.step();
        check(f.cpu.TH0 == 0x11 && f.cpu.TL0 == 0x20,
              "mode3_th0_runs_on_tr1",
              "TH0=%02x TL0=%02x", f.cpu.TH0, f.cpu.TL0);
    }
}

void test_external_interrupt_edge_level_behavior() {
    mark(0x00);

    // IT0=0 level mode: IE0 should remain set after vectoring.
    {
        Fixture f;
        f.cpu.IE = 0x81; // EA|EX0
        f.cpu.TCON &= static_cast<std::uint8_t>(~bit0); // IT0=0
        f.place(0x0500, 0x00);
        f.cpu.triggerExternalInterrupt0();
        f.step();
        check((f.cpu.TCON & 0x02) != 0,
              "it0_level_ie0_persists",
              "TCON=%02x expected IE0 set", f.cpu.TCON);
    }

    // IT0=1 edge mode: IE0 should auto-clear on vector.
    {
        Fixture f;
        f.cpu.IE = 0x81; // EA|EX0
        f.cpu.TCON |= bit0; // IT0=1
        f.place(0x0501, 0x00);
        f.cpu.triggerExternalInterrupt0();
        f.step();
        check((f.cpu.TCON & 0x02) == 0,
              "it0_edge_ie0_clears",
              "TCON=%02x expected IE0 cleared", f.cpu.TCON);
    }
}

void test_init_resets_interrupt_nesting() {
    mark(0x00);

    Fixture f;
    f.cpu.IE = 0x81; // EA|EX0
    f.place(0x0600, 0x00);
    f.cpu.triggerExternalInterrupt0();
    f.step(); // enter ISR, irqDepth becomes non-zero

    f.cpu.init(); // should clear internal nesting state
    f.cpu.IE = 0x81;
    f.place(0x0000, 0x00);
    f.cpu.triggerExternalInterrupt0();
    f.step();
    check(f.cpu.PC == 0x0003,
          "init_clears_irq_nesting",
          "PC=%04x expected 0003", f.cpu.PC);
}

void test_init_resets_register_bank() {
    for (int bank = 1; bank <= 3; ++bank) {
        Fixture f;
        f.place(0, 0x75, 0xd0, static_cast<std::uint8_t>(bank << 3)); // MOV PSW,#bank
        f.step();
        f.cpu.init();
        f.cpu.Ri(1, 0x5a);
        check(f.cpu.IRAM[1] == 0x5a && f.cpu.IRAM[bank * 8 + 1] == 0,
              "init_register_access_uses_bank_zero",
              "previous bank=%d IRAM[01]=%02x old R1=%02x",
              bank, f.cpu.IRAM[1], f.cpu.IRAM[bank * 8 + 1]);
        f.place(0, 0x78, 0x42); // First instruction after reset: MOV R0,#42h
        f.step();
        check(f.cpu.IRAM[0] == 0x42 && f.cpu.IRAM[bank * 8] == 0 && f.cpu.Ri(0) == 0x42,
              "init_first_instruction_uses_bank_zero",
              "previous bank=%d IRAM[00]=%02x old R0=%02x R0=%02x",
              bank, f.cpu.IRAM[0], f.cpu.IRAM[bank * 8], f.cpu.Ri(0));
    }
}

void test_disassembler_format_and_targets() {
    char out[32];

    // Fixed mnemonic column (5 chars + one separator space).
    {
        Fixture f;
        f.place(0x0000, 0x00); // nop
        f.cpu.disasm(out);
        check(std::strncmp(out, "nop   ", 6) == 0,
              "disasm_nop_column",
              "prefix='%.6s' expected 'nop   '", out);
    }

    {
        Fixture f;
        f.place(0x0000, 0x74, 0x55); // mov A,#55
        f.cpu.disasm(out);
        check(std::strncmp(out, "mov   ", 6) == 0,
              "disasm_mov_column",
              "prefix='%.6s' expected 'mov   '", out);
        check(std::strstr(out, "A,#55") != nullptr,
              "disasm_mov_operand",
              "line='%s' expected operand A,#55", out);
    }

    // AJMP/ACALL should include current page bits from PC+2.
    {
        Fixture f;
        f.place(0x8a34, 0x41, 0x5a); // AJMP block 2
        f.cpu.disasm(out);
        check(std::strstr(out, "8a5a") != nullptr,
              "disasm_ajmp_addr11_page",
              "line='%s' expected target 8a5a", out);
    }

    {
        Fixture f;
        f.place(0x9234, 0x51, 0x6b); // ACALL block 2
        f.cpu.disasm(out);
        check(std::strstr(out, "926b") != nullptr,
              "disasm_acall_addr11_page",
              "line='%s' expected target 926b", out);
    }

    // Relative branch target wraps/prints as 16-bit target.
    {
        Fixture f;
        f.place(0x1000, 0x80, static_cast<std::uint8_t>(-2)); // SJMP -2
        f.cpu.disasm(out);
        check(std::strstr(out, "1000") != nullptr,
              "disasm_sjmp_relative_target",
              "line='%s' expected target 1000", out);
    }

    // Reserved opcode should disassemble safely without garbage chars.
    {
        Fixture f;
        f.place(0x2000, 0xa5);
        f.cpu.disasm(out);
        check(std::strncmp(out, "?     ", 6) == 0,
              "disasm_a5_safe_prefix",
              "prefix='%.6s' expected '?     '", out);
    }
}

bool is_carry_branch(std::uint8_t op) {
    return op == 0x40 || op == 0x50;
}

bool is_acc_branch(std::uint8_t op) {
    return op == 0x60 || op == 0x70;
}

bool is_bit_branch(std::uint8_t op) {
    return op == 0x10 || op == 0x20 || op == 0x30;
}

void seed_operands_for_opcode(Fixture& f, std::uint8_t op) {
    // Default operands for deterministic one-step tests.
    std::uint8_t b1 = 0x20;
    std::uint8_t b2 = 0x02;

    if (op == 0x02 || op == 0x12) { b1 = 0x12; b2 = 0x34; } // ljmp/lcall
    if (op == 0x90) { b1 = 0x20; b2 = 0x10; } // mov dptr,#imm16
    if (op == 0x80) { b1 = 0x01; b2 = 0x00; } // sjmp +1
    if (op == 0x73) { b1 = 0x00; b2 = 0x00; } // jmp @a+dptr
    if (op == 0x22 || op == 0x32) { b1 = 0x00; b2 = 0x00; } // ret/reti
    if (is_bit_branch(op)) { b1 = 0x20; b2 = 0x02; }
    if (op == 0xd5) { b1 = 0x20; b2 = 0x02; }
    if ((op >= 0xb4 && op <= 0xbf) || (op >= 0xd8 && op <= 0xdf)) { b1 = 0x20; b2 = 0x02; }

    f.place(0x0200, op, b1, b2);
}

void test_dispatch_equivalence() {
    std::mt19937 random(8051);
    for (const auto model : {Emu51Model::i8051, Emu51Model::i8052})
    for (unsigned op = 0; op < 256; ++op) {
        Fixture sw, table;
        sw.cpu.setModel(model); table.cpu.setModel(model);
        for (auto& byte : sw.mem) byte = static_cast<std::uint8_t>(random());
        for (auto& byte : sw.cpu.IRAM) byte = static_cast<std::uint8_t>(random());
        for (auto& byte : sw.cpu.SFR) byte = static_cast<std::uint8_t>(random());
        sw.cpu.PSW = 0; // Both machines start in register bank zero.
        sw.cpu.IE = 0x9f;
        sw.place(0x1000, static_cast<std::uint8_t>(op), 0x40, 0x42);
        std::memcpy(table.mem, sw.mem, sizeof(sw.mem));
        std::memcpy(table.cpu.IRAM, sw.cpu.IRAM, sizeof(sw.cpu.IRAM));
        std::memcpy(table.cpu.SFR, sw.cpu.SFR, sizeof(sw.cpu.SFR));
        table.cpu.PC = sw.cpu.PC;
        for (unsigned step = 0; step < 16; ++step) {
            if (step == 4) { sw.cpu.triggerExternalInterrupt0(); table.cpu.triggerExternalInterrupt0(); }
            if (step == 9) { sw.cpu.triggerExternalInterrupt1(); table.cpu.triggerExternalInterrupt1(); }
            const int swCycles = sw.cpu.process();
            const int tableCycles = table.cpu.process_table();
            const bool equal = swCycles == tableCycles && sw.cpu.PC == table.cpu.PC &&
                std::memcmp(sw.cpu.IRAM, table.cpu.IRAM, sizeof(sw.cpu.IRAM)) == 0 &&
                std::memcmp(sw.cpu.SFR, table.cpu.SFR, sizeof(sw.cpu.SFR)) == 0 &&
                std::memcmp(sw.mem, table.mem, sizeof(sw.mem)) == 0;
            check(equal, "dispatch_equivalence", "initial op=%02x step=%u differs", op, step);
            if (!equal) break;
        }
    }
}

void test_opcode_assembly_roundtrip() {
    emu51_asm::Assembler assembler;
    for (unsigned op = 0; op < 256; ++op) {
        if (op == 0xa5) continue; // Reserved byte has no assembly mnemonic.
        Fixture f;
        f.place(0x1000, static_cast<std::uint8_t>(op), 0x40, 0x42);
        char disassembly[128]{};
        asm51[op].make_ds(f.cpu, 0x1000, disassembly);
        // The display uses bare hex; assembly input requires explicit radix.
        std::string source(disassembly);
        for (std::size_t i = 6; i < source.size(); ++i) {
            if (std::isdigit(static_cast<unsigned char>(source[i])) &&
                !std::isalnum(static_cast<unsigned char>(source[i - 1]))) {
                while (i < source.size() && std::isxdigit(static_cast<unsigned char>(source[i]))) ++i;
                source.insert(i, "h");
            }
        }
        const auto result = assembler.assemble_string("roundtrip.asm",
            std::string("org 1000h\n") + source + "\nend\n");
        check(result.success(), "opcode_roundtrip", "op=%02x assembly=%s failed", op, disassembly);
        if (!result.success()) continue;
        check(result.image.bytes.size() == asm51[op].lenght,
              "opcode_roundtrip", "op=%02x incorrect encoded length", op);
        for (unsigned i = 0; i < asm51[op].lenght; ++i) {
            const auto it = result.image.bytes.find(static_cast<std::uint16_t>(0x1000 + i));
            check(it != result.image.bytes.end() && it->second == f.mem[0x1000 + i],
                  "opcode_roundtrip", "op=%02x byte=%u assembly=%s differs", op, i, disassembly);
        }
    }
    const auto reserved = assembler.assemble_string("reserved.asm", "?\nend\n");
    check(!reserved.success(), "opcode_reserved", "Reserved A5 must not have an assembly mnemonic");
}

void test_all_opcodes_basic_contract() {
    for (int opi = 0; opi < 256; opi++) {
        const std::uint8_t op = static_cast<std::uint8_t>(opi);
        mark(op);
        Fixture f;
        seed_operands_for_opcode(f, op);

        // Common deterministic setup.
        f.cpu.Acc = 0x11;
        f.cpu.B = 0x03;
        f.cpu.PSW &= static_cast<std::uint8_t>(~0x80); // C=0
        f.cpu.Ri(0, 0x22);
        f.cpu.Ri(1, 0x23);
        f.cpu.IRAM[0x20] = 0x01; // bit-addressable byte with bit0=1
        f.cpu.IRAM[0x22] = 0x05;
        f.cpu.IRAM[0x23] = 0x07;
        f.cpu.IRAM[0x24] = 0x01;
        f.cpu.SP = 0x30;
        f.cpu.IRAM[0x31] = 0x34;
        f.cpu.IRAM[0x32] = 0x12;
        f.cpu.DPTR(0x3000);
        f.mem[0x3000] = 0x77;
        f.mem[0x3001] = 0x88;
        f.mem[0x1234] = 0x55;

        // Avoid taking conditional branches for this generic contract.
        if (is_carry_branch(op)) {
            f.cpu.PSW &= static_cast<std::uint8_t>(~0x80);
        }
        if (is_acc_branch(op)) {
            if (op == 0x60) f.cpu.Acc = 0x01; // jz not taken
            if (op == 0x70) f.cpu.Acc = 0x00; // jnz not taken
        }
        if (is_bit_branch(op)) {
            // bit at address 0x20 is 0 (so JB/JBC not taken, JNB taken handled by relative offset +2).
            f.cpu.IRAM[0x24] = 0x00;
        }
        if (op == 0xd5) {
            f.cpu.IRAM[0x20] = 0x01; // djnz will decrement to 0 and not jump
        }
        if (op >= 0xd8 && op <= 0xdf) {
            f.cpu.Ri(op - 0xd8, 0x01); // decrement to 0 and not jump
        }
        if (op >= 0xb4 && op <= 0xbf) {
            // Make compare equal so no jump.
            if (op == 0xb4 || op == 0xb5) { f.cpu.Acc = 0x20; }
            if (op == 0xb6) { f.cpu.IRAM[f.cpu.Ri(0)] = 0x20; }
            if (op == 0xb7) { f.cpu.IRAM[f.cpu.Ri(1)] = 0x20; }
            if (op >= 0xb8 && op <= 0xbf) { f.cpu.Ri(op - 0xb8, 0x20); }
        }
        if (op == 0x22 || op == 0x32) {
            f.cpu.SP = 0x32;
            f.cpu.IRAM[0x32] = 0x12;
            f.cpu.IRAM[0x31] = 0x34;
        }
        if (op == 0x73) {
            f.cpu.Acc = 0x10;
            f.cpu.DPTR(0x2000);
        }

        const std::uint16_t old_pc = f.cpu.PC;
        f.step();
        const std::uint16_t new_pc = f.cpu.PC;

        // Basic contract from ISA: CPU must make progress or jump/call/return.
        check(new_pc != old_pc,
              "opcode_progress",
              "op=%02x oldPC=%04x newPC=%04x", op, old_pc, new_pc);

        // Reserved opcode should still consume at least one byte in emulator.
        if (op == 0xa5) {
            check(new_pc == static_cast<std::uint16_t>(old_pc + 1),
                  "opcode_a5_reserved",
                  "reserved op A5 got PC=%04x expected=%04x", new_pc, static_cast<std::uint16_t>(old_pc + 1));
        }
    }
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string normalize_newlines(std::string text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            continue;
        }
        normalized.push_back(text[i]);
    }
    return normalized;
}

void test_program_loader_mtask2_hex_vs_asm_match() {
#ifndef EMU51_REPO_ROOT
#define EMU51_REPO_ROOT "."
#endif
    const std::filesystem::path repo_root = EMU51_REPO_ROOT;
    const std::filesystem::path asm_path = repo_root / "progs" / "asm" / "demo" / "mtask2.asm";
    const std::filesystem::path hex_path = repo_root / "progs" / "hex" / "mtask2.hex";

    std::array<std::uint8_t, 0x10000> asm_mem{};
    std::array<std::uint8_t, 0x10000> hex_mem{};
    emu51 asm_cpu(asm_mem.data());
    emu51 hex_cpu(hex_mem.data());

    const auto asm_result = emu51_asm::load_program_file(asm_cpu, asm_path);
    check(asm_result.success,
          "program_loader_mtask2_asm",
          "assembly load failed: %s", asm_result.message.c_str());

    const auto hex_result = emu51_asm::load_program_file(hex_cpu, hex_path);
    check(hex_result.success,
          "program_loader_mtask2_hex",
          "hex load failed: %s", hex_result.message.c_str());

    for (int addr = 0; addr < 0x10000; ++addr) {
        const std::uint8_t asm_byte = asm_cpu.readProg(static_cast<std::uint16_t>(addr));
        const std::uint8_t hex_byte = hex_cpu.readProg(static_cast<std::uint16_t>(addr));
        check(asm_byte == hex_byte,
              "program_loader_mtask2_compare",
              "address=%04x asm=%02x hex=%02x", addr, asm_byte, hex_byte);
        if (asm_byte != hex_byte)
            return;
    }
}

void test_program_loader_binary_load_address() {
    const std::filesystem::path bin_path =
        std::filesystem::temp_directory_path() / "emu51_program_loader_binary.bin";
    {
        std::ofstream out(bin_path, std::ios::binary);
        out.put(static_cast<char>(0xaa));
        out.put(static_cast<char>(0xbb));
        out.put(static_cast<char>(0xcc));
    }

    Fixture f;
    f.mem[0x1233] = 0x11;
    f.mem[0x1234] = 0x22;
    f.mem[0x1235] = 0x33;
    f.mem[0x1236] = 0x44;

    const auto result = emu51_asm::load_program_file(
        f.cpu,
        bin_path,
        emu51_asm::ProgramLoadOptions{0x1234});
    check(result.success,
          "program_loader_binary_offset_success",
          "binary load failed: %s", result.message.c_str());
    check(f.cpu.readProg(0x1233) == 0x11,
          "program_loader_binary_offset_before",
          "expected preceding byte unchanged, got %02x", f.cpu.readProg(0x1233));
    check(f.cpu.readProg(0x1234) == 0xaa &&
              f.cpu.readProg(0x1235) == 0xbb &&
              f.cpu.readProg(0x1236) == 0xcc,
          "program_loader_binary_offset_bytes",
          "loaded bytes=%02x %02x %02x",
          f.cpu.readProg(0x1234), f.cpu.readProg(0x1235), f.cpu.readProg(0x1236));

    std::error_code ec;
    std::filesystem::remove(bin_path, ec);
}

void test_program_loader_and_hex_edge_cases() {
    const std::filesystem::path long_hex_path =
        std::filesystem::temp_directory_path() / "emu51_loader_long_record.hex";
    const std::filesystem::path bad_hex_path =
        std::filesystem::temp_directory_path() / "emu51_loader_bad_checksum.hex";
    const std::filesystem::path too_large_bin_path =
        std::filesystem::temp_directory_path() / "emu51_loader_too_large.bin";

    std::vector<std::uint8_t> long_record;
    for (int i = 0; i < 32; ++i)
        long_record.push_back(static_cast<std::uint8_t>(i));

    {
        std::ofstream out(long_hex_path);
        out << intel_hex_record(0x00, 0x0100, long_record);
        out << intel_hex_record(0x01, 0x0000, {});
    }

    Fixture f;
    check(f.cpu.loadHex(long_hex_path.string().c_str()) == 0,
          "hex_loader_accepts_32_byte_record",
          "expected long Intel HEX record to load");
    for (int i = 0; i < 32; ++i) {
        check(f.cpu.readProg(static_cast<std::uint16_t>(0x0100 + i)) == static_cast<std::uint8_t>(i),
              "hex_loader_32_byte_record_contents",
              "addr=%04x got=%02x expected=%02x",
              0x0100 + i,
              f.cpu.readProg(static_cast<std::uint16_t>(0x0100 + i)),
              i);
    }

    {
        std::ofstream out(bad_hex_path);
        out << ":010000000000\n"; // Bad checksum; correct checksum would be FF.
    }
    Fixture bad;
    check(bad.cpu.loadHex(bad_hex_path.string().c_str()) != 0,
          "hex_loader_rejects_bad_checksum",
          "expected malformed Intel HEX to fail");

    {
        std::ofstream out(too_large_bin_path, std::ios::binary);
        std::vector<char> bytes(0x10001, '\0');
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    Fixture bin;
    const auto result = emu51_asm::load_program_file(bin.cpu, too_large_bin_path);
    check(!result.success,
          "program_loader_rejects_oversized_binary",
          "oversized binary should fail");

    std::error_code ec;
    std::filesystem::remove(long_hex_path, ec);
    std::filesystem::remove(bad_hex_path, ec);
    std::filesystem::remove(too_large_bin_path, ec);
}

void test_hex_loader_commits_only_valid_images() {
    const auto path = std::filesystem::temp_directory_path() / "emu51_loader_atomic.hex";
    std::array<std::uint8_t, 0x10000> memory;
    memory.fill(0x5a);
    const auto original = memory;
    int writes = 0;
    auto read = [&](std::uint16_t address) { return memory[address]; };
    auto write = [&](std::uint16_t address, std::uint8_t value) {
        ++writes;
        memory[address] = value;
    };
    functional_address_space space(read, write);
    emu51 cpu(space);
    const std::string valid_prefix = intel_hex_record(0x00, 0x0100, {0x74, 0x42});
    const std::vector<std::string> invalid_suffixes = {
        ":010000000000\n", // Bad checksum after valid data.
        "not a record\n",
        intel_hex_record(0x00, 0xffff, {0xaa, 0xbb}), // Overflow within a record.
        intel_hex_record(0x04, 0, {0, 1}) + intel_hex_record(0x00, 0, {0xaa}),
        intel_hex_record(0x02, 0, {0}), // Invalid extended-address record length.
        intel_hex_record(0x01, 0, {0}), // Invalid EOF record length.
    };
    for (std::size_t i = 0; i < invalid_suffixes.size(); ++i) {
        {
            std::ofstream out(path);
            out << valid_prefix << invalid_suffixes[i];
        }
        const auto result = emu51_asm::load_program_file(cpu, path);
        check(!result.success, "hex_atomic_rejects_invalid_image", "case=%zu", i);
        check(writes == 0 && memory == original,
              "hex_atomic_failure_leaves_memory_unchanged",
              "case=%zu write callbacks=%d", i, writes);
        memory = original;
        writes = 0;
    }
    // Successful sparse loads retain untouched bytes; later overlapping records win.
    // Keep accepting files that end at physical EOF, as the loader already did.
    for (bool with_eof : {false, true}) {
        memory = original;
        {
            std::ofstream out(path);
            out << valid_prefix << intel_hex_record(0x00, 0x0101, {0x99});
            if (with_eof)
                out << intel_hex_record(0x01, 0, {});
        }
        check(cpu.loadHex(path.string().c_str()) == 0,
              "hex_atomic_accepts_valid_image", "with EOF=%d", with_eof);
        check(memory[0x0100] == 0x74 && memory[0x0101] == 0x99 &&
                  memory[0x00ff] == 0x5a && memory[0x0102] == 0x5a,
              "hex_atomic_sparse_and_overlapping_records",
              "with EOF=%d bytes=%02x %02x %02x %02x", with_eof,
              memory[0x00ff], memory[0x0100], memory[0x0101], memory[0x0102]);
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void test_hex_loader_extended_address_records() {
    const std::filesystem::path seg_path =
        std::filesystem::temp_directory_path() / "emu51_loader_extended_seg.hex";
    const std::filesystem::path lin_overflow_path =
        std::filesystem::temp_directory_path() / "emu51_loader_extended_linear_overflow.hex";
    const std::filesystem::path bad_seg_count_path =
        std::filesystem::temp_directory_path() / "emu51_loader_extended_bad_count.hex";
    const std::filesystem::path no_colon_path =
        std::filesystem::temp_directory_path() / "emu51_loader_no_colon.hex";

    // Type 02 extended-segment: base = 0x0010 << 4 = 0x0100, then write at 0x0034 -> 0x0134.
    {
        std::ofstream out(seg_path);
        out << intel_hex_record(0x02, 0x0000, {0x00, 0x10});
        out << intel_hex_record(0x00, 0x0034, {0xbb, 0xcc});
        out << intel_hex_record(0x01, 0x0000, {});
    }
    {
        Fixture f;
        check(f.cpu.loadHex(seg_path.string().c_str()) == 0,
              "hex_loader_extended_segment_loads",
              "expected extended-segment record to load");
        check(f.cpu.readProg(0x0134) == 0xbb && f.cpu.readProg(0x0135) == 0xcc,
              "hex_loader_extended_segment_offset",
              "prog[0134]=%02x prog[0135]=%02x expected bb/cc",
              f.cpu.readProg(0x0134), f.cpu.readProg(0x0135));
        check(f.cpu.readProg(0x0034) == 0x00,
              "hex_loader_extended_segment_unshifted_untouched",
              "prog[0034]=%02x expected 00", f.cpu.readProg(0x0034));
    }

    // Type 04 with non-zero linear base puts targets past 0xffff -> loader rejects.
    {
        std::ofstream out(lin_overflow_path);
        out << intel_hex_record(0x04, 0x0000, {0x00, 0x01});
        out << intel_hex_record(0x00, 0x0000, {0xaa});
        out << intel_hex_record(0x01, 0x0000, {});
    }
    {
        Fixture f;
        check(f.cpu.loadHex(lin_overflow_path.string().c_str()) != 0,
              "hex_loader_extended_linear_overflow_rejected",
              "expected linear base above 64K with data to fail");
    }

    // Type 02 with wrong byte count must fail before changing program memory.
    {
        std::ofstream out(bad_seg_count_path);
        out << intel_hex_record(0x02, 0x0000, {0x00});
        out << intel_hex_record(0x01, 0x0000, {});
    }
    {
        Fixture f;
        f.cpu.writeProg(0x0000, 0x42);
        check(f.cpu.loadHex(bad_seg_count_path.string().c_str()) != 0,
              "hex_loader_extended_segment_bad_count_rejected",
              "expected type-02 record with wrong count to fail");
        check(f.cpu.readProg(0x0000) == 0x42,
              "hex_loader_extended_segment_bad_count_no_partial_write",
              "prog[0000]=%02x expected 42", f.cpu.readProg(0x0000));
    }

    // Line without leading colon must fail.
    {
        std::ofstream out(no_colon_path);
        out << "0100000001\n";
    }
    {
        Fixture f;
        check(f.cpu.loadHex(no_colon_path.string().c_str()) != 0,
              "hex_loader_rejects_missing_colon",
              "expected line without ':' prefix to fail");
    }

    std::error_code ec;
    std::filesystem::remove(seg_path, ec);
    std::filesystem::remove(lin_overflow_path, ec);
    std::filesystem::remove(bad_seg_count_path, ec);
    std::filesystem::remove(no_colon_path, ec);
}

void test_parse_hex_input_helper() {
    {
        const auto parsed = emu51ParseHexInput("1234", 0xffff);
        check(parsed.has_value() && *parsed == 0x1234u,
              "parse_hex_basic",
              "expected 0x1234 from '1234'");
    }
    {
        const auto parsed = emu51ParseHexInput("  ABcd  ", 0xffff);
        check(parsed.has_value() && *parsed == 0xabcdu,
              "parse_hex_trim_whitespace",
              "expected 0xabcd from ' ABcd '");
    }
    {
        const auto parsed = emu51ParseHexInput("", 0xffff);
        check(!parsed.has_value(),
              "parse_hex_rejects_empty",
              "expected nullopt for empty string");
    }
    {
        const auto parsed = emu51ParseHexInput("zzz", 0xffff);
        check(!parsed.has_value(),
              "parse_hex_rejects_garbage",
              "expected nullopt for non-hex input");
    }
    {
        const auto parsed = emu51ParseHexInput("12g", 0xffff);
        check(!parsed.has_value(),
              "parse_hex_rejects_trailing_junk",
              "expected nullopt for trailing non-hex char");
    }
    {
        const auto parsed = emu51ParseHexInput("10000", 0xffff);
        check(!parsed.has_value(),
              "parse_hex_rejects_above_max",
              "expected nullopt for value above max");
    }
    {
        const auto parsed = emu51ParseHexInput("ff", 0xff);
        check(parsed.has_value() && *parsed == 0xffu,
              "parse_hex_accepts_max_inclusive",
              "expected 0xff at max boundary");
    }
    {
        const char full[] = {'F', 'F'};
        const char whitespace[] = {' ', '\t'};
        check(emu51ParseHexInput(full, 0xff) == 0xfful && !emu51ParseHexInput(whitespace, 0xff),
              "parse_hex_bounded_arrays",
              "expected full non-terminated arrays to be parsed within their bounds");
    }
}

bool diagnostics_contain(const std::vector<emu51_asm::Diagnostic>& diags, const char* fragment) {
    for (const auto& diag : diags) {
        if (diag.message.find(fragment) != std::string::npos)
            return true;
    }
    return false;
}

void test_assembler_diagnostics() {
    // Unknown mnemonic surfaces a precise diagnostic.
    {
        emu51_asm::Assembler assembler;
        const auto result = assembler.assemble_string("unknown.asm", "frobnicate A,#5\nEND\n");
        check(!result.success(),
              "asm_unknown_mnemonic_fails",
              "expected diagnostic for unknown mnemonic");
        check(diagnostics_contain(result.diagnostics, "unknown mnemonic"),
              "asm_unknown_mnemonic_message",
              "no 'unknown mnemonic' diagnostic among %zu", result.diagnostics.size());
    }

    // Duplicate label is rejected.
    {
        emu51_asm::Assembler assembler;
        const auto result = assembler.assemble_string("dup.asm",
            "label:\tnop\nlabel:\tnop\nEND\n");
        check(!result.success(),
              "asm_duplicate_label_fails",
              "expected diagnostic for duplicate label");
        check(diagnostics_contain(result.diagnostics, "duplicate symbol"),
              "asm_duplicate_label_message",
              "no 'duplicate symbol' diagnostic");
    }

    // DB operand outside 0..0xff is rejected.
    {
        emu51_asm::Assembler assembler;
        const auto result = assembler.assemble_string("range.asm", "db 1000h\nEND\n");
        check(!result.success(),
              "asm_db_out_of_range_fails",
              "expected diagnostic for DB out of range");
        check(diagnostics_contain(result.diagnostics, "out of range"),
              "asm_db_out_of_range_message",
              "no 'out of range' diagnostic");
    }

    // SJMP target further than 127 bytes away is rejected.
    {
        emu51_asm::Assembler assembler;
        const auto result = assembler.assemble_string("sjmp.asm",
            "org 0\nsjmp far\norg 200h\nfar: nop\nEND\n");
        check(!result.success(),
              "asm_sjmp_far_fails",
              "expected diagnostic for far SJMP");
        check(diagnostics_contain(result.diagnostics, "relative branch out of range"),
              "asm_sjmp_far_message",
              "no 'relative branch out of range' diagnostic");
    }

    // Empty $INCLUDE() reports missing path.
    {
        emu51_asm::Assembler assembler;
        const auto result = assembler.assemble_string("empty_include.asm", "$INCLUDE()\nEND\n");
        check(!result.success(),
              "asm_empty_include_fails",
              "expected diagnostic for empty $INCLUDE");
        check(diagnostics_contain(result.diagnostics, "missing include path"),
              "asm_empty_include_message",
              "no 'missing include path' diagnostic");
    }

    // Unresolvable include path produces a 'unable to resolve include' diagnostic.
    {
        emu51_asm::Assembler assembler;
        const auto result = assembler.assemble_string("noinc.asm",
            "$INCLUDE(no_such_file_emu51_test_xyz.inc)\nEND\n");
        check(!result.success(),
              "asm_unresolvable_include_fails",
              "expected diagnostic for unresolved include");
        check(diagnostics_contain(result.diagnostics, "unable to resolve include"),
              "asm_unresolvable_include_message",
              "no 'unable to resolve include' diagnostic");
    }
}

void test_assembler_checked_expressions() {
    emu51_asm::Assembler assembler;
    struct InvalidCase { const char* expression; const char* diagnostic; };
    const InvalidCase invalid[] = {
        {"9223372036854775807+1", "integer overflow"},
        {"(-9223372036854775807-1)-1", "integer overflow"},
        {"0-(-9223372036854775807-1)", "integer overflow"},
        {"-9223372036854775807+(-2)", "integer overflow"},
        {"9223372036854775807-(-1)", "integer overflow"},
        {"-(-9223372036854775807-1)", "integer overflow in negation"},
        {"18446744073709551615+2", "numeric literal exceeds"},
        {"9223372036854775808", "numeric literal exceeds"},
        {"0xffffffffffffffff+2", "numeric literal exceeds"},
        {"18446744073709551616", "invalid numeric literal"},
    };
    for (const auto& item : invalid) {
        const auto result = assembler.assemble_string("boundary.asm", std::string("db ") + item.expression + "\nend\n");
        check(!result.success() && diagnostics_contain(result.diagnostics, item.diagnostic),
            "assembler_checked_expression", "did not reject %s with %s", item.expression, item.diagnostic);
        check(!result.diagnostics.empty() && result.diagnostics.front().file == "boundary.asm" &&
            result.diagnostics.front().line == 1, "assembler_overflow_location", "missing source location");
    }
    const char* valid[] = {
        "9223372036854775807-9223372036854775807+42",
        "(-9223372036854775807-1)+9223372036854775807+43",
        "0-(-9223372036854775807)-9223372036854775807+42",
        "-(-9223372036854775807)-9223372036854775807+42",
        "0+(-9223372036854775807-1)+9223372036854775807+43",
        "-1-(-9223372036854775807-1)-9223372036854775807+42",
    };
    for (const auto* expression : valid) {
        const auto result = assembler.assemble_string("valid.asm", std::string("db ") + expression + "\nend\n");
        check(result.success() && result.image.bytes.size() == 1 && result.image.bytes.at(0) == 42,
            "assembler_valid_boundary", "valid signed-boundary arithmetic failed: %s", expression);
    }
    // Deterministic expression fuzzing uses small operands and an independent
    // accumulated value, keeping the oracle's own arithmetic well within range.
    std::mt19937 random(0x8051);
    for (int trial = 0; trial < 1000; ++trial) {
        std::int64_t expected = 0;
        std::string expression = "0";
        for (int term = 0; term < 20; ++term) {
            const auto value = static_cast<std::int64_t>(random() % 1000000u);
            const bool add = (random() & 1) != 0;
            expected += add ? value : -value;
            expression += (add ? "+(+" : "-(-(-") + std::to_string(value) + (add ? ")" : "))");
        }
        expression += "-(" + std::to_string(expected) + ")+42";
        const auto result = assembler.assemble_string("generated.asm", "db " + expression + "\nend\n");
        check(result.success() && result.image.bytes.size() == 1 && result.image.bytes.at(0) == 42,
            "assembler_expression_fuzz", "generated expression failed at trial %d", trial);
    }
    const auto deep = assembler.assemble_string("deep.asm", "db " + std::string(10000, '-') + "1\n");
    check(!deep.success() && diagnostics_contain(deep.diagnostics, "nesting limit"),
        "assembler_expression_depth", "deep unary expression did not report a nesting limit");
}

void test_atomic_editor_saves_and_recovery() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() /
        ("emu51-file-tests-" + std::to_string(Emu51CycleTiming::Clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const fs::path source = root / "source.asm";
    check(emu51WriteTextFileAtomically(source, "original\n"), "atomic_new_file", "unable to create source");
    check(emu51WriteTextFileAtomically(source, "replacement\n") && read_text_file(source) == "replacement\n",
        "atomic_replace", "replacement failed");
    const fs::path directory = root / "directory";
    fs::create_directories(directory);
    std::ofstream(directory / "keep.txt") << "keep";
    check(!emu51WriteTextFileAtomically(directory, "bad") && read_text_file(directory / "keep.txt") == "keep",
        "atomic_failed_replace", "failed replacement damaged existing target");
    check(!emu51WriteTextFileAtomically(root / "absent" / "source.asm", "bad"),
        "atomic_missing_parent", "saving to an absent parent unexpectedly succeeded");
#if !defined(_WIN32)
    const fs::path link = root / "link.asm";
    fs::create_symlink(source.filename(), link);
    check(emu51WriteTextFileAtomically(link, "through symlink\n") && fs::is_symlink(link) &&
        read_text_file(source) == "through symlink\n", "atomic_symlink", "save replaced symlink instead of target");
    // Force a genuine mid-write failure in a child, preserving parent limits.
    const pid_t child = fork();
    if (child == 0) {
        std::signal(SIGXFSZ, SIG_IGN);
        const rlimit limit{1024, 1024};
        if (setrlimit(RLIMIT_FSIZE, &limit) != 0)
            _exit(2);
        _exit(emu51WriteTextFileAtomically(source, std::string(4096, 'x')) ? 1 : 0);
    }
    int status = 0;
    if (child > 0)
        waitpid(child, &status, 0);
    check(child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
        read_text_file(source) == "through symlink\n", "atomic_partial_write_failure",
        "a partial write did not leave the original intact");
#endif
    for (const auto& entry : fs::directory_iterator(root))
        check(!entry.path().filename().string().ends_with(".tmp"), "atomic_cleanup", "temporary file leaked");

    const fs::path recoveryRoot = root / "recovery";
    auto active = Emu51EditorRecovery::create(recoveryRoot);
    check(active != nullptr, "recovery_create", "unable to create recovery session");
    if (active) {
        const fs::path session = active->directory();
        const Emu51EditorDraft draft{source.string(), "source.asm", "mov a,#42h\nend\n"};
        check(active->save(draft), "recovery_save", "unable to save draft");
        check(!Emu51EditorRecovery::claim(session), "recovery_live_lock", "claimed another active editor's draft");
        const auto loaded = active->load();
        check(loaded && loaded->sourcePath == draft.sourcePath && loaded->sourceName == draft.sourceName &&
            loaded->text == draft.text, "recovery_roundtrip", "draft lost source metadata or contents");
        active.reset(); // Simulate an abandoned session; draft must survive.
        const auto candidates = Emu51EditorRecovery::candidates(recoveryRoot);
        check(candidates.size() == 1 && candidates.front() == session,
            "recovery_discovery", "abandoned draft was not discoverable");
        auto recovered = Emu51EditorRecovery::claim(session);
        check(recovered && recovered->load().has_value(), "recovery_claim", "cannot recover abandoned draft");
        if (recovered) {
            recovered->clear();
            check(!recovered->load(), "recovery_clear", "saved/discarded draft remains recoverable");
        }
        recovered.reset();
        check(!fs::exists(session), "recovery_session_cleanup", "empty recovery session leaked");
    }
    fs::remove_all(root);
}

void test_assembler_device_modules() {
    struct ModuleCase {
        const char* name;
        const char* source;
        std::vector<std::uint8_t> expected;
        bool timer2;
    };
    const ModuleCase cases[] = {
        {"mod51", "mov P0,#1\nsetb TR0\nmov A,ACC\n",
            {0x75, 0x80, 0x01, 0xd2, 0x8c, 0xe5, 0xe0}, false},
        {"mod8051", "mov P0,#1\nsetb TR0\nmov A,ACC\n",
            {0x75, 0x80, 0x01, 0xd2, 0x8c, 0xe5, 0xe0}, false},
        {"mod52", "mov T2CON,#0\nmov RCAP2L,#034h\nmov RCAP2H,#012h\n"
                  "mov TL2,#056h\nmov TH2,#078h\nsetb TR2\nsetb ET2\nsetb PT2\n"
                  "clr TF2\nsetb T2\nclr T2EX\n",
            {0x75, 0xc8, 0, 0x75, 0xca, 0x34, 0x75, 0xcb, 0x12, 0x75, 0xcc, 0x56,
             0x75, 0xcd, 0x78, 0xd2, 0xca, 0xd2, 0xad, 0xd2, 0xbd, 0xc2, 0xcf,
             0xd2, 0x90, 0xc2, 0x91}, true},
        {"mod8052", "setb CP_RL2\nsetb C_T2\nsetb EXEN2\nsetb TCLK\nsetb RCLK\nclr EXF2\n",
            {0xd2, 0xc8, 0xd2, 0xc9, 0xd2, 0xcb, 0xd2, 0xcc, 0xd2, 0xcd, 0xc2, 0xce}, true},
        {"mod851", "mov EADRL,#0\nmov EADRH,#1\nmov EDAT,#2\nmov ETIM,#3\nmov ECNTRL,#4\n",
            {0x75, 0xf2, 0, 0x75, 0xf3, 1, 0x75, 0xf4, 2, 0x75, 0xf5, 3, 0x75, 0xf6, 4}, false},
        {"mod89s51", "mov DP0L,#0\nmov DP0H,#1\nmov DP1L,#12h\nmov DP1H,#34h\n"
                    "orl AUXR1,#DPS_MASK\norl AUXR,#WDIDLE_MASK\nmov WDTRST,#01Eh\n",
            {0x75, 0x82, 0, 0x75, 0x83, 1, 0x75, 0x84, 0x12, 0x75, 0x85, 0x34,
             0x43, 0xa2, 1, 0x43, 0x8e, 0x10, 0x75, 0xa6, 0x1e}, false},
        {"mod89s52", "mov T2MOD,#T2OE_MASK\norl T2MOD,#DCEN_MASK\nsetb CP_RL2\n"
                    "setb C_T2\nmov DP1L,#34h\nmov WDTRST,#0E1h\n",
            {0x75, 0xc9, 2, 0x43, 0xc9, 1, 0xd2, 0xc8, 0xd2, 0xc9,
             0x75, 0x84, 0x34, 0x75, 0xa6, 0xe1}, true},
        {"mod89c2051", "mov P1,#55h\nsetb RXD\nclr TI\n",
            {0x75, 0x90, 0x55, 0xd2, 0xb0, 0xc2, 0x99}, false},
    };
    emu51_asm::Assembler assembler;
    for (const auto& test : cases) {
        std::string mixed = test.name;
        for (std::size_t i = 0; i < mixed.size(); i += 2)
            mixed[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(mixed[i])));
        const std::string directives[] = {"$" + std::string(test.name), "$" + mixed,
            "$INCLUDE(" + std::string(test.name) + ")"};
        for (const auto& directive : directives) {
            const auto result = assembler.assemble_string("device.asm",
                directive + "\norg 0\n" + test.source + "end\n");
            check(result.success() && result.image.to_binary() == test.expected,
                "assembler_device_module", "%s: incorrect encoding or %zu diagnostics",
                directive.c_str(), result.diagnostics.size());
            check(result.symbols.contains("T2CON") == test.timer2,
                "assembler_device_timer2", "%s: incorrect Timer 2 symbol availability", test.name);
            check(result.symbols.contains("ECNTRL") == (std::string_view(test.name) == "mod851"),
                "assembler_device_eeprom", "%s: incorrect EEPROM symbol availability", test.name);
            if (std::string_view(test.name) == "mod89c2051") {
                for (const char* absent : {"P0", "P2", "WR", "RD", "AUXR", "WDTRST"})
                    check(!result.symbols.contains(absent), "assembler_device_reduced_ports",
                        "%s must not define %s", test.name, absent);
            }
            if (std::string_view(test.name) == "mod52" || std::string_view(test.name) == "mod8052")
                check(!result.symbols.contains("T2MOD"), "assembler_generic_timer2",
                    "generic 8052 must not define vendor-specific T2MOD");
            for (const char* mask : {"DPS_MASK", "DCEN_MASK", "T2OE_MASK"}) {
                const auto symbol = result.symbols.find(mask);
                if (symbol != result.symbols.end())
                    check(symbol->second.kind == emu51_asm::SymbolKind::equ,
                        "assembler_device_mask_kind", "%s: %s is a mask, not a bit address", test.name, mask);
            }
        }
    }

    // New/custom module names use the existing -I mechanism and include diagnostics.
    const auto root = std::filesystem::temp_directory_path() /
        ("emu51-module-tests-" + std::to_string(Emu51CycleTiming::Clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    std::ofstream(root / "modcustom") << "CUSTOM DATA 0A5h\n";
    emu51_asm::AssembleOptions options;
    options.include_directories.push_back(root);
    const auto custom = emu51_asm::Assembler(options).assemble_string("custom.asm",
        "$MoDcUsToM\nmov CUSTOM,#42h\nend\n");
    check(custom.success() && custom.image.to_binary() == std::vector<std::uint8_t>{0x75, 0xa5, 0x42},
        "assembler_custom_module", "custom module did not resolve through the include path");
    const auto missing = assembler.assemble_string("missing.asm", "$MODDOESNOTEXIST\nend\n");
    check(!missing.success() && diagnostics_contain(missing.diagnostics, "unable to resolve include 'moddoesnotexist'")
        && missing.diagnostics.front().file == "missing.asm" && missing.diagnostics.front().line == 1,
        "assembler_missing_module", "missing module did not report its name and source location");
    const auto malformed = assembler.assemble_string("malformed.asm", "$MOD51 extra\nend\n");
    check(!malformed.success() && diagnostics_contain(malformed.diagnostics, "unsupported directive"),
        "assembler_malformed_module", "module directive with trailing arguments was accepted");
    std::filesystem::remove_all(root);
}

void test_assembler_relative_branch_and_binary_output() {
    static const char* source =
        "$mod851\n"
        "org 0000h\n"
        "start: sjmp target\n"
        "db 00h\n"
        "target: nop\n"
        "END\n";

    emu51_asm::Assembler assembler;
    const auto result = assembler.assemble_string("relative.asm", source);
    check(result.success(),
          "assembler_relative_success",
          "expected success, got %zu diagnostics", result.diagnostics.size());
    check(result.image.bytes.size() == 4,
          "assembler_relative_size",
          "byte count=%zu expected=4", result.image.bytes.size());

    const auto binary = result.image.to_binary();
    check(binary.size() == 4 &&
              binary[0] == 0x80 &&
              binary[1] == 0x01 &&
              binary[2] == 0x00 &&
              binary[3] == 0x00,
          "assembler_relative_bytes",
          "unexpected bytes");
}

void test_assembler_relative_branch_wraparound() {
    emu51_asm::Assembler assembler;
    struct Branch {
        const char* instruction;
        std::uint8_t opcode;
        int length;
    };
    const Branch branches[] = {{"sjmp", 0x80, 2}, {"cjne A,#0,", 0xb4, 3}};
    const std::uint16_t addresses[] = {0x0000, 0x007e, 0x1000, 0xfffd, 0xfffe, 0xffff};
    for (const Branch& branch : branches) {
        for (std::uint16_t pc : addresses) {
            for (int offset : {-129, -128, -1, 0, 1, 127, 128}) {
                const auto target = static_cast<std::uint16_t>(pc + branch.length + offset);
                char source[128];
                std::snprintf(source, sizeof(source), "org 0%04xh\n%s 0%04xh\nEND\n",
                              pc, branch.instruction, target);
                const auto result = assembler.assemble_string("branch_wrap.asm", source);
                const bool in_range = offset >= -128 && offset <= 127;
                check(result.success() == in_range,
                      "assembler_relative_wrap_range",
                      "instruction=%s pc=%04x offset=%d success=%d",
                      branch.instruction, pc, offset, result.success());
                if (!in_range || !result.success())
                    continue;
                const auto displacement_address = static_cast<std::uint16_t>(pc + branch.length - 1);
                const auto displacement = result.image.bytes.find(displacement_address);
                const auto opcode = result.image.bytes.find(pc);
                check(displacement != result.image.bytes.end() &&
                          displacement->second == static_cast<std::uint8_t>(offset) &&
                          opcode != result.image.bytes.end() && opcode->second == branch.opcode,
                      "assembler_relative_wrap_encoding",
                      "instruction=%s pc=%04x offset=%d", branch.instruction, pc, offset);
            }
        }
    }
}

void test_assembler_dw_and_hex_output() {
    static const char* source =
        "org 0100h\n"
        "db 1,2,3\n"
        "dw 1234h\n"
        "END\n";

    emu51_asm::Assembler assembler;
    const auto result = assembler.assemble_string("data.asm", source);
    check(result.success(),
          "assembler_data_success",
          "expected success, got %zu diagnostics", result.diagnostics.size());

    const auto binary = result.image.to_binary();
    check(binary.size() == 5 &&
              binary[0] == 0x01 &&
              binary[1] == 0x02 &&
              binary[2] == 0x03 &&
              binary[3] == 0x12 &&
              binary[4] == 0x34,
          "assembler_data_binary",
          "unexpected binary output");

    const std::string expected_hex = ":050100000102031234AE\n:00000001FF\n";
    check(result.image.to_intel_hex() == expected_hex,
          "assembler_data_hex",
          "unexpected Intel HEX output");
}

void test_assembler_matches_existing_sample_hex() {
#ifndef EMU51_REPO_ROOT
#define EMU51_REPO_ROOT "."
#endif
    const std::filesystem::path repo_root = EMU51_REPO_ROOT;
    const std::filesystem::path asm_path = repo_root / "progs" / "asm" / "demo" / "mtask2.asm";
    const std::filesystem::path hex_path = repo_root / "progs" / "hex" / "mtask2.hex";

    emu51_asm::Assembler assembler;
    const auto result = assembler.assemble_file(asm_path);
    check(result.success(),
          "assembler_sample_success",
          "expected success, got %zu diagnostics", result.diagnostics.size());

    const std::string expected_hex = normalize_newlines(read_text_file(hex_path));
    check(normalize_newlines(result.image.to_intel_hex()) == expected_hex,
          "assembler_sample_match",
          "assembled HEX does not match sample output");
}

std::uint64_t run_due_cycles(Emu51CycleTiming& timing, emu51& cpu, int limit = 1000000) {
    std::uint64_t total = 0;
    for (int step = 0; step < limit && timing.cyclesDue(); ++step) {
        const unsigned cycles = static_cast<unsigned>(cpu.process());
        timing.consume(cycles);
        total += cycles;
    }
    return total;
}

void test_cycle_timing_frequency_and_frame_rate() {
    using namespace std::chrono;
    struct TimingCase {
        int frames;
        double mhz;
        std::uint64_t expected;
    };
    const TimingCase cases[] = {
        {30, 12.0, 1000000}, {60, 12.0, 1000000}, {144, 12.0, 1000000},
        {60, 11.0592, 921600}, {60, 24.0, 2000000},
    };
    for (const TimingCase& test : cases) {
        Fixture f; // Zero-filled program: NOPs, one cycle each.
        Emu51CycleTiming timing;
        const Emu51CycleTiming::Clock::time_point start{};
        timing.reset(start);
        std::uint64_t total = 0;
        for (int frame = 1; frame <= test.frames; ++frame) {
            const auto now = start + nanoseconds(1000000000LL * frame / test.frames);
            timing.update(now, test.mhz);
            total += run_due_cycles(timing, f.cpu);
        }
        check(total == test.expected && !timing.cyclesDue(),
              "cycle_timing_frequency", "fps=%d MHz=%.4f cycles=%llu expected=%llu",
              test.frames, test.mhz, static_cast<unsigned long long>(total),
              static_cast<unsigned long long>(test.expected));
        check(std::abs(timing.cyclesPerSecond(start + seconds(1)) - static_cast<double>(test.expected)) < 0.001,
              "cycle_timing_measured_rate", "fps=%d MHz=%.4f measured=%.3f cycles/s",
              test.frames, test.mhz, timing.cyclesPerSecond(start + seconds(1)));
    }

    Fixture mixed;
    mixed.mem[0] = 0x00; // NOP (1 cycle)
    mixed.mem[1] = 0xa4; // MUL AB (4 cycles)
    mixed.mem[2] = 0x04; // INC A (1 cycle)
    mixed.mem[3] = 0x80; mixed.mem[4] = 0xfb; // SJMP to 0 (2 cycles)
    Emu51CycleTiming timing;
    const Emu51CycleTiming::Clock::time_point start{};
    timing.reset(start);
    std::uint64_t total = 0;
    for (const int millisecondsFromStart : {1, 7, 22, 25, 110, 333, 500, 620, 999, 1000}) {
        timing.update(start + milliseconds(millisecondsFromStart), 12.0);
        total += run_due_cycles(timing, mixed.cpu);
    }
    check(total >= 1000000 && total - 1000000 < 4 && !timing.cyclesDue(),
          "cycle_timing_mixed_costs_and_jitter", "cycles=%llu expected within one instruction of 1000000",
          static_cast<unsigned long long>(total));
}

void test_cycle_timing_instruction_debt_and_work_limit() {
    using namespace std::chrono;
    Fixture f;
    f.cpu.IE = 0x81;
    f.cpu.TCON = bit0;
    f.cpu.triggerExternalInterrupt0();
    Emu51CycleTiming timing;
    const Emu51CycleTiming::Clock::time_point start{};
    timing.reset(start);
    timing.update(start + microseconds(1), 12.0);
    const auto entry = run_due_cycles(timing, f.cpu);
    check(entry == 3 && f.cpu.PC == 3 && !timing.cyclesDue(),
          "cycle_timing_interrupt_debt", "cycles=%llu PC=%04x", static_cast<unsigned long long>(entry), f.cpu.PC);
    timing.update(start + microseconds(3), 12.0);
    check(!timing.cyclesDue(), "cycle_timing_debt_preserved", "interrupt entry cycles were lost");
    timing.update(start + microseconds(4), 12.0);
    check(run_due_cycles(timing, f.cpu) == 1 && f.cpu.PC == 4,
          "cycle_timing_debt_repaid", "expected one ISR NOP at four elapsed cycles");

    timing.reset(start);
    timing.update(start + milliseconds(1), 12.0);
    const auto limited = run_due_cycles(timing, f.cpu, 10);
    const bool backlog = timing.cyclesDue();
    check(timing.cyclesPerSecond(start + milliseconds(1)) == 10000.0,
          "cycle_timing_measured_rate_counts_executed_work", "measurement included unexecuted budget");
    timing.update(start + milliseconds(1), 12.0); // No additional host time.
    const auto remaining = run_due_cycles(timing, f.cpu);
    check(limited == 10 && backlog && remaining == 990 && !timing.cyclesDue(),
          "cycle_timing_work_limit_retains_cycles", "first=%llu remaining=%llu",
          static_cast<unsigned long long>(limited), static_cast<unsigned long long>(remaining));
}

void test_cycle_timing_reset_and_validation() {
    using namespace std::chrono;
    Fixture f;
    Emu51CycleTiming timing;
    const Emu51CycleTiming::Clock::time_point start{};
    check(timing.update(start, 12.0), "cycle_timing_first_rebase_signal", "initialization did not report a rebase");
    check(!timing.cyclesDue(), "cycle_timing_first_update", "first update should establish the clock origin");
    timing.update(start + milliseconds(10), 12.0); // Unconsumed work.
    timing.reset(start + seconds(5)); // Pause/resume, reset, or settings Apply.
    check(!timing.cyclesDue(), "cycle_timing_reset_clears_backlog", "reset retained old cycles");
    check(timing.cyclesPerSecond(start + seconds(6)) == 0.0,
          "cycle_timing_reset_clears_measurement", "reset retained execution speed from the previous run");
    timing.update(start + seconds(5) + milliseconds(1), 24.0);
    check(run_due_cycles(timing, f.cpu) == 2000,
          "cycle_timing_resume_new_frequency", "expected 2000 cycles at 24 MHz after resuming for 1 ms");

    check(timing.update(start + seconds(10), 12.0), "cycle_timing_suspend_rebase_signal", "long suspension did not report a rebase");
    check(!timing.cyclesDue(), "cycle_timing_long_suspension", "long suspension created catch-up work");
    check(!timing.update(start + seconds(10) + milliseconds(1), 12.0), "cycle_timing_continuous_signal", "ordinary execution reported a rebase");
    check(run_due_cycles(timing, f.cpu) == 1000,
          "cycle_timing_after_suspension", "expected 1000 cycles after a fresh 1 ms interval");
    timing.update(start, 12.0); // Defensive handling of an out-of-order timestamp.
    check(!timing.cyclesDue(), "cycle_timing_clock_rebase", "backwards time created work");

    for (const double mhz : {0.0, -12.0, 0.000000001, 1001.0, std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
        timing.reset(start);
        timing.update(start + milliseconds(1), mhz);
        check(!Emu51CycleTiming::validCrystalMHz(mhz) && !timing.cyclesDue(),
              "cycle_timing_invalid_frequency", "invalid MHz=%g accepted", mhz);
    }
    check(Emu51CycleTiming::kDefaultCrystalMHz == 12.0 &&
              Emu51CycleTiming::validCrystalMHz(0.5) && Emu51CycleTiming::validCrystalMHz(11.0592) &&
              Emu51CycleTiming::validCrystalMHz(1000.0),
          "cycle_timing_valid_frequencies", "default or fractional MHz validation failed");
}

void test_execution_settings_persistence() {
    const auto path = std::filesystem::temp_directory_path() /
        ("emu51_execution_settings_" + std::to_string(Emu51CycleTiming::Clock::now().time_since_epoch().count()) + ".json");
    const auto defaults = emu51LoadExecutionSettings(path);
    check(defaults.cycleExact && defaults.crystalMHz == 12.0 && defaults.stepsPerFrame == 500,
          "execution_settings_defaults", "missing config should use Cycle exact at 12 MHz");

    const Emu51ExecutionSettings cases[] = {
        {.cycleExact = true, .crystalMHz = 11.0592, .stepsPerFrame = 1234},
        {.cycleExact = false, .crystalMHz = 24.000001, .stepsPerFrame = 1000000},
        {.cycleExact = true, .crystalMHz = 12.00000001, .stepsPerFrame = 1},
    };
    for (const auto& settings : cases) {
        check(emu51SaveExecutionSettings(path, settings), "execution_settings_save", "could not save configuration");
        const auto restored = emu51LoadExecutionSettings(path);
        check(restored.cycleExact == settings.cycleExact && restored.crystalMHz == settings.crystalMHz &&
                  restored.stepsPerFrame == settings.stepsPerFrame,
              "execution_settings_roundtrip", "mode=%d MHz=%.17g steps=%d", restored.cycleExact,
              restored.crystalMHz, restored.stepsPerFrame);
    }

    auto invalid = cases[0];
    invalid.crystalMHz = std::numeric_limits<double>::quiet_NaN();
    check(!emu51SaveExecutionSettings(path, invalid),
          "execution_settings_reject_invalid_save", "invalid settings were written");
    const auto preserved = emu51LoadExecutionSettings(path);
    check(preserved.crystalMHz == cases[2].crystalMHz && preserved.stepsPerFrame == cases[2].stepsPerFrame,
          "execution_settings_failed_save_preserves_file", "invalid save overwrote existing settings");
    check(!emu51SaveExecutionSettings({}, cases[0]) &&
              !emu51SaveExecutionSettings(std::filesystem::temp_directory_path(), cases[0]),
          "execution_settings_report_save_failure", "saving to an invalid destination reported success");
    std::filesystem::remove(path);
}

void test_execution_settings_invalid_and_partial_config() {
    const auto path = std::filesystem::temp_directory_path() /
        ("emu51_execution_settings_invalid_" + std::to_string(Emu51CycleTiming::Clock::now().time_since_epoch().count()) + ".json");
    struct InvalidValue {
        const char* json;
        bool schemaFailure;
    };
    // Application bounds fall back per field. Schema failures (wrong types or
    // unrepresentable values) reject the complete configuration.
    const InvalidValue frequencies[] = {
        {"0", false}, {"-12", false}, {"1001", false}, {"0.000000001", false},
        {"1e999", true}, {R"("nan")", true}, {R"("inf")", true}, {R"("12")", true},
        {"null", true}, {"true", true}, {"{}", true}, {"[]", true},
    };
    for (const auto& mhz : frequencies) {
        {
            std::ofstream output(path);
            output << "{\"execution_mode\":\"instructions_per_frame\",\"crystal_mhz\":" << mhz.json
                   << ",\"instructions_per_frame\":1234}";
        }
        const auto restored = emu51LoadExecutionSettings(path);
        check(restored.cycleExact == mhz.schemaFailure && restored.crystalMHz == 12.0 &&
                  restored.stepsPerFrame == (mhz.schemaFailure ? 500 : 1234),
              "execution_settings_invalid_frequency", "invalid MHz '%s' did not use the expected defaults", mhz.json);
    }
    const InvalidValue stepCounts[] = {
        {"0", false}, {"-1", false}, {"1000001", false}, {"2147483648", true},
        {"4.5", true}, {"1e3", true}, {R"("4")", true},
        {"null", true}, {"true", true}, {"{}", true}, {"[]", true},
    };
    for (const auto& steps : stepCounts) {
        {
            std::ofstream output(path);
            output << "{\"execution_mode\":\"invalid_mode\",\"crystal_mhz\":11.0592,\"instructions_per_frame\":"
                   << steps.json << '}';
        }
        const auto restored = emu51LoadExecutionSettings(path);
        check(restored.cycleExact && restored.crystalMHz == (steps.schemaFailure ? 12.0 : 11.0592) &&
                  restored.stepsPerFrame == 500,
              "execution_settings_invalid_mode_and_steps", "invalid steps '%s' or mode was accepted", steps.json);
    }
    {
        std::ofstream output(path);
        output << "\r\n{\"unknown_setting\":{\"execution_mode\":\"instructions_per_frame\"},\r\n"
                  "\"crystal_mhz\": 11.0592 }\r\n";
    }
    const auto partial = emu51LoadExecutionSettings(path);
    check(partial.cycleExact && partial.crystalMHz == 11.0592 && partial.stepsPerFrame == 500,
          "execution_settings_partial_config", "partial configuration did not preserve defaults");
    {
        std::ofstream output(path);
        output << R"({"execution_\u006dode":"instructions_per_\u0066rame","crystal_\u006dhz":1.10592e1,"instructions_per_frame":1234})";
    }
    const auto escaped = emu51LoadExecutionSettings(path);
    check(!escaped.cycleExact && escaped.crystalMHz == 11.0592 && escaped.stepsPerFrame == 1234,
          "execution_settings_json_escapes", "escaped JSON keys/values or scientific notation were not decoded");
    std::filesystem::remove(path);
}

void test_execution_settings_malformed_json() {
    const auto path = std::filesystem::temp_directory_path() /
        ("emu51_execution_settings_malformed_" + std::to_string(Emu51CycleTiming::Clock::now().time_since_epoch().count()) + ".json");
    std::vector<std::string> documents = {
        "", " ", "null", "12", R"("text")", "[]",
        R"([{"execution_mode":"instructions_per_frame","crystal_mhz":24}])",
        R"({"execution_mode":"instructions_per_frame",)",
        R"({"crystal_mhz":24,})",
        R"({"execution_mode":"instructions_per_frame"} garbage)",
        R"({"execution_mode":"instructions_per_frame"}{"crystal_mhz":24})",
        R"({"execution_mode":"instructions_per_frame","ignored":[1,]})",
        R"({"execution_mode":"instructions_per_frame","crystal_mhz":nan})",
        R"({"execution_mode":true,"crystal_mhz":24})",
        R"({"execution_mode":"instructions_per_frame","crystal_mhz":24,"instructions_per_frame":1234,"execution_mode":"cycle_exact"})",
        R"({"execution_mode":"instructions_per_frame","crystal_mhz":24,"instructions_per_frame":1234,"crystal_mhz":12})",
        R"({"execution_mode":"instructions_per_frame","crystal_mhz":24,"instructions_per_frame":1234,"instructions_per_frame":500})",
        R"({"execution_mode":"a_mode_name_that_exceeds_the_schema_string_capacity","crystal_mhz":24})",
    };
    documents.push_back(std::string(R"({"execution_mode":"instructions_per_frame"})") + '\0' + "garbage");
    for (const auto& document : documents) {
        {
            std::ofstream output(path, std::ios::binary);
            output.write(document.data(), static_cast<std::streamsize>(document.size()));
        }
        const auto restored = emu51LoadExecutionSettings(path);
        check(restored.cycleExact && restored.crystalMHz == 12.0 && restored.stepsPerFrame == 500,
              "execution_settings_malformed_json", "malformed or non-object JSON applied partial settings: %s", document.c_str());
    }
    std::filesystem::remove(path);
}

void check_opcode_coverage() {
    for (int op = 0; op < 256; op++) {
        if (!g_seen[op]) {
            fail("coverage", "opcode 0x%02x missing test", op);
        }
    }
}

} // namespace

int emu51_serial_tests();
int emu51_model_tests();
int emu51_validation_tests();

int main() {
    g_failures += emu51_serial_tests();
    g_failures += emu51_model_tests();
    g_failures += emu51_validation_tests();
    test_dptr_roundtrip();
    test_parity_flag_odd_is_set();
    test_a5_advances_pc();
    test_ajmp_acall_page_behavior();
    test_movc_reads_program_memory();
    test_xchd_keeps_upper_nibble();
    test_movx_and_mov_coverage();
    test_mov_direct_imm_and_mov_bit_c_regressions();
    test_address_space_and_program_memory_edges();
    test_arithmetic_and_logic_coverage();
    test_arithmetic_flag_edge_cases();
    test_subb_and_mul_div_flag_rules();
    test_div_by_zero_sets_ov_and_clears_c();
    test_da_a_known_vectors();
    test_da_a_all_bcd_sums();
    test_bit_address_mapping_and_cpl_roundtrip();
    test_inc_dec_rotate_swap_clr_cpl_setb_coverage();
    test_branch_coverage();
    test_pc_stack_bit_and_branch_wrap_edges();
    test_cjne_djnz_coverage();
    test_stack_xch_coverage();
    test_mov_indirect_single_cycle_timing();
    test_interrupt_entry_cycles();
    test_timer_overflow_during_interrupt_entry();
    test_timer_mode2_reload_timing();
    test_timer_mode2_reload_during_interrupt_entry();
    test_external_interrupt_trigger_and_reti();
    test_tf0_persists_until_serviced();
    test_interrupt_priority_and_preemption();
    test_timer_gate_and_counter_bits();
    test_timer_modes_0_and_3();
    test_external_interrupt_edge_level_behavior();
    test_init_resets_interrupt_nesting();
    test_init_resets_register_bank();
    test_disassembler_format_and_targets();
    test_all_opcodes_basic_contract();
    test_opcode_assembly_roundtrip();
    test_dispatch_equivalence();
    test_assembler_relative_branch_and_binary_output();
    test_assembler_relative_branch_wraparound();
    test_assembler_dw_and_hex_output();
    test_assembler_matches_existing_sample_hex();
    test_assembler_diagnostics();
    test_assembler_device_modules();
    test_assembler_checked_expressions();
    test_atomic_editor_saves_and_recovery();
    test_parse_hex_input_helper();
    test_program_loader_mtask2_hex_vs_asm_match();
    test_program_loader_binary_load_address();
    test_program_loader_and_hex_edge_cases();
    test_hex_loader_commits_only_valid_images();
    test_hex_loader_extended_address_records();
    test_cycle_timing_frequency_and_frame_rate();
    test_cycle_timing_instruction_debt_and_work_limit();
    test_cycle_timing_reset_and_validation();
    test_execution_settings_persistence();
    test_execution_settings_invalid_and_partial_config();
    test_execution_settings_malformed_json();
    check_opcode_coverage();

    if (g_failures != 0) {
        std::fprintf(stderr, "\nTotal failures: %d\n", g_failures);
        return 1;
    }

    std::puts("emu51 instruction compliance tests passed");
    return 0;
}
