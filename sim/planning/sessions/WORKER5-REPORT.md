# WORKER5 report: the RV32 controller on the framework (PLAN-M1 item 8)

One tile controller, built as units on W3's engine, runs the same firmware as `sim/ref` and writes **the same retire
trace, line for line, from reset (the boot ROM included) to halt, on all 67 golden traces**, under every debug check and
on up to 16 threads. Branch `sim-w5`; the details, the profiles and every number's provenance are in `NOTES.md`.

## What was built

| file | what |
|---|---|
| `src/units/rv32.h` | the core: `Controller.runCycle`/`execute` line for line (RV32I + Zmmul, divides illegal; the trap CSRs, `mcycle`/`mtime`; traps, halts, WFI, parking; the memories, the boot ROM, the address decode with its bus errors and UB stops; ref's timing), templated on its environment; catch-up of the cycles it sleeps through; its digest |
| `src/units/regblock.h` | TileControl's M1 subset as plain code: `endCycle` in its order, the view (PARAMS, STATUS, ERRORS, EVENTS, the mask, EVENTS_REQUIRED, the wake and interrupt terms), RequestPort's count of the request FIFO |
| `src/units/regmap.h` | the address map and the register table (Registers.java's subset) |
| `src/units/controller.h/.cpp` | the three units of sketch 2 on the API: `Core` (level 1), `RegBlock` (the shifted end of cycle, level 0), `TestBench` (replays the golden's MEMW, GO, CMD and REQ-FREED lines and nothing else); `configure_tile` |
| `src/units/trace_log.h/.cpp` | the trace as fixed-size records written in the loop, formatted after the run |
| `src/units/units.mk` | the make rules (one include line in `sim/Makefile`): `make golden`, `make bench_controller`, `make tsan-units` |
| `tests/golden_run.h/.cpp` | the harness: parse a golden, load its images and the boot ROM, check its D12 header against our configuration (refusing, naming the key and both values), run (direct driver or engine), write the trace, compare line for line with context; the debug-check option sets; the threads test (`check_parallel`) |
| `tests/golden_main.cpp` | `golden [--direct \| --engine \| --threads N --cpus ... \| --bench R] [names]` |
| `tests/bench_controller.cpp` | the speed benchmark (a 13-cycle loop booted as `hello`), on the engine, on the plain core, on T threads; `--write` for ref |
| `tests/ref/SpeedRun.java` | ref's speed on the same program, booted as TraceRun boots a golden, the observer off |
| tests | `test_rv32` (34), `test_regblock` (7), `test_golden` (8), `test_controllers` (2), `test_broken_units` (3) |

## Acceptance

**Machine for the runner results:** Intel Core i5-14400F, 1 socket, 6 P cores (CPUs 0-11, 2 threads each, 4.7 GHz max) and
4 E cores (CPUs 12-15, 3.5 GHz max), L3 20 MiB, 64 GB, Linux 6.8.0-147; clang 18.1.3, g++ 13.3.0, OpenJDK 21.0.12.1.

| test | result | where |
|---|---|---|
| the 67 goldens, direct driver (plain logic, ref's order, no framework) | all match, the whole file: our own header (sha256s, the skew from `java.util.Random(1)`), body, closing block | `make check`, every build |
| the 67 goldens on the engine | all match, nothing left asleep with data waiting, no allocation in the run | `make check`; Quest 127, 131 |
| the six option sets: ready-driven, shuffle seeds 1-3, instance-major, step-all | every trace equal to the golden, every digest to the ready-driven run's, step-all checked to be in effect | `make check`; **Quest 131** |
| the checks bite | a block counting its own steps: step-all's digest differs (the trace can't see it); a block leaving its bus word: step-all's digest differs and the missed-wake detector names `t.ctl.bus` at cycle 12 | `test_broken_units` |
| D12 | a golden made under another configuration is refused, naming it (`refused (D12): config mul_latency: golden 4, ours 3`); a core that differs is caught | `test_golden` |
| N controllers on N threads (W4's `run_independent`), each a different golden, all 67 in rounds of N | every trace equal to its golden, every digest to the same golden alone, under the six option sets, at N = 1, 2, 4, 6, 12 pinned (CPU lists detected, D21) and 16 unpinned; TSan clean (gcc, clang) | `make check` (N = 3), `make tsan-units`; **Quest 129, 131** |
| `make check` | clean: gcc and clang, debug (ASan, UBSan) and release, TSan | locally and Quest 131 |

## Lines of code (not blank, not comment-only; total in brackets)

| C++ | | ref | |
|---|---|---|---|
| `rv32.h` (the core, its catch-up and digest, the block's load/store decode) | 411 (474) | `Controller.java` | 376 (449) |
| `regblock.h` | 84 (107) | TileControl's M1 parts (~165 of its 300) and RequestPort's count (~10 of 29) | |
| `regmap.h` | 67 (82) | Registers.java's map and block table | |
| `controller.h/.cpp` (three units, ports, wiring) | 149 (190) | (Tile's wiring) | |
| `trace_log.h/.cpp` | 107 (129) | Observer.java | |

## Speed

**Machine:** the runner above; clang 18.1.3 -O2 -DNDEBUG (D18); one thread pinned to a P core (CPU 0; D13) unless stated.
The program: a loop of 9 instructions in 13 cycles (data load and store, three ALU, a multiply, a register-block load, a
taken branch), 10^6-10^7 iterations, booted as `hello`; its trace at N = 1000 is identical in ref and the framework.

| run | M instructions/s | ns per core step | source |
|---|---|---|---|
| the framework, the engine, untraced | **44.0** | **20.5** | Quest 130 (127: 43.6) |
| the framework, traced | 42.0 | 21.4 | Quest 127 |
| the plain core alone (no engine, no block) | 156 | 5.8 | Quest 127 |
| the 67 goldens, 300 times each (short runs, cold start) | 37.4 | 24.6 | Quest 127 |
| **ref, idle-skip on** (the default; 63 tiles idle) | **0.76** | (0.91 µs per chip cycle) | Quest 130 |
| **ref, idle-skip off** | **0.019** | (36 µs per chip cycle, as D15) | Quest 130 |

**Against ref:** 58x with idle-skip on, 2,300x off; but ref steps a whole chip (the request bus, its scans and invariant
checks, every unit of 64 tiles when idle-skip is off) and M1's framework run has only this tile's three units, so those
ratios mostly measure chip-level work the framework doesn't do yet. **Unit for unit: ~4.5x** (ref's Controller and
TileControl ~104 ns per instruction, from JFR: 7.95% of its time; the framework's whole run ~23 ns). Where the time goes:
the framework, ~63% engine (`Engine::cycle` a third: fixed per-cycle work), ~34% the units; ref, 33% RequestBus, 49%
Chip's loop, scans and checks, 2.3% Controller, 5.6% TileControl.endCycle (NOTES, "The framework against ref").

**Threads** (T controllers, each its own loop; Quest 129):

| threads | CPUs | M instructions/s in all | per thread | vs 1 |
|---|---|---|---|---|
| 1 | 0 | 43.0 | 43.0 | 1.0x |
| 6 | one per P core | 221-224 | 37.2 | 5.2x |
| 12 | every P-core thread | 211-239 | 18.6 | ~5.2x |
| 4 | the E cores | 75.0 | 18.8 | — |

Hyperthreads add nothing (IPC 5.5: one thread fills a core); six P cores give 5.2x (the all-core clock is lower).

## For M2

- **What M1 doesn't model stops loudly**: `grep "not modelled in M1" src/units/rv32.h` lists them (the router registers
  and send holding register, the write counters, DMA, the read port, REQUEST_GET/RECEIVE, peripheral 1's window, the
  table window's HIST and its stores). Each needs its unit in M2; ref's `Controller.runCycle` step 3 (the refused router
  send's retry) is the one cycle-order item not yet in the core.
- **The bench goes**: MEMW (the director's port-B writes), GO and CMD (C&C and the landing) and REQ-FREED (the request
  bus) become real units' outputs into the same ports (`memw`, `go`, `landed`, `freed`, the `req_freed` wire).
- **The write counters must reach the block as events**, not Counts (PORTS.md 8): they feed endCycle's status and the
  counter events, which are a no-op in M1 (counters never armed).
- **The interrupt paths M1 can't provoke** (from ordinary code; waiting for a multiply or branch stall; preempting a
  block load's first cycle or a refused REQUEST_SEND) follow ref's rule and are unit-tested on the plain core
  (`test_rv32`); M2 tests them end to end once done-early (ERRORS [8]) exists.
- **A unit that sleeps and catches up** (the core's `mcycle`, its stall and WFI counts) must fold those counters into its
  digest relative to its last step, or step-all's digest can't match; `Rv32::digest` is the pattern.
- **The engine's per-cycle cost dominates one busy unit** (~390 x86 instructions a cycle, `Engine::cycle` a third of the
  time with 3 wheels and 3 runs); with W3 for its performance pass. The core's own fast path (~141 instructions per RV32
  instruction now; a third in the checks before each one) can follow.
- **Chip-level work is where ref spends its time**: the request bus and the chip's scans and checks run every cycle in
  ref. As M2 adds them, keep them event-driven (D23), or the framework rebuilds ref's bottleneck one level up.
- **Open, with W3**: the missed-wake detector allocates inside the run (one death test runs outside the allocation hook
  until the fix lands). `Sim::run_until(t)` is now inclusive; the harness uses `run(END)`, unaffected.
- **D21**: my task scripts take the CPU lists as arguments or detect them, and record the machine
  (`planning/sessions/WORKER5-tasks/machine.inc`, until W4's `sim/tools/machine.sh`).
- **The direct driver** stays as a regression check only (`golden --direct`, `test_golden`), not to be extended.
