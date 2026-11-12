# WORKER25 notes (P7b: Stage B and the replay acceptance kit)

Branch `sim-w25`, from main 67d42fb. Rows: E2, E3, E4, E5 (V135-INVENTORY), with H7's `stageb.profile`.

## The plan (agreed with the owner, 2026-11-11)

0. **E2 (TracerT1): no port.** D41 Q6: the 128-segment chain is TracerT1's equivalent. Recorded here and in the report.
1. **Fixtures from `ref`.** `oracle/ref/stageb.sh` runs v1.3's `StageB synthetic` in a scratch copy of `sim/ref` and
   writes the fixture (`tests/stageb/stageb.log`), regenerated rather than taken from `ref`'s committed
   `results/stageb.log` (which may be stale, as W20's F1 found for accounting). `--check` regenerates and compares. The
   guest-window steps (`DELIVERY=`: `stageb-<guest>`) get their references on the runner, where the delivery is.
2. **Stage B** (E3): `stageb` (`tests/stageb.{h,cpp}`, `stageb_main.cpp`), StageB.java line for line on W20's `Mach`:
   the loader (the window into tile 0's big SRAM per `replay_layout.h`), C&C 1 loading and starting `replay_ctl`, the
   run with the CL4 starvation watch (`--norow N`, ref's `-Dstageb.norow`), the rows against `rows.bin` (or Stage A),
   the streams consumed exactly, the end state, the replayer clean, the checker's violations, `rp_results`; the
   `synthetic` mode (16 program windows, the 2 controller-fed windows, the old-map CL4 case) from W15's
   `replay_window.h` and `replay_driver.h`; tile 0 alone (stage B, the 63 others parked and checked untouched) and the
   full chip (stage F); `--profile` (H7, ref's `-Dstageb.profile`). Log-equal to `ref`, plain and under the debug
   options (shuffle, both orders, step-all); the CL4 line's "in X s" is a timing figure and is masked.
3. **Stage A from the manifest** (E4): `replay_delivery` also takes `DELIVERY PROGRAM VARIANT [window ...]` and reads
   the manifest itself (`tools/delivery/json.h`), its lines unchanged; the window code factored so `replay_shard`
   reuses it.
4. **`replay_shard`** (ReplayShard.java): stages A, B, F; `--first/--count`, `--stride/--phase`, `--full-every`; every
   file a replay reads against the manifest's sha256 (CORRUPT); the tables against `data/tables/manifest.json`; CSV
   lines equal to v1.3's but for `wall_ms`.
5. **The kit** (E5), in `sim/tests/acceptance/` (owner; P8 may tidy the layout): `run_shard.sh`, `calibrate.sh`
   (`synthetic_delivery`, seed 11), `make_kit.sh`, `make_task.sh`, `make_acceptance_task.sh`. JVMs become processes,
   the `part-*.csv` files concatenated in the shell glob's order as v1.3's. `run_shard.sh`'s Python summary becomes a
   small C++ step (D40: no Python), writing `SUMMARY.txt` and `VERDICT.txt` as v1.3's. The kit carries source and
   builds with clang on the runner, as W10's and W17's kits do.
6. **Checks.** Locally: a small seed-11 synthetic delivery through v1.3 patched as D43 (as `oracle/synthetic_crosscheck.sh`)
   and through ours: `results.csv` (wall time masked) and `VERDICT.txt` byte-identical, `SUMMARY.txt`'s non-timing
   lines equal (owner); at `-j 1` and `-j 3`, stages ABF, `-x`, a planted corrupt file. On the runner: the full
   calibration (128 windows) on both kits, a real evm-zone-29 or cyclone-29 range on both, and the five guest windows.
7. **The suite runner**: `replay-build`, `stageb`, `stageb-<guest>` (with `DELIVERY=`) in W20's `sim/run_tests.sh`
   (not on main yet), through the integrator; `stageb` in `make check` if quick enough.

## The owner's answers (2026-11-11)

- The kit scripts: `sim/tests/acceptance/`.
- On the runner: the kit carries source and builds with clang.
- The item-113 delivery: look in WORKER13-NOTES (the runner's data paths) first; if it isn't there, list the runner's
  data directory with a small Quest task.
- `-q 512` (the 8 KB-log control): port it if it amounts to making the replayer's log-queue depth a configuration value
  (W15's replayer, now unowned: note the change); estimate first, and bring it to the owner if it is more.
- `SUMMARY.txt`: compare its non-timing lines only.

## Needs from W20 (asked through the integrator, 2026-11-11)

1. The replayer on tile 0 only, every other tile's peripheral 0 (ref's `Config.testPeripheral`, `periphType = 0`).
2. C&C 1's peripheral `expect = 1` (only tile 0 starts, 6.2).
3. Parked tiles (ref's `Chip.parked`) and `parkedUntouched`.
4. A row hook on the replayer (ref's `rowListener`), with the cycle.

Until they land: the fixtures, Stage A from the manifest, `replay_shard`'s stage A, the kit's scripts and summary step,
the `-q` estimate, the guest delivery's whereabouts.

## Findings

- **F1. `ref`'s committed `results/stageb.log` is stale** (2026-11-07): 18 windows, no CL4 case, and other cycle counts
  (program 0: 235 cycles, Stage A 5.720 cycles/row; today's v1.3: 236 and 3.400). The fixture is regenerated
  (`oracle/ref/stageb.sh`, 19 windows, deterministic: `--check` equal).
- **F2. The replayer's error texts aren't ref's.** StageB's detail lines (the CL4 case in `stageb.log`; any FAIL line in
  the kit's `results.csv`) print ref's Replayer lists verbatim: `head(r.violations)` (the first three, then "... (N in
  all)") and, in Stage A, `errorReport()` with every entry. The C++ replayer keeps the last 64 facts in a ring and
  formats them with its own texts ("@t queue 2 pushed past the credits taken for it: 1 entries", where ref writes "t0
  @t: queue 2 pushed past the credits taken for it: 1 entries, 0 credits (4.19.8; invariant 16)"; invalid expansions
  as numbers). Exactness needs ref's ~36 message texts in `fact_text` and ref's lists (the first entries per list, a
  capacity set at configuration, with the counts): about 200-250 lines in W15's replayer (unowned). Brought to the
  owner (beyond `-q`'s scope). The `meshsim: violations ...` part of the same lines is the checker's: the replayer's
  violations as chip violations (W20).
  **Owner (2026-11-11): go ahead**, in scope (it is the acceptance check); the replayer's tests, ReplayConf and T2
  unchanged. Boundary: W25 the replayer's own messages and lists; W20 the replayer's violations as chip violations.

## Environment

- The container had no clang or javac: installed `clang lld openjdk-21-jdk-headless` (clang 18.1.3, gcc 13.3.0,
  JDK 21.0.12). One CPU.

## Progress

- `oracle/ref/stageb.sh` and the fixture `tests/stageb/stageb.log` (F1).
- **`-q 512` (estimate: small, done).** The replayer's log-queue depth is a constructor argument (`Replayer(me,
  log_entries = 256)`, ref's `-Dreplayer.logq`), `PeriphSlotConfig::replayer_log_entries` passes it to a tile's
  replayer, `ReplayerStream::MAX_CAP` 256 → 512 (every stream's ring is a fixed array of MAX_CAP; 12 KB more per
  replayer). **A change in W15's replayer and slot (now unowned)**: `replayer.h`, `replayer.cpp`, `periph_slot.h`,
  `periph_slot.cpp`. ReplayerUnitTests' 8 KB half of queueSizes now ported (2 checks: 1,458 of ref's 1,473; 15 left).
  test_replayer, the slot tests (clang debug, gcc release), test_t2_harness, test_g1_replay, test_chain,
  test_tracer_slot (clang release): pass.
- **F2 done: ref's lists in the replayer** (a change in W15's unowned replayer: `replayer.h`, `replayer.cpp`).
  `Replayer(me, log_entries, list_capacity = 4)`: five lists (ref's violations, invalids, overruns, underflows,
  leftovers; badWrites is always empty, a bad write being FATAL), each entry ref's text, formatted when recorded by
  `snprintf` into a buffer allocated at construction (N_LISTS x capacity x 256 bytes; D24), the first `list_capacity`
  kept and every entry counted (`list_count`, `list_entry`). The fact codes, counts and ring (ReplayConf's
  observations, `fact_text`) are unchanged. Two recording details: V_CREDIT carries ref's credits (allowance / half)
  as a third argument, V_UNDEFINED the ALU's reason; the control transfer's underflow past the read log's values is a
  new cause, UF_TRANSFER_END (ref's text differs from the words case). `tests/replay_report.h`: `list_string`,
  `head` (StageB.head), `error_report` (Replayer.errorReport).
  **Checked against ref**: `oracle/ref/ReplayLists.java` + `replaylists.sh` replay every committed ReplayConf script
  on ref's Replayer and print every list whole (`tests/replayer/lists.txt`); `tests/test_replay_lists` does the same on
  the C++ replayer: equal (13 scripts: 240 credit violations, 340 overruns, 4 underflow kinds' texts, an invalid
  class 3, 4 leftovers across the three streams), and the kept-entries limit. **Not yet reached by a fixture**: the
  other violations (no flush, two sources, foreign source, offset_imm, log and nfv, undefined, end word, no nfv,
  misaligned), the ROM-end and invalid-word texts, the over-words underflows, leftover kinds 2 and 3: ported from
  ref's source line by line; a script provoking each would close it (to do if the budget allows).
  The replayer's tests, the slot tests, ReplayConf (gcc and clang, debug and release), test_t2_harness, test_g1_replay,
  test_chain, test_tracer_slot (clang release): pass, unchanged.
- **Shared file**: one line in the Makefile, `include tests/p7b.mk` (P7b's rules). Tell the integrator.
- **E4 done: Stage A from the manifest.** `tests/stage_a.h` (shared with `replay_shard` and Stage B): ref's
  ReplayDelivery.runWindow line for line (the line, the summary, every detail line of a failing window: the row
  count, the first mismatching row with ref's fields, describe and diag.bin lines and the two rows before it, the
  streams' consumption, the end state, `errorReport()` from F2's lists, the missing-template log), `tablesFor`, the
  manifest's delivered windows, `consumedExactly`. `replay_delivery DELIVERY VARIANT [PROGRAM ...] [--keep-going]`
  is ref's `java ReplayDelivery` (STOPPED line included); W15's list form is kept, its lines unchanged for passing
  windows (its failure details are now ref's). Checked locally against ref's ReplayDelivery (scratch build): a
  6-window seed-11 synthetic delivery (3,000 rows), all three variants, `cmp` equal; and the same with planted faults
  (a flipped row byte with a diag.bin, a changed end-state register, a truncated rows.bin, the manifest's rows +10
  and -10: an underflow and a row count) under --keep-going and stopping: equal, clang release and gcc debug. (The
  kit's crosscheck script will carry this check.)
- **`replay_shard`, stage A** (`tests/replay_shard_main.cpp`; ref's ReplayShard): the tables against their manifest
  (`tables` FAIL line), every file a replay reads against the window entry (CORRUPT: missing, no SHA-256, a hash,
  rows_sha256, a record not v1.26), the range, stride and phase, the variant (state-pick if present), `--full-every`,
  the CSV line (ref's but for wall_ms) and Stage A's stdout line. Stages B and F stop with a FATAL until Stage B lands.
  Checked locally against ref's ReplayShard (scratch build): the 6-window delivery (plain; state-lru over a strided
  range), the planted-fault delivery with the manifest's hashes refreshed (5 FAIL lines with ref's details), and a
  corrupted copy (a tables FAIL line, each CORRUPT kind, a hash given as a bare string): CSV with wall_ms masked, stdout
  and exit code all equal.
- **`shard_summary`** (`tests/acceptance/shard_summary.cpp`): v1.3's `run_shard.sh` inline Python in C++ (D40),
  writing SUMMARY.txt and VERDICT.txt and the exit code. Checked against v1.3's own Python, taken verbatim from
  `sim/ref/meshsim/acceptance/run_shard.sh` by `oracle/ref/shardsummary.sh` (python3 on the oracle side only), on six
  cases in `tests/acceptance/summary/` (OK lines of A, B, F with stall counts; a B/F cycle disagreement; FAIL,
  CORRUPT, tables and malformed lines; a blank line; a range; "all" from a manifest; -x adding F; an empty file; a
  passing shard): `test_shard_summary` (make check) equal on gcc and clang, debug and release; a planted difference in
  a fixture fails it.
