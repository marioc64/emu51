<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Desktop peripherals

The desktop application uses the independently LGPL-2.1-only
[peripheral SDK](peripheral-sdk/README.md). Its configuration, CPU adapters and
UI remain GPL-2.0-only. The SDK can also be built and used without the emulator.

## Segment display examples

Open a project and select **Peripherals → Live**, then Run:

| Project | Circuit |
| --- | --- |
| [seven_segment_clock.emu51](progs/asm/demo/seven_segment_clock.emu51) | Four common-cathode digits and colon. Timer 0 mode 2 scans every 250 microseconds and maintains an MM:SS clock at 12 MHz. |
| [segment_decoders.emu51](progs/asm/demo/segment_decoders.emu51) | BCD counter through CD4511/common-cathode and 74LS47/common-anode circuits. |
| [max7219_counter.emu51](progs/asm/demo/max7219_counter.emu51) | Chained MAX7219 and MAX7221, separate intensities, Code-B numbers, and a raw display wired to the first controller's scan outputs. |
| [ht16k33_alpha.emu51](progs/asm/demo/ht16k33_alpha.emu51) | I2C alphanumeric panel displaying `EMU51 IO`. Expand Key scan inputs in Live: firmware reads all six bytes to IRAM 20h..25h when INT asserts. |
| [tm1637_counter.emu51](progs/asm/demo/tm1637_counter.emu51) | Four-digit colon module using the address-less LSB-first protocol. IRAM 21h shows key-read codes. |
| [segment_gallery.emu51](progs/asm/demo/segment_gallery.emu51) | Walking-bit test across 7-, 14-, 16-segment and bar-graph displays, including 17-bit wiring split over three CPU ports. |

The Add menu contains all ten display/controller types. Raw displays expose
segment, common and colon inputs; controllers expose their serial/BCD inputs
and physical segment/common outputs. The inspector selects digit count,
polarity, persistence, color, colon and HT preview geometry/address. Ports wider
than eight bits wrap their bit connectors inside the graph block.

Live normally shows brightness accumulated on the emulated timeline. It freezes
on CPU Pause. Multiplex gain is applied only to visualization.
Controller previews derive from their RAM/intensity settings; raw displays
measure the actual wired output duty. See [SDK display profiles](peripheral-sdk/DISPLAYS.md)
for bit assignments, timing and modeled interfaces. Activate edited circuits
with **Apply** in the Peripherals title bar.

## Front-panel, serial and sound-chip examples

Open an example project, select **Peripherals → Live**, and Run:

| Project | Devices and behavior |
| --- | --- |
| [front_panel.emu51](progs/asm/demo/front_panel.emu51) | HD44780 LCD, 4x4 keypad, bouncing button, switch and LED. Click keypad keys to hold/release them. Hold the pushbutton; either contact lights the LED. Firmware scans the keypad and updates the LCD. |
| [shift_registers.emu51](progs/asm/demo/shift_registers.emu51) | Eight switches feed a 74HC165; firmware copies their byte to P2 and one 74HC595, while a chained 595 receives A5h. Toggle switches in Live. |
| [eeprom_counter.emu51](progs/asm/demo/eeprom_counter.emu51) | Firmware bit-bangs I2C to increment EEPROM byte 2Ah once per reset. IRAM 20h/21h/22h show previous/new/readback; 23h is the error flag. |
| [ay_chords.emu51](progs/asm/demo/ay_chords.emu51) | Original three-voice AY chord sequence, with a hardware envelope on voice C and a timer ISR changing notes. Open the default playback device in Live, then Run. No microphone is used. |

Copy the EEPROM project **and its `.asm`** into a writable folder before use.
Its 256-byte `eeprom_counter.bin` image is created alongside the project. Only
committed EEPROM bytes are saved: automatically while paused and on session
close, or with **Save committed EEPROM bytes** in Live. Reset retains EEPROM
contents; reopening the app loads the image. Leave **Image file** empty for a
volatile device. Image paths are project-relative where possible. An invalid
existing image is rejected rather than truncated. Project-opening candidates do
not write files if opening is canceled or validation fails.

The Graph popup includes all new device types. The inspector exposes polarity,
button bounce duration, LCD dimensions, EEPROM address/image path, and AY clock
and XDATA base. Click **Apply** in the Peripherals title bar to activate these
settings and wiring changes.

See [SDK component contracts](peripheral-sdk/COMPONENTS.md) for modeled interfaces
and limits, and [SID/reSID assessment](SID.md) for the separate GPL integration
proposal. SID is not currently an available block.

## Build and try the board

From the public source root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Launch `build/emu51/emu51_imgui.app` on macOS or `build/emu51/emu51_imgui` on
Linux. Windows paths depend on the selected CMake generator/configuration.
Normal application builds enable `EMU51_PERIPHERAL_HOST_AUDIO`; turn it off to
build without native sound-card support. The analog audio bridge remains usable.

Open an included project with **File → Open project**:

- [Microphone loopback + echo](progs/asm/demo/microphone_covox.emu51) connects
  microphone capture through ADC0804 and CPU P0, then CPU P1 through Covox to playback.
- [Covox duet](progs/asm/demo/covox_duet.emu51) generates two-voice music using
  only CPU timers, Covox and playback.

Projects load paused and leave physical audio devices closed. In
**Window → Peripherals → Live**, list devices, keep **System default** or select
the desired device/channels, then **Open selected audio devices** and **Run**.
The `emu51_peripheral_adc_demo` command-line host separately demonstrates an
8255/ADC/DAC board and checks interrupt delivery with both CPU dispatch engines.

## Two-voice Covox duet

Open [covox_duet.emu51](progs/asm/demo/covox_duet.emu51) with **File → Open
project**. It loads the [assembly](progs/asm/demo/covox_duet.asm),
paused in Cycle exact mode at **24.576 MHz**, with `P1[7:0] → covox.data[7:0]`
and `covox.out → audio.playback`. This graph contains only Covox and a
playback-only audio bridge. No capture device or microphone permission is used.
Open playback devices in **Live**, then Run. The output oscilloscope shows the synthesized signal.

Two real 8051 timer interrupts perform all synthesis and sequencing:

- **Timer 0**, high priority, mode 2 auto reload: 8,000 samples/second. Each
  voice advances a 16-bit phase accumulator by its pitch step. Its phase sign
  selects a square-wave contribution of −63 or +63; a closed gate contributes
  zero. The ISR writes `128 + voice1 + voice2` to P1 and the last-output status
  byte. Both voices off give `80h`; a solo gives `41h`/`BFh`; the combined output
  is `02h`, `80h` or `FEh`, without arithmetic clipping.
- **Timer 1**, low priority, mode 1: nominal 100 music ticks/second (about
  99.87 Hz including software reload). It consumes note durations, reads the
  next two pitches and gates from code memory, and looks up their phase steps.
  It briefly masks ET0 to publish both voices together. Audio otherwise
  preempts sequencing, with separate register banks and saved foreground state.

The `song` table contains five bytes per record:

```asm
; duration in music ticks, MIDI pitch 1, gate 1, MIDI pitch 2, gate 2
    db 25,60,1,52,1      ; C4 and E3, about 250 ms
    db 25,60,0,48,1      ; voice 1 off; voice 2 plays C3
    db 25,72,1,48,0      ; voice 1 plays C5; voice 2 off
    db 50,60,0,48,0      ; both off, about 500 ms
    db 0                ; loop back to song
```

Duration is 1–255 ticks; zero terminates and loops. Gate zero means off,
nonzero means on. Supported pitches are MIDI **48–83 (C3–B5)**; invalid pitches
mute that voice. Each record resets both phases. The table stores
`round(frequency × 65536 / 8000)` for each pitch, so frequency is independent
of ISR instruction counts. Changing the crystal also changes audio pitch and
tempo; retain the example's clock. Edit the score in the Assembly Editor and
Compile and Load to hear changes. The included original dyad exercise cycles
through chords, individual voices and rests, then repeats.

Inspect **Internal RAM Monitor** at `30h..53h` for player status. Words below
are little-endian; counters wrap.

| IRAM | State |
| --- | --- |
| `30h` | Player initialized/playing (`1`) |
| `31h..32h` | Music interrupt count |
| `33h` | Current score row, starting at 1 |
| `34h` | Last Covox output byte |
| `35h..36h` | Audio sample count |
| `37h` | Completed score loops |
| `38h..39h`, `40h..41h` | Voice 1/2 phase |
| `3Ah..3Bh`, `42h..43h` | Voice 1/2 phase step |
| `3Ch`, `44h` | Voice 1/2 MIDI pitch |
| `3Dh`, `45h` | Voice 1/2 gate |
| `3Eh`, `46h` | Voice 1/2 signed contribution |
| `48h..49h` | Address of next record in code memory |
| `4Ah` | Remaining music ticks in current record |
| `4Ch..53h` | Pending pitch/gate/step data for atomic publication |

Register banks 1 and 2 belong to the ISRs. The stack starts at `5Fh`, reserving
`60h..7Fh`; foreground code idles in bank 0. Reset restarts both timers and the
score. Run/Pause controls native playback as in other audio examples.

From the public source root:

```sh
cmake -S . -B build-audio -DCMAKE_BUILD_TYPE=Release
cmake --build build-audio --target emu51_imgui emu51_covox_duet --parallel
./build-audio/emu51/emu51_covox_duet --run --seconds 8
```

The command-line host paces the actual CPU and timers; it does not generate
samples itself. `--null --seconds 0.2` tests playback without hardware.
`--write-project path.emu51` writes the portable embedded-source project without
opening devices. Native audio requires an explicit `--run`.

## Microphone → ADC → P0; P1 → Covox → audio output

Open [microphone_covox.emu51](progs/asm/demo/microphone_covox.emu51) with
**File → Open project**. It loads the direct-port example and embedded
[assembly program](progs/asm/demo/microphone_covox.asm), paused at a 3 MHz crystal.
No 8255 is used; MOVX accesses ordinary XDATA RAM for the echo delay line.

```text
Microphone -> audio.capture -> ADC0804.data[7:0] -> CPU P0[7:0]
                                                    |
                                         (input >> 1) + (buffer[p] >> 1)
                                                    |
Default output <- audio.playback <- Covox.data[7:0] <- CPU P1[7:0]
```

| CPU pin | ADC0804 signal |
| --- | --- |
| P0.0–P0.7 | D0–D7 |
| P3.4 | /CS |
| P3.5 | /WR |
| P3.6 | /RD |
| P3.2 | /INTR |

P1.0–P1.7 connect directly to the Covox input. ADC VIN+ comes from the microphone
bridge, VIN− is grounded. Firmware releases all P0 bits, pulses /WR, waits for
/INTR, asserts /RD and reads P0. It then applies a feedback echo entirely in
8051 assembly:

```text
mixed = (input >> 1) + (buffer[p] >> 1)
buffer[p] = mixed
P1 = mixed
p = (p + 1) modulo BUFFER_SIZE
```

Both halves use `CLR C` followed by `RRC A`, which makes each shift logical.
The sum cannot exceed 254. The buffer starts at unsigned silence `80h` (2.5 V),
so a silent input stays centered from the first sample. IRAM `30h` shows dry
input, `31h` the mixed output, and `32h` becomes 1 after buffer initialization.
DPTR is the current delay-line pointer.

Change these constants in the assembly source, then Compile and Load:

```asm
ECHO_BASE EQU 08000h
BUFFER_SIZE EQU 4096
```

`BUFFER_SIZE` is a byte/sample count from 1 to 32,767 for this base address;
non-power-of-two lengths work too. The buffer occupies XDATA `8000h..8FFFh` at
the default size. Keep its range free of program overlays and mapped devices.
Initialization fills all slots with `80h`; CPU Reset clears the echo by running
this initialization again. Pause/Resume retains the delay line and pointer.

Delay is `BUFFER_SIZE / sample_rate`. At the example's 3 MHz crystal and 240-tick
ADC conversion, the additional mix instructions give approximately 5,556 samples
per second: 4,096 samples produce about 0.737 seconds of delay. Each delayed
repeat is halved again. Larger buffers give longer delays; smaller ones give
shorter delays. Exact sample rate includes pointer-wrap instruction overhead.
The desktop initially outputs silence while the firmware initializes its buffer;
the command-line host prepares the buffer before opening audio.

Select **Live → List audio devices**, retain **System default** or choose the
physical microphone as Capture device, and select the Playback device. Click
**Open selected audio devices**, then Run (F9). Capture channel 0 and playback channel
−1 (all) are the defaults. Headphones avoid microphone feedback. Playback uses the full audio range: `(Covox byte - 128) / 128`, so `00h` maps
to −1, `80h` to silence, and `FFh` to +127/128. Pause (F9) silences/stops callbacks; Run resumes.
The microphone preset starts with **+20 dB (10×) input gain**. In
**Live → Audio signals**, adjust **Microphone gain** from −20 to +60 dB while
running; it takes effect without CPU reset or reopening audio. Projects
save the gain. Older projects without this setting retain unity gain (0 dB).
Amplification happens before ADC quantization: `2.5 + 2.5 × clip(mic × gain, −1, 1)`
volts. Silence remains `80h`, and the extremes clip at `00h`/`FFh`. If **INPUT
CLIPPING** appears, reduce gain.

The same section shows input (after gain) and output oscilloscopes, plus the
microphone's raw peak level. Choose a 10–80 ms window. Both plots use fixed
bipolar full-scale limits; **Output scope zoom** magnifies only the displayed
waveform, independently of playback volume; its default is 1×. Peak percentages always show the
actual level. Scopes freeze on Pause and clear on resume/reset. They show bridge
sample boundaries, before physical-device buffering, rather than measuring
sound from the speakers.

The Live tab shows Running, Paused or Closed and frame-use counters, so an open
but paused device is distinguishable from active capture/playback. CPU pacing
restarts after native startup completes, including every resume; startup delay
is excluded from emulation time.

Use an optimized build for real-time audio. From the public source root:

```sh
cmake -S . -B build-audio -DCMAKE_BUILD_TYPE=Release
cmake --build build-audio --target emu51_imgui emu51_microphone_covox --parallel
./build-audio/emu51/emu51_microphone_covox --list
./build-audio/emu51/emu51_microphone_covox --run --capture default --seconds 10
```

`--capture index` selects a microphone from the listed inputs; playback always
uses the current default output. Add `--repeat 5` to test five stop/resume
cycles on the same devices and CPU, with 100 ms pauses between runs.
`--null --seconds 0.15 --repeat 3` exercises the complete
CPU/ADC/P0/P1/Covox path with software audio. `--write-project path.emu51`
creates a portable paused project without opening devices. The native example
prints captured/played frame counts, the mixed byte range, real-time pacing and
queue misses/drops, and checks every mix against the dry ADC byte and old delay
slot, its write back to the same location, and the resulting Covox voltage. Creating/listing the example does not start capture;
native devices open only with `--run` or an explicit desktop action.

The desktop host uses 16,384-frame queues and 2,048 frames (42.7 ms) of latency
in each direction to accommodate frame-sized execution batches and native
callback jitter. Long UI stalls can still produce counted silence/dropouts.
The underlying SDK defaults remain 4,096/480 frames for hosts with tighter pacing.

## Configure a graph

### Live device layout

**Live** is the hardware front panel: LCD/segment displays, bargraphs, LEDs,
switches, pushbuttons and keypads. Audio and adjustable voltage/digital sources
are the utility exceptions. CPU state, pin tables, instantaneous signal switches
and bare ICs (8255, ADC/DAC, shift registers, EEPROM and AY) are not front-panel
boxes. Device configuration remains in the Graph inspector.

Boxes use compact single-line name headers; hover a header for the device type.
Displays show their normal accumulated brightness. Audio gain and scopes are
under **Signals**; capture/playback selection and opening/closing are under
**Devices**. Audio channels, buffering and diagnostics are collapsed by default.

Enable **Edit layout** to drag headers and resize bottom-right corners. Device
controls are disabled during layout editing. Turn it off to operate switches,
buttons, keyboards and audio controls. Selected boxes come to the front when
they overlap. **Arrange** packs boxes into rows; **Compact** also restores their
small default sizes, useful for layouts saved by older versions. The canvas scrolls in
both directions; smaller boxes scroll their controls internally, keeping options
inside the device. Minimum sizes keep controls usable.
Fonts, controls, LCD/segment geometry, LEDs and scopes scale with the box.
Key grids fill the available area, and their clickable regions resize with them.

Each keypad (including display-controller key inputs) offers **Toggle**, where
a click holds a key and another click releases it, and **While pressed**, where
the key follows the mouse button. **Release all** clears the keys in either mode.
Momentary keys release on mouse-up, focus loss, hiding Live, collapsing a key
section, or entering layout edit mode. Toggle-mode holds intentionally remain
latched when hidden. Mode changes clear previous holds. Keypad mode is project
presentation data; reopening a project never restores pressed keys.

**Save Project** stores Live positions and sizes separately from Graph positions.
Layout changes take effect immediately without Apply or a CPU reset. Opening a
project restores its arrangement with edit mode off. Older projects receive a
default arrangement the first time Live is shown.

### Wiring and device settings

Projects load and validate an active graph before replacing the current one.
Invalid IDs, pins, bit ranges, overlapping address windows or analog connections
leave the active session intact. The graphical and table editors modify a draft.
Click **Apply** in the Peripherals title bar to validate and activate wiring and
inspector device settings. Applying resets and pauses the CPU. Invalid drafts
show an error and preserve the active circuit and CPU state. Apply is disabled
when there are no pending configuration changes.

Save Project stores the active graph, so apply circuit edits before saving them.
Names and positions for active blocks are saved independently and need no Apply.

The tabs sit in the title bar, to the right of **Peripherals**, and the canvas
fills the content area below. Narrow docked panes provide tab scrolling arrows. Right-click the canvas for its commands. The CPU appears as an Intel 8051 or 8052
block according to Execution settings. Every supported endpoint is visible even
when disconnected: P0-P3 and each of their eight bits, XDATA, device control and
data pins, analog voltages, and enabled host capture/playback directions. This
is a diagram of the emulator's functional interfaces, not a physical package
or power/clock schematic.

- Drag a block's header to move it. Wheel zooms around the pointer; middle/right
  dragging pans. A right-click without dragging opens the graph popup.
- In the popup, **Fit → All blocks** frames the circuit; **Selected block**
  centers the selected block; **Actual size (100%)** restores normal zoom.
  **Arrange blocks** restores a grid while preserving block names.
- Digital connectors are blue circles. Drag a whole port to another port to
  connect their common width, or drag numbered bit connectors for a one-bit wire.
  Select the resulting wire to edit first-bit offsets and width in the inspector.
- Analog connectors are amber squares. Drag between an output and an input in
  either direction. An output may feed several inputs; an input has one source.
- Purple XDATA links show automatic 8255 address mappings. Select an 8255 block
  or its bus link to edit its base address; the four registers remain mapped
  together. Bus links are not digital GPIO wires.
- Choose a device from the popup's **Add** submenu. The block appears at the
  original right-click position. Select its rectangle to edit settings or remove
  it and its attached wires. The CPU is always present.
- Set **Block name** in the inspector to label any block, including the CPU.
  Display names are saved with the layout; endpoint IDs and wires stay stable.
  An empty name restores the automatic label.
- Toggle **Inspector** in the popup to hide or show the properties panel. The canvas uses the
  freed space. In narrow docked panes, the inspector appears below the canvas.

The popup's **Controls** submenu describes gestures; its **Status** section
shows the latest graph message. Labels show IN, OUT or I/O. Select a block or hover a connector for details.
IDs such as `device1` remain stable. Projects save positions for the CPU and
active devices, separately from topology; dragging does not reset the CPU. Existing projects receive an initial arrangement.
Drag the Peripherals title onto the
standard docking targets, or right-click the title for Float/Left/Right/Top/Bottom/Center.
The Window snap commands work here too. Dock placement and inspector visibility
are retained in the per-user window layout.
The limits are 32 devices, 128 digital wires and 64 analog wires. Peripheral
graphs and local hardware modules are mutually exclusive. **Digital wiring** and
**Analog wiring** retain the table editors for explicit endpoint selection.

| Device | Digital pins | Analog pins / bus |
| --- | --- | --- |
| CPU | `cpu.P0` through `cpu.P3`, 8 bits each | XDATA initiator |
| 8255 | `A`, `B`, `C` (8 bits); active-high `RESET` | Four XDATA registers at configured base: A, B, C, control |
| ADC0804 | `data` (8 bits); active-low `CS`, `RD`, `WR`, `INTR` | `plus`, `minus` inputs |
| Parallel DAC / Covox | `data` (8 bits) | `out` voltage |
| Voltage source | — | `out`, editable live |
| Digital source | `out` (8 bits), editable live | — |
| Host audio | — | `capture` output and/or `playback` input |

In **Digital wiring**, each row joins equal-width slices beginning at Bit A and
Bit B. Bit numbers start at zero. For example, `cpu.P1`, bit 2, width 3 to a DAC's
`data`, bit 0 joins P1.2–P1.4 to DAC bits 0–2. Add rows for arbitrary bit
permutations or groups across ports. Digital connections are bidirectional nets.
The Live table displays resolved values, floating masks and contention masks.

CPU P1–P3 have weak pull-ups. P0 needs an external driver/pull-up for released
bits to read high. GPIO latch writes release high bits; read-modify-write
instructions use the CPU latch. Ordinary reads use resolved pin levels.

In **Analog wiring**, choose an output and input. One output can fan out, and
each input accepts one source. Values are in volts. The DAC outputs
`reference * code / 256`; the ADC quantizes its sampled differential input.
8255 Mode 0 and BSR are implemented; handshake Modes 1/2 are unsupported.
These are functional models at instruction boundaries, with no oscillator-phase
or electrical analog circuit simulation. See the SDK guide for detailed limits.

Debugger XDATA inspection uses passive peeks and never advances the device
clock. Mapped MOVX accesses bypass backing RAM; unmapped accesses retain normal
memory/watchpoint behavior. A fatal SDK error pauses execution and is cleared
by reset or loading a project with a corrected graph. Changing crystal frequency rebuilds
the graph at the new oscillator frequency and closes any open audio host.

## Host audio

1. Open a project containing a Host audio device and analog connections, such
   as the microphone/Covox or Covox duet examples linked above. Covox connects
   to `audio.playback`; microphone capture connects to `adc.plus`, with ground
   on `adc.minus`.
2. Select **Live → List audio devices**.
3. Select devices and zero-based channels. Playback channel `-1` uses all
   channels. Click **Open selected audio devices**. Capture may request OS
   microphone permission. Listing devices does not open them.
4. Use **Cycle exact** execution and Run. Pause, reset and stepping silence
   audio; the host resumes with Run. Instructions-per-frame execution is silent.
   **Close audio devices** releases them explicitly.

The desktop bridge uses mono 48 kHz samples and bounded queues. Capture maps
normalized ±1 samples to 0–5 V. Playback maps 0–5 V to ±1 without attenuation. Covox bytes use offset-binary
encoding: `(byte - 128) / 128`. Changing a steady DAC voltage produces DC rather than a tone; firmware
must vary its output to synthesize sound. Missing samples become silence and
miss counters are visible. They count **audio frames**, not underrun incidents,
and are **cumulative** across Run/Pause, reset and timeline resynchronization.
At 48 kHz, a 40 ms shortage alone adds 1,920 misses; an old total does not mean
playback is still missing samples.

To test buffering manually, open **Live → Audio signals**, enter **Audio buffer
(ms)** and click **Apply audio buffer**. The range is **10–2000 ms**; **0** selects
automatic buffering. Try 256, 512 or 1024 ms to compare underruns. The display
shows the applied latency and frame count, independently of the pending value.
Applying briefly stops/reopens the selected audio devices and resumes at the
current CPU position. CPU state, music position and gain are preserved. Larger
values add the corresponding delay before sound is heard. The queue capacity
grows automatically to support the requested latency. Save Project to retain
the setting; loading the project restores it with devices closed.

Automatic buffering uses the following defaults: playback-only desktop graphs
reserve **128 ms** of bridge buffering to absorb
host scheduling jitter. Capture/duplex graphs retain **42.7 ms** per direction to
limit microphone-monitoring latency. The Live tab shows the configured value.
A nearly 100% toolbar rate measures average CPU pacing; it cannot guarantee
that every native audio deadline was met. CPU/peripheral sample production now
runs on a separate execution worker: editor rendering, native dialogs and GPU
waits do not suspend it. A busy host can still exhaust a finite buffer. After a
worker/system suspension over one second, CPU pacing and audio timelines rebase
together, allowing playback to recover without reopening devices or resetting
the song.

Sustained real-time audio requires the emulator to
keep up with the chosen crystal speed; use an optimized build.

Audio device selections and open state are deliberately local to the running
session. Opening a project never opens the microphone or speakers. Automated tests use the software NULL backend; physical-device
permission, routing and sound quality require a manual test. For callback,
queue, reset and ownership contracts, see [Peripheral audio](peripheral-sdk/AUDIO.md).

## Creating additional devices

The LGPL [factory registry](peripheral-sdk/REGISTRY.md) lets C hosts discover
models and register custom factories with typed parameters and pin metadata.
The desktop uses it to instantiate the 8255, ADC and DAC. Its Add Device list
and project schema currently cover the built-in profiles; custom factories are
available to SDK hosts and do not appear automatically in that list.
