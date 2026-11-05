# WORKER22 plan: the Tracer's bring-up in meshsim (B1)

Approved by the owner on 2026-11-05, with rulings A–E below; spec and DECISIONS-v1.3 read up to 6990b94. Code goes on branch
`v1.3-tracer` (from `main`); notes, questions and the report go in `WORKER22/` on `main`. Spec questions and their answers
are in `SPEC-QUESTIONS.md`.

## What exists, and the gaps

- C&C 3 runs as a core unit (`CcCores`) on tile 2's fifth port, with a stub of type 3 in place of the Tracer.
- `PeripheralSlot`'s DMA is v1.26.1's: counts in 32-bit words, with remainder packets. It is not wired to any router, and
  only `TestPeripheral` uses it.
- Today's Stage F is one tile replaying on the full chip. The Tracer needs 64 tiles, each fed its window by C&C 3's DMA.
- Gold writes frames 1–5. It writes no frame 6 or frame 7, and frame 2 is still the flat 131 × u64. WORKER26 stands in for
  frames 6 and 7 (section 3). I convert frame 2 until gold and WORKER16's tools move to v1.3's formats, and say so.

## Rulings (owner, 2026-11-05)

- **A.** The replayer's pure helpers are extracted into a shared class: the decode index, the expansion-word fields, the
  operand resolution and the source immediate. The replayer stays cycle-identical, checked by `ReplayerTest` and
  `ReplayerUnitTests` and a Stage B smoke run (the synthetic delivery, plus a real 64-window shard).
- **B.** `PeripheralSlot`'s DMA moves to v1.3's 4.17.9. The count is in 128-bit words, every packet is 128 bits, there are
  no remainders, and `DMA_ADDR` advances by 16. `Peripheral.outTake` returns a 128-bit word.
- **C.** Item 118 is decided. The Tracer's DMA packets carry **src 74** (the controller's id + 8) and enter C&C 3's router
  as an extra input. They leave through its down port, the link to tile 2, and arrive there like a neighbour's. 72–74 are
  sources only. What is still deferred is modelled simply, as follows:
  - The link keeps meshsim's existing port assignment (`Router.CC_LINK_OUT`, W14-01). That is the spec's down port under
    another name.
  - The DMA input is one more feeder of the controller's router with a FIFO of its own. The DMA puts at most one packet a
    cycle, and only while that FIFO has room, so it is back-pressured by the FIFO and nothing else.
  - Routing is by dst (Q11): a packet whose dst is C&C 3 itself (the 64th state service) is ejected there like any
    arrival.
- **D.** The jump map is **WORKER26's section-18 map**: the static table, the hash constants and frame 6's supplement,
  merged for bring-up. I agree its format with WORKER26 (below). An every-instruction-start table is used only for the
  first functional tests, and **no timing is measured until WORKER26's table is in**.
- **E.** T2's scope at 2²⁹ starts with about 1 segment in 16 plus the heaviest. I measure the speed before going wider.
- **Access:** jolt-asic-planning is cloned with the meshsim token. That gives the guests, WORKER9's `delivery_writer` and
  WORKER16's tools.
- **The memory system** (replacing my warm-line model) is described in section 2 below.
- **The target** is 4 cycles per traced row (DECISIONS-v1.3 section 11). I report the core's cycles per row against it, and
  the all-in cycles per segment against the 100 ms budget (100 M cycles at 1 GHz).

## 1. The model (`src/Tracer.java`, type 3)

`Tracer` implements `Peripheral`. It is built from 4.21:

- **The region (4.21.2):**
  - part 0 holds the record, the decode table and the ROM;
  - part 2 is the read queue (the advice), with two values per entry, low value first;
  - part 4 is the expansion queue;
  - parts 1 and 3 and everything else are UB;
  - landing follows `rp_latency`.
- **Control (4.21.3):**
  - the word-0 write resets everything 4.21.3 lists, and flags a write that finds advice still unconsumed;
  - `PULSE_RUN` starts a window;
  - `RUN_CONFIG`: rows above 65,536 are UB, and [28:26] enable compression per stream;
  - words 33–43, with `EXPANSION_TEMPLATE`'s latch and the bitmap;
  - the events and their depths: done, DMA done, the advice credit, the expansion credit, new expansion, and the four live
    service flags;
  - every `ERRORS` bit, with item 152's stopping rule.
- **Execution (4.21.4):**
  - fetch through the instruction cache, a 32-bit instruction straddling two lines when it must;
  - RVC decompression, a16z's (D12);
  - the decode table, then the ROM or the controller-fed path;
  - the rd = x0 policy and the temporary offset, by the shared helpers;
  - rows through `ReplayerAlu`, plus the evaluation of the `VirtualAssert…` rows (appendix 2 H, [11]), which the replayer
    skips;
  - the load, store, advice, control-transfer and end rows of 4.21.4's table, padding at pc 0, and windows that start and
    end inside expansions.
- **The jump LRU and the probe:**
  - the LRU has 64 entries and is true LRU;
  - the key is *o* = pc − `lowest_address`;
  - the home line is ((key × *p*₁) mod 2³²) >> (32 − *k*), and the probe steps by *p*₃, reading at most 3 lines;
  - free entries, three full lines, *o* ≥ 2³² and a jump to pc 0 are [7]; a hole in the map is [9].
- **Memory (4.21.5):**
  - two caches of 128 lines, fully associative and true LRU; the data cache is write-back and write-allocate, with a
    writable bit per line;
  - a one-entry write-back buffer, with a miss allowed to take its line back;
  - one read outstanding, in the core's order;
  - the range comparator, (*a* − `lowest_address`) mod 2⁶⁴ < 8·K, and the floor-address misalignment rule ([12]);
  - when a row fails both checks, [10] and [12] are set together (Q6).
- **Output (4.21.6):**
  - the code FIFO and the 66-bit log FIFO, each 65,536 entries;
  - two compressor sets of three encoders each. They are true LRU and code a match as its lowest index, the write log's
    E[15] first. A miss is coded 32-bit when the value's top half is 0;
  - the choice per stream, the stream word's fields and words 37–39;
  - one scan per FIFO into its packer and output queue, with the credits per 2 KB chunk and per last part;
  - queue 0 as part 0 read in place through a read pointer;
  - done last.
- **Timing (4.21.9):**
  - rows take 4 cycles, 6 for class M;
  - the stalls by cause;
  - `tr_probe_compare` = 2 per probed line;
  - the drain at one entry per cycle per FIFO;
  - the DMA at one packet per cycle.
- **Statistics** are meshsim-only counters, not registers, since item 157 is open: cycles, stalls by cause, misses, probes,
  walk cycles and drain cycles.
- **Other classes:**
  - the shared helpers (ruling A);
  - `Rvc.java`, the decompressor;
  - `LruCoder.java`, the encoder, which shares its rules with the replayer's decoder.

## 2. The memory system (`src/MemorySystem.java`; owner, 2026-11-05)

- **The fill bus is the Tracer's only interface to the memory system:**
  - a read of (root, depth, line), answered with 1,024 bits and a status;
  - a write of (root, depth, line, 1,024 bits), answered with an acknowledgement.
- **`FileStore` is the simulated LPDDR.** It holds 4 KB pages at 128 B-unit addresses and real §20 trees: u32 entries, bit
  31 the owned bit, 0xFFFFFFFF a hole.
- **Translation is the walk, with no TLB anywhere (§21 (b)).** Each index read is a shared-cache access of the index line
  holding the entry, and the data line is one more access. For `.pages` (depth 2) that is two index lines and the data line.
- **The status is computed on the walk:**
  - 0 if every entry on the path is owned (Q1);
  - 1 if any entry on the path is a hole, which reads as zeros;
  - 2 if every entry exists but some entry on the path isn't owned;
  - 3 if the line is beyond the depth's capacity (Q2).
- **The shared cache:**
  - 512 KB in 128 B lines, 8 ways × 512 sets (the set is the LPDDR line address's low 9 bits), LRU, write-back,
    write-allocate;
  - it holds data and index lines alike;
  - a hit takes 6 cycles and a miss fills from LPDDR in 80;
  - one request outstanding;
  - the write-back policy (Q12): the next victim in every set is kept free or clean. A dirty LRU line is evicted at once,
    in the background, into a 4-line write buffer, which drains to LPDDR at one line per 80 cycles (parameters). A clean LRU
    line stays hittable and is dropped at no cost when a fill needs its way. A read that misses the cache waits until the
    buffer is empty;
  - cold at each segment's start (the word-0 write).
- **A Tracer write-back** walks like a read. A hit writes the line in place, dirty. A miss allocates without an LPDDR read
  (Q13), taking a free or clean way.
- **Reported per segment:** hit rates for data lines and index lines, the walk's share of cycles, early evictions,
  write-backs and read waits on the write buffer. Only the Tracer's traffic is modelled, so the hit rates are optimistic.

## 3. Host-side inputs (owner, 2026-11-05: built by WORKER26; WORKER22 loads them)

- **WORKER26 builds:**
  - each segment's start image (the cycle-0 image plus the earlier segments' updates, with the pages the segment writes
    owned), checked against gold's memory at the segment's first row;
  - word 66's values and the hash constants;
  - the map's lines and frame 6.
- **The format proposed to WORKER26** is in `NOTES.md`.
- **WORKER22's side (`TracerInputs`):**
  - loads those outputs into `FileStore`, which lays them out as §20 trees: the `.pages` file at depth 2, its shared pages
    reached through entries with owned = 0 and its owned pages through all-owned paths, and the map as a dense file;
  - builds word 66 and the record, converting frame 2's flat 131 × u64 from gold's segment file, with frame 3's advice.
- **Until WORKER26's outputs land,** only the minimal images the first functional tests need: hand-built programs, and a
  small guest's image from gold's loader with an every-start map. No timing is taken from them.

## 4. The firmware and the tiles

- **`sw/tracer/tracer_ctl.c` on C&C 3,** with a layout header in its 2 MB of big SRAM:
  - the contents: the decode table and the ROM, the 75 templates (221 KB) and their index, the record, the advice, the
    hash constants, `RUN_CONFIG`, and each tile's destinations (its record and three stream areas, from
    `replay_layout.h`);
  - the harness lands these through `CcCores.hostLoad`, standing in for C&C 1 and C&C 2;
  - the loop is 4.21.3's: load the tables, the constants and the record (word 0 first), mask the events, preload the
    advice, then run 64 windows of 65,536 rows;
  - it answers new expansion with `EXPANSION_TEMPLATE` before the first word, then feeds from `EXPANSION_OFFSET` on the
    credits; on a segment start inside an expansion, the first word carries rd0;
  - it answers the advice credit and the expansion credit, and serves queue 0 in four transfers and queues 1–3 by their
    credits, sized from words 37–39, waiting for DMA done between transfers;
  - it sends window 0's start to tile 0 from its own copy of the record (Q3);
  - at n = 63 it sends all 68 words to its own memory (dst 66) and forwards words 1 and 67 to tile 63 (Q4, Q11);
  - it checks the leftover advice and the end state at the end.
- **Tile side:**
  - `replay_ctl.c` takes its stream sizes from the record's stream word (layout version 5);
  - the harness preloads the universal templates into every tile (C&C 3's template broadcast is out of scope);
  - the harness starts the 64 replays after the segment's last done, and says so in the report.

## 5. Tests and acceptance

- **Directed tests (`TracerTest`):**
  - every `ERRORS` bit, every event and its depth, and both input credits;
  - the drain under adversarial service orders (no deadlock), set 1 and set 2 agreeing on sizes, raw against coded and the
    tie rule;
  - window cuts inside ROM and controller-fed expansions, a zero-row run (no code value, Q7), the end rows, padding;
  - the caches, the write-back buffer, straddling fetches, the probe's outcomes, every fill status;
  - the RVC decompressor against gold's on all 2¹⁶ encodings.
- **T1, the model:**
  - the Tracer, `MemorySystem` and an ideal Java controller;
  - per window, the record and streams are checked against WORKER9's delivery: raw streams exactly, coded ones by their
    decoded values; each window is then replayed by Stage A against `rows.bin`;
  - compression on and off;
  - small inputs first (trace-23 and input/22, with WORKER9's writer for the reference), then every segment of both
    workloads at 2²⁹, on the runner.
- **T2, the full chip:**
  - C&C 3's firmware, the DMA into 64 tiles, `replay_ctl.c` on every tile, rows against `rows.bin`;
  - what the DMA delivers must equal T1's output;
  - small inputs first, then the 2²⁹ subset under ruling E.
- **The suites:** the quick and full suites, and the full-size sort at 1,229,008 cycles.
- **Reported:** the core's cycles per row against 4; the all-in cycles per segment against 100 M; stalls by cause; the
  walk's share; the drain time. All of it is taken with WORKER26's map only.
- **Quest:** task numbers are claimed in NUMBERS.md first, kits are smoke-tested locally, there is an explicit exit before
  any embedded kit, and long runs go to the runner.

## Milestones

1. The shared helpers (replayer unchanged, checked), the Tracer's core and `MemorySystem`, with the ideal controller; the
   directed tests; small guests against gold.
2. The output path and the 128-bit DMA.
3. Loading WORKER26's inputs and T1, small, then at 2²⁹.
4. The DMA port, the firmware, the tile side and T2, small, then the subset.
5. The suites and the sort; `REPORT.md`.
