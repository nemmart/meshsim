# WORKER24 report: P6, the sort and its kit

Branch `sim-w24`. Rows D1-D10, H4, H7 of `V135-INVENTORY.md`. Details, every check's numbers and the owner's answers are
in `NOTES.md`. Final, 2026-11-12: **every row of P6 is ported and byte-identical to v1.3** where v1.3 runs it,
with three recorded exceptions (the inputs line's simulator field, Q2; the ORACLE split, Q5; ready_latency below 3,
D47). Nothing is open.

## What v1.35 has now

| row | what | where | checked against v1.3 |
|---|---|---|---|
| D1, D3, D4 | SortTest: `p1`, `sort [full] [only=]`, `run kind=/keys= n= fmt= shaped label= ready= cfg.= csv= phases= force=bt`, the seed (the debug shuffle) | `tests/sort_main.cpp` → `sort_test` (on W20's `tests/mach.h`) | **log-equal: sort-p1** (plain, shuffled, instance-major, step-all), **sort, sort-shuf, sort-bt-kv32, sort-bt-k128**, every check passing (42, 104, 104, 10, 11); all five live in run_tests.sh, `--check` clean |
| D2 | every path exact | `SORT_FP=tests/sort/fp sort_test ...` | **every point of all 14 SortFp references equal** (the boot's end, every GO and all-READY, the end): p1 both formats, the 10 runs of `sort` (direct, LB, TH, stage 2, shaped, both formats), both BT runs at 16K, a shuffled run |
| D5 | the inputs line | `sort_test` | equal but its first field (the simulator: `sort_test=<sha256>`, Q2; run_tests.sh masks it) |
| D6 | SortSummary | `tests/sort_summary_main.cpp` → `sort_summary` | **SUMMARY.md and all.csv byte-identical** on v1.3's kit run at 64 × 2,000 (Quest 178) and on every edge case tried |
| D7 | SortCompare (D41 Q3; PhaseNeutral, VerifySummary and the verify kits dropped) | `tests/sort_compare_main.cpp` → `sort_compare` | output byte-identical to v1.3's on every edge case tried |
| D8 | the measurement kit on the C++ binaries | `tests/sort/kit/` (assemble.sh, run.sh, tools/runjob.sh, v1.3's jobs.txt) | at 2,000 against Quest 178: p4 and shuffle (46 runs) **every run CSV byte-identical**, every log equal; the sweep (46): **38 run CSVs byte-identical**, their logs equal, the six F3 points refused as v1.3 refuses them, two `ready-1` points refused (D47); **all 84 phases CSVs byte-identical**; **at full size (Quest 181), five runs (direct, LB, TH, BT, shuffled) side by side with v1.3's kit: CSVs, logs and TOTALs equal but the peaks**; uniform-k128 1,229,687 as sort64k |
| D10 | Sweep (`quick`, `full`; `results/sweep-<mode>.csv`) | `tests/sweep_main.cpp` → `sweep` (MODE=suite) | **sweep-quick's and sweep full's logs and CSVs byte-identical** (full: Quest 183's v1.3 fixtures); the sweep-quick step live, its CSV checked with `--check` |
| D9 | SplitRun + splitdrv | — | **inherited dead code, not ported** (W19; owner's ruling): `splitdrv.c` doesn't compile in v1.3, no v1.3 build makes its image, `run_tests.sh` never runs SplitRun |
| H4 | the per-run CSV | `sort_test csv=` | byte-identical to v1.3's (46 runs at 2,000) |
| H7 | the profiles (sort.profile, sort.prof, sort.watch; the overlay-load column) | `SORT_PROFILE`, `SORT_PROF=lo-hi`, `SORT_WATCH=f,...` | equal on two runs (`tests/sort/ref/h7-*.log`) |

**Invariant 2 during the sort**: sort_test starts a new disjoint-buffer phase on a tile whenever its core leaves WFI, as
SortTest's monitor does (tile by tile, BigSram::new_phase with W26's disjoint tracking): 0 disjoint violations in every
sort run, as in ref, and every log, fingerprint and kit CSV unchanged.

**make check**: `test_sort_kit` (the kit tools on Quest 178's run; Java's formatting pinned against JDK 21's output) and
`test_sort_suites` (sort-p1's log and fingerprints, the direct and TH runs at every reference point, the profiles). The
full pass on main 500b60fa (all four builds with `-k`, each test run alone, sort_test under ASan and UBSan; the suite
builds' sort tests) is clean (NOTES). **run_tests.sh**: sort-p1, sort, sort-shuf, sort-bt-kv32, sort-bt-k128 and
sweep-quick are steps (MODE=suite: PhaseLog runs there only, so sort_test and sweep refuse a release build);
`sort_test` and `sweep` in SUITES.

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
- `ready_latency` below 3 refused (D47; F5 below): the kit's two `ready-1` sweep runs end with no result.
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
- **F5 → D47. `ready_latency` below 3 is refused, a deliberate difference** (integrator, 2026-11-12): the chip builder's
  READY lines are Links of ready_latency - 2, and the physical default is 8. v1.3 runs ready_latency 1, which the kit's
  sweep sets (`ready-1`, both sets): those two runs end with no result, listed under SUMMARY's "Runs with no result"
  as F3's six are. Sweep's own ready points (4, 8) are unaffected.
- **F6. PhaseLog's read-port peak** counted one more than ref's when a store and an injection fell in the same cycle.
  Fixed by W20 (673f1733): no phases CSV differs in it now.
- **F7. Invariant 8's command-read check reported a violation v1.3 doesn't** in the sweep's workloads (after Mach's
  boot): W22's F4, fixed on main by W26 (6532b75c: the boot ROM takes the boot command at GO).
- **Q6. No v1.3 run takes the PLAN path** (full size, the runner's p4, the 2,000-key kit: direct, LB, TH, BT only).
  Reported; no case invented.

## Changes outside my folders (approved)

`src/units/rv32.h` (Q3's diagnostic), `src/units/cnc.cpp` (F4), `tests/sort_input.h` (the Oracle's `shaped` and R),
the Makefile's `include tests/sort.mk`, `run_tests.sh` (the sort and sweep steps, `sort_test` and `sweep` in SUITES).

## Open

Nothing. The freeze check (PLAN-V135 step 4) reruns the sort steps, the sweep and the kit; anything it turns up comes
back here.
