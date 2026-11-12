# v1.35's documentation: a proposal

*The docs session, 2026-11-12, at the integrator's request; for P8's plan (inventory J1, J2). A proposal: nothing
in `sim/` is edited by it. The owner has said the docs session owns the documentation, so the choices below are made,
not offered, except the four marked **for the owner** at the end. Paths are relative to `sim/` (the future `v1.35/`).*

## 1. What is wrong today, in one paragraph

Almost every document in `sim/` is a construction diary: written for the worker who was building against `ref` that
week, in `ref`'s terms. A reader who wasn't there can't use them. Measured on what would be frozen (495 text files
outside `ref/`, `oracle/`, `planning/`): 360 name a worker, 128 point into `ref/`, 132 name a `.java` file, 26 point
into `wip/`. `README.md` is the owner's brief of 2026-11-06, `src/README.md` says the folder is empty, `data/tables/README.md`
is titled `wip/tables`, `fw/REGISTERS.md` is headed v1.2 and says `RegDoc.java` generates it. `REPLAY-INPUT-REQUIREMENTS.md`,
the replay inputs' format, is cited by five files and exists only in `ref/`, which the freeze leaves out. And **there is
no spec**: v1.35 would be a simulator with nothing saying what it simulates.

## 2. The readers

| reader | arrives wanting | needs | starts at |
|---|---|---|---|
| the RTL engineer | what to build; what to compare the RTL against | the spec; "nothing changes for the RTL in v1.35"; how meshsim differs from the spec on purpose; how to get a reference run (traces, fingerprints, counters) | `README` → `spec/` → `CHANGES-v1.35` |
| a firmware author | to write or change a unit's program | the registers, the conventions, the recipes, how to build an image and run it on a tile | `fw/README` |
| the proof-pipeline user | to make inputs and run a replay, a segment or a proof | prerequisites, one command per job, run times, where outputs go, the file formats | `BUILDING` → `RUNNING` → `pipeline/README` |
| someone adding a unit | to model a new unit or peripheral (the field engine is next) | how the simulator works, the port kinds, a checklist and a worked example, the tests a unit must pass | `src/README` |
| an integrator taking over | where the project stands and why it is as it is | the status, the decisions, how work is run | `README` → `planning/README` |
| the owner | to know it is right without reading it all | two pages, and a short list of what is new and his to confirm | `spec/OVERVIEW` |

## 3. The rules

1. **Four kinds of document, never mixed.** *Entry* (`README`), *how-to* (`BUILDING`, `RUNNING`), *reference* (true
   of v1.35 as frozen, no history), *history* (`planning/`).
2. **Every folder has a `README.md`** saying what is in it and how to use it; that is where a folder's reference goes.
   Named documents (`PORTS.md`, `REGISTERS.md`) are for one subject each.
3. **A reference document is complete without `planning/`.** It may cite a decision or a report as the reason
   (`(D26)`); it may not need it to be understood. No worker names, no `ref/` or `wip/` paths, no "to come".
4. **Code comments keep their provenance** (`ref`'s class, the worker, the decision): rewriting 1,500 of them would
   lose information and risk errors. `README` carries a short glossary instead (`ref`, W*n*, D*n*, Quest *n*).
   Usage lines, error messages and logs are cleaned (J2).
5. **The headers are the API reference** (`sim.h`, `config.h`, `check.h`, `mach.h` already are). Documents explain and
   point; they don't restate what a header says.
6. **Paths are relative to the version's root**, so the copy to `v1.35/` needs no edits.
7. **A document keeps its file name when it moves**, since code cites documents by name (47 files cite the audits).

## 4. The documents of v1.35

| document | contents | made from | when |
|---|---|---|---|
| `README.md` | what v1.35 is; the reader's map (section 2); the layout; status; the glossary. One page | new | P8 |
| `BUILDING.md` | prerequisites; the three builds and what each is for (**release has the spec's checks compiled out; suite and debug have them**); one command per thing built | J1 | P8 |
| `RUNNING.md` | every suite, driver, tool and kit: the command, the time, the outputs, the reference it is checked against. Its table checked against `run_tests.sh --list` (a small new option) | J1; `run_tests.sh`'s header; `tests/*/README` | P8 |
| `CHANGES-v1.35.md` | **for the RTL:** no hardware change; the deliberate differences from v1.3's simulator (D27–D29, D33/D44, D34). **For users:** each v1.3 command and its v1.35 equivalent; the `Config` fields dropped (D41 Q4); the tools that changed (D42, D43) | DECISIONS; the inventory's "equivalent" column | P8 |
| `spec/OVERVIEW.md` | the goal, the system, one proof's flow, platform against Jolt-specific, status. Two pages | new; **drafted, branch `v1.35-spec`** | now |
| `spec/GUIDELINES.md` | the rules the design follows (checked or UB; terminal errors; the mesh's regimes; what spec, meshsim and RTL must agree on) | the spec's preamble; the owner's rulings | now |
| `spec/machine-spec.md`, `replayer-opcodes.md`, `schedule.md` | v1.3's, hardware and section numbers unchanged; every statement about the simulator, and every path, made true of v1.35 (under a hundred edits, listed in a table) | `v1.3/spec/` | now |
| `spec/history/` | the older `CHANGES-*` guides and the four `DECISIONS-v1.x` logs the spec cites, unchanged | `v1.3/spec/`, `v1.3/planning/` | now |
| `src/README.md` | how the simulator works: the cycle and its levels, wakes, sizing at startup, threads and the partition, the two coding regimes, determinism and its checks; then **adding a unit**: a checklist, one worked example, the tests required | `README.md`'s brief; D6, D16, D20, D23, D24, D26, D30; `sim.h` | P8 |
| `src/PORTS.md` | the port kinds' rules, stated on their own; the `ref` class and line of each moves to an appendix | today's, restructured | P8 |
| `src/units/README.md` | the model: unit ↔ file ↔ spec section; `MachConfig`'s fields ↔ spec 10; the checker's kinds ↔ the invariants of spec 9; the simulator-only words | new, mostly tables | P8 |
| `tests/README.md` | how exactness is established: goldens, conformance drivers, fingerprints; what each fixture folder is and how it is regenerated (from `oracle/`, which is not in the frozen copy) | the `golden*` READMEs; AUDITs' section 0s | P8 |
| `tests/TRACE-FORMAT.md` | as today, extended to the fingerprint and phase-log formats | today's | P8 |
| `data/REPLAY-INPUT-REQUIREMENTS.md` | the replay inputs' format | `ref/meshsim/`, version banners trimmed | P8 |
| `fw/README.md`, `CONVENTIONS.md`, `RECIPES.md`, `REGISTERS.md` | content unchanged (D38); banners and paths corrected; `REGISTERS.md` and `mesh.h` from the C++ generator, its header saying so | today's | P8 |
| `pipeline/README.md` and its three | as today | today's | — |
| `planning/README.md` | what the history is and how to read it; the sessions' index | `sessions/README.md` | P8 |

## 5. What happens to what is there today

| today | becomes |
|---|---|
| `README.md` (the brief) | its lasting content folded into `src/README.md`; the original kept as `planning/BRIEF.md` |
| `planning/DECISIONS.md`, `PLAN-M1`–`M3`, `PLAN-V135`, `V135-INVENTORY.md` | history, kept whole in `planning/`; what a user needs from them is folded into `CHANGES-v1.35.md` and `src/README.md` |
| `planning/sessions/` | **the reports and the index kept**; notes, prompts, parked messages and task folders left out of the frozen copy (they are in git at the freeze commit, which `planning/README.md` names) |
| `tests/AUDIT-M1`–`M3` | history: `planning/audits/`, names unchanged. They specify `ref`'s couplings to the workers; the rules live in the units' headers |
| `src/PORTS.md`, `tests/TRACE-FORMAT.md`, `tests/*/README.md`, `data/tables/README.md` | kept as reference, cleaned to rule 3 |
| `fw/replay/README.md`, `fw/recipes/README.md`, `fw/sort/**` | kept; each gets a first paragraph true of v1.35, the older text under "history" as it already is |
| `ref/`, `oracle/` | not in the frozen copy (D38, D41) |
| `src/sketch/`, leftover `bench/` | not in the frozen copy (J2) |
| v1.3's `spec/` | carried, as section 4 |
| v1.3's `diagrams/`, `intro/`, `results/`, `RESULTS-*`, `DESIGN-NOTES`, `DEVIATIONS`, `CHANGES-v1.1`, `SIM-CHANGES-*`, `WORKER*_PROMPT`, `deliverables/`, `kit/`, `runner/`, `verify-v1.0/`, `meshsim/README`, `REPLAYER-NOTES`, `UPSTREAM-CHECK` | **not carried**: they describe v1.0 to v1.3 and stay in `v1.3/`. The spec's companion paragraph says where the figures are and that they are at v1.1 |

## 6. How it is checked

- **`tools/doccheck.sh`, in `make check`:** every path a document cites exists; no `sim/`, `ref/`, `oracle/`, `wip/`
  path outside `planning/`; every folder has a README; every document named in a code comment is in the tree.
- **BUILDING followed on a fresh machine** (a Quest task; already P8's check).
- **A fresh-reader test:** a new session, given only the frozen folder, answers a fixed set of questions per reader
  of section 2 (for example: which build do I use to check a firmware change, and why). Wrong answers are doc bugs.
- **The spec's edits:** a script shows every line of `machine-spec.md` is v1.3's, or is in the table of edits.
- **The owner reads `spec/OVERVIEW.md`**, and each new document's list of statements that are his to confirm.

## 7. Order of work

**Now, in parallel with the ports** (new files only, in `spec/`, on `v1.35-spec`): the overview (drafted), the
guidelines, the corrected spec with its edit table. **At P8:** everything else, by P8's worker from sections 3 to 5,
`RUNNING.md` last, when the step list is final. **The freeze's gate for docs:** section 6's five checks.

## 8. For the owner

1. **A spec in v1.35.** This widens D38 ("only docs, build scripts and cleanup") by one folder; no hardware changes.
2. **The spec's restructuring** (platform against Jolt-specific, history moved out) **is not a gate for the freeze.**
   It lands before it if ready, otherwise as v1.4's first commit; either way as a move checked line for line.
3. **Session notes and prompts left out** of the frozen copy (v1.3 kept its notes).
4. **v1.3's figures and introduction are not carried.** Redrawing them is its own piece of work, not v1.35's.
