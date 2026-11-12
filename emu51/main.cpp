// SPDX-License-Identifier: GPL-2.0-only
#include "cli_support.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    std::uint64_t steps = 1'000'000;
    std::filesystem::path path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            std::puts("Usage: emu51 [--steps COUNT] PROGRAM\n"
                      "Run ASM, Intel HEX, or BIN (loaded at address 0) at full speed.\n"
                      "Execution starts at PC=0 and stops after COUNT instructions (default 1000000).");
            return 0;
        }
        if (arg == "--steps" && i + 1 < argc) {
            if (emu51_cli::parseCount(argv[++i], steps, 10'000'000'000ull)) continue;
        } else if (!arg.empty() && arg[0] != '-' && path.empty()) {
            path = argv[i];
            continue;
        }
        std::fprintf(stderr, "Invalid argument: %s. Use --help.\n", arg.c_str());
        return 2;
    }
    if (path.empty()) {
        std::fputs("A program path is required. Use --help.\n", stderr);
        return 2;
    }
    const auto program = emu51_cli::prepareProgram(path);
    if (!program.result.success) {
        std::fprintf(stderr, "%s\n", program.result.message.c_str());
        return 1;
    }
    emu51 cpu(true);
    emu51_asm::apply_program_image(cpu, program.image);
    std::uint64_t cycles = 0;
    for (std::uint64_t i = 0; i < steps; ++i) cycles += static_cast<unsigned>(cpu.process());
    std::printf("instructions=%llu cycles=%llu PC=%04x SP=%02x hash=%016llx\n",
                static_cast<unsigned long long>(steps), static_cast<unsigned long long>(cycles),
                cpu.PC, cpu.SP, static_cast<unsigned long long>(emu51_cli::hashState(cpu)));
    return 0;
}
