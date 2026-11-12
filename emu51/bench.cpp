// SPDX-License-Identifier: GPL-2.0-only
#include "cli_support.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace {
using BenchClock = std::chrono::steady_clock;
struct Result { double seconds; std::uint64_t cycles; std::uint64_t hash; };
Result run(const emu51_asm::AssemblyImage& image, std::uint64_t steps, bool table) {
    emu51 cpu(true);
    emu51_asm::apply_program_image(cpu, image);
    const auto start = BenchClock::now();
    std::uint64_t cycles = 0;
    // Keep dispatch selection outside the measured loop.
    if (table) {
        for (std::uint64_t i = 0; i < steps; ++i) cycles += static_cast<unsigned>(cpu.process_table());
    } else {
        for (std::uint64_t i = 0; i < steps; ++i) cycles += static_cast<unsigned>(cpu.process());
    }
    const auto end = BenchClock::now();
    return {std::chrono::duration<double>(end - start).count(), cycles, emu51_cli::hashState(cpu)};
}
} // namespace

int main(int argc, char** argv) {
    std::uint64_t steps = 5'000'000, repeats = 5;
    std::filesystem::path path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            std::puts("Usage: emu51_bench [--steps COUNT] [--repeats COUNT] [PROGRAM]\n"
                      "Compare switch and function-table dispatch using median elapsed time.\n"
                      "Default: deterministic synthetic workload, 5000000 instructions, 5 trials.\n"
                      "PROGRAM may be ASM, HEX or BIN (address 0); execution starts at PC=0.\n"
                      "Use a Release build; loading, setup and state hashing are outside timing.");
            return 0;
        }
        if ((arg == "--steps" || arg == "--repeats") && i + 1 < argc) {
            auto& count = arg == "--steps" ? steps : repeats;
            if (emu51_cli::parseCount(argv[++i], count, arg == "--steps" ? 10'000'000'000ull : 99)) continue;
        } else if (!arg.empty() && arg[0] != '-' && path.empty()) {
            path = argv[i];
            continue;
        }
        std::fprintf(stderr, "Invalid argument: %s. Use --help.\n", arg.c_str());
        return 2;
    }
    const auto program = emu51_cli::prepareProgram(path);
    if (!program.result.success) {
        std::fprintf(stderr, "%s\n", program.result.message.c_str());
        return 1;
    }
    std::printf("workload=%s instructions=%llu trials=%llu\n",
                path.empty() ? "synthetic" : path.string().c_str(),
                static_cast<unsigned long long>(steps), static_cast<unsigned long long>(repeats));
    // Warm each path on a fresh machine, outside the reported trials.
    const auto warmupSteps = std::min(steps, std::uint64_t{100'000});
    const auto warmSwitch = run(program.image, warmupSteps, false);
    const auto warmTable = run(program.image, warmupSteps, true);
    if (warmSwitch.hash != warmTable.hash || warmSwitch.cycles != warmTable.cycles) {
        std::fputs("Dispatch state/cycle mismatch during warmup\n", stderr);
        return 1;
    }
    std::vector<double> samples[2];
    std::optional<Result> reference;
    for (std::uint64_t trial = 0; trial < repeats; ++trial) {
        // Alternate ordering to reduce first/second-run bias.
        for (unsigned n = 0; n < 2; ++n) {
            const unsigned mode = static_cast<unsigned>((trial + n) % 2);
            const auto result = run(program.image, steps, mode != 0);
            if (reference && (reference->hash != result.hash || reference->cycles != result.cycles)) {
                std::fputs("Dispatch state/cycle mismatch\n", stderr);
                return 1;
            }
            reference = result;
            samples[mode].push_back(result.seconds);
        }
    }
    double medians[2]{};
    for (unsigned mode = 0; mode < 2; ++mode) {
        auto& times = samples[mode];
        std::sort(times.begin(), times.end());
        const auto middle = times.size() / 2;
        medians[mode] = times.size() % 2 ? times[middle] : (times[middle - 1] + times[middle]) / 2;
        const double duration = medians[mode];
        std::printf("%s: median=%.6fs %.3f MIPS %.3f Mcycles/s hash=%016llx cycles=%llu\n",
                    mode ? "table " : "switch", duration, duration > 0 ? static_cast<double>(steps) / duration / 1e6 : 0,
                    duration > 0 ? static_cast<double>(reference->cycles) / duration / 1e6 : 0,
                    static_cast<unsigned long long>(reference->hash),
                    static_cast<unsigned long long>(reference->cycles));
    }
    std::printf("table/switch speed ratio: %.3fx (workload/compiler dependent)\n",
                medians[1] > 0 ? medians[0] / medians[1] : 0);
    return 0;
}
