# Machine spec v1.26 → v1.26.1: what changed

`spec/machine-spec.md` is now **v1.26.1**, a point release: **v1.26 with exactly the four changes of
`planning/DECISIONS-v1.26.md` section 3** (items 13–16, owner, 2026-11-04), made with the Tracer's design (DECISIONS-v1.3
sections 23–24) so that the Tracer's chapter (SP1) and the RTL start from the final layouts, plus item 17's statement of what
the record is, which changes no layout. Every item cites **D26-*n*** (DECISIONS-v1.26's item *n*) or **O** for the owner's
rulings on WORKER25's plan (2026-11-05, recorded in `WORKER25/NOTES.md`).

**Scope.** Section 4.19 (the replayer), 4.17's shared words, and the places that restate them (the front matter, 5, 8.11, 9,
10 and 13). Section, invariant and item numbers are v1.26's; no item is added. `spec/replayer-opcodes.md` and
`spec/schedule.md` are unchanged.

**For the RTL:** `CHANGES-v1.25-to-v1.26.1.md`, the change guide from v1.25 straight to v1.26.1, by hardware block (v1.26's
guide, renamed and extended).

## The changes in brief

1. **`RUN_CONFIG`, window word 32, R/W, on both units** (D26-13): [23:0] the rows to run, [24] use the store log, [25]
   output `rd_prev`, [31:26] reserved. The replayer runs exactly that many rows and raises done, so a pass can be shorter
   than a window (a short trace, its record sized to it); it was always 65,536. The Tracer runs the count, as `RUN_CYCLES`
   did, and ignores the flags. Written while stopped, used by the next `PULSE_RUN`; a write while running is UB.
   **0 at reset and sticky** (O): it holds the last value written across passes. **A count of 0 runs nothing** (O): `STATUS`
   [0] rises and falls and run done is posted, as for a DMA with a count of 0.
2. **Words 32–35 are the same on both units** (D26-14): 32 `RUN_CONFIG`, 33 `ROWS_RUN`, 34 `EXPANSION_OFFSET`, 35
   `EXPANSION_OPCODE`, so the replayer's three status words move up one. **`ROWS_RUN` widens to 24 bits**, a diagnostic.
3. **The state word's [115] and [116] are reserved** (D26-15): the record describes only where the program is, so every pass
   loads words 0–65 verbatim. The done check [6] and the write log's exemption follow `RUN_CONFIG` [24]. Reserved bits are
   written 0 and ignored, not checked (O). **[6] applies whatever the row count** (O).
4. **The stream word is rearranged, contiguous, with advice** (D26-16): words [15:0], [31:16], [47:32]; values out [64:48],
   [81:65], [98:82]; compressed [99], [100], [101] (code, read log, write log); **advice values [124:102]**, the Tracer's,
   which replayers ignore; [127:125] spare.
5. **What the record is** (D26-17): the superset of both units' state plus the window's descriptor, with a table of which
   unit uses each word, and the resuming test for state. No layout change.

## By section

| section | what changed | why |
|---|---|---|
| title, companion material | v1.26.1; `CHANGES-v1.26.1.md` and the guide `CHANGES-v1.25-to-v1.26.1.md` listed | D26 s3 |
| front matter | "v1.26.1 in brief" added before v1.26's; v1.26's notes the flags' move | D26-13 to D26-17 |
| 4.17.2 run | the replayer's run is a pass of the rows `RUN_CONFIG` names | D26-13 |
| 4.17.4 the standard window | **words 32–35 of the running peripherals**: the same four on the replayer and the Tracer; `RUN_CONFIG`'s rules (while stopped, sticky, 0 at reset) | D26-13, D26-14; O |
| 4.19.1 overview | a pass runs `RUN_CONFIG` [23:0] rows, typically 65,536; the interfaces list `RUN_CONFIG` and words 33–35 | D26-13, D26-14 |
| 4.19.2 the run log and the checkpoint | **the state word:** [116:115] reserved (written 0, ignored, not checked); it describes only where the program is. **The stream word:** the new contiguous table, with the advice field and which fields the replayer uses; v1.26's layout noted. **The record's table:** word 1's advice field ignored. **"What the record is"**, item 17's statement and table. **Use the store log** and **output `rd_prev`** are `RUN_CONFIG` [24] and [25], chosen per pass by the controller. **"Nothing in the record changes per pass"** replaces "only the flags change per pass"; the working state's paragraph: the checks read the sizes from part 0 and the flags from `RUN_CONFIG` | D26-15, D26-16, D26-17; O |
| 4.19.9 the events, the words and the loop | the window table: words 32–35; a new **`RUN_CONFIG`** paragraph (the row count, short passes and [6], the count of 0, the flags, written while stopped, sticky, 0 at reset, the Tracer's use); `ROWS_RUN` in 24 bits, rows left = the count − rows run; the state word has no flags; the loop writes `RUN_CONFIG` once and loads words 0–65 verbatim | D26-13, D26-14, D26-15; O |
| 4.19.10 the region | the map's state-word row ([116:112] reserved) and stream-word row (the new bits); the load from big SRAM is verbatim; `RUN_CONFIG` follows the ownership rule (a store while running UB) | D26-13, D26-15, D26-16 |
| 4.19.11 run, done and the errors | `PULSE_RUN` takes the row count and flags from `RUN_CONFIG`; with a count of 0 there is no row; done after `RUN_CONFIG` [23:0] rows, even inside an expansion; [5] and [6] cite `RUN_CONFIG` [24]; [6] whatever the row count, 0 included | D26-13, D26-15; O |
| 4.19.12 the output row | `rd_prev` is 0 with `RUN_CONFIG` [25] = 0 | D26-15 |
| 4.19.15 stream decompression | the write log's exemption cites `RUN_CONFIG` [24] | D26-15 |
| 5 timing summary | a pass is typically 65,536 rows (`RUN_CONFIG`) | D26-13 |
| 8.11 the replayer's window | words 32 `RUN_CONFIG` (R/W, reset 0), 33 `ROWS_RUN` (24 bits), 34 `EXPANSION_OFFSET`, 35 `EXPANSION_OPCODE`; the Tracer shares them; item 53's modulo count superseded | D26-13, D26-14 |
| 9 invariant 16 | no `RUN_CONFIG` write while a pass runs (UB, meshsim) | D26-13 |
| 10 parameter summary | the replayer's row: a pass of `RUN_CONFIG` [23:0] rows; words 32–35 shared; the advice field ignored | D26-13, D26-14, D26-16 |
| 13 open items | a **v1.26.1 status table**: 53 superseded, 108 and 123 changed; no new item (the count of 0, proposed in D26-13, is ruled: O) | D26 s3; O |

## Not changed

- **Every timing, queue, credit and check rule** of v1.26 other than where the flags live: the state write still resets the
  pass, `PULSE_RUN` only runs, the queues are 4 KB, the sizes and checks are v1.26's.
- **The record's 68 words and their offsets**, the register image, the files word and the template bitmap.
- **The output row's layout**, the decode table, the ROM and the LRU coding.
- **The Tracer** (v1.3): only its words 32–35 and the advice field are fixed here; its chapter is SP1's.

## Verification

meshsim, the controller (`replay_ctl.c`, layout version 4), recipe 26, the kit and the synthetic deliveries follow v1.26.1
(WORKER25, branch `v1.26.1`). The acceptance is v1.26's rerun: every window of EVM Zone and Cyclone at 2²⁹, converted from the
v1.25 deliveries in each task's scratch, in stages A and B, and every 64th in F (Quest 106, 107), with the suites and the
full-size sort (108). Results: `WORKER25/REPORT.md`.
