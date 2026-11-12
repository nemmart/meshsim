# WORKER23 notes (P5: the control suites, programs and recipes)

Branch `sim-w23`, from main 67d42fb. Rows C8, C10, C12, C13, C14, C15, C18, G2, G3.

## The plan (go-ahead from the owner, 2026-11-12)

**Files I own.** `sim/tests/ctl/` (one program per v1.3 suite, named by its step: `suite_ctrl`, `suite_cond`,
`suite_hooks`, `suite_cccores`, `suite_recipes`, `suite_accounting`, `suite_regdoc`, `bench_s1`, `bench_cc`; on W20's
`suite.h` and `mach.h`), `sim/tests/ctlsuites/` (the fixtures, with `PROVENANCE`), `sim/tests/ctlsuites.mk` (the
rules), `sim/oracle/ref/ctlsuites.sh` (the Java side). Shared files, through the integrator: one `include` line in the
Makefile, the steps in W20's `run_tests.sh`.

**How each is checked** (the prompt's rule): `ctlsuites.sh --fixtures` runs v1.3's step exactly as `run_tests.sh` does
(`java <Suite> <args> 2>&1`), in a scratch copy of `ref/meshsim`'s layout with v1.35's committed images (as W20's
`machself.sh`; the images are byte-identical to v1.3's, W19), and commits the log; `--check` regenerates and compares.
The C++ suite's stdout+stderr must equal it byte for byte (none of these logs has a timing line), plain and shuffled
where `ref` shuffles; `make check` runs the quick ones in the debug builds (shuffle, both orders, step-all) too.

| v1.3 step | suite | where | needs from W20 (beyond main today) |
|---|---|---|---|
| cccores, cccores-shuf | CcCoresTest -1 / 4 | make check | none expected; counters below |
| ccbench | CcBench | runner list (full) | as cccores |
| ctrl, ctrl-shuf; ctrl-full | CtrlTest -1 / 4; -1 full | make check; full on the runner | the checker's kinds `invariants()` provokes; the request bus from the harness (target 0's pending, peek, receive, get, idle) |
| hooks, hooks-shuf | HooksTest -1 / 4 | make check | **a core load/store from the harness** (ref's `p.core.load/store(cycle + 1, addr, ...)`, the bus-error result) |
| regdoc | RegDocTest | make check | the same load/store; RegDoc's rows from W19's `tools/regs` table |
| cond, cond-shuf | CondTest -1 / 4 | make check | reset (soft), `Cc::reset` as a queued step, the kinds for invariants 1, 3, 4, 5, 8, 9, 15; the request bus as above |
| accounting; accounting-full | AccountingTest -1; -1 full | make check; full on the runner | PhaseLog (step 4); the CSVs byte-identical |
| recipes, recipes-shuf | RecipeTest -1 / 4 + SelfTest | make check | `wait_until` (below) |
| s1bench | S1Bench | runner list (full) | S1's measurement (`measure_s1`: late, sram/ptr/hist max, `report(K)`) |

**Order** (so I never wait on W20): 0. the fixtures (all steps; the two full ones and the benches on the runner if
they are too slow on this one-CPU container); 1. CcCoresTest and CcBench; 2. CtrlTest; 3. HooksTest and RegDocTest;
4. CondTest; 5. AccountingTest; 6. RecipeTest; 7. S1Bench; then the full runs on the runner (Quest) and the REPORT.
Commit and push after each. Statistics a suite reads that a C++ unit doesn't keep (ctl: counterWraps, eventWakes,
readies, gos, spuriousEdges, reservedAccesses, wfiLog, errorsAt ...; core: firstImageAt, parked, inHandler, trapCause;
ag: startedAt, firstRequestAt, doneAt; cc1p: gos, serialOut) are added to the units as counters only, each marked, as
mach.h allows; I list them for W20 before adding any to a unit W20 is editing.

**Fingerprints (item 4):** CtrlTest, CondTest and RecipeTest boot many programs; where a log mismatch doesn't say
where, a per-program StateFp at the end of each program's run (against an `oracle/ref` driver), as M2Prog did. Only
when needed.

## Item 2: CnC's `waitUntil` (to settle with W20 through the integrator)

Only RecipeTest uses it among my suites (four times, always `waitUntil(() -> tilesIdle(m)).call(...)`; CondTest does
not, despite the prompt; SortTest does, W24's). In `ref` the predicate runs inside C&C's step. **Proposal:**
`Cc& wait_until(std::function<bool()> cond)`, a queued step that the harness evaluates between cycles (D6), before
each cycle, on the state at the end of the cycle before; C&C passes the step in the first cycle whose check held, as
`ref`'s C&C does if its step reads the tiles as of the cycle before. W20 places it in the cycle as `call` and
`at_all_ready` are; RecipeTest's cycle table (and SortTest's) is the check.

## Item 3: the recipe book's Python (the integrator decides before I build it)

- `regions.py` is used only by the other two. `check_book.py` duplicates RecipeTest's own snippet check (its group
  "snippets"), which I port to C++ in `suite_recipes` anyway (it is in the log). `gen_book.py` makes `fw/RECIPES.md`
  from `RECIPES.tmpl.md` and the sources (checked now: it reproduces `fw/RECIPES.md` byte for byte).
- **Proposed (A):** port. The region extractor in one header shared by the suite and a small C++ `gen_book` (about 150
  lines in all, half a day); `make check` checks `gen_book`'s output equals `fw/RECIPES.md`; `check_book.py` and
  `regions.py` dropped as superseded by the suite's check. No Python left in `fw/`.
- **(B):** keep the three unchanged as optional tools (no cost, python3 an optional prerequisite for regenerating the
  book only; the suite checks the book either way).
- To explain while porting: `check_book.py` counts 47 blocks, RecipeTest 46 (different patterns).

**The same question for RegDocTest's `tools/header_diff.py`** (74 lines, Python plus gcc): RegDocTest asserts on its
output, and `ref`'s log has it run (no "skipped" line), so keeping Python makes python3 a prerequisite of `regdoc`'s
log equality. **Proposed:** port it to a shell script over the host gcc (`gcc -E -dM`, a generated C program for the
macro values and `offsetof`), its output byte-identical to `header_diff.py`'s on mesh.h and the draft (about 120
lines).

## Questions for the integrator

1. Item 3's two choices (the book; header_diff).
2. The W20 operations above: the harness core load/store, the request bus from the harness, `Cc::reset`,
   `wait_until`; and the timing of the checker kinds, reset, PhaseLog and S1 against my order.
3. The Makefile `include` line and my steps in `run_tests.sh` (W20's, not on main yet): who adds them, when.
4. `cc-build` and `recipes-build`: v1.3's build steps; in v1.35 both are `fw/build.sh` (W19). One `fw-build` step, or
   W20's runner keeps v1.3's two names?

## The owner's answers (2026-11-12)

1. The book: port `gen_book` to C++ (output byte-identical to `fw/RECIPES.md`), drop `check_book.py` and `regions.py`,
   **on condition** that this file lists what `check_book.py` checks and shows the RecipeTest port makes each check.
2. `header_diff.py`: a shell script over the host gcc, output byte-identical to the Python's (D40).
3. The four W20 needs are parked for W20 (with W24's and W25's). `wait_until` as proposed (checked between cycles;
   C&C moves on in the next cycle), one form for RecipeTest and SortTest.
4. I add my own Makefile include and runner steps and tell the integrator. **v1.3's step names stay**: `cc-build` and
   `recipes-build` separate, each running its part of `fw/build.sh`.

## Progress

- **Step 0 done**: `oracle/ref/ctlsuites.sh` (`--fixtures`/`--check`, quick, full or named steps): sim/ref copied to a
  scratch tree, v1.35's committed images in `meshsim/sw/build/`, each step run as run_tests.sh runs it. Every step of
  my rows, quick and full, made locally (one CPU; ctrl-full the longest, 238 s), `--check` reproduces all but ctrl-full
  identically (ctrl-full not rerun yet). Fixtures in `tests/ctlsuites/` (16 logs, the two phases CSVs, PROVENANCE).
  JDK 21.0.13 (apt; the container had a JRE only) and clang 18.1.3 installed.
- **Finding**: v1.3's committed `results/*.log` are stale for my steps (ctrl 6,893 checks there, 6,954 now; cond
  141 / 153; hooks 142 / 141; recipes 2,042 / 2,056): the fixtures are regenerated from `sim/ref`, never taken from
  `results/` (as W20's F1).
- **Approved (integrator, 2026-11-12):** three statistics in `src/units/rv32.h`, ref's Controller fields the suites
  read: `trap_at`, `trap_cause` (the last trap; -1 before any) and `first_image_at` (the first fetch from program memory
  since the reset). Counters only, set in `trap` and `fetch_execute`; not in the digest or the trace. W20's reset must
  set them back to -1 (ref's Controller.reset).
- **W20 requests 9 and 10** (parked by the integrator): ERRORS bits set from the test after the block's endCycle (ref's
  `ctl.conditions |= bits`), and `checkErrors` switched off mid-run. Stand-ins in `tests/ctl/ctl.h`, marked STAND-IN.
  **A suite that passes only through a stand-in is not counted as matched.**
- **Step 1, CcCoresTest** (`tests/ctl/suite_cccores.cpp`, `tests/ctl/ctl.h`, `tests/ctlsuites.mk`,
  `tests/test_ctl_suites.cpp`): clang release, 77 passed, the log equal to ref's but for one figure in errorPath's
  printed line: "dump done at +770026", ref +770025. errorPath is the one group using both stand-ins (the ERRORS bits
  possibly a cycle early): **not matched** until W20's 9 and 10 replace them.
- **F2. CcCoresTest reaches D33** (C&C 1's peripheral applies stores a cycle later than ref, "a UART byte's start runs
  one cycle later"; D33 says no reference run reached it). errorPath's printed line "dump done at +770026" (ref
  +770025, plain and shuffled) is the end of C&C 1's UART dump; every other figure and all 77 checks are equal.
  Evidence that it is D33 and not the stand-ins: injecting the ERRORS bits a cycle later leaves every figure unchanged
  (trapped +10, drain +4800, dump +770026). **For the integrator:** D33 is now reached by a reference run; accept the
  figure as D33's recorded difference (the fixture line, or a masked comparison, approved), or make the UART's start
  exact. Until then `cccores` stays PENDING in the runner and its make-check tests are off (`kCcCoresMatched`).
- **The runner** (`run_tests.sh`): `cc-build` and `recipes-build` are steps, each `fw/build.sh --check` on its part
  (`PARTS=cc`, `PARTS=recipes`; W23's addition to fw/build.sh: `part NAME` around each section, `cd "$FW"` after each,
  the full build unchanged: 64 + 93 images equal, as W19's). Without the RISC-V toolchain (the runner) the step logs
  "skipped" (no FAIL, no PENDING). The logs have no reference (toolchain output). accounting, accounting-full, s1bench
  and ccbench were labelled P6 in the runner's PENDING lines: relabelled P5 (W23), my rows (C10, G2, G3).
- Not in my rows but labelled P5 in the runner: riscv (C9), periph (C16), reqbus (C17, P4's list). Left as they are.
- **Step 1, CcBench matched** (`tests/ctl/suite_ccbench.cpp`): log-equal to ref's, clang release and the suite build
  (the checker on). Its GO cycle is ref's: ref counts PULSE_GO in the store's cycle; C&C 1's peripheral here applies
  the store at t + 1 (D33) and keeps the store's cycle as `last_go_store`, which the suite reads (the model's
  `last_go_at` likewise). Polling `gos` instead shows GO a cycle late (+21/+23 for ref's +20/+24) while the tiles start
  at ref's cycle: D33 again, in a statistic only. In the runner (step `ccbench`, full mode) and in make check (fast).
- CcCoresTest in the suite build: errorPath counts the injected ERRORS as a violation (tile 9's, invariant 15; C&C 2's
  isn't reported), as expected while request 10's stand-in does nothing.
- Checks so far: test_ctl_suites, suite_cccores, suite_ccbench on clang and gcc release and the clang suite build; the
  protected test_golden, test_controllers, test_chip pass with the rv32 statistics (clang release). The debug builds
  (ASan, UBSan, the debug checks) are run with the next full make check.

## Step 2, CtrlTest: what it needs (read through, 2026-11-12)

CtrlTest is the widest of my suites. Beyond main today it needs, from W20 (through the integrator):
- **Cc::phase_log** (CnC.phaseLog: {all READY, GO store} per GO; FATAL in mach.cpp "with step 4"): commands, the a2a.
- **reset**, soft and hard, as C&C steps (`cc.reset(false)`, `.reset(true)`), `resets`, `last_reset_at` (faults, the
  hard reset with a packet in flight); **requests 6, 7**.
- **the request bus from the harness** (target 0's pending, get(word), receive, idle; a tile leaf's idle): faults;
  request 6.
- **config switches mid-run**: check_errors (request 10), strict_replies, max_violation_messages; and clearing the
  violation count and messages (`m.c.violations = 0`; only `clear_violation_messages` exists).
- **the checker's kinds** CtrlTest provokes: invariants 4, 5, 8 (strict), 9, 20, 21 (ROUTER_HDR's and WRITE_COUNTER_2's
  reserved bits), 25, the reserved register-block word, "address generator is running", the overlay after the first
  command, the counter wraps, the eight ecalls; with ref's texts.
- **Cc::send** (exists) and `Chip::quiescent` (exists); **chip.agReport()** (ref's per-run AG report, the a2a's lines).
- **the boot ROM's size check** (Controller.checkBootRom: a 65-word image refused, `throws`): a C++ equivalent.
Statistics I add to units myself (counters only, as rv32's), unless W20 prefers to: TileCtl `gos`, `readies`,
`event_wakes`, `spurious_edges`, `counter_wraps` (exists), `last_done_at`, `last_landed_at`, `ready_pulse_at`,
`go_visible(t)`; the controller memories' `writes_cmd`, `landed_at`, `overwritten_unread`; the AG's `started_at`;
the core's WFI log (`wfiLog`: {enter, leave, cause} per WFI; a fixed array of the first 8, D24).
Order inside CtrlTest: fields, boot, registers, commands, wfi, blockCopy and the ROM size first (on what exists plus
the statistics); faults, invariants and the a2a's tables as W20's pieces land.

## D44 (2026-11-12)

- D33's UART cycle in CcCoresTest accepted (D44): the fixture keeps v1.3's line; `test_ctl_suites` and the runner
  mask "dump done at +N" (run_tests.sh's `MASK_EXTRA`, `D44_MASK`; a step's own approved mask).
- **cccores, matched with a stand-in**: log-equal (D44's mask) in the release builds, plain and shuffled; its make-check
  tests are on there. With the checker on (debug, the suite build) errorPath's injected ERRORS count as a violation
  until W20's requests 9 and 10 replace the stand-ins: the make-check comparison is skipped in those builds and the
  runner's steps stay PENDING ("W20's requests 9 and 10"), the two step lines ready in a comment.
- CcBench's `last_go_store` approved. Runner labels: riscv and periph are W20's, reqbus W22's.
- **Step 2 started, CtrlTest** (`tests/ctl/suite_ctrl.cpp`, in progress): `fields` and `boot` ported; output equal to
  the first line(s) of ref's ctrl.log and ctrl-shuf.log (1,621 checks pass, clang release). Not yet in the runner or
  make check (its log is whole only when every group is in). `Cc::ready_bits` FATALs on main ("with the coreless
  machine"); the suite reads C&C 1's model directly (`ctl::model_ready_bits`, Cnc's `ready(k, t)`), no W20 need.
  Next: registers (bar its violation-count lines: the checker's kinds 20, 21 and the counter wraps), wfi, blockCopy,
  romSize, with the TileCtl/AG/WFI statistics listed above.
- **Step 2, CtrlTest: registers, wfi, misaligned, romSize, blockCopy ported.** Every printed line so far (boot,
  registers, WFI, block copy) equal to ref's, plain and shuffled (clang release); 5,921 checks pass. Four fail, all
  waiting on W20:
  - registers' violation count (384: the counter wraps, invariants 20 and 21) and the named messages: the checker's kinds
    (W20's list above). Ref's zeroing (`m.c.violations = 0`) is test-side here: the count since a point (`Zeroed`).
  - misaligned (two `ub` checks): **W20 request 11, new**: the harness's run stops with a FATAL naming ref's
    UndefinedBehavior ("tile 9", "controller", "misaligned 4-byte load") when a core stops on UB (Rv32's `ub`, its
    Stop::ub). Today the core stops quietly and the run goes on to its limit.
- Statistics added (counters only, out of the digests, marked W23): Rv32 `go_wakes`, `event_wakes` (ref's TileControl
  goWakes/eventWakes, counted where the core takes the wake), the WFI log (`wfi_log[16][3]`, `wfi_n`: Controller.wfiLog,
  {entered, woke, 1 GO / 2 event} outside the boot ROM; `wfi_at`, `wfi_in_rom`); RegBlockState `spurious_edges`,
  `last_done_at`, `last_landed_at`, `ready_pulse_at`. W20's reset must set them back as ref's resets do.
- **The boot ROM's size check** (ref's Controller.checkBootRom): `units::check_bootrom` (rv32.h), FATAL above 64 words,
  called where a tile's core gets its ROM (tile_ctl.cpp). romSize's three checks pass.
- Protected tests unchanged (clang release): test_golden, test_controllers, test_chip, test_cc_units, test_mach.

## Item 3 done: the book's Python ported (owner's ruling 2026-11-12)

- **gen_book** in C++: `fw/recipes/gen_book.cpp` with the region extractor in `fw/recipes/book.h` (regions.py's
  `region()` and gen_book.py's template fill; RecipeTest's port will include the same header). `gen_book --check`
  reproduces `fw/RECIPES.md` byte for byte (`tests/test_gen_book`, in make check, with `region()` on the cases
  regions.py's patterns decide: the de-indent, trailing blank lines, the first marker with an END, absent names, the
  template line's form; each checked against the Python before removal). `gen_book.py`, `check_book.py` and
  `regions.py` removed from `fw/recipes/`, their lines from `COPIES.sha256` (check_copies: 322 files).
  `fw/recipes/README.md` (a v1.3 copy) still names check_book.py: for P8's docs.
- **What check_book.py checked, and where each check now lives:**
  1. Every block in RECIPES.md marked `<!-- recipe: NAME (FILE) -->` followed by a fenced block equals, exactly, the
     region NAME of FILE as regions.py extracts it: **test_gen_book** (the whole book equal to the template filled
     from the sources, byte for byte: stronger, since it covers the text between blocks too); and RecipeTest's
     snippet check (each block equals its region after RecipeTest's normalisation; in the port, `suite_recipes`).
  2. The FILE a block names exists and has region NAME (else check_book stopped: "no region"): **test_gen_book**
     (gen_book fails on a missing file or region); RecipeTest's "has a RECIPE region in the sources" and "the file
     named is the one that holds it".
  3. It counted the blocks ("N blocks checked, M differ"): RecipeTest's "snippets: N code blocks in RECIPES.md, N RECIPE
     regions" line (in its log).
  RecipeTest adds what check_book.py didn't: each region marked once, and every region quoted in the book.
- The "47 vs 46" I noted earlier was v1.3's stale `results/recipes.log` (46); today's v1.3 run (our fixture) says 47
  blocks and 47 regions, as check_book.py: nothing to explain.

## header_diff.py ported (owner's ruling 2026-11-12)

- `tools/regs/header_diff.sh`: v1.3's header_diff.py in bash, awk, sort and comm over the host gcc (sort/comm in code
  point order; gcc keeps the caller's locale, as under Python). Byte-identical to the Python on mesh.h and the draft
  (70 lines, 12 s) and on perturbed pairs reaching every kind of line (`oracle/ref/header_diff_crosscheck.sh`: values
  differing, macros only in one header, members only in one, offsets differing; and a header that doesn't compile,
  equal but for the temporary file's path in gcc's message). Its output depends on the host gcc (its predefined
  macros, stdint.h), as the Python's; RegDocTest asserts only what must agree.

## HooksTest and RegDocTest: what they need beyond TileRef::load/store (for W20, 2026-11-12)

Read through both against main's `TileRef::load/store` and Net:
1. **Net on the machine as specified** (HooksTest's `machine()`: `Config.machine()`, periph_type 0 or 4, wrapped in
   Net; settled by `n.step()`): Net refuses a non-coreless config today. HooksTest uses on it: `step`, `at`, C&C 1's
   send (`n.c.cnc.send(cycle, ...)`), `run_all`, and tile 7's director log (`r.dir.log`).
2. **TileRef::load of a present peripheral's window** (HooksTest: window 1 on a tile with the replayer, word 0): today
   FATAL ("this bench has no peripheral slot"; LoadEnv has no `window_load`). Absent windows already give the bus error.
3. **TileRef::load with ref's side effects** (RegDocTest loads every register): EVENTS (consumes what it returned under
   the mask), REQUEST_GET_W0-W2 on a mesh tile (0), REQUEST_RECEIVE on a mesh tile (0 and receive underflow, ERRORS
   [14], seen two cycles later): today FATAL ("not modelled yet").
4. **From the test, between cycles, after the block's endCycle** (HooksTest's loadLatency): set sticky EVENTS bits
   (`p.ctl.events |= bits`, as request 9 for ERRORS), and schedule a GO for one tile at a given cycle
   (`p.ctl.scheduleGo(t)`).
What I do test-side or add myself: C&C 1's model's `expect` (Cnc's field, set between cycles), the director's
`remaining` (TileRef::set_remaining), `sendsBySize` and `droppedBad` (counters, if a unit lacks them), the AG's UB
(already a FATAL naming the tile and "a start while a run is running").

## W20's requests 9 and 10 on main (2026-11-12)

- Stand-ins removed from `tests/ctl/ctl.h`; CcCoresTest uses `TileRef::set_errors` and `Chip::set_check_errors`,
  CtrlTest `Chip::clear_violations` (in place of the count-since-a-point).
- **cccores is not matched yet, for a new reason**: `set_errors` reaches tiles 0-63 only. mach.cpp sets the block's
  `host_on` (which applies `host_cond`) on `t<k>.ctl` for k < 64, not on `cc1-cc3.ctl`, so errorPath's C&C 2 error is
  dropped (6 FAILs: ERROR_LINES_2 [1], 24 reports drained of 48, ...). **For W20: one line, `host_on = true` on
  cc1-cc3.ctl too with cc_cores.** Tried locally (not committed): with it CcCoresTest is 77 passed, its log equal
  (D44's mask), plain and shuffled, release and suite builds. Until then cccores stays PENDING in the runner (the two
  step lines ready in a comment) and off in make check (`kSetErrorsOnControllers`).
- `test_ctl_suites.cpp`: std::regex dropped (gcc 13's false -Wmaybe-uninitialized in the debug build); the D44 mask
  by hand.

## Step 6 started, RecipeTest (`tests/ctl/suite_recipes.cpp`, in progress)

- Ported: snippets (Java's two patterns and `norm` by hand: "47 code blocks, 47 RECIPE regions"), copy, cmem, status,
  rules, and the cycle table's printing; every line equal to a line of ref's log, plain and shuffled; 891 checks pass.
- Waiting on W20's `Cc::phase_log` (still FATAL on main): cmdloop, bucket, aug, gather, xchg, xvec, a2a, rdport, sync,
  boot, signal (goToReady, goToFirstRequest). `wait_until` (on main) is used by blocks, selftest, tables, cc1, which
  come next with SelfTest.

## 2026-11-12, later

- **Pushed 860aee67** (11c8b7ba, a1b47088 and main merged at 7b790887's successor): make check's four builds, every
  test passing (74 of 74 in gcc debug, gcc release, clang debug, clang release), check_copies clean. On the way:
  test_stageb (W25's) failed on the branch before that merge (a replayer violation's cycle one earlier) and passes on
  main and after the merge: the branch's lag, not my units.
- **RecipeTest on W20's phase_log**: cmdloop, bucket, aug, gather, xchg, xvec, a2a (with goToFirstRequest), rdport,
  blocks (with SelfTest: selftest.h's constants read from fw/tests/selftest.h as SelfTest.java does) ported: every
  line in ref's log, plain and shuffled; 1,967 checks pass (ref: 2,056). Left: sync, boot, signal (the phase log;
  next), selftest.
- **selftest's needs from C&C 1's model, for W20** (the C++ Cnc has neither): `drainReports` (C&C 1's model drains
  target 0's requests and answers each, as ref's CnC with drainReports = true) and `results` (the requests it drained:
  {t, requestor, w2, w3} as ref's cc.results). ref's `then((c, t) -> ...)` step (wait for the tiles idle, then poke
  one record, in one step) I will try as `wait_until(...).call(...)` against ref's selftest figures once the two above
  exist (only its figures can tell whether the call's step costs C&C a cycle there); asked for only if they differ.
- **Pushed 0b00a47e** after the four-build check (74 of 74 in each build).
- **RecipeTest: sync and signal** ported (every line in ref's log, plain and shuffled; 2,040 of ref's 2,056 checks).
  Left: selftest and boot, both on C&C 1's model's drainReports/results (boot also `reportsOf(9)` and `cc.reset(false)`):
  W20's queue. RegBlockState gains `counter_write_at[8]` (ref's counterWriteAt; statistics).
- **CtrlTest: commands** ported; its block of lines equal to ref's, plain and shuffled. CmemTrack (W26's, rv32.h) gains
  `writes_cmd`, `landed_at` (ref's cmem.writesCmd, landedAt; statistics, set in track_port_b, which sees port B's
  write of t at t + 1: the suite uses the recorded cycle, ref's `m.c.cycle` when its predicate saw the write). The
  four FAILs left in the release build: registers' violation count and names (W26), misaligned's UB stops (W20's 11).
