<!-- SPDX-License-Identifier: LGPL-2.1-only -->
# Serial devices and logic analyzer

The built-in registry exposes `emu51.ssd1306`, `emu51.ds1307`,
`emu51.mcp23017`, `emu51.w25q64` and `emu51.logic-analyzer`. Use the direct
constructor in [serial_devices.h](include/emu51/serial_devices.h) for handles
that support passive snapshots, raw flash images and analyzer controls. Use all
handles on the context's simulation thread. The SDK performs no file I/O.

| Device | Pins | Settings |
| --- | --- | --- |
| SSD1306 | SCL, SDA | I²C address 3Ch or 3Dh |
| DS1307 | SCL, SDA, SQW | I²C address 68h, initial day and time |
| MCP23017 | SCL, SDA, A[7:0], B[7:0], INTA, INTB, RESET | I²C address 20h–27h |
| W25Q64 | CS, SCK, MOSI, MISO | Standard single-lane SPI with active-low CS |
| Logic analyzer | IN[7:0] | Immediate capture or rising/falling edge on any channel |

I²C SDA uses open-drain output and SCL/SDA have weak pull-ups. The master must
release SDA for ACK and read cycles. The models accept START, repeated START and
STOP, with 7-bit addressing. Timing follows observed signal transitions at the
host's simulation boundaries.

## SSD1306

This profile implements a 128×64 I²C module with page, horizontal and vertical
addressing, display bounds, start line, offset, row/column mirroring, contrast,
inversion, all-on mode, multiplex selection and display enable. Horizontal and
diagonal scrolling use a nominal 60 Hz frame rate on the emulated timeline.
Initialization sequences can send analog configuration bytes such as charge
pump and precharge settings. The snapshot renders visible pixels as packed rows
and reports contrast separately.

[Solomon Systech SSD1306](https://www.solomon-systech.com/en/product/SSD1306)

## DS1307

The clock starts on the configured day since 2000-01-01 and seconds since
midnight. It advances only with SDK ticks and repeats after 2099. Registers
00h–06h contain BCD time and date, with 12/24-hour modes and a clock-halt bit.
A START snapshots the clock registers for coherent sequential reads. The
weekday starts with Sunday as 1 and can be written independently. Register 07h
controls the open-drain SQW output, which is sampled at simulation boundaries.
Addresses 08h–3Fh provide 56 bytes of scratch RAM. A context reset preserves
the calendar and RAM. A new device instance starts from the configuration.

[Analog Devices DS1307 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/DS1307.pdf)

## MCP23017

A and B are resolved bidirectional byte-wide ports. The register map supports
banked and interleaved layouts, sequential and byte addressing, data direction,
input polarity, programmable weak pull-ups, GPIO and output latches. INTA/INTB
support independent or mirrored outputs, push-pull polarity and open-drain
operation. An input change can latch the port into INTCAP. Reading GPIO or
INTCAP clears the corresponding port interrupt. A condition configured to
compare against DEFVAL can remain asserted until the input matches that value.
RESET is active low.

[Microchip MCP23017 datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/20001952C.pdf)

## W25Q64

The single-lane SPI profile samples on rising edges and changes MISO on falling
edges. It supports normal/fast read, JEDEC and manufacturer IDs, write
enable/disable, status reads, page program, 4/32/64 KiB erase, chip erase and
software reset. Page program wraps within 256 bytes and can only clear bits.
Program and erase commit after deterministic busy intervals. Logical context
reset finishes an accepted operation and preserves the array. The erased array
contains FFh. Quad I/O, security registers, protection bits, suspend/resume and
deep power-down are outside this profile.

Image import/export transfers exactly 8 MiB. Export snapshots committed bytes.
A host can check the revision to avoid copying an unchanged image.

[Winbond W25Q64JV product documentation](https://www.winbond.com/hq/product/code-storage-flash/qspi-nor/w25q-jv/?__locale=en&partNo=W25Q64JVSSIQ)

## Logic analyzer

The analyzer records up to 4096 changes to the resolved input port, with a tick
and masks for floating and contended signals. It starts capturing immediately
by default. Edge-triggered mode keeps up to 1024 pre-trigger samples and uses
the rest of the same buffer for events after the trigger. Capture stops when the
buffer fills or the host stops it. Re-arming clears the old capture. Context
reset starts a new capture with the last armed trigger settings.

The decoder recognizes 7-bit I²C START/STOP, addresses, bytes and ACK/NACK with
D0 wired to SCL and D1 to SDA. For SPI, wire D0 to SCK, D1 to MOSI, D2 to MISO and
D3 to active-low CS. SPI decode reads MSB first on rising edges. Align capture
before START or chip selection to decode the transaction from its beginning.
