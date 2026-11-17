# v1.4_wip: the design work toward v1.4

Started 2026-11-16 (owner), to hold the v1.4 design in one place ahead of a proper specification session. Documents
and a figure only, so far: no code.

**What v1.4 is to deliver** (owner, 2026-11-15): the Tracer running on memory images that the Updater built from the
host's frames, with the Updater built in full and the path to LPDDR and Ingress as black boxes. The cache was a black
box too; since 2026-11-16 its controller is designed as a real unit. The file system comes after it.

**Its base is v1.36,** the RTL's hardware as built, frozen 2026-11-15. The design here was written in v1.35's terms
before v1.36 was read; `planning/V136-SCOPE.md` says where the two meet and where they collide.

## What is here

| file | what |
|---|---|
| `planning/DESIGN-NOTES-2026-11-14.md` | the first day's decisions: the superblock, formatting and recovery, copy-on-write, directory entries, the program's files, the working directory, the Updater's queues, the stream's credits, the control row |
| `planning/DESIGN-NOTES-2026-11-15.md` | the second day's: the v1.4 deliverable, the AXI arbiter (first called the bus driver), the cache and its look-up interface, the Updater's page faults and runs, the frames and the jump table; and what changed on 2026-11-16 |
| `planning/DESIGN-NOTES-2026-11-16.md` | the third day's: the cache as a controller and a cache memory, flush and discard, updates that write through to LPDDR and their sequence, and the order of the work (software prototypes of the two controllers before the specification) |
| `planning/HOST-FRAMES.md` | the nine frames the host sends, field by field: what gold is to produce |
| `planning/UPDATER-INTERFACE.md` | the Updater's registers, events, errors and queue ports, as walked through with the owner; the Updater is the master tile's peripheral |
| `planning/AXI-INTERFACE.md` | the path to LPDDR: a client's AXI port, the AXI arbiter (round robin, bursts cut into 32-beat pieces, an ID for each client), the controller's port, and the LPDDR model in the simulator. Agreed 2026-11-16 |
| `planning/CACHE-INTERFACE.md` | the cache from outside: the look-up port's fields and handshake, what look up, update and discard reply in every case, an update's write-through sequence, the fill port, and the stand-in. Agreed 2026-11-16 |
| `planning/INGRESS-INTERFACE.md` | how the master tile takes the host's packets out of Ingress: a service flag and a word count for each stream, reading out by DMA, the credits for the Updater; and v1.37's black box fed from a packetized file |
| `planning/HOST-LINK.md` | the link under the frames: two streams, chunks with a 128-bit header, the two checks, and recovery from a loss of sync |
| `planning/V136-SCOPE.md` | v1.36 against this design: the names, seven collisions and how the first pass ruled on them, what v1.36 already gives, what was not yet read |
| `planning/RTL-CHANGE-REQUESTS.md` | what the design asks Tony to change in hardware the RTL has built |
| `diagrams/svg/fig_m1_lpddr_path.svg` | the figure: LPDDR, the memory controller, the AXI arbiter, the cache and their clients. **Stale since 2026-11-16:** it shows the Updater on the memory tile, fed over the peripheral bus |
| `diagrams/src/fig_m1_lpddr_path.py` | its generator; it uses `wip/diagrams/src/common.py` |

## Reading order

1. `planning/V136-SCOPE.md`, for what must be ruled on first.
2. The three design notes, in order. Where they disagree, the later one stands.
3. `planning/HOST-FRAMES.md`, `planning/UPDATER-INTERFACE.md`, `planning/INGRESS-INTERFACE.md` and
   `planning/HOST-LINK.md`, with the figure.

Each document marks what the owner decided, what is proposed and what is open.

## Beside it

- `updater_wip/`: where the Updater and the cache controller are built in simulation, as a design input to v1.4;
  gold's frames first. Its sessions' prompts are there.
- `fs_wip/`: the file system's own folder, with the shell's specification. The file system is the step after v1.4's
  deliverable; its design decisions so far are in the first design note here.
- `v1.36/`: the base.
- `wip/` and `sim/`: the earlier work toward v1.4, from v1.3, and the simulator that became v1.35.

## To rebuild the figure

```
python3 diagrams/src/fig_m1_lpddr_path.py
```

It needs only Python 3. A PNG, if wanted, is a rendering of the SVG (`cairosvg`), and is not kept here.
