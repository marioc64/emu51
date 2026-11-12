// SPDX-License-Identifier: GPL-2.0-only
#include "emu51.h"

template<unsigned Opcode>
void emu51::exec_opcode(std::uint8_t fetched)
{
	const std::uint8_t code = Opcode < 256 ? static_cast<std::uint8_t>(Opcode) : fetched;
	switch (code)
	{
	case 0x00:  //nop                      00
		++PC;
		break;
	case 0x74:  // mov a,#(byte)           74 xx
		_mova(readProg(++PC));
		break;
	case 0x75:  // mov (adress),#(byte)    75 xx yy
		_op_mov_direct_imm();
		break;
	case 0x02:  // ljmp 16bit_adres        02 xx yy
		_op_ljmp();
		break;
	case 0x80:  // sjmp offset             80 oo
		++PC;
		PC = static_cast<std::uint16_t>(PC + static_cast<signed char>(readProg(PC)) + 1);
		break;
	case 0x85:  // mov (adress1),(ardess2) 85 yy xx
		_op_mov_direct_direct();
		break;
	case 0xe5:  // mov A,(adress)
		_mova(SFR[readProg(++PC)]);
		break;
	case 0x92:  // mov (bit),C
		_op_mov_bit_c();
		break;
	case 0x04:  // inc a
		_inc(Acc);
		break;
	case 0x05:  // inc (adress)
		_op_inc_direct();
		break;
	case 0xa3:  // inc DPTR
		DPTR(DPTR() + 1);
		++PC;
		break;
	case 0x06: //inc @R0
		_inc(SFR[R[0]]);
		break;
	case 0x07: //inc @R1
		_inc(SFR[R[1]]);
		break;
	case 0x08: //inc R0
		_inc(R[0]);
		break;
	case 0x09: //inc R1
		_inc(R[1]);
		break;
	case 0x0a: //inc R2
		_inc(R[2]);
		break;
	case 0x0b: //inc R3
		_inc(R[3]);
		break;
	case 0x0c: //inc R4
		_inc(R[4]);
		break;
	case 0x0d: //inc R5
		_inc(R[5]);
		break;
	case 0x0e: //inc R6
		_inc(R[6]);
		break;
	case 0x0f: //inc R7
		_inc(R[7]);
		break;
	case 0x14: //dec A
		_dec(Acc);
		break;
	case 0x15: //dec (adress)
		_op_dec_direct();
		break;
	case 0x16: //dec @R0
		_dec(SFR[R[0]]);
		break;
	case 0x17: //dec @R1
		_dec(SFR[R[1]]);
		break;
	case 0x18: // dec R0
		_dec(R[0]);
		break;
	case 0x19: // dec R1
		_dec(R[1]);
		break;
	case 0x1a: // dec R2
		_dec(R[2]);
		break;
	case 0x1b: // dec R3
		_dec(R[3]);
		break;
	case 0x1c: // dec R4
		_dec(R[4]);
		break;
	case 0x1d: // dec R5
		_dec(R[5]);
		break;
	case 0x1e: // dec R6
		_dec(R[6]);
		break;
	case 0x1f: // dec R7
		_dec(R[7]);
		break;
	case 0x84:  // div AB
		_op_div_ab();
		break;
	case 0xa4:  // mul AB
		_op_mul_ab();
		break;
	case 0xa5:  // reserved opcode, emulate as NOP
		++PC;
		break;
	case 0x23:  // rl A
		_op_rl_a();
		break;
	case 0x03:  // rr A
		_op_rr_a();
		break;
	case 0x33:  // rlc A
		_op_rlc_a();
		break;
	case 0x13:  // rrc A
		_op_rrc_a();
		break;
	case 0x90:  // mov DPTR,#(16-bit data)
		_op_mov_dptr_imm16();
		break;
	case 0xf5:  // mov (adress),A
		_op_mov_direct_a();
		break;
		// ADD (full functional)
	case 0x24:   // add A,#(data)
		_add_a(readProg(++PC));
		break;
	case 0x25:  // add A,(adres)
		_add_a(SFR[readProg(++PC)]);
		break;
	case 0x26:  // add A,@R0
		_add_a(SFR[R[0]]);
		break;
	case 0x27:  // add A,@R1
		_add_a(SFR[R[1]]);
		break;
	case 0x28: //add A,R0
		_add_a(R[0]);
		break;
	case 0x29: //add A,R1
		_add_a(R[1]);
		break;
	case 0x2a: //add A,R2
		_add_a(R[2]);
		break;
	case 0x2b: //add A,R3
		_add_a(R[3]);
		break;
	case 0x2c: //add A,R4
		_add_a(R[4]);
		break;
	case 0x2d: //add A,R5
		_add_a(R[5]);
		break;
	case 0x2e: //add A,R6
		_add_a(R[6]);
		break;
	case 0x2f: //add A,R7
		_add_a(R[7]);
		break;
		// ADDC
	case 0x34:  // addc A,#(data)
		_addc_a(readProg(++PC));
		break;
	case 0x35:  // addc A,(adres)
		_addc_a(SFR[readProg(++PC)]);
		break;
	case 0x36:  // addc A,@R0
		_addc_a(SFR[R[0]]);
		break;
	case 0x37:  // addc A,@R1
		_addc_a(SFR[R[1]]);
		break;
	case 0x38: //addc A,R0
		_addc_a(R[0]);
		break;
	case 0x39: //addc A,R1
		_addc_a(R[1]);
		break;
	case 0x3a: //addc A,R2
		_addc_a(R[2]);
		break;
	case 0x3b: //addc A,R3
		_addc_a(R[3]);
		break;
	case 0x3c: //addc A,R4
		_addc_a(R[4]);
		break;
	case 0x3d: //addc A,R5
		_addc_a(R[5]);
		break;
	case 0x3e: //addc A,R6
		_addc_a(R[6]);
		break;
	case 0x3f: //addc A,R7
		_addc_a(R[7]);
		break;
	case 0xe0:  // movx A,@DPTR
		Acc = readData(DPTR());
		++PC;
		break;
	case 0xf0:  // movx @DPTR,A
		writeData(DPTR(), Acc);
		++PC;
		break;
	case 0xc0:  // push (adress)
		_op_push_direct();
		break;
	case 0xd0:  // pop (adress)
		_op_pop_direct();
		break;
	case 0xc3:  // clr C
		clr_C();
		++PC;
		break;
	case 0xd3:  // setb C
		set_C();
		++PC;
		break;
	case 0xd2:  // setb (bit)
		_op_setb_bit();
		break;
	case 0xc2:  // clr (bit)
		_op_clr_bit();
		break;
	case 0x62:  // xrl (adress),A
		_op_xrl_direct_a();
		break;
	case 0x63:  // xrl (adress),#(data)
		_op_xrl_direct_imm();
		break;
	case 0x64:  // xrl A,#(data)
		_xrl(readProg(++PC));
		break;
	case 0x65:  // xrl A,(adress)
		_xrl(SFR[readProg(++PC)]);
		break;
	case 0x66:  // xrl A,@R0
		_xrl(SFR[R[0]]);
		break;
	case 0x67:  // xrl A,@R1
		_xrl(SFR[R[1]]);
		break;
	case 0x68:  // xrl A,R0
		_xrl(R[0]);
		break;
	case 0x69:  // xrl A,R1
		_xrl(R[1]);
		break;
	case 0x6a:  // xrl A,R2
		_xrl(R[2]);
		break;
	case 0x6b:  // xrl A,R3
		_xrl(R[3]);
		break;
	case 0x6c:  // xrl A,R4
		_xrl(R[4]);
		break;
	case 0x6d:  // xrl A,R5
		_xrl(R[5]);
		break;
	case 0x6e:  // xrl A,R6
		_xrl(R[6]);
		break;
	case 0x6f:  // xrl A,R7
		_xrl(R[7]);
		break;
	case 0xc5:  // xch A,(adress)
		_xch(SFR[readProg(++PC)]);
		break;
	case 0xc6:  // xch A,@R0
		_xch(SFR[R[0]]);
		break;
	case 0xc7:  // xch A,@R1
		_xch(SFR[R[1]]);
		break;
	case 0xc8:  // xch A,R0
		_xch(R[0]);
		break;
	case 0xc9:  // xch A,R1
		_xch(R[1]);
		break;
	case 0xca:  // xch A,R2
		_xch(R[2]);
		break;
	case 0xcb:  // xch A,R3
		_xch(R[3]);
		break;
	case 0xcc:  // xch A,R4
		_xch(R[4]);
		break;
	case 0xcd:  // xch A,R5
		_xch(R[5]);
		break;
	case 0xce:  // xch A,R6
		_xch(R[6]);
		break;
	case 0xcf:  // xch A,R7
		_xch(R[7]);
		break;
	case 0x12:  // lcall 16bit_adres        02 xx yy
		_op_lcall();
		break;
	case 0x22: // ret
		PC = _pop16();
		break;
	case 0x70: // jnz (offset)
		_op_jcond(Acc != 0);
		break;
	case 0x60: // jz (offset)
		_op_jcond(Acc == 0);
		break;
	case 0x40:  // jc (offset)
		_op_jcond(check_C());
		break;
	case 0x50:  // jnc (offset)
		_op_jcond(!check_C());
		break;
	case 0xc4:  // swap A
		_op_swap_a();
		break;
	case 0x93:  // movc A,@DPTR+A
		_op_movc_a_dptr();
		break;
	case 0x83:  // movc A,@PC+A
		_op_movc_a_pc();
		break;
	case 0xe6:  // mov A,@R0
		_mova(SFR[R[0]]);
		break;
	case 0xe7:  // mov A,@R1
		_mova(SFR[R[1]]);
		break;
	case 0xf6:  // mov @R0,A
		_mov_indirect_a(0);
		break;
	case 0xf7:  // mov @R1,a
		_mov_indirect_a(1);
		break;
	case 0x78:  // mov R0..R7,#(data)
	case 0x79:
	case 0x7a:
	case 0x7b:
	case 0x7c:
	case 0x7d:
	case 0x7e:
	case 0x7f:
		_mov_reg_imm(code - 0x78);
		break;
	case 0xa8:  // mov R0..R7,(adress)
	case 0xa9:
	case 0xaa:
	case 0xab:
	case 0xac:
	case 0xad:
	case 0xae:
	case 0xaf:
		_mov_reg_direct(code - 0xa8);
		break;
	case 0x88:  // mov (adress),R0..R7
	case 0x89:
	case 0x8a:
	case 0x8b:
	case 0x8c:
	case 0x8d:
	case 0x8e:
	case 0x8f:
		_mov_direct_reg(code - 0x88);
		break;
	case 0xf8:  // mov R0,A
		_mov_r_a(0);
		break;
	case 0xf9:  // mov R1,A
		_mov_r_a(1);
		break;
	case 0xfa:  // mov R2,A
		_mov_r_a(2);
		break;
	case 0xfb:  // mov R3,A
		_mov_r_a(3);
		break;
	case 0xfc:  // mov R4,A
		_mov_r_a(4);
		break;
	case 0xfd:  // mov R5,A
		_mov_r_a(5);
		break;
	case 0xfe:  // mov R6,A
		_mov_r_a(6);
		break;
	case 0xff:  // mov R7,A
		_mov_r_a(7);
		break;
	case 0xe8:  // mov A,R0
		_mov_a_r(0);
		break;
	case 0xe9:  // mov A,R1
		_mov_a_r(1);
		break;
	case 0xea:  // mov A,R2
		_mov_a_r(2);
		break;
	case 0xeb:  // mov A,R3
		_mov_a_r(3);
		break;
	case 0xec:  // mov A,R4
		_mov_a_r(4);
		break;
	case 0xed:  // mov A,R5
		_mov_a_r(5);
		break;
	case 0xee:  // mov A,R6
		_mov_a_r(6);
		break;
	case 0xef:  // mov A,R7
		_mov_a_r(7);
		break;
	case  0xe4: //clr A
		++PC;
		Acc = 0;
		break;
	case 0x01: // ajmp
	case 0x21:
	case 0x41:
	case 0x61:
	case 0x81:
	case 0xa1:
	case 0xc1:
	case 0xe1:
		_op_ajmp(code);
		break;
	case 0x11: // acall
	case 0x31:
	case 0x51:
	case 0x71:
	case 0x91:
	case 0xb1:
	case 0xd1:
	case 0xf1:
		_op_acall(code);
		break;
	case 0xd8: // djnz R0..R7,(offset)
	case 0xd9:
	case 0xda:
	case 0xdb:
	case 0xdc:
	case 0xdd:
	case 0xde:
	case 0xdf:
		_op_djnz_reg(code - 0xd8);
		break;
	case 0xd5: // djnz (adress),(offset)
		_op_djnz_direct();
		break;
	case 0x43: // orl (adress),#(data)
		_op_orl_direct_imm();
		break;
	case 0x44: // orl A,#(data)
		_orl(readProg(++PC));
		break;
	case 0x45: // orl A,(adress)
		_orl(SFR[readProg(++PC)]);
		break;
	case 0x42: // orl (adress),A
		_op_orl_direct_a();
		break;
	case 0x48: // orl A,R0
		_orl(R[0]);
		break;
	case 0x49: // orl A,R1
		_orl(R[1]);
		break;
	case 0x4a: // orl A,R2
		_orl(R[2]);
		break;
	case 0x4b: // orl A,R3
		_orl(R[3]);
		break;
	case 0x4c: // orl A,R4
		_orl(R[4]);
		break;
	case 0x4d: // orl A,R5
		_orl(R[5]);
		break;
	case 0x4e: // orl A,R6
		_orl(R[6]);
		break;
	case 0x4f: // orl A,R7
		_orl(R[7]);
		break;
	case 0x46: // orl A,@R0
		_orl(SFR[R[0]]);
		break;
	case 0x47: // orl A,@R1
		_orl(SFR[R[1]]);
		break;
	case 0x54:  // anl A,#(data)
		_anl(readProg(++PC));
		break;
	case 0x55: // anl A,(adress)
		_anl(SFR[readProg(++PC)]);
		break;
	case 0x52: // anl (adress),A
		_op_anl_direct_a();
		break;
	case 0x53: // anl (adress),#(data)
		_op_anl_direct_imm();
		break;
	case 0x58: // anl A,R0
		_anl(R[0]);
		break;
	case 0x59: // anl A,R1
		_anl(R[1]);
		break;
	case 0x5a: // anl A,R2
		_anl(R[2]);
		break;
	case 0x5b: // anl A,R3
		_anl(R[3]);
		break;
	case 0x5c: // anl A,R4
		_anl(R[4]);
		break;
	case 0x5d: // anl A,R5
		_anl(R[5]);
		break;
	case 0x5e: // anl A,R6
		_anl(R[6]);
		break;
	case 0x5f: // anl A,R7
		_anl(R[7]);
		break;
	case 0x56: // anl A,@R0
		_anl(SFR[R[0]]);
		break;
	case 0x57: // anl A,@R1
		_anl(SFR[R[1]]);
		break;
	case 0xa2: // mov C,(bit)
		_op_mov_c_bit();
		break;
	case 0xb2: // cpl (bit)
		_op_cpl_bit();
		break;
	case 0xb3: // cpl C
		_op_cpl_c();
		break;
	case 0x72: // orl C,(bit)
		_op_orl_c_bit(false);
		break;
	case 0xa0: // orl C,/(bit)
		_op_orl_c_bit(true);
		break;
	case 0x82: // anl C,(bit)
		_op_anl_c_bit(false);
		break;
	case 0xb0: // anl C,/(bit)
		_op_anl_c_bit(true);
		break;
	case 0xf4: // cpl A
		Acc = Acc ^ 0xff;
		++PC;
		break;
	case 0xa6: // mov @R0,(adress)
		_mov_indirect_direct(0);
		break;
	case 0xa7: // mov @R1,(adress)
		_mov_indirect_direct(1);
		break;
	case 0x76: // mov @R0,#(data)
		_mov_indirect_imm(0);
		break;
	case 0x77: // mov @R1,#(data)
		_mov_indirect_imm(1);
		break;
	case 0x86: // mov (adress),@R0
		_mov_direct_indirect(0);
		break;
	case 0x87: // mov (adress),@R1
		_mov_direct_indirect(1);
		break;
	case 0x73: // jmp @A+DPTR
		PC = (std::uint16_t)(Acc + DPTR());
		break;
	case 0xb4: // cjne A,#(byte),offset
		_op_cjne(Acc, readProg(++PC));
		break;
	case 0xb5: // cjne A,(adress),offset
		_op_cjne(Acc, SFR[readProg(++PC)]);
		break;
	case 0xb6: // cjne @R0,#(byte),offset
		_op_cjne(SFR[R[0]], readProg(++PC));
		break;
	case 0xb7: // cjne @R1,#(byte),offset
		_op_cjne(SFR[R[1]], readProg(++PC));
		break;
	case 0xb8: // cjne R0..R7,#(byte),offset
	case 0xb9:
	case 0xba:
	case 0xbb:
	case 0xbc:
	case 0xbd:
	case 0xbe:
	case 0xbf:
		_op_cjne(R[code - 0xb8], readProg(++PC));
		break;
	case 0x32: // reti
		PC = _pop16();
		pop_interrupt_source();
		break;
	case 0x30: //jnb (bit),(offset)
		_op_bit_branch(false, false);
		break;
	case 0x20: //jb (bit),(offset)
		_op_bit_branch(true, false);
		break;
	case 0x10: //jbc (bit),(offset)
		_op_bit_branch(true, true);
		break;
	case 0x94: // subb A,#(data)
		_subb_a(readProg(++PC));
		break;
	case 0x95: // subb A,(adress)
		_subb_a(SFR[readProg(++PC)]);
		break;
	case 0x96: // subb A,@R0
		_subb_a(SFR[R[0]]);
		break;
	case 0x97: // subb A,@R1
		_subb_a(SFR[R[1]]);
		break;
	case 0x98: // subb A,R0
		_subb_a(R[0]);
		break;
	case 0x99: // subb A,R1
		_subb_a(R[1]);
		break;
	case 0x9a: // subb A,R2
		_subb_a(R[2]);
		break;
	case 0x9b: // subb A,R3
		_subb_a(R[3]);
		break;
	case 0x9c: // subb A,R4
		_subb_a(R[4]);
		break;
	case 0x9d: // subb A,R5
		_subb_a(R[5]);
		break;
	case 0x9e: // subb A,R6
		_subb_a(R[6]);
		break;
	case 0x9f: // subb A,R7
		_subb_a(R[7]);
		break;
	case 0xd4:  // da A
		_op_da_a();
		break;
	case 0xd6:  // xchd A,@R0
		_op_xchd_a_indirect(0);
		break;
	case 0xd7:  // xchd A,@R1
		_op_xchd_a_indirect(1);
		break;
	case 0xe2:  // movx A,@R0
		Acc = readData(static_cast<std::uint16_t>(R[0]) | (static_cast<std::uint16_t>(P2) << 8));
		++PC;
		break;
	case 0xe3:  // movx A,@R1
		Acc = readData(static_cast<std::uint16_t>(R[1]) | (static_cast<std::uint16_t>(P2) << 8));
		++PC;
		break;
	case 0xf2:  // movx @R0,A
		writeData(static_cast<std::uint16_t>(R[0]) | (static_cast<std::uint16_t>(P2) << 8), Acc);
		++PC;
		break;
	case 0xf3:  // movx @R1,A
		writeData(static_cast<std::uint16_t>(R[1]) | (static_cast<std::uint16_t>(P2) << 8), Acc);
		++PC;
		break;
	}
}

// 8051 assembler macros

inline void emu51::_add(std::uint8_t &a, std::uint8_t b)
{
	int tmp1, tmp2;
	((0xf & a) + (0xf & b)) > 0xf ? set_AC() : clr_AC();
	tmp1 = ((0x7f & a) + (0x7f & b)) > 0x7f ? 1 : 0;
	(tmp2 = a + b) > 0xff ? (set_C(), tmp1 ? clr_OV() : set_OV()) :
		(clr_C(), tmp1 ? set_OV() : clr_OV());
	a = (std::uint8_t)tmp2 & 0xff;
}

inline void emu51::_addc(std::uint8_t &a, std::uint8_t b)
{
	int tmp1, tmp2, c;
	c = check_C();
	((0xf & a) + (0xf & b) + c) > 0xf ? set_AC() : clr_AC();
	tmp1 = ((0x7f & a) + (0x7f & b) + c) > 0x7f ? 1 : 0;
	(tmp2 = a + b + c) > 0xff ? (set_C(), tmp1 ? clr_OV() : set_OV()) :
		(clr_C(), tmp1 ? set_OV() : clr_OV());
	a = (std::uint8_t)tmp2 & 0xff;
}

inline void emu51::_subb(std::uint8_t &a, std::uint8_t b)
{
	const unsigned c = check_C() ? 1u : 0u;
	const unsigned au = static_cast<unsigned>(a);
	const unsigned bu = static_cast<unsigned>(b);
	const unsigned subtrahend = bu + c;
	const unsigned result = (au - subtrahend) & 0xffu;

	((au & 0x0fu) < ((bu & 0x0fu) + c)) ? set_AC() : clr_AC();
	(au < subtrahend) ? set_C() : clr_C();
	(((au ^ bu) & (au ^ result) & 0x80u) != 0u) ? set_OV() : clr_OV();

	a = static_cast<std::uint8_t>(result);
}

inline void emu51::_mova(std::uint8_t b)
{
	Acc = b;
	++PC;
}

inline void emu51::_mov_r_a(int regIndex)
{
	R[regIndex] = Acc;
	++PC;
}

inline void emu51::_mov_a_r(int regIndex)
{
	Acc = R[regIndex];
	++PC;
}

inline void emu51::_mov_reg_imm(int regIndex)
{
	R[regIndex] = readProg(++PC);
	++PC;
}

inline void emu51::_mov_reg_direct(int regIndex)
{
	R[regIndex] = SFR[readProg(++PC)];
	++PC;
}

inline void emu51::_mov_direct_reg(int regIndex)
{
	SFR[readProg(++PC)] = R[regIndex];
	++PC;
}

inline void emu51::_mov_indirect_a(int indirectIndex)
{
	SFR[R[indirectIndex]] = Acc;
	++PC;
}

inline void emu51::_mov_indirect_direct(int indirectIndex)
{
	SFR[R[indirectIndex]] = SFR[readProg(++PC)];
	++PC;
}

inline void emu51::_mov_indirect_imm(int indirectIndex)
{
	SFR[R[indirectIndex]] = readProg(++PC);
	++PC;
}

inline void emu51::_mov_direct_indirect(int indirectIndex)
{
	SFR[readProg(++PC)] = SFR[R[indirectIndex]];
	++PC;
}

inline void emu51::_inc(std::uint8_t &a)
{
	++a;
	++PC;
}

inline void emu51::_dec(std::uint8_t &a)
{
	--a;
	++PC;
}

inline void emu51::_add_a(std::uint8_t b)
{
	_add(Acc, b);
	++PC;
}

inline void emu51::_addc_a(std::uint8_t b)
{
	_addc(Acc, b);
	++PC;
}

inline void emu51::_subb_a(std::uint8_t b)
{
	_subb(Acc, b);
	++PC;
}

inline void emu51::_orl(std::uint8_t b)
{
	Acc = static_cast<std::uint8_t>(Acc | b);
	++PC;
}

inline void emu51::_anl(std::uint8_t b)
{
	Acc = static_cast<std::uint8_t>(Acc & b);
	++PC;
}

inline void emu51::_xrl(std::uint8_t b)
{
	Acc = static_cast<std::uint8_t>(Acc ^ b);
	++PC;
}

inline void emu51::_xch(std::uint8_t &a)
{
	const std::uint8_t tmp = Acc;
	Acc = a;
	a = tmp;
	++PC;
}

inline std::uint16_t emu51::_decode_addr11(std::uint8_t opcode)
{
	return static_cast<std::uint16_t>(((PC + 2) & 0xf800) |
		((static_cast<std::uint16_t>(opcode) & 0xe0) << 3) |
		readProg(PC + 1));
}

inline void emu51::_push16(std::uint16_t value)
{
	SP = static_cast<std::uint8_t>(SP + 1);
	SFR[SP] = static_cast<std::uint8_t>(value & 0x00ff);
	SP = static_cast<std::uint8_t>(SP + 1);
	SFR[SP] = static_cast<std::uint8_t>(value >> 8);
}

inline std::uint16_t emu51::_pop16()
{
	const std::uint8_t high = SFR[SP];
	SP = static_cast<std::uint8_t>(SP - 1);
	const std::uint8_t low = SFR[SP];
	SP = static_cast<std::uint8_t>(SP - 1);
	return static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8) | low);
}

inline void emu51::_op_jcond(bool condition)
{
	++PC;
	if (condition)
		PC = static_cast<std::uint16_t>(PC + static_cast<signed char>(readProg(PC)) + 1);
	else
		++PC;
}

inline void emu51::_op_ljmp()
{
	++PC;
	PC = static_cast<std::uint16_t>((static_cast<std::uint16_t>(readProg(PC)) << 8) | readProg(PC + 1));
}

inline void emu51::_op_mov_direct_direct()
{
	SFR[readProg(PC + 2)] = SFR[readProg(PC + 1)];
	PC = static_cast<std::uint16_t>(PC + 3);
}

inline void emu51::_op_inc_direct()
{
	++SFR[readProg(++PC)];
	++PC;
}

inline void emu51::_op_dec_direct()
{
	--SFR[readProg(++PC)];
	++PC;
}

inline void emu51::_op_div_ab()
{
	clr_C();
	if (B != 0)
	{
		const std::uint8_t dividend = Acc;
		Acc = static_cast<std::uint8_t>(dividend / B);
		B = static_cast<std::uint8_t>(dividend % B);
		clr_OV();
	}
	else
	{
		set_OV();
	}
	++PC;
}

inline void emu51::_op_mul_ab()
{
	const std::uint16_t product = static_cast<std::uint16_t>(Acc) * static_cast<std::uint16_t>(B);
	clr_C();
	(product > 0xff) ? set_OV() : clr_OV();
	B = static_cast<std::uint8_t>(product >> 8);
	Acc = static_cast<std::uint8_t>(product & 0xff);
	++PC;
}

inline void emu51::_op_rl_a()
{
	const std::uint8_t v = Acc;
	Acc = static_cast<std::uint8_t>((v << 1) | (v >> 7));
	++PC;
}

inline void emu51::_op_rr_a()
{
	const std::uint8_t v = Acc;
	Acc = static_cast<std::uint8_t>((v >> 1) | (v << 7));
	++PC;
}

inline void emu51::_op_rlc_a()
{
	const std::uint8_t v = Acc;
	Acc = static_cast<std::uint8_t>((v << 1) | (check_C() ? 1 : 0));
	(v & 0x80) ? set_C() : clr_C();
	++PC;
}

inline void emu51::_op_rrc_a()
{
	const std::uint8_t v = Acc;
	Acc = static_cast<std::uint8_t>((v >> 1) | ((check_C() ? 1 : 0) << 7));
	(v & 0x01) ? set_C() : clr_C();
	++PC;
}

inline void emu51::_op_mov_dptr_imm16()
{
	DPH = readProg(++PC);
	DPL = readProg(++PC);
	++PC;
}

inline void emu51::_op_lcall()
{
	const std::uint16_t ret = static_cast<std::uint16_t>(PC + 3);
	++PC;
	PC = static_cast<std::uint16_t>((static_cast<std::uint16_t>(readProg(PC)) << 8) | readProg(PC + 1));
	_push16(ret);
}

inline void emu51::_op_da_a()
{
	++PC;
	if ((Acc & 0x0f) > 9 || check_AC()) {
		const unsigned corrected = static_cast<unsigned>(Acc) + 0x06;
		if (corrected > 0xff)
			set_C();
		Acc = static_cast<std::uint8_t>(corrected);
	}
	if ((Acc >> 4) > 9 || check_C())
	{
		if (static_cast<int>(Acc) + 0x60 > 0xff)
			set_C();
		Acc = static_cast<std::uint8_t>(Acc + 0x60);
	}
}

inline void emu51::_op_xchd_a_indirect(int indirectIndex)
{
	++PC;
	const std::uint8_t tmp = SFR[R[indirectIndex]];
	SFR[R[indirectIndex]] = static_cast<std::uint8_t>((tmp & 0xf0) | (Acc & 0x0f));
	Acc = static_cast<std::uint8_t>((Acc & 0xf0) | (tmp & 0x0f));
}

inline void emu51::_op_mov_direct_imm()
{
	++PC;
	const std::uint8_t addr = readProg(PC);
	const std::uint8_t imm = readProg(PC + 1);
	SFR[addr] = imm;
	PC = static_cast<std::uint16_t>(PC + 2);
}

inline void emu51::_op_mov_direct_a()
{
	SFR[readProg(++PC)] = Acc;
	++PC;
}

inline void emu51::_op_mov_bit_c()
{
	const std::uint8_t bit = readProg(++PC);
	check_C() ? setB(bit) : clrB(bit);
	++PC;
}

inline void emu51::_op_push_direct()
{
	SP = static_cast<std::uint8_t>(SP + 1);
	SFR[SP] = SFR[readProg(++PC)];
	++PC;
}

inline void emu51::_op_pop_direct()
{
	SFR[readProg(++PC)] = SFR[SP];
	SP = static_cast<std::uint8_t>(SP - 1);
	++PC;
}

inline void emu51::_op_setb_bit()
{
	setB(readProg(++PC));
	++PC;
}

inline void emu51::_op_clr_bit()
{
	clrB(readProg(++PC));
	++PC;
}

inline void emu51::_op_xrl_direct_a()
{
	const std::uint8_t addr = readProg(++PC);
	SFR[addr] = static_cast<std::uint8_t>(SFR[addr] ^ Acc);
	++PC;
}

inline void emu51::_op_xrl_direct_imm()
{
	const std::uint8_t addr = readProg(++PC);
	SFR[addr] = static_cast<std::uint8_t>(SFR[addr] ^ readProg(PC + 1));
	PC = static_cast<std::uint16_t>(PC + 2);
}

inline void emu51::_op_orl_direct_a()
{
	const std::uint8_t addr = readProg(++PC);
	SFR[addr] = static_cast<std::uint8_t>(SFR[addr] | Acc);
	++PC;
}

inline void emu51::_op_orl_direct_imm()
{
	const std::uint8_t addr = readProg(++PC);
	SFR[addr] = static_cast<std::uint8_t>(SFR[addr] | readProg(PC + 1));
	PC = static_cast<std::uint16_t>(PC + 2);
}

inline void emu51::_op_anl_direct_a()
{
	const std::uint8_t addr = readProg(++PC);
	SFR[addr] = static_cast<std::uint8_t>(SFR[addr] & Acc);
	++PC;
}

inline void emu51::_op_anl_direct_imm()
{
	const std::uint8_t addr = readProg(++PC);
	SFR[addr] = static_cast<std::uint8_t>(SFR[addr] & readProg(PC + 1));
	PC = static_cast<std::uint16_t>(PC + 2);
}

inline void emu51::_op_djnz_direct()
{
	const std::uint8_t addr = readProg(++PC);
	++PC;
	--SFR[addr];
	if (SFR[addr] != 0)
		PC = static_cast<std::uint16_t>(PC + static_cast<signed char>(readProg(PC)));
	++PC;
}

inline void emu51::_op_swap_a()
{
	const std::uint8_t low = static_cast<std::uint8_t>((Acc & 0x0f) << 4);
	const std::uint8_t high = static_cast<std::uint8_t>((Acc & 0xf0) >> 4);
	Acc = static_cast<std::uint8_t>(low | high);
	++PC;
}

inline void emu51::_op_movc_a_dptr()
{
	Acc = readProg(static_cast<std::uint16_t>(Acc + DPTR()));
	++PC;
}

inline void emu51::_op_movc_a_pc()
{
	++PC;
	Acc = readProg(static_cast<std::uint16_t>(Acc + PC));
}

inline void emu51::_op_mov_c_bit()
{
	checkB(readProg(++PC)) ? set_C() : clr_C();
	++PC;
}

inline void emu51::_op_cpl_bit()
{
	const std::uint8_t bit = readProg(++PC);
	checkB(bit) ? clrB(bit) : setB(bit);
	++PC;
}

inline void emu51::_op_cpl_c()
{
	check_C() ? clr_C() : set_C();
	++PC;
}

inline void emu51::_op_orl_c_bit(bool invertBit)
{
	const bool bit = checkB(readProg(++PC));
	const bool rhs = invertBit ? !bit : bit;
	(check_C() || rhs) ? set_C() : clr_C();
	++PC;
}

inline void emu51::_op_anl_c_bit(bool invertBit)
{
	const bool bit = checkB(readProg(++PC));
	const bool rhs = invertBit ? !bit : bit;
	(check_C() && rhs) ? set_C() : clr_C();
	++PC;
}

inline void emu51::_op_ajmp(std::uint8_t opcode)
{
	PC = _decode_addr11(opcode);
}

inline void emu51::_op_acall(std::uint8_t opcode)
{
	const std::uint16_t ret = static_cast<std::uint16_t>(PC + 2);
	_push16(ret);
	PC = _decode_addr11(opcode);
}

inline void emu51::_op_djnz_reg(int regIndex)
{
	R[regIndex]--;
	++PC;
	if (R[regIndex] != 0)
		PC = static_cast<std::uint16_t>(PC + static_cast<signed char>(readProg(PC)));
	++PC;
}

inline void emu51::_op_cjne(std::uint8_t lhs, std::uint8_t rhs)
{
	(lhs < rhs) ? set_C() : clr_C();
	++PC;
	if (lhs != rhs)
		PC = static_cast<std::uint16_t>(PC + static_cast<signed char>(readProg(PC)));
	++PC;
}

inline void emu51::_op_bit_branch(bool branchWhenSet, bool clearBitWhenTaken)
{
	const std::uint8_t bit = readProg(++PC);
	++PC;

	const bool condition = checkB(bit);
	const bool take = branchWhenSet ? condition : !condition;
	if (take)
	{
		if (clearBitWhenTaken)
			clrB(bit);
		PC = static_cast<std::uint16_t>(PC + static_cast<signed char>(readProg(PC)));
	}
	++PC;
}

void emu51::exec_opcode_switch(std::uint8_t code)
{
    exec_opcode<256>(code);
}

const std::array<emu51::opcode_handler_t, 256>& emu51::opcode_dispatch_table()
{
    static constexpr auto table = build_dispatch_table(std::make_index_sequence<256>{});
    return table;
}
