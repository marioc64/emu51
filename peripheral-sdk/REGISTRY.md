<!-- SPDX-License-Identifier: LGPL-2.1-only -->
# Device factory registry

The optional catalog in [`emu51/registry.h`](include/emu51/registry.h) provides
discovery and configuration on top of the existing peripheral C ABI. It is
part of `emu51::peripheral`, under LGPL-2.1-only. There are no CPU, GUI, native
audio, C++ or platform types in its public interface. Existing direct device
constructors remain supported.

Create a registry with the exact `E51P_REGISTRY_ABI_VERSION`, register factories,
then enumerate their descriptors or find a stable type ID. Each descriptor
contains a display label, typed parameter schema, pin descriptions and a create
callback. Registration copies descriptor arrays and their strings. Temporary
stack metadata is safe. The callback's `user` pointer remains caller-owned.

## Discover and create

This C99 example creates a DAC with a 3.3 V reference. Check every returned
status and clean up on failures in a production host.

```c
#include <emu51/registry.h>
#include <stddef.h>

const e51p_api* api = NULL;
e51p_context* context = NULL;
e51p_registry* registry = NULL;
const e51p_factory_desc* factory = NULL;
e51p_endpoint pins[2];
e51p_parameter_value reference = {
    sizeof(reference), "reference_volts", E51P_PARAMETER_REAL, 0, 3.3, NULL
};

e51p_get_api(E51P_ABI_VERSION, &api);
api->create(12000000, &context);
e51p_registry_create(E51P_REGISTRY_ABI_VERSION, &registry);
e51p_registry_add_builtins(registry);
e51p_registry_find(registry, "emu51.parallel-dac", &factory);
e51p_registry_instantiate(registry, factory->type_id, api, context, "dac",
    &reference, 1, pins, 2);
```

Wire the returned pins, start the graph, then eventually destroy the context
and registry. The complete [registry example](examples/registry.c) includes error handling,
context creation, a custom factory, wiring and cleanup. It destroys its registry
before starting the graph, demonstrating that device state belongs to the
context. Do not access borrowed metadata after destroying the registry.

| Built-in type ID | Parameters and defaults | Pins in result order |
| --- | --- | --- |
| `emu51.i8255.mode0` | None | `bus`, `A`, `B`, `C`, `RESET` |
| `emu51.adc0804` | `conversion_ticks`: integer, 768; `full_scale_volts`: real, 5.0 | `data`, `CS`, `RD`, `WR`, `INTR`, `plus`, `minus` |
| `emu51.parallel-dac` | `reference_volts`: real, 5.0 | `data`, `out` |

Interactive components, LCD, shift registers, EEPROM and AY-3-8910 factories
are also built in; see [their complete pin and parameter catalog](COMPONENTS.md).
Raw segment displays, bar graphs, BCD decoders, MAX7219/MAX7221, HT16K33 and
TM1637 are described in [DISPLAYS.md](DISPLAYS.md).

Use `e51p_registry_count` and `e51p_registry_at` to discover types; use the
descriptor's pin names to resolve returned handles, rather than assuming every
factory follows the same result layout. Pin descriptors include kind, width,
direction, bus address width, and digital pulls. Names are logical aliases;
for example, the DAC's `out` is its model's native `voltage` endpoint. The 8255
`bus` is an 8-bit target with a two-bit register address. The host chooses the
XDATA address window and connects downstream pins using the ordinary API.

Builtin registration is atomic: an existing ID or allocation failure leaves
the entire catalog unchanged. It never replaces an existing factory. IDs and
keys are case-sensitive ASCII letters, digits, `.`, `_` and `-`.

## Configuration rules

Schemas support signed 64-bit integers, finite real numbers, booleans (0 or 1),
and bounded strings. Numeric limits are inclusive. Each field has a default;
omit it from supplied values to use that default. An empty string is valid when
the schema allows it. Strings are bounded in bytes, excluding the NUL terminator.
The built-in voltage parameters accept finite positive doubles; conversion time
accepts integers from 1 through `INT64_MAX` oscillator ticks.

Instantiation rejects unknown keys, duplicate keys, incorrect types, out-of-range
values, nonfinite numbers and insufficient pin capacity **before** invoking a
factory. No automatic numeric or string conversion occurs. The callback receives
every schema parameter in descriptor order, including defaults, regardless of
the order supplied by the host. A host can save the stable type ID and named
values independently of display labels or enumeration order.

Registries are bounded to 256 factories, 64 parameters per factory and 64 pins
per factory. Metadata addresses remain stable across later registrations. Query
outputs are cleared on failure. Pin output buffers with valid capacity (0–64)
are initialized to `E51P_INVALID_ID`, and partial results are never published.

## Author a factory

Define an `e51p_factory_desc` and call `e51p_registry_register`. Use a stable
namespaced ID for your device family/version. The create callback must:

1. Allocate device state and use the provided peripheral API and configuring
   context to register devices and endpoints.
2. Transfer cleanup to a device `destroy` callback after `add_device` succeeds.
   Before that transfer, the factory owns cleanup for failed registration.
3. Fill every pin handle in the descriptor's advertised order.
4. Return an SDK status, keep borrowed arguments local to the call, and avoid
   graph start, wiring, host I/O, exceptions or reentrant registry mutation.

Creation may partially register a device before failing. Build in a fresh
context and discard that context after any factory failure. The registry
cannot roll back arbitrary callback state. Missing pin results become
`E51P_DEVICE_ERROR`; defensive exception handling prevents C++ exceptions from
crossing the C boundary, although callbacks must not throw. As with ordinary
device callbacks, a factory is trusted native code, not a sandbox.

The host owns synchronization: all registry operations belong to its control or
simulation thread. They are not audio-callback safe. Do not destroy a registry
inside one of its callbacks. Keep factory code loaded for subsequent creation
and while any device callbacks created from it exist; keep any borrowed application state alive for its users.
Opening a native microphone/speaker remains a separate, explicit host action.
The audio bridge/native host retain their existing constructors and lifecycle.

## Build and test

From the public source root:

```sh
cmake -S peripheral-sdk -B build-sdk
cmake --build build-sdk --parallel
ctest --test-dir build-sdk --output-on-failure
./build-sdk/emu51_peripheral_registry_demo
```

The demo registers `example.byte-source`, discovers a DAC, connects their ports,
and prints `Custom source 128 -> discovered DAC -> 2.500000 V`. The separate
installed/relocated C consumer also uses the registry. Tests cover metadata
ownership and lifetime, parameter normalization and rejection, catalog limits,
transactional builtin registration, failure boundaries, and working bus/DAC
devices. Desktop workflow tests exercise the registry-created ADC board.

The desktop now uses this catalog for its built-in 8255, ADC and DAC construction
and pin selectors. Its project format and Add Device list still cover its known
built-in device profiles. Registering another factory in a host does not add it
automatically to the desktop editor; generic custom-device project schemas and
shared-library loading are separate extensions.
