# v1.4_wip: the design work toward v1.4

Started 2026-11-16 (owner), to hold the v1.4 design in one place ahead of a proper specification session. Documents
and a figure only, so far: no code.

**What v1.4 is to deliver** (owner, 2026-11-15): the Tracer running on memory images that the Updater built from the
host's frames, with the Updater built in full and the cache, the path to LPDDR and Ingress as black boxes. The file
system comes after it.

**Its base is v1.36,** the RTL's hardware as built, frozen 2026-11-15. The design here was written in v1.35's terms
before v1.36 was read; `planning/V136-SCOPE.md` says where the two meet and where they collide.

## What is here

| file | what |
|---|---|
| `planning/DESIGN-NOTES-2026-11-14.md` | the first day's decisions: the superblock, formatting and recovery, copy-on-write, directory entries, the program's files, the working directory, the Updater's queues, the stream's credits, the control row |
| `planning/DESIGN-NOTES-2026-11-15.md` | the second day's: the v1.4 deliverable, the AXI arbiter (first called the bus driver), the cache and its look-up interface, the Updater's page faults and runs, the frames and the jump table; and what changed on 2026-11-16 |
| `planning/HOST-FRAMES.md` | the nine frames the host sends, field by field: what gold is to produce |
| `planning/UPDATER-INTERFACE.md` | the Updater's registers, events, errors and queue ports |
| `planning/V136-SCOPE.md` | v1.36 against this design: the names, seven collisions and how the first pass ruled on them, what v1.36 already gives, what was not yet read |
| `planning/RTL-CHANGE-REQUESTS.md` | what the design asks Tony to change in hardware the RTL has built |
| `diagrams/svg/fig_m1_lpddr_path.svg` | the figure: LPDDR, the memory controller, the AXI arbiter, the cache and their clients |
| `diagrams/src/fig_m1_lpddr_path.py` | its generator; it uses `wip/diagrams/src/common.py` |

## Reading order

1. `planning/V136-SCOPE.md`, for what must be ruled on first.
2. The two design notes, in order. Where they disagree, the second is later.
3. `planning/HOST-FRAMES.md` and `planning/UPDATER-INTERFACE.md`, with the figure.

Each document marks what the owner decided, what is proposed and what is open.

## Beside it

- `fs_wip/`: the file system's own folder, with the shell's specification. The file system is the step after v1.4's
  deliverable; its design decisions so far are in the first design note here.
- `v1.36/`: the base.
- `wip/` and `sim/`: the earlier work toward v1.4, from v1.3, and the simulator that became v1.35.

## To rebuild the figure

```
python3 diagrams/src/fig_m1_lpddr_path.py
```

It needs only Python 3. A PNG, if wanted, is a rendering of the SVG (`cairosvg`), and is not kept here.
