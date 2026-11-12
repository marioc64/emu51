// SPDX-License-Identifier: GPL-2.0-only
#include "assembler_resources.h"

#include <cstdint>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifndef EMU51_INSTALLED_ASM_FROM_BINDIR
#define EMU51_INSTALLED_ASM_FROM_BINDIR "../share/emu51/asm"
#endif

namespace emu51_asm::detail {
namespace {
namespace fs = std::filesystem;

fs::path executable_path()
{
#if defined(__APPLE__)
    std::vector<char> buffer(1024);
    auto size = static_cast<std::uint32_t>(buffer.size());
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        buffer.resize(size);
        if (_NSGetExecutablePath(buffer.data(), &size) != 0)
            return {};
    }
    return fs::path(buffer.data());
#elif defined(_WIN32)
    std::vector<wchar_t> buffer(512);
    while (buffer.size() <= 32768) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return {};
        if (length < buffer.size())
            return fs::path(std::wstring(buffer.data(), length));
        buffer.resize(buffer.size() * 2);
    }
    return {};
#else
    std::error_code error;
    return fs::read_symlink("/proc/self/exe", error);
#endif
}
} // namespace

std::vector<fs::path> packaged_asm_directories()
{
    fs::path executable = executable_path();
    if (executable.empty())
        return {};
    std::error_code error;
    const fs::path canonical = fs::canonical(executable, error);
    if (!error)
        executable = canonical;
    const fs::path directory = executable.parent_path();
    std::vector<fs::path> directories;
#if defined(__APPLE__)
    if (directory.filename() == "MacOS" && directory.parent_path().filename() == "Contents")
        directories.push_back(directory.parent_path() / "Resources" / "asm");
#endif
    directories.push_back(directory / "resources" / "asm");
    directories.push_back((directory / EMU51_INSTALLED_ASM_FROM_BINDIR).lexically_normal());
    return directories;
}

} // namespace emu51_asm::detail
