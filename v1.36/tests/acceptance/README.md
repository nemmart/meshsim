# The acceptance kit (v1.35)

v1.3's acceptance kit (`meshsim/acceptance/`, WORKER11; v1.26 WORKER20; v1.26.1 WORKER25) on the C++ meshsim
(sim/ WORKER25, P7b; inventory E5): **every window of a delivery, replayed by the replayer model (Stage A) and by the
replay controller on a meshsim tile (Stage B), matches the delivery's rows row for row**, on the Quest runner. Its
outputs are v1.3's: `results.csv` byte-identical but for the wall-time column, `VERDICT.txt` byte-identical,
`SUMMARY.txt`'s non-timing lines equal.

## What it runs

`run_shard.sh -d DELIVERY -p PROGRAM -f FIRST -n COUNT [-j JOBS] [-s AB] [-m MANIFEST_SHA256] OUTDIR` (options as
v1.3's; the script's header lists them). The manifest's SHA-256 is checked first. JOBS processes (`replay_shard`, v1.3's
JVMs running ReplayShard) then take every JOBS-th window of FIRST..FIRST+COUNT-1. Per window and stage:
- every file the replay reads (the state record, the three streams its stream word names, `rows.bin`, `end.bin`)
  against the manifest's SHA-256 (a mismatch is CORRUPT, not a replay failure);
- **Stage A**: the replayer model fed by the ideal controller (`tests/stage_a.h`, v1.3's ReplayDelivery);
- **Stage B**: the replay controller (`fw/replay/replay_ctl.c`, prebuilt) on a meshsim tile feeding its replayer
  (v1.3's StageB): B tile 0 alone, F the full chip;
- every row compared byte for byte with the reference, the streams consumed exactly, the end state equal, the
  replayer and meshsim free of conditions and violations.

Output (OUTDIR only): `results.csv` (program, window, first row, stage:variant, verdict, rows, cycles, wall ms, detail),
`SUMMARY.txt` (rows per process-second per stage, windows per hour; v1.3's wording: "JVM-s"), `VERDICT.txt` (PASS only
if every window of the range is in the manifest and matched in every stage), `jvm-*.log` (one per process; v1.3's
names). The summary step is `shard_summary` (v1.3's inline Python, in C++).

`calibrate.sh OUTDIR [SCRATCH]`: a 128-window synthetic delivery (`synthetic_delivery`, seed 11; D42, D43) in scratch,
replayed at 1, 4, 8 and 16 processes, for the runner-hours estimate.

## What it needs from the delivery

As v1.3's: a v1.26 delivery (`manifest.json` or `PROGRAM.manifest.json` with `programs[].window_entries[]`: `index`,
`first_row`, `rows`, `files`, `rows_sha256`; `PROGRAM/window_NNNNN.*`; `tables/` optional, else the kit's
`data/tables/replayer`, checked against `data/tables/manifest.json`). A v1.25 delivery is converted first
(`convert_delivery`).

## Building and submitting

From the version's root folder, committed first (the kit is HEAD's):

    tests/acceptance/make_kit.sh /tmp/kit.tgz
    tests/acceptance/make_task.sh /tmp/kit.tgz NNN-w25-calibrate './calibrate.sh $RES/calib' > quest/tasks/NNN-w25-calibrate.sh

The task unpacks the kit, checks its HASHES, builds it with clang (`./build.sh`: the suite build, release flags with the checker on, so the machine's violations reach the verdict as in v1.3), records the machine
(`tools/machine.sh`) and runs the command. `make_acceptance_task.sh` writes one program's whole acceptance.

## Differences from v1.3's kit

- No jar, no Java, no Python: the kit carries sim/'s sources and builds on the runner; the summary step is C++.
- `calibrate.sh`'s delivery is D43's reproducible SyntheticDelivery (v1.3's own differs run to run from the first
  inline's advice on); its results compare with v1.3 patched as D43 says (the oracle's acceptance cross-check, outside the frozen copy).
- As in v1.3, `calibrate.sh`'s exit code is `tail`'s (its pipeline has no pipefail): read each run's VERDICT.txt.

## v1.36: Stage A's cycles are the RTL's

The replayer model is the RTL's `replayer_unit` (`src/units/replayer.h`), and Stage A's ideal controller
(`tests/replay_driver.h`) is the RTL acceptance's, load for load: a consuming load of `EVENTS` in each cycle its level
is up, 128 entries released a credit, a new expansion answered by loads of `EXPANSION_OPCODE` and `EXPANSION_OFFSET`
in the next two cycles, one packet a cycle on the write port from the cycle after its release, an expansion word
first, else a stream in turn, the row port always ready. So Stage A checks, besides the rows, **the pass's cycles and
the cycle every row leaves the row port against the RTL's own run of the same window**
(`tests/acceptance/rtl/<program>.txt`; `tests/replayer/README.md` says how it is made). A window whose cycles or whose
rows' leaving differ fails, with `cycles: model …, the RTL …` in its detail; `REPLAY_RTL_LEFT=DIR` (the RTL's dumps)
adds the first row that left in another cycle.

- **Stage A's `cycles` column** is now the RTL acceptance's measure: from `START`'s cycle to the last row leaving the
  row port (v1.35: from `PULSE_RUN` to the pass's end, which is 1 less).
- `replay_shard --rtl FILE` names the reference, `--rtl none` runs without one (a delivery the RTL has not run);
  by default it is `tests/acceptance/rtl/<program>.txt` where that file exists. `replay_delivery` reads
  `$REPLAY_RTL/<program>.txt` (default `tests/acceptance/rtl`) and prints the run's figures as the RTL's acceptance
  prints its own: the cycles, the rows' schedules, and the cycles past them by the kind of row.
- **References held:** `evm-zone-23` (128 windows), `cyclone-22` (64) and `synthetic` (`calibrate.sh`'s 128), each
  in the three record variants. Every one of the 960 passes equals the RTL's.
- **`-q 512` is gone.** Every input queue is `REPLAY_QUEUE_ENTRIES` = 256 (19); the 8 KB-log control was v1.35's
  model's, and the replayer refuses another size.
- **Stages B and F** are v1.35's firmware on v1.35's tile with this replayer in its slot: they pass, rows and end
  state, and their cycles are neither v1.35's nor the RTL chip's until the platform and firmware stages reach them
  (`PENDING-v1.36.md`).
