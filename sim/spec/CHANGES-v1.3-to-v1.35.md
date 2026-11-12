# v1.3 to v1.35

*What v1.35 changes against v1.3. The hardware is v1.3's; the simulator is v1.3's rewritten in C++ (`src/`); the spec is v1.3's, restructured.*

## For the RTL

**Nothing changes.** v1.35's hardware is v1.3's, unit for unit: every normative statement, register, number and parameter of v1.3's spec stands. The guide from v1.26.1 is still `spec/history/CHANGES-v1.26.1-to-v1.3.md`.

## Where v1.3's spec went

Every section keeps its number; a section that moved leaves a one-line stub under its number in `spec/machine-spec.md`.

| v1.3 | v1.35 |
|---|---|
| `machine-spec.md`, all but what follows | `spec/machine-spec.md`, the platform |
| 4.19, the replayer; 8.11's replayer window; its invariant (9, 16) and parameters (10) | `spec/jolt/replayer.md` |
| 4.21, the Tracer; 8.16, its window; its invariant (9, 16) and parameters (10) | `spec/jolt/tracer.md` |
| `replayer-opcodes.md`, the appendix | `spec/jolt/replayer-opcodes.md` |
| `schedule.md` | `spec/schedule.md` |
| the preamble's "in brief" blocks and its companions for the history documents | `spec/history/machine-spec-v1.3-preamble.md` |
| section 13 as it stood | `spec/history/machine-spec-v1.3-section-13.md`; the new section 13 lists what is open today |
| the `CHANGES-*` files; `v1.3/planning/DECISIONS-v1.2.md` to `-v1.3.md` | `spec/history/`, names unchanged |

Every statement about the simulator and every path was made true of v1.35; nothing else in the text changed.

## The simulator's deliberate differences from v1.3's

v1.35's meshsim matches v1.3's results exactly, and its cycle counts exactly except in these cases, each approved and recorded (the decision in brackets):

- **A counter store and a decrement in the same cycle** (D27). The traffic director judges a due decrement without the core's counter store of the same cycle, which reaches the register block a cycle later; v1.3's simulator did the same through its fixed step order. No reference program reaches the case.
- **A router FIFO's overflow is counted at t + 1** (D28), with its `ERRORS` bit. v1.3's counted it whenever the FIFO was next touched, a variable number of cycles late. No reference program overflows one.
- **A second READY pulse while the first is still visible** (D29). v1.35 keeps the spec's sticky bit and counts the case (`ready_repulses`); v1.3's read 0 at C&C 1 until the new pulse landed. Likewise an overflow that feeds `ERRORS` reaches the register block as an event, counted at t + 1. No reference program reaches either.
- **C&C 1's peripheral applies a store one cycle later** (D33, D44): at t + 1, as every peripheral slot does, where v1.3's applied it within the store's cycle. C&C 1's own work (the watchdog's first count, a UART byte's start) runs a cycle later; one suite's printed figure differs by one cycle and is masked in its comparison.
- **The request bus's credit latency is a setting** (D34): 0 is exact as v1.3's (every sender in the bus's domain); 3, the default, is the spec's, each port's leaf in its tile. They differ only when a node's 8-beat buffer fills, which is counted; the reference runs never reach it.

## Where v1.35's checks differ from the rule

The machine specification's preamble gives the rule: undefined behaviour is fatal, the silent traps stop the core, and every other invariant is counted and, in the debug and suite builds, reported. As of 2026-11-12 these cases differ:

- **Counted, not fatal:** a packet of size 11 (3.1, undefined); a stream with a misaligned source or destination, or with *d* = 126 from a mesh tile (4.2), which are also not reported.
- **Counted but not reported:** a late rotation (invariant 3) and a `HIST` count past 17 bits (invariant 6). **Reported but not counted** in the release build: uncounted traffic other than mode 0 (invariant 22).
- **Fatal, where the spec says ignored and flagged:** a run started while the replayer runs (4.19.9), and `PULSE_DMA` while a run is running (8.6); invariant 26 and 4.17.2 make both undefined.
- **Not checked:** a buffer both read and written in one run (invariant 2); `DMA_HDR` or `DMA_ADDR` changed mid-transfer (4.17.9); the 1, 3 and 1 requests outstanding, the tag windows and reads during a group run (invariant 17); one source per destination for modes 1–3 into big SRAM (invariant 18); the overtaking read (invariant 24), which only the harness's S1 measurement covers; a Tracer transfer asking for more than its credits cover (4.21).

## The drivers the spec's measurements name

The `[meas: …]` labels name v1.3's Java tests and drivers (Markers). What replaces each in v1.35:

| v1.3 (Java) | v1.35 |
|---|---|
| NetTest (fan-in, `skewtable`) | `tests/suite_net.cpp` |
| MemTest | `tests/suite_mem.cpp` |
| Stage B | `tests/stageb.cpp` |
| AllToAllTest (`measure`) | not yet ported (2026-11-12) |
| ReadPortGatherTest | not yet ported (2026-11-12) |
| SearchBench | not yet ported (2026-11-12) |
| CondTest | not yet ported (2026-11-12) |
| S1Bench | not yet ported (2026-11-12); `measure_s1` (`tests/mach.h`) is the harness side of its measurement |
| SplitRun | not carried: its driver doesn't build in v1.3 |
