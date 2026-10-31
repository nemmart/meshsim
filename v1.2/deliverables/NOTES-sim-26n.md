# meshsim to 26n: running notes (simulator session, 2026-10-27)

## Commit A: link word, modes, counting, register map, wake events (units stubbed)

Expectation changes, old -> new, and why:

| suite | change | why |
|---|---|---|
| NetTest | header 40 -> 41 bits, addr 20 -> 21 bits | 21-bit addresses |
| NetTest director | rewritten: modes 3 (explicit, aligned), 7 (command slot addr[3:0]), 8 (reply by source, 32/64), 9 (type-0 slot: bad peripheral write, counted); 7 bad-packet cases (was 3); C&C's packets counted | part C (k.1) |
| NetTest director | address checks: 8 -> 5 violations (invariants 12, 13 withdrawn) | k.1.5 |
| NetTest C&C port | the 64 packets to dst 65 are now flagged (2.1: no endpoint) | 2.1 |
| MemTest | wrap at 2^21 on an 8 MB device; 4 MB top-of-space reads 0; a 1.25 MB device with a half-height area; sizes not a multiple of 256 KB refused (163 -> 170) | 21-bit addresses, size parameter |
| MemTest, ReadPortGatherTest | read-port results are mode 6, counted in DMA_CM_REMAINING (were mode 7 / REPLY) | k.1.6 |
| ReadPortGatherTest | reserved stream codes 0, 7, 8, 10, 15 (were 0, 6, 7, 9, 15); 64- and 128-bit reads; a misaligned read (condition 7) | k.1.1, k.1.6 |
| AgTest | computed fields for modes 6 and 9 (were 5 and 8); 21-bit wrap | k.1.1 |
| CtrlTest registers | counters add; three writes past 2^20 - 1 wrap and are flagged (192 expected violations); per-counter done events stay pending at the reset mask; 4 WFIs | m.1 |
| CtrlTest wfi | GO is sticky: a WFI after a missed GO falls straight through (was: waits for the next GO); done clears counters done | i.1, 4.12 |
| CtrlTest fields | 40 offsets; TILE_STATUS [19:16] the only spare; 15 conditions; ROUTER_SET_HDR uses all 32 bits | 8.1, 8.2, 8.3 |
| CondTest | cases 0 and 4: the receiver of dropped traffic parks; the harness HALTs it and checks counters, events, queues and conditions are clean | 4.12 (a counter is never overwritten; HALT recovers) |
| HooksTest | rewritten for the part E map, the send-port reorder, TILE_PULSE [2]/[3], the empty slot, the unwired UART | j, k, l, m |
| RegDocTest | PENDING until RegDoc is regenerated | |

## Commit B: the peripheral slot with the replayer (cp6)

- `Peripheral.java`, `Replayer.java`, `ReplayerAlu.java` and the replayer's tests copied unchanged from replayer-cp6.
- `PeripheralSlot`: type from `Config.periphType` (default 1, every mesh tile) or `periphTypes[]`; `runCycle` every cycle
  (HALT included); mode 9 handed over at the director, counted on `landed(t)` in RUN only; run, lines, status and claims
  at the sample cycle; `setHalted` on HALT, HALT -> RUN and a halted controller, only on a change; conditions 11-13 from
  the peripheral; a mode-9 packet in the rp_latency path counts as in flight (invariant 7, quiescence).
- Tests that test the empty slot set `periphType = 0` (NetTest director, AgTest computed fields, HooksTest).
- New: `PeripheralSlotTest` (43, with a test-only peripheral, type 15, one line of each kind), `ReplayerTest` (1,173, the
  owner's suite) in run_tests.sh. Quick suite: 208 s -> 219 s with a replayer in every tile.

## Commit C: the sender

- `Sender` (4.17.9, 8.12) with a test-only recording sink at its output port (nothing is attached on tiles 0-63).
- New: `SenderTest` (48): splits for n mod 4 = 0-3, addr stepping for modes 3/6/9 and 0 otherwise, busy and [20], a count
  of 0, pacing by the peripheral, ignored stores (busy, no such queue, no output), HALT abandoning a send.

## Commit D: the request port and the request bus

- `RequestPort` (4.18): the 128 x 128 request FIFO (stall rule, request_busy, [12] on its fall), the 16 x 128 receive
  queue in every tile (GET, RECEIVE, [19] live, condition 14, HALT keeps it).
- `RequestBus` (3.3): eight row nodes and a root, 16-bit beats over Config.reqHopLatency-cycle (3) pipelined wires with
  credit flow into one-request buffers, wormhole per request, requestor stamping at the row node, the root's
  round-robin skipping a target with no credit, 16 credits per target returned by each pop, drops for reserved targets
  and targets with no receiver (flagged). It runs first in every cycle.
- Targets have receivers only through the test-only strap `Config.targetReceiver` (DEVIATIONS.md).
- New: `RequestBusTest` (49) and `sw/tests/req.c`. Measured: 17 cycles unloaded, from the REQUEST_SEND store to the
  request in its target's queue, the same from every row; one request every 8 cycles at the root under saturation.

## Commit E: tokens, per-client return queues, the loopback and the fifth port

- `SRAMReadQueue`: one pool of 64 tokens (reads in flight plus loopback packets), freed at injection, except a loopback
  packet's, held until it leaves the loopback FIFO; in-order return queues for the address generator and the GATHER
  engine, read-port returns unordered; one return a cycle by request priority. Room judged on the start-of-cycle count.
  The read queue and the injection point run as one unit (they share the pool).
- `InjectionPath`: a send-port packet for the tile itself or a broadcast waits for a token.
- `Router`: the loopback FIFO 64 deep (Config.loopbackDepth, default the token count); the eject selector serves it only
  when every other eject FIFO is empty; every router has the fifth port, connected only on tile 0.
- Simulator-only header bits: [46:41] a read return's token, 62 a read return from the queue, 61 a token-holding
  loopback packet (no routing or landing effect).

Expectation changes, old -> new:

| suite | change | why |
|---|---|---|
| ReadPortGatherTest | read port under an all-to-all: 561 packets lost -> 0 (the check now requires 0) | the token-protected loopback served last (4.5, 4.7): the v1.0 hazard is gone; items 24 and 70 re-measured |
| ReadPortGatherTest | read-port tolerance: 32 results against a full-rate stream overflowed -> no overflow (now required) | as above |
| ReadPortGatherTest | packets accepted with results outstanding 88,137 -> 9,527; gather from C 12,334 -> 12,335 | the loopback waits for the links |
| MemTest | token window: issue waits 76 -> 80 cycles; read latency max +1 | a token is freed at injection, not at the hold |
| AccountingTest | a2a phase unchanged (3,134); its read-latency column 4 -> 5 | latency now measured to injection |
| MemTest (new) | a GATHER read of a free area passes the AG's queued reads; the AG's stay in order (170 -> 173) | per-client order (item 77) |
| NetTest (new) | the loopback under a full-rate stream: peak 64 of 64, the send port waits 167 cycles for tokens, no overflow, self-sends after the stream; the fifth port on every router, unconnected on tiles 1-63 (30,531 -> 30,537 + 64) | 4.5, 4.7, 4.16 |

## Commit F: register-load latency 2 (the reference setting), and GO's event timing

- `Config.regLoadLatency` = 2 (4.11: meshsim models 2, the worst case): a register or table-window load holds the core
  one extra cycle and samples in its last cycle; its side effect (a consuming WAKE_EVENTS load, REQUEST_RECEIVE) is at
  the end of that cycle. Data-memory loads and every store are unchanged.
- GO seen by a tile at t now sets [8] at the end of t, visible to loads and WFI from t + 1 (4.12's timing). Before, a
  WFI woke, and a load saw GO, in the cycle the tile saw it: one cycle earlier.
- New in HooksTest: the sampling rule at L = 1 and L = 2 (an event pending at the sample point is returned and
  consumed; one set in the load's last cycle survives; each load costs L cycles) (143 -> 153).
- The quick suite passes unchanged at L = 2 (its cycle-exact expectations are relative). The measured figures move:
  see the L = 1 / L = 2 table in SIM-CHANGES.

## Commit G: RegDoc and REGISTERS.md's generated blocks

- RegDoc for 26n: the 40 offsets (the request port's store and load names together), every field (TILE_STATUS's type,
  endpoint id and 256 KB size; TILE_PULSE's PERIPH_RUN and GO; the read port's setup register; WAKE_EVENTS' layout;
  the request words; SENDER_SEND; the UART), the 15 conditions, the modes table, the 169-bit link word, the packed-word
  map with both blocks, the three controller-memory producers (all mode 6), the command block's slots.
- REGISTERS.md: every generated block regenerated; the send port's marker renamed W1-W3 -> W0-W2 (the reorder); three
  parentheticals that said the generated tables were stale removed (prose only, no other prose edited).
- RegDocTest back in run_tests.sh (197 -> 283): 44 names at 40 offsets with no holes, 15 conditions, the withdrawn
  names absent, resets (WAKE_MASK, UART_DIVISOR on an unwired tile, TILE_STATUS's type and id), additive counters,
  the modes table's addr steps against the send port. Its split_regs.h check stays PENDING until the sort is ported.

## Commit H: replayer-cp9

- `Replayer.java` from cp9 (PERIPH_STATUS_0 keeps the next offset after a pass that ends mid-expansion, 4.19.9);
  `Peripheral.java` and `ReplayerAlu.java` unchanged since cp6. cp9's tests (`ReplayDelivery`, the loader), `sw/replay/`
  (the Stage B controller), `guests/item113/` and its notes copied in unchanged.
- ReplayerTest 1,173 passed; `ReplayDelivery` on delivery 2's five guest windows: 327,680 rows, 0 failed.

## Commit I: the recipes, RECIPES.md and CONVENTIONS.md ported

- `sw/tests/rcp_*.c`, `rcp.h`: the three STREAM arrays, TILE_PULSE, the renumbered mode names, the send-port reorder.
  Semantic changes: the one-word exchange sends 32-bit replies, landing by source, and copies `REPLY[2 s]` out;
  `send_words` sends mode-6 writes counted in DMA_CM_REMAINING; the read-port recipes use the setup register and
  DMA_CM_REMAINING; self-replies land at `REPLY[2 me]`; commands carry 7 arguments at most (rdport: 7 queries, was 8;
  cmdloop: a full block of 7, was 31).
- Rule 4 replaced: v1.1 allows mixed read sizes in a group (item 27), so "one read size per group" no longer bites.
  The new rule 4 is row-aligned elements in a group run: right, a 64-bit and a 32-bit stream in one group; wrong,
  64-bit elements from chunk 3 drift on every tile. Rule 5: C&C halts the tiles before its uncoordinated staging
  write, then boots them again (counted data never reaches a running tile unannounced).
- RECIPES.md: code blocks resynced (`tools/recipes_sync.py`), the cycles table regenerated, prose for the 26n modes,
  replies, the read port, the counters and the start path. CONVENTIONS.md rewritten for 26n.
- RecipeTest back in run_tests.sh (2,325 passed, and shuffled). Start path, GO store to first request, at 2-cycle loads:
  stream run 12-13 (v1.0: 9-10), group run 20-21 (17-18).

## Commit J: the sort, the split and the planner ported

- **The split** (`sort/split`, mirrored in `meshsim/sw/ext/split-bisect`): `split_regs.h` at mesh.h 1.1's addresses,
  its `wfi()` consuming counters done; every exchange as replies, landing by source: n, seeds, probes and XA's cuts as
  32-bit replies read from the reply block (or copied out), the counts as one 64-bit reply (lb, ub) per owner (the
  split's paired words: owner's ruling, combine into one reply). The read port's searches use the setup register and
  DMA_CM_REMAINING. `split-barrier.patch` regenerated; BINDING.md updated.
- **The sort program** (`sort/program/sw`, mirrored in `meshsim/sw/ext/sort/sw`): `xchg` and BT's half-word exchange as
  32-bit replies, copied out; BT's descriptor exchange (up to 5 words per sender per receiver, more than a reply carries)
  as mode-6 writes into `held[l][sender]`, then a per-sender marker word into `desc_in[sender]`, polled (one sender's
  packets arrive in order; owner's ruling, the mode-6 fallback); V counts one packet per 128-bit value (v1.1: a 128-bit
  element is one packet); `CMD_STAGE`: the tile arms DMA_SRAM_REMAINING with C&C's staging packet count and waits;
  the loader and data memory end below the reply block (0x5BC0: 484 B of stack headroom).
- **The planner** (`sort/skew/planner`, patched copy in `meshsim/sw/ext/skew-planner`): the tile build writes the three
  STREAM arrays and counts one packet per value; the host harness's shadow keeps design v1.0's encoding, so its
  expected results stand. `planner.patch` regenerated (EMPTY_HI as count 0, mode 2, the kind's size).
- **SortTest:** boot announces the overlay staging with `CMD_STAGE` (C&C sends 64 cycles after GO); totals start at L1.
- **Results:** every built-in set exact (124 checks, unshuffled and shuffled, totals identical); P1 54 checks; forced BT
  at 64 x 16,384 (trace hot 10 / 10 %) exact in both formats: 709,658 cycles (32-bit values; v1.0 701,028) and 927,893
  (128-bit; v1.0 998,040).

| set | path | v1.0 | 26n | change |
|---|---|---:|---:|---:|
| uniform-128 | direct | 257,089 | 258,194 | +1,105 |
| uniform-32 | direct | 237,054 | 241,114 | +4,060 |
| few-128 | direct | 239,122 | 230,226 | -8,896 |
| ragged-32 | direct | 222,233 | 226,214 | +3,981 |
| sorted-128 | LB | 253,776 | 255,791 | +2,015 |
| equal-32 | LB | 69,976 | 73,010 | +3,034 |
| equal-128 | LB | 87,805 | 88,997 | +1,192 |
| reverse-128 | TH | 287,938 | 289,132 | +1,194 |
| reverse-32 | TH | 258,372 | 263,245 | +4,873 |
| ragged-128 shaped | direct | 241,908 | 241,907 | -1 |

## Commit K: Stage B on the tile

- `test/StageB.java`: the Java-side loader of `sw/replay/replay_layout.h` (a delivery window into tile 0's big SRAM),
  C&C's boot of `replay_ctl` on tile 0, the pass, and Stage A's checks (every row byte for byte against rows.bin, the
  streams consumed exactly, the end state, the replayer clean, no meshsim violations). `-Dstageb.profile` gives the
  controller's pc during the replayer's new-expansion stalls. run_tests.sh runs it on the five guests when
  `DELIVERY=<replay-inputs dir>` is set.
- `replay_regs.h`'s every address agrees with mesh.h 1.1 (checked); `replay_ctl` built unchanged from cp9.
- Results (state variant; Stage A from the replayer session's logs, same windows):

| program | window | rows | Stage A cycles | Stage B cycles | Stage A /row | Stage B /row | new expansions |
|---|---:|---:|---:|---:|---:|---:|---:|
| lrd_x0 (delivery 2) | 0 | 65,536 | 196,712 | 196,875 | 3.002 | 3.004 | 0 |
| evm-zone-29 | 53 | 65,536 | 214,292 | 249,975 | 3.270 | 3.814 | 226 |
| evm-zone-29 | 54 | 65,536 | 214,454 | 250,665 | 3.272 | 3.825 | 222 |
| evm-zone-23 | 42 | 65,536 | 213,224 | 246,785 | 3.254 | 3.766 | 210 |
| evm-zone-23 | 44 | 65,536 | 214,453 | 250,391 | 3.272 | 3.821 | 222 |
| cyclone-25 | 103 | 65,536 | 232,113 | 232,302 | 3.542 | 3.545 | 0 |
| cyclone-25 (delivery 3) | 78 | 65,536 | 196,978 | 207,667 | 3.006 | 3.169 | 52 |
| cyclone-25 (delivery 3) | 83 | 65,536 | 196,978 | 207,667 | 3.006 | 3.169 | 52 |
| cyclone-25 (delivery 3) | 84 | 65,536 | 196,978 | 207,667 | 3.006 | 3.169 | 52 |
| evm-zone-23 (delivery 3) | 43 | 65,536 | 214,195 | 249,624 | 3.268 | 3.809 | 222 |

  Every window exact. The difference is the new-expansion service (owner's note: it goes to the replayer-model session,
  which owns replay_ctl): about 157 replayer stall cycles per new expansion (window 78: 9,591 over 52; window 43: 35,077
  over 222)
  (35,175 on window 53, against 452 in Stage A), of which about 33 the controller in WFI for the template's fetch
  (the address-generator run's lead-in, the SRAM read and the mode-9 landing) and about 123 the controller's own
  instructions between the claim and the run (2-cycle register loads). Without new expansions Stage B is within 0.1 %
  of Stage A.

## Commit L: replayer-cp12's Stage B (the new-expansion fast path)

- `sw/replay/` from cp12 (preloaded streams, a direct-mapped table, ruling N3's checks off the fast path); Replayer.java,
  Peripheral.java and ReplayerAlu.java unchanged since cp11. StageB prints the slow-path count (rp_results[6]).

| program | window | Stage A /row | Stage B cp9 /row | Stage B cp12 /row | new-expansion stall (cp9 -> cp12) | per expansion |
|---|---:|---:|---:|---:|---:|---:|
| evm-zone-29 | 53 | 3.270 | 3.814 | 3.479 | 35,175 -> 13,004 | 156 -> 58 |
| evm-zone-29 | 54 | 3.272 | 3.825 | 3.492 | 36,268 -> 14,480 | 163 -> 65 |
| cyclone-25 (delivery 3) | 78 | 3.006 | 3.169 | 3.090 | 9,591 -> 4,281 | 184 -> 82 |

  Every row still byte for byte. One expansion per window takes the slow path (the first of the pass). Profile of
  window 53's new-expansion stalls (-Dstageb.profile): the controller's instructions about 41 cycles per expansion
  (main 6,332, set_active 2,713, refill 183) and WFI for the template's fetch about 17 (3,776), against the estimate of
  22-25 controller cycles.

## Commit M: the runner's results; job 2's packaging

- Task 065 (the 43 full-size P4 runs): all pass, same paths as v1.0, k128 4.3-13.4 % faster, kv32 0.4-0.7 % slower
  (`RESULTS-full-size.md`). Tasks 066-067 (the full-size suites): 066 failed for want of `meshsim/data/` in the
  package; 067, with it, all four pass (CtrlTest full 7,167, EngineTest full 383, the skew table, the full sweep).
- `runner/suites/meshsim/data/` added; `run_suites.sh` checks its payload first; `meshsim/kit/assemble_runner.sh`
  refreshes the payloads from the tree (`-k` also the p4 kit, which changes HASHES); measured times in the READMEs.
- CtrlTest's measurement lines: stale wording (MASTER_STATUS, mode-5 packets, mailbox packets) fixed.

## Commit N: the milestone merge

- The spec session's archive through part M, replayer-cp14 and the runner results merged (`MERGE-2026-10-27.md`, every
  conflict listed). `Config.stageDelay` = 32 (spec 6.2's `stage_delay`) replaces SortTest's 64-cycle wait.
- New measurements: `TileControl.counterWriteAt`; the counter store at +7..+8 (RecipeTest sync, the shortest start
  path) and +14..+15 (SortTest staging, the sort's CMD_STAGE path) cycles after C&C's GO store.
- Item 116: experiment switches (`-Dmeshsim.loopbackLast`, `-Dmeshsim.loopbackTokens`, `-Dmeshsim.loopbackDepth`;
  defaults unchanged): the 64-deep loopback FIFO removes v1.0's loss (16 deep, no tokens, no priority: 561 lost, as v1.0).
- The quick suite passes on the merged tree; Stage B with cp14 matches on windows 53 and 78.
