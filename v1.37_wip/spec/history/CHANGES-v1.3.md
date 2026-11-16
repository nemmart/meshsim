# Machine spec v1.26.1 → v1.3: what changed

`spec/machine-spec.md` is now **v1.3**: v1.26.1 with **the Tracer**, C&C 3's peripheral, specified in full as section 4.21,
and the changes elsewhere that it brings. Every row cites **V3-*n*** (`planning/DECISIONS-v1.3.md` section *n*) or **O**
for the owner's rulings on SP1's plan (2026-11-05, recorded in DECISIONS-v1.3 up to 830b8e7), **O6** for the owner's rulings
of 2026-11-06 (DECISIONS-v1.3 at a247a1f, and on WORKER30's plan), and **B1 Q*n*** for the answers to B1's spec questions
(`planning/sessions/WORKER22-SPEC-QUESTIONS.md`, all answered 2026-11-05 and 11-06; SP1's own questions stay under O). Built
on v1.26.1 (WORKER25's point release), which the integrator merges first. **The RTL's guide** from v1.26.1, unit by unit, is
`CHANGES-v1.26.1-to-v1.3.md`.

**How v1.3's text is meant** (O6): it makes reasonable default calls and states what the hardware must do observably, not how;
where the RTL finds a better way, the spec follows it.

**Scope.** The new section 4.21; 4.17's DMA; 4.19's ground truth, template broadcast and end of program; the places that
restate them (the front matter, 5, 6.3, 6.6, 6.7, 7, 8.11, 8.16, 9, 10 and 13); and `spec/replayer-opcodes.md`'s ground-truth
line. `spec/schedule.md` is unchanged. Section and invariant numbers are v1.26.1's, plus 4.21 and 8.16; v1.3's items are
144–158. **Not here:** the latency lines of section 10 (`go_latency`, `ready_latency`, `error_latency`, "1 in simulation"),
which WORKER28's latency commit changes (GO and READY at 8).

**For the RTL.** The replayer's hardware is unchanged: its `RUN_CONFIG` [28:26] become the Tracer's and are ignored (they
were reserved). The Tracer is new (4.21). Every sending peripheral's DMA changes (4.17.9).

## The changes in brief

1. **The Tracer, 4.21** (V3 1–25): what it is and what it shares with the replayer; its region and the 68-word record, with
   word 66's layout and word 1's live advice count; control (two pulses, the word-0 write's resets, `RUN_CONFIG` with the
   compression enables, words 36–43, the events, the input credits); execution (the replayer's rows and templates, advice
   from the host, the jump LRU and the hash probe, the end at any JAL with immediate 0, padding); memory (the 16 KB caches,
   the fill bus, its statuses, the range comparator, misalignment); output (the 66-bit log FIFO, two-pass compression, the
   drain by service requests with credits, done last); boundaries; errors; timing and cost; **the statistics counters**,
   words 44–51 (O6, item 157); **the memory system's timing as the shared cache** (B1 Q12, Q13).
2. **The peripheral DMA counts 128-bit words** (V3-24): every packet 128 bits, no remainder packets (4.17.9).
3. **The template broadcast** (V3-25): C&C 3 broadcasts the segment's used set when proving starts; each tile keeps the 60
   CSR templates and the inlines its bitmap names (4.19.9, 4.19.13).
4. **The ground truth** (V3-17): a16z's Jolt at `7189216` decides; gold is the Java emulator; jolt-cpp `88dca85` is the pin,
   checked: rows, segment files and `capture_writes` identical to `821e1bd` on both workloads (WORKER16, fa59d9a; 4.19).
   a16z's D13 and D14 are stated (4.19.4); the tables follow after the freeze (item 158, O6).
5. **The segment on the chip, 4.21.10** (V3-24; B1 Q3, Q4, Q14–Q17; O6): C&C 1's two barriers (READY unarmed, GO, arming on
   waking, nothing sooner than `stage_delay` = 64 after it; READY at counters 0, the final GO), the record's fixed pieces armed
   at the first barrier, the per-window "armed" handshake for the streams. Software only.

## Section by section

| section | change | cites |
|---|---|---|
| front matter | title v1.3; `CHANGES-v1.3.md` in the companion list; the figure set to 9.3; "v1.3 in brief", with 4.21.10; **how v1.3's text is meant**: defaults, observable behaviour | V3 1–25; O6 |
| 4.17.4 | `DMA_HDR`'s count in 128-bit words; words 32–35: `RUN_CONFIG` [28:26] the Tracer's compression enables, [31:29] reserved; the Tracer's words in 4.21.3 and 8.16 | V3-24; O |
| 4.17.9 | **the DMA counts 128-bit words**, up to 4,095; every packet 128 bits, `DMA_ADDR` advancing by 16, no remainder packets; the DMA sends only for a started transfer, the peripheral's section saying how its queues fill and ask to be drained; a transfer asking for more than its queue will hold is UB; the router port: src 74, the down port; **routing by dst, a packet for its router's own unit ejected there** | V3-24; O (item 118); B1 Q5, Q11 |
| 4.19 (ground truth) | rewritten to section 17: **a16z's Jolt at `7189216` decides** (the verifier's source, jolt-cpp's submodule); **gold is the Java emulator** (`wip/gold/`); **jolt-cpp `88dca85` is the pin, checked** (its `isa/` unchanged since `821e1bd`; every window's rows, the segment files and `capture_writes` identical on both workloads, WORKER16 at fa59d9a); jolt-cpp checked as data; the item-113 tags stay until gold settles each. Replaces `LayerZero-Research/jolt` `735355e` and jolt-cpp as gold standard | V3-14 (c), V3-17; O (Q9) |
| 4.19.1 | the reference: v1.3's pin `88dca85`, checked; `821e1bd`'s tables and citations stand | V3-14 (c) |
| 4.19.2 | the tracer engine is the Tracer (4.21); the files word's layout (memory root and depth, map root and depth, log₂ K, **the I/O region's size, 0x8000_0000 − `lowest_address`, in bytes**, [102:72], exact, replacing `lowest_address` / 4 KB); the end of trace from v1.3 is any JAL with immediate 0, whatever rd | V3-23, V3-25; O (Q5) |
| 4.19.4 | **D13** (the rd = x0 rewrite row never virtual) and **D14** (LR/SC with rd = x0 remapped), a16z's rule; the hardware unchanged; the tables still at `821e1bd`, rebuilt after the freeze (item 158) | V3-17; O6 (item 158) |
| 4.19.7 | the pc table: from v1.3 no trace holds a JALR or branch to pc 0 (the Tracer's `ERRORS` [7]); the rule stays | V3-25 |
| 4.19.9 | `RUN_CONFIG` [28:26] ignored by the replayer; the Tracer's word 32 runs at most 65,536 rows and enables compression; **the controller-fed set is broadcast** by C&C 3 when proving starts, all 60 CSR templates and the segment's inlines, each tile keeping the CSR templates and its bitmap's inlines (replacing every tile preloading the whole set) | V3-24, V3-25; O |
| 4.19.12 | the record's values checked against gold and a16z, not jolt-cpp as gold standard | V3-17 |
| 4.19.13 | the window's templates: those the record's word 67 names, kept from C&C 3's broadcast | V3-25 |
| **4.21** (new) | **the Tracer (peripheral type 3)**, 4.21.1–4.21.10: below; its opening cites B1's questions and states the stance | V3 1–25; O; O6; B1 Q1–Q17 |
| 4.21.1 | overview: its own core sharing the replayer's formats and tables; its rows are the replayer's; the parts; its interfaces (no host channel); a segment as 64 runs; the rate (4 cycles a row, 6 for a multiply) | V3-1, V3-5, V3-11, V3-25; O |
| 4.21.2 | the region (the replayer's map: part 2 the read queue, part 4 the expansion queue, parts 1 and 3 unused); the decode table and ROM loaded once at startup, the replayer's universal tables; the record word by word at load, while tracing and at a run's end (words 0 and 2–66 window *n* + 1's start, words 1 and 67 run *n*'s totals); **word 66's layout**; the advice field as the live count | V3-12, V3-13, V3-23, V3-24; O (word 66) |
| 4.21.3 | **two pulses**, no start pulse; **the word-0 write** empties every queue, sets the credits, clears the counters, invalidates the caches and the jump LRU without write-back; written only at a segment's start (a mid-segment rewrite UB); **`RUN_CONFIG`** [23:0] at most 65,536 rows (more is UB; a longer trace runs in chunks), [25:24] ignored, **[28:26] the compression enables**; `PULSE_RUN` starts a window (compressors, packers, output queues, bitmap) and leaves the segment's state alone; `STATUS`; **words 36–43** (`STATE_LENGTH`, `CODE_LENGTH`, `READ_LOG_LENGTH`, `WRITE_LOG_LENGTH`, `HASH_MUL`, `HASH_STEP`, `HASH_LINES`, `EXPANSION_TEMPLATE`); **the events** (*Proposed* positions, item 144); **the advice credit** and **the expansion credit**, half a queue each; **words 44–51, the statistics counters**: 32 bits, wrapping, cleared by the word-0 write, counting while `STATUS` [0] is 1, data stalls including the write-back wait, output stalls one per cycle either scan stalls, the waits on C&C 3 in none, defaults; the controller's loops, **with the segment's start (`stage_delay` after C&C 1's start), window 0's start from C&C 3's copy, the last record forwarded, the dropped expansion on a record load and the "armed" handshake** | V3-18, V3-24; O (Q2, Q3, Q8; words 36–43, the advice credit, the compression enables); O6 (item 157); B1 Q3, Q4, Q14, Q15, Q17 |
| 4.21.4 | the 128-register machine; fetch, RVC decompression as a16z's (D12), the code-stream word; the templates by the replayer's decode and rd = x0 rules; a run restarting inside an expansion; **the controller-fed path**, not presented again at a run boundary; **the template number by `EXPANSION_TEMPLATE`**, latched and re-set at `PULSE_RUN` inside an inline; **what each row does** (loads, stores, advice from the read queue, control transfers from the map, assertions evaluated, padding); **the jump LRU and the map** (the key *o* = pc − `lowest_address`, the hash, a 3-line probe, 8-byte entries, free key 0xFFFFFFFF); **the end at any JAL with immediate 0**, EBREAK and `JAL ra, 0` included, a JALR or branch to 0 the map error; padding while pc is 0; **stores into code** rejected by the host, [8] the Tracer's backstop, a deliberate divergence from a16z | V3-7, V3-13, V3-17, V3-18, V3-25; O (Q5, Q10; the map as a file, the probe, `EXPANSION_TEMPLATE`) |
| 4.21.5 | the memory system a black box; `.pages`; **the caches**, 128 lines of 128 B, fully associative, tagged by `.pages` line, write-back and write-allocate, a writable bit per data line; **the fill bus** (read and write of a (root, depth, line)); **the read statuses** (ok, **ok only if every entry on the path is owned**, hole, not owned, **beyond the depth's capacity**), [8] at the store, **the default interface** (item 148); straddling instructions; ordering (item 147); **the range comparator**; **misalignment, [12]**: **a16z's rule on every memory row**, the floor address ea − (ea & *m*), *m* = 0, 4, 6, 7, a multiple of 8, native and inline rows alike (`floor_access.rs:66–75`, jolt-cpp's `checked_memory_address`), with the native instruction's natural alignment as why it is right | V3-12, V3-15, V3-20, V3-21, V3-25; O (Q6, Q7; the fill responses; the misalignment rule, item 150); O6 (item 148); B1 Q1, Q2 |
| 4.21.6 | the record and three streams; **the FIFOs, the log FIFO 66 bits with a 2-bit tag** (load, other read log, write log) that never leaves the Tracer; **two-pass compression**, the encoders' rules (lowest index, E[15] first, the miss width *Proposed*, item 151); the choice per stream (enabled, fewer words, raw on a tie) and the stream word's output fields; **one scan per FIFO** into the packers and **4 KB output queues 1–3**; **credits per 2 KB chunk and for a stream's tail, live service flags**, no deadlock; sizing is C&C 3's, more is UB; **output queue 0 by a read pointer**, four transfers, tile *n*'s totals before its streams, the last record to C&C 3 (dst 66) and forwarded, window 0's start from C&C 3's copy; **a zero-row run writes no code value**; **done last**, once drained; where the output lands, every delivery counted | V3-2, V3-3, V3-4, V3-6, V3-24; O (Q1, Q2, Q4, Q8); B1 Q3, Q4, Q7, Q11, Q15 |
| 4.21.7 | window boundaries (nothing flushed); segment boundaries (the word-0 write; **C&C 3 drops any expansion it was feeding**); the end-of-segment checks in software | V3-5, V3-23, V3-24; B1 Q14 |
| 4.21.8 | **`ERRORS`**: [1] queue overrun, [2] invalid expansion, [4] advice underflow, [7] jump target not in the map, [8] store where the copy can't write (the default, item 148), [9] page doesn't exist (beyond the depth's capacity), [10] outside the provable range, [11] failed assertion, [12] misaligned access (it stops the Tracer); what an error does (item 152); **several errors in one row**: [10] and [12] together, [8] and [9] only if the access goes ahead | V3-24, V3-25; O (Q6, Q7; [12], 2026-11-05); O6 (item 148); B1 Q2, Q6 |
| 4.21.9 | **the timing parameters** (rows 4 and 6 cycles, `ms_line_hit` 6 and `ms_line_miss` 80 **per shared-cache access**, a fill the walk's depth + 1 accesses with no TLB, a write-back allocating without an LPDDR read, the probe's 2 per line and 3 lines, the drain and the DMA); **meshsim's model of the memory system** (the shared cache, the write buffer, cold at a segment's start); the stalls and which counters count them; memory, about 850 KB, and logic | V3-11, V3-18, V3-21; O (the timing parameters); B1 Q8, Q12, Q13 |
| **4.21.10** (new) | **the segment on the chip** (software): what each tile receives and on which counter; C&C 1's two barriers, the arming on waking, `stage_delay`, unexpected arrival as the detector; the per-window "armed" handshake; READY at counters 0; the final GO, all tiles replaying together; what comes later (landing in LPDDR, signed counters) | V3-24; B1 Q3, Q4, Q15, Q16 (reversed), Q17; O6 (`stage_delay` 64) |
| 5 | the Tracer's timing row | O |
| 6.3 | **a traced segment**: the READY/GO cycle twice, sequenced by C&C 1 (`CC_SEGMENT` in meshsim); C&C 3 outside READY/GO | V3-24; B1 Q15, Q17 |
| 6.6 | C&C 3's peripheral is the Tracer: its window, region, DMA and firmware's role | V3-24, V3-25 |
| 6.7 | **the Tracer's fill bus**: its misses don't use the request bus; its statuses (beyond the depth's capacity); the translator's walk, no TLB | V3-20, V3-21; B1 Q2 |
| 7 | what v1.3 fixes of the memory system (files of 4 KB pages, direct reads by (root, depth, line), one translator, the shared 512 KB cache, the fill bus); the rest a black box; **meshsim's model of it**: 8 ways × 512 sets, LRU, write-back, 6 and 80 cycles, the next victim kept free or clean, the 4-line write buffer, a read miss waiting for it to empty, the write-back's allocation without a read | V3-12, V3-20, V3-21; B1 Q12, Q13 |
| 8.11 | `DMA_HDR`'s count in 128-bit words; the replayer's `RUN_CONFIG` [28:26] ignored | V3-24; O |
| **8.16** (new) | **the Tracer's window**, every word and bit, **words 44–51 the statistics counters** | V3-24; O; O6 (item 157) |
| 9 | invariant 16: the replayer's rows against gold and a16z; **the Tracer's invariants**, with the dropped expansion and the counted deliveries | V3-17, V3-24, V3-25; B1 Q14, Q15 |
| 10 | the peripherals' DMA in 128-bit words; the replayer's reference (tables at `821e1bd`, pin `88dca85`, checked); **the Tracer's row** (fills through the shared cache, the counters); `stage_delay` 64 (already at 11-06) | V3-14, V3-24; O; O6 |
| 13 | the status of items 33, 57, 113, 115, 118, 119, 125, 137 and 141; **new items 144–158** | V3; O; O6 |
| `replayer-opcodes.md` | the ground-truth line brought to section 17 | V3-17 |

## v1.3's items (section 13)

| item | subject | status |
|---|---|---|
| 144 | the Tracer's `EVENTS` bit positions | accepted (O) |
| 145 | when C&C 3 stores `EXPANSION_TEMPLATE` | accepted (O) |
| 146 | the map's key shared with the host's tools; the direct [7] checks | accepted (O) |
| 147 | cache replacement and the fill bus's ordering | accepted (O); sizes to measure |
| 148 | the fill bus's statuses; [8] and [9] | **decided as the default** (O6): ok (every entry on the path owned, B1 Q1), hole, not owned, beyond the depth's capacity (B1 Q2); a hole in the map, accepted (O) |
| 149 | the range comparator's upper bound | **decided (O6): 8·K**, the cell index below K |
| 150 | the misalignment bit [12] | decided (O): a16z's floor-address rule on every memory row |
| 151 | the encoder's miss width | accepted (O) |
| 152 | what a Tracer error does | accepted (O) |
| 153 | the timing parameters, to measure in B1 | decided for meshsim (O); measured by B1 with the shared cache (WORKER22-REPORT section 4) |
| 154 | the pin `88dca85`'s checks | closed: passed (WORKER16, fa59d9a) |
| 155 | frame 7, the proof parameters | decided (O); its layout is the segment file's |
| 156 | the Tracer's ROM single-ported | accepted (O) |
| 157 | statistics registers | **decided (O6)**: words 44–51; the attributions defaults (data stalls with the write-back wait, output stalls either scan); modelled in meshsim |
| 158 | the universal tables to a16z's rule for D13 and D14 | **decided (O6)**: rebuilt right after the freeze, not in v1.3; no RTL effect |
