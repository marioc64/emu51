// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>

// Wall-clock pacing for a classic 12-clock 8051 or logical 60 Hz instruction
// budgets. The CPU itself remains independent of host time.
class Emu51CycleTiming {
public:
	using Clock = std::chrono::steady_clock;
	static constexpr double kDefaultCrystalMHz = 12.0;
	static constexpr double kMinCrystalMHz = 0.000001; // 1 Hz resolution.
	static constexpr double kMaxCrystalMHz = 1000.0;

	static bool validCrystalMHz(double mhz)
	{
		return std::isfinite(mhz) && mhz >= kMinCrystalMHz && mhz <= kMaxCrystalMHz;
	}

	void reset(Clock::time_point now = Clock::now())
	{
		lastUpdate_ = now;
		measurementStart_ = now;
		cycleBudget_ = 0;
		cycleRemainder_ = 0;
		executedCycles_ = 0;
		initialized_ = true;
	}

	void update(Clock::time_point now, double crystalMHz)
	{
		if (!validCrystalMHz(crystalMHz)) {
			reset(now);
			return;
		}
		updateBudget(now, static_cast<std::uint64_t>(std::llround(crystalMHz * 1'000'000.0)), 12'000'000'000ULL);
	}

	// A logical frame is 1/60 second, independent of display refresh and service
	// frequency. In this mode callers consume one budget unit per instruction.
	void updateInstructions(Clock::time_point now, unsigned instructionsPerFrame)
	{
		if (instructionsPerFrame == 0 || instructionsPerFrame > 1'000'000) {
			reset(now);
			return;
		}
		updateBudget(now, static_cast<std::uint64_t>(instructionsPerFrame) * 60, 1'000'000'000ULL);
	}

	bool cyclesDue() const { return cycleBudget_ > 0; }

	void consume(unsigned cycles)
	{
		cycleBudget_ -= cycles;
		executedCycles_ += cycles;
	}

	double cyclesPerSecond(Clock::time_point now = Clock::now()) const
	{
		if (!initialized_ || now <= measurementStart_)
			return 0.0;
		const double seconds = std::chrono::duration<double>(now - measurementStart_).count();
		return static_cast<double>(executedCycles_) / seconds;
	}

private:
	void updateBudget(Clock::time_point now, std::uint64_t rate, std::uint64_t unitsPerCycle)
	{
		// A long suspension (for example, system sleep) starts a fresh interval.
		// Ordinary frame jitter and unconsumed work retain their cycle balance.
		if (!initialized_ || now < lastUpdate_ || now - lastUpdate_ > std::chrono::seconds(1)) {
			reset(now);
			return;
		}
		const auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now - lastUpdate_).count();
		lastUpdate_ = now;
		// Hz * nanoseconds / (12 * 10^9) gives machine cycles. Retain the
		// integer remainder to avoid drift at fractional-MHz crystal rates.
		// The one-second interval and 1 GHz limit keep this product in range.
		const auto units = static_cast<std::uint64_t>(elapsedNs) * rate + cycleRemainder_;
		cycleBudget_ += static_cast<std::int64_t>(units / unitsPerCycle);
		cycleRemainder_ = units % unitsPerCycle;
	}

	Clock::time_point lastUpdate_{};
	Clock::time_point measurementStart_{};
	std::int64_t cycleBudget_ = 0;
	std::uint64_t cycleRemainder_ = 0;
	std::uint64_t executedCycles_ = 0;
	bool initialized_ = false;
};
