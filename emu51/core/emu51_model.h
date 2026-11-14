// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

enum class Emu51Model : std::uint8_t { i8051, i8052 };
struct Emu51ModelInfo {
    Emu51Model model;
    const char* id;
    unsigned internalRamBytes;
    unsigned internalRomBytes;
    bool timer2;
};
inline constexpr Emu51ModelInfo emu51ModelInfo(Emu51Model model)
{
    return model == Emu51Model::i8052
        ? Emu51ModelInfo{model, "8052", 256, 8192, true}
        : Emu51ModelInfo{Emu51Model::i8051, "8051", 128, 4096, false};
}
inline std::optional<Emu51Model> emu51ParseModel(std::string_view id)
{
    if (id == "8051") return Emu51Model::i8051;
    if (id == "8052") return Emu51Model::i8052;
    return std::nullopt;
}
