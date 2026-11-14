# v1.35 errata

*What the frozen tree differs from what the freeze meant to deliver.* v1.35 was frozen on 2026-11-13 (commit 5dd88302,
sim/ at 09283083) after a shortened final sequence. Its code and checks stand (Quests 194-196: every `run_tests.sh`
step, none pending, logs equal to v1.3's; the full-size sort, T2, both chains, the calibration, the sort kit, `make
check` from a clean tree). The items below are documentation and record-keeping that the shortened sequence left
undone or unchecked. This file is the only addition to v1.35/ since the freeze; nothing else in it changes.

## For anyone building or running v1.35

1. **`make check` on newer kernels needs `setarch`.** `make check` runs the ThreadSanitizer tests (`make tsan`,
   `tsan-chip`, `tsan-units`). On kernels whose address randomisation is above what TSan supports
   (`vm.mmap_rnd_bits` above 28, as on Ubuntu 24.04's 6.8 kernel), they stop at once with "ThreadSanitizer: unexpected
   memory mapping". Run it as `setarch $(uname -m) -R make check` (address randomisation off for make and its
   children); the freeze check ran that way. `BUILDING.md` doesn't say so.
2. **`RUNNING.md`'s times were filled in by the integrator**, from Quests 194-196, into the docs writer's draft; the
   docs writer's final pass didn't happen. The "writes" column wasn't reconciled against a full run's outputs, and the
   finished table wasn't reviewed. Every path it cites exists (`tools/doccheck.sh` passes).
3. **`src/units/README.md`'s invariants table wasn't rerun against the final tree.** Checker kinds and invariant
   cases changed late (CtrlTest's invariants; the checker's fact sink dropping the kinds its switches hold off), so a
   row may be stale. The checker's code (`tests/checker.cpp`) is the authority.

## Checks the shortened sequence skipped

4. **The comment strip ran without its own `make check` pass** (`tools/freeze/strip.sh --apply`, not
   `--make-check`). Its proof held (preprocessed output identical but through messages.txt's edits; firmware images
   byte-identical; `pipeline/check.sh`), but it edited one generated file, `fw/include/mesh.h`, so it no longer equalled
   its generator. The freeze check found it (Quest 193) and it was restored before the freeze (6a751eb0). A generated
   file that no test compares with its generator could carry a strip edit unnoticed; none is known.
5. **The frozen commit is not exactly the checked one.** The freeze check ran on 6a751eb0; v1.35 is 09283083, which
   adds only `RUNNING.md`'s times, doccheck's exemptions (`tools/doccheck-exempt.txt`) and a one-line fix to
   `tools/doccheck.sh` (a clean tree now exits 0). `make check DOCCHECK=1` was not run as one command; doccheck was run
   on its own and is clean.
6. **The copy itself wasn't built and run where it stands.** An earlier copy (d6c9fec5) was: the kept benches built and
   `run_tests.sh quick --check` passed inside it.
7. **The checker's fact sink** (the fix that lets the sort kit's few5-k128 complete at full size: `src/check.h`,
   `src/check.cpp`, `tests/checker.h`, `tests/checker.cpp`) was verified by the runner's runs (194, 196: CheckSelf's
   strict-reply cases included) and not by a CheckSelf case of its own.

## The record

8. **`planning/sessions/WORKER27/REPORT.md` is a partial report**, and WORKER27 has no row in
   `planning/sessions/README.md`. The complete report is in the repository's `sim/planning/sessions/`.
9. **`planning/PLAN-V135.md` doesn't record the freeze**: it ends with "what remains is the freeze sequence". The
   freeze check is Quests 194-196 (193 found item 4's edit).
