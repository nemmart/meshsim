# Worker 18 (sim/, v1.35): the inventory: everything v1.3's Java meshsim had, and its C++ equivalent

**The project.** meshsim is the simulator for a parallel compute mesh (part of an ASIC for proving the Jolt zkVM).
v1.3's meshsim is Java (frozen in `v1.3/`, and in `sim/ref/` as the exactness oracle). **meshsim v1.35 is its C++
implementation in `sim/`** (D37): three milestones have made it exact against v1.3 and much faster (the controller's
programs, the full-size sort at 20.9×, v1.3's segment test T2 at 6.5×). **The owner's decision (D38,
`sim/planning/PLAN-V135.md`): freeze the C++ simulator as v1.35, with no Java in it, provided it has an equivalent for
everything v1.3's Java meshsim had.** You establish whether it does.

READ FIRST: the top-level `README.md` (the versions, how folders are frozen), `sim/README.md`, `sim/planning/PLAN-V135.md`,
`PLAN-M1.md`, `PLAN-M2.md`, `PLAN-M3.md` (each close-out), `sim/planning/DECISIONS.md` (D30, D33, D36-D38), and
`sim/planning/sessions/README.md` (what every worker built, and where its report is). **The Java:** `sim/ref/meshsim/`
(`README.md`, `run_tests.sh`, `src/`, `test/`, `tools/`, `sw/`, `kit/`, `verify/`, `acceptance/`, and the other folders),
`sim/ref/sort/`, `sim/ref/gold/`, `sim/ref/tables/`, and v1.3's own docs in `v1.3/`. **The C++:** `sim/src/`,
`sim/tests/`, `sim/bench/`, `sim/fw/`, `sim/tools/`, the Makefile and its includes.

THE WORK
1. **The inventory**, `sim/planning/V135-INVENTORY.md`: one table, one row per capability of v1.3's Java meshsim:
   - every suite and step in `run_tests.sh` (quick and full), every test and driver class, every recipe, every
     firmware and its build, every tool that makes or converts inputs (KeyGen, ConvertDelivery, SyntheticDelivery,
     and the rest), every bench (ReplayBench, CcBench), every option and flag users set (idle-skip, shuffle seeds,
     latencies, configurations), every output users read (CSVs, phase logs, fingerprints, digests, summaries), the kits;
   - for each: what it does, who uses it (the RTL engineer, the proof pipeline, regressions), its C++ equivalent (file,
     test, tool) with how it's checked against v1.3, or **a gap**;
   - for each gap, a proposed disposition: **port** (with an estimate: small, medium, large), **point to v1.3**
     (a reference-only tool whose outputs v1.35 consumes unchanged), or **drop** (with the reason), and why.
   Group the rows (the chip and its units; programs and suites; the sort; the Tracer and replayers; input tools; benches;
   options and outputs; kits and docs). End with a summary: the gaps by disposition, and the work they would take.
2. **Check, don't assume:** for each claimed equivalent, name the test or run that shows it matches v1.3 (and if none
   does, that's a gap in checking even if the code exists).
3. **Questions for the owner**: where the right disposition depends on how v1.35 will be used, ask; don't decide.

RULES
- **Propose your plan first** in `WORKER18/NOTES.md` and wait for the go-ahead. This is documents only: no code changes.
- Work on branch `sim-w18`; the integrator merges. Keep notes and report in your session folder.
- `sim/ref` is frozen. Running v1.3's suites to see what they do is fine (locally with a JDK, or on the runner: pull Quest
  and re-read `NUMBERS.md` immediately before claiming; tasks carry embedded kits; no machine-specific values, D21).
- Never write a token into a file. Never `pkill -f`; use `pgrep` and kill by PID. Background jobs don't survive a turn.
- Commit and push after each completed piece, keep the last tool calls for committing, end each turn with a clean
  `git status`. Keep replies to the owner short.

DELIVERABLES: `V135-INVENTORY.md` with the summary and the questions; `WORKER18/REPORT.md`.
