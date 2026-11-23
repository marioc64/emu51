; SPDX-License-Identifier: GPL-2.0-only
; Open seven_segment_clock.emu51. 8051 at 12 MHz; P1=a..g,DP; P2.0..3=digit commons.
; Common-cathode display. P3.0/.1 drive the colon during digit 1.
; Timer 0 mode 2 autoreloads every 250 us; four digits refresh every 1 ms.
; IRAM 30..33 = MMSS BCD digits, 34=scan index, 35..36=ms (0..499),
; 37=quarter-ms counter, 38=half-second count, 39=colon state.
$MOD51
org 0
    ljmp start
org 0Bh
    ljmp scan
org 30h
start:
    mov SP,#60h
    mov P1,#0
    mov P2,#0FFh
    mov P3,#0
    mov R0,#30h
    mov R7,#10
    clr A
clear_ram:
    mov @R0,A
    inc R0
    djnz R7,clear_ram
    mov 39h,#1
    mov TMOD,#2
    mov TH0,#6
    mov TL0,#6
    setb ET0
    setb EA
    setb TR0
idle:
    sjmp idle
scan:
    push ACC
    push PSW
    push DPL
    push DPH
    mov PSW,#8
    mov P2,#0FFh
    mov A,34h
    add A,#30h
    mov R0,A
    mov A,@R0
    mov DPTR,#font
    movc A,@A+DPTR
    mov P1,A
    mov A,39h
    jz colon_off
    mov P3,#3
    sjmp select_digit
colon_off:
    mov P3,#0
select_digit:
    mov DPTR,#commons
    mov A,34h
    movc A,@A+DPTR
    mov P2,A
    inc 34h
    anl 34h,#3
    inc 37h
    mov A,37h
    cjne A,#4,scan_done
    mov 37h,#0
    inc 35h
    mov A,35h
    jnz check_half
    inc 36h
check_half:
    mov A,36h
    cjne A,#1,scan_done
    mov A,35h
    cjne A,#0F4h,scan_done
    mov 35h,#0
    mov 36h,#0
    xrl 39h,#1
    inc 38h
    mov A,38h
    cjne A,#2,scan_done
    mov 38h,#0
    lcall second
scan_done:
    pop DPH
    pop DPL
    pop PSW
    pop ACC
    reti
second:
    inc 33h
    mov A,33h
    cjne A,#10,second_done
    mov 33h,#0
    inc 32h
    mov A,32h
    cjne A,#6,second_done
    mov 32h,#0
    inc 31h
    mov A,31h
    cjne A,#10,second_done
    mov 31h,#0
    inc 30h
    mov A,30h
    cjne A,#6,second_done
    mov 30h,#0
second_done:
    ret
font: db 03Fh,006h,05Bh,04Fh,066h,06Dh,07Dh,007h,07Fh,06Fh
commons: db 0FEh,0FDh,0FBh,0F7h
end
