# The data path: how a tile's bytes reach a file and come back, and where it all lives

*The FS-API session, 2026-11-20. (owner) marks a ruling, with its date; a dagger (†) a proposal not ruled on. For
step 4 (the RW unit and the mesh) and step 5 (the device's file system). This is how the memory tile, the tracer tile
and a caller's library carry out the calls of `FS-API.md`, and how their memories are spent.*

## 1. Why

The memory tile serves every tile's file work: 64 compute tiles writing their `printf` logs, and the tracer tile
writing 24 to 48 MB of run logs in about 10 ms (FS-API 3.4). It must not copy bytes, and it cannot shift them: the
scratchpad has no byte addressing and no byte moves, only 128-bit words. A `printf` of 37 bytes appended at byte 21 of
a word would otherwise cost the memory tile a read of that word, a merge and a shift of everything after it. And it
must not read LPDDR over and over for the same directory and index lines.

**The rule behind it** (owner, 2026-11-20): work moves from the memory tile to the callers wherever it can. A little
more work on a compute tile costs little, since it is spread over 64 of them; the memory tile serves them all.

## 2. The pieces

| Piece | Its part |
| --- | --- |
| **the caller's library** (a compute tile's, the tracer tile's, the shell's) | formats and sends the bytes; keeps the file's last partial word (3) |
| **the memory tile's core** | the file system: paths, directories, the index, grants, the cache (6) |
| **its SRAM** (1 MiB, owner, 2026-11-20) | **the pool of 2 KiB buffers** shared by the cache and the writers (6), the landing slots for paths (8): what the DMA, the director and the RW unit move. **The core reaches it slowly** (owner, 2026-11-20) |
| **its local memory** (owner, 2026-11-20) | **32 KB of program memory** and **32 KB of data memory**: its code, and what it touches on every call: the fd table, CLOCK's tables, the working directories (8) |
| **its DMA** | sends a full 2 KiB half from the SRAM to the RW unit |
| **the RW unit** | the memory tile's peripheral (owner, 2026-11-20): its registers and events in its window, as in v1.37's 17.7; **its data input on the router's +8 link** (owner), so that a half sent to it does not come back in through the memory tile's own input. **It moves only 2 KiB and 4 KiB** (owner, 2026-11-20) |
| **the tracer tile** | **the page service** (7): the page maps, the two pools, their prepared batches, the discards (owner, 2026-11-20) |

## 3. A write, step by step (owner, 2026-11-20)

The caller keeps **its tail**: the last *length* mod 16 bytes of the file, the part of its last 128-bit word that is
filled. It can, since a file open for writing has one writer (FS-API 5.2). It learns the length when it opens the
file and keeps it from then on †.

1. **The request:** "write *n* bytes to fd".
2. **The answer:** where to write them, how many it may write, and on which counter:
   - the address: the writer's current half's buffer + (*length* mod 2,048, rounded down to 16);
   - *g*, the bytes granted: at most *n*, and no more than the half has left (2,048 − *length* mod 2,048);
   - a counter of the memory tile's, armed for ⌈((*length* mod 16) + *g*) / 16⌉ packets.
3. **The data:** the caller sends its tail followed by the *g* bytes, as whole 128-bit words, `WRITE` packets from the
   address up, each counted on that counter. The first word sends the tail's bytes again over themselves; the last
   word's bytes past the new length are whatever the caller had, and the next write covers them.
4. **Done:** the counter reaches 0 (its `counter_done` event); the core adds *g* to the length. The caller's tail is
   now the last (*length* mod 16) bytes it sent.
5. **A full half** goes out (4), and the writer's next grant lands in a fresh buffer from the pool.
6. **What is left,** *n* − *g*, the caller asks for again. **One grant at a time for an fd,** so a file's bytes arrive
   in order.

So no byte is copied, merged or shifted on the memory tile: every word arrives whole, at its place in the half.

**Grants in flight: four,** on four of the memory tile's seven counters (12.2), the other three left for its own work
(owner, 2026-11-20). A request that finds all four in use waits, first come first served †. A grant is at most a
half, about 430 cycles at the tracer tile's rate and far less for a `printf`. A tile has at most three requests
outstanding to the memory tile (11.3).

## 4. A full half out, while the next fills (owner, 2026-11-20)

**The writer builds its bytes up to 2 KiB** (owner, 2026-11-20), and the half goes to LPDDR when it fills:

1. for a half that starts a page, the core takes the file's next data page from its pool's batch (7);
2. it sets the RW unit's job: a write of 2 KiB, the LPDDR address, where in the RW unit's write buffer it lands (17.7's
   `RW_CONFIG`, `RW_LPDDR_ADDR`, `RW_BUFFER_ADDR`);
3. **the DMA sends the half from the SRAM over +8 into the RW unit's write buffer;** the RW unit writes it, and its
   done event comes back to the core;
4. **only then**, for a page's first half, the entry naming the page goes into the cached half of the writer's index
   page (6): the data is in LPDDR before an entry names it (23.6.8). The page's second half, written later, is past
   the length until then;
5. the buffer goes back to the pool.

**Meanwhile the writer's grants land in another buffer:** data flows into the SRAM through the director while the DMA
drains the full half over +8 (owner, 2026-11-20). The RW unit's write buffer should hold two halves, so that the DMA
never waits on LPDDR †.

**`fs_flush` and `fs_close`** write the writer's part-filled half (the bytes past the length do not matter), the half
of its index page that changed, and the half holding its directory entry (FS-API 4.6). Each is a 2 KiB job.

## 5. Which calls go through the memory tile (owner, 2026-11-20)

**Only a whole page read skips the memory tile's SRAM.**

| Call | Its data's path |
| --- | --- |
| `fs_read_page` | **straight:** the core finds the page in the index and sets the RW unit's 4 KiB read job; the RW unit's output goes to the caller's buffer, counted on the caller's counter |
| `fs_write_page` | through the SRAM: two grants of a half each, then out over +8 |
| `fs_write`, `fs_fprintf` | through the SRAM: sections 3 and 4 |
| `fs_read` | through the cache (6): the halves read into it, then the 128-bit words that cover the bytes asked for sent to the caller. The caller's library takes its bytes out of them: the one copy is the caller's |

- **The tiles cannot write the RW unit directly:** only the memory tile's DMA reaches its data input, by +8 (owner,
  2026-11-20).
- **The walk** (FS-API 4.4) reads index pages whole, 4 KiB jobs, outside the cache.

## 6. The cache: a pool of 2 KiB buffers, by CLOCK (owner, 2026-11-20)

The memory tile keeps **one pool of 2 KiB buffers** in its SRAM. **The cache and the writers share it** †: a writer's
current half is a buffer of the pool, pinned while a grant lands in it.

**What it holds:** halves of directories (path look-ups, `fs_list`, entry updates), halves of index pages (finding a
page behind an offset; a writer's index entries), halves of data for `fs_read`, the superblock's half. **What it
never holds:** a COW file that is not frozen (the Updater changes its index lines through the shared cache, behind the
memory tile's back); `fs_read_page`'s data; the walk's pages.

**It stays right by construction:** the memory tile is the only reader and writer of directories and streamed files;
a frozen COW file never changes; a buffer of a page the file system frees is dropped at once.

**CLOCK** (owner, 2026-11-20):

- **A hash table** from a half's LPDDR address to its buffer: 512 buckets of 16 bits, chained through a 16-bit "next"
  in each buffer's record. In the core's data memory (8).
- **A record for each buffer:** the half's LPDDR address in halves (3 bytes), the chain's next (2 bytes), and a byte of
  a "used" bit, a pin count and a "dirty" bit: 6 bytes, in the core's data memory.
- **384 buffers** (owner, 2026-11-20): 768 KiB, the most beside the rest of the SRAM's use. About 80 are writers' at the
  worst moment (about 67 part-filled halves, a few more draining to the RW unit), which leaves about 300 as cache.
- **An fd names its buffer** (FS-API 4.6), a writer's own or a reader's last, so a call on it finds its buffer without
  the hash table; a reader's is a hint, checked against the buffer's tag.
- **A hit** sets the used bit: a hash, a probe or two, a store. **A miss** sweeps the hand round the buffers, clearing
  used bits, to the first not used, not pinned; a dirty one (a writer's half) is written out first, a clean one simply
  taken.
- **Writes go through** †, but for a writer's half: an entry changed in a cached directory or index half is written
  out as that half at once, so nothing waits for a flush but the writers' halves, which `fs_flush`, `fs_close` and
  `fs_shutdown` write.

## 7. The page service, on the tracer tile (owner, 2026-11-20)

**The tracer tile keeps the page maps and hands out every page.** Its promise: **every page it hands out is new**
(23.6.8): taken in its map, and discarded from the cache first if it was last in a COW file.

- **Two pools** (owner, 2026-11-20): **the Updater's**, which the master tile reserves for each request and draws on for
  every run and every COW file's root; and **the file system's**, from which the memory tile takes the pages of
  directories and streamed files.
- **A prepared batch of 256 pages for each** (owner, 2026-11-20): taken in the map and discarded if they needed it,
  before anyone asks. An acquire of 256 or fewer is a copy of addresses out of the batch and an answer; the tracer tile
  then refills the batch in the background.
- **The Updater's pool prefers pages that need a discard,** the file system's prefers those that do not †: the
  Updater's refills come mostly before tracing, the file system's during it (the run logs), so the discards fall where
  the Tracer is idle (README finding 1).
- **A reservation is a count** (owner, 2026-11-20): `fs_reserve_pages(n)` checks that n pages are free, counting the
  Updater's batch, and holds them; the file system's pool may not take what is held. No list of pages, no extents.
- **The discards are the tracer tile's,** on a look-up port of the cache (FOR-THE-HARDWARE.md 3), in the background
  (owner, 2026-11-19).
- **Freeing:** the memory tile's walk (FS-API 4.4) sends the pages it frees to the tracer tile in batches, each with
  whether it was in a COW file.
- **A mount:** the memory tile walks every file and sends the pages it finds to the tracer tile, which rebuilds its
  maps; the batches start empty.

## 8. Where it all lives, and that it fits (owner, 2026-11-20)

**The rule:** what the core touches on every call lives in its data memory; the SRAM holds what the DMA, the director
and the RW unit move, since the core reaches the SRAM slowly (owner, 2026-11-20).

**The memory tile's 32 KB of data memory:**

| What | Size |
| --- | --- |
| the fd table (FS-API 4.6): 512 × 16 B (owner, 2026-11-20) | 8 KiB |
| CLOCK: the hash table (512 × 2 B) and a record of 6 B for each of 384 buffers (owner) | 3.3 KiB |
| the working directories (FS-API 4.7): 66 × 4 B (owner) | 0.3 KiB |
| a name cache †: (directory, name) to an entry's address, 256 × 8 B, so that most path look-ups read no SRAM | 2 KiB |
| stack | 4 KiB |
| state: the grants, the requests in hand, the RW unit's jobs, the file system's batch of pages from the tracer tile | 2 KiB |
| **total** | **about 20 KiB**, 12 KiB spare |

**Its 32 KB of program memory:** the file system's code (owner, 2026-11-20).

**Its 1 MiB of SRAM:**

| What | Size |
| --- | --- |
| **the pool: 384 buffers of 2 KiB** (owner, 2026-11-20) | **768 KiB** |
| the landing slots for paths: 1 KiB for each of 66 tiles (owner, 2026-11-20) | 66 KiB |
| **total** | **834 KiB**, 190 KiB spare |

**The tracer tile's share** (its other uses are the Tracer's and are not counted here):

| What | Size |
| --- | --- |
| the "taken" map: 2²⁰ pages | 128 KiB |
| the "may be cached" map | 128 KiB |
| the two prepared batches: 2 × 256 × 4 B | 2 KiB |
| the pools' counts, the reservation | a few bytes |
| **total** | **about 258 KiB** of its SRAM |

**What the numbers rest on:**

- **Paths:** a request has 12 bytes of its own (words 1–3, 11.5): a name of 12 bytes or fewer, which the working
  directories make the common case (`tile3.log`), travels in the request. A longer one, up to 1,023 bytes, lands in
  its tile's 1 KiB slot by `WRITE` packets before the request (owner, 2026-11-20). **A tile has one call that carries
  a path in flight at a time** (owner, 2026-11-20): its slot is then never overwritten. A path goes with `fs_open`,
  `fs_mkdir`, `fs_remove`, `fs_stat`, `fs_list` and `fs_chdir` only, so a proof uses the slots about a hundred times.
- **The writers in the pool:** about 67 writers at once (the 64 compute tiles' logs, the tracer tile's three) hold a
  part-filled half each, and a few more halves drain to the RW unit: about 80 buffers, which leaves about 300 for the
  cache. An idle writer's half may be evicted, written out first, and read back at its next write.
- **The maps** are for 4 GiB of LPDDR; less LPDDR, smaller maps.

## 9. Open

1. How the caller learns the length at `fs_open` (in the answer, as a field †).
2. The order in which waiting grants are served (first come, first served †).
3. The size of the RW unit's write buffer (two halves †).
4. How a `fs_read_page` request names the caller's buffer and counter, and how the RW unit's output is addressed to
   it (17.7's output queue 0 and its DMA).
5. Whether writers share the cache's pool, or keep buffers apart (6).
6. Whether the name cache earns its 2 KiB (8).
7. The tracer tile's SRAM: whether its 258 KiB fits beside the Tracer's own use.
