# The M3 audit: what a segment exercises, how the new units couple, what we check against

*(WORKER13, 2026-11-09; in progress. The unit workers' specification for M3, as `AUDIT-M2.md` was for M2.)* References
are to `sim/ref/meshsim/src` unless stated; `ref` is `sim/ref` as it runs (D27): idle-skip on (WORKER28's stage 1, its
default), `Config`'s defaults. Sections are published as they become solid: **3.1 and 3.2 (W15) first**, then 3.3 (W14),
then the rest.

| section | state |
|---|---|
| 0 The run | draft (Quest 159) |
| 1 What a segment touches | to come (the coverage driver) |
| 2 The cycle as `ref` runs it | the slot's place (2.1); the rest to come |
| **3.1 The peripheral slot** (W15) | **published** |
| **3.2 The replayer** (W15) | **published** |
| 3.3 The Tracer, its caches and memory system (W14) | next |
| 3.4 The C&C cores; 3.5 the DMA and request paths | to come |
| 4 Chip-level work (D23); 5 unit groups | to come |
| 6 The fingerprint | to come (the Tracer's canonical streams first, for W14) |
| 7 The references; 8 localising; 9 findings | to come; 9 has first entries |

## 0. The run

**The reference run is evm-zone-29 segment 10** (Quest 110u's, T2's acceptance with every unit on its real firmware):
`TracerT2 $DATA/w9-deliveries evm-zone-29 --w26 $DATA/w26-jumpmap/evm-zone-29 --segments 10` (64 windows, raw), the
images `sw/build/cc1`, `cc23`, `sw/tracer/build/tracer_ctl`, `sw/replay/build/replay_ctl`, the boot ROM, `hops.txt` and
the universal tables byte-identical to 110u's kit (`sim/planning/sessions/WORKER13/tasks/IMAGES`). The second run,
**cyclone-29 segment 10** (110u's other, the heaviest segment of either workload), is added if coverage shows paths the
first doesn't reach.

**Quest 159** (`sim/ref`, the runner): both OK, every delivery and 4,194,304 rows checked each.

| | C&C 3, start to exit | Tracer rows | drain | transfers | DMA packets | replay max | wall |
|---|---|---|---|---|---|---|---|
| evm-zone-29 10 | 23,693,532 | 19,531,654 | 4,129,492 | 14,162 | 1,772,179 | 234,206 | 46 s |
| cyclone-29 10 | 28,105,968 | 23,810,180 | 4,264,156 | 15,716 | 1,974,150 | 200,588 | 50 s |

Equal to 110u in every column but C&C 3's span, +26 in both: `sim/ref`'s `Config` has GO 8, READY 8 (error 8) and
`stage_delay` 64 where 110u (a326fbd) had 1, 1 and 32, and C&C 1's `CC_SEGMENT` barriers fall inside the span. The
references' D12 header carries these values.

**The run, step by step** (`TracerT2.main` and `chip`):
1. **Setup on T1's path** (not the chip): `TracerRig` loads WORKER26's initial image and map, the tables; the one memory
   is carried through segments 0-9 by the Tracer *model* with the ideal controller (`g.window` 64 times a segment),
   dirty lines written back untimed. Segment 10's ownership (`fs.setOwnership`, its written pages) is set and the memory
   snapshot (`g.fs.copy()`) is what the chip's Tracer runs on. This is setup: no cycle of it is the chip's.
2. **The chip**: `Mach.coresCfg(-1)` with `cc3Tracer` (C&C 1-3 as cores, C&C 3's peripheral the Tracer, its DMA into
   C&C 3's router), the Tracer's memory a fresh `MemorySystem` on the snapshot (`ms.cold` on a state write).
3. **Harness stand-ins before the boot** (`BigSRAM.poke`): C&C 3's big SRAM per `tracer_layout.h` (header, tables, the
   template index and words, the segment's record from frame 2 with frame 3's count, the files word, the advice, the
   expected end state for 64 windows); every tile's replay region per `replay_layout.h` v5 (tables, index, templates,
   header: `RL_WAIT_HANDSHAKE` for tiles below `windows`). Every tile's replayer gets a `rowListener` (the rows'
   observer).
4. **The boot** (`Mach.bootCores`): C&C 1's image and descriptor through the host port; C&C 1's firmware boots C&C 2
   (`cc23`), C&C 3 (`tracer_ctl`) and the 64 tiles (`replay_ctl`), until `coresBooted`.
5. **The segment**: one host command to C&C 1, `CC_SEGMENT` (0x54): all READY, GO, C&C 3's start, all READY again
   (every window landed), the final GO. C&C 3 traces 64 windows, its DMA sending each record and its streams into the
   tile's replay region; each tile replays as soon as its window has landed (Q15's handshake). The run ends when C&C 3
   and every tile have exited (fail-fast: no progress for 3 M cycles, or a tile not exited 1 M after C&C 3).
6. **Checks** (harness, after): C&C 3's `tr_results` (status 1); per tile the delivered record against `state.bin`,
   the raw streams against the delivery's; the rows (`rowListener`) against `rows.bin`, the exit value 1, the replayer
   clean; no chip violations.

## 2. The cycle as `ref` runs it (the new units' place)

### 2.1 Within a unit (`Tile.runCycle`, `Tile.java:127-134`)

```
children, in order (the shuffle permutes only these): router, (SRAM read queue; injection), director, cmem port B,
    big SRAM, write engine, AG, table store, HIST, read port, gather, core
slot.runCycle(t)   for n = 1..3: peripheral n's runCycle(t), then its DMA's runCycle(t); invariant 16's check
slot.endCycle(t)   the consuming EVENTS loads of t, the EVENTS_MASK stores of t
ctl.endCycle(t)    the register block's end of cycle (M2's)
```

The slot is **outside the shuffle and after every child, the core included**. Every other unit reaches the peripheral by
synchronous calls during its own step (the director's `accept`, the core's window loads and stores, the register block's
live bits), and the peripheral's lazy settle makes those calls order-free (3.1). The C&C units are `Tile`s too (6.6), so
the same order holds on C&C 1-3 with their peripherals.

## 3. The units: state, ports, same-cycle couplings

### 3.1 The peripheral slot (`PeripheralSlot.java`, `Peripheral.java`, `EventCounts.java`) — W15

**Structure (approved, owner 2026-11-09).** One framework unit per `Tile` (mesh tile or controller): the slot, with its
peripherals as **member objects called synchronously, dispatched by a switch on the peripheral's type** (no virtual
calls), as `ref`'s slot calls its `Peripheral`s. Types in v1.3: **1** C&C 1's Ingress/Egress with the UART
(`Cnc1Peripheral`), **2, 3** the stubs on C&C 2 and (without `cc3Tracer`) C&C 3 (`CcCores.Stub`: nothing raised, always
idle), **4** the replayer (every mesh tile's peripheral 1, `Tile.meshPeripheral`), **the Tracer** on C&C 3 with
`cc3Tracer` (`Tracer.TYPE`, 3.3), and the test peripheral (`TestPeripheral`, the suites). Only peripheral 1 is ever
present in v1.3 (`present()` 1); the slot keeps 1-3 as `ref` does.

**State** (the slot's own; the peripheral's is in its section):

| state | `ref` | notes |
|---|---|---|
| `mask[n]` | EVENTS_MASK, 0 at reset | written at the end of a store's cycle (`pendMask`, `endCycle`) |
| pending consume | `consumeT[n]`, `consumeBits[n]` | a consuming EVENTS load's bits, applied at the end of its sample cycle |
| per DMA (`Dma`, peripherals with output queues only: the Tracer, C&C 1) | `hdr`, `addr`, `done` (depth 1), `doneAt`, `startedAt`, `busy`, `left`, `q` | `hdr`/`addr` are plain fields: a store sets them in its own cycle |
| counters (diagnostic) | `accepted`, `runsIgnored`, `reservedAccesses`, `violations`, per DMA `transfers`, `packets`, `ignored` | `transfers`, `packets` are in T2's summary |
| invariant 16's watermark | `fullSeen[n]` | flags a post to a full credit counter |

**The timing rule `ref` implements** (`Peripheral.java` header): inputs (`accept`, `run`, stores, a consuming load)
called during t **take effect at the end of t**; level reads at t return **the state at the end of t − 1**. The replayer
realises it with a lazy `settle(T)` (3.2); the slot's own `mask` and consumes with `endCycle(t)`.

**Ports onto the framework.** Every coupling below is `ref`'s synchronous call, re-expressed so that the effect lands in
the same cycle as in `ref`. One rule covers them: **an input that takes effect at the end of cycle c arrives at the slot
on a `Link` at c + 1 and is applied at level 0 of c + 1, before anything reads**; the slot's **published window is a
`RegFile` written only at level 0** (PORTS.md 7: readers at later levels see this cycle's values), holding exactly what
`ref`'s reads at t return: the state at the end of t − 1.

| # | from → to | `ref` | framework port | effect |
|---|---|---|---|---|
| S1 | director → slot (region write) | `TrafficDirector.java:112`: `periph.accept(t, n, d, h, hi)` in the director's step; the peripheral queues a landing at t + `RP_LATENCY` (2) | `Link(3)`, width 3 (the link word: data, header, hi), one per cycle | lands at the **end of t + 2**: applied at level 0 of t + 3. Counted at the director at t (M2's write counters, unchanged). |
| S2 | core → slot (window store) | `Controller.store` → `TileControl.write` → `slot.write(t, n, w, v)`, synchronous; PULSE_RUN, RUN_CONFIG, EVENTS_MASK, PULSE_DMA as inputs at the end of t; DMA_HDR / DMA_ADDR set at once | `Link(1)` {n, word, value}, at most one per cycle (one store per core cycle) | applied at level 0 of t + 1. DMA_HDR / DMA_ADDR may be applied there too: nothing reads them before t + 1 (the earliest load after the store samples at t + 2; the DMA reads `hdr` only at a PULSE_DMA, itself a later store). A PULSE_DMA stored at t sets `startedAt = t` (the store's cycle, not the arrival's): the first packet goes at t + 1 as in `ref` (`runCycle`: `t > startedAt`), so its level-0 application must be in the same step as, and before, the DMA's work at t + 1. |
| S3 | core → slot (consuming load of EVENTS, word 5) | `slot.read` at the load's sample cycle s sets `consumeT = s`, `consumeBits = value & mask`; `endCycle(s)` consumes | `Link(1)` {n, bits} on the same port as S2 (a load and a store never share a cycle) | applied at level 0 of s + 1, **first** (see order below) |
| S4 | slot → core (window loads) | `slot.read(t, n, w)` at the sample cycle (`regLoadLatency` 2: a load issued at t − 1 samples at t) | the slot's window `RegFile` (PARAMS; STATUS [0] running, [1] DMA busy `busy && t > startedAt`, the peripheral's bits; ERRORS; EVENTS = counts as at the end of t − 1 \| DMA done (`done > 0 && doneAt < t`); EVENTS_MASK; DMA_HDR, DMA_ADDR; the peripheral's defined words 32-127) | read at the core's level, after level 0 |
| S5 | slot → register block (live bits) | `TileControl.liveEvents(t)`: EVENTS [15:13] = per peripheral `(events \| done) & mask != 0`; `errorsAt(t)`: ERRORS [13:11] = per peripheral `errors != 0`; `errorLine()` | in the same `RegFile` (two words), read by the register block and the wake term | the wake term at t uses them (WFI); a change of ERRORS [13:11] is an **event** to the register block and the error tree (D29's rule, D23): `ref`'s `Chip` polls `errorLine()` every cycle (`Chip.java:223`) |
| S6 | slot (DMA) → controller router | `Dma.runCycle`: `dmaPort.canPut(t)` then `put`, one packet a cycle | `Fifo` of `ccDmaDepth` (4) into the router's `CC_DMA_IN` input (`CcCores.java:57-59`), with refusal (`canPut` judged at the start of t) | controllers only; a mesh tile's peripherals have no DMA (4.19) |
| S7 | slot → harness (rows) | the replayer's `drain`: one row per `sinkInterval` (1) cycles into `rowListener` | an observer hook (a fixed-size row log), not a port | 3.2 |
| S8 | core → slot (store stall) | `slot.storeStalls(t, n, w)` (C&C 1's UART_TX while its FIFO is full) | in the `RegFile` (the TX FIFO's occupancy as at the end of t − 1, with S2's stores applied) | C&C 1 only (3.4) |

**The order within the slot's step at t** (this is what reproduces `ref`'s `endCycle(t − 1)` then the lazy `settle`):

1. **Level 0, the end of t − 1** (`slot.endCycle(t − 1)`, then the peripheral's `settle(t)`):
   1. the consume of S3 (from t − 1) and the EVENTS_MASK store (S2, from t − 1): **before** the peripheral's inputs. In
      `ref`, `endCycle(t − 1)` consumes before `settle(t)` applies a landing of t − 1; a state-word landing sets the
      credits to 2 (`flush`), so the other order gives 1 where `ref` gives 2;
   2. the peripheral's inputs that took effect at the end of t − 1, in `settle`'s order: by time, and at one time the
      landings (S1) before the core's inputs (S2: run, RUN_CONFIG);
   3. **publish the `RegFile`**: what `ref`'s reads at t return.
2. **The work of t** (`slot.runCycle(t)`): for n = 1..3, the peripheral's `runCycle(t)` (the replayer: its drain, rows,
   pops, posts at t), then the DMA's (S6); invariant 16's check. **Nothing here changes the `RegFile` before t + 1.** In
   `ref` this runs after the core; with the `RegFile` published at level 0 it may run at any level after it.
3. **Wake**: if step 2 changed anything the `RegFile` shows, or the peripheral or DMA is busy (`ref`'s `idle()` false),
   the slot is stepped at t + 1. Otherwise it sleeps until S1, S2 or S3 delivers (D23: it never polls).

**Same-cycle couplings that remain** (none crosses a unit boundary in the same cycle): the core's sample at t reads
level 0's `RegFile`; the director's `accept` at t is three cycles from its effect; the DMA's put at t meets the router's
`Fifo` at t + 1. The slot can therefore run in the tile's domain at any level after 0 (D26: it never needs a `Wire`).

**The flags** (`violate`, to `TileControl.violate`: a count and a message, read by T2 as `m.c.violations`): record
fixed-size facts (D24). T2 requires none.

**Idle-skip in `ref`** (WORKER28): a tile sleeps only with its slot idle (`Tile.canSleep`: every peripheral's `idle()`
and every DMA idle); the replayer is idle with no pass, no inputs or landings queued and its output queue empty; the
Tracer never reports idle, so **C&C 3's unit never sleeps in `ref`** (`Tracer.skipCycles`' comment). The framework's
slot follows its own wake rule (step 3) whatever `ref` skips: the result is the same.

### 3.2 The replayer (`Replayer.java`, `ReplayerAlu.java`, `RowCore.java`) — W15

**What T2 runs on it.** Every tile's peripheral 1, driven by `replay_ctl.c` (layout version 5, `RL_WAIT_HANDSHAKE`):
the tables and ROM by one mode-9 run, region 0 through the send port (66 `SEND_128`s), then passes under 4.19.9's loop
(the credits as EVENTS [5:2], new expansion [6], run done [0]; the refills by AG runs through the read queue and the
loopback, as 128-bit mode-0 writes into region 0x04). `RUN_CONFIG` = `Replayer.runConfig(65536, true, true)` (store log,
rd_prev). Every row goes to the harness's `rowListener`.

**State** (W15's model; the fingerprint's canonical streams are section 6's, to come):

| part | `ref` | size |
|---|---|---|
| memories (4.19.10) | `region0` (record words 0-65: state, stream, 64-register image), `decodeTable` (2,048 × 16 bits), `romLo`/`romHi` (512 × 128 bits) | words 66, 67 written and ignored |
| queues (4.19.8) | `code` (256 × 32-bit values' 128-bit entries), `rlog`, `wlog` (`LOG_QUEUE_ENTRIES` 256 each, 64-bit values; `-Dreplayer.logq` changes it before construction), each with head, occupancy, pending, the stream position, the LRU decompressor's 16 entries, the latched sizes; `exp` (`ExpQueue`) | true FIFOs with their low-water credits |
| the core | `running`, `pc`, `instrIdx`, `startOffset`, `rowsPerPass`, `fStore`, `fRdPrev`, `stateWritten`, `copyDoneAt`, `rowsStarted`, `rowsDone`, `cur`/`nxt` (`Instr`), `firstInstr`, `busyUntil`, `inflight` (`Pending`), `presented`, `expDiscard`, `codeEnded`, `lastStartAt`, `stoppedOffset`, `srcOf[4]` | the register file is `region0[4 + r]` (item 12) |
| config | `runConfig` (RUN_CONFIG, 0 at reset, sticky) | |
| events and errors | `ev` (`EventCounts`: depths {1, 0, 2, 2, 2, 2, 1}, credits flagged when full), `condEvents[7]` (ERRORS, sticky) | |
| the output queue | `outQ` (≤ 32 rows of 10 longs), `outAt` (each row's ready cycle), `lastSink`, `sinkInterval` (1) | |
| the lazy settle | `inputs`, `landing` (time-stamped), `settled`, the snapshot `sRunning`, `sStatus0`, `sStatus1`, `sRows`, `sRunConfig`, `acceptTimes` | in the framework: S1/S2's Links and the slot's level 0 |
| statistics (not architectural) | `stalls[7]`, `passCycles`, `rowsTotal`, `rowsM`, `rowsCtrl`, `newExpansions`, `passes`, `passStartAt`, `passEndAt`, `flushes`, `configWrites`, ... | T2 reads `passStartAt`/`passEndAt` (replay max) |

**Its interface to the slot** (`Peripheral`), with the time each call takes effect:

| call | `ref` | effect |
|---|---|---|
| `accept(t, src, offset, size, lo, hi)` | queues a landing at t + 2; two in one cycle is a simulator bug | the landing at the end of t + 2 (`land`): part 0 (record words, decode table, ROM), parts 1-4 (the queues); part 0 while running, a size other than 128, an unmapped offset: **undefined behaviour** (meshsim stops); a state-word landing flushes every input queue and sets the credits to 2 |
| `run(t)` (PULSE_RUN) | an input at t | the end of t: `doRun` (UB while running); the first row may start at t + 1 (`copyDoneAt = t + 1`) |
| `writeWord(t, 32, v)` (RUN_CONFIG) | an input at t | the end of t (UB while running); words 33-35 read-only |
| `word(t, w)`, `running(t)`, `errors(t)`, `events().value(t)` | `settle(t)` first | the end of t − 1 |
| `runCycle(T)` | `settle(T)`; `drain(T)` (one row to the sink if due); the pass's work | posts at T visible from T + 1 (`EventCounts.value`) |
| `idle(t)` | `!running && inputs, landing, outQ empty` | `ref`'s idle-skip; the slot's wake rule (3.1 step 3) |

**For W15, beyond the ports:**
- The row sink takes one row per cycle from a 32-row output queue (`drain` before the pass's work in `runCycle`); a
  full queue stalls row starts (`Stall.OUTQ`). In T2 the sink is the harness: an observer, not a port.
- `land` and the queue pushes run at level 0 (3.1 step 1); credits come back from pops in the work phase
  (`Stream.pop`: one per half a queue popped, `ev.post(cycle, credit)`), visible from t + 1 through the `RegFile`.
- Invariant 16 and invariant 18 (one source per queue in a pass; only the tile itself refills during a pass) are flags.
- `Replayer.idle` ignores `doneSetAt` and the events: a tile with done posted and nothing else sleeps in `ref`; the
  framework's slot need not step for it (the `RegFile` already holds done).

## 9. Findings for the record

- **9.1 The latency defaults since 110u** (section 0): GO, READY, error 1 → 8 and `stage_delay` 32 → 64 shift C&C 3's
  span by 26 cycles in both workloads; nothing else in T2's summary moves. To be confirmed directly by setting the old
  values in the coverage driver.
- **9.2 `EventCounts`' header vs the slot** (`EventCounts.java:8-9`, `PeripheralSlot.java:126-133`): the header says a
  consuming load sampling at s takes effect at the end of s + 1; the slot consumes at `endCycle(s)`, the end of s. `ref`
  as it runs is the slot's (D27). The same header's "an event posted after the sample point survives" holds for depth-2
  credits but not for a depth-1 event already at its count when posted at s (the post is dropped as full, then the
  consume clears it): run done and new expansion. To count in the coverage run (a post at s to a full depth-1 bit with
  a consume of that bit at s); expected 0, as `replay_ctl` consumes done once per pass.
