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
