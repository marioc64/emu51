; SPDX-License-Identifier: GPL-2.0-only
; A decoder selects RAM or an 8255 from the same XDATA bus.
; 8255 registers alias throughout 8000h-FFFFh because only A0/A1 reach it.
$MOD51
org 0
    mov DPTR,#8003h
    mov A,#80h
    movx @DPTR,A
    mov DPTR,#8001h
    mov A,#1
    movx @DPTR,A
loop:
    sjmp loop
end
