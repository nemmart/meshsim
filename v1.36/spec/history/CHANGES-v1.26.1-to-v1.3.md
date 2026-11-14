# The change guide from v1.26.1 to v1.3

For the RTL engineer, upgrading from **v1.26.1** (whose guide from v1.25 is `CHANGES-v1.25-to-v1.26.1.md`) to **v1.3**. The
step **adds the Tracer** (C&C 3's peripheral 1, type 3), **changes every sending peripheral's DMA** (128-bit words, its router
port), and **leaves the replayer and the tiles unchanged**. Each item says whether it affects the RTL.

**Sources.** The authority is `spec/machine-spec.md` v1.3: section 4.21 (the Tracer), 4.17.9 (the DMA), 8.16 (the Tracer's
window). `CHANGES-v1.3.md` lists the same delta by spec section, with every decision. Decisions: `planning/DECISIONS-v1.3.md`
(cited **V3-*n***), the owner's rulings (**O**, 2026-11-05; **O6**, 2026-11-06) and the answers to B1's spec questions
(**B1 Q*n***, `planning/sessions/WORKER22-SPEC-QUESTIONS.md`). A later spec change to this step needs an entry here.

**How to read v1.3** (O6). The spec makes **reasonable default calls** and states **what the hardware must do observably, not
how**. Where a mechanism is given (a buffer, an ordering, a counter's exact attribution) it is a default; where the RTL finds a
better way, the spec follows it. Tell us, and the spec and meshsim change.

## 1. In brief

| # | change | unit | RTL? | section |
|---|---|---|---|---|
| 1 | **the Tracer**, new: the RV64 row machine that traces the guest and writes each window's record and three streams | C&C 3, peripheral 1 | **yes, new** | 4.21.1–4.21.9, 8.16 |
| 2 | **the DMA counts 128-bit words**: count [11:0] in 128-bit words, every packet 128 bits, `DMA_ADDR` + 16, no remainder packets | every sending peripheral's DMA (C&C 1–3) | **yes** | 4.17.4, 4.17.9 |
| 3 | **the DMA's router port**: packets carry src = the controller's id + 8 (72–74), enter the controller's router as an extra input, leave through its down port; **routing by dst**, a packet for the router's own unit ejected there | the controllers' routers | **yes** | 4.17.9 (item 118) |
| 4 | **the fill bus**: the Tracer's interface to the memory system, one 128 B line of a file by (root, depth, line), with a status on every read | the Tracer's side; the memory system a black box | **yes** (the Tracer's side) | 4.21.5, 6.7, 7 |
| 5 | the replayer | tiles 0–63 | **no**: unchanged since v1.26.1 | 4.19 |
| 6 | the universal tables to a16z's rule for D13, D14 | data | **no**: after the freeze (item 158) | 4.19.4 |
| 7 | the segment's sequence: C&C 1's two barriers, the "armed" handshake, the final GO | firmware | **no** | 4.21.10, 6.3 |
| 8 | the template broadcast; the ground truth (a16z `7189216`, gold, the pin `88dca85`) | firmware, verification | **no** | 4.19, 4.19.9, 4.19.13 |

## 2. The Tracer (new; 4.21) — RTL: yes

The whole of 4.21.1–4.21.9 is to build; 4.21.10 is the software around it. It is **its own core** (V3-25): what it shares with
the replayer is **formats and tables**, not the design, though the replayer's decode-table indexing, operand resolver, ALU
(`VirtualPext` for any mask) and multiplier may well be reused as blocks. Its memory is about 850 KB, dominated by the FIFOs, in
C&C 3 only (4.21.9).

### 2.1 Interfaces

| ID | interface | what | section |
|---|---|---|---|
| I1 | region 0x04 on C&C 3 | mode-0 128-bit packets from C&C 3's traffic director, landing at `rp_latency` = 2: part 0 the record, decode table (0x1000) and ROM (0x2000, single-ported, item 156); part 2 the read queue (advice); part 4 the expansion queue; parts 1, 3 unused | 4.21.2 |
| I2 | the window, 0xFFFFFA00 | the standard words (4.17.4) and words 32–51 | 4.21.3, 8.16 |
| I3 | C&C 3's `EVENTS` [13] and `ERRORS` [11] | live: `EVENTS & EVENTS_MASK` non-zero; `ERRORS` non-zero (the error line) | 4.17.3, 4.17.6 |
| I4 | the DMA | output queues 0–3 to C&C 3's router (2, 3 below) | 4.21.6 |
| I5 | **the fill bus** | read: op, root [30:0], depth [1:0], line [34:0] → 1,024 bits and a status [1:0]; write: the same plus 1,024 bits → an acknowledgement; at most one read outstanding, a one-entry write-back buffer | 4.21.5 |

### 2.2 The blocks

| ID | block | what to build | section |
|---|---|---|---|
| T1 | the record, part 0 | the 68-word record **as the working state**: the state word (pc, `instr_idx`, next offset), the stream word (outputs [101:0], the advice field [124:102] the live count), the 128 × 64-bit register file as the image, word 66 (the files and bounds: memory root/depth, map root/depth, log₂ K, the I/O region's size [102:72]), word 67 the template bitmap (an output) | 4.21.2 |
| T2 | the word-0 write | empties every queue; advice and expansion credits to 2, output credits 0; clears the read queue's pointer, the length registers and **the statistics counters**; invalidates both caches and the jump LRU **without writing back**; clears the template latch and bitmap | 4.21.3 |
| T3 | control | two pulses (`PULSE_RUN` runs `RUN_CONFIG` [23:0] ≤ 65,536 rows and drains them; `PULSE_DMA`); `RUN_CONFIG` [28:26] the compression enables; `PULSE_RUN` resets only the window's output state; `STATUS` [0] through the drain | 4.21.3 |
| T4 | the window's words | 36–39 the queue lengths (0 until the rows end, then fixed); 40–42 the hash's constants; 43 `EXPANSION_TEMPLATE` (sets the bitmap bit, latches the number); **44–51 the statistics counters** (T13) | 4.21.3, 8.16 |
| T5 | events | [0] run done (after the drain), [1] DMA done, [3] advice credit, [5] expansion credit (depth 2), [6] new expansion, **[7]–[10] the queues' service flags (live)** | 4.21.3 |
| T6 | execution | fetch through the instruction cache (16 bits, a 32-bit instruction possibly straddling two lines and pages); the RVC decompressor as a16z's (D12); the decode table and ROM, the rd = x0 policy and offset; controller-fed expansions through the expansion queue; the rows of 4.21.4's table (loads and stores from the data cache, advice from the read queue, control transfers through the LRU and map, `VirtualAssert…` evaluated, the end at any JAL with immediate 0, padding at pc 0) | 4.21.4 |
| T7 | the jump LRU and the map | 64 entries, true LRU; on a miss, the probe: key *o* = pc − `lowest_address`, home line ((key × *p*₁) mod 2³²) >> (32 − *k*), 16 entries compared at once, at most 3 lines; the direct [7] checks (*o* ≥ 2³², a JALR or branch to 0) | 4.21.4 |
| T8 | the caches | 16 KB each, 128 lines of 128 B, fully associative, true LRU, tagged by `.pages` line; data write-back and write-allocate, **a writable bit per line from its fill's status** | 4.21.5 |
| T9 | the fill statuses | **ok** only if every entry on the path from the root is owned (B1 Q1); **hole** (zeros, not writable); **not owned** (not writable); **beyond the depth's capacity**, line ≥ 2^(5 + 10·depth) (B1 Q2): the default interface (O6, item 148), to revisit with the memory system | 4.21.5 |
| T10 | the address checks | on every memory row, beside the cache lookup: **the range comparator**, (*a* − `lowest_address`) mod 2⁶⁴ < 8·K (item 149), and **the misalignment test**, the floor address a multiple of 8 (item 150); both set together if both fail, and the access then doesn't happen (B1 Q6) | 4.21.5, 4.21.8 |
| T11 | output | the code FIFO (65,536 × 32) and the log FIFO (65,536 × 66, a 2-bit tag that never leaves); set 1 sizing as rows run, set 2 compressing on the way out, the encoders matching the replayer's decoders exactly; one scan per FIFO into three packers and **4 KB output queues 1–3 with credits** (one per 2 KB chunk and one for a stream's tail); **output queue 0 is part 0 read in place by a pointer**; done only once everything has drained; a zero-row run writes no code value (B1 Q7) | 4.21.6 |
| T12 | errors | `ERRORS` [1], [2], [4], [7]–[12]; queue overrun drops and runs on, every other error stops the Tracer with `STATUS` [0] at 1 (item 152) | 4.21.8 |
| T13 | **the statistics counters** | words 44–51, 32 bits wrapping, read-only, cleared by the word-0 write and the reset, counting while `STATUS` [0] is 1: cycles; fetch, data (**with the write-back buffer's wait**) and map stall cycles; output stall cycles (**either drain scan on a full queue**, once a cycle); I- and D-cache misses; map probes. The waits on C&C 3 in none. **The attributions are defaults** (O6): refine them if the RTL has a better split, and tell us | 4.21.3 (item 157) |
| T14 | timing | rows of 4 cycles, 6 for a multiply, plus stalls; the probe 2 cycles a line; the drain one entry a cycle per FIFO; the memory system's latencies are its own (T15). meshsim's values, for the RTL's to replace (item 153) | 4.21.9 |

### 2.3 What the Tracer may assume of the memory system (T15)

The memory system is **a black box** (4.21.5, section 7): build the Tracer's side of the fill bus only. meshsim models it as a
512 KB shared cache (8 ways × 512 sets, LRU, write-back, a hit in 6 cycles, a miss in 80), no TLB (a fill is a walk of depth + 1
accesses), dirty victims evicted early through a 4-line write buffer, a read miss waiting for the buffer to empty, and a Tracer
write-back that misses allocating without an LPDDR read (B1 Q12, Q13; 4.21.9). None of that is the Tracer's to build; it only
sets what the measured cycles assume.

## 3. The DMA (4.17.9) — RTL: yes

| ID | v1.26.1 | v1.3 | why |
|---|---|---|---|
| D1 | `DMA_HDR` count [11:0] in 32-bit words; ⌊*n*/4⌋ 128-bit packets, then 64- and 32-bit remainders | **count in 128-bit words**, up to 4,095; **every packet 128 bits**, one queue entry each; `DMA_ADDR` + 16 per packet, 16-byte aligned; size [18:17] reserved | V3-24: the Tracer's streams are padded to whole 128-bit words, and their exact lengths travel in the stream word |
| D2 | the router port, its src deferred (item 118) | **src = the controller's id + 8** (72–74; the Tracer's 74), **sources only** (packets for 67–125 stay errors); the packets **enter the controller's router as an extra input** and **leave through its down port**, the link to its tile (tile 2 for C&C 3) | O (item 118), B1 Q5 |
| D3 | — | **routing by dst, no special case**: a packet whose dst is its router's own unit (dst 66 from C&C 3's DMA, the last record of a segment) is ejected there like any arrival; every other packet leaves through the down port | B1 Q11 |
| D4 | — | a transfer asking for more than its queue will hold is UB (the DMA would wait forever; no check) | V3-24 |

The rest of the controllers' routing and the DMA port's flow control stay deferred (item 118).

## 4. Unchanged for the RTL

| ID | item | why there's no RTL change | section |
|---|---|---|---|
| N1 | **the replayer** | unchanged since v1.26.1: its `RUN_CONFIG` [28:26] are already stored and ignored (the v1.26.1 guide's W1) | 4.19 |
| N2 | **D13, D14** | a16z's rd = x0 rules (the rewrite row never virtual; LR/SC remapped) are table data: `rd0` and the offset already express them. The tables are rebuilt right after the freeze (item 158, O6) | 4.19.4 |
| N3 | the template broadcast | C&C 3's firmware; each tile keeps the 60 CSR templates and its bitmap's inlines | 4.19.9, 4.19.13 |
| N4 | the ground truth | a16z `7189216` decides, gold is the Java emulator, jolt-cpp `88dca85` the pin, checked | 4.19 |
| N5 | the tiles, the mesh, the request bus, C&C 1 and C&C 2's cores | no change in v1.3 (beyond D1–D3 for C&C 1 and C&C 2's DMAs, if they send) | — |
| N6 | GO and READY at 8 cycles in meshsim | WORKER28's latency commit (meshsim's values; the physical ones stay Open, section 10); no RTL change | 4.13, 10 |

## 5. Software-visible rules (firmware; meshsim checks them; no hardware)

- **The segment's sequence** (4.21.10; B1 Q15–Q17; O6): tiles raise READY with nothing armed; C&C 1 pulses GO and starts
  C&C 3; each tile arms for its record's fixed pieces (66 words of state, 2 of totals) on waking; C&C 3 sends nothing sooner
  than `stage_delay` = 64 cycles after the GO (unexpected arrival, `ERRORS` [2], if it ever did); per window, tile *n* gets its
  totals, arms its streams and replies "armed" before C&C 3 sends them; each tile raises READY when its counters reach 0; the
  final GO starts every tile's replay together.
- **Window 0's start** goes to tile 0 from C&C 3's own copy of the record (B1 Q3); **the last record** goes to C&C 3's memory
  and its words 1 and 67 are forwarded to tile 63 (B1 Q4).
- **On loading a segment's record, C&C 3 drops any expansion it was feeding** (B1 Q14), since the word-0 write resets the
  expansion credit and the Tracer presents the expansion again.
- The word-0 write only at a segment's start; part-0 writes only while stopped; a transfer sized to its credits (4.21.3,
  4.21.6; invariant 16).

## 6. Verification

meshsim models the Tracer as specified (B1, WORKER22; merged at e67bc43): **T1, every window of EVM Zone and Cyclone at 2²⁹**,
and **18 full-chip T2 runs** with C&C 3's firmware (`tracer_ctl.c`), C&C 1's `CC_SEGMENT` barriers and the tiles' replay,
checked against the delivered run logs and gold; the final regression (Quest 110v) passed, the sort unchanged at 1,229,008 cycles
(`planning/sessions/WORKER22-REPORT.md` sections 3, 4). The statistics counters are modelled and tested (WORKER30, TracerTest's
counters group), so the RTL and meshsim can be compared counter for counter.
