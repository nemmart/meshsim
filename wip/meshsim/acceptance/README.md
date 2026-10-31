# The v1.25 acceptance kit (meshsim WORKER11)

DECISIONS-v1.25 s2: **every window of EVM Zone and Cyclone at 2²⁹ rows, replayed by meshsim's replayer, matches jolt-cpp
`821e1bd`'s trace row for row**, on the Quest runner. s7 (revised 2026-11-01): generating any window means tracing the
program from its start, so **one held WORKER9 writer task per program writes the whole delivery on the runner box (about
17 minutes, ~31 GB) and keeps it there**; the acceptance tasks read their shards from it and never write next to it.

## What it runs

`run_shard.sh -d DELIVERY -p PROGRAM -f FIRST -n COUNT [-j JOBS] [-s AB] [-m MANIFEST_SHA256] OUTDIR`

The manifest's SHA-256 (as the writer task reported it) is checked first. `JOBS` JVMs (`ReplayShard`) then take every
JOBS-th window of FIRST..FIRST+COUNT-1. Per window and stage:
- **every file the replay reads** (the state record, the three streams its flags name, `rows.bin`, `end.bin`) against
  the manifest's SHA-256 (a mismatch is reported as CORRUPT, not as a replay failure);
- **Stage A**: the replayer model fed by the ideal controller (`ReplayDelivery`);
- **Stage B**: the replay controller (`sw/replay/replay_ctl.c`, prebuilt) on a meshsim tile feeding its replayer (`StageB`);
- every row compared byte for byte with the reference, the three streams consumed exactly, the end state (`end.bin`)
  equal, the replayer and meshsim free of conditions and violations.

Output (OUTDIR only): `results.csv` (program, window, first row, stage:variant, verdict, rows, cycles, wall ms, detail),
`SUMMARY.txt` (rows per JVM-second per stage, windows per hour), `VERDICT.txt` (PASS only if every window of the range is
in the manifest and matched in every stage), `jvm-*.log`.

`calibrate.sh OUTDIR [SCRATCH]`: a 64-window synthetic delivery (`SyntheticDelivery`, below) in scratch, replayed at
1, 8 and 16 JVMs, for the runner-hours estimate.

## What it needs from the delivery

`manifest.json` (`programs[].window_entries[]` with `index`, `first_row`, `rows`, `files` (name to `{"sha256": ...}` or
the hash), `rows_sha256`), `PROGRAM/window_NNNNN.*` (`state.bin` and/or `state-pick.bin` with the streams they name,
`rows.bin`, `end.bin`), and `tables/` (or none: the kit's copy of `wip/tables/replayer` is used; either is checked
against `wip/tables/manifest.json`). `state-pick` is replayed when present, else `state`. From WORKER9: the delivery's
path on the box and the manifest's SHA-256. Section 8 of `REPLAY-INPUT-REQUIREMENTS.md`.

Until WORKER9's writer exists, the kit is tested with **`SyntheticDelivery`** (in the jar): random RV64IMAC programs
through the universal tables, every instruction cross-checked against an RV64 interpreter; its reference rows are the
model's own semantics, so it tests the kit and measures speed, not jolt-cpp agreement.

## Building and submitting

From `wip/meshsim`, after `sw/build.sh` and `sw/replay/build.sh`:

    ./acceptance/make_kit.sh /tmp/kit.tgz
    ./acceptance/make_task.sh /tmp/kit.tgz 0NN-w11-calibrate './calibrate.sh $RES/calib' > quest/tasks/0NN-w11-calibrate.sh

A shard's task: `./run_shard.sh -d /path/on/box -p cyclone -f 0 -n 1024 -m <manifest sha> $RES/cyclone-0`. The shard plan
and the hours are in `WORKER11/REPORT.md`.
