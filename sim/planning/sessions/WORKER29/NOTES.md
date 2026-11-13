# WORKER29 notes (CtrlTest's last groups, the checker for what it needs, periph-shuf)

Branch `sim-w29`, from main 5694f93a. Takes over CtrlTest from W23 and the checker (CheckSelf) from W26.

## The plan (go-ahead from the owner, 2026-11-13)

0. CheckSelf split out of `tests/test_mach.cpp` into `tests/test_checkself.cpp` (its own entry in `tests/mach.mk`), the
   code unchanged, output equal before and after; MachSelf and NetSelf stay in test_mach (W28's).
1. CtrlTest's `invariants`; the checker kinds it provokes (4, 5, 8 strict, 9, 20, 21) checked line for line through
   cases appended to `oracle/ref/CheckSelf.java` and `test_checkself.cpp`, `checkself.out` regenerated from ref. If its
   hard reset meets W28's reset bug, that part waits and the all-to-all goes first.
2. The all-to-all (quick: uniform 2000; full: the four sets).
3. `faults`, after W28's reset fix; harness gaps (the request bus's idle, a tile's request port idle, the core's
   parked, C&C 1's unit-error bit and toast) to the integrator.
4. My PENDING lines become steps: ctrl, ctrl-shuf (and make check, through test_ctl_suites), ctrl-full. W27's freeze task
   runs ctrl-full; a Quest task of mine is an early check only, skipped if the runner is busy.
5. periph-shuf: `test_tileslotconf` takes a seed (harness-only, off by default); the step runs seed 4 and requires
   output identical to the plain run (our seeds don't map to ref's).
6. The closing check (`./run_tests.sh quick --check`: "all passed", no LOG DIFFERS, none of the four PENDING); REPORT.md.

Shared files: test_mach.cpp, test_ctl_suites.cpp, ctl.h, ctlsuites.mk (W28's): my own entries only; run_tests.sh and
make check: only my own lines. gcc debug and clang release before every push; the four builds every two or three.

## Progress

- **Step 0**: `tests/test_checkself.cpp` (CheckSelf's sequence moved verbatim, with its own copies of `say`/`file`),
  `$(B)/test_checkself: $(MACH_LINK)` in mach.mk; make check picks it up (tests/test_*.cpp). gcc suite: test_mach's
  seven other tests and test_checkself's one print exactly what the eight printed before.
  gcc debug 75 of 75 (test_p4suites and test_sort_suites case by case with the filter; suite_bus 154 passed) and clang
  release 75 of 75.
- **Invariants** (`suite_ctrl.cpp`): ref's group, 152 checks; every line in ref's ctrl.log and ctrl-shuf.log (gcc suite).
  Its hard reset with a packet in flight passes (not the soft-reset bug). **CheckSelf**: kinds 4, 5, 8 strict, 9 and
  the stream/packet sides of 20 and 21 were reached already (W26's inv cases); appended: the `regs` program's 384
  violations (the counter wraps, ROUTER_HDR's and WRITE_COUNTER_2's reserved bits, the odd DMA_RECORD_BASE_ADDRESS), the
  overlay after a command and invariant 25, both from C&C 1's model; checkself.out regenerated from ref (389 lines
  added, the rest unchanged), test_checkself line for line.
- **The all-to-all**: quick and full equal to ref's logs, plain and shuffled (full: 74 s here, gcc suite). The output
  is ref's but for the faults line and the totals (6,468 of 6,954; faults has 486).
- **Faults** waits for W28 (after its reset fix): the request bus's idle, a tile's request-port idle, C&C 1's toast
  latch on the model machine. What exists: the core's parked/halt/trap_cause, Cnc's error_bit, ReqBus with
  reqbus_host, Cc::reset, resets, last_reset_at, clear_violations. ctrl, ctrl-shuf, ctrl-full stay PENDING until the
  log is whole; in make check they will run in the debug builds only (the registers and invariants groups need the
  checker).
- Builds before the push: gcc debug and clang release, every test binary (test_p4suites and test_sort_suites case by
  case), all passing; suite_bus 154 passed.
- **periph-shuf live**: `TILESLOTCONF_SEED=<n>` (unset by default; the test harness's argv[1] is its filter) runs only
  test_tileslotconf's shuffled run with that seed against the same observations; the step runs seed 4 and requires its
  output byte-identical to the plain run's (`results/periph.out`); checked that it fails when they differ.
- Main merged (9f7f3cd5: W28's reset, W27's AgTest); the four builds on the merged tree: 93 of 93 test runs each (the
  two long tests case by case), suite_bus 154 passed in both debug builds.
