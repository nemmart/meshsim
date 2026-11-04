# WORKER19 notes: what WORKER20 needs early (v1.26)

The spec text follows these; where `wip/spec/machine-spec.md` and this file differ, the spec wins once it is pushed.
Decisions: `wip/planning/DECISIONS-v1.26.md` (cited **D26-*n***, its section 1 item *n*); owner rulings on WORKER19's plan
(2026-11-01) cited **O**.

## 1. The record (D26-1)

68 × 128-bit words, 1,088 bytes, landing in part 0 at consecutive offsets from 0x000:

| word | offset | contents | replayer |
|---|---|---|---|
| 0 | 0x000 | state word (section 3 below) | used; **its write flushes the queues** (D26-4) |
| 1 | 0x010 | stream word (section 2 below) | used |
| 2–65 | 0x020–0x41F | register image (as v1.25) | used |
| 66 | 0x420 | files word (the Tracer's layout, v1.3) | **ignored** |
| 67 | 0x430 | template bitmap (was word 0 at v1.25) | **ignored**; the tile's controller reads it from the record in big SRAM |

The replayer needs only words 0–65 (v1.25's part-0 image, now in record order), so **a controller sends the record from byte
0**, 66 words (or 68; writes to 0x420–0x43F are accepted and ignored). Like the rest of part 0, words 66–67 are written only while
the replayer is stopped. In converted deliveries word 66 is 0.

## 2. The stream word (word 1, offset 0x010) — proposed layout

Per stream: its size in **128-bit words** as sent, padding included (16 bits; at most about 33,800), its **values out** (17 bits;
at most 65,536) and its **compression flag** (moved from the state word's [114:112]). Grouped by kind, so the word counts the
firmware sends from sit on 16-bit boundaries and every field stays inside one 64-bit half:

| bits | field |
|---|---|
| [15:0] | code: words |
| [31:16] | read log: words |
| [47:32] | write log: words |
| [48] | code: LRU-compressed |
| [49] | read log: LRU-compressed |
| [50] | write log: LRU-compressed |
| [63:51] | spare (0) |
| [80:64] | code: values out |
| [97:81] | read log: values out |
| [114:98] | write log: values out |
| [127:115] | spare (0) |

102 bits used. Bounds: a log stream is at most 65,536 × 66 bits = 33,792 words (32,768 raw); code at most 17,408 words
compressed (16,384 raw).

**The sizes' meaning, exactly:**
- **The decoder stops at the value count**; the bits after a stream's last value, to the end of its last word, are padding and
  can be anything, compressed or not (D26-5). Bit lengths are gone (D26-2).
- **Words** is the number of 128-bit entries the stream occupies, so for a raw stream ⌈values / 4⌉ (code) or ⌈values / 2⌉ (logs);
  for a compressed one the fewest words holding its coded values. An entry leaves its queue when its last bit has been read or
  when the stream's last value ends inside it.
- **Underflow, at once** ([3] code, [4] read log, [5] write log; D26-6, D26-7): a pop past the stream's values out, or a decode
  that needs an entry beyond the stream's words. **The pass stops**, as for invalid expansion (O, ruling 4): `STATUS` [0] falls
  and run done is posted. This takes over v1.25's "a pop past a stream's length is [2]"; [2] no longer covers it.
- **Leftover, at done** ([6]; O, ruling 5): at the pass's 65,536th row, some stream has not had exactly its words popped from its
  queue and exactly its values decoded. Set as the pass ends (run done is posted as usual). One bit; the controller reads the queue
  occupancies to see which stream. **The write log is excluded from [5] and [6] when the state word's [115] is 0.**
  - So a stream with an extra whole padding word is an error ([6]); the producer pads only within the last word.
- The expansion queue has no sizes and no check (it ends by `seq_remaining`, V3-24).

## 3. The state word (word 0, offset 0x000)

pc [63:0], `instr_idx` [95:64], next offset [111:96] as at v1.25; **[114:112] reserved** (v1.25's compression flags, moved to
the stream word; written 0, ignored, not reused so a stale v1.25 word can't be misread); **[115] use the store log** (as v1.25's
"includes store log"); **[116] output `rd_prev`** (proposed; new, D26-8): when 0, every output row's `rd_prev` field (row word 3
[63:0], 4.19.12) is 0; register state is unaffected. [127:117] spare.

- The Tracer's records carry [115] = 1 and [116] = 1; **converted v1.25 deliveries must set [116] = 1** (and move [114:112] into
  the stream word, then clear them).
- **Only the flags change per pass** (D26-8): the controller resends word 0 with its flags and nothing else; the record's other
  words, the write log's sizes included, stay as produced.

## 4. Queues, the flush and the run (D26-3, D26-4; O, rulings 1–3)

- **Every queue is 4 KB, 256 entries, half 128** (code, read log, write log, expansion). Please report Stage B stall counts on
  both workloads at 2²⁹ (per-window averages of the read-log and write-log waits, as `WORKER11/REPORT.md` section 5 did); I'll put
  them in 4.19.2/4.19.8 when you have them (the 4 KB size itself is decided).
- **The flush:** a write to the state word (part 0 offset 0x000) empties the four input queues (contents and occupancy). Not the
  32-row output queue. Nothing else flushes.
- **`PULSE_RUN`** no longer empties anything: it sets every queue's credits to 2, resets the decompressors' tables, latches the
  state word's two flags and the stream word's sizes and compression flags, copies the image, clears the rows-run count and the
  pop counters.
- **The queues must be empty at `PULSE_RUN`** (invariant 16; meshsim flags). No preloading. Item 76 becomes "a push lands only
  after the state write".
- **Software writes the state word before every pass**, even re-running the same window (O, ruling 3): a pass that ends inside a
  controller-fed expansion can leave expansion words queued, and the flush clears them. About seven stores.
- **Software rule, moved:** before the state write (was: before a start), every queue push has landed. meshsim flags a push in
  flight at the state write, and a queue not empty at `PULSE_RUN` (a push between the two is preloading).

## 5. `ERRORS`

[0] reserved, [1] queue overrun, [2] invalid expansion (no longer includes the pop past a stream's end), **[3] code-stream
underflow, [4] read-log underflow, [5] write-log underflow, [6] done with input left over** (all P, sticky, trap the controller
like the others).
