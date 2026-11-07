# Worker 3 (sim/): the framework: configuration, the engine, the port kinds, and the unit author's API

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`): very fast, parallel, and easy to
build units in, with v1.3's Java simulator (`sim/ref/`) as the exactness oracle. Milestone 1 is the primitives plus one RV32
controller running real firmware, matching `ref` instruction for instruction. W1 built the cycle loop's structures
(`sim/src/base/`). **You build the framework itself** (PLAN-M1 items 3-6): how units declare ports, how a chip is sized and
wired, the cycle loop, the port kinds with `ref`'s exact semantics, and the debug checks. **Your API decides whether units are
easy to build, which is half the goal**: the owner reviews it himself, after the integrator (D10). Beside you: W2 (the
reference side, Java) and W6 (benchmarks whose results shape your API); after you: W4 (threads) and W5 (the controller).

READ FIRST: `sim/README.md` (the owner's direction: the core loop, phases, thread-local work, local and cross-domain ports,
memory, untyped 64-bit payloads, sizing at startup, the two coding regimes, the build), `wip/planning/SIMULATOR-NEXT.md`,
`sim/planning/PLAN-M1.md` (you are W3), `sim/planning/DECISIONS.md`, `sim/planning/sessions/WORKER1-REPORT.md` (the
structures and their conventions), and **`wip/planning/sessions/WORKER29-DESIGN.md` in full** (the port kinds and their
semantics, the map of today's same-cycle couplings onto them, ordering, wakes, layout, authoring). The oracle's ports:
`sim/ref/meshsim/src/Bus.java`, `Buses.java`, `WordBuses.java`, `LinkWord.java`, `RequestBus.java`, and
`sim/ref/meshsim/README.md`'s conventions (a put at t seen at t + L, shuffle seeds). For a unit you will sketch:
`sim/ref/meshsim/src/Controller.java`.

THE WORK, in two stages
**Stage 1: the API, before implementation.**
1. **The port semantics**, `sim/src/PORTS.md`: for each kind (`Link(L)`, `Fifo(N)` with its overflow count, credit `Fifo`,
   `Hold`, `Reg`, `Wire`, `Count`, a minimal `RegFile`), its exact rule as `ref` implements it (when a put becomes visible,
   when full is judged, what a refusal does, what a reader may see in which phase), with the `ref` class and lines each comes
   from. Where WORKER29's draft and `ref` differ, `ref` wins: say so.
2. **The unit author's API**, `sim/src/sim.h` (or a small set of headers): how a unit type declares its ports and capacities,
   its state, its `step` (reading inputs, writing outputs as plain code; no commits, no stepping order, no reaching into
   another unit), its wakes (none, next cycle, at cycle t), and an optional **result-only form** (D7). How a chip is
   declared, sized and wired (the configuration regime: any C++, D6). How a port's route is resolved once at configuration
   (local bucket, or an outbox W4 will add: **the unit's code must not change** when a port crosses domains). How phases are
   defined by what they may read and write, and how levels come from the Wires. Payloads are untyped `uint64_t` words; no
   templated transports or message types (`sim/README.md`).
3. **Two sketches against it**, compiled but not run: a FIFO consumer, and the outline of the controller's surroundings
   (the core's ports to its register block and the test bench, following PLAN-M1 item 8 and W2's audit when it lands). Count
   their lines against `ref`'s equivalents.
4. **W6's findings** (the link word, fixed or runtime widths, how units hold 128-bit values) come to you through the
   integrator; fold them in. **Then stop for review:** the integrator, then the owner. Implementation starts only on the
   owner's go-ahead.

**Stage 2: the implementation** (PLAN-M1 items 3-6, local forms only; W4 adds the cross-domain forms and threads).
- Configuration: the type registry (small integer ids, no RTTI), declarations, the sizing pass (every pool, ring, bucket and
  value array at its exact maximum, per thread, carved from W1's blocks), wiring, levels, combinational loops rejected with the
  units named.
- The engine: the wheel of delivery buckets, ready masks set by delivery, run lists by level and type (dispatch per type's
  run, never per port or per unit), the phases, wakes with catch-up of per-cycle counters.
- The port kinds; the debug checks (shuffle within a level by seed, wire discipline, type-major against instance-major by
  digest).
- **Acceptance** (PLAN-M1, W3): port conformance against `ref` (one seeded script of puts, takes and peeks per kind drives
  `ref`'s bus classes through a Java driver in `sim/tests/ref/`, compiled into `ref`'s default package (D5), and the C++
  port; per-cycle observations identical over at least 10⁵ cycles at several depths and latencies); sizes equal to
  hand-derived ones; overflow fatal; loops rejected; digests stable across shuffle seeds and both orders; wire-discipline
  violations fatal; an idle unit never stepped; a 10⁹-cycle sleep costing one wake; no allocation in the loop (W1's hook); a
  toy unit's two forms agreeing.

RULES
- **Propose your plan first** (the order of work, the files, how you'll derive each port's semantics from `ref`) in
  `WORKER3/NOTES.md`, and wait for the go-ahead. Stage 1's deliverables are documents and headers; stage 2 waits for the
  owner's review of the API.
- Work on branch `sim-w3`, in `sim/src/` (beside `base/`, which is W1's: propose any change to it in your notes) and
  `sim/tests/`. W2 also writes in `sim/tests/ref/` (its trace runner): use your own file names there. W6 works in
  `sim/bench/`.
- `sim/ref` is frozen (D5): your Java driver uses its classes, never changes them.
- The cycle-loop rules (D6): no STL, no allocation, no virtual calls, untyped words; anything goes in configuration and tests.
  Sanitizers and deterministic tests from the first commit; `make check` stays clean on gcc and clang.
- Long runs go to the runner (claim plain Quest numbers in `NUMBERS.md` first; tasks carry embedded kits). Benchmark threads
  pinned to the performance cores (D13).
- Never write a token into a file. Never `pkill -f` a pattern that matches your own command line.
- Commit and push before anything long in a turn, keep the last tool calls for committing, end each turn with a clean
  `git status`.
- Keep replies to the owner short.

DELIVERABLES: stage 1: `PORTS.md`, the API headers, the two sketches, for review. Stage 2: the implementation on `sim-w3`
with its tests, `make check` clean; `WORKER3/REPORT.md`.
