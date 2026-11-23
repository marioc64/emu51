; SPDX-License-Identifier: GPL-2.0-only
; Two chained 74HC595s and one 74HC165. Open shift_registers.emu51, then Run.
; Change the eight switches in Live. P2 and the first 595 show the input byte.
; The second 595 shows A5h. P1_0=SER, P1_1=SRCLK, P1_2=RCLK.
; P3_0=165 CLK, P3_1=LOAD_n, P3_2=QH. IRAM 20h = sampled switch byte.
$MOD51
P1_0 BIT 090h
P1_1 BIT 091h
P1_2 BIT 092h
P3_0 BIT 0B0h
P3_1 BIT 0B1h
P3_2 BIT 0B2h
org 0
    mov SP,#60h
    mov P1,#0
    mov P3,#0FFh
main:
    clr P3_0
    clr P3_1
    nop
    setb P3_1
    clr A
    mov R6,#8
read_bit:
    mov C,P3_2
    rlc A
    setb P3_0
    clr P3_0
    djnz R6,read_bit
    mov 20h,A
    mov P2,A
    mov A,#0A5h
    lcall send
    mov A,20h
    lcall send
    setb P1_2
    clr P1_2
    sjmp main
send:
    mov R7,#8
send_bit:
    rlc A
    mov P1_0,C
    setb P1_1
    clr P1_1
    djnz R7,send_bit
    ret
end
