# updater_wip: building the Updater and the cache controller in simulation

Started 2026-11-16 (owner). The design in `v1.4_wip/` has two controllers of moderate complexity, the Updater's and
the cache's. They are built and run here in simulation first, and what is learned is a design input to v1.4's
specification.

**The folder is not a version** (owner). **The simulator work is built on v1.36** (owner, 2026-11-16): its firmware
arrived that day, with a program for the memory tile, and its own checks pass on the owner's runner (Quest 198 and
199; `tasks/v136-check/`). Gold, the first piece, is the same in v1.35 and v1.36.

## The order of the work

The owner's plan for v1.37 (`v1.4_wip/planning/DESIGN-NOTES-2026-11-16.md`, section 14): something that works end to
end first, with software black boxes, and the real controllers afterwards.

1. **Gold writes the host's frames** (`v1.4_wip/planning/HOST-FRAMES.md`), with a reader and a reference that turns
   the frames into the memory images. **Done** (WORKER1).
2. **The packetizer** wraps gold's frame file in the link's packets (`v1.4_wip/planning/HOST-LINK.md`). **Done**
   (WORKER2).
3. **v1.37, done** (WORKER3; `v1.37_wip/RUNNING-v1.37.md` says how to run it): the simulator is started with the packet file; the master tile's real firmware reads the frames out
   of Ingress and drives the Updater; the Tracer traces on the images the Updater built. Ingress and the Updater
   are software black boxes behind their real registers (`INGRESS-INTERFACE.md`, `UPDATER-INTERFACE.md`); the cache
   is v1.36's software model; nothing runs on the memory tile. WORKER3, in `v1.37_wip/`.
4. **The threshold** in Ingress's black box and the firmware (`v1.4_wip/planning/INGRESS-INTERFACE.md`). **Done**
   (WORKER4). The firmware is still to be tuned to the owner's target: the Updater at most 10% of the master tile's
   load.
5. **The two real controllers, by two workers who share no code** (owner): the cache's (WORKER5) and the Updater's
   (WORKER6), each behind a setting beside its black box. WORKER5 first writes the interface between them,
   `AXI-INTERFACE.md` and `CACHE-INTERFACE.md`; when those are agreed WORKER6 starts. Ingress stays a black box.
6. **Joining them:** the Updater on the arbiter, talking to the real cache, on the proof runs.

## What is here

| folder | what |
|---|---|
| `pipeline/gold/` | gold with the frames (`frames`, `frames-read`, `frames-images`; WORKER1) and the link's packets (`packets`, `packets-read`, the vectors in `test/packets/`; WORKER2): a copy of `v1.36/pipeline/gold/` with their work; its `README.md` says how to run it |
| `planning/sessions/` | the sessions, numbered from WORKER1 in a series of their own |
| `../v1.37_wip/` | the simulator and firmware for v1.37: v1.36 copied unchanged (main, 67aaeb86), then WORKER3's work |
| `tasks/v137-check/` | the same for v1.37, with the proof steps: the host's side made on the runner, then `proof-real`, `proof-matrix` and `proof-29` (WORKER3) |
| `tasks/v136-check/` | the generator of v1.36's own checks as Quest tasks: the suites, with the small inputs built on the runner, and `make check` |

The design documents stay in `v1.4_wip/`: the three design notes, `HOST-FRAMES.md`, `HOST-LINK.md`,
`INGRESS-INTERFACE.md`, `UPDATER-INTERFACE.md` and the figure.

**Two golds, for now:** the frames are in `pipeline/gold/` here. `v1.37_wip/pipeline/gold/` is v1.36's, without
them, and is what v1.36's own inputs and suites use. They are brought together when v1.37 is frozen.

## Sessions

The scheme is `sim/planning/sessions/README.md`'s. While a worker runs, its folder is
`planning/sessions/WORKERn/`: `PROMPT.md`, and the worker's own `NOTES.md`, `QUESTIONS.md` and `REPORT.md`. Code
goes on the worker's branch (`updater-wn`); the design session that wrote the prompt is the integrator and merges
into `main`.

| session | what it did | kept in `planning/sessions/` |
|---|---|---|
| WORKER1 | gold writes the host's frames as `HOST-FRAMES.md` stands after the owner's rulings; the reader with its checks; the reference images on copy-on-write trees, with page counts checked two ways; both workloads at 2²⁹ (Quests 197 and 200). Closed and merged 2026-11-16 | `WORKER1-REPORT.md`, `WORKER1-NOTES.md`, `WORKER1-QUESTIONS.md`, `WORKER1-NEXT.md` (the rulings), `WORKER1-tasks/` |
| WORKER2 | the packetizer (`packets`), its reader (`packets-read`) and the test vectors (`pipeline/gold/test/packets/`), the two sides sharing no code; every frame file round-trips, every single-bit change of the small files is refused, both workloads at 2²⁹ (Quest 201); the packets add 3.13%. Closed and merged 2026-11-16 | `WORKER2-REPORT.md`, `WORKER2-NOTES.md`, `WORKER2-QUESTIONS.md`, `WORKER2-tasks/` |
| WORKER3 | v1.37 in `v1.37_wip/`: the Ingress and Updater black boxes behind their real registers, the master tile's firmware (`fw/cc/master.c`) with the page map, and the Tracer running on the images the Updater built from a packet file; 37 proof runs on both workloads, small and at 2²⁹, every image equal to gold's; all of v1.36's checks still pass (Quests 202 to 205). Closed and merged 2026-11-16 | `WORKER3-REPORT.md`, `WORKER3-NOTES.md`, `WORKER3-QUESTIONS.md`; its tasks are `tasks/v137-check/` |
| WORKER4 | the threshold in v1.37's Ingress black box and the master tile's firmware, Ingress's buffers at 16 KB a stream, the Updater's segment queue at 8 KB; the master's cost a packet's worth fell from about 330 cycles awake to about 100; 174 more runs with the link at 2, 4 and 8 bytes a cycle, where the Updater becomes the limit (Quests 206 to 208). Closed and merged 2026-11-16 | `WORKER4-REPORT.md`, `WORKER4-NOTES.md` |
| WORKER5 | Part A, done and merged: the two interface documents, `AXI-INTERFACE.md` and `CACHE-INTERFACE.md`, with their headers, agreed with the owner. Part B, running: the cache's real controller, with the cache memory, the AXI arbiter and the LPDDR model | on branch `updater-w5` (`WORKER5/`) |
| WORKER6 | the Updater's real controller, in the shape of hardware, behind the black box's registers; built from the documents alone and tested against a cache stand-in | prompt written (`WORKER6/PROMPT.md`), not started |
| WORKER7 | tuning the master tile's firmware to the owner's target: feeding the Updater at most 10% of the master tile's load | prompt written (`WORKER7/PROMPT.md`), not started |
