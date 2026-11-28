; SPDX-License-Identifier: GPL-2.0-only
; MCP23017 scans a 4x4 keypad. A0..3 are rows, B0..3 are columns.
; INTB is mirrored and wired to INT0. B4 lights while a key is detected.
$MOD51
SDA bit 090h
SCL bit 091h
org 0
    ljmp start
org 3
    ljmp key_interrupt
org 30h
start:
    mov SP,#60h
    mov 23h,#0
    mov R0,#00h
    mov R1,#0F0h
    lcall register_write     ; IODIRA: four row outputs
    mov R0,#01h
    mov R1,#0EFh
    lcall register_write     ; IODIRB: B4 output
    mov R0,#0Ah
    mov R1,#040h
    lcall register_write     ; IOCON: mirrored interrupt
    mov R0,#0Dh
    mov R1,#00Fh
    lcall register_write     ; GPPUB: four column pull-ups
    mov R0,#05h
    mov R1,#00Fh
    lcall register_write     ; GPINTENB: interrupt on column change
    mov 23h,#055h
    mov IE,#081h             ; level-sensitive INT0
idle:
    sjmp idle

key_interrupt:
    mov R0,#05h
    mov R1,#0
    lcall register_write     ; disable interrupts during scan
    mov R2,#0FEh
    mov R3,#24h
    mov R4,#4
    mov R5,#0
scan_row:
    mov R0,#014h
    mov R1,02h
    lcall register_write
    lcall read_columns
    cpl A
    anl A,#0Fh
    mov R0,03h
    mov @R0,A
    orl A,R5
    mov R5,A
    inc R3
    mov A,R2
    rl A
    mov R2,A
    djnz R4,scan_row

    mov R0,#015h
    mov R1,#0
    mov A,R5
    jz update_led
    mov R1,#010h
update_led:
    lcall register_write
    mov R0,#014h
    mov R1,#0
    lcall register_write
    lcall read_columns      ; update/clear baseline before reenabling interrupts
    mov R0,#05h
    mov R1,#00Fh
    lcall register_write
    reti

read_columns:
    lcall i2c_start
    mov A,#040h
    lcall send_checked
    mov A,#013h
    lcall send_checked
    lcall i2c_start
    mov A,#041h
    lcall send_checked
    setb C
    lcall i2c_read
    lcall i2c_stop
    ret

register_write:
    lcall i2c_start
    mov A,#040h
    lcall send_checked
    mov A,R0
    lcall send_checked
    mov A,R1
    lcall send_checked
    ljmp i2c_stop
send_checked:
    lcall i2c_send
    jc failed
    ret
failed:
    mov 23h,#0EEh
    lcall i2c_stop
stopped:
    sjmp stopped
$INCLUDE(serial_i2c.inc)
end
