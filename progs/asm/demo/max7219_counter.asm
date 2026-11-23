; SPDX-License-Identifier: GPL-2.0-only
; Chained MAX7219 + MAX7221. P1.0=DIN, P1.1=CLK, P1.2=LOAD/CS.
; Both chips share clock/select; first DOUT feeds second DIN. Code-B decode.
; First display starts 12345678, second 87654321; digit 0 becomes a counter.
$MOD51
DIN BIT 090h
CLOCK BIT 091h
LOAD BIT 092h
org 0
    mov SP,#60h
    mov P1,#4
    mov B,#9
    mov R4,#0FFh
    mov R5,#0FFh
    lcall write_pair
    mov B,#10
    mov R4,#15
    mov R5,#7
    lcall write_pair
    mov B,#11
    mov R4,#7
    mov R5,#7
    lcall write_pair
    mov B,#12
    mov R4,#1
    mov R5,#1
    lcall write_pair
    mov R0,#1
initial:
    mov B,R0
    mov A,R0
    mov R4,A
    mov A,#9
    clr C
    subb A,R0
    mov R5,A
    lcall write_pair
    inc R0
    cjne R0,#9,initial
    mov 20h,#0
main:
    mov B,#1
    mov A,20h
    mov R4,A
    mov A,#9
    clr C
    subb A,R4
    mov R5,A
    lcall write_pair
    mov R3,#100
wait_outer:
    mov R6,#250
wait_inner:
    djnz R6,wait_inner
    djnz R3,wait_outer
    inc 20h
    mov A,20h
    cjne A,#10,main
    mov 20h,#0
    sjmp main
write_pair:
    clr LOAD
    mov A,B
    lcall shift_byte
    mov A,R5
    lcall shift_byte
    mov A,B
    lcall shift_byte
    mov A,R4
    lcall shift_byte
    setb LOAD
    ret
shift_byte:
    mov R7,#8
shift_bit:
    rlc A
    mov DIN,C
    setb CLOCK
    clr CLOCK
    djnz R7,shift_bit
    ret
end
