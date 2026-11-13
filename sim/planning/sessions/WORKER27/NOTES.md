# WORKER27 notes: P4's close and the freeze check's task (after WORKER22)

## The plan (agreed with the owner, 2026-11-13)

1. AgTest's last 6 (W22-21 (a)-(c), the harness fix on main at 401534eb): (a) and (b) need nothing in the suite; for
   (c), the table write between cycles (`store_ptr_at_read`, a `set_ptr`) replaced by `TileRef::store` at the cycle
   just run, as ref's `core.store(tr, ...)`. agtest and agtest-shuf live in run_tests.sh (new lines in place of the two
   `pending` ones; `suite_ag` on a new SUITES line) and in make check (two cases in test_p4suites).
2. The freeze task's sort on 6 threads: test_sort (protected) splits only in row bands (1, 2, 4 or 8), so sort-split6
   runs bench/w10's bench_sort with Quests 157/158's partition (`--partition tiles`: each tile a domain, 64 domains
   grouped in order into 6 engines, declared placement), against a one-thread bench_sort on the same keys: PASS when
   both digests (the machine at L2's all-READY) are equal and both spans are 1,229,687. sort-one stays on test_sort's
   fingerprint points.
3. TIMES's chains line: it was taken when the wait loop reached the chains, after run_tests.sh, so it included that
   wait whenever the chains finished first. Each chain now writes its own end time in its job; one line per workload.
4. (owner, added) The runner rebuilds the firmware images: the freeze record shows the toolchain's versions
   (riscv64-unknown-elf-gcc, binutils, picolibc: the dpkg versions where there are packages, and picolibc's own
   `__PICOLIBC_VERSION__`) and the image builds' locale. run_tests.sh runs under LC_ALL=C (Quest 188: nm orders the
   .sym files by the locale).
5. (owner, added) run_tests.sh's `mask()`: `${MASK_EXTRA:+; $MASK_EXTRA}` was unquoted, so D44's expression was split
   into words that sed took as file names ("sed: can't read ..."): both sides of the diff empty, the cccores steps
   compared nothing. Quoted; shown compared by an altered fixture line.

## Log

- 1903be13: (1)-(4). AgTest 240 of 240, plain and shuffled, byte for byte, and the same under step-all and
  instance-major. gcc debug (74 tests; test_p4suites case by case, 17 of 17; suite_bus; check_copies) and clang
  release (74) clean. Merged into main by the integrator.
- Quest 189 claimed (NUMBERS re-read first) and its task pushed: the rehearsal from sim-w27 1903be13.
- `LC_ALL=C ./run_tests.sh quick --check` (this container, 1903be13): all passed, no LOG DIFFERS, 9 PENDING (P5 7,
  fw/build.sh 2), none P4. It printed sed's "can't read" lines at cccores: (5).
- 23f37a0e: (5). With the real mask() and step() and the two cccores lines: the fixtures as committed, 0 LOG DIFFERS;
  one line altered in each fixture ("77 passed" to "78"), 2 LOG DIFFERS; the same altered fixtures under main's old
  line, 0 LOG DIFFERS and sed's errors; D44's figure altered in both fixtures, 0 (the mask works). Fixtures restored.
  By hand, both steps' outputs equal their fixtures with the mask applied properly, so the old line hid nothing.

- 788035df: main 9f7f3cd5 merged (W28's reset, CondTest live; every side's run_tests.sh lines kept). gcc debug (75
  tests, suite_bus, check_copies) and clang release (75) clean; clang debug and gcc release clean on 23f37a0e.

## Working in this sandbox

- Background jobs die at the end of each command; `setsid nohup ... < /dev/null &` survives the command but not the
  turn (the container restarts between turns; /tmp and the checkout persist): runs resumable by marker files.
- One CPU: a full four-build pass is long; test_p4suites (17 cases) and test_sort_suites run case by case with the
  test framework's filter (`./build/X/test_p4suites CASE`) when a command's 300 s limit is near.
