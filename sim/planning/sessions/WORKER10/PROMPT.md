# Worker 10 (sim/, M2): framework speed: one chip, one thread faster, and one chip split over threads cheaper

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`), with v1.3's Java simulator
(`sim/ref/`) as the exactness oracle. Milestone 1 is closed. **Milestone 2's target (owner, D25): the full-size sort,
exact against `ref`, one chip split over threads, in about 4 s against `ref`'s ~96 s: about 24×** (`PLAN-M2.md`). **We
simulate one chip at a time (D26)**, so only two figures count: **speed per thread** (M1: ~4.5× `ref` unit for unit, D22)
and **one chip split over threads** (form 2: ~3× on four threads for the mesh so far). Independent chips on separate
threads (form 1) are a thread-safety test only. **You own the framework's speed** while W8 and W9 (and later G3, G4)
build the sort's units on it. You change the framework's internals; **the unit API stays stable.**

READ FIRST: `sim/README.md`; `sim/planning/PLAN-M2.md` and `PLAN-M1.md` ("Carried to M2"); `sim/planning/DECISIONS.md`
(D15, D16, D21-D26). **The framework and why it is as it is:** `sim/src/sim.h`, `config.h`, `PORTS.md`, `engine.h/.cpp`,
`config.cpp`, `threads.h/.cpp`, `barrier.h/.cpp`, and **WORKER3's and WORKER4's reports and notes**
(`sim/planning/sessions/WORKER3-REPORT.md`, `-NOTES.md`: the performance pass, Quest 132, the dependency chain, the solo
loop set aside; `WORKER4-REPORT.md`, `-NOTES.md`: form 2, Quest 133, the split's cost, footprint). The measurements:
`WORKER6-REPORT.md` (the hand-rolled mesh), `WORKER5-REPORT.md` (the controller's profile), `WORKER7-REPORT.md` (`ref`'s
baseline) and `sim/tests/AUDIT-M2.md` sections 1 and 4 (what the sort's chip does, its chip-level work).

THE WORK (each measured before and after on the runner, digests and every golden unchanged)
1. **A cheaper split** (W4, Quest 133: on one thread, six domains cost +18% time and +52% memory; each domain carries its
   own engine structures, boundary units and outboxes). Make a domain's fixed cost small, so splitting finer costs little.
2. **Partitioning by load**, not by rows or tile groups alone (Quest 133: the mesh's 8 rows over 6 threads left two
   threads with twice the work, so 6 threads ran slower than 4). Propose how load is estimated (declared, or measured from
   a calibration run's step counts), and a partitioner that balances it under form 2's constraint (every crossing a Link,
   so the barrier interval stays 3).
3. **A Link whose consumer may hold up to a declared depth** (W4: routers copy every arriving word into their own queue,
   holding each word twice, ~74 KB of the mesh's 228 KB): a new port kind or an option on Link, with `ref`'s semantics
   intact and its own conformance test; then W9's router can use it.
4. **Footprint** (D15 amended by D26: one chip's state should fit the caches of the cores it's split over): the wheel's
   worst-case sizing (W3, flagged), and whatever else the sort's chip shows.
5. **The per-cycle dependency chain**, if it matters on busy workloads (PLAN-M1 "Carried to M2"): a general fix in the
   common cycle's bookkeeping, never a lone-unit special case.
Benchmarks: W4's mesh and synthetic chip now; the sort's chip as W8 and W9 assemble it. Report one thread, then form 2
over 2-12 threads, with the machine (D21).

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
- Branch `sim-w10`, in the framework (`sim/src/*.h|cpp` outside `units/`, `sim/src/base/`) and `sim/tests/`, `sim/bench/`.
  **The unit API (sim.h's and config.h's operations) stays stable**: W8 and W9 build on it now. An addition is fine if
  additive; any change to an existing operation comes to the integrator first. Merge `main` often: W8's and W9's units
  are your best benchmarks as they land.

DELIVERABLES: each change with its before/after table on the runner (one thread and form 2, with the machine); the
Link that holds a declared depth, with conformance; the partitioner; `WORKER10/REPORT.md`.
