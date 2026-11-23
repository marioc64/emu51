; SPDX-License-Identifier: GPL-2.0-only
; TM1637 four-digit preview, colon mapped from DP of digit 1.
; P1.0=DIO, P1.1=CLK. Address-less, LSB-first protocol (not I2C).
; Display 12:3n; IRAM 20h=counter, 21h=key code (FFh when no key).
$MOD51
DIO BIT 090h
CLK BIT 091h
org 0
    mov SP,#60h
    mov P1,#0FFh
    mov 20h,#0
    mov A,#40h
    lcall command
    mov A,#8Fh
    lcall command
main:
    lcall start
    mov A,#0C0h
    lcall send
    mov A,#06h
    lcall send
    mov A,#0DBh
    lcall send
    mov A,#04Fh
    lcall send
    mov DPTR,#font
    mov A,20h
    movc A,@A+DPTR
    lcall send
    lcall stop
    lcall start
    mov A,#42h
    lcall send
    lcall receive
    mov 21h,A
    lcall stop
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
command:
    push ACC
    lcall start
    pop ACC
    lcall send
    lcall stop
    ret
start:
    setb DIO
    lcall delay
    setb CLK
    lcall delay
    clr DIO
    lcall delay
    clr CLK
    ret
stop:
    clr DIO
    lcall delay
    setb CLK
    lcall delay
    setb DIO
    lcall delay
    ret
send:
    mov R4,#8
send_bit:
    rrc A
    mov DIO,C
    lcall delay
    setb CLK
    lcall delay
    clr CLK
    djnz R4,send_bit
    setb DIO
    lcall delay
    setb CLK
    lcall delay
    clr CLK
    ret
receive:
    setb DIO
    clr A
    mov R4,#8
read_bit:
    setb CLK
    lcall delay
    mov C,DIO
    rrc A
    clr CLK
    lcall delay
    djnz R4,read_bit
    ret
delay:
    mov R7,#5
wait:
    djnz R7,wait
    ret
font: db 03Fh,006h,05Bh,04Fh,066h,06Dh,07Dh,007h,07Fh,06Fh
end
