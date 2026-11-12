# WORKER23 notes (P5: the control suites, programs and recipes)

Branch `sim-w23`, from main 67d42fb. Rows C8, C10, C12, C13, C14, C15, C18, G2, G3.

## The plan (proposed, waiting for the go-ahead)

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
