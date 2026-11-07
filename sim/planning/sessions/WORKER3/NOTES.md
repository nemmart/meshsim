# WORKER3 (sim/): notes

Branch `sim-w3`. PLAN-M1 items 3-6: configuration, the engine, the port kinds (local forms), the debug checks; the unit
author's API first. Plan agreed with the owner 2026-11-07 (his five notes folded in below, marked **owner**).

## For the owner: stage 1 for review (2026-11-07)

**What to read:** `sim/src/sim.h` (the unit author's API: the cycle, wakes, payloads and widths, unit state, the debug
checks), `sim/src/config.h` (building a machine: sizing, the wheel's rule and cost, mixed fidelity), `sim/src/PORTS.md`
(each port kind's exact rule from `ref`), and three compiled sketches in `sim/src/sketch/`: `write_engine.cpp` (a FIFO
consumer), `controller.cpp` (the controller's surroundings, against W2's AUDIT-M1), `tile_builder.cpp` (a tile cycle-level
or high-level per instance).

**The eight choices**, each with what the alternative would cost:

| # | choice | the alternative, and its cost |
|---|---|---|
| 1 | **Explicit wakes**: a unit steps on a delivery, a change of a Wire it reads, or a wake it asked for; data left waiting doesn't re-step it. Safety net: the step-all debug check (digests equal) | re-step while any input holds data: simpler for authors, but a step per blocked unit per cycle (W6: polling a quiet mesh is 1.85x slower than marks) |
| 2 | **The shifted end of cycle**: `endCycle(t)` is a level-0 step at t + 1, fed by Links of latency 1 | a commit phase that runs unit logic: a third kind of step with its own ordering rules and run lists |
| 3 | **A Wire is a persistent level**; a change wakes its readers in the same cycle | wires driven every cycle: every wire's producer steps every cycle, so no idle-skipping for those units |
| 4 | **A Hold's refusal wakes its producer** | producers poll `refused` every cycle while they hold a put (as `ref` does): steps with nothing to do |
| 5 | **One level per (type, form)**, the largest of its instances | levels per instance: more, shorter runs per type and more dispatches; nothing in M1 needs it |
| 6 | **Units moved into arenas, handles patched** at build (move-constructible and no virtuals checked by `static_assert`; no pointer into itself or another unit, stated) | units allocated in place: count instances before adding them (by hand, which the brief forbids) or give up type-contiguous state; or an indirection on every port access |
| 7 | **One wheel per (kind, width)**, fixed-size copies, a delivery loop per width (D16) | one wheel for all: a switch per delivered entry and copies at the widest width (W6: 11-13% more instructions with runtime widths) |
| 8 | **Every unit is stepped at the first cycle** (cycle 1, as in `ref`), to drive its initial outputs | an `init` hook per type: one more concept for authors, for what the first step already does |

**Folded in from W6 and D15-D18:** the link word is three words inline and 128-bit values live in units as two words
(`values()` only above 128 bits, D17); widths are runtime in declarations and specialised inside (D16); the destination
id is an entry's first word, marks per delivery, one run per (type, form) (W6 confirms all three); `Sizes` reports words by
structure, by unit type and per chip (D15). **Flagged for M2, unchanged:** the wheel's worst-case sizing (a quarter of W6's
mesh footprint; two options in `config.h`). **Stage 2's Makefile (D18):** `make` and the timing builds use clang (release
`-O2`); `make check` keeps gcc and clang, debug and release; `make conformance` runs the Java matrix (`portconf.sh`).

## The plan

### Stage 1: the API, before implementation (then stop for review: the integrator, then the owner)

1. **`sim/src/PORTS.md`, the port semantics.** For each kind (`Link(L)`, `Fifo(N)` with its overflow count, credit `Fifo`,
   `Hold`, `Reg`, `Wire`, `Count`, a minimal `RegFile`), its exact rule as `ref` implements it: when a put becomes visible,
   when full is judged, what a refusal does, what a reader may see in which phase, with the `ref` class and lines.
   How each is derived:
   - read the class (`Bus.java`, `Buses.java`, `WordBuses.java`) and quote its rule with lines;
   - check it against `ref/meshsim/test/BusTest.java`;
   - grep which class each real coupling in `ref` instantiates, so each kind maps to **the class the machine actually runs**
     (**owner:** that class is the rule; where WORKER29's draft or a generic class differs, say so).
   Divergences seen while planning:
   - `WordFIFO` judges full at the start of the cycle (count + takes, `countBeforePop`); the generic `TimedFIFO` after the
     pop. The word class is what runs.
   - `WordLatch`: re-putting the registered value with nothing pending is a no-op (no put noted). **Owner:** behaviour must
     equal `ref`; the debug "driven twice" check may be stricter than `ref` only if no unit relies on re-putting the same
     value. PORTS.md says which, from the call sites.
   - `WordBackpressureDelayLine` (a delay line whose producer reserves the landing) is not in WORKER29's set. **Owner:**
     documented in PORTS.md as a reserved `Link`; implemented only if cheap (the memory port's landing is M2).
   - "Credit `Fifo`": `WordBackpressureFIFO` (unused by the design) or `RequestBus`'s credited receive queues; pinned down
     before it is written.
2. **The API headers**, `sim/src/sim.h` (split into `config.h` / `ports.h` if that reads better):
   - a unit type is a plain struct: port handles as members, its own state, `step(Ctx&)` returning its wake (none, next
     cycle, at cycle t); reading inputs and writing outputs as plain code, no commits, no stepping order, no reach into
     another unit;
   - **a port handle is a small index into its kind's pool** (a destination id plus a slot, as in `sim/README.md`), never a
     pointer to a port object (**owner**);
   - **a resolved route is a data value** (where a put's entry is appended: the local wheel or an outbox), so a put runs the
     same code either way: no branch on the domain, no virtual call (**owner**); unit code never changes when a port
     crosses domains;
   - declaration through a configuration-time builder (any C++, D6); sizing, wiring and levels from it;
   - **run lists keyed by (type, form)**: a type may have a result-only form beside its cycle-accurate one, chosen per
     instance; dispatch stays one function pointer per run, generated by a template at registration (**owner**);
   - the phases defined by what each may read and write; levels from the Wires;
   - payloads untyped `uint64_t` words; word width per port a runtime value until W6's findings say otherwise.
3. **Two sketches**, compiled by `make check`, not run: a FIFO consumer, and the controller's surroundings. The controller
   sketch starts from PLAN-M1 item 8 and **switches to W2's `AUDIT.md` as soon as it lands on `sim-w2`** (**owner**): its
   same-cycle queries (the WFI wake term, the interrupt term, the request FIFO stall) are the real test of the phases and
   Wires. Line counts against `ref`'s equivalents.
4. **W6's findings** (the link word, fixed or runtime widths, 128-bit values in units), through the integrator, folded in.
   Then stop for review.

The conformance driver's **script format** (puts, takes, peeks per cycle, and what is observed) is written in stage 1, so
PORTS.md and the stage 2 tests share one vocabulary.

### Stage 2: the implementation (on the owner's go-ahead after his review)

PLAN-M1 items 3-6, local forms only, with the acceptance tests of PLAN-M1 (W3); the Java driver in `sim/tests/ref/` under
my own names (`PortConf*.java`). `WORKER3/REPORT.md` at the end.

**Added to the acceptance (integrator, 2026-11-07): step-all.** A debug mode that steps every instance every cycle; its
digest must equal the ready-driven run's (WORKER28's proof for idle-skipping), on the synthetic graphs and on the sketched
units once they run. It is the safety net for explicit wakes: a missed `next()` otherwise fails silently.

### Stage 2: the order (2026-11-07; the owner's go-ahead, D20)

In working increments, each pushed with `make check` clean on both compilers, so W5 can integrate from increment 2:

1. **The build (D18) and the fixtures.** `make` and the timing builds with clang `-O2`; `make check` gcc and clang,
   debug and release; `make conformance` (the Java matrix against the C++ ports). The conformance fixtures
   (`sim/tests/portconf/`, seed 1 of each configuration at 2,000 cycles, gzipped, a provenance header) and
   `portconf.sh --check`.
2. **The engine, local and single-threaded: Link, Fifo, Hold.** `Config` (staging, `Ports`, the type registry, wiring,
   build), the sizing pass carved from W1's blocks, routes, the (kind, width) wheels and their delivery loops, the run
   sets by (level, type, form), wakes (the wheel and the far list), `Ctx`, `Sim::run`, the digest, `Sizes`. Tests: sizes
   against hand-derived ones; full fatal; an idle unit never stepped; a 10^9-cycle sleep one wake; no allocation in the
   loop. **W5 can integrate from here.**
3. **The rest of the kinds**: `Reg`, `Wire` (levels, loops rejected with names), `RegFile`, `Count`, credit `Fifo`; the
   reserved `Link` if cheap. W5's controller needs Wire and RegFile: they come second only because Link/Fifo/Hold carry
   the wheel.
4. **Port conformance, the C++ side**: the script reader and a producer/consumer pair per kind; observations equal to the
   fixtures in `make check`, to the full matrix in `make conformance`.
5. **The debug checks**: shuffle within a level; type-major against instance-major; wire discipline; step-all; the
   missed-wake detector and the end-of-run report (D20). Digests stable across seeds and orders.
6. **The result-only form** and the toy unit's two forms agreeing.
7. **Footprint and the report**: `Sizes` by structure, type and chip checked; what fixed-width copies recovered of W6's
   11-13%; `REPORT.md`.

**Acceptance** (PLAN-M1, W3, with D20's additions): port conformance against `ref` (fixtures in `make check`, 10^5 cycles
per configuration in `make conformance`); sizes equal to hand-derived ones; overflow fatal; loops rejected with names;
digests stable across shuffle seeds and both orders; wire-discipline violations fatal; an idle unit never stepped; a
10^9-cycle sleep one wake; no allocation in the loop; a toy unit's two forms agree; **step-all's digest equal to the
ready-driven run's; the missed-wake detector fatal on a unit that forgets `next()` (a death test) and silent on units
blocked on a credit or a refusal; the end-of-run report listing a planted sleeper.**

**API stability (integrator, 2026-11-07):** W5 writes against `sim.h` and `config.h` from now; any change to an agreed
operation goes to the integrator first. Additions this round, before W5 depends on them: `Options::missed_wake_after`,
`missed_wake_fatal`, `Sim::sleepers()` (D20).

### W1's `base/`

No change foreseen; any proposal goes here first.

## Stage 1: state (2026-11-07)

| deliverable | file | state |
|---|---|---|
| the port semantics | `sim/src/PORTS.md` | draft |
| the API | `sim/src/sim.h` (the unit author's view), `sim/src/config.h` (building a machine) | draft, declarations only |
| sketch 1, a FIFO consumer | `sim/src/sketch/write_engine.cpp` (`ref`'s WriteEngine) | compiles (gcc and clang, `make check`) |
| sketch 2, the controller's surroundings | `sim/src/sketch/controller.cpp` (core, register block subset, test bench) | compiles; **revised against `sim/tests/AUDIT-M1.md` and `TRACE-FORMAT.md`** (main, W2 merged) |
| W6's findings | `sim.h`, `config.h` | folded in (D15-D18; below) |
| sketch 3, mixed fidelity | `sim/src/sketch/tile_builder.cpp` | compiles |

`sim/Makefile`: one rule added (the sketches are compiled in every build, never linked or run).

### What PORTS.md found

- **The machine runs three bus classes**: `WordDelayLine`, `WordFIFO`, `WordHold`. `WordLatch`, `RegisterBlock` and the
  backpressure classes are instantiated only by `BusTest`; `Reg`, `RegFile`, `Wire` and `Count` follow the timing rules
  `TileControl`, `EventCounts`, `RequestPort` and `Chip` implement by hand.
- **Fifo full**: `WordFIFO`'s start-of-cycle rule (the generic `TimedFIFO` judges after the pop; the word class wins).
- **Reg's same-value no-op** (owner's note 1): no unit puts on a `WordLatch`, so none relies on it; the framework's `Reg`
  is stricter in debug (a second put in a cycle is fatal) and equal in behaviour.
- **Credit Fifo**: `WordBackpressureFIFO` is unused; the machine's credited queue is the request tree's receive queues
  (credit back at t + 1). Both reduce to a producer-side credit count, returned through the wheel.
- **The request FIFO's stall** (`RequestPort.canPush`: room, or freed by the bus in the same cycle) is a `Wire` from the
  bus, not a FIFO rule; in M1 the test bench drives it (W2's observer now logs REQ-FREED, which is this wire).
- **GO** is a pulse visible at one cycle: a width-0 `Link`, not a `Reg`.
- **Overflow timing**: `ref` counts an overflow at the FIFO's first touch after the put, which is t + 1 for every FIFO the
  machine runs (its consumer polls every cycle it is awake); the framework counts it at delivery, t + 1.

### Design choices for review (sim.h, config.h)

1. **Wakes are explicit**: a unit is stepped on a delivery, a change of a Wire or RegFile it reads, or a wake it asked
   for. Data left in an input does not re-step it; the unit returns `next()` if it wants another look. The alternative
   (re-step while any input holds data) is simpler for authors and costs a step per blocked unit per cycle.
2. **The shifted end of cycle**: `TileControl.endCycle(t)` becomes a level-0 step at t + 1 fed by Links of latency 1, which
   drives the Wires and the RegFile the core reads at level 1. No commit phase for unit logic; the cycle is deliver, run
   by level, end.
3. **A Wire is a persistent level**, changed only by its producer's step; a change wakes its readers in the same cycle.
   An idle producer need not run to keep its wire driven.
4. **A Hold's refusal wakes its producer** (`ref`'s producers poll every cycle; the wake makes polling unnecessary).
5. **One level per (type, form)**: the largest of its instances.
6. **Handles are patched after the build**: units are staged at configuration, moved into one arena per type, and their
   handles rewritten to final pool indices (ports numbered consumer type-major). So a unit type must be movable and hold no
   pointer into itself.
7. **One wheel per (kind, width)**: delivery is a tight loop per kind, the route selects the wheel (no branch on kind or
   domain in a put), and W6's two-or-three-word link word is a width.
8. **Every unit is stepped at the first cycle**, to drive its initial outputs.

### Line counts (code lines, no comments or blanks)

- **The write engine**: `ref` 84 (of which about 15 are S1 accounting, the log and stats resets, left out of the sketch);
  the sketch 76 with its local link-word helpers, **53 for the unit itself**. The gain is small for this unit: `ref`'s write
  engine was already a clean consumer of three buses. What disappears is outside it: the per-cycle polling, the commit
  rules, and `Tile.java`'s construction of its buses.
- **The controller's surroundings**: 124 lines for the three units and their configuration (core outline 51, register block
  subset 29, test bench 20). No fair `ref` equivalent yet: the same couplings are spread over `Controller.java`,
  `TileControl.java` and `RequestPort.java`. W5 reports the full controller against `Controller.java` (444).

### The integrator's notes on stage 1 (2026-11-07)

1. The three choices (explicit wakes, the shifted end of cycle, Wires as persistent levels): provisionally yes; they go
   to the owner with the API.
2. Step-all, a fourth debug check: in `sim.h` (DEBUG CHECKS), `config.h` (`Options::step_all`) and stage 2's acceptance.
   The rule it checks, stated in `sim.h`: a step the unit did not ask for, with nothing delivered, changes nothing but what
   the skipped cycles would have; both sketches keep it (the core counts a stalled or WFI cycle the same either way).
3. A Count wakes nobody; a count whose change must step a unit is an event (PORTS.md 8; M2's write counters).
4. clang 18.1 installed locally: `make check` clean with gcc and clang, debug and release.
5. The test bench and the request FIFO: it keeps the FIFO's occupancy from the core's pushes (a `Link(1)` from the core)
   and the trace's REQ-FREED events, and drives `req_free` = occupancy < depth or freed at t (`RequestPort.canPush`).
   **REQ-REFUSED is not an input**: driving the refusal from the trace would make it an input, and the comparison could
   no longer catch a core that stalls on the wrong cycle; modelled this way it is an output, checked line for line. The
   trace's GO and CMD lines at s mean "seen from s", so the bench puts them at s - 1. To check against AUDIT.md.

### For sketch 2's revision against AUDIT.md (integrator, 2026-11-07; done, below)

- **MEMW**: port-B writes into the traced tile's memories, as a bench `Link` carrying address and value into the core
  (visible from t + 1, as W2's observer writes them).
- **GO or CMD visible from cycle 0** can't be put at -1. In `ref` it can't happen: the first cycle is 1 (`Chip.step` runs
  `++cycle`), the earliest `endCycle` is 1, so GO and CMD are seen from 2 at the earliest (the goldens' first CMD is at
  8). So **the engine numbers its first cycle 1, as `ref` and the traces do**, and the bench refuses at configuration
  (fatal, naming the event) any event that would need a put before the first cycle.

### Sketch 2 against AUDIT-M1.md (2026-11-07)

Main merged into `sim-w3` (W2's audit, trace format, goldens). The integrator's five checks:

1. **`Controller.runCycle`'s order** (AUDIT 0): mcycle, halted, the interrupt (only when `stall == 0`), WFI's wake (going
   on in the same cycle), the stall, execute. The sketch sleeps through a branch or multiply stall (an early wake finds it
   still stalled, so no interrupt is taken during one), and judges the interrupt before WFI's wake. A register load's first
   cycle and a refused `REQUEST_SEND` are not stalls: the instruction returns without retiring and is re-executed at
   t + 1 after the interrupt check, so an interrupt preempts it with `mepc` = that instruction.
2. **`errorsAt`**: the core puts the error on `bus` at t (an event, so it steps the block: PORTS.md 8); the block sets the
   condition in `endCycle(t)` at level 0 of t + 1 and drives `irq`; the core sees it at t + 1, not before.
3. **Memories and MEMW**: a MEMW line at C is a port-B write at C - 1; the bench puts it at C - 1 on `Link(1)` `memw`;
   delivered at C, the core applies it at the start of its step at C, before executing: a load at C sees it, as the
   start-of-cycle read rule says. A 64- or 128-bit packet writes up to four words in one cycle and a Link takes one put a
   cycle, so the bench packs a cycle's words into one width-3 entry (address and count, then four 32-bit values in two
   words); configuration checks they are consecutive and at most four.
4. **The request FIFO** (depth 128, a free at t before the push at t): the count is the tile's (RequestPort), so it moved
   from the bench into the register block. `canPush(t)` = `!req_busy || req_freed`: `req_busy` (the block's Wire,
   STATUS [6], the count at the end of t - 1 at or above the depth) and `req_freed` (the bench's Wire, REQ-FREED at t).
   The block's `endCycle(t)` counts t's free (a `Link(1)` event from the bench) and t's push (the store on `bus`), then
   busy, then EVENTS [11] on its fall. The bench replays exactly AUDIT 5's four kinds: MEMW, GO, CMD, REQ-FREED.
5. **`endCycle`'s same-cycle cases**: the block applies GO, then the store, then the landing (TileControl.java:227-258),
   so a `PULSE_DONE` in GO's cycle leaves `done` and the gate set, and a command landing in GO's cycle sets `landed` and
   EVENTS [9] after GO cleared `landed`. The consume clears only the bits the load returned under the mask, so a GO latched
   in the same `endCycle` survives.

**Did the API need a change?** No change to any operation. Two clarifications: cycles are numbered from 1, as in `ref`
and the traces (`sim.h`); and a unit stepped by a delivery it didn't ask for (the core receiving MEMW while in WFI or a
stall) handles it and sleeps on, which is the step-all rule already stated. The one-put-per-cycle rule shaped MEMW into one
packed entry per packet: no API change. Lines: core 64, register block 45, bench 24 (166 with the configuration).

### The conformance driver, Java side (2026-11-07; the integrator's go-ahead, before the owner's review)

`sim/tests/ref/PortConfGen.java` (the seeded script), `PortConfDriver.java` (runs it on `ref`'s `WordDelayLine`,
`WordFIFO`, `WordBackpressureFIFO`, `WordHold`, `WordLatch`, `WordBackpressureDelayLine`), `portconf.sh` (builds `ref`
unchanged plus the two, runs the matrix, checks that `ref` never raises and that both are deterministic, writes the
observations' sha256s). The format is PORTS.md section 10. Full matrix: 51 runs × 10^5 cycles, 70 s, 346 MB of scripts and
observations (not committed).

For stage 2 (proposal): `make check` can't need Java, so a small fixture set (each configuration at seed 1, 2,000 cycles,
script and `ref`'s observations, about 2.5 MB) is committed for the C++ test, and the full matrix runs as `make conformance`
(Java present) and on the runner. The C++ side is not started (the owner's go-ahead).

**Approved for stage 2 (integrator, 2026-11-07)**, with two conditions:
1. `portconf.sh --check`: regenerate the fixtures from `ref` and compare with the committed ones (as W2's `regen.sh
   --check`), so they can't drift from `ref` unnoticed.
2. Each fixture records at its top its configuration, its generator seed and cycles, and the `ref` commit it was made
   from. Compressed (gzip) if git handles that better: decided when they are made.

### The integrator's review and D15-D18 (2026-11-07)

1. D17: three-word link word; 128-bit values inline as two words; `values()` only for payloads above 128 bits (`sim.h`,
   PAYLOADS AND WIDTHS, and `Ctx::values`).
2. D16: widths runtime in declarations; a wheel and a delivery loop per (kind, width), templated on the word count
   (0-3); puts fixed-arity, so copies are fixed-size (`sim.h`, `config.h`'s Route). Stage 2 measures how much of W6's
   11-13% this recovers.
3. D18: planned for stage 2's Makefile (above).
4. Mixed fidelity: `config.h` (MIXED FIDELITY) and sketch 3, `tile_builder.cpp`: a tile is a function returning its
   external handles (`TileIO`); a high-level tile adds one unit with the same ports; it emits each result at t + k with a
   wake and an ordinary put (a queue of results in flight in its own state). No new mechanism. `Config::add` now takes a
   `std::string` name (64 tiles need unique names).
5. Footprint: `Sizes` by structure, by unit type and per chip (`Config::chip` scopes); the wheel's sizing rule and its
   cost stated, M2's two options named, nothing changed.
6. Unit state: `static_assert` move-constructible and not polymorphic at `add`; `sim.h` states that a unit holds no
   pointer into itself or another unit (permanent state allocated apart is fine); stale self-pointers fault under ASan
   (the staging storage is poisoned after the move, debug).
7. The summary for the owner: at the top of this file.

`make check` clean on gcc and clang, debug and release (the three sketches compiled in every build).

### Stage 2, increment 2: the engine, local, Link/Fifo/Hold (2026-11-07)

`src/engine.h` (the internals: port pools, routes, wheels, run sets; `Ctx`'s operations inline; the delivery loops per
(kind, width)), `src/engine.cpp` (the cycle: deliver, run by level, end; the far-wake heap; fast-forward when nothing is
in flight or ready), `src/config.cpp` (staging, `Ports`, wiring, the build: order, numbering, wheels, sizing carved from
one block, unit arenas and handle patching, `Sizes`; `Sim`). `tests/test_engine.cpp`, 16 tests: a link word's latency;
WordFIFO's start-of-cycle full (depth 1, a put and a take every cycle: half dropped); a hold's refusals and retries in
order; an idle unit stepped only at cycle 1; a 10^9-cycle sleep, two steps; no allocation in 10^4 busy cycles (W1's
hook); sizes against hand-derived ones; the digest; the end-of-run report; death tests (two puts, a link not taken, a
wake in the past, an unwired port, a unit of the wrong type, widths). ASan found one bug on the way (name strings in a
reallocating vector, fixed).

Engine choices below the API: far wakes in an indexed heap (each instance at most once, so a step's wake replaces its
last: no stale entries); `next()` in a per-run "next" bitmap swapped in at the end of the cycle; with nothing in flight
and nothing ready, the clock jumps to the next far wake. A unit's digest is its bytes unless its type defines
`digest()` (optional, additive: flagged to the integrator).

### Stage 2, increment 3: every port kind (2026-11-07)

Reg (a change wakes its readers; the registered value again is a no-op), Wire (a persistent level; a change steps its
readers in the same cycle at higher levels; debug: written twice, written after a read), RegFile (owner writes, readers
at later levels), Count (one per connected set of producers and readers; `now` and `through_last`; a Fifo's overflows
counted at t + 1), the credit Fifo (credits on the producer's side, a take's return delivered at t + 1 through its own
wheel). Levels from the Wires and RegFiles, a loop rejected with its units named, one level per (type, form) checked.
`tests/test_kinds.cpp`, 12 tests. The reserved Link stays M2's (PORTS.md 2a).

From the integrator's review of increment 2: **a unit's digest** is `digest()` if defined, nothing for an empty type,
else its bytes only when `std::has_unique_object_representations_v<T>` (static_assert at `add`): padding or pointers mean
`digest()`, made one line by `hash_fields` (sim.h). **The missed-wake threshold per unit**:
`Config::missed_wake_after(unit, cycles)` (stored; the detector is increment 5). Both additive.

### For M2 (integrator, 2026-11-07)

- **One level per (type, form) refuses a Wire chain through units of the same type** (build names them: `levels: 'r1'
  ... drives a wire read by 'r2'`). Nothing in M1 has one; M2 may (identical pipeline stages joined by Wires). If one
  appears, **split a type's run by level** (runs keyed by (level, type, form) already: let a type's instances sit at
  several levels) rather than force the unit's design. Not changed now.
- **The wheel's worst-case sizing** (config.h): the two options named there, decided on M2's numbers.

### Wide payloads by reference (integrator, 2026-11-07; PLAN-M1, W3)

`values()` is gone; payloads wider than three words travel by reference through **slot pools per size class**:
- `base/value_pool.h`: W1's ValuePool generalised to `SlotPool` (N-word slots, lowest free index, full fatal naming the
  pool and its slot size); `ValuePool` stays as its 2-word view, W1's tests unchanged, two added for a 16-word class.
- `sim.h`: `Ports::by_ref(handle, slot_words, held = 0)` on both ends of a Link, Fifo, credit Fifo or Hold (checked
  equal); `Ctx::alloc(out)`, `slot(port, i)`, `free(port, i)`; word 0 of the payload is the index. A Fifo overflow frees
  the dropped slot; a refused Hold put keeps it for the retry. Reg and Wire carry no references (a replaced value would
  leak).
- `config.h`: each class's pool is sized from its ports: L + 2 per Link, depth + 1 per Fifo, depth per credit Fifo, 2
  per Hold, plus what either end declares it holds. Sizes reports `slot pool wN`.
- Thread crossings (W4): the route carries its class, so the outbox merge can copy the slot and rewrite word 0.
- `tests/test_wide.cpp`, 5 tests: a 16-word class sized as derived (5 slots for a depth-4 Fifo; 7 for a latency-3 Link
  with 2 held), lines intact and freed, overflows freeing their slots, allocating beyond the declaration fatal naming the
  class, mismatched declarations refused. M1 doesn't use it.

### The leak check (integrator, 2026-11-07)

Debug builds, at the end of every `Sim::run` and optionally every `Options::leak_check_every` cycles: per size class, the
slots in use must not exceed those on its ports (visible in a ring or a hold, or an entry in a wheel) plus what units may
hold (their declared `held`, and one per by-reference Hold for a refused word awaiting its retry). Fatal, naming the class
and the count (`slot pool 'w16' leaks at cycle 10: 1 slots in use, 0 on its ports, at most 0 held by units: 1 allocated
and never put or freed`). Two tests: a planted leak; none reported for lines in flight, queued, overflowing or held.

### Stage 2, increment 4: port conformance against ref (2026-11-07)

`tests/portconf.h` (the script parser; one unit owning both ends of the port under test, so the ops run in their written
order as in the Java driver; the observer queries through `TestingAccess`), `tests/test_portconf.cpp` (every committed
fixture, in `make check`), `tests/portconf_main.cpp` (the tool) and `make conformance` (the Java matrix, then the tool on
every script, compared file by file). **All 14 configurations of the four kinds M1 builds conform: the fixtures in
`make check`, and the full matrix, 42 scripts of 10^5 cycles (three seeds), equal to ref line for line.** The reserved
link's 3 configurations are reported as M2's. The comparison has teeth: planting the generic TimedFIFO's after-pop rule
fails three fifo fixtures at their first differing cycle.

Two fixes the driver found before it ran (both to match `ref`, both additive or a correction to the documented meaning):
`Ctx::credits` deducted the unit's own put of the same cycle, unlike `canPut` (and unlike its documented "room as of
the start of t"): it now adds it back; and `put(RegOut, a, b, c)`, the three-word Reg put the other kinds have.

### E1 and E2, for W4 (integrator, 2026-11-07; before increment 5)

- **E1, the type registry per configuration.** The process-wide part is only each type's pure description
  (`detail::type_ops<T>()`: a function-local static `TypeInfo`, name, size, move, destroy, run and digest functions,
  initialised thread-safely by the language). Each `Config` numbers the types it uses itself, in the order they are first
  added to it (`Config::Impl::types`); the `Sim` keeps that list. So ids are deterministic per machine, whatever other
  threads build. `Sim::type_of(unit)` reads an id (an observer, additive). `tests/test_registry.cpp`: ids in each
  Config's first-add order (ABC, CAB, BCA), and three threads building 200 machines each at once, every id as expected;
  also clean under TSan (built locally, not in `make check`).
- **E2, the internals in a header.** `Config::Impl` (declarations, staging, the type list) and `Sim::Impl` (the engine,
  the unit stores, names) moved to `src/config_impl.h`, with `kind_name` and the handle shapes; `config.cpp` keeps the
  behaviour. A mechanical move.
- `-pthread` added to the build flags (the test's threads; W4's).
- **For W4:** W1's allocation hook (`alloc_guard.cpp`) replaces `operator new`, which TSan's runtime also defines, so a
  TSan build doesn't link with it. For this test I linked a stub instead; W4's TSan builds need a decision there (the
  hook off under TSan, or the wrap form for new as well).

### Stage 2, increment 5: the debug checks (2026-11-07)

Done first, before the detector, after W5 broke a unit on purpose and it passed all six option sets (they were accepted
and ignored). **Every option is now implemented or refused at `build()`** (`option threads = 4 not implemented`); the rule
holds for any future option (config.h).
- **Shuffle and instance-major** (`Engine::run_level_debug`): a level's ready instances collected, ordered by add order
  (instance-major) or by run (type-major), shuffled with a seeded splitmix64 if asked, and stepped one by one through a
  per-type step function (`detail::step_one`). The fast path is untouched.
- **Step-all**: every instance marked after delivery, every cycle; no fast-forward.
- **The missed-wake detector** (debug): a unit becomes a suspect when a step leaves it asleep with data waiting; at the end
  of each cycle each suspect is checked against D20's rule (a delivery to its inputs in a wheel, a credit output with no
  credit left, a Hold put this cycle) and reported after its threshold, naming the unit and the ports. Per-unit
  thresholds (`Config::missed_wake_after`); fatal or report-and-go-on.
- **`run_until(t)` runs through cycle t inclusive**; `run(n)` the next n cycles (config.h). W5's harness calls `run(n)`,
  unaffected; its comment "(run_until(t) stops before t)" in `tests/golden_run.cpp:317` is now stale (W5's file).
- `tests/test_debug.cpp`, 10 tests: W4's synthetic chip digests the same under seven option sets; step-all catches a
  forgotten `next()` (and the corrected unit agrees); the detector names it (death test); instance-major and the seeds
  catch an illegal shared global; the detector is silent on a credit-blocked and a refusal-blocked relay; a per-unit
  threshold covers a wait on a Wire, which without it is flagged; report-and-go-on; `run_until` inclusive; `threads`
  refused. **W5's `golden_engine_debug_checks` now really runs its six option sets: all 67 goldens match under each.**

Two findings on the way. The rule's credit case was too narrow: a producer blocked on credits sleeps while the consumer
holds them, before any return is in a wheel; "a credit output with no credit left" is the right test (a consumer that
never takes is a deadlock, not a missed wake). And the detector caught a real bug in my own test unit: **a Hold's
acceptance wakes no one**, so a Hold producer with more data waiting must look again next cycle (`ref`'s WriteEngine
idiom); only the cycle of a put is covered by the refusal that may come.

Bug found and fixed: the slot pools' names went into the per-declaration name list before the declarations, shifting
every ring's name when a size class existed (names only, no behaviour); they now have their own list.

Still to do for increment 5: merge main once W4's E5 digest is there and re-run the comparisons with it.

### The performance pass (planned, after increment 5)

Benchmarks: W5's `bench_controller` (Quest 127: 20.6 ns per unit step, about 13 of them the engine's: `Engine::cycle`
33%, dispatch 15%, `after` 7%, `run_until` 5%, the heap 3%; ~390 instructions per cycle for 3 wheels and 3 runs) and W4's
framework mesh (speed per mesh-cycle; 228 KB per mesh after W4's queue sizing, against W6's hand-rolled 127 KB). Measured
on the runner (perf stat and record, P-cores, clang release), each change guarded by digests, the conformance matrix and
W5's goldens, and re-measured.

**Speed: make the per-cycle cost scale with activity, not structure.**
1. Delivery visits only non-empty buckets: a per-bucket-slot mask of wheels with entries, set by the put (one OR), read
   by the cycle (one word), instead of testing every wheel's bucket.
2. The run phase visits only runs with ready instances (a mask or list of non-empty runs set by `mark`), and the end of
   the cycle swaps only runs with next-cycle requests; `idle()` reads one global ready count instead of every run.
3. Near wakes on a short wheel (e.g. 16 cycles) of per-run bitmaps, as `next()` already is: every 1-2 cycle stall (a
   multiply, a taken branch) avoids the heap, which keeps only far wakes.
4. `after`'s common case (a `next()` or `none` with nothing in the heap) on a straight path; the step counters kept (cheap)
   or moved to stats.
5. Dispatch: run sets iterated by their non-zero words only (a summary word for big runs).

**Footprint (D15): W4's breakdown first, then:**
6. Port headers: `LinkPort`/`FifoPort` carry a whole `Ring` (pointer, width, depth, head, count, name); a ring of a known
   width and depth per port could keep only head and count, the rest in the type or the kind's table.
7. Rings rounded to whole cache lines each: small rings of one domain packed together (one thread owns them; W1's
   one-structure-per-line convention guards against false sharing, which can't happen inside a domain): a W1 convention
   change, to propose.
8. The wheels' worst-case sizing (M2's flag, config.h): per (kind, width, latency) wheels, Σ L words per output.
9. Per-instance arrays (seven, each line-rounded) and the debug-only ones (watches, suspects, scratch): the debug ones
   carved only in debug builds.

## Log

- 2026-11-07: plan agreed (owner's go-ahead with five notes). W2's audit not yet on `sim-w2`; W6 not started.
- 2026-11-07: stage 1 drafts pushed (PORTS.md, sim.h, config.h, two sketches). Waiting for W2's AUDIT.md (sketch 2) and
  W6's findings (through the integrator) before the review.
- 2026-11-07: the integrator's five notes folded in (step-all, Counts as events, clang, the bench from REQ-FREED).
- 2026-11-07: main merged (W2 closed); sketch 2 revised against AUDIT-M1.md and TRACE-FORMAT.md. Waiting for W6's report.
- 2026-11-07: the conformance driver's Java side (PortConfGen, PortConfDriver, portconf.sh); the full matrix runs clean.
- 2026-11-07: main merged (W6, D15-D18); the integrator's review folded in (1-7). Ready for the owner's review.
- 2026-11-07: stage 2 (owner's go-ahead, D20). Increment 1 (the build, D18; fixtures and --check) and increment 2 (the
  engine with Link, Fifo, Hold) landed.
- 2026-11-07: increment 3 (Reg, Wire, RegFile, Count, credit Fifo; levels) landed: W5 can integrate on every kind.
- 2026-11-07: wide payloads by reference (SlotPool, size classes; values() replaced); the M2 notes.
- 2026-11-07: the leak check; increment 4 (port conformance, the C++ side): every M1 kind equal to ref over the full
  matrix.
- 2026-11-07: E1 (the type registry per configuration) and E2 (config_impl.h) for W4, before increment 5.
- 2026-11-07: increment 5 (options refused or implemented; shuffle, instance-major, step-all; the missed-wake detector;
  run_until inclusive); the performance pass planned.
