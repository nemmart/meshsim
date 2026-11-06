# WORKER28 — notes (P1: full-detail simulation at full scale)

Branch for the measurements: `sim-idle-profile` (v1.3-tracer 515c11a plus measurement-only code, never merged). The
implementation is on `sim-idle-skip`, made from v1.3-tracer's tip 515c11a (owner, 2026-11-06: stage 1 now, without waiting
for B1; merged forward when B1 lands, and the proof rerun). These notes live on `sim-idle-skip` from now on.

## 1. The profile (step 1)

**Tools** (on `sim-idle-profile`; neither changes a log: the quick suites CcCoresTest -1/4, HooksTest, CondTest 4, StageB
synthetic, TracerTest and CtrlTest give identical logs with and without each, wall times aside):

- `-Dmeshsim.probe=FILE` (`src/Probe.java`): for every tile and C&C unit, every cycle, a class: **idle** (core disabled,
  halted or in a WFI it didn't leave; nothing visible to the router, the engines or the peripheral), **router** (only the
  router has a word), **engines** (the engines, memories or peripheral busy, the core idle), **core** (the core executed,
  stalled or woke). Also the core's states, the idle cycles with a word *in flight* towards the unit (a timed wake, not a
  flag), the idle-run lengths, and per-2²⁰-cycle bins of the 64 tiles. It costs about as much again as the run.
- `-Dmeshsim.timeprobe=FILE` (the same class): nanoseconds per cycle by chip section (request bus, endpoints, the 64
  tiles, the CnC model, CcCores, the READY/error-line loop, the tail, `checkInvariants`) every cycle; per tile, by its
  class and by child (router … core, the slot, `ctl.endCycle`) 1 cycle in 16; **C&C 1-3 per child every cycle**, the
  slot's peripheral (the Tracer, the memory system under it) split from its DMA; the cores' step by state (the
  interpreter's share). `System.nanoTime()`'s own cost is calibrated and subtracted per section. About 5% overhead.
- `test/JfrFold.java`: a JFR recording's samples by unit (the innermost `runCycle`), top frame, and inclusive by class
  and by method (`Controller.step`: the interpreter executing).

**Runs:** Quest 113 (JFR on T2 segment 0 of both workloads, all 64 windows; the activity probe on the first 32; Stage B and
F on windows 0-15 of both, under JFR and the probe; ~25 min), 113b (the time probe on the same T2 windows; ~13 min) and
113c (Stage B and F under all three tools: 113 and 113b's Stage B runs failed at their start, the kit lacking
`tables/manifest.json`).

**113b, T2 segment 0, first 32 windows (10.5 M cycles EVM Zone, 11.4 M Cyclone; 540 and 585 s), µs per cycle:**

| section | EVM Zone | Cyclone |
|---|---|---|
| **the 64 mesh tiles** | **39.8** | **39.9** |
| CcCores (C&C 1-3 together) | 3.4 | 3.2 |
| `checkInvariants` | 1.8 | 1.8 |
| the READY and error-line loop | 1.2 | 1.2 |
| the request bus | 0.6 | 0.6 |
| the rest (endpoints, the CnC model, the tail) | 0.1 | 0.1 |
| **total** | **46.9** | **46.8** |

- **The mesh tiles are 85% of the time, and 98.4% of their tile-cycles are idle** (sampled: 41.3 M of 42.0 M EVM Zone,
  44.9 M of 45.6 M Cyclone; router only 0.4%, engines 1.0%, core 0.35%). An idle tile-cycle costs about 0.6 µs
  (39.8 µs / 64, from the unsampled cycles). The per-child split is small numbers after subtracting `nanoTime()`'s
  41-53 ns per section (some come out slightly negative, the calibration over-correcting), so it ranks the units
  rather than sizes them: the SRAM read queue with the injection point (~100 ns), `ctl.endCycle` (~90 ns) and the router
  (~35 ns) lead, the others near nothing. The sum of the children (40-230 ns) is well under the whole tile's 0.6 µs
  measured unsampled: most of an idle tile's cost is reaching its objects (64 tiles' graphs walked every cycle, out of
  cache), which is what the wheel removes.
- **C&C 3's tile is about 0.55 µs per cycle, not 11-25:** its router 47 ns, the read queue and injection 115, its core
  122 (in WFI 87% of the time; 129 ns per cycle executing), the slot 150 (**the Tracer and the memory system under it
  85-128 ns**, its DMA 18-22), `ctl.endCycle` 100. C&C 1 and C&C 2, in WFI almost throughout, are 0.52 and 0.41 µs. So
  the three C&C units are 3.4 µs per cycle in all, of which C&C 1 and 2 are pure idle cost.
- **The interpreter's share is small:** a core cycle that executes costs 89-129 ns (mesh cores 89-113, C&C 3 100-129);
  C&C 3 executes 1.0-1.1 M of the 10.5-11.4 M cycles, the mesh cores 0.3% of theirs. About 0.3% of T2's time.
**113, T2 segment 0** (JFR on all 64 windows, 1,052-1,221 s; the activity probe on the first 32; its Stage B runs failed
at the start, the kit lacking the tables' manifest: 113c redid them): **98.3% of tile-cycles idle** (router only 0.4%,
engines 0.9-1.0%, core 0.3%), and **the idle stretches are long: 85-90% of idle cycles are in runs of 2²⁰ cycles or more**
(56% in runs of 2²³), so a sleeping tile is woken rarely. JFR's top units are the idle polling again:
`TileControl.endCycle` 20%, the SRAM read queue 19%, the router 8%, the table store 8%, the replayer 5%; the interpreter
executing (`Controller.step`, inclusive) is below the fold's top 60 methods, under 0.5%.

**113d, the same probe on WORKER22's READY/GO barriers** (v1.3-tracer a326fbd, now B1 in main): **the analytic adjustment
holds.** Idle tile-cycles 98.3-98.4% as before; the cores now wait in WFI for the segment's GO (98.7%, halted 1.1%) where
before most had replayed early and halted (WFI 26-28%, halted 72-74%); the idle stretches the same (55% in runs of 2²³).
For stage 1 a WFI-waiting tile and a halted one sleep alike, so the gain is unchanged.

**113c, Stage B on windows 0-15 of both workloads** (stage B: tile 0 alone, 63 parked; stage F: the full chip;
197K cycles a window; the probe runs held every chip and ran out of heap after 12 windows, so their figures are over 12):

| µs per cycle (time probe) | F, full chip | B, tile 0 alone |
|---|---|---|
| the 64 tiles | 51.1-52.8 | 0.59-0.66 (tile 0) |
| `checkInvariants` | 2.18 | 1.00 (it still visits the 63 parked tiles) |
| READY and error-line loop | 1.0-1.1 | 0.55 (the same) |
| request bus | 0.77 | 0.39 |
| **total** | **55.1-56.9** | **2.5-2.6** |

- On the full chip **98.4% of tile-cycles are idle**, in runs of a whole window (2¹⁷ cycles and more); tile 0's core sits in
  WFI 85% of the time while its replayer runs (the "engines" class), executing 12%.
- In stage B the chip's own loops cost three times tile 0 itself: they visit all 64 tiles, parked or not. **Done (with
  stage 1 on):** the READY/error-line loop and `checkInvariants` skip parked tiles as they skip sleeping ones (a parked
  tile never steps, so its lines and counters never move); StageB synthetic, full and `-Dstageb.lone`, the same off and on.
- JFR (stage F dominates the samples): the SRAM read queue 22%, `TileControl.endCycle` 22%, the router 8%,
  `checkInvariants` 6%, the table store 6%: the same polling of empty units as on T2.

- **Estimate for stage 1** on T2's trace: skipping the idle tiles removes ~39 of the 40 µs; the line loop and the
  invariant checks fall to the tiles that stepped (~3 µs → near 0); C&C 1 and 2 sleep (~0.9 µs); left: C&C 3 (~0.55 µs,
  less with its idle units skipped), the request bus (0.6, to be made event-driven too), the few awake tiles. **About 1.5-3 µs
  per cycle against 47: 15-30×**, so a 21 M-cycle segment in roughly 30-60 s instead of 20 minutes. To be checked by
  113's activity probe (the idle runs' lengths) and by stage 1 itself.

**Before the runs (local, one slow core, so only proportions; superseded by 113b above where they differ):**

- 110h's Stage B smoke: stage F (the full chip) 8.1 s per window against stage B (tile 0 alone, 63 parked) 2.2 s, for
  the same 197K cycles: **41 against 11 µs per cycle, so 63 idle tiles cost about 30 µs per cycle** (0.48 µs per idle
  tile-cycle). T2 runs at about 58 µs per cycle (110e: 1,228 s for 21 M cycles).
- The time probe on StageB synthetic: an idle tile-cycle is about 0.5 µs, mostly the SRAM read queue polling its clients
  (with the injection point), `ctl.endCycle` (the GO queue, the counter-event edge detectors, `computeStatus`), the router
  polling its links and FIFOs, the table and HIST stores. `checkInvariants` (64 tiles × 13 counters compared every cycle)
  and the READY/error-line loop are another few µs per cycle at chip level.
- JFR on the same: `TileControl.advance` and `BusBase.enterSlow` (a bus's first touch in a cycle) at the top. That is
  polling empty buses: a bus is entered, committed and found empty every cycle whether or not anything was put.
- (The question 113b answered: is C&C 3's tile the 11-25 µs per cycle the totals suggested, against T1's ~0.4 µs for the
  Tracer's work? No: see above.)

## 2. Wake sources, pending work and the audit (step 2)

Each tile has these units, stepped in this order (Tile's `children`, then the slot, its end of cycle and the control's):
router, SRAM read queue + injection point (one unit: they share the token pool), traffic director, controller memories
(port B), big SRAM, write engine, address generator, table store, HIST store, read port, gather engine (or the stand-in
drain), the core; then the peripheral slot (the peripheral and its DMA), the slot's end of cycle, `TileControl.endCycle`.
A C&C unit is the same `Tile` with extra router inputs (the link from its tile, the host port, C&C 3's DMA port) and its
own peripheral (C&C 1: `Cnc1Peripheral`; C&C 2: a stub, the memory system sitting under the Tracer; C&C 3: the Tracer).

"Work" below is what makes a unit's next step necessary; "wakes" are the events that create work, each with the cycle
at which it does (the wheel's entry). Everything else a unit does in a cycle without work is a no-op, to be confirmed
unit by unit in the code reading of step 3 (the list marks what is already confirmed).

| unit | work in progress (stays awake) | wakes (at the cycle the input becomes visible) | per-cycle state to catch up |
|---|---|---|---|
| router | any of its FIFOs non-empty (incl. the loopback FIFO and, on tiles 0-2, the C&C ones) | a put on a link in (t + link_latency, 3), on the injection bus (t + intra_tile_latency, 1), on `fromCnc` (t + cc_link_latency, 1); a C&C unit's host port and DMA port (t + 1); a take by a downstream FIFO freeing room (it was blocked) | none (round-robin pointers move only when selecting) |
| SRAM read queue + injection point | reads outstanding (`reads`), a word in `readReturn` or `sendHold`, a client FIFO non-empty, a refused put to retry | a put by a client (the address generator's hold, the gather engine's hold, the read port's queue) at t + 1; an SRAM area's return (t + sram_read_latency); a send store (`sendHold`, t + 1); a token freed by the loopback (`loopbackLeft`, same-cycle call from the router: see the hazards) | the token pool's `advance(t)` (idempotent, recomputed from counts: no catch-up) |
| traffic director | the K delay line holds decrements (`inFlight() > 0`: a timed wake at its next decrement) | a put on the eject bus (t + 1) | none |
| controller memories | a word on port B | a put on port B (t + cm_latency, 2) | none |
| big SRAM | an area queue non-empty | a put on its read or write holding register (t + 1) | none |
| write engine | `count > 0`, the SRAM hold occupied, a table-store return pending | a put on its input FIFO (t + 1); the table store's return (t + ts_engine_latency) | none |
| address generator | `running` (and the pulse/arm latches) | TILE_PULSE store (t + 1); returns from the table store (t + L) | none (its drift and lateness counters move only while running) |
| table store, HIST store | `busy`, a request in their FIFOs or pipes | a put by the address generator or the write engine (t + L) | none |
| read port | requests queued or results outstanding | a read-port store (t + 1); a result's injection and acceptance (same-cycle calls from the injection point and the director) | none |
| gather engine | `busy(t)` (its queue, `left`, its hold, reads in flight) | a put on its queue by the director (t + 1) | none |
| core | executing, or `stall > 0` (a timed wake at t + stall), or a send refused (retry) | **halted:** only the reset. **WFI:** a change to the wake term: an EVENTS bit set by `TileControl.endCycle` (GO, command landed, a counter's done edge, counters done, run done, request room), a live event (the peripheral's service lines, a request pending), or ERRORS ≠ 0 with MIE (the hard interrupt) | `mcycle` (+1 every cycle while enabled), `cyclesHalted` or `cyclesWfi` (+1 per skipped cycle, by the state it slept in), `profCycles` (+1 while not halted with pc in the profiled range), so `mtime` too (derived from mcycle) |
| TileControl | a store pending (`pendT`), a consuming load's clear, the edge detectors with a changed input | **GO** as this tile sees it (`goQ`: t_store + go_latency + skew, a timed wake); command landed (port B's landing); a counter reaching 0 (the director's K line); the AG's done; the request port's room; the core's register stores (t + 1) | `computeStatus` is recomputed every cycle: recompute at wake (its inputs don't change while every unit is idle); the edge detectors' `prev` values hold (no input changes while asleep) |
| peripheral slot: replayer | `running`, inputs or landings queued (`settle` applies them by time), its output queue | a write landing (t + rp_latency, 2); PULSE_RUN / RUN_CONFIG stores (t + 1) | none: the model is lazily settled and `passCycles` counts only while running |
| peripheral slot: DMA | `busy` | PULSE_DMA (t + 1); its peripheral's output becoming ready (a same-cycle query: the peripheral's own wake covers it) | none |
| request port / request bus | the port's FIFO non-empty; the bus's beats in its hop pipeline (reqbus hops 3 cycles) | a REQUEST_SEND store (t + 1); a beat arriving at its target (t + 3 per hop) | none |
| endpoints (fifth ports), CnC model | an rx or send FIFO non-empty; the model's sequencer step not finished | a put on the FIFOs (t + 1); the model's own waits (`readyWaitCycles` counts while waiting: catch up or keep it awake) | `readyWaitCycles` |
| C&C 1's peripheral | the watchdog counting (a timed wake at its expiry), the UART shifting (timed by the divisor), a GO to issue | **READY** from each tile (`readyAt = t_edge + ready_latency`, a timed wake); the error lines (t + error_latency); UART RX bytes from the outside world; its own stores | the watchdog's count (a deadline, not a per-cycle decrement: compute, don't step) |
| C&C 3: the Tracer (+ memory system) | running, or draining its output queues, or a fill outstanding (`busyUntil`) | PULSE_RUN, PULSE_DMA, the state write, expansion and advice writes (t + 1 or the landing latency) | none known (`runCycles` counts only while running) |
| links | — | a put at t is visible at t + 3; the consumer's wake at t + 3 | — |

**Chip-wide lines and work.** GO: `CnC.goNet` (called by C&C 1's PULSE_GO) schedules every tile's GO at
t + go_latency + skew, the only place GO's latency is applied: each becomes a wheel entry for that tile. READY: a tile's
`ctl.endCycle` sets `readyPulseAt` on a rising edge of done AND command landed; the chip's line loop hands it to C&C 1's
peripheral, which applies ready_latency: a wake for C&C 1 at that cycle. The error lines: each tile's level, collected by
the same loop, delayed by error_latency into C&C 1's peripheral and the CnC model. RUN: `run_latency` is configured but
nothing in meshsim reads it (to confirm; spec 4.13 names it). The reset: every unit. The request bus runs first each
cycle and stays chip-serial. **The chip's own per-cycle loops** (READY and error lines over 64 tiles, `checkInvariants`'
13 counters × 64 tiles, the condition path) only ever change when a tile steps, so stage 1 runs them for the tiles that
stepped in the cycle, not all 64.

**Exactness hazards found so far** (each needs a test in step 4):

1. **Same-cycle direct calls between units**, not through timed buses: the router calls the token pool
   (`loopbackLeft`); the injection point calls the SRAM read queue (`takeForSend`, `injected`) and the read port
   (`injectedAt`); the director calls the read port (`acceptedAt`) and the slot; the core calls `ctl`, the slot and the
   request port; `ctl.endCycle` reads the director, the AG, cmem and the request port. Within a tile these are fine (the
   tile's units step in a fixed order). Each one is also a wake: the callee is woken by the call itself.
2. **Edge detectors** in `TileControl.endCycle` (`readyLevel`, `rdDonePrev`, `requestBusyPrev`, the counter qualifiers):
   correct across a skip only if their inputs can't change while the tile sleeps, which the wake list above must
   guarantee (every input's change is a wake of the tile).
3. **Lazily settled models** (the replayer, the Tracer): settle on wake; their queries are already idempotent at t.
4. **Harness reads of per-cycle counters** (`mcycle`, `cyclesWfi`, …; 1 site in SortTest, 1 in SplitRun, 3 in PhaseLog;
   TracerT2's fail-fast reads every unit's pc and counters): read through accessors that add the pending skipped cycles,
   so a sleeping tile reports the same numbers at any cycle.
5. **Violation and log order**: messages are appended in stepping order; with only the active units stepped, the
   order among them is unchanged, but stage 2 must merge per-thread logs in the serial order (below).
6. **The shuffle**: a skipped tile draws nothing (owner, 2026-11-06). Exactness is proved without a seed (skip on and off,
   cycle for cycle); with seeds, skip-on runs must give the same logs as unshuffled ones, which the convention requires.

### The audit: same-cycle reads and calls between units

Every place a unit reads or changes another unit's state within the cycle, other than through a bus's committed data.
Stage 1 and stage 2 by tile keep a tile's units in one thread in a fixed order, so these are safe there; the chip-level
and cross-tile ones are what stage 2 by tile must route through the barrier; for the by-type step (deliver, run, merge)
every one becomes data delivered in phase 1 (a credit, an occupancy snapshot) or a read of last cycle's committed state.

**Direct calls inside a tile** (from the code, `sim-idle-profile`):
- router → SRAM read queue: `tokens.loopbackLeft` (a loopback token freed by the eject selector);
- injection point → SRAM read queue: `loops`, `takeForSend` (a send waits for a token), `injected`; → read port:
  `injectedAt`;
- director → control: `ctl.pendingAdd` (a counter write in the same cycle is judged against count + N); → read port:
  `acceptedAt`, `outstanding`; → the slot: `periph.accept` (a landing, queued by time in the peripheral);
- gather engine → SRAM read queue: `inFlightOf`;
- table store → its client buses (`req.ready/peek/take`, delay lines), its memory;
- core → control: register `read`/`write`, `takeWake`, `errorsAt` (the interrupt), the send header and the read-port
  setup; → cmem: `loaded`, `stored`, `peek`, `poke`; → AG: `idle`, `runningForStatus`; → read port: `store`; → request port:
  `canPush`; → the send holding register: `put`, `refused`, `retry`; → the slot: `storeStalls` (UART_TX full);
- `TileControl` (its loads and stores, and `endCycle` after every unit): the director's counters (`counterAtStart`,
  `remaining`, `resetCounters`), the AG (`doneForStatus`, `pulse`), the gather engine (`busy`), the read port (`free`),
  the request port (`busy`, `pending`, `get`, `receive`, `send`, `setW`), `sendHold.isEmpty`, the slot (`liveEvents`,
  `liveErrors`, `read`, `write`), the big SRAM (`newPhase`), the core's pc (the boot ROM's command check);
- the slot ↔ its peripheral and DMA (`outReady`, `outTake`, the DMA port's `canPut`); the Tracer ↔ its memory system
  (one unit: they stay together in every partition).

**Bus reads that see the other side's current cycle:** a producer's `canPut(t)` on a FIFO reads the consumer's count
before the cycle's pop (the FIFO rule; order-independent, but a read of shared state); `WordHold.refused(t)` reads the
consumer's refusal of t − 1; every `ready`/`peek`/`isEmpty` runs the bus's lazy commit (`BusBase.enter`), which writes
the bus. Within one thread these are safe; across threads the lazy commit is the race (the owner's point), which is
why by-type needs the delivery queues.

**Across tiles and chip-wide:** the links and the C&C links (shared delay lines: stage 2 by tile routes their puts through
per-thread outboxes merged at the barrier); **GO**: C&C 1's peripheral (`pulseGo` → `CnC.goNet`) writes every tile's GO
queue during C&C 1's step (a cross-tile write: through the outbox, due at t + go_latency ≥ t + 1); the CnC model's
`ctl.scheduleGo` the same; READY and the error lines are read after the cycle by the chip's line loop (serial, after the
barrier); the request bus runs first and serially, reading and writing every port; `checkInvariants`, `scanOverflows`,
the logs, S1 and PhaseLog are chip-serial after the barrier.

## 2b. Stage 1 as built (`sim-idle-skip`, 613e81b and after)

- **Sleeping.** At the end of every cycle each awake unit (tiles 0-63, C&C 1-3 as 64-66) is checked (`Tile.canSleep`):
  its core did nothing at t (halted, or in a WFI it didn't leave: `Controller.idleAt == t`) and won't wake at t + 1
  (no interrupt, the wake term false), its peripherals and DMAs idle (`Peripheral.idle`: the replayer when not running
  with nothing queued; C&C 2's stub always; C&C 1's with no watchdog, UART or error-line change pending; the Tracer
  never, for now), its request port empty, every queue, bus and engine empty (`Tile.quiescent` plus the write engine,
  the AG's pulse latch, the gather engine), nothing in flight on its links or fifth port. Then it sleeps and isn't
  stepped; its next GO and its peripheral's next line change are scheduled as wakes.
- **Waking**, at the cycle the input becomes visible: a put on any bus into the unit (the links, the C&C links, the host
  and fifth-port FIFOs, and every bus the unit built: `BusBase.wakeChip`), its GO (`scheduleGo`), a request delivered to it
  (C&C units), C&C 1's READY, error-line and UART inputs; at once: a load, store, pulse or poke from Java. `run` and
  `runUntil` wake every unit first, because tests change fields directly between cycles (CcCoresTest raises a condition
  by hand). The wakes sit in a 512-cycle ring of unit bitmasks at chip level, with a heap beyond. A spurious wake costs a
  step and is exact; only a missed one would be a bug.
- **Catch-up.** A sleeping core's `mcycle`, `cyclesWfi` or `cyclesHalted`, and `profCycles` are added on waking, at every
  `run`/`runUntil` exit, before PhaseLog reads them, and in the fingerprint.
- **Chip loops.** The READY/error-line loop and `checkInvariants` visit only the units that stepped; the error lines' OR is
  a running count.
- **Fingerprint** (`-Dmeshsim.digest=FILE`): at exit, per chip in creation order, every unit's counters and state hashed.
- **Off switch:** `-Dmeshsim.skip=false` (Config.idleSkip): every unit every cycle, the code path as before.

**The proof so far** (`WORKER28/tools/prove_skip.sh`: each step run with stage 1 off and on, the logs compared with wall
times masked; `shuffle_vs_unshuffled.sh`):
- **Quick suite, locally: 37 of 39 steps the same, cycle for cycle** (bus, net and its 2 seeds, mem and its 2 seeds,
  engine and its seed, ctrl and its seed, riscv, rdport and its seed, accounting, searchbench, agtest and its seed, recipes
  and its seed, cond and its seed, regdoc, hooks and its seed, periph and its seed, reqbus and its seed, cccores and its
  seed, stageb, replayer, memsys, rvc, tracer, sort-p1). The sort and its seed take longer than a session's call allows:
  Quest 114b runs all 39 on the runner.
- **Under shuffle seeds: all 14 shuffled steps with stage 1 on give the same logs as their unshuffled bases with stage 1
  off** (the seed's banner masked), as the convention requires; and each the same as stage 1 off under its seed. Stage 1's
  sleeping tiles draw nothing from their shuffle (owner, 2026-11-06), so these are different step orders.
- Three harness paths reached into sleeping units without a wake and were fixed by the proof (HooksTest's direct loads and
  stores, PeripheralSlotTest's feeding of the send port, CcCoresTest's hand-raised conditions).
- **Quest 114b, the quick suite on the runner: all 39 steps the same off and on, the sort and its seed included; all 15
  shuffled steps with stage 1 on the same as their unshuffled bases with stage 1 off.**
- **Quest 114, T2 segment 0 of both workloads, all 64 windows: every chip's fingerprint, the logs and the CSVs the same off
  and on.** Stage B/F (windows 0-63 of both): all four JVMs ran out of heap after 12 windows, off and on alike, because
  the fingerprint held every chip to the end; up to there (24 chips each) the fingerprints, the logs (the heap error's
  trace aside) and the CSVs were the same. Fixed (each chip fingerprinted when its thread makes the next one, then
  dropped) and rerun in 115.
- **Speed (114), wall seconds per simulated cycle:**

  | run | stage 1 off | stage 1 on | gain |
  |---|---|---|---|
  | T2 segment 0, EVM Zone (21.4 M cycles) | 1,030 s, 48.2 µs/cycle | **54 s, 2.53 µs/cycle** | 19× |
  | T2 segment 0, Cyclone (21.6 M cycles) | 1,052 s, 48.7 µs/cycle | **54 s, 2.50 µs/cycle** | 19× |
  | Stage B and F, 12 windows, EVM Zone | 28.5 µs/cycle | 3.3 µs/cycle | 8.6× |
  | Stage B and F, 12 windows, Cyclone | 27.5 µs/cycle | 4.0 µs/cycle | 6.8× |

  Against 113b's 46.9 µs per cycle (the time probe, 32 windows): 2.5, **18.5× faster**, inside section 1's 15-30×
  estimate. A T2 segment is now under a minute. (The Stage B rows mix stage B, tile 0 alone, and F, the full chip, and
  predate the parked-tile change.)
- **After B1 (merged into main at e67bc43, then into `sim-idle-skip` at 3fd4e5f, a clean merge: main changed only the
  firmware and TracerT2 since 515c11a), the proof rerun passes in full:**
  - **115b:** the quick suite, all 39 steps the same off and on, the sort included; all 15 shuffled steps on the same as
    their unshuffled bases off;
  - **115:** T2 segment 0 of both workloads (all 64 windows, READY/GO barriers) and Stage B/F on windows 0-63 of both (128
    chips each): every chip's fingerprint, the logs and the CSVs the same off and on. Speed:

    | run (115, B1 + stage 1 + the parked-tile change) | stage 1 off | stage 1 on | gain |
    |---|---|---|---|
    | T2 segment 0, EVM Zone (21.4 M cycles) | 1,170 s, 54.7 µs/cycle | **64 s, 2.99 µs/cycle** | 18.3× |
    | T2 segment 0, Cyclone (21.6 M cycles) | 1,200 s, 55.6 µs/cycle | **64 s, 2.96 µs/cycle** | 18.8× |
    | Stage B and F, 64 windows, EVM Zone (29.9 M cycles) | 786 s, 26.3 µs/cycle | **64 s, 2.14 µs/cycle** | 12.3× |
    | Stage B and F, 64 windows, Cyclone (29.0 M cycles) | 762 s, 26.3 µs/cycle | **58 s, 2.00 µs/cycle** | 13.1× |

    (The off runs are slower than 114's, 54.7 against 48.2 µs, and the on runs by the same proportion: the runner's load
    differed, eight JVMs at once both times. Against 113b's 46.9 µs, 2.99 is 15.7×; against its own off run, 18.3×.)
- **Then the latency commit** (GO, READY and the error line at 8: owner, P1), its baselines re-recorded in
  `WORKER28/BASELINES-8.md` (Quest 116: the suites, the full-size sort and the Stage B smoke; 116b: T2 off and on at 8).
- Local speed-ups on full-chip suites: StageB synthetic 16.2 → 4.1 s, CcCoresTest 30 → 7.1 s (seeded 61.8 → 8.6), the request
  bus suite 3.6 → 2.0; the coreless suites (no sleeping: their tiles are driven from Java) unchanged.

## 3. Plans

### 3a. GO and READY at 8 cycles (first, its own commit)

On `sim-idle-skip` as its first commit (or earlier on a branch of its own if you want the baselines sooner):
`Config.goLatency = 8`, `readyLatency = 8` (the error line follows: `errorLatency = -1`), `runLatency = 8` for
consistency. Before changing anything, list what reads them: `CnC.goNet` and its settle step, `Cnc1Peripheral.readyPulse`
and `errorLine`, `PhaseLog`, `Mach` (passes go_latency to the firmware in desc[19]), `RecipeTest.sk`, `CtrlTest`'s GO
check, `SortTest`'s `ready=` option, `Sweep` (which already runs 4 and 8, so the machine works there). Then:

- run the quick and full suites at 8; fix any test that hard-codes latency 1 (by reading the config, not by new numbers);
- re-record every baseline it moves, old → new, in `WORKER28/BASELINES-8.md`: the full-size sort (1,229,008), the suites'
  recorded cycle counts, `RESULTS-full-size.md`, Stage B's per-window cycles (tile 0's boot and GO), T2's segment figures
  (C&C 3's start to exit, the tiles' replay), the T1/T2 "all-in" numbers WORKER22 reports;
- one Quest task for the suites and the sort (as 110h), one for Stage B's 64-window smoke and a T2 segment.

Decided (owner, 2026-11-06): **the first commit on `sim-idle-skip`, once B1 is merged**, so every baseline (the Tracer's T2
cycle counts included) is re-recorded once; **the spec and README text that says "1 in simulation", and Config's
comments, change in the same commit**; the new baselines go in `WORKER28/BASELINES-8.md`.

### 3b. Stage 1: a timing wheel per tile, one thread

**The wheel.** Each tile and C&C unit has a ring of W = 128 buckets (every latency in the model is ≤ 80, the fill; GO's
8 + skew, READY's 8, links 3, K 8). A bucket holds the bitmask of the tile's units to step at that cycle (15 units fit a
`short`; the ring is one `short[128]`), so scheduling is an OR and taking a cycle's set is a read and a clear. A wake
beyond W cycles (the watchdog, a long core stall) goes to a small per-tile heap, moved into the ring as it comes within
range. The chip has the same one level up: a ring of tile bitmasks (`long` + 3 bits), so the chip steps only tiles with
an entry at t or a unit still working.

**Decided (owner, 2026-11-06): the wheel holds the wakes; the data in flight stays in the buses.** The delay lines already hold each
word with its arrival time, and their semantics (a put at t seen at t + L, the FIFO rule) are what BusTest and every suite
check. Making each bus schedule its consumer's wake on its put (a bus gets a `consumer`: a wheel and a unit bit, set when
the tile wires it) gives the wheel its job, never walks the object graph, and leaves the bus classes' behaviour intact.
It is enough for stage 1 and for stage 2 by tile. Data in delivery queues is the by-type step's (3d), built only if the
numbers justify it.

**A unit's step** returns whether it still has work (the table's left column); if so it is re-armed for t + 1, or for a
known later cycle (a core stall of n, the director's next decrement, the watchdog's expiry). A unit with no work and no
wake is not visited. The tile steps its units in the existing order, filtered by the cycle's mask, then its slot and
`ctl.endCycle` if any unit stepped or the control has a pending store or GO. A tile with an empty mask is not visited:
it sleeps.

**Catch-up.** A tile records the cycle it last stepped; when it next steps, the core adds the skipped cycles to `mcycle`
and to `cyclesWfi` or `cyclesHalted` (the state it slept in), and `profCycles` if its pc was in range; harness reads go
through accessors (hazard 4). `computeStatus` is recomputed on the first step after a sleep.

**Chip level.** The request bus and the lines stay serial; the READY/error loop and `checkInvariants` run over the tiles
that stepped; `scanOverflows` stays as is (every 16 cycles).

**Flags.** `-Dmeshsim.skip=false` (stage 1 off: every unit every cycle, today's code path), on by default once proved.

**Order of work.** The wheel and the bus consumers; the units' "still working" results, router first (the most
frequent wake), then the injection/read-queue pair, the director, the core and control; the tile and chip loops; the
counters' catch-up. Each unit converted is proved exact (the suites with skipping on and off) before the next.

**Expected gain** (113b, section 1): on T2's trace about 47 → 1.5-3 µs per cycle, 15-30×; inside the active tiles (C&C 3
during the trace, tile 0 in Stage B, every tile during the replay after GO) the idle units go too.

### 3c. Stage 2: threads with a barrier per cycle

**What parallelism can buy depends on the phase.** During T2's trace (17.9 M of 21 M cycles) one unit, C&C 3's tile, does
nearly all the work and a few tiles wake per window: one thread is fastest, and the barrier would only add cost. During
the replay after GO (about 236K cycles) and in suites like the sort, all 64 tiles are busy: there threads pay. So
**adaptive one-or-many**: run serially while few tiles are awake, switch to the pool when the wheel shows many. For the
acceptance (step 5) the throughput comes mainly from running segments in parallel (TracerT2 already gives each segment its
own thread on its own memory snapshot); stage 2 shortens the longest segment's tail and the all-tile phases.

**By tile** (the first implementation): threads own fixed sets of tiles (contiguous, rebalanced by measured cost); each
steps its tiles' wheels for cycle t; the barrier, then the serial chip work. Cross-tile couplings are the links (3
cycles), the C&C links (1 cycle) and the chip lines (serial). A link's delay line is touched by both its producer and its
consumer, so it is split: the producer's put goes to its thread's outbox, merged into the consumer's delay line and wheel
at the barrier (visible at t + 3 ≥ t + 1, so the consumer never needs it within t). Shared chip state (the log, violation
messages, S1, PhaseLog, overflow sets) gets per-thread buffers merged in tile order, which is today's stepping order.

**By unit type**: superseded by 3d below. Inside a tile
the couplings are 1 cycle through buses, but several are same-cycle direct calls (hazard 1) and shared arrays: the core's
stores land in `TileControl` at the end of the same cycle, its fetches read `cmem` while the director writes through port B
in the same cycle, its sends go through `sendHold` to the injection point at t + 1, its loads read the slot's and the
control's state of t − 1. So by-type needs a second phase in the cycle (the cores, then the units and `endCycle`, or the
reverse) and an audit of every such call; I'd build it after by-tile, behind the same partition interface, and let the
measurements choose (the brief: the profile decides).

**Flags.** `-Dmeshsim.threads=N` (1: stage 2 off). Proofs at 1, 2, 8 and 16 threads.

### 3d. Deliver, run, merge (the owner's final form; a third step, only if the numbers say so)

Each cycle: deliver the data due at t from the wheel into the units' inputs; run every unit with something delivered or
work pending, reading only what was delivered and its own state, writing outputs into its task's delivery queue at
t + L ≥ t + 1; merge the queues into the wheel. Units grouped into tasks by cost (e.g. 16 cores a task, 64 routers
another), one barrier per cycle, no races by construction. It needs every entry of the audit above turned into
delivered data or last cycle's committed state, and the buses' lazy commit replaced by delivery. **Whether to build it:**
113b says the cores cost little (0.3% of T2) and stage 1 leaves C&C 3's tile as the critical path during the trace, so
the case rests on the all-tile phases (T2's replay after GO, the sort); stage 2 by tile's numbers decide.
