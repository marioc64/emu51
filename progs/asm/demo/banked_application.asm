; SPDX-License-Identifier: GPL-2.0-only
; Logical address 3 remains address 3; mapping chooses physical ROM region 14.
$MOD51
org 3
    mov A,#42h
    mov DPTR,#8000h
    movx @DPTR,A
loop:
    sjmp loop
end
