# WORKER21 report: SP1, the Tracer's spec chapter

Branch `v1.3-spec`, from `origin/v1.26.1` (fast-forwarded to c16fbc1, which touched no spec file). Edits only in
`wip/spec/` and here. The decisions are `planning/DECISIONS-v1.3.md` on `main` up to 830b8e7 (read there, not merged into
this branch), with the owner's rulings on my plan of 2026-11-05.

## Delivered

- **`machine-spec.md` 4.21, the Tracer (peripheral type 3)**, nine subsections: overview; region and record (word 66's
  layout); control (pulses, the word-0 write, `RUN_CONFIG` [28:26], words 36–43, events, credits, the firmware loops);
  execution (rows, templates, the controller-fed path and `EXPANSION_TEMPLATE`, the map and its probe, the end, padding,
  stores into code); memory (caches, fill bus and statuses, range comparator, misalignment); output (FIFOs, two-pass
  compression, the drain by service requests, queue 0's pointer, done last); boundaries; errors; timing and cost.
- **Elsewhere in the spec:** 4.17.4 and 4.17.9 (the DMA in 128-bit words); 4.19's ground truth, 4.19.1, 4.19.2, 4.19.7,
  4.19.9 (the template broadcast), 4.19.12, 4.19.13; 6.6, 6.7, 7; 8.11 and a new 8.16 (the Tracer's window); invariant 16;
  sections 5 and 10; section 13's status table and items 144–157; the front matter; `replayer-opcodes.md`'s ground-truth
  line.
- **`CHANGES-v1.3.md`**: one row per section touched, each citing DECISIONS-v1.3 or the owner's rulings, and the new items.

## Rulings applied (owner, 2026-11-05)

All my proposals as written (words 36–43 with `HASH_LINES` its own word, word 66, `RUN_CONFIG` [28:26], the advice credit,
`EXPANSION_TEMPLATE`, the map as a file with a 3-line probe, the fill responses with a writable bit, frame 7, the timing
parameters); Q1 the 66-bit log FIFO; Q2/Q8 the drain by service requests with credits, done last; Q3 the expansion credit;
Q4 queue 0's read pointer; Q5 any JAL with immediate 0 ends; Q6 [8] for a hole or a page not owned, provisional; Q7 a
failed assertion checked; Q9 the pin pending; Q10 stores into code caught by the host, [8] the backstop.

## Findings and choices to check

1. **Misalignment, `ERRORS` [12]** *(Proposed, item 150)*. The Q7 ruling adds a bit only if an access path has no
   alignment assertion before it. **At `821e1bd` none has one:** `VirtualAssertWordAlignment` and
   `VirtualAssertHalfwordAlignment` are gone (spec 4.19.7, appendix 2.0), LD and SD are one-word templates
   (`tables/replayer/listing.txt`), the sub-word loads are three rows with no assertion, and both tracers abort instead
   (`WORKER18/UPSTREAM-DIFFS.md` D6). So I added [12]: a load or store row's address not a multiple of 8, jolt-cpp's
   `checked_memory_address` test. Drop it if you'd rather leave misalignment unchecked.
2. **The service flags are live, and a credit is taken when the DMA has read the last entry it covers** (4.21.6), rather
   than "per transfer": it gives the same behaviour for one transfer per credit, and also covers queue 0's four transfers
   against its one credit, and a chunk split in two.
3. **The template latch** (4.21.4): `PULSE_RUN` re-sets the latched inline's bit when a run starts inside it, so the tile
   whose window starts inside an inline is sent it. Without this, window *n* + 1's bitmap would miss it.
4. **Two direct [7] checks** *(Proposed, item 146)*: a target with *o* = pc − `lowest_address` ≥ 2³² (it would alias a
   32-bit key), and a JALR or branch to pc 0 (its key could match a real entry by wraparound).
5. **A hole in the jump map sets [9]** *(Proposed, item 148)*: the map is dense, and a zeroed line would match key 0.
6. **Smaller proposals:** the encoder's 32-bit miss rule (gold's, item 151); true LRU in both caches and the fill bus's
   ordering with a one-entry write-back buffer (item 147); what an error does (item 152); the ROM single-ported (item
   156); `RUN_CONFIG` [23:0] ≤ 65,536 on the Tracer, more UB (the FIFOs' size, §2); meshsim's check for a mid-segment
   word-0 write (advice left above 0), since it can't otherwise tell one from a new segment.
7. **The end-of-segment checks need queue 0 read into C&C 3's own memory** at the 64th run: C&C 3 can't load part 0, and
   the pointer reads each word once, so it sends all 68 words to itself and forwards words 1 and 67 to tile 63 (4.21.3).

## For others

- **DECISIONS-v1.3** (integrator): section 24 still carries "Proposed: the queue lengths at words 36-39" and the template
  number's two options; section 18 "where the Tracer keeps it: open" (ruled: a file, no SRAM); section 25's misalignment
  sentence assumes alignment assertions that no longer exist (finding 1). Frame 7 isn't in section 16's table yet.
- **meshsim (B1, WORKER22):** the replayer must now **ignore** `RUN_CONFIG` [28:26] (reserved at v1.26.1, so a 1 there
  may be flagged by invariant 21 today). The Tracer's model follows 4.21 and 8.16; the black-box memory is 4.21.9's two
  latencies keyed by (root, depth, line) since the last word-0 write.
- **Gold and WORKER16's tools:** frame 7 (item 155; layout theirs); **the map's key is *o* = pc − `lowest_address`**, so
  frame 6's pc offsets and the static table use it (item 146); frame 2 takes the record's layout (§23).
- **Figures (WORKER24):** 9.3's advice field is at the stream word's [124:102] (v1.26.1), not the v1.26 spares, and word
  66's positions are now fixed (4.21.2); 9.1's open items 11–17 are answered by 4.21 (fill bus, probe bound, the shared
  subtractor, the compression enables, the bits, words 36–43, output queues); figure 9.2 doesn't exist yet.
- **The pin:** item 154 closed (owner, 2026-11-05): WORKER16's check passed (fa59d9a); 4.19 states `88dca85` as the pin, checked.

## Rulings on the report (owner, 2026-11-05; DECISIONS-v1.3 up to 7fab0ad)

- **Misalignment, [12]:** kept, and it stops the Tracer, but checked **on the native instruction**, natural alignment for
  its access size (8, 4, 2, never for bytes; LR, SC and the AMOs by their width). **Confirmed against the sources:** a16z
  at 7189216 tests each memory row's floor address ea − (ea & *m*), *m* = 0/4/6/7, for a multiple of 8
  (`tracer/src/instruction/floor_access.rs:66–75`; LD and SD in `tracer/src/emulator/mmu.rs:323, 431`), and gold's port of
  jolt-cpp's `checked_memory_address` does the same (`wip/gold/src/gold/Isa.java:60–68`). That is ea mod 8, 4, 2 and
  nothing, so, every memory row of these templates taking the instruction's rs1, immediate and width, the native check
  fails on exactly the instructions the tracers abort on. (My first row check, on the floor address, was equivalent; only a
  check on the doubleword address ea & ~7 would miss sub-word accesses. The native one is clearer.)
- **One gap, proposed (item 150):** the **inline templates' own LD and SD rows** (gold's `Sha256Inline.java:53, 59` and
  the other inlines) aren't native loads or stores, and both tracers abort on a misaligned one. 4.21.5 proposes checking
  those rows, in a controller-fed expansion, for a multiple of 8, with the same bit.
- **A run longer than 65,536 rows is UB**, a longer trace run in chunks; **every other proposal accepted**, and the
  section-13 items marked so.
