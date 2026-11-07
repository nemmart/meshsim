# WORKER5 notes: the RV32 controller on the framework

## The plan (for the go-ahead)

### What reading the oracle changed in the sketch and the spec

1. **Table-window loads take 1 cycle, not 2.** `Controller` waits a cycle only for `Registers.inRegs(ea)`, i.e. `ea >=
   0xFFFFF800` (the block and the peripheral windows); the table window (`0xFFFF2000-0xFFFF35FF`) is below it. AUDIT 6 and
   TRACE-FORMAT's table say 2. The golden agrees with the code (`faults-t6`: `lui` at 97, the PTR load's `TRAP` at 98). Not
   a golden error, a doc error: I follow the code; the two docs want a one-line fix (integrator's).
2. **A block or window load waits first, then checks.** `ref` takes the wait cycle for any `ea >= 0xFFFFF800` before the
   `f3` check (illegal), the alignment check (UB) and the decode (bus error). So a sub-word or reserved-`f3` load there
   costs its wait cycle before it traps. Sketch 2's `load_reg` bus-errors without waiting: I follow `ref`.
3. **The `REQUEST_SEND` refusal** is checked before the `f3 > 2` and alignment checks, and only for `sw` at that word.
4. **`violate` only counts in `ref`** (reserved words, an offset past 32 KB that aliases, a store into data memory's first
   16 bytes, reserved bits in a stored value, mret): the C++ counts them the same way and carries on; none is fatal.
5. **An error in a handler that halts (HALT 8) still raises ERRORS**: `busErrors++`/`illegals++` come before `trap`'s
   stop. So the core posts the error on `bus` in that case too. One `bus` put a cycle is enough: a faulting access has no
   other effect.
6. **`start_skew`** is `java.util.Random(1).nextInt(2)` per tile (`Chip`, `skewSeed` 1): the harness reproduces it (10
   lines) so the D12 check compares it like any key.
7. **Outside M1, fatal "not modelled in M1: <what>"**: ROUTER_HDR/ADDR/SET_W0-W2/SEND_*, the write counters, DMA_*,
   PULSE_DMA, SRAM_RD_*, REQUEST_GET_W0-W2 and REQUEST_RECEIVE (loads), peripheral 1's window (present, type 4), HIST loads
   and stores, stores to PTR/STREAM/GROUP. **Implemented exactly, being local and cheap**: reserved words (read 0, stores
   ignored, counted), the absent windows 2-3 (bus error), loads from PTR/STREAM/GROUP (bus error), EVENTS_REQUIRED (it is
   in the wake term), TRACE (counted), `mret`.

### The design: three layers

**(a) The logic, as plain code** (no framework):
- `Rv32`: `Controller.runCycle` and `execute` line for line, templated on its **environment**: the register view, the
  wake term, the interrupt term, `canPush`, the bus to the block, the trace sink. Two environments: a plain struct (unit
  tests, the direct driver) and the unit's adapter over `Ctx`. Both inline; two instantiations of genuinely different
  code, nothing templated on payloads (D16).
- `RegBlockState`: TileControl's M1 subset and RequestPort's count; `end_cycle(go, store, landed, freed, error,
  consume)` in `endCycle`'s order, then the view (STATUS, EVENTS, ERRORS, the wake and interrupt terms, request busy).
- The register map and decode (`Registers.java`'s subset: `loadAt`, `storeAt`, regions, windows, tables).

**(b) The three units** of sketch 2 on `sim.h`, thin wrappers of (a), with sketch 2's ports, wires and levels (bench and
block at level 0, core at level 1). The bench replays MEMW (a cycle's consecutive words as one put), GO, CMD and REQ-FREED,
nothing else.

**(c) The harness** (`sim/tests/`): parse a golden (header, replayed events, body, closing block); load the images and
`bootrom.hex`, checking their sha256 against the header; **refuse on any D12 difference, naming the key and both
values**; run; write the trace; compare line for line, reporting the first difference with 5 lines of context each side.

**Two drivers, the same trace.** A **direct driver** (~60 lines, no framework): per cycle, the bench's events, the core's
`run_cycle`, the block's `end_cycle`, in `ref`'s order. It matches all 67 goldens **before W3's engine exists**, and
stays as a second oracle: when the engine run differs, the direct run tells a unit bug from an engine bug. The **engine
driver** builds the three units with `Config` and runs them; its trace and closing block must equal both.

**The trace in the loop** (D6): a `TraceLog`, a fixed array of small records (cycle, rank, kind, up to 4 words) allocated
at configuration from the golden's line count plus a margin; full stops recording and is reported as a difference, never
grows. Formatting happens after the run. Order within a cycle by rank (MEMW, GO, CMD, the core's lines, HALT, REQ-FREED),
stable, as TRACE-FORMAT orders the file.

**The end of a run**: the harness runs to the golden's END cycle. The core records how it stopped (halt R, parked, UB) and
the cycle; a halted core stays asleep, its `mcycle` caught up from its last step (so step-all, which steps it anyway, gives
the same closing block). Not stopped by then: `END timeout`, a difference. No API change needed.

### Files

| file | what |
|---|---|
| `src/units/regmap.h` | the register map, the address decode |
| `src/units/rv32.h` | the core (templated on the environment), CSRs, memories, boot ROM, traps, timing |
| `src/units/regblock.h` | `RegBlockState` |
| `src/units/trace_log.h`, `.cpp` | the records; formatting (configuration regime) |
| `src/units/controller.h`, `.cpp` | `Core`, `RegBlock`, `TestBench`; `configure_tile()` |
| `tests/golden_run.h`, `.cpp` | parse, D12 check, load, the two drivers, compare |
| `tests/sha256.h`, `.cpp`; `tests/java_random.h` | the digests; the skew |
| `tests/test_rv32.cpp`, `test_regblock.cpp` | unit tests on (a) |
| `tests/test_golden.cpp` | all 67 in `make check` (direct now; engine, seeds, orders, step-all once W3 lands) |
| `tests/golden_main.cpp` | the `golden` command: `golden [--direct\|--engine] [--seed N] [--order instance] [--step-all] [names]` |

`sim/Makefile`: two small additions (the `src/units` objects in the test links; `make golden`). It is shared with W3's
stage 2, so I'll keep the diff minimal and merge `main` before touching it.

### Order of work, and how the core is tested before the engine

1. The register map, sha256, Java's Random; the golden parser, D12 check and image loader, with tests.
2. `Rv32` and its unit tests against a scripted environment: every instruction class (sign and zero extension, shifts,
   the three `mulh` against 64-bit arithmetic, `div`/`rem` illegal), the CSR rules (`mtime` write illegal, `csrr` not a
   write, `misa`, `mip`, MPP on read, `mepc`/`mtvec` low bits), traps (`mepc`, `mtval`, `mcause`, MPIE), the halts (2, 3,
   5, 6, 7, 8), parked, UB; the timing (branch +1, multiply 3, block load 2 with an interrupt preempting its first cycle,
   a refused `REQUEST_SEND` re-executed, the interrupt held off by `stall` but not in WFI); items 1-3 above.
3. `RegBlockState` and its tests (`endCycle`'s order: GO, store, landed; a consume beats a same-cycle set; the gate;
   request room on busy's fall; STATUS for t + 1).
4. The direct driver, `TraceLog`, compare: **all 67 matching**; `make golden`. Commit and push.
5. The three units on the API (they compile against `sim.h` now, as the sketches do).
6. W3's engine lands: merge `main`; all 67 on the engine; then shuffle seeds, both orders, step-all (its digest equal to the
   ready-driven run's) and the missed-wake detector; debug and release, gcc and clang.
7. W4's threads: N controllers on N threads, different programs, each matching.
8. The report: lines against `Controller.java` (444) and the `TileControl`/`RequestPort` parts replaced; instructions per
   second against `ref`, on the runner (clang release, pinned to a P-core), the number claimed in `NUMBERS.md` first.

The 67 goldens are at most ~1,000 lines and a few thousand cycles each, so all of them run in `make check` (every build).

### For the integrator

- Item 1's doc fix (AUDIT 6, TRACE-FORMAT): table-window loads are 1 cycle.
- Item 7's list of what is fatal and what is exact: OK?
- No API change needed so far.

## The go-ahead (owner, 2026-11-07)

Approved as planned, with: the doc fix for table-window loads (done on main, merged); follow `ref` for the block load's
order; the fatal/exact split as listed, each fatal message naming the register or window and saying "not modelled in M1"
(so M2 can grep for them); the direct driver kept small, a regression check once the engine path matches; my make rules in
`src/units/units.mk`, one include line in `sim/Makefile`.

## Progress

**Step 1-4 done (commit "W5: the controller's logic ...").** `src/units/`: `regmap.h` (the address map and register
table), `rv32.h` (the core), `regblock.h` (the block's subset), `trace_log.h/.cpp`, `controller.h/.cpp` (the three units,
compiled in every build, linked once the engine lands). `tests/`: `golden_run.h/.cpp` (the harness and the direct
driver, ~60 lines of loop, run under the allocation hook), `golden_main.cpp` (`make golden`), `sha256`, `java_random.h`,
`test_rv32.cpp` (34), `test_regblock.cpp` (7), `test_golden.cpp` (6).

- **All 67 goldens match with the direct driver**, the whole file: our own header (sha256 of the images and the boot ROM,
  `data_at` and `entry` from the `.sym` as TraceRun takes them, the skew from `java.util.Random(1)`, the schedule's steps),
  the body, and the closing block. 0.03 s for all 67 (release).
- The D12 check refuses with the key and both values (`refused (D12): config mul_latency: golden 4, ours 3`), or the
  header line otherwise (image, tile, boot ROM). Tested, as is a core that differs (a 4-cycle multiply) being caught.
- `make check` clean: gcc and clang, debug (ASan, UBSan) and release.
- Digests: each unit defines `digest()` (sim.h: a unit holding pointers must). The core folds the counters `catch_up`
  advances in relative to `last` (`mcycle - last`, stall and WFI cycles `- last`, the stall's end cycle), so a core that
  slept and one stepped every cycle digest the same (tested).

**Waiting for W3**: increment 2 (on `sim-w3`, not on `main`) has Link, Fifo and Hold; the controller needs `Wire` and
`RegFile` (increment 3). A trial merge of `sim-w3` into this branch (scratch worktree, not pushed) merges cleanly, the
Makefile included.

### For W3 (no change asked)

- **Catch-up counters and the step-all digest.** A unit that counts cycles lazily (`mcycle`) differs between the two
  runs until it next steps, so its bytes can't be compared; `digest()` has to fold those counters relative to the cycle it
  last ran. Worth a sentence in sim.h's step-all paragraph: "counts the same either way" holds only after catch-up.
- Struct padding: a unit digested by its bytes can differ in its padding between two machines; my units define
  `digest()` anyway, but the default might want a note.

## The engine path (after W3's increments 1-3 merged into main, 2026-11-07)

- **All 67 goldens match on the engine**, ready-driven, the whole file, with the end-of-run sleepers report empty and the
  run under the allocation hook (`golden --engine`; `test_golden: golden_all_match_engine` in `make check`, every build).
  Nothing in my units needed changing for the engine except RegBlock's `digest()`, which W3's new static_assert caught
  missing (a struct with padding).
- The harness also runs shuffle seeds 1-3, instance-major and step-all, each trace against the golden and each digest
  against the ready-driven run's; where the engine's trace differs from the golden it compares the direct driver's too
  and says which side is at fault.

### For the owner: the debug-check options are accepted and ignored (W3's increment 5)

`Options::shuffle_seed`, `order` and `step_all` are declared in config.h and documented in sim.h's DEBUG CHECKS, but
nothing in `src/*.cpp` reads them yet (W3's NOTES schedules them for increment 5, with the missed-wake detector). They
are silently no-ops: all six option sets give identical step counts and digests. **Smallest case:** `hello` with
`Options{.step_all = true}` steps the block 5 times in 104 cycles (core 78, bench 4), exactly as ready-driven. And a unit
deliberately broken for step-all (the block counting its own steps in its digest) passes all six. So the debug checks
can't be claimed clean yet. Not worked around: the harness probes for it (`engine_has_debug_checks()`: step-all on
`hello` must step the block every cycle), runs ready-driven only while it is absent, and enforces all six the moment it
lands; `make check` prints "pending" meanwhile. Suggestion for W3: until a check exists, a non-default option could be
fatal ("not implemented yet") rather than ignored.

### For W3 (minor)

- `Sim::run_until(t)` stops **before** cycle t (the engine's `now` is the next cycle to run), so after it `now() == t`
  and cycle t hasn't run; `run(n)` from a fresh machine runs cycles 1..n. Natural once known, but sim.h/config.h don't
  say; my first engine run stopped one cycle short of every halt. I use `run(END)`.

### Line counts (code lines: not blank, not comment-only; total in brackets)

| C++ (src/units) | | ref | |
|---|---|---|---|
| `rv32.h` (the core, its catch-up and digest, the block's load/store decode) | 406 (466) | `Controller.java` | 376 (449) |
| `regblock.h` (endCycle's M1 subset, the view, the request count) | 84 (107) | `TileControl.java`'s M1 parts (fields, reset, eventsAt, wakeTerm, takeWake, read/write's block decode, endCycle, apply's M1 branches, conditions, status, params): ~165 of its 300; `RequestPort`'s count: ~10 of 29 | |
| `regmap.h` (the address map, the register table) | 67 (82) | `Registers.java`'s map and block table | |
| `controller.h/.cpp` (the three units, their ports and wiring) | 149 (190) | (`Tile`'s wiring of the same) | |
| `trace_log.h/.cpp` (records, formatting) | 107 (129) | `Observer.java` | |

### First local speed figure (this sandbox, 1 vCPU VM, clang release, not pinned: the runner measurement follows)

`golden --bench 300`: every golden run 300 times on the engine, ready-driven, the run alone timed (not the build):

| | instructions/s | cycles/s | per unit step |
|---|---|---|---|
| traced (the core writes its trace) | 21-22 M | 26-28 M | 41-44 ns |
| no trace | 23 M | 29 M | 39-40 ns |

The programs are short (100-1,000 cycles each), so this includes cold-start effects; ~40 ns a step for three small units
looks high for the engine against W6's measurements, worth a `perf` look on the runner.

## Speed on the runner (Quest 127, sim-w5 8738cfd; i5-14400F, CPU 0 alone (P-core, sibling 1 idle), clang 18 -O2)

The program (`tests/bench_controller.cpp`): booted as `hello` (boot ROM, replayed command and GO), then a loop of 9
instructions in 13 cycles (data load and store, three ALU, a multiply, a register-block load, the count, a taken branch),
10^7 iterations, then SIM_EXIT. Checked first: at 10^3 iterations the engine's trace equals the direct driver's; every run
halts by SIM_EXIT with the count in memory. Runs alone timed (not the build); 3 repetitions each, spread under 1%.

| run | M instructions/s | M cycles/s | ns per unit step | x86 instructions per step (perf stat) | IPC |
|---|---|---|---|---|---|
| engine, untraced, 9·10^7 instructions | **43.6** | 63.0 | **20.6** | ~535 | 5.5 |
| engine, traced (10^6 iterations) | 42.0 | 60.6 | 21.4 | — | — |
| the plain core alone (no engine, no block; constant environment) | **156** | — | **5.8** | ~141 | 5.2 |
| the 67 goldens, 300 times each (short runs, cold start) | 37.4 | 46.6 | 24.6 | — | — |

Steps: the core is stepped 10^8 times in 1.3·10^8 cycles (it sleeps through multiply and branch stalls); the block 6 and
the bench 4 times in the whole run, so "per step" is per core step. The trace costs ~0.8 ns a step in the loop (its
formatting after the run dominates a traced run's profile, but that is outside the timed run). Footprint of the tile's
engine structures (`Sizes`): 484 words, 3 wheels, 3 runs, 5 links, 4 wires, 1 register file.

### Where the 20.6 ns per step goes (perf record, UNITS_PROFILE build: the core's step and interpreter out of line)

| part | symbols | share | ≈ ns/step |
|---|---|---|---|
| **the engine** | `Engine::cycle` 33.1%, `run_form<Core>` 14.9% (dispatch, the inlined Ctx reads), `Engine::after` 7.0%, `run_until` 5.1%, `heap_set` 1.6%, `heap_remove` 1.2%, memset 0.5% | **~63%** | **~13** |
| **my units** | `Rv32::execute` 12.7%, `Rv32::step` 10.7% (catch-up, interrupt and WFI checks), `Core::step` 5.7%, `fetch_execute` 3.3%, load/store/read_reg 1.7% | **~34%** | **~7** |
| the trace | in the loop: ~4% (traced against untraced); untraced runs have none | | ~0.8 |
| unresolved | ~2% | | |

The plain build (no UNITS_PROFILE) gives the same split within a point or two (clang keeps `execute` out of line anyway).

**For W3 (the engine's part):** with one ready unit, ~390 x86 instructions a cycle are the engine's (535 per step, of
which the core alone accounts for ~141), at IPC 5.5: cheap instructions, but many. `Engine::cycle` alone is a third of the
time with only 3 wheels and 3 runs, so it is fixed per-cycle work (the wheel scan, the far-wake check, the run-set
swaps, `idle()`), not scaling with ports. The far wakes cost ~3% (a multiply and a taken branch each iteration go
through the heap; a stall of 1-2 cycles might not want the heap). Profiles: Quest `results/127-w5-controller-speed/
perf-report-{plain,noinline,traced,core}.txt`.

**For me (the units' part, ~7 ns):** the plain core alone runs ~141 x86 instructions per RV32 instruction, which is
more than an interpreter needs; `Rv32::step` is a third of it (the catch-up and the checks before every instruction).
Possible later, keeping exactness: a fast path when nothing can interrupt (no catch-up gap, not in WFI, no stall), and
the statistics counters out of the hot path. Not done: correctness first, and the engine is the larger part.

## M1's threads test: N controllers on N threads (W4's run_independent; Quest 129, sim-w5 8e19528)

`check_parallel` (tests/golden_run.cpp): the 67 goldens in a mixed order (k·7 mod 67, so rounds mix riscv-tests and the M1
programs), in rounds of N; each round N controllers on N threads through `run_independent`, each a different golden. Each
chip's tile (memories, trace) and machine are built, run and compared on its own thread; only read-only data (goldens,
images, headers) is shared, prepared before. Every trace must equal its golden, every digest the same golden's run alone,
and no unit may be left asleep with data waiting. In `make check`: `test_controllers` at N = 3 (debug and release, gcc
and clang), and under TSan (`make tsan-units`, gcc and clang, included in `make check`). CLI: `golden --threads N --cpus
a,b,...`; `bench_controller --threads T --cpus ...`.

- Locally (1 vCPU): N = 2, 3, 4: all 67 match; TSan clean on gcc and clang.
- **Runner (Quest 129): all 67 match at N = 1, 2, 4, 6, 8, 12 pinned to the P-cores and 16 unpinned, 3 repetitions
  each; TSan clean on gcc and clang.**

Throughput (T controllers, each running the 13-cycle loop, 10^7 iterations; 3 repetitions):

| threads | CPUs | M instructions/s in all | per thread | ns per step | vs 1 thread |
|---|---|---|---|---|---|
| 1 | 0 | 43.0 | 43.0 | 20.9 | 1.0x |
| 6 | 0,2,4,6,8,10 (one per P-core) | 221-224 | 37.2 | 24.1 | 5.2x |
| 12 | 0-11 (P-cores and their hyperthreads) | 211-239 | 18.6 | 48 | 5.2x (±0.4) |
| 4 | 12-15 (E-cores) | 75.0 | 18.8 | 47.9 | — |

Six P-cores give 5.2x (each thread 14% slower than one alone: the all-core clock is lower than the single-core boost; the
working set per controller is ~70 KB, so not memory). Hyperthreads add nothing: at IPC 5.5 one thread already fills the
core. So the box's best for this load is about 300 M instructions/s (P-cores plus E-cores).

## The machines (D21: every speed result records its machine)

**The runner** (Quest 127, 129, 130, 131: the same box; recorded by Quest 131's `machine.txt`, the CPU list by Quest 122):
Intel Core i5-14400F, 1 socket, 16 CPUs: 6 P cores (CPUs 0-11, two threads each, max 4.7 GHz, min 0.8 GHz) and 4 E cores
(CPUs 12-15, max 3.5 GHz); governor powersave; L1d 416 KiB (10), L1i 448 KiB (10), L2 9.5 MiB (7), L3 20 MiB; 64 GB
(65,671,456 kB); Linux 6.8.0-147-generic; clang 18.1.3 (Ubuntu 18.1.3-1ubuntu1), g++ 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1),
OpenJDK 21.0.12.1. Builds: clang -O2 -DNDEBUG (D18) unless stated. The tables above for Quest 127 and 129 were measured
on this machine.

**This sandbox** (the "first local figure" and local runs): Intel Xeon @ 2.10 GHz (a VM), 1 vCPU, L2 2 MiB, L3 260 MiB,
4 GB; Linux 6.18.44; clang 18.1.3, g++ 13.3.0, OpenJDK 21.0.12.1.

From Quest 131 on, my tasks take the CPU lists as arguments (`$1` P cores, `$2` E cores) or detect them
(`planning/sessions/WORKER5/tasks/machine.inc`: `/sys/devices/cpu_core/cpus`, `cpu_atom/cpus`, thread siblings), and
record the machine first; W4's `sim/tools/machine.sh` replaces machine.inc once it is on main. Quest 127, 129 and 130
(before D21) used CPU 0 and the lists `0,2,4,6,8,10`, `0-11`, `12-15` written in.

## The debug checks real (W3's increment 5), and M1's acceptance under them (Quest 131)

**Machine:** the runner, as recorded above (Quest 131).

- All six option sets (ready-driven, shuffle seeds 1-3, instance-major, step-all) are enforced everywhere: `golden
  --engine`, `test_golden`, and the threads test (`check_parallel`: every trace equal to its golden, every digest equal
  to the same golden's run alone ready-driven, step-all checked to be in effect). The "pending" probe is gone.
- **Quest 131, all PASS:** 67 goldens × 6 option sets on the engine; N controllers on N threads under the six option sets
  at N = 1, 2, 4, 6 (one per P core), 12 (every P-core thread) pinned and 16 unpinned, with the CPU lists detected;
  `make check` (gcc and clang, debug and release, TSan).
- **The checks bite** (`tests/test_broken_units.cpp`, in `make check`): a register block that counts its own steps
  (invisible to the trace) gives a step-all digest unlike the ready-driven one (6b30c796fa65afea against
  e285d089a19551ba), the healthy block equal ones; a block that leaves the core's bus word untaken on even cycles gives
  unequal digests, and the missed-wake detector stops it: `missed wake at cycle 12: t.ctl is asleep with data waiting in
  t.ctl.bus, and nothing on its way will step it (...)`.

### For the owner and W3: the missed-wake report allocates inside the run

The detector formats its report with `std::string` in the cycle loop, so under W1's allocation hook (`sim::begin_run()`,
which my harness puts around every run, D6) the hook fires first: `FATAL src/base/alloc_guard.cpp:68: operator new of 37
bytes inside a run`, in both modes (fatal, and report-and-go-on). Smallest case: the LazyBlock tile on `hello`, debug
build, run inside begin_run/end_run. Not worked around in the units: only the one death test that shows the detector's
message runs outside the hook (`run_built(..., alloc_guard = false)`, with a comment); it goes back under the hook when W3's
fix (a fixed buffer, no allocation in either mode) lands. *(Owner, 2026-11-07: with W3 now.)*

## The framework against ref (Quest 130, sim-w5 a228eee)

**Machine:** the runner, as recorded above; CPU 0 (a P core), `taskset`; clang 18.1.3 -O2 for the framework; OpenJDK
21.0.12.1, `java -Xmx1g`, for ref.

**Like for like.** The same program: bench_controller's loop (9 instructions in 13 cycles; then SIM_EXIT), written by
`bench_controller --write` and booted in ref exactly as TraceRun boots a golden (`tests/ref/SpeedRun.java`: the image
poked in, C&C 1 expecting tile 0 alone, the boot command with entry 0x01000000, GO; observer off). At N = 1000 ref's trace
(TraceRun, observer on) equals the framework's, body and closing block, 9,038 lines; at every N both report the same
instructions and cycles (N = 10^6: 9,000,028 and 13,000,048). Timed: Chip.runUntil alone in ref (a fresh Chip per run, in
one JVM; run 1 warms the JIT), Sim::run alone in the framework.

| | N | runs | M instructions/s | M cycles/s | per chip cycle | framework ahead |
|---|---|---|---|---|---|---|
| **framework** (engine, untraced) | 10^4, 10^6, 10^7 | 3 each | **43.6-44.0** | 63.5 | 15.7 ns | — |
| **ref, idle-skip on** (the default) | 10^6 | 4 (2-4: 0.75, 0.76, 0.77) | **0.76** | 1.10 | 0.91 µs | **58x** |
| **ref, idle-skip off** (`-Dmeshsim.skip=false`) | 10^4 | 4 (2-4: 0.019 each) | **0.019** | 0.028 | 36 µs | **2,300x** |

Ref's run 1: 0.71 (skip on), 0.018 (off). Skip off's 36 µs per chip cycle agrees with D15's 36.8 µs per chip-cycle.

**Where it isn't like for like.** Ref steps a whole chip: 64 tiles with every unit of each (router, director, read port,
engines, SRAMs, the peripheral slot), C&C, the request bus, its invariant and overflow checks. With idle-skip on the 63
other tiles are skipped but the chip-level work is not; with it off every unit of every tile runs every cycle. M1's
framework run has the one tile's three units and nothing else (M1's scope: the rest is replayed by the bench or absent).
So 58x and 2,300x mostly measure chip-level work the framework doesn't do yet. **Unit for unit:** ref's Controller and
TileControl together take 7.95% of its idle-skip-on time (below), 72 ns per chip cycle = ~104 ns per instruction; the
framework's whole run (the core, the block, the bench and the engine) is ~23 ns per instruction: **~4.5x**. The core
alone (no engine, `bench_controller --only core`, Quest 127) is 6.4 ns per instruction against ref's Controller at ~30
ns: ~4.7x.

### Ref's profile (JFR, settings=profile, folded by WORKER28/29's JfrFold; Quest 130)

Idle-skip on, N = 10^6, 2 runs (2,098 samples, 98.9% inside Chip.runCycle). By unit (the innermost runCycle/endCycle):

| unit | share | ≈ ns per chip cycle |
|---|---|---|
| RequestBus (`runCycle`, every cycle, all 64 leaves) | 33.3% | 303 |
| Chip.scanOverflows | 19.5% | 177 |
| Chip's own loop (`runCycle`) | 17.0% | 155 |
| Chip.sleepCheck (`Tile.canSleep`) | 7.0% | 64 |
| TileControl.endCycle | 5.6% | 51 |
| Chip.checkInvariants | 5.0% | 45 |
| Cnc1Peripheral | 2.5% | 23 |
| **Controller** | **2.3%** | **21** |
| SRAMReadQueue | 2.2% | 20 |
| Replayer | 1.8% | 17 |
| the rest (CnC, BigSRAM, Router, TableStore, ...) | 3.8% | 35 |

By top frame: RequestBus.runCycle 33.3%, Chip.scanOverflows 19.5%, Chip.runCycle 11.0%, Tile.canSleep 7.0%,
Replayer.errors 6.0%, TileControl.endCycle 5.4%, Chip.checkInvariants 5.0%, BusBase.enterSlow 3.4%, Controller.execute
0.8%, Controller.step 0.7%.

Idle-skip off, N = 10^4, 2 runs (864 samples, 99.4% inside Chip.runCycle). By unit: TileControl.endCycle 21.8% (64 tiles),
SRAMReadQueue 19.9%, Router 9.4%, TableStore 6.5%, Replayer 5.0%, Controller 4.1%, Tile 4.0%, Chip's loop 3.8%,
Chip.checkInvariants 3.8%, WriteEngine 3.1%, AddressGenerator 2.8%, the rest ≤ 2.4% each. By top frame's class: BusBase
36.6% (the bus classes' settle on every touch), TileControl 22.2%, Router 9.4%, Replayer 7.2%, PeripheralSlot 6.1%.

### The framework's profile beside it (perf, Quest 127, the same loop; the table in "Speed on the runner" above)

| | framework (20.6 ns per core step, 15.7 ns per chip cycle) | ref, idle-skip on (910 ns per chip cycle) |
|---|---|---|
| the controller core | ~34% (~7 ns per step: execute, step, Core::step, fetch, load/store) | 2.3% (21 ns) |
| its register block | (in the units' 34%: end_cycle runs 6 times in the run) | TileControl.endCycle 5.6% (51 ns) |
| the engine or the chip loop | ~63% (~13 ns: Engine::cycle, dispatch, after, run_until, the wake heap) | Chip's loop, scans and checks 49% (446 ns) |
| the rest of the chip | — (not modelled in M1) | RequestBus 33%, the other units ~8% |
