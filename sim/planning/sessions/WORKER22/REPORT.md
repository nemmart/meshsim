# WORKER22 report: P4, the network, memory and engine suites (inventory C3-C7, C11, C17, G1)

**DRAFT, 2026-11-12: work in progress.** Suites still waiting on harness and checker work are marked; this report is
completed as they land. Branch `sim-w22`; merged into main as it went.

v1.3's suites in my rows, ported onto W20's harness (`tests/mach.h`, `tests/suite.h`), each checked against v1.3's log
of the same `run_tests.sh` step, regenerated from `sim/ref` (not `ref/results`, stale for every one of these steps).

## 1. Where each step stands

| row | suite | steps | state |
|---|---|---|---|
| C3 | NetTest | net, net-shuf, net-shuf2, skewtable | **byte for byte** (net in both modes); live; quick three in make check; skewtable 40 on the runner (Quest 179) |
| C5 | EngineTest | engine, eng-shuf, engine-full | **byte for byte** (365 of 365; engine-full 92 s); live; engine and eng-shuf in make check |
| C11 | SearchBench | searchbench | **byte for byte**, 56 of 56; live; in make check |
| G1 | AgBench | agbench | **byte for byte** (145 lines); live (all mode) |
| C17 | RequestBusTest | reqbus, reqbus-shuf | **byte for byte** (50 of 50); live; both in make check |
| C7 | ReadPortGatherTest | rdport, rdport-shuf, rdport-full | every printed line equal; 586 of 587: the legal run of the disjoint group waits on F6 (W26) |
| C4 | MemTest | mem, mem-shuf, mem-shuf2 | every printed line equal; 170 of 171: the disjoint group's count of every offending access (W26) |
| C6 | AgTest | agtest, agtest-shuf | 206 of 206 ported (no printed lines); figure 12 (34) waits on W22-16 |

The steps not yet equal stay PENDING in run_tests.sh with what each waits on; each goes live (and into make check where
v1.3's step is quick) when its log is equal.

## 2. Files

| file | what |
|---|---|
| `tests/suite_net.cpp`, `suite_mem.cpp`, `suite_engine.cpp`, `suite_ag.cpp`, `suite_rdport.cpp`, `suite_search.cpp`, `suite_agbench.cpp` | the suites, Java's order and wording, `--only=GROUP` for partial runs |
| `tests/p4_java.h`, `p4_java.cpp` | java.util.Random, Collections.shuffle, List.toString; Java's `%.Nf` (HALF_UP on the shortest digits) and `%,d` |
| `tests/test_p4suites.cpp` | make check: net-quick, net-shuf, net-shuf2, searchbench, engine, eng-shuf byte for byte, net-quick under step-all and engine under instance-major (debug builds; skipped in release, where the statistics aren't kept) |
| `tests/suite_reqbus.cpp` | RequestBusTest |
| `tests/p4_debug.h`, `p4_debug_checks.sh` | the debug options on every suite (`--step-all`, `--instance-major`), and the check that each prints the same under both |
| `tests/p4_drift.cpp` | the drift and token-wait measurement over engine-full's data sets (spec items 62, 70; Quest 185) |
| `tests/p4suites.mk` | the rules (one include line in the Makefile) |
| `tests/p4_lines.sh` | a partial run's printed lines as one block of a fixture (`P4_ALLOW_FAIL`: FAIL lines listed) |
| `tests/p4suites/` | the fixtures: v1.3's logs of the 20 steps (net in both modes), PROVENANCE |
| `oracle/ref/p4suites.sh` | the Java side: makes the fixtures from `sim/ref` (`--fixtures`), `--check` regenerates and compares |

## 3. How it is checked

- **Log-equal**: each suite's stdout and stderr against v1.3's log, byte for byte (none of these logs has a timing line
  to mask). While a suite is incomplete, each group's printed lines as a contiguous block of the log (`p4_lines.sh`),
  group by group as it was written.
- **Seeds**: every step at its own seed (the shuffled steps through the framework's debug shuffle; ref's shuffled logs
  differ only in their header and summary lines).
- **Builds**: the clang suite build (run_tests.sh's), clang and gcc debug (ASan, UBSan); the timing builds compile every
  suite (those needing SIM_CHECKS as a stub saying so). Four-build make check clean at 6e0d9e2d.
- **The fixtures' JDK**: the 16 quick steps regenerated on the runner equal the container's (Quest 176).
- **Step-all and both orders** (W22-8, W20's MachConfig fields): every suite at its steps' seeds prints the same under
  each as plain (`p4_debug_checks.sh`); the finished logs byte for byte under both. The missed-wake detector runs in
  the debug builds throughout.

## 4. Changes outside my files (each approved by the owner)

| change | where | build | evidence |
|---|---|---|---|
| Fifo peak (ref's WordFIFO.highWater) | src/engine.h, config.h/.cpp | SIM_CHECKS | NetTest's queue peaks; release code identical but config.o's two new functions |
| big SRAM per area: reads, writes, combined, queue peak | src/units/big_sram.h | SIM_CHECKS | MemTest; release differs only in FATAL's line numbers |
| read queue per client: lat_max, first/last issue | src/units/inject.h | SIM_CHECKS | MemTest |
| F1: window-full cycles counted while asleep | src/units/inject.h | every build | token window 80 as ref; test_memconf (compares this counter with ref) unchanged |
| write engine: stage peak, writes by size | src/units/write_engine.h | SIM_CHECKS | EngineTest, ReadPortGatherTest (release differs only in FATAL's line numbers) |
| F3: the write engine's UB stop names the PTR | src/units/write_engine.h | text | EngineTest's ub |
| F5: the traffic director's UB stops | src/units/director.h | text | AgTest's ub |

test_chip and test_sort print the same before and after every one of them (clang release).

## 5. Findings

- **F1** (fixed): the read queue's `window_full_cycles` undercounted while the unit slept with the window full.
- **F2** (W20): `TileRef::mem_idle` indexed the area queues modulo 16, not the configured depth.
- **F3**, **F5** (fixed): UB stops not naming what ref names (the PTR; "traffic director").
- **F4** (W26, fixed): the command-slot check (invariant 8) flagged three commands on the tiles nearest C&C 1 after the
  boot (SearchBench, ReadPortGatherTest's gather from C).
- **The Net's director logs are fixed in size** (16,384 a tile), ref's are lists: the full-size all-to-alls overflowed
  them (Quest 184); suites that don't read them run on the plain chip.
- **W22-20** (W20, fixed): `req_send` took one push a cycle; ref's RequestPort.send any number between cycles.
- **F6** (W26): ref starts a disjoint-buffer phase at the counters-done latch too (TileControl.java:280), not only at
  newPhase and GO; ReadPortGatherTest's legal A -> B, local wake, B -> A run is flagged without it.
- **ref's disjointViolations** (every offending access) isn't kept by the C++ big SRAM (W26: a counter in its mark()).
- **The local new_phase no-op broke eng-shuf** once W26's disjoint check went live: replaced by W20's Chip::new_phase
  wherever ref calls newPhase.
- **ref's committed results are stale** for every P4 step (assertion counts differ; its net.log is the quick run's).

## 6. Runner tasks

| Quest | what | result |
|---|---|---|
| 176 | the four long steps' logs from sim/ref; the 16 quick regenerated there | PASS (retry: attempt 1's check also compared the 4 long ones, which had no fixture yet) |
| 179 | skewtable 40 on the C++; NetTest's groups against the four net logs | PASS (225 s) |
| 184 | engine-full, rdport-full's ported groups on the C++ | rdport-full PASS; engine-full aborted (the Net's fixed director logs at full size), fixed and checked locally |
| 185 | spec items 62 and 70 (p4_drift): drift and token waits over engine-full's data sets | PASS: no drift on any run (the control detects tile 7's), token waits 0 (re-run after its check's fix) |

## 7. Still to do

- W26: F6 and the disjoint counter: then MemTest and ReadPortGatherTest byte for byte, their steps live, the quick ones
  in make check, rdport-full live.
- W22-16 (after W20's reset): AgTest's figure 12, then agtest and agtest-shuf live and in make check.
- A runner pass of the full steps at the end.
