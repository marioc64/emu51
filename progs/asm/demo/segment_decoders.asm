; SPDX-License-Identifier: GPL-2.0-only
; CD4511 -> common-cathode digit and 74LS47 -> common-anode digit.
; P1.0..3=BCD for both. P3.0=LE, P3.1/.2=4511 LT/BI,
; P3.3/.4/.5=LS47 LT/BI/RBI. All controls inactive; LE=0 transparent.
$MOD51
org 0
    mov SP,#60h
    mov P3,#0FEh
    mov 20h,#0
main:
    mov A,20h
    mov P1,A
    mov R5,#100
wait_outer:
    mov R6,#250
wait_inner:
    djnz R6,wait_inner
    djnz R5,wait_outer
    inc 20h
    mov A,20h
    cjne A,#10,main
    mov 20h,#0
    sjmp main
end
