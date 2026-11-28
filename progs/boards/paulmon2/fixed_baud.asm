; SPDX-License-Identifier: GPL-2.0-only
; PAULMON2 type-249 initialization program, using the upstream fix_baud protocol.
; The emu51 terminal sends UART bytes. The initialization header bypasses pin-level
; auto-baud detection without changing the monitor image.
$MOD52
org 03F00h
    db 0A5h,0E5h,0E0h,0A5h
    db 249,255,0,0
    db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    db 255,255,255,255
    db 70,105,120,101,100,32,98,97,117,100,32,40,57,54,48,48,41,0 ; Fixed baud (9600)
org 03F40h
    mov 07Bh,#0FAh
    mov 07Ah,#0AFh
    mov 079h,#036h
    mov 078h,#0E7h
    ret
end
