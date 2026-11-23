<!-- SPDX-License-Identifier: LGPL-2.1-only -->
# Peripheral audio

The LGPL audio bridge connects normalized mono audio frames to SDK analog
voltage endpoints. A separate optional LGPL native host uses pinned miniaudio
0.11.26 under MIT-0 for sound-card access. No backend or platform types cross
the public C ABI. The GPL desktop host exposes explicit device/channel controls
in its [Peripherals window](../PERIPHERALS.md#host-audio). Project loading creates
the bridge without enumerating or opening physical devices.

## Build

Normal emu51 builds enable `EMU51_PERIPHERAL_HOST_AUDIO`. Standalone SDK builds
default to the platform-independent bridge only:

```sh
cmake -S peripheral-sdk -B build-sdk -DEMU51_PERIPHERAL_HOST_AUDIO=ON
cmake --build build-sdk --parallel
ctest --test-dir build-sdk --output-on-failure
```

Native builds use `../third_party/miniaudio` by default, or an explicit
`EMU51_MINIAUDIO_DIR` containing the matching header and license. There are no
configure-time downloads. Disable `EMU51_PERIPHERAL_HOST_AUDIO` to build without
the native dependency. Only test executables need threads in a bridge-only build.
Installed consumers use `emu51::peripheral` for the bridge and request the
`audio_host` CMake component / `emu51::audio_host` target for native support.
The backend license is installed with the optional archive.

## Bridge API and wiring

Use `emu51/audio.h`. `e51p_audio_config_init()` supplies 48 kHz, 4096-frame
queues, 480-frame latency and a normalized -1..1 to 0..5 V mapping. Set the
capture/playback direction flags; register the stream before starting the graph:

```c
e51p_audio_config config = e51p_audio_config_init(E51P_AUDIO_CAPTURE | E51P_AUDIO_PLAYBACK);
e51p_audio_endpoints pins = {0};
e51p_audio_stream* stream = 0;
pins.struct_size = sizeof(pins);
/* Check all statuses in application code. */
e51p_audio_create(api, context, "audio", &config, &stream, &pins);
api->connect_analog(context, pins.capture_voltage, adc.vin_positive);
api->connect_analog(context, dac_voltage, pins.playback_voltage);
api->start(context);
```

Capture voltage is `input_offset_volts + clip(sample * gain, -1, 1) * input_scale_volts`.
Call `e51p_audio_set_input_gain(stream, gain)` on the simulation thread to change
linear gain live (0–1000, default 1). It affects queued input when consumed,
preserves the zero-sample voltage and survives pause/reset. It requires no
native-device restart. Nonfinite, negative or excessive gains are rejected.

`e51p_audio_read_scope()` copies up to the newest 4096 sample boundaries in
chronological order: raw normalized capture, amplified/clipped capture, and
normalized playback queued to the host. Call it on the simulation thread with a
caller-owned array; it neither consumes audio queues nor runs in callbacks.
The returned rate determines the time axis. History freezes on pause and clears
on resume/reset. Missing capture samples are zero; coarse advances repeat the
final observable input over skipped boundaries. The scope reports bridge
signals, before native latency and playback queue gaps.
Playback is `(voltage - output_offset_volts) / output_scale_volts`, clipped to
-1..1. Capture clips finite samples and replaces nonfinite samples with zero.
Both scales are positive; configured input extremes must remain finite. An
absent playback voltage is normalized silence. The bridge supports context
clocks up to 1 GHz and sample rates from 1 to 192000 Hz. Queue capacity is a
power of two from 2 to 1048576; latency must be smaller than capacity.

The context and caller each hold a stream reference. The native host retains
an additional reference while attached. Stop callback users before releasing
their references. Context teardown detaches and silences its stream; retained
callback storage remains valid without accessing a destroyed SDK context.
Endpoint registration failure requires discarding the partial graph.

### Changing buffer sizes

`e51p_audio_set_buffering(stream, capacity_frames, latency_frames)` changes both
queue allocations and startup latency without changing the context clock, gain,
held output or cumulative statistics. Call it on the simulation thread with a
paused stream, **all manual callbacks stopped/joined**, and **no native host
attached**. A paused native device still counts as attached: close it first.
Invalid arguments/state fail without altering buffers; both replacement
allocations complete before changes are published. Reopen the host and resume
to begin a fresh audio epoch with the new latency. The desktop Live control
performs this stop/close/resize/reopen sequence and preserves CPU/player state.

## Timing and buffering

The desktop's execution worker produces CPU/peripheral audio independently of
UI drawing and event waits. The UI briefly takes exclusive model access for
inspection, edits and device reconfiguration; no SDK operations on the same
context run concurrently. Native callbacks remain isolated from both.

The simulation owner alone calls the SDK, pauses/resumes the bridge, and resets
the context. Audio callback threads only call `e51p_audio_capture()` and
`e51p_audio_playback()`. Each ring has exactly one producer and one consumer;
do not combine manual callback calls with an attached native host. The two
directions can use different callback threads. Buffers are allocated at creation;
callback entry points use lock-free atomics and do not allocate, lock, query a
wall clock or access the SDK. Platforms without the required lock-free atomics
reject bridge creation.

Integer arithmetic retains fractional emulated ticks between sample boundaries.
Playback samples hold the most recent analog voltage. Capture updates its
voltage endpoint to the sample for the latest boundary crossed by an advance.
If a single advance spans several input samples, the CPU observes the final
one; this does not add sub-instruction scheduling to the core. Long advances
perform bounded work and account for dropped playback instead of allocating or
looping over every skipped frame. Unrepresentable frame counts latch an error.

Every frame carries an epoch and sample index. Capture and playback each begin
with the configured latency as silence. Missing expected samples become silence;
future samples keep their original index. Full queues drop newest frames and
preserve the resulting gaps. Old/intermediate capture frames are discarded
when the simulation has passed their sample index. Playback similarly discards
late frames rather than replaying old audio after an underrun.

`e51p_audio_set_paused()` immediately gates callbacks, rebases the stream's sample
timeline at the current emulated time and invalidates old queued frames. Paused
input is the configured zero-sample voltage. Resume starts a fresh latency
window. Context reset starts another epoch, clears the bridge error and resets
its held playback sample; analog connections then republish device reset values.
Output already submitted to the OS retains the native device's buffer latency.
Neither operation resets queue indices owned by another thread.

`e51p_audio_get_stats()` returns cumulative atomic counters and the first fatal
bridge status. Capture misses count sample-boundary lookups without the expected
sample, not host callback frames. Capture skips include samples crossed inside
large simulation advances. Check fatal status after servicing execution. Live
I/O needs wall-clock pacing near the configured oscillator rate; accelerated
execution, stalls and clock drift can produce counted gaps or drops. The first
version does not implement adaptive clock-drift correction or a recording file
format. Manual callback entry points also support externally recorded/replayed
frame streams when the caller preserves timing, reset epochs and configuration.

## Native devices

Use `emu51/audio_host.h`. Create a host, enumerate capture and playback lists,
then open a selected stream with `e51p_audio_host_config_init()` and explicit
device indices or `E51P_AUDIO_DEFAULT_DEVICE`. Indices refer to that host's
immutable enumeration and are not persistent device identifiers. Recreate the
host to refresh a changed device list. The native backend never silently falls
back to the null backend.

Capture selects one native channel, zero-based. Playback can select a single
channel or replicate mono to all channels; unselected channels are silent.
The backend converts native sample formats/rates to the stream's configured
float rate, requesting 256-frame callback periods where supported. Each callback
uses fixed stack workspace for channel selection. A stream admits one native
host to preserve the ring-buffer ownership contract.

Creation/enumeration do not open a device. Open may invoke OS microphone
permission handling for a capture device, and start begins callbacks. The
example embeds a microphone-purpose description on macOS; applications embedding
the SDK must supply their own platform privacy descriptions/permissions.
`host_start` rebases/unpauses the stream. `host_stop` pauses it, clears pending
audio and waits for callbacks to stop; close/destroy release native resources
before dropping the host's stream reference. Do not call these from callbacks.
Native failures include a readable `e51p_audio_host_error()` message.

## Runnable ADC / DAC example

The normal build produces `build/peripheral-sdk/emu51_peripheral_audio_demo`.
With a standalone SDK build, it is directly in that build directory.

```sh
./build/peripheral-sdk/emu51_peripheral_audio_demo --list
./build/peripheral-sdk/emu51_peripheral_audio_demo --capture default --playback default --seconds 5
./build/peripheral-sdk/emu51_peripheral_audio_demo --playback default --seconds 5
./build/peripheral-sdk/emu51_peripheral_audio_demo --null --capture default --playback default --seconds 0.15
```

The capture/playback example wires host input -> analog voltage -> ADC0804 ->
parallel DAC -> host output, using the SDK's real control pins and data ports.
Without capture it supplies a 440 Hz analog test signal to the ADC. The example
paces the graph in real time, uses 1024-frame latency, and maps the DAC's full
voltage span to +/-0.1 playback amplitude. Device/channel options are printed by
running without arguments. Nothing opens until a capture/playback command is
selected. The null backend exercises the same threaded path without touching a
microphone or speakers.

For each start/resume, the native host drains old queues while callbacks are
stopped, starts the device with its bridge paused, and opens a fresh sample epoch
only after initialization completes. The application must reset its wall-clock
pacing at that point; time spent waiting for native device startup is not CPU
execution time. Counting that delay can leave capture permanently ahead of the
incoming samples, producing silence even though device callbacks are running.
The desktop resets pacing on this transition and displays Running/Paused/Closed
alongside captured, capture-used and playback-used frame counts. Pause leaves
the selected devices open; Run restarts them without resetting CPU state.

Automated checks cover exact voltage mappings, rational clocks, clipping,
overflow/underflow gaps, pause/reset while callbacks run, reference lifetime,
actual ADC/DAC wiring, native null-device enumeration/channel validation and
five successive stop/start cycles, with and without a graph reset. Physical device behavior still needs interactive testing on
each target OS; tests do not open real devices.
