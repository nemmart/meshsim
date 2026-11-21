# The file system's hardware: what was worked out, for step 4

*The FS-API session, 2026-11-19 to 2026-11-20, rewritten 2026-11-20 for the design session. (owner) marks a ruling,
with its date; a dagger (†) a proposal not ruled on. Section numbers without a document are `v1.37/spec/machine-spec.md`'s.
The design behind it is `v1.40_wip/planning/FS-API.md` (the calls) and `v1.40_wip/planning/FS-DATA-PATH.md` (how the
tiles carry them out, and where everything lives). This is a list for step 4, not a design: it says what the RW unit,
the mesh, the cache and the special tiles must carry. **Nothing here changes hardware Tony has built;** every item is a
request to step 4.*

## 1. Who does what

| Tile | Its part in the file system |
| --- | --- |
| **the memory tile** | **runs the file system** (owner, 2026-11-19): paths, directories, the index, the writes' grants, a cache of 2 KiB buffers. Its peripheral is the RW unit. It never touches a byte of file data: the director, its DMA and the RW unit move it |
| **the tracer tile** | **runs the page service** (owner, 2026-11-20): the page maps, a pool for each tile that asks, prepared batches of 256 pages, and every discard from the cache. It also writes the run logs, and holds the Tracer's copy and the map for the trace |
| **the master tile** | serves the host; drives the Updater (unchanged, owner 2026-11-19), reserving and acquiring its pages from the tracer tile; on a compute tile's hardware error, closes the file system out (`fs_shutdown`) |
| **the compute tiles** | write their own logs |

## 2. The memory tile's memories (owner, 2026-11-20)

| Memory | Holds |
| --- | --- |
| **32 KB of program memory** | the file system's code |
| **32 KB of data memory** | what the core touches on every call: the fd table (512 × 16 B, 8 KiB), CLOCK's hash table and buffer records (3.3 KiB), the 66 working directories (0.3 KiB), the names' hashes of cached directory halves (about 1.6 KiB), the stash of 256 page addresses (1 KiB), four request contexts (0.3 KiB), stack and state. About 20 KiB |
| **1 MiB of SRAM** | what the DMA, the director and the RW unit move: **384 buffers of 2 KiB** (768 KiB) for the cache and the writers, and a **1 KiB path slot for each of 66 tiles** (66 KiB). About 834 KiB |

- **The core reaches the SRAM slowly** (owner, 2026-11-20): that is why the tables are in data memory, and why the
  design never has the core copy, merge or shift file data in the SRAM.
- **The SRAM has no byte addressing:** every write that lands there is a whole 128-bit word (section 4).

## 3. The RW unit (owner, 2026-11-20)

1. **The memory tile's peripheral:** its registers and events in the memory tile's window, as in 17.7.
2. **Its data input on the router's +8 link,** not "to self": the memory tile's DMA sends it a half or a page over
   +8, so that data flowing into the SRAM through the director and data drained out to the RW unit do not share the
   memory tile's own input. Both flow at once.
3. **It moves only 2 KiB and 4 KiB,** one job each, and never a line: directory entries, index entries and the
   superblock are changed in the memory tile's cached halves and written as halves.
4. **A done event for each job:** the index entry naming a new page goes out only after the page's data is in LPDDR
   (23.6.8).
5. **Its write buffer should hold two halves** †, so that the DMA never waits on LPDDR.
6. **`fs_read_page` goes straight:** a 4 KiB read job whose output goes to the caller's buffer, on another tile,
   counted on the caller's counter (FS-DATA-PATH 5). The memory tile's SRAM holds none of it. **Open:** how the
   request names the caller's buffer and counter, and how the RW unit's output is addressed to another tile (17.7's
   output queue 0 and its DMA).
7. **The rate:** the tracer tile's run logs, 24 to 48 MB in about 10 ms (owner, 2026-11-19), so 2.4 to 4.8 bytes a
   cycle at 1 GHz into LPDDR, sustained, as 2 KiB halves, while the cache serves the Tracer.
8. **The walk** reads index pages as 4 KiB jobs (removal, the mount); it never reads a data page.

## 4. The mesh and the memory tile's counters

**A write is a grant** (owner, 2026-11-20; FS-DATA-PATH 3):

1. the caller asks to write *n* bytes;
2. the memory tile answers with an address in one of its SRAM buffers, the bytes it may write (no more than the 2 KiB
   half has left), and one of **its** counters, armed for the packets;
3. the caller sends its last partial 128-bit word again, then the new bytes, as whole 128-bit words, `WRITE` packets
   through the memory tile's director, counted on that counter;
4. the counter's `counter_done` event tells the core the bytes are there.

What that asks:

- **Four grants in flight, on four of the memory tile's seven counters** (owner, 2026-11-20); the other three are its
  own.
- **Up to four reads and writes served in parallel** (owner, 2026-11-20); every other call one at a time.
- **A full queue on the way holds the sender back, never an error:** a log's bytes cannot be dropped, and a call's
  answer is all its caller has. The tracer tile to the memory tile, both special tiles, must be flow-controlled as the
  controllers' part of the mesh is.
- **The traffic:** the tracer tile into the memory tile at 2.4 to 4.8 bytes a cycle, 15 % to 30 % of a link's 16 bytes
  a cycle; the same out of the memory tile over +8 to the RW unit. The compute tiles' `printf` logs are small, and are
  not buffered on the tile (owner, 2026-11-20: a crash would lose them).
- **Requests:** 128 bits, 12 bytes of them the call's own (11.5): a path of 12 bytes or fewer travels in the request.
  A longer one, up to 1,023 bytes, lands by `WRITE` packets in the caller's 1 KiB slot before the request (owner,
  2026-11-20); **one call carrying a path in flight per tile.** Up to three requests per tile outstanding (11.3).
- **Pages between tiles:** the master tile reserves and acquires its pages from the tracer tile, up to 256 a call; the
  memory tile refills its stash of 256 with one request when it is empty; freed pages go to the tracer tile in batches,
  by address alone.

## 5. The cache

1. **A look-up port for the tracer tile,** one of ports 0, 2 and 3 (23.6.2), which have no client in v1.37: **the page
   service's discards go through it** (owner, 2026-11-20). The memory tile does not use the cache at all: directories
   and streamed files never pass through it.
2. **The discard as background work** (owner, 2026-11-19): the walker serves a discard a line at a time, and between
   its lines serves any request of the Tracer's that **hits**; a Tracer request that **misses** may wait until the
   discard is done, and so may every look-up port's request. The reply still follows the 32nd line. Today a discard
   holds the walker for its 32 lines, about 35 cycles (`v1.37/src/units/cache_ctl.cpp`), and nothing else is served. A
   change to 23.6.3 and 23.6.5.
3. **How many discards:** a page is discarded only when it is handed out again, and only if it was handed out for the
   Updater or the Tracer (`fs_acquire_cow_pages`) since its last discard (owner, 2026-11-20). The tracer tile's batches
   are prepared ahead, the master tile's preferring such pages (its refills come mostly before tracing), the memory
   tile's preferring clean ones (its come during tracing). In the emulation, a second proof on reused pages made all its
   508 discards outside tracing.
4. *Options, not requests:* the tag memory in 32 banks by the set's bits [4:0], so that a page's 32 tag rows are read,
   and its lines invalidated, in a cycle or two; an op that invalidates the whole cache between proofs (about 512
   cycles: index lines are never dirty, and the Tracer's dirty lines are thrown away).

## 6. The tracer tile's memory, for the page service

| What | Size |
| --- | --- |
| the "taken" map: a bit for each of 2²⁰ pages | 128 KiB |
| the "may be cached" map | 128 KiB |
| two prepared batches of 256 page addresses | 2 KiB |
| the pools' counts and reservations | a few bytes |
| **total** | **about 258 KiB** |

**Open:** whether it fits beside the Tracer's own use of the tracer tile's SRAM. *Proposed, not ruled* †: a summary
bit for each 128-page word of the maps, 2 KiB in the tracer tile's data memory, so that a refill finds free pages
without scanning the SRAM; whole 128-bit words of the map taken and freed at once; a high-water mark above which pages
are free and clean without a bit read.

## 7. The rest

- **The reset's cause,** hard or soft, readable by the memory tile: a hard reset formats, a soft reset mounts (owner,
  2026-11-19). The superblock cannot tell them apart.
- **The master tile's error handler makes a request and waits for its answer** (`fs_shutdown`, FS-API 3.7), with its
  interrupts off: the request unit and the counters must still work after an error has raised the NMI.
- **The free page queue gives no page back** (17.10.4): with the host's counts exact (29.7) none is left; with a wrong
  count, the pages are lost until the next hard reset. Noted, not asked.
- **The Updater is not changed** (owner, 2026-11-19): it makes the roots (`RUN_CONFIG.root` 1 and 2). The master tile
  holds each COW file it builds open with `FS_ROOT` for the run, and the tracer tile holds the Tracer's copy and the map
  for the trace (owner, 2026-11-20): a file system call, not hardware.
- **Errors in the file system's own path** (the memory tile, the RW unit, the mesh between) are not covered (owner,
  2026-11-19); what follows any error is item 181.

## 8. Open for step 4

1. How `fs_read_page` names the caller's buffer and counter, and how the RW unit addresses its output to another tile
   (3.6).
2. The RW unit's write buffer: two halves (3.5).
3. The tracer tile's SRAM: the page service's 258 KiB beside the Tracer's own (6).
4. The page service's summary bits, word-at-a-time maps and high-water mark (6).
5. The flow control from the tracer tile to the memory tile (4).
