# WORKER1 report: gold writes the host's frames

2026-11-16. Branch `updater-w1`. The specification is `v1.4_wip/planning/HOST-FRAMES.md` (HF) as on main after the
owner's ruling on Q1 (the 128-bit header). The plan and the choices are `NOTES.md`; the one question, answered, is
`QUESTIONS.md`.

## What was built

In `updater_wip/pipeline/gold/`, a copy of `v1.36/pipeline/gold/` (its first commit, unchanged), with:

| command | what |
|---|---|
| `frames` | runs gold once and writes the nine frames for one program and one proof, always 128 segments; the update words of frames 5, 6 and 9 raw or LRU (`--lru`); the jump table as a full new table or as pages added to a base (`--base=static`: frame 2 is the static targets' table); every count of frames 3 and 4, the page counts included; `<out>.json` with every frame's bytes and both forms' sizes; `--verify` |
| `frames-read` | decodes a frame file and prints every frame's fields; `--check` verifies it against itself; `--seg` against gold's v1.3 segment file |
| `frames-images` | the reference: plays a frame file into empty memory for a given *s* and gives the four images (cycle-0, start, the Tracer's copy, final), the program image and the run's table, each with a digest and the pages it took; `--out` writes them |

Files: `Frames`, `UpdateWords`, `FileTree`, `PageImage`, `FrameWriter` (the writing side); `FrameFile`, `Reference`,
`FrameReader` (the reading side, sharing no code with the writing side); `Verify` (`--verify`); in the copied files,
small hooks only (a listener in `SegmentWriter`, the static-only table and the run's targets in `JumpTable`, `copy` and
`find` in `HashTable`, a page accessor in `Memory`, a load without inputs in `Program`). jumpmap was not copied.

**The image format:** every page of a file holding a non-zero byte, ascending by offset, each a u64 offset then the
4,096 bytes; the digest is its SHA-256 (`sha256sum` of a written image). A page not present reads as zero, so equal
contents give equal digests.

**The page counts two ways (owner's change 2):** the writer counts from page sets (`FileTree`: the data pages and the
index pages above them). `Reference` builds every file as a real tree in a model of LPDDR: index pages of 1,024 32-bit
entries (an address in 128-byte units with the owned bit at 31, the hole 0xFFFF_FFFF), a copy as its source's root
with the owned bit clear, a write to a page the file doesn't own taking a page at each level it doesn't own (a shared
index page copied with its children's owned bits cleared). It counts the pages each file takes. **A page written in
several segments is counted once per file**: the first write makes it the file's own, later writes find it owned. The
root rule (C11) is in one place on each side: `FileTree.ROOT_COUNTED` and `Reference.ROOT_IS_A_PAGE_TAKEN`.

## The checks, and their results

*Local* is this session's container (2 threads); *Quest 197* is the runner. Each run of `frames` is twice: a full new
table with every stream raw, and the static base with every stream LRU.

| check | guests (40 runs) | EVM Zone trace-23 | Cyclone input/22 | EVM Zone 2²⁹ | Cyclone 2²⁹ |
|---|---|---|---|---|---|
| cycle-0, start (every *s*), Tracer's copy = start, final images = gold's memory | ok | ok | ok | ok | ok |
| page counts: the reference's trees = frames 3 and 4, every *s* | ok | ok | ok | ok | ok |
| every jump target found by the probe (full / added) | ok | 5,526 / 5,526 | 2,945 / 2,945 | 6,195 / 6,195 | 4,240 / 4,240 |
| LRU decodes to raw (frames 5, 6, 9) | ok | ok | ok | ok | ok |
| `--check` (lengths, counts, program id, advice in 7 and 8, word 66) | ok | ok | ok | ok | ok |
| `--seg`: registers, advice, memory segments = the segment file's | ok | ok (93 segments, 35 past the end empty) | ok (111, 17 empty) | ok | ok |
| the `segments` file unchanged | byte-identical to v1.36 gold's (168 runs) | byte-identical to v1.36 gold's | byte-identical | = published `3cd32881…` | = published `e932baaf…` |
| gold's own tests, `check_guests.sh` | pass | | | | |

Result on the runner: **Quest 197**, PENDING (see the section at the end).

**The checks catch what they are for** (`FramesTest`, in `build.sh`): a per-ASIC count changed, the Tracer's copy count
of the last segment, frame 3's data image pages, frame 4's pages, a reserved header bit, frame 7's advice field against
frame 8, and one value in frame 9 (caught by `--verify` against gold): each is caught. The counts two ways agree on
240 synthetic cases (lengths of 1 to 3·2²⁰ pages, depths 1 to 3, 128 segments with pages rewritten across them).

**Not exercised:** the fallback from an added table to a full one (C10). On every run the static base took the run's
targets within three lines (Cyclone's run targets are all static targets already; EVM Zone's add 2 entries).

## Sizes

Every frame's bytes, the 128-bit header included (local runs; the runner's side files are in Quest 197's results).

| run | table, form | 1 ELF | 2 base | 3 header | 4 | 5 table | 6 data image | 7 registers | 8 advice | 9 segments | file |
|---|---|---|---|---|---|---|---|---|---|---|---|
| EVM Zone trace-23 | full, raw | 2,015,088 | — | 2,912 | 48 | 1,048,608 | 9,280 | 139,280 | 30,352 | 584,496 | 3,830,064 |
| | added, LRU | 2,015,088 | 1,048,608 | 2,912 | 96 | 3,888 | 8,528 | 139,280 | 30,352 | 195,632 | 3,444,384 |
| Cyclone input/22 | full, raw | 3,316,064 | — | 2,880 | 48 | 1,048,608 | 32,288 | 139,280 | 2,768 | 859,024 | 5,400,960 |
| | added, LRU | 3,316,064 | 1,048,608 | 2,880 | 80 | 16 | 26,272 | 139,280 | 2,768 | 217,776 | 4,753,744 |
| EVM Zone 2²⁹ | full, raw | 2,015,088 | — | 2,912 | 48 | 1,048,608 | 9,152 | 139,280 | 31,328 | 2,585,424 | 5,831,840 |
| | added, LRU | 2,015,088 | 1,048,608 | 2,912 | 96 | 3,888 | 8,576 | 139,280 | 31,328 | 1,579,264 | 4,829,040 |
| Cyclone 2²⁹ | full, raw | 3,316,064 | — | 2,880 | 48 | 1,048,608 | 2,903,120 | 139,280 | 774,320 | 17,665,504 | 25,849,824 |
| | added, LRU | 3,316,064 | 1,048,608 | 2,880 | 80 | 16 | 2,436,256 | 139,280 | 774,320 | 9,422,800 | 17,140,304 |

Raw against LRU, frame bytes (the same words either way):

| run | frame 5: words, raw, LRU | frame 6: words, raw, LRU | frame 9: words, raw, LRU | LRU / raw, frame 9 |
|---|---|---|---|---|
| EVM Zone trace-23, full table | 131,073, 1,048,608, 471,632 | 1,158, 9,280, 8,528 | 73,060, 584,496, 195,632 | 0.33 |
| EVM Zone trace-23, added | 1,026, 8,224, 3,888 | | | |
| Cyclone input/22, full table | 131,073, 1,048,608, 416,256 | 4,033, 32,288, 26,272 | 107,376, 859,024, 217,776 | 0.25 |
| Cyclone input/22, added | 0, 16, 16 | | | |
| EVM Zone 2²⁹, full table | 131,073, 1,048,608, 471,632 | 1,141, 9,152, 8,576 | 323,176, 2,585,424, 1,579,264 | 0.61 |
| EVM Zone 2²⁹, added | 1,026, 8,224, 3,888 | | | |
| Cyclone 2²⁹, full table | 131,073, 1,048,608, 416,256 | 362,888, 2,903,120, 2,436,256 | 2,208,186, 17,665,504, 9,422,800 | 0.53 |
| Cyclone 2²⁹, added | 0, 16, 16 | | | |

The page counts:

| run | log₂ K | image length, pages (depth) | program pages | data image pages | table k, entries | table pages taken: full / added | pages written: sum, max | per ASIC, max: start / final / Tracer's copy |
|---|---|---|---|---|---|---|---|---|
| EVM Zone trace-23 | 23 | 32,768 (2) | 491 | 8 | 13, 49,772 | 257 / 3 | 533, 45 | 140 / 140 / 48 |
| Cyclone input/22 | 24 | 131,072 (2) | 808 | 12 | 13, 42,388 | 257 / 0 | 739, 52 | 162 / 162 / 55 |
| EVM Zone 2²⁹ | 23 | 32,768 (2) | 491 | 8 | 13, 49,772 | 257 / 3 | 1,761, 81 | 145 / 145 / 84 |
| Cyclone 2²⁹ | 24 | 131,072 (2) | 808 | 713 | 13, 42,388 | 257 / 0 | 6,093, 1,048 | 1,995 / 1,995 / 1,053 |

Read: the static analysis covers the runs' targets almost entirely (EVM Zone adds 2 entries, 3 pages; Cyclone none),
so with a base table frame 5 is a few kilobytes against a megabyte. Frame 9 at 2²⁹ is at most 17.7 MB raw, far
under section 16's 256 MiB cap. The program image is the largest frame on every run but Cyclone 2²⁹.

## The choices where HF was open

As in `NOTES.md`, section 7, with the owner's ruling on C1:

- **C1** (ruled): the header is one 128-bit word, reserved [63:0], type [95:64], length [127:96]; lengths and counts 32
  bits, never 64, and the writer refuses one that doesn't fit.
- **C2** the LRU table all zeros at each frame's start, carried across segments in frame 9; frames 5 and 6 may be LRU.
- **C3** every image of a proof one length: a16z's `max_ram_k` cells in 4 KB pages (known from the program alone);
  a jump table 2^k lines in pages; depth the smallest *d* ≥ 1 with 1,024^*d* ≥ the length.
- **C4** the strings from options, defaulting to `/programs/<name>`, `/work/<name>-<input>`, `<stem>.base`.
- **C5** the program id: SHA-256 of frame 1's data before its padding.
- **C6** frame 1 lists the pages with a non-zero byte.
- **C7** frame 6: one run per input file, its length rounded up to 8 bytes, zeros included.
- **C8** frame 5: maximal runs of consecutive pages; a table under a page padded with free entries.
- **C9** always 128 segments; a shorter cut list padded with past-the-end segments, as `SegmentWriter` writes them.
- **C10** the base is the static targets' table; the run's targets inserted in ascending order; a full table if one
  doesn't fit.
- **C11** a job's pages: the data pages it writes and every index page above them, the root included; none when it
  writes nothing. One place on each side.

## What I would change in HOST-FRAMES.md (for the owner)

1. **Frame 1's page records are 4,104 bytes** (u64 offset, then the page), so from the second page on a page doesn't
   start on a 128-bit word: the same thing Q1 fixed for headers. Either a 16-byte record head (u32 page number and 12
   bytes of 0, or the offset in a 128-bit word), or all the offsets first, padded, then the pages.
2. **Frame 2's three u32 (k, p1, p3) put the lines 12 bytes off** a 128-bit word; pad them to 16 bytes.
3. **Say each file's length** (C3). The images' length (and so their depth) isn't in any frame but frame 1; the
   Updater's `FILE_PAGES` needs it for every image. Proposed: a16z's `max_ram_k`, which frame 1 carries; or a field in
   frame 3.
4. **Say whether a page count includes the root** (C11), and that a page written in many segments counts once per file.
5. **Say what frame 6 sends** (C7): the whole of each input file, zeros included, is what gold sends now.
6. **The program id's definition** (C5): the SHA-256 of frame 1's data is what the chip can recompute from what it
   stores.
7. **The segment count**: the frames carry exactly 128; a run cut into fewer segments (the hard-cut lists) needs
   empty ones (C9). Worth a sentence.
8. **The LRU flags on frames 5 and 6 are worth keeping:** frame 5 as a full table codes to 0.40-0.45 of raw; frame 6 to
   0.81-0.94; frame 9 to 0.25-0.61.
9. **Base tables work:** with the static analysis as the base, a run adds 0 to 3 pages. The full table is 257 pages,
   so the Open item on a base outliving the runs built on it matters more than its size.
10. **The per-ASIC counts** (an Open item): they are cheap to compute and now checked two ways; keeping them costs
    1,536 bytes of frame 3.

## Quest 197

PENDING.
