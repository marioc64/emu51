// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_editor_files.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;

std::string uniqueSuffix()
{
    static std::atomic<unsigned> sequence{0};
#if defined(_WIN32)
    const auto process = _getpid();
#else
    const auto process = getpid();
#endif
    return std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(process) + "-" + std::to_string(sequence++);
}

std::intptr_t lockDirectory(const fs::path& directory)
{
    const fs::path path = directory / "lock";
#if defined(_WIN32)
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return handle == INVALID_HANDLE_VALUE ? -1 : reinterpret_cast<std::intptr_t>(handle);
#else
    const int fd = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0)
        return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return -1;
    }
    return fd;
#endif
}

void unlockDirectory(std::intptr_t lock)
{
#if defined(_WIN32)
    CloseHandle(reinterpret_cast<HANDLE>(lock));
#else
    close(static_cast<int>(lock));
#endif
}
} // namespace

bool emu51WriteTextFileAtomically(const fs::path& path, std::string_view text)
{
    if (path.empty())
        return false;
    fs::path target = path;
    std::error_code error;
    if (fs::is_symlink(target, error)) {
        target = fs::canonical(target, error);
        if (error)
            return false;
    }
    const fs::path temporary = target.parent_path() /
        ("." + target.filename().string() + ".emu51-" + uniqueSuffix() + ".tmp");
#if defined(_WIN32)
    int fd = -1;
    if (_wsopen_s(&fd, temporary.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
        _SH_DENYRW, _S_IREAD | _S_IWRITE) != 0)
        return false;
#else
    const int fd = open(temporary.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
#endif
    bool success = true;
#if !defined(_WIN32)
    struct stat original{};
    if (stat(target.c_str(), &original) == 0 && fchmod(fd, original.st_mode & 0777) != 0)
        success = false;
#endif
    std::size_t offset = 0;
    while (success && offset < text.size()) {
#if defined(_WIN32)
        const auto count = _write(fd, text.data() + offset,
            static_cast<unsigned>(std::min<std::size_t>(text.size() - offset, 1u << 30)));
#else
        const auto count = write(fd, text.data() + offset,
            std::min<std::size_t>(text.size() - offset, 1u << 30));
#endif
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            success = false;
        else
            offset += static_cast<std::size_t>(count);
    }
#if defined(_WIN32)
    if (success && _commit(fd) != 0)
        success = false;
    if (_close(fd) != 0)
        success = false;
    if (success)
        success = MoveFileExW(temporary.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (success && fsync(fd) != 0)
        success = false;
    if (close(fd) != 0)
        success = false;
    if (success) {
        fs::rename(temporary, target, error);
        success = !error;
    }
#endif
    if (!success)
        fs::remove(temporary, error);
    return success;
}

Emu51EditorRecovery::Emu51EditorRecovery(fs::path directory, std::intptr_t lock)
    : directory_(std::move(directory)), lock_(lock) {}

std::unique_ptr<Emu51EditorRecovery> Emu51EditorRecovery::create(const fs::path& root)
{
    if (root.empty())
        return {};
    std::error_code error;
    fs::create_directories(root, error);
    if (error)
        return {};
    const fs::path directory = root / ("session-" + uniqueSuffix());
    if (!fs::create_directory(directory, error))
        return {};
    auto recovery = claim(directory);
    if (!recovery)
        fs::remove(directory, error);
    return recovery;
}

std::unique_ptr<Emu51EditorRecovery> Emu51EditorRecovery::claim(const fs::path& directory)
{
    const auto lock = lockDirectory(directory);
    if (lock == -1)
        return {};
    return std::unique_ptr<Emu51EditorRecovery>(new Emu51EditorRecovery(directory, lock));
}

std::vector<fs::path> Emu51EditorRecovery::candidates(const fs::path& root)
{
    std::vector<fs::path> paths;
    std::error_code error;
    for (fs::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        std::error_code itemError;
        if (it->is_directory(itemError) && it->path().filename().string().starts_with("session-") &&
            fs::is_regular_file(it->path() / "draft", itemError))
            paths.push_back(it->path());
    }
    std::sort(paths.rbegin(), paths.rend());
    return paths;
}

Emu51EditorRecovery::~Emu51EditorRecovery()
{
    // Remove empty sessions while still holding their lock. Abandoned drafts
    // survive destruction (or a crash) until explicitly saved or discarded.
    std::error_code error;
    const bool empty = !fs::exists(directory_ / "draft", error) && !error;
#if !defined(_WIN32)
    if (empty) {
        fs::remove(directory_ / "lock", error);
        fs::remove(directory_, error);
    }
#endif
    unlockDirectory(lock_);
#if defined(_WIN32)
    if (empty) {
        fs::remove(directory_ / "lock", error);
        fs::remove(directory_, error);
    }
#endif
}

bool Emu51EditorRecovery::save(const Emu51EditorDraft& draft)
{
    std::ostringstream document;
    document << "emu51-draft-v1\n" << std::quoted(draft.sourcePath) << '\n'
        << std::quoted(draft.sourceName) << '\n' << draft.text;
    return emu51WriteTextFileAtomically(directory_ / "draft", document.str());
}

std::optional<Emu51EditorDraft> Emu51EditorRecovery::load() const
{
    std::ifstream input(directory_ / "draft", std::ios::binary);
    std::string version;
    Emu51EditorDraft draft;
    if (!std::getline(input, version) || version != "emu51-draft-v1" ||
        !(input >> std::quoted(draft.sourcePath)) || input.get() != '\n' ||
        !(input >> std::quoted(draft.sourceName)) || input.get() != '\n')
        return {};
    char chunk[4096];
    while (input.read(chunk, sizeof(chunk)) || input.gcount() != 0)
        draft.text.append(chunk, static_cast<std::size_t>(input.gcount()));
    if (input.bad() || !input.eof())
        return {};
    return draft;
}

void Emu51EditorRecovery::clear()
{
    std::error_code error;
    fs::remove(directory_ / "draft", error);
}
