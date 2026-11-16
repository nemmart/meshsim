# updater_wip: building the Updater and the cache controller in simulation

Started 2026-11-16 (owner). The design in `v1.4_wip/` has two controllers of moderate complexity, the Updater's and
the cache's. They are built and run here in simulation first, and what is learned is a design input to v1.4's
specification.

**It is not tied to a version** (owner). Whether the simulator work sits on v1.35 or on v1.36 is not yet chosen: it
waits on whether the RTL has firmware for the memory tile. Gold, the first piece, is the same in both.

## The order of the work

1. **Gold writes the host's frames** (`v1.4_wip/planning/HOST-FRAMES.md`), with a reader and a reference that turns
   the frames into the memory images. This is WORKER1.
2. **The Updater** in the simulator, against a simple stand-in for the cache. The Tracer running on the Updater's
   images is the check.
3. **The cache controller** replaces the stand-in.

Steps 2 and 3 are not planned in detail yet.

## What is here

| folder | what |
|---|---|
| `pipeline/gold/` | gold with the frames added: a copy of `v1.36/pipeline/gold/`, made by WORKER1 on its branch |
| `planning/sessions/` | the sessions, numbered from WORKER1 in a series of their own |

The design documents stay in `v1.4_wip/`: the three design notes, `HOST-FRAMES.md`, `UPDATER-INTERFACE.md` and the
figure.

## Sessions

The scheme is `sim/planning/sessions/README.md`'s. While a worker runs, its folder is
`planning/sessions/WORKERn/`: `PROMPT.md`, and the worker's own `NOTES.md`, `QUESTIONS.md` and `REPORT.md`. Code
goes on the worker's branch (`updater-wn`); the design session that wrote the prompt is the integrator and merges
into `main`.

| session | what | status |
|---|---|---|
| WORKER1 | gold writes the host's frames; the reader; the reference images | prompt written, not started |
