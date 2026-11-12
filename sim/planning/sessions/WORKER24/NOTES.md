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
- **Step 2, the driver** (`tests/sort_main.cpp` → `sort_test`, on W20's `Mach`; `sort_input.h`'s Oracle gains `shaped`
  and R): SortTest's arguments and output. **sort-p1 log-equal; sort: every line equal but the peaks** (the GATHER queue
  peak and the "peaks in" lines, which wait for W20's PhaseLog, H3), 104 passed, as v1.3. Until PhaseLog lands the
  phases come from C&C's `last_go_at`/`last_all_ready_at`, read between cycles; two offsets found and documented in the
  code: SortTest's `allReadyAt` is ref's `c.cycle` inside C&C's step (the harness's t - 1), PhaseLog's end is t; and
  C&C's `last_all_ready_at` also moves at a GO (not a phase end). Mach::Image reads only three-field .sym lines; the
  driver reads `nm -S`'s four-field ones as SortTest does. Not yet: shuffled and debug options, the BT steps,
  fingerprints at every GO, the CSVs against the kit, profile/watch (H7), the runner's steps.
- **F4. C&C's model set `last_all_ready_at` at the harness's all-READY wait** (`CncStep::WAIT_READY`, SortTest's
  `atAllReady` waitUntil) as well as at `go()`'s; in ref only `go()` sets `lastAllReadyAt` (CnC.java:193; SortTest's
  waitUntil keeps its own `allReadyAt`). Fixed in the unowned C&C model (owner, 2026-11-12): WAIT_READY no longer
  touches it. test_mach (MachSelf), test_sort, test_t2_harness, test_chain, test_chip, test_cc_units, test_cnc
  unchanged (clang release); the driver's phases read go()'s value directly (no offset).
- **Step 2, the paths exact** (clang release): `SORT_FP=tests/sort/fp sort_test ...` compares every run with its
  SortFp reference at every point ref took (the boot's end, every GO and all-READY, the end): **every point of all 14
  references equal**: p1 (both formats), the 10 runs of `sort` (direct, LB, TH, stage 2, shaped, both formats), both BT
  runs (16K, forced), and uniform-k128 shuffled (seed 4). Logs, against v1.3's, equal but the peak lines (PhaseLog):
  sort-p1 (also shuffled, instance-major, step-all), sort (plain, shuffled seed 4, instance-major, step-all: 104
  passed each), sort-bt-kv32 and sort-bt-k128 (inputs line equal under the runner's mask). BT runs here: 19 s and 28 s.
- **The runner**: `sort-p1` is a step (its log equal); sort, sort-shuf and the BT steps stay pending with the reason
  (their peak lines) until PhaseLog lands. `tests/sort/keys.sh`: the BT key set prebuilt and checked by hash, made with
  pipeline/keys only if missing and a JDK is present (Q4's rule, for the steps too).
- **The per-run CSV (H4)**: `sort_test run ... csv=` against Quest 178's kit CSVs (`kit/n2000.tgz`): p4/uniform-k128,
  few5-k128, trace10-10-kv32, zipf10-k128 equal in every column but the five peak columns (PhaseLog); their logs equal
  but the peak lines, the inputs lines equal under the mask.
- **make check**: `test_sort_suites` runs sort_test: sort-p1's log against ref's (first field masked) with both p1
  fingerprints, and the direct (uniform-k128) and TH (reverse-kv32) runs at every reference point (17 s, clang release).
- **The full pass** (2026-11-12, at c3d56dff, this container: 1 CPU, gcc 13.3.0, clang 18.1.3): make check's copies and
  TSan stages pass; then every build with `make -k` and each of the 70 tests run separately: **gcc debug, clang debug,
  gcc release, clang release all pass**, sort_test under ASan and UBSan included (test_sort_suites, both debug builds).
  The one exception: W23's `test_ctl_suites.cpp` doesn't build on gcc 13 debug (std::regex, -Wmaybe-uninitialized; W23's,
  told by the owner). sort_main.cpp's own gcc-debug failure (-Wdangling-reference on a TileRef temporary) fixed first.
- **The kit on the C++ binaries** (`tests/sort/kit/`: assemble.sh, run.sh, tools/runjob.sh, v1.3's jobs.txt,
  KEYS.sha256 = v1.3's kit's key hashes, README): assembled and run on the uniform jobs at -n 2000 (p4 both formats,
  shuf): every run's CSV equal to Quest 178's but the peak columns; SUMMARY from sort_summary. Waiting: the peaks and
  phases CSVs (W20's PhaseLog), the sweep's `cfg.*` (W20's Config, step 3).
- **H7, the profiles** (SortTest's -Dsort.profile, sort.prof, sort.watch as SORT_PROFILE, SORT_PROF=lo-hi,
  SORT_WATCH=f,...; the overlay-load column from tile 0's profile): references `tests/sort/ref/h7-uniform-kv32.log`
  and `h7-reverse-k128.log` (sortlogs.sh; not v1.3 steps). Both equal, the second but its peak lines. Sampled between
  cycles as ref's monitor and Controller.runCycle do (sort.prof counts the boot's cycles too, not the final check).
  `test_sort_suites` checks the first: all four builds pass. SORT_WATCH's clone lookup takes the first name of the
  .sym with the prefix where SortTest searches a HashMap (one candidate in every case here).
- **Quest 181** (claimed after re-reading NUMBERS.md; pushed): the C++ kit and v1.3's side by side at 64 x 65,536 on `tests/sort/kit/jobs-sample.txt` (direct, LB, TH, BT, shuffled); compared CSVs and logs (peaks aside), sort_compare, uniform-k128 against sort64k's 1,229,687.
- **Quest 181's result** (re-run after its first attempt failed in assemble.sh: make's targets named by absolute path,
  fixed; task updated in place, the failed result removed): **all verdicts PASS** on the runner (i5-14400F, 16 CPUs).
  The C++ kit and v1.3's at 64 x 65,536, five runs each (direct, LB, TH, BT, shuffled): every CSV equal but the peak
  columns, every log equal but the peak lines and the simulator's field, every TOTAL line equal up to the GATHER peak;
  uniform-k128's TOTAL 1,229,687 as sort64k. sort_compare lists every step of every run as differing: the peak columns
  (0 here until PhaseLog), as expected. Wall times recorded, not timing (five runs at a time each side).
- **PhaseLog (W20, main 0de4a23c) in**: sort_test attaches `Chip::phases()` at construction (SortTest's namer before the
  boot; p1 the default), closes it after the run, takes its phases and peaks; my stand-in (C&C's GO and all-READY times)
  is gone. PhaseLog runs in the debug and suite builds only: sort_test refuses a release build with a message; the kit
  (assemble.sh) and run_tests.sh build MODE=suite; test_sort_suites passes without running in release builds.
  **Every log now equal to v1.3's line for line** (the inputs line's first field aside): sort-p1, sort, sort-shuf,
  sort-bt-kv32, sort-bt-k128, H7's two. **run_tests.sh: sort, sort-shuf and both BT steps live** (the BT key set by
  tests/sort/keys.sh); `--check` clean on all five sort steps.
- **The kit at 2,000 against Quest 178** (suite build; p4 and shuffle, 46 runs): **every run's CSV byte-identical**, every
  log equal (first field aside), every phases CSV equal but one column, `unreadOverwrites` (invariant 8 as a statistic:
  0 until W26's tracking lands, phase_log.h; 64 in some of v1.3's SPLIT phases). The sweep group waits for the Config
  fields (W20's next item).
- **Checks of this change** (2026-11-12): sort_test, test_sort_suites and test_sort_kit built and run in all six builds (clang and gcc: suite, release, debug): all pass, the release builds' sort_test checks passing without running (PhaseLog). The runner's five sort steps with `--check`: no log differs.
- **Sweep (D10)**: `tests/sweep_main.cpp` → `sweep [quick|full]` (W20's Config front end, main 4a2930f0, and
  PhaseLog; MODE=suite): **sweep-quick's log and CSV byte-identical** to v1.3's (94 s here). The `sweep-quick` step is
  live in run_tests.sh; with --check its CSV is compared too (a difference prints a FAIL line into the log).
- **The kit's sweep group at 2,000** against Quest 178: the six F3 points refused as v1.3 refuses them (K below kMin);
  **38 run CSVs byte-identical**, their 38 logs equal; the phases CSVs equal but `unreadOverwrites` (W26) and F6.
  **But two more runs are refused here and ran in v1.3: F5.** sort_test now flushes each printed line, so a refused
  run's log keeps its inputs line, as Java's does.
- **F5. `ready_latency` 1 is refused by v1.35's chip builder** (`src/units/chip.cpp`: `ready_latency < 3` is FATAL,
  since M2's stage 4: the READY lines are Links of ready_latency - 2 through the row collectors). v1.3 runs it: the
  kit's sweep has a `ready-1` point in both sets (`cfg.readyLatency=1`), which v1.3 completes and v1.35 refuses (a log,
  no CSV; SUMMARY lists them under "Runs with no result"). Sweep's own points (4, 8) are unaffected. **For the owner:**
  model ready latencies 1-2 (the line wiring restructured), or record a deliberate difference (D30: v1.35 needs
  ready_latency >= 3; the physical default is 8).
- **F6. PhaseLog's `readPort` peak differs at sram read latency 3 and 4** (the kit's sweep: 1-28 phases of 99 in 4 runs,
  C++ 2 where v1.3 has 1; every run CSV and log equal). ref's ReadPort.inUse() is `stores - injected` with `injected`
  counting the injections of the current cycle as they happen; the C++ `rp_peak` (tile_ctl.h) subtracts injections
  before t (the free-entry count's rule), so a store in the same cycle as an injection counts one more. A statistic,
  W20's (PhaseLog); for W20 through the integrator.
- **F5 ruled (D47, 2026-11-12)**: ready_latency below 3 stays refused, a deliberate difference; the kit's two `ready-1` runs end with no result, listed under "Runs with no result" as F3's. **F6** is with W20.
- **The full pass** (2026-11-12, after merging main with W23's fix): copies check; gcc debug, clang debug, gcc release, clang release, every test run alone, all pass (test_ctl_suites included); the suite builds (gcc, clang): test_sort_suites and test_sort_kit pass.
- **The kit's 92 runs at 2,000 against Quest 178 again** (2026-11-12, main with W20's F6 fix 673f1733 and W26's
  invariant 8 020519bd, suite build): **84 run CSVs byte-identical, 84 logs equal** (first field aside); the 8 without
  a result are F3's six and D47's two. **F6 fixed**: no phases CSV differs beyond column 27. **But `unreadOverwrites` is
  still 0 in every phase** (v1.3: nonzero in 4,417 rows of these 84 files): W26 counts it
  (`TileCore::track.overwritten_unread`, rv32.h) but PhaseLog doesn't read it yet (tests/phase_log.cpp:79,
  `v[17] += 0`, "pending"). For W20 through the integrator; then the phases CSVs again.
