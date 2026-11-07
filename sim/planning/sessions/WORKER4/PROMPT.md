# Worker 4 (sim/): threads: independent chips, and one chip over several threads

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`): very fast, parallel, and easy to
build units in, with v1.3's Java simulator (`sim/ref/`) as the exactness oracle. W3 has built the engine, local and
single-threaded, with every port kind (on `main`); it is still adding port conformance (increment 4) and the missed-wake
detector (increment 5). W5 is putting the RV32 controller on it. **You make it parallel** (PLAN-M1 item 7), in the two forms
`sim/README.md` and SIMULATOR-NEXT section 4 describe:
1. **Independent chips on independent threads**, throughput first: a proof's segments are independent ASICs, so most runs
   are many chips at once with no synchronisation at all. This is also M1's last acceptance test: N controllers on N
   threads, each still matching its golden trace.
2. **One chip over several threads** (tile groups): each thread owns its partition with its own wheels, run sets and pools;
   a put to another thread's unit goes to an outbox for that thread, merged at a barrier; the barrier interval is the
   smallest cross-domain latency (3 on the mesh: every cross-group coupling is a link of 3 or a line of 8).

READ FIRST: `sim/README.md` (thread-local work; "Crossing a thread domain is a different kind of port"; the 128-bit and
wide payloads); `wip/planning/SIMULATOR-NEXT.md` section 4 (threads) and section 3 (the measurements: memory-bound across
the box); `sim/planning/PLAN-M1.md` (you are W4, item 7; the wide-payload note under W3); `sim/planning/DECISIONS.md` (D9,
D13, D15, D18, D20 especially). The engine: `sim/src/sim.h`, `config.h`, `PORTS.md`, `engine.h`, `engine.cpp`,
`config.cpp`, and `sim/planning/sessions/WORKER3/NOTES.md` (stage 2's order and the hooks left for you: `Config::domain`,
`Options::threads`, routes that already name their wheel or an outbox and carry their slot class). The measurements:
`sim/planning/sessions/WORKER6-REPORT.md` (throughput against N, the hybrid runner, footprint) and `WORKER2-REPORT.md`
(the bar: 36.8 µs per chip-cycle for one `ref` replay, ~100,000-110,000 chip-cycles/s for the box).

THE WORK
1. **Independent chips.** Several machines built and run side by side, one per thread, sharing nothing mutable: check
   that nothing in the framework is a hidden global (the allocation hook is thread-local; find anything else). Tests: N
   synthetic machines on N threads give the same digests as one at a time; TSan clean.
2. **Cross-domain ports.** For each port kind that may cross (Link, Fifo, credit Fifo, Hold, Reg, Count's `through_last`),
   its cross-domain form, chosen at configuration from the partition: the route points at the outbox for the destination's
   thread, so a unit's code is the same either way (sim.h). A Wire, a RegFile or a Count `now` read across domains is fatal
   at build, naming the units. Slot payloads (128-bit and wide classes) are copied into the destination's pool at the merge
   and the index rewritten. Outboxes sized at startup from the cross-domain ports, fatal if exceeded.
3. **The barrier and the merge.** The barrier interval from the partition (the smallest cross-domain latency, at least 1);
   per thread, run that many cycles, then merge every outbox into the destination's wheels, then go on. No locks in the
   cycle. Choose the barrier's implementation by measurement (spinning against blocking on the hybrid runner).
4. **Determinism.** The same machine on 1, 2, 4 and 8 threads, over several partitions, must give **identical digests**
   (and, for W5's controller, identical traces). In your plan, argue why delivery order within a bucket can't change a
   result (one put per port per cycle; Counts sum) or, where it can, how the merge fixes the order. Keep the debug checks
   working per thread (shuffle, both orders, step-all, the missed-wake detector when it lands).
5. **Measure** (on the runner, clang release, D18; pinned, D13): independent chips scaling 1, 6 and 12 threads on the
   P-cores, E-cores separately, as W6 did; one chip over 2, 4 and 6 threads against one thread on a synthetic busy chip
   (W6's 8 × 8 mesh, rebuilt on the framework, is the natural one), with the barrier's cost; and memory per chip (D15).
6. **With W5:** once W5's controller matches on the engine, N controllers on N threads running different programs, each
   matching its golden: M1's last acceptance test.

RULES
- **Propose your plan first** (the two forms, the files, how you'll change the engine with the least overlap with W3's
  remaining increments, the determinism argument, the tests) in `WORKER4/NOTES.md`, and wait for the go-ahead before writing
  code.
- Work on branch `sim-w4`. W3 owns `sim/src/` and is still changing `engine.h`/`config.cpp`: put your code in new files
  where you can (e.g. `threads.h`/`.cpp`), keep engine changes small and say each one in your notes, and merge `main` often
  (never rebase); I merge W3's increments into `main` as they land. If the API needs a change, write it up and tell me first.
- The cycle-loop rules (D6): no STL, no allocation, no virtual calls, no locks in the cycle; untyped words. Sanitizers
  (ASan, UBSan, and **TSan** with gcc and clang: the runner supports it, Quest 122) and deterministic tests from the first
  commit; `make check` clean on both compilers.
- Long runs and all timing go to the runner (claim plain Quest numbers in `NUMBERS.md` first; tasks carry embedded kits);
  benchmark threads pinned to the P-cores (CPUs 0-11; 0, 2, .., 10 for one per physical core), E-cores (12-15) reported
  separately.
- Never write a token into a file. Never `pkill -f` a pattern that matches your own command line.
- Commit and push after each completed piece, keep the last tool calls for committing, end each turn with a clean
  `git status`.
- Keep replies to the owner short.

DELIVERABLES: both forms on `sim-w4` with their tests (identical digests across thread counts and partitions, TSan clean);
the Quest measurements; with W5, N controllers on N threads matching; `WORKER4/REPORT.md`.
