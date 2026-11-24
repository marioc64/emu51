<!-- SPDX-License-Identifier: LGPL-2.1-only -->
# emu51 peripheral SDK (experimental, 0.1)

This independently buildable SDK provides a C ABI and headless wiring runtime
for composable peripherals. The core has no CPU, GUI or native audio dependency.
An optional separate native audio host is described in [AUDIO.md](AUDIO.md).
Physical RAM/ROM, sparse lookup decoders, passive electrical snapshots and a
functional pin-routed bus are described in [MEMORY_BUS.md](MEMORY_BUS.md).
Its headers, implementation, build rules, documentation, tests and
examples are **LGPL-2.1-only**, Copyright (C) 2026 Mariusz Kasolik. See
[LICENSE](LICENSE) for LGPL 2.1 and [COPYING](COPYING) for the referenced GPL 2.
Preserve the license notices and meet the applicable source/relinking conditions
when distributing the library or linked binaries. The surrounding emulator and
its host adapter remain GPL-2.0-only. The SDK license does not provide a linking
exception for the GPL application or change the license of existing code.

## Build and use

```sh
cmake -S peripheral-sdk -B build-sdk
cmake --build build-sdk --parallel
ctest --test-dir build-sdk --output-on-failure
cmake --install build-sdk --prefix /path/to/sdk
```

The normal application build also builds the SDK, its tests, a pure C example,
and CPU integration tests. The SDK installs a static library, public C header,
licenses, and a relocatable CMake package. Consumers use:

```cmake
project(my_peripheral LANGUAGES C CXX)
find_package(emu51Peripherals 0.1.0 EXACT CONFIG REQUIRED)
add_executable(my_peripheral main.c)
target_link_libraries(my_peripheral PRIVATE emu51::peripheral)
```

The implementation needs a C++17 toolchain; clients can be C99. Request the
function table with `e51p_get_api(E51P_ABI_VERSION, &api)` and check the returned
version and table size. Only fixed-width data, opaque handles, function pointers,
and caller-owned buffers cross the ABI. This initial ABI is experimental and
uses exact version negotiation; it does not yet promise binary compatibility
with future versions. Descriptor `struct_size` must cover the current descriptor;
trailing fields are ignored. Handles are local to one context and do not carry a
context identity: never mix handles from different contexts.

## Devices and topology

Create a context with a fixed nonzero tick frequency, add device instances with
unique IDs, add their named endpoints, then connect and map them before calling
`start`. Device descriptors are per-instance callbacks. The optional
[factory registry](REGISTRY.md) adds type discovery, typed configuration and
creation by stable IDs without changing the core ABI. It accepts custom native
factories; dynamic shared-library discovery remains a separate host concern.
Descriptor strings and callback tables are copied. Device `user` storage must
remain valid until destruction. Successful registration transfers cleanup
responsibility if `destroy` is provided; unsuccessful registration does not.
Cleanup runs once in reverse registration order. If an example's multi-step
setup fails, discard that context; there is no removal/rollback API yet.

A device can have any combination of:

- **Digital ports**, 1–32 bits wide, with input, output or bidirectional capability.
  `connect_digital` joins slices as electrical nets. Repeated one-bit connections
  support reordered bits and groups assembled from several different ports.
  Fanout and transitive connections are supported. A digital pin is a one-bit port.
- **Bus initiators and targets**, with 1–32 address bits and 1–32 data bits.
  A mapping decodes an initiator window and translates it into target addresses.
  Overlaps on one initiator are rejected before mutation; separate initiators
  may access the same target. This is a transaction bus, not oscillator-phase
  address/data/control-pin emulation.
- **Analog inputs and outputs**, scalar finite values in volts. Outputs fan out;
  an input has at most one source. An unset source is explicitly unavailable.
  A disconnected input returns `E51P_NOT_FOUND` rather than a fabricated sample.

After `start`, topology is immutable. `reset` retains wiring and frequency,
releases output drives, invalidates analog signals, resets time to zero, calls
device reset callbacks, then propagates initial signals. Devices may establish
their own reset output levels in those callbacks. Destroy and recreate the
context to reconfigure wiring. Project serialization and the desktop graph
editor belong to the GPL host; they are not SDK dependencies.

## Digital electrical behavior

`drive` replaces an endpoint's full value, enable mask and open-drain mask.
Disabled bits are high impedance; open-drain bits driven high also release their
lines. Strong drivers override weak pulls. Opposing strong drivers, or opposing
weak pulls without a strong driver, set the per-bit contention mask. Floating
bits and conflicted bits have deterministic zero value and distinct masks;
consumers must check these masks when undefined input matters.

Contention commits the update and returns `E51P_CONTENTION`; releasing the
conflicting driver recovers normally. Invalid arguments and topology conflicts
are rejected without applying that operation. Direction capability is fixed,
while bidirectional ports change individual output enables at runtime.
Signal inversion can be implemented as a device, not a special wire flag.

## Timing, callbacks and inspection

All SDK access must be serialized on the simulation thread. There are no worker
threads, sleeps, or wall-clock reads. Time is an explicit monotonic 64-bit tick
counter. `advance_to` sets the new time, calls device advance callbacks in
registration order, then settles signals. An unchanged timestamp does not call
advance callbacks. An advance callback owns any internal deadlines it crosses;
the runtime does not yet subdivide intervals or provide an event scheduler.
The tick frequency is immutable; recreate the context if the clock rate changes.

Digital nets and analog input snapshots are resolved before notifications.
Notifications run in endpoint registration order. Callback drives are resolved
in the next delta round at the same timestamp, without recursive notification.
Signal reads inside a callback see the last resolved snapshot, not pending
drives from that round. After 1,024 unresolved rounds the runtime reports
`E51P_OSCILLATION` and requires reset. Reset notifications include initial
digital state; analog notifications start when a source first supplies a value.
An unchanged voltage is not a sample-stream event.

Callbacks may drive or sample signals and read the clock; they must not recurse
into bus operations, lifecycle operations or time advancement. Callback-side
drive success means queued successfully; the outer operation returns any final
propagation error. Callbacks must return promptly and must not throw. As a last
resort exceptions are contained at the C boundary and fault the context until
reset. All state changes preceding a callback failure remain applied. Destruction
inside a callback is ignored; its owner must destroy the context after returning.

`sample`, `analog_read`, and `clock` are passive. Bus reads distinguish `READ`
from `PEEK`; devices must implement `PEEK` without consuming data or changing
internal state. The runtime rejects signal writes during peek. Only unmapped bus
accesses return `NOT_FOUND`; this status from a mapped device is converted to
`DEVICE_ERROR`. The host can then fall back to ordinary data memory
only for unmapped accesses. Program loading and instruction fetches use the
host's program map independently.

## Connecting an 8051 CPU

The GPL adapters in `emu51/peripherals/` integrate the standalone SDK with the
CPU. Keep the context alive longer than its adapters and attached CPU. Set its
tick rate to the oscillator frequency in Hz; classic machine cycles use 12 ticks.

`Emu51PeripheralBus` wraps an existing CPU address space for XDATA routing. When
used alone, call `advanceMachineCycles` after every instruction and reset both
CPU and context explicitly.

`Emu51PeripheralGpio` registers four bidirectional endpoints, P0 through P3, and
implements the core's optional `emu51_port_io` interface. Create it before
starting the topology, connect full ports or slices, start the context, then
attach it to the CPU. For example:

```cpp
auto gpio = std::make_shared<Emu51PeripheralGpio>(*api, *context);
e51p_endpoint data, voltage;
// Check each returned status in application code.
e51p_add_parallel_dac(api, context, "covox", 5.0, &data, &voltage);
api->connect_digital(context, gpio->port(1), 0, data, 0, 8);
api->start(context);
emu51 cpu;
cpu.setPortIo(gpio);
cpu.process();
```

With GPIO attached, the CPU automatically advances the context before timer and
interrupt processing. **Do not also call the bus adapter's manual advancement**
or time will advance twice. Bus and GPIO adapters can share the same context.
`cpu.init()` and `setModel()` reset the entire connected context, its devices and
clock; detachment with `cpu.setPortIo(nullptr)` does not alter the topology or
release its existing output drives.

Ordinary port operands and `readDirect()` sample resolved pins. `readLatch()`
and raw `P0`..`P3` references inspect output latches. Port read-modify-write
instructions use latches, including JBC and bit destinations; XCH reads pins.
This follows the port distinction in the [8051 hardware manual, section
2.5.4](https://www.microchip.com/content/dam/mchp/documents/OTH/ProductDocuments/UserGuides/doc4316.pdf).
Byte/bit writes publish immediately at the current instruction-start timestamp.
Legacy raw latch edits are published at the start of the next instruction.
Inspection never advances devices or delivers signal-change events.

GPIO lows drive strongly and highs release the pin. P1..P3 have weak pull-ups;
P0 has none, so floating P0 bits sample as zero with the SDK's floating mask set.
This digital approximation omits the transient strong pull-up after an 8051
output rises, analog loading, and physical bus multiplexing. A CPU without an
attached GPIO interface retains its original latch-only behavior.

For each completed instruction (and separately for two interrupt-entry cycles),
the ordering is:

1. Advance peripherals to the end timestamp and settle their signals.
2. Sample INT0/INT1 and T0/T1, then advance timers and UART frame processing.
   Timer 2 samples resolved P1.0/P1.1, ANDed with the legacy Timer 2 input mask.
3. Arbitrate interrupts after an instruction. Entry cycles do not arbitrate again.

INT0/INT1 support level requests and latched falling-edge requests. T0/T1 counter
inputs count sampled falling edges; timer gates use resolved INT0/INT1 pins.
Pulses entirely between boundaries can be missed. UART byte queues remain the
serial interface; this adapter does not implement RXD/TXD pin waveforms. Hosts
without GPIO keep the explicit external-interrupt request API.

GPIO adapter `status()` retains the first error (a fatal error replaces earlier
contention) until `clearStatus()` or CPU reset. Check it after stepping and stop
on fatal errors; subsequent successful reads cannot hide a device failure.
These integrations and the port instruction cases are tested with both CPU
dispatch implementations. The GPL desktop host provides a
[Peripherals window](../PERIPHERALS.md) with built-in device factories, editable
wiring, project persistence and explicit audio controls. All effects remain
instruction-boundary rather than physical-pin or oscillator-phase accurate.

## Examples

`examples/parallel_expander.c` exposes a four-register bus window and three
bidirectional ports, which can drive further devices. It is a generic example,
**not an 8255 model**. `examples/wiring.c` maps it at 8000h, connects A[7:4] to a
four-bit LED input, and demonstrates a separate analog source/input connection.
No private board code or firmware is required.

`emu51/devices.h` also supplies an LGPL ideal eight-bit parallel DAC suitable
for Covox-style connections. Its digital `data` input produces an analog
`voltage` output: `reference_volts * code / 256`. Reset starts at zero; once the
input becomes defined, it follows the resolved digital value. Floating or
contended inputs retain the last valid voltage as a deterministic convention.
The DAC models voltage without an output filter or load. Sample streams and
physical devices are supplied separately by the optional [audio host](AUDIO.md).

`emu51/peripherals/examples/gpio_dac.cpp` is a complete headless CPU host example:

```sh
./build/emu51/emu51_peripheral_gpio_demo
```

It executes three MOV P1 instructions and prints their DAC voltages at 24, 48
and 72 oscillator ticks. `emu51_peripheral_gpio_tests` additionally builds a DAC
input from bits belonging to two different CPU ports and checks pin/latch
semantics, timer inputs and interrupt-boundary ordering.

## 8255A Mode 0 profile

`e51p_add_i8255_mode0` in `emu51/devices.h` exposes the four-register bus window
and three eight-bit ports A/B/C, plus an active-high `reset` input. Initialize
the result structure's `struct_size` before calling it. Map `bus` at any suitable
initiator address; offsets 0/1/2 select A/B/C and offset 3 writes control.

The implemented [Intel 8255A profile](https://kvazer.com/resources/prod_47739_p8255a-5-ocrpdf.pdf)
supports Mode 0 and Port C bit set/reset. Mode-set words select A/B direction
and each C nibble's direction independently, and clear output latches even when
rewriting the same mode. BSR leaves these directions and A/B latches intact.
Context reset or asserted reset puts all ports into input mode with zeroed
latches. Transactions while reset is held are rejected. The reset endpoint has
a weak pull-down; data ports have no implicit pulls.

Port A reads resolved pins. B/C output bits read latches, while their input bits
read pins, following [Intel's output-readback clarification, Q16](https://www.eletr.ufpr.br/edu/ie_/8255_Q%26A.html).
All reads and peeks are passive; contention remains observable through SDK
status/net masks. Original 8255A control reads return `E51P_UNSUPPORTED` rather
than implementing the enhanced 82C55A control-read feature. Mode 1/2 writes also
return `E51P_UNSUPPORTED`, preserving the prior configuration and latches.
Handshake modes, physical bus strobes and electrical timing are future profiles.

## ADC0804 profile

`e51p_add_adc0804` exposes an eight-bit tri-state `data` output, active-low
`cs_n`, `rd_n`, `wr_n` inputs, `intr_n` output, and two analog voltage inputs.
Initialize the result's `struct_size`, supply a nonzero conversion duration in
context ticks and a finite positive full-scale span. The span represents
`2 * VREF/2`; 5 V is the conventional default. Control inputs have weak pull-ups.

The [TI ADC0804 control protocol](https://www.ti.com/lit/ds/symlink/adc0804-n.pdf)
is modeled at simulation boundaries: CS/WR both low hold conversion reset;
releasing either starts it. Another write pulse aborts/restarts conversion.
Completion latches the result and asserts INTR. CS/RD both low acknowledge INTR
and enable data; deselection or RD high releases the bus. Reading during a
conversion returns the previous latch; holding RD active suppresses the
completion interrupt while exposing the new result.

This is an ideal functional profile. It captures `VIN+ - VIN-` when conversion
starts, rounds to the nearest step of `span / 256`, and clips to 0..255. It does
not simulate the physical converter's successive-approximation comparisons,
analog input settling, clock phases, startup/access delays or read-strobe timing
requirements. Completion is delivered at the first advancement boundary covering
the configured duration; oscillator scheduling and RC timing are not modeled.
Elapsed-time subtraction avoids absolute-deadline overflow.

An absent VIN- is treated as ground. An absent VIN+ makes the completed result
undefined, leaving data high impedance even during a read instead of inventing
a sample. Conversion timing and INTR still complete. Reset cancels pending work,
clears the deterministic initial output latch to zero and releases data.
Analog inputs can be supplied by any SDK source, including the DAC; physical
sound-card capture is supplied by the optional [audio host](AUDIO.md).

## CPU / 8255 / ADC / DAC example

`emu51/peripherals/examples/adc_board.cpp` assembles and executes a short 8051
program using the following wiring:

```text
CPU XDATA 8000h..8003h -> 8255 register window
                         A[7:0] <- ADC data
                         B[7:0] -> parallel DAC -> analog voltage
                         C0/C1/C2 -> ADC CS/WR/RD
                         C4 <- ADC INTR -> CPU P3.2 / INT0
host analog sources -> ADC VIN+ / VIN-
```

```sh
./build/emu51/emu51_peripheral_adc_demo
./build/emu51/emu51_peripheral_adc_demo --table
```

The firmware sets mode 98h, releases the converter's write reset, handles INT0,
reads Port A and writes the sample to Port B. The example repeats after reset,
checking two input voltages, the returned ADC codes, output voltages, IRQ delivery,
backing-memory isolation and total CPU/peripheral time. CTest runs both dispatch
implementations. The SDK's standalone device tests cover all 16 Mode 0 direction
combinations, BSR encodings, unsupported modes, output readback, reset, conversion
restarts, all ADC code centers and half-LSB thresholds.

## Factory discovery and configuration

The LGPL [factory registry](REGISTRY.md), exposed by `emu51/registry.h`, catalogs
parameter schemas and logical pins for `emu51.i8255.mode0`, `emu51.adc0804` and
`emu51.parallel-dac`. Hosts can register additional factories using C callbacks.
It copies metadata, validates named configuration before invoking a factory,
and returns pin handles in catalog order. The pure-C
`emu51_peripheral_registry_demo` creates a custom source and a discovered DAC;
SDK install/relocation tests exercise this API independently of the emulator.
The desktop uses the catalog for these built-in models' construction and pin
selectors. Its generic sources and audio bridge retain their host-specific
lifecycle. See the registry guide for ownership and extension limits.

## Interactive and serial components

[COMPONENTS.md](COMPONENTS.md) documents the LED, switch, bouncing pushbutton,
HD44780 LCD, 4x4 keypad, 74HC595/165, 24C02 EEPROM and AY-3-8910 profiles.
They use the existing C ABI and are included in the installed LGPL SDK.
The same guide covers generic eight-bit bus latches/input buffers and a
configurable 1–8-row, 1–4-column key matrix, including a retriggerable latch
pulse suitable for scan watchdogs.
[DISPLAYS.md](DISPLAYS.md) adds 7/14/16-segment displays, bar graphs, BCD decoders
and serial display controllers, with emulated-time brightness snapshots.

## Next increments

1. Audio recording/replay files, adaptive clock-drift correction and interactive
   physical-device testing on each desktop OS. The audio bridge and optional
   native host are implemented; see [AUDIO.md](AUDIO.md) for their contracts.
2. Generic custom-device project schemas, deadline scheduling and optional
   shared-library loading. Factory discovery and typed creation are implemented;
   the desktop saves its known built-in graphs in its versioned project format.

Keep those additions outside the current ABI until their contracts are tested.
