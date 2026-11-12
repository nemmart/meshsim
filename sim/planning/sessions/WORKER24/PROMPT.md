# Worker 24 (sim/, v1.35, P6): the sort and its kit

**The project.** meshsim is the simulator for a parallel compute mesh (part of an ASIC for proving the Jolt zkVM).
**meshsim v1.35 is its C++ implementation in `sim/`** (D37), exact against v1.3's Java (kept as the oracle in
`sim/ref/`) and much faster: the controller's programs, the full-size sort (20.9×), and v1.3's segment test T2 on all 128
segments of both workloads (6.5×). **v1.35 is being frozen** (D38-D41, `sim/planning/PLAN-V135.md`): **the simulator
and everything that runs it off Java, every capability of v1.3's Java meshsim with an equivalent, and v1.35 a
stand-alone directory with all the source needed to build and run everything**; the offline input tools (`gold`,
jumpmap, KeyGen, SplitSets, ConvertDelivery, SyntheticDelivery) stay Java in v1.35's own `pipeline/` folder. W18's
inventory (`sim/planning/V135-INVENTORY.md`) is the porting plan: rows by capability, pieces P1-P8, waves.

**You are P6** (inventory rows D1-D10, H4, H7): v1.3's sort beyond M2's p4/uniform-k128: the other key sets and sequences (kv32 and the rest), the other paths (BT, LB, TH, PLAN), shaped runs, the 64 × 2,000 catalogue, the driver, the measurement kit with its per-run CSVs, and SortSummary (D41 Q3: SortCompare too, as a generic run-comparison tool). M2's sort driver (W12's `tests/test_sort.cpp`, `sort_input.h`) and the input key sets (KeyGen and SplitSets in `pipeline/keys/`, D40) are your starting point.

READ FIRST: the top-level `README.md`, `sim/README.md`, `sim/planning/PLAN-V135.md`, **`sim/planning/V135-INVENTORY.md`**
(your piece's rows in detail, and its porting plan), `sim/planning/DECISIONS.md` (D6, D21, D24, D27, D30, D37-D43),
`sim/planning/sessions/WORKER18-REPORT.md`, **`WORKER20-NOTES.md`** (the machine harness `tests/mach.h` and its body,
`suite.h`, the checker's fact interface `src/check.h`, `MODE=suite`) and `WORKER19-REPORT.md` (the stand-alone tree:
`data/`, `fw/`, `pipeline/`, `oracle/`). How a port is checked here: `WORKER9-REPORT.md` (RouterConf), `WORKER15-REPORT.md`
(ReplayConf, TileSlotConf), `WORKER21-REPORT.md` (suites byte-identical to `ref`'s logs).

HOW EVERY SUITE IS CHECKED: run v1.3's step (from `sim/ref/meshsim/run_tests.sh`, quick and full) once in `ref` to make
its log, commit the log as the fixture, and require the C++ suite's output to equal it byte for byte (timing lines
masked as W20's runner does), plain and under the debug options (shuffle, both orders, step-all) where `ref` runs them
shuffled. Each suite joins W20's suite runner (`sim/run_tests.sh`) under v1.3's step name, and `make check` runs the
quick ones. The Java side (any driver that makes or regenerates a fixture) lives in `sim/oracle/ref/`; nothing in v1.35
reads `sim/ref` or needs Java (D39).

WORKING WITH W20: W20 owns the harness (`tests/mach.*`), the checker (`src/check.h`, `tests/checker.*`) and the
recording of violation facts in the existing units. If your suite needs a harness operation or a violation kind that
isn't there yet, ask W20 through the integrator; don't edit those files or add facts to existing units yourself. New
units you add record their own facts through `check::record`.

THE WORK
1. Every path exact against `ref`: fingerprints at every GO, the cycle count and the result, as M2 did for p4.
2. The kit's CSVs byte-identical to v1.3's on its smoke runs, and SUMMARY byte-identical; SortCompare's output too.
3. PhaseLog's CSVs come from W20's P2 (H3): use them, don't duplicate them.
4. The split driver (D9) is inherited dead code (W19): don't port it; say so in the report.
5. Long runs (the catalogue, full sizes) on the runner: pull Quest and re-read NUMBERS.md immediately before claiming; no machine-specific values (D21).

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
- Branch as named below, in `sim/tests/` (suites, fixtures), `sim/fw/` (programs your suites need, built by
  `fw/build.sh`) and `sim/oracle/ref/` (Java drivers). Merge `main` often. Four wave-2 workers work in parallel: keep
  your files your own, and tell the integrator before touching a shared file (the Makefile, `run_tests.sh`).
- Exactness first (D30 applies): a log difference is a bug until explained; a deliberate difference needs the
  integrator's approval.
- Branch `sim-w24`.

DELIVERABLES: every suite in your rows ported and log-equal to v1.3, in the runner and (quick ones) in `make check`; `WORKER24/REPORT.md`.
