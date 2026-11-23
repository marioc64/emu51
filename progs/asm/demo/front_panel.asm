; SPDX-License-Identifier: GPL-2.0-only
; Open front_panel.emu51. 8051 at 12 MHz. Controls/display are in Peripherals > Live.
; P1 -> LCD data, P3_0=RS, P3_1=E, RW tied low.
; P2_0..3 -> keypad rows, P2_4..7 <- columns.
; P3_2 <- pushbutton, P3_3 <- switch; P3_7 -> active-low LED.
; IRAM 20h = last key character. LED lights when either contact is closed.
$MOD51
P2_0 BIT 0A0h
P2_4 BIT 0A4h
P3_0 BIT 0B0h
P3_1 BIT 0B1h
P3_2 BIT 0B2h
P3_3 BIT 0B3h
P3_7 BIT 0B7h
org 0
    ljmp start
org 30h
start:
    mov SP,#60h
    mov P1,#0FFh
    mov P2,#0FFh
    mov P3,#0FFh
    clr P3_1
    clr P3_0
    lcall long_delay
    mov A,#38h
    lcall command
    mov A,#0Ch
    lcall command
    mov A,#01h
    lcall command
    lcall long_delay
    mov DPTR,#title
    lcall print
    mov A,#0C0h
    lcall command
    mov DPTR,#line2
    lcall print
main:
    mov C,P3_2
    anl C,P3_3
    mov P3_7,C
    lcall scan
    mov 20h,A
    push ACC
    mov A,#0C5h
    lcall command
    pop ACC
    lcall character
    mov A,#0CDh
    lcall command
    mov A,#30h
    jb P3_2,not_pressed
    mov A,#31h
not_pressed:
    lcall character
    sjmp main
scan:
    mov R4,#0FEh
    mov R5,#0
row:
    mov A,R4
    mov P2,A
    nop
    mov A,P2
    swap A
    anl A,#0Fh
    cjne A,#0Fh,found_row
    mov A,R4
    rl A
    orl A,#0F0h
    mov R4,A
    mov A,R5
    add A,#4
    mov R5,A
    cjne R5,#16,row
    mov A,#2Dh
    ret
found_row:
    mov R6,#0
column:
    rrc A
    jnc found_key
    inc R6
    sjmp column
found_key:
    mov A,R5
    add A,R6
    mov DPTR,#keys
    movc A,@A+DPTR
    ret
print:
    clr A
    movc A,@A+DPTR
    jz printed
    lcall character
    inc DPTR
    sjmp print
printed:
    ret
command:
    clr P3_0
    sjmp lcd_byte
character:
    setb P3_0
lcd_byte:
    mov P1,A
    setb P3_1
    nop
    clr P3_1
    lcall short_delay
    ret
short_delay:
    mov R7,#25
short_loop:
    djnz R7,short_loop
    ret
long_delay:
    mov R3,#40
long_loop:
    lcall short_delay
    djnz R3,long_loop
    ret
title: db 065h,06Dh,075h,035h,031h,020h,049h,02Fh,04Fh,020h,070h,061h,06Eh,065h,06Ch,0 ; emu51 I/O panel
line2: db 04Bh,065h,079h,03Ah,020h,02Dh,020h,020h,042h,074h,06Eh,03Ah,020h,030h,0 ; Key: -  Btn: 0
keys: db 031h,032h,033h,041h,034h,035h,036h,042h,037h,038h,039h,043h,02Ah,030h,023h,044h ; 123A456B789C*0#D
end
