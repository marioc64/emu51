<!-- SPDX-License-Identifier: GPL-2.0-only -->
# SID / reSID integration assessment

The existing peripheral clock, XDATA bridge and host-audio queues can host a
reSID-based MOS6581/MOS8580. SID is not included in the current device catalog.
The new AY-3-8910 example exercises the corresponding bus-to-sound-to-host path.

## License and ownership

The [reSID mirror maintained by libsidplayfp](https://github.com/libsidplayfp/resid)
is a C++ chip emulator. Its [SID source/header](https://github.com/libsidplayfp/resid/blob/master/sid.h)
is licensed GPL version 2 or later. The emu51 application is GPL-2.0-only, so a
combined application can use the GPLv2 option. Keep the wrapper and upstream
sources in a separate GPL host target; do not add them to the independently
LGPL-2.1-only `emu51::peripheral` target or its installed SDK archive. Preserve
upstream notices and distribute the selected upstream license/source with any
binary using it. No reSID code has been copied or relicensed in this change.

## Proposed device contract

- `SID 6581/8580` block with a selectable model and chip-clock frequency.
- A 32-byte XDATA target for the SID register file; base address configurable.
- Optional functional GPIO interface: address bits A0..4, data D0..7, CS, RW,
  RESET and emulated PHI2. The CPU oscillator and SID clock must remain separate.
- Analog `out` for the host-audio bridge; optional EXT IN with explicit scaling.
- Clock conversion keeps a fractional remainder, preserving long-run pitch when
  the CPU oscillator is not an integer multiple of the SID clock. Work runs on
  the simulation worker, using reSID's clock/sampling interface, and never in the
  UI or native audio callback.
- Ordinary reads use `SID::read`; debugger PEEK uses a passive state snapshot to
  avoid changing bus-decay state. Reset, gate/ADSR, oscillator sync/ring modulation,
  waveform combinations and the model-specific filter remain reSID's job.
- Project JSON stores model, clock and wiring. New builds must reject a SID
  project clearly when the optional reSID component is unavailable.

## Work required before enabling SID

Pin an upstream revision and its complete build inputs/configuration, build it
on macOS/Linux/Windows, and add a separate build option with no configure-time
network fetch. Validate register masks/read side effects, 6581/8580 reset,
frequency and ADSR timing, filter response, interrupt-heavy 8051 writes, pause
and resume, and bounded audio queues. Compare identical register traces against
upstream reSID. Provide an original SID melody example driven by 8051 firmware;
loading an arbitrary C64 `.sid` file also requires its 6502 player/C64 environment
and is a separate feature.
