# The RW session (v1.40): the design of the RW system

**The project.** meshsim is the simulator and specification for a 64-tile mesh processor, an ASIC that proves
the Jolt zkVM. A proof is cut into segments, and each ASIC proves one. The host sends the data that builds a
segment's memory images; the master tile's firmware takes it in, the Updater builds the images in LPDDR as
copy-on-write files, and the Tracer runs on them through the cache. v1.37, frozen on 2026-11-19, is the first
version with that whole path. The repository is `nemmart/meshsim`.

**v1.40 is the next version** (`v1.40_wip/MILESTONE-v1.40.md`): in software a file system and a small shell,
and in hardware the RW unit and changes to Ingress/Egress. The file system is designed, and it runs in
software: its API, its data path and its firmware are written down, a C++ emulation passes ten workflows, and
the shell runs on the emulation. **None of it runs on the machine,** because the hardware it needs is not
designed.

**Your part is that hardware: the RW system.** The owner: "I want to begin designing the RW system." You design
it with the owner, in conversation, one question at a time. It is a hardware design session: no code, and no
specification text until the owner asks for it.

## What the RW system is

It is not settled where its edge is; that is one of the first things to agree. As the file system's design
leaves it, it is at least:

- **the RW unit,** the memory tile's peripheral and a client of the AXI arbiter, which moves file data between
  the mesh and LPDDR. v1.37's is v1.36's RW engine, unchanged but for its place (17.7), and nothing in v1.37's
  proof runs uses it. The owner, of its figures: "All that stuff will get reworked once we have a real RW engine
  anyway." Tony (the RTL engineer) expects to redesign it in full;
- **its paths on the mesh:** the data into the memory tile's SRAM through the director, out of it to the RW
  unit, and from the RW unit straight to another tile;
- **the mesh's two regimes.** The owner: "With it we're going to add the regimes to the compute tile for router
  queue full = error versus router queue full = flow control." Today a full router queue is always an error
  (chapter 16). `v1.4_wip/planning/TO-ADD.md`, entry 3;
- **what the file system's data path asks besides:** the memory tile's counters for the write grants, the flow
  control from the tracer tile to the memory tile, and a change to the cache's discard.

## What is already ruled

`v1.40_wip/sessions/FS-API/FOR-THE-HARDWARE.md` is the list the file system's session left for you, with each
of the owner's rulings marked and dated. The ones that shape the RW unit most:

- it is the memory tile's peripheral;
- **its data input is on the router's +8 link,** not "to self", so that data flowing into the SRAM and data
  drained out to the RW unit do not share the memory tile's own input;
- **it moves only 2 KiB and 4 KiB,** one job each, never a line;
- a write is a grant: the caller sends whole 128-bit words straight into one of the memory tile's SRAM
  buffers, counted on one of the memory tile's counters; four grants in flight;
- `fs_read_page` goes straight from the RW unit to the caller's tile;
- the tracer tile's run logs set the rate: 24 to 48 MB in about 10 ms.

A ruling stands until the owner changes it. A dagger (†) in those documents marks a proposal that was never
ruled on: it is a question, not a decision.

## What to read, in this order

**The hardware as it is: v1.37's specification** (`v1.37/spec/`). It is frozen and you do not edit it.

1. `README.md`, `GUIDELINES.md`, `OVERVIEW.md`.
2. `machine-spec.md`, whole. It is long; these chapters are the ones the RW system touches, and want a slow
   reading: 2 (tiles) and 3 (interfaces); 4 (the packet); 7 (the tile's control unit, its counters and events);
   8 to 15 (the DMA, write, read, request, receive, gather and inject units, and the scratchpad); **16 (the
   router: ports, links, the attach ports, the line between the special tiles, pacing, overflow);** 17.1 to 17.5
   (what every peripheral has) and **17.7 (the RW engine as it is);** 17.10 (the Updater); 20 and 21 (the
   invariants and the timing); **23 (the memory system: the arbiter, LPDDR, the cache, and what the cache asks
   of its clients);** 26 (the open items; 64, 119, 180, 181 and 182 bear on you).
3. `CHANGES-v1.36-to-v1.37.md`, for what moved.

**The file system's design** (`v1.40_wip/`):

4. `sessions/FS-API/REPORT.md`, then `FOR-THE-HARDWARE.md`.
5. `planning/FS-DATA-PATH.md`: how the tiles carry the calls out. **This is the document your design must
   serve.**
6. `planning/FS-API.md`: the system and its flows (section 3), the structures, the calls.
7. `planning/FS-FIRMWARE.md`: the firmware tile by tile: the memory tile's event loop, the tracer tile's page
   service, the master tile's changes.
8. `sessions/FS-API/NOTES.md`, section 3: the 49 questions and how each ended.

**The firmware and the simulator as built:**

9. `v1.37/fw/cc/`: `memory.c` and `cc_mem.c` (the memory tile's, serving `READ` and `WRITE` with the RW
   engine), `master.c` (the master tile's).
10. `v1.37/src/units/`: `rw_engine.h`, `write_engine.h`, `router.*`, `director.h`, `inject.*`, `axi_arbiter.*`,
    `axi_port.h`, `cache_ctl.*`, `tile_ctl.*`.
11. `v1.40_wip/fs/fs_emu.cpp`: the file system in software, laid out as the memory tile's would be. Every LPDDR
    transfer it makes is 2 or 4 KiB and counted; `tests/out/*.log` are the exact call sequences of ten
    workflows.

**The earlier thinking on the mesh's regimes and this peripheral:**

12. `v1.4_wip/planning/TO-ADD.md`, entry 3; `DESIGN-NOTES-2026-11-14.md`, sections 7 to 9;
    `DESIGN-NOTES-2026-11-16.md`, section 41; `RTL-CHANGE-REQUESTS.md`, for how a request to Tony is written.

**Which wins when two disagree:** the owner's ruling, the latest first; then v1.37's specification, for the
hardware as it is; then the file system's documents; then the older design notes.

## How to work

1. **Read in. Then tell the owner, in a few lines,** what you understand the RW system must do and what you
   think is the first thing to decide, **and wait.**
2. **The flows first, at a high level,** before any register or signal: for each kind of traffic (a write's
   grant and its words, a full half out to LPDDR, `fs_read_page` to another tile, an index page read for a walk,
   the run logs at their rate), who sends what to whom, over which links, and what holds it back when
   something is full. The last session's brief asked for the detail first and the owner turned it round.
3. **One question at a time,** the ones that shape the most first: the question in a sentence or two, your
   recommendation, and why. Never a list of questions.
4. **Record each ruling the day it is made,** in your design document, with (owner) and the date, in the
   owner's words where they are short. Until then a point carries a dagger (†).
5. **When the design is whole,** the owner will say what is written next: the specification's sections, the
   requests for Tony, the simulator.

## The questions you inherit

Bring them in the order you judge shapes the most. From the file system's documents:

- **How `fs_read_page` names the caller's buffer and counter,** and how the RW unit addresses its output to
  another tile (FOR-THE-HARDWARE 3.6): the one part of the data path not designed.
- **How the memory tile knows a long path has landed** in the caller's slot: the request travels on the
  request tree and the `WRITE` packets on the mesh, with no order between them (FS-FIRMWARE 2.6).
- **The flow control from the tracer tile to the memory tile,** and the two regimes on a compute tile: where a
  full queue is an error and where it holds the sender back.
- **The RW unit's write buffer:** two halves †.
- **The cache's discard as background work** (FOR-THE-HARDWARE 5): the owner has ruled it; whether its design
  is yours or another session's is the owner's to say.
- **The tracer tile's SRAM:** whether the page service's 258 KiB fits beside the Tracer's own use.

## Rules

- **A question from the owner is not a go-ahead.** Do not write a design document's section, a specification
  or code until a plan is agreed and the owner has said go ahead. Your notes are yours to keep as you go.
- **Keep replies short.** No paragraphs of agreement. A table where there is something to compare.
- **Do not add what was not asked.** Flow control goes only where the owner puts it; a queue's size is derived
  or it is a question; error recovery is not designed (item 181) and is not to be improvised.
- **Say what a thing costs** where you can: flip-flops, memory bits, cycles, a link's share. The owner weighs
  designs by it. `v1.4_wip/planning/CONTROLLER-SIZING.md` shows the method.
- **Hardware Tony has built is changed only by a request to him.** The router, the tile's units and the cache's
  controller are built or specified for RTL; a change to one is drafted as an entry for
  `v1.4_wip/planning/RTL-CHANGE-REQUESTS.md`, in its form, and the owner sends it. The RW unit he has not
  redesigned yet: it is ours to specify.
- **The file system's documents are not yours to change.** Where your design needs the API or the data path
  to change, that is a question for the owner, and a note for the design session.
- **One name for one thing.** 128 bits is "a 128-bit word". The special tiles are the master, memory and tracer
  tiles; the owner also says C&C 1, 2 and 3.
- **A frozen folder is never edited** (`v1.37/` and earlier).
- **Commit to `main`,** in your own files only: `v1.40_wip/planning/RW-SYSTEM.md` and
  `v1.40_wip/sessions/RW/`. Pull before each push; never rebase, never force.
- Attach the repository to the session to push. **Never write a token into a file or repeat one in a reply.**
- Never `pkill -f`: use `pgrep` and kill by PID.
- End each turn with a clean `git status`.

## Not your work

The file system's API, its emulation and its firmware. The shell. The changes to Ingress/Egress (the links'
statistics, the watchdog): another session's. The simulator, until the owner asks.

## What you leave

| file | what |
|---|---|
| `v1.40_wip/planning/RW-SYSTEM.md` | the design as it is agreed: the flows, the RW unit, its paths on the mesh, the regimes; every ruling marked (owner) with its date, every proposal † |
| `v1.40_wip/sessions/RW/NOTES.md` | what you read and found; the questions and how each ended; what in the file system's documents your design would change |
| `v1.40_wip/sessions/RW/REQUESTS-FOR-TONY.md` | each change to hardware he has built, drafted in the form of `RTL-CHANGE-REQUESTS.md` |
| `v1.40_wip/sessions/RW/REPORT.md` | at the end: what was designed, what is open, what the next sessions need to know |
