// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstdint>

class emu51;

class code_51
{

public:
	constexpr code_51(std::uint8_t code, const char* mnem, const char* datas, std::uint8_t length, std::uint8_t cycles, std::uint16_t assemblyOrder) : mnem(mnem), datas(datas), code(code), lenght(length), cycles(cycles), assemblyOrder(assemblyOrder)
	{}
	const char* const mnem;          // instruction mnemonic (2-4 characters)
	const char* const datas;         // string which contains datas which will be displayed after mnemonic
	const std::uint8_t code;           // instruction code
	const std::uint8_t lenght;         // bytes which are needed to write this instruction into memory
	const std::uint8_t cycles;         // time unit

	const std::uint16_t assemblyOrder; // 255 denotes the reserved, non-assemblable opcode.

	char* make_ds(emu51& env, std::uint16_t ram_pl, char* buffer) const; // make display string
	void process();            // process the instruction
};

extern const std::array<code_51, 256> asm51;
