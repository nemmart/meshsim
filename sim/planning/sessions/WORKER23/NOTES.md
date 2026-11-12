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
