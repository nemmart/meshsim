# Machine spec v1.25 → v1.26: what changed

`spec/machine-spec.md` is now v1.26: **v1.25 with exactly the replayer changes decided for v1.3**, and nothing else (no
Tracer, no new memory system, no file system). This file lists every section v1.26 touches, one row each. Every item cites
**`planning/DECISIONS-v1.26.md`** section 1, as **D26-*n*** (its item *n*), or **O** for an owner's ruling on the spec plan
(2026-11-01, recorded in `WORKER19/NOTES.md`). D26's items come from `DECISIONS-v1.3.md` sections 12, 23 and 24.

**Scope.** Only section 4.19 (the replayer) and the places that restate it (the front matter, 8.11, 9, 10, 11 and 13) change.
4.17 (peripherals) is unchanged: the peripheral DMA in 128-bit words belongs to the Tracer (v1.3). Section numbers, invariant
numbers and item numbers are v1.25's; one item is added (143). `spec/replayer-opcodes.md` and `spec/schedule.md` are unchanged.

**For the RTL:** `CHANGES-v1.25-to-v1.26.md` is the change guide for this step, by hardware block.

## The changes in brief

1. **The state record: 68 words in a new order** (D26-1): [0] state word, [1] stream word, [2–65] register image, [66] the files
   word, [67] the template bitmap (moved from first to last). Part 0's words 0–65 are v1.25's image; the record loads from byte 0,
   and writes to words 66–67 are ignored.
2. **The stream word** (D26-2): per stream its 128-bit words (16 bits, padding included), its values out (17 bits) and its
   compression flag (moved from the state word). Bit lengths are dropped.
3. **Every queue is 4 KB** (D26-3): the read-log and write-log queues shrink from 8 KB.
4. **Writing the state word flushes the four input queues** (D26-4), in place of `PULSE_RUN` emptying them; software writes it
   before every pass (O); the queues are empty at `PULSE_RUN`, no preloading (O).
5. **A stream's padding can be anything** (D26-5): the decoder stops at the value count.
6. **The size checks** (D26-6): underflow at once, leftover at done; the write log is excluded without the store log (D26-8).
7. **`ERRORS` [3]–[6]** (D26-7): code-stream, read-log and write-log underflow, done with input left over. Underflows stop the
   pass (O).
8. **The state word's per-pass flags** (D26-8): [115] use the store log (v1.25's "includes store log"), [116] **output `rd_prev`**
   (new); [114:112] reserved. Only the flags change per pass; the record is never edited.

## By section

| section | what changed | why |
|---|---|---|
| title, companion material | v1.26; `CHANGES-v1.26.md` and `CHANGES-v1.25-to-v1.26.md` listed | D26 |
| front matter | "v1.26 in brief" added before v1.25's | D26-1 to D26-8 |
| 4.19.1 overview | the read-log and write-log queues are 4 KB, 256 × 128 bits; the checkpoint is the record's words 0–65; the state word is rewritten before each pass; the errors list gains the underflows and the leftover check | D26-1, D26-3, D26-4, D26-7; O |
| 4.19.2 the run log and the checkpoint | **the state word:** [114:112] reserved (the compression flags moved), [115] use the store log, [116] output `rd_prev` (new), [127:117] spare; its write flushes the queues. **The stream word:** words [15:0], [31:16], [47:32], compression flags [48], [49], [50], values out [80:64], [97:81], [114:98] (code, read log, write log), with the bounds (33,792 words, 65,536 values). **The state record:** 68 words, 1,088 bytes, in the new order, landing at consecutive part-0 offsets from 0x000; words 66–67 ignored; loaded from byte 0 (was 67 words, the bitmap first, sent from byte 16). **Use the store log:** the write log's sizes are ignored when it is 0. **Output `rd_prev`:** new paragraph. **Only the flags change per pass.** **Re-runs:** a run latches the two flags and the stream word's sizes and flags | D26-1, D26-2, D26-8 |
| 4.19.8 the queues | sizes table: every queue 4 KB, half 128; the justification from the densities, the Stage B stall counts pending (item 143); a stream's end is its value count; **"the start clears the queues" replaced by "writing the state word flushes the queues"**: the write to offset 0x000 empties the four input queues (not the output queue); the state word is written before every pass; the queues are empty at `PULSE_RUN`, no preloading; the software rule moves to "before the state write, every push has landed" | D26-3, D26-4; O (flush scope, before every pass, empty at the run) |
| 4.19.9 the events, the words and the loop | the window table's `ERRORS` row gains [3]–[6]; the loop writes the state word before `PULSE_RUN`, and `PULSE_RUN` no longer empties the queues; what a stream has left is counted in words from the stream word; "between passes" refers to the state write's flush | D26-2, D26-4, D26-7; O |
| 4.19.10 the region | the map's state-word and stream-word rows restated; **a new row, 0x000420–0x00043F: the record's words 66–67, accepted and ignored**; the read-log and write-log queue ports 4 KB; part 0's words 0–67 are the record's; loading from big SRAM starts at the record's byte 0; the state word is resent before every pass; the ownership table: a push is flushed by the next state write, none between the state write and the run; item 76's paragraph restated | D26-1, D26-3, D26-4; O |
| 4.19.11 run, done and the errors | `PULSE_RUN` empties nothing: it sets the credits, resets the decompressors, latches the flags and sizes, clears the size counters; done checks the input (leftover expansion words wait for the next flush); **the errors table gains [3] code-stream underflow, [4] read-log underflow, [5] write-log underflow, [6] done with input left over**; [2] no longer covers a pop past a stream's end; underflows stop the pass like an invalid expansion; why the sizes are hardware-checked | D26-4, D26-6, D26-7; O (underflows stop the pass) |
| 4.19.12 the output row | `rd_prev` is 0 in every row of a pass with output `rd_prev` off | D26-8 |
| 4.19.13 the resident region | the rules: write the state word before every pass; no preloading (the state write flushes, the queues empty at the start); every push has landed before the state write | D26-4; O |
| 4.19.14 cost | memory per tile recomputed: 16 KB of queues, about **33 KB** in all (was 41 KB), about 2.1 MB per chip (was 2.6 MB), roughly 6–8 mm² with the logic; logic per tile about 0.09–0.14 mm² with its memories; the decompressors gain their size counters | D26-3, D26-6 |
| 4.19.15 stream decompression | the compression flags are in the stream word; an entry also leaves when the stream's last value ends inside it; **"the stream's end" rewritten**: sizes in words and values, the decoder stops at the value count, padding can be anything, underflow at once, leftover at done (a whole extra padding word is an error), the write log excluded without the store log, the expansion queue unchecked; the cost names the size counters | D26-2, D26-5, D26-6, D26-8 |
| 8.11 the replayer's window | `ERRORS` row gains [3]–[6] | D26-7 |
| 9 invariant 16 | the replayer's hardware checks include the underflows and the leftover check; every push has landed before the state write, and every queue is empty at a run | D26-4, D26-7; O |
| 10 parameter summary | the replayer's row: all four queues 256 × 128 bits, flushed by the state write; the 68-word record; the size fields; about 33 KB per tile | D26-1, D26-2, D26-3, D26-4 |
| 11 logic budget | the replayer's row: 33 KB of memories, about 265 Kbit, about 0.09–0.14 mm² | D26-3 |
| 13 open items | a **v1.26 status table** (items 52, 76, 103, 104, 108, 115, 123, each changed); **new item 143**, the 4 KB log queues' Stage B stall counts, open until WORKER20's acceptance runs | D26-1 to D26-8 |

## Not changed

- **4.17** (peripherals): `PULSE_RUN`'s generic meaning is unchanged; what a replayer's run does is 4.19.11's.
- **The queue ports, credits and refill rule** (4.19.8): 128-bit packets only, depth-2 credits of half a queue, every refill but
  a stream's last a half; only the half's size changes for the logs (256 → 128 entries).
- **The expansion queue**: unchanged, unsized and unchecked; it ends by `seq_remaining`.
- **The register image, the decode table, the ROM, the LRU coding and the output row's layout** are v1.25's.
- **The files word's layout** is the Tracer's (v1.3); the replayer ignores it.
