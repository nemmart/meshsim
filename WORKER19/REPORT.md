# WORKER19 report: the v1.26 spec

**Task:** bring `wip/spec/` from v1.25 to v1.26, exactly the replayer changes of `wip/planning/DECISIONS-v1.26.md` section 1,
spec only (WORKER20 does meshsim and the software in parallel). Done, with one item open on WORKER20's numbers (item 143).

## Deliverables

| file | commit | what |
|---|---|---|
| `WORKER19/NOTES.md` | `161942c` | the layouts and rulings for WORKER20, pushed first |
| `wip/spec/machine-spec.md` | `47d10da` | v1.26: all eight items of DECISIONS-v1.26 section 1, the sweep, 4.19.14's cost recomputed |
| `wip/spec/CHANGES-v1.26.md` | `d1a8c5f` | the delta from v1.25, one row per section touched, citing DECISIONS-v1.26 |
| `wip/spec/CHANGES-v1.25-to-v1.26.md` | `8ba21cf` | the RTL's guide for the step, by block; plus a one-line pointer at the top of `CHANGES-to-v1.25.md` |
| `WORKER19/REPORT.md` | this | |

## What the spec now says (in one place each)

- **The record** (4.19.2): 68 words, [0] state, [1] stream, [2–65] image, [66] files, [67] bitmap; loads from byte 0; part 0
  accepts and ignores 0x420–0x43F (4.19.10).
- **The stream word** (4.19.2): words [15:0], [31:16], [47:32]; compressed [48], [49], [50]; values out [80:64], [97:81],
  [114:98] (code, read log, write log). Grouped by kind so the word counts the firmware sends from sit on 16-bit boundaries and no
  field crosses the 64-bit halves.
- **The state word** (4.19.2): [114:112] reserved, [115] use the store log, [116] output `rd_prev`.
- **Queues** (4.19.8): all 4 KB; the state word's write flushes the four input queues; written before every pass; empty at
  `PULSE_RUN`; no preloading.
- **Checks** (4.19.11, 4.19.15): underflow at once ([3]–[5], stopping the pass), leftover at done ([6]); the write log excluded
  without the store log; the expansion queue unchecked.

## Choices I made within the rulings (for the owner to confirm or overturn)

1. **[116] for output `rd_prev`**, leaving [114:112] reserved rather than reusing [112], so a stale v1.25 state word can't be read
   with a wrong flag. Converted deliveries must set [116] = 1 (in NOTES for WORKER20).
2. **Words 66–67 are written only while stopped**, like the rest of part 0, so the ownership table keeps one row; the hardware
   just drops them.
3. **A whole extra padding word is an error** ([6]): a stream's word count must be the fewest entries holding its values. This
   follows from "every stream's words and values used exactly" (ruling 5) with the decoder releasing the entry its last value
   ends in.
4. **The underflow is defined as two cases:** a value requested past the value count, or an entry needed past the word count.
5. **`PULSE_RUN` also clears the six size counters**, so re-runs of a window check afresh.

## Findings

- **The 4 KB justification's figure.** DECISIONS-v1.3 section 12 says a half-queue refill (256 words) lasts "about 1,500 cycles
  of the densest window at 3 cycles a row". At the densest measured 0.83 log words per row that is about 300 rows, some 900
  cycles. 4.19.8 states the derived 900 (still well above a local refill of a few hundred cycles). The conclusion is unchanged;
  the measured stall counts (item 143) settle it.
- **Item texts in section 13** (52, 76, 103, 104, 108, 115, 123) are kept as written, by v1.25's convention; a new v1.26 status
  table gives their current state.
- No file in `wip/sw/`, `wip/tables/` or `wip/kit/` restates the record's old order or sizes in the strings I searched for (byte
  16, 67 words, includes store log, [114:112], 8 KB); WORKER20 may still find code that does.

## Open

- **Item 143:** Stage B's stall counts with the 4 KB queues, on both workloads at 2²⁹, from WORKER20's acceptance runs. When
  they arrive they go into 4.19.2 (the logs' cost), 4.19.8 (the size's justification) and item 143 closes. Asked in
  `NOTES.md` section 4.
