# The M2 audit: what the sort needs, how each unit couples, what we check against

*(WORKER7, 2026-11-07; draft while Quest 134 runs. The unit workers' specification for M2, as `AUDIT-M1.md` was W5's.)*
References are to `sim/ref/meshsim/src` unless stated. Evidence comes from runs of `sim/ref` through WORKER7's drivers
(`sim/tests/ref/M2Sort.java`, `M2Prog.java`, `FpHooks.java`, `StateFp.java`), which only read between cycles: with them on,
ref's own digest and every result are unchanged (section 6.5).

## 0. The run

**The full sort is `p4/uniform-k128`**: `SortTest -1 run label=p4/uniform-k128 keys=keys-65536/uniform.keys fmt=k128
ready=8`, the key file from `KeyGen DIR --n 65536 --only uniform` (sha256 `a7636b68...`; *not* SortTest's built-in
`kind=uniform`). `Config.machine()` with `ready_latency` 8; the image `sw/ext/sort/build` (no `ORACLE`: the real split
runs), byte-identical to Quest 118's. 1,229,687 cycles from L1's GO to L2's all READY. The 16K reference is the same with
`--n 16384` (`3db703f3...`): **496,619 cycles**, every check passing.

**The run, step by step** (`SortTest.sortRun`, replayed exactly by `M2Sort`):
1. `new Chip(Config.machine())`, 64 tiles, C&C 1 the Java sequencer `CnC` (**`Config.ccCores` is false**: the cores of
   C&C 1-3 never run; `Chip.cores == null`).
2. **Boot** (simulated, 14,791 cycles, outside the count): C&C broadcasts program and data memory as 128-bit mode-0
   packets, `CMD_JUMP`, waits for all READY, GO; the boot ROM jumps; every tile reaches `wait_for_command`'s WFI.
3. **Input load** (harness stand-in, between cycles): `BigSRAM.poke` of each tile's keys at `BUF_IN` and its 128-bit
   values at `BUF_VIN`, and `S.n_in` in data memory (`pokeLocal`).
4. **The command sequence**, queued in `CnC` up front: L1, SPLIT (+ 66 barrier phases), XB, RED, XC, TH, SWEEP, DESC, K,
   K1.0-4, K2.0-4, V, V1.0-4, V2.0-4, L2, EXIT: each a broadcast command (arguments then slot 0) and a GO when all 64
   are READY (`atAllReady` runs a harness callback at that point: in this image, only bookkeeping).
5. **Run** until C&C's queue is empty and every core has stopped; `SortTest.Machine.monitor()` runs between every two
   cycles (harness only: the disjoint-buffer checker's phases, the lowest stack pointer, tile 0's profile).
6. **Verification** (harness, after the run): every owner's `BUF_Y` and `BUF_VS` against the oracle, `S.n_recv`, the
   exits, no violations, every tile's ERRORS 0, HIST clear.

**What C&C's model must reproduce** (`CnC.java`, `Cnc1Peripheral.java`, `Endpoint.java`): the step queue (one register
action per cycle; `Instant` steps take none), `send` through the endpoint's holding register into tile 0's
from-controller route block (a refused put retried), `command()`'s parity and argument block, `go()` (all READY seen at
t, then at t + `cnc_reaction` clear READY and pulse GO; with `cnc_go_settle`, wait until `lastGoAt + go_latency + 1`),
READY bits in `Cnc1Peripheral` (a tile's pulse at its edge e is visible at e + `ready_latency`; cleared by the GO's
store), GO's net (`goNet`: every tile sees GO at t + `go_latency` + its skew). Not exercised by the sort: C&C's receive
FIFO, replies, reports, the error bit's consequences (it stays 0), the watchdog, the UART.

## 1. What the sort touches

### 1.1 Coverage (counters per step; `sim/tests/golden-m2/sort16k.cover.csv`)

Ref's own counters summed over the 64 tiles, snapshotted at every GO (`FpHooks`, `cover=`). At 16K, from the boot's end:

| unit | used by the sort (16K) | never in the sort |
|---|---|---|
| controller | 13.2 M instructions; taken-branch stalls; 2-cycle register loads; WFI (17.4 M cycles asleep), woken by GO (6,272) and by events (14,242); the table window: 131 K loads (HIST), 309 K stores (HIST, PTR, STREAM_*, GROUP) | multiplies (0), a refused send store (0), a REQUEST_SEND stall, traps, bus errors, illegal instructions |
| register block (TileControl) | 217 K sends (32- and 64-bit, SPLIT, XB, RED); 99 done pulses per tile; counter writes; DMA runs (13 per tile) | 128-bit sends, TRACE, counter wraps, done early, reserved words |
| router | 16.2 M injected, 25.4 M routed, 16.3 M ejected; C&C's from-controller input (tile 0) | the to-controller output, eject errors, route errors |
| injection point | 16.0 M read returns, 218 K sends | a send waiting behind a read return, a token wait |
| traffic director | modes 0 (commands, replies, read-port results), 1, 2, 3, 4 (gathers), 6,272 commands | mode 5, modes 6-7, unexpected arrivals, strays, bad packets, size 11, read-port overlaps |
| controller memories (port B) | 519 K data writes (commands, replies, read-port results); program memory only at the boot | |
| big SRAM | 13.6 M writes, 16.0 M reads | write combining, any area-queue stall |
| SRAM read queue | 13.6 M AG reads, 2.1 M gather reads, 295 K read-port reads (SPLIT) | its stall at a full area, a full token window, out-of-range reads |
| write engine | 13.6 M writes (modes 1, 2, 3: augmented 2.1 M) | mode-0 writes to big SRAM ("explicit"), a stall at the SRAM |
| address generator | 832 runs (stream and group), 13.6 M requests, **2.1 M refused puts on its holding register** (the SRAM read queue serves gathers first) | drift (group-run refusals), late rotations |
| table store, HIST | 2.5 M table reads, 13.6 M PTR RMWs, 8.4 M HIST RMWs | a controller access losing port B to a write-back |
| GATHER engine | 2.1 M requests (L1, L2) | a refused read |
| read port | 295 K reads (SPLIT only) | its overflow |
| request port, request bus | — | **everything: no request is sent** |
| peripheral slot (the replayer, type 4) | — | everything |
| C&C 1-3 | C&C 1's Java model (sends, READY, GO) | C&C 2 and 3 (endpoints idle), C&C's receive path |
| buses | every FIFO and hold above; no FIFO overflow, no read-return or send-hold refusal | |

**The register-block words the sort uses** (retire traces of tiles 0, 27, 63; identical sets):
loads `PARAMS`, `EVENTS` (consuming), `SRAM_RD_ADDR_QUEUE` (free entries); stores `PULSE_DONE`, `PULSE_DMA`,
`EVENTS_MASK`, **`EVENTS_REQUIRED`** (the all-together wake: used), `ROUTER_HDR`, `ROUTER_ADDR`, `ROUTER_SET_W0`,
`ROUTER_SEND_32`, `ROUTER_SEND_64`, `DMA_CONFIG`, `DMA_RECORD_BASE_ADDRESS`, `WRITE_COUNTER_1-3`, `SRAM_RD_ADDR_QUEUE`,
`SRAM_RD_DEST_ADDR`, `SIM_EXIT`; the table window: HIST loads and stores, PTR, STREAM_SRC, STREAM_DEST, STREAM_COUNT and
GROUP stores. Never: `STATUS`, `ERRORS`, `EVENTS_PEEK`, `ROUTER_SET_W1/W2`, `ROUTER_SEND_128`, `REQUEST_*`, `TRACE`,
counters 4-7, any counter load, PTR loads, the peripheral windows. Per tile: 99 commands, 99 GOs, about 320 wakes.

**The harness stand-ins** the C++ bench must offer: the boot's images (as packets through C&C: simulated, not a stand-in);
the input load (big-SRAM and data-memory pokes between cycles, before the first command); the command sequence (C&C's
model, above); verification (peeks after the run). `monitor()`'s per-cycle work is harness bookkeeping with no effect on
the run: the C++ bench need not reproduce it.

### 1.2 The split the unit groups follow

- **(A) The sort needs:** the controller (M1's, plus the table window and the send path), the register block (M1's subset
  plus the words above), the router (all six outputs; tile 0's from-controller input), the injection point, the traffic
  director (modes 0-4, the K-stage counter line), the controller memories' port B, the big SRAM, the SRAM read queue
  and its tokens, the write engine, the address generator, the table store and HIST, the GATHER engine, the read port,
  the links, C&C 1's Java model (CnC, Cnc1Peripheral's READY and GO, the endpoint's send).
- **(B) Only regs, wfi, the all-to-all and the mesh tests need:** 128-bit sends, counter wraps and the reads `regs` checks
  (`STATUS`, `EVENTS_PEEK`, `ROUTER_SET_W1/W2`, counter loads, `EVENTS_MASK/REQUIRED` loads) (regs; Quest-free measure:
  `M2Prog regs cover=`); wfi and a2a add nothing beyond (A). NetTest and EngineTest drive the mesh and the engines from
  Java (coreless chips: packets put on holds, engines configured by `Tile.set*`): their stimulus would have to be ported,
  and they add the broadcast trees under skew, the fifth port's to-controller output, FIFO overflows and the engines'
  corner cases (references: their logs and ref's per-chip digests, section 7).
- **(C) Nothing in M2 needs:** the request port and request bus (beyond holding their state empty), the peripheral slot
  and the replayer, C&C 2 and 3, C&C as cores (`CcCores`), the Tracer, the memory system, the UART, the watchdog, reset,
  the stage-B parking, M1's interrupt paths beyond what the counters make reachable (an interrupt from ordinary code needs
  an error source: none occurs in the sort).

## 2. The cycle as ref runs it, and the order that matters

`Chip.runCycle(t)`: (1) `reqBus.runCycle(t)` **first**; (2) the children: C&C 1-3's endpoints, tiles 0-63, `CnC` (shuffled
together when `shuffle_seed` ≥ 0); (3) for every awake tile: `readyPulseAt == t` → `cc.readyPulse` and PhaseLog, its
error line to `cc1p`; (4) `cc1p.runCycle`; (5) `cc.errorLine`, invariant 15; (6) `checkInvariants(t)` (with
`scanOverflows` every 16 cycles); (7) `sleepCheck` (idle-skip). `Tile.runCycle(t)`: the children `router, [srq; inj],
dir, cmem, sram, writer, ag, ts, hist, readPort, gather, core` (shuffled per tile), then `slot.runCycle`,
`slot.endCycle`, **`ctl.endCycle(t)` last**.

**Evidence that the children's order doesn't matter for the sort:** the canonical state (section 6) at the end of every
cycle is the same with the fixed order and with `shuffle_seed` 4, at n = 2,000: 51 interval fingerprints and every cycle
of a 200-cycle window, every unit of every tile (`M2Sort ... seed=4`, identical files). Also identical with idle-skip off.

**The order that does matter** (fixed in ref by construction, not by the shuffle):
1. The request bus before every tile: a `REQUEST_SEND` at t sees an entry freed at t (`freedAt == t`; AUDIT-M1 4). Not
   exercised by the sort.
2. **The SRAM read queue before the injection point, as one unit** (`Tile`'s `srqInj`): the injection point takes a
   loopback token for a send (`takeForSend`) against the count fixed at the queue's `advance(t)`, and frees a read's token
   (`injected`) after the queue has issued at t. Build them as one unit.
3. `TileControl.endCycle(t)` after every unit of its tile: it reads what they did at t (below).
4. The chip's READY and error-line pass after every tile.
5. **One order dependence ref leaves open, in a case the M2 programs never reach:** `TrafficDirector.decrement` and
   `schedule` call `ctl.pendingAdd(c, t)`, the write-counter store the core made at t, which `TileControl.write` records
   only if the core has already run at t. With the director first, the store is missed. The framework must not depend on
   order within a level, so the result has to come from structure: section 3.3 gives the model.

## 3. The units: state, ports, same-cycle couplings

Port kinds as in `sim/src/PORTS.md` (`Link(L)`, `Fifo(N)`, `Hold`, `Reg`, `Wire`, `Count`, credit `Fifo`). "At t, effect
t + 1" means: done during t, visible to anyone from t + 1. Every unit's canonical state is in section 6 (its order is
normative there).

### 3.1 Links and the router (`Router.java`, `Chip.java`)
- **Ports.** In: four links `Link(link_latency 3)` (capacity L + 2: the router takes every word the cycle it arrives),
  the injection bus `Link(intra_tile_latency 1)`, on tiles 0-2 the from-controller `Hold` (C&C's send). Out: four links,
  the eject bus `Link(1)` to the director, the to-controller output (tiles 0-2; never used by the sort).
- **State.** One `Fifo(queue_depth 16)` per (output, feeder) (`FEEDERS_TILE0` on **every** tile: every router has the
  fifth port's FIFOs; `attached` — `idx < 3` — only decides whether the to-controller selector runs), the round-robin
  pointers `rr[0..5]`. `maybe` is a hint (not state).
- **Within the router at t:** the selectors run first (each output takes one word from its FIFOs as at the start of t),
  then the route blocks put arrivals into FIFOs (visible t + 1). The eject selector takes the loopback FIFO only when
  every other eject FIFO is empty (`loopbackLast`).
- **Coupling:** a token-holding packet leaving the loopback FIFO calls `srq.loopbackLeft(t)`: the token is free from
  t + 1 (an event into the read queue's token count, `Link(1)` or a `Count` read through(t)).
- Tiles 0-2's routers differ only in `attached` (the to-controller output). Tile 0 also has C&C's input.

### 3.2 The injection point and the SRAM read queue (`InjectionPath.java`, `SRAMReadQueue.java`): one unit
- **Ports.** In: three clients — the gather `Hold` (priority 0), the AG's `Hold` (1), the read port's `Fifo(64)` (2); 16
  area returns `Link(sram_read_latency 2)`; the send `Hold` (the core's). Out: the SRAM read `Hold`, the read-return
  `Hold`, the injection `Link(1)`.
- **State.** 64 token slots (request words, issue cycle, ready cycle, parts left, client, data, zero), the free list,
  the in-order return queues of the gather and the AG, the read port's ready set, the tokens held by loopback packets.
- **At t, in order:** `advance` (frees from t - 1 take effect; the start-of-cycle count is fixed), the area returns
  placed, one return released (gather's head, then the AG's, then the read port's earliest ready), then one issue (if
  the SRAM read hold refused t - 1's put: retry and stall). Then the injection point: a read return before a send; a
  send for the tile itself or a broadcast takes a token (`takeForSend`) or waits.
- **The token free list's order is behaviour, and the C++ must reproduce it exactly.** Two read-port returns ready in the
  same cycle are released in token order (`rpReady` is a `TreeSet` ordered by (ready cycle, token)); a token is taken from
  the head of the free list (`free.poll()`) and returned to its tail when its read is injected (`free.add(tok)`), the list
  starting 0, 1, ..., 63. Two read-port reads to different areas can complete in the same cycle, so the token numbering
  decides which result is injected first (and so which reply lands first). The token number also travels in the packet's
  header bits [59:54] (simulator-only) and in the area queues' entries. Evidence: `SRAMReadQueue.java` lines with
  `rpReady`, `issue`, `injected`, `release`; the read port issues 295 K reads in the sort's SPLIT. So the canonical state
  keeps the token numbers and the free list's order (section 6). (Whether a same-cycle tie actually occurs in the sort is
  not measured; the rule must be reproduced either way.)
- **Couplings.** From the router: `loopbackLeft(t)` (above). To the read port: `injectedAt(t)` (its entry free from
  t + 1). Read by `TileControl.endCycle`: `gather.busy` reads `inFlightOf(GATHER)`.

### 3.3 The traffic director and the controller memories' port B (`TrafficDirector.java`, `ControllerMemories.java`)
- **Ports.** In: the eject `Link(1)`. Out: port B `Link(cm_latency 2)`, the write engine's input `Fifo(we_in_depth)`, the
  gather queue `Fifo(16)`, the peripheral write (unused).
- **State.** The write counters `remaining[1..7]` (20 bits); the K-stage line (K = 8): pending decrements (due cycle,
  counter).
- **At t:** the decrement due at t first (a counter at 0 stays 0: unexpected), then one packet.
- **Couplings.** `ctl.pendingAdd(c, t)` (below); `readPort.acceptedAt(t)` and `readPort.outstanding()` (the overlap
  check); `remaining[]` read by `TileControl` at `endCycle(t)` (the counter events) and by the core's counter load
  (`counterAtStart`: the value at the start of t, so order-free); counter stores add to `remaining[]` at `endCycle(t)`
  (`apply`).
- **The write counters' arithmetic, and `pendingAdd` (section 2, item 5).** ref's result with its fixed order (the core
  before the director) is, for counter c in cycle t, with N the core's store to c at t (0 if none) and D whether a
  decrement of c falls due at t (accepted at t - K):
  - **judgement:** if D and `remaining + N == 0`, the arrival is unexpected: the counter stays where it is, ERRORS [2]
    (`unexpectedTotal`) at the end of t; otherwise a due decrement takes one;
  - **the value at the end of t:** `remaining - (D and not unexpected) + N` (wrapping at 20 bits, flagged);
  - **during t** every reader sees the value at the start of t (`counterAtStart`; the core's counter load samples it);
  - the stray diagnostic at an acceptance at t (invariant 9, a violation only with `checkUnexpected`) uses
    `remaining - (decrements in the line) + N` the same way.

  **Model (order-free, equal to ref's core-before-director in every case):** do the counter arithmetic in the register
  block's shifted end of cycle (PORTS.md 7), the place `TileControl.endCycle` already holds. The director keeps its K-stage
  line and, at t, emits "decrement c falls due" as a `Link(1)` event to the register block; the core's counter store at t
  reaches the register block as its usual `Link(1)` store; at level 0 of t + 1 the register block applies both by the
  rule above (judge `remaining + N == 0` with D, then subtract and add), then the counter events, then STATUS. Both inputs
  are registered, so no order within any level matters. During t + 1 the core and the director read the counters as the
  register block's `RegFile` (the start-of-cycle value, as `counterAtStart`). The stray diagnostic moves with it (the
  acceptance's counter as a `Link(1)` event; or keep it in the director, reading the pending store through the same
  path, as it is a diagnostic only). *Alternative:* the core's pending store as a `Wire` (value, counter and the cycle it
  was written: a Wire keeps its last value while the core sleeps, so the reader must check the cycle) read by the director
  at a later level; exact too, but it ties the director's level to the core's for a case that never occurs. *Not
  equivalent:* registering the store for t + 1 while the director judges at t (the judgement would miss N).

  **The M2 programs never reach the case** (`FpHooks pendcheck=1`: cycles in which a tile's core stores a write counter
  and its director decrements or accepts a packet on that same counter): **0** in the sort at 16K (fixed order), at
  2,000 with the children shuffled (seed 4), in regs, wfi and a2a; ref's unexpected arrivals and strays are 0 in all of
  them (`dir.unexpectedTotal`, `dir.strays`). So the model matters for exactness in principle (a program that arms a
  counter in the cycle a stray decrement falls due), not for any M2 reference.
- **Port B** (`ControllerMemories`): one write per cycle into data or program memory (four word-interleaved banks); a
  write covering command slot 0 sets `landedAt = t`, which `endCycle(t)` latches (command landed, EVENTS [9]). The
  memories commit writes at the end of the cycle (`SRAMMacro`): a core load at t + 1 sees a port-B write of t.

### 3.4 The big SRAM (`BigSRAM.java`)
- **Ports.** In: the read `Hold` (from the read queue) and the write `Hold` (from the write engine). Out: 16 area
  returns `Link(2)`.
- **State.** 16 area queues (depth 8) of (read or write, row, chunk mask, token and part, or four data chunks); the
  contents (writes stored when served: immediately).
- **At t:** every non-empty area serves its head (as at the start of t), then the write hold's request is queued, then
  the read hold's, each only if its area's count **before the cycle's pop** has room (13a, `fifoCountBeforePop`); one
  left in its hold makes the producer's next put refused (never in the sort). Write combining is on but never applies.

### 3.5 The write engine (`WriteEngine.java`), the table store and HIST (`TableStore.java`)
- **Write engine.** In: `we-in` `Fifo`, PTR returns `Link(ts_engine_latency 2)`. Out: PTR requests and HIST requests
  `Link(2)`, the SRAM write `Hold`. State: the pointer stage (2L + 2 entries: packet, PTR value when back) and the order
  awaiting PTR returns; DMA_CONFIG's digits and the record base (set at `endCycle` by `setMasterConfig`/`setRecordBase`).
  At t: a PTR return; the write side (retry a refusal, else the stage head if its PTR is in); one packet in.
- **Table store.** In: three request `Link(2)`s into three `Fifo(16)`s (STREAM, GROUP, PTR). Out: three return `Link(2)`s.
  Memories: STREAM_SRC, STREAM_DEST, STREAM_COUNT (256 × 32), GROUP (64 × 64), PTR (256 × 21). PTR's read-modify-write
  pipe: read at t, write back at t + 1, with a bypass.
- **HIST.** In: the request `Link(2)` into a `Fifo(16)`; HIST (256 × 17) with the same pipe.
- **The controller's table window** (`Controller.window`): HIST and PTR through port B: a store is lost and a load
  returns the engine's value when a write-back happens in the same cycle (`RmwPipe.wbSlotAt` handles either order:
  order-free); STREAM_*/GROUP through their own port. Not met in the sort (counted: 0).

### 3.6 The address generator (`AddressGenerator.java`) and the GATHER engine (`GatherEngine.java`)
- **AG.** In: the STREAM and GROUP returns `Link(2)`. Out: the STREAM and GROUP requests `Link(2)`, its `Hold` into the
  read queue (client 1). State (section 6): the run (kind, start, count), status (running, done, the start pulse,
  lead-in), the two read-ahead queues (entries and reads in flight), the STREAM reader's position, the stream-run and
  group-run state machines (absolute cycles `groupS`, `rotSched`). **Its hold is refused 2.1 M times in the sort** (gather
  reads go first): `refused`, `retry` and `heldAtStart` must be exact. Started by `PULSE_DMA` (`ag.pulse` at `endCycle(t)`,
  acting at t + 1); `rd_done` and `rd_running` are read by `endCycle` (EVENTS [10]).
- **GATHER.** In: `gather-q` `Fifo(16)`. Out: its `Hold` (client 0). State: k, base, the active request (left, ordinal,
  index, counter, wide).

### 3.7 The register block and the controller (`TileControl.java`, `Controller.java`)
AUDIT-M1 sections 0-6 hold, with these additions for the sort:
- **Sends** (`ROUTER_SEND_32/64`): the store at t puts the packet on the send `Hold` at t (header from `ROUTER_HDR` and
  `ROUTER_ADDR` as they stand, payload from the store and `ROUTER_SET_W0`); `apply` at `endCycle(t)` advances
  `ROUTER_ADDR` (mode 0 without no-increment) and clears W0-W2. A refused send (never in the sort) stalls the core.
- **The write counters**: a store adds at `endCycle(t)`, arming the per-counter and counters-done qualifiers; the events
  [6:0] and [7] rise on the counters' falling-to-zero edge at `endCycle` (not while the done gate is closed).
- **EVENTS_REQUIRED** is used: the wake term's all-together half (AUDIT-M1 2).
- **DMA_CONFIG, DMA_RECORD_BASE_ADDRESS, PULSE_DMA**: applied at `endCycle(t)` into the AG, the write engine, the gather
  engine; `PULSE_DMA` clears EVENTS [10] and starts the AG at t + 1.
- **The read port** (`SRAM_RD_ADDR_QUEUE` store): `ReadPort.store` is called **during the core's step at t** (a put on
  its `Fifo(64)`, visible t + 1; free entries judged by stores before t and injections before t); `apply` at `endCycle(t)`
  advances `SRAM_RD_DEST_ADDR`. A load returns the free entries at its sampling cycle.
- **`endCycle(t)` reads** (all at the end of t, after every unit): GO due at t; the core's store; `cmem.landedAt`; the
  event counts (every overflow count, AG drift, read-port errors, the director's counts, bus errors, illegal
  instructions, done early, receive underflows) for ERRORS; `ag.doneForStatus` (EVENTS [10]); `req.busy` (EVENTS [11]);
  `dir.remaining` (counter events); then STATUS for t + 1: `ag.runningForStatus/doneForStatus`, `gather.busy`, the send
  hold's emptiness, done, landed, `req.busy`. In the framework: the shifted end of cycle (PORTS.md 7): level 0 of t + 1,
  from `Link(1)`/`Count` inputs.
- **READY** (`readyPulseAt == t`) is collected by the chip after the tiles; GO is scheduled per tile by C&C's `goNet`.

### 3.8 The request port and bus, the peripheral slot
Not exercised (section 1.2 (C)). The request FIFO and the bus must exist only as far as `REQUEST_SEND`/`STATUS` would
read them (empty). The slot's live events and errors are 0.

## 4. Chip-level work, event-driven (D23)

ref does these every cycle, whatever happens; the framework must drive each by its cause:

| ref, per cycle | cost in ref (W29's profile, idle-skip on) | the event that should drive it |
|---|---|---|
| **`RequestBus.runCycle`**: credit returns, deliveries, every node input's arrivals, **a scan of all 64 tiles' request FIFOs**, 8 row nodes, the root | 33% | a push into a request FIFO, a beat arriving, a credit returned. **D23's first concrete case: in the sort the request bus never carries a request (0 sends, 0 beats at the root), so the whole 33% is scanning empty FIFOs**; event-driven it costs nothing. |
| the READY and error-line pass over 64 tiles | part of the chip's 17% | a READY edge (`readyPulseAt`) or an error line's change, posted by `endCycle` |
| `checkInvariants`: 64 tiles × 14 violation counters; `scanOverflows` every 16 cycles over every FIFO | 19.5% (overflows) | an overflow is judged at delivery (PORTS.md 3): count it and report it there; a violation is reported where it is counted |
| `CnC.runCycle`: its step re-checks `allReady` (64 bits) every cycle | small | the 64th READY's arrival (`ready_latency` after its edge) schedules the step |
| `cc1p.runCycle`, `cc.errorLine` | small | the watchdog's deadline; error-line changes |
| `sleepCheck` / `canSleep` / `quiescent` (idle-skip's own scans) | — | not needed: the framework's ready lists |
| `SortTest.Machine.monitor()` (harness: 64 tiles every cycle) | — | not reproduced (harness bookkeeping) |

## 5. Unit groups for parallel workers, and the order

Groups follow section 1.2 (A) first, each checked with the canonical fingerprint (section 6) against `golden-m2`:

1. **G1, the tile's control** (one worker, starts first; extends W5's controller): the register block's additions (3.7),
   the controller's send path and table-window decode, the director's counters and K-line (3.3), port B. regs and wfi
   send to themselves through the router, so G1's first check comes with G2's router: **`regs` and `wfi`, every unit of
   tile 0 every cycle.**
2. **G2, the network** (one worker, in parallel with G1): the router (3.1), links, the injection point's send side, the
   eject path, C&C's model (0) and the chip's READY/GO lines. Checked first on `wfi` (self-sends) and the boot of any
   program (C&C's broadcasts: the boot is 14,791 cycles of pure network and port-B traffic before any engine runs).
3. **G3, the memory path** (one or two workers, after G1's register block exists): the big SRAM (3.4), the read queue
   and tokens with the injection point's read side (3.2, one unit), the write engine (3.5), the table store and HIST,
   the read port. Checked on `a2a` (AG, read queue, write engine, mode 1) then the sort's SPLIT (the read port, replies).
4. **G4, the engines** (after G3): the address generator (3.6, group and stream runs, its refused hold), the GATHER
   engine, modes 2-4 at the director. Checked on the sort's L1 (all of them at once: AG, gather, HIST, buckets).
5. **The sort at 16K, then full size**; then speed (PLAN-M2's order). Section 1.2 (B)'s extras (128-bit sends, counter
   wraps, the reads regs checks) come with G1; NetTest/EngineTest's stimuli are ported only if a mismatch needs them.

Framework items the groups need (from PLAN-M1's carried list): the shifted end of cycle for the register block
(TileControl reads ten units at `endCycle`); the reserved `Link` is not needed (no backpressure FIFO in the sort); Holds
with refusal and retry under load (the AG's 2.1 M refusals); a Link whose consumer holds words (the router's FIFOs
double-hold, W4) is a speed item, not a correctness one; the chip-level events of section 4.

## 6. The canonical state fingerprint

### 6.1 Why not `Chip.digest()`
`Chip.digest()` (`-Dmeshsim.digest`, at exit) hashes, per tile, 20 of ref's **statistics counters** (`mcycle`,
`cyclesWfi`, `cyclesHalted`, `cyclesStalled`, `instructions`, `pc`, `halt`, `profCycles`, the router's `injected`,
`routed`, `ejected`, the director's `accepted`, the SRAM's `accesses`, `gos`, `readies`, `dones`, `conditions`, the
write engine's and read queue's stall cycles, the request port's sends), the slot's counters, then the chip's violation,
log and condition counts. It sees no memory contents, no queue contents, no register beyond `pc`, and it depends on
counters the C++ need not keep (and on their exact definitions: `profCycles` is a simulator profile). It is useful as
ref's own "nothing changed" check (section 6.5), not as a cross-implementation reference.

### 6.2 The definition
At the end of cycle c (after `Chip.runCycle(c)`, before c + 1), each **unit** writes a defined sequence of 64-bit words;
its fingerprint is a hash of that stream. The normative per-unit streams are `StateFp.java` (one method per unit, the
words named): 16 units per tile (`core, ctl, cmem, dir, router, inj, srq, sram, sram.data, we, ag, ts, hist, gather,
rdport, req`) and 4 at the chip (`cnc, cc1p, reqbus, ep`). **What is in a unit's stream:** its architectural state
(registers, CSRs, `mcycle`, counters, flags, state-machine variables, configuration), its memories, and **every bus that
delivers into it** (what is queued or in flight towards it), in a fixed order. Excluded: statistics, checkers' marks,
hints (`maybe`), per-cycle temporaries, harness state.

- **Hash** (both sides): `h = 0x6A09E667F3BCC908; for each word w: h = fmix64(h ^ w); at the end h = fmix64(h ^ n)`, n the
  word count; `fmix64` is MurmurHash3's finaliser. Lists are written as their length, then their entries.
- **A memory** is one word: `Σ fmix64(region << 56 ^ index << 32 ^ value)` mod 2^64 over its non-zero 32-bit words
  (regions: data memory 1, program memory 2, big SRAM 3, STREAM_SRC 4, STREAM_DEST 5, STREAM_COUNT 6, GROUP 7, PTR 8,
  HIST 9; the index is the word's (chunk's) index). Order-free and zero-free, so it can be kept incrementally (a write
  changes the sum by two terms) and an unallocated area equals a zeroed one.
- **A bus**, as at the end of c: a `Link`: its entries in order, each (arrival cycle, a, b, c); a `Fifo`: its entries
  (a, b, c), then whether a put was made at c and its words (not yet judged: the framework judges it at c + 1); a `Hold`:
  occupied and its word, then the put made at c. The three words are ref's (payload [63:0], the header as ref lays it
  out **including its simulator-only bits** — the token [59:54], from-queue 62, token-held 61, read-port 63 — and
  payload [127:64]), D17's three-word link word.
- **Reading ref without moving it**: ref settles a bus lazily (its first touch after the put commits it, `Bus.java`), so
  the reader never calls a bus method: it reads the private fields and settles a put made before c itself, by the bus's
  own rule (`WordFIFO.commit`'s start-of-cycle fullness, `WordHold.commit`'s refusal); memory macros' uncommitted port
  writes are overlaid. A sleeping tile's `mcycle` is caught up as `Chip.catchUp` would.
- **Output**: per call, `cycle unit hash` for every unit (1,027 lines), then `cycle chip hash` over them in order.
  `fpdump=C:UNIT` prints one unit's stream word by word with names, the C++ side's comparison target.

### 6.3 How each side computes it
- **ref** (built): `StateFp` reads every field by reflection between cycles; `FpHooks` calls it every `fpevery` cycles
  (default 10,000), at every GO and all-READY, at the end, and every cycle in a window `fpwin=A-B`. In a window the big
  SRAM's sum is kept incrementally: before each cycle, the rows the area queues' head writes will store are read, and
  after it the sums are corrected; a full scan at the window's end must agree (it does; checked every run).
- **C++** (with the units): each unit type implements the same stream from its own state (a `fingerprint(out)` per type,
  a debug/test facility, not in the cycle loop); memories keep their sums **incrementally** at every write (two
  `fmix64`s and an add per written word), so a per-cycle fingerprint never scans a memory. Buses are written from the
  port's ring and the delivery buckets (puts of c are the bucket entries for c + 1).

### 6.4 Cost (ref, 16K sort, at cycle 300,000; all 64 tiles; `fpcost=`)

| unit | µs (64 tiles) | | unit | µs | | unit | µs |
|---|---|---|---|---|---|---|---|
| **sram.data** | **82,311** | | srq | 904 | | dir | 142 |
| cmem (two memories) | 1,520 | | ag | 609 | | ctl | 138 |
| router | 1,242 | | ts (five memories) | 410 | | gather | 93 |
| sram (queues) | 226 | | core | 212 | | inj, rdport, req | 42, 31, 19 |
| we | 167 | | hist | 148 | | chip units | 26 |

About 88 ms a call with the big SRAM, **about 6 ms without it**. The big SRAM is 93%: in ref it is a full scan of every
allocated area (64 × up to 16 × 128 K chunks); every other unit is small. So the C++ side must keep the **memories' sums
incrementally** (big SRAM above all; controller memories and tables too, for per-cycle use), and then a full fingerprint
is a few microseconds per tile. At 10,000-cycle intervals ref spends about 4 s per 16K run (248 calls); a per-cycle
window costs about 6 ms a cycle (10,000 cycles: one minute).

### 6.5 Proofs (local, n = 2,000 unless stated)
- **Neutral:** with fingerprints at 749 points, a window, dumps and the coverage counters on, ref's digest is unchanged
  (`f25e2469...`, as SortTest's own run) and TOTAL is 250,338 as SortTest's; at 16K, `51c8f9a4...` with the observer on
  or off; every golden-m2 `.out` of a program is identical with fingerprints, traces or neither.
- **Implementation-independent reading:** fingerprints identical with idle-skip off (`-Dmeshsim.skip=false`: every bus is
  touched every cycle, so nothing is left to settle lazily), and with the children shuffled (seed 4).
- **Incremental = full:** the window's big-SRAM sums equal a full scan at its end (every run).

## 7. The references (`sim/tests/golden-m2/`)

`RUNS` lists every run; `regen.sh [--check]` rebuilds ref and the drivers in a scratch copy of `sim/ref/meshsim` with the
committed images (`images/MANIFEST.md`), runs them and compares every file's sha256 with `HASHES` (all SAME locally:
34 files of the small runs, the 16K set regenerated). In git: the small runs' files and the sort's facts, coverage and
PhaseLog; by hash only: the whole-chip fingerprints, the 16K and full sort's fingerprints and traces (regenerable).

| run | what | where |
|---|---|---|
| `regs`, `wfi`, `a2a` | `M2Prog` (CtrlTest's runs): end cycle 869 / 1,616 / 7,698, results, the end fingerprint, ref's digest; tile 0's fingerprint every cycle (a2a: every 10); retire traces of tiles 0, 27, 63 | git |
| `regs-chip`, `wfi-chip`, `a2a-chip` | every unit of every tile, every 10 (a2a 100) cycles | hash |
| `net`, `engine` | NetTest -1 quick, EngineTest -1: the log and ref's per-chip digest (171 and 23 chips) | git |
| `sort16k` | 496,619 cycles, verification, PhaseLog, coverage | git |
| `sort16k-fp`, `-t0`, `-t27`, `-t63` | fingerprints every 10,000 cycles and at every GO and all-READY; retire traces (225-232 K lines) | hash |
| `sort64k` (+ `-fp`, `-t0/27/63`) | the full sort, the same set | runner, Quest 134 |

The D12 header: each trace carries TraceRun's (configuration, images, boot ROM); the sort's `.out` starts with SortTest's
`inputs:` line (every file's sha256); every `.fp` file starts with the run (driver, program or key set, ready latency,
seed), its inputs' sha256 (images, boot ROM, hops, the key file), every `Config` field and the start skews. A C++ bench
compares its own configuration with these lines before the hashes and refuses on any difference.

## 8. Localising a mismatch

**What the references give, coarsest first:** (1) the end facts (cycles, verification); (2) PhaseLog's CSV (each phase's
GO and all-READY cycles: the first phase whose times differ); (3) the interval fingerprints (every 10,000 cycles and at
every GO and all-READY: the first point and **the first (tile, unit)** that differs); (4) `fpwin=A-B` over that interval
in ref, and the C++ side's per-cycle fingerprint: **the first cycle and unit** that differs; (5) `fpdump=C:UNIT` on both
sides: **the named word** that differs (a register, a counter, a queue entry, a bus's pending put); (6) the tile's retire
trace, for the controller.

**Is that enough to develop units one at a time?** For localising, yes: a unit's stream includes every bus delivering
into it, so "this unit's put at cycle t differs" shows as the destination's stream differing at the end of t (the put
is listed as pending, with its words), and the first differing (cycle, unit, word) is found in two runs. **What it does
not give is stimulus**: it checks a unit, it cannot drive one in isolation, as M1's bench replayed `MEMW`/`GO`/`CMD`
into a lone controller. That matters only if a unit is to be tested before its neighbours exist. The groups of section 5
are built to run whole programs early (G1 with G2 on regs/wfi), so the fingerprint suffices.

**It stays a proposal** (owner, 2026-11-07: not now): the fingerprint finds the first differing cycle, unit and word,
which is enough to develop G1-G4 against whole-program references. It is available if a unit ever needs testing before
its neighbours exist. **The proposal (D5 would grow):** a put/take trace of one
tile's named buses. Hooks: `WordDelayLine.put/take`, `WordFIFO.put/take/commit` (an overflow), `WordHold.put/take/
commit` (a refusal), each one guarded line as D5's, filtered to the traced tile by the bus's name (`t5.we-in`); plus the
direct same-cycle calls of section 2 (5 call sites: `loopbackLeft`, `takeForSend`, `injected`, `injectedAt`,
`pendingAdd`). About 10 call sites in 4 files. Volume: tile 0 at 16K moves about 1 M packets through its buses (routed
400 K, SRAM accesses 460 K, ...), each a few puts and takes: 3-5 M lines, ~200 MB per tile; full size about 4×. Proof:
Quest 123's quick-suite proof again (39 steps, off and on). Not built.

## 9. Findings for the record
- `p4/uniform-k128` reads KeyGen's `uniform.keys`, not SortTest's `kind=uniform` (section 0).
- Quest 120's 110 s is the whole JVM (key file, chip, boot, run, verification); Quest 134 splits it.
- The read queue's token free-list order is observable behaviour (3.2).
- `TrafficDirector`'s `pendingAdd` makes an error-case judgement order-dependent (section 2, item 5).
- `RequestBus.runCycle` scans 64 empty request FIFOs every cycle of the sort (section 4).
