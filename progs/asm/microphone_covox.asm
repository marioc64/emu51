; SPDX-License-Identifier: GPL-2.0-only
; Microphone -> ADC0804 -> P0 -> circular echo buffer -> P1 -> Covox.
; Use the microphone preset/project: 3 MHz crystal, Cycle exact, 8051 bank 0.
; P3.4 = /CS, P3.5 = /WR, P3.6 = /RD, P3.2 = /INTR.
; Each output = (input >> 1) + (old buffer byte >> 1), with logical shifts.
; IRAM 30h = dry input, 31h = mixed output, 32h = buffer initialized flag.
; DPTR = circular pointer; R4/R5 = exclusive end address; R7 = half input.
; Silence is unsigned 80h. Initialize the delay line to silence, not zero.
$MOD51
ECHO_BASE EQU 08000h
BUFFER_SIZE EQU 4096     ; Delay in samples: change this (1..32767 bytes).
ECHO_END EQU ECHO_BASE+BUFFER_SIZE
org 0
    mov P0,#0FFh        ; Release all ADC input pins.
    mov P1,#080h        ; Keep Covox centered during initialization.
    mov 032h,#0
    setb 0B6h           ; /RD high: ADC data bus disabled.
    setb 0B5h           ; /WR high.
    clr 0B4h            ; /CS low: select ADC.
    mov DPTR,#ECHO_END
    mov R4,DPL          ; Capture both bytes without HIGH/LOW directives.
    mov R5,DPH
    mov DPTR,#ECHO_BASE
initialize_buffer:
    mov A,#080h
    movx @DPTR,A
    inc DPTR
    mov A,DPH
    cjne A,05h,initialize_buffer
    mov A,DPL
    cjne A,04h,initialize_buffer
    mov DPTR,#ECHO_BASE
    mov 032h,#1         ; Ready: the entire delay line contains silence.
convert:
    clr 0B5h
    setb 0B5h           ; Rising /WR samples microphone voltage.
wait_adc:
    jb 0B2h,wait_adc
    clr 0B6h           ; /RD low: ADC drives P0.
    mov A,P0
    mov 030h,A         ; Preserve dry byte for inspection.
    clr C
    rrc A              ; Logical input >> 1; RR would rotate bit 0 into bit 7.
    mov R7,A
    movx A,@DPTR       ; Read the delayed mixed output at this pointer.
    clr C
    rrc A              ; Logical delayed byte >> 1.
    add A,R7           ; Sum is 0..254, so it cannot overflow.
    movx @DPTR,A       ; Feed the mixed result back into the same slot.
    mov P1,A           ; Send the same mixed byte to Covox.
    mov 031h,A
    setb 0B6h           ; Release ADC bus.
    inc DPTR
    mov A,DPH
    cjne A,05h,convert
    mov A,DPL
    cjne A,04h,convert
    mov DPTR,#ECHO_BASE ; Wrap after BUFFER_SIZE samples (any size, not just powers of two).
    sjmp convert
end
