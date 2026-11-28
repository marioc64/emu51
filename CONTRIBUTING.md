# Contributing

Start with [README.md](README.md) for build commands and application behavior,
[RELEASE.md](RELEASE.md) for packaging, and
[CYCLE_ACCURATE_EXECUTION.md](CYCLE_ACCURATE_EXECUTION.md) for timing limits.
The physical-memory workflow is documented in [MEMORY_BUS.md](MEMORY_BUS.md),
with the implementation design in [MEMORY_BUS_PLAN.md](MEMORY_BUS_PLAN.md).

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

`emu51_ui_tests` covers title-bar Apply and draft/project separation, the actual
rendered colon geometry for 7/14/16-segment displays, and saved editor buffers
(including failed saves, undo/redo, and source-file preservation). On macOS,
`emu51_project_picker_tests` exercises the real dialog backend with simulated
selections and cancellation. It compiles generated AppleScript with the system
`osacompile` tool without displaying a file picker, including repeated opens and
paths containing spaces. Run these with
`ctest --test-dir build -C Debug -R 'emu51_(ui|project_picker)_tests' --output-on-failure`.

`emu51_memory_bus_tests` validates the LGPL storage/decoder/routing contracts.
`emu51_memory_project_tests` covers sparse programming, overlays, include drafts,
binary lengths, migration and schema round trips. `emu51_memory_ui_tests` covers
independent editors, physical source identity, Clock Apply, power-cycle recipes
and failed replacement. The execution-worker timing check runs serially because
it measures audio delivery during real UI stalls.

Keep changes to vendored code separate from application changes. Preserve
upstream license notices; project-owned code uses GPL-2.0-only, except the
`peripheral-sdk/` subtree, which uses LGPL-2.1-only. Do not move GPL code into the
SDK under a new license without the relevant copyright holders' authorization.
Do not commit build output, credentials, private firmware, board includes, or
local settings.
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
| `peripheral-sdk/` | Standalone LGPL C API, wiring runtime, examples and tests |
| `emu51/peripherals/` | GPL CPU adapters, graph configuration and desktop session for the SDK |
| `progs/` | Generic assembly, HEX, and binary examples |
| `resources/` | Shared application artwork and Linux desktop integration |
| `cmake/` | Packaging, deployment, manifest, and relocation checks |
| `scripts/` | Public-source and release checks |
| `third_party/` | Vendored dependencies and their upstream documentation |

Executables are `emu51` (desktop), `emu51cli` (console), `emu51_asm`,
`emu51_bench`, `emu51_tests`, and `emu51_ui_tests`. `emu51_core_lib` supplies the core; `emu51_asm_lib` links it
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

`core/emu51_model.h` defines stable 8051/8052 IDs and capabilities. `IRAM` and
`SFR` are separate storage: use `readDirect()`/`writeDirect()` for direct operands,
`readIndirect()`/`writeIndirect()` for host RAM access, and the internal
`indirect()` reference helper for opcode/stack operands. Register banks live in
IRAM. Raw `SFR` indices below 80h no longer represent RAM. `setModel()` resets CPU
state; `init()` preserves the selected model. External program/data mappings are
independent of model selection.

`core/emu51_timer2.cpp` advances classic 8052 Timer 2 and supplies overflow counts
to the UART. TCLK/RCLK select independent serial clocks; Timer 2 interrupts have
their own source, priority and vector. External T2/T2EX inputs are sampled at
instruction boundaries, not oscillator phases. `model_tests.cpp` exercises both
dispatch implementations. `validation_tests.cpp` supplies independent exhaustive
arithmetic/flag checks, opcode cycle costs and 8051 memory/stack edge cases from
the manufacturer instruction specification. Project `cpu_model` defaults to `8051`; watchpoint space
0 retains direct RAM/SFR semantics and space 3 observes internal RAM separately.

`emu51_port_io` is an optional core interface for passive pin reads, latch writes,
peripheral clocks and reset. The GPL `Emu51PeripheralGpio` adapter registers the
CPU's four ports in the LGPL SDK. Ordinary port reads sample resolved pins;
`readLatch()` and port RMW operands read output latches. Bit writes pass through
`writeDirect()` so devices see every output transition. Legacy raw latch edits
synchronize before instruction fetch. SDK time advances before external-input
sampling, timers/UART and interrupt arbitration, including separate entry cycles.
GPIO hosts must not also call the bus adapter's manual clock advancement.
The standalone core's latch-only and explicit interrupt-trigger behavior remains
available without a GPIO interface. See the SDK guide for electrical limits,
reset/lifetime rules and sticky host error reporting.

## Assembly and loading

`io/emu51_project_build.*` compiles independent units and plans a complete set of
physical writes before mutating chips. `io/emu51_project_migration.cpp` converts
legacy fallback arrays and fixed MMIO into explicit memory/decoder/Clock wiring.
`peripherals/emu51_memory.cpp` adapts CPU CODE/XDATA and passive resolution to
the LGPL wired bus. Keep direct programming separate from CPU writes.
`imgui/emu51_project_editor.cpp` owns independent editor bundles and programming
rows; `emu51_physical_sources.cpp` selects loaded sources by physical identity.
Source refresh runs on the UI thread and checks routing plus relevant storage
page revisions; it must never touch TextEditor from the execution worker.

`core/emu51_serial.cpp` owns bounded byte queues and transmit/receive frame
progress. Direct byte operands use `readDirect()`/`writeDirect()` so every SBUF
destination triggers transmission without overwriting its receive register.
Raw `SFR`/`SBUF` references are passive/legacy state access; host code must use
`writeDirect(0x99, byte)` to transmit and `queueSerialReceive()` to offer input.
Bit operands retain their distinct bit-address mapping (bit 99h is TI, not SBUF).
Timer helpers report overflow counts independently of sticky TF flags. UART work
advances after timers, including during interrupt entry, before the next poll.
No host sleeps or worker threads are involved.

`imgui/emu51_imgui_serial.cpp` drains completed output after each CPU instruction,
including while the terminal is hidden. Input is parsed/size-checked completely
before enqueueing, and inspection/drawing never advances the CPU. Optional
`serial_terminal_open` project state defaults to false for existing version-1
projects. Queues/transcripts are runtime state. See the timing document for
frame accuracy limits and `serial_tests.cpp` for both-dispatch regression vectors.

Serial file input uses a binary file stream, refilling the existing bounded
receive queue in chunks before CPU instructions. It runs independently of panel
visibility. The default preserves raw bytes without text/hex parsing. Optional
file ending normalization snapshots the selected mode, coalesces CRLF across
chunk boundaries, and reserves up to two output bytes per source byte so CRLF
expansion cannot overflow the receive queue. Progress counts source bytes read;
normalization never rewrites the source or appends an ending at EOF. Optional
file line pacing stops at a source CR/LF/CRLF boundary, waits for queued reception
to drain, then waits against emulated cycle counts. No host sleeps are involved;
reset cancels transfers and the selected delay is snapshotted at start. Projects
save manual/file endings and the preferred line delay, with zero defaults for
older files. The TinyBASIC workflow test loads the public-domain reference image,
checks its bytes, uploads a BASIC file, and tests expression evaluation, INPUT,
LIST and FOR/NEXT execution without UART overruns.
File selection/validation does not mutate CPU state. Cancellation and CPU-reset
paths close the stream; file progress and pending transfers are runtime state.

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

Normal CPU/peripheral execution runs on `Emu51ExecutionWorker`; editor access
and rendering stay on the UI thread. Frontends serialize model access with
`app.access()`. See the timing document for budget, stepping, and suspension rules.

## Optional local hardware

The experimental [peripheral SDK](peripheral-sdk/README.md) is a separate,
headless device/wiring foundation. It supports bus windows, digital nets with
port slices and analog voltage connections. `emu51_peripheral_cpu_tests` runs
an actual MOVX/expander/downstream-port workflow through both CPU dispatch paths;
`emu51_peripheral_gpio_tests` covers wired port operands, latch-based RMW, counter
edges, interrupt timing and a Covox-style DAC. `emu51_peripheral_gpio_demo` is a
small runnable CPU/port/DAC example.
`emu51_peripheral_device_tests` validates the LGPL 8255A Mode 0/BSR and ADC0804
profiles. `emu51_peripheral_adc_demo` executes assembled board firmware through
both dispatch paths in CTest, including ADC IRQ delivery and an ADC-to-DAC round
trip through 8255 ports. The SDK guide documents supported behavior and limits;
unsupported 8255 handshake modes must fail without changing existing state.
The LGPL audio bridge uses SPSC queues and sample-indexed epochs to isolate SDK
access from native callbacks. `emu51_peripheral_audio_tests` exercises concurrent
pause/reset and signal conversion; native host tests and the audio demo's CTest
entry always select miniaudio's software null backend. Normal builds enable the
optional `EMU51_PERIPHERAL_HOST_AUDIO`; standalone SDK builds default it off.
See [AUDIO.md](peripheral-sdk/AUDIO.md) for callback ownership, latency, lifecycle,
device selection and platform integration. Native callbacks must never call SDK
or GUI operations. Vendored miniaudio remains unmodified and outside strict
project warnings.
`emu51/registry.h` adds an LGPL factory catalog independently of the core ABI.
It deep-copies bounded type/schema/pin metadata, normalizes typed configuration,
and invokes trusted native creation callbacks. Registration and creation belong
to the simulation/control thread. The desktop session uses one immutable builtin
catalog for 8255/ADC/DAC creation and pin descriptions, while its sources/audio
remain host-specific. `emu51_peripheral_registry_tests`, the pure-C registry demo
and the installed C consumer cover discovery, validation and lifetime. Generic
custom factories are an SDK host feature; exposing them in project files/UI and
loading shared libraries require separate schema/lifecycle work.

The desktop Peripherals window is implemented in `imgui/emu51_imgui_peripherals.cpp`.
`Emu51PeripheralEditor` implements its Graph tab without a third-party node
editor. It draws a UI-owned draft using session pin descriptions, including
bus endpoints for visualization. The title-bar Apply button activates draft
wiring/device settings through `applyPeripheralConfig`: validate and construct
a candidate before replacing the session, then reset and pause the CPU. Invalid
drafts retain the active session and CPU state and show a validation popup.
Unapplied edits are not saved as the active graph. Project loading also validates
a candidate before installing it. Canvas rendering releases
the execution lock; it must
not access live CPU/session state. Block positions are independent optional
project presentation data, keyed by stable IDs and validated on load/save.
Their optional `name` labels are presentation too, including the CPU title;
renaming never rewrites SDK endpoint IDs. The Peripherals pane must participate
in `PaneKey`, pane/window registration, snap layout and persisted placement.
Its tab headers use a title-bar rectangle with the window name and native
collapse/close controls and Apply button reserved. Header submission restores cursor/layout and
clipping before drawing content, keeping the headers fixed while content scrolls.
Use ImGui tab fitting/scrolling for narrow docks; tab clicks must not start
window dragging, and caption drags must still reach the shared snap path.

`Emu51PeripheralLive` owns the Live canvas and box geometry; device controls stay
in the application/session path. Layout edit mode enables header dragging and
corner resizing, disables device controls, and keeps paused redraws active during
gestures. `visible()` selects displays, physical controls, audio and adjustable
sources. CPU/internal ICs and pin tables do not appear in Live. Headers and
default sizes are compact; Compact resets sizes while Arrange retains them.
Audio diagnostics and buffering are optional sections in the audio box. Momentary buttons release when hidden or entering edit mode. Optional
`peripheral_live_layout` project records are keyed by stable device IDs, validated
on load/save, and independent of circuit configuration and Graph presentation.
Old projects arrange boxes on first use; project load always exits layout editing.
Content uses an aspect-preserving scale with fonts and widget spacing scoped to
each box. Custom LCD/segment renderers receive the same scale; key grids and
audio scopes also use available content dimensions. Visible hit areas grow with
the controls. Keypad `momentary_keys` is optional project presentation data,
defaulting to false for older layouts. Only momentary key contacts are released
when hidden, on focus loss, or entering layout editing; toggle holds stay latched.
UI regressions measure rendered content growth for every front-panel device type
and exercise real mouse presses, mode changes, focus loss and project round trips.
Inspector visibility is a per-user layout preference; narrow panes stack the
inspector below the canvas. Canvas context menus own Add/Fit/Arrange/Inspector
actions and gesture/status help. Distinguish a right-click from a right-drag,
retain the popup's original world position when adding, and sustain redraws
while a popup is open so submenu hover timers work when execution is paused.
All inputs, outputs and numbered digital bits remain visible. The graph uses
ImGui mouse capture plus `interacting()` to sustain paused redraws, and cancels
drags when hidden. Workflow tests exercise real mouse dragging, bit/byte and
analog wiring, pan/zoom, validation, layout round trips and legacy projects.
`Emu51PeripheralSession` owns a graph, CPU GPIO adapter, built-in voltage/digital
sources and an optional explicitly opened audio host. Configuration uses stable
`device.pin` names; construction validates a candidate before replacing a live
session. App XDATA callbacks route mapped addresses to the graph and retain
backing-memory/watchpoint behavior elsewhere. Debugger reads use SDK PEEK.
`resetCpuAndPeripherals()` stops audio before CPU/context reset. Native startup
keeps the bridge paused until the device is ready; `syncPeripheralAudio()` then
resets CPU pacing. `tickAt()` discards its pre-start timestamp by returning
without executing CPU work on that transition. A delayed-start regression prevents startup time from becoming CPU
backlog and desynchronizing microphone input. Host tests require capture-use and
playback-use counters to advance after every restart, with/without graph reset.
Detach CPU port
I/O before destroying a session. Changing oscillator frequency rebuilds the graph
and closes audio. Project loading validates the graph before resetting memory;
old project files default to no graph. Projects save applied topology/source
values, never unapplied edits or open-device state. UI workflow tests run the
ADC board firmware, project round trips, rejected edits, reset/error recovery,
and the NULL audio lifecycle. The microphone/Covox recipe connects the ADC
straight to P0 and Covox straight to P1; firmware controls CS/WR/RD via P3 and
polls INTR. Its assembly source is embedded at configure time for the
`emu51_microphone_covox` host; the desktop loads the portable project normally. Its XDATA circular echo writes
`(input >> 1) + (old slot >> 1)` to both that slot and Covox; the delay line
initializes to unsigned silence. `emu51_echo_tests` executes the real firmware
on both CPU dispatchers, checking impulse delay/decay, odd-byte logical shifts,
incoming carry, arbitrary buffer lengths/page boundaries and memory canaries.
UI tests check actual ADC/GPIO/mixer/Covox values, portable project reload and
explicit audio opening/Run with NULL devices. The command-line host also verifies
each mix and same-address buffer write, including across pause/resume.
`peripheral-sdk/src/components.cpp` supplies the LGPL front-panel, LCD, shift
register, EEPROM and AY models through `emu51/components.h`. The desktop uses
registry metadata for their pins and direct constructors for interactive handles.
The Live tab owns only presentation/control; firmware and chip clocks remain on
the simulation worker. EEPROM callbacks never access the filesystem. The host
imports exact 256-byte images, snapshots committed bytes and releases the
execution lock for atomic saves. Project candidates remain disarmed for destructor
saves until installed; canceled/invalid opens cannot create images. Replacement
sessions carry committed EEPROM bytes for shared image paths at time zero, before
new CPU execution. See [component profiles](peripheral-sdk/COMPONENTS.md) and the
separate [GPL reSID assessment](SID.md). SID is not an SDK dependency or an
implemented block. Component tests exercise bus protocols; GUI tests run all four
firmware projects, image persistence, canceled opens and actual NULL audio output.

`peripheral-sdk/src/displays.cpp` adds the independently LGPL segment/driver
models. `emu51_segment_display.cpp` only draws passive snapshots; persistence
must use SDK ticks rather than ImGui time. Raw displays integrate prior states
at pin transitions and evaluate snapshots without clearing history. Controller
snapshots include programmed RAM and physical instantaneous masks; their scan
outputs can also drive raw displays. Keep 14/16-segment bit maps consistent
between the SDK documentation and renderer. Wide connector rows must remain
inside graph blocks. Display tests exercise polarity/duty, decoder priority,
chained MAX serial timing, HT I2C/key RAM and TM's distinct LSB-first protocol.
GUI tests execute all six firmware examples and check actual outputs and JSON
round trips, then render snapshots while paused. See
[DISPLAYS.md](peripheral-sdk/DISPLAYS.md) for the functional profiles.

`peripheral-sdk/src/serial_devices.cpp` models the SSD1306, DS1307, MCP23017,
W25Q64 and an eight-channel logic analyzer. I²C slaves share one open-drain
state machine. MCP23017 weak pulls participate in the normal net resolver.
Flash snapshots use the same host-side atomic write path as EEPROM images,
with revision checks for unchanged images. Logic transitions and decoder
timestamps use SDK ticks. The desktop Live panel draws passive OLED and
analyzer snapshots. See [serial device profiles](peripheral-sdk/SERIAL_DEVICES.md).

`Emu51ExecutionWorker` owns normal desktop execution on a dedicated thread.
Cocoa/GLFW callers keep `app.access()` alive while reading or editing the live
model; UI-only work uses `withoutExecutionLock` so event dialogs, text rendering
and file writes do not hold up the producer. Editor callbacks reacquire access
before touching source/debugger state. The sound-card callback still only uses
SPSC audio queues. One worker services CPU, timers, GPIO and the SDK graph; it
never calls ImGui or native window APIs. Stop/join precedes UI/context teardown.
Do not reset worker pacing merely because UI I/O completed: execution continued
during that work. Worker tests cover UI and editor stalls with a 32 ms buffer,
modal dialogs, pause/step/reset, debugger stops, project replacement, buffering
and shutdown/restart; the same tests run under ThreadSanitizer.

The playback-only Covox duet CLI embeds `progs/asm/demo/covox_duet.asm` in the
same generated firmware header. Desktop examples use normal project loading. T0's 8 kHz high-priority ISR advances and mixes
two 16-bit DDS square waves; T1's nominal 100 Hz ISR reads duration/MIDI-pitch/gate
records from code memory. A short ET0 mask publishes both voices atomically.
All player state is in IRAM `30h..53h`; register banks and stack are reserved by
the example. `emu51_covox_duet_tests` runs both dispatchers against a sample and
score-duration oracle, including nested-interrupt register preservation, stack
canaries, song loops, invalid pitches and code-memory page crossings. UI tests
cover portable project loading; the `emu51_covox_duet` CLI paces
the real firmware and supports explicit native playback or NULL audio.
Physical devices remain an explicit manual test. Desktop queues use a larger
latency/capacity than the SDK defaults to cover GUI/native callback jitter:
128 ms for playback-only graphs and 42.7 ms per direction for capture/duplex.
Projects optionally store `audio_buffer_ms` (0=automatic, otherwise 10–2000 ms).
The Live control pauses execution, closes/joins the native host, transactionally
reallocates the bridge's capture/playback rings, reopens the previous device and
channel selection, then resumes without resetting CPU/graph time or gain.
Larger configured latency automatically grows queue capacity. The SDK refuses
queue replacement while a native host is attached. Tests cover callback safety,
changed latency/capacity, live player-state preservation and project round trips.
A pacing rebase after a host-clock discontinuity also rebases the running audio
bridge, using its pause/epoch mechanism without reopening hardware or resetting
CPU/player state. Callback frame indices must not continue on the abandoned
wall interval. UI recovery tests exercise an 80 ms service gap and a >1 s stall
with a real NULL-host callback, including steady output after recovery.
The SDK and local hardware interface below are mutually exclusive in an active
session. Normal startup creates the default memory circuit without searching
for board firmware. The AVT-2250 module has been replaced by a
[public circuit](progs/boards/avt2250/README.md) using generic SDK primitives.
`emu51_trainer_tests` validates this circuit with an original synthetic ROM;
optional monitor/serial arguments accept local user-supplied files. Normal CTest
does not require them. See the SDK README for timing/ownership contracts and
standalone CMake package usage.

`imgui/emu51_hardware.*` defines one optional, statically registered hardware
factory. Standard builds register none and do not search for modules or firmware.
An explicit `-DEMU51_LOCAL_EXTENSION=/absolute/path/to/module` adds a local CMake
directory after the application targets exist. Keep the extension's sources,
firmware paths and resources outside this source tree; use a separate build
directory. Extension builds reject installation before copying any files and
omit CPack targets. Use a standard build for releases.

An extension registers an `Emu51HardwareFactory` from an object source added to
`emu51_imgui_common`. Claimed external-data reads/writes bypass backing memory;
unclaimed accesses use the normal memory map. Program fetches and image loading
remain independent. `advance()` receives completed machine cycles, including
interrupt entry, after every executed instruction. Resets also reset hardware.
Rendering never clocks hardware; `wantsRedraw()` supports held input and initial
window frames while execution is paused. Keep input restricted to the focused
panel and release held keys when hiding it.

The host offers transactional program loading, CPU reset and status reporting.
Loading through this interface enables the module, applies its oscillator and
selects cycle-exact execution after successful image preparation. Both native
Cocoa and ImGui Window menus expose the registered module without adding
shortcuts. Projects retain an optional `hardware_module` ID (127 bytes maximum);
unavailable IDs are rejected before any live state changes. Older projects
default to no hardware, and module runtime state is not serialized.

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

Recent projects and programs use independent ten-entry arrays in `state.json`.
Successful opens and saves update a deduplicated list and atomically replace the
complete UI state snapshot. Failed/canceled operations preserve the order; the native
and ImGui recent-project menus use the same guarded project-opening path.

Source saves use sibling temporary files and atomic replacement; POSIX
permissions and symlinks are preserved. Cancellation or write failure must leave
the pending editor replacement/quit action canceled. Text normalization on load
must not create a false modified-buffer state.

Recovery sessions use OS locks to avoid restoring another live instance's draft.
Draft saves are scheduled after up to 500 ms while the UI is serviced, and before
unsaved-change dialogs. Window-close and quit paths on both frontends must use
the same unsaved-buffer guard.

`emu51_ui_state.cpp` uses the vendored KaSON schema API for versioned `state.json`:
layout preferences and pane records under `layout`, plus `recent_programs` and
`recent_projects`. The application captures all three sections before releasing
the execution lock for an atomic write; a recent-list update must never discard
the current layout or the other list. Failed saves retain the previous file and
leave layout state dirty for retry. Loading validates the full document before
applying it. Missing JSON triggers a read-only import of the three legacy text
files; existing invalid/newer JSON is preserved and blocks state writes for the
session. `imgui.ini` remains owned by ImGui. Persistence tests use temporary
state paths and must not touch the real per-user directory.

`emu51_execution_settings.cpp` uses the vendored KaSON schema API to load/save
`settings.json`. Missing fields use defaults; malformed documents use the full
default settings. Numeric bounds are validated by the application. Follow PC is
stored per disassembly view in layout state, not in execution settings. Its
checkbox is a title-bar control; caption clipping reserves its space and keeps
collapse/close buttons usable. Preserve the stable `###disassembly_ID` identity
and restore content cursor/clipping after drawing the header. Capture floating
pane sizes from the root window, before entering the disassembly child. The
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
