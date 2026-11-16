# WORKER22 report: P4, the network, memory and engine suites (inventory C3-C7, C11, C17, G1)

**Done but one group.** v1.3's eight suites in my rows run in C++ on W20's harness (`tests/mach.h`, `tests/suite.h`),
without Java or `sim/ref`. Seven are byte for byte equal to v1.3's log of every one of their `run_tests.sh` steps,
quick and full, plain and under the debug options, and are live in `sim/run_tests.sh`, the quick ones in `make check`.
The eighth, AgTest, passes all 206 of its assertions ported; its figure-12 group (34) waits on W22-16 (W20). Branch
`sim-w22`, merged into main as it went.

## 1. The steps

| row | suite | steps | result | live | make check |
|---|---|---|---|---|---|
| C3 | NetTest | net (quick and all), net-shuf, net-shuf2, skewtable | byte for byte, 31,328 assertions; skewtable 40 on the runner (Quest 179, 225 s) | all four | net, net-shuf, net-shuf2 |
| C4 | MemTest | mem, mem-shuf, mem-shuf2 | byte for byte, 171 | all three | all three |
| C5 | EngineTest | engine, eng-shuf, engine-full | byte for byte, 365 (engine-full 392, 92 s) | all three | engine, eng-shuf |
| C6 | AgTest | agtest, agtest-shuf | 206 of 206 ported; figure 12 (PTR and HIST at L = 1 and 3, 34 assertions): W22-16 | PENDING (W22-16) | when equal |
| C7 | ReadPortGatherTest | rdport, rdport-shuf, rdport-full | byte for byte, 587 (rdport-full 594, 18 s) | all three | rdport, rdport-shuf |
| C11 | SearchBench | searchbench | byte for byte, 56 | yes | yes |
| C17 | RequestBusTest | reqbus, reqbus-shuf | byte for byte, 50 | both | both |
| G1 | AgBench | agbench | byte for byte, 145 lines (14 s, about 1,500 machines) | yes (all mode) | no (v1.3's step is full-only) |

`make check`'s `test_p4suites`: those 13 quick steps byte for byte, plus net-quick under step-all and engine under
instance-major: 15 tests, 169 s in clang debug (skipped in the timing builds, which don't keep the statistics read).

## 2. Files

| file | what |
|---|---|
| `tests/suite_net.cpp`, `suite_mem.cpp`, `suite_engine.cpp`, `suite_ag.cpp`, `suite_rdport.cpp`, `suite_search.cpp`, `suite_reqbus.cpp`, `suite_agbench.cpp` | the suites: Java's assertions and printed lines in Java's order; v1.3's arguments; `--only=GROUP` (partial runs), `--step-all`, `--instance-major` |
| `tests/p4_java.h`, `p4_java.cpp` | java.util.Random, Collections.shuffle, List.toString; Java's `%.Nf` (HALF_UP on the shortest digits) and `%,d` |
| `tests/p4_debug.h`, `p4_debug_checks.sh` | the framework's debug options on every machine a suite builds; the check that every suite prints the same under each |
| `tests/test_p4suites.cpp` | make check (above) |
| `tests/p4_lines.sh` | a partial run's printed lines as one block of a fixture (`P4_ALLOW_FAIL`: FAIL lines listed): the group-by-group check while porting |
| `tests/p4_drift.cpp` | drift and token waits over engine-full's data sets (spec items 62, 70; Quest 185) |
| `tests/p4suites.mk` | the rules (one include line in the Makefile) |
| `tests/p4suites/` | the fixtures: v1.3's logs of the 20 steps (net in both modes), PROVENANCE |
| `oracle/ref/p4suites.sh` | the Java side: `--fixtures` makes them from `sim/ref`, `--check` regenerates and compares |
| `run_tests.sh` | (shared) the P4 steps, under v1.3's names, against the fixtures with `--check` |

## 3. How it is checked

- **Log-equal**: each suite's stdout and stderr against v1.3's log of the same step, byte for byte (none of these logs
  has a timing line). The references are regenerated from `sim/ref` (`oracle/ref/p4suites.sh`), not taken from
  `ref/results`, which is stale for every P4 step (other assertion counts; its net.log is the quick run's). The 16 quick
  logs regenerated on the runner equal the container's (Quest 176); the four long ones were made there.
- **While porting**: each group's lines checked against the fixture as it was written (`p4_lines.sh`), at every
  seed, before the suite was whole.
- **Seeds and orders**: every step at its own seed (the framework's debug shuffle; ref's shuffled logs differ from the
  unshuffled only in their header and summary lines); every suite the same under step-all and instance-major as plain
  (`p4_debug_checks.sh`), the logs byte for byte under both; the missed-wake detector on in the debug builds.
- **Builds**: the clang suite build (run_tests.sh's); clang and gcc debug (ASan, UBSan) for every suite; four-build
  `make check` clean (last full run 6e0d9e2d; each later push built and tested on clang and gcc debug and the suite
  build). gcc caught two `-Wdangling-reference` cases (a reference through a TileRef temporary), fixed.
- **The harness's own limits met**: a UB stop runs in `suite::ub`'s forked child, so where ref keeps the facts recorded
  before its stop, the parent runs to the cycle before the stop (AgTest's computed fields); the Net's director logs are
  fixed in size where ref's are lists, so suites that don't read them run on the plain chip (EngineTest's all-to-all at
  full size, found by Quest 184).

## 4. Changes outside my files (each approved by the owner)

| change | where | build | evidence |
|---|---|---|---|
| a Fifo's peak (ref's WordFIFO.highWater) | src/engine.h, config.h, config.cpp | SIM_CHECKS | NetTest's queue peaks; release code identical but config.o's two new functions |
| big SRAM per area: reads, writes, combined, queue peak | src/units/big_sram.h | SIM_CHECKS | MemTest |
| the read queue per client: latency max, first and last issue | src/units/inject.h | SIM_CHECKS | MemTest |
| F1: the read queue's window-full cycles counted while it sleeps | src/units/inject.h | every build | MemTest's token window (80 as ref); test_memconf, which compares this counter with ref, unchanged |
| the write engine's stage peak and writes by size | src/units/write_engine.h | SIM_CHECKS | EngineTest, ReadPortGatherTest |
| F3: the write engine's UB stop names the PTR | src/units/write_engine.h | text | EngineTest's `ub` |
| F5: the traffic director's UB stops name it so | src/units/director.h | text | AgTest's `ub` |

For each: test_chip and test_sort print the same before and after (clang release), and the rebuilt release objects'
code differs only as stated (the new functions, or FATAL's `__LINE__` argument).

## 5. Findings

| # | what | where | state |
|---|---|---|---|
| F1 | the read queue's `window_full_cycles` undercounted while the unit slept with the window full (9 for ref's 80) | inject.h (W11's, W20's path) | fixed, every build |
| F2 | `TileRef::mem_idle` indexed the area queues modulo 16, not the configured depth | mach.cpp (W20) | W20 |
| F3 | the write engine's UB stop didn't name the PTR (ref: PTR[src N], PTR[digit]) | write_engine.h | fixed |
| F4 | the command-slot check (invariant 8) flagged the first command after the boot on the tiles nearest C&C 1 | W26's | fixed by W26 |
| F5 | the traffic director's UB stops said "director" | director.h | fixed |
| F6 | no disjoint-buffer phase at the counters-done latch (ref: TileControl.java:280) | W26's | fixed by W26 |
| — | ref's committed results stale for every P4 step | sim/ref/meshsim/results | recorded; references regenerated |
| — | the local new_phase no-op broke eng-shuf once W26's disjoint check went live | my suites | fixed: W20's Chip::new_phase wherever ref calls newPhase |

## 6. The harness asks (W22-1 to W22-20)

Answered by W20 and W26 as listed in NOTES.md: the coreless machine's missing pieces (C&C's receive, reset_stats, the
send-port hold, the writer and gather logs, K's check), the debug options (W22-8), Net on the machine as specified
(W22-9), the request bus from a test (W22-10, W22-11, W22-19, W22-20), the core's data port (W22-12), the checker's
kinds (W22-6, W22-13, invariants 2, 3, 6, 8), phase_done (W22-15), the TRACE log (W22-17), the read port's host queue
(W22-18), new_phase (W22-14). **Open: W22-16** (the write engine's RMW requests on the table stores' request lines, for
figure 12), W20's next item.

## 7. Runner tasks

| Quest | what | result |
|---|---|---|
| 176 | the four long steps' logs from sim/ref; the 16 quick regenerated there | PASS (on retry: attempt 1's check also compared the four long ones, which had no fixture yet) |
| 179 | skewtable 40 on the C++; NetTest's groups against the four net logs | PASS, 225 s |
| 184 | engine-full, rdport-full's ported groups on the C++ | rdport-full PASS; engine-full aborted (fixed director logs at full size): fixed, then checked locally |
| 185 | spec items 62 and 70 (p4_drift), for the docs session | PASS: no drift on any run of engine-full's data sets (the control detects tile 7's), token waits 0, read window-full 0; run cycles equal engine-full.log's |

## 8. Notes for others

- **For P8 and the freeze check**: the full P4 steps' times: skewtable 225 s on the runner (Quest 179); engine-full
  92 s, rdport-full 18 s, agbench 14 s in this container (one CPU, clang suite build). `p4_debug_checks.sh` runs every
  suite under both debug options (a few minutes).
- **For suite writers**: `p4_java.h` has Java's Random (nextInt with any bound, nextDouble, nextLong), shuffle and
  number formatting; `p4_lines.sh` checks a suite group by group against its fixture.

## 9. Still to do

- W22-16, then AgTest's figure 12: agtest and agtest-shuf byte for byte, live and in make check. Then this report's
  status line, and a last four-build make check.
