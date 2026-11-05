# WORKER25 notes: v1.26.1

The layouts and rulings, fixed before the code (owner's go-ahead, 2026-11-05). Decisions: `wip/planning/DECISIONS-v1.26.md`
section 3, items 13-17 (cited D26-*n*).

## The layouts

**`RUN_CONFIG`, window word 32 (offset 0xA80 in peripheral 1's window), R/W, both units** (D26-13): [23:0] rows to run,
[24] use the store log, [25] output `rd_prev`, [31:26] reserved. **Resets to 0, and is sticky:** it holds the last value written across passes, untouched by `PULSE_RUN`, the
state write and done (owner, 2026-11-05). Written while
stopped, used by the next `PULSE_RUN`; a write while running is UB (meshsim flags it). The Tracer ignores [24] and [25].

**Words 32-35, the same on both units** (D26-14): 32 `RUN_CONFIG` (R/W), 33 `ROWS_RUN` (R, [23:0], [31:24] 0, rows of this
run completed, cleared by `PULSE_RUN`), 34 `EXPANSION_OFFSET` (R), 35 `EXPANSION_OPCODE` (R). A store to 33-35 is a bus error.

**The state word** (D26-15): pc [63:0], `instr_idx` [95:64], next offset [111:96]; [116:112] reserved, **written 0 and
ignored, not checked** (owner, 2026-11-05); [127:117] spare.

**The stream word** (D26-16), stream *s* = 0 code, 1 read log, 2 write log:

| bits | field |
|---|---|
| [15:0], [31:16], [47:32] | words (128-bit entries as sent) |
| [64:48], [81:65], [98:82] | values out |
| [99], [100], [101] | LRU-compressed |
| [124:102] | advice values (0 to 2²²; the Tracer's; **replayers ignore it**) |
| [127:125] | spare |

So words at 16*s*, values at 48 + 17*s*, the flag at 99 + *s*. The values-out fields cross the 64-bit halves (code's [64:48]).

## The rulings on my questions (owner, 2026-11-05)

1. **A row count of 0** runs nothing: `STATUS` [0] rises at the end of the run store's cycle and falls the next, and run
   done is posted, as a DMA with count 0 (4.17.9). The done check [6] still applies.
2. **[6] is unconditional:** a pass of fewer rows than its record's streams describe ends with input left over. Short passes
   are for short traces, with records sized to the pass.
3. **`RUN_CONFIG` resets to 0, and is sticky** (owner, 2026-11-05): it holds the last value written across passes;
   `PULSE_RUN`, the state write and done leave it alone. It is 0 only after a reset.
4. **Deliveries are converted per task, in scratch** (v1.25 straight to v1.26.1), as WORKER20 did; nothing new in persistent data.
5. **No 8 KB controls:** 106/107 are compared with 098/099.
6. **Reserved [115]/[116]: written 0 and ignored, not checked.**

## For the software

The replay layout's header gains **`RL_H_RUN_CONFIG` (word 14)**, written to `RUN_CONFIG` once, before the
first pass (it is sticky); layout version 4.
The record's words 0-65 are loaded verbatim every pass.

## The acceptance's flags: like for like with 098/099 (owner's check, 2026-11-05)

098/099 ran v1.26 records converted by v1.26's `ConvertDelivery`, which **kept [115] use the store log (1, as the tracer
emits it) and set [116] output `rd_prev`**: both flags on. v1.26.1's `ConvertDelivery` requires [115] = 1 and clears both, and
every stage passes the same two flags through `RUN_CONFIG` instead:

| stage | where RUN_CONFIG comes from | value |
|---|---|---|
| A (`ReplayDelivery`, `ReplayDriver`) | the window: `w.rows` from the manifest, `storeLog` and `rdPrev` true | rows \| [24] \| [25] |
| B and F (`StageB.runWindow`, `replay_ctl.c`) | layout header word 14, `Replayer.runConfig(rows, true, true)`, written once before the pass | rows \| [24] \| [25] |

So 106/107 run **store log on, output `rd_prev` on, 65,536 rows**, as 098/099 did, and their rows, end states and stall
counts compare like for like.
