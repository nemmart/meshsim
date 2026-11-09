# The M3 audit: what a segment exercises, how the new units couple, what we check against

*(WORKER13, 2026-11-09; in progress. The unit workers' specification for M3, as `AUDIT-M2.md` was for M2.)* References
are to `sim/ref/meshsim/src` unless stated; `ref` is `sim/ref` as it runs (D27): idle-skip on (WORKER28's stage 1, its
default), `Config`'s defaults. Sections are published as they become solid: **3.1 and 3.2 (W15) first**, then 3.3 (W14),
then the rest.

| section | state |
|---|---|
| 0 The run | draft (Quest 159) |
| **1 What a segment touches** | **published** (Quest 160) |
| 2 The cycle as `ref` runs it | the slot's place (2.1); the rest to come |
| **3.1 The peripheral slot** (W15) | **published** |
| **3.2 The replayer** (W15) | **published** |
| **3.3 The Tracer, its memory system and memory** (W14) | **published** |
| **3.4 The C&C cores** (with the links, host port, request bus, chip lines) | **published** |
| **4 Chip-level work (D23); 5 unit groups** | **published** |
| 6 The fingerprint | **6.6-6.9 published**: the Tracer (W14), the slot and replayer (W15), the C&C units and the chip |
| **7 The references; 8 the baseline; 9 findings** | **published** (Quests 159-161) |

## 0. The run

**The reference run is evm-zone-29 segment 10** (Quest 110u's, T2's acceptance with every unit on its real firmware):
`TracerT2 $DATA/w9-deliveries evm-zone-29 --w26 $DATA/w26-jumpmap/evm-zone-29 --segments 10` (64 windows, raw), the
images `sw/build/cc1`, `cc23`, `sw/tracer/build/tracer_ctl`, `sw/replay/build/replay_ctl`, the boot ROM, `hops.txt` and
the universal tables byte-identical to 110u's kit (`sim/planning/sessions/WORKER13/tasks/IMAGES`). The second run,
**cyclone-29 segment 10** (110u's other, the heaviest segment of either workload), is added if coverage shows paths the
first doesn't reach: **it does** (section 1), so **the M3 runs are both: evm-zone-29 segment 10 and cyclone-29 segment
10.**

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

## 1. What a segment touches

**The driver**: `sim/tests/ref/M3Seg.java` runs `TracerT2.chip` exactly (Quest 160: its figures equal 159's; with every
hook on, the same figures and the same fingerprints as with none) and reads, between cycles, ref's own counters per
unit group (the 64 tiles, C&C 1, 2, 3) at the boot's end, the segment's start, C&C 3's exit and the end, the new units'
statistics, and every unit's pc each cycle. The references are `sim/tests/golden-m3/cover/` (`evm.cover.csv`,
`cyc.cover.csv`, and the pc histograms `*.pcs.gz`: image, pc, cycles).

**The run's shape** (evm-zone-29 10; cyclone-29 10 in brackets): setup on T1's path 12.5 s (9.3); the boot to
`coresBooted`, then the segment; the chip runs 23,940,707 cycles (28,319,576), C&C 3 from its start to its exit
23,693,532 (28,105,968).

**Beyond M2's chip** (counters that are 0 in every M2 reference and not 0 here):
- **The C&C cores** (3.4): C&C 1-3 as tiles running `cc1`, `cc23` (C&C 2 runs 54 instructions at boot, then WFI),
  `tracer_ctl`: their controllers, register blocks, controller memories, big SRAM, read queue, AG and table store all
  used (C&C 1: the boot's staged images, 509 AG reads; C&C 3: 2.3 M instructions in the segment); the **host port**
  (3 packets: C&C 1's image, descriptor, command), the **C&C links** (C&C 1 to tile 0 at boot; C&C 3 to tile 2, 1.77 M
  words in, 65 out in the segment), the **request port and bus** (C&C 2's and C&C 3's one "started" each, at boot; the
  tiles send none), and **C&C 1's chip lines** (READY, GO, error) through `Cnc1Peripheral` in place of the Java `CnC`.
- **The Tracer** (3.3): 64 runs, 4,194,304 rows, 662,819 class M; one state write; 6.4 M instruction fetches (69,495
  misses, 33,390 straddling), 1.4 M data accesses (379 misses, 160 write-backs), 41,854 jumps (1,352 map probes);
  stalls fetch 1.32 M, data 29,244, bus 52, probe 81,992 cycles; the drain 4.13 M cycles with 1.86 M scan stalls. The
  memory system: 71,226 reads (2,054 from LPDDR), 160 writes. **No advice stall, no new expansion** in evm-zone 10;
  **cyclone-29 10: 30 new expansions (7,356 stall cycles), and the memory system's early evictions (48,501) and read
  waits (16,321, 786,874 cycles)**, all 0 in evm-zone.
- **The DMA** (3.1 S6): 14,162 transfers, 1,772,179 128-bit packets (15,716 and 1,974,150), src 74, into the
  `CC_DMA_IN` FIFO; on the tiles they arrive as mode-0 writes into big SRAM (the write engine, as M2's).
- **The replayer and the slot** (3.1, 3.2): 64 passes of 65,536 rows; region writes 1,822,040 (the tiles' AG runs
  through the read queue and the loopback, as 128-bit mode-0 packets into region 0x04; C&C 3's into the Tracer's
  region); stalls code 11,776, read log 3,769, write log 9,720 cycles; no controller-fed row in evm-zone (cyclone:
  36 rows, 30 new expansions, 5,808 stall cycles).
- **Count-only packets** (mode 5, the director): 64 into C&C 3 (Q15's handshake), never used in M2.
- Not reached in either run: errors of any unit, overruns, `ms.beyond`, write-allocates, padding rows, UART traffic,
  the watchdog, traps.

**Firmware** (pcs reached, against the ELFs built here, byte-identical to 110u's images):

| image | instructions | evm-zone 10 | cyclone-29 10 | either | never reached |
|---|---|---|---|---|---|
| `replay_ctl` | 932 | 564 | 715 | 715 | `send_exp_word` (N3's send-port word), part of `main` |
| `tracer_ctl` | 882 | 616 | 732 | 754 | `send_exp_word`, part of `main` (`exp_chunk`: cyclone only) |
| `cc1` | 864 | 358 | 358 | 358 | `trap_handler`, `cc1_toast`, `cc1_trap`, `park` |
| `cc23` | 198 | 34 | 34 | 34 | `trap_handler`, `park` |

**`ReplayBench`** (the D8/D15 bar, `sim-step-cost`; `sim/tests/ref/baseline.sh`): every tile replays a window preloaded
into its big SRAM in Stage B's layout (windows 0-63 of evm-zone-29, i.e. **segment 0**), the tiles booted by C&C's
broadcast; **no Tracer, no C&C 3, no DMA, no C&C cores**. It is the replay phase of T2 only, on another segment, and
since the tiles replay as their windows land, it is not a slice of T2's timeline either. Its numbers compare with
T2's tiles' replay (at most 234,206 cycles a tile in evm-zone 10) and not with T2's run (23.9 M chip cycles, dominated
by C&C 3's trace and drain). M3's timing is T2's run, against `ref`'s 46 s and 50 s (Quest 159).

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

### 2.2 The chip (`Chip.runCycle`, `Chip.java:210-245`, with `Config.ccCores`)

```
wakeDue (idle-skip); the request bus (first: a request store at t sees the entry it frees)
children: [0] the Java CnC's endpoint, [1] memsub, [2] cc3 endpoint (built, unconnected with cores), tiles 0-63, [67] the
    Java CnC (built, idle with cores), [68] CcCores: the host queue's head into each controller's host FIFO (one a cycle),
    then units 64-66 (C&C 1-3), each a Tile (2.1: its children, its slot, its register block's end of cycle)
per tile (awake): a READY pulse of t into C&C 1's peripheral (readyAt = t + ready_latency); its error line's change
    into C&C 1's peripheral (visible error_latency later)
CcCores.endCycle: C&C 2's and C&C 3's error lines into ERROR_LINES_2, C&C 1's into its toast pin
invariants (the error scan, invariant 15), idle-skip's sleep check
```

C&C 1-3 run **after** the 64 tiles, so in `ref` a C&C unit's step at t sees the tiles' puts of t only through buses (all
registered), and the tiles see the C&C units' puts of t from t + 1 on: no same-cycle coupling crosses between them except
the C&C links' `Link(1)` (3.4).

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
   0. the slot-owned store effects of t − 1 (S2): DMA_HDR, DMA_ADDR, and PULSE_DMA (`Dma.start`: `startedAt = t − 1`,
      and the peripheral's `outStart(t − 1, q, words)`, synchronous in `ref`'s store). In `ref` these happen during the
      core's step at t − 1, so before the peripheral's work of t − 1 and before its pending inputs are settled; here
      they come after that work (step 2 of t − 1). The two orders differ only when the peripheral's work at t − 1, or an
      input of t − 2 or t − 1, changes what `outStart` checks (the Tracer: its output queues' credits, `q0ptr`,
      `xferLeft`): a PULSE_DMA in the cycle after a state-word landing or a PULSE_RUN, or a transfer started on queues
      whose drain posted done in the same cycle, all undefined behaviour or impossible in a well-formed program
      (finding 9.3; counted in the coverage run, expected 0);
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

### 3.3 The Tracer, its memory system and its memory (`Tracer.java`, `MemorySystem.java`, `FileStore.java`, `LruCoder.java`, `Rvc.java`, `RowCore.java`) — W14

**What T2 runs on it.** C&C 3's peripheral 1 (`Tracer.TYPE` 3; `Config.cc3Tracer`), driven by `tracer_ctl.c`
(`tracer_layout.h`): the tables and ROM, the record and the hash parameters (HASH_MUL, HASH_STEP, HASH_LINES), then per
window PULSE_RUN with RUN_CONFIG 65,536 rows (T2's raw run: no compression enables), the advice (part 2) and
controller-fed expansions (part 4) refilled on their credits, EXPANSION_TEMPLATE stores, and after the rows the drain:
DMA transfers from output queue 0 (the record, read in place) and queues 1-3 (code, read log, write log) into the tile's
replay region, src 74. Which paths and how often: section 1 (the coverage run).

**Structure.** The Tracer is a member object of C&C 3's slot unit (3.1), dispatched by type. **Its memory system and
its memory are members of the Tracer**, called synchronously: `ref` times every fill-bus access at the row's start,
serially, with one request outstanding (`MemorySystem.read`/`write` return the line and its ready cycle at once; the
data is `FileStore`'s, functional), so there is nothing in flight between them and no port. `Rvc` (the RVC
decompressor) and `RowCore` (decode, the expansion word's fields, operands) are pure functions shared with the replayer
(W15), as `ReplayerAlu` is: one C++ copy for both.

**State** (the fingerprint's streams, section 6.6, list it word by word):

| part | `ref` | size |
|---|---|---|
| registers (4.21.3) | `runConfig`, `hashMul`, `hashStep`, `hashLines`, `lengthCode`, `lengthRlog`, `lengthWlog` | words 32, 40-42, 37-39 |
| part 0 (4.21.2) | `rec` (record words 0-67: the state word, the stream word with the advice count, the 64-register image = the working register file, the files word, the template bitmap), `decodeTable` (2,048 × 16 bits), `romLo`/`romHi` (512 × 128 bits) | |
| input queues | the advice queue (`rq*`: 256 × 128 bits, head, occupancy, the half in use, pops since the last credit), the expansion queue (`eq*`: 256 × 128 bits, head, occupancy, pops) | part 2, part 4 |
| the caches (4.21.5) | `icache`, `dcache`: 128 lines each (`icacheLines` a sweep parameter), **fully associative, true LRU** (`LinkedHashMap` in access order: a hit moves the line to the newest end, a fill evicts the eldest); a line is its .pages line number, 16 × 64 bits, `dirty`, `writable` (its fill's status was ok) | `iSeen` is statistics only |
| the jump LRU | `jumpLru`: 64 entries (target pc → base `instr_idx`), true LRU | |
| the FIFOs and set 1 (4.21.6) | `codeFifo` (`codeN` × 32 bits), `logFifo`/`logTag` (`logN` × 64 bits + a 2-bit tag), `set1Code`/`Rlog`/`Wlog` (16-entry LRU tables and their bit counts), `rlogValues`, `wlogValues` | up to 65,536 each |
| the core | `running`, `stopped`, `rowsEnded`, `drained`, `done`, `rowsTarget`, `rowsDone`, `startAt`, `busyUntil`, `waitUntil`, `waitCause`, `presented`, `expansionOffset`, `latch`, `templateStored`, `bitmap[2]`, `cur` (`Instr`: w, kind, start, rd0, offset, cls, nextRow, ctrlFirstSeen, fetched, presented, codeRun), `expDiscard`, `runId`, `statUntil`, `statCause`, `errors` | |
| the output path | `oq[1..3]` (256 × 128 bits each, head, occupancy, `pushed`, `read`, the credits' bounds `bound[0..granted)`, `ended`, `at[]`, `lastPopT`, `popsAt`/`popsCycle`), `q0ptr`, `q0LastT`, `scanStart`, `scansDone`, `codeScan`, `logScan`, `set2*` (LRU tables), the packers `pk*` (the word being packed, its fill, words out), `comp0-2`, `codeEnded`, `logEnded`, `xferQueue`, `xferLeft` | |
| events, errors | `ev` (depths {1, 0, 0, 2, 0, 2, 1, 0, 0, 0, 0}: [0] run done, [3] advice credit, [5] expansion credit, [6] new expansion; [7]-[10] live service flags, set in `settle`), `errors` (sticky; every bit but overrun stops the Tracer, item 152) | |
| the statistics counters | `stat[8]` (words 44-51): architectural (the window reads them), cleared by the state word, ticking every running cycle | |
| statistics (not architectural) | `stalls`, `rows`, `rowsM`, `iAccess`..., `iMissBy`, `runCycles`, `drainCycles`, `rowsEndAt`, ... | T2 reads `runCycles`, `drainCycles`, `rows` |
| **the memory system** (`MemorySystem`) | the shared cache's `tag`, `dirty`, `stamp` (512 sets × 8 ways, LRU by stamp), `clock`, the write buffer (`wbDone` ring, head, count), `busyUntil` (the bus); parameters hit 6, miss 80, write buffer 4 lines, drain 80 | cold at every state-word landing in T2 (`onStateWrite = ms::cold`) |
| **the memory** (`FileStore`) | `pages` (4 KB pages by address in 128 B units: index pages of 1,024 32-bit entries, data pages), `nextFree` | T2's is the snapshot at segment 10's start (section 7) |

**Its interface to the slot**, with the time each call takes effect (3.1's rule applies unchanged):

| call | `ref` | effect |
|---|---|---|
| `accept(t, ...)` | a landing at t + 2; two region packets in one cycle is a simulator bug | the end of t + 2 (`land`): part 0 (record words 0-66, word 67 ignored; decode table at 0x1000, ROM at 0x2000; all UB while running), part 2 the advice queue, part 4 the expansion queue (a push into a full queue: ERRORS [1], dropped, the Tracer runs on); anything else UB. **A state-word landing** (`stateWrite`) empties both input queues, the FIFOs, the output queues, sets the advice and expansion credits to 2, clears the caches and the jump LRU **without writing back**, the template latch and bitmap, the statistics counters, and (T2) makes the memory system cold |
| `run(t)` | an input at t | the end of t (`doRun`: UB while running or with more than 65,536 rows; the first row at t + 1) |
| `writeWord(t, w, v)` | RUN_CONFIG, HASH_MUL, HASH_STEP, HASH_LINES (UB while running), EXPANSION_TEMPLATE (the latch and the bitmap; [31] clear: the latch off): inputs at t; words 33-39, 44-51 read-only (a bus error) | the end of t |
| `word(t, w)`, `running`, `errors`, `events().value(t)` | `settle(t)`, then the snapshot | the end of t − 1 (the live service flags [7]-[10] included: set by the settle from `rowsEnded`, `stopped`, `q0ptr`, the output queues' credits) |
| `outStart(t, q, words)` | synchronous in the PULSE_DMA store (3.1 step 1.0, finding 9.3); UB beyond queue 0's word 67 or a queue's granted credits | `xferQueue`, `xferLeft` |
| `outReady(t, q)`, `outTake(t, q, w)` | the DMA's work at t, after the Tracer's (3.1 step 2): queue 0 from `rowsEndAt < t`; queues 1-3 an entry pushed at t' is readable from t' + 1; the scans judge room before the cycle's pops (`room`: occupancy plus the pops of t) | one 128-bit word a cycle |
| `runCycle(T)` | `settle(T)`; a dropped expansion word popped (`expDiscard`, **also while not running**); `stat[0]++` while running; then the rows (start, 4-cycle rows, 6 for class M; stalls by cause, the data and map stalls counted cycle by cycle from `statUntil`) or the drain (`drainCycle`: one entry per scan per cycle into the set-2 coders and packers; done last, as of the end of T − 1) | posts at T visible from T + 1 |

**Wake rule** (D23). `ref` never lets C&C 3 sleep (the Tracer's `idle()` is the interface's default, false). The framework's
slot steps the Tracer while it is running (the statistics counters tick every running cycle: step it, or catch them up
exactly), while a dropped expansion word waits (`expDiscard > 0` with the expansion queue non-empty), and on S1/S2
deliveries; otherwise its state cannot change. The result is the same as `ref`'s.

**The memory traffic, exactly** (W14's model; `ref`'s choices in the class header): a row's accesses are timed at its
start in the core's order (the fetch's halves, the data line, the map's lines), each `read` at `max(t, mem.freeAt())`;
a dirty data victim's write-back is issued at the fill's ready cycle (`mem.write(ready, ...)`), so the next read waits
for the bus (`Stall.BUS`); the one-entry write-back buffer (`wbLine`) is never occupied across a read. The memory
system's walk costs one access per index level plus the data line, each a shared-cache hit (6) or an LPDDR read (80,
after the write buffer drains); a dirty LRU line in a full set is evicted at once into the write buffer (4 lines, one
per 80 cycles); a write-back allocates without an LPDDR read.

**For the framework's run of T2** (section 7 provides them): the memory at segment 10's start (the `FileStore`
snapshot after T1's carry through segments 0-9, with segment 10's ownership), C&C 3's big-SRAM landing and every tile's
replay region (the harness's `poke`s, step 3 of section 0).

### 3.4 The C&C cores (`CcCores.java`, `Cnc1Peripheral.java`, `Router.java`'s controller variant, `RequestBus.java`) — the next worker

**Structure.** With `Config.ccCores`, C&C 1-3 are units 64-66, each a `Tile` (6.6): **the same tile units M2 built**
(controller, register block, controller memories with port B, big SRAM, read queue and injection, director, write
engine, AG, table store, HIST, read port, gather, request port) running `cc1`, `cc23`, `tracer_ctl`, plus a slot whose
peripheral 1 is C&C 1's Ingress/Egress (`Cnc1Peripheral`, type 1), a stub (C&C 2: type 2; `Stub`: nothing, idle) or the
Tracer (C&C 3, 3.3). They replace M2's Java sequencer (`CnC`), which stays built and idle. What differs from a mesh tile:

| part | `ref` | framework |
|---|---|---|
| **the router** | `Router.feeders(me ≥ 64)` (`FEEDERS_CC`), `ccRoute`: from the loopback or `CC_DMA_IN`, a packet for this controller is ejected and any other (broadcasts included) leaves on `CC_LINK_OUT` (the link); every other input is ejected | M2's router with the controller's feeder table and route rule (no hop table, no broadcast table); its inputs `CC_LINK_IN` (= `FROM_LEFT`), `CC_HOST_IN` (`FROM_RIGHT`), `CC_DMA_IN` (`FROM_UP`), output `CC_LINK_OUT` (`TO_RIGHT`) |
| **the C&C links** | `toCc[c]`, `fromCc[c]`: `WordDelayLine(ccLinkLatency = 1)` each way between tile c's to-controller selector (`router.toCnc`) and C&C c's `CC_LINK_IN`, and C&C c's `CC_LINK_OUT` and tile c's from-controller route block (`router.fromCnc`) | **`Link(1)`**: so **C&C c and tile c must be in one thread domain** (D26: a crossing is a Link of at least 3) |
| **the host port** | `host[c]`: `WordFIFO(4)` into `CC_HOST_IN`; `CcCores.hostQ[c]` (the outside world: `hostWrite`, `hostLoad`, `hostCommand`) feeds it one packet a cycle while it has room, at the start of `CcCores.runCycle` | a `Fifo(4)` fed by a harness stand-in (the host's queue), stepped only while the queue is non-empty |
| **the DMA port** (C&C 3) | `dmaIn`: `WordFIFO(ccDmaDepth = 4)` into `CC_DMA_IN` | 3.1 S6 |
| **C&C 1's chip lines** | READY: a tile's pulse at its edge e sets `readyAt[tile] = max(·, e + ready_latency)` (`Chip.java:222`); ERROR_LINES: a unit's error line's change at t is visible at t + error_latency (`errChanges`); GO: C&C 1's PULSE_GO store at t calls `goNet(t)`, every tile's GO at t + go_latency + its skew (`CnC.goNet`) | READY a width-0 `Link(ready_latency)` per tile into C&C 1's slot; an error line's change a `Link(error_latency)` event (D23: never a scan); GO a `Link(go_latency + skew − 1)` per tile from C&C 1's slot, put when it applies the PULSE_GO store at level 0 of t + 1 (3.1), so the tile sees GO at t + go_latency + skew as in `ref` |
| **the request bus** | the controllers' request ports feed root inputs 8-10 (`RequestBus.java:113-121`: the head stored before t, one beat a cycle, the requestor stamped 64 + c); target c's queue is read by C&C c + 1's receive side; a queued request wakes that unit | M2's request bus with three more root inputs and the controllers' receive sides |

**C&C 1's peripheral** (`Cnc1Peripheral`; window words 32-45, events {1, 1, 0, 1, 0, 0}: [0] run done, [1] DMA done
(never), [2] all READY (live), [3] watchdog (depth 1), [4] UART RX not empty, [5] UART TX not full (live)). Unlike the
replayer and the Tracer it is **not lazily settled**: a store's `writeWord` takes effect at once, in the core's step (the
watchdog's count, a UART TX push, READY_EXPECT, PULSE_GO's `clearReady(t)` (`clearedAt = t + 1`: a pulse arriving
during t is kept) and its GO), and `runCycle(t)` (C&C 1's slot, after its core) then applies the error lines due,
decrements the watchdog, runs the UART's TX shifter and sets the live bits from `allReady(t)`. Reads are immediate:
READY_LINES at a sample t is `readyBits(t)` (arrivals up to t), while EVENTS [2] is the live bit `runCycle(t − 1)` set
(`allReady(t − 1)`). Under 3.1's rule (the store applied at level 0 of t + 1) the framework reproduces this exactly for
every read (the earliest sample after a store is t + 2) and for GO (above); it is **one cycle later than `ref` for the
peripheral's own work in the store's cycle**: a watchdog written at t starts counting at t + 1 instead of t, a UART byte
pushed at t can start at t + 1 instead of t. Neither run reaches the watchdog's expiry or the UART (section 1): a
deliberate difference to propose under D30 if the C&C worker prefers it to applying C&C 1's stores within the store's
cycle (a `Wire` from the core, which keeps C&C 1's core and slot at one level order).
**Ruled (owner, 2026-11-09): D33**, the deliberate difference: the slot's rule is kept, and C&C 1's watchdog and UART
run one cycle later than `ref` after a store.

**The order that matters on C&C 1's slot at level 0 of t:** the READY arrivals of t applied (READY_LINES reads them at t);
EVENTS [2] published from `allReady(t − 1)`, i.e. before them; the error lines due at t; then the store effects of t − 1.

**The boot** (`Mach.bootCores`, a harness step): C&C 1's images staged into its big SRAM by `poke` (the outside world's
overlay), its descriptor and its first command through the host port (`hostLoad`, `hostCommand`); C&C 1's firmware then
boots C&C 2 and C&C 3 (its AG sends their images out on its link into tile 0, across the mesh and out of tiles 1 and 2
into their controllers' links: the tiles' `router.fromCncWords` 509 and `toCncWords` 275 at boot), waits for their
"started" on the request bus (one request each), and boots the 64 tiles (14,976 mode-0 writes into their controller
memories, READY, GO). At boot's end C&C 1 has run 444 instructions, C&C 2 54, C&C 3 5,101 (its tables and setup), the
64 tiles 1,121 in all; the cycle `coresBooted` returns is in the references (section 7).

## 4. Chip-level work, event-driven (D23)

What `ref`'s `Chip.runCycle` does by scanning, and the event that replaces each scan:
- **READY pulses** (`Chip.java:222`: every awake tile's `readyPulseAt == t`): the register block's pulse is a put on a
  width-0 `Link(ready_latency)` into C&C 1's slot (3.4); M2's framework already has READY from the register block.
- **Error lines** (`Chip.java:223-224`, `CcCores.endCycle`): a unit's ERRORS becoming non-zero or zero (its conditions,
  or its slot's live errors, 3.1 S5) is a put on a `Link(error_latency)` into C&C 1's slot; nothing polls `errorLine()`.
- **Invariant 15's error scan** (`Chip.java:229-237`): only while some error line is high; driven by the same events.
- **The request bus**: M2's (event-driven there already); its controller inputs wake the bus when a head is stored.
- **The host queue**: a harness stand-in, stepped while it holds packets.
- **The peripherals**: the slot's wake rule (3.1 step 3); the Tracer steps every cycle it runs (its counters tick), C&C
  1's peripheral while its watchdog counts, its UART shifts or an error line is in flight, the replayer while a pass,
  an input or a row is pending.
In both runs the tiles sleep most of the segment (section 1: 1.53 G of the tiles' 1.53 G core cycles in WFI, summed),
C&C 2 sleeps throughout, and C&C 3's Tracer works almost every cycle: **the run's critical path is C&C 3's domain**.

## 5. Unit groups for parallel workers, and the order

| group | units | worker | its references |
|---|---|---|---|
| **G5** | the Tracer, its memory system and memory (3.3) | W14 (started) | its suites (`TracerTest`, `MemorySystemTest`, `RvcTest`, `TracerRig`), TracerConf, T1's path; 6.6's fingerprints on the M3 runs |
| **G6** | the slot (every type's dispatch, the window, the DMA) and the replayer (3.1, 3.2), wired into the mesh tile | W15 (started) | `ReplayerTest`, `ReplayerUnitTests`, `PeripheralSlotTest`, Stage A on the deliveries, Stage B; 6.7-6.8 |
| **G7** | **the C&C cores and the chip's integration** (3.4, 4): units 64-66 as framework tiles (M2's units, the controller router variant), the C&C links, the host port and its stand-in, C&C 1's peripheral (READY, GO, error lines, watchdog, UART) and C&C 2's stub inside G6's slot, the request bus's controller inputs and targets, the chip lines as events; the T2 harness (the stand-ins of section 0 step 3, the memory snapshot loaded, the rows observed, the checks); the partition (C&C c with tile c) | **the next worker** | `CcCoresTest`, `CcBench`; the boot (`coresBooted`'s cycle and the boot-end fingerprints of every unit, section 7); then T2 on both runs |

**Order.** G5 and G6 go on (each against its suites and conformance drivers). G7 starts now: the C&C units and the boot
need neither the Tracer nor the replayer (C&C 2 and the tiles' slots can hold stubs, C&C 3's a stub until G5 lands; the
boot of section 0 step 4 reaches `coresBooted` with `replay_ctl` waiting), so **the boot exact against its references is
G7's first milestone**. Then, with G5 and G6 wired: T2's segment exact on evm-zone-29 10 (fingerprints at every 100,000
cycles localise any mismatch to a unit), then cyclone-29 10. G7 owns the slot's type-1 and type-2 bodies; G6 owns the
slot and its dispatch.

**For the timing that follows** (D26): one chip split over threads gains little on T2 as it is: C&C 3's Tracer is a
serial machine stepping almost every cycle for 23.7 M cycles, while the tiles work in turn as their windows land (at
most 234,206 cycles each). The speed of **C&C 3's domain on one thread** is the figure that decides M3's wall time;
placement keeps C&C 3 with tile 2 (the link of 1) and spreads the replaying tiles over the others.

## 6. The canonical state fingerprint (M3)

M2's definition (`AUDIT-M2.md` 6.2) holds: per unit, at the end of cycle c, a defined sequence of 64-bit words hashed
with `h = 0x6A09E667F3BCC908; h = fmix64(h ^ w) per word; h = fmix64(h ^ n)`; lists as their length then their entries;
memories as order-free sums. M3 adds units (6.6-6.8) and three rules:

- **Cycle-valued fields** (a deadline or a stamp: `busyUntil`, `waitUntil`, `startedAt`, ...) are written as
  **max(v, c)**: every such field is compared only with cycles after c, so values at or before c are equivalent; a
  field that can then only read c (`lastPopT`, `q0LastT`, `popsCycle`, an output entry's push cycle, `EventCounts`'
  `lastPostAt`) is left out.
- **Inputs not yet in effect** (a peripheral's `landing` and `inputs` in `ref`; the framework's S1 and S2 entries in
  flight, 3.1) are written as a list of **(effect cycle e, kind, words)**, e the cycle at whose end the input takes
  effect, in order; the slot's own store effects (DMA_HDR, DMA_ADDR, PULSE_DMA and its `outStart`) are written **as
  applied**, since `ref` applies them in the store's cycle; a consume or an EVENTS_MASK store of c is a pending entry.
  So `ref`'s side reads the deques as they are and never settles (settling early is not neutral: finding 9.3).
- **A large memory's sum** over non-zero 64-bit words: `Σ fmix64(fmix64(region << 56 ^ index) ^ value)` mod 2^64, index
  the word's index within its region (regions continue M2's numbering, 6.6).

### 6.6 The Tracer (C&C 3, peripheral 1): units `tr`, `tr.ic`, `tr.dc`, `tr.mem`, `tr.files`

**`tr`**, in this order (booleans 0/1; "none" is -1):
1. registers: `runConfig`, `hashMul`, `hashStep`, `hashLines`, `lengthCode`, `lengthRlog`, `lengthWlog`;
2. `rec[0..135]` (record words 0-67, lo then hi);
3. the decode table as one memory sum (region 12; index the entry 0-2,047, value its 16 bits), the ROM as one (region
   13; index 2j for `romLo[j]`, 2j + 1 for `romHi[j]`);
4. the advice queue: `rqOcc`, `rqHalf`, `rqPopped`, then its entries head to tail (lo, hi); the expansion queue:
   `eqOcc`, `eqPopped`, its entries;
5. the core: `running`, `stopped`, `rowsEnded`, `drained`, `done`, `rowsTarget`, `rowsDone`, max(`startAt`, c),
   max(`busyUntil`, c), max(`waitUntil`, c), `waitCause` (ordinal; none if `waitUntil` ≤ c), `presented`,
   `expansionOffset`, `latch`, `templateStored`, `bitmap[0]`, `bitmap[1]`, `expDiscard`, max(`statUntil`, c),
   `statCause` (none if `statUntil` ≤ c), `errors`; then `cur`: 0 if null, else 1, `w`, `kind`, `start`, `rd0`,
   `offset`, `cls`, `nextRow`, `ctrlFirstSeen`, `fetched`, `presented`, (`codeRun == runId`);
6. the events: the counts of bits 0-10;
7. `stat[0..7]`;
8. the jump LRU: its size, then (target, base) eldest to newest;
9. the FIFOs: `codeN`, `codeFifo[0..codeN)`; `logN`, then (`logFifo[i]`, `logTag[i]`) for i < `logN`; `rlogValues`,
   `wlogValues`; set 1's three coders: each `e[0..15]`, `bits`;
10. the output path: `q0ptr`, max(`scanStart`, c), `scansDone`, `codeScan`, `logScan`, `comp0-2`, `codeEnded`,
    `logEnded`, `xferQueue`, `xferLeft`; set 2's three coders' `e[0..15]`; each packer (code, read log, write log):
    0 if null, else 1, `lo`, `hi`, `fill`, `words`; each output queue 1-3: `occ`, its entries head to tail (lo, hi),
    `pushed`, `read`, `granted`, `bound[0..granted)`, `ended`;
11. inputs not yet in effect (6's rule): the landings' count, then each (e, src, offset, size, lo, hi), e = the accept
    cycle + 2; then the inputs' count, then each (e, kind, word, value): kind 1 PULSE_RUN (word, value 0), 2 a window
    word store; e = the store's cycle.

**`tr.ic`, `tr.dc`**: the line count, then each line eldest to newest: its line number, `d[0..15]`, `dirty`, `writable`.

**`tr.mem`** (the memory system's timing state): max(`busyUntil`, c); the write buffer: its lines' drain completions
after c, oldest first (a line due at or before c is retired at the next touch: left out), as count then values; then
for each set holding a valid way, in set order: the set, then its 8 ways in way order, each (tag, dirty, rank), rank the
way's place among the set's valid ways by `stamp` (0 the least recent; an invalid way writes tag -1, dirty 0, rank -1).
The ways' positions are written because a fill takes the first free way by index; `stamp` and `clock` are not.

**`tr.files`** (the memory): `nextFree`, then one memory sum (region 11) over every allocated page's 512 words, index
`(page address / 32) × 512 + i` (an index page's -1 entries included: non-zero). The C++ side keeps it incrementally (a
write-back changes 16 words); `ref`'s side rescans only when the memory system's `writes` count has moved since the last
call (between write-backs it is constant).

**Cost per call** (`ref`, evm-zone-29 10, 240 calls, Quest 160): `tr` 339 µs, `tr.ic` 44, `tr.dc` 20, `tr.mem` 63,
`tr.files` 44 (a rescan only after a write-back); about 0.5 ms a call in all. By size: `tr` is dominated by the FIFOs (up to 2 × 65,536 words late in a window) and the decode table
and ROM sums (about 3,000 terms); the caches 2 × 128 × 19 words; `tr.mem` at most 512 × 25 words; `tr.files` a scan
of every page (evm-zone-29: about 500 image pages plus the map and the segment's copies) when it has changed.

### 6.7 The slot (every unit, 0-66): unit `slot`

For n = 1..3 with a peripheral present: `mask[n]`; if the peripheral has output queues, its DMA: `hdr`, `addr`, `done`,
`busy`, `left`, `q` (`startedAt`, `doneAt` left out by rule 1: both at or before c, read only against later cycles);
then the pending entries of c (rule 2): a consume of c (`consumeT[n] == c`: 1 and its bits, else 0), an EVENTS_MASK
store of c (`pendMaskT[n] == c`: 1 and its value, else 0). The slot's store effects of c (DMA_HDR, DMA_ADDR, PULSE_DMA)
are already applied in `ref`, and are written as applied.

### 6.8 The replayer (tiles 0-63): unit `rp`

In this order:
1. `region0[0..131]`; the decode table and the ROM as 6.6's sums (regions 12, 13); `runConfig`;
2. the code, read-log and write-log queues, each: `occ`, its entries head to tail (lo, hi), `pend`, `poppedSince`,
   `base`, `consumed`, `words`, `valuesOut`, `limit`, `taken`, `endBits`, `landedEntries`, `comp`, `E[0..15]`, and the
   pops made at c (`popCycle == c ? pops : 0`: a landing of effect cycle c is judged against them, `Stream.push`);
   the expansion queue: `occ`, its entries, `pend`, `poppedSince`, `newestValid`, `newestSeq`, the pops at c;
3. the core: `running`, `pc`, `instrIdx`, `startOffset`, `rowsPerPass`, `fStore`, `fRdPrev`, `stateWritten`,
   max(`copyDoneAt`, c), `rowsStarted`, `rowsDone`, `firstInstr`, max(`busyUntil`, c), `presented`, `expDiscard`,
   `codeEnded`, `stoppedOffset`, `srcOf[0..3]`; then `cur` and `nxt`, each 0 if null, else 1, `w`, `kind`, `start`,
   `offset`, `rd0`, `nextRow`, `lastStarted`, `firstRow`, `ctrlFirstSeen`, `codeEnd` (`decodedAt` left out: at or
   before c); then `inflight`: 0 if null, else 1, `rdReg`, `rdVal`, `newPc`, `newIdx`, `hold`, `lastOfInstr`, which
   instruction it belongs to (0 `cur`, 1 `nxt`, 2 neither), its record (10 words);
4. the events' counts (bits 0-6); ERRORS as bits (`condEvents[k] > 0`);
5. the output queue: its rows (each max(ready cycle, c), then its 10 words), oldest first;
6. inputs not yet in effect, as 6.6's item 11 (landings: e, src, word address, size, lo, hi; inputs: e, kind 2 run /
   3 RUN_CONFIG, value).

Left out: the checkers' bookkeeping (`pushed`, `allowance`, the violation lists), the statistics, the decode caches
(`peekValue`, `peekBits`, `peekHit`: recomputed before every commit), `lastStartAt`, `lastSink`, `doneSetAt`,
`newExpSetAt` (only compared with later cycles or never read).

### 6.9 The C&C cores (units 64-66) and the chip

- **Units 64-66** write **M2's 16 tile streams** unchanged (`StateFp.TILE_UNITS`: the router's inputs include
  `CC_LINK_IN`, `CC_HOST_IN` and `CC_DMA_IN`, its `linkIn[0..3]`; tile c's router lists the C&C link into it as
  `fromCnc`), named `cc1.core` ... `cc3.req`, plus their `slot` (6.7) and, on C&C 3, 6.6's.
- **`cc1p`** (C&C 1's peripheral with cores): READY per tile as M2's (visible 1, pending its arrival, none 0, against
  `clearedAt`), `expect`, the error lines in flight (`errChanges`: visible-at, id, level, in order), `errLine` as two
  words of bits (ids 0-63, 64-127), max(`wdCount`, 0), `wdN`, `toast`, `led`, the TX and RX FIFOs (count, then bytes),
  `divisor`, `txDivisor`, `rxDivisor`, max(`txBusyUntil`, c), `rxOverrun`, `framing`, the events' counts.
- **`cc.host`**: per controller, the host queue (count, then each packet's three words) and the C&C links are in the
  routers' streams; M2's `reqbus` (its controller root inputs included, `rootIn[8..10]`), `cnc` and `ep` are kept (the
  Java model's, idle with cores).

**The chip line** as M2's: every unit's hash, then `chip` over them in order: tiles 0-63 (M2's 16, then `slot`, `rp`),
C&C 1-3 (the 16, `slot`; C&C 3's 6.6), then `cc1p`, `cc.host`, `reqbus`, `cnc`, `ep`.

## 7. The references (`sim/tests/golden-m3/`)

Quest 161 (`README.md` there): both runs with every unit's fingerprint every 100,000 cycles and at the boot's end
(cycle 7,222: `coresBooted`), C&C 3's exit and the end (242 and 286 points of 1,214 units); the figures equal Quest
159's; the Tracer's lines equal Quest 160's at all 1,195 common points. The chip lines and the D12 headers are in the
repository; every unit's lines, **the memory at segment 10's start** (`M3Inputs.files`: the `FileStore` T1's carry
leaves, 880 pages in evm-zone, 3,584 in cyclone) and **the harness's big-SRAM landings** (`M3Inputs.sram`: C&C 3's
`tracer_layout.h` and every tile's `replay_layout.h`, as poked before the boot) are on the runner,
`$DATA/w13-golden-m3/`, with their sha256 in `HASHES`. The framework's T2 harness loads those two dumps in place of
T1's carry and the `poke`s; everything else (the boot, `CC_SEGMENT`, the checks against the delivery) it does as
`M3Seg`. `regen.sh` regenerates and checks.

**Localising a mismatch** (as AUDIT-M2 section 8): the first chip line that differs names the 100,000-cycle interval,
the unit lines name the unit, `M3Seg fpchip=… fpchipevery=1` over that interval names the cycle, and
`StateFpM3.dump` names the word.

**The cost** (`ref`, per call, ms): the whole chip about 17, of which the 67 big SRAMs 12-13 (a full scan; the
framework keeps them incrementally, M2's rule), the replayers 0.5, the Tracer's five units 0.4 (`tr.files` 0.05, or 3
when it rescans after write-backs), C&C 1's peripheral 0.02.

## 8. The baseline (Quest 161; `golden-m3/baseline/`)

`ref` as T2 runs it (`M3Seg` without hooks), alone on the runner, one JVM pinned to one P core (CPUs 0 and 1: the core
and its hyperthread; i5-14400F, `machine.txt`; D21), three runs each:

| run | the segment (boot's end to the end) | per chip cycle | setup (T1's carry) | the boot | checks |
|---|---|---|---|---|---|
| evm-zone-29 10 | **46.23 s** (45.87-46.93), 23,933,485 cycles | 1.93 µs | 10.5 s | 0.29 s (7,222 cycles) | 0.9 s |
| cyclone-29 10 | **50.90 s** (50.28-51.38), 28,312,354 cycles | 1.80 µs | 8.8 s | 0.29 s | 0.9 s |

These are the figures M3's timing compares against (the owner, 2026-11-09). For scale: M2's sort ran at ~78 µs per chip
cycle in `ref` (idle-skip on, but every tile busy); T2's segment runs at ~1.9 µs because the tiles sleep most of it
(section 4) and C&C 3's units do the work.

## 9. Findings for the record

- **9.1 The latency defaults since 110u** (section 0): GO, READY, error 1 → 8 and `stage_delay` 32 → 64 shift C&C 3's
  span by 26 cycles in both workloads; nothing else in T2's summary moves. **Confirmed** (Quest 160): `M3Seg` with GO
  1, READY 1, `stage_delay` 32 gives 110u's 23,693,506 exactly.
- **9.2 `EventCounts`' header vs the slot** (`EventCounts.java:8-9`, `PeripheralSlot.java:126-133`): the header says a
  consuming load sampling at s takes effect at the end of s + 1; the slot consumes at `endCycle(s)`, the end of s. `ref`
  as it runs is the slot's (D27). The same header's "an event posted after the sample point survives" holds for depth-2
  credits but not for a depth-1 event already at its count when posted at s (the post is dropped as full, then the
  consume clears it): run done and new expansion. To count in the coverage run (a post at s to a full depth-1 bit with
  a consume of that bit at s); expected 0, as `replay_ctl` consumes done once per pass. **Quest 161: none** in either
  run (Quest 160's "one per workload" was the counter's own artefact: the Tracer's `drainedAt` starts at 0 and was read
  at cycle 0). Were it reached, the framework would reproduce it without special handling: a post to a full count is
  dropped when it is made, as in `ref`, and 3.1 applies the consume after the cycle's posts.
- **9.3 `outStart` doesn't settle** (`Tracer.outStart`, `PeripheralSlot.Dma.start`): `ref` calls it inside the
  PULSE_DMA store, during the core's step, before the Tracer's own work of that cycle and before its pending inputs are
  settled; the framework applies the store at level 0 of the next cycle (3.1 step 1.0). The orders differ only for a
  PULSE_DMA within a cycle of a state-word landing or a PULSE_RUN, or on queues whose drain posted done in the same
  cycle: each undefined behaviour (a transfer beyond the credits granted) or impossible in a well-formed program. To
  count in the coverage run (a PULSE_DMA with Tracer inputs or landings pending from the previous cycle); expected 0.
  **Quest 160: 0** of 14,162 PULSE_DMAs (evm-zone) and of 15,716 (cyclone).
  The same reason keeps the fingerprint's `ref` side from settling the Tracer early (6's second rule).
- **9.4 The Tracer's memory system: the write buffer isn't capped at its depth** (W14, 2026-11-09;
  `MemorySystem.wbPush`): `wbDepth` (4) only delays a line's entry (`enter`), so lines queue beyond it; `ref` throws
  only when the ring passes 64 entries. A guest storing to 136-176 pages queues 19-63 lines. Ruled reproduced exactly
  (D27: `ref`'s own simplification, self-consistent); the C++ stops at the same point. How close the reference runs
  come (Quest 161): **evm-zone 10 never queues a line; cyclone-29 10 at most 3** (1 between cycles, at most 3 within one:
  the count before the cycle plus its early evictions), against the ring's 64 and below the depth of 4. Neither
  reference run reaches the uncapped case; a test of it needs a store-heavy guest (W14's 136-176 pages).
