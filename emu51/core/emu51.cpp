// SPDX-License-Identifier: GPL-2.0-only


#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "emu51.h"

char* code_51::make_ds(emu51& env, std::uint16_t ram_pl, char* buffer) const
{
	char* display_string = buffer;
	std::memset(display_string, 0, 32);

	// Mnemonic column: fixed-width (5 chars) + one space separator.
	std::snprintf(display_string, 7, "%-5.5s ", mnem);

	char operands[32];
	std::memset(operands, 0, sizeof(operands));

	// AJMP/ACALL are page-relative; decode full 11-bit target from opcode+PC.
	if ((code & 0x1f) == 0x01 || (code & 0x1f) == 0x11) {
		const std::uint16_t target = static_cast<std::uint16_t>(((ram_pl + 2) & 0xf800) |
			((code & 0xe0) << 3) |
			env.readProg(ram_pl + 1));
		std::snprintf(operands, sizeof(operands), "%04x", target);
	}
	else if (!(datas[0] == '%' && datas[1] == '\0')) {
		const int datasLen = static_cast<int>(std::strlen(datas));
		int out = 0;
		auto append_char = [&](char ch) {
			if (out < static_cast<int>(sizeof(operands)) - 1)
				operands[out++] = ch;
		};
		auto append_hex2 = [&](std::uint8_t value) {
			if (out <= static_cast<int>(sizeof(operands)) - 3)
				std::snprintf(operands + out, sizeof(operands) - static_cast<size_t>(out), "%02x", value), out += 2;
		};
		auto append_hex4 = [&](std::uint16_t value) {
			if (out <= static_cast<int>(sizeof(operands)) - 5)
				std::snprintf(operands + out, sizeof(operands) - static_cast<size_t>(out), "%04x", value), out += 4;
		};

		for (int c = 0; c < datasLen; c++) {
			if (datas[c] == 'O' && c + 2 < datasLen && datas[c + 1] == '%' &&
				(datas[c + 2] == '1' || datas[c + 2] == '2')) {
				const int arg = datas[c + 2] - '0';
				const std::uint16_t base = static_cast<std::uint16_t>(ram_pl + arg + 1);
				const signed char rel = static_cast<signed char>(env.readProg(ram_pl + arg));
				append_hex4(static_cast<std::uint16_t>(base + rel));
				c += 2;
				continue;
			}
			if (datas[c] == '%' && c + 1 < datasLen && (datas[c + 1] == '1' || datas[c + 1] == '2')) {
				const int arg = datas[c + 1] - '0';
				append_hex2(env.readProg(ram_pl + arg));
				++c;
				continue;
			}
			append_char(datas[c]);
		}
	}

	for (int i = 0; i < static_cast<int>(sizeof(operands)) - 1 && operands[i] != '\0'; i++)
		display_string[6 + i] = operands[i];
	return display_string;
}

class memory_address_space : public address_space
{
	std::uint8_t * const data;
	std::uint8_t * const prog;

	const bool freeData;
	const bool freeProg;
public:
	memory_address_space(bool mixed) :
		data(new std::uint8_t[0x10000]()),
		prog(mixed ? data : new std::uint8_t[0x10000]()),
		freeData(true), freeProg(!mixed) { 
	}
	memory_address_space(std::uint8_t * data, std::uint8_t * prog) :
		data(data), prog(prog), freeData(false), freeProg(false) {
	};
	~memory_address_space() override {
		if (freeData)
			delete[] data;
		if (freeProg)
			delete[] prog;
	}

	void writeData(std::uint16_t a, std::uint8_t d) override
	{
		data[a] = d;
	}
	std::uint8_t readData(std::uint16_t a) override
	{
		return data[a];
	}

	void writeProg(std::uint16_t a, std::uint8_t d) override
	{
		prog[a] = d;
	}

	std::uint8_t readProg(std::uint16_t a) override
	{
		return prog[a];
	}
};

emu51::emu51(bool mixed_mode) : emu51(std::make_shared<memory_address_space>(mixed_mode))
{
}

emu51::emu51(std::uint8_t * RAM) : emu51(RAM, RAM) { }
emu51::emu51(std::uint8_t * RAM, std::uint8_t * PMEM) : emu51(std::make_shared<memory_address_space>(RAM, PMEM)) { }

int emu51::interrupt_priority(interrupt_source src)
{
	switch (src)
	{
	case interrupt_source::ext0: return checkB(0xb8) ? 1 : 0; // PX0
	case interrupt_source::t0:   return checkB(0xb9) ? 1 : 0; // PT0
	case interrupt_source::ext1: return checkB(0xba) ? 1 : 0; // PX1
	case interrupt_source::t1:   return checkB(0xbb) ? 1 : 0; // PT1
	default: return 0;
	}
}

bool emu51::interrupt_pending_enabled(interrupt_source src)
{
	switch (src)
	{
	case interrupt_source::ext0: return checkB(0x89) && checkB(0xa8); // IE0 && EX0
	case interrupt_source::t0:   return checkB(0x8d) && checkB(0xa9); // TF0 && ET0
	case interrupt_source::ext1: return checkB(0x8b) && checkB(0xaa); // IE1 && EX1
	case interrupt_source::t1:   return checkB(0x8f) && checkB(0xab); // TF1 && ET1
	default: return false;
	}
}

void emu51::clear_interrupt_flag(interrupt_source src)
{
	switch (src)
	{
	case interrupt_source::ext0:
		if (checkB(0x88)) // IT0 edge triggered
			clrB(0x89);
		break;
	case interrupt_source::t0:   clrB(0x8d); break; // TF0
	case interrupt_source::ext1:
		if (checkB(0x8a)) // IT1 edge triggered
			clrB(0x8b);
		break;
	case interrupt_source::t1:   clrB(0x8f); break; // TF1
	}
}

std::uint16_t emu51::interrupt_vector(interrupt_source src)
{
	switch (src)
	{
	case interrupt_source::ext0: return 0x0003;
	case interrupt_source::t0:   return 0x000b;
	case interrupt_source::ext1: return 0x0013;
	case interrupt_source::t1:   return 0x001b;
	default: return 0x0000;
	}
}

bool emu51::can_service_priority(int priority)
{
	if (irqDepth <= 0)
		return true;
	const int currentPriority = interrupt_priority(static_cast<interrupt_source>(irqStack[irqDepth - 1]));
	return priority > currentPriority;
}

void emu51::push_interrupt_source(interrupt_source src)
{
	if (irqDepth >= 8)
		return;
	irqStack[irqDepth++] = static_cast<std::uint8_t>(src);
}

void emu51::pop_interrupt_source()
{
	if (irqDepth <= 0)
		return;
	irqDepth--;
}

bool emu51::try_service_interrupt()
{
	if (!checkB(0xaf)) // EA
		return false;

	const interrupt_source pollOrder[] = {
		interrupt_source::ext0,
		interrupt_source::t0,
		interrupt_source::ext1,
		interrupt_source::t1
	};

	interrupt_source selected;
	bool hasSelected = false;
	for (int desiredPriority = 1; desiredPriority >= 0 && !hasSelected; desiredPriority--)
	{
		for (const interrupt_source src : pollOrder)
		{
			if (!interrupt_pending_enabled(src))
				continue;
			if (interrupt_priority(src) != desiredPriority)
				continue;
			if (!can_service_priority(desiredPriority))
				continue;
			selected = src;
			hasSelected = true;
			break;
		}
	}

	if (!hasSelected)
		return false;

	SP = SP + 1;
	SFR[SP] = static_cast<std::uint8_t>(PC & 0x00ff);
	SP = SP + 1;
	SFR[SP] = static_cast<std::uint8_t>(PC >> 8);
	clear_interrupt_flag(selected);
	push_interrupt_source(selected);
	PC = interrupt_vector(selected);
	return true;
}

// initialize the emulator
void emu51::init()
{
	int i;
	c_time = 0; // cycles counter
	irqDepth = 0;
	for (std::uint8_t &src : irqStack)
		src = 0;
	for (i = 0; i < 256; i++)
	{
		SFR[i] = 0; // all regs in internal RAM & SFR
	}
	R = SFR; // PSW was reset to bank 0; discard the previously selected bank.
	// Some SFR registers has other default value than 0
	P0 = 0xff;
	P1 = 0xff;
	P2 = 0xff;                        
	P3 = 0xff;
	SP = 0x07;

	PC = 0x0000;
}

int emu51::process()
{
	const std::uint8_t code = readProg(PC);
	const std::uint8_t cycles = asm51[code].cycles;
	exec_opcode_switch(code);
	return finalize_instruction(cycles);
}

int emu51::finalize_instruction(std::uint8_t cycles)
{
	std::uint8_t accBits = 0;
	std::uint8_t tmpB;

	// count bits in Acc
	for (std::uint8_t c = 0; c <= 7; c++)
		if ((1 << c) & Acc) ++accBits;
	// parity check: 1 for odd parity in Acc
	if (accBits % 2) set_P();
	else clr_P();

	// setup Rx bank
	tmpB = check_RS1() << 1 | check_RS0();
	R = &SFR[tmpB << 3];

	advance_cycles(cycles);
	if (try_service_interrupt()) {
		// The hardware LCALL consumes two cycles. Timers keep running during
		// entry, but another interrupt is not polled until the next instruction.
		constexpr std::uint8_t entryCycles = 2;
		advance_cycles(entryCycles);
		return cycles + entryCycles;
	}
	return cycles;
}

void emu51::advance_cycles(std::uint8_t cycles)
{
	c_time += cycles;

	std::uint8_t modeT0, modeT1;
	const bool gateT0 = (TMOD & 0x08) != 0;
	const bool ctT0 = (TMOD & 0x04) != 0;
	const bool gateT1 = (TMOD & 0x80) != 0;
	const bool ctT1 = (TMOD & 0x40) != 0;
	const bool runT0 = checkB(0x8c) && (!gateT0 || (P3 & bit2)); // TR0 && (!GATE0 || INT0 high)
	const bool runT1 = checkB(0x8e) && (!gateT1 || (P3 & bit3)); // TR1 && (!GATE1 || INT1 high)

	// Advance timers without polling interrupts.
	modeT0 = TMOD & 0x03;
	modeT1 = (TMOD & 0x30) >> 4;
	auto inc_mode0 = [cycles](std::uint8_t &th, std::uint8_t &tl, std::uint8_t tfBit, emu51* self) {
		unsigned val = (static_cast<unsigned>(th) << 5) | (static_cast<unsigned>(tl) & 0x1f);
		val += static_cast<unsigned>(cycles);
		if (val > 0x1fff) {
			val &= 0x1fff;
			self->setB(tfBit);
		}
		th = static_cast<std::uint8_t>((val >> 5) & 0xff);
		tl = static_cast<std::uint8_t>((tl & 0xe0) | (val & 0x1f));
	};
	auto inc_mode1 = [cycles](std::uint8_t &th, std::uint8_t &tl, std::uint8_t tfBit, emu51* self) {
		const unsigned val = (static_cast<unsigned>(th) << 8) | static_cast<unsigned>(tl);
		const unsigned next = val + static_cast<unsigned>(cycles);
		if (next > 0xffff) {
			self->setB(tfBit);
		}
		const unsigned wrapped = next & 0xffff;
		th = static_cast<std::uint8_t>((wrapped >> 8) & 0xff);
		tl = static_cast<std::uint8_t>(wrapped & 0xff);
	};
	auto inc_mode2 = [cycles](std::uint8_t &th, std::uint8_t &tl, std::uint8_t tfBit, emu51* self) {
		const unsigned next = static_cast<unsigned>(tl) + static_cast<unsigned>(cycles);
		if (next > 0xff) {
			self->setB(tfBit);
			const unsigned reloadPeriod = 0x100u - static_cast<unsigned>(th);
			tl = static_cast<std::uint8_t>(th + (next - 0x100u) % reloadPeriod);
		}
		else {
			tl = static_cast<std::uint8_t>(next);
		}
	};
	auto inc_mode3_counter = [cycles](std::uint8_t &counter, std::uint8_t tfBit, emu51* self) {
		const unsigned next = static_cast<unsigned>(counter) + static_cast<unsigned>(cycles);
		if (next > 0xff)
			self->setB(tfBit);
		counter = static_cast<std::uint8_t>(next & 0xff);
	};

	// Timer 0
	if (modeT0 == 3) {
		if (runT0 && !ctT0)
			inc_mode3_counter(TL0, 0x8d, this);
		if (checkB(0x8e)) // TR1 controls TH0 in mode 3
			inc_mode3_counter(TH0, 0x8f, this);
	}
	else if (runT0 && !ctT0)
	{
		switch (modeT0)
		{
		case 0:
			inc_mode0(TH0, TL0, 0x8d, this);
			break;
		case 1:
			inc_mode1(TH0, TL0, 0x8d, this);
			break;
		case 2:
			inc_mode2(TH0, TL0, 0x8d, this);
			break;
		case 3:
			break;
		}
	}
	if (modeT0 != 3 && runT1 && !ctT1)
	{
		switch (modeT1)
		{
		case 0:
			inc_mode0(TH1, TL1, 0x8f, this);
			break;
		case 1:
			inc_mode1(TH1, TL1, 0x8f, this);
			break;
		case 2:
			inc_mode2(TH1, TL1, 0x8f, this);
			break;
		case 3:
			break;
		}
	}
}

int emu51::process_table()
{
    const std::uint8_t code = readProg(PC);
    opcode_dispatch_table()[code](this);
    return finalize_instruction(asm51[code].cycles);
}

void emu51::triggerExternalInterrupt0()
{
	setB(0x89); // IE0
}

void emu51::triggerExternalInterrupt1()
{
	setB(0x8b); // IE1
}

char* emu51::disasm(char* buffer)
{
	std::uint8_t c = readProg(PC);
	code_51 code = asm51[c];
	return code.make_ds(*this, PC, buffer);
}

int emu51::loadHex(const char * filename)
{
	if (filename == nullptr || *filename == 0)
		return -1;

	std::ifstream input(filename);
	if (!input)
		return -1;

	auto hex_nibble = [](char ch) -> int {
		if (ch >= '0' && ch <= '9')
			return ch - '0';
		if (ch >= 'a' && ch <= 'f')
			return ch - 'a' + 10;
		if (ch >= 'A' && ch <= 'F')
			return ch - 'A' + 10;
		return -1;
	};

	auto read_hex_byte = [&](const std::string& line, std::size_t offset, std::uint8_t& value) -> bool {
		if (offset + 1 >= line.size())
			return false;
		const int high = hex_nibble(line[offset]);
		const int low = hex_nibble(line[offset + 1]);
		if (high < 0 || low < 0)
			return false;
		value = static_cast<std::uint8_t>((high << 4) | low);
		return true;
	};

	// Validate the image before invoking any memory-write callbacks. A later
	// malformed record must not leave part of a rejected program in memory.
	std::vector<std::pair<std::uint16_t, std::uint8_t>> pendingWrites;
	auto commit = [&]() {
		for (const auto& [address, value] : pendingWrites)
			addressSpace->writeProg(address, value);
		return 0;
	};
	std::uint32_t extendedBase = 0;
	std::string line;
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty())
			continue;
		if (line[0] != ':' || line.size() < 11)
			return -1;

		std::uint8_t count = 0;
		std::uint8_t addressHigh = 0;
		std::uint8_t addressLow = 0;
		std::uint8_t recordType = 0;
		if (!read_hex_byte(line, 1, count) ||
			!read_hex_byte(line, 3, addressHigh) ||
			!read_hex_byte(line, 5, addressLow) ||
			!read_hex_byte(line, 7, recordType))
			return -1;

		const std::size_t expectedSize = 11u + static_cast<std::size_t>(count) * 2u;
		if (line.size() != expectedSize)
			return -1;

		unsigned checksumSum = count + addressHigh + addressLow + recordType;
		std::string data;
		data.reserve(static_cast<std::size_t>(count));
		for (std::size_t i = 0; i < count; ++i) {
			std::uint8_t value = 0;
			if (!read_hex_byte(line, 9u + i * 2u, value))
				return -1;
			data.push_back(static_cast<char>(value));
			checksumSum += value;
		}

		std::uint8_t checksum = 0;
		if (!read_hex_byte(line, 9u + static_cast<std::size_t>(count) * 2u, checksum))
			return -1;
		checksumSum += checksum;
		if ((checksumSum & 0xffu) != 0)
			return -1;

		const std::uint16_t address = static_cast<std::uint16_t>((static_cast<std::uint16_t>(addressHigh) << 8) | addressLow);
		switch (recordType) {
		case 0x00:
			for (std::size_t i = 0; i < data.size(); ++i) {
				const std::uint32_t targetAddress = extendedBase + address + static_cast<std::uint32_t>(i);
				if (targetAddress > 0xffffu)
					return -1;
				pendingWrites.emplace_back(static_cast<std::uint16_t>(targetAddress), static_cast<std::uint8_t>(data[i]));
			}
			break;
		case 0x01:
			return count == 0 ? commit() : -1;
		case 0x02:
			if (count != 2)
				return -1;
			extendedBase = static_cast<std::uint32_t>(
				(static_cast<std::uint8_t>(data[0]) << 8) | static_cast<std::uint8_t>(data[1])) << 4;
			break;
		case 0x04:
			if (count != 2)
				return -1;
			extendedBase = static_cast<std::uint32_t>(
				(static_cast<std::uint8_t>(data[0]) << 8) | static_cast<std::uint8_t>(data[1])) << 16;
			break;
		case 0x03:
		case 0x05:
			break;
		default:
			return -1;
		}
	}
	if (input.bad() || !input.eof())
		return -1;
	return commit();
}

bool emu51::check_C(void)
{
	return PSW & bit7;
}
bool emu51::check_AC(void)
{
	return emu51::PSW & bit6;
}
bool emu51::check_OV(void)
{
	return PSW & bit2;
}
void emu51::set_C(void)
{
	PSW = PSW | bit7;
}
void emu51::set_AC(void)
{
	PSW = PSW | bit6;
}
void emu51::set_OV(void)
{
	PSW = PSW | bit2;
}
void emu51::clr_C(void)
{
	PSW = PSW & 0x7f;
}
void emu51::clr_AC(void)
{
	PSW = PSW & 0xbf;
}
void emu51::clr_OV(void)
{
	PSW = PSW & 0xfb;
}
bool emu51::check_P(void)
{
	return PSW & bit0;
}
void emu51::set_P(void)
{
	PSW = PSW | bit0;
}
void emu51::clr_P(void)
{
	PSW = PSW & 0xfe;
}
void emu51::setB(std::uint8_t bit) // (bit) <- 1
{
	std::uint8_t tmpB;
	std::uint8_t tmpbit;
	tmpbit = bit & 0x07;
	tmpB = bit & 0xf8;
	if (bit >= 128)
	{         // bit set in SFR area
		SFR[tmpB] = SFR[tmpB] | (1 << tmpbit);
	}
	else {  // bit set in int. RAM
		tmpB = (tmpB >> 3) + 32;
		SFR[tmpB] = SFR[tmpB] | (1 << tmpbit);
	}
}
void emu51::clrB(std::uint8_t bit) // (bit) <- 0
{
	std::uint8_t tmpB;
	std::uint8_t tmpbit;
	tmpbit = bit & 0x07;
	tmpB = bit & 0xf8;
	if (bit >= 128)
	{         // bit clear in SFR area
		SFR[tmpB] = SFR[tmpB] & ~(1 << tmpbit);
	}
	else {  // bit clear in int. RAM
		tmpB = (tmpB >> 3) + 32;
		SFR[tmpB] = SFR[tmpB] & ~(1 << tmpbit);
	}
}
bool emu51::checkB(std::uint8_t bit) // returns (bit)
{
	std::uint8_t tmpB;
	std::uint8_t tmpbit;
	tmpbit = bit & 0x07;
	tmpB = bit & 0xf8;
	if (bit >= 128)
	{         // bit check in SFR area
		return SFR[tmpB] & 1 << tmpbit;
	}
	else {  // bit check in int. RAM
		tmpB = (tmpB >> 3) + 32;
		return SFR[tmpB] & 1 << tmpbit;
	}
}
bool emu51::check_RS0(void)
{
	return PSW & bit3;
}
void emu51::set_RS0(void)
{
	PSW = PSW | bit3;
}
void emu51::clr_RS0(void)
{
	PSW = PSW & 0xf7;
}
bool emu51::check_RS1(void)
{
	return PSW & bit4;
}
void emu51::set_RS1(void)
{
	PSW = PSW | bit4;
}
void emu51::clr_RS1(void)
{
	PSW = PSW & 0xef;
}
