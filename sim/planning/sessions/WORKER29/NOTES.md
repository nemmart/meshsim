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
