; SPDX-License-Identifier: GPL-2.0-only
; Open eeprom_counter.emu51 from a writable folder. P1_0=SDA, P1_1=SCL, P1_2=WP.
; Reads byte 2Ah, increments it, programs it, ACK-polls, then verifies it.
; Erased FFh becomes 00h. Reset increments again; the image survives app restarts.
; IRAM 20h=previous, 21h=new, 22h=readback, 23h=error. P2 shows readback.
$MOD51
P1_0 BIT 090h
P1_1 BIT 091h
P1_2 BIT 092h
org 0
    ljmp start
org 30h
start:
    mov SP,#60h
    mov 23h,#0
    mov P1,#0FFh
    clr P1_2
    lcall read_value
    mov 20h,A
    inc A
    mov 21h,A
    lcall i2c_start
    mov A,#0A0h
    lcall send
    jc error
    mov A,#2Ah
    lcall send
    jc error
    mov A,21h
    lcall send
    jc error
    lcall i2c_stop
poll:
    lcall i2c_start
    mov A,#0A0h
    lcall send
    push PSW
    lcall i2c_stop
    pop PSW
    jc poll
    lcall read_value
    mov 22h,A
    mov P2,A
    cjne A,21h,error
idle:
    sjmp idle
error:
    mov 23h,#1
    mov P2,#0EEh
    sjmp idle
read_value:
    lcall i2c_start
    mov A,#0A0h
    lcall send
    jc error
    mov A,#2Ah
    lcall send
    jc error
    lcall i2c_start
    mov A,#0A1h
    lcall send
    jc error
    lcall receive
    push ACC
    lcall i2c_stop
    pop ACC
    ret
i2c_start:
    setb P1_0
    lcall delay
    setb P1_1
    lcall delay
    clr P1_0
    lcall delay
    clr P1_1
    ret
i2c_stop:
    clr P1_0
    lcall delay
    setb P1_1
    lcall delay
    setb P1_0
    lcall delay
    ret
send:
    mov R6,#8
send_bit:
    rlc A
    mov P1_0,C
    lcall delay
    setb P1_1
    lcall delay
    clr P1_1
    djnz R6,send_bit
    setb P1_0
    lcall delay
    setb P1_1
    lcall delay
    mov C,P1_0
    clr P1_1
    ret
receive:
    setb P1_0
    clr A
    mov R6,#8
read_bit:
    setb P1_1
    lcall delay
    mov C,P1_0
    rlc A
    clr P1_1
    lcall delay
    djnz R6,read_bit
    setb P1_0
    setb P1_1
    lcall delay
    clr P1_1
    ret
delay:
    mov R7,#5
wait:
    djnz R7,wait
    ret
end
