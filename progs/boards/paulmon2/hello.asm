; SPDX-License-Identifier: GPL-2.0-only
; Download hello.hex over the PAULMON2 terminal, then jump to 2000h.
$MOD52
LED BIT 090h
org 02000h
    clr LED
    lcall 0048h          ; PAULMON2 newline
    mov DPTR,#message
    lcall 0038h          ; PAULMON2 print null-terminated CODE string
    lcall 0048h
    lcall 0032h          ; wait for one byte from the terminal
    setb LED
    ret
message:
    db 72,101,108,108,111,32,102,114,111,109,32,82,65,77,46,32,83,101,110,100,32,97,32,107,101,121,32,116,111,32,114,101,116,117,114,110,46,0 ; Hello from RAM. Send a key to return.
end
