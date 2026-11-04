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
5. ~~**`PULSE_RUN` also clears the six size counters**~~: superseded by DECISIONS-v1.26 item 10 (the state write clears them).

## Revision, 2026-11-04 (DECISIONS-v1.26 items 9–12)

Choices 1–4 confirmed (item 9). **The state write resets the pass** (flush, credits, size counters, decompressors) and
**`PULSE_RUN` only runs** (item 10); **preloading is allowed** (item 11); **part 0 is the working state**, with no copy at a run's
start and no latches, so the record's words 0–65 are reloaded before every pass (item 12). Applied to the spec (`cbdcd90`:
front matter, 4.19.1, 4.19.2, 4.19.7–4.19.11, 4.19.13–4.19.15, sections 5, 9, 10, 11 and 13's status table, items 41, 52, 76 and
108), `CHANGES-v1.26.md`, the guide and `NOTES.md` (a dated note to WORKER20 at its top). 4.19.14 is now about 32 KB per tile:
the separate 1 KB image is gone.

## Findings

- **The 4 KB justification's figure.** DECISIONS-v1.3 section 12 says a half-queue refill (256 words) lasts "about 1,500 cycles
  of the densest window at 3 cycles a row". At the densest measured 0.83 log words per row that is about 300 rows, some 900
  cycles. 4.19.8 states the derived 900 (still well above a local refill of a few hundred cycles). The conclusion is unchanged;
  the measured stall counts (item 143) settle it.
- **Item texts in section 13** (52, 76, 103, 104, 108, 115, 123) are kept as written, by v1.25's convention; a new v1.26 status
  table gives their current state.
- No file in `wip/sw/`, `wip/tables/` or `wip/kit/` restates the record's old order or sizes in the strings I searched for (byte
  16, 67 words, includes store log, [114:112], 8 KB); WORKER20 may still find code that does.

## Item 143, closed (2026-11-04)

WORKER20's Stage B counts on every window of both workloads at 2²⁹ (`WORKER20/NOTES.md`; Quest 098–101), cycles per window:

| | EVM Zone, 4 KB | 8 KB control | Cyclone, 4 KB | 8 KB control |
|---|---|---|---|---|
| read-log wait | 39.2 | 39.2 | 43.5 | 43.5 |
| write-log wait | 100.0 | 176.2 | 84.5 | 153.6 |
| share of a pass | 0.066 % | 0.102 % | 0.063 % | 0.097 % |

Stated in 4.19.2 (beside v1.25's figures, with the control named as the comparison for the queue size) and 4.19.8 (the table
and the start-up explanation: the waits are all at a pass's start, and after it a 128-entry refill always lands in time);
item 143 closed; `CHANGES-v1.26.md` and the guide updated. These are the full runs, not the smoke run. The finding above on
DECISIONS-v1.3's 1,500-cycle figure is moot: after the start, no window waits.

## WORKER20's choices where the spec left room (`WORKER20/NOTES.md` section 2)

| WORKER20's choice | the spec | done |
|---|---|---|
| the stream word's sizes latched at the state word's write and again at the stream word's | agrees: the checks read part 0, which can't change while running, so latching as the words land is the same; the guide (S3) now says an implementation may do this | guide |
| a pass started with no state write since the previous pass is flagged | agrees with invariant 16 (reload before every pass); 4.19.11 now says meshsim flags it. **A gap, not a disagreement:** meshsim checks the word-0 write, not that words 1–65 were reloaded; a pass from a stale image would still be caught by the Stage B comparison | spec 4.19.11 |
| a push in flight at the state write can't be seen inside the replayer | **the spec disagreed:** 4.19.13 said meshsim flags a state write with a push in flight. Corrected: the rule is software's (invariant 16), kept by waiting on the refill counters; the replayer can't see it and meshsim doesn't check it. For pushes from another unit over the mesh the rule still matters, and nothing checks it | spec 4.19.13, my NOTES |
| an underflow stops the pass; [6] evaluated as the 65,536th row finishes | agrees (4.19.11, 4.19.15) | — |
| the stream word's layout and [116] | agree (4.19.2) | — |

## Open

Nothing.
