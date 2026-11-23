; SPDX-License-Identifier: GPL-2.0-only
; 7/14/16-segment displays and a 10-LED bar share the same walking bit pattern.
; P1=low 8 segments, P2=next 8, P3.0=16-segment DP. Commons tied low.
; This is a segment/pin test, not a character decoder.
$MOD51
DP BIT 0B0h
org 0
    mov SP,#60h
main:
    mov P1,#0FFh
    mov P2,#0FFh
    setb DP
    lcall delay
    mov 20h,#1
    mov 21h,#0
    clr DP
    mov R4,#16
walk:
    mov P1,20h
    mov P2,21h
    lcall delay
    clr C
    mov A,20h
    rlc A
    mov 20h,A
    mov A,21h
    rlc A
    mov 21h,A
    djnz R4,walk
    sjmp main
delay:
    mov R5,#40
wait_outer:
    mov R6,#250
wait_inner:
    djnz R6,wait_inner
    djnz R5,wait_outer
    ret
end
