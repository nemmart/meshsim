# WORKER29 report (CtrlTest's last groups, the checker for what it needs, periph-shuf)

Branch `sim-w29`. Took over CtrlTest from W23 and the checker (CheckSelf) from W26. Details and the plan in `NOTES.md`.

## What was delivered

- **CheckSelf in its own binary**: `tests/test_checkself.cpp` (its own entry in `tests/mach.mk`), moved verbatim out of
  test_mach so each stays inside the tool limit in the debug builds; output equal before and after.
- **CtrlTest complete** (`tests/ctl/suite_ctrl.cpp`): `invariants` (152 checks), the all-to-all (quick, and the four
  data sets in full) and `faults` (486 checks, on W28's request-bus idle, request-port idle, toast latch and several
  receives a cycle) added to W23's groups. **ctrl and ctrl-shuf (6,954 checks) and ctrl-full (6,963) are byte-identical
  to ref's logs**, gcc and clang suite builds. ctrl-full takes 74 s here (one CPU); no runner task was needed.
- **The checker for CtrlTest**: kinds 4, 5, 8 strict, 9 and the stream/packet sides of 20 and 21 were already reached
  by CheckSelf (W26). Appended, line for line against ref (`checkself.out` regenerated from ref, the existing lines
  unchanged): the `regs` program's 384 violations (the counter wraps, ROUTER_HDR's and WRITE_COUNTER_2's reserved bits,
  the odd DMA_RECORD_BASE_ADDRESS), an overlay landing after a command (6.2), and invariant 25, both from C&C 1's model.
- **Steps live**: ctrl, ctrl-shuf (quick), ctrl-full (all); ctrl and ctrl-shuf in make check in the debug builds only
  (registers and invariants need the checker's facts; 5 s in gcc debug).
- **periph-shuf live**: `TILESLOTCONF_SEED=<n>` (unset by default; the test harness's argv[1] is its filter) runs only
  test_tileslotconf's shuffled run, against the same observations of ref; the step uses seed 4 (v1.3's) and requires
  its output byte-identical to the plain run's (our shuffle seeds don't map to ref's).

## The closing check

On main c96c541d merged: `./run_tests.sh quick --check` ends "all passed", no LOG DIFFERS; ctrl, ctrl-shuf and
periph-shuf are steps; cccores and cccores-shuf compared (after the mask fix) and equal; ctrl-full's step with
`--check` equal too. The PENDING left are recipes and recipes-shuf (RecipeTest's boot, not in this scope). The four
builds: 93 of 93 test runs each (test_p4suites and test_sort_suites case by case with the filter), suite_bus 154 passed
in both debug builds, check_copies clean. The protected tests unchanged (no unit was edited).

## Findings

- Faults' restart group passed before W28's reset fix (the reboot a cycle early): its checks read the state after
  every tile has stopped, so a one-cycle shift there doesn't show in CtrlTest's log.
- In gcc and clang debug, test_p4suites and test_sort_suites each run past 300 s whole; case by case with the filter
  they pass (the integrator's ruling: no split).

## What's left

Nothing in this scope.
