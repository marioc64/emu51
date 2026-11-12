// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

class emu51;

namespace emu51_asm {

enum class ProgramFileKind {
    assembly_source,
    intel_hex,
    raw_binary,
};

enum class OutputFormat {
    intel_hex,
    raw_binary,
};

enum class SymbolKind {
    label,
    equ,
    data,
    bit,
};

struct Diagnostic {
    std::string file;
    std::size_t line = 0;
    std::string message;
};

struct Symbol {
    std::uint16_t value = 0;
    SymbolKind kind = SymbolKind::label;
};

struct AssemblyImage {
    struct SourceLine {
        std::string file;
        std::size_t line = 0;
    };
    std::map<std::uint16_t, std::uint8_t> bytes;
    std::map<std::uint16_t, SourceLine> sourceLines;

    bool empty() const;
    std::uint16_t lowest_address() const;
    std::uint16_t highest_address() const;
    std::vector<std::uint8_t> to_binary() const;
    std::string to_intel_hex(std::size_t record_size = 16) const;
};

struct AssembleOptions {
    std::vector<std::filesystem::path> include_directories;
};

struct AssembleResult {
    AssemblyImage image;
    std::map<std::string, Symbol> symbols;
    std::vector<Diagnostic> diagnostics;

	bool success() const;
};

struct ProgramLoadResult {
	bool success = false;
	std::string message;
};

struct ProgramLoadOptions {
    std::uint16_t binary_load_address = 0;
};

// Preparation never touches the running machine. Callers may reset it only
// after a complete image has been read and validated successfully.
struct PreparedProgram {
    ProgramLoadResult result;
    AssemblyImage image;
};

class Assembler {
public:
    explicit Assembler(AssembleOptions options = {});

    AssembleResult assemble_file(const std::filesystem::path& source_path) const;
    AssembleResult assemble_string(
        const std::string& source_name,
        std::string_view source,
        const std::filesystem::path& base_directory = {}) const;

private:
    AssembleOptions options_;
};

ProgramFileKind detect_program_file_kind(const std::filesystem::path& path);
PreparedProgram prepare_program_file(
    const std::filesystem::path& path,
    const ProgramLoadOptions& options = {});
void apply_program_image(emu51& cpu, const AssemblyImage& image);
ProgramLoadResult load_program_file(
    emu51& cpu,
    const std::filesystem::path& path,
    const ProgramLoadOptions& options = {});
std::vector<std::string> opcode_mnemonics();

} // namespace emu51_asm
