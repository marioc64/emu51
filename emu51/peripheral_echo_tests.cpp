// SPDX-License-Identifier: GPL-2.0-only
#include "emu51.h"
#include "assembler.h"
#include "emu51_peripheral_firmware.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {
void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
// DSP test source: drive P0 with controlled ADC codes and hold INTR ready.
// The application/NULL-audio tests separately exercise the real ADC and GPIO graph.
class SamplePorts final : public emu51_port_io {
public:
    std::uint8_t input = 128;
    std::array<std::uint8_t, 4> latch{255, 255, 255, 255};
    std::uint8_t readPort(unsigned i) const override {
        return i == 0 ? input : i == 3 ? static_cast<std::uint8_t>(latch[i] & ~4u) : latch[i];
    }
    void writePort(unsigned i, std::uint8_t value) override { latch[i] = value; }
    void advance(unsigned) override {}
    void reset() override { latch.fill(255); }
};
void run(unsigned size, unsigned base, bool table) {
    std::string source = emu51MicrophoneCovoxFirmware;
    const auto replace = [&](const std::string& from, const std::string& to) {
        const auto where = source.find(from); check(where != std::string::npos, "Echo configuration constants exist");
        source.replace(where, from.size(), to);
    };
    replace("BUFFER_SIZE EQU 4096", "BUFFER_SIZE EQU " + std::to_string(size));
    replace("ECHO_BASE EQU 08000h", "ECHO_BASE EQU " + std::to_string(base));
    const auto assembly = emu51_asm::Assembler{}.assemble_string("microphone_covox.asm", source);
    if (!assembly.success()) for (const auto& d : assembly.diagnostics) std::fprintf(stderr, "%s\n", d.message.c_str());
    check(assembly.success(), "Echo firmware assembles for arbitrary buffer lengths");
    std::array<std::uint8_t, 65536> data, program{};
    data.fill(0xa5);
    auto memory = std::make_shared<functional_address_space>(
        [&](std::uint16_t a) { return data[a]; }, [&](std::uint16_t a, std::uint8_t v) { data[a] = v; },
        [&](std::uint16_t a) { return program[a]; }, [&](std::uint16_t a, std::uint8_t v) { program[a] = v; });
    emu51 cpu(memory);
    auto ports = std::make_shared<SamplePorts>(); cpu.setPortIo(ports);
    emu51_asm::apply_program_image(cpu, assembly.image);
    const auto step = [&] { if (table) cpu.process_table(); else cpu.process(); };
    unsigned init = 0;
    while (!cpu.IRAM[0x32] && init++ < 1000000) step();
    check(cpu.IRAM[0x32] == 1 && cpu.DPTR() == base, "Buffer initialization finishes with pointer at its base");
    for (unsigned a = 0; a != data.size(); ++a)
        check(data[a] == (a >= base && a < base + size ? 128 : 0xa5), "Initialization fills only the selected buffer with unsigned silence");
    std::vector<unsigned> history(size, 128);
    const unsigned samples = 3 * size + 19;
    for (unsigned i = 0; i < samples; ++i) {
        // An impulse returns after exactly N samples, decaying on each wrap.
        // Then odd and full-scale codes verify logical shifts and rounding.
        const unsigned input = i == 0 ? 255 : i < 2 * size + 1 ? 128 : (i * 73 + 19) & 255;
        const auto position = i % size;
        const auto expected = (input >> 1) + (history[position] >> 1);
        ports->input = static_cast<std::uint8_t>(input);
        cpu.PSW |= 0x80; // Incoming carry must not enter either logical shift.
        bool output = false;
        for (unsigned n = 0; n < 256; ++n) {
            const auto pc = cpu.PC;
            output = cpu.readProg(pc) == 0xf5 && cpu.readProg(static_cast<std::uint16_t>(pc + 1)) == 0x90;
            step();
            if (output) break;
        }
        check(output, "Each ADC code reaches the Covox write");
        check(cpu.DPTR() == base + position && data[base + position] == expected,
            "Mix overwrites the same circular slot before incrementing its pointer");
        check(cpu.readLatch(0x90) == expected && ports->latch[1] == expected && cpu.IRAM[0x30] == input,
            "Real 8051 shifts/add match half-input plus half-delayed-output, including odd values and carry set");
        step(); // MOV 31h,A: expose the mixed output for inspection.
        check(cpu.IRAM[0x31] == expected, "Output diagnostic contains the mixed byte");
        history[position] = expected;
    }
    for (unsigned a = 0; a != data.size(); ++a)
        check(data[a] == (a >= base && a < base + size ? history[a - base] : 0xa5),
            "Repeated wraps leave all memory outside the echo buffer intact");
}
}
int main() {
    for (bool table : {false, true}) {
        for (unsigned size : {1u, 3u, 17u, 256u, 4096u, 32767u}) run(size, 0x8000, table);
        run(17, 0x80f8, table); // Pointer crosses a page at an unaligned buffer end.
    }
    std::puts("Echo initialization, impulse delay/decay, shifts, arbitrary wraps and memory bounds passed for both CPU dispatchers.");
}
