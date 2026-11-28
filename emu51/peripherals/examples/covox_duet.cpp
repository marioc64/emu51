// SPDX-License-Identifier: GPL-2.0-only
// Real 8051 timer interrupts synthesize two voices; the host only paces time.
#include "emu51_peripheral_session.h"
#include "emu51_peripheral_firmware.h"
#include "emu51_project.h"
#include "assembler.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
constexpr double crystalMHz = 24.576;
constexpr std::uint64_t tickHz = 24576000;
void check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
void usage() {
    std::puts("Usage: emu51_covox_duet --run [--seconds 8]\n"
        "       emu51_covox_duet --null [--seconds 0.2]\n"
        "       emu51_covox_duet --write-project path.emu51\n"
        "T0: 8 kHz two-voice oscillator/mixer -> P1 -> Covox -> default output.\n"
        "T1: pitch/gate sequencer; status in IRAM 30h..53h. Playback only.\n"
        "Native audio opens only with --run. Use Cycle exact at 24.576 MHz in the GUI.");
}
}
int main(int argc, char** argv) {
    try {
        bool run = false, null = false;
        double seconds = 8;
        std::filesystem::path projectPath;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") { usage(); return 0; }
            if (arg == "--run") { run = true; continue; }
            if (arg == "--null") { null = run = true; continue; }
            check(i + 1 < argc, "Option needs a value: " + arg);
            const std::string value = argv[++i];
            if (arg == "--write-project") projectPath = value;
            else if (arg == "--seconds") {
                std::size_t used = 0; seconds = std::stod(value, &used);
                check(used == value.size() && std::isfinite(seconds) && seconds >= 0.1 && seconds <= 3600,
                    "Seconds must be between 0.1 and 3600");
            } else throw std::runtime_error("Unknown option: " + arg);
        }
        check(projectPath.empty() || !run, "Choose --run or --write-project");
        if (!projectPath.empty()) {
            projectPath = std::filesystem::absolute(projectPath);
            Emu51Project project;
            project.peripherals = emu51PeripheralPreset(4);
            project.execution = {true, crystalMHz, 500};
            project.programs.push_back({(projectPath.parent_path() / "covox_duet.asm").string(), 0, true, emu51CovoxDuetFirmware});
            project.editor = {true, true, true, "", "covox_duet.asm", projectPath.parent_path().string(), emu51CovoxDuetFirmware, 0, 0};
            std::string error; check(emu51SaveProject(projectPath, project, error), error);
            std::printf("Saved %s (paused; playback device remains closed).\n", projectPath.string().c_str()); return 0;
        }
        if (!run) { usage(); return argc == 1 ? 0 : 1; }
        auto graph = Emu51PeripheralSession::create(emu51PeripheralPreset(4), crystalMHz);
        emu51 cpu(true); cpu.setPortIo(graph->gpio());
        const auto assembled = emu51_asm::Assembler{}.assemble_string("covox_duet.asm", emu51CovoxDuetFirmware);
        if (!assembled.success()) for (const auto& d : assembled.diagnostics) std::fprintf(stderr, "%s\n", d.message.c_str());
        check(assembled.success(), "Cannot assemble Covox duet firmware");
        emu51_asm::apply_program_image(cpu, assembled.image);
        // Prepare the program with playback still paused, before opening hardware.
        const auto idle = assembled.image.symbols.at("IDLE").value;
        unsigned init = 0;
        while (cpu.PC != idle && init++ < 10000) cpu.process();
        check(cpu.PC == idle && cpu.IRAM[0x30] == 1, "Music player initialization failed");
        std::vector<std::string> capture, playback; std::string error;
        check(graph->enumerateAudio(capture, playback, error, null), error);
        check(graph->openAudio(-1, -1, 0, -1, error), error);
        check(graph->syncAudio(true, error), error);
        const auto first = graph->ticks(), limit = first + static_cast<std::uint64_t>(seconds * tickHz);
        const auto begin = std::chrono::steady_clock::now();
        std::uint64_t samples = 0;
        unsigned minimum = 255, maximum = 0;
        const auto outputPc = assembled.image.symbols.at("COVOX_WRITE").value;
        while (graph->ticks() < limit) {
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
            const auto target = std::min(limit, first + static_cast<std::uint64_t>(elapsed * tickHz));
            if (graph->ticks() >= target) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
            while (graph->ticks() < target) {
                const auto pc = cpu.PC;
                const bool write = pc == outputPc;
                cpu.process();
                check(graph->status() == E51P_OK, "Peripheral error");
                if (write) {
                    const unsigned value = cpu.readLatch(0x90); double volts = 0;
                    check(graph->sampleAnalog("covox.out", volts) && volts == 5.0 * value / 256.0, "Covox must follow P1");
                    minimum = std::min(minimum, value); maximum = std::max(maximum, value); ++samples;
                }
            }
        }
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        // A short emulation can finish before the host's initial playback
        // latency has elapsed. Let the callback consume the queued audio.
        e51p_audio_stats stats{};
        const auto playbackDeadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(static_cast<int>(graph->audioLatencyMs()) + 1000);
        do {
            check(graph->audioStats(stats) && stats.status == E51P_OK, "Cannot read playback statistics");
            if (stats.playback_used) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < playbackDeadline);
        check(graph->syncAudio(false, error), error);
        check(graph->audioStats(stats) && stats.status == E51P_OK && stats.playback_used && !stats.captured_frames && samples,
            "Playback-only host did not deliver generated audio (queued " + std::to_string(stats.playback_queued) +
            ", missed " + std::to_string(stats.playback_misses) + " frames)");
        std::printf("%s: %llu samples (%.1f Hz), Covox %02X..%02X, %.1f%% real-time\n"
            "IRAM: row=%u ticks=%u pitch/gate=%u/%u + %u/%u loops=%u\n"
            "Playback used=%llu misses=%llu dropped=%llu; capture frames=%llu\n",
            null ? "NULL" : "Native", static_cast<unsigned long long>(samples), samples / seconds, minimum, maximum, 100 * seconds / wall,
            cpu.IRAM[0x33], cpu.IRAM[0x31] + 256u * cpu.IRAM[0x32], cpu.IRAM[0x3c], cpu.IRAM[0x3d], cpu.IRAM[0x44], cpu.IRAM[0x45], cpu.IRAM[0x37],
            static_cast<unsigned long long>(stats.playback_used), static_cast<unsigned long long>(stats.playback_misses),
            static_cast<unsigned long long>(stats.playback_dropped), static_cast<unsigned long long>(stats.captured_frames));
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
