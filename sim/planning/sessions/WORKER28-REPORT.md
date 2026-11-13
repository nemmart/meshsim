# WORKER28 report (the reset, CondTest, RecipeTest's boot, the harness for CtrlTest's faults)

Branch `sim-w28`, from main; every push merged into main by the integrator (last: fb411007). Took over the machine
harness (`tests/mach.*`, MachSelf) and the reset from W20, and CondTest and RecipeTest's boot from W23. Every item below
is checked against `ref`: a fixture regenerated from `ref` (MachSelf through `oracle/ref/machself.sh`) or v1.3's own
suite log, byte for byte.

## The closing check

On main f2bc6109 merged (fb411007): `./run_tests.sh quick --check` ends "all passed", no LOG DIFFERS, no FAIL and **no
step PENDING** (cond, cond-shuf, recipes and recipes-shuf are steps, as are ctrl and ctrl-shuf from W29). gcc debug and
clang release pass every test (test_p4suites, test_sort_suites and test_ctl_suites case by case under the tool limit);
the four builds every two or three pushes. The five protected tests (test_chip, test_sort, test_t2_harness,
test_cc_units, test_golden) were compared with main's outputs (timings masked) at every push: unchanged throughout.

## 1. The reset: the router overflow surviving it

CondTest's error 0 (tiles 8, 10, 1 and 17 flood tile 9) failed one check after the 9th reset: tile 9's ERRORS [0] back.

- **The field: the router's `ovf_due`.** The router counts an overflowing put at its cycle t and reports it (the Count
  and the event to the register block, D29) in its next step. ref's reset flushes the FIFO before that put is committed
  (`Bus.flush` drops a pending put), so an overflow of the reset's own cycle is never counted there. Ours kept the
  pending count: the router's first step after the reset raised ERRORS [0] at reset + 1.
- **The 61 cycles** in the report were not when the bit rose: it rose at 6138 (the reset at 6137), the error line
  reached C&C at 6146, and 6198 was the first cycle after C&C's queue finished (6197), the first point the suite looked
  (its check at 6247 is 6197 + 50).
- **The fix: what ref clears, cleared; what it keeps, kept** (`WordFIFO.flush`: head, count, the overflow flag and its
  cycle cleared, `overflowCount` kept). In `Chip::reset`: each router FIFO's overflow of the reset cycle uncounted, its
  flag cleared, its count kept, `ovf_due` cleared. `Sim::flush_ports` now keeps the framework FIFOs' overflow counts and
  clears only the flag (it zeroed the counts, which could have raised a false ERRORS [4] or [6] after a reset, the write
  engine and gather comparing against their last-seen counts). The write engine's and gather's last-seen counts are set
  at the reset (ref's `TileControl.reset` copies the counts into `evPrev`). The checker's invariant 1 reports on the flag.
  One line in `router.h`: the overflow cycle set when the flag is clear (the same without a reset).
- **MachSelf's floodReset** (both seeds): error 0 from a fresh machine, a FIFO overflowing in the reset's own cycle,
  through the reboot. Without the fix it reproduces the failure; with it every figure equals ref's. Its between-cycles
  read of the overflow count leaves out the cycle just run (D28: ref counts lazily, at the FIFO's next touch).

**Recorded, not changed:** whether ref reports an overflow made in the last cycles before a reset depends on its lazy
count (D28) and its scan every 16 cycles; v1.35 reports it. No suite reaches it.

## 2. The reset: the reboot a cycle early

floodReset showed the boot again finishing at 1078 against ref's 1079 (CtrlTest's faults 5235 against 5236).

- **C&C's queue.** ref's `cc.reset(false)` is three steps: the reset (cycle A), `waitCycles(2)` (A+1, A+2), and setting
  READY_EXPECT, a register action of its own (A+3); the next command leaves at A+4. The harness's form set READY_EXPECT
  with an instant call, so the command left at A+3. **Fix:** a step kind in C&C's sequencer, `CALL_STEP` ("a call that
  takes its cycle"), unused unless the harness queues it; `Cc::reset` queues it. The queued-step count equals ref's at
  every cycle (MachSelf's existing reset case unchanged).
- **STATUS at the reset's cycle.** ref keeps STATUS as computed at the end of the reset's cycle until the next end of
  cycle; the register-block unit gains a harness-only hold (`reset_status`, `reset_status_at`) that the end-of-cycle
  preview returns for that cycle.
- **D49 (the integrator's, a deliberate difference).** ref calls `cc.errorLine(t, err)` after `doReset`, with `err`
  computed before the reset: with a tile's error line high at the reset, C&C's model gets one more high level and its
  error bit latches about the error latency later. No v1.3 suite reads it. floodReset's whole-chip fingerprint at the
  reset, the next cycle and after the reboot leaves out C&C's model's error-line words, its comment citing D49.

## 3. CondTest (cond, cond-shuf)

W23's port (b30e95df, cherry-picked). With the reset fixed, 153 of 153 and byte-equal to v1.3's logs, plain and
shuffled. Steps live; in make check in the debug and suite builds (the checker's messages are part of its log). Its
toast pin reads the harness's toast latch (below).

## 4. RecipeTest's boot (recipes, recipes-shuf)

The `boot` group ported: CMD_STAGE with 16 broadcast rows, the handler's 24 reports to C&C 1, a soft reset and the boot
again. **2,056 of 2,056 checks, byte-equal to v1.3's logs, plain and shuffled**, in the suite, release and debug builds.
Steps live; in make check in every build.

- **Harness fix: C&C's drained lists as ref's between cycles.** The harness drains C&C 1's receive queue for cycle
  t + 1 between cycles, so `reports_of()` returned a report one cycle before ref's list holds it, and boot's run stopped a
  cycle early (454 against 455). `reports()`, `reports_of()`, `requests()` and `results()` now return what was drained at
  or before the cycle just run. Their callers (RecipeTest, NetSelf's drain case) unchanged; CcCores, CcBench, S1Bench,
  Accounting, CondTest and CtrlTest don't call them.

## 5. The harness for CtrlTest's faults (W29)

All off by default, each checked by MachSelf's faultsReads against ref (CtrlTest's faults image):

- `ReqBus::idle()` (ref's `reqBus.idle()`), `TileRef::req_idle()` (ref's `p.req.idle()`), `Chip::toast()` and
  `toast_at()` (ref's `cc1p.toast`, `toastAt`; a latch in C&C's model, cleared by the reset).
- **Several receives of one target in a cycle** (ref's `RequestBus.receive`): the further pops ride with the cycle's
  first, their credits at the root the next cycle. The fingerprint counts the test's pops and now reads the receive
  queues of a test-driven bus (it wrote them as empty); the reset resyncs the harness's pop count.
- `ReqBus::received(target)`.

faultsReads drains 112 reports in one cycle, as ref's faults does; **its whole-chip fingerprint equals ref's at every
cycle from 0 to 5,000**, and at the drain and the two cycles after.

## 6. A parked core runs its park loop (model)

Found by faultsReads' fingerprints: ref's core keeps executing the boot ROM's park loop (`j .`) every cycle after a
fault (only a halt stops `Controller.runCycle`), so its `stall` alternates and `instructions` counts; ours stopped
stepping a parked core, so 35 of 64 parked cores showed a different `stall`. Nothing architectural differed. **Fix**
(`rv32.h`): a parked core goes on running; `parked` is still set only by the handler's park loop and stays set; the
run's end is still recorded at the park (`stop`, `stop_cycle`), so test_golden's lines are unchanged. test_rv32's
park-loop case updated to ref's behaviour. CondTest, CtrlTest (faults included) and RecipeTest byte-equal, plain and
shuffled. (Tile 60's big-SRAM hash, seen once at faults' drain, does not reappear: equal at every cycle.)

## Model code changed (each approved first; protected tests unchanged)

`src/units/router.h` (the overflow cycle), `src/config.cpp` (`flush_ports` keeps the counts), `src/units/cnc.{h,cpp}`
(`CALL_STEP`, the toast latch), `src/units/reqbus.h` (the further pops of a cycle), `src/units/tile_ctl.h` (the
STATUS hold), `src/units/rv32.h` (the park loop).

## Left

Nothing queued. D28's reporting corner at a reset (section 1) and D49 (section 2) are recorded differences; no v1.3
suite reaches either.
