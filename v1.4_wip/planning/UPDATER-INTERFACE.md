# The Updater's peripheral interface

Draft for review, 2026-11-16, in v1.36's register layout and names (`v1.36/spec/machine-spec.md` 5.2, 5.3, 17.1). It
replaces the draft of 2026-11-15, which used v1.35's. A dagger (†) marks a choice that is proposed and not yet ruled
on.

The Updater is **the memory tile's peripheral 2** (the RW engine Tony built stays peripheral 1). Its unit of work is a
**run**: `START` starts it, `STATUS.busy` is 1 while it lasts, and `EVENTS.done` is posted at its end. A run applies
one frame of update words (`HOST-FRAMES.md`) to a file.

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

**For the Updater:** peripheral 2, so its window is at **0xFFFF_FC00** and its region is **0x05**.

## Block 0: the control block

The same words as on every peripheral. The Updater adds bits [2] and up.

| word | offset | register | access | bits |
|---|---|---|---|---|
| 0 | 0x000 | `PARAMS` | R | [7:0] `type` = 5 †; [15:8] `version` = 0; [31:16] 0 |
| 1 | 0x004 | `STATUS` | R | [0] `busy`; [1] `dma_busy`, always 0; [3:2] `stretch`, in a memory-segments run: 0 before *s*, 1 *s*, 2 after *s* |
| 2 | 0x008 | `ERRORS` | R | sticky until the reset; below |
| 3 | 0x00C | `EVENTS` | R | below; a load consumes |
| 4 | 0x010 | `EVENTS_PEEK` | R | the same, consuming nothing |
| 5 | 0x014 | `EVENTS_MASK` | R/W | the events that reach the tile's bit and that a load consumes; 0 at the reset |
| 6 | 0x018 | `START` | W | starts the run that block 2 describes |

| `EVENTS` bit | name | kind |
|---|---|---|
| [0] | `done` | the run has ended; counted, depth 1 |
| [1] | `dma_done` | never set: no DMA |
| [2] | `free_page_credit` | half the free page queue is free; counted, depth 2 |

| `ERRORS` bit | name | meaning |
|---|---|---|
| [0] | `queue_overrun` | a packet into a full segment queue or free page queue |
| [1] | `input_left_over` | the data did not end where `WORDS128_TOTAL` says |
| [2] | `beyond_file` | an address past the file's length |
| [3] | `not_whole_pages` | a jump table run with a run of words that is not whole pages |
| [4] | `lpddr_error` | a read beat or a write response from LPDDR that is not OKAY (the RW engine's name for it) |

- The Updater's bits are packed upward from the first free one, as the RTL packs the Tracer's (item 161, which the
  owner has still to rule on).

## Block 1: no DMA

The Updater sends no packets. Words 16 to 31 read 0, and a store there is undefined, as on every peripheral without
a DMA.

## Block 2: the run

Written by the memory tile's core while the Updater is idle, and taken at `START`. A root or a page is an address in
128-byte units, the form an index entry stores.

| word | offset | register | access | meaning |
|---|---|---|---|---|
| 32 | 0x080 | `RUN_CONFIG` | R/W | [1:0] `type`: 0 jump table, 1 data image, 2 memory segments. [2] `lru`: 0 the data is raw, 1 LRU-coded. [3] `empty`: 0 the target starts as a copy of `SRC_ROOT`, 1 it starts empty † |
| 33 | 0x084 | `WORDS_RUN` | R | the 64-bit words of this run consumed so far |
| 34 | 0x088 | `SRC_ROOT` | R/W | the root to copy from |
| 35 | 0x08C | `ROOT` | R/W | the target's root page |
| 36 | 0x090 | `ROOT_FINAL` | R/W | memory segments: the final image's root page |
| 37 | 0x094 | `ROOT_TRACER` | R/W | memory segments: the Tracer's copy's root page |
| 38 | 0x098 | `FILE_PAGES` | R/W | the target file's length in 4 KB pages: the bounds check, and the tree's depth † |
| 39 | 0x09C | `WORDS_BEFORE` | R/W | memory segments: the 64-bit words before segment *s* |
| 40 | 0x0A0 | `WORDS_IN` | R/W | memory segments: the 64-bit words in segment *s* |
| 41 | 0x0A4 | `WORDS_TOTAL` | R/W | the 64-bit words in the whole run; it marks the data's end |
| 42 | 0x0A8 | `WORDS128_TOTAL` | R/W | the 128-bit words of the run's data; a check only |
| 43 | 0x0AC | `PAGES_USED` | R | the free pages this run has taken |

- `WORDS_RUN` and `PAGES_USED` clear at `START`.
- † One bit says how the target starts: every run starts from a copy or from empty. The depth has no field of its
  own: it follows from `FILE_PAGES`.

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

## What each type of run uses

| | jump table | data image | memory segments |
|---|---|---|---|
| `ROOT` | the run's table | the cycle-0 image | the start image |
| `SRC_ROOT` | the base table, or unused when the target starts empty | the program image | the cycle-0 image |
| `ROOT_FINAL`, `ROOT_TRACER` | unused | unused | the final image, the Tracer's copy |
| `WORDS_BEFORE`, `WORDS_IN` | unused | unused | from the proof header's counts and *s* |
| runs of words | whole pages only | any | any |
| old pages saved to a second file | no | no | in stretch *s*, to the Tracer's copy |

## The region: queue ports

Region 0x05 on the memory tile, `WRITE` packets of 128 bits only, the part picked by offset bits [22:20] as in the
Tracer's region. A packet is appended at the queue's tail and the rest of its address is ignored.

| part | port | queue | written by | an entry |
|---|---|---|---|---|
| 1 | segment queue | 4 KB, 256 entries | Ingress/Egress | 128 bits of the frame's data |
| 2 | free page queue | 256 entries † | the memory tile's core | one free page, in [31:0] † |
| 0, 3-7 | unmapped | | | |

## Its other connections

| to | what |
|---|---|
| the AXI arbiter | an AXI port of its own: pages and half pages, read and written |
| the cache | a look-up port: look up, update, flush; replies on the shared reply bus |
| Ingress/Egress | the segment queue's credits, a private signal (Ingress is set aside for now) |

## Open

- Whether the roots and the word counts are window words, as here, or a small state record in the region, as on the
  Tracer.
- The free page queue's size, and whether an entry carries one page or four.
- What a `START` during a run does: an error bit, or undefined as elsewhere.
- The type number, 5: v1.36 assigns 0 to 4.
