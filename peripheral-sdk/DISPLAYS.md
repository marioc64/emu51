<!-- SPDX-License-Identifier: LGPL-2.1-only -->
# Segment displays and controllers

`emu51/displays.h` is part of the LGPL SDK. Create a device with
`e51p_display_config_init` and `e51p_add_display`, or use its registry factory.
The context owns the returned opaque handle. Constructors validate settings
before registration; discard the context on partial setup failure. All controls,
callbacks and snapshots belong to the simulation/control thread. No GUI, file,
wall-clock or native audio operations occur in the models.

| Factory ID | Pin order | Configuration |
| --- | --- | --- |
| `emu51.seven-segment` | `segments` (8 input bits), `commons` (8 input bits), `colon` (2 input bits) | 1..8 digits, common-anode/cathode, colon, persistence |
| `emu51.fourteen-segment` | `segments` (15 bits), `commons`, `colon` | Same |
| `emu51.sixteen-segment` | `segments` (17 bits), `commons`, `colon` | Same |
| `emu51.bargraph` | `segments` (16 input bits) | 1..16 bars, active-low/high, persistence |
| `emu51.cd4511` | `BCD` (4 input bits), `LE`, `LT`, `BI`, `segments` (7 output bits) | Persistence |
| `emu51.74ls47` | `BCD`, `LT`, `BI`, `RBI`, `segments`, `RBO` | Persistence |
| `emu51.max7219` / `emu51.max7221` | `DIN`, `CLK`, `LOAD`, `DOUT`, `segments` (8 output bits), `commons` (8 output bits) | Preview digit count, colon |
| `emu51.ht16k33` | `SCL`, `SDA`, `segments` (16 output bits), `commons`, `keys0`, `keys1`, `keys2` (13 input bits each), `INT` | Preview digits, 7-bit I2C address 70h..77h, colon |
| `emu51.tm1637` | `CLK`, `DIO`, `segments`, `commons`, `keys0`, `keys1` (8 input bits each) | 1..6 preview digits, colon |

Pull-ups on serial data/clock and active-low controls are included. Other raw
inputs have no artificial pull-downs, so they work with quasi-bidirectional 8051
ports. Supply pins, LED current, resistors, transistor drive and propagation/setup
limits are outside these instruction-boundary functional profiles.

## Raw displays and persistence

Common-cathode: a common must be low and a segment high to illuminate it.
Common-anode: common high, segment low. Each common enables one digit; multiple
commons can be active. Unused common bits are ignored by the configured digit
count. A floating or contended segment/common appears in the snapshot's
`undefined` mask. Bar graphs use the configured polarity and need no common pin.

Bit assignments:

- 7-segment: A, B, C, D, E, F, G, DP (bits 0..7).
- 14-segment: A, B, C, D, E, F, G1, G2, H, I, J, K, L, M, DP (0..14).
- 16-segment: A1, A2, B, C, D1, D2, E, F, G1, G2, H, I, J, K, L, M, DP (0..16).
- H/I/J are upper-left diagonal, upper vertical and upper-right diagonal;
  K/L/M are lower-right diagonal, lower vertical and lower-left diagonal.

There is no character decoder in raw displays: arbitrary segment patterns and
wiring permutations are supported. Two colon inputs share the common for digit
1 (digit 0 on a single-digit display).

Brightness is the exponentially weighted integral of actual digital on-time:
`new = old * exp(-dt/tau) + on * (1-exp(-dt/tau))`, with `tau=persistence_ms`.
The default is 20 ms; 1..1000 ms is configurable. Transitions integrate the prior
state before accepting the new one. Passive snapshots account for elapsed
**emulated** ticks without mutating history. Frequent/infrequent GUI reads give
the same answer, CPU Pause freezes persistence, and Reset clears it. Firmware
that stops refreshing leaves its last driven digit lit while other digits decay.

Snapshots report physical `instantaneous` masks, `programmed` patterns, and true
0..1 per-segment duty in `brightness`. On raw displays, programmed equals the
instantaneous input pattern. The desktop boosts multiplex duty by the displayed
digit count for readability and offers instantaneous mode for debugging; it
never feeds rendered brightness back into the simulated circuit.

## BCD decoders

4511: LE low is transparent; LE high holds the last BCD digit. LT low lights all
seven segments and overrides BI; BI low blanks otherwise. Inputs 10..15 blank.
Outputs are active high for common-cathode displays.

LS47: active-low open-collector outputs include weak off-state pulls. The font
has the original tailless 6/9 and the chip's patterns for inputs 10..15, rather
than a hexadecimal font. BI low overrides lamp test; LT low lights all segments.
RBI low blanks input zero and asserts RBO low for leading-zero suppression.
The physical combined BI/RBO package pin is represented by separate logical
`BI` input and open-drain `RBO` output aliases, allowing explicit ripple wiring
without a feedback latch. RBO is released during lamp test.

## MAX7219 and MAX7221

DIN shifts MSB-first on rising CLK; LOAD rising latches the last 16 bits. DOUT
updates on falling CLK for chaining. MAX7219 clocks regardless of LOAD;
MAX7221 only clocks while LOAD/CS is low. Registers 1..8 hold digit RAM; 9
selects Code-B decoding per digit, A is intensity, B scan limit, C shutdown,
F display test, and 0 no-op. Test overrides shutdown/limit/decode without changing
RAM. Code B includes decimal digits, dash, E/H/L/P and blank; DP is independent.
Native no-decode bit 6 is A and bit 0 is G. The exposed `segments` port translates
this into the raw display's A..G,DP bit order.

The functional scanner refreshes complete frames at 800 Hz. MAX7219 intensity
uses (2n+1)/32; MAX7221 uses min(n+1,15)/16. Common outputs sink the selected digit.
Controllers expose an ideal register-based brightness preview and physical scan
outputs for wiring to a raw display. Pin edges are published at instruction
boundaries: advance in sufficiently small steps when measuring high-frequency
PWM through the physical outputs. Preview intensity does not depend on step size.

## HT16K33

MSB-first I2C supports address ACK/NACK, repeated START, writes and reads. A data
pointer selects 16 display RAM bytes; reads/writes auto-increment and wrap at
0Fh. Two little-endian bytes form each 16-segment/common pattern. Commands cover
oscillator standby (20h/21h), display enable and blink (80h..87h), 16 brightness
steps (E0h..EFh), and interrupt configuration (A0h group). Standby/display-off
retain RAM. Blink is 2/1/0.5 Hz on the emulated timeline.

Key aliases are active low; host-held keys use bit `column*13+row` in
`e51p_display_set_keys`. The profile uses a fixed 20 ms simulated debounce before
latching 39 key bits. Read flag 60h or key RAM 40h..45h; each column occupies a
low byte and a five-bit high byte. Reading all six bytes clears key RAM and INT.
A held key can trigger again after another debounce interval. These logical key
inputs are separate from LED scan outputs; shared physical ROW/COM key wiring
and electrical ghosting are not modeled. INT enable/polarity are supported.

The 16 output lines can drive a 14-segment display plus DP, or a 16-segment
layout without DP. The desktop lets the preview geometry be selected; physical
outputs always retain their 16-bit RAM layout.

## TM1637

This is an address-less, **LSB-first** two-wire protocol, not I2C. DIO acknowledges
each command/data byte. Implemented commands: 40h auto-increment, 44h fixed
address, C0h..C5h digit address, 80h group display enable/eight brightness settings,
and 42h key read. Six digit bytes are retained while blanked. Data beyond the
sixth digit is ignored until another valid address is selected. Brightness duty
is 1/2/4/10/11/12/13/14 sixteenths. Segment outputs sink current and common outputs
source it, for common-anode displays.

Host key-mask bits 0..7 select K1/SG1..8; 8..15 select K2/SG1..8. Parallel input
aliases accept the same active-low keys. Reading returns FFh when none are held,
otherwise the lowest numbered key's documented code. Multiple-key combinations
are not encoded. Optional controller colon preview maps digit 1's DP to both dots.
For HT previews the corresponding optional mapping uses bit 15 of digit 1.

## Reference datasheets

- [TI CD4511B](https://www.ti.com/lit/ds/symlink/cd4511b.pdf)
- [TI SN74LS47](https://www.ti.com/lit/ds/symlink/sn74ls47.pdf)
- [Analog Devices MAX7219/MAX7221](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX7219-MAX7221.pdf)
- [Holtek HT16K33](https://cdn.sparkfun.com/assets/3/f/8/6/2/HT16K33v120.pdf)
- [Titan Micro TM1637](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/unit/digi_clock/TM1637.pdf)
