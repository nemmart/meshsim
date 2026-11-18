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
4. **The threshold** in Ingress's black box and the firmware. **Done** (WORKER4), and since replaced in the design
   by item 6.
5. **The two real controllers, by two workers who share no code** (owner): the cache's (WORKER5) and the Updater's
   (WORKER6), each behind a setting beside its black box. WORKER5 first wrote the interface between them,
   `AXI-INTERFACE.md` and `CACHE-INTERFACE.md`, now agreed. Ingress stays a black box.
6. **The forward count** (owner, 2026-11-16; `v1.4_wip/planning/INGRESS-INTERFACE.md`): Ingress moves the bulk
   data to the Updater itself, over a flow-controlled word port, and the firmware wakes once a frame. It is how
   the owner's target is to be met: the Updater at most 10% of the master tile's load. WORKER7, in v1.37's Ingress
   black box and firmware; WORKER6 builds its controller to the same port.
7. **Joining them:** the Updater on the arbiter, talking to the real cache, fed by Ingress, on the proof runs.
8. **The rest of Ingress/Egress** as designed on 2026-11-17 (`v1.4_wip/planning/INGRESS-INTERFACE.md`,
   `HOST-LINK.md`): the link header's 18-bit numbers and hops field; v1.36's stream words taken out; the UART
   redesigned; Egress. `v1.4_wip/planning/DESIGN-NOTES-2026-11-16.md`, section 21, has where each stands and the
   order.

**The milestone this is all heading for is `planning/MILESTONE-v1.37.md`** (owner, 2026-11-17): two ASICs on a
chain behind a driver, each building its images with the real controllers, tracing its segment, and sending a reply
frame back. It has the picture, the steps and who does each.

## What is here

| folder | what |
|---|---|
| `pipeline/gold/` | gold with the frames (`frames`, `frames-read`, `frames-images`; WORKER1) and the link's packets (`packets`, `packets-read`, the vectors in `test/packets/`; WORKER2): a copy of `v1.36/pipeline/gold/` with their work; its `README.md` says how to run it |
| `planning/MILESTONE-v1.37.md` | the plan for v1.37: the daisy chain, the driver, the reply frame, the order of the work |
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
| WORKER5 | Part A: the two interface documents, `AXI-INTERFACE.md` and `CACHE-INTERFACE.md`, with their headers, agreed with the owner. Part B: the cache's real controller with its cache memory, the AXI arbiter and the LPDDR model, in the shape of hardware, behind a setting (`MachConfig::cache_ctl`, off by default); the Tracer runs on it in all 37 proof runs, every image equal to gold's; against v1.37's model the cycles are −2.7% to +4.4%; with the setting off nothing changes, to the cycle (Quests 209 to 212). Closed and merged 2026-11-16 | `WORKER5-REPORT.md`, `WORKER5-NOTES.md`, `WORKER5-QUESTIONS.md` |
| WORKER6 | the Updater's real controller, in the shape of hardware, behind the black box's registers, its data by the word port (with v1.37's segment queue as a shim in front until WORKER7's work lands); built from the documents alone and tested against a cache stand-in. Alone it gives gold's images on every fixture; against the black box the stores are equal byte for byte, on gold's frames and on 90 random scenarios; 14 planted bugs each caught; in the chip the 37 proof runs pass with its timing fixed and varied (Quests 216 to 219). It found that the black box counts one read too few when a run ends inside a run of values. Closed and merged 2026-11-17 | `WORKER6-REPORT.md`, `WORKER6-NOTES.md` |
| WORKER7 | the forward count in v1.37: Ingress's black box forwards the bulk data to the Updater over the word port, or discards it, and the master tile's firmware wakes once a frame (46 to 54 wakes a proof where there were 79 to 97; the master awake 0.2 to 0.6% of a proof); then the link's header with 18-bit numbers and the hops field, in gold's packetizer and reader, the vectors and v1.37. Quests 213 to 215, 220 and 221 pass. (Its first brief, tuning the firmware, was withdrawn before it started.) Closed 2026-11-17; merged with WORKER9's step 1 | `WORKER7-REPORT.md`, `WORKER7-NOTES.md`, `WORKER7-GUESTS-FIGURES.md` |
| WORKER8 | a robust simulation of the host link's protocol, on its own: a link end as a state machine, chains of up to 16, wires that misbehave, consumers that stall, random runs and an exhaustive search of a shrunken model; every gap in `HOST-LINK.md` reported with a rule proposed | done, on branch `updater-w8` (`WORKER8/`), ready to merge: the protocol as first written did not hold, was mended with the owner rule by rule, and now holds in every search and in the runner's campaigns (Quests 231, 232, 237). The link end has an interface to build on (`link/hostlink.h`) and its hardware is counted (`WORKER8/HARDWARE.md`). The last long runs with the fast unit are still to come (Quests 238 to 242) |
| WORKER9 | joining the controllers: `updater-w7` merged and the shim taken out; the Updater's controller fed by Ingress's forwarder, on the real arbiter and the real cache, with the Tracer on the same cache; the proof runs with both controllers on | step 1 done and merged 2026-11-17 (`updater-w7` merged, the shim out, the controller fed by the forward count; Quests 222 to 226 pass, the proof runs' cycles equal to Quest 220's with every setting off); step 2, the joint, running on branch `updater-w9` (`WORKER9/`) |
| WORKER10 | the wire between processes, for the chain: shared memory with a sync every 64 cycles, `--clients`, `--client` and `--link`, a launcher, stand-in ends and a one-process reference that the processes must match to the cycle; no chip and no protocol | built, on branch `updater-w10` (`WORKER10/`), ready to merge: every check passes where it ran. No campaign on the runner (owner, 2026-11-18) |
| SPEC | v1.37's specification for the full system, written first (owner, 2026-11-18): `v1.37_wip/spec/` brought from v1.36's, from the design documents; every † ruled on or made an open item; the list of where the simulator differs | to be started: `SPEC/PROMPT.md` |
| INTEGRATOR | everything that is left of building v1.37, in one session: the two merges; the Updater's controller on the real arbiter and cache; Egress and the reply frame; the chain in the IEU black box; the driver; the three processes; the freeze | **on hold** until the specification exists; its brief, `INTEGRATOR/PROMPT.md`, is then rewritten |
