# Worker 14: real cores for C&C 1-3 in meshsim

You are working on **meshsim**, the cycle-level simulator of the mesh, part of an ASIC for proving the Jolt zkVM. v1.2 is frozen
(`v1.2/`, tag `v1.2`), with a simulator that passes every suite and runs the sort at full size. Its three known gaps all come from
one thing: **C&C 1-3 are Java models (`CnC.java`), not cores.** So the boot ROM's branch for the controllers (endpoints 64-66) never runs, C&C 2/3's
"started" requests aren't modelled, and `rcp_cc1.c` is built but never executed. The spec already makes the controllers ordinary
tile instances (a controller core, its memories, its peripheral). This task makes meshsim run them that way, as an **option**,
without disturbing anything that uses the Java models today. It is also groundwork for v1.3, where C&C 3's firmware feeds the
Tracer its long expansions (`wip/planning/DECISIONS-v1.3.md` section 10).

REPOSITORY AND BRANCH
- Clone https://github.com/nemmart/meshsim with the meshsim token from your user preferences (as
  `https://x-access-token:<token>@github.com/nemmart/meshsim.git`). Never write a token into a file.
- `main` contains the v1.2 simulator (`wip/meshsim/`) and the v1.25 spec (`wip/spec/`). **Don't touch `v1.0/`, `v1.1/` or `v1.2/`.** **Work on a new branch `cc-cores`, made from `main`.**
- WORKER11 is updating the replayer on `v1.25-meshsim` and has added an opt-in `Chip.parked` in your area; keep your changes to
  `Chip` (meshsim's top level) small and mergeable, and merge `main` into your branch as it moves (merge, don't rebase). The integrator merges your
  branch into `main` when it's done.
- Quest task numbers: claim yours in the Quest repository's `NUMBERS.md` before writing a task (pull, append the next number with
  your session and task, commit, push; on a rejected push, pull and take the next). End task scripts with an explicit `exit` before
  any embedded kit.

INPUTS
- **`wip/planning/sessions/WORKER4-REPORT.md`**: how the v1.2 simulator was built (WORKER4, six phases), its suites, the boot
  ROM and `Mach.boot`, the C&C 1 Java model, the runner kits (`tools/run_quick.sh`, `run_full.sh`), and the known gaps. Read it
  first; it replaces the memory of the session that wrote the simulator. `WORKER4-QUESTIONS.md` beside it.
- `wip/spec/machine-spec.md`: the controller (4.11-4.16), peripherals (4.17), C&C 1's peripheral (6.6, 8.14), reset and boot
  (6.1-6.3), the boot ROM exactly as listed in 6.2.
- `wip/sw/RECIPES.md` and `wip/sw/recipes/`: recipe 22 (boot), 24 (the handler) and 27 (`rcp_cc1.c`, C&C 1's side of a phase).
- `wip/meshsim/`: `README.md`, `src/` (`CnC.java`, `Chip.java`, `Mach.java`, `TileControl.java`), `test/`, `sw/`, `run_tests.sh`.

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
  Java model glossed over), record a question in `WORKER14/QUESTIONS.md` with your proposed answer, mark it in the code, and carry on.
- Don't change the sort, the spec, the recipes' text or the decisions logs; changes to recipe sources only if a recipe is wrong, as a
  question first.
- **Keep the last few tool calls of every turn for committing and pushing**, and end each turn with a clean `git status`. Background
  jobs don't survive a turn: long runs finish within one or go to the runner, with a smoke-tested kit. Keep `WORKER14/NOTES.md` current.

DELIVERABLES
- The branch `cc-cores`: controllers as cores behind the switch, the firmware, the tests passing, and every existing suite unchanged
  with the switch off.
- `WORKER14/REPORT.md`: what was built, the measurements, what v1.2's known gaps become, and what v1.3's C&C 3 firmware can build on.
