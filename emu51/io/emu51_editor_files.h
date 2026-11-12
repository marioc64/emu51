// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Write, flush and close a sibling temporary file before replacing the target.
// On failure the original target is left intact. Existing symlinks are followed.
bool emu51WriteTextFileAtomically(const std::filesystem::path& path, std::string_view text);

struct Emu51EditorDraft {
    std::string sourcePath;
    std::string sourceName;
    std::string text;
};

// Each session owns a separate directory and an OS lock. Recovery only claims
// abandoned sessions, so two running editors never recover each other's work.
class Emu51EditorRecovery {
public:
    static std::unique_ptr<Emu51EditorRecovery> create(const std::filesystem::path& root);
    static std::vector<std::filesystem::path> candidates(const std::filesystem::path& root);
    static std::unique_ptr<Emu51EditorRecovery> claim(const std::filesystem::path& directory);
    ~Emu51EditorRecovery();
    Emu51EditorRecovery(const Emu51EditorRecovery&) = delete;
    Emu51EditorRecovery& operator=(const Emu51EditorRecovery&) = delete;

    bool save(const Emu51EditorDraft& draft);
    std::optional<Emu51EditorDraft> load() const;
    void clear();
    const std::filesystem::path& directory() const { return directory_; }

private:
    Emu51EditorRecovery(std::filesystem::path directory, std::intptr_t lock);
    std::filesystem::path directory_;
    std::intptr_t lock_;
};
