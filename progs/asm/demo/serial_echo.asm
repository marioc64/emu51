; SPDX-License-Identifier: GPL-2.0-only
; Classic 8051 mode 1 UART, Timer 1 mode 2, interrupt-driven echo.
; At an 11.0592 MHz crystal this is 9600 baud, 8 data bits, one stop bit.
; Open Execution > Serial terminal, Run, then send text or hexadecimal bytes.
; The program sends '>' when ready and echoes incoming bytes, including NUL.

$mod51

        org 0000h
        ljmp start
        org 0023h
        ljmp serial_isr
        org 0030h

start:  mov TMOD,#20h
        mov TH1,#0fdh
        mov TL1,#0fdh
        mov SCON,#50h
        setb TR1
        mov SBUF,#3eh
ready:  jnb TI,ready
        clr TI
        setb ES
        setb EA
idle:   sjmp idle

serial_isr:
        push ACC
        push PSW
        jnb TI,check_rx
        clr TI
check_rx:
        jnb RI,done
        mov A,SBUF
        clr RI
        mov SBUF,A
done:   pop PSW
        pop ACC
        reti
        end
