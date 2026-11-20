# What the file system asks of the hardware (step 4's starting point)

*From the FS-API session, 2026-11-20. A list, not a design: what the API of `v1.40_wip/planning/FS-API.md` implies the
RW unit, the mesh and the cache must carry. (owner) marks a ruling; † a proposal. Nothing here changes hardware Tony
has built: each item is a request to step 4.*

## 1. Who sends what to whom

| From | To | What | How much |
| --- | --- | --- | --- |
| every caller | the memory tile | a call: a request (11.5), its path and data by `WRITE` packets | a request a call; a path up to 1,023 bytes |
| the memory tile | the caller | the answer; a read's data (`fs_read`, `fs_read_page`, `fs_list`) | up to a page a call |
| **the tracer tile** | **the memory tile** | **the run logs, by `fs_write`** (owner, 2026-11-19) | **24 to 48 MB in about 10 ms: 2.4 to 4.8 bytes a cycle at 1 GHz, a 128-bit word every 3 to 7 cycles, for the whole trace; 15 % to 30 % of a link's 16 bytes a cycle** |
| the compute tiles | the memory tile | their own logs, `fs_fprintf` | small; at least four files open a tile (owner) |
| the memory tile | itself, by `fs_acquire_pages` | page addresses, to the master tile | up to 128 a call, as the free page queue's credits come |
| the master tile | the memory tile | `fs_shutdown`, from its error handler (3.7) | one request, one answer |

- **A full queue on the way must hold the sender back, never be an error:** a run log's bytes cannot be dropped, and
  a call's answer is all the caller has. The controllers' part of the mesh is flow-controlled (DESIGN-NOTES-2026-11-14
  section 9, owner); the tracer tile to the memory tile, both special tiles, must be too.
- **A write is a grant** (owner, 2026-11-20; `../../planning/FS-WRITE-PATH.md`): the caller asks to write *n* bytes;
  the memory tile answers with where in its scratchpad, how many, and which of its counters; the caller's `WRITE`
  packets land in a page buffer through the director, whole 128-bit words, counted on that counter. **Four grants in
  flight, on four of the memory tile's seven counters** (owner, 2026-11-20).
- **Up to three requests outstanding from a tile to the memory tile** (11.3). At one `fs_write` of 4 KB the tracer
  tile makes about 12,000 calls in 10 ms, one every 800 cycles; a call's round trip must be well under that, or the
  tracer tile must have several outstanding. The size of a call's data is open (FS-API 8.11).

## 2. The RW unit

- **The memory tile's peripheral, with its data input on the router's +8 link** (owner, 2026-11-20): its registers and
  events stay in its window (17.7); the pages the memory tile's DMA sends it arrive by +8, not "to self", so that
  they do not share the memory tile's own input with the tiles' writes. **Data flows into the scratchpad while the
  DMA drains full pages out to the RW unit** (owner, 2026-11-20).
- **Whole pages of 2 or 4 KiB** (owner, 2026-11-20), one job each; its write buffer should hold two, so that the DMA
  never waits on LPDDR †. A done event for each job: the index entry naming a page goes out only after it (23.6.8).
- **Page writes** (4 KiB) and **page reads**: a streamed file's data pages; the walk's index pages (4.4); `fs_read`.
  The read path is not designed (FS-WRITE-PATH.md 6.5).
- **Line writes and reads** (128 bytes, 8 beats): directory entries, the superblock, a writer's index lines (4.6).
- **At the tracer tile's rate** above, sustained, while the cache serves the Tracer.
- The file system never reads a page whose newest lines may be in the cache alone: the Tracer's copy is never read
  directly (FS-API 2.4).

## 3. The cache

1. **A look-up port for the memory tile,** one of ports 0, 2 and 3 (23.6.2), which have no client in v1.37: its
   discards go through it (FS-API 4.2).
2. **The discard as background work** (owner, 2026-11-19): the walker serves a discard a line at a time and, between
   its lines, serves any request of the Tracer's that **hits**. A Tracer request that **misses** may wait until the
   discard is done; so may every look-up port's request. The reply still follows the 32nd line. Today a discard holds
   the walker for its 32 lines, about 35 cycles (`cache_ctl.cpp`), and nothing else is served.
3. *Options, not requests:* the tag memory in 32 banks by the set's bits [4:0], so that a page's 32 tag rows are read,
   and its lines invalidated, in a cycle or two; an op that invalidates the whole cache between proofs (about 512
   cycles; index lines are never dirty and the Tracer's dirty lines are thrown away).

## 4. The rest

- **The reset's cause,** hard or soft, readable by the memory tile: a hard reset formats, a soft reset mounts (owner,
  2026-11-19). The superblock cannot tell them apart.
- **The master tile's error handler makes a request and waits for its answer** (`fs_shutdown`, 3.7), with its
  interrupts off: the request unit and the counters must still work after an error has raised the NMI.
- **The free page queue gives no page back** (17.10.4): with the host's counts exact (29.7) none is left; with a
  wrong count, the pages are lost to the file system until the next hard reset. Noted, not asked.
- **The Updater is not changed** (owner, 2026-11-19): it keeps making the roots (`RUN_CONFIG.root` 1 and 2).
