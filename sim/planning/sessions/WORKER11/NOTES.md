# WORKER11 (G3: the memory path): notes

Branch `sim-w11` from `4774960`. The prompt is `PROMPT.md`; the specification is `sim/tests/AUDIT-M2.md` (1, 2, 3.2, 3.4,
3.5, 5, 6). **Plan approved by the owner, 2026-11-08**, with the answers below.

Read: PLAN-M2, DECISIONS (D5-D29), AUDIT-M2, WORKER8 and WORKER9 NOTES (their REPORTs are not on main yet), W12's
prompt, PORTS.md, `inject.h/.cpp`, `write_engine.h`, `table_store.h`, `memsum.h`, `director.h`, `statefp_g1/g2`,
and ref's `BigSRAM`, `SRAMReadQueue`, `InjectionPath`, `ReadPort`, `WriteEngine`, `TableStore`/`RmwPipe`/`HistStore`,
`Tile` (wiring), `StateFp` (srq, inj, sram, we, ts, hist, rdport).

## The owner's answers (2026-11-08)

1. Start now. W9's report is on main (read); W8's report later: its NOTES and code until then.
2. **The framework hook**: a Fifo's overflow reported to its consumer as an event (W10, asked by the owner). Build the
   write engine to use it; don't wait (no M2 program overflows we-in).
3. **Big SRAM: one zero-filled block per tile, allocated once at configuration at its declared size** (4 MB by
   default); the OS backs only touched pages. Sum kept incrementally. **Record the footprint (allocated against
   touched) for W10.**
4. MemConf.java: yes, outside sim/ref, reflection only.
5. Agree the ports with W12 in one round (its proposal mirrors mine).

## Questions asked (2026-11-08)

1. **W8's and W9's reports aren't on main** (PLAN-M2: W11 starts when they are). I can start from their NOTES and code,
   which cover what I extend; or wait. Proposal: start, and re-read the reports when merged.
2. **Overflow events from framework Fifos (D29).** WE_IN_OVF is judged by the framework at the we-in Fifo's delivery
   (t + 1); the write engine can't see it, and may be asleep. Options: (a) **a framework hook** (W10): a Fifo may declare
   a width-0 overflow-event output to a named unit, put at the delivery that drops; W12's gather-q needs the same;
   (b) mine alone: we-in becomes a `Link(1)` into the engine's own 4-entry ring (the engine steps on every arrival and
   posts the event). **I recommend (a)** (one rule for both queues, the director's port unchanged). The table store's
   four request FIFOs (TABLE_OVF) are internal to my units, so their event is mine either way.
3. **Big SRAM storage**: one anonymous `mmap` per tile at configuration (`mem_chunks` x 4 bytes, 4 MB), zero pages
   committed by the OS on first touch: no allocation in the loop, untouched areas cost nothing (ref allocates an area at
   its first write). OK?
4. **A conformance driver for the memory path** (`sim/tests/ref/MemConf.java`, reflection only, outside `sim/ref`, as
   W9's RouterConf): random stimulus at the tile's G3 boundary (client requests, we-in packets, window stores) into ref's
   srq + inj + sram + we + ts + hist, fingerprinted every cycle, against the same script on my units. It reaches what no
   M2 program does and the audit says must still be exact: refused holds and full areas, a full token window,
   **two read-port returns ready in one cycle (the free list's order)**, write combining, window conflicts. OK?

## Ports to agree with W12 (proposal; AUDIT 3.2, 3.5, 3.6)

| port | kind | from -> to | words | latency |
|---|---|---|---|---|
| gather read | Hold | W12's gather -> my srq (client 0) | 2: reqA, reqB (SRAMReadQueue.reqA/reqB) | (Hold) refused, retried at t + 1 |
| AG read | Hold | W12's AG -> my srq (client 1) | 2: reqA, reqB | (Hold) refused, retried at t + 1 (its 2.1 M refusals) |
| gather srq busy | Reg | my srq -> the register block | 1: the gather Hold occupied at the end of t OR inFlightOf(GATHER) > 0 | (Reg) put on change (W12's change, agreed) |
| PTR view | RegFile | my table store -> W12's AG | 256: PTR as at the store's step | (RegFile) the gather alignment check |
| STREAM / GROUP requests | Link | W12's AG -> my table store | 3: op, index, addend (TableStore's words) | ts_engine_latency (2) |
| STREAM / GROUP returns | Link | my table store -> W12's AG | 3: as TableStore.retStream / retGroup | ts_engine_latency (2) |
| we-in, gather-q | Fifo | W8's director -> my WE / W12's gather | 3 (link words) | as now (W8); overflow per question 2 |

Reads go through the srq's free list exactly as ref's (client priority 0, 1, 2; one issue a cycle; a refused SRAM read
Hold stalls the queue). In-order returns per client; W12's units see their data as packets through the injection point,
so there is no other return port between us. **Published: `src/units/mem.h`** (declarations only: the table, the words, the client numbers, `MemIO`).

## The plan

**P1. `regs` from cycle 825** (mode-1 self-sends: write engine, PTR's RMW, big SRAM writes), in one piece:
- `BigSRAM` (new unit, `units/big_sram.h`): 16 area queues (depth `area_queue_depth`), served by a non-empty mask
  (no scan of 16), the write Hold then the read Hold, room by the count before the pop (13a), write combining (on in
  ref, never applying), out-of-range dropped; contents in the mmap; **the contents' sum kept incrementally** (`MemSum`,
  region 3, per written chunk); 16 area returns `Link(sram_read_latency)` (reads land in P2). Wakes only while a queue
  is non-empty or a Hold is put.
- `WriteEngine` (extending W8's file): the pointer stage (2L + 2), the awaiting order, PTR returns, the write side with
  the SRAM Hold's refusal and retry, one packet in a cycle; modes 0 (region 0x03), 1, 2 (bucket digit), 3 (augmented
  records); count-on-arrival to HIST; misaligned PTR = UB (fatal, named); out-of-range dropped and counted.
- `TableStore`/`HistStore` (extending W8's file): request `Link(2)`s into internal `Fifo(16)` rings, the RMW pipe (read
  at t, write back at t + 1, the bypass), returns on `Link(2)`; the window's stores lost on a write-back cycle (ref's
  `ctrlWrite`), HIST loads returning the engine's value on one (`ctrlRead`; the core reads a write-back-of-t register
  published at t - 1, so order-free); TABLE_OVF as an event (D29).
- Fingerprint writers in a new `tests/statefp_g3.*` (`we, ts, hist, sram, sram.data`, then `srq, inj, rdport`),
  replacing W8's/W9's stubs; incremental = full checked.
- **Lift `fp::kRegsCheckedThrough` in the same commit: `regs` equal to `golden-m2` to its end (869), every unit of
  tile 0 every cycle and every tile every 10 cycles** (`regs-chip`, regenerated locally), one thread and split, under
  every debug option.

**P2. The read side and the read port** (AUDIT 3.2: one unit with W9's send side; `InjReadSide` filled, `Tokens`
shared): 64 token slots, **the free list as a ring in ref's exact order** (taken at the head, returned at the tail),
the gather's and AG's in-order return queues, the read port's ready set ordered by (ready cycle, token) as a small
sorted array (no TreeSet), area returns placed, one release a cycle (gather, AG, read port), one issue; zero-width and
out-of-range reads ready at t + 2 + latency (a timed wake); the read-return Hold inside the unit; `injected` frees or
moves the token. The read port: the core's `SRAM_RD_ADDR_QUEUE` store and load (free entries from its own stores and a
Count of injections before t), a dropped store as a READ_PORT event (D29), SRAM_RD_DEST_ADDR's advance (W8's);
the director accepting read-port results (`outstanding`, the overlap check counted). These touch W8's core, regmap and
director: noted here as I change them.

**P3. The rest, exact in cases no program reaches**: the conformance driver (question 4) over all of the above, every
cycle, with the debug options; the units' digests for step-all.

**P4. Checks with W12**: `a2a` (AG, read queue, write engine), then the sort's SPLIT (the read port, replies); each on
one thread and split, under every debug option. Then `REPORT.md`.

Each piece: its tests, `make check` clean on gcc and clang (build by build if the tool limit bites), commit, push.

## Log

- 2026-11-08: read the prompt's list; plan proposed (above). Branch `sim-w11`.
- 2026-11-08: go-ahead. Main merged (W9's report). **`src/units/mem.h` published** for W12: the crossing (two Holds of
  2 words into the read queue, clients 0 and 1; the gather's in-flight Reg; STREAM/GROUP requests and returns as
  `Link(ts_engine_latency)` of 3 words), the request words, and `MemIO` (the read queue's ports as the control builder
  hands them to tile_cycle). **Wiring proposal for the integrator:** `ControlIO` grows a `MemIO mem` (W9's contract);
  `tile_cycle` connects it to the injection unit's read side; the injection unit declares the read side's ports only
  when `mem.present` (network-only tiles, RouterConf and test_router keep today's ports). clang 18 and OpenJDK 21
  installed in this container (apt).
- 2026-11-08, owner's second round: commit first (P1 pushed as WIP 1710fef, "unchecked: clang release only"), then
  builds and make check; from here, commit after each piece compiles and passes its own test.
  - **chip.h is shared with W12** now: `ControlIO` grows `MemIO mem`, wired by `tile_cycle`, the read side's ports declared
    only when present (approved). Changes to chip.h noted here as they land.
  - **Footprint for W10:** big SRAM 256 MB allocated (64 x 4 MB, one mapping per tile at configuration), regs touches
    256 KB (one page per tile), wfi none (`test_chip` prints both).
  - **Agreed with W12 (its NOTES on sim-w12):** the port table as proposed; the gather's busy half is mine as a Reg to the
    register block (`gather_srq_busy`: its Hold occupied at the end of t OR a gather read in flight), the other two halves
    W12's; **a PTR view** (a RegFile from the table store) for the gather alignment check: no problem; big-SRAM `poke` and
    `peek` of 32 and 64 bits between cycles (`BigSram::poke/peek/poke64/peek64`, the sum kept). **The split of the register
    block and the director, as W12 proposes:** mine `SRAM_RD_*`, READ_PORT, WE_IN_OVF, TABLE_OVF, the director's read-port
    results; W12's PULSE_DMA, STATUS's rd_running/rd_done/gather_busy, EVENTS [10], ERRORS [3] and [6], mode 4 and
    `gather_put`. `mem.h` updated.

## Owed before M2 closes (D29: overflows feeding ERRORS are events)
- **READ_PORT:** a dropped SRAM_RD_ADDR_QUEUE store (P2).
- (Done 2026-11-08: WE_IN_OVF through W10's `overflow_steps`; TABLE_OVF from the table store's and HIST's request FIFOs.
  Both reach the block as width-0 `Link(1)` events, put in the step that judges the overflow, so its endCycle(t + 1)
  sets ERRORS [4] / [5].)
- 2026-11-08: **P1 done: make check clean at d10789c**, build by build (this container: g++ 13.3.0, clang 18.1.3; one CPU):
  make run gcc debug, gcc release, clang debug, clang release (36 test binaries each, all 0 failed); make tsan gcc and
  clang (7 binaries each), make tsan-units, make tsan-chip gcc and clang (form 2 over 2, 4 and 8 threads). **regs
  equals golden-m2 to its end (869)**: every unit of tile 0 every cycle, one thread, the debug options (step-all,
  shuffle, instance-major; ASan and the missed-wake detector in debug builds) and form 2; **every tile every 10 cycles**
  (`CHIP_FP_DIR`, regs-chip and wfi-chip regenerated from ref: SAME), one thread and split over 4. wfi unchanged.
  The fix after the WIP commit: test_statefp_g1's plain-state check stops at 824 (no G3 path without a machine).
  The container needed `libclang-rt-18-dev` for clang's sanitizers. **Ready for the integrator to merge.**
  Next: P2 (the read side in the injection unit, the read port; MemIO through ControlIO and tile_cycle).
- 2026-11-08: main merged (P1, W10's Fifo overflow event). **WE_IN_OVF and TABLE_OVF are events (D29):** the write
  engine declares `overflow_steps(in)` and puts `ovf_ev` when `Ctx::overflows(in)` moves; the table store and HIST put
  theirs when a request FIFO's put is judged dropped. The block has three width-0 inputs (`we_ovf`, `ts_ovf`,
  `hist_ovf`; regblock's `In::we_in_ovf_ev`, `table_ovf_ev`), also in `block_at_end`. [4] no longer reads the we-in
  Count (still wired, statistics). **`tests/test_mem.cpp`**: the overflow event's timing (judged t + 1, event t + 2),
  the PTR path and the SRAM Hold's refusal, ReqFifo's judgment, the block's bits, the big SRAM's writes, sum and room
  by the count before the pop. (Write combining needs a read in the same area: with P2's MemConf.)
- 2026-11-08: **make check clean at 4de8844** (build by build: gcc and clang debug and release, 37 binaries each; tsan, tsan-units, tsan-chip on both). The overflow events piece is ready to merge. Next: P2.
- 2026-11-08: **the write engine's missed-wake threshold (1,024 cycles, add_g1), why it waits legitimately:** with its
  pointer stage full, the engine can take nothing from we-in until its head's PTR returns; that return's request sits in
  the table store's request FIFO (ReqFifo, the store's own state: ref's WordFIFO behind a delay line), so for a cycle or
  more nothing is on its way to the engine's ports while we-in holds data. The store serves the FIFO every cycle and the
  return follows at ts_engine_latency, so the wait is bounded by the FIFO's 16 entries plus the two latencies; ref's
  engine simply polls every cycle. Same reasoning as W9's injection unit (a token free that isn't on its way yet).
- 2026-11-08: **the PTR view for W12** (`TableStore::ptr_view`, 256 registers, declared with `with_ag`). Checked against
  ref's order: the tile's children run router, srqInj, dir, cmem, sram, writer, **ag, ts**, hist, ...;
  `Tile.checkGatherAlignment` reads `ts.peek`, which flushes the macro's pending port writes (the controller's store
  and the write-back of t - 1) but the store hasn't run its write-back of t. So the AG at t sees PTR at the end of t - 1.
  A RegFile is read at a later level of the same cycle, so the store publishes a write-back of t at its step at t + 1
  (it asks for that step); a window store of t - 1 is published at t, as it lands. Test
  `table_store_ptr_view_is_ptr_before_this_cycles_write_back` (hand-derived): the write-back of 4 is seen from 5, a window
  store of 6 from 7, and a window store on a write-back cycle (10) is lost while the write-back is seen from 11. (Under
  ref's debug shuffle the children's order varies; the model follows the default order.)

## P2's design (2026-11-08, from SRAMReadQueue, InjectionPath, ReadPort, Controller.java:380, TileControl 222-340)

Pieces, each committed when it compiles and passes its own test:
- **P2a, the read queue in the injection unit (`InjReadSide`):** 64 token slots (reqA, reqB, issueT, readyT, lo, hi,
  partsLeft, client, zero); **the free list as a ring in ref's ArrayDeque order** (poll the head at issue, add at the
  tail at `injected`); two in-order queues (gather, AG) as rings of 64; the read port's ready set ordered by (readyT,
  token) as a sorted array of at most 64 (insertion; ties by token as ref's TreeSet). The step at t: (1) area returns
  placed (16 `Link(sram_read_latency)` from the big SRAM, `{tok | part << 16, lo, hi}`); (2) one release (gather head,
  AG head, then the earliest ready read-port entry with readyT <= t) put into the read-return hold, **inside the unit**
  (ref's WordHold: put at t, injected at t + 1; never refused, since the injection takes it first); (3) issue: a refused
  SRAM read Hold is retried and nothing issues; else with a token free (start + taken < cap) the first ready client
  (0, 1, 2) is taken and issued (misaligned: UB, fatal, named; past mem_chunks or size 11: zero-width, ready at
  t + 2 + sram_read_latency, a timed wake). `injected(h)`: reads--, the token to the free list's tail, kept as
  loop_held when the packet loops; the read port's injected count (a Count to the core: free entries at t are from
  injections before t). Wakes: next() while a release or issue is possible, the earliest zero-width readyT, the SRAM
  return Links and the clients wake it otherwise.
  **Clients present by flag** (`MemIO::present`, and per client): W12's gather and AG Holds are declared and wired when
  W12's units have their `rd` ports; until then only the read port (client 2). The big SRAM's `rd` Hold and 16 returns
  are declared with the read side (BigSram `with_reads`, today false).
- **P2b, the read port:** the core's store to SRAM_RD_ADDR_QUEUE at t (ref: `readPort.store` in the store's cycle, with
  `rdDest/rdSize/rdCounter` as they stand at t): free(t) = depth - (stores - injections before t), from the core's own
  stores and the injection unit's Count; a store with no free entry is dropped and is a READ_PORT event (D29, ERRORS
  [7]); else the request (reqA mode 0, dst me; reqB DMEM | dest) on the core's `Fifo(read_port_depth)` into client 2.
  The block's endCycle advances SRAM_RD_DEST_ADDR's destination on the store (TileControl:337). A load of
  SRAM_RD_ADDR_QUEUE returns free(t) at its sampling cycle. The director: read-port results (TAG_READPORT) accepted
  (`acceptedAt`; ref's overlap check counted). Touches W8's core, regmap, block and director: the bits agreed with W12.
- **P2c, the writers:** "srq" (the free list in order, the held slots in token order, the two orders, rpReady, held,
  client0-2 buses, ret0-15 links), "inj" (the read-return hold: occupied, its word, a put at c), "rdport" (inUse,
  outstanding), "sram" with its `rd` Hold; W9's stubs removed.
- **P2d, MemConf** (tests/ref/MemConf.java): ref's srq + inj + sram + we + ts + hist driven by a seeded script at the
  tile's G3 boundary, fingerprinted every cycle, against the same script on the units; then a2a with W12.
- 2026-11-08: **P2a landed, make check clean at 49399c7** (gcc and clang debug and release, 37 binaries each; tsan,
  tsan-units, tsan-chip on both). The read queue in the injection unit (InjReadSide, as designed above); `MemIO` in
  `ControlIO` (chip.h, shared with W12) wired by `tile_cycle` when `mem.present`; the big SRAM's read side on in
  add_g1; the core declares `rd_port` (Fifo out to client 2) and `rp_injected` (Count in), unused until P2b; tests that
  build add_g1 without the chip wire `tests/mem_stand.h`. regs and wfi unchanged (no reads). **For W12:** to join as
  clients, set `io.mem.gather` / `io.mem.ag` and give `io.mem.rd_gather` / `rd_ag` (HoldOut, width 2) and
  `io.mem.gather_srq_busy` (RegIn, width 1, the block's) in add_g1. test_mem's
  `read_queue_read_port_returns_and_a_same_cycle_tie` (hand-derived): a read waits in the SRAM's Hold behind a full
  area (depth 1), a zero-width read past mem_chunks becomes ready the same cycle (8) and goes second by token order; the
  freed tokens join the free list's tail in injection order. Next: P2b (the read port), P2c (writers), P2d (MemConf).
- 2026-11-08: **P2b and P2c landed, make check clean at 93c2316** (all four builds, 37 binaries each; tsan, tsan-units,
  tsan-chip on both; clang release run before the debug ones).
  - **P2b, the read port:** the core's store to SRAM_RD_ADDR_QUEUE (Controller.java:380) queues
    {reqA(chunk, size, mode 0, counter, me), reqB(DMEM | dest)} on its Fifo into client 2 with SRAM_RD_DEST_ADDR as it
    stands at t, if free(t) = depth - (stores - injections before t) > 0 (the injection unit's Count, read through
    t - 1); else dropped. The store reaches the block on the bus with bit 31 = dropped: endCycle advances the destination
    by the size (queued or not, TileControl:337) and counts READ_PORT (ERRORS [7], an event since it comes with the
    store). A load returns free(t) at its sampling cycle. The director counts read-port results accepted and packets
    accepted with results outstanding (readPortOverlaps; the core's stores as a Count, through t - 1: ref's director runs
    before the core). Touched W8's rv32.h, regmap.h, regblock.h, tile_ctl.*, director.h: the bits agreed with W12.
    test_rv32's death test now names the bench without a read port. **test_chip ** (one
    tile, hand-written boot ROM, the router's loopback): both results land in data memory, the load sees 62 free, the
    destination advances to 0x208; at depth 1 the second store is dropped, ERRORS = bit 7, the destination still
    advances.
  - **P2c, the writers:** "srq" (free list, held slots in token order with readyT = Long.MAX_VALUE until ready, the two
    orders, rpReady, held, client buses: absent W12 clients written as empty Holds, the read port's Fifo, the 16 area
    returns), "inj" (the read-return hold: its release of c as the put), "rdport" (inUse, outstanding; G1Tile::uinj set
    by the chip's driver). regs, wfi and every tile unchanged; the writers' real check is MemConf (P2d) and a2a.
