// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_execution_settings.h"

#include <fstream>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

#include "kason_schema.h"

namespace {

struct StoredExecutionSettings {
	char executionMode[32];
	double crystalMHz;
	int stepsPerFrame;
};

const kason_schema* executionSettingsSchema()
{
	static const kason_schema_field fields[] = {
		KaSON_FIELD_STRING(StoredExecutionSettings, executionMode, "execution_mode", KaSON_DEFAULT_STRING("cycle_exact")),
		KaSON_FIELD_DOUBLE(StoredExecutionSettings, crystalMHz, "crystal_mhz", KaSON_DEFAULT_DOUBLE(Emu51CycleTiming::kDefaultCrystalMHz)),
		KaSON_FIELD_INT(StoredExecutionSettings, stepsPerFrame, "instructions_per_frame", KaSON_DEFAULT_INT(500)),
	};
	KaSON_SCHEMA_DEFINE(schema, StoredExecutionSettings, fields, 8);
	// Function-local initialization prepares the shared schema once; subsequent
	// unpack/pack calls use its immutable lookup data.
	static const bool initialized = kason_schema_init(&schema) == KaSON_SCHEMA_SUCCESS;
	return initialized ? &schema : nullptr;
}

} // namespace

Emu51ExecutionSettings emu51LoadExecutionSettings(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	if (json.empty() || input.bad())
		return {};

	StoredExecutionSettings stored{};
	const auto* schema = executionSettingsSchema();
	// Strict unpacking validates the complete bounded document, its field types,
	// and duplicate keys before any settings are applied.
	if (!schema || kason_unpack_range(json.data(), json.data() + json.size() - 1,
		schema, &stored, nullptr) != KaSON_SCHEMA_SUCCESS)
		return {};

	Emu51ExecutionSettings settings;
	settings.cycleExact = std::string_view(stored.executionMode) != "instructions_per_frame";
	if (Emu51CycleTiming::validCrystalMHz(stored.crystalMHz))
		settings.crystalMHz = stored.crystalMHz;
	if (stored.stepsPerFrame >= Emu51ExecutionSettings::kMinStepsPerFrame &&
		stored.stepsPerFrame <= Emu51ExecutionSettings::kMaxStepsPerFrame)
		settings.stepsPerFrame = stored.stepsPerFrame;
	return settings;
}

bool emu51SaveExecutionSettings(const std::filesystem::path& path, const Emu51ExecutionSettings& settings)
{
	if (path.empty() || !Emu51CycleTiming::validCrystalMHz(settings.crystalMHz) ||
		settings.stepsPerFrame < Emu51ExecutionSettings::kMinStepsPerFrame ||
		settings.stepsPerFrame > Emu51ExecutionSettings::kMaxStepsPerFrame)
		return false;
	const auto* schema = executionSettingsSchema();
	StoredExecutionSettings stored{"cycle_exact", settings.crystalMHz, settings.stepsPerFrame};
	if (!settings.cycleExact) {
		constexpr char mode[] = "instructions_per_frame";
		static_assert(sizeof(mode) <= sizeof(stored.executionMode));
		std::memcpy(stored.executionMode, mode, sizeof(mode));
	}
	char json[256];
	kason_writer writer{};
	if (!schema || kason_writer_init_buffer(&writer, json, sizeof(json)) != KaSON_SCHEMA_SUCCESS ||
		kason_pack(&writer, schema, &stored, 0, nullptr) != KaSON_SCHEMA_SUCCESS)
		return false;

	std::ofstream output(path, std::ios::trunc);
	if (!output)
		return false;
	output.write(json, static_cast<std::streamsize>(kason_writer_length(&writer)));
	output.put('\n');
	output.close();
	return !output.fail();
}
