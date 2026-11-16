# WORKER27 report: P4's close and the freeze check's task

**P4 is closed**: AgTest, the last of v1.3's eight P4 suites, is byte for byte equal to v1.3's log (240 of 240, plain
and shuffled), live in `sim/run_tests.sh` and in `make check`; no PENDING step cites P4. **The freeze check's task**
(`tests/freeze/`) is fixed and rehearsed again (Quest 189): every item passes but `run_tests`, which fails on one step
outside P4 (bus, below; its fixture is on W30's branch). Branch `sim-w27` (1903be13 merged into main; 23f37a0e, main
9f7f3cd5 merged in, and this report to merge).

## 1. AgTest (inventory C6)

The 6 assertions WORKER22 left (W22-21 (a)-(c): figure 12's PTR and HIST at L = 1 and 3) pass on the harness fix on main
(401534eb): (a) and (b) needed nothing in the suite. For (c), WORKER22's workaround (the table write between cycles,
`set_ptr`, in `store_ptr_at_read`) is gone: both places are `TileRef::store` at the cycle just run, as ref's
`core.store(tr, ...)`. Checked:

- `suite_ag -1` and `suite_ag 4` byte for byte against `tests/p4suites/agtest.log`, `agtest-shuf.log` (240 passed, 0
  failed), and the same under `--step-all` and `--instance-major`.
- run_tests.sh: `agtest` and `agtest-shuf` as `step` lines in place of their `pending` ones, `suite_ag` on a new SUITES
  line; make check: `agtest_equals_ref_log`, `agtest_shuf_equals_ref_log` in `test_p4suites` (now 17 cases).
- Builds: section 5 (all four across 23f37a0e and its merge with main).

WORKER22's REPORT.md (`sessions/WORKER22/`) still lists AgTest as 206 of 206 and W22-16 open: this report supersedes
its section 1 row C6 and section 9.

## 2. The freeze check's task (tests/freeze/, PLAN-V135 step 4)

| change | why |
|---|---|
| **sort-split6**: `bench_sort --keys K --threads 6 --partition tiles --reps 1` against `--threads 1` on the same keys; PASS when both exit 0, both spans are 1,229,687 and the digests (the machine at L2's all-READY) are equal | Quest 186 stopped at once: test_sort (protected) splits only in row bands (1, 2, 4 or 8). `--partition tiles` is Quests 157/158's: each tile a domain, 64 domains grouped in order into 6 engines (declared placement, not by load). sort-one stays on test_sort's every fingerprint point |
| **TIMES**: `chain-evm`, `chain-cyclone`, each from its own job's end | the one `chains` line was taken when the wait loop reached the chains, after run_tests.sh, so it included that wait whenever the chains finished first |
| **toolchain** (owner): `toolchain.txt`, appended to `machine.txt`: riscv64-unknown-elf-gcc's and binutils' versions, the three packages' dpkg versions, picolibc's `__PICOLIBC_VERSION__`, the image builds' locale; verdict `toolchain` | the runner now rebuilds the firmware images |
| **run_tests.sh under LC_ALL=C** | the image builds' locale (Quest 188: nm orders the .sym files by the locale) |

**The freeze task now depends on bench/w10's `bench_sort`** (built by the task beside test_sort): P8's cleanup must keep
`bench/w10/bench_sort.cpp` and its rule (`bench/w10/w10.mk`).

### Quest 189 (sim-w27 1903be13; i5-14400F, the machine record in its results)

| item | verdict | detail | time |
|---|---|---|---|
| machine, toolchain | PASS, PASS | gcc 13.2.0 (13.2.0-11ubuntu1+12), binutils 2.42 (2.42-1ubuntu1+6), picolibc 1.8.6 (1.8.6-2); LC_ALL=C | |
| build | PASS | | 13 s |
| sort-one | PASS | 321 points equal; TOTAL 1,229,687; phases 98 | 37 s |
| sort-split6 | **PASS** | span 1,229,687 at both; digest 3d87d546fb080239 at 1 thread and at 6 by tiles | 148 s |
| t2 (evm, cyclone; 1 and 6 domains) | PASS ×4 | 241 points, 0 with a difference, lag 0; rows and exits 64 of 64 | 23 s |
| run_tests | **FAIL** | everything passed; one `LOG DIFFERS`: bus (below). 10 PENDING (P5 8, fw/build.sh 2) | 748 s |
| chain-evm, chain-cyclone | PASS, PASS | 128 of 128 each | 1085 s, 1066 s |
| calibration | PASS | stages A and B 128 of 128 | 39 s |

**The one failure, outside P4 (for the integrator):** run_tests.sh's `bus` step takes its reference from
`$V13/bus.log` (`ref/meshsim/results/bus.log`), which a stand-alone kit doesn't have (`sim/ref` is excluded; D39):
"LOG DIFFERS (bus): no reference log ref/meshsim/results/bus.log". BusTest itself passes (154 of 154). Quest 186 ran
before that step existed. The fix: a fixture in `tests/` regenerated from ref (as every other suite's), cited by the
step. Not in my files.

**PENDING, 186 to 189: 18 to 10.** Gone: agtest and agtest-shuf (P4), and six that main's other merges made live
(cccores, its shuffled run, bus, S1, accounting, accounting-full). The 10 left: P5 (8) and fw/build.sh (2,
sw-build and sort-build). None cites P4.

## 3. P4's closing check

`LC_ALL=C ./run_tests.sh quick --check` (this container, 1903be13): "all passed", exit 0, no LOG DIFFERS, 9 PENDING
(P5 7, fw/build.sh 2), none P4. Its log had sed's "can't read" lines at cccores, which led to the next section.

## 4. run_tests.sh's mask() (23f37a0e)

`mask()` appended a step's own mask unquoted: `'...'${MASK_EXTRA:+; $MASK_EXTRA}`. The shell split D44's expression
into words that sed took as file names; sed then read none of its input, so both sides of the diff were empty and
**cccores and cccores-shuf were compared with nothing**. Now `'...'"${MASK_EXTRA:+; $MASK_EXTRA}"`. Shown with the
real mask() and step() and the two cccores lines:

| fixtures | mask() | LOG DIFFERS |
|---|---|---|
| as committed | fixed | 0 |
| one line altered in each ("77 passed" to "78") | fixed | **2** (cccores, cccores-shuf) |
| the same altered ones | main's old line | 0, and sed's errors |
| D44's figure altered in each ("dump done at +999") | fixed | 0 (the mask works) |

Fixtures restored. Both steps' outputs equal their fixtures under the mask applied properly, so the old line hid
nothing; no other step sets MASK_EXTRA.

## 5. Builds

| commit | clang debug | gcc debug | gcc release | clang release |
|---|---|---|---|---|
| 1903be13 | | 74 tests, suite_bus, check_copies | | 74 tests |
| 23f37a0e | 74 tests, suite_bus 154 | (container restarted mid-run) | 74 tests | |
| 788035df (main 9f7f3cd5 merged) | | 75 tests (test_checkself new), suite_bus, check_copies | | 75 tests |

## 6. Files

| file | what |
|---|---|
| `tests/suite_ag.cpp` | the workaround replaced by `TileRef::store` at the cycle just run |
| `tests/test_p4suites.cpp` | two cases |
| `run_tests.sh` | (shared) agtest's two step lines and SUITES line; mask()'s quoting |
| `tests/freeze/body.sh`, `make_task.sh` | sort-split6, the chains' times, the toolchain record, LC_ALL=C |
| `planning/sessions/WORKER27/` | NOTES.md, this report |
