# Worker 4, round 2: real cores for C&C 1-3 in meshsim

v1.2 is frozen (`v1.2/`, tag `v1.2`) with your simulator in it, thank you. Its three known gaps all come from one thing: **C&C
1-3 are Java models (`CnC.java`), not cores.** So the boot ROM's branch for the controllers (endpoints 64-66) never runs, C&C 2/3's
"started" requests aren't modelled, and `rcp_cc1.c` is built but never executed. The spec already makes the controllers ordinary
tile instances (a controller core, its memories, its peripheral). This round makes meshsim run them that way, as an **option**,
without disturbing anything that uses the Java models today. It is also groundwork for v1.3, where C&C 3's firmware feeds the
Tracer its long expansions (`wip/planning/DECISIONS-v1.3.md` section 10).

REPOSITORY AND BRANCH
- `main` now contains your simulator and the v1.25 spec in `wip/spec/`. **Work on a new branch `cc-cores`, made from `main`.**
- WORKER11 is updating the replayer on `v1.25-meshsim` and has added an opt-in `Chip.parked` in your area; keep your changes to
  `Chip` small and mergeable, and merge `main` into your branch as it moves (merge, don't rebase). The integrator merges your
  branch into `main` when it's done.
- Quest task numbers: claim yours in the Quest repository's `NUMBERS.md` before writing a task (pull, append the next number with
  your session and task, commit, push; on a rejected push, pull and take the next). End task scripts with an explicit `exit` before
  any embedded kit.

WHAT TO BUILD
1. **Controllers as cores, opt-in** (a `Config` switch, off by default): C&C 1-3 run the same controller core, memories, register
   block and boot ROM as a tile, with their peripheral (C&C 1's Ingress/Egress with the UART and the chip lines, spec 6.6, 8.14;
   C&C 2's and C&C 3's engines as stubs that raise nothing). The Java models stay the default, so every existing suite and the sort
   are unchanged.
2. **Boot for the controllers** (spec 6.2): the boot ROM's branch for 64-66 runs; the test harness, playing the outside world, lands
   C&C 1's overlay and first command; C&C 1's firmware then boots C&C 2 and C&C 3 (their overlays and first commands), which send
   C&C 1 their **"started" requests** through the request tree; then C&C 1 boots the tiles with READY/GO, as `Mach.boot` does today.
3. **Minimal firmware** for C&C 1 (the boot sequence, then a phase loop driven by commands from the harness) and for C&C 2 and C&C
   3 (start-up and the "started" request), in `sw/`, built against the generated `mesh.h`.
4. **Tests:** the controllers' boot path end to end; the "started" requests; **`rcp_cc1.c` run** as C&C 1's side of a phase against
   real tiles; the error path with C&C 1's real handler (a tile's error line, the unit-error bit, the toast pin, the 24 reports
   drained); RecipeTest's cc1 driver turned from "built" to "run".
5. **Measurements:** the boot from release of reset to all 64 tiles running, with C&C 1 as a core, against the Java model's 120
   cycles; a phase with C&C 1 as a core against the Java model.

RULES
- **The spec is the design.** Where it is silent on how a controller behaves as a core (what C&C 1's firmware must do, timing the
  Java model glossed over), record a question in `WORKER4/QUESTIONS-2.md` with your proposed answer, mark it in the code, and carry on.
- Don't change the sort, the spec, the recipes' text or the decisions logs; changes to recipe sources only if a recipe is wrong, as a
  question first.
- **Keep the last few tool calls of every turn for committing and pushing**, and end each turn with a clean `git status`. Background
  jobs don't survive a turn: long runs finish within one or go to the runner, with a smoke-tested kit. Keep `WORKER4/NOTES-2.md` current.

DELIVERABLES
- The branch `cc-cores`: controllers as cores behind the switch, the firmware, the tests passing, and every existing suite unchanged
  with the switch off.
- `WORKER4/REPORT-2.md`: what was built, the measurements, what v1.2's known gaps become, and what v1.3's C&C 3 firmware can build on.
