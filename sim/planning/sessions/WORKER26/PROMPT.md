# Worker 26 (sim/, v1.35, P2's second half): the checker's remaining kinds, S1 and BusTest

**The project.** meshsim is the simulator for a parallel compute mesh (part of an ASIC for proving the Jolt zkVM).
**meshsim v1.35 is its C++ implementation in `sim/`** (D37), exact against v1.3's Java (kept as the oracle in
`sim/ref/`) and much faster: the controller's programs, the full-size sort (20.9×), and v1.3's segment test T2 on all 128
segments of both workloads (6.5×). **v1.35 is being frozen** (D38-D41, `sim/planning/PLAN-V135.md`): **the simulator
and everything that runs it off Java, every capability of v1.3's Java meshsim with an equivalent, and v1.35 a
stand-alone directory with all the source needed to build and run everything**; the offline input tools (`gold`,
jumpmap, KeyGen, SplitSets, ConvertDelivery, SyntheticDelivery) stay Java in v1.35's own `pipeline/` folder. W18's
inventory (`sim/planning/V135-INVENTORY.md`) is the porting plan: rows by capability, pieces P1-P8, waves.

**You take over part of W20's queue** (P2, the machine harness and the checker; W20's estimate: more than one worker's
worth). **W20 keeps the harness** (`tests/mach.*`, its API, PhaseLog, `Config`, reset, the requests wave 2 makes of the
harness). **You own the checker's violation kinds**: recording each kind's facts in the units (where `ref` does
`violations++; lastViolation = ...`) and their texts in `tests/checker.cpp`, checked line for line against `ref` through
`oracle/ref/CheckSelf.java`. Four wave-2 workers port suites beside you (W22 P4, W23 P5, W24 P6, W25 P7b); a suite whose
log needs a kind asks you through the integrator.

READ FIRST: the top-level `README.md`, `sim/README.md`, `sim/planning/PLAN-V135.md`, `sim/planning/V135-INVENTORY.md`
(rows B19, B20, C2), `sim/planning/DECISIONS.md` (D6, D21, D24, D27, D30, D37-D44), **`sim/planning/sessions/WORKER20/NOTES.md`
in full** (the checker's design, every kind recorded so far, the once-only rules, the config switches, the stray check's
known gap, and W20's list of what's left), `sim/src/check.h` (the fact interface), `tests/checker.{h,cpp}`,
`oracle/ref/CheckSelf.java` and `checkself.sh`, and `ref`'s `Chip.java` (`checkInvariants`, `scanOverflows`) with each
unit's `violations` sites.

THE WORK, in the order suites will need it (ask the integrator if a suite's request reorders it)
1. **The remaining kinds** (W20's list): the AG, the read port, the read queue, the controller memories, big SRAM's
   disjoint-buffer check (invariant 2, with `Chip::new_phase`, which W20 adds to the harness; agree the hook with W20
   through the integrator), HIST overflow, late rotations, the read-return refusal, the request bus (including requests to
   a reserved or deferred target, W22's W22-13), the endpoints, C&C 1's peripheral, the cores' checks, and the counter
   wraps and invariants 20 and 21 (W23's CtrlTest `registers` group needs them). Each: facts recorded where `ref` counts
   it (counters unchanged; D24: no allocation), its text in `checker.cpp`, a CheckSelf case provoking it, line for line on
   gcc and clang in the debug and suite builds.
2. **S1** (invariant 24, on by default in `ref`): a counted write reaching big SRAM's area queue, or completing its PTR or
   HIST write-back, more than K + 4 cycles after acceptance. W20 ran one case with `measureS1` off on both sides until
   this exists: switch it back on when it does.
3. **BusTest** (C2, the runner's first suite in W20's plan): ported on `suite.h` onto the framework's port kinds, log-equal
   to v1.3's ("154 passed, 0 failed"); v1.3's checks that expect an exception become fatal-error checks (`suite::throws`).
   Then take its runner step off PENDING.

RULES
- **Propose your plan first** in your `NOTES.md` and wait for the go-ahead before writing code.
- Work on your own branch; merge `main` in often (never rebase); the integrator merges into `main`. Keep notes, questions
  and report in your session folder.
- `sim/ref` is frozen (D5). Exactness is the requirement: a difference from a reference is a bug, never a tolerance; if
  you believe a reference is wrong, stop and report it with the evidence.
- The cycle-loop rules (D6, D24): no STL, no allocation, no virtual calls in any `step` or engine path, diagnostics
  included; untyped 64-bit words; three-word link words (D17). Sanitizers and deterministic tests from the first commit;
  `make check` clean on gcc and clang (run it build by build if the tool limit bites). Every new unit passes the debug
  checks: shuffle, both orders, step-all, the missed-wake detector.
- Long runs and all timing go to the runner. **Quest numbers:** pull Quest and re-read `NUMBERS.md` immediately before
  claiming, push the claim before writing the task. Tasks carry embedded kits. **No machine-specific values** (D21): CPU
  lists from `sim/tools/machine.sh`, every result with its machine. Background jobs don't survive a turn.
- Never write a token into a file. Never `pkill -f`; use `pgrep` and kill by PID.
- Commit and push after each completed piece, keep the last tool calls for committing, end each turn with a clean
  `git status`.
- Keep replies to the owner short.
- Branch `sim-w26`. You edit the units only to record facts and the checker's files; W20 owns the harness. Don't add facts
  where W20 already has (its NOTES lists them). The units' behaviour and their counters stay exactly as they are: before and
  after each change, `test_chip`, `test_sort`, `test_t2_harness`, `test_cc_units` and `test_golden` unchanged.
- The stray check's gap (W20's NOTES): if a suite's log reaches it, tell the integrator.

DELIVERABLES: every remaining kind and S1 recorded and line for line with `ref` in CheckSelf; BusTest log-equal in the
runner; `WORKER26/REPORT.md`.
