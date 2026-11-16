# The Updater's peripheral interface

Draft for review, in v1.36's register layout and names (`v1.36/spec/machine-spec.md` 5.2, 5.3, 17.1). Revised on
2026-11-16 after the walk through the registers with the owner; it replaces that morning's draft. A dagger (†) marks
a choice that is proposed and not yet ruled on. Where a line says (owner), the owner ruled it.

**The Updater is the master tile's peripheral** (owner, 2026-11-16), beside Ingress/Egress. It was the memory tile's.
Why, and what follows, is `DESIGN-NOTES-2026-11-16.md` section 10.

Its unit of work is a **run**: `START` starts it, `STATUS.busy` is 1 while it lasts, and `EVENTS.done` is posted at
its end. **Every run is the same operation:** it applies a number of update words (`HOST-FRAMES.md`) to a file that
already exists.

## How a peripheral's registers are laid out (v1.36)

| level | size | how it is found |
|---|---|---|
| a **window** | 512 bytes, 128 words of 32 bits | peripheral *n* (1 to 3) has its window at 0xFFFF_F800 + 0x200·*n* |
| a **block** | 16 words | a window is 8 blocks; block *b* is words 16·*b* to 16·*b* + 15 |
| a **word** | 32 bits | word *w* is at the window's base + 4·*w* |

| block | words | in every peripheral's window |
|---|---|---|
| 0 | 0-15 | **the control block,** the same seven words on every peripheral |
| 1 | 16-31 | **its DMA,** if it sends packets; if not, every word reads 0 |
| 2-7 | 32-127 | **its own** |

- A word that a block does not define is unmapped: an access fault.
- A peripheral also has a **region** for packets written into it: peripheral *n*'s is region 0x03 + *n*.

**For the Updater:** the master tile's peripheral 2 †, Ingress/Egress being its peripheral 1. Its window is at
**0xFFFF_FC00** and its region is **0x05**.

## Block 0: the control block

The same words as on every peripheral. The Updater adds bits [2] and up.

| word | offset | register | access | bits |
|---|---|---|---|---|
| 0 | 0x000 | `PARAMS` | R | [7:0] `type` = 5 †; [15:8] `version` = 0; [31:16] 0 |
| 1 | 0x004 | `STATUS` | R | [0] `busy`; [1] `dma_busy`, always 0 |
| 2 | 0x008 | `ERRORS` | R | sticky until the reset; below |
| 3 | 0x00C | `EVENTS` | R | below; a load consumes |
| 4 | 0x010 | `EVENTS_PEEK` | R | the same, consuming nothing |
| 5 | 0x014 | `EVENTS_MASK` | R/W | the events that reach the tile's bit and that a load consumes; 0 at the reset |
| 6 | 0x018 | `START` | W | starts the run that block 2 describes |

| `EVENTS` bit | name | kind |
|---|---|---|
| [0] | `done` | the run has ended; counted, depth 1 |
| [1] | `dma_done` | never set: no DMA |
| [2] | `segment_credit` | the segment queue's credit; counted, depth 2 |
| [3] | `free_page_credit` | the free page queue's credit; counted, depth 2 |

| `ERRORS` bit | name | meaning |
|---|---|---|
| [0] | `queue_overrun` | a push into a full segment queue or free page queue; the push is dropped |
| [1] | `beyond` | an address past what the file's depth can reach |
| [2] | `lpddr_error` | a read beat or a write response from LPDDR that is not OKAY (the RW engine's name for it) |
| [3] | `out_of_pages` | a page was needed with `PAGE_COUNT` at 0 (owner) |
| [4] | `not_owned` † | the cache refused an update: an index page on the path is not the file's own |

- **There is no "left over" error** (owner). The replayer sets `ERRORS.leftover` when a pass ends with input still in
  a stream; the Updater must not, because its stream carries on from one run to the next.
- The Updater's bits are packed upward from the first free one, as the RTL packs the Tracer's (item 161, which the
  owner has still to rule on).

## Block 1: no DMA

The Updater sends no packets. Words 16 to 31 read 0, and a store there is undefined, as on every peripheral without
a DMA.

## Block 2: the run

Written by the master tile's core while the Updater is idle, and taken at `START`. A root is an address in 128-byte
units, the form an index entry stores, and the form the Tracer's state record holds its roots in.

| word | offset | register | access | meaning |
|---|---|---|---|---|
| 32 | 0x080 | `RUN_CONFIG` | R/W | below (owner: "fine") |
| 33 | 0x084 | `WORD_COUNT` | R/W | the 64-bit update words this run applies, run header words and values alike. The Updater counts it down, and the run ends at 0 † |
| 34 | 0x088 | `PAGE_COUNT` | R/W | the pages that will be queued for this run. The Updater counts it down by one for each page it takes from the free page queue (owner) |
| 35 | 0x08C | `IMAGE_ROOT` | R/W | the root of the file the run updates (owner) |
| 36 | 0x090 | `TRACER_ROOT` | R/W | with `tracer_copy`: the root of the Tracer's copy (owner) |

| `RUN_CONFIG` bits | name | meaning |
|---|---|---|
| [0] | `lru` | the data is LRU-coded; from the frame header's compression type |
| [1] | `continue` | this run carries on in the same frame as the last one: the decoder keeps its LRU table and the bits it has left over. 0: a new frame; both are cleared |
| [2] | `tracer_copy` | before a page is changed, a copy of it as it stands goes into the file in `TRACER_ROOT` |
| [4:3] | `depth` | the file's depth, 0 to 3, as the Tracer's `memory_depth` and `map_depth` (owner) |

- **`WORD_COUNT` and `PAGE_COUNT` read during a run may be stale** (owner). They are written only while the Updater is
  idle.
- **After `done`, `PAGE_COUNT` is the pages not used.** With the host's count written in it should be 0.
- **A run's end touches nothing** (owner): what is left in the segment queue, in the decoder and in the free page
  queue stays, and is no error. The next run picks it up.
- **There is no file length** (owner). The depth is all the Updater needs, to ask the cache for a page and to build
  index pages on a fault. An address past what the depth reaches is `ERRORS.beyond`, as on the Tracer.
- **The file system makes every file and its root** before the run (owner): there is no source root and no "empty"
  setting. `IMAGE_ROOT` and `TRACER_ROOT` are never changed by the Updater.
- **These are window words, not a state record** †. The Tracer keeps its roots in a record because the record is its
  working state; the Updater has five words.

## Block 3: statistics †

32 bits each, wrapping, counting while `STATUS.busy` is 1, cleared at the reset.

| word | offset | register | access | meaning |
|---|---|---|---|---|
| 48 | 0x0C0 | `STAT_CYCLES` | R | cycles with `STATUS.busy` at 1 |
| 49 | 0x0C4 | `STAT_STALL_DATA` | R | cycles waiting on an empty segment queue |
| 50 | 0x0C8 | `STAT_STALL_PAGES` | R | cycles waiting on an empty free page queue |
| 51 | 0x0CC | `STAT_STALL_MEMORY` | R | cycles waiting on the cache or on LPDDR |
| 52 | 0x0D0 | `STAT_PAGES_READ` | R | pages and half pages read |
| 53 | 0x0D4 | `STAT_PAGES_WRITTEN` | R | pages and half pages written |

Blocks 4 to 7 are unused.

## The runs

| run | the file in `IMAGE_ROOT` | `continue` | `tracer_copy` | `WORD_COUNT` |
|---|---|---|---|---|
| program image | the new image, empty | 0 | 0 | frame 1's count |
| jump table | the new table: empty, or a copy of the base table | 0 | 0 | frame 4's count |
| data image | the cycle-0 image, a copy of the program image | 0 | 0 | frame 3's count |
| memory segments, before *s* | the start image, a copy of the cycle-0 image | 0 | 0 | the sum of the first *s* counts |
| memory segments, segment *s* | the final image, a copy of the start image; the Tracer's copy in `TRACER_ROOT` | 1 | 1 | segment *s*'s count |
| memory segments, after *s* | the final image | 1 | 0 | the rest |

- **The owner's sequence** (2026-11-16): build the cycle-0 image; make a copy of it; run the segments up to *s* into
  the copy; freeze it and make two copies of it; run segment *s*, each page written going updated into one file and
  as a copy with its original data into the other; run the rest.
- **Why the Tracer's copy gets the original page:** the Tracer writes its copy in place, with no copy-on-write on its
  side, so every page it will write must be the copy's own before tracing starts. Those are the pages segment *s*
  writes.
- **† Memory segments is three runs.** The owner ruled two, with the firmware making the two copies at the cut. The
  third `START` costs the firmware nothing, and at the second run's `done` the Tracer's copy is complete, so tracing
  can begin without waiting for the final image.
- **Whole pages are not a mode.** A program image or a jump table arrives as whole pages and so is never read first;
  the Updater is not told.

## The region: the two queues

Region 0x05 on the master tile, `WRITE` packets of 128 bits only, the part picked by offset bits [22:20] as in the
Tracer's region. A packet is appended at the queue's tail and the rest of its address is ignored.

| part | queue | size | an entry | filled by |
|---|---|---|---|---|
| 1 | segment queue | 256 entries of 128 bits, 4 KB | 128 bits of a frame's data | Ingress, on the same tile |
| 2 | free page queue | 256 entries | one free page, in [31:0] † | the master tile's core, from the run's page list |
| 0, 3-7 | unmapped | | | |

**The credits are the replayer's and the Tracer's, unchanged** (owner; replayer chapter 17.8.8):

- A queue's credit is a counted event of depth 2. One credit is leave to push up to half a queue, 128 entries.
- A credit is returned each time 128 entries have left the queue since the last return.
- A credit is taken by a consuming load of `EVENTS`, one from each credit that `EVENTS_MASK` selects.
- A push into a full queue is dropped and sets `ERRORS.queue_overrun`.

Two differences, both because the Updater has no state write and its stream runs across runs:

- **Only the reset empties the queues and fills the credits to 2.** A `START` does neither.
- **† The segment queue is filled as the replayer's queues are:** the core takes a credit and starts a transfer of
  up to half a queue from Ingress. Whether Ingress should feed the queue directly instead is open; Ingress is still
  to be designed (`HOST-LINK.md`).

## Its other connections

| to | what |
|---|---|
| the AXI arbiter | an AXI port of its own: pages and half pages, read and written |
| the cache | a look-up port: look up and update; replies on the shared reply bus. An update's reply comes once the changed line has landed in LPDDR (`DESIGN-NOTES-2026-11-16.md`, section 4) |

Both reach across to the memory side. Latency on them does not matter (owner): the Updater works in big bursts.

## Open

- The page list: how the master tile gets a run's free pages. Proposed: it asks for all of them before the run, by a
  request, and keeps the list in its own memory.
- Whether an entry of the free page queue carries one page or four.
- What a `START` during a run does: an error bit, or undefined as elsewhere.
- The type number, 5: v1.36 assigns 0 to 4.
- The peripheral number on the master tile.
