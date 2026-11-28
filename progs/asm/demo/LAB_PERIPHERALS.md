<!-- SPDX-License-Identifier: GPL-2.0-only -->
# 8051 lab peripherals

Open these projects in emu51 and select **Run** in Cycle exact mode.

| Project | Experiment |
| --- | --- |
| [Stepper motor](stepper_control.emu51) | In **Peripherals → Live**, toggle **Reverse** and **Half step**. The shaft view shows angular position, energized coils and invalid transitions. |
| [ADC0809 scan](adc0809_scan.emu51) | Adjust the eight voltage sources in **Live**. Watch IRAM 20h–27h for the latest channel readings and 28h for the scan counter. |
| [PAULMON2 EEPROM trainer](../../boards/paulmon2/paulmon2_eeprom.emu51) | Upload an Intel HEX program to shared RAM, save its first page with the monitor’s `B` command, then power cycle the trainer. See the [board guide](../../boards/paulmon2/README.md#save-and-restore-a-program-with-24lc256). |

## Device profiles

**24LC256** uses the same SCL, SDA and WP connections as the 24C02. Select an
I²C address from 50h through 57h. Firmware sends a two-byte word address.
Sequential reads wrap across the 32 KiB array, and writes wrap within a
64-byte page. STOP starts a 5 ms write cycle during which the device NACKs its
I²C address. WP high at STOP inhibits programming. An optional 32,768-byte image
stores committed bytes. [Microchip 24LC256](https://www.microchip.com/en-us/product/24LC256)

**ULN2003** exposes seven inputs and seven open-collector outputs. A high
input sinks the corresponding output. Wire outputs 0–3 to the four-phase
stepper’s **COILS** in A–D order. The coil inputs pull released outputs high
and treat low as energized. The shaft model has 200 full steps per turn.
It recognizes adjacent single-coil and paired-coil sequences in both
directions, reports invalid patterns and skipped phases, and holds position
when coils are released. It uses ideal steps and digital coil states.
[TI ULN2003A](https://www.ti.com/product/ULN2003A)

**ADC0809** latches its three-bit **ADDR** on ALE rising. START rising takes
EOC low and restarts any outstanding conversion. START falling samples the
selected analog input and starts the configured conversion interval. EOC
rises when the result is latched. OE high enables the eight-bit data output.
The default interval is 1,200 emulator oscillator ticks for a new Graph block
and the scan demo. The full-scale reference defaults to 5 V, with 0 V as
the lower reference. The model uses a timed conversion and exposes the
handshake pins. An unwired analog channel leaves the result undefined.
[TI ADC0808/ADC0809 datasheet](https://www.ti.com/lit/ds/symlink/adc0808-n.pdf)
