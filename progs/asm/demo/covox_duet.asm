; SPDX-License-Identifier: GPL-2.0-only
; Two interrupt-driven square-wave voices -> P1 -> Covox -> audio output.
; 8051, 24.576 MHz crystal, Cycle exact. No ADC or microphone is used.
; T0 mode 2: 2.048 MHz / 256 = 8000 samples/s, high priority.
; T1 mode 1: nominal 100 Hz music tick (small software-reload overhead).
; Song is in code memory: duration ticks, MIDI pitch 1, gate 1, pitch 2, gate 2.
; Pitch range C3..B5 (MIDI 48..83). Gates: 0=off, nonzero=on.
; A zero duration loops the song. Every record retriggers both phases.
; Each gated voice contributes +/-63; mix = 128 + voice1 + voice2 (2..254).
; DDS frequency = step * 8000 / 65536; no waveform lookup in the audio ISR.
;
; Internal RAM player status (hex addresses, little-endian words):
; 30 playing; 31..32 music ticks; 33 current row (1-based); 34 Covox byte;
; 35..36 sample count; 37 loop count (all counters wrap).
; 38..39 voice 1 phase; 3A..3B step; 3C pitch; 3D gate; 3E signed sample.
; 40..41 voice 2 phase; 42..43 step; 44 pitch; 45 gate; 46 signed sample.
; 48..49 next song record; 4A ticks left; 4C..53 pending note data.
; Bank 0=main, bank 1=T0, bank 2=T1; stack 60h..7Fh (SP starts at 5Fh).
; T1 calculates pitches first, then briefly masks ET0 for an atomic two-voice
; update. T0 otherwise preempts T1 and never touches its DPTR/register bank.
$MOD51
PLAYING EQU 030h
MUSIC_TICKS_LO EQU 031h
MUSIC_TICKS_HI EQU 032h
SONG_ROW EQU 033h
LAST_SAMPLE EQU 034h
SAMPLES_LO EQU 035h
SAMPLES_HI EQU 036h
SONG_LOOPS EQU 037h
V1_PHASE_LO EQU 038h
V1_PHASE_HI EQU 039h
V1_STEP_LO EQU 03Ah
V1_STEP_HI EQU 03Bh
V1_PITCH EQU 03Ch
V1_GATE EQU 03Dh
V1_SAMPLE EQU 03Eh
V2_PHASE_LO EQU 040h
V2_PHASE_HI EQU 041h
V2_STEP_LO EQU 042h
V2_STEP_HI EQU 043h
V2_PITCH EQU 044h
V2_GATE EQU 045h
V2_SAMPLE EQU 046h
SONG_PTR_LO EQU 048h
SONG_PTR_HI EQU 049h
TICKS_LEFT EQU 04Ah
NEXT_PITCH1 EQU 04Ch
NEXT_GATE1 EQU 04Dh
NEXT_STEP1_LO EQU 04Eh
NEXT_STEP1_HI EQU 04Fh
NEXT_PITCH2 EQU 050h
NEXT_GATE2 EQU 051h
NEXT_STEP2_LO EQU 052h
NEXT_STEP2_HI EQU 053h

org 0000h
    ljmp start
org 000Bh
    ljmp audio_isr
org 001Bh
    ljmp music_isr
org 0100h
start:
    mov IE,#0
    mov SP,#05Fh
    mov P1,#080h
    mov R0,#030h
    mov R7,#36
clear_status:
    mov @R0,#0
    inc R0
    djnz R7,clear_status
    mov LAST_SAMPLE,#080h
    mov DPTR,#song
    mov SONG_PTR_LO,DPL
    mov SONG_PTR_HI,DPH
    mov PLAYING,#1
    mov TMOD,#012h       ; T1: 16-bit; T0: 8-bit auto reload.
    mov TH0,#0
    mov TL0,#0
    mov TH1,#0B0h        ; 65536-20480: nominal 10 ms at 2.048 MHz.
    mov TL1,#0
    mov IP,#002h        ; PT0 high, PT1 low: audio can interrupt sequencing.
    mov TCON,#050h       ; TR1 + TR0, clear old flags.
    mov IE,#08Ah         ; EA + ET1 + ET0.
idle:
    sjmp idle

; Sample generator: bounded work, no song reads or division.
audio_isr:
    push ACC
    push PSW
    mov PSW,#008h       ; Register bank 1.
    mov A,V1_PHASE_LO
    add A,V1_STEP_LO
    mov V1_PHASE_LO,A
    mov A,V1_PHASE_HI
    addc A,V1_STEP_HI
    mov V1_PHASE_HI,A
    mov V1_SAMPLE,#0
    mov A,V1_GATE
    jz voice1_done
    mov A,V1_PHASE_HI
    jb 0E7h,voice1_negative
    mov V1_SAMPLE,#63
    sjmp voice1_done
voice1_negative:
    mov V1_SAMPLE,#0C1h  ; -63, signed two's complement contribution.
voice1_done:
    mov A,V2_PHASE_LO
    add A,V2_STEP_LO
    mov V2_PHASE_LO,A
    mov A,V2_PHASE_HI
    addc A,V2_STEP_HI
    mov V2_PHASE_HI,A
    mov V2_SAMPLE,#0
    mov A,V2_GATE
    jz voice2_done
    mov A,V2_PHASE_HI
    jb 0E7h,voice2_negative
    mov V2_SAMPLE,#63
    sjmp voice2_done
voice2_negative:
    mov V2_SAMPLE,#0C1h
voice2_done:
    mov A,#080h
    add A,V1_SAMPLE
    add A,V2_SAMPLE
covox_write:
    mov P1,A            ; Both off = 80h, one voice = 41h/BFh, two = 02h/80h/FEh.
    mov LAST_SAMPLE,A
    inc SAMPLES_LO
    mov A,SAMPLES_LO
    jnz sample_count_done
    inc SAMPLES_HI
sample_count_done:
    pop PSW
    pop ACC
    reti

; Independent control interrupt: duration and pitch/gate events in code memory.
music_isr:
    push ACC
    push PSW
    push DPL
    push DPH
    mov PSW,#010h       ; Register bank 2; bank 1 remains private to audio.
    clr TR1
    mov TH1,#0B0h
    mov TL1,#0
    setb TR1
    inc MUSIC_TICKS_LO
    mov A,MUSIC_TICKS_LO
    jnz tick_count_done
    inc MUSIC_TICKS_HI
tick_count_done:
    mov A,TICKS_LEFT
    jz next_record
    dec TICKS_LEFT
    mov A,TICKS_LEFT
    jnz music_done
next_record:
    mov R7,SONG_ROW     ; Next row index stays private until both voices commit.
    mov DPL,SONG_PTR_LO
    mov DPH,SONG_PTR_HI
    clr A
    movc A,@A+DPTR
    jnz record_ready
    mov DPTR,#song      ; Zero duration is the loop marker.
    inc SONG_LOOPS
    mov R7,#0
    clr A
    movc A,@A+DPTR
record_ready:
    mov TICKS_LEFT,A
    inc DPTR
    clr A
    movc A,@A+DPTR
    mov NEXT_PITCH1,A
    inc DPTR
    clr A
    movc A,@A+DPTR
    mov NEXT_GATE1,A
    inc DPTR
    clr A
    movc A,@A+DPTR
    mov NEXT_PITCH2,A
    inc DPTR
    clr A
    movc A,@A+DPTR
    mov NEXT_GATE2,A
    inc DPTR
    mov SONG_PTR_LO,DPL
    mov SONG_PTR_HI,DPH
    mov A,NEXT_PITCH1
    lcall pitch_step
    mov NEXT_STEP1_LO,R4
    mov NEXT_STEP1_HI,R5
    jnc pitch1_valid
    mov NEXT_GATE1,#0
pitch1_valid:
    mov A,NEXT_PITCH2
    lcall pitch_step
    mov NEXT_STEP2_LO,R4
    mov NEXT_STEP2_HI,R5
    jnc pitch2_valid
    mov NEXT_GATE2,#0
pitch2_valid:
    clr ET0             ; Publish a whole chord; never expose half a step word.
    mov V1_PITCH,NEXT_PITCH1
    mov V1_GATE,NEXT_GATE1
    mov V1_STEP_LO,NEXT_STEP1_LO
    mov V1_STEP_HI,NEXT_STEP1_HI
    mov V2_PITCH,NEXT_PITCH2
    mov V2_GATE,NEXT_GATE2
    mov V2_STEP_LO,NEXT_STEP2_LO
    mov V2_STEP_HI,NEXT_STEP2_HI
    mov V1_PHASE_LO,#0
    mov V1_PHASE_HI,#0
    mov V2_PHASE_LO,#0
    mov V2_PHASE_HI,#0
    mov SONG_ROW,R7
    inc SONG_ROW
notes_published:
    setb ET0
music_done:
    pop DPH
    pop DPL
    pop PSW
    pop ACC
    reti

; MIDI note -> 16-bit DDS step in R4:R5 (low:high), CY=1 for invalid pitch.
pitch_step:
    clr C
    subb A,#48
    jc invalid_pitch
    cjne A,#36,pitch_range
pitch_range:
    jnc invalid_pitch
    add A,ACC
    mov R2,A
    mov DPTR,#pitch_table
    movc A,@A+DPTR
    mov R4,A
    inc DPTR
    mov A,R2
    movc A,@A+DPTR
    mov R5,A
    clr C
    ret
invalid_pitch:
    mov R4,#0
    mov R5,#0
    setb C
    ret

; Little-endian round(440 * 2^((MIDI-69)/12) * 65536 / 8000).
pitch_table:
    db 48,4        ; MIDI 48
    db 111,4        ; MIDI 49
    db 179,4        ; MIDI 50
    db 250,4        ; MIDI 51
    db 70,5        ; MIDI 52
    db 150,5        ; MIDI 53
    db 235,5        ; MIDI 54
    db 70,6        ; MIDI 55
    db 165,6        ; MIDI 56
    db 10,7        ; MIDI 57
    db 117,7        ; MIDI 58
    db 231,7        ; MIDI 59
    db 95,8        ; MIDI 60
    db 223,8        ; MIDI 61
    db 102,9        ; MIDI 62
    db 245,9        ; MIDI 63
    db 140,10        ; MIDI 64
    db 45,11        ; MIDI 65
    db 215,11        ; MIDI 66
    db 139,12        ; MIDI 67
    db 74,13        ; MIDI 68
    db 20,14        ; MIDI 69
    db 235,14        ; MIDI 70
    db 206,15        ; MIDI 71
    db 190,16        ; MIDI 72
    db 189,17        ; MIDI 73
    db 203,18        ; MIDI 74
    db 234,19        ; MIDI 75
    db 25,21        ; MIDI 76
    db 90,22        ; MIDI 77
    db 174,23        ; MIDI 78
    db 22,25        ; MIDI 79
    db 148,26        ; MIDI 80
    db 41,28        ; MIDI 81
    db 214,29        ; MIDI 82
    db 156,31        ; MIDI 83

; An original repeating dyad exercise. Duration is in nominal 10 ms ticks.
;                ticks, pitch1, gate1, pitch2, gate2
song:
    db 25,60,1,52,1      ; C4 + E3
    db 25,64,1,55,1      ; E4 + G3
    db 25,67,1,60,1      ; G4 + C4
    db 25,72,1,64,1      ; C5 + E4
    db 25,71,1,62,1      ; B4 + D4
    db 25,67,1,59,1      ; G4 + B3
    db 25,64,1,55,1      ; E4 + G3
    db 10,60,0,48,0      ; Both gates off (silence).
    db 25,65,1,57,1      ; F4 + A3
    db 25,69,1,60,1      ; A4 + C4
    db 25,72,1,65,1      ; C5 + F4
    db 25,69,1,60,1      ; A4 + C4
    db 25,67,1,59,1      ; G4 + B3
    db 25,64,1,55,1      ; E4 + G3
    db 50,60,1,52,1      ; Hold C4 + E3.
    db 25,60,0,48,1      ; Voice 2 alone.
    db 25,72,1,48,0      ; Voice 1 alone.
    db 50,60,0,48,0      ; Rest, then loop.
song_end:
    db 0
end
