# WORKER25 report: P7b, Stage B and the replay acceptance kit

P7b (V135-INVENTORY rows E2, E3, E4, E5; with C20, the runner's `replayer` step, taken over from the dropped P3):
**Stage B and the acceptance kit run without Java or Python, and their outputs equal v1.3's**: `stageb synthetic`
byte for byte v1.3's log, the kit's results equal to v1.3's kit on the calibration and on real windows of both
workloads (Quest 180), and the `replayer` and `replay-build` steps equal to v1.3's logs. Branch `sim-w25`; details and
every check's numbers in `NOTES.md`.

## The rows

| row | what | v1.35 | checked |
|---|---|---|---|
| E2 | TracerT1 | no port: the 128-segment chain is its equivalent (D41 Q6) | (W17's Quest 174) |
| E3 | StageB | `stageb` (`tests/stageb.{h,cpp}`, `stageb_main.cpp`): synthetic windows and a delivery's windows, tile 0 alone or the full chip, `--norow`, `--profile`, the debug options | `stageb synthetic` byte-identical to v1.3's log (`tests/stageb/stageb.log`; the CL4 line's seconds masked), the CL4 case's 256-violation line at v1.3's cycles; plain, shuffled, step-all and instance-major all the same; `test_stageb` in make check; the runner's step `stageb` |
| E4 | ReplayDelivery (Stage A) | `tests/stage_a.h` (ReplayDelivery.runWindow line for line, every detail line of a failing window); `replay_delivery DELIVERY VARIANT [PROGRAM ...]` reads the manifest | `cmp`-equal to v1.3's on a synthetic delivery in all three variants, with planted faults |
| E5 | the acceptance kit | `tests/acceptance/`: `replay_shard` (ReplayShard), `shard_summary` (run_shard.sh's Python in C++), run_shard.sh, calibrate.sh, build.sh, make_kit.sh, make_task.sh, make_acceptance_task.sh, README | `oracle/acceptance_crosscheck.sh`: v1.35's kit against v1.3's, 15 local cases and Quest 180's runner cases, all equal |
| C20 | the step `replayer` (ReplayerTest) | `suite_replayer`: W15's ported parts and the 15 ConvertDelivery checks on the C++ converter | byte-identical to v1.3's log (2,187 checks); `test_suite_replayer` |
| — | the step `replay-build` | `tests/stageb/replay_build.sh`: rebuild and compare with the toolchain, the MANIFESTs without | v1.3's line from its own build |

## How the kit is checked

`oracle/acceptance_crosscheck.sh` builds v1.3's kit in scratch (ref's classes in a jar, SyntheticDelivery patched as
D43, v1.3's run_shard.sh and calibrate.sh unchanged) and v1.35's (made from HEAD, built with the checker on), runs both
run_shard.sh on the same delivery, and compares **results.csv with the wall-time column masked, VERDICT.txt,
SUMMARY.txt's non-timing lines** (the stall lines included), the exit codes and every per-process log. Locally (9
synthetic windows of 4,000 rows, seed 11), all 15 cases equal: stage A at 1 and 3 processes, a range, `-n all`, a
range past the manifest's end, planted faults (FAIL details), corruptions (each CORRUPT kind, a tables FAIL); AB at 1
and 3 processes, ABF, AB with `-x 3`, B with `-n all`, AB on the planted faults and the corruptions; AB with `-x 4` and
`-q 512`. `shard_summary` alone is also checked against v1.3's own Python on six crafted cases (`test_shard_summary`).

**On the runner, Quest 180: ALL PASS** (runner `godspeed`, 16 CPUs, 62 GB; JDK 21.0.13; clang 18.1.3). Deliveries
converted to v1.26.1 in scratch by v1.35's convert_delivery (the selected manifests' sha256 equal Quest 164's).

| case | windows | stages | v1.35 against v1.3 | verdict (both kits) |
|---|---|---|---|---|
| calibration, 1 process | 0-7 of 128 synthetic (65,536 rows, seed 11) | AB | equal | PASS |
| calibration, 4 processes | 8-39 | AB | equal | PASS |
| calibration, 8 processes | 40-71 | ABF | equal | PASS |
| calibration, 16 processes | 0-127 | AB | equal | PASS |
| W9's selected windows, evm-zone-29 | 12 | AB, F on every window | equal | PASS |
| W9's selected windows, cyclone-29 | 13 | AB, F on every window | equal | PASS |
| evm-zone-29 windows 0-127 | 128 (8,388,608 rows) | AB, F every 32nd | equal | PASS |

SUMMARY's rates, for the record only (not a timing claim): evm-zone-29 0-127 at 16 processes, Stage B 0.14 s per
window (v1.3: 1.37), Stage A 0.02 (0.16). Quest 180's kit was the release build.

**Quest 182: ALL PASS**, the same cases with the kit as it now ships (built with the checker on, so the machine's
violations reach the verdict as in v1.3), plus W9's selected evm-zone-29 windows with `-q 512` (the 8 KB-log
control; AB, F every 4th): every case equal to v1.3's kit, every window PASS in both. With the checker on, Stage B
costs about what v1.3's did (evm-zone-29 0-127: 1.36 s per window, v1.3 1.37; Stage A 0.02): the checker's per-cycle
recording dominates Stage B's tile. A full program's acceptance at 16 processes is then about 8,192 x 1.36 / 16, some
12 minutes of Stage B per program on this runner: affordable; the release kit (0.14 s) remains a build option if
speed is wanted without the violation check.

## Findings

- **F1.** `ref`'s committed `results/stageb.log` is stale (before the CL4 case; other cycle counts): the fixture is
  regenerated from v1.3 (`oracle/ref/stageb.sh`).
- **F2.** The replayer's error texts weren't v1.3's (a ring of the last 64 facts, its own wording). The replayer now
  keeps v1.3's lists (v1.3's texts, the first entries kept and every entry counted, nothing allocated in the run);
  `test_replay_lists` equals v1.3's lists entry for entry on every ReplayConf script. The chip side (the replayer's
  violations as chip facts) is W20's, on main. Texts no fixture reaches yet: most violation kinds (no flush, two
  sources, foreign source, offset_imm, log and nfv, undefined, end word, no nfv, misaligned), the ROM-end and
  invalid-word texts, the over-words underflows, leftover kinds 2 and 3 (ported line by line from v1.3's source).
- **F3.** `ref`'s committed `results/replay-build.log` is stale; the reference is v1.3's own build today.
- **F4.** `ref`'s committed `results/replayer.log` is stale (242 vector rows, 1,188 checks; today 252, 2,187).
- **F5.** ReplayerTest's coverage check runs once per uncovered opcode (a lost append makes the check the loop's body),
  so a passing run counts none; W15's port counted one per opcode. It now counts as v1.3 does and still fails on any
  uncovered opcode.
- **F6.** A double free with a row hook (`Chip::on_row`): fixed by W20 (the slot frees only its own buffer).
- **F7.** Withdrawn: the planted-fault case's "ERRORS ... peripheral 1 (invariant 15)" line was missing only because
  the kit was the release build (no checker). The kit now builds with the checker on, and the line is ours too.
- **F8.** A peripheral's violation was reported at the replayer's cycle, v1.3's at the next: fixed by W20 (t + 1); the
  CL4 line now equals v1.3's.
- **F9 → D46.** Stage B prints the CL4 window's FAIL by design (that window must fail) and the runner's summary greps
  every log for FAIL; v1.3's own run_tests.sh fails the same way today (it never showed: ref's committed log predates
  CL4). **A deliberate difference in the runner** (D46, integrator): the FAIL grep skips lines containing "(CL4: must
  fail)".
- Java's `%.3f` rounds the shortest decimal half-up (12,946 / 4,000 = 3.2365 prints 3.237; printf 3.236): found by the
  crosscheck, reproduced.
- Quest 180 ran `tools/machine.sh` with `sh` (it is a bash script), so its machine.txt holds only the error; the run
  log names the host, CPUs, memory and versions. Quest 182 then ran it without its `record` argument (usage only).
  Both fixed for later tasks (`bash tools/machine.sh record`); both tasks' run logs name the machine.

## Deliberate differences and rulings

- **D46** (above): the runner's FAIL grep skips Stage B's must-fail window.
- **The item-113 guest steps stay unrun, as in v1.3** (owner, 2026-11-11): v1.3 ran `stageb-<guest>` only with
  `DELIVERY=`; there is no such delivery on the runner (Quest 177 listed the data directories), and tracing new guests
  with jolt-cpp is beyond a freeze. The runner keeps the steps, running when `DELIVERY=` is given, as v1.3's do.
- The kit: processes in place of JVMs (v1.3's file names and SUMMARY's wording kept, so the outputs compare); no jar,
  Java or Python (it carries sim/'s sources and builds on the runner, with the checker on); calibrate.sh's delivery is
  D43's reproducible SyntheticDelivery.

## Changes in others' files (each recorded in NOTES)

- The replayer (W15's, unowned): the log-queue depth a constructor argument (`-q 512`), the stream cap 512, v1.3's
  error lists (F2), a new underflow cause for v1.3's distinct text; the slot passes the depth through.
- `test_replayer.cpp`: the 8 KB control's half of queueSizes ported; `replay_driver.h`: v1.3's missing-template log;
  `test_replayer_alu.cpp`: counts its passing checks, the coverage check counted as v1.3 does (F5);
  `replay_delivery_main.cpp`: the manifest form, on the shared Stage A window.
- `convert_delivery.cpp`: gcc 13's false `-Wmaybe-uninitialized` inside std::regex silenced in the debug build (it
  didn't build there; make check never built it before).
- Shared files: `Makefile` (`include tests/p7b.mk`), `run_tests.sh` (the steps replay-build, stageb, replayer, the
  guest steps under `DELIVERY=`, D46's grep).

## Open

- The F2 texts no fixture reaches (above): the kit's planted-fault runs reach the underflow, row and end-state paths;
  the others remain checked by reading.
