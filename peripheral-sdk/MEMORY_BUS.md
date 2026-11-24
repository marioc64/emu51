<!-- SPDX-License-Identifier: LGPL-2.1-only -->
# Physical memory and functional wired buses

These APIs extend the SDK through separate C headers and exported functions.
The ABI-v1 function table and existing descriptors retain their layouts. Like
the rest of the SDK, calls are serialized by the simulation owner. There are
no internal threads, native UI dependencies or shared-library discovery.

## Storage: `emu51/memory.h`

`e51p_memory_create` allocates RAM or ROM independently of a context. Supply
`struct_size`, kind, capacity (1–2^32 bytes) and fill. Retain/release share its
lifetime between replacement sessions. `address_bits` is the minimum pin width
needed for capacity, with one bit for a one-byte chip.

`read` and RAM `write` represent storage access. ROM `write` returns
`E51P_UNSUPPORTED`. `program` and `fill` deliberately modify either kind.
`copy` takes a physical-range snapshot into a caller-owned buffer. All ranges
are checked before mutation, including overflow. Zero-length ranges may start
at capacity. Revision increases only if bytes change. `page_revision` reports
the last revision touching the containing 256-byte page, allowing source/cache
users to ignore unrelated storage writes. Resetting an SDK context does not
reset physical memory. File I/O and initialization recipes belong to the host.

```c
e51p_memory_config config = {sizeof(config), E51P_ROM, 524288, 255};
e51p_memory* rom = 0;
e51p_status status = e51p_memory_create(&config, &rom);
/* Check status, then explicitly program physical ranges. */
e51p_memory_release(rom);
```

## Decoder: `emu51/decoder.h`

`e51p_decoder_create` copies 1–32 input bits and a list of output descriptors.
Each has a stable nonempty unique ID, polarity and exact activating values.
Input bit 0 is least significant. Duplicate values within or across outputs
are conflicts; values must fit the width. Lookup is sparse and does not allocate
2^N entries. There is no mask, expression or device-specific bank logic.

`select` returns an output index or `NOT_FOUND` with `INVALID_ID` for an
unassigned value. `level` returns the selected/inactive electrical level for
one output. Floating or contended relevant inputs return `INVALID_STATE`.
These model functions are passive.

`attach` creates an ordinary combinational digital device with an `IN` port and
one output per ID. The context retains the model; the caller can destroy its
reference afterwards. Changed GPIO inputs settle through normal delta rounds.
Unknown inputs release outputs. Discard the context if multi-step attachment
fails, as with other SDK factories. Functional bus evaluation also propagates
unknown decoder results through its virtual workspace.

## Passive workspace: `emu51/snapshot.h`

Create a reusable snapshot after context start. `begin` captures current drive
states. `drive` replaces the virtual endpoint drive, including an undefined-bit
mask, and `sample` resolves the resulting nets with the runtime's strong drives,
open-drain behavior and weak pulls. No callbacks run and no live pins or ticks
change. `e51p_digital_net` identifies sealed nets for dependency compilation.
The context must outlive the snapshot.

`target_read` dispatches directly to a bus-target callback. PEEK is required to
be passive and cannot drive signals. During dispatch, ordinary `api->sample`
sees the supplied workspace. `target_set_write_validator` registers a passive
preflight; `target_check_write` validates without committing. The 8255 and AY
profiles provide validators. A custom target must supply one to participate in
wired writes. Validators and PEEK must return promptly and leave device state
unchanged. A callback failure during final commitment is reported, with no
general rollback of already committed callbacks.

## Routed transactions: `emu51/wired_bus.h`

Create a bus before context start, then add memories, register targets and
decoders. Wire the returned endpoints with the existing digital API. Repeated
bit connections support permutations and addresses assembled from CPU, GPIO,
latches or port devices. Each target owns local address/data/control endpoint
handles; its controls have weak pulls to the configured inactive levels.
ROM has no WE endpoint. The bus retains memory handles.
Register targets may specify zero address bits to omit ADDR and always dispatch
local address zero. This fits a single output latch or input buffer whose
address aliases are defined entirely by CS.

The initiator provides ADDR, 8-bit DATA, active-low code-read, data-read and
data-write controls. Start the context, then `compile` seals a reusable routing
workspace and topologically orders decoders. Pure combinational decoder cycles
are rejected. Destroy the bus before its context.

`access` accepts CODE_READ, DATA_READ or DATA_WRITE and returns the resolved byte,
target index and local physical/register address. For reads, the CPU releases
DATA, which has weak FFh pulls. For writes it drives the byte. The transaction
evaluates decoder controls, selected targets and electrical state, preflights
register writes, and only then commits. Multiple RAM write receivers may accept
one byte. Multiple enabled target readers are conservatively diagnosed even
if their current values agree. A reader during a CPU write, writer during a CPU
read, or simultaneous OE/WE is invalid. Physical address bounds are checked
before any selected device changes.

Unmapped access returns NOT_FOUND (read value FFh). Floating or contended relevant
nets, incompatible data drivers, unsupported register inspection and out-of-range
memory return explicit errors. `error` provides a diagnostic valid until the next
transaction. `peek=1` is supported for reads and suppresses final side effects.
`sample` inspects an idle virtual bus using the last committed CPU address.

Bus drives remain in the private workspace during both live and peek accesses.
Register adapters receive committed callbacks; unrelated pin-level devices do
not receive live strobe-change events. An ADC0804 using physical CS/RD/WR, for
example, still needs its GPIO protocol or a suitable explicit register adapter.
This model supports functional transaction routing, not ALE/multiplex phases,
electrical access delays or oscillator-edge simulation. Hosts advance time
through their existing CPU/SDK timing integration.

The SDK's memory-bus tests cover storage above 64 KiB, ROM protection, exact
lookup collision checks, live decoder outputs, GPIO banking, permuted address
bits, active-high controls, contention, failure preflight, shared CODE/XDATA
RAM, an 8255 on the same bus, and side-effect-free peeks. The installed pure-C
consumer exercises the new headers after relocating the SDK installation.
