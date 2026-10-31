# The v1.25 acceptance kit (meshsim WORKER11)

DECISIONS-v1.25 s2: **every window of EVM Zone and Cyclone at 2²⁹ rows, replayed by meshsim's replayer, matches jolt-cpp
`821e1bd`'s trace row for row**, on the Quest runner. s7: deliveries are not stored (about 31 GB a program): each runner
task has the delivery writer (WORKER9 part 3) generate its shard of windows into scratch space, a batch at a time,
replays each batch at once and deletes it, keeping only the results.

## What it runs

`run_shard.sh -p PROGRAM -f FIRST -n COUNT -w 'WRITER' [-b BATCH] [-j JOBS] [-s AB] OUTDIR`

For each batch of windows (default 128, about 0.7 GB of scratch): the writer command writes them
(`{out} {first} {count} {program}` substituted) in `REPLAY-INPUT-REQUIREMENTS.md`'s format; `JOBS` JVMs then replay them
(`ReplayShard`, every JOBS-th window each) and the batch is deleted. Per window and stage:
- the reference rows' SHA-256 against the manifest (a corrupt delivery is reported as such);
- **Stage A**: the replayer model fed by the ideal controller (`ReplayDelivery`);
- **Stage B**: the replay controller (`sw/replay/replay_ctl.c`, prebuilt) on a meshsim tile feeding its replayer (`StageB`);
- every row compared byte for byte with the reference, the three streams consumed exactly, the end state (`end.bin`)
  equal, the replayer and meshsim free of conditions and violations.

Output: `results.csv` (program, window, first row, stage:variant, verdict, rows, cycles, wall ms, detail), `SUMMARY.txt`
(rows per JVM-second per stage), `VERDICT.txt` (PASS only if every window of the shard was written and matched in every
stage), the writer's and replayers' logs per batch.

`calibrate.sh OUTDIR`: synthetic windows (`SyntheticDelivery`, below) at 1, 8 and 16 JVMs, for the runner-hours estimate.

## What it needs from the writer

A command that writes windows FIRST..FIRST+COUNT-1 of a program, and nothing else, into a directory: `manifest.json`
(`programs[].window_entries[]` with `index`, `first_row`, `rows`, `files`, `rows_sha256`), `PROGRAM/window_NNNNN.*`
(`state.bin` and/or `state-pick.bin` with the streams they name, `rows.bin`, `end.bin`), and `tables/` (or none: the
kit's copy of `wip/tables/replayer` is used). `state-pick` is replayed when present, else `state`. Section 8 of
`REPLAY-INPUT-REQUIREMENTS.md`.

Until WORKER9's writer exists, the kit is tested with **`SyntheticDelivery`** (in the jar): random RV64IMAC programs
through the universal tables, every instruction cross-checked against an RV64 interpreter; its reference rows are the
model's own semantics, so it tests the kit and measures speed, not jolt-cpp agreement.

## Building and submitting

From `wip/meshsim`, after `sw/build.sh` and `sw/replay/build.sh`:

    ./acceptance/make_kit.sh /tmp/kit.tgz
    ./acceptance/make_task.sh /tmp/kit.tgz 0NN-w11-calibrate './calibrate.sh $RES/calib' > quest/tasks/0NN-w11-calibrate.sh

A real shard's task adds a setup file that unpacks the writer (hand-copied to the box and SHA-256 checked, as task
069) and sets `$WRITER`; see `REPORT.md` for the shard plan and the hours.
