; SPDX-License-Identifier: GPL-2.0-only
; MOVX writes a byte that MOVC reads back from the same physical RAM.
$MOD51
org 0
    mov DPTR,#0100h
    mov A,#55h
    movx @DPTR,A
    clr A
    movc A,@A+DPTR
loop:
    sjmp loop
end
