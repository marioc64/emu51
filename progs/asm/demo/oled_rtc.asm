; SPDX-License-Identifier: GPL-2.0-only
; SSD1306 and DS1307 share I2C. RTC BCD seconds/minutes/hours go to 28h..2Ah.
$MOD51
SDA bit 090h
SCL bit 091h
org 0
    ljmp start
org 30h
start:
    mov SP,#60h
    mov 23h,#0
    lcall i2c_start
    mov A,#078h
    lcall send_checked
    clr A
    lcall send_checked
    mov DPTR,#setup
    mov R4,#setup_end-setup
    lcall send_table
    lcall i2c_stop

    lcall i2c_start
    mov A,#078h
    lcall send_checked
    mov A,#040h
    lcall send_checked
    mov DPTR,#title
    mov R4,#title_end-title
    lcall send_table
    mov R5,#(128-(title_end-title))
clear_title:
    clr A
    lcall send_checked
    djnz R5,clear_title

    ; Plot a rising/falling trace in page 1 and a baseline in page 2.
    mov R5,#128
    mov R3,#0
wave:
    mov A,R3
    mov DPTR,#plot
    movc A,@A+DPTR
    lcall send_checked
    inc R3
    anl 03h,#0Fh
    djnz R5,wave
    mov R5,#128
baseline:
    mov A,#080h
    lcall send_checked
    djnz R5,baseline
    lcall i2c_stop

    lcall i2c_start
    mov A,#078h
    lcall send_checked
    clr A
    lcall send_checked
    mov DPTR,#scroll
    mov R4,#scroll_end-scroll
    lcall send_table
    lcall i2c_stop
    mov A,#055h
    mov 23h,A

read_clock:
    lcall i2c_start
    mov A,#0D0h
    lcall send_checked
    clr A
    lcall send_checked
    lcall i2c_start
    mov A,#0D1h
    lcall send_checked
    clr C
    lcall i2c_read
    mov 28h,A
    clr C
    lcall i2c_read
    mov 29h,A
    setb C
    lcall i2c_read
    mov 2Ah,A
    lcall i2c_stop
    mov R4,#50
wait_clock:
    mov R5,#200
wait_inner:
    djnz R5,wait_inner
    djnz R4,wait_clock
    sjmp read_clock

send_table:
    clr A
    movc A,@A+DPTR
    inc DPTR
    lcall send_checked
    djnz R4,send_table
    ret
send_checked:
    lcall i2c_send
    jc failed
    ret
failed:
    mov 23h,#0EEh
    lcall i2c_stop
stopped:
    sjmp stopped

setup:
    db 020h,0,021h,0,127,022h,0,7,08Dh,014h,0AFh
setup_end:
title:
    db 07Fh,049h,049h,049h,041h,0 ; E
    db 07Fh,002h,00Ch,002h,07Fh,0 ; M
    db 03Fh,040h,040h,040h,03Fh,0 ; U
    db 027h,045h,045h,045h,039h,0 ; 5
    db 000h,042h,07Fh,040h,000h,0 ; 1
title_end:
plot:
    db 1,2,4,8,16,32,64,128,128,64,32,16,8,4,2,1
scroll:
    db 027h,0,1,7,1,0,0FFh,02Fh
scroll_end:
$INCLUDE(serial_i2c.inc)
end
