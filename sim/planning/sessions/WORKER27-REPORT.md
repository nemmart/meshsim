# WORKER27 report: P4's close and the freeze check's task

**P4 is closed.** AgTest, the last of v1.3's eight P4 suites, is byte for byte equal to v1.3's log (240 of 240, plain
and shuffled), live in `run_tests.sh` and in `make check`; no PENDING step cites P4. **The freeze check's task**
(`tests/freeze/`) runs the whole check in three parts (main, kit, check), each under the runner's 45-minute limit.
Rehearsed by me as Quests 189-192; the accepted run, Quests 193-196, was the integrator's, from main after the strip.
WORKER27 closed before the end: the integrator finished the few5-k128 fix (my design, his version), the three stale
status lines and the freeze; my last local commits were superseded and not pushed. This report replaces the partial
`sessions/WORKER27/REPORT.md` (in v1.35/ as frozen; its errata note points here).

## 1. AgTest (inventory C6)

The 6 assertions WORKER22 left (W22-21 (a)-(c): figure 12's PTR and HIST at L = 1 and 3) pass on the harness fix
(401534eb): (a) and (b) needed nothing in the suite. For (c), WORKER22's workaround (the table write between cycles,
`set_ptr`, in `store_ptr_at_read`) is gone: both places are `TileRef::store` at the cycle just run, as ref's
`core.store(tr, ...)`.

- `suite_ag -1` and `suite_ag 4` byte for byte against `tests/p4suites/agtest.log` and `agtest-shuf.log` (240 passed,
  0 failed), and the same under `--step-all` and `--instance-major`.
- `run_tests.sh`: `agtest` and `agtest-shuf` as `step` lines in place of their `pending` ones, `suite_ag` on a new
  SUITES line. `make check`: `agtest_equals_ref_log` and `agtest_shuf_equals_ref_log` in `test_p4suites` (17 cases).
- Merged at 1903be13 (all four builds clean across it and its merges).

WORKER22's report lists AgTest at 206 of 206 ported with figure 12 (34) waiting on W22-16; this supersedes its row C6.

## 2. The freeze check's task (tests/freeze/, PLAN-V135 step 4)

| change | why |
|---|---|
| **sort-split6**: `bench_sort --threads 6 --partition tiles` against `--threads 1` on the same keys; PASS when both exit 0, both spans are 1,229,687 and the digests (the machine at L2's all-READY) are equal | Quest 186 stopped at once: `test_sort` (protected) splits only in row bands (1, 2, 4 or 8). `--partition tiles` is Quests 157/158's: each tile a domain, 64 domains grouped in order into 6 engines (declared placement, not by load). sort-one stays on `test_sort`'s every fingerprint point |
| **TIMES: one line per chain**, each from its own job's end | the single `chains` line was taken when the wait loop reached the chains, after `run_tests.sh`, so it included that wait whenever the chains finished first |
| **toolchain** (the owner's): `toolchain.txt`, appended to `machine.txt`: riscv64-unknown-elf-gcc and binutils versions, the three packages' dpkg versions, picolibc's own `__PICOLIBC_VERSION__`, the image builds' locale; verdict `toolchain` | the runner rebuilds the firmware images |
| **`run_tests.sh` under LC_ALL=C**; `make_task.sh` too | the image builds' locale (Quest 188: `nm` orders the .sym files by the locale); the kit's file order and hashes |
| **STEP_TIMES=1** in `run_tests.sh` (new lines only, off by default): each step's, each equivalent's and the build's wall time in `results/.step-times`; no step's log changes | per-step times for RUNNING.md |
| **`run_tests.sh` alone**; the chains overlap only the sort and T2 | unloaded step times |
| **sort-kit**: `tests/sort/kit` at full size, 92 runs, `-j 10`, alone; PASS when every run exits 0 with a CSV or is refused as recorded (F3 ×6, D47 ×2: exit 134, no CSV, its FATAL line logged), SUMMARY.md is written and p4/uniform-k128's TOTAL is 1,229,687 | the kit's time and a full-size check of every run |
| **make-check**: `make -j$(nproc) check` in a copy of the kit's `sim/` taken before anything is built, alone | make check's time from a clean tree |
| **ITEMS** (`make_task.sh NNN-name "main" / "kit" / "check"`) | the whole check is about 75 min, over the runner's 45-minute task limit: three tasks, claimed in order |
| T2's rows reference moved to `tests/freeze/t2-rows.txt`; the jumpmap messages without a worker name | the frozen copy leaves out `planning/` (W30's audit); D45 |

The task depends on `bench_sort` (now `bench/sort/`, renamed by the strip with the task's paths).

## 3. run_tests.sh's mask()

`mask()` appended a step's own mask unquoted (`'...'${MASK_EXTRA:+; $MASK_EXTRA}`): the shell split D44's expression
into words that sed took as file names ("sed: can't read ..."), sed read none of its input, both sides of the diff were
empty, and **cccores and cccores-shuf were compared with nothing**. Quoted (23f37a0e). Shown with the real `mask()`
and `step()` and the two cccores lines: the fixtures as committed, 0 LOG DIFFERS; one line altered in each fixture,
2; the same altered fixtures under the old line, 0 and sed's errors; D44's figure altered, 0 (the mask works).
Fixtures restored. Both steps' outputs equal their fixtures under the mask applied properly, so the old line hid
nothing; no other step set MASK_EXTRA.

## 4. The rehearsals (Quests 189-192, the runner: i5-14400F, 6 P + 4 E cores)

| Quest | from | result | finding |
|---|---|---|---|
| 189 | sim-w27 1903be13, one task | every item PASS but run_tests | run_tests: the `bus` step read its reference from `ref/`, which a stand-alone kit doesn't have (D39); fixed by a fixture in `tests/p2suites/` (W30). sort-split6 PASS: digest 3d87d546fb080239 at 1 thread and at 6 by tiles, span 1,229,687 at both |
| 190 | sim-w27 3e5b90ab, main | every item PASS; 6 PENDING (all P5) | run_tests 655 s alone (the sum of its 50 step times); skewtable 252 s, engine-full 82 s; chains 1033 s and 1007 s; calibration 36 s |
| 191 | 3e5b90ab, kit | FAIL on one run (moved to hold after its retry) | p4/few5-k128 at full size: "the checker's fact list is full (262144 facts)". All 283,648 facts were `CMEM_REPLY_OVERWRITE`, recorded on every overwrite though the checker judges them only with `strict_replies` (off by default; v1.3 counts them). With the capacity raised the run completes (TOTAL 1,599,920). Fixed (section 5). The other 91: 83 results, 8 refused as recorded; 1287 s |
| 192 | 3e5b90ab, check | FAIL after 16 s | in the TSan targets: link failures in `build/gcc-tsan` (several make targets building the same TSan objects at once under `-j`) and "ThreadSanitizer: unexpected memory mapping" (TSan against the runner kernel's mmap randomisation, `vm.mmap_rnd_bits`). Fixed on main by the integrator: the TSan groups built one after another, the TSan binaries under `setarch -R` |

The integrator's accepted run (main after the strip): 193 check FAIL only on the strip's edit to the generated
`fw/include/mesh.h` (RegDocTest), since restored; 194 kit PASS (1312 s); 195 main PASS, run_tests with no step
PENDING (712 s); 196 check PASS (1428 s). RUNNING.md's times come from these.

## 5. The few5-k128 fix (the checker's sink)

My design, the integrator's version on main (76204c20): the `Sink` has a bit per fact kind; `add()` drops a kind
whose bit is set; `Checker::install()`, before every run and step, sets the bits from `enabled()` for the five
switched kinds (DIR_READPORT_OVERLAP, DIR_UNEXPECTED, CMEM_COMMAND_UNREAD, CMEM_REPLY_OVERWRITE, DISJOINT);
`switches()` clears them until the next install. My local version, checked before it was superseded: few5-k128 at
full size completed at the default capacity with its CSV, phases and log identical to the raised-capacity run's;
of the clang release objects only `check.o` changed, and `test_chip` and `test_sort` printed byte-identical output
before and after; `test_checkself` (`checkself.out`, strict-reply cases included) passed; all four builds clean.

Review of main's version against that design: nothing missed.
- A switch changed between install and a run: impossible without `switches()`, which clears the bits (nothing
  dropped, as before the fix); both callers (`set_check_errors`, `set_strict_replies`) also finalize the checks
  first, so no fact is judged under a switch other than the one it was recorded under. Every `Chip::step` and
  `Chip::run` installs before running; `run_until` goes through `step`. Facts recorded between runs use the last
  install's bits, which match the switches unless `switches()` was called since (then nothing is dropped).
- CheckSelf's strict-reply cases set `strict_replies` at construction or through `set_strict_replies`: the bit is
  clear when they run.
- Other readers: the only sink installer is `Checker::install`, the only reader of the list `Checker::process`, which
  already skipped these kinds; `Checker::facts()` (the count, now without dropped facts) has no callers. The units'
  own counters (`overwritten_unread`, ref's count) are untouched.
- Differences from mine, harmless: 256 bits with a `ki < 256` guard instead of bits sized from `Kind::COUNT` (77
  kinds today; a kind past 255 would just never be dropped); `switches()` clears the four words by index.
- Unrelated, seen in passing: the strip left `checker.cpp`'s TRACER_VIOLATION comment reading "(Tracer.violate, the)".

## 6. Times

RUNNING.md's times are from Quests 194-196 (the integrator's). My `times.md` (Quest 190's step times, unloaded) was
committed locally only and is superseded by them; Quest 190's `run_tests/.step-times` in the Quest repository has
the same per-step figures for the earlier tree.

## 7. Notes for whoever runs the freeze task again

- One task per group (`make_task.sh NNN-name main`, `kit`, `check`); claim the numbers in order. A deterministic
  failure is retried by the runner (MAX_ATTEMPTS 3): move such a task to `tasks/hold/` so it doesn't hold up the next.
- The kit's eight refused runs are expected (F3's six ts-latency-3, ts-latency-4 and corner points, D47's two ready-1
  points); any other non-zero exit fails the item.
- In this sandbox (one CPU, 300 s per command, the container restarted between turns): builds and test runs went in
  resumable chunks with marker files; `test_p4suites` and `test_sort_suites` case by case with the test filter.

## Files (pushed, merged at 1903be13 and 3e5b90ab)

| file | what |
|---|---|
| `tests/suite_ag.cpp` | the workaround replaced by `TileRef::store` at the cycle just run |
| `tests/test_p4suites.cpp` | two cases |
| `run_tests.sh` (shared) | agtest's two step lines and SUITES line; mask()'s quoting; STEP_TIMES |
| `tests/freeze/body.sh`, `make_task.sh`, `t2-rows.txt` | section 2 |
| `planning/sessions/WORKER27-REPORT.md`, `-NOTES.md` | this report, the notes |
