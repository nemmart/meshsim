# updater_wip: building the Updater and the cache controller in simulation

Started 2026-11-16 (owner). The design in `v1.4_wip/` has two controllers of moderate complexity, the Updater's and
the cache's. They are built and run here in simulation first, and what is learned is a design input to v1.4's
specification.

**The folder is not a version** (owner). **The simulator work is built on v1.36** (owner, 2026-11-16): its firmware
arrived that day, with a program for the memory tile, and its own checks pass on the owner's runner (Quest 198 and
199; `tasks/v136-check/`). Gold, the first piece, is the same in v1.35 and v1.36.

## The order of the work

1. **Gold writes the host's frames** (`v1.4_wip/planning/HOST-FRAMES.md`), with a reader and a reference that turns
   the frames into the memory images. **Done** (WORKER1).
2. **The Updater** in the simulator, against a simple stand-in for the cache. The Tracer running on the Updater's
   images is the check.
3. **The cache controller** replaces the stand-in.

Steps 2 and 3 are not planned in detail yet.

## What is here

| folder | what |
|---|---|
| `pipeline/gold/` | gold with the frames added (`frames`, `frames-read`, `frames-images`): a copy of `v1.36/pipeline/gold/` with WORKER1's work; its `README.md` says how to run it |
| `planning/sessions/` | the sessions, numbered from WORKER1 in a series of their own |
| `tasks/v136-check/` | the generator of v1.36's own checks as Quest tasks: the suites, with the small inputs built on the runner, and `make check` |

The design documents stay in `v1.4_wip/`: the three design notes, `HOST-FRAMES.md`, `UPDATER-INTERFACE.md` and the
figure.

## Sessions

The scheme is `sim/planning/sessions/README.md`'s. While a worker runs, its folder is
`planning/sessions/WORKERn/`: `PROMPT.md`, and the worker's own `NOTES.md`, `QUESTIONS.md` and `REPORT.md`. Code
goes on the worker's branch (`updater-wn`); the design session that wrote the prompt is the integrator and merges
into `main`.

| session | what it did | kept in `planning/sessions/` |
|---|---|---|
| WORKER1 | gold writes the host's frames as `HOST-FRAMES.md` stands after the owner's rulings; the reader with its checks; the reference images on copy-on-write trees, with page counts checked two ways; both workloads at 2²⁹ (Quests 197 and 200). Closed and merged 2026-11-16 | `WORKER1-REPORT.md`, `WORKER1-NOTES.md`, `WORKER1-QUESTIONS.md`, `WORKER1-NEXT.md` (the rulings), `WORKER1-tasks/` |
