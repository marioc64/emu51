# Contributing

Start with [README.md](README.md) for build commands and application behavior,
[RELEASE.md](RELEASE.md) for packaging, and
[CYCLE_ACCURATE_EXECUTION.md](CYCLE_ACCURATE_EXECUTION.md) for timing limits.

## Build and validation

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DEMU51_WERROR=ON
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
python3 scripts/check_public_sources.py
```

Build all default targets before running CTest. The installed-package test needs
all installable executables, including the GUI when it is configured. For
Clang/GCC sanitizer builds, add `-DEMU51_ASAN=ON` in a separate build directory.
Single-configuration and multi-configuration output paths differ as described
in the README.

Keep changes to vendored code separate from application changes. Preserve
upstream license notices; project-owned code uses GPL-2.0-only. Do not commit
build output, credentials, private firmware, board includes, or local settings.
The source audit checks tracked content and paths in reachable Git history; it
is not a complete secret scanner or license audit.

## Source layout and targets

| Location | Responsibility |
| --- | --- |
| `emu51/core/` | CPU state, opcode semantics, disassembly, and shared metadata |
| `emu51/asm/` | Assembler, include/resource lookup, program loader, assembler CLI |
| `emu51/imgui/` | Shared application state, panes, menus, pacing, and settings |
| `emu51/imgui/macos/` | Cocoa menus, Metal frontend, and native execution timer |
| `emu51/imgui/emu51_imgui_glfw.cpp` | Shared Linux/Windows GLFW/OpenGL frontend |
| `emu51/io/` | Atomic saves, editor recovery, and project file encoding |
| `progs/` | Generic assembly, HEX, and binary examples |
| `resources/` | Shared application artwork and Linux desktop integration |
| `cmake/` | Packaging, deployment, manifest, and relocation checks |
| `scripts/` | Public-source and release checks |
| `third_party/` | Vendored dependencies and their upstream documentation |

Executables are `emu51_imgui`, `emu51_asm`, `emu51`, `emu51_bench`, `emu51_tests`,
and `emu51_ui_tests`. `emu51_core_lib` supplies the core; `emu51_asm_lib` links it
and exposes assembly/loading services. Settings, file storage, and shared ImGui
code have their own build targets. Strict warning flags apply to project-owned
sources, not the vendored UI libraries.

## CPU and opcode metadata

`emu51/core/opcode_metadata.cpp` contains one entry for every opcode byte, with
mnemonic, operand pattern, length, machine-cycle cost, and assembler match order.
The core, disassembler, assembler, and editor mnemonic list share it. Reserved
A5 has no assembly mnemonic. Preserve match ordering when changing overlapping
assembly forms; encoding choices can otherwise change for existing sources.

`process()` uses switch dispatch. `process_table()` uses a table of specialized
handlers generated from the same opcode implementation. Both paths finish through
`finalize_instruction()`, which updates parity/register-bank state, advances
timers, and services supported interrupts. The benchmark checks their end-state
hashes and cycle totals; regression tests also compare intermediate behavior.

## Assembly and loading

`Assembler::assemble_file()` reads files. `Assembler::assemble_string()` supports
in-memory buffers with a source name and optional base directory. Results carry
bytes, symbols, diagnostics, and address-to-file/line mappings. Includes resolve
from the current file's directory, packaged resource directories, then explicit
include paths. Do not add an implicit dependency on the development `progs/` tree.

`prepare_program_file()` parses and validates an image without mutating a live
machine. Callers reset only after successful preparation, then apply bytes with
`apply_program_image()`. The GUI owns reset, reload-target, and recent-file
behavior. Preserve that separation when adding formats or entry-point handling.

`AssemblyImage` retains symbols, exact source text for the main file and includes,
and instruction start addresses alongside byte-to-source mappings. `AssembleResult`
also retains its existing symbols map for API compatibility. HEX/BIN serialization
emits only bytes. Both file loading and editor compilation pass the same debug
metadata to the GUI; an overlay replaces the previous image's debug information.

Editor compilation caches an `AssemblyImage` per application instance. Buffer
changes, failed compilation, and path changes invalidate the cache. Loading the
image is an in-memory operation. The GUI retains source locations from the last
loaded assembly image; a program write invalidates all mappings from the affected
source line, including operand bytes and wrapped instructions. Editor source
actions require normalized text equality against the captured source; they do not
rely on the saved/dirty flag. Source files are compared when opened, without a
background disk watcher. Symbols resolve case-insensitively, before bare hex.
Named value watches are passive; DATA/BIT use the internal RAM/SFR mapping, EQU
shows its constant, and labels show a program byte. Breakpoints remain numeric.
Editor gutter controls use the vendored editor's public decorator/context-menu
callbacks; platform menu and shortcut ownership stays unchanged.

## UI and platform conventions

The shared state and CPU/memory callbacks live in `emu51_imgui_app.*`; debugger
controls live in `emu51_imgui_debugger.cpp`. Memory inspection must remain passive:
watchpoint callbacks are active only while an instruction is executing. Internal
RAM/SFR watches compare values across an instruction rather than intercepting
all reads and writes.

On macOS, the native Cocoa menu is the shipping application menu. The ImGui menu
is visible temporarily for development. Overlapping shortcuts must be handled
by Cocoa, so a keypress cannot invoke the same action twice. Linux/Windows use
the ImGui application menu and shortcut path. The README is the user-facing
shortcut reference; bindings are implemented in `emu51_imgui_actions.cpp` and the
Cocoa menu setup.

Paused applications avoid unnecessary rendering. Scrolling, dragging, editor
animation, and pending recovery saves must request enough updates to remain
smooth. In particular, macOS input must not be coalesced into too few rendered
frames. Linux/Windows wait for events while fully idle and use timed waits while
animation or execution is active.

CPU execution, editor access, and rendering share the main thread. Service work
is independent of drawing but is not a background worker. Keep mutations on that
thread unless a future change introduces explicit synchronization and snapshots.
See the timing document for budget, stepping, and suspension rules.

## Files, preferences, and recovery

`io/emu51_project.*` defines the versioned project model and bounded KaSON
schemas. It validates settings, addresses, collection sizes, and watchpoint
semantics before returning a project. Paths are stored as relative UTF-8 paths
where possible, with canonical parent directories handling symlinked paths.
Project writes share the editor's atomic replacement implementation.

`imgui/emu51_imgui_project.cpp` owns project dialogs, capture, and restoration.
Successful program/editor-image loads retain a load recipe; full machine/program
clears discard it. Opening prepares every image and editor file before resetting
the machine. If the unsaved-source guard saves an input file, preparation runs
again before applying. Restoration starts paused, clears transient debugger
actions/dialogs, and does not persist the project's execution settings globally.
Unsaved editor text is embedded separately from compiled source, so source
matching still detects drafts that differ from loaded code. Untitled builds
retain their include base directory. Projects preserve source/load recipes,
not runtime CPU/memory snapshots or per-user window layouts.

Recent projects are kept separately from recent programs in `recent_projects.txt`.
Successful opens and saves update a deduplicated ten-entry list, persisted through
atomic replacement. Failed/canceled operations preserve the order; the native
and ImGui recent-project menus use the same guarded project-opening path.

Source saves use sibling temporary files and atomic replacement; POSIX
permissions and symlinks are preserved. Cancellation or write failure must leave
the pending editor replacement/quit action canceled. Text normalization on load
must not create a false modified-buffer state.

Recovery sessions use OS locks to avoid restoring another live instance's draft.
Draft saves are scheduled after up to 500 ms while the UI is serviced, and before
unsaved-change dialogs. Window-close and quit paths on both frontends must use
the same unsaved-buffer guard.

`emu51_execution_settings.cpp` uses the vendored KaSON schema API to load/save
`settings.json`. Missing fields use defaults; malformed documents use the full
default settings. Numeric bounds are validated by the application. Follow PC is
stored per disassembly view in layout state, not in execution settings. The
README lists preference filenames and platform directories.

## Packaging and checks

`cmake/Packaging.cmake` defines explicit binary install lists and CPack metadata.
The project version comes from the top-level CMake `project()` declaration.
`cmake/AssemblerModules.cmake` explicitly lists the packaged device modules.
They are copied to each build configuration's resource directory and to the
macOS app; install lookup uses paths relative to executables. Module contents
and manufacturer references are documented in `emu51/asm/MODULES.md`.

`cmake/AppIcon.cmake` packages the shared application icon for macOS, embeds the
Windows executable resource and GLFW window icons, and installs Linux desktop
and hicolor theme entries. See `resources/icons/README.md` for asset regeneration.

The finalization script signs staged macOS executables when configured, audits
package paths, and creates `SHA256SUMS`. The installed-package test validates real
installation and relocation, required notices, checksums, and all three console
tools. It does not launch the desktop frontend. CPack source packaging is disabled;
source releases use `git archive` from a committed public revision.

The CI workflow defines warnings-as-errors builds on Linux, macOS, and Windows,
package verification, public-source checks, and a Linux sanitizer build. Record
actual platform validation separately from the presence of those workflow jobs.
