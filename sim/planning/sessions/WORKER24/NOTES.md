# WORKER24 notes (P6: the sort and its kit)

Branch `sim-w24`, from main 67d42fb. Rows: D1-D10, H4, H7 (V135-INVENTORY).

## The plan (agreed with the owner, 2026-11-11)

Commit and push after each step; each step checked against `ref`.

1. **The references, from `ref`, committed as fixtures** (drivers in `oracle/ref/`):
   - v1.3's logs of my steps: `sort-p1`, `sort`, `sort-shuf`, `sort-bt-kv32`, `sort-bt-k128`, `sweep-quick` (and its
     CSV), each run once in `ref`;
   - `SortFp`: SortTest's sequence for any run, a hash-only StateFp at every GO (as M2Sort): one run per path and format
     (direct, LB, TH with stage 2, shaped, p1; BT forced at 16K in both formats; PLAN if some v1.3 run takes it, Q6);
   - the kit at `-n 2000` (92 runs: the CSVs, phases CSVs, SUMMARY.md, all.csv), on the runner.
2. **The `sort` driver** (D1-D4, H4, H7): SortTest's arguments and output in C++, from `test_sort` and W20's `Mach`;
   Inputs (D5). Log-equal on p1, sort, sort-shuf, plain and under the debug options; fp at every GO.
3. **The CSVs**: the per-run CSV byte-identical (H4); the phases CSV through W20's PhaseLog (H3), not my own.
4. **Sweep** (D10): `sweep quick|full` on `Mach` with the a2a, gather and search images; `sweep-quick.log` and its CSV
   equal.
5. **The kit** (D8): `assemble.sh`, `run.sh` (the same options and jobs, restartable) on the C++ binaries; SortSummary
   (D6) and SortCompare (D7) in C++. The CSVs and SUMMARY byte-identical on the 2,000-key smoke, then a full-size sample
   on the runner.
6. **The steps join W20's runner** under v1.3's names; the quick ones in `make check`.
7. **REPORT.md**: D9's split driver is dead code, not ported.

## The owner's answers (2026-11-11)

- **Q1 (W20's pieces not on main: PhaseLog, Config fields, the runner).** Steps 1, 2 (without the peak lines) and 5's
  SortSummary and SortCompare first; the rest as W20's PhaseLog, Config fields and runner land.
- **Q2 (the `inputs:` line).** The binary's sha256 in place of `meshsim.jar=(classes)`; that one field masked in the
  log check. Tooling metadata, not the model; noted in REPORT.md.
- **Q3 (stack headroom, tile 0's profile, `sort.watch`).** Approved: a diagnostic-only lowest-SP record in `rv32.h`,
  taken at writes to x2, off by default and zero cost when off; idle-skipped spans attributed in bulk. Before and after:
  test_chip, test_sort, test_t2_harness, test_cc_units and M1's goldens unchanged. Noted here and in a comment.
- **Q4 (key sets).** The kit is a testing run, so no Java (D40): prebuilt key sets checked by hash by default;
  `pipeline/keys` called only if a JDK is present and the sets are missing.
- **Q5 (SortTest's ORACLE branch).** Left out and recorded like D9: no v1.3 build makes an image with `ORACLE`, so
  there is nothing to check against.
- **Q6 (PLAN).** Search the full-size results for a run that takes PLAN; if none exists, report it. Don't invent a case.

## Environment

- Container: 1 CPU, Ubuntu 24.04; installed `openjdk-21-jdk-headless`, `clang lld` (clang 18.1.3; gcc 13.3.0).

## Progress
- **Step 1, the references** (all from fresh `ref` runs, committed in `tests/sort/`; README there):
  - `ref/`: v1.3's six steps (sort-p1, sort, sort-shuf, sort-bt-kv32, sort-bt-k128, sweep-quick and its CSV) through
    `oracle/ref/sortlogs.sh`; every check passes in `ref`; sort-shuf equals sort but for its last line.
  - `fp/`: SortFp (`oracle/ref/SortFp.java`, `sortfp.sh`): 14 runs (p1 both formats, every `sort` run, both BT runs),
    a fingerprint at every GO and all-READY. The full files are ~1 MB gzipped each (13 MB for the short runs), so the
    committed form is a **digest per point** (`.fpd`: cycle, line count, sha256 of the point's unit lines sorted
    bytewise); the full `.fp` is regenerated with `KEEP=DIR` to localise a difference, its sha256 in `HASHES`. The
    short runs here, the two BT runs on the runner (Quest 178).
  - `kit/n2000.tgz`: v1.3's kit (`oracle/ref/sortkit.sh`) at `-n 2000` on the runner (Quest 178, i5-14400F, 16 CPUs,
    `kit/n2000-machine.txt`): 86 run CSVs, phases CSVs and logs, SUMMARY.md, all.csv, HASHES. 6 runs refused (F3).
- **Step 5, the kit's tools**: `sort_summary`, `sort_compare` (`tests/sort_kit.h`: Java's `%,d`, `%+.2f` as Formatter
  rounds, HALF_UP on the shortest digits, not C's exact-binary rounding; Path's normalisation; Inputs). Output for
  output against v1.3's on ref kit runs and edge cases (differing HASHES, none, mixed kits, a log with no inputs line, an
  empty CSV, failed checks, M10's column name, missing phases, trailing slashes, an explicit output path). On Quest
  178's run, `SUMMARY.md` and `all.csv` byte-identical. `test_sort_kit` (make check) pins the formatting against JDK 21's
  own output and runs both tools on `kit/n2000.tgz`. `tests/sort.mk`, one include line in the Makefile.

## Findings

- **F1. v1.3's committed logs of the sort's steps are stale** (`results/sort*.log`): they name an older sort image
  (`sort.code.bin` 60060d9c..., today 2ab3c746...) and boot ROM (23ca7f15..., today b2165425...), with other cycle counts
  and `ready_latency 1` against today's 8 (v1.25's default). Not used: the fixtures are fresh `ref` runs.
- **F2. v1.3's measurement kit can't run v1.3's sort as committed** (`meshsim/kit/assemble.sh`): it copies v1.1's
  overlay files (`loader.bin`, `sort.ovinit.bin`, `sort.stage.bin`; v1.2's image has none) and leaves out
  `sw/bootrom.hex` (read from the working directory since v1.2). `sortkit.sh` fixes exactly those two; the C++ kit
  works as committed. For REPORT.md: fixes to v1.3's kit.
- **F3. Six of the kit's sweep points never ran in v1.3**: `ts_engine_latency` 3 and 4 and the all-pessimistic corner,
  in both sweep sets: the jobs set `cfg.tsEngineLatency` without raising K, and v1.3 refuses the machine (K = 8 below
  max(2L + 4, ...) = 10 or 12; D49). Owner (2026-11-11): **reproduced faithfully** (a log, no CSV; SUMMARY's "Runs with
  no result"), v1.35 being a freeze (D38); a finding for a later version: the kit should raise K with
  ts_engine_latency.
- **Q6. No v1.3 run takes PLAN**: v1.3's full-size results (`results/full-size`, `runner/065-p4-full`) and the 2,000-key
  kit take direct, LB, TH and BT only. Reported, no case invented.

## Requests

- **W20 (runner)**: `mask()` to mask the `inputs:` line's first field (the simulator's hash; Q2). Owner: with W20.
