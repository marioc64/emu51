// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "emu51.h"

void emu51DrawAlternatingContentRowBackground(int rowIndex, float width, float height);
void emu51DrawMarkedHexByte(std::uint8_t value, std::uint16_t address, std::uint16_t pc, std::uint16_t dptr);
