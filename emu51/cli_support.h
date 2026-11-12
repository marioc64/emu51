// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "assembler.h"
#include "emu51.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace emu51_cli {

inline bool parseCount(std::string_view text, std::uint64_t& value, std::uint64_t maximum) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() &&
           value > 0 && value <= maximum;
}

inline emu51_asm::PreparedProgram prepareProgram(const std::filesystem::path& path) {
    if (!path.empty()) return emu51_asm::prepare_program_file(path);
    // Deterministic synthetic workload, independent of local sample programs.
    // Arithmetic, internal/external memory, DPTR, register loops and jumps.
    constexpr std::uint8_t bytes[] = {
        0x75, 0x81, 0x30, 0x90, 0x80, 0x00, 0x78, 0x40, 0x7f, 0x80, 0x74, 0x01,
        0x04, 0x24, 0x03, 0x64, 0x55, 0x23, 0xf6, 0xf0, 0xa3, 0xdf, 0xf5, 0x02, 0, 0
    };
    emu51_asm::PreparedProgram program;
    program.result.success = true;
    for (std::size_t i = 0; i < sizeof(bytes); ++i)
        program.image.bytes.emplace(static_cast<std::uint16_t>(i), bytes[i]);
    return program;
}

inline std::uint64_t hashState(emu51& cpu) {
    std::uint64_t hash = 14695981039346656037ull;
    const auto mix = [&hash](std::uint8_t value) { hash = (hash ^ value) * 1099511628211ull; };
    for (const auto value : cpu.SFR) mix(value);
    for (unsigned i = 0; i < 65536; ++i) {
        mix(cpu.readData(static_cast<std::uint16_t>(i)));
        mix(cpu.readProg(static_cast<std::uint16_t>(i)));
    }
    mix(static_cast<std::uint8_t>(cpu.PC));
    mix(static_cast<std::uint8_t>(cpu.PC >> 8));
    return hash;
}

} // namespace emu51_cli
