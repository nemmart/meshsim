# WORKER22 report: B1, the Tracer's bring-up in meshsim

Branch **`v1.3-tracer`** (from `main`, main merged throughout; last at 181232a). The plan, the rulings and the formats are in
`PLAN.md` and `NOTES.md`, and the review of spec 4.21 is in `SPEC-QUESTIONS.md` (Q1–Q16, all answered). **Status: draft,
with Quest 110f, 110g (T2's subset) and 110h (the regression) pending; section 6 gets their results.**

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
| **C&C 3's firmware** | `sw/tracer/tracer_ctl.c`, `tracer_layout.h`, `build.sh` | 4.21.3's loop: the tables, constants and record; window 0's start to tile 0 (Q3); new expansion (item 145, N3), the expansion and advice credits; the drain one DMA transfer at a time, queue 0 in its order to tiles *n* + 1 and *n*, the last window to its own memory and its totals forwarded (Q4, Q11); Q14; the end checks (advice left, the end state against the next frame 2); Q15's handshake |
| the tiles' side | `sw/replay/replay_ctl.c`, `replay_layout.h` | layout version 5 (4 still accepted): Q15's handshake (the record's pieces armed at boot, "booted"; the streams armed from the totals' word 1, "armed"), then the replay as soon as its window has landed (Q16); the stream sizes from the record |
| **the harnesses** | `test/TracerRig.java`, `TracerTest`, `MemorySystemTest`, `RvcTest`, `TracerT1`, `TracerT2`, `Mach` | the rig (an assembler, images, maps, an ideal controller serving the real queues); T1 (the model on real segments in order on one memory, against WORKER9's deliveries and Stage A); T2 (the full chip: C&C 1's firmware boots C&C 2, C&C 3 with `tracer_ctl` and the 64 tiles with `replay_ctl`; every tile's delivery and rows checked) |
| tasks | `WORKER22/tasks/` | the Quest task generators (T1, T2, the regression) and their summaries |

## 2. The review of 4.21 (`SPEC-QUESTIONS.md`)

Sixteen questions, all answered. The spec is to be updated to match these:

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
| Q15 | the counting handshake |
| Q16 | a tile replays when its window has landed |

The rest: Q5 was answered by item 118, Q8 is moot, and Q10 was fixed in DECISIONS.

Two of them were found by running:
- **Q14**, by Quest 110's failure on Cyclone segment 16, which starts inside an inline.
- **Q15**, by invariant 22 when the first T2 ran.

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
| 110f | EVM Zone segments 16, 32, 37, 48, 64, 80, 84 raw, 37 LRU (the handshake) | *pending* |
| 110g | Cyclone segments 10, 14, 16, 32, 48, 64, 80 raw, 10 LRU | *pending* |

**The regression (110h, pending):** the quick and full suites, the full-size sort against 1,229,008 cycles, and the 64-window
Stage B smoke of both workloads.

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
- **Two departures from 4.21.3, both accepted:**
  - queue 0 is sent in its read order: the totals reach a tile before its streams, but after the state words;
  - the tiles replay as soon as their window has landed (Q16).

## 6. Pending results (110f, 110g, 110h)

*(to be filled in)*

## 7. For the integrator (the merge)

- The branch changes these files outside the new ones: `Replayer.java` (now using `RowCore`; cycle-identical),
  `PeripheralSlot.java` and `Peripheral.java` (the DMA's 128-bit words; `PeripheralSlotTest` updated), `Router.java`
  (controller routers' extra feeder; nothing else changes order), `LinkWord.java` (`tileOf` for 72–74), `CcCores.java`,
  `Config.java`, `Registers.java` (a field's description), `MeshHeader.java` and both `mesh.h` copies (`TracerRegs`;
  RegDocTest passes), `replay_ctl.c` and `replay_layout.h` (version 5; version 4 still accepted, so Stage B is unchanged),
  `Mach.java`, `TestPeripheral.java`, `run_tests.sh` (three new steps).
- **The spec to update:** the answered questions above; 4.21.9's memory timing as the shared cache (Q12, Q13); 4.21.3's
  loop (Q3, Q4, Q14, Q15, Q16); 4.17.9's packets carrying src 74.
- **Next:**
  - the T2 subset widened if wanted (about 20 minutes per segment and JVM);
  - the instruction cache's size for EVM Zone (4.65 → 4.37 cycles per row at 32 KB) is the owner's call; the 16 KB caches
    stay for now;
  - item 157's statistics registers, if bring-up wants them;
  - the later delivery design (windows into C&C 3's memory and LPDDR), which removes Q15's handshake.
