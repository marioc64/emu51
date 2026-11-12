// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <filesystem>
#include <vector>

namespace emu51_asm::detail {

// Locate packaged include files relative to the running executable, so moving
// the application or install tree does not require the original checkout.
std::vector<std::filesystem::path> packaged_asm_directories();

} // namespace emu51_asm::detail
