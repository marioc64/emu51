; SPDX-License-Identifier: GPL-2.0-only
; A complete scan leaves channel readings in IRAM 20h..27h.
; P1.0..2 address, P1.3 ALE, P1.4 START, P1.5 OE, P1.6 EOC, P2 DATA.
$MOD51
ALE_PIN bit 093h
START_PIN bit 094h
OE_PIN bit 095h
EOC_PIN bit 096h
org 0
    mov P2,#0FFh
    mov P1,#040h          ; release EOC. Hold the three controls low.
sample_all:
    mov R0,#20h
    mov R1,#0
sample_channel:
    mov A,R1
    orl A,#040h
    mov P1,A
    setb ALE_PIN
    clr ALE_PIN
    setb START_PIN
busy_low:
    jb EOC_PIN,busy_low
    clr START_PIN
busy_high:
    jnb EOC_PIN,busy_high
    setb OE_PIN
    mov A,P2
    mov @R0,A
    clr OE_PIN
    inc R0
    inc R1
    cjne R1,#8,sample_channel
    inc 28h
    sjmp sample_all
end
