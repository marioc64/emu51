# emu51 v1.0.0

emu51 is an Intel 8051 emulator with a desktop debugger, an integrated assembly
editor and assembler, and command-line tools.

**Author and maintainer: Mariusz Kasolik.** Development began in 2001 on Windows
and DOS. Version 1.0.0 continues the original [SourceForge project](https://emu51.sourceforge.net/)
with a reusable emulation engine and a Dear ImGui interface. See
[HISTORY.md](HISTORY.md) for the project history.

## Features

- Selectable 8051/8052 CPU emulation with wired external RAM/ROM, shared CODE/XDATA
  storage, GPIO banking, a lookup address decoder and graph-configured Clock.
- Register, disassembly, internal RAM, and external memory views.
- Run, pause, reset, Step In, Step Over, and Step Out.
- Execution breakpoints, memory watchpoints, instruction tracing, and assembly
  source locations.
- Symbol lookup, named value watches, and source editor gutter breakpoints.
- Portable project sessions with program overlays, editor drafts, and debugger settings.
- Assembly editing with syntax highlighting, diagnostics, formatting, and
  compilation directly into emulator memory.
- Multiple independent assembly editors, sparse physical load mappings, and
  whole-chip/range binary images; see [memory circuits and projects](MEMORY_BUS.md).
- Save/discard/cancel protection, atomic source saves, and recovery drafts.
- Saved layouts, Follow PC settings, and execution preferences.
- Crystal-paced execution using the classic 12-clock 8051 timing model.
- Serial modes 0–3, Timer 1/SMOD and 8052 Timer 2 baud clocks, serial interrupts, and a byte-oriented terminal.
- Configurable peripheral graphs: 8255, ADC0804, Covox, LCD, keypad, switches/LEDs, shift registers, I²C EEPROM, AY-3-8910, and optional host audio.
- Generic bus output latches, input buffers and configurable key matrices;
  the [AVT-2250 circuit](progs/boards/avt2250/README.md) uses these with a user-supplied ROM.
- A standalone assembler, bounded console runner, and dispatch benchmark.

The timing mode paces emulated machine cycles against wall time. It does not
provide pin-level hardware simulation; see [execution timing and limitations](CYCLE_ACCURATE_EXECUTION.md).

## Build requirements

All targets require CMake 3.16 or newer and a C++20 compiler. Vendored dependencies
are included in the source tree; configuring the project does not download them.

| Platform | Additional GUI requirements |
| --- | --- |
| macOS | Xcode command-line tools with the macOS SDK; Cocoa, Metal, MetalKit, QuartzCore, and GameController frameworks |
| Linux | OpenGL 3.0 and GLFW 3 development libraries; Zenity or KDialog for native dialogs |
| Windows | A C++20-capable compiler and Windows/OpenGL libraries; CMake uses an installed GLFW target or the bundled GLFW fallback when available |

The macOS frontend uses Cocoa/Metal and does not require GLFW. Linux and Windows
use GLFW/OpenGL. On Linux, CMake skips the desktop executable if GLFW is absent;
OpenGL is still required while configuring that frontend path. The CPU, assembler,
console runner, and benchmark do not link GUI libraries. Application workflow
tests use ImGui without opening a desktop window.

Python 3 is needed for the source audit and the release-check script, not for
compiling the C++ targets or running CTest.

## Build and run

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

Build all default targets before running CTest: package and console-tool tests
also need the desktop executable, when configured, and all three console tools.
`--config Debug` selects the configuration for Visual Studio and other
multi-configuration generators. Single-configuration generators use
`CMAKE_BUILD_TYPE` instead.

Run the desktop executable for your build:

| Platform/generator | Executable |
| --- | --- |
| macOS, single configuration | `build/emu51/emu51.app/Contents/MacOS/emu51` |
| Linux, single configuration | `build/emu51/emu51` |
| Windows, Visual Studio Debug | `build\emu51\Debug\emu51.exe` |
| Windows, single configuration | `build\emu51\emu51.exe` |

Other multi-configuration builds also put executables or app bundles under their
configuration directory. Useful build options are `-DEMU51_WERROR=ON` for warnings
as errors and `-DEMU51_ASAN=ON` for AddressSanitizer/UBSan with Clang or GCC.

## Peripherals

Open **Window → Peripherals** to edit a graph or inspect its signals in **Live**.
Examples are loaded with **File → Open project**. On Linux this opens an
in-app browser where you can select an `.emu51` file or enter its path.

For microphone loopback, open the [example project](progs/asm/demo/microphone_covox.emu51),
then use **Live → List audio devices → Open selected audio devices** and **Run**.
Retain **System default** or select a physical microphone/playback device.
The example connects ADC data directly to P0 and P1 directly to Covox, with P3
controlling conversions. Its circular echo combines half the input with half
the delayed buffer byte, storing the result in both the buffer and P1.
`BUFFER_SIZE EQU 4096` sets the default delay to about 0.737 seconds. The
[assembly source](progs/asm/demo/microphone_covox.asm) and
[loopback instructions](PERIPHERALS.md#microphone--adc--p0-p1--covox--audio-output) are included.

For generated music without an ADC, open [covox_duet.emu51](progs/asm/demo/covox_duet.emu51),
open playback in **Live**, then **Run**. Two timer interrupts synthesize and mix
square-wave voices on P1 and sequence pitch/gate events from code memory.
Player status is in IRAM `30h..53h`. The [assembly source](progs/asm/demo/covox_duet.asm)
and [score/state documentation](PERIPHERALS.md#two-voice-covox-duet) are included.

CPU and peripheral/audio generation run on a dedicated execution worker.
Editing, file dialogs and graphics stalls can proceed while sound continues.
In **Live → Audio signals**, **Audio buffer (ms)** lets you test a smaller or
larger buffer; the setting is saved with the project.

Peripherals places its tabs beside the window name in the title bar; narrow
docked panes provide tab scrolling arrows. The **Graph** tab shows draggable blocks for the 8051/8052 CPU and all devices,
with every supported digital/analog port visible. Drag connector circles for
whole ports or numbered connectors for individual bits; select a wire to edit
its bit range. Analog squares and XDATA bus links have distinct colors. Wheel
zooms and middle/right drag pans. Right-click the canvas for **Add**, **Fit**,
**Arrange blocks**, and the **Inspector** toggle. Close the Inspector with the
× in its header and reopen it with **Inspector** in the Peripherals title bar.
Arrange blocks groups connected devices into columns and aligns their pins
where space permits. Wires leave each
connector perpendicular to the block edge, then bend around blocks with rounded
corners. They reroute when a block moves. The graph opens at a readable zoom.
Use **Fit → All blocks** for a circuit overview. New blocks appear at the
right-click position. The inspector edits block names and selected block or wire
settings. The Peripherals title supports the same docking targets as the other
windows. Projects save block positions and names.
The wiring tables remain available for precise edits. Click **Apply** in the
Peripherals title bar to activate graph and inspector settings; this validates
the draft, resets the CPU and pauses execution. Save Project stores the active
graph, live source values, and block names/positions.
Native audio requires explicit device opening and follows Run/Pause in Cycle
exact mode. See [Peripherals](PERIPHERALS.md) for wiring and audio instructions.

The **Live** tab is a compact hardware front panel: displays, LEDs, switches,
buttons and keypads, with audio and adjustable source controls as utilities.
Enable **Edit layout** to drag headers and resize corners; turn it off to use
controls. **Arrange** packs boxes into rows and **Compact** restores small device
sizes. **Save Project** retains the arrangement. CPU state and bare ICs stay out
of Live; wiring and device configuration remain in Graph.
Box contents scale when resized, including display pixels, text and controls.
Keypads offer **Toggle** (click to hold/release) and **While pressed** modes;
the selected mode is saved with the project.

Registers can be closed with its title-bar **X** and reopened with
**Window → Registers** on every platform. Visibility and docking are retained
in the per-user window layout.

## CPU models

Choose **8051** or **8052** in **Execution settings → CPU model**. The default is
8051 with 128 bytes of internal RAM. The 8052 provides 256 bytes, with upper RAM
physically separate from SFRs, plus Timer 2 capture, auto-reload, counter,
interrupt and UART baud-clock support. Both use classic 12-clock timing.

Changing models resets and pauses the CPU while preserving loaded program memory.
The selection is saved in project files; older projects default to 8051. Global
execution preferences save pacing settings only. Internal RAM views show the
selected model's RAM; SFRs are shown in Registers. Debugger watchpoints offer
separate direct RAM/SFR changes and internal RAM changes.

Use `$MOD51` or `$MOD52` in source as appropriate. Assembler modules define names;
they do not change the selected runtime model. Watchdogs, dual data pointers,
EEPROM, comparators, and derivative-specific Timer 2 extensions remain unsupported.
Programs can still use the full 64 KB external code/data spaces; on-chip ROM sizes
are descriptive and do not restrict the loader.

The console runner and benchmark accept `--model 8051` or `--model 8052`, for example:

```sh
build/emu51/emu51cli --model 8052 --steps 1000000 program.hex
```

## Loading programs and memory modes

The loader recognizes assembly source (`.asm`, `.s03`) and Intel HEX (`.hex`,
`.ihx`, `.ihex`), case-insensitively. Other extensions are treated as raw binary.
The GUI asks for a binary load address; console tools load binaries at address
zero. Intel HEX start-address records do not set the CPU's program counter.

- **Open program** validates the file, then stops execution, clears memory and
  CPU state, and loads the image. It becomes the target for **Reload program**.
- **Load program** overlays an image on the existing machine without resetting
  it or changing the reload target.
- **Reload program** opens the remembered program again, including its binary
  load address, and resets the machine after validation.

A failed preparation leaves CPU state, memory, execution counters, and the reload
target intact. Assembly source remains available in the editor when compilation
fails. Program loading and the editor both use the built-in assembler.

**Unified mode** uses the same external memory for program and data accesses.
With separate memory selected, program and external data have distinct storage.
Internal RAM and SFRs remain separate from these external spaces. Use the Memory
menu to change modes, clear memory, or fill a range. The generic examples under
`progs/` do not require proprietary board firmware; `mtask2.asm` expects unified
memory, as described in its source comments.

[Peripheral examples](PERIPHERALS.md#front-panel-serial-and-sound-chip-examples)
include an interactive LCD/keypad panel, chained shift registers, a persistent
EEPROM counter, an AY-3-8910 chord player, and
[segment-display examples](PERIPHERALS.md#segment-display-examples) covering
multiplexed clocks, BCD decoders, MAX7219/MAX7221, HT16K33 and TM1637. [SID.md](SID.md) assesses a separate
GPL reSID integration; SID is not yet an implemented device.

## Project sessions

Use **File → Save project** to create a `.emu51` project, **Save project as...**
to save a separate copy, and **Open project...** to restore it. These commands
are available in the native macOS menu and the Linux/Windows ImGui menu. Save
and Save As keep their existing assembly-source shortcuts. The window title
shows the selected project's filename.

**File → Open Recent Projects** lists the ten most recently opened or saved
projects, with the newest first. **Clear Menu** clears this list independently
of recent programs. The list is saved between application sessions; opening a
recent project uses the same validation and unsaved-source guard as Open project.

Projects retain the program load sequence, binary load addresses, reload target,
unified/separate memory mode, execution settings, numeric breakpoints,
watchpoints, named watches, debugger visibility, and the trace-recording choice. They also
retain the editor's file, cursor, visibility, and unsaved text. Saving a project
does not save over source files or clear the editor's modified flag relative to
the `.asm` file. Text already stored in the project closes without a save prompt.
Further edits use the normal save/discard/cancel protection until saved to the
source file or project.

Custom local builds may also retain a hardware-module requirement. Opening such
a project requires the matching local build; an unavailable module is reported
without changing the current session. Standard builds include no hardware module.

Paths are relative to the project file where possible; keep referenced files
with the project when moving its folder. File-based program loads and clean
editor buffers are reread from disk. In-memory assembly loads retain the main
source text that was compiled; includes still resolve from the source directory
and packaged modules. The restored editor draft may differ from the loaded
assembly, so normal source-matching checks still apply.

Opening first validates all inputs and asks about any current unsaved source.
An invalid project, missing file, assembly error, or canceled save leaves the
current machine intact. A successful open resets the CPU and memory, reapplies
the saved loads in order, and starts paused. Live CPU/register state, runtime
memory changes, trace history, and manual memory edits are not saved. Window
layouts continue to use the existing per-user preferences. Project execution
settings apply to the session without overwriting global preferences.

Project JSON is versioned and saved through atomic replacement. Current limits
are 32 program loads, 256 KiB per retained assembly source, a 1 MiB editor buffer/draft,
1,024 watchpoints and named watches each, and a 32 MiB project file. Exceeding a
limit fails the save without replacing an existing project.

## Assembly editor

The Build menu provides three actions:

| Action | Effect |
| --- | --- |
| Compile | Assemble the buffer and retain the image in memory |
| Load | Load the compiled image without resetting the machine |
| Compile and Load | Compile, then reset CPU/memory and load if successful |

Compilation does not create a temporary HEX file. Editing or replacing a buffer,
changing its source path, or a failed compilation invalidates its cached image.
Use Save or Save As to write source files. **Format Code** is available from the
Edit menu.

Replacing a modified buffer or quitting asks whether to save, discard, or cancel.
A canceled Save As or failed write cancels the pending operation. Atomic source
replacement protects the previous file on write failure. Loading tabs, CRLF line
endings, or a UTF-8 BOM does not itself mark the editor as modified.

Recovery drafts are saved while editing and before unsaved-change dialogs.
Startup offers to restore, discard, or keep abandoned drafts; drafts from another
running instance are skipped. Recovery is a fallback for interrupted sessions,
not a replacement for saving source files.

## Debugger

Open **Debugger** from the toolbar, or from the native Execution menu on macOS.
Double-click an address in a disassembly view to toggle a breakpoint. Its context
menu offers **Run to cursor** and **Show source**. The Debugger window also offers
run-to-editor-cursor and breakpoint/watchpoint controls.

Assembly loads retain symbols from the main file and includes. Labels appear
beside their addresses in disassembly. The Debugger's **Address or symbol** field
and the disassembly **G** dialog accept case-insensitive names such as `main_loop`
as well as hexadecimal addresses. Names take precedence over bare hex; use an
explicit `0x` prefix to disambiguate an address such as `0xFACE`. Breakpoints
resolve to fixed addresses and are not automatically relocated by later builds.

Expand **Symbols** to filter names and add **Watches**. These show internal
RAM/SFR bytes for `DATA`, individual bits for `BIT`, constant values for `EQU`,
and program bytes for labels. Watches follow names in the current loaded image;
missing names are marked unavailable. They inspect values without stopping the
CPU. **Watchpoints** below control execution stops and use the selected memory
space; bit symbols must be inspected through Watches or watched by byte address.

| Watchpoint space | Behavior |
| --- | --- |
| External data | Read and/or write accesses during CPU execution |
| Program | Read and/or write accesses, including instruction fetches |
| Internal RAM/SFR | Value changes between instruction boundaries |

Internal watches do not detect reads, writes that preserve the value, or changes
that are undone within the same instruction. UI inspection does not trigger
watchpoints. Breakpoints stop before an instruction; watchpoints stop after the
instruction finishes. Step In bypasses the current execution breakpoint once.

Source locations refer to the most recently loaded assembly image, including
included files. Click the editor gutter beside an instruction to toggle its
breakpoint, or right-click its line number for breakpoint/run-to-line controls.
The matching editor buffer shows the PC line in dark green. The editor's text
must match the source captured during assembly for gutter controls, PC highlighting,
and source navigation; tab, CRLF, and BOM normalization is allowed. Saving changed
text alone does not restore matching locations: compile and load again. Included
files are checked when opened for source navigation; files are not monitored in
the background. Program writes invalidate source locations for the affected line.
HEX/binary loads replace assembly debug information with an empty symbol/source map.

Optional tracing retains the latest 4,096 instructions, including PC, three raw
bytes starting at PC, next PC, cycle cost, A, PSW, and SP. Step Over/Out run as
cancelable batches with a one-million-instruction action limit. Pause or a
debugger stop cancels the pending step. See [execution timing](CYCLE_ACCURATE_EXECUTION.md)
for batching and main-thread limitations.

## Serial terminal

Open **Execution → Serial terminal** on any platform. The firmware configures
SCON, PCON, and Timer 1; the terminal displays that configuration and sends bytes
at its simulated baud rate. Mode 0 uses eight shift clocks; modes 1 and 3 use
Timer 1 overflows; mode 2 uses the fixed oscillator-derived baud rate. Serial
interrupts share vector `0023h`, with EA/ES enable and PS priority controls.
Reading SBUF accesses received data; writes start transmission, including writes
of the same value. Firmware must clear RI and TI itself.

Send text or space-separated hexadecimal bytes such as `41 00 FF`. Select None,
LF, CR, or CRLF endings and, for nine-bit modes, the receive ninth bit. **Send**
queues input without starting the emulator. Input waits while paused, while
REN is clear, or while the configured baud clock is stopped. **Clear pending
input** cancels queued/in-flight host input without changing SBUF or RI.

**Send file...** chooses a file to send into the UART. **File endings → Unchanged**
(the default) sends every byte exactly as stored, including binary data. For text
receivers that require specific line endings, select **LF**, **CR**, or **CRLF**
before choosing the file. These modes normalize existing CRLF pairs, lone CR and
lone LF, including pairs split across streaming chunks. The source file is never
modified, and no extra ending is appended at EOF.

**File line delay (ms)** inserts extra emulated time between complete source
lines after the preceding UART input drains. Zero (the default) sends continuously;
20 ms suits TinyBASIC program uploads. CRLF is one source-line boundary. Pausing
also pauses the delay. The selected delay is captured when the transfer starts.

File transfers ignore the manual input's Hex input and Ending controls, and
capture File endings and the receive ninth bit when started. Large files stream
through the bounded input queue as the emulator executes; progress reports source bytes queued,
which may still be waiting for reception. Pausing, disabling REN, or stopping
the baud clock eventually fills the queue and pauses file reading. Sending text
is disabled until the entire file is queued or the transfer is canceled.
**Cancel file** and **Clear pending input** stop file reading and clear all
pending input. CPU reset or opening a program/project also cancels transfers;
projects do not save or resume active transfers. File selection does not start execution.
Enter the firmware's receive command before sending. Clear pending input before
a new transfer if a receiver stopped before consuming the preceding file.

Output can be copied as text or hex; text hides CR, keeps LF/tab, and escapes
other non-printable/non-ASCII bytes as `\xNN`. Hex output marks set ninth bits
with `/1`. Input and the retained output transcript are bounded to 4,096 bytes.
The window reports receive overruns, premature transmit writes, and core output
queue drops. CPU reset clears pending serial state but keeps the transcript;
opening a program/project clears it. Projects retain terminal visibility,
manual/file line endings and the file line delay. Serial traffic and other terminal input controls remain runtime state.

Try [serial_echo.asm](progs/asm/demo/serial_echo.asm): open it, select an
11.0592 MHz crystal for 9600 baud, open the terminal, and Run. It prints `>` and
echoes input using serial interrupts. The serial model operates at instruction
boundaries; see [timing limits](CYCLE_ACCURATE_EXECUTION.md#serial-model).

For an interactive language demo, open
[TinyBASIC's project](progs/tinybasic/tinybasic.emu51). It selects the 8052 CPU,
11.0592 MHz crystal, CR endings and upload pacing automatically. Run, send
`PRINT 2+3`, then upload `progs/tinybasic/squares.bas`, send `LIST`, and send `RUN`.
See the [TinyBASIC walkthrough](progs/tinybasic/README.md) for expected output,
INPUT examples and source provenance.

## Execution settings and shortcuts

Execution settings offers **Cycle exact**, defaulting to a 12 MHz crystal, and
**Instructions per frame**, defaulting to 500 instructions per logical 1/60-second
frame. Display refresh does not determine execution speed.

macOS application shortcuts use native menus. Linux and Windows use the ImGui
menu/shortcut path. Menu-only actions are marked with an em dash.

| Action | macOS | Linux / Windows |
| --- | --- | --- |
| Open program | Cmd+O | — |
| Load program | Cmd+L | — |
| Reload program | Cmd+Option+R | — |
| Save source | Cmd+S | Ctrl+S |
| Save source as | Cmd+Shift+S | Ctrl+Shift+S |
| Compile | Cmd+B | Ctrl+B |
| Load compiled image | Cmd+Shift+L | Ctrl+Shift+L |
| Compile and Load | Cmd+Shift+B | Ctrl+Shift+B |
| Run / pause | F9 | F9 |
| Reset CPU, preserving Run/Pause state | Cmd+R | Ctrl+R |
| Reset CPU and stop | Cmd+Shift+R | Ctrl+Shift+R |
| Step In | F7 | F7 |
| Step Over | F8 | F8 |
| Step Out | Shift+F8 | Shift+F8 |
| New Disassembly View | Cmd+D | — |

In an active disassembly or memory pane, G opens its address dialog, Up/Down
scroll, Enter submits dialog input, and Escape closes a dialog. The assembly
editor also provides normal text-editing, find/replace, and indentation shortcuts.

## Configuration

Preferences use the per-user `emu51` directory:

| Platform | Directory |
| --- | --- |
| macOS | `~/Library/Application Support/emu51/` |
| Windows | `%APPDATA%\emu51\` (or `%USERPROFILE%\AppData\Roaming\emu51\` when APPDATA is absent) |
| Linux | `$XDG_CONFIG_HOME/emu51/`, otherwise `~/.config/emu51/` |

`settings.json` holds execution preferences. `state.json` holds docking/layout
state, each disassembly view's Follow PC choice, Registers and peripheral
inspector visibility, and both recent program and project lists. These are
saved together by atomic replacement. `imgui.ini` holds ImGui window positions,
sizes, and collapsed state; `editor-recovery/` holds recovery sessions.

When `state.json` is absent, the application imports `imgui_layout_state.txt`,
`recent_programs.txt`, and `recent_projects.txt` automatically. The old files
remain as backups and are no longer updated. An existing `state.json` takes
precedence; an invalid or unsupported file is preserved and disables layout/history
saving for that session. Persistence can be unavailable if the configuration
directory cannot be created.

Follow PC sits in the disassembly title bar; narrow windows show a compact
checkbox with a Follow PC tooltip.

## Assembler CLI

These examples use single-configuration paths. For Visual Studio Debug, replace
`./build/emu51/emu51_asm` with `build\emu51\Debug\emu51_asm.exe`.

```sh
./build/emu51/emu51_asm progs/asm/demo/mtask2.asm -o build/mtask2.hex
./build/emu51/emu51_asm progs/asm/demo/mtask2.asm -o build/mtask2.bin --format bin
./build/emu51/emu51_asm path/to/program.asm -o build/program.hex -I path/to/includes
```

The input and `-o` output path are required. Output defaults to Intel HEX; a
`.bin` output extension selects binary, or use `--format bin`. Use `.hex` when
requesting HEX output. `-I` can be repeated. The assembler CLI currently has no
`--help` option; invoking it without arguments prints its usage.

Supported directives include `ORG`, `DB`, `DW`, `END`, `EQU`, `DATA`, `BIT`,
`$MOD51`, `$MOD52`, device-specific `$MOD...` forms, and `$INCLUDE(...)`.
Diagnostics contain file/line locations.
Expressions use checked signed 64-bit arithmetic with a nesting limit of 128;
positive literals above `INT64_MAX` are rejected. Binary output starts at the
image's lowest emitted address and zero-fills gaps, so load it at that original
address if it is not zero. HEX retains addresses.

Packaged [device modules](emu51/asm/MODULES.md) cover standard 8051/8052,
Philips 80C851, AT89S51, AT89S52 and AT89C2051 register symbols. The existing
`$MOD851` remains compatible; `$MOD8051` and `$MOD8052` alias `$MOD51` and `$MOD52`.
Choose one module per assembly unit. These are assembler definitions; including
a variant does not add its peripherals to the emulator's 8051 runtime model.

Instruction metadata is compiled into the binaries. The device modules are
packaged text includes. Include lookup checks the current source file's directory,
resources relative to the executable, then explicit `-I` directories. An application
using `emu51_asm_lib` can supply `AssembleOptions::include_directories` itself.

## Console runner and benchmark

The runner requires a program path and stops after the requested number of
instructions; its default is 1,000,000. It runs at full speed rather than using
the GUI pacing setting. It reports instructions, cycles, PC, SP, and a state hash.

```sh
./build/emu51/emu51cli --steps 100000 progs/hex/mtask2.hex
./build/emu51/emu51cli --help
```

Use a Release build for benchmarking:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release --parallel
./build-release/emu51/emu51_bench --steps 5000000 --repeats 5
```

The benchmark defaults to a deterministic built-in workload; an optional program
path selects ASM, HEX, or binary input. It compares switch and specialized
function-table dispatch, warms both paths, alternates trial order, reports median
throughput, and checks state hashes and cycle totals. Setup, loading, and hashing
are outside the timed loop. Results depend on compiler and workload. Both tools
start at PC zero; binary input is loaded at zero. On multi-configuration builds,
use the executable under `Release/`. Both tools accept `--help`.

## Installation and release packages

After building all targets, stage an installation:

```sh
cmake --install build --config Debug --prefix /path/to/package
```

The default layout contains `bin/emu51_asm`, `bin/emu51cli`, `bin/emu51_bench`, the
GUI as `bin/emu51` or a root-level macOS `emu51.app`, and the device modules under
`share/emu51/asm/`. Windows tools have an `.exe` suffix. Documentation and licenses
are under `share/doc/emu51/`; `SHA256SUMS` covers installed files. The
macOS app has its own resource, license, and notice copies and can be moved by
itself. GNUInstallDirs settings can change the install paths.

CPack produces TGZ archives on macOS/Linux and ZIP archives on Windows.
[RELEASE.md](RELEASE.md) covers optimized builds, runtime dependencies, optional
signing/notarization, package verification, and corresponding source archives.

## Tests and contributions

CTest registers five tests: `emu51_tests` (CPU/assembler/settings/files),
`emu51_ui_tests` (headless application workflows), `emu51_packaged_resources`
(resource lookup), `emu51_headless_tools` (CLI behavior), and
`emu51_installed_package` (real installation, relocation, manifests, and notices).
The source audit is separate:

```sh
python3 scripts/check_public_sources.py
```

The CI workflow defines Linux GCC/Clang, macOS Clang, and Windows MSVC builds,
package checks, and a Linux Clang sanitizer job. These automated checks do not
replace desktop testing on the target platform. See [CONTRIBUTING.md](CONTRIBUTING.md)
for architecture and implementation conventions.

Only generic 8051 examples belong in this repository. Proprietary firmware,
board includes, local configuration, and private development history are
excluded from public sources and binary packages.

## License and third-party code

Copyright (C) 2001–2026 Mariusz Kasolik. Project code, documentation, and examples
are licensed under **GNU GPL version 2 only** (`GPL-2.0-only`), without warranty,
except for the independently buildable [peripheral SDK](peripheral-sdk/README.md).
The SDK's headers, implementation, documentation and examples use
**LGPL-2.1-only**; its emulator host adapter remains GPL-2.0-only. The SDK currently
provides a headless C API for bus maps, digital port/bit wiring and analog signals,
plus an ideal Covox-style parallel DAC, an 8255A Mode 0/BSR profile and an
ADC0804 functional profile. The GPL CPU adapters connect XDATA and
P0..P3 with pin/latch semantics and instruction-boundary timing. Its
[factory registry](peripheral-sdk/REGISTRY.md) supports discovery, typed parameters
and custom C factories. Run the
`emu51_peripheral_gpio_demo` build target for a CPU-to-DAC example, or
`emu51_peripheral_adc_demo` for an interrupt-driven CPU/8255/ADC/DAC board.
The desktop [Peripherals window](PERIPHERALS.md) configures and saves these graphs. The optional [native audio host](peripheral-sdk/AUDIO.md)
connects analog endpoints to selected input/output devices through bounded
callback queues; the audio demo provides explicit device/channel selection.
See [LICENSE](LICENSE), [COPYRIGHT](COPYRIGHT), and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Vendored components and embedded
font data retain their respective licenses.
