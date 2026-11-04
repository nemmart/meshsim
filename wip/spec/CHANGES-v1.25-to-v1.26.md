# The change guide from v1.25 to v1.26

For the RTL engineer of the mesh, upgrading from **v1.25** (whose guide from v1.1 or v1.2 is `CHANGES-to-v1.25.md`) to
**v1.26**. The step touches **only the replayer** (peripheral 1, type 4, on tiles 0–63); every other unit is v1.25's.

**Sources.** The authority is `spec/machine-spec.md` v1.26, section 4.19; this guide matches the spec as of commit `47d10da`
(2026-11-01). `CHANGES-v1.26.md` lists the same delta by spec section. Decisions: `planning/DECISIONS-v1.26.md` section 1 (cited
**D26-*n***) and the owner's rulings on the spec plan (**O**, recorded in `WORKER19/NOTES.md`). A later spec change to this step
needs an entry here.

## 1. In brief

| # | change | blocks touched | section |
|---|---|---|---|
| 1 | the state record is 68 words; part 0 ignores writes to its words 66–67 (0x420–0x43F) | region decode | 4.19.2, 4.19.10 |
| 2 | the stream word's new layout: words, values out and compression flag per stream; bit lengths gone | stream-word latches, decompressors | 4.19.2 |
| 3 | the state word: [114:112] reserved, [115] use the store log, [116] output `rd_prev` (new) | state-word latches, output row | 4.19.2, 4.19.12 |
| 4 | the read-log and write-log queues: 8 KB → 4 KB (512 → 256 entries, half 256 → 128) | two queue memories, their credit logic | 4.19.8 |
| 5 | the write to the state word flushes the four input queues; `PULSE_RUN` no longer empties them | queue control, run logic | 4.19.8, 4.19.11 |
| 6 | per-stream counters of words taken and values decoded, checked against the latched sizes | decompressors | 4.19.15 |
| 7 | `ERRORS` [3]–[6]; [2] loses "a pop past a stream's length" | error logic | 4.19.11 |

## 2. By block

### 2.1 The region decode and part 0 (4.19.10)

| ID | v1.25 | v1.26 | why |
|---|---|---|---|
| P1 | part 0 offsets 0x000–0x41F: state word, stream word, register image; 0x420–0x0FFF unmapped (a write there UB) | **0x420–0x43F (words 66–67, the files word and the template bitmap) accepted and dropped**: decoded, not stored. Like the rest of part 0, written only while stopped | D26-1 |
| P2 | a write to the state word stores it | the same, **and pulses the queue flush** (2.3) | D26-4 |
| P3 | the record arrives from byte 16 (word 0 was the bitmap) | the record arrives from byte 0; nothing changes in the port, which sees words 0–65 at the same offsets as v1.25's image | D26-1 |

Nothing else in the map moves: the decode table, the ROM and the four queue ports keep their parts and offsets.

### 2.2 The state word and the stream word (4.19.2)

| ID | v1.25 | v1.26 | why |
|---|---|---|---|
| S1 | state word [112]–[114]: each stream LRU-compressed; [115] includes store log; [127:116] spare | **[114:112] reserved** (ignored), **[115] use the store log** (same function, renamed), **[116] output `rd_prev`**; [127:117] spare | D26-2, D26-8 |
| S2 | stream word: code length [23:0], read-log length [55:32], write-log length [87:64], in bits | **words** [15:0], [31:16], [47:32]; **compressed** [48], [49], [50]; **values out** [80:64], [97:81], [114:98] (code, read log, write log); [63:51] and [127:115] spare | D26-2 |
| S3 | a run latches four flags and three 24-bit lengths | a run latches **two flags** (state word) and, per stream, **a 16-bit word count, a 17-bit value count and its compression flag** (stream word) | D26-2, D26-8 |

### 2.3 The queues and the flush (4.19.8)

| ID | v1.25 | v1.26 | why |
|---|---|---|---|
| Q1 | read-log and write-log queues 8 KB, 512 × 128 bits, credit returned per 256 entries popped | **4 KB, 256 × 128 bits, credit per 128 entries**, like the code and expansion queues (all four identical now) | D26-3 |
| Q2 | `PULSE_RUN` empties all four queues (contents and occupancy) | **the state word's write empties all four input queues** as it lands; the 32-row output queue is not touched. Nothing else empties them but the reset | D26-4; O |
| Q3 | `PULSE_RUN` sets every queue's credits to 2 | unchanged: `PULSE_RUN` still sets the credits to 2; the queues are empty then (software's rule, below) | O |
| Q4 | a push while stopped lands and is discarded by the next run | a push while stopped lands and is discarded by the next **state write**; software never pushes between the state write and the run (no preloading) | O |

### 2.4 Run and done (4.19.11)

| ID | v1.25 | v1.26 | why |
|---|---|---|---|
| U1 | `PULSE_RUN`: empty the queues, credits to 2, reset the decompressors, latch, copy the image, clear rows run | **no emptying**; the rest as before, **plus clear the six size counters** (2.5) | D26-4, D26-6 |
| U2 | done: `STATUS` [0] falls, run done posted; queues left as they are | the same, **plus the leftover check** ([6], 2.6) at the 65,536th row | D26-6 |

### 2.5 The decompressors and their size counters (4.19.15)

| ID | v1.25 | v1.26 | why |
|---|---|---|---|
| D1 | a 24-bit length counter per stream; the decoder ignores the bits after the length; the tracer pads with zero bits | per stream **an entries-taken counter (16 bits) and a values-decoded counter (17 bits)**; **the decoder stops at the value count**: after the last value it releases the current entry and decodes nothing more; the padding bits can be anything | D26-2, D26-5 |
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

The two log queues shrink by 4 KB each: **about 33 KB of replayer memory per tile** (was 41 KB), about 2.1 MB per chip (was
2.6 MB). The size counters (six, 99 bits in all per tile), the flush and the new error bits are a few hundred flops.

## 3. Software-visible rules that change (meshsim checks them; no hardware)

- The controller writes the state word **before every pass**, even re-running the same window (O): its flags are the pass's,
  and its write clears leftover expansion words from a pass that ended inside an expansion.
- Every queue push has landed **before the state write** (was: before the start), and the queues are **empty at `PULSE_RUN`**
  (invariant 16).
- A stream's word count is the fewest 128-bit entries holding its values; a whole extra padding word is an error ([6]).
- Refill sizes are counted from the stream word's word counts (was: bit lengths rounded up).

## 4. Verification

meshsim's replayer model follows the spec at v1.26 (WORKER20), with tests for the flush, each new error bit and output
`rd_prev`; v1.26's acceptance is every window of EVM Zone and Cyclone at 2²⁹ matched in stages A, B and F with the 4 KB queues,
as v1.25's was (D26 section 2). Stage B's stall counts with the 4 KB queues are spec item 143.
