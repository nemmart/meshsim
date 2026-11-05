# WORKER27 report: the input pipeline

**One documented pipeline that rebuilds every input of meshsim's acceptance from pinned sources on a fresh machine:**
`wip/inputs/` on branch **`inputs-pipeline`** (README there, written for Tony). Findings, and the note for WORKER22:
`WORKER27/NOTES.md`.

## 1. What it is

| script | what |
|---|---|
| `deps.sh` | what Ubuntu 24.04 needs (JDK 21, cmake ≥ 3.24, g++, libelf-dev, python3, git, curl, the RISC-V gcc for Stage B's firmware), the memory reservation and the network; prints the apt line |
| `fetch.sh` | jolt-asic-planning at the pin; a token from `$GH_TOKEN` goes to each git command as a header, never into a file |
| `build.sh WORKLOAD SIZE [STEP…]` | **the one command**: `evm-zone` or `cyclone`, `small` (trace-23, input/22) or `29`; steps `tools`, `deliveries`, `convert` (**v1.26.1, the RTL's target**), `gold` (v1.2, and v1.3 with `GOLD_FORMAT`), `jumpmap`; staged, checked, `COMPLETE`-marked, resumable; the output laid out as the runner's persistent data |
| `record.sh`, `check.sh` | the checksum manifest of a build (`manifest/RUN.sha256`), and a build against it |
| `compare_runner.sh` | a 2²⁹ build against the runner's copies, file by file (`manifest/runner-refs.txt`) |
| `quest/make_task.sh` | the 2²⁹ build as a Quest task (kit: the scripts, the tables, classes compiled here, the tracer at the pin, a libelf) |

Pins: jolt-asic-planning **fa59d9a** (jolt-cpp 88dca85); the tables and `ConvertDelivery` from frozen **v1.26.1**; gold
and jumpmap by meshsim commit (**d97ecde**: task 109's gold 7ef9039 and jumpmap 4788e0e; **eeb64b5** for the v1.3 file).

What moved out of Quest tasks into `wip/inputs/`: 103/104's regeneration body (the deliveries step), 105–107's conversion
(the convert step), 109's gold and jumpmap run (its generator was never committed). WORKER20's regen and WORKER25's
accept generators now point at them.

## 2. Results

| run | where | tools | build | result |
|---|---|---|---|---|
| evm-zone-23 | local, 1 CPU | 3 min (once) | 22 s, 0.7 GB | first build recorded; a second build and a kit build (libelf fallback) equal it |
| cyclone-22 | local | | 15 s, 0.35 GB | the same |
| evm-zone-29 | Quest 111 | | | *pending* |
| cyclone-29 | Quest 112 | | | *pending* |
| gold at 2²⁹, both workloads | local | | 30 s each | v1.2 files equal 109's (`208520ec…`, `f45f71b6…`); v1.3 files equal WORKER18's (`3cd32881…`, `e932baaf…`) |

## 3. The changes outside wip/inputs

- `wip/meshsim/test/ConvertDelivery.java`: the manifest's `converted` object in a fixed order (it was a `Map.of`, so one
  conversion had two SHA-256s from one JVM run to the next); `ReplayerUnitTests`: two JVM runs and an in-process run must
  give the same manifest. ReplayerTest 2187/0. v1.26.1 is unchanged (the owner adds an ERRATA note).
- Header notes in `wip/planning/sessions/WORKER20-tools/tasks/make_regen_task.sh` and `WORKER25-tasks/make_accept_tasks.sh`.

## 4. For the owner

- **Merge:** `inputs-pipeline` adds `wip/inputs/` and the ConvertDelivery fix; it's based on d97ecde, and main has moved
  (gold's v1.3 default, jumpmap's K). Nothing it changes conflicts: the pipeline pins gold and jumpmap by commit.
- **v1.26.1's ERRATA:** ConvertDelivery's `converted` key order (NOTES finding 1).
- **WORKER22:** its NOTES' `image.pages` and `seg/NNN.written` are `initial.pages` and `seg/NNN.pages` (NOTES).
- **Next pin moves** (not done): jumpmap at main's commit changes `proof.json`'s `gold_commit`; the jolt-cpp D13 fix
  branch (`worker18-fix-d13-rd0-noop-not-virtual`) would change the pin if it lands on `tracer-standalone-main`.
