// SPDX-License-Identifier: GPL-2.0-only
#include "cli_support.h"
#include "emu51_project_build.h"
#include "emu51_peripheral_session.h"

#include <cstdio>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    std::uint64_t steps = 1'000'000;
    Emu51Model model = Emu51Model::i8051;
    bool modelSpecified = false;
    std::filesystem::path path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--model" && i + 1 < argc) {
            const auto parsed = emu51ParseModel(argv[++i]);
            if (!parsed) { std::fputs("Unsupported CPU model. Use 8051 or 8052.\n", stderr); return 2; }
            model = *parsed;
            modelSpecified = true;
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            std::puts("Usage: emu51cli [--model 8051|8052] [--steps COUNT] PROGRAM\n"
                      "Run ASM, Intel HEX, BIN (loaded at address 0), or an emu51 project at full speed.\n"
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
    std::unique_ptr<Emu51PeripheralSession> session;
    emu51 cpu(true);
    bool executing = false;
    try {
        auto extension = path.extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".emu51") {
            std::string error;
            auto project = emu51LoadProject(path,error);
            if (!project) throw std::runtime_error(error);
            if (!project->hardwareModule.empty()) throw std::runtime_error("The console runner cannot load a local hardware module");
            if (!emu51MigrateProject(*project,error)) throw std::runtime_error(error);
            const auto build = emu51BuildProject(*project);
            if (!build.success()) throw std::runtime_error(build.error);
            session = Emu51PeripheralSession::create(project->peripherals,project->execution.crystalMHz);
            if (!emu51ProgramProject(build,*session,error)) throw std::runtime_error(error);
            cpu.setModel(modelSpecified ? model : project->cpuModel);
            cpu.setAddressSpace(std::make_shared<functional_address_space>(
                [&](uint16_t a) { uint8_t v = 255; session->readData(a,v,!executing); return v; },
                [&](uint16_t a, uint8_t v) { session->writeData(a,v); },
                [&](uint16_t a) { uint8_t v = 255; session->readProgram(a,v,!executing); return v; },
                [&](uint16_t a, uint8_t v) { session->programLogical(true,a,v); }));
            cpu.setPortIo(session->gpio()); cpu.init();
        } else {
            const auto program = emu51_cli::prepareProgram(path);
            if (!program.result.success) throw std::runtime_error(program.result.message);
            cpu.setModel(model);
            emu51_asm::apply_program_image(cpu, program.image);
        }
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
    std::uint64_t cycles = 0;
    for (std::uint64_t i = 0; i < steps; ++i) {
        executing = true; cycles += static_cast<unsigned>(cpu.process()); executing = false;
        if (session && session->status() != E51P_OK && session->status() != E51P_CONTENTION) {
            std::fprintf(stderr,"Bus fault: %s (status %u)\n",session->busError().c_str(),session->status()); return 1;
        }
    }
    std::printf("instructions=%llu cycles=%llu PC=%04x SP=%02x A=%02x hash=%016llx\n",
                static_cast<unsigned long long>(steps), static_cast<unsigned long long>(cycles),
                cpu.PC, cpu.SP, cpu.Acc, static_cast<unsigned long long>(emu51_cli::hashState(cpu)));
    return 0;
}
