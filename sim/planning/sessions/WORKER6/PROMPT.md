# Worker 6 (sim/): the first benchmarks: the link word, and the cost of 128-bit arithmetic

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`): very fast, parallel, and easy to
build units in, with v1.3's Java simulator (`sim/ref/`) as the exactness oracle. W1 built the cycle loop's structures
(`sim/src/base/`). W3 is now designing the framework's configuration, engine and port kinds, starting with **the unit
author's API**. Your measurements decide two things in that API before it is agreed (DECISIONS D10), so speed of answer
matters more than polish:
1. **The mesh's link word:** (a) two words, the routing header and the data (the data itself for a 32- or 64-bit payload, an
   index into the thread's 128-bit value array otherwise), against (b) three words inline. See `sim/README.md`, "Payloads are
   64-bit words, untyped".
2. **Arithmetic:** what a unit doing real Fp128 arithmetic costs, so the field engine to come (D7) can be simulated cheaply.

READ FIRST: `sim/README.md`, `wip/planning/SIMULATOR-NEXT.md` (sections 1, 3, 4, 4b), `sim/planning/PLAN-M1.md` (you are
W6), `sim/planning/DECISIONS.md` (D6, D7, D10, D13), `sim/planning/sessions/WORKER1-REPORT.md` (the structures and how to
use them: carve, don't allocate; the allocation hook). For background on why memory layout dominates:
`wip/planning/sessions/WORKER29-DESIGN.md` sections 1 and 6. For the runner: Quest's `PIPELINE.md` and `NUMBERS.md`, and
Quest task 122 (`tasks/122-sim-runner-check.sh`: an embedded kit, the runner's facts).

THE WORK (a hand-rolled loop over W1's structures, in `sim/bench/`, not the framework: W3 is building that)
1. **A synthetic busy mesh.** 8 × 8 tiles, each a simple XY router (five input FIFOs as `Ring`s of declared depth, one per
   direction plus local injection; one word out per output per cycle) joined by links of a fixed latency (3, as the mesh's
   cross-group links); each tile injects at a configurable rate to random destinations, with a configurable **payload mix**
   (all 128-bit, the field engine's case; and a mixed 32/64/128-bit case). Structure it as `sim/README.md`'s core loop, so the
   result transfers: per thread, delivery buckets by arrival cycle (a wheel of `Bucket`s), deliver into FIFOs, mark ready, run
   the ready routers, commit. Exactness to `ref` is **not** required; determinism is: fixed seeds, and **(a) and (b) must
   deliver the identical sequence of payloads to every destination** (check by digest).
2. **The two link words.** (a): a 128-bit payload is stored in the injecting thread's `ValuePool` and freed at ejection;
   routers read only the header word. (b): three words inline, no pool. Also measure, while you have the harness, the
   **runtime-width `Ring`/`Bucket` against a compile-time fixed width** (W1's question 2): a copy of the two headers with the
   width a template parameter is enough. That choice goes to the owner if fixed width wins measurably (it edges toward
   templated transports).
3. **Scale.** ns per mesh-cycle and per hop: one mesh on one thread; then 128 independent meshes on 1, 6 and 12 threads
   pinned to the performance cores (`taskset -c 0-11`; D13), the efficiency cores (12-15) separately. `perf stat` (cycles,
   instructions, cache misses) for each, and a `perf record` profile of the busiest case.
4. **Fp128 arithmetic.** The project's modulus isn't in the repository yet: use a representative 128-bit prime (say
   2^128 - 159) unless the owner names one (ask in `QUESTIONS.md`). Measure a Montgomery multiply with `unsigned __int128`
   (and with explicit 64×64→128 multiplies if that's faster): latency and throughput per operation; then **in a unit-like
   loop**: a toy multiplier unit that takes operand pairs (indices into the `ValuePool`) from a `Ring`, multiplies, and pushes
   the result index, so the cost includes the data movement. Same pinning and `perf stat`.
5. **The recommendation**, short, in `WORKER6/REPORT.md`, and the integrator relays it to W3: the link word (a or b, and by
   how much on each mix), fixed or runtime width, how a unit should hold and compute on 128-bit values, and anything else the
   numbers say about W3's API (the delivery entry's layout, where the destination's id goes, what a ready mark costs).

RULES
- **Propose your plan first** (the harness's structure, the variants, the runs, the Quest tasks) in `WORKER6/NOTES.md`, and
  wait for the go-ahead before writing code. Keep it lean: these are measurements to decide an API, not a product.
- Work on branch `sim-w6`, in `sim/bench/` (and your session folder). Don't change `sim/src/base/`: if a structure needs a
  change, propose it in your notes and the integrator decides. W3 works in `sim/src/` and W2 in `sim/ref`, `sim/tests/ref`,
  `sim/tests/golden` and `sim/fw`.
- The cycle-loop rules hold in the benchmark (DECISIONS D6): no STL, no allocation, no virtual calls in the loop; prove the
  loop allocates nothing with W1's hook.
- Release builds for timing (`-O2`; try `-O3` and `-march=native` and say whether they matter). Timing runs go to the runner
  (claim plain Quest numbers in `NUMBERS.md` first; the meshsim repository is private, so tasks carry embedded kits); nothing
  else runs on the box during a timing task if you can help it, so keep tasks short and say so in their header.
- Never write a token into a file. Never `pkill -f` a pattern that matches your own command line.
- Commit and push before anything long in a turn, keep the last tool calls for committing, end each turn with a clean
  `git status`.
- Keep replies to the owner short.

DELIVERABLES: `sim/bench/` on `sim-w6` with its `make` targets; the Quest tasks and their numbers; `WORKER6/REPORT.md` with
the tables and the recommendation.
