<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Memory circuits and assembly projects

External memory is configured in **Peripherals → Graph**. RAM and ROM own a
continuous physical byte array; CPU accesses follow the address, data and control
wires. Program loading uses a separate mapping from assembled addresses to chip
offsets. An assembly `ORG` changes neither the wiring nor the physical placement.

## Try the examples

Open one of these projects with **File → Open project**, then Step or Run:

| Project | Result |
| --- | --- |
| [banked_rom.emu51](progs/asm/demo/banked_rom.emu51) | A 512 KiB ROM uses CPU A0–A14 and four P1 selector bits. A15 drives ROM select directly and reaches RAM select through a Bit Inverter. Reset selects region 15. Boot changes P1 to select region 14, executes its application and writes 42h to external RAM. Three independent sources program boot, application and RAM tables. |
| [shared_ram.emu51](progs/asm/demo/shared_ram.emu51) | One RAM serves CODE and XDATA. MOVX writes 55h at 0100h and MOVC reads the same byte. A second source supplies a table in the same chip. |
| [decoder_bus.emu51](progs/asm/demo/decoder_bus.emu51) | A lookup decoder selects program ROM, external RAM and an 8255. The 8255 uses CPU A0/A1 for its registers and drives an LED. Its registers alias throughout the selected region. |

The console runner loads the same circuit and programming recipes without opening
audio devices:

```sh
./build/emu51/emu51cli --steps 100 progs/asm/demo/banked_rom.emu51
```

## Wire a circuit

The CPU exposes these functional connections in addition to its ordinary GPIO:

| Endpoint | Meaning |
| --- | --- |
| `cpu.ADDR` | 16-bit logical address, bit 0 least significant |
| `cpu.DATA` | Bidirectional 8-bit data; released on reads, driven on writes |
| `cpu.PSEN` | Active-low CODE read, including instruction fetch and MOVC |
| `cpu.RD` | Active-low XDATA read |
| `cpu.WR` | Active-low XDATA write |
| `cpu.CLK` | Connect one Clock `OUT`; its frequency sets oscillator pacing |

Connect memory `ADDR`, `DATA`, `CS`, `OE` and, for RAM, `WE`. The inspector shows
active-low controls with a slash and allows changing their polarity. Unselected
or disabled outputs release DATA. A typical ROM has OE connected to PSEN; RAM
has OE connected to RD and WE connected to WR. A decoder can combine read
controls when a RAM must serve both address spaces.

Grouped wires connect matching bit ranges. Individual wires can permute address
bits or assemble the address from several sources. To expose 512 KiB through a
32 KiB CPU window, wire CPU A0–A14 to ROM A0–A14 and four GPIO, 8255 or latch
outputs to ROM A15–A18. There is no bank-count or bank-register setting. All
aliases refer to the same physical bytes.

RAM and ROM support 1 byte through 4 GiB, subject to host memory availability.
Non-power-of-two capacities are valid; selected addresses outside capacity are
faults. Internal CPU RAM and SFRs remain inside the CPU model.

For the 8255 and AY register profiles, enable **Wire address/data/control pins** in the inspector
and connect their local register ADDR, DATA and controls. Their old XDATA base
does not decode accesses in this mode. The decoder has no knowledge of the
selected device's type. Serial devices retain their serial interfaces; the
ADC0804 retains its GPIO control protocol through ports or an appropriate
controller. Merely attaching its physical strobes to the CPU's functional
transaction endpoints does not create a register adapter.

Functional bus transactions resolve signals in a private electrical workspace.
They commit memory or register operations once after validation. They do not
emit live SDK pin-change callbacks for every CPU bus transition. GPIO-driven
decoders also operate as ordinary combinational devices. Multiplexed P0/ALE
timing, oscillator edges and access-time delays are outside this model.

## Define decoder outputs

For single-bit negation, choose **Add → Bit Inverter** in the Graph. Wire one
digital bit to `IN` and connect `OUT` to its destination. The inverter also
works on functional CPU address and control lines during bus accesses.

Add **Address Decoder**, set 1–32 input bits, and connect signals to `IN`.
`IN[0]` is the least significant bit. Add named output lines, choose each
polarity, and list the exact values which activate it. Multiple values may
activate one output. Unassigned values leave every output inactive.

For example, wire CPU A13/A14/A15 to IN0/IN1/IN2, then assign:

| Output | Values | Selected region |
| --- | --- | --- |
| `/CS_BOOT` | 0 | 0000h–1FFFh |
| `/CS_RAM` | 1, 2, 3 | 2000h–7FFFh |
| `/CS_PROGRAM` | 4, 5, 6, 7 | 8000h–FFFFh |

Value lists accept decimal, `0x10` and `10h`, separated by whitespace, commas or
semicolons. **Add value** accepts a hexadecimal value. Expand **Assigned values
and bits** to inspect binary representations or remove individual entries.
Output names can change while IDs and wiring remain stable.

Duplicates within an output, assignments shared by different outputs, malformed
values and values outside the configured width block Apply. Invalid text stays
in the draft for correction. Width reductions do not discard assignments.
Separate decoders can still select competing data drivers; bus resolution
diagnoses that at runtime. Combinational decoder feedback is rejected at Apply.

## Physical images and power-on contents

Each RAM/ROM inspector has capacity, fill and **Power-on binary images**. Defaults
are 00h for new RAM and FFh for new ROM. Initialization occurs in this order:

1. Fill the whole chip.
2. Load configured binary images into their physical offset/length ranges.
3. Apply the project's ordered program mappings.

For manual **Load binary…** and **Save binary…**, choose the whole device or a
physical offset and length. Loads require exactly that many bytes. No padding,
truncation or selector changes occur. Input validation completes before any
bytes change; saves take a consistent copy and replace files atomically.
Manual loading can program ROM, while ordinary CPU writes cannot.

A manual load changes runtime contents. Add an initialization image explicitly
to retain it as a power-on recipe. Save Project stores references and drafts;
it does not overwrite source files or binary images, or save live chip contents.
Use binary Save to retain those bytes. Paths are project-relative where possible.

## Multiple sources and explicit load mappings

Open **Project files** in the assembly editor. Add an existing source or create a
new embedded one. Each file has its own buffer, cursor, undo history, diagnostics
and compiled image. **Include file** retains an editable include without building
it independently. **Build only** permits an output with no programming rows.
Per-unit include directories resolve relative to the project folder. Unsaved
include drafts participate in assembly without first being written to disk.

Expand **Program load mappings** and add rows with a source unit, source start
and inclusive end, target RAM/ROM ID, physical destination and explicit order.
For a source byte at address `a`, the destination is:

```text
physical destination + (a - source start)
```

An application assembled at ORG 8000h can therefore start at physical offset
10000h without changing its encoded addresses. The circuit must expose that
physical region at the intended CPU address when executing it. Sparse gaps
remain untouched. One unit may have several rows, and several units may share
a chip. Every emitted byte needs a mapping unless its unit is Build only.

Mapping IDs and orders must be unique. Destination ranges must fit. Colliding
emitted bytes are rejected unless both overlapping rows explicitly allow
**Overlay**; the later order wins. All sources, mappings and destination bytes
are validated before programming any chip. A failed build preserves the active
machine. HEX uses its encoded addresses; BIN uses its declared source base.

**Build project** compiles and validates. **Build and load** programs the mapped
bytes without changing selectors. **Reload** rebuilds and loads all project
units, following its CPU-reset action. **Power cycle** constructs fresh memory
from the full recipe, resets the CPU and pauses. Compilation can also be used
independently in each editor, with its listing and symbols available in the tree.
Save Project retains all dirty source drafts. Ordinary source Save updates only
the selected file. Switching files preserves independent recovery drafts;
abandoned drafts are offered for recovery at startup.

## Debugging and lifecycle

CODE and XDATA panes follow current wiring. In the debugger, **Physical chip
memory** selects a chip and physical offset, including storage outside the
currently visible CPU window. It offers an explicit physical-range fill,
including ROM programming. The PC readout shows its resolved chip and offset.
Binary operations remain in the chip inspector.

Peeks use virtual access controls and current selector snapshots. They do not
advance time, toggle live pins, consume peripheral data or acknowledge events.
Unmapped reads return FFh and unmapped writes have no recipient. Relevant
floating/contended address, control or data nets, invalid control combinations,
competing readers and selected out-of-range storage pause execution with a bus
diagnostic. No invalid bus transaction side effects commit. CPU work already
performed earlier in the instruction is not rolled back.

Source matching follows the loaded unit, row, device and physical offset.
Changing selectors selects the corresponding source. Changing a byte removes
stale source matches for its line, including changed instruction operands.
Ambiguous symbols can be qualified as `UNIT.SYMBOL`. Breakpoints and watchpoints
remain CPU logical addresses; physical-qualified breakpoint editing is not
provided in this version.

| Action | CPU | External memory |
| --- | --- | --- |
| Reset | Existing reset/stop behavior; ports return to reset levels | Bytes preserved; reset ports may expose another region |
| Apply wiring or Clock | Validate candidate, reset and pause | Preserve chips with the same ID, type and capacity |
| Apply changed ID/type/capacity | Reset and pause after successful validation | Recreate the affected storage from fill and binary images; load program rows explicitly |
| Power cycle | Reset and pause | Fresh fill, binary images, then program rows |
| Save Project | Preserve runtime state | Persist recipes and source drafts; no implicit binary save |

Clock supports 1 Hz–1 GHz. Exactly one Clock must reach cpu.CLK. Its frequency
controls the existing classic 12-oscillator-tick machine-cycle pacing and SDK
time. Clock edits use Apply and preserve unchanged chip bytes. Bare memory,
decoder, Clock and CPU blocks stay in Graph; Live remains a front panel.

## Existing projects

Version-1 projects open through a transactional migration. Unified memory becomes
one shared RAM; split memory becomes separate CODE and XDATA RAMs, preserving
legacy writable program backing. Generated decoder wiring preserves peripheral
MMIO priority without excluding the same CODE addresses. The old oscillator
setting becomes a connected Clock, and program recipes become build units and
physical mappings. Existing layouts and debugger settings are retained.

Overlapping or unaligned legacy register windows require explicit correction;
migration reports the problem and preserves the active machine. Opening does
not rewrite a file. An explicit Save Project writes version 2. Optional local
hardware modules retain their existing host path. CPU-only tests and standalone
ASM/HEX/BIN console execution can still use a simple address-space host.

The implementation design and acceptance matrix are retained in
[MEMORY_BUS_PLAN.md](MEMORY_BUS_PLAN.md). Reusable storage, decoder and snapshot
contracts are documented in the [LGPL SDK guide](peripheral-sdk/MEMORY_BUS.md).
