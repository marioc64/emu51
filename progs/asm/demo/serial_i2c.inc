; SPDX-License-Identifier: GPL-2.0-only
; Open-drain master on P1.0 (SDA) and P1.1 (SCL).
i2c_start:
    setb SDA
    setb SCL
    nop
    clr SDA
    clr SCL
    ret

i2c_stop:
    clr SDA
    setb SCL
    nop
    setb SDA
    ret

; A contains the byte. Carry is set on NACK.
i2c_send:
    mov R6,#8
i2c_send_bit:
    rlc A
    mov SDA,C
    nop
    setb SCL
    nop
    clr SCL
    djnz R6,i2c_send_bit
    setb SDA
    setb SCL
    nop
    mov C,SDA
    clr SCL
    ret

; Carry selects ACK (0) or NACK (1). Returns the received byte in A.
i2c_read:
    push PSW
    setb SDA
    clr A
    mov R6,#8
i2c_read_bit:
    setb SCL
    nop
    mov C,SDA
    rlc A
    clr SCL
    djnz R6,i2c_read_bit
    pop PSW
    mov SDA,C
    setb SCL
    nop
    clr SCL
    setb SDA
    ret
