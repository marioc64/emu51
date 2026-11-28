; SPDX-License-Identifier: GPL-2.0-only
; Save CODE/XDATA 2000h..20FFh to EEPROM 0100h..01FFh.
; EEPROM 0000h holds a commit marker, 0001h holds the byte sum.
$MOD52
SDA bit 092h
SCL bit 093h
SLOT equ 02000h
MARKER equ 0A5h
SUM_SAVE equ 020h

org 03B00h
save_slot:
    push 06h
    mov R2,#0
    mov R3,#0
    mov R4,#0
    lcall begin_write
    jc save_failed
    clr A
    lcall i2c_send             ; Invalidate the old slot before changing its data.
    jc save_failed
    lcall write_cycle
    jc save_failed
    lcall begin_read
    jc save_failed
    setb C
    lcall i2c_read
    lcall i2c_stop
    jnz save_failed            ; Also catches write protection.
    mov DPTR,#SLOT
    mov R2,#1
save_page:
    mov R3,DPL
    lcall begin_write
    jc save_failed
    mov R0,#64
save_byte:
    movx A,@DPTR
    add A,R4
    mov R4,A
    movx A,@DPTR
    lcall i2c_send
    jc save_failed
    inc DPTR
    djnz R0,save_byte
    lcall write_cycle
    jc save_failed
    mov A,DPL
    jnz save_page
    mov SUM_SAVE,R4
    lcall calculate_sum
    jc save_failed
    mov A,R4
    cjne A,SUM_SAVE,save_failed
    mov R2,#0
    mov R3,#0
    lcall begin_write
    jc save_failed
    mov A,#MARKER
    lcall i2c_send
    jc save_failed
    mov A,R4
    lcall i2c_send
    jc save_failed
    lcall write_cycle
    jc save_failed
    mov DPTR,#saved_message
    sjmp report_save
save_failed:
    lcall i2c_stop
    mov DPTR,#error_message
report_save:
    lcall 0048h
    lcall 0038h
    lcall 0048h
    pop 06h
    ret

restore_slot:
    push 06h
    mov R2,#0
    mov R3,#0
    lcall begin_read
    jc restore_done
    clr C
    lcall i2c_read
    mov R1,A
    setb C
    lcall i2c_read
    mov SUM_SAVE,A
    lcall i2c_stop
    mov A,R1
    cjne A,#MARKER,restore_done
    lcall calculate_sum
    jc restore_done
    mov A,R4
    cjne A,SUM_SAVE,restore_done
    lcall begin_slot_read
    jc restore_done
    mov DPTR,#SLOT
    mov R0,#0
restore_byte:
    lcall read_slot_byte
    movx @DPTR,A
    inc DPTR
    djnz R0,restore_byte
    lcall i2c_stop
    mov DPTR,#restored_message
    lcall 0038h
    lcall 0048h
restore_done:
    lcall i2c_stop
    pop 06h
    ret

calculate_sum:
    lcall begin_slot_read
    jc sum_done
    mov R0,#0
    mov R4,#0
sum_byte:
    lcall read_slot_byte
    add A,R4
    mov R4,A
    djnz R0,sum_byte
    lcall i2c_stop
    clr C
sum_done:
    ret

read_slot_byte:
    cjne R0,#1,read_with_ack
    setb C
    ljmp i2c_read
read_with_ack:
    clr C
    ljmp i2c_read

begin_slot_read:
    mov R2,#1
    mov R3,#0
begin_read:
    lcall begin_write
    jc transfer_address_done
    lcall i2c_start
    mov A,#0A1h
    ljmp i2c_send

begin_write:
    lcall i2c_start
    mov A,#0A0h
    lcall i2c_send
    jc transfer_address_done
    mov A,R2
    lcall i2c_send
    jc transfer_address_done
    mov A,R3
    ljmp i2c_send
transfer_address_done:
    ret

write_cycle:
    lcall i2c_stop
    mov R5,#160
busy:
    lcall i2c_start
    mov A,#0A0h
    lcall i2c_send
    lcall i2c_stop
    jnc write_ready
    djnz R5,busy
write_ready:
    ret

saved_message:
    db 83,108,111,116,32,115,97,118,101,100,46,0 ; Slot saved.
error_message:
    db 69,69,80,82,79,77,32,119,114,105,116,101,32,102,97,105,108,101,100,46,0 ; EEPROM write failed.
restored_message:
    db 83,108,111,116,32,114,101,115,116,111,114,101,100,46,0 ; Slot restored.

$INCLUDE(../../asm/demo/serial_i2c.inc)

org 03D00h
    db 0A5h,0E5h,0E0h,0A5h,254,66,0,0
    db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    db 255,255,255,255
    db 83,97,118,101,32,69,69,80,82,79,77,32,115,108,111,116,0 ; Save EEPROM slot
org 03D40h
    ljmp save_slot

org 03E00h
    db 0A5h,0E5h,0E0h,0A5h,253,255,0,0
    db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    db 255,255,255,255
    db 76,111,97,100,32,69,69,80,82,79,77,32,115,108,111,116,0 ; Load EEPROM slot
org 03E40h
    ljmp restore_slot
end
