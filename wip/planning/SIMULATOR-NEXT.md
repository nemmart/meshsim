# The next simulator: what we know, and where to start

Written 2026-11-06, at v1.3's freeze, to carry the simulator work into a clean-sheet project (owner: "spend a day or two on
this, and dozens of sessions"; a new integrator session, its workers numbered from W1 again). Everything below is either
**measured** (with its Quest task or session) or **the owner's direction**; proposals are marked as such.

## 1. Why

- **Simulation is a roadblock** (owner). Every version after v1.3 grows, and the runs that matter most are long and busy:
  **a proof replays each tile's window about 50 times for the sumchecks**, so a proof's simulation is dominated by all 64
  tiles replaying at once. With 128 segments × 50 replays × ~236 K cycles, a proof's replay is **~1.5 × 10⁹ chip-cycles:
  about 17.5 hours on one thread with today's engine** (WORKER29, Quest 120).
- **The goal:** full detail, run blazingly fast, **and units that stay easy to write**. Investing in a framework now pays for
  itself across v1.4, v1.5, v1.6 and beyond (owner).

## 2. What today's engine (meshsim) is

A Java, cycle-level model, about 10,000 lines (`wip/meshsim/src`): one class per hardware unit; every unit stepped every
cycle; units connected by timed buses (a put at t seen at t + L) that **commit lazily**, on first touch in a later cycle;
inside a tile, units also **call each other directly in the same cycle** (WORKER28's audit: the core's loads and stores into
controller memory, register writes into `TileControl`, status queries, the director's `pendingAdd`, the injection point's
`takeForSend`, `TileControl.endCycle` reading every unit), correct only because a tile's units step in a fixed order on one
thread. Exactness is checked by **shuffle seeds** (a random stepping order must change nothing) and by comparing logs.
Generated artefacts (`mesh.h`, `REGISTERS.md`) come from the same Java source, so firmware and simulator agree.

## 3. What was measured (the facts the design rests on)

| finding | evidence |
|---|---|
| **An idle chip is dominated by walking idle objects**: on the Tracer's full-chip run (T2), the 64 mesh tiles are 85% of the time, 98.4% of their tile-cycles idle, ~0.6 µs per idle tile per cycle just to walk it; the Tracer itself ~0.1 µs | WORKER28, Quest 113b (46.9 µs per cycle) |
| **Exact idle-skipping gives ~18-19× on the full chip**: units and whole tiles sleep when idle, woken by any input; counters caught up; proven identical off and on (fingerprints, logs, CSVs, shuffle seeds) | WORKER28, Quests 114-118; in v1.3 (T2 segment: 1,030 s → 54 s) |
| **A busy cycle is bound by memory latency over scattered objects, not by the units' logic** | WORKER29: making the buses' commit free (**B**) gained nothing, the time moved from `enterSlow` to `enter` (the cache miss of reading each of ~2,700 bus objects polled per busy cycle); the Parallel GC (which lays objects out differently) is ~40% slower than G1; transparent huge pages help ~10% |
| **The state is small but scattered**: a tile is 2,251 objects and 236 KB (77 buses); the chip ~144 K objects and ~15 MB, interleaved with large arrays | WORKER29 |
| **The units' own work is a minority of a busy cycle**: on the real replay, the replayer 18%, the SRAM read queue 16%, `TileControl.endCycle` 16.5% (reading other units), the core 9.6%, the router 7.7%, bus entry 17% | WORKER29, Quest 116 |
| **Removing per-cycle reaches into other objects pays; trimming bookkeeping pays a little**: A (cheap S1 bookkeeping) + C (event counts as they happen, a cached GO head, the read port's earliest time, direct calls) = **−16%** on both the full-size sort and the busy replay (49.7 → 41.7 µs per chip-cycle), proven exact | WORKER29, Quests 119, 120 (branch `sim-step-cost`, **not yet merged**: the owner's call) |
| **The interpreter (the RV32 cores) is ~0.3-3% of the time** | WORKER28, WORKER29 |
| **Idle-skipping does nothing for a busy chip** (the sort, the replay after GO) | by construction; the sort's cost is the step itself |

## 4. The design direction (owner + WORKER29's draft)

**The framework carries the rules; a unit's author writes ordinary code.** Prior art to borrow from: Verilog's sensitivity
lists, Verilator's ordering of combinational logic, SystemC's delta cycles, gem5's ports and events, Sparta.

- **A cycle has three phases:** deliver (everything due at t into its consumers' inputs, setting their ready bits), run
  (units with input or pending work step, level by level), commit (staged writes and pushes applied; link words into the
  timing wheel at t + L). Today's convention made structural, no lazy commit, no fixed stepping order to respect.
- **Communication primitives (ports), declared once; units never talk directly.** WORKER29's set, each with today's exact
  semantics so the current engine stays the oracle: `Link(L)` (visible from t + L), `Fifo(N)` (full judged by the count at the
  start of t), credit FIFO, `Hold` (a holding register; refused if the old word isn't taken), `Reg` (visible from t + 1),
  `RegFile` (with merge rules), `Mem(ports)` (a memory macro), `Wire` (combinational, this cycle's value), `Count` (event
  counters summed). Rule for authors: **an input that must be this cycle's value is a `Wire`; anything else is registered.**
  The mapping of every one of today's same-cycle couplings onto these ports is in `sessions/WORKER29-DESIGN.md` section 3.
- **Transports are payload-agnostic** (owner): a `Link`, `Fifo` or `Hold` decides when something moves and whether there is
  room, never what it is; only the units at the ends interpret it. **But not by pointer** (integrator, from the
  memory-latency finding): small payloads (a link word, 128 bits plus header, ~24 bytes; a queue entry, 64 bits) travel **by
  value in fixed-size opaque slots**, rings of slots of the port's declared width, moved by a few-word copy, no allocation,
  no lifetime; large payloads travel **by handle**, an index into a contiguous pool with a free list.
- **Ordering from the wires**: a level per unit (its longest wire path); a combinational loop rejected when the chip is
  built; **type-major evaluation** (for each level, each unit type, every instance), legal because no wire crosses a tile.
- **Wakes without asking**: a **ready mask** per unit, set by delivery; a unit reads only ports whose bits are set (an empty
  input is never read: exactly B's cost); a unit with no bit and no pending work isn't stepped. This subsumes idle-skipping,
  at the unit level, inside busy tiles too. A **timing wheel** of ~128 buckets (largest latency, a memory fill, 80).
- **Data layout is the centre** (WORKER29): ports in **pools by kind** (parallel primitive arrays, numbered consumer
  type-major), ready masks and status words in dense arrays; unit state first as ordinary objects **allocated by type**, big
  arrays kept apart; an opt-in **structure-of-arrays** form `[type][field][chip × 64 + tile]` for a GPU later.
- **Threads:** throughput first, **independent chips on independent threads** (a proof's segments are independent ASICs; no
  sync); then one chip over tile groups, synchronising **every 3 cycles** (every cross-group coupling is a link of 3 or a line
  of 8; GO, READY and the error line are 8 since v1.3), cross-group words through per-thread outboxes merged at the barrier.
- **Debug checks:** shuffle within a level; wire discipline (read before driven, or driven twice, throws); two evaluation
  orders compared by digest.
- **A GPU backend later, as a design target, not built** (owner's idea): **one unit type per warp**, over **many chips at
  once** (a proof's 128 segments × 64 tiles = 8,192 instances of each type), state laid out so a warp's lanes read adjacent
  words; the cores the hardest part (divergence), eased when a batch runs the same firmware.
- **Languages** (owner): **build the framework in Java first** (the JVM is fast, and today's engine is the oracle), get it
  running and tuned, **then port it to C++ with minimal STL and our own low-level structures** (rings of fixed slots,
  bitmasks, pools with free lists, the wheel). Lay the Java version out so the port is mechanical: slots as `long[]` ranges,
  no generics over boxed objects.

## 5. How it would be built (WORKER29's migration plan, each step proven against today's engine)

M0, the framework core with a whole `Tile` as one unit (its links and C&C links as ports, the lines as lines); M1, inside
the tile, unit by unit, hottest first (router and FIFOs, the SRAM read queue and injection, the director, a status array
for `TileControl`, the engines, the core, the slot); M2, the layout (type-contiguous allocation, type-major evaluation); M3,
threads (independent chips, then tile groups); M4, the GPU form, only if the owner decides. **Exactness is the requirement
at every step**: identical logs and fingerprints, cycle for cycle, any difference a bug.

**The first prototype** (WORKER29's step 3, not started): a FIFO consumer, the router and a small core; judged on exactness
against the all-to-all (NetTest), speed per chip-cycle (one mesh, then 128 at once on 1, 8 and 16 threads), and lines of code
per unit against today's. **Estimates** (WORKER29, unmeasured): 3-4× per thread on a busy chip (~10-15 µs per chip-cycle
against 41.7); a proof's replay in ~20-40 minutes on 16 threads with independent chips.

## 6. Open questions, carried over

1. **Are a window's ~50 replays identical in control flow?** Each pass replays the same window from the same record and
   streams; what can differ is `RUN_CONFIG`'s two per-pass flags (use the store log, output rd_prev). The owner to confirm
   how the flags vary over a proof. If identical, a GPU can run passes in lockstep.
2. **Unit state as objects first, arrays opt-in later** (integrator: yes, for ease of authoring; the measurements decide).
3. **Measure today's engine with 16 independent chips at once first**, so the framework is judged against today's best,
   not one thread (integrator: yes).
4. **Merge A + C (`sim-step-cost`) into `main`?** Proven exact on the full regression's runs, −16% on busy runs; the new
   framework would replace it anyway, but it helps every run meanwhile (integrator: yes).

## 7. Where everything is

- `wip/planning/sessions/WORKER29-DESIGN.md`: WORKER29's full design draft (~3,200 words; port semantics, the coupling
  map, ordering, wakes, layout, threads, debug, authoring, migration, prototype, estimates).
- `wip/planning/sessions/WORKER29-NOTES.md`: its measurements (Quests 116, 119, 120; the object graph; B's negative
  result), and `WORKER29-tools/` (JFR folding, the proof task builder, `prove_same.sh`).
- `wip/planning/sessions/WORKER28-NOTES.md`: the idle-skipping work (the wake sources, the exactness hazards, the
  profile, stage 1 as built), `WORKER28-BASELINES-8.md`, and `WORKER28-tools/` (`prove_skip.sh`, the digest, `baseline_diff.py`).
- Branches: `sim-step-cost` (A + C, proven, unmerged), `sim-step-cost-b1-measured` (B's measured attempt), `sim-idle-profile`
  (the profiling probe, never merged).
- The benchmark: `test/ReplayBench.java` (on `sim-step-cost`): all 64 tiles replaying at once on real windows.
- The runner: Quest, `PIPELINE.md` (claim **plain numbers**: a letter suffix runs first; the 45-minute task limit is the
  owner's to raise; give long runs a fail-fast).
