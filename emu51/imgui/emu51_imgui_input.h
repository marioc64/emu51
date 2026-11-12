// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <optional>

inline std::optional<unsigned long> emu51ParseHexInput(const char* text, unsigned long maxInclusive)
{
	if (text == nullptr)
		return std::nullopt;

	const char* p = text;
	while (*p != '\0' && std::isspace(static_cast<unsigned char>(*p)))
		++p;
	if (*p == '\0')
		return std::nullopt;

	errno = 0;
	char* end = nullptr;
	const unsigned long value = std::strtoul(p, &end, 16);
	if (end == p)
		return std::nullopt;
	while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end)))
		++end;
	if (*end != '\0')
		return std::nullopt;
	if (errno == ERANGE || value > maxInclusive)
		return std::nullopt;
	return value;
}
