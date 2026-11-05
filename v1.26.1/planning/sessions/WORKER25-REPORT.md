# WORKER25 report: v1.26.1, the point release

Branch **`v1.26.1`**, from `main` at `bb8bb4d`. v1.26.1 is **v1.26 with exactly DECISIONS-v1.26 section 3's four
changes** (items 13–16) and item 17's statement, plus one owner's addition, `RUN_CONFIG` [28:26]. All of it is **specified,
modelled, carried through the firmware, the book and the kit, and accepted on every window** of both workloads, identical to
v1.26 cycle for cycle. Layouts and rulings: `WORKER25/NOTES.md`. Nothing is frozen: the release is proposed in section 5.

## 1. What changed

| item | spec | meshsim and software |
|---|---|---|
| **D26-13** `RUN_CONFIG`, word 32 | 4.17.4, 4.19.1, 4.19.9 (a new paragraph), 4.19.10, 4.19.11, 8.11, invariant 16 | `Replayer`: R/W word 32, taken at `PULSE_RUN`; 0 at reset, sticky; a write while running is UB; a pass runs [23:0] rows; a count of 0 is done the next cycle. The `rowsPerPass` test knob is gone: debug windows write their rows. `replay_ctl.c` (layout version 4) writes it once from header word 14; recipe 26's `replayer_config` |
| **D26-14** words 32–35 shared | 4.17.4, 4.19.9, 8.11 | words 33–35 moved; `ROWS_RUN` 24 bits; `mesh.h` (both copies), the draft header, `REGISTERS.md` |
| **D26-15** state word [115], [116] reserved | 4.19.2, 4.19.10, 4.19.11, 4.19.12, 4.19.15 | the flags from `RUN_CONFIG`; the record loaded verbatim; reserved bits written 0, ignored, not checked |
| **D26-16** the stream word, with advice | 4.19.2, 4.19.10 | `parseStreamWord`/`streamWord`: words [47:0], values out [98:48], flags [101:99], advice [124:102], ignored |
| **D26-17** what the record is | 4.19.2: the statement and the unit-by-word table | (no layout) |
| **owner, 2026-11-05:** `RUN_CONFIG` [28:26] | 4.17.4, 4.19.9, 8.11, the guide's W1 | the Tracer's per-stream compression enables: **held and read back, ignored, never flagged**; [31:29] reserved, read 0 |

**The owner's rulings on my plan** (2026-11-05, NOTES): a count of 0 runs nothing and posts run done, as a DMA's count of 0;
[6] is unconditional, so short passes need records sized to them; `RUN_CONFIG` resets to 0 and is sticky; deliveries are
converted per task in scratch; no 8 KB controls; reserved [115]/[116] ignored, not checked.

**Change notes:** `spec/CHANGES-v1.26.1.md` (v1.26 → v1.26.1, by section), and **the RTL guide from v1.25 straight to
v1.26.1, `spec/CHANGES-v1.25-to-v1.26.1.md`**: v1.26's guide renamed and extended, v1.26's forms in notes where they differ,
a new section 2.9 for the window's words. Pointers in `CHANGES-to-v1.25.md` and `CHANGES-v1.26.md` follow.

## 2. Tests

New in `ReplayerUnitTests` (all pass), each group named "v1.26.1: …":
- a 10-row pass of a 25-row record runs 10 rows and sets [6] alone; a pass sized to its record is clean;
- a row count of 0: `STATUS` [0] rises at the end of the run store's cycle and falls the next, run done is posted, no row;
  no error on an empty record, [6] on a full one;
- sticky: two passes, one store; 0 after the reset; reads back; a write while running is UB;
- **[28:26] every value 1–7** (and [31:29] set with 7): the same rows and cycles as 0, no error, nothing flagged; reads back
  with [28:26] and without [31:29]; raw streams stay raw with every enable set;
- the flags from `RUN_CONFIG`: a record with [115] and [116] set and `RUN_CONFIG`'s flags off gives the flags-off rows, the
  write log's sizes unchecked; and the reverse;
- the advice field at 2²² changes nothing and stays in part 0;
- `ROWS_RUN` reads 70,000 after a 70,000-row pass (v1.26's modulo count would read 4,464);
- `ConvertDelivery`'s record, field by field, and its refusal of a v1.25 record with [115] = 0.
`PeripheralSlotTest` reads and writes `RUN_CONFIG` through the slot. Local totals: ReplayerTest 2182/0, StageB synthetic
19/0, RecipeTest 2056/0, RegDocTest 269/0, PeripheralSlotTest 95/0, AgTest 240/0; the book 47 blocks, 0 differing.

## 3. Acceptance (Quest 105–107)

The v1.25 deliveries in persistent data (tasks 103/104) were converted to v1.26.1 records in each task's scratch
(`ConvertDelivery`: 32,768 records per workload, OK) and replayed with **`RUN_CONFIG` = 65,536 rows | store log | rd_prev**,
the flags 098/099 ran with (NOTES), in Stage A (the model), Stage B (`replay_ctl.c` on tile 0) and Stage F (the full chip,
every 64th window).

| | 105 smoke (windows 0–63) | 106 EVM Zone | 107 Cyclone |
|---|---|---|---|
| stage A | 64 + 64 | 8,192 of 8,192 | 8,192 of 8,192 |
| stage B | 64 + 64 | 8,192 of 8,192 | 8,192 of 8,192 |
| stage F | 4 + 4 | 128 of 128 | 128 of 128 |
| wall | 1.5 min | 29 min | 28 min |

**Against v1.26 (098/099), every window and stage is identical**: verdict, rows, cycles, and the detail column's stalls and
refills, 16,512 of 16,512 lines for each workload. That is expected: the one new store, `RUN_CONFIG`, comes before
`PULSE_RUN`, and nothing in a pass's timing changed. Stage B's log waits are v1.26's: 39.2 / 100.0 cycles per window (EVM
Zone), 43.5 / 84.5 (Cyclone).

## 4. Suites (Quest 108)

run_tests' quick suite, **36 steps**, and its full-mode extras, **11 steps**: every one passed, no log with a FAIL. The
full-size sort **p4/uniform-k128: 1,229,008 cycles**, unchanged.

**After 105–108:** the [28:26] addition changed the model's store of `RUN_CONFIG` (a mask) and comments in the headers. Every
acceptance pass writes those bits 0, the controller binary is byte-identical to the one 105–107 ran, and the affected suites
were rerun locally (above). I didn't rerun 105–108.

## 5. The release (proposed; not done)

**`v1.26.1/` = `v1.26/` plus exactly this branch's 26 changed files in `wip/`, plus `planning/DECISIONS-v1.26.md` as it
stands** (items 13–17), with v1.26's guide `CHANGES-v1.25-to-v1.26.md` removed (renamed). Everything `wip/` gained since v1.26
froze for v1.3 stays out, as v1.26 left it out: `gold/`, `tracer/`, `DECISIONS-v1.3.md`, `INTEGRATOR-HANDOVER.md`, WORKER17's
session files, and **WORKER24's Tracer figures** (`diagrams/`: v1.26's kept unchanged). `WORKER25/tasks/make_release.sh` does
exactly that; dry-run in a scratch copy, the result differs from `wip/` only in those exclusions. Then, per DECISIONS-v1.26
section 3, `v1.26/` is deleted. Whether this report goes into `v1.26.1/planning/sessions/`, as WORKER20's did for v1.26, is
yours to decide.

**The top README's row**, drafted:

> | `v1.26.1/` | frozen (2026-11-05) | **the RTL's target, from v1.25 directly** (guide: `spec/CHANGES-v1.25-to-v1.26.1.md`):
> v1.26 plus the Tracer's final layouts for what the two units share: `RUN_CONFIG` at window word 32 (the rows a pass runs,
> the replayer's two per-pass flags, the Tracer's compression enables; sticky), words 32–35 the same on both units
> (`ROWS_RUN` 24 bits), no flags in the state word (the record loaded verbatim), the stream word rearranged with the advice
> field. **Accepted on every window of EVM Zone and Cyclone at 2²⁹, identical to v1.26 cycle for cycle** (Quest 106, 107);
> the suites and the full-size sort at 1,229,008 cycles (108) |

## 6. For the integrator

- **DECISIONS-v1.26** should record the owner's [28:26] addition (2026-11-05) and my plan's rulings (NOTES), which only
  NOTES and the spec carry now; I didn't edit DECISIONS.
- **One choice of mine to confirm:** the replayer **holds and reads back** [28:26] (one register block for both units), and
  [31:29] read 0. The owner asked only that the replayer ignore [28:26].
- **WORKER24's figure 9.3** draws v1.26's state word flags and stream word: the new layouts are in NOTES.
- The Tracer's chapter (SP1) can take words 32–35, `RUN_CONFIG` and the stream word from 4.17.4 and 4.19.2 as they stand.
- `v1.26/README.md` (and `wip/README.md`) are v1.1's inherited folder README; the version summary lives in the top README,
  so I left them.
