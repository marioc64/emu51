; SPDX-License-Identifier: GPL-2.0-only
; HT16K33 at I2C 70h -> 8 x 14-segment display. Message: EMU51 IO.
; P1.0=SDA, P1.1=SCL; INT_n -> P3.2. Live key inputs are read to IRAM 20h..25h.
; IRAM 26h counts completed key-RAM reads; command A2h enables active-low INT.
$MOD51
SDA BIT 090h
SCL BIT 091h
KEY_INT BIT 0B2h
org 0
    mov SP,#60h
    mov P1,#0FFh
    mov P3,#0FFh
    mov 26h,#0
    mov A,#21h
    lcall command
    mov A,#81h
    lcall command
    mov A,#0EFh
    lcall command
    mov A,#0A2h
    lcall command
    lcall start
    mov A,#0E0h
    lcall send
    clr A
    lcall send
    mov DPTR,#message
    mov R4,#16
write_ram:
    clr A
    movc A,@A+DPTR
    lcall send
    inc DPTR
    djnz R4,write_ram
    lcall stop
main:
    jb KEY_INT,main
    lcall start
    mov A,#0E0h
    lcall send
    mov A,#40h
    lcall send
    lcall start
    mov A,#0E1h
    lcall send
    mov R0,#20h
    mov R4,#6
read_keys:
    lcall receive
    mov @R0,A
    inc R0
    djnz R4,read_keys
    lcall stop
    inc 26h
    sjmp main
command:
    push ACC
    lcall start
    mov A,#0E0h
    lcall send
    pop ACC
    lcall send
    lcall stop
    ret
start:
    setb SDA
    lcall delay
    setb SCL
    lcall delay
    clr SDA
    lcall delay
    clr SCL
    ret
stop:
    clr SDA
    lcall delay
    setb SCL
    lcall delay
    setb SDA
    lcall delay
    ret
send:
    mov R6,#8
send_bit:
    rlc A
    mov SDA,C
    lcall delay
    setb SCL
    lcall delay
    clr SCL
    djnz R6,send_bit
    setb SDA
    lcall delay
    setb SCL
    lcall delay
    clr SCL
    ret
receive:
    setb SDA
    clr A
    mov R6,#8
read_bit:
    setb SCL
    lcall delay
    mov C,SDA
    rlc A
    clr SCL
    lcall delay
    djnz R6,read_bit
    push ACC
    mov A,R4
    cjne A,#1,ack
    setb SDA
    sjmp ack_clock
ack:
    clr SDA
ack_clock:
    lcall delay
    setb SCL
    lcall delay
    clr SCL
    setb SDA
    pop ACC
    ret
delay:
    mov R7,#5
wait:
    djnz R7,wait
    ret
; 14-segment masks: A..F,G1,G2,H,I,J,K,L,M,DP.
message: db 0F9h,0,036h,005h,03Eh,0,0EDh,0,006h,0,0,0,009h,012h,03Fh,0
end
