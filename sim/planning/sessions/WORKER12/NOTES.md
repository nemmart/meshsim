# WORKER12 (G4: the engines, and the sort end to end): notes

Branch `sim-w12` from `4a4c363`. The prompt is `PROMPT.md`. **Plan approved by the owner, 2026-11-08** (answers below).

I have read PLAN-M2, DECISIONS (D5-D29), AUDIT-M2 (0-8), and `WORKER9-REPORT.md`. W8's report is not on main yet, so I
used W8's `NOTES.md` and code (`ag.h`, `gather.h`, `director.h`, `tile_ctl.h`, `regblock.h`) in its place. I have also
read W10's NOTES (the stop request, placement by load), W11's NOTES on `sim-w11` (its port proposal), PORTS.md 5, 6 and 8,
and `sim.h`'s Hold. On ref's side I read `AddressGenerator`, `GatherEngine`, `TrafficDirector`, `SRAMReadQueue` (the
clients, issue, release, `inFlightOf`), `TableStore` (the STREAM and GROUP paths), `Tile` (wiring, `setMasterConfig`,
`checkGatherAlignment`), `TileControl.computeStatus`, `WordHold`, `StateFp` (ag, gather), `M2Sort`, and SortTest's
`loadInput`, `readKeys`, `valueHalf`, `Oracle` and verification.

## Ports: reply to W11 (one round; AUDIT 3.2, 3.5, 3.6, 3.7)

**Agreed as W11 proposes:**

| port | kind | from -> to | words | latency |
|---|---|---|---|---|
| gather read | Hold | my gather -> W11's srq, client 0 | 2: reqA, reqB | Hold |
| AG read | Hold | my AG -> W11's srq, client 1 | 2: reqA, reqB | Hold |
| STREAM / GROUP requests | Link | my AG -> W11's table store | 3: op, index, 0 | ts_engine_latency |
| STREAM / GROUP returns | Link | W11's table store -> my AG | 3: as `retStream` / `retGroup` | ts_engine_latency |
| we-in, gather-q | Fifo | the director -> W11's WE / my gather | 3 | as W8 built them |

Notes on the agreed rows:
- **The read Holds are width 2.** ref puts a third word, but it is always 0, so the srq's writer writes 0 for it.
- **The refusal is the framework's:** `refused()`, then a retry of my own copy of the words. The AG's done test reads
  `held_at_start()` on its own HoldOut (`AddressGenerator.java:152`). `sim.h` already has both.
- **ref's comments name the clients wrongly** ("AG client 2", "gather client 1"). The code (`SRAMReadQueue.GATHER = 0,
  AG = 1`, `Tile.java:90-99`) rules.

**One change: "gather in flight" goes to the block, not to the gather engine.** The gather engine never reads it. Only
`TileControl.computeStatus` does, through `gather.busy(t)` at `endCycle(t)`, and STATUS is in `ctl`'s fingerprint, so
the bit must be exact every cycle. ref's `busy` is the OR of four terms:

    !gatherIn.isEmpty(t) || left > 0 || !out.isEmpty(t) || srq.inFlightOf(GATHER) > 0

No single unit knows all four. I propose three inputs to the block, which it ORs:

| port | kind | from -> to | carries |
|---|---|---|---|
| `gather_srq_busy` | Reg | **W11's srq** -> block | gather Hold occupied at the end of t, OR `inFlightOf(GATHER) > 0`. Only the consumer knows whether it took the word at t. |
| `gather_busy` | Reg | my gather -> block | queue non-empty after t's take, OR `left > 0`, OR a put on its Hold at t |
| `gather_put` | Link(1), width 0 | director -> block | a mode-4 put at t: ref's `isEmpty` counts the queue's pending put, which reaches the gather only at t + 1 |

**Mine, not between W11 and me** (block side as W8 planned it):

| port | kind | from -> to | carries |
|---|---|---|---|
| AG status | Reg | AG -> block | `running` and `done` at the end of t. The block adds its own pulse of t for `runningForStatus` and `doneForStatus`. |
| start pulse | Wire | block -> AG | written at level 0 of t + 1 (the shifted `endCycle(t)`), acted on at t + 1 |
| drift | Link(1), width 0 | AG -> block | a refusal in a group run (ERRORS [3]; D29) |

**W11's question 2: I support (a).** That is a framework Fifo with a declared overflow-event output, for we-in and for the
gather queue (GATHER_OVF) alike. It changes W10's API, so it goes to the integrator.

**The register block and the director are now edited by both of us.** I propose splitting them by register and bit:
- **Mine:** `PULSE_DMA`; STATUS's rd_running, rd_done and gather_busy; EVENTS [10]; ERRORS [3] (drift) and [6]
  (GATHER_OVF as an event); the director's mode-4 path and `gather_put`.
- **W11's:** `SRAM_RD_*`; READ_PORT; WE_IN_OVF; TABLE_OVF; the director's read-port results.

**For the sort's input load** I need between-cycles `poke` and `peek` of 32 and 64 bits on W11's big SRAM, with its
incremental sum kept. These are ref's `BigSRAM.poke`, `poke64` and `peek64`.

## The owner's answers (2026-11-08)

1. **No merging `sim-w11`.** The integrator merges each W11 piece to main once its make check passes and tells me; I
   merge main.
2. **The gather alignment check's PTR half: not dropped.** ref stops there. Ask W11 for a read-only view of PTR within
   the tile. If that is cheap, check it exactly; if it makes a same-cycle coupling problem, make it an explicit fatal
   ("gather alignment PTR check: not modelled in M2") and record it here.
3. **W8's and W9's files are mine to edit**, each change noted here. The register block and director split is agreed
   with W11 before either of us edits. Gather busy into the block as the OR of three inputs: agreed.
- The halt fan-in, C&C's stops at L1's GO and L2's all-READY, and EngConf.java: agreed.

## For W11 (2026-11-08): three points to settle before either of us edits

**(a) `mem.h`'s "gather in flight" row, per the owner's ruling.** It should be `gather_srq_busy`, a Reg, width 1, from
the srq to **the block**, not to the gather. Its value is (gather Hold occupied at the end of t) OR
(`inFlightOf(GATHER) > 0`). In `MemIO`, `gather_reads` would become the block's RegIn under that name. My ends are in
`gather.h` (published on `sim-w12`).

**(b) PTR[me] for the alignment check:** `ptr_misaligned`, a Reg, width 1, from your table store to my AG. Its value is
`(PTR[me] & 3) != 0` as the table memory stands at the end of t, put on change.
- **Why it is exact:** ref's AG reads `ts.peek(PTR + me)` at its step, before `ts.runCycle(t)` (`Tile.children`: ag
  before ts), so it sees the memory as at the end of t - 1. A Reg of the end of t - 1, read at t, is that.
- **Why it is cheap:** one compare per PTR write-back of entry `me`, and a put only when the low bits change, which is
  rare.
- If you see a problem, the check becomes the explicit fatal (owner's answer 2).

**(c) The split of W8's files:**

| file | mine (G4) | W11's (G3) |
|---|---|---|
| `regblock.h` | PULSE_DMA; STATUS rd_running, rd_done, gather_busy (the OR); EVENTS [10] and `rd_done_prev`; ERRORS [3] DRIFT and [6] GATHER_OVF as events (in `counts[]`, indices 3 and 6 only) | SRAM_RD_ADDR_QUEUE, SRAM_RD_DEST_ADDR; ERRORS [4] WE_IN_OVF, [5] TABLE_OVF, [7] READ_PORT (indices 4, 5, 7) |
| `tile_ctl.h/.cpp` | the block's inputs `ag_status`, `gather_busy`, `gather_srq_busy` (your Reg, my declaration), `gather_put`, `drift`, the `ag_start` Wire; in `add_g1`, the AG and gather units and every connection with one end on them (incl. the STREAM/GROUP Links and `ptr_misaligned`) | the block's read-port and G3 event inputs; in `add_g1`, WE, ts, hist and their connections; `ControlIO`/`MemIO` |
| `regmap.h` | PULSE_DMA's entry | SRAM_RD_* entries |
| `director.h` | the mode-4 path, `gather_put` | the read-port results (the overlap check, `acceptedAt`) |
| `chip.*` | the halt fan-in (P6) | `MemIO` into `tile_cycle` |

**Also for the integrator:** merge `mem.h` (declarations only) to main early, so both sides build against one copy.

## Questions (owner / integrator), answered above

1. **W11's units before they reach main.** a2a and the sort need W11's table store, write engine, big SRAM and read
   queue. May I merge `sim-w11` into `sim-w12` (merge, never rebase)? The alternative is that the integrator merges each
   of W11's pieces to main as it lands.
2. **The gather alignment check's PTR half.** At a run's first mode-4 stream, ref checks the gather base and PTR[me]
   for 128-bit alignment (`Tile.checkGatherAlignment`; UB, so fatal). PTR lives in W11's table store, and a cross-unit
   read at that moment is not a port. My proposal: the AG checks the base half, and the PTR half is dropped, recorded as
   a gap. It is a UB stop that no M2 program reaches. The other option is a PTR snapshot port from the table store, which
   costs a port for a check that never fires.
3. **W8's and W9's files.** I would edit `regblock.h`, `tile_ctl.*` and `director.h` (the split above), `chip.*` (my units
   in the builder), and `cnc.*` (a stop at a named step, P6). Is that OK, with each change noted here?

## The plan

**P1. Headers, declarations only** (done, 2026-11-08: `ag.h`, `gather.h`; the block's and director's after the split is agreed). `ag.h` and `gather.h` get the ports above, and so do the
director's and the block's G4 inputs. W11 and I publish ours together.

**P2. The GATHER engine and the director's modes 2-4.**
- **The director:** modes 2 and 3 already go to we-in. Mode 4 goes to the gather queue and puts `gather_put`.
- **The gather engine** (extending W8's `gather.h`): the active request (left, ordinal, i, counter, wide), k odd or even
  from the `dma` RegFile, one read a cycle into its Hold, and a refusal retried with nothing advancing.
- **Wakes:** a queue arrival, a refusal, and `next()` while it is issuing.
- **The block:** gather_busy from the three inputs.

**P3. The address generator** (extending W8's `ag.h`), exactly as `AddressGenerator.java`:
- **Its parts:** the start pulse; the read-ahead rings (`ag_lookahead`); the STREAM reader; stream runs (gaps, count-0
  streams, the last gap not waited); group runs (S_g, `rotSched`, R = 0 groups, late starts and underruns moving the
  timetable); `checkEntry`'s counts as fixed facts (D24); the refusal, retry and `held_at_start` done test; drift as an
  event.
- **Fatal, named:** UB (a start while running, a run past the table's end, modes 6-7). HALT's reset is fatal too, since
  M2 never resets.
- **The block's side:** PULSE_DMA (no longer fatal), STATUS's rd bits, EVENTS [10] on rd_done's edge.
- **Wakes:** `next()` while running, because the timetable is per cycle, plus table returns. Timed wakes across gaps
  and rotations are a speed item for later, after exactness.
- **Fingerprints:** writers for "ag" and "gather" in a new `tests/statefp_g4.*`, replacing W8's stubs, word for word as
  `StateFp`.

**P4. An engine conformance driver**, `sim/tests/ref/EngConf.java`, on the model of W9's RouterConf. It reads ref by
reflection only and lives outside `sim/ref`.
- **What it builds:** ref's AG, the table store's STREAM and GROUP paths, and the GatherEngine, wired with ref's own
  buses.
- **The script:** seeded tables, DMA_CONFIG, pulses, and mode-4 requests into the gather queue. The read queue's side is
  played by the script, taking or refusing the two Holds in phases.
- **Observations every cycle:** the Holds' puts, the "ag" and "gather" streams, rd_running, rd_done, busy and drift.
- **Why:** it reaches what the sort never does but must still be exact, such as group-run refusals (drift), underruns,
  late group starts, R = 0 groups with gaps, count-0 streams, and k odd and even. The sort's own 2.1 M stream-run
  refusals are covered too.
- **The C++ side:** the same script on my units. The table-store paths are a test stand-in, a Fifo(16) and one read a
  cycle, as `TableStore.java:121-137`, until W11's unit is available, then W11's.
- **Where it runs:** fixtures in make check under shuffle, both orders and step-all; long runs on demand.

**P5. The sort's driver** (`tests/test_sort.cpp`, from W9's `check`; the sequence exactly as M2Sort runs it):
1. **Boot.** SortTest's boot, i.e. `run_when_ready(id)` without Mach's extra `.call`, then `release()`.
2. **Run to B,** the first cycle with `!cnc.busy()` and every core in WFI with `halt == RUN`, by polling, because setup
   is not timed.
3. **Load the input** between cycles: BUF_IN and BUF_VIN in the big SRAM, and `S.n_in` in data memory.
4. **Queue M2Sort's sequence:** L1, SPLIT plus 66 barriers, ..., L2, EXIT, with ref's step granularity. Then
   `release_next()` and `wake("cnc")` at B.
5. **Run to the stop and verify.** Verification is a C++ port of SortTest's `Oracle`, `readKeys`, `valueHalf` and
   `mismatches` (about 60 lines). Keys misplaced, receive sizes, values, exits, violations and ERRORS must all be 0.
- **The key file** is KeyGen's, produced by `regen.sh` as the hash-only goldens are. Its sha256 is checked against the
  `.fp` header (D12) along with the configuration and images.
- **Comparisons, coarsest first (AUDIT 8):** the end facts; the phases (each GO and all-READY cycle, from C&C) against
  `sort16k.phases.csv`; the interval fingerprints from B, every 10,000 cycles and at each GO and all-READY, against
  `sort16k-fp`. `fpwin` and dumps are for localising a mismatch.

**P6. The stop without polling** (for timing runs, as W9's report notes):
- **The fan-in:** every block posts a width-0 halt event when its core stops. Per-row collectors, as W9's, feed a small
  `StopWatch` unit in tile 0's domain. C&C posts when its queue is empty.
- **The stop:** `StopWatch` calls `Ctx::stop()` when all 64 cores have halted and C&C is idle. It records the true
  cycle as the arrival minus the path's fixed latency.
- **The counted span:** C&C asks for a stop at two named steps, L1's GO and L2's all-READY, given by step id at
  configuration. The timing run is then three `run()` calls with a wall-clock reading between them: no polling, and no
  boundaries taken from the goldens.
- **Checking runs** keep polling, so the end fingerprint is taken at ref's exact end cycle.

**P7. The checks, in order:**
1. **a2a with W11:** every unit of tile 0 every cycle, every tile every 100.
2. **The sort's L1 at 16K:** the AG, the gather engine, HIST and the buckets at once, through L1's all-READY.
3. **The whole 16K sort:** 496,619 cycles, PASS, phases and fingerprints. It runs on one thread and split (2, 4 and 8
   row bands, and W10's `by_load`), under shuffle, both orders, step-all and the missed-wake detector. The long debug
   runs go to the runner.
4. **Full size on the runner** against `sort64k` (Quest 134): 1,229,687 cycles, the result, phases and fingerprints, on
   one thread and split.

**P8. The first timing** (on the runner, a clang release build):
- **The span:** L1's GO to L2's all-READY, timed apart from setup as in W7's baseline.
- **Threads:** one, and split over threads, with CPU lists from `machine.sh` (D21) and the machine recorded.
- **Quest:** I pull Quest and read `NUMBERS.md` immediately before claiming, and push the claim before writing the task.
  Then it is W10's.

**Dependencies.** P2-P4 need nothing of W11's: they use the test stand-in. P5 up to the input load needs W11's big SRAM
poke. a2a and the sort need W11's P1 and P2 (question 1). Each piece: its tests, `make check` clean on gcc and clang
(build by build), commit, push, a clean `git status`.

## Log

- 2026-11-08: read the list; ports answered to W11; plan proposed (above). Branch `sim-w12`.
- 2026-11-08: go-ahead; main merged (W8's report). P1: `ag.h` and `gather.h` carry G4's port tables and members.
  They are declared to the framework only when `add_g1` wires them, since the build refuses an unwired port. g++ and
  clang syntax-check both headers, and `test_g1_tile` passes (gcc debug). clang 18 and OpenJDK 21 are installed in
  this container. For W11: (a)-(c) above.
- 2026-11-08: main merged (W11's P1; W10's Fifo overflow event). WIP 3d15a73 (pushed before any run; "unchecked").
  **P2 built:** the gather engine (`gather.h`) and `tests/test_gather.cpp` (6 tests). Also the director's `gather_put`
  and the block's G4 bits, all behind the `g4` / `wired` flags, which are off by default:
  - PULSE_DMA counts starts on the `ag_start` Wire, and clears EVENTS [10] unless the AG is running.
  - STATUS: rd_running, rd_done and gather_busy, the last as the OR of the gather's Reg, `gather_put` and W11's
    `gather_srq_busy`.
  - EVENTS [10] on rd_done's edge.
  - ERRORS [3] (drift) and [6] (the gather queue's overflow, through W10's `overflow_steps`) as events.
  - The fingerprint preview `block_at_end` covers all of these.
  **Also written: the AG** (`ag.h`, a port of AddressGenerator.java; no tests yet). The flags stay off until W11's
  read queue (P2) brings `MemIO`; both read Holds need a consumer.
  **Edits to W8's files** (split agreed with W11):
  - `regblock.h`: `dma_starts`, `dma_pulse`, `rd_running()`, `rd_done()`; the In fields `gather_busy`,
    `gather_ovf_ev`, `ag_running`, `ag_done`, `drift_ev`; `apply()` takes the AG's running.
  - `tile_ctl.h`: TileCtl's g4 inputs and the `ag_start` Wire; `block_at_end`'s g4 part.
  - `director.h`: `DirectorConfig::g4`, `gather_put`.
- 2026-11-08: **checked at 3d15a73:**
  - Every test binary passes in gcc debug, gcc release, clang debug and clang release (one per call, under the tool
    limit). `test_chip`'s wfi and regs are unchanged.
  - `make tsan-chip` is clean (gcc and clang, form 2, 8 domains).
  - Not run: `tsan` and `tsan-units`. My edits don't reach them: they cover framework and M1 units.
- **For W11 (relayed by the owner):** the PTR view's timing. At the AG's step in cycle t, `ptr[me]` must be PTR as it
  stood at the end of t - 1, before the table store's write-back of t. ref's AG runs before ts and reads
  `ts.peek(PTR + me)`. A RegFile is read at a later level in the same cycle, so the table store must publish the value
  from before its write-back.
- 2026-11-08: **P3, the AG's tests:** `tests/test_ag.cpp`, 10 tests (80bb78c), passing in gcc and clang, debug and
  release. They cover:
  - the stream timetable by hand: lead-in 2L + 1, gaps, the last gap not waited, done when the Hold empties;
  - the group timetable: lead-in 4L + 2, the rotation period;
  - refusals in a group run as drift events, losing nothing; refusals in a stream run, which are not drift;
  - a count-0 run; two runs (per-run counters);
  - the three UB stops (a start while running, a run past the table, misaligned gather buffers);
  - checkEntry's counts.
- 2026-11-08: **P4, EngConf** (1b0b138): ref's AddressGenerator, TableStore (STREAM and GROUP paths) and GatherEngine,
  against ours, every cycle.
  - **Files:** `tests/ref/EngConf.java` and `engconf.sh`, as RouterConf; `tests/engconf/` (4 fixtures of 3,000 cycles,
    L = 1, 2, 3); `tests/test_engconf.cpp`.
  - **The script:** random STREAM and GROUP tables (every mode but 6-7; size 11, misaligned sources, odd d values
    counted as violations); runs started only while idle (a config store, then the pulse); mode-4 requests; the read
    queue taking one client a cycle, gather first, at phased rates.
  - **Each cycle's observation:** the AG's and gather's Hold puts, rd_running and rd_done, the AG's StateFp stream
    (without its return lines), the gather's, the three-way gather busy, drift, violations.
  - **Results:** the fixtures are equal to ref (20-25 runs each, 173-898 drift events), also under shuffle,
    instance-major and step-all, in gcc and clang, debug and release. The long runs (24 scripts x 10^5 cycles, run with
    `ENGCONF_LONG=DIR test_engconf long`) are equal too. `engconf.sh --check` regenerates the fixtures identically.
  - **Negative check:** a perturbed observation is reported at its line.
  - **Generator note:** a first generator had the gather starve the AG (client 0 first) for 1,700 of 3,000 cycles, so I
    lowered the gather's rate.
  - **Not covered:** the C++ side uses a stand-in for the table store's AG paths, and the block's endCycle is applied
    by the harness. The real TileCtl and W11's table store meet the AG in the chip (a2a, the sort). EngConf will switch
    to W11's table store once its PTR view exists.
- 2026-11-08: **main merged into sim-w12 (a06e824)**: W11's WE_IN_OVF and TABLE_OVF events. The conflicts in
  `regblock.h` and `tile_ctl.h` were resolved by keeping both sides; they are disjoint by the agreed split.
  - The conditions enum now has DRIFT 3, WE_IN_OVF 4, TABLE_OVF 5 and GATHER_OVF 6.
  - The In struct has both sides' event fields, and `end_cycle` sets all four bits.
  - TileCtl declares W11's three event inputs always and mine under `g4`; the step applies both.
  - The preview `block_at_end` merged without conflict.
  **make check at a06e824, build by build:**
  - gcc and clang, debug and release: 40 test binaries each, 0 failed; the sketches build.
  - `tsan` (gcc and clang: 7 binaries each), `tsan-units` (gcc, clang) and `tsan-chip` (gcc, clang; form 2,
    8 domains): clean.
- **For W11, turning g4 on (proposal):** your srq's client-0 and client-1 Holds and `gather_srq_busy` come through
  `MemIO`, and my two read Holds need your srq as their consumer. Neither side can be wired without the other, so:
  1. Your P2 lands on main with the read queue's client inputs declared only when the tile has G4's clients
     (`MemIO::g4` or equivalent, false by default), so main stays green with g4 off.
  2. I merge main and, in one commit, turn `G1Ctx::g4` on in `add_g1` and wire my side. That means the AG and the gather
     with `wired`, `rd_ag` and `rd_gather` into `MemIO`, `gather_srq_busy` into the block, `ptr_view` to the AG's `ptr`,
     the STREAM and GROUP Links, the block's G4 inputs and `ag_start`, and the director's `gather_put`. The flag is on
     for every add_g1 tile from then on.
  3. That commit's check: wfi and regs unchanged, then a2a with you (W11's P4).
  Then EngConf switches to your table store (owner, 2026-11-08: when the PTR view reaches main).
- 2026-11-08: **g4 on (dcc0b74), step 2 of the proposal above.** W11's P2a (clients behind `io.mem.gather` and
  `io.mem.ag`) and the PTR view (ef39033) were on main, merged first. In one commit:
  - **The wiring (`add_g1`):** every M2 tile has the AG and the gather `wired`; TileCtl and the director have `g4`; the
    table store has `with_ag`.
    - Into `MemIO`: `rd_gather` (client 0), `rd_ag` (client 1) and the block's `gather_srq_busy`.
    - Inside the tile: the PTR view to the AG's `ptr`; the STREAM and GROUP Links (ts_engine_latency); `ag_start`,
      `status` and `drift`; the gather's `busy` and `ovf`; the director's `gather_put`.
    - `G1Config::ag_lookahead` (0: ref's 2L + 2).
  - **Edits to W8's and W11's files:**
    - `tests/statefp_g1.*`: real writers for "ag" (forty fields as StateFp.ag, pStart from the block's preview
      `dma_pulse`, Java ints sign-extended, the rings, the two return Links) and "gather" (with its queue), replacing
      W8's idle stubs; `G1Tile::uag` and `ugather`. A bench without the units writes the reset state (`AddrGen::idle()`,
      `Gather::idle()`).
    - `tests/mem_stand.h` (W11's): MemStand also gives the G4 clients' ends (the two read Holds, never taken, a request
      fatal; `gather_srq_busy`, never put), for the G1 benches built without W9's chip (`test_g1_tile`,
      `test_g1_replay`).
  - **EngConf on W11's table store** (`units::TableStore` with the AG's ports and the PTR view; the tables written into
    its memories before cycle 1; PTR[me] also stored through the window at cycle 1, so the view holds it from cycle 2,
    before the first run starts).
  **Checks:**
  - wfi and regs unchanged on tile 0, every unit (ag and gather now real), every cycle: one thread, step-all, shuffle,
    instance-major, and split over 2, 4 and 8 row bands.
  - wfi and regs unchanged on every tile every 10 cycles, one thread and split over 4 threads, against `wfi-chip` and
    `regs-chip`, regenerated locally (`KEEP=DIR regen.sh --check 'wfi-chip|regs-chip'`: all SAME).
  - EngConf: the 4 fixtures (plain, shuffle, instance-major, step-all) and the 24 long runs of 10^5 cycles equal to ref
    on W11's table store.
  - make check, build by build: gcc and clang, debug and release, 40 test binaries each, 0 failed, the sketches built;
    `tsan` 7 binaries per compiler (threads.mk's `TSAN_TESTS`, the same list for both), clean; `tsan-units` and
    `tsan-chip` clean on both.
  - The "9 binaries" I first reported for gcc's tsan was an artefact. A two-compiler command hit the tool's time limit,
    and its orphaned clang `make` kept writing into the same log. The rerun with its own log has 7, as clang's.
- **For W11:** g4 is on in dcc0b74 (merging to main). From then on, your read queue serves clients 0 and 1 in every M2
  tile, and the block's `gather_srq_busy` reads your Reg. Please cross-check `test_chip` and `test_mem` on main after
  the merge; then a2a with your P2b-P2d.
- 2026-11-08: main merged (f10f866's merge with W11's P2b/P2c). The integrator's two resolved lines are right:
  `TileCore` takes W11's tile and read-port depth, `TileCtl` my `g4`; `G1Tile` has both sides' pointers. D30 read.
- 2026-11-08: **P5a, the sort's driver up to the input load** (1240253).
  - **`tests/sort_input.h`:** SortTest's `readKeys` (KeyGen's file), `value32`, `valueHalf`, `loadInput` (big SRAM
    BUF_IN and BUF_VIN; S.n_in at the image's `S`, 0x1778) and `Oracle` (for the verification; not exercised until the
    sort ends).
  - **`tests/test_sort.cpp`:**
    - **The program:** SortTest.Machine.boot (`run_when_ready(0)`, without Mach's extra `.call`), then a release point,
      then M2Sort's sequence exactly as M2Sort queues it (L1, SPLIT + 66 barriers, ..., L2, EXIT; `at_all_ready(0)`).
    - **The run:** cycle by cycle until `!cnc.busy()` and every core is in WFI with `halt == RUN`. That cycle is B.
    - **At B:** the input load, then `release_next()` and `wake("cnc")`.
  - **`src/units/g4.mk`** (included by the Makefile) links `test_sort` as `test_chip`.
  **Results:**
  - **The boot ends at B = 14791**, sort16k.out's "boot 14791 cycles" (in make check; gcc and clang, debug and release).
  - **The state at B after the input load equals ref's**, sort16k-fp's first point (FpHooks takes it at B, after the
    load and the queueing): 1029 unit streams, every unit of every tile (big SRAM's data included) and the chip's, on
    one thread and on 4 domains.
    - On demand, as the hash-only goldens: `SORT_KEYS=.../uniform.keys SORT_FP=.../sort16k-fp.fp test_sort`.
    - The keys come from KeyGen (`--n 16384 --only uniform`), sha256 checked against HASHES. The golden comes from
      `KEEP=DIR regen.sh --check sort16k-fp` (SAME; a few minutes here). The test refuses a golden whose D12
      configuration keys differ.
  - **Negative check:** without the load, the first difference is `t0.cmem` (S.n_in).
- **Next (with W11):** the sort past B. Cycle by cycle against sort16k-fp's points (every 10,000 cycles, each GO and
  all-READY), L1 first, then whole; then the phases and the verification (the Oracle).
