# v1.2_wip: a snapshot of the v1.2 spec and recipes

A **read-only snapshot** of the v1.2 design, taken from `wip/` at commit `6decc23` (2026-10-29), for sessions that
want a stable target while the simulator is being ported. It is **not a frozen milestone**: v1.2 is frozen as
`v1.2/` once meshsim runs the sort and the replayer against it. `wip/` stays the working copy and may move ahead
of this snapshot; this folder is replaced, not edited, when a newer snapshot is wanted.

| file | what |
|---|---|
| `spec/machine-spec.md` | the machine spec, v1.2: the authority. Section 13 lists what is still open, *(Proposed)* or *(Deferred)* |
| `spec/CHANGES-v1.2.md` | every change from v1.1, section by section, each citing its decision |
| `spec/replayer-opcodes.md` | the replayer's opcode table (spec 4.19) |
| `sw/RECIPES.md` | the programming recipes, v1.2: how a tile program does each common job, with a walk-through of what each tile executes |
| `sw/CONVENTIONS.md` | the software conventions |
| `sw/mesh-v1.2-draft.h` | the draft header the recipes compile against (the generated `mesh.h` replaces it with the simulator) |
| `sw/recipes/` | the recipes' compiled sources: every code block in `RECIPES.md` is a region of one of these files (`build.sh`, `check_book.py`). **Compiled, not run**: there is no v1.2 simulator yet |
| `planning/DECISIONS-v1.2.md` | the decisions behind the spec, in the order made, for the reasoning |

The reviews of the decisions and the reports of the spec and recipe sessions are in `wip/planning/reviews/` and
`wip/planning/sessions/`.
