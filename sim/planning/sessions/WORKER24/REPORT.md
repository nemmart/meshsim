# WORKER24 report: P6, the sort and its kit (in progress)

Branch `sim-w24`. Rows D1-D10, H4, H7 of `V135-INVENTORY.md`. Details, every check's numbers and the owner's answers are
in `NOTES.md`. **Written as far as the work goes (2026-11-12):** what waits for W20's PhaseLog and Config is listed
under "Open".

## What v1.35 has now

| row | what | where | checked against v1.3 |
|---|---|---|---|
| D1, D3, D4 | SortTest: `p1`, `sort [full] [only=]`, `run kind=/keys= n= fmt= shaped label= ready= cfg.= csv= phases= force=bt`, the seed (the debug shuffle) | `tests/sort_main.cpp` → `sort_test` (on W20's `tests/mach.h`) | **sort-p1 log-equal** (plain, shuffled, instance-major, step-all); **sort**, sort-shuf, sort-bt-kv32, sort-bt-k128 **log-equal but the peak lines** (PhaseLog), every check passing (104, 104, 10, 11) |
| D2 | every path exact | `SORT_FP=tests/sort/fp sort_test ...` | **every point of all 14 SortFp references equal** (the boot's end, every GO and all-READY, the end): p1 both formats, the 10 runs of `sort` (direct, LB, TH, stage 2, shaped, both formats), both BT runs at 16K, a shuffled run |
| D5 | the inputs line | `sort_test` | equal but its first field (the simulator: `sort_test=<sha256>`, Q2; run_tests.sh masks it) |
| D6 | SortSummary | `tests/sort_summary_main.cpp` → `sort_summary` | **SUMMARY.md and all.csv byte-identical** on v1.3's kit run at 64 × 2,000 (Quest 178) and on every edge case tried |
| D7 | SortCompare (D41 Q3; PhaseNeutral, VerifySummary and the verify kits dropped) | `tests/sort_compare_main.cpp` → `sort_compare` | output byte-identical to v1.3's on every edge case tried |
| D8 | the measurement kit on the C++ binaries | `tests/sort/kit/` (assemble.sh, run.sh, tools/runjob.sh, v1.3's jobs.txt) | p4 and shuffle runs at 2,000: CSVs equal to Quest 178's but the five peak columns; a full-size sample against v1.3's kit side by side: Quest 181 |
| D9 | SplitRun + splitdrv | — | **inherited dead code, not ported** (W19; owner's ruling): `splitdrv.c` doesn't compile in v1.3, no v1.3 build makes its image, `run_tests.sh` never runs SplitRun |
| H4 | the per-run CSV | `sort_test csv=` | equal to v1.3's but the peak columns |
| H7 | the profiles (sort.profile, sort.prof, sort.watch; the overlay-load column) | `SORT_PROFILE`, `SORT_PROF=lo-hi`, `SORT_WATCH=f,...` | equal on two runs (`tests/sort/ref/h7-*.log`), the second but its peak lines |

**make check**: `test_sort_kit` (the kit tools on Quest 178's run; Java's formatting pinned against JDK 21's output) and
`test_sort_suites` (sort-p1's log and fingerprints, the direct and TH runs at every reference point, the profiles). The
full pass (all four builds with `-k`, each of the 70 tests run alone, sort_test under ASan and UBSan) is clean, W23's
`test_ctl_suites.cpp` on gcc debug aside (W23's). **run_tests.sh**: `sort-p1` is a step; `sort_test` in SUITES.

## The references (all from fresh `ref` runs, committed in `tests/sort/`)

- `ref/`: v1.3's six steps (`oracle/ref/sortlogs.sh`) and H7's two runs.
- `fp/`: SortFp (`oracle/ref/SortFp.java`, `sortfp.sh`; M2Sort generalised: shaped, force=bt, SortTest's ready, p1), 14
  runs, committed as a digest per point (the full files ~1 MB each, regenerated with `KEEP=DIR`).
- `kit/n2000.tgz`: v1.3's kit (`oracle/ref/sortkit.sh`) at `-n 2000`, all 92 jobs (Quest 178).

## Deliberate differences (tooling, not the model)

- The inputs line names the simulator binary (`sort_test=<sha256>`) where v1.3 names the jar (owner, Q2).
- SortTest's ORACLE split isn't ported: no v1.3 build makes that image, so nothing to check against (owner, Q5).
- The stack headroom comes from a diagnostic lowest stack pointer in the controller (`rv32.h`: taken at writes to x2,
  armed by the harness, off by default; owner, Q3), not a per-cycle scan; the protected tests unchanged.
- SortTest's `-D` options are environment variables (`SORT_BUILD`, `SORT_MAXCYCLES`, `SORT_PROFILE`, `SORT_PROF`,
  `SORT_WATCH`; `SORT_OPT` and `SORT_FP` are new: the debug options and the fingerprint check).
- The kit: `sort_test` and `sort_summary` in place of the jar; key sets prebuilt and checked by hash, made with
  `pipeline/keys` only where a JDK is present (owner, Q4); `make_jobs.py` not carried (`jobs.txt` is its output).

## Findings

- **F1. v1.3's committed logs of the sort's steps are stale**: they name an older sort image and boot ROM, other cycle
  counts and `ready_latency 1` (today 8). The fixtures are fresh `ref` runs.
- **F2. Fixes to v1.3's kit** (`meshsim/kit/assemble.sh` can't run v1.3's own sort): it copies v1.1's overlay files,
  which v1.2's image no longer has, and leaves out `sw/bootrom.hex`, read from the working directory since v1.2.
  `oracle/ref/sortkit.sh` makes exactly those two fixes; the C++ kit works as committed.
- **F3. Six of the kit's sweep points never ran in v1.3** (`ts_engine_latency` 3, 4 and the corner, both sets): the jobs
  raise L without K, and v1.3 refuses the machine (K = 8 below 2L + 4). Reproduced faithfully (a log, no CSV), v1.35
  being a freeze (D38). **For a later version: the kit should raise K with ts_engine_latency.**
- **F4. C&C's model set `last_all_ready_at` at the harness's all-READY wait too**; in ref only `go()` sets it. Fixed in
  the C&C model (unowned); the protected tests unchanged.
- **Q6. No v1.3 run takes the PLAN path** (full size, the runner's p4, the 2,000-key kit: direct, LB, TH, BT only).
  Reported; no case invented.

## Changes outside my folders (approved)

`src/units/rv32.h` (Q3's diagnostic), `src/units/cnc.cpp` (F4), `tests/sort_input.h` (the Oracle's `shaped` and R),
the Makefile's `include tests/sort.mk`, `run_tests.sh` (the sort-p1 step, `sort_test` in SUITES).

## Open

- **W20's PhaseLog (H3)**: the per-phase peaks (the GATHER queue peak, the "peaks in" lines, the CSV's peak columns)
  and the phases CSV (`phases=`, the kit's `<id>.phases.csv`). Then: the sort, sort-shuf and BT steps go live in
  run_tests.sh, and the kit's CSVs at 2,000 are checked byte for byte against Quest 178. Until then sort_test derives
  the phases from C&C's own GO and all-READY times, read between cycles.
- **W20's Config (step 3)**: `cfg.<field>=` for the kit's sweep jobs; **D10, Sweep** (`sweep quick|full`, its CSV) is not
  started: it needs the Config fields and PhaseLog.
- **W20 / W26**: `Chip::new_phase` (SortTest's new phase at every core wake) for the disjoint-buffer check (invariant 2).
- Quest 181's result (the full-size sample).
