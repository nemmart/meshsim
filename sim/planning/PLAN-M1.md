# Milestone 1: the primitives and one RV32 controller, matching `ref` instruction for instruction

Agreed with the owner 2026-11-06 (integrator's draft, the owner's answers folded in; decisions in `DECISIONS.md`). The brief
is `sim/README.md` and `wip/planning/SIMULATOR-NEXT.md`; this file is the plan for the first milestone, with each worker's
acceptance tests. Workers are numbered from W1 again; their folders and reports are in `sessions/`.

## What M1 proves

The framework's primitives (structures, configuration, engine, port kinds, threads) and **one tile controller** built on
them run the same firmware as `sim/ref` (v1.3's Java simulator) and produce **the same retire trace, line for line, from
reset (the boot ROM included) to halt**, with the same halt reason, registers, CSRs, `mcycle` and data-memory digest.

**The programs** (D2, D3):
- the **48 riscv-tests programs** in `sim/ref/meshsim/test/riscv/bin` (rv32ui, rv32um; div, divu, rem, remu trap as illegal
  instructions to the boot ROM's handler), booted as `RiscvTest` boots them: the command lands, then GO, on tile 0 alone;
- `sw/tests`' single-tile programs: **`hello`**, **`faults`** (cases 0-7, one run per tile index; the 0x600D restart needs
  C&C 1 and waits), **`misalign_ld`** and **`misalign_st`** (both stop on the same instruction with the same UB named);
- **new small M1 programs** (W2) covering what `regs` and `wfi` would have: the trap CSRs, `mcycle`/`mtime`, and WFI woken by a
  command and by GO. `regs.c` and `wfi.c` move to M2: both send to their own tile through the router and use the write
  counters, GO and commands.

**What the controller sees of its tile** is reproduced by a **test-bench unit**: when a command lands, when GO arrives, and
when the request FIFO accepts or refuses a `REQUEST_SEND` store (the boot ROM handler's reports; no M1 program uses the router's
send holding register). Its cycles come from `ref`'s trace of the same
run, so nothing outside the controller has to be modelled in M1. **What the bench replays** (W2, 2026-11-07): the command
landing and GO; each word the traffic director writes into data memory (the command's slot-0 words and arguments: `MEMW`,
at the first cycle the core can see it); and the cycles the request FIFO frees an entry (`REQ-FREED`). The bench keeps the
FIFO's count from the core's `REQUEST_SEND` stores and those frees, so refusals, STATUS [6] (request busy) and EVENTS [11]
(request room) all follow; `REQ-REFUSED` stays in the trace as a check.

**The bar** (D8, measured by W2, Quest 123: ReplayBench, segment 0 of evm-zone-29, `sim-step-cost`): **36.8 µs per
chip-cycle** for one replay alone (one JVM on one P-core and its hyperthread), and **about 100,000-110,000 chip-cycles per
second** for the whole box (12 JVMs on the P-cores: 96,800; 16 on every CPU: 108,700). Two JVMs per P-core each run 3.4×
slower than one alone, more than hyperthreading explains: shared cache and memory, as the measurements in SIMULATOR-NEXT
section 3 predict.

## Components, in build order

1. **Build and discipline.** Plain `make` (`sim/Makefile`), gcc and clang, C++20, `-fno-exceptions -fno-rtti -Wall -Wextra
   -Werror`; a debug build with ASan and UBSan, a release build; `FATAL(fmt, ...)` (context to stderr with `fprintf`, then
   `std::abort()`); death tests (fork, expect the abort and its message); a hook that aborts on any allocation between
   `begin_run` and `end_run`, used by every later test.
2. **The cycle loop's structures**, each sized once at configuration and fatal when full: a ring of N-word slots (N fixed at
   configuration), a bitmap with find-first, an index pool over the thread's array of 128-bit values (free slots as a bitmap),
   append-only delivery buckets of fixed-width entries, a per-type arena for permanent state with big arrays allocated apart.
   Payloads are untyped `uint64_t` words throughout.
3. **Configuration.** A type registry (each unit type a small integer id); declarations (each unit's ports: kind, width in
   words, depth or latency); the sizing pass (adds them up, allocates every pool, bucket and value array per thread at its
   exact maximum); wiring (each port's route resolved once: the local bucket or an outbox, D9); levels from the Wires, a
   combinational loop rejected with its units named. Any C++ that makes this clear (STL, templates, lambdas).
4. **The engine.** The wheel of delivery buckets; ready masks set by delivery; run lists by level and type; as many phases as
   the units need, each defined by what it may read and write (no target number); wakes (none, next cycle, at cycle t) with
   catch-up of per-cycle counters, so a long sleep costs one wake; a unit type may have a **result-only form** beside its
   cycle-accurate one, chosen at configuration (D7). No STL, no allocation, no virtual calls in the loop.
5. **Port kinds, local form**, with `ref`'s exact bus semantics: `Link(L)`, `Fifo(N)` with its overflow count, credit `Fifo`,
   `Hold`, `Reg`, `Wire`, `Count`, and a minimal `RegFile`. Shared `Mem` ports wait for M2: in M1 the only outside writer of
   the controller's memory is the test bench's replay of the director's writes (`MEMW`), a registered input to the core, with
   no port contention to model.
6. **Debug checks.** Shuffle within a level by seed; wire discipline (read before driven, or driven twice, is fatal); the
   type-major and instance-major orders compared by digest.
7. **Cross-domain forms and threads.** The outbox merge at the barrier copies a 128-bit payload into the destination thread's
   array and rewrites its index; the barrier interval is the smallest cross-domain latency; a `Wire` across domains is fatal.
8. **The controller** (`RV32_core` and its tile surroundings): RV32I + Zmmul + WFI, the trap CSRs, `mcycle`/`mtime`; data and
   program memory, the boot ROM, the address decode with its bus errors and UB stops; `ref`'s timing (register loads 2 cycles
   sampling in the last, a taken branch +1, a multiply 3, posted stores, a send stalling while refused); the register-block
   subset (PARAMS, STATUS, EVENTS and its mask, PULSE_DONE, COMMAND, ERRORS and its interrupt, SIM_EXIT, TRACE, the table
   window's decode); the test-bench unit.

## Workers

| worker | does | starts | branch |
|---|---|---|---|
| **W1** | items 1-2, and a probe of the Quest runner | now | `sim-w1` |
| **W2** | the reference side: the observer, traces, the coupling audit, the M1 programs, the 16-JVM baseline | now, beside W1 | `sim-w2` |
| **W6** | the first benchmarks: two words against three for a link word; the Fp128 multiply's cost | after W1, beside W3 | `sim-w6` |
| **W3** | items 3-6, the unit author's API first | after W1 | `sim-w3` |
| **W4** | item 7 | after W3's engine | `sim-w4` |
| **W5** | item 8 | its core logic once W2's audit and W3's API are agreed; on the engine once W3 lands | `sim-w5` |

**W1, scaffolding and structures.**
- `make check` clean with gcc and clang, debug (ASan, UBSan) and release, warnings as errors, no exceptions, no RTTI.
- Each structure: unit tests, and a seeded differential test (10⁶ operations against an STL model) under the sanitizers.
- A death test for every capacity overflow, the message naming the structure; the allocation hook proven to fire.
- The runner probe (one Quest task): compilers and versions, cores, memory, Java, the riscv toolchain, `perf`, huge pages,
  and `make check` run there. From it, the decision on prebuilt firmware images (D4).

**W2, the reference side** (Java and Quest).
- **The observer** (D5): one opt-in class in `sim/ref`, off by default, logging each retired instruction (cycle, pc, the
  instruction word, rd and its value, any memory operation) and the test-bench events (command landed, GO, a request refused). **Proof it changes nothing:** with it present and off, `run_tests.sh` quick, `RiscvTest` and `CtrlTest` give logs
  identical to the pristine copy; on, only the trace files differ. Recorded in `sim/ref/README.md` as the one permitted change.
- **The trace format** (`sim/tests/TRACE-FORMAT.md`): what a line's cycle means for multi-cycle instructions, stalls, WFI,
  traps and halts.
- **The coupling audit**: every interaction between the Controller and the rest of its tile in the M1 programs (registers
  read and written, the WFI wake term, the interrupt term, the request FIFO, the command and GO), with the cycle each
  takes effect and every same-cycle query. It is W5's specification of the register-block subset and the test bench.
- **The M1 programs** (`sim/fw/`): CSRs, `mcycle`/`mtime`, WFI woken by a command and by GO; sources, build script and images
  committed; each run in `ref` with a driver that lands the commands and GO where the program expects them.
- **Golden traces** (`sim/tests/golden/`) for every M1 program, each run on the traced tile alone. Each starts with a **header
  recording its configuration** (D12): the latencies (GO and READY 8, the register load, the rest of `Config`'s relevant
  values), `stage_delay`, the shuffle seed, the tile, and the image's sha256.
- **The baseline** (D8): ReplayBench (on `sim-step-cost`) as 1 JVM and as 16 JVMs at once on the runner: µs per chip-cycle per
  JVM and chip-cycles per second in aggregate. The bar the framework is judged against.

**W6, the first benchmarks** (D6): on a hand-rolled loop over W1's structures, before W3's API is agreed.
- A synthetic busy 8 × 8 mesh of simple XY routers with FIFOs (not exact to `ref`), link words as (a) two words, the header and
  the data or a 128-bit index into the thread's value array, against (b) three words inline; µs per mesh-cycle on one mesh and
  one thread, then 128 meshes on 1, 6 and 12 threads pinned to the
  performance cores (D13), the efficiency cores reported separately.
- The cost of an Fp128 multiply (a Crandall prime, reduced by folding the high half, not Montgomery: D14), per operation
  and in a unit-like loop.
- A recommendation for the link word and for how units do 128-bit arithmetic, **delivered to W3 before its API header is
  agreed**.

**W3, configuration, engine and port kinds.**
- **First, the unit author's API header**, with a sketch of two units written against it (a FIFO consumer and the
  controller's outline), reviewed by the integrator, **then by the owner** (D10), before the implementation proceeds.
- **Port conformance:** for each kind, one seeded script of puts, takes and peeks drives `ref`'s bus classes (a Java driver in
  `sim/tests/`, compiled into `ref`'s default package, D5) and the C++ port; the per-cycle observations identical over at
  least 10⁵ cycles at several depths and latencies.
- Sizes computed equal to hand-derived ones on small graphs; overflow fatal; loops rejected with names.
- Digests stable across shuffle seeds and both evaluation orders; wire-discipline violations fatal (death tests).
- An idle unit is never stepped (counted); a 10⁹-cycle sleep costs one wake; no allocation in the loop; a toy unit's two forms
  agree.

**W4, threads.**
- Cross-domain forms of every port kind, chosen at configuration; the barrier interval from the partition.
- A seeded synthetic graph on 1, 2, 4 and 8 threads, over several partitions: identical digests; the 128-bit copy across
  threads checked; no value array shared (asserted in debug).
- TSan (gcc and clang), ASan and UBSan clean; the scaling of N independent instances on N threads measured, threads pinned
  to the performance cores (D13), the efficiency cores reported separately.

**W5, the controller.**
- The test bench checks its own configuration against the golden trace's header before comparing, and refuses on any
  difference, naming it (D12).
- Every M1 program's trace equal to its golden trace, line for line, from reset to halt; halt reason, registers, CSRs,
  `mcycle` and the data-memory digest equal.
- Shuffle and order checks pass; sanitizers clean.
- N controllers on N threads, running different programs, each still matching.
- Reported: lines of code against `Controller.java` (444) and the subset of `TileControl` it replaces; instructions per
  second against `ref`.

## Carried to M2

- `regs.c` and `wfi.c` (D2): they send through the router and use the write counters, GO and commands.
- **The external interrupt from ordinary code, and a pending interrupt waiting for a multiply or branch stall** (W2,
  2026-11-07): the only controller-local error source that doesn't trap first is "done early", which needs the write
  counters. M1 covers the interrupt only inside a handler that sets MIE again (`irq`); W5 still implements `ref`'s rule in
  full, and M2 tests these paths once the counters exist.

## Layout

`sim/Makefile`; `sim/src/` (the framework, then `units/`); `sim/tests/` (C++ tests, the Java drivers, `golden/`,
`TRACE-FORMAT.md`); `sim/fw/` (the M1 programs and their images); `sim/planning/` (this plan, `DECISIONS.md`, `sessions/`).
Workers may refine the layout under `src/` and `tests/` in their plans.

## Status

| worker | state |
|---|---|
| W1 | **done**, merged 2026-11-07 (`sessions/WORKER1-REPORT.md`); D4: prebuilt firmware images, the C++ shipped as source |
| W2 | **done**, merged 2026-11-07 (`sessions/WORKER2-REPORT.md`); the audit is `sim/tests/AUDIT-M1.md`; 67 goldens, `regen.sh --check` all SAME |
| W6 | **done**, merged 2026-11-07 (`sessions/WORKER6-REPORT.md`); its recommendations go to the owner with W3's API |
| W3 | stage 1 (the API) done, reviewed by the integrator (D19) and merged 2026-11-07; stage 2 waits for the owner's go-ahead |
| W4, W5 | not started |
