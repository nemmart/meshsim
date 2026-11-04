# Machine spec v1.25 → v1.26: what changed

`spec/machine-spec.md` is now v1.26: **v1.25 with exactly the replayer changes decided for v1.3**, and nothing else (no
Tracer, no new memory system, no file system). This file lists every section v1.26 touches, one row each. Every item cites
**`planning/DECISIONS-v1.26.md`** section 1, as **D26-*n*** (its item *n*), or **O** for an owner's ruling on the spec plan
(2026-11-01, recorded in `WORKER19/NOTES.md`; items 9–12 of D26 record the later ones). D26's items come from `DECISIONS-v1.3.md` sections 12, 23 and 24.

**Scope.** Only section 4.19 (the replayer) and the places that restate it (the front matter, 5, 8.11, 9, 10, 11 and 13) change.
4.17 (peripherals) is unchanged: the peripheral DMA in 128-bit words belongs to the Tracer (v1.3). Section numbers, invariant
numbers and item numbers are v1.25's; one item is added and closed (143). `spec/replayer-opcodes.md` and `spec/schedule.md` are unchanged.

**For the RTL:** `CHANGES-v1.25-to-v1.26.md` is the change guide for this step, by hardware block.

## The changes in brief

1. **The state record: 68 words in a new order** (D26-1): [0] state word, [1] stream word, [2–65] register image, [66] the files
   word, [67] the template bitmap (moved from first to last). Part 0's words 0–65 are v1.25's image; the record loads from byte 0,
   and writes to words 66–67 are ignored.
2. **The stream word** (D26-2): per stream its 128-bit words (16 bits, padding included), its values out (17 bits) and its
   compression flag (moved from the state word). Bit lengths are dropped.
3. **Every queue is 4 KB** (D26-3): the read-log and write-log queues shrink from 8 KB.
4. **Writing the state word resets the pass** (D26-4, D26-10): it flushes the four input queues (not the output queue), sets the
   credits to 2, clears the size counters and resets the decompressors. **`PULSE_RUN` only starts running.** Preloading the
   queues after the state write is allowed (D26-11).
5. **A stream's padding can be anything** (D26-5): the decoder stops at the value count.
6. **The size checks** (D26-6): underflow at once, leftover at done; the write log is excluded without the store log (D26-8).
7. **`ERRORS` [3]–[6]** (D26-7): code-stream, read-log and write-log underflow, done with input left over. Underflows stop the
   pass (O).
8. **Part 0's state is the working state** (D26-12): writing words 0–65 writes the live pc, `instr_idx`, offset and register
   file; there is no copy at a run's start and nothing is latched (the checks read the sizes and flags from part 0). So the
   controller **reloads words 0–65 before every pass**, about 66 cycles from big SRAM, as v1.25's internal copy took 64.
9. **The state word's per-pass flags** (D26-8): [115] use the store log (v1.25's "includes store log"), [116] **output `rd_prev`**
   (new); [114:112] reserved. Only the flags change per pass; the record is never edited.

## By section

| section | what changed | why |
|---|---|---|
| title, companion material | v1.26; `CHANGES-v1.26.md` and `CHANGES-v1.25-to-v1.26.md` listed | D26 |
| front matter | "v1.26 in brief" added before v1.25's | D26-1 to D26-12 |
| 4.19.1 overview | the read-log and write-log queues are 4 KB, 256 × 128 bits; the checkpoint is the record's words 0–65 **and the working state**; the working register file is the image; the record is reloaded before each pass; the errors list gains the underflows and the leftover check | D26-1, D26-3, D26-7, D26-12 |
| 4.19.2 the run log and the checkpoint | **the state word:** [114:112] reserved (the compression flags moved), [115] use the store log, [116] output `rd_prev` (new), [127:117] spare; its write resets the pass. **The stream word:** words [15:0], [31:16], [47:32], compression flags [48], [49], [50], values out [80:64], [97:81], [114:98] (code, read log, write log), with the bounds (33,792 words, 65,536 values). **The state record:** 68 words, 1,088 bytes, in the new order, landing at consecutive part-0 offsets from 0x000; words 66–67 ignored; loaded from byte 0 (was 67 words, the bitmap first, sent from byte 16). **Use the store log:** the write log's sizes are ignored when it is 0. **Output `rd_prev`:** new paragraph. **Only the flags change per pass.** **"Re-runs" replaced by "the working state and re-runs":** part 0 is the live state, running changes it in place, words 0–65 are reloaded before every pass, the checks read sizes and flags from part 0, nothing is copied or latched; **the logs' measured cost with 4 KB queues** (item 143): 39.2 + 100.0 and 43.5 + 84.5 cycles per window (EVM Zone, Cyclone; read + write log), 0.066 % and 0.063 % of a pass, against the 8 KB control's 0.102 % and 0.097 % | D26-1, D26-2, D26-8, D26-12; item 143 |
| 4.19.7 executing a row | the register file **is** the register image, written directly by the image's packets while stopped (was: not addressable, filled from the image at each start) | D26-12 |
| 4.19.8 the queues | sizes table: every queue 4 KB, half 128; the justification from the densities (a log's half refill lasts at least about 300 rows, some 900 cycles), **the measured Stage B log waits, 4 KB against the 8 KB control, on every window of both workloads** (item 143): 4 KB waits less, since the waits are all at a pass's start, where a smaller first refill lands each stream's first entry sooner, and after it a 128-entry refill always lands in time; a stream's end is its value count; credits are set to 2 by the state write (was: by `PULSE_RUN`); **"the start clears the queues" replaced by "writing the state word resets the pass"**: the write to offset 0x000 flushes the four input queues (not the output queue), sets the credits, clears the size counters and resets the decompressors; nothing else does but the reset; `PULSE_RUN` only starts running; words 0–65 are reloaded before every pass; **preloading allowed**; the software rule becomes "before the state write, every push has landed" | D26-3, D26-4, D26-10, D26-11, D26-12; O (flush scope) |
| 4.19.9 the events, the words and the loop | the window table's `ERRORS` row gains [3]–[6]; the loop reloads words 0–65 (state word first) before `PULSE_RUN`, which only starts running; what a stream has left is counted in words from the stream word; the state word is also the live state; "between passes" refers to the reload, and notes that a loop may preload | D26-2, D26-4, D26-7, D26-10, D26-11, D26-12 |
| 4.19.10 the region | the map's state-word, stream-word and image rows restated (live state, the working register file); **a new row, 0x000420–0x00043F: the record's words 66–67, accepted and ignored**, a write while running UB like any part-0 write; the read-log and write-log queue ports 4 KB; part 0's words 0–67 are the record's; **"how a controller loads part 0" rewritten**: words 0–65 before every pass, from big SRAM from byte 0 (about 66 cycles) or from data memory (about 264 stores); the ownership table: part 0 changed in place while running, a push before the state write flushed and one after it kept; item 76's paragraph restated (preloading allowed) | D26-1, D26-3, D26-4, D26-9, D26-11, D26-12 |
| 4.19.11 run, done and the errors | **`PULSE_RUN` sets `STATUS` [0], clears rows run and runs from the working state, and does nothing else**: no copy, no emptying, no credits, no counters, no decompressor reset; done checks the input and leaves part 0 holding the window's end state; **the errors table gains [3] code-stream underflow, [4] read-log underflow, [5] write-log underflow, [6] done with input left over**; [2] no longer covers a pop past a stream's end; underflows stop the pass like an invalid expansion; why the sizes are hardware-checked; after a trap, part 0 keeps its contents; meshsim flags a pass started with no state write since the previous one | D26-6, D26-7, D26-10, D26-12; O (underflows stop the pass) |
| 4.19.12 the output row | `rd_prev` is 0 in every row of a pass with output `rd_prev` off | D26-8 |
| 4.19.13 the resident region | the rules: reload words 0–65 before every pass from the tile's copy in big SRAM; pushes after the state write are kept (preloading); every push has landed before the state write, a software rule kept by the refill counters (the replayer can't see a push in flight, and meshsim doesn't check it) | D26-11, D26-12 |
| 4.19.14 cost | memory per tile recomputed: 16 KB of queues, the register file serving as the image (no separate 1 KB image), about **32 KB** in all (was 41 KB), about 2.0 MB per chip (was 2.6 MB), roughly 6–8 mm² with the logic; logic per tile about 0.09–0.14 mm² with its memories; the decompressors gain their size counters | D26-3, D26-6, D26-12 |
| 4.19.15 stream decompression | the compression flags are in the stream word; an entry also leaves when the stream's last value ends inside it; the LRU tables and E[15] reset at the state write (was: each run's start); **"the stream's end" rewritten**: sizes in words and values read from part 0, counters cleared by the state write, the decoder stops at the value count, padding can be anything, underflow at once, leftover at done (a whole extra padding word is an error), the write log excluded without the store log, the expansion queue unchecked; the cost names the size counters | D26-2, D26-5, D26-6, D26-8, D26-9, D26-10 |
| 5 timing summary | the replayer's line: the record is reloaded before each pass (about 66 cycles), and a run starts at once (was: a run copies the image in 64 cycles) | D26-12 |
| 8.11 the replayer's window | `ERRORS` row gains [3]–[6] | D26-7 |
| 9 invariant 16 | the replayer's hardware checks include the underflows and the leftover check; every push has landed before the state write; no part-0 write while a pass runs, words 66–67 included; words 0–65 reloaded before every pass | D26-4, D26-7, D26-11, D26-12 |
| 10 parameter summary | the replayer's row: all four queues 256 × 128 bits, reset by the state write; the 68-word record, words 0–65 the working state; the size fields; no start copy; about 32 KB per tile | D26-1, D26-2, D26-3, D26-10, D26-12 |
| 11 logic budget | the replayer's row: 32 KB of memories, about 256 Kbit, about 0.09–0.14 mm² | D26-3, D26-12 |
| 13 open items | a **v1.26 status table** (items 41, 52, 76, 103, 104, 108, 115, 123, each changed); **new item 143**, the 4 KB log queues' Stage B stall counts, **closed** with Quest 098–101's figures | D26-1 to D26-12 |

## Not changed

- **4.17** (peripherals): `PULSE_RUN`'s generic meaning is unchanged; what a replayer's run does is 4.19.11's.
- **The queue ports, credits and refill rule** (4.19.8): 128-bit packets only, depth-2 credits of half a queue, every refill but
  a stream's last a half; only the half's size changes for the logs (256 → 128 entries).
- **The expansion queue**: unchanged, unsized and unchecked; it ends by `seq_remaining`.
- **The register image's layout, the decode table, the ROM, the LRU coding and the output row's layout** are v1.25's.
- **The files word's layout** is the Tracer's (v1.3); the replayer ignores it.
