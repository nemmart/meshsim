# WORKER18 notes (v1.35: the inventory)

Branch `sim-w18`. Prompt: `PROMPT.md`. **Plan agreed with the owner 2026-11-10** (go-ahead, Q1-Q3 answered below). Documents only.

## What I read so far

The READMEs, PLAN-V135, PLAN-M1/M2/M3 close-outs, D30-D38, the sessions table; a first survey of `sim/ref` (= `v1.3/meshsim`
plus `Observer.java`, checked by a file-list diff) and of `sim/` (src, tests, tests/ref, bench, fw, tools, the makefiles).
Rough size: 52 src classes, 54 test classes (43 with a `main`), ~50 `run_tests.sh` steps (quick and full), 4 firmware
builds (`sw/build.sh`, `build_cc.sh`, `build_recipes.sh`, `replay/build.sh`, plus `tracer/build.sh` and the sort's), the
kits (`kit/`, `verify/`, `acceptance/`), tools (`tools/`, `KeyGen`, `ConvertDelivery`, `SyntheticDelivery`, ...).

## The plan

1. **Enumerate mechanically, so nothing is missed.** One candidate row per: `run_tests.sh` step (quick and full, and the
   `DELIVERY=` Stage B loop); class with a `main` (its argument modes, e.g. `SortTest p1|sort|run`, `NetTest skewtable`);
   script in `tools/`, `kit/`, `verify/`, `acceptance/`, `sw/`, the sort's and gold's build/check scripts; option read by
   `System.getProperty` or `Config` (idle-skip, shuffle seed, latencies, `machine()`/`defaults()`, `ccCores`, digests,
   the observer); file the Java writes (grep for writers: CSVs, phase logs, fingerprints, digests, summaries); generated
   artifacts (`Registers` -> `mesh.h`, `RegDoc` -> REGISTERS.md tables). Then merge candidates into capabilities.
2. **Map each to the C++**: Makefile targets and `make check`/`conformance`, `sim/tests/*` (conf drivers, goldens,
   fingerprints, T2, the chain), `sim/bench`, `sim/fw`, `sim/tools`.
3. **Check, don't assume:** every claimed equivalent names its evidence (a conformance script, a golden with
   `regen.sh --check`, a fingerprint run, a Quest number). Code with no comparison against v1.3 is marked
   **equivalent, unchecked** (a gap in checking). Status values: equivalent (checked) / equivalent, unchecked / partial /
   gap.
4. **Run v1.3 where the source doesn't settle what a step does or writes:** locally (JRE 21 is here; I'd install a JDK
   and, if needed, read the prebuilt images rather than build firmware). First I read `results/` (the last full run) and
   Quest 118's logs. The runner only if a long run is really needed (claimed by the Quest rules).
5. **The table** (`sim/planning/V135-INVENTORY.md`), grouped as the prompt says (chip and units; programs and suites;
   the sort; Tracer and replayers; input tools; benches; options and outputs; kits and docs). Columns: # | capability |
   what it does | used by | C++ equivalent | checked by | status | disposition (port S/M/L, point to v1.3, drop) | why.
   Then the summary (gaps by disposition, the work they'd take) and the owner's questions.
6. **Commits**: the skeleton with every enumerated row; each group filled in; the summary and questions;
   `REPORT.md`. Each pushed with a clean `git status`.

## Scope questions (now, before I start; answers change the row count)

- **Q1. Siblings outside `meshsim/`.** `v1.3/` also has `gold/` (Java reference emulator), `jumpmap/`, `inputs/` (the
  input pipeline), `tracer/`, `merge-inputs/`, `tools/replayer-opcodes/`, and the top-level kits `kit/`, `runner/`,
  `verify-v1.0/`. My proposal: inventory them in a separate short group, default disposition "point to v1.3" (they make
  meshsim's inputs; v1.35 consumes their outputs), unless you want them in full.
- **Q2. ReplayBench** is not in v1.3; it lives on the `sim-step-cost` branch. Include it as a row (it was the D8/D15 bar)?
- **Q3. The Java drivers in `sim/tests/ref/`** (RouterConf, M2Sort, M3Seg, StateFp, ...) are how the C++ is checked
  against v1.3. With "no Java in v1.35", I'd treat them as part of the oracle (they stay with `sim/ref`, outside the
  freeze), not as gaps. Agreed?

## The owner's answers (2026-11-10)

- **Q1:** the folders outside `meshsim/` get a short separate group, one row each (what it does, who uses it), default
  "point to v1.3". Enough to rule on; not a full inventory.
- **Q2:** no row for ReplayBench; a one-line note that it lived only on `sim-step-cost` and that T2 and the chain
  timings supersede it.
- **Q3:** the drivers in `sim/tests/ref/` are the oracle, left out of v1.35, not gaps. **What follows:** frozen v1.35
  has no `sim/ref` and no Java, so its build, `make check`, and every suite and tool a user runs must work without
  either. **A further group**: everything in `sim/` today that needs Java or `sim/ref` to build, test or run (the
  `--check` regenerators, fixtures generated at build time such as TracerConfRig, kits that embed `ref`), each with a
  disposition: committed fixture, moved out of v1.35, or a gap.

## Progress

- 2026-11-10: the survey; `sim/` built and tested without `ref/` (48 of 63 pass, the 15 failures all data files from
  `ref/`). D39 (owner): every gap is ported; the inventory becomes the porting plan.
- 2026-11-10: `V135-INVENTORY.md` written: groups A-J, pieces P1-P9, the order, the summary, Q1-Q8. `REPORT.md`.
