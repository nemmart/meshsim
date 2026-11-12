# v1.35: the fast simulator, frozen

Agreed with the owner, 2026-11-10 (D38). **v1.35 is meshsim's C++ implementation (`sim/`), frozen**: no other
improvements, only its docs, build scripts and cleanup, and **no Java**, provided v1.35 has an equivalent for
everything v1.3's Java meshsim had. The Java stays available as v1.3 (`v1.3/`, and `sim/ref` as the oracle).

**Refined by D40-D41 (owner): the simulator and everything that runs it are off Java; the offline input tools
(`gold`, jumpmap, KeyGen, SplitSets, ConvertDelivery, SyntheticDelivery) stay Java in v1.35's own `pipeline/` folder.
So the inventory's P3 and P9 are dropped; the pieces are P1, P2, P4-P8.**

**Refined by D39 (owner, 2026-11-10): everything off Java, even if it takes several workers, and v1.35 a stand-alone
directory with all the source needed to build and run everything.** Every gap is ported (no "point to v1.3"); each port
is checked against v1.3 as M1-M3's units were.

## Steps

1. **The inventory** (W18): every capability of v1.3's Java meshsim (`run_tests.sh`'s suites, the test drivers, the
   recipes, the tools that make inputs, benches, options, outputs such as CSVs, phase logs and kits), each with its C++
   equivalent or a gap. One table, `sim/planning/V135-INVENTORY.md`. Under D39 every gap is ported: the inventory
   becomes the porting plan (estimates, worker-sized groups, the order, how each port is checked against v1.3).
2. **The ports**, by several workers in parallel, from the inventory's groups.
3. **Docs, build scripts and cleanup** (one worker): v1.35's README and BUILDING, how to run every suite and tool,
   stale paths, leftover benchmarks.
4. **The freeze check** on the runner (as v1.3's Quest 118): every suite, the full-size sort, T2 on both workloads,
   W17's 128-segment chains. Then `sim/` is copied, without `sim/ref`, to a frozen `v1.35/`, with its row in the
   top-level README.

## Pieces (from `V135-INVENTORY.md`, after D40-D41)

| wave | piece | worker |
|---|---|---|
| 1 | P1, the stand-alone tree (data files, firmware sources and builds, the mesh.h generator, `pipeline/`, `sim/oracle/`) | W19 |
| 1 | P2, the machine harness, the checker, reset, `Config`'s used fields, PhaseLog, the suite runner | W20 |
| 1 | P7a, the Tracer's rig and directed cases, MemorySystemTest, RvcTest (C21-C23) | W21 | **done**, closed 2026-11-11 (`sessions/WORKER21-REPORT.md`): P7a, the Tracer's suites stand-alone |
| W22 | P4, the network, memory and engine suites: prompt written 2026-11-11 |
| W23 | P5, the control suites, programs and recipes: prompt written 2026-11-11 |
| W24 | **done**, closed 2026-11-12 (`sessions/WORKER24-REPORT.md`): P6, the sort and its kit byte-identical to v1.3 |
| W25 | **done**, closed 2026-11-12 (`sessions/WORKER25-REPORT.md`): P7b, Stage B and the acceptance kit equal to v1.3's (Quests 180, 182) |
| W26 | P2's second half (split from W20): the checker's remaining kinds, S1, BusTest; prompt written 2026-11-12 |
| 2 | P4 (network, memory, engine suites), P5 (control suites, programs, recipes), P6 (the sort and its kit), P7b (Stage B, the acceptance kit) | after P2 |
| now | the docs session: `sim/spec/` (the overview, the guidelines, the spec's restructuring, on `v1.35-spec`) | the docs session (D45) |
| 3 | P8: the rest of the docs from `V135-DOCS-PROPOSAL.md` sections 3-5, the comment strip, cleanup; then the freeze check, with section 6's docs gate | last |

**The restructured spec is on main** (`sim/spec/`, 35 files, the owner's rulings applied; the docs session, D45). At the
freeze, `spec/CHANGES-v1.3-to-v1.35.md`'s section "Where v1.35's checks differ from the rule" must come out empty and be
removed: every check in the spec session's findings 9-12 is ported by then (W26's queue), and the spec session reruns its
mechanisms table to confirm.

## Where it stands (end of 2026-11-12)

**Done:** P1 (the stand-alone tree), P6 (the sort and its kit), P7a and P7b (the Tracer's suites, Stage B and the
acceptance kit); W20's harness queue (empty, REPORT written); W26's checker queue except CheckSelf's endpoint case and
whatever CtrlTest's invariants group needs; the restructured spec (`sim/spec/`, merged). `main`: 75 test binaries, and
`./run_tests.sh quick --check` passing with no log differences.
**In flight:** W22: AgTest (234 of 240 until W20's W22-21, now on main), then the freeze check's runner task (a
rehearsal on today's main). W23: cccores off PENDING, S1Bench, CondTest, RecipeTest's boot, CtrlTest's faults,
invariants and all-to-all, then ctrl-full and accounting-full on the runner. W26: CheckSelf's endpoint case.
**Next:** P8: v1.35's documents (a docs-writing session on `v1.35-docs`, under the docs session; RUNNING.md last), W24's
tooling (the docs check, the freeze's copy script, the comment-strip script run once at the end, the helper switch);
then the freeze check (every step live, none PENDING; the full-size sort, T2, the chains, the kit's calibration; the
docs session's gate) and the copy into `v1.35/`.

## Status

| worker | state |
|---|---|
| W17 | **done**, closed 2026-11-10 (`sessions/WORKER17-REPORT.md`): all 128 segments of both workloads pass, back to back (Quest 174: evm 878.5 s, cyclone 849.7 s of simulation, 0.30 µs per cycle); part of the freeze check |
| W18 | the inventory and porting plan: `V135-INVENTORY.md` (merged); the owner's rulings D40-D41 |
| W19 | P1: prompt written 2026-11-10 |
| W20 | P2: prompt written 2026-11-10 |
| W21 | P7a: prompt written 2026-11-10 |
