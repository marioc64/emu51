// SPDX-License-Identifier: GPL-2.0-only
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "assembler.h"

namespace fs = std::filesystem;

namespace {

void print_usage() {
    std::cerr << "Usage: emu51_asm <input.asm> -o <output.{hex|bin}> [--format hex|bin] [-I <include_dir>]\n";
}

std::string to_lower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    fs::path input_path;
    fs::path output_path;
    emu51_asm::OutputFormat format = emu51_asm::OutputFormat::intel_hex;
    emu51_asm::AssembleOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value after -o\n";
                return 1;
            }
            output_path = argv[++i];
            continue;
        }
        if (arg == "--format") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value after --format\n";
                return 1;
            }
            const std::string format_name = to_lower(argv[++i]);
            if (format_name == "hex" || format_name == "ihex" || format_name == "intelhex") {
                format = emu51_asm::OutputFormat::intel_hex;
            } else if (format_name == "bin" || format_name == "binary") {
                format = emu51_asm::OutputFormat::raw_binary;
            } else {
                std::cerr << "Unsupported format: " << format_name << '\n';
                return 1;
            }
            continue;
        }
        if (arg == "-I") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value after -I\n";
                return 1;
            }
            options.include_directories.emplace_back(argv[++i]);
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << '\n';
            return 1;
        }
        if (input_path.empty()) {
            input_path = arg;
        } else {
            std::cerr << "Unexpected positional argument: " << arg << '\n';
            return 1;
        }
    }

    if (input_path.empty() || output_path.empty()) {
        print_usage();
        return 1;
    }

    if (output_path.has_extension() && format == emu51_asm::OutputFormat::intel_hex) {
        const std::string ext = to_lower(output_path.extension().string());
        if (ext == ".bin") {
            format = emu51_asm::OutputFormat::raw_binary;
        }
    }

    emu51_asm::Assembler assembler(options);
    emu51_asm::AssembleResult result = assembler.assemble_file(input_path);
    if (!result.success()) {
        for (const auto& diagnostic : result.diagnostics) {
            if (diagnostic.line != 0) {
                std::cerr << diagnostic.file << ':' << diagnostic.line << ": " << diagnostic.message << '\n';
            } else {
                std::cerr << diagnostic.file << ": " << diagnostic.message << '\n';
            }
        }
        return 1;
    }

    std::ofstream output(output_path, format == emu51_asm::OutputFormat::raw_binary ? std::ios::binary : std::ios::out);
    if (!output) {
        std::cerr << "Unable to open output file: " << output_path << '\n';
        return 1;
    }

    if (format == emu51_asm::OutputFormat::intel_hex) {
        output << result.image.to_intel_hex();
    } else {
        const std::vector<std::uint8_t> binary = result.image.to_binary();
        output.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
    }

    return 0;
}
