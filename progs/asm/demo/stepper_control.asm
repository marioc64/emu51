; SPDX-License-Identifier: GPL-2.0-only
; Four phases on P1.0..P1.3 via ULN2003. Close switches to reverse and use half steps.
$MOD51
REVERSE bit 0B2h
HALF_STEP bit 0B3h
org 0
    mov P1,#0
    setb REVERSE
    setb HALF_STEP
    mov R0,#0
    mov DPTR,#sequence
next_phase:
    mov A,R0
    movc A,@A+DPTR
    mov P1,A
    mov R6,#40
outer_delay:
    mov R7,#250
inner_delay:
    djnz R7,inner_delay
    djnz R6,outer_delay
    mov R1,#1
    jnb HALF_STEP,choose_direction
    mov R1,#2
    ; When switching from half-step, align to a two-coil state.
    mov A,R0
    jb ACC_BIT0,choose_direction
    mov R1,#1
choose_direction:
    mov A,R0
    jb REVERSE,forward
    clr C
    subb A,R1
    sjmp select_phase
forward:
    add A,R1
select_phase:
    anl A,#7
    mov R0,A
    sjmp next_phase
sequence:
    db 1,3,2,6,4,12,8,9
ACC_BIT0 bit 0E0h
end
