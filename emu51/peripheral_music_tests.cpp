// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_peripheral_session.h"
#include "emu51_peripheral_firmware.h"
#include "assembler.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace {
void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
unsigned word(const emu51& cpu, unsigned address) { return cpu.IRAM[address] + 256u * cpu.IRAM[address + 1]; }
unsigned stepFor(unsigned pitch) {
    return static_cast<unsigned>(std::lround(440 * std::pow(2.0, (static_cast<int>(pitch) - 69) / 12.0) * 65536 / 8000));
}
void run(bool table, bool invalidPitch = false) {
    std::string source = emu51CovoxDuetFirmware;
    if (invalidPitch) {
        const std::string firstRow = "db 25,60,1,52,1";
        const auto pos = source.find(firstRow);
        check(pos != std::string::npos, "First score row is present");
        source.replace(pos, firstRow.size(), "db 1,47,1,84,1");
        // Exercise 16-bit code-memory record pointers across a page boundary.
        const auto song = source.find("\nsong:\n");
        source.insert(song + 1, "org 0FFCh\n");
    }
    const auto assembled = emu51_asm::Assembler{}.assemble_string("covox_duet.asm", source);
    if (!assembled.success()) for (const auto& d : assembled.diagnostics) std::fprintf(stderr, "%s\n", d.message.c_str());
    check(assembled.success(), "Covox duet assembles");
    const auto sym = [&](const char* name) { return assembled.image.symbols.at(name).value; };
    auto config = emu51PeripheralPreset(4);
    check(config.devices.size() == 2 && config.devices[1].audioDirections == E51P_AUDIO_PLAYBACK, "Duet has only Covox and playback");
    auto graph = Emu51PeripheralSession::create(config, 24.576);
    emu51 cpu(true); cpu.setPortIo(graph->gpio());
    emu51_asm::apply_program_image(cpu, assembled.image);
    const auto execute = [&] { if (table) cpu.process_table(); else cpu.process(); };
    unsigned init = 0;
    while (cpu.PC != sym("IDLE") && init++ < 10000) execute();
    check(cpu.PC == sym("IDLE") && cpu.IRAM[0x30] == 1 && cpu.readLatch(0x90) == 128,
        "Player starts at centered silence with timers configured");
    check(cpu.TMOD == 0x12 && cpu.IE == 0x8a && cpu.IP == 2 && !graph->audioOpen(),
        "Independent T0/T1 interrupt sources and priorities require no native devices");
    cpu.Acc = 0x5a; cpu.PSW = 0x80; cpu.B = 0xa5; cpu.DPTR(0xbeef);
    for (unsigned i = 0; i < 8; ++i) cpu.IRAM[i] = static_cast<std::uint8_t>(0x90 + i);
    for (unsigned i = 0x54; i < 0x60; ++i) cpu.IRAM[i] = 0xa5;
    for (unsigned i = 0x70; i < 0x80; ++i) cpu.IRAM[i] = 0xa5;
    std::uint64_t outputs = 0, firstSampleTick = 0, lastSampleTick = 0, musicTicks = 0;
    unsigned expectedPhase1 = 0, expectedPhase2 = 0, expectedOutput = 128, records = 0, stackMax = 0;
    bool nested = false, silent = false, solo1 = false, solo2 = false;
    std::set<unsigned> mixed;
    const unsigned rows = (sym("SONG_END") - sym("SONG")) / 5;
    unsigned steps = 0, nextRecordTick = 1;
    do {
        const auto pc = cpu.PC;
        stackMax = std::max(stackMax, unsigned(cpu.SP));
        if (pc == sym("AUDIO_ISR")) {
            nested |= cpu.SP > 0x61;
            const unsigned currentRow = cpu.IRAM[0x33];
            for (unsigned voice = 0; voice < 2; ++voice) {
                const unsigned base = voice ? 0x40 : 0x38;
                const auto data = sym("SONG") + (currentRow ? currentRow - 1 : 0) * 5;
                const unsigned pitch = currentRow ? cpu.readProg(static_cast<std::uint16_t>(data + 1 + 2 * voice)) : 0;
                const bool valid = pitch >= 48 && pitch <= 83;
                const unsigned gate = valid ? cpu.readProg(static_cast<std::uint16_t>(data + 2 + 2 * voice)) : 0;
                check(cpu.IRAM[base + 4] == pitch && cpu.IRAM[base + 5] == gate &&
                    word(cpu, base + 2) == (valid ? stepFor(pitch) : 0),
                    "Audio preemption observes a complete old or new chord, never a partially published note");
            }
            if (lastSampleTick) {
                const auto interval = graph->ticks() - lastSampleTick;
                check(interval > 190 * 12 && interval < 322 * 12, "Audio ISR meets every 8 kHz deadline, including note changes");
            } else firstSampleTick = graph->ticks();
            lastSampleTick = graph->ticks();
            expectedPhase1 = (word(cpu, 0x38) + word(cpu, 0x3a)) & 65535;
            expectedPhase2 = (word(cpu, 0x40) + word(cpu, 0x42)) & 65535;
            const int a = cpu.IRAM[0x3d] ? (expectedPhase1 & 32768 ? -63 : 63) : 0;
            const int b = cpu.IRAM[0x45] ? (expectedPhase2 & 32768 ? -63 : 63) : 0;
            expectedOutput = static_cast<unsigned>(128 + a + b);
            silent |= !cpu.IRAM[0x3d] && !cpu.IRAM[0x45];
            solo1 |= cpu.IRAM[0x3d] && !cpu.IRAM[0x45];
            solo2 |= !cpu.IRAM[0x3d] && cpu.IRAM[0x45];
        }
        if (pc == sym("MUSIC_ISR")) ++musicTicks;
        if (pc == sym("NOTES_PUBLISHED")) {
            ++records;
            const auto row = cpu.IRAM[0x33];
            check(row >= 1 && row <= rows && word(cpu, 0x48) == sym("SONG") + 5u * row,
                "Player status exposes current row and next code-memory record");
            const auto data = sym("SONG") + (row - 1) * 5;
            check(musicTicks == nextRecordTick, "Each score record holds for exactly its duration in music interrupts");
            nextRecordTick += cpu.readProg(data);
            check(cpu.IRAM[0x4a] == cpu.readProg(data), "Record duration is stored in internal RAM");
            for (unsigned voice = 0; voice < 2; ++voice) {
                const unsigned pitch = cpu.readProg(static_cast<std::uint16_t>(data + 1 + 2 * voice));
                const unsigned gate = cpu.readProg(static_cast<std::uint16_t>(data + 2 + 2 * voice));
                const bool valid = pitch >= 48 && pitch <= 83;
                const unsigned base = voice ? 0x40 : 0x38;
                check(cpu.IRAM[base + 4] == pitch && cpu.IRAM[base + 5] == (valid ? gate : 0) &&
                    word(cpu, base + 2) == (valid ? stepFor(pitch) : 0) && word(cpu, base) == 0,
                    "Music interrupt atomically publishes both pitches, gates and 16-bit DDS steps");
            }
        }
        execute();
        check(graph->status() == E51P_OK, "Duet GPIO graph stays healthy");
        if (pc == sym("COVOX_WRITE")) {
            double volts = 0;
            check(word(cpu, 0x38) == expectedPhase1 && word(cpu, 0x40) == expectedPhase2 &&
                cpu.readLatch(0x90) == expectedOutput && graph->sampleAnalog("covox.out", volts) && volts == 5.0 * expectedOutput / 256,
                "Every real timer-driven DDS/mix sample matches the independent oracle and reaches Covox");
            ++outputs; mixed.insert(expectedOutput);
        }
        if (cpu.PC == sym("IDLE")) {
            check(cpu.Acc == 0x5a && cpu.PSW == 0x80 && cpu.B == 0xa5 && cpu.DPTR() == 0xbeef && cpu.SP == 0x5f,
                "Nested timer ISRs restore foreground registers, flags, DPTR and stack");
            for (unsigned i = 0; i < 8; ++i) check(cpu.IRAM[i] == 0x90 + i, "Private ISR banks preserve main bank");
            check(word(cpu, 0x35) == (outputs & 65535) && word(cpu, 0x31) == (musicTicks & 65535) &&
                cpu.IRAM[0x34] == cpu.readLatch(0x90), "IRAM sample/music counters and last output agree with execution");
        }
        check(++steps < 15000000, "Song advances and loops within bounded execution");
    } while (!(cpu.IRAM[0x37] && cpu.IRAM[0x33] == 1 && cpu.PC == sym("IDLE")));
    check(records == rows + 1 && nested && silent && solo1 && solo2 && mixed == std::set<unsigned>({2, 65, 128, 191, 254}),
        "Both gates, each solo voice, full mix, song loop and real interrupt preemption are exercised");
    const double sampleHz = (outputs - 1) * 24576000.0 / (lastSampleTick - firstSampleTick);
    const double musicHz = musicTicks * 24576000.0 / graph->ticks();
    check(std::abs(sampleHz - 8000) < 1 && musicHz > 99 && musicHz < 101, "Independent timers sustain 8 kHz audio and nominal 100 Hz music");
    check(stackMax < 0x70, "Nested interrupt stack stays within reserved internal RAM");
    for (unsigned i = 0x54; i < 0x60; ++i) check(cpu.IRAM[i] == 0xa5, "Player state does not overwrite stack guard");
    for (unsigned i = 0x70; i < 0x80; ++i) check(cpu.IRAM[i] == 0xa5, "Nested ISR stack does not overwrite high RAM guard");
    std::printf("%s%s: %llu samples, %.2f Hz audio / %.2f Hz music, %u rows, stack peak %02X\n",
        table ? "table" : "switch", invalidPitch ? " / invalid pitches + page crossing" : "",
        static_cast<unsigned long long>(outputs), sampleHz, musicHz, records, stackMax);
}
}
int main() {
    for (bool table : {false, true}) { run(table); run(table, true); }
}
