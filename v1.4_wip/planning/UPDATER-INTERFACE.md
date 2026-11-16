# The Updater's peripheral interface

Draft for review, 2026-11-15. The Updater as C&C 2 sees it, for v1.4, in the conventions of the replayer (spec 4.19) and
the Tracer (4.21, 8.16). A dagger (†) marks a choice that is proposed and not yet ruled on.

**Written in v1.35's terms, before v1.36 was read.** v1.36 renames the registers and has built the RW engine as
the memory tile's peripheral 1: `V136-SCOPE.md`, section 3.

The unit of work is a **run**, as on those two: `PULSE_RUN` starts it, `STATUS` [0] is "run in progress", event [0] is
"run done". A run applies one frame of update words (`HOST-FRAMES.md`) to a file.

## The window: standard words (spec 4.17.4)

| word | register | access | here |
|---|---|---|---|
| 0 | `PARAMS` | R | [7:0] = 2, the type; [15:8] the version |
| 1 | `STATUS` | R | [0] run in progress; [1] 0 (no DMA); [3:2] the stretch of a memory-segments run: 0 before *s*, 1 *s*, 2 after *s* |
| 2 | `ERRORS` | R | sticky until the reset; bits below |
| 3 | `PULSE_RUN` | W | a store of 1 starts the run set up in words 32 and 34-42 |
| 4 | `PULSE_DMA` | | unused: the Updater sends no packets |
| 5-7 | `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK` | R, R, R/W | bits below |
| 8, 9 | `DMA_HDR`, `DMA_ADDR` | | unused; read 0 |

## The window: the Updater's words

Every word is 32 bits. The R/W words are written while stopped and used by the next `PULSE_RUN`. A root or a page is an
address in 128-byte units, the form an index entry stores.

| word | register | access | meaning |
|---|---|---|---|
| 32 | `RUN_CONFIG` | R/W | [1:0] the run's type: 0 jump table, 1 data image, 2 memory segments. [2] the data's coding: 0 raw, 1 LRU. [3] how the target starts: 0 a copy of `SRC_ROOT`, 1 empty † |
| 33 | `WORDS_RUN` | R | the 64-bit words of this run consumed so far |
| 34 | `SRC_ROOT` | R/W | the root to copy from |
| 35 | `ROOT` | R/W | the target's root page |
| 36 | `ROOT_FINAL` | R/W | memory segments: the final image's root page |
| 37 | `ROOT_TRACER` | R/W | memory segments: the Tracer's copy's root page |
| 38 | `FILE_PAGES` | R/W | the target file's length in 4 KB pages: the bounds check, and the tree's depth † |
| 39 | `WORDS_BEFORE` | R/W | memory segments: the 64-bit words before segment *s* |
| 40 | `WORDS_IN` | R/W | memory segments: the 64-bit words in segment *s* |
| 41 | `WORDS_TOTAL` | R/W | the 64-bit words in the whole run; it marks the data's end |
| 42 | `WORDS128_TOTAL` | R/W | the 128-bit words of the run's data; a check only |
| 43 | `PAGES_USED` | R | the free pages this run has taken |
| 44 | `STAT_CYCLES` | R | cycles with `STATUS` [0] at 1 |
| 45 | `STAT_STALL_DATA` | R | cycles waiting on an empty segment queue |
| 46 | `STAT_STALL_PAGES` | R | cycles waiting on an empty free page queue |
| 47 | `STAT_STALL_MEMORY` | R | cycles waiting on the cache or the bus driver |
| 48 | `STAT_PAGES_READ` | R | pages and half pages read |
| 49 | `STAT_PAGES_WRITTEN` | R | pages and half pages written |

- † `RUN_CONFIG` [3] is one bit: every run starts from a copy or from empty. The depth is not a field: it follows from
  `FILE_PAGES`.
- `WORDS_RUN` and `PAGES_USED` clear at `PULSE_RUN`. The `STAT_` words are 32 bits, wrapping, cleared at the reset. †

## What each type of run uses

| | jump table | data image | memory segments |
|---|---|---|---|
| `ROOT` | the run's table | the cycle-0 image | the start image |
| `SRC_ROOT` | the base table, or unused when the target starts empty | the program image | the cycle-0 image |
| `ROOT_FINAL`, `ROOT_TRACER` | unused | unused | the final image, the Tracer's copy |
| `WORDS_BEFORE`, `WORDS_IN` | unused | unused | from the proof header's counts and *s* |
| runs of words | whole pages only | any | any |
| old pages saved to a second file | no | no | in stretch *s*, to the Tracer's copy |

## `ERRORS`

| bit | error |
|---|---|
| [1] | queue overrun: a packet into a full segment queue or free page queue (the replayer's and the Tracer's position) |
| [6] | done with input left over: the data did not end where `WORDS128_TOTAL` says (the replayer's position) |
| [7] | an address beyond the file's length |
| [8] | a jump table run with a run of words that is not whole pages |
| [9] | a read or a write the bus driver reported as failed |
| the rest | reserved |

## `EVENTS`

| bit | event | kind and depth |
|---|---|---|
| [0] | run done | counted, depth 1 |
| [2] | free-page credit: half the free page queue is free | counted, depth 2 |
| the rest | reserved | |

- A credit is permission to send up to half a queue, as on the replayer and the Tracer.
- The segment queue has no event: its credits go to Ingress on a private signal.

## The region: queue ports

Region 0x04 on C&C 2, mode-0 packets of 128 bits only, the part picked by offset bits [22:20] as on the replayer and
the Tracer. A packet is appended at the queue's tail and the rest of its address is ignored.

| part | port | queue | written by | an entry |
|---|---|---|---|---|
| 1 | segment queue | 4 KB, 256 entries | Ingress | 128 bits of the frame's data |
| 2 | free page queue | 256 entries † | C&C 2 | one free page, in [31:0] † |
| 0, 3-7 | unmapped | | | |

## Its other connections

| to | what |
|---|---|
| Ingress | the segment queue's credits, a private signal |
| the cache | a look-up port: look up, update, flush; replies on the shared reply bus |
| the bus driver | page and half-page reads and writes, with a confirmation for each write |

## Open

- Whether the roots and the word counts are window words, as here, or a small state record in the region, as on the
  Tracer.
- The free page queue's size, and whether an entry carries one page or four.
- What a `PULSE_RUN` during a run does: an error bit, or undefined as elsewhere.
