; SPDX-License-Identifier: GPL-2.0-only
; Original eight-chord demo, AY-3-8910 at 2 MHz, CPU at 12 MHz.
; Open ay_chords.emu51. In Live: List audio devices > Open audio devices > Run.
; XDATA 9000h=register select, 9001h=data. Three voices; C uses the envelope.
; Timer 0 sequences chords at about 5 Hz. The AY generates samples independently.
; IRAM 30h=ticks until next chord, 31h=next row, 32h=rows played.
$MOD51
org 0
    ljmp start
org 0Bh
    ljmp music
org 30h
start:
    mov SP,#60h
    mov 30h,#1
    mov 31h,#0
    mov 32h,#0
    mov B,#7
    mov A,#38h
    lcall ay_write
    mov B,#8
    mov A,#13
    lcall ay_write
    mov B,#9
    mov A,#11
    lcall ay_write
    mov B,#10
    mov A,#16
    lcall ay_write
    mov B,#11
    mov A,#128
    lcall ay_write
    mov B,#12
    clr A
    lcall ay_write
    mov TMOD,#1
    mov TH0,#0D8h
    mov TL0,#0F0h
    setb ET0
    setb EA
    setb TR0
idle:
    sjmp idle
music:
    push ACC
    push PSW
    push B
    push DPL
    push DPH
    mov PSW,#8
    mov TH0,#0D8h
    mov TL0,#0F0h
    djnz 30h,music_done
    mov 30h,#20
    mov A,31h
    mov B,#6
    mul AB
    mov DPTR,#notes
    add A,DPL
    mov DPL,A
    clr A
    addc A,DPH
    mov DPH,A
    mov R6,#0
note_byte:
    clr A
    movc A,@A+DPTR
    mov B,R6
    lcall ay_write
    inc DPTR
    inc R6
    cjne R6,#6,note_byte
    mov B,#13
    mov A,#9
    lcall ay_write
    inc 32h
    inc 31h
    mov A,31h
    anl A,#7
    mov 31h,A
music_done:
    pop DPH
    pop DPL
    pop B
    pop PSW
    pop ACC
    reti
ay_write:
    push DPL
    push DPH
    mov DPTR,#9000h
    xch A,B
    movx @DPTR,A
    inc DPTR
    mov A,B
    movx @DPTR,A
    pop DPH
    pop DPL
    ret
notes:
    db 0DEh,1,7Bh,1,3Fh,1
    db 66h,1,1Ch,1,0EFh,0
    db 3Fh,1,0FDh,0,0D5h,0
    db 0EFh,0,0BEh,0,9Fh,0
    db 1Ch,1,0EFh,0,0BEh,0
    db 66h,1,1Ch,1,0EFh,0
    db 3Fh,1,0FDh,0,0D5h,0
    db 0DEh,1,7Bh,1,3Fh,1
end
