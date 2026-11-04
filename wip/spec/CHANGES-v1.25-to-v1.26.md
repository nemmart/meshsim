# The change guide from v1.25 to v1.26

For the RTL engineer of the mesh, upgrading from **v1.25** (whose guide from v1.1 or v1.2 is `CHANGES-to-v1.25.md`) to
**v1.26**. The step touches **only the replayer** (peripheral 1, type 4, on tiles 0–63); every other unit is v1.25's.

**Sources.** The authority is `spec/machine-spec.md` v1.26, section 4.19; this guide matches the spec as of commit `cbdcd90`
(2026-11-01, with DECISIONS-v1.26 items 9–12). `CHANGES-v1.26.md` lists the same delta by spec section. Decisions: `planning/DECISIONS-v1.26.md` section 1 (cited
**D26-*n***) and the owner's rulings on the spec plan (**O**, recorded in `WORKER19/NOTES.md`). A later spec change to this step
needs an entry here.

## 1. In brief

| # | change | blocks touched | section |
|---|---|---|---|
| 1 | the state record is 68 words; part 0 ignores writes to its words 66–67 (0x420–0x43F) | region decode | 4.19.2, 4.19.10 |
| 2 | the stream word's new layout: words, values out and compression flag per stream; bit lengths gone | part 0's stream word (read in place), decompressors | 4.19.2 |
| 3 | the state word: [114:112] reserved, [115] use the store log, [116] output `rd_prev` (new) | part 0's state word (read in place), output row | 4.19.2, 4.19.12 |
| 4 | the read-log and write-log queues: 8 KB → 4 KB (512 → 256 entries, half 256 → 128) | two queue memories, their credit logic | 4.19.8 |
| 5 | **the write to the state word resets the pass** (flush, credits, size counters, decompressors); **`PULSE_RUN` only starts running** | queue control, credits, decompressors, run logic | 4.19.8, 4.19.11 |
| 6 | per-stream counters of words taken and values decoded, cleared by the state write, checked against the sizes in part 0 | decompressors | 4.19.15 |
| 7 | `ERRORS` [3]–[6]; [2] loses "a pop past a stream's length" | error logic | 4.19.11 |
| 8 | **part 0's state is the working state**: the image is the register file, the state word's pc, `instr_idx` and offset are the live ones; no copy at a run's start, no latches | register file, state registers, run logic | 4.19.2, 4.19.7, 4.19.11 |

## 2. By block

### 2.1 The region decode and part 0 (4.19.10)

| ID | v1.25 | v1.26 | why |
|---|---|---|---|
| P1 | part 0 offsets 0x000–0x41F: state word, stream word, register image; 0x420–0x0FFF unmapped (a write there UB) | **0x420–0x43F (words 66–67, the files word and the template bitmap) accepted and dropped**: decoded, not stored. Like the rest of part 0, written only while stopped | D26-1 |
| P2 | a write to the state word stores it | it writes the **live** pc, `instr_idx` and offset (2.4), **and pulses the pass reset** (2.3) | D26-4, D26-10, D26-12 |
| P3 | the record arrives from byte 16 (word 0 was the bitmap) | the record arrives from byte 0, words 0–65 at the same offsets as v1.25's image, **before every pass** | D26-1, D26-12 |
| P4 | the image's packets write a 1 KB image memory | they **write the working register file directly**, two registers per 128-bit packet, one packet per cycle, while stopped (the file is true dual-port; both ports take the packet's two halves). The separate image memory is gone | D26-12 |

Nothing else in the map moves: the decode table, the ROM and the four queue ports keep their parts and offsets.

### 2.2 The state word and the stream word (4.19.2)

| ID | v1.25 | v1.26 | why |
|---|---|---|---|
| S1 | state word [112]–[114]: each stream LRU-compressed; [115] includes store log; [127:116] spare | **[114:112] reserved** (ignored), **[115] use the store log** (same function, renamed), **[116] output `rd_prev`**; [127:117] spare | D26-2, D26-8 |
| S2 | stream word: code length [23:0], read-log length [55:32], write-log length [87:64], in bits | **words** [15:0], [31:16], [47:32]; **compressed** [48], [49], [50]; **values out** [80:64], [97:81], [114:98] (code, read log, write log); [63:51] and [127:115] spare | D26-2 |
| S3 | a run latches four flags and three 24-bit lengths | **nothing is latched**: the checks, the decoders and the output row read the two flags (state word [116:115]) and, per stream, **the 16-bit word count, the 17-bit value count and the compression flag** (stream word) straight from part 0, which can't be written while running | D26-2, D26-8, D26-12 |

### 2.3 The queues and the flush (4.19.8)

| ID | v1.25 | v1.26 | why |
|---|---|---|---|
| Q1 | read-log and write-log queues 8 KB, 512 × 128 bits, credit returned per 256 entries popped | **4 KB, 256 × 128 bits, credit per 128 entries**, like the code and expansion queues (all four identical now) | D26-3 |
| Q2 | `PULSE_RUN` empties all four queues (contents and occupancy) | **the state word's write empties all four input queues** as it lands; the 32-row output queue is not touched. Nothing else empties them but the reset | D26-4; O |
| Q3 | `PULSE_RUN` sets every queue's credits to 2 | **the state word's write sets them to 2** | D26-10 |
| Q4 | a push while stopped lands and is discarded by the next run | a push lands and is kept until the next **state write** flushes it; a push after the state write is kept whether it lands before `PULSE_RUN` or after (**preloading allowed**) | D26-11 |

### 2.4 Run and done (4.19.11)

| ID | v1.25 | v1.26 | why |
|---|---|---|---|
| U1 | `PULSE_RUN`: empty the queues, credits to 2, reset the decompressors, copy the state word into working pc, `instr_idx` and offset, latch the flags and lengths, copy the image into the working file (64 cycles), clear rows run | **sets `STATUS` [0], clears rows run and runs from the working state as it is; nothing else**. The queue emptying, credits, decompressor reset and the (new) size-counter clear move to the state write (2.3); the copies and latches are gone (2.1 P4, 2.2 S3). The first row starts at once | D26-10, D26-12 |
| U2 | done: `STATUS` [0] falls, run done posted; queues left as they are; part 0 unchanged | the same, **plus the leftover check** ([6], 2.6) at the 65,536th row; **part 0 holds the window's end state** (pc, `instr_idx`, offset, registers), changed in place by the pass | D26-6, D26-12 |

### 2.5 The decompressors and their size counters (4.19.15)

| ID | v1.25 | v1.26 | why |
|---|---|---|---|
| D1 | a 24-bit length counter per stream; the decoder ignores the bits after the length; the tracer pads with zero bits | per stream **an entries-taken counter (16 bits) and a values-decoded counter (17 bits), cleared by the state write**; the LRU tables (and E[15]) also reset at the state write, not at the run; **the decoder stops at the value count**: after the last value it releases the current entry and decodes nothing more; the padding bits can be anything | D26-2, D26-5 |
| D2 | an entry leaves its queue when its last bit is consumed or the stream ends inside it | when its last bit is consumed or **the stream's last value ends inside it** | D26-2 |
| D3 | a pop past the length is an invalid expansion | **underflow** (2.6): a value requested past the value count, or an entry needed past the word count | D26-6 |
| D4 | the write log decoded only with the store log | the same; **its size checks are disabled** when [115] = 0 | D26-8 |

The expansion queue gets no counters: it ends by `seq_remaining`, as before.

### 2.6 The errors (4.19.11)

| bit | v1.25 | v1.26 | set when | effect |
|---|---|---|---|---|
| 2 | invalid expansion, including a pop past a stream's length | invalid expansion, **without** the stream's end | as v1.25, less that case | stops the pass |
| 3 | — | **code-stream underflow** | 2.5's D3 on the code stream | **stops the pass** (`STATUS` [0] falls, run done posted), as [2] |
| 4 | — | **read-log underflow** | the same, read log | stops the pass |
| 5 | — | **write-log underflow** | the same, write log; never with [115] = 0 | stops the pass |
| 6 | — | **done with input left over** | at the 65,536th row, any stream (the write log only with [115] = 1) whose entries taken ≠ its words or values decoded ≠ its values out | set as the pass ends normally |

All are sticky until the reset and raise the tile's `ERRORS` [11] like the others. [6] is one bit; the controller reads the queue
occupancies to see which stream.

### 2.7 The output row (4.19.12)

| ID | v1.25 | v1.26 | why |
|---|---|---|---|
| O1 | row word 3 [63:0] is `rd_prev`, rd's value before the row | the same when [116] = 1; **0 when [116] = 0** (a mux or an enable on that field); nothing else in the row or the register file changes | D26-8 |

### 2.8 Memory and area (4.19.14, 11)

The two log queues shrink by 4 KB each, and the separate 1 KB register image goes (the register file is the image): **about 32 KB
of replayer memory per tile** (was 41 KB), about 2.0 MB per chip (was 2.6 MB). The working pc, `instr_idx` and offset are the
state word's own fields, so their separate copies go too. The size counters (six, 99 bits in all per tile), the pass reset and
the new error bits are a few hundred flops.

## 3. Software-visible rules that change (meshsim checks them; no hardware)

- The controller **reloads the record's words 0–65 before every pass**, even re-running the same window (D26-12): part 0 holds
  the last pass's end state. About 66 cycles from big SRAM. The state word carries the pass's flags, and its write resets the
  pass, clearing among other things leftover expansion words from a pass that ended inside an expansion.
- Every queue push has landed **before the state write** (was: before the start); pushes after it may come before or after
  `PULSE_RUN` (preloading, D26-11).
- No part-0 write while a pass runs, words 66–67 included (invariant 16).
- A stream's word count is the fewest 128-bit entries holding its values; a whole extra padding word is an error ([6]).
- Refill sizes are counted from the stream word's word counts (was: bit lengths rounded up).

## 4. Verification

meshsim's replayer model follows the spec at v1.26 (WORKER20), with tests for the flush, each new error bit and output
`rd_prev`; v1.26's acceptance is every window of EVM Zone and Cyclone at 2²⁹ matched in stages A, B and F with the 4 KB queues,
as v1.25's was (D26 section 2). Stage B's stall counts with the 4 KB queues are spec item 143.
