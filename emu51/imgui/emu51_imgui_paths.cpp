// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_paths.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

namespace {

namespace fs = std::filesystem;

constexpr const char* kAppConfigDirName = "emu51";
constexpr const char* kImGuiIniFileName = "imgui.ini";
constexpr const char* kImGuiLayoutFileName = "imgui_layout_state.txt";
constexpr const char* kRecentProgramsFileName = "recent_programs.txt";
constexpr const char* kSettingsFileName = "settings.json";

std::string getEnvOrEmpty(const char* name)
{
#if defined(_MSC_VER)
	char* value = nullptr;
	std::size_t size = 0;
	if (_dupenv_s(&value, &size, name) != 0)
		return {};
	const std::unique_ptr<char, decltype(&std::free)> ownedValue(value, &std::free);
	return ownedValue != nullptr ? ownedValue.get() : "";
#else
	const char* value = std::getenv(name);
	return value != nullptr ? value : "";
#endif
}

fs::path getConfigDir()
{
	fs::path configDir;

#if defined(_WIN32)
	std::string appData = getEnvOrEmpty("APPDATA");
	if (!appData.empty()) {
		configDir = fs::path(appData) / kAppConfigDirName;
	} else {
		std::string userProfile = getEnvOrEmpty("USERPROFILE");
		if (!userProfile.empty())
			configDir = fs::path(userProfile) / "AppData" / "Roaming" / kAppConfigDirName;
	}
#elif defined(__APPLE__)
	std::string home = getEnvOrEmpty("HOME");
	if (!home.empty())
		configDir = fs::path(home) / "Library" / "Application Support" / kAppConfigDirName;
#else
	std::string xdgConfigHome = getEnvOrEmpty("XDG_CONFIG_HOME");
	if (!xdgConfigHome.empty()) {
		configDir = fs::path(xdgConfigHome) / kAppConfigDirName;
	} else {
		std::string home = getEnvOrEmpty("HOME");
		if (!home.empty())
			configDir = fs::path(home) / ".config" / kAppConfigDirName;
	}
#endif

	if (configDir.empty())
		return {};

	std::error_code ec;
	fs::create_directories(configDir, ec);
	if (ec)
		return {};

	return configDir;
}

}  // namespace

std::string emu51GetImguiIniPath()
{
	const fs::path configDir = getConfigDir();
	if (configDir.empty())
		return {};

	return (configDir / kImGuiIniFileName).string();
}

std::string emu51GetImguiLayoutPath()
{
	const fs::path configDir = getConfigDir();
	if (configDir.empty())
		return {};

	return (configDir / kImGuiLayoutFileName).string();
}

std::string emu51GetRecentProgramsPath()
{
	const fs::path configDir = getConfigDir();
	if (configDir.empty())
		return {};

	return (configDir / kRecentProgramsFileName).string();
}

std::string emu51GetSettingsPath()
{
	const fs::path configDir = getConfigDir();
	if (configDir.empty())
		return {};
	return (configDir / kSettingsFileName).string();
}

std::string emu51GetEditorRecoveryDirectory()
{
	const fs::path configDir = getConfigDir();
	return configDir.empty() ? std::string{} : (configDir / "editor-recovery").string();
}
