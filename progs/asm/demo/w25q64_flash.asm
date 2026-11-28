; SPDX-License-Identifier: GPL-2.0-only
; Reads JEDEC ID, erases sector 0, programs "EMU51" and verifies the readback.
$MOD51
SCK bit 090h
MOSI bit 091h
MISO bit 092h
FLASH_CS bit 093h
org 0
    ljmp start
org 30h
start:
    mov SP,#60h
    mov 23h,#0
    setb MISO
    setb FLASH_CS
    clr SCK
    clr FLASH_CS
    mov A,#09Fh
    lcall exchange
    clr A
    lcall exchange
    mov 20h,A
    clr A
    lcall exchange
    mov 21h,A
    clr A
    lcall exchange
    mov 22h,A
    setb FLASH_CS
    lcall write_enable
    clr FLASH_CS
    mov A,#020h
    lcall command_address
    setb FLASH_CS
    lcall wait_ready
    lcall write_enable
    clr FLASH_CS
    mov A,#02h
    lcall command_address
    mov DPTR,#message
    mov R4,#5
program:
    clr A
    movc A,@A+DPTR
    inc DPTR
    lcall exchange
    djnz R4,program
    setb FLASH_CS
    lcall wait_ready
    clr FLASH_CS
    mov A,#03h
    lcall command_address
    mov DPTR,#message
    mov R0,#28h
    mov R4,#5
verify:
    clr A
    lcall exchange
    mov @R0,A
    mov R5,A
    clr A
    movc A,@A+DPTR
    cjne A,05h,failed
    inc DPTR
    inc R0
    djnz R4,verify
    setb FLASH_CS
    mov 23h,#055h
finished:
    sjmp finished
failed:
    setb FLASH_CS
    mov 23h,#0EEh
stopped:
    sjmp stopped
write_enable:
    clr FLASH_CS
    mov A,#6
    lcall exchange
    setb FLASH_CS
    ret
wait_ready:
    clr FLASH_CS
    mov A,#5
    lcall exchange
poll:
    ; Space status reads so the analyzer also captures the program and readback.
    mov R7,#180
poll_delay:
    mov R5,#30
poll_inner:
    djnz R5,poll_inner
    djnz R7,poll_delay
    clr A
    lcall exchange
    rrc A
    jc poll
    setb FLASH_CS
    ret
command_address:
    lcall exchange
    clr A
    lcall exchange
    clr A
    lcall exchange
    clr A
    sjmp exchange
exchange:
    mov R6,#8
bit_loop:
    rlc A
    mov MOSI,C
    setb SCK
    mov C,MISO
    clr SCK
    djnz R6,bit_loop
    rlc A
    ret
message:
    db 045h,04Dh,055h,035h,031h
end
