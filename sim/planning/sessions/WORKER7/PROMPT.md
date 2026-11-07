# Worker 7 (sim/): M2's reference side: what the sort needs, how each unit couples, and what we check against

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`): very fast, parallel, and easy to
build units in, with v1.3's Java simulator (`sim/ref/`) as the exactness oracle. Milestone 1 (nearly closed) built the
framework and one RV32 controller matching `ref` instruction for instruction, on one thread or many. **Milestone 2's
target (owner): the full-size sort, exact against `ref`, multithreaded, in about 4 s against today's ~110 s**
(`sim/planning/PLAN-M2.md`). You do for M2 what WORKER2 did for M1: establish what must be built, the precise behaviour
each piece must reproduce, and the references to check it against. Unit workers start from your audit.

READ FIRST: `sim/README.md`; `sim/planning/PLAN-M2.md` (the target, what it likely needs, how exactness is checked) and
`PLAN-M1.md` (what M1 built, "Carried to M2"); `sim/planning/DECISIONS.md` (D5, D12, D15, D21, D23 especially). **M1's
equivalents, your model:** `sim/tests/AUDIT-M1.md`, `sim/tests/TRACE-FORMAT.md`, `sim/ref/README.md`, and
`sim/planning/sessions/WORKER2-REPORT.md`. **The framework you're auditing for:** `sim/src/PORTS.md` (the port kinds, each
from a `ref` class) and `sim/src/sim.h`. **Earlier audits to build on:** `wip/planning/sessions/WORKER29-DESIGN.md`
section 3 (today's same-cycle couplings mapped onto ports) and `WORKER28-NOTES.md` (the same-cycle calls between units,
the wake sources). **The oracle:** `sim/ref/meshsim/src/` (`Chip.java`, `Tile.java` and every unit it builds),
`sim/ref/meshsim/test/SortTest.java` and its harness, `sim/ref/sort/`, `sim/ref/meshsim/run_tests.sh`.

THE WORK
1. **What the sort exercises.** Run the sort in `ref` (n = 16,384 locally; full size on the runner) and establish every
   unit, bus, register and chip-level mechanism it touches, and every harness stand-in (input load, verification). Say
   what it doesn't touch. C&C: say whether the sort runs the Java sequencer or the cores, and what the framework must
   reproduce of it.
2. **The coupling audit**, `sim/tests/AUDIT-M2.md`: for each unit the sort needs, its state, its ports onto the framework's
   kinds (PORTS.md), every same-cycle coupling with the cycle it takes effect, the order within `Chip.runCycle` and
   `Tile.runCycle` that matters, and the chip-level work (`RequestBus.runCycle`, overflow scans, invariant checks) with how
   it can be event-driven (D23). Propose **unit groups for parallel workers**, and the order to build and check them in.
3. **The references.** Golden runs for `regs`, `wfi`, the mesh tests (NetTest, all-to-all), the sort at n = 16,384, and
   the full sort: cycle counts, results, `ref`'s fingerprints and logs, and per-tile controller retire traces where
   useful, each with a D12 header (its configuration, the images). A regenerate-and-check script, as `regen.sh --check`.
   Large references stay on the runner (not in git): say where, with their hashes.
4. **How to localise a mismatch.** Find what `ref` already offers between "the fingerprint differs" and "this unit's put at
   cycle t differs" (fingerprints at intervals, per-tile state, logs). If that's not enough to develop units one at a
   time, **propose** a finer reference (for example a trace of puts and takes on a tile's named buses), what it would
   change in `sim/ref`, and its cost. It would extend D5's one observer, so it's the owner's decision: propose, don't
   build.
5. **The baselines** on the runner, with the machine recorded (D21; use `sim/tools/machine.sh` once on main, else W5's
   `sim/planning/sessions/WORKER5-tasks/machine.inc`): the full sort's wall time in `ref` on one JVM, split into setup and
   the run, at `main`'s engine, three runs. That is the 110 s to beat; the 4 s aim excludes setup the same way.

RULES
- **Propose your plan first** in `WORKER7/NOTES.md` and wait for the go-ahead.
- `sim/ref` is frozen apart from D5's observer: no other change. Your drivers live in `sim/tests/ref/`, compiled into
  `ref`'s default package; your references in `sim/tests/golden-m2/` (or on the runner, with hashes).
- Work on branch `sim-w7`; the integrator merges. Keep your notes, questions and report in
  `sim/planning/sessions/WORKER7/`. W3 and W4 are finishing M1 in `sim/src/`; don't touch it.
- Exactness: where `ref`'s behaviour looks wrong, record it with evidence; never "fix" the reference.
- Long runs go to the runner. **Quest numbers:** pull Quest and re-read `NUMBERS.md` immediately before claiming, and push
  the claim before writing the task (`sim/planning/sessions/README.md`). Tasks carry embedded kits; no hardcoded CPU lists
  (D21).
- Never write a token into a file. Never `pkill -f` a pattern that matches your own command line.
- Commit and push after each completed piece, keep the last tool calls for committing, end each turn with a clean
  `git status`.
- Keep replies to the owner short.

DELIVERABLES: `AUDIT-M2.md` with the unit groups and their order; the references and their check script; the proposal on
localising mismatches; the baselines; `WORKER7/REPORT.md`.
