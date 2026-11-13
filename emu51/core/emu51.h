// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include "opcode_metadata.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

inline constexpr std::uint8_t bit0 = 0x01;
inline constexpr std::uint8_t bit1 = 0x02;
inline constexpr std::uint8_t bit2 = 0x04;
inline constexpr std::uint8_t bit3 = 0x08;
inline constexpr std::uint8_t bit4 = 0x10;
inline constexpr std::uint8_t bit5 = 0x20;
inline constexpr std::uint8_t bit6 = 0x40;
inline constexpr std::uint8_t bit7 = 0x80;

/*
template<class DataType, class AddrType>
class functions_as_array
{
	std::function <DataType(AddrType) readF;
	std::function <void(AddrType,DataType)> writeF;
public:
	functions_as_array(std::function < DataType(AddrType) readF, std::function<void(AddrType, DataType)> writeF) : readF(readF), writeF(writeF) {}
	DataType operator[] (int index) {
		return 
	}
};
*/

class address_space
{
public:
	virtual ~address_space() = default;
	virtual void writeData (std::uint16_t a, std::uint8_t d) = 0;
	virtual std::uint8_t readData  (std::uint16_t a) = 0;
	virtual void writeProg (std::uint16_t a, std::uint8_t d) = 0;
	virtual std::uint8_t readProg  (std::uint16_t a) = 0;
};

class functional_address_space : public address_space
{
public:
	typedef std::function<std::uint8_t(std::uint16_t)> read_f;
	typedef std::function<void(std::uint16_t, std::uint8_t)> write_f;

private:
	read_f  readDataF;
	write_f writeDataF;
	read_f  readProgF;
	write_f writeProgF;

public:
	functional_address_space(read_f readDataF, write_f writeDataF, read_f readProgF, write_f writeProgF) :
		readDataF(readDataF), writeDataF(writeDataF), readProgF(readProgF), writeProgF(writeProgF) {}

	functional_address_space(read_f readDataF, write_f writeDataF) :
		readDataF(readDataF), writeDataF(writeDataF), readProgF(readDataF), writeProgF(writeDataF) {}

	void writeData (std::uint16_t a, std::uint8_t d) override { writeDataF(a, d); }
	std::uint8_t readData  (std::uint16_t a)         override { return readDataF(a); }
	void writeProg (std::uint16_t a, std::uint8_t d) override { writeProgF(a, d); }
	std::uint8_t readProg  (std::uint16_t a)         override { return readProgF(a); }
};

class emu51
{

	std::shared_ptr<address_space> addressSpace;

public:
	std::uint8_t SFR[0x100];                 // internal memory and sfr area 256 bytes

	std::uint8_t & Acc  = SFR[0xe0];          // 8-bit accumlator
	std::uint8_t & DPH  = SFR[0x83];          // high byte of DPTR
	std::uint8_t & DPL  = SFR[0x82];          // low byte of DPTR
	std::uint8_t & B    = SFR[0xf0];          // B register
	std::uint8_t & SP   = SFR[0x81];          // Stack Pointer
	std::uint8_t & PSW  = SFR[0xd0];          // Program Status Word
	std::uint8_t & P0   = SFR[0x80];          // 1st Port
	std::uint8_t & P1   = SFR[0x90];          // 2nd Port
	std::uint8_t & P2   = SFR[0xa0];          // 3rd Port
	std::uint8_t & P3   = SFR[0xb0];          // 4th port
	std::uint8_t & SBUF = SFR[0x99];          // Serial transmission Buffer
	std::uint8_t & IE   = SFR[0xa8];          // Int Enable
	std::uint8_t & IP   = SFR[0xb8];          // Interrupt Priority
	std::uint8_t & SCON = SFR[0x98];          // Serial Control
	std::uint8_t & TH1  = SFR[0x8d];          // Timer1 High
	std::uint8_t & TH0  = SFR[0x8c];          // Timer0 High
	std::uint8_t & TL1  = SFR[0x8b];          // Timer1 Low
	std::uint8_t & TL0  = SFR[0x8a];          // Timer0 Low
	std::uint8_t & TMOD = SFR[0x89];          // Timer Mode
	std::uint8_t & TCON = SFR[0x88];          // Timer Control
	std::uint8_t & PCON = SFR[0x87];          // Power Control
	
	std::uint16_t PC = 0x0000;                 // Progam Counter

private:

	std::uint8_t *R = SFR;                    // Additional 8 Rx Registers
	unsigned long c_time = 0;

	bool check_C(void);
	bool check_AC(void);
	bool check_OV(void);
	void set_C(void);
	void set_AC(void);
	void set_OV(void);
	void clr_C(void);
	void clr_AC(void);
	void clr_OV(void);
	bool check_P(void);
	void set_P(void);
	void clr_P(void);
	void setB(std::uint8_t bit); // (bit) <- 1
	void clrB(std::uint8_t bit); // (bit) <- 0
	bool checkB(std::uint8_t bit); // returns (bit)
	bool check_RS0(void);
	void set_RS0(void);
	void clr_RS0(void);
	bool check_RS1(void);
	void set_RS1(void);
	void clr_RS1(void);

	// 8051 assembler macros

	void _add(std::uint8_t &a, std::uint8_t b);
	void _addc(std::uint8_t &a, std::uint8_t b);
	void _subb(std::uint8_t &a, std::uint8_t b);
	void _mova(std::uint8_t b);
	void _mov_r_a(int regIndex);
	void _mov_a_r(int regIndex);
	void _mov_reg_imm(int regIndex);
	void _mov_reg_direct(int regIndex);
	void _mov_direct_reg(int regIndex);
	void _mov_indirect_a(int indirectIndex);
	void _mov_indirect_direct(int indirectIndex);
	void _mov_indirect_imm(int indirectIndex);
	void _mov_direct_indirect(int indirectIndex);
	void _inc(std::uint8_t &a);
	void _dec(std::uint8_t &a);
	void _add_a(std::uint8_t b);
	void _addc_a(std::uint8_t b);
	void _subb_a(std::uint8_t b);
	void _orl(std::uint8_t b);
	void _anl(std::uint8_t b);
	void _xrl(std::uint8_t b);
	void _xch(std::uint8_t &a);
	std::uint16_t _decode_addr11(std::uint8_t opcode);
	void _push16(std::uint16_t value);
	std::uint16_t _pop16();
	void _op_jcond(bool condition);
	void _op_ljmp();
	void _op_mov_direct_direct();
	void _op_inc_direct();
	void _op_dec_direct();
	void _op_div_ab();
	void _op_mul_ab();
	void _op_rl_a();
	void _op_rr_a();
	void _op_rlc_a();
	void _op_rrc_a();
	void _op_mov_dptr_imm16();
	void _op_lcall();
	void _op_da_a();
	void _op_xchd_a_indirect(int indirectIndex);
	void _op_mov_direct_imm();
	void _op_mov_direct_a();
	void _op_mov_bit_c();
	void _op_push_direct();
	void _op_pop_direct();
	void _op_setb_bit();
	void _op_clr_bit();
	void _op_xrl_direct_a();
	void _op_xrl_direct_imm();
	void _op_orl_direct_a();
	void _op_orl_direct_imm();
	void _op_anl_direct_a();
	void _op_anl_direct_imm();
	void _op_djnz_direct();
	void _op_swap_a();
	void _op_movc_a_dptr();
	void _op_movc_a_pc();
	void _op_mov_c_bit();
	void _op_cpl_bit();
	void _op_cpl_c();
	void _op_orl_c_bit(bool invertBit);
	void _op_anl_c_bit(bool invertBit);
	void _op_ajmp(std::uint8_t opcode);
	void _op_acall(std::uint8_t opcode);
	void _op_djnz_reg(int regIndex);
	void _op_cjne(std::uint8_t lhs, std::uint8_t rhs);
	void _op_bit_branch(bool branchWhenSet, bool clearBitWhenTaken);
	template<unsigned Opcode>
	void exec_opcode(std::uint8_t fetched);
	void exec_opcode_switch(std::uint8_t code);
	int finalize_instruction(std::uint8_t cycles);
	void advance_cycles(std::uint8_t cycles);
	void advance_serial(std::uint8_t cycles, unsigned timer1Overflows);
	void sync_serial_mode();
	using opcode_handler_t = void(*)(emu51*);
	template<std::uint8_t Op>
	static void dispatch_opcode(emu51* cpu) { cpu->exec_opcode<Op>(Op); }
	template<std::size_t... I>
	static constexpr std::array<opcode_handler_t, 256> build_dispatch_table(std::index_sequence<I...>) {
		return { &dispatch_opcode<static_cast<std::uint8_t>(I)>... };
	}
	static const std::array<opcode_handler_t, 256>& opcode_dispatch_table();

	enum class interrupt_source : std::uint8_t {
		ext0 = 0,
		t0 = 1,
		ext1 = 2,
		t1 = 3,
		serial = 4
	};
	std::uint8_t irqStack[8] = { 0 };
	int irqDepth = 0;

	int interrupt_priority(interrupt_source src);
	bool interrupt_pending_enabled(interrupt_source src);
	void clear_interrupt_flag(interrupt_source src);
	std::uint16_t interrupt_vector(interrupt_source src);
	bool can_service_priority(int priority);
	void push_interrupt_source(interrupt_source src);
	void pop_interrupt_source();
	bool try_service_interrupt();

public:
	struct SerialByte {
		std::uint8_t value = 0;
		bool ninth = false;
		bool stop = true;
	};
	static constexpr std::size_t kSerialQueueCapacity = 4096;
	// Direct CPU/SFR writes must use this API to trigger peripherals. Raw SFR
	// and SBUF references remain available for passive inspection/legacy setup.
	std::uint8_t readDirect(std::uint8_t address) const { return SFR[address]; }
	void writeDirect(std::uint8_t address, std::uint8_t value);
	bool queueSerialReceive(std::uint8_t value, bool ninth = false, bool stop = true);
	std::optional<SerialByte> takeSerialTransmit();
	void clearSerialReceiveQueue();
	std::size_t pendingSerialReceiveCount() const { return serialInput_.size() + (serialRx_ ? 1 : 0); }
	unsigned serialMode() const { return SCON >> 6; }
	bool serialTransmitBusy() const { return serialTx_.has_value(); }
	std::uint64_t serialReceiveOverruns() const { return serialRxOverruns_; }
	std::uint64_t serialTransmitOverwrites() const { return serialTxOverwrites_; }
	std::uint64_t serialOutputDrops() const { return serialOutputDrops_; }

private:
	struct SerialFrame {
		SerialByte byte;
		std::uint64_t phase = 0;
		bool flagged = false;
	};
	std::optional<SerialFrame> serialTx_, serialRx_;
	std::optional<SerialByte> serialNextTx_;
	std::deque<SerialByte> serialInput_, serialOutput_;
	unsigned serialMode_ = 0;
	std::uint64_t serialRxOverruns_ = 0, serialTxOverwrites_ = 0, serialOutputDrops_ = 0;

public:
	emu51(bool mixed_mode = false);
	emu51(std::uint8_t * RAM);
	emu51(std::uint8_t * RAM, std::uint8_t * PMEM);
	emu51(std::shared_ptr<address_space> addressSpace) :
		addressSpace(std::move(addressSpace))
	{
		init();
	}

	emu51(address_space& addressSpace) :
		addressSpace(std::shared_ptr<address_space>(&addressSpace, [](address_space*) {}))
	{
		init();
	}

	virtual ~emu51() { }

	void init();
	// Execute one instruction and return machine cycles, including interrupt entry.
	int process();
	int process_table();
	std::uint16_t DPTR() {
		return static_cast<std::uint16_t>(DPL | (DPH << 8));
	}
	std::uint8_t Ri(int i) {
		if (i < 0 || i > 7)
			return 0;
		return R[i];
	}
	void Ri(int i, std::uint8_t value) {
		if (i < 0 || i > 7)
			return;
		R[i] = value;
	}
	void DPTR(std::uint16_t v) {
		DPL = static_cast<std::uint8_t>(v & 0xff);
		DPH = static_cast<std::uint8_t>(v >> 8);
	}
	void writeData(std::uint16_t addr, std::uint8_t data)
	{
		addressSpace->writeData(addr, data);
	}
	void writeProg(std::uint16_t addr, std::uint8_t data)
	{
		addressSpace->writeProg(addr, data);
	}
	void setAddressSpace(std::shared_ptr<address_space> newAddressSpace)
	{
		addressSpace = std::move(newAddressSpace);
	}
	std::uint8_t readData(std::uint16_t addr)
	{
		return addressSpace->readData(addr);
	}
	std::uint8_t readProg(std::uint16_t addr)
	{
		return addressSpace->readProg(addr);
	}
	int loadHex(const char * filename);
	char * disasm(char* buffer);
	void triggerExternalInterrupt0();
	void triggerExternalInterrupt1();
};
