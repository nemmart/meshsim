# WORKER20 notes (P2: the machine harness, the checker and the outputs)

Branch `sim-w20`, from main e90facf. Rows: C1 with B18, B19, B20, H1, H2, H3, H6, B25, C0's runner, C2.

## The plan (agreed with the owner, 2026-11-10)

Commit and push after each step; each step checked against `ref`.

0. **`sim/tests/suite.h`, alone and first** (owner: W21 builds its Tracer suites on it now). Java's `T` in C++: `group`,
   `pending`, `check`, `eq`, `eqo`, `throws` (Java's `throwsISE`), `ub`, `done`, every line and count as Java's `T`
   prints them. Checked by one driver on both sides (`tests/ref/TSelf.java` against `tests/test_suite.cpp`), its output
   committed as a fixture.
1. **The machine harness** (C1, B18), header `sim/tests/mach.h` published before its body, for P4-P6:
   - the full machine (`Config.machine()`): boot an image through C&C's model or through the cores (`Mach.boot`,
     `bootCores`, `hostBoot`, `stageCc1`, `phase`, `mbox`), run until a condition, stop, peek and poke SRAM, the
     controller memories and the registers, exit values and the halt summary, images with their symbols;
   - the coreless machine (`Config.defaults()`, `Net`, `Tile.set*`): tiles in RUN without a controller, GATHER a drain,
     a harness feeder unit per tile driving the read-return hold, the read-queue clients and C&C's sends (`Net.at`,
     `rd`, `cncSends`), the director's arrival logs, the engines configured from the harness; `A2A`'s data.
   Checked on regs, wfi and a2a against golden-m2, and a small coreless run against a `ref` driver in `tests/ref`.
2. **The checker** (B19, B20): units record fixed-size facts (cycle, tile, check, arguments) in a list sized at startup
   (D24); formatted after the run into `ref`'s exact strings, in `Chip.checkInvariants`' scan order, `scanOverflows`
   every 16 cycles, `maxViolationMessages`, the per-check switches; the missing checks (disjoint buffers, S1, invariant 8
   strict, read-port overlap, overwritten command words, unexpected arrivals, ERRORS once per tile and bit). Chip reset
   (hard, soft): every port flushed, every unit to power-on, the memories kept. Checked by provoking each violation in
   C++ and in a Java driver in `tests/ref/`, the messages compared.
3. **`Config`** (H1, B25, H2): `MachConfig` with the fields some v1.3 driver, kit or sweep sets (D41 Q4), the rest
   listed as dropped; one `cfg.<field>=` front end; StateFp as every driver's fingerprint (D41 Q5); the H2 options.
4. **The outputs**: PhaseLog's CSVs (H3) byte-identical on AccountingTest (quick here, full on the runner); the TRACE
   store log (H6).
5. **The runner** (C0): `sim/run_tests.sh [all|quick]`, v1.3's step and log names, PENDING for suites not yet ported,
   the FAIL summary, `--check` against v1.3's logs with timing masked; **BusTest** (C2) its first suite, on the
   framework's port kinds (`throwsISE` as a death check), "BusTest: 154 passed, 0 failed".

## The owner's answers (2026-11-10)

- **Q1.** The checker is a compile-time switch: on in debug and in the suite build, off in the timing builds (D6).
- **Q2.** The PhaseLog reference is regenerated from `sim/ref` and committed as the fixture (finding F1).
- **Q3.** The reset flush and the violation list are framework additions; tell the owner if either changes an
  existing operation.
- New Java drivers go in `sim/tests/ref/` (W19 moves them to `sim/oracle/`).

## Findings

- **F1. `ref`'s committed `results/phases-a2a.csv` and `results/accounting.log` disagree**: the boot phase's GO is at
  cycle 98 in the CSV (cycles 930) and at 91 in the log (cycles 916). Today's `ref` puts the a2a boot's GO at 98
  (MachSelf: `runAt 98`), so the **log** is the stale one, not the CSV (corrected 2026-11-10; first written the other way
  round). Neither is used as a reference: the fixture is regenerated from `sim/ref`.

## Environment

- The container had no clang (D18 needs it): installed `clang lld` (clang 18.1.3; gcc 13.3.0); JDK 21.0.12.

## Progress

- **Step 0 done**: `tests/suite.h`, `suite.cpp`; `tests/test_suite.cpp` runs TSelf's sequence and compares with
  `tests/suite/tself.out`, written by `tests/ref/tself.sh --fixtures` from v1.3's own `T.java` (every method, passing
  and failing: 7 passed, 13 failed, exit 1). Equal on gcc and clang, debug and release. A suite links `tests/suite.o`
  (the Makefile's `test_suite` rule shows how). For W21: `throws`/`ub` run their function in a forked child.
- **Step 1, the header published**: `tests/mach.h` (MachConfig, Chip, TileRef, Cc, Mach, Net, A2A, `fields`), with
  `tests/test_mach.cpp` pinning `mach::fields` to the values ref's `Fields.java` prints. The body comes next.

- **Step 1, the machine as specified**: `tests/mach.cpp` (MachConfig, Chip, TileRef's memories and controller, Cc,
  Mach with `boot` and `boot_cores`, A2A's data and `verify`), `tests/mach.mk`. Checked by `tests/ref/MachSelf.java`
  (v1.3's own Mach, run in a scratch copy of ref/meshsim's layout with the committed images; `tests/ref/machself.sh`)
  against `test_mach`: regs, wfi (with a `run(n)` then `run_until`), a2a unshuffled and shuffled (seed 4), and the
  boot with C&C 1-3 as cores, line for line (`tests/mach/machself.out`), gcc and clang, debug and release. The owner's
  protected tests (test_chip, test_sort, test_t2_harness, test_cc_units, test_golden) unchanged, clang release and debug.

## What the body needs outside `sim/tests/` (additions only; the owner hears if any changes an existing operation)

- **Framework (done):** `Sim::wake_all()` and `Sim::in_flight()` (config.h, config.cpp, threads.h/.cpp; marked
  "W20 (harness-only)"). `wake_all` is ref's `wakeAll` at the start of `run`/`runUntil`; safe by the step-all rule: an
  unasked step with nothing delivered changes nothing); `in_flight` is the data-in-motion half of ref's `quiescent`.
- **Units** (no owner; optional parts, declared only on the harness's machines, nothing in the loop otherwise):
  - `add_g1`: a coreless tile (ref's `core.enabled = !coreless`: the core never executes) and the GATHER engine as a
    drain (`realResponder` off);
  - the injection unit: a harness input into the read-return holding register (`Net.at`) and harness-fed read-queue
    clients (`Net.rd`, `testReadClients`);
  - the director: an arrival log sized at configuration (`dir.log`; fatal when exceeded);
  - `TileCtl`: the harness's register writes (DMA_CONFIG, the record base, TILE_PULSE's start) applied in its level-0
    step, so the engines see them in the next cycle as ref's direct pokes do.
- **C&C's queue between cycles:** CncProgram's vectors reserved at build, steps appended between cycles behind a
  release point (cnc.h's mechanism), `call` and `at_all_ready` run from `Cnc::call_at` after the cycle they ran in.
- **Open for P5:** CnC's `waitUntil(predicate)` (a lambda evaluated inside C&C's step in ref) is not declared yet:
  CondTest and RecipeTest use it; its form is settled with the first suite that needs it.
