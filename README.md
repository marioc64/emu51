# emu51 v1.0.0

emu51 is an Intel 8051 emulator with a desktop debugger, an integrated assembly
editor and assembler, and command-line tools.

**Author and maintainer: Mariusz Kasolik.** Development began in 2001 on Windows
and DOS. Version 1.0.0 continues the original [SourceForge project](https://emu51.sourceforge.net/)
with a reusable emulation engine and a Dear ImGui interface. See
[HISTORY.md](HISTORY.md) for the project history.

## Features

- 8051 CPU emulation with unified or separate program/data memory.
- Register, disassembly, internal RAM, and external memory views.
- Run, pause, reset, Step In, Step Over, and Step Out.
- Execution breakpoints, memory watchpoints, instruction tracing, and assembly
  source locations.
- Assembly editing with syntax highlighting, diagnostics, formatting, and
  compilation directly into emulator memory.
- Save/discard/cancel protection, atomic source saves, and recovery drafts.
- Saved layouts, Follow PC settings, and execution preferences.
- Crystal-paced execution using the classic 12-clock 8051 timing model.
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
| macOS, single configuration | `build/emu51/emu51_imgui.app/Contents/MacOS/emu51_imgui` |
| Linux, single configuration | `build/emu51/emu51_imgui` |
| Windows, Visual Studio Debug | `build\emu51\Debug\emu51_imgui.exe` |
| Windows, single configuration | `build\emu51\emu51_imgui.exe` |

Other multi-configuration builds also put executables or app bundles under their
configuration directory. Useful build options are `-DEMU51_WERROR=ON` for warnings
as errors and `-DEMU51_ASAN=ON` for AddressSanitizer/UBSan with Clang or GCC.

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
included files. The matching, unmodified editor buffer shows the PC line in dark
green. Recompile and load after changing source; locations are not a live source
checksum check. HEX and binary images contain no source map.

Optional tracing retains the latest 4,096 instructions, including PC, three raw
bytes starting at PC, next PC, cycle cost, A, PSW, and SP. Step Over/Out run as
cancelable batches with a one-million-instruction action limit. Pause or a
debugger stop cancels the pending step. See [execution timing](CYCLE_ACCURATE_EXECUTION.md)
for batching and main-thread limitations.

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

`settings.json` holds execution preferences. `imgui.ini` and
`imgui_layout_state.txt` hold window/layout state, including each disassembly
view's Follow PC choice. `recent_programs.txt` holds recent paths, and
`editor-recovery/` holds recovery sessions. Persistence can be unavailable if the
configuration directory cannot be created.

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
`$mod851`, and `$INCLUDE(...)`. Diagnostics contain file/line locations.
Expressions use checked signed 64-bit arithmetic with a nesting limit of 128;
positive literals above `INT64_MAX` are rejected. Binary output starts at the
image's lowest emitted address and zero-fills gaps, so load it at that original
address if it is not zero. HEX retains addresses.

Instruction metadata is compiled into the binaries. `mod851` is a packaged text
include. Include lookup checks the current source file's directory, resources
relative to the executable, then explicit `-I` directories. An application using
`emu51_asm_lib` can supply `AssembleOptions::include_directories` itself.

## Console runner and benchmark

The runner requires a program path and stops after the requested number of
instructions; its default is 1,000,000. It runs at full speed rather than using
the GUI pacing setting. It reports instructions, cycles, PC, SP, and a state hash.

```sh
./build/emu51/emu51 --steps 100000 progs/hex/mtask2.hex
./build/emu51/emu51 --help
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

The default layout contains `bin/emu51_asm`, `bin/emu51`, `bin/emu51_bench`, the
GUI under `bin/` or as a root-level macOS `.app`, and
`share/emu51/asm/mod851`. Windows tools have an `.exe` suffix. Documentation and
licenses are under `share/doc/emu51/`; `SHA256SUMS` covers installed files. The
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
are licensed under **GNU GPL version 2 only** (`GPL-2.0-only`), without warranty.
See [LICENSE](LICENSE), [COPYRIGHT](COPYRIGHT), and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Vendored components and embedded
font data retain their respective licenses.
