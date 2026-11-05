# Worker 24: a block diagram of the Tracer

You are drawing the **Tracer**, C&C 3's peripheral in meshsim's machine (an ASIC for proving the Jolt zkVM): the RV64 row
machine that executes the guest program and writes each window's run log for the tiles' replayers. The owner wants a block
diagram to solidify the design, and it will also serve the Tracer's spec chapter (4.21, WORKER21). Its design is decided in
`wip/planning/DECISIONS-v1.3.md` sections 1-25 (13 and 17-25 supersede parts of 1-12, marked in place); spec 4.17
(peripherals) and 4.19 (the replayer) give the surrounding machine.

REPOSITORY: https://github.com/nemmart/meshsim (clone with the meshsim token from your user preferences, as
`https://x-access-token:<token>@github.com/...`; never write a token into a file). Write only in `wip/diagrams/` (new files) and
your `WORKER24/`, committing to `main` (`git pull --rebase` before every push).

THE STYLE: the figure set in `wip/diagrams/` (`README.md`; generators in `src/`, built by `src/build.sh`, SVGs in `svg/`, PNGs
rendered by cairosvg at twice size). Learn it before drawing: `common.py` (the Fig class, fonts, the palette and what each colour
means, the dash styles for read, write, control, proposed and open), `rtl.py` (the RTL figures' conventions and `finish()`), and
two or three figures as models (1.3 the tile and 3.1 the controller for block level; 3.2 or 3.4 for RTL detail). Each figure is
one self-contained SVG with its title, subtitle naming its sources, caption and source line; 1300 px wide; the README lists
every figure with its caption. Use the existing helpers and palette; add to them only if you must, and say so.

THE OWNER'S PICTURE (the starting point; check it against the decisions):
- **Centre: the RV64 core**, a large box. **Three buses out to the left**, Harvard style: **instruction** and **data**, each
  ending in an L1 cache (128 B lines, 128 lines, 16 KB, fully associative, single-cycle), and **pc to `instr_idx`**, ending in the
  64-entry jump LRU with its hash-probe unit behind it (a multiply, a shift, one line read, 16 comparators; section 18).
- **Out of the left of each of the three boxes, a fill bus**: one 128 B line per transfer, as (root, depth, line) requests,
  reads and the data cache's dirty write-backs; it runs up and down the diagram, and at the top an arrow to **the memory system,
  a black box** (the translator, its TLB and the shared 512 KB cache behind it).
- **The range comparator** on the data path, `(address − lowest_address) < 8·K`, beside the L1 lookup (section 25).
- **Right: everything that interfaces with C&C 3 (RV32)**, on **a peripheral bus** that various units read and write; **the
  bus also goes into the RV64 core**, since part 0's state is the working state (pc, `instr_idx`, offset, registers 0-127 read
  and written directly; DECISIONS-v1.26 item 12, v1.3 sections 23-24). On it: part 0 (the 68-word record, the decode table, the
  template ROM), the read queue (advice) and the expansion queue, the control window (`RUN_CYCLES`, `ROWS_RUN`,
  `EXPANSION_OFFSET`/`OPCODE`, `STATUS`, `ERRORS`, `EVENTS` and the six wake flags, the pulses), and the output side: the code
  FIFO (32 bits) and log FIFO (65 bits), the two compressor sets (set 1 sizing, set 2 compressing on the way out), the packers,
  output queues 0-3 with their lengths, and the DMA to the tiles.

THE WORK
1. **Review**: read sections 1-25 for every component, bus and width the picture needs, and list what the decisions leave
   unsaid or contradictory (in `WORKER24/NOTES.md`). Don't resolve them: draw them as open (the "open" dash) and ask.
2. **Plan**: propose the figure (or figures: a block-level 9.1 first, RTL detail later if the owner wants it), its layout,
   which colours mean what, and the caption, **and wait for the owner's go-ahead**. A rough sketch (an early SVG) is welcome with
   the plan, since the owner is refining his picture as he goes.
3. **Draw** `wip/diagrams/src/fig9_1_tracer.py` (and any later ones), build, check the PNG renders, add the rows to the
   README; iterate with the owner.

RULES
- Draw what is decided; mark proposals and open points as such, never as decided.
- Keep the last tool calls of every turn for committing and pushing, and end each turn with a clean `git status`.

DELIVERABLES: the generator(s), SVG and PNG in `wip/diagrams/`, the README rows, `WORKER24/NOTES.md` (the review's findings).
