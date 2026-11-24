<!-- SPDX-License-Identifier: GPL-2.0-only -->
# AVT-2250 circuit

[avt2250.emu51](avt2250.emu51) runs in the standard `emu51` application using
ordinary Graph peripherals. It needs no board module or special executable.
The circuit and this guide are redistributable project code. The original
monitor ROM and proprietary example programs are **not included**.

## Supply the ROM

Copy the project into a writable folder and place your own 8192-byte monitor
image beside it as `monitor.bin`. Then use **File → Open project** and select
the copied project. Opening validates the image before replacing the active
machine. A missing or incorrectly sized image produces an error.

Alternatively, edit the ROM's entry in the project's `memory_images` array to
reference your existing image. Its target is `monitor_rom`, physical offset 0,
length 8192. No assembly load mapping is needed for this binary initialization.
Do not commit your ROM into this source tree.

Open **Peripherals → Live**, then Run. The original monitor normally scans a
HELLO greeting before accepting commands. The project selects the 8051 model
and a connected 11.0592 MHz Clock. The serial terminal opens without attaching
native audio or other host devices.

The same project can run headlessly:

```sh
emu51cli --steps 1000000 /path/to/your/avt2250.emu51
```

## Circuit and controls

The default memory arrangement is an 8 KiB EPROM at 0000h and 32 KiB shared
CODE/XDATA RAM at 8000h. Lookup decoders implement the address windows, latch
selection and combined RAM read control. The functional wiring follows the
[publisher's hardware description](https://serwis.avt.pl/manuals/AVT2250.pdf),
printed pages 8–11, with normalized display-driver levels.

| CPU access | Connected peripheral |
| --- | --- |
| CODE 0000h–1FFFh | `monitor_rom`, physical 0000h–1FFFh |
| CODE/XDATA 8000h–FFFFh | `program_ram`, physical 0000h–7FFFh |
| XDATA write 4000h–5FFFh, even | `segments` output latch, A–G and decimal point |
| XDATA write 4000h–5FFFh, odd | `scan` output latch, low three bits select a digit/keyboard row |
| XDATA read 6000h–7FFFh | `keyboard_input` buffer; D0/D1 scanned keys, D2 M, D3 OK |

The remaining addresses are unselected. Reads from output-only windows return
FFh; keyboard-window writes have no recipient. The RAM can hold executable user
programs without modifying EPROM. Change capacity and decoding explicitly to
model different jumper arrangements.

In Live, all eighteen keys occupy one panel, with five columns and four rows:

```text
1  2  3   A  B
4  5  6   C  D
7  8  9   E  F
M  0  OK
```

The last two positions are empty. Function legends appear below the key labels:

| Key | Legend |
| --- | --- |
| 0 | Jump |
| 4 / 6 | Dec / Inc (previous/next address in Edit) |
| 7 / 8 | Load / Save |
| 9 | Test |
| B | Baud |
| C / D | Copy / Dump |
| E / F | Edit / Fill |
| M / OK | Monitor / Enter |

Dump is the printed D legend; the supplied monitor version leaves that command
unimplemented. All commands run in firmware; changing a legend does not patch it.

**Toggle** holds a key until a second click; **While pressed** holds it only
while the mouse is down. Both modes apply to hexadecimal keys, M and OK. Use
**Release all** when finished. Momentary contacts release when the panel is hidden.

In the keypad's Graph inspector, **Custom key layout** sets the visual row and
column count. Each position selects a contact or **Empty**; choosing an already
placed contact swaps positions. **Key labels and function legends** edits the
text. These visual settings preserve electrical contact numbers. The sixteen
hexadecimal keys still use eight scan rows and two columns; M and OK drive two
independent active-low outputs. Other circuits can use a 5×4 electrical matrix
with two unused contacts. Save Project retains layout, holes, labels and legends.
The Live **Edit layout** controls resize and move the entire panel.

The segment latch's retriggerable pulse drives the digit decoder's fourth input.
Its configured 25 ms watchdog blanks scanning if segment writes stop. Q retains
its stored byte after the pulse expires. Brightness/persistence use emulated
time and freeze while paused. These are functional digital models; RC tolerances,
LED current, transistor analog behavior and multiplexed P0/ALE timing are not
simulated. The normal display represents the combined segment/digit driver
behavior rather than individual transistors.

## Programs, reset and saving

CPU Reset preserves ROM and RAM bytes while resetting the output latches and
scan watchdog. **Power cycle** in the assembly editor's **Project files** area
replays the ROM image recipe and clears RAM to its configured fill. Save Project
stores references and settings; it does not write ROM bytes back to the image.

To program RAM through the host, add an assembly/HEX/BIN build unit and an
explicit load row. Source 8000h–FFFFh maps to `program_ram` physical offset 0.
For example, a source assembled at ORG 9000h can use a row with source start
9000h and physical destination 1000h. Loading does not patch the monitor.

For serial loading, use the monitor's **7 / Load** command and the normal serial
terminal's **Send file** action. Select **File endings → CRLF** for a monitor
that expects CRLF after each Intel HEX record. This changes the transmitted
stream without rewriting the source file. Clear pending input before starting
another transfer. The monitor executes its own commands and interrupt handlers.

The public `emu51_trainer_tests` check uses an original synthetic ROM generated
during the test. An optional `--monitor-project PROJECT` invocation checks a
locally supplied original monitor; ordinary builds/tests do not search for it.
