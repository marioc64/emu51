// SPDX-License-Identifier: GPL-2.0-only
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
constexpr double crystalMHz = 3;
constexpr std::uint64_t tickHz = 3000000;
void require(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }
void usage() {
    std::puts("Usage: emu51_microphone_covox --run [--seconds 5] [--capture default|index] [--repeat 1]\n"
        "       emu51_microphone_covox --list\n"
        "       emu51_microphone_covox --null [--seconds 0.15]\n"
        "       emu51_microphone_covox --write-project path.emu51\n"
        "Microphone -> ADC0804 -> P0 -> circular echo -> P1 -> Covox -> default output.\n"
        "Native audio opens only with --run. Covox playback uses full scale: (byte - 128) / 128.");
}
}
int main(int argc, char** argv) {
    try {
        bool run = false, null = false, list = false;
        double seconds = 5;
        int captureDevice = -1;
        unsigned repeats = 1;
        std::filesystem::path projectPath;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") { usage(); return 0; }
            if (arg == "--run") { run = true; continue; }
            if (arg == "--null") { null = run = true; continue; }
            if (arg == "--list") { list = true; continue; }
            require(i + 1 < argc, "Option needs a value: " + arg);
            const std::string value = argv[++i];
            if (arg == "--write-project") projectPath = value;
            else if (arg == "--seconds") {
                std::size_t used = 0; seconds = std::stod(value, &used);
                require(used == value.size() && std::isfinite(seconds) && seconds >= 0.1 && seconds <= 3600,
                    "Seconds must be between 0.1 and 3600");
            } else if (arg == "--repeat") {
                require(!value.empty() && value.find_first_not_of("0123456789") == std::string::npos, "Invalid repeat count");
                const auto count = std::stoull(value);
                require(count >= 1 && count <= 20, "Repeat count must be between 1 and 20");
                repeats = static_cast<unsigned>(count);
            } else if (arg == "--capture") {
                if (value == "default") captureDevice = -1;
                else {
                    require(!value.empty() && value.find_first_not_of("0123456789") == std::string::npos, "Invalid capture index");
                    const auto index = std::stoull(value);
                    require(index <= 2147483647, "Capture index out of range"); captureDevice = static_cast<int>(index);
                }
            } else throw std::runtime_error("Unknown option: " + arg);
        }
        require(!(run && list) && (projectPath.empty() || (!run && !list)), "Choose --run, --list, or --write-project");
        if (!projectPath.empty()) {
            projectPath = std::filesystem::absolute(projectPath);
            Emu51Project project;
            project.peripherals = emu51PeripheralPreset(3);
            project.execution = {true, crystalMHz, 500};
            project.programs.push_back({(projectPath.parent_path() / "microphone_covox.asm").string(), 0, true, emu51MicrophoneCovoxFirmware});
            project.editor = {true, true, true, "", "microphone_covox.asm", projectPath.parent_path().string(), emu51MicrophoneCovoxFirmware, 0, 0};
            std::string error; require(emu51SaveProject(projectPath, project, error), error);
            std::printf("Saved %s (paused; audio devices remain closed).\n", projectPath.string().c_str()); return 0;
        }
        if (!run && !list) { usage(); return argc == 1 ? 0 : 1; }
        auto graph = Emu51PeripheralSession::create(emu51PeripheralPreset(3), crystalMHz);
        std::vector<std::string> capture, playback;
        std::string error;
        require(graph->enumerateAudio(capture, playback, error, null), error);
        if (list) {
            for (std::size_t i = 0; i < capture.size(); ++i) std::printf("capture %zu: %s\n", i, capture[i].c_str());
            for (std::size_t i = 0; i < playback.size(); ++i) std::printf("playback %zu: %s\n", i, playback[i].c_str());
            return 0;
        }
        emu51 cpu(true);
        cpu.setPortIo(graph->gpio());
        const auto assembled = emu51_asm::Assembler{}.assemble_string("microphone_covox.asm", emu51MicrophoneCovoxFirmware);
        require(assembled.success(), assembled.diagnostics.empty() ? "Cannot assemble microphone firmware" : assembled.diagnostics.front().message);
        emu51_asm::apply_program_image(cpu, assembled.image);
        const auto echoBase = assembled.image.symbols.at("ECHO_BASE").value;
        const auto echoSize = assembled.image.symbols.at("BUFFER_SIZE").value;
        require(echoSize && unsigned(echoBase) + echoSize <= 65535, "Invalid echo buffer bounds");
        // Initialize the delay line with audio still paused. Native run duration
        // and diagnostics describe actual conversions, not buffer preparation.
        unsigned initializationSteps = 0;
        while (!cpu.IRAM[0x32] && initializationSteps++ < 1000000) cpu.process();
        require(cpu.IRAM[0x32] == 1, "Echo buffer initialization did not complete");
        for (unsigned i = 0; i < echoSize; ++i)
            require(cpu.readData(static_cast<std::uint16_t>(echoBase + i)) == 128, "Echo buffer must start at silence");
        std::printf("Echo: %u samples at XDATA %04X; mix=(input >> 1)+(delayed >> 1).\n", unsigned(echoSize), unsigned(echoBase));
        std::printf("%s: microphone -> ADC -> P0 -> echo -> P1 -> Covox -> default output (%.2f seconds)\n",
            null ? "NULL" : "Native", seconds); std::fflush(stdout);
        require(graph->openAudio(captureDevice, -1, 0, -1, error), error);
        // Keep verification state across pause/resume in the middle of a mix.
        unsigned expectedEcho = 128;
        std::uint16_t echoAddress = echoBase;
        bool haveDelayedSample = false;
        for (unsigned pass = 1; pass <= repeats; ++pass) {
            e51p_audio_stats before{};
            require(graph->audioStats(before), "Cannot read audio counters");
            require(graph->syncAudio(true, error), error);
            const auto begin = std::chrono::steady_clock::now();
            const auto firstTick = graph->ticks();
            const auto limit = firstTick + static_cast<std::uint64_t>(seconds * tickHz);
            std::uint64_t conversions = 0;
            unsigned minimum = 255, maximum = 0;
            while (graph->ticks() < limit) {
                const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
                const auto target = std::min(limit, firstTick + static_cast<std::uint64_t>(elapsed * tickHz));
                if (graph->ticks() >= target) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
                while (graph->ticks() < target) {
                    const auto pc = cpu.PC;
                    const auto opcode = cpu.readProg(pc);
                    const bool copy = opcode == 0xf5 && cpu.readProg(static_cast<std::uint16_t>(pc + 1)) == 0x90;
                    const auto address = cpu.DPTR();
                    cpu.process();
                    if (opcode == 0xe0) { // MOVX A,@DPTR: the old byte in this delay slot.
                        require(address >= echoBase && unsigned(address) < unsigned(echoBase) + echoSize, "Echo read escaped its buffer");
                        expectedEcho = (cpu.IRAM[0x30] >> 1) + (cpu.Acc >> 1);
                        echoAddress = address;
                        haveDelayedSample = true;
                    }
                    require(graph->status() == E51P_OK, "Peripheral error: " + std::to_string(graph->status()));
                    if (copy) {
                        const auto value = cpu.readLatch(0x90);
                        e51p_digital_state input{}; double output = 0;
                        require(graph->sampleDigital("cpu.P0", input) && !input.floating && !input.contention && input.value == cpu.IRAM[0x30],
                            "P0 must supply the dry ADC sample");
                        require(haveDelayedSample && value == expectedEcho && cpu.readData(echoAddress) == value,
                            "Covox and the same delay slot must receive (input >> 1) + (old slot >> 1)");
                        haveDelayedSample = false;
                        require(graph->sampleAnalog("covox.out", output) && output == 5.0 * value / 256.0,
                            "Covox must follow the byte written by firmware to P1");
                        minimum = std::min(minimum, unsigned(value)); maximum = std::max(maximum, unsigned(value)); ++conversions;
                    }
                }
            }
            const auto wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
            require(graph->syncAudio(false, error), error);
            e51p_audio_stats stats{};
            require(graph->audioStats(stats) && stats.status == E51P_OK && conversions && stats.captured_frames > before.captured_frames &&
                stats.capture_used > before.capture_used && stats.playback_used > before.playback_used,
                "Host audio did not deliver capture/playback samples");
            std::printf("Run %u/%u: ADC -> P0 -> echo -> P1 -> Covox: %llu conversions, mixed range %02X..%02X, %.1f%% real-time\n"
                "Capture frames=%llu used=%llu; playback used=%llu; misses=%llu/%llu; drops=%llu/%llu\n",
                pass, repeats, static_cast<unsigned long long>(conversions), minimum, maximum, 100 * seconds / wall,
                static_cast<unsigned long long>(stats.captured_frames - before.captured_frames), static_cast<unsigned long long>(stats.capture_used - before.capture_used),
                static_cast<unsigned long long>(stats.playback_used - before.playback_used), static_cast<unsigned long long>(stats.capture_misses - before.capture_misses),
                static_cast<unsigned long long>(stats.playback_misses - before.playback_misses), static_cast<unsigned long long>(stats.capture_dropped - before.capture_dropped),
                static_cast<unsigned long long>(stats.playback_dropped - before.playback_dropped));
            std::fflush(stdout);
            if (pass < repeats) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
