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
