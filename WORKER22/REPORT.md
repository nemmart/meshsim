# WORKER22 report: B1, the Tracer's bring-up in meshsim

Branch **`v1.3-tracer`** (from `main`, main merged throughout; last at a326fbd). The plan, the rulings and the formats are in
`PLAN.md` and `NOTES.md`, and the review of spec 4.21 is in `SPEC-QUESTIONS.md` (Q1–Q17, all answered). **Status: complete.**
T2's subset passes on the READY/GO barriers (110q–110t), and with C&C 1's firmware owning them (110u). The final
regression at the branch head is Quest 110v (section 6).

## 1. What was built (`wip/meshsim/`)

| part | where | what |
|---|---|---|
| **the Tracer** | `src/Tracer.java` | 4.21 in full: the region and record; control (the word-0 write, `PULSE_RUN`, `RUN_CONFIG`, words 32–43, the events and both input credits); execution through the universal tables and the controller-fed path (N3, the temporary offset, item 145's latch and bitmap); advice; the jump LRU and the 3-line probe; the two 16 KB caches with the write-back buffer; the range comparator (8·K, Q9) and the floor-address rule; the FIFOs; set 1 and set 2; the packers, output queues 0–3, credits and live service flags; the drain with done last; the DMA's side; every `ERRORS` bit with item 152's stop; 4.21.9's timing |
| shared rules | `src/RowCore.java` | the replayer's pure helpers, extracted (ruling A); the replayer is cycle-identical (its logs equal the baseline's, cycles included) |
| RVC | `src/Rvc.java` | gold's decompressor (a16z's, with D12), checked on all 49,152 compressed encodings |
| the coder | `src/LruCoder.java` | the replayer decoder's inverse: lowest index, E[15] first, item 151's miss width |
| **the memory system** | `src/FileStore.java`, `src/MemorySystem.java` | §20's trees with `cow_open`, path copying and the one-memory ownership; the statuses (Q1: ok only with every entry on the path owned; Q2: status 3 beyond the depth's capacity); the shared cache (512 KB, 8 × 512, LRU, write-back), no TLB (the walk through the cache), the free-or-clean victim policy and its 4-line write buffer (Q12), write-allocate without a read (Q13); hit and miss 6 and 80 cycles; statistics by line class |
| the DMA in 128-bit words | `src/PeripheralSlot.java`, `src/Peripheral.java` | 4.17.9 at v1.3 (ruling B): one packet per queue entry, `DMA_ADDR` + 16 |
| **the DMA's router port** | `src/Router.java`, `src/LinkWord.java`, `src/CcCores.java`, `src/Config.java` | item 118: src = the controller's id + 8 (the Tracer's 74; `tileOf` maps 72–74 to tiles 0–2), a FIFO into C&C 3's router as an extra input (`CC_DMA_IN`), routed by dst (Q11); C&C 3's peripheral is the Tracer under `Config.cc3Tracer` |
| `mesh.h` | `src/MeshHeader.java`, both `mesh.h` copies | `TracerRegs` (8.16) and the `TR_` constants, generated |
| **C&C 3's firmware** | `sw/tracer/tracer_ctl.c`, `tracer_layout.h`, `build.sh` | 4.21.3's loop: the tables, constants and record; its start on C&C 1's command, then nothing sooner than stage_delay (Q17); window 0's start to tile 0 (Q3); new expansion (item 145, N3), the expansion and advice credits; the drain one DMA transfer at a time, queue 0 in its order to tiles *n* + 1 and *n*, the last window to its own memory and its totals forwarded (Q4, Q11); Q14; Q15's per-window "armed"; the end checks (advice left, the end state against the next frame 2) |
| the tiles' side | `sw/replay/replay_ctl.c`, `replay_layout.h` | layout version 5 (4 still accepted): READY with nothing armed; on the GO, the record's counters armed; the streams armed from the totals' word 1 and "armed" sent; READY again with every counter at 0; the replay at the final GO (Q15, Q17); the stream sizes from the record |
| **C&C 1's barriers** | `sw/cc/cc1.c` | `CC_SEGMENT`: the tiles' command, all 64 READY, GO, C&C 3's start, the second command, all READY again, the final GO (Q15's barriers, owned by C&C 1) |
| **the harnesses** | `test/TracerRig.java`, `TracerTest`, `MemorySystemTest`, `RvcTest`, `TracerT1`, `TracerT2`, `Mach` | the rig (an assembler, images, maps, an ideal controller serving the real queues); T1 (the model on real segments in order on one memory, against WORKER9's deliveries and Stage A); T2 (the full chip, every unit's real firmware: C&C 1 boots C&C 2, C&C 3 with `tracer_ctl` and the 64 tiles with `replay_ctl`, then runs the segment's barriers; every tile's delivery and rows checked; each chosen segment in a thread of its own on a snapshot of the one memory; a fail-fast that stops a hung run within minutes with every unit's state) |
| tasks | `WORKER22/tasks/` | the Quest task generators (T1, T2, the regression) and their summaries |

## 2. The review of 4.21 (`SPEC-QUESTIONS.md`)

Seventeen questions, all answered. The spec is to be updated to match these:

| question | the answer |
|---|---|
| Q1 | ok only with every entry on the path owned |
| Q2 | status 3 means beyond the depth's capacity |
| Q3 | window 0's start comes from C&C 3's own copy |
| Q4 | the last window's words 1 and 67 are forwarded |
| Q6 | [10] and [12] are set together |
| Q7 | a zero-row run writes no code value |
| Q9 | the range bound is 8·K meanwhile |
| Q11 | routing is by dst |
| Q12 | the free-or-clean policy, the write buffer, and read misses waiting for it to empty |
| Q13 | write-allocate without a read |
| Q14 | C&C 3 drops an expansion it was feeding when it loads a record |
| Q15 | the counting handshake; the segment's barriers are C&C 1's READY/GO |
| Q16 | a tile replays when its window has landed (reversed: all 64 replay together at the final GO) |
| Q17 | the barriers follow every phase's rule: READY with nothing armed, GO, arming on waking, nothing sent sooner than stage_delay |

The rest: Q5 was answered by item 118, Q8 is moot, and Q10 was fixed in DECISIONS.

Two of them were found by running:
- **Q14**, by Quest 110's failure on Cyclone segment 16, which starts inside an inline.
- **Q15**, by invariant 22 when the first T2 ran; its barriers, by the fan-in hang (section 6).
- **Q17**, by 4.13's done-early rule against the barrier as first worded.

## 3. Acceptance

**The rig's directed tests** are `TracerTest` 150/0, `MemorySystemTest` 56/0 and `RvcTest` 49,157/0, all steps in
`run_tests.sh`. They cover:
- every error bit;
- the windows cut inside ROM and controller-fed expansions;
- a 3,121-word inline streamed on the credits;
- the output path in six service orders, a slow controller and a stalled one;
- write-backs;
- an unaligned `lowest_address`.

**T1** is the model on real segments in order on one memory. The inputs are WORKER26's initial image and map and gold's
segment file; the references are WORKER9's deliveries and jolt-cpp's rows. A run passes when:
- every window's start state equals WORKER9's;
- the raw streams equal WORKER9's value for value;
- the coded sizes and picks equal WORKER9's encoder's;
- the bitmap equals the manifest's inline templates;
- Stage A, the replayer model on the Tracer's own streams, gives every row and consumes each stream exactly;
- at every boundary, the state equals the next frame 2 and every updated cell equals the cumulative updates.

| run | what | result |
|---|---|---|
| local | Cyclone input/22 (64 windows), EVM Zone trace-23 (128), raw and LRU | PASS |
| Quest 110 | 2²⁹, both workloads | FAIL on Cyclone from segment 16: the ideal controller's stale expansion words at a segment's start (Q14), fixed |
| **110b** | 2²⁹, both workloads, every window and boundary, raw / LRU with Stage A / the separate-map control | **PASS** |
| **110c** | the same, the instruction-fetch breakdown and a cache sweep | **PASS** |
| **110d** | the same through milestone 2's output path | **PASS** |

**T2** is the full chip: `tracer_ctl` on C&C 3, the Tracer's DMA into the tiles, and every tile replaying with `replay_ctl`.
For each tile, its delivered record and streams are checked against WORKER9's, and its rows against jolt-cpp's.

| run | what | result |
|---|---|---|
| local | EVM Zone trace-23 segments 0 (2 windows) and 1 (3, segment 0 carried), Cyclone input/22 segment 0 (8, LRU), with Q15's handshake | PASS |
| **110e** | 2²⁹, segment 0 of both workloads, 64 windows, raw and LRU (before the handshake, under its interim exemption) | **PASS**: 4,194,304 rows a run |
| 110f, 110g; 110i–110l | the subset with the 64-tile "booted" | timed out: the fan-in hang (section 6) |
| **110m–110p** | the subset, "booted" up the request tree: 16 runs | **PASS** |
| **110q–110t** | the subset on the READY/GO barriers (Q17), host-sequenced: 16 runs | **PASS** |
| **110u** | segment 10 of each workload, C&C 1's `CC_SEGMENT` running the barriers: every unit's real firmware | **PASS** |

**The regression (110h): PASS** (section 6).

## 4. Measurements (2²⁹, WORKER26's map and image; Quest 110b, 110c, 110d, 110e)

| | EVM Zone | Cyclone |
|---|---|---|
| **the core's cycles per real row** (target 4) | **4.650** | **4.480** |
| rows alone (class M rows take 6) | 4.313 | 4.141 |
| stalls per row: instruction fetch / data miss / map probe | 0.306 / 0.008 / 0.022 | 0.072 / 0.120 / 0.146 |
| **all-in per segment, rows and drain, max** (100 ms budget) | **23.7 ms** | **27.2 ms** (segment 10) |
| the drain per segment (median) | 4.12 M cycles | 3.15 M cycles |
| shared cache: `.pages` index / data; map index / data lines | 99.99 / 98.15%; 99.56 / 50.40% | 99.94 / 77.79%; 99.98 / 95.05% |
| the walk: share of the real rows' cycles | 4.27% | 2.72% |
| the map's lines' effect on the data (the map on its own cache) | none (+0.006%) | +0.61% (data hits 77.8% against 80.9%) |
| instruction-fetch stalls: capacity misses' share | 93% | 76% |
| core cycles per row with a 32 KB instruction cache | 4.368 | 4.463 |
| T2, segment 0: C&C 3, start to exit | 21.0 ms | 20.9 ms |
| T2, the subset's worst segment, C&C 3 start to exit | 23.7 ms (37) | 28.2 ms (10) |
| T2, segment 0: the tiles' replay, longest | 236K cycles | 212K cycles |

**Reading:**
- EVM Zone's gap to 4 cycles a row is its code's working set against the 16 KB instruction cache, plus the multiply rows'
  6 cycles.
- Cyclone's gap is mostly data misses and map probes.
- Both workloads are well inside the 100 ms budget. The owner keeps the 16 KB caches.

## 5. Model choices and their limits (for the RTL and the next session)

- **The memory system's timing:**
  - a row's memory traffic is timed at its start, serially; this is exact with one read outstanding;
  - a dirty victim's write-back is issued as its fill completes. So the one-entry buffer never holds a line while a read is
    wanted, and **4.21.5's take-back case is not exercised.** A directed test checks that evicted dirty lines come back;
  - an early eviction finding the write buffer full frees its way at once;
  - only the Tracer's traffic is modelled, so **the hit rates are optimistic.**
- **The harness, where C&C 1 and C&C 2 are out of scope:**
  - T1 and T2 land C&C 3's inputs (the frames, the tables, the templates) and each tile's tables and templates directly. C&C
    3's template broadcast is out of scope;
  - at a segment's end the harness writes the Tracer's dirty lines into the one memory, untimed (the hardware drops them);
  - T1's ideal controller serves the real queues but isn't C&C 3's firmware; T2 runs the firmware.
- **The DMA port's flow control** (item 118, the part still deferred) is modelled as a 4-packet FIFO into C&C 3's router: the
  DMA waits while it is full.
- **meshsim's controller link** stays on `CC_LINK_OUT`, which is the spec's down port.
- **One departure from 4.21.3, accepted:** queue 0 is sent in its read order, so the totals reach a tile before its streams,
  but after the state words.
- **The barriers:** C&C 1's `CC_SEGMENT` runs them in T2 from 110u on. Before that, the host sequenced the same steps
  (C&C 1's two phases and C&C 3's start). stage_delay is 64 cycles in `tracer_ctl` (32 Proposed, 64 as meshsim's harness
  has always used).

## 6. The regression, the T2 subset, the fan-in hang and the firmware

### The final regression (Quest 110v, at the branch head 0d023ba)

*Pending.* It is as 110h: the quick and full suites, the full-size sort and the 64-window smoke. The branch head's `wip/` is
identical to a326fbd's, the code 110u ran.

### The full regression (Quest 110h): PASS

Branch 181232a, everything precompiled for the runner, gold's classes included.
- **The quick suite:** 39 steps, 0 failures, including MemorySystemTest 56/0, RvcTest 49,157/0 and TracerTest 150/0.
- **The full-mode extras:** 11 steps, 0 failures.
- **The full-size sort**, p4/uniform-k128: 1,229,008 cycles, unchanged.
- **The 64-window smoke** of both workloads, as Quest 105: Stage A 64/64, Stage B 64/64, F 4/4 each.

Against 105's v1.26.1 run of the same windows:
- **Stage A,** the replayer model, is identical in all 64 windows of both workloads (verdicts, rows and cycles). The
  replayer is unchanged.
- **Stages B and F** have the same verdicts and rows, with 4 cycles fewer in every window and up to 500 (EVM Zone) and 142
  (Cyclone) fewer in the expansion-heavy ones. They run `replay_ctl` on a tile, whose binary changed with layout 5. That
  is the controller program's difference, not the replayer's.

### T2's 2²⁹ subset

About one segment in 16 plus the heaviest: 16 runs, four a task, each task about 23 minutes. Every run checks all 64 tiles'
deliveries against WORKER9's and all 4,194,304 rows against jolt-cpp's.

| runs | 110m–110p (request-tree "booted") | 110q–110t (READY/GO, host-sequenced) |
|---|---|---|
| EVM Zone 16, 32, 37, 48 | OK; C&C 3 23.7 ms each | OK; 23.7 ms each |
| EVM Zone 37 LRU, 64, 80, 84 | OK; 22.6–23.7 ms | OK; 23.7, 23.6, 23.7, 22.6 ms |
| Cyclone 10, 10 LRU, 14, 16 | OK; 28.2, 27.2, 27.4, 18.9 ms | OK; 28.1, 27.2, 27.4, 18.9 ms |
| Cyclone 32, 48, 64, 80 | OK; 21.6–23.3 ms | OK; 23.2, 23.2, 23.2, 21.5 ms |

The times are C&C 3's, from its start to its exit, at 1 GHz. Each task took 22–24 minutes.

**110u (PASS)** ran C&C 1's `CC_SEGMENT`: every unit runs its real firmware, the barriers' owner included. Segment 10 of EVM
Zone took 23.7 ms and segment 10 of Cyclone 28.1 ms, the same cycles as the host-sequenced 110s to within 20.

**The worst segment of either workload is Cyclone 10:** 28.1 ms raw and 27.2 ms LRU-coded, against the 100 ms budget. The
tiles' replay follows the trace, at most about 236K cycles.

Locally, EVM Zone trace-23 segment 0 (64 windows) passes on every barrier version: 419 s, 429 s and 448 s, against 425 s
before the handshake.

### The fan-in hang (Quest 110f, 110i–110l)

**Symptom.** Every full-segment run on the handshake version hit the 45-minute timeout with no segment done. 110e, before
the handshake, had done a segment in about 20 minutes. Locally, a quarter segment took the same time at both commits, and
a full one ran to the cycle cap.

**Cause.** At boot every tile sent C&C 3 a count-only "booted". 64 packets to one receiver through tile 2's port
overflowed its queue (`ROUTER_OVF`) and two were dropped. C&C 3 waited forever for them, so every run went on to the
55 M-cycle cap. 3.2 already rules this out: no flow control, and 63 × 1 overflows in every case measured. Partial runs of
16 windows or fewer had only that many senders, so they worked.

**How it was found.** By the fail-fast added for it, which stops a run with no progress for 3 M cycles with every unit's
pc, `ERRORS` and counters. That showed C&C 3's "booted" counter at 2 and tile 2's `ERRORS` [0].

**Fix.** First "booted" up the request tree (110m–110p), then, as ruled, C&C 1's READY/GO barriers with no "booted" at all
(Q15, Q17; 110q–110u). The fail-fast stays in `TracerT2`, together with a second trigger: a tile not exited 1 M cycles after
C&C 3's exit.

### The firmware against the ideal controller (segment-matched)

T2's Tracer rows (C&C 3 running `tracer_ctl.c`) against T1's (the ideal Java controller) for the same segment, from 110b's
per-segment figures. An earlier "9% gap" set other segments' 19.5 M against segment 0's 17.9 M: a mismeasurement.

| segment | EVM Zone T2 / T1 rows (difference) | Cyclone T2 / T1 rows (difference) |
|---|---|---|
| 0 | 17,848,648 / 17,744,468 (+0.59%) | 17,892,530 / 17,876,600 (+0.09%) |
| 10 | | 23,810,201 / 23,802,978 (+0.03%) |
| 14 | | 23,079,946 / 23,077,524 (+0.01%) |
| 16 | 19,518,112 / 19,518,112 (0.00%) | 17,808,969 / 17,301,102 (+2.94%) |
| 32 | 19,548,488 / 19,548,488 (0.00%) | 18,966,790 / 18,966,790 (0.00%) |
| 37 | 19,552,528 / 19,552,528 (0.00%) | |
| 48 | 19,531,662 / 19,531,662 (0.00%) | 19,921,552 / 19,851,456 (+0.35%) |
| 64 | 19,498,536 / 19,498,536 (0.00%) | 19,887,542 / 19,887,542 (0.00%) |
| 80 | 19,531,476 / 19,531,476 (0.00%) | 18,373,855 / 18,361,766 (+0.07%) |
| 84 | 18,571,806 / 18,571,806 (0.00%) | |

The firmware is identical to the cycle in 9 of the 18 runs and within +0.6% in all but one. The exception is Cyclone 16
(+2.94%), which starts inside an inline: there the firmware's mid-sequence start (one word sent with rd0, then a run)
costs a little.

## 7. For the integrator (the merge)

- The branch changes these files outside the new ones: `cc1.c` (`CC_SEGMENT`; CcCoresTest and RecipeTest pass), `Replayer.java` (now using `RowCore`; cycle-identical),
  `PeripheralSlot.java` and `Peripheral.java` (the DMA's 128-bit words; `PeripheralSlotTest` updated), `Router.java`
  (controller routers' extra feeder; nothing else changes order), `LinkWord.java` (`tileOf` for 72–74), `CcCores.java`,
  `Config.java`, `Registers.java` (a field's description), `MeshHeader.java` and both `mesh.h` copies (`TracerRegs`;
  RegDocTest passes), `replay_ctl.c` and `replay_layout.h` (version 5; version 4 still accepted, so Stage B is unchanged),
  `Mach.java`, `TestPeripheral.java`, `run_tests.sh` (three new steps).
- **The spec to update:** the answered questions above; 4.21.9's memory timing as the shared cache (Q12, Q13); 4.21.3's
  loop (Q3, Q4, Q14, Q15, Q17, and Q16 reversed); 4.17.9's packets carrying src 74; 6.3's segment barriers as C&C 1's
  (`CC_SEGMENT`).
- **Next:**
  - the T2 subset widened if wanted (about 20 minutes per segment and JVM);
  - the instruction cache's size for EVM Zone (4.65 → 4.37 cycles per row at 32 KB) is the owner's call; the 16 KB caches
    stay for now;
  - item 157's statistics registers, if bring-up wants them;
  - the later delivery design (windows into C&C 3's memory and LPDDR), which removes Q15's handshake;
  - signed counters (candidate C2, item 125), which would close every phase's window between GO and arming.
