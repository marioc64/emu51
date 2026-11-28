<!-- SPDX-License-Identifier: LGPL-2.1-only -->
# Interactive components and serial devices

## Functional parallel peripherals

`emu51.bus-latch8` (`E51P_COMPONENT_BUS_LATCH`) has `bus`, `Q`, `Q_n`, `PULSE`
and `PULSE_n`. A committed write stores eight bits; either local address aliases
the same register. Reads are unsupported. Q resets to zero and Q_n to FFh.
`e51p_component_set_latch_pulse_ticks` configures an optional retriggerable pulse
before start; the registry exposes `pulse_ticks`. Every write, including an
unchanged byte, retriggers it. Expiry releases the active pulse without changing
Q. Time advancement and reset control pulse lifetime; peeks never trigger it.

`emu51.bus-buffer8` (`E51P_COMPONENT_BUS_BUFFER`) has `bus` and `IN`. Reads and
peeks sample its eight digital inputs, which have weak pull-ups. Floating or
contended relevant bits return a fault. Writes are unsupported; both local
addresses alias the input. Neither model is a complete physical TTL chip.
They provide generic transaction adapters for latches and input buffers.

`emu51.key-matrix` (`E51P_COMPONENT_MATRIX`) has eight active-low `rows` inputs,
four open-drain `columns` outputs and eight open-drain `direct` outputs. Configure 1–8 rows and 1–4 columns; defaults
are 8×2. Key bit `row * columns + column` closes that contact. Multiple selected
rows combine their keys, with diode-isolated behavior and no ghost paths. Unused
columns release their lines. This adds a configurable device without changing
the existing 4×4 keypad's pin widths or key encoding. The C configuration
structure and ABI-v1 function table retain their layouts.

Before start, `e51p_component_set_matrix_direct_keys` adds 0–8 independent keys
after the scanned contacts, up to 32 contacts total. The registry parameter is
`direct_keys`. Key bit `rows*columns+n` pulls `direct[n]` low without depending on
the selected scan row. Unconfigured direct outputs release their lines. Reset
preserves physically held contacts, and peeks never consume a press.

The GPL host exposes these devices in Graph. Latch/buffer address, data and
controls use the wired bus adapter. Leave the latch's bus OE and buffer's WE
inactive. Matrix labels and latch pulse milliseconds are project settings;
the SDK timing API uses context ticks. These primitives support the ROM-free
[AVT-2250 circuit](../progs/boards/avt2250/README.md) and other parallel devices.

`emu51/components.h` adds context-owned devices to the existing C ABI. The
constructors and device callbacks do no file, GUI or native audio I/O. Use
`e51p_component_config_init`, edit the required properties, then call
`e51p_add_component`. The returned opaque handle is borrowed until context
destruction. All controls and snapshot functions run on the simulation thread;
a GUI must use its host's execution lock. Discard a context after partial setup
failure. Factory-created versions expose identical pins via the registry.

| Type ID | Pins (in handle-result order) | Parameters |
| --- | --- | --- |
| `emu51.led` | `in` (1-bit input) | `active_low` (default 1) |
| `emu51.switch` | `out` (1-bit output) | `active_low`, `initially_pressed` |
| `emu51.button` | `out` (1-bit output) | `active_low`, `bounce_ticks` |
| `emu51.hd44780` | `data` (8-bit I/O), `RS`, `RW`, `E` | `columns` (1..20, default 16), `rows` (1, 2 or 4, default 2) |
| `emu51.keypad4x4` | `rows` (4-bit input), `columns` (4-bit open-drain output) | None |
| `emu51.74hc595` | `SER`, `SRCLK`, `RCLK`, `SRCLR`, `OE`, `Q` (8-bit output), `QH` | None |
| `emu51.74hc165` | `D` (8-bit input), `SER`, `CLK`, `INH`, `LOAD`, `QH`, `QH_n` | None |
| `emu51.24c02` | `SCL`, `SDA` (open-drain I/O), `WP` | `i2c_address` (50h..57h) |
| `emu51.ay38910` | `bus`, `DA` (8-bit I/O), `BDIR`, `BC1`, `BC2`, `RESET`, `A`, `B` (8-bit I/O), `out`, `channelA`, `channelB`, `channelC` (analog outputs) | `clock_hz` (1 kHz..4 MHz, default 2 MHz) |

These are instruction-boundary functional profiles. Supply and package pins,
analog loading, propagation delays and minimum pulse widths are not modeled.

## Buttons, switches, LEDs and keypad

A released contact leaves its line pulled to the inactive level. Closing an
active-low contact sinks the line (open drain); active-high contacts drive both logic levels so their inactive low also works
with quasi-bidirectional 8051 inputs. `e51p_component_set_pressed` changes the physical
contact without resetting CPU state. Switches are stable; buttons optionally
alternate the contact each simulated millisecond until `bounce_ticks` expires.
The first contact transition occurs immediately. Reset retains held controls.

`e51p_component_get_led` reports both illumination and validity: floating or
contended input is undefined rather than a valid off state.

Set a keypad's held mask with `e51p_component_set_keys`: bit `row*4+column` is a
closed key. Firmware drives one or more row bits low and reads the columns.
Multiple selected rows combine their pressed columns. This is a directed scanner
profile; reverse scanning and physical ghost-current paths are not modeled.

## HD44780

Power-on/reset selects 8-bit transfers. Function Set can select 4-bit transfers
on DB4..DB7; two strobes transfer the high nibble followed by the low nibble.
Writes latch on E falling; reads drive only while E and RW are high. RW low
releases the device's data drivers. Busy/address reads and character-data reads
are supported, including address advancement after a full transfer. Normal
commands/data take 37 microseconds; clear/home take 1.52 milliseconds, converted
to the context tick frequency. Writes received while busy are ignored.

The profile includes 80 character DDRAM locations across the two controller
lines, 64 bytes of 5x8 CGRAM, display/cursor/blink controls, entry direction,
entry/display shifts, and home/clear. Snapshot rows use the usual 00h/40h and
14h/54h offsets for a 20x4 display. The host renders ASCII and custom CGRAM
characters; it does not bundle a proprietary character-ROM font. Other ROM codes
are shown as `?`. 5x10 fonts and alternate one-line controller layouts are not
modeled.

## Shift registers

595: SRCLK rising shifts SER into bit 0 toward QH (bit 7). RCLK rising copies the
shift register to Q; simultaneous rising clocks latch the preceding shift value.
SRCLR low clears the shift register, leaving the storage register unchanged.
OE high releases Q; QH remains available for chaining.

165: LOAD low continuously loads D. With LOAD high, a rising edge of the combined
CLK/INH gate shifts SER into bit 0. QH and QH_n expose bit 7 and its complement.
Use low INH and pulse CLK for ordinary firmware; QH supports chaining into SER.

## EEPROM

The 24C02 profile follows the AT24C02D organization: 256 erased-FFh bytes, 8-byte
page writes, address straps represented by `i2c_address`, and a 5 ms programming
cycle. SDA is open drain with a pull-up. START, repeated START, STOP, address ACK,
current/random/sequential reads, master ACK/NACK, page wrap and write-protect at
STOP are supported. The device NACKs its address while programming. Bytes become
visible only when programming completes. Reset preserves committed memory and
completes an already accepted STOP-triggered write; an unfinished transaction is
discarded.

Import/export exactly 256 bytes with the EEPROM image functions. Import is
allowed only before the context's first time advance (including freshly reset
time zero). Export is a passive committed-memory snapshot. The GPL host owns
path resolution, atomic disk saves and error reporting. No file I/O occurs from
SCL/SDA callbacks or the time-advance callback.

## Additional lab devices

The same EEPROM API supports `emu51.24lc256`. Import/export transfers a
32,768-byte committed image. The device uses two address bytes, 64-byte page
wrap and a 5 ms write cycle. Hosts can query the image revision to avoid
copying an unchanged image.

`emu51.uln2003` is a seven-channel open-collector sink. `emu51.stepper`
accepts four active-low coil inputs and exposes a passive position, coil and
direction snapshot in half-step units. `emu51.adc0809` provides three
address bits, ALE/START/OE/EOC, an eight-bit tri-state output, and eight
analog inputs. Its full-scale voltage and conversion duration can be set
before context start. See the
[lab device profiles](../progs/asm/demo/LAB_PERIPHERALS.md) for board examples
and wiring.

## AY-3-8910

The chip clock is independent of the CPU tick frequency. The profile generates
three 12-bit tone dividers, a shared 17-bit noise LFSR, all 16 envelope shapes,
per-channel fixed/envelope volume and mixer gates. Zero tone/noise/envelope
period is treated as one. Register writes apply the hardware register masks.
The two parallel ports use register 7 direction bits (0=input, 1=output).

Wire DA/BDIR/BC1 with BC2 high and RESET pulled high, or map `bus` to two XDATA
addresses: +0 selects a register, +1 reads/writes its data. The two access methods
share the same registers. Native GPIO accesses follow BC2=1 modes: BDIR/BC1
11=latch, 10=write, 01=read, 00=inactive. Change the two control bits together and
release the CPU data drivers before reads. RESET is active low.

Tone frequency is `clock_hz / (16 * period)`. Envelope steps use
`clock_hz / (256 * period)`. The functional mixer uses logarithmic amplitude
steps, without a transistor/DAC/filter or board-loading model. The summed `out`
and individual channels are centered at 2.5 V for the existing 0..5 V host audio
bridge; zero volume is exactly 2.5 V. Connect `out` to host playback. This is not
a YM2149 variant or a replacement for a cycle/transistor-accurate chip emulator.

## References

- [TI SN74HC595 datasheet](https://www.ti.com/lit/ds/symlink/sn74hc595.pdf)
- [TI SN74HC165 datasheet](https://www.ti.com/lit/ds/symlink/sn74hc165.pdf)
- [Hitachi HD44780U datasheet](https://www.sparkfun.com/datasheets/LCD/HD44780.pdf)
- [Microchip AT24C01D/AT24C02D datasheet](https://ww1.microchip.com/downloads/aemDocuments/documents/MPD/ProductDocuments/DataSheets/AT24C01D-AT24C02D-Data-Sheet-DS20006100.pdf)
- [General Instrument AY-3-8910 datasheet](https://www.cselettronica.com/datasheet/AY3-8910.pdf)
