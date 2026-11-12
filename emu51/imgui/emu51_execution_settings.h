// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <filesystem>

#include "emu51_cycle_timing.h"

struct Emu51ExecutionSettings {
	static constexpr int kMinStepsPerFrame = 1;
	static constexpr int kMaxStepsPerFrame = 1000000;

	bool cycleExact = true;
	double crystalMHz = Emu51CycleTiming::kDefaultCrystalMHz;
	int stepsPerFrame = 500;
};

Emu51ExecutionSettings emu51LoadExecutionSettings(const std::filesystem::path& path);
bool emu51SaveExecutionSettings(const std::filesystem::path& path, const Emu51ExecutionSettings& settings);
