// SPDX-License-Identifier: GPL-2.0-only
#include "assembler.h"

#include "emu51.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>
#include <string>
#include <vector>

namespace emu51_asm {

namespace {

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

PreparedProgram assemble_program(const std::filesystem::path& path) {
    Assembler assembler;
    AssembleResult result = assembler.assemble_file(path);
    if (!result.success()) {
        ProgramLoadResult load_result;
        if (!result.diagnostics.empty()) {
            const Diagnostic& diagnostic = result.diagnostics.front();
            if (diagnostic.line != 0) {
                load_result.message =
                    diagnostic.file + ":" + std::to_string(diagnostic.line) + ": " + diagnostic.message;
            } else {
                load_result.message = diagnostic.file + ": " + diagnostic.message;
            }
        } else {
            load_result.message = "failed to assemble program";
        }
        return {std::move(load_result), {}};
    }

    return {{true, "assembly source loaded"}, std::move(result.image)};
}

PreparedProgram read_binary_program(
    const std::filesystem::path& path,
    std::uint16_t load_address) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {{false, "failed to open binary program"}, {}};
    }

    // Read at most one byte beyond the address space, even for huge files.
    std::vector<char> bytes(0x10001u);
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(input.gcount()));
    if (input.bad() || (input.fail() && !input.eof()))
        return {{false, "failed to read binary program"}, {}};
    if (bytes.size() > 0x10000u) {
        return {{false, "binary file is larger than 64KB"}, {}};
    }
    if (static_cast<std::size_t>(load_address) + bytes.size() > 0x10000u) {
        return {{false, "binary program does not fit in memory at the selected address"}, {}};
    }

    AssemblyImage image;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        image.bytes.emplace(
            static_cast<std::uint16_t>(load_address + i),
            static_cast<std::uint8_t>(static_cast<unsigned char>(bytes[i])));
    }
    char message[64];
    std::snprintf(message, sizeof(message), "binary program loaded at %04X", load_address);
    return {{true, message}, std::move(image)};
}

} // namespace

ProgramFileKind detect_program_file_kind(const std::filesystem::path& path) {
    const std::string ext = to_lower_copy(path.extension().string());
    if (ext == ".asm" || ext == ".s03") {
        return ProgramFileKind::assembly_source;
    }
    if (ext == ".hex" || ext == ".ihx" || ext == ".ihex") {
        return ProgramFileKind::intel_hex;
    }
    return ProgramFileKind::raw_binary;
}

PreparedProgram prepare_program_file(
    const std::filesystem::path& path,
    const ProgramLoadOptions& options) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
        return {{false, "unable to open program file"}, {}};
    switch (detect_program_file_kind(path)) {
    case ProgramFileKind::assembly_source:
        return assemble_program(path);
    case ProgramFileKind::intel_hex: {
        PreparedProgram prepared;
        // Reuse the core's validated HEX parser, collecting writes in an image
        // instead of invoking the live machine's memory/peripheral callbacks.
        auto space = std::make_shared<functional_address_space>(
            [](std::uint16_t) { return std::uint8_t{0}; },
            [&](std::uint16_t address, std::uint8_t value) { prepared.image.bytes[address] = value; });
        emu51 parser(space);
        if (parser.loadHex(path.string().c_str()) == 0)
            prepared.result = {true, "Intel HEX program loaded"};
        else
            prepared.result = {false, "failed to load Intel HEX program"};
        return prepared;
    }
    case ProgramFileKind::raw_binary:
    default:
        return read_binary_program(path, options.binary_load_address);
    }
}

void apply_program_image(emu51& cpu, const AssemblyImage& image) {
    for (const auto& [address, value] : image.bytes)
        cpu.writeProg(address, value);
}

ProgramLoadResult load_program_file(
    emu51& cpu,
    const std::filesystem::path& path,
    const ProgramLoadOptions& options) {
    PreparedProgram prepared = prepare_program_file(path, options);
    if (prepared.result.success)
        apply_program_image(cpu, prepared.image);
    return prepared.result;
}

} // namespace emu51_asm
