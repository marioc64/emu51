// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

inline std::optional<unsigned long> emu51ParseHexInput(std::string_view text, unsigned long maxInclusive)
{
	// Dialog buffers may include unused bytes after the first NUL.
	const std::string input(text.substr(0, text.find('\0')));
	const char* begin = input.c_str();
	errno = 0;
	char* end = nullptr;
	const unsigned long value = std::strtoul(begin, &end, 16);
	if (end == begin || errno == ERANGE || value > maxInclusive)
		return std::nullopt;
	const auto trailing = std::string_view(end, static_cast<std::size_t>(begin + input.size() - end));
	for (const char ch : trailing) {
		if (!std::isspace(static_cast<unsigned char>(ch)))
			return std::nullopt;
	}
	return value;
}

template <std::size_t Size>
inline std::optional<unsigned long> emu51ParseHexInput(const char (&text)[Size], unsigned long maxInclusive)
{
	return emu51ParseHexInput(std::string_view(text, Size), maxInclusive);
}
