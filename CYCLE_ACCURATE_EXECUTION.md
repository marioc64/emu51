# Execution timing and accuracy

emu51 provides wall-clock pacing for the classic 12-clock 8051 model. The settings
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
starts a fresh interval instead of replaying suspended time. Ordinary jitter
and unfinished work retain the budget. When a host cannot keep up, execution
slows relative to wall time rather than discarding ordinary accumulated work.

The frontends service execution independently of drawing:

- macOS uses a common-mode NSTimer with a nominal 240 Hz interval.
- Linux/Windows use timed GLFW event waits, nominally 240 Hz while running, with
  rendering paced separately at approximately 60 Hz.

These are requested service intervals, not real-time scheduling guarantees.
Each service call executes at most 65,536 instructions. A four-millisecond wall
budget is checked every 64 instructions, so that deadline is a responsiveness
target rather than a strict upper bound. Step Over/Out use bounded service
batches too and stop after one million instructions if the expected return is
not reached. Pause, debugger stops, resets, and relevant machine edits cancel
pending step tasks.

The toolbar reports completed machine cycles per second in crystal mode and the
percentage of the requested rate. At 12 MHz, 1.000 Mcycles/s is 100%. The core
contains no host sleeps. A blocked main thread, such as a synchronous dialog,
still suspends CPU servicing; no worker thread currently runs the CPU.

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
- Counter mode suppresses automatic timer increments, but external input pulses
  are not sampled as counter events.
- The core's `c_time` is an `unsigned long`; on Windows it is 32 bits and wraps
  after about 71.6 minutes at one million cycles/second. GUI and console totals
  use separate 64-bit accounting. The pacer does not use `c_time` as its clock.

Frequency settings do not select other 8051 derivatives or 1T/6T timing profiles.
Applications requiring exact pin transitions or complete peripherals need a
more detailed event/timing model.

## Serial model

The byte/frame model follows the classic 8051 serial modes described in chapter
3 of the [Intel MCS-51 User's Manual](https://web.mit.edu/6.115/www/document/8051.pdf).
Mode 0 shifts eight data bits at oscillator/12. Mode 1 frames contain ten bits;
modes 2/3 contain eleven, including TB8/RB8. Mode 2 uses oscillator/64, or /32
with SMOD. Modes 1/3 divide Timer 1 overflow events by 32, or by 16 with SMOD.
Every overflow counts, even if several occur during one instruction. Existing
counter-input and Timer 0 split-mode limitations also affect the baud clock;
8052 Timer 2 baud generation is not implemented.

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
