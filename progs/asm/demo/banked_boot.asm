; SPDX-License-Identifier: GPL-2.0-only
; P1 starts at FFh: boot in physical 32 KiB region 15.
; The next instruction is fetched from region 14, without resetting.
$MOD51
org 0
    mov P1,#0FEh
end
