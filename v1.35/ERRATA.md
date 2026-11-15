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
   children); the freeze check ran that way, as `setarch $(uname -m) -R make -j16 check`. `BUILDING.md` and
   `RUNNING.md`'s make check row give plain `make check`. Fixed in sim/.
2. **`RUNNING.md` has four errors.** Reviewed after the freeze against Quests 194-196: the suites table's 56 rows match
   `run_tests.sh` (commands, references, outputs) and every time matches Quest 195's; the long-runs table matches
   `tests/freeze/body.sh`. The errors: the make check row (item 1); periph-shuf also writes `results/periph-shuf.out`;
   `replay_shard` also takes `[--stride S --phase K]`; the layout row's v1.3 check is
   `v1.3/meshsim/tools/check_layout.sh` (not `tools/check_layout.sh`). The tools table's `sort_test` line is unchecked
   (the program prints no usage). Fixed in sim/.
3. **`src/units/README.md`'s invariants table is stale and incomplete.** Row 2 says the disjoint-buffer rule holds "by
   construction", but it is checked (DISJOINT, `src/units/big_sram.h`); the table names 32 of `src/check.h`'s 72 fact
   kinds. `tests/checker.cpp`'s messages name each kind's invariant and are the authority. Rebuilt in sim/ from those
   messages.
4. **`spec/CHANGES-v1.3-to-v1.35.md`'s drivers table is stale**: ReadPortGatherTest (`tests/suite_rdport.cpp`),
   CondTest (`tests/ctl/suite_cond.cpp`) and S1Bench (`tests/ctl/suite_s1bench.cpp`) read "being ported"; all three are
   ported and live steps (rdport, cond, s1bench). Fixed in sim/, through `spec/check/build.py`.

## Checks the shortened sequence skipped

5. **The comment strip ran without its own `make check` pass** (`tools/freeze/strip.sh --apply`, not
   `--make-check`). Its proof held (preprocessed output identical but through messages.txt's edits; firmware images
   byte-identical; `pipeline/check.sh`), but it edited one generated file, `fw/include/mesh.h`, so it no longer equalled
   its generator. The freeze check found it (Quest 193) and it was restored before the freeze (6a751eb0). A generated
   file that no test compares with its generator could carry a strip edit unnoticed; none is known.
6. **The frozen commit is not exactly the checked one.** The freeze check ran on 6a751eb0; v1.35 is 09283083, which
   adds only `RUNNING.md`'s times, doccheck's exemptions (`tools/doccheck-exempt.txt`) and a one-line fix to
   `tools/doccheck.sh` (a clean tree now exits 0). `make check DOCCHECK=1` was not run as one command; doccheck was run
   on its own and is clean.
7. **The copy itself wasn't built and run where it stands.** An earlier copy (d6c9fec5) was: the kept benches built and
   `run_tests.sh quick --check` passed inside it.
8. **The checker's fact sink** (the fix that lets the sort kit's few5-k128 complete at full size: `src/check.h`,
   `src/check.cpp`, `tests/checker.h`, `tests/checker.cpp`) was verified by the runner's runs (194, 196: CheckSelf's
   strict-reply cases included) and not by a CheckSelf case of its own. A review against the original design found no case missed (switches changed
   between install and a run, CheckSelf's strict-reply cases, other readers of the fact list).

9. **The strip left 20 comments garbled.** It replaced a worker's possessive ("W14's") with "the", so 20 comments in
   v1.35's sources read oddly, e.g. `tests/checker.cpp`'s "(Tracer.violate, the)" and `bench/sort/bench_sort.cpp`'s
   "with the, the and the units". Comments only: no code, log or output differs. All 20 are rewritten in sim/ (scanned
   by comparing every line the strip edited with its original); the strip's report lists every edit.
10. **The fresh-reader test was never run** on any document (the docs proposal's last check: a reader new to the
   project follows each document as written). The spec's mechanisms table was rerun after the freeze and confirmed:
   the checks it lists are there (invariants 2, 3 and 6 counted and reported; DMA_HDR / DMA_ADDR changed mid-transfer,
   `src/units/periph_slot.cpp`; the Tracer's oversized transfer fatal, `src/units/tracer.cpp`), and its list of checks
   neither simulator has is right.

## The record

11. **`planning/sessions/WORKER27/REPORT.md` is a partial report**, and WORKER27 has no row in
   `planning/sessions/README.md`. The complete report is the repository's `sim/planning/sessions/WORKER27-REPORT.md`.
12. **`planning/PLAN-V135.md` doesn't record the freeze**: it ends with "what remains is the freeze sequence". The
   freeze check is Quests 194-196 (193 found item 5's edit).
