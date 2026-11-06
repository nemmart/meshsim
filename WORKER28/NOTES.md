# WORKER28 — notes (P1: full-detail simulation at full scale)

Branch for the measurements: `sim-idle-profile` (v1.3-tracer 515c11a plus measurement-only code, never merged). The
implementation goes on `sim-idle-skip`, made from v1.3-tracer once B1 is merged into main (the brief, step 3).

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
F on windows 0-15 of both, under JFR and the probe; ~25 min) and 113b (the time probe on the same T2 and Stage B windows;
~13 min). Results to be added here.

**Before the runs (local, one slow core, so only proportions):**

- 110h's Stage B smoke: stage F (the full chip) 8.1 s per window against stage B (tile 0 alone, 63 parked) 2.2 s, for
  the same 197K cycles: **41 against 11 µs per cycle, so 63 idle tiles cost about 30 µs per cycle** (0.48 µs per idle
  tile-cycle). T2 runs at about 58 µs per cycle (110e: 1,228 s for 21 M cycles).
- The time probe on StageB synthetic: an idle tile-cycle is about 0.5 µs, mostly the SRAM read queue polling its clients
  (with the injection point), `ctl.endCycle` (the GO queue, the counter-event edge detectors, `computeStatus`), the router
  polling its links and FIFOs, the table and HIST stores. `checkInvariants` (64 tiles × 13 counters compared every cycle)
  and the READY/error-line loop are another few µs per cycle at chip level.
- JFR on the same: `TileControl.advance` and `BusBase.enterSlow` (a bus's first touch in a cycle) at the top. That is
  polling empty buses: a bus is entered, committed and found empty every cycle whether or not anything was put.
- The owner's point stands: T1 does the Tracer's work at about 0.4 µs per cycle, C&C 3's tile costs 11-25 µs. 113b's
  per-child figures for cc3 say how much of that is its idle units (router, SRAM, engines with nothing to do), the
  core, the slot's plumbing and the DMA (which appends every packet to `dmaOut`, 1.4 M `long[8]`s per raw segment), and
  how much is the Tracer itself.

## 2. Wake sources and pending work (step 2)

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

Open for you: should the spec text (meshsim README: "1 in simulation; 8 used for estimates", Config's comments, 4.13's
references) change in the same commit?

### 3b. Stage 1: a timing wheel per tile, one thread

**The wheel.** Each tile and C&C unit has a ring of W = 128 buckets (every latency in the model is ≤ 80, the fill; GO's
8 + skew, READY's 8, links 3, K 8). A bucket holds the bitmask of the tile's units to step at that cycle (15 units fit a
`short`; the ring is one `short[128]`), so scheduling is an OR and taking a cycle's set is a read and a clear. A wake
beyond W cycles (the watchdog, a long core stall) goes to a small per-tile heap, moved into the ring as it comes within
range. The chip has the same one level up: a ring of tile bitmasks (`long` + 3 bits), so the chip steps only tiles with
an entry at t or a unit still working.

**Proposal: the wheel holds the wakes; the data in flight stays in the buses.** The delay lines already hold each
word with its arrival time, and their semantics (a put at t seen at t + L, the FIFO rule) are what BusTest and every suite
check. Making each bus schedule its consumer's wake on its put (a bus gets a `consumer`: a wheel and a unit bit, set when
the tile wires it) gives the wheel its job, never walks the object graph, and leaves the bus classes' behaviour intact.
Moving the data itself into the wheel (your wording) is a later step if 113b shows the buses' own cost matters once
nothing polls them. **Your call: wakes-only first, or data in the wheel from the start?**

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

**Expected gain** (to be replaced by 113's numbers): the 63 idle tiles' ~30 µs per cycle go; inside the active tiles
(C&C 3 during the trace; tile 0 in Stage B; every tile during T2's replay after GO) the idle units go too. If C&C 3's tile
falls towards the Tracer's own cost (T1's ~0.4 µs plus the core), a 21 M-cycle segment is a few minutes, not 20.

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

**By unit type** (to measure against by tile): e.g. 4 threads for the 67 RV32 cores, others for the units. Inside a tile
the couplings are 1 cycle through buses, but several are same-cycle direct calls (hazard 1) and shared arrays: the core's
stores land in `TileControl` at the end of the same cycle, its fetches read `cmem` while the director writes through port B
in the same cycle, its sends go through `sendHold` to the injection point at t + 1, its loads read the slot's and the
control's state of t − 1. So by-type needs a second phase in the cycle (the cores, then the units and `endCycle`, or the
reverse) and an audit of every such call; I'd build it after by-tile, behind the same partition interface, and let the
measurements choose (the brief: the profile decides).

**Flags.** `-Dmeshsim.threads=N` (1: stage 2 off), `-Dmeshsim.partition=tile|type`. Proofs at 1, 2, 8 and 16 threads.
