# The data path: how a tile's bytes reach a file, and come back

*The FS-API session, 2026-11-20. (owner) marks a ruling, with its date; a dagger (†) a proposal not ruled on. For
step 4 (the RW unit and the mesh) and step 5 (the device's file system). The API (`FS-API.md`) is unchanged by it:
this is how the memory tile and a caller's library carry out `fs_write`, `fs_fprintf`, `fs_write_page`, `fs_read` and
`fs_read_page`.*

## 1. Why

The memory tile serves every tile's file work: 64 compute tiles writing their `printf` logs, and the tracer tile
writing 24 to 48 MB of run logs in about 10 ms (FS-API 3.4). It must not copy bytes, and it cannot shift them: the
scratchpad has no byte addressing and no byte moves, only 128-bit words. A `printf` of 37 bytes appended at byte 21 of
a word would otherwise cost the memory tile a read of that word, a merge and a shift of everything after it.

**The rule behind it** (owner, 2026-11-20): work moves from the memory tile to the callers wherever it can. A little
more work on a compute tile costs little, since it is spread over 64 of them; the memory tile serves them all.

## 2. The pieces

| Piece | Its part |
| --- | --- |
| **the caller's library** (a compute tile's, the tracer tile's, the shell's) | formats and sends the bytes; keeps the file's last partial word (3) |
| **the memory tile's core** | the file system: grants writes, counts them done, sends full pages out, keeps the index |
| **its scratchpad** | the **page buffers**: two for each file open for writing † |
| **its DMA** | sends a full page from the scratchpad to the RW unit |
| **the RW unit** | the memory tile's peripheral (owner, 2026-11-20): its registers and events in its window, as in v1.37's 17.7; **its data input on the router's +8 link** (owner, 2026-11-20), so that a page sent to it does not come back in through the memory tile's own input |

## 3. A write, step by step (owner, 2026-11-20)

The caller keeps **its tail**: the last *length* mod 16 bytes of the file, the part of its last 128-bit word that is
filled. It can, since a file open for writing has one writer (FS-API 5.2). It learns the length when it opens the
file and keeps it from then on †.

1. **The request:** "write *n* bytes to fd".
2. **The answer:** where to write them, how many it may write, and on which counter:
   - the address: the page buffer's base + (*length* mod 4,096, rounded down to 16);
   - *g*, the bytes granted: at most *n*, and no more than the page has left (4,096 − *length* mod 4,096);
   - a counter of the memory tile's, armed for ⌈((*length* mod 16) + *g*) / 16⌉ packets.
3. **The data:** the caller sends its tail followed by the *g* bytes, as whole 128-bit words, `WRITE` packets from the
   address up, each counted on that counter. The first word sends the tail's bytes again over themselves; the last
   word's bytes past the new length are whatever the caller had, and the next write covers them.
4. **Done:** the counter reaches 0 (its `counter_done` event); the core adds *g* to the length. The caller's tail is
   now the last (*length* mod 16) bytes it sent.
5. **A full page** goes out (section 4), and the next grant lands in the other buffer.
6. **What is left,** *n* − *g*, the caller asks for again. **One grant at a time for an fd,** so a file's bytes arrive
   in order.

So no byte is copied, merged or shifted on the memory tile: every word arrives whole, at its place in the page.

**Grants in flight: four,** on four of the memory tile's seven counters (12.2), the other three left for its own work
(owner, 2026-11-20: "if we can get four in flight, I'd be happy"). A request that finds all four in use waits, first
come first served †. A grant is at most a page, about 850 cycles at the tracer tile's rate and far less for a
`printf`, so the wait is short. The requests themselves cannot overflow anything: a tile has at most three
outstanding to the memory tile (11.3).

## 4. A full page out, while the next fills (owner, 2026-11-20)

**The RW unit moves whole pages** of 2 or 4 KiB (owner, 2026-11-20: "we want the RW unit to be processing 2KB or 4KB
pages"). When a page buffer fills:

1. the core takes the file's next data page in LPDDR (FS-API 4.2) and sets the RW unit's job: a write of the page's
   beats, the LPDDR address, where in the RW unit's write buffer it lands (17.7's `RW_CONFIG`, `RW_LPDDR_ADDR`,
   `RW_BUFFER_ADDR`);
2. **the DMA sends the page from the scratchpad over +8 into the RW unit's write buffer;**
3. the RW unit writes it to LPDDR, and its done event comes back to the core;
4. **only then** the entry naming the page goes into the writer's index line (FS-API 4.6): the data is in LPDDR
   before an entry names it (23.6.8);
5. the buffer is free again.

**Meanwhile the writer's grants land in its other buffer:** data flows into the scratchpad through the director while
the DMA drains the full page out over +8 (owner, 2026-11-20). That is why each writer has two buffers †, and why the
RW unit's write buffer should hold two pages, so that the DMA never waits on LPDDR †.

**`fs_flush` and `fs_close`** send the part-filled buffer out the same way, a whole page (the bytes past the length do
not matter), then the writer's index lines and its directory entry (FS-API 4.6).

## 5. Which calls go through the memory tile (owner, 2026-11-20)

**Only a whole page read skips the memory tile's scratchpad.**

| Call | Its data's path |
| --- | --- |
| `fs_read_page` | **straight:** the core finds the page in the index and sets the RW unit's read job; the RW unit's output goes to the caller's buffer, counted on the caller's counter. The memory tile's scratchpad holds none of it |
| `fs_write_page` | through the scratchpad: the page into a buffer, by a grant of a whole page (3), then out over +8 (4) |
| `fs_write`, `fs_fprintf` | through the scratchpad: sections 3 and 4 |
| `fs_read` | through the scratchpad: the page read into it, then the 128-bit words that cover the bytes asked for sent to the caller. The caller's library takes its bytes out of them: the one copy is the caller's, not the memory tile's |

- **The tiles cannot write the RW unit directly:** only the memory tile's DMA reaches its data input, by +8 (owner,
  2026-11-20: "I don't think it could go straight").
- **The index reads** a walk or a look-up makes (FS-API 4.4) are the memory tile's own: lines or pages read into its
  scratchpad †.

## 6. What it costs

- **Scratchpad:** two buffers of 4 KiB a writer. About 67 writers at once (the 64 compute tiles' logs and the tracer
  tile's three run logs) take about 540 KiB of the memory tile's 4 MiB; 256 (four a tile) would take 2 MiB.
- **The fd table** names a writer's buffer in its position word, which a writer does not use: a pool of 128 writers
  (owner, 2026-11-20; FS-API.md 4.6).
- **The mesh:** each grant is a request, an answer and the data; the run logs are FOR-THE-HARDWARE.md's 2.4 to 4.8
  bytes a cycle into the memory tile, and the same out over +8 to the RW unit.

## 7. Open

1. How the caller learns the length at `fs_open` (in the answer, as a field †).
2. Two buffers a writer, or fewer shared among them. (The pool: 128 writers, named by the fd entry's position word, ruled
   2026-11-20.)
3. The order in which waiting grants are served (first come, first served †).
4. A page of 2 KiB or 4 KiB for the RW unit's jobs, and the size of its write buffer.
5. How a `fs_read_page` request names the caller's buffer and counter, and how the RW unit's output is addressed to
   it (17.7's output queue 0 and its DMA).
