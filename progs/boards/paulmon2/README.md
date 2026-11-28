<!-- SPDX-License-Identifier: GPL-2.0-only -->
# PAULMON2 trainer

Open [paulmon2.emu51](paulmon2.emu51) with **File → Open project**, then
select **Run**. The serial terminal opens with the project. After the monitor's
startup delay, it prints a welcome message and the prompt
`PAULMON2 Loc:2000 >`. This project uses PJRC’s 8 KiB RAM configuration and
includes the firmware needed to boot. You can copy this whole directory to
work on the project elsewhere.

## Load and run a program

1. Send `D` with the terminal input **Ending** set to **None**. Wait for the
   monitor’s full transfer prompt.
2. Choose **Send file…** and select [hello.hex](hello.hex). The project uses
   **CR** file endings and a 20 ms delay between records. Wait for the
   download summary and the monitor prompt.
3. Send `J`. After the monitor displays the current address, send `2000`.
   The monitor accepts an address as soon as it receives four hex digits.
4. The program lights the P1.0 LED in **Peripherals → Live** and prints
   `Hello from RAM. Send a key to return.` Send any single character to turn
   off the LED and return to the monitor.

Use `?` for help, `H` for a memory dump, and `M` to list recognized program
headers and the included extra commands. See the
[PAULMON2 manual](https://www.pjrc.com/tech/8051/pm2_docs/) for command details.

To edit the example, assemble [hello.asm](hello.asm) to Intel HEX. Keep the
origin at 2000h for the same jump address.

```sh
emu51_asm hello.asm -o hello.hex
```

## Circuit and boot

| Address range | Connection |
| --- | --- |
| 0000h–1FFFh | Monitor ROM, CODE only |
| 2000h–3EFFh | Shared CODE/XDATA program RAM |
| 3F00h–3FFFh | Last page of the same RAM, reserved for the baud initializer |
| 4000h–FFFFh | Unselected |

The project uses a classic 8052 at 11.0592 MHz. An address decoder selects
the ROM or RAM. A second decoder combines active-low PSEN and RD for RAM
reads, so programs written by MOVX can be fetched as instructions.
The P1.0 LED is an addition for the emu51 example. This configuration models
the RAM trainer. PJRC’s separate parallel flash and expansion board
configuration is outside this project.

The terminal supplies UART bytes at the baud rate configured by firmware.
[fixed_baud.asm](fixed_baud.asm) provides a type-249 initialization header
at 3F00h, using the [upstream fixed-baud example](https://www.pjrc.com/tech/8051/fix_baud.asm)
as a reference. The monitor finds it before entering auto-baud and configures
9600 baud. The published monitor image bytes are preserved.

CPU reset preserves shared RAM. **Power cycle** in the assembly editor’s
**Project files** area clears downloaded programs and restores the ROM and
baud initializer from their load mappings.

## Save and restore a program with 24LC256

Open [paulmon2_eeprom.emu51](paulmon2_eeprom.emu51) for the EEPROM variant.
Run it from a writable checkout. When moving the example, keep the board
directory and `progs/asm/demo/serial_i2c.inc` in the same relative positions.

1. Upload [hello.hex](hello.hex) using the steps above.
2. Send `B` at the monitor prompt and wait for **Slot saved.** The command
   saves RAM 2000h–20FFh to a single slot in the 24LC256. Pause to flush the
   committed image to `program_slot.bin`.
3. Select **Power cycle** in **Project files**, then Run. The monitor’s startup
   extension validates and restores the saved page to RAM and prints
   **Slot restored.** Use `J` and `2000` to run the restored program.

The extension at 3B00h–3EFFh is preloaded into RAM. The EEPROM uses P1.2 for
SDA and P1.3 for SCL. If the commit marker or checksum is invalid, startup
leaves the program page unchanged. The monitor firmware is shared with the
base trainer project.

## Firmware provenance

Paul Stoffregen released PAULMON2 into the public domain. The
[official download page](https://www.pjrc.com/tech/8051/paulmon2.html)
provides the core and Extras assembly sources and the preassembled images.
The [upstream](upstream/) directory contains byte-for-byte copies of
`paulmon21.asm`, `extra.asm`, and `pm21_2.obj`, saved as `pm21_2.hex` so the
emu51 project loader recognizes its Intel HEX format. The upstream assembly
uses AS31 syntax. The project loads the published HEX directly.

SHA-256 of the bundled firmware HEX

```text
0354f2132be422ea63370308565eac7429a7d496d5d59589fe68147e3d8a093e
```
