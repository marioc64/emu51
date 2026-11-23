# Execution timing and accuracy

emu51 provides wall-clock pacing for the classic 12-clock 8051 and 8052 models. The settings
label **Cycle exact** selects that pacing mode; it does not imply oscillator-phase,
bus, pin, or complete peripheral accuracy.

## Timing model

```text
machine cycles per second = crystal frequency in Hz / 12
instruction duration      = instruction machine cycles * 12 / crystal frequency
simulated elapsed time    = completed machine cycles * 12 / crystal frequency
```

| Crystal frequency | Machine cycles/second | One machine cycle |
| --- | ---: | ---: |
| 11.0592 MHz | 921,600 | about 1.08507 microseconds |
| 12 MHz | 1,000,000 | 1 microsecond |
| 24 MHz | 2,000,000 | 0.5 microseconds |

Instructions have different costs. At 12 MHz, a one-cycle instruction takes
1 microsecond of simulated time, and MUL/DIV take four cycles. Interrupt entry
adds a two-cycle hardware call to the completed step.

## Modes and saved settings

**Cycle exact** is the default, with a 12 MHz crystal. The supported frequency
range is 0.000001–1000 MHz; conversion to integer Hz gives 1 Hz resolution.
**Instructions per frame** budgets a fixed number of instructions per logical
1/60-second frame. It defaults to 500, or 30,000 instructions/second. Its valid
range is 1–1,000,000 instructions per logical frame. Neither mode uses display
refresh as its time source.

Apply saves execution preferences to the per-user `settings.json`; Cancel leaves
saved settings unchanged. For example:

```json
{
  "execution_mode": "cycle_exact",
  "crystal_mhz": 12,
  "instructions_per_frame": 500
}
```

The alternative mode string is `instructions_per_frame`. Missing fields use
schema defaults. Invalid JSON, duplicate fields, or schema/type errors cause the
whole document to fall back to defaults. Valid documents with out-of-range
numeric fields retain defaults for those fields. Unknown execution-mode strings
select Cycle exact. Preference paths are listed in [README.md](README.md#configuration).

## Budget and service loop

`Emu51CycleTiming` uses `std::chrono::steady_clock`. In crystal mode, integer
arithmetic converts elapsed nanoseconds and crystal Hz to machine-cycle debt.
The fractional remainder is retained, and completed instructions debit their
actual cost, including interrupt entry. An instruction may finish ahead of the
budget; that negative balance is carried forward rather than rounded away.
Instruction mode uses the same mechanism, consuming one unit per instruction.

Pause/resume, CPU reset, program loading, and settings changes rebase the pacing
interval. A gap longer than one second, or a backward time value in a test,
starts a fresh interval instead of replaying suspended time. When native audio
is running, this also rebases the audio bridge's frame timeline: callbacks may
have continued during the suspension. CPU/peripheral state and emulated time
are preserved; the native device stays open. Otherwise the callback would keep
requesting future frame indices while every newly generated frame was stale.
Ordinary jitter
and unfinished work retain the budget. When a host cannot keep up, execution
slows relative to wall time rather than discarding ordinary accumulated work.

The desktop runs CPU instructions, timers and peripherals on a dedicated
**execution worker**, independently of UI event loops and graphics submission.
It normally services wall-clock debt every millisecond. A busy batch yields the
model lock after roughly one millisecond (checked every 64 instructions), so
Pause and debugger commands can take effect promptly. Each batch is also
limited to 65,536 instructions; Step Over/Out retain their one-million-step
limit. Headless/manual servicing retains its four-millisecond batch limit.

UI reads and machine edits acquire exclusive model access at batch boundaries.
The text editor releases that access during highlighting/layout/rendering, and
its breakpoint/edit callbacks reacquire it briefly. Native file dialogs,
assembly preparation, project writes, recovery autosaves and graphics backend
work run outside the model guard. CPU/audio production therefore continues
through slow editor rendering, modal dialogs, minimized windows and GPU waits.
Applying a program or changing machine/device settings is synchronized with the
worker; reset, stepping, breakpoints and watchpoints retain their CPU semantics.

- macOS keeps its common-mode UI refresh timer. Its `tick()` call wakes the
  worker instead of executing CPU work on the main thread. Metal rendering has
  its own display cadence; the execution worker uses interactive QoS.
- Linux/Windows wait for events with a nominal 60 Hz animated redraw cadence.
  `tick()` likewise signals the worker. A pending final redraw after a worker
  debugger stop ensures the stopped state is visible before idle event waits.
- Both frontends stop and join the worker before destroying UI/native resources.
  Native sound-card callbacks remain separate and only access audio queues.

A worker thread improves scheduling isolation; it is not a hard real-time OS
reservation. Audio still needs a buffer for scheduler jitter and heavy host
load. Live audio buffering is configurable in the Peripherals window. The
requested oscillator rate must remain within the host's emulation capacity.
System sleep can suspend both the worker and UI; after a long suspension CPU
pacing and audio timelines rebase together.

The toolbar reports completed machine cycles per second and the percentage of
the requested rate. At 12 MHz, 1.000 Mcycles/s is 100%. The core contains no host
sleeps. The worker sleeps/yields only in the host service layer, retaining
fractional cycle debt and instruction/interrupt timing.

## Core behavior and limits

Opcode sizes and cycle costs live in `emu51/core/opcode_metadata.cpp` and are
shared by CPU dispatch, disassembly, and assembly. `process()` and
`process_table()` apply the instruction, update parity/register-bank state,
advance timers/serial frames, and then arbitrate supported interrupts. Interrupt entry also
advances timers/serial frames. Timer mode 2 handles each overflow/reload within the elapsed
instruction and entry cycles.

The following limits remain:

- Instruction effects are atomic before timers advance. Register and port
  reads/writes have no modeled position within an instruction's machine cycles.
- Supported external/timer/serial interrupts are arbitrated after instructions. Detailed
  request sampling delays and inhibition following RETI or IE/IP access are not
  modeled.
- Idle mode and power-down execution behavior are not modeled.
- Timer 0/1 counter mode suppresses automatic increments; external pulses are not
  sampled in standalone hosts. With the optional GPIO interface, Timer 0/1 count
  sampled falling edges on P3.4/P3.5. Timer 2 samples T2/T2EX levels at instruction
  boundaries, so pulses entirely within an instruction are not observed.
- The core's `c_time` is an `unsigned long`; on Windows it is 32 bits and wraps
  after about 71.6 minutes at one million cycles/second. GUI and console totals
  use separate 64-bit accounting. The pacer does not use `c_time` as its clock.

CPU selection chooses 8051 or 8052. Frequency settings do not select a CPU model
or change the classic 12-clock instruction timing to 1T/6T.
Applications requiring exact pin transitions or complete peripherals need a
more detailed event/timing model.

## Optional peripheral GPIO host

The headless peripheral SDK integration exposes P0..P3 as connectable digital
ports. Ordinary port reads sample pins; RMW operands read latches. Byte/bit writes
settle at the instruction's start timestamp. Peripheral time advances to the end
boundary before pin sampling, timer/UART updates and interrupt arbitration.
Interrupt entry separately advances devices and timers for its two cycles,
without another arbitration pass. Connected INT0/INT1 support level/edge requests;
timer gates and external counters use sampled pin levels/edges. Detailed
within-instruction transitions and interrupt sampling delays are not modeled.
See [the SDK guide](peripheral-sdk/README.md) for host setup and limitations.

## CPU memory and Timer 2

The 8051 has 128 bytes of internal RAM; the 8052 has 256. Direct addresses below
80h access lower RAM, while direct addresses 80h–FFh access SFRs. Indirect and
stack accesses use internal RAM, never SFRs. Unimplemented 8051 upper RAM reads
return FFh and writes are discarded; this is a deterministic emulator convention,
not a claim about undefined hardware reads. Unimplemented SFRs follow the same
FFh/discard convention. Reset clears both RAM and SFR storage.

The 8052 implements the classic T2CON, RCAP2L/H and TL2/TH2 registers. Timer 2
supports 16-bit capture, auto-reload and baud generation. TF2/EXF2 request interrupt
002Bh under EA/ET2 and IP.PT2; software must clear these flags. TF2 is suppressed
in baud mode. T2EX capture/reload requires EXEN2, and in baud mode only sets EXF2.
Timer 2 also advances during interrupt entry.

Host code can set external T2/T2EX levels with `setTimer2Inputs()`. Levels are
ANDed with the resolved P1.0/P1.1 pins when GPIO is connected (otherwise the
latches) and sampled at instruction boundaries. A falling
edge on T2 advances counter mode once; a falling T2EX edge captures or reloads
at the modeled end of the instruction. Oscillator-phase sampling is not modeled.
T2MOD, down-counting and clock-output extensions are excluded from this classic
8052 profile. See the [Intel MCS-51 User's Manual](https://bitsavers.trailing-edge.com/components/intel/8051/MCS-51_Users_Manual_Feb94.pdf).

## Serial model

The byte/frame model follows the classic 8051 serial modes described in chapter
3 of the [Intel MCS-51 User's Manual](https://web.mit.edu/6.115/www/document/8051.pdf).
Mode 0 shifts eight data bits at oscillator/12. Mode 1 frames contain ten bits;
modes 2/3 contain eleven, including TB8/RB8. Mode 2 uses oscillator/64, or /32
with SMOD. Modes 1/3 divide Timer 1 overflow events by 32, or by 16 with SMOD.
Every overflow counts, even if several occur during one instruction. Existing
Timer 1 counter-input and Timer 0 split-mode limitations also affect that clock.
On 8052, TCLK and RCLK independently select Timer 2 for transmission/reception in
modes 1/3. Each Timer 2 overflow is divided by 16 regardless of SMOD. In baud timer
mode Timer 2 advances six ticks per machine cycle (oscillator/2); all overflows
within an instruction clock the UART.

TI is delivered at the modeled start of the stop bit (after eight bits in mode
0); RI is delivered at the modeled midpoint of the stop bit (after eight bits
in mode 0). Flags remain latched until software clears them. An uncleared RI
drops a newly completed asynchronous frame, preserving the unread receive byte.
SM2 filters invalid stop bits in mode 1 and clear ninth bits in modes 2/3.
Serial interrupts use the shared RI/TI request at vector `0023h` and participate
in the normal EA/ES and IP.PS priority arbitration.

Frames begin on instruction service boundaries. The model does not reproduce
oscillator phases, initial UART startup/sampling delays, RXD/TXD pin transitions,
baud mismatch, noise, mode 0 pin arbitration, or an external serial device.
Queued host bytes wait for REN and a usable clock. Writes after TI preserve the
stop bit before the next frame; premature SBUF writes restart the active frame
and increment an overwrite counter, rather than reproducing hardware corruption.
Changing the serial mode aborts in-flight frames. Configure baud controls between
frames for predictable timing. CPU reset clears the queues, active frames, and
serial counters. Pin-level timing remains outside emu51's accuracy claims.

## Validation

Regression tests cover opcode contracts and selected independent timing costs,
interrupt entry, timer modes and reload boundaries, pacing with injected time,
fractional frequencies, irregular service intervals, retained debt, suspension,
reset/resume, and bounded stepping. Both dispatch paths are compared for CPU,
memory, cycle, and interrupt behavior.

Serial tests independently check mode/SMOD flag boundaries, ninth/stop bits,
read/write buffer separation, direct SBUF instruction forms, interrupt priority,
overflow/queue bounds, reset, and interrupt-entry clocking in both dispatch paths.
Application tests assemble and run the generic serial-interrupt echo example
through the terminal input/output paths, including binary data and project compatibility.

`emu51_bench` measures unpaced core throughput with a synthetic or supplied
program; its results are workload/compiler dependent and exclude the GUI. Use
an optimized Release build. Core throughput does not establish full-application
pacing accuracy or desktop responsiveness. Test those on each release platform.

Historical development probes are not release performance specifications, and
regression coverage is not an exhaustive hardware conformance certification.

Model tests cover RAM/SFR isolation, upper-RAM stack/calls/interrupts, Timer 2
capture/reload/counter edges, sticky flags, priority, and separate UART clocks.
Project workflow tests cover model selection, reset, backward compatibility,
unknown-model validation, and upper-RAM inspection/watchpoints.

`validation_tests.cpp` explicitly selects the 8051 profile and independently
checks all ADD/ADDC/SUBB input bytes and carry states, all MUL/DIV byte pairs,
result flags and parity, and a specification-based cycle table for every defined
opcode. It also checks all register banks and MOVX pages, isolation of absent
upper RAM, PUSH SP ordering, and POP SP destinations including 00h and FFh.
Both dispatch paths run the same expectations. POP decrements the old stack
pointer before writing its destination, so POP SP retains the popped byte.
Expectations follow section 1 of the
[Atmel 8051 Hardware Manual](https://www.microchip.com/content/dam/mchp/documents/OTH/ProductDocuments/UserGuides/doc4316.pdf).
These checks validate instruction-boundary behavior within the limits above;
they do not establish pin-level or complete hardware conformance.
