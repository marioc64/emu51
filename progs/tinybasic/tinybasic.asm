; SPDX-License-Identifier: GPL-2.0-only
; Standalone UART startup for the public-domain PJRC Tiny BASIC V2.3 image.
; Select CPU 8052, 11.0592 MHz, then open Serial Terminal and Run.
; UART: 9600 baud, 8 data bits, 1 stop bit. Input lines end with CR.
; Tiny BASIC uses upper internal RAM for its stack and external RAM 2000h..2FFFh.

$mod52
        org 0
        ljmp startup
        org 0030h
startup:
        mov IE,#0
        mov PCON,#0
        mov TMOD,#20h
        mov TH1,#0fdh
        mov TL1,#0fdh
        mov SCON,#52h
        setb TR1
        ljmp 8000h

; The included image retains its upstream public-domain status.
$include(../../third_party/tb51/image.inc)
        end
