# WORKER1 report: gold writes the host's frames

2026-11-16. Branch `updater-w1`. The specification is `v1.4_wip/planning/HOST-FRAMES.md` (HF) as on main after the
owner's rulings on this report's first version (`NEXT.md`). The plan and the choices are `NOTES.md` (section 7 is
current); the questions are `QUESTIONS.md` (Q1 answered; Q2, the readings taken on the rulings).

## Status

| | |
|---|---|
| the frames as first specified | built and checked; **Quest 197 passed on the runner** (every step: build and tests, the small inputs, v1.36 gold's `segments` byte-identical, both workloads at 2²⁹) |
| the frames as ruled on 2026-11-16 | built and checked here; **Quest 200 sent** (updater-w1 `562fd725`), the same steps; its result is below when it comes |

## What was built

In `updater_wip/pipeline/gold/`, a copy of `v1.36/pipeline/gold/` (its first commit, unchanged), with:

| command | what |
|---|---|
| `frames` | runs gold once and writes the frames for one program and one proof in HF's order: 1 and 2 the program image; 4 and 5 a base table on its own (with `--base=static`); 3 the proof header; 4 and 5 the run's jump table; 6 the data image; 7 to 9 the segments' data. *N* segments (`--asics`); frames 2, 5, 6 and 9 in the smaller form; every count of frames 1, 3 and 4; `<out>.json` with every size; `--verify` |
| `frames-read` | decodes and prints every frame; `--check` the file against itself; `--seg` against gold's v1.3 segment file |
| `frames-images` | the reference: plays a frame file into empty memory for a given *s*: the program image, the cycle-0, start and final images, the Tracer's copy, the run's table; digests, the pages each run took; `--out` writes them |

The writing side (`Frames`, `FrameWriter`, `UpdateWords`, `FileTree`, `PageImage`) and the reading side (`FrameFile`,
`Reference`, `FrameReader`) share no code. `Verify` (`--verify`) compares the reading side's results with gold's own
run. In the copied files, small hooks only. jumpmap was not copied.

**The image format:** every page of a file holding a non-zero byte, ascending by offset, each a u64 offset then the
4,096 bytes; the digest is its SHA-256 (`sha256sum` of a written image).

**The page counts two ways:** the writer counts from page sets (`FileTree`); `Reference` builds every file as a real
tree in a model of LPDDR (index pages of 1,024 entries with owned bits; a copy shares its source's pages; a write to a
page the file doesn't own takes a page at each level it doesn't own) and counts the pages each run takes. **Without
the root** (ruled: the firmware makes it), in one place on each side. **A page written in several segments counts
once per file:** the first write makes it the file's own; later writes take nothing.

## The rulings, as built

| # | ruling | in gold |
|---|---|---|
| 1 | the program image is two frames | frame 1: stem, file length, frame 2's 64- and 128-bit words, pages; frame 2: every page with a non-zero byte, maximal runs of whole pages, into an empty file |
| 2 | a base table is frames 4 and 5 on their own | sent after 1 and 2; frame 4's new first field is its path; the run's frame 4 has path "" |
| 3 | no length fields | the images inherit frame 1's length; a table's follows from k |
| 4 | page counts exclude the root | `FileTree.ROOT_COUNTED = false`, `Reference.ROOT_IS_A_PAGE_TAKEN = false` |
| 5 | frame 6 sends the least data | zero stretches skipped by a new run when that is smaller in the form sent: raw by the exact rule, LRU by trying 14 thresholds; skipped words checked zero in the program image |
| 6 | no program id | removed from frame 3 and the checks |
| 7 | *N* segments | `--asics=64\|128\|256`; the reader infers *N* from frame 7; `segments` unchanged |
| 8 | compression in the header | [7:0]: 0 none, 1 LRU; frames 2, 5, 6, 9 sent in the smaller form (a tie uncompressed); `--compression=none\|lru` for the tests |
| 9 | the fall-back tested | `FramesTest`: a base already full (k 0, 16 made-up keys) on the jumps guest: the run gets a full new table, its frame 4 names no base, every jump is found, `--check` passes |
| 10 | the per-ASIC counts stay | without the root |

## The checks, and their results

Each workload is written three times: a full table, uncompressed, *N* 128; the static base, the smaller form, *N* 128
(the real configuration); the static base, all LRU, *N* 256. The guests four times (adding *N* 64).

| check | guests (80 runs) | EVM Zone trace-23 | Cyclone input/22 | EVM Zone 2²⁹ | Cyclone 2²⁹ |
|---|---|---|---|---|---|
| cycle-0, start (every *s*), Tracer's copy = start, final images = gold's memory | ok | ok | ok | ok | ok |
| page counts: the reference's trees = frames 1, 3 and 4 (program image, base, table, data image, per ASIC for every *s*) | ok | ok | ok | ok | ok |
| every jump target found by the probe | ok | 5,526 | 2,945 | 6,195 | 4,240 |
| both forms of frames 2, 5, 6, 9 decode; frame 6's two forms make one image; the smaller sent | ok | ok | ok | ok | ok |
| `--check` (lengths, counts, advice in 7 and 8, word 66, stems) | ok | ok | ok | ok | ok |
| `--seg`: registers, advice, memory segments = the segment file's | ok | ok | ok | ok | ok |
| the `segments` file unchanged | byte-identical to v1.36 gold's (168 runs) | byte-identical | byte-identical | = published `3cd32881…` | = published `e932baaf…` |
| gold's own tests, `check_guests.sh` | pass | | | | |

**The checks catch what they are for** (`FramesTest`): frame 1's pages, a per-ASIC count, the last Tracer's copy count,
frame 3's data image pages, frame 4's pages, a reserved header bit, compression on frame 3, frame 7's advice field
against frame 8, and a value in frame 9 (by `--verify`): each caught. The counts two ways agree on 240 synthetic cases
(lengths 1 to 3·2²⁰ pages, depths 1 to 3, pages rewritten across segments).

`build.sh` runs the guests, `FramesTest` and the `segments` comparison, and fails if any fails. That is fixed today:
until now a pipe hid `FramesTest`'s exit status.

## Sizes

Every frame's bytes with its 16-byte header. **L**: sent LRU-coded. Local runs; Quest 200 writes the same side files.

| run | configuration | 1 | 2 program image | 4 base | 5 base | 3 header | 4 | 5 table | 6 data image | 7 registers | 8 advice | 9 segments | file |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| EVM Zone trace-23 | full table, none, N 128 | 80 | 2,011,168 | — | — | 2,880 | 48 | 1,048,608 | 8,800 | 139,280 | 30,352 | 584,496 | 3,825,712 |
| | static base, smaller, N 128 | 80 | 1,997,280 L | 96 | 471,616 L | 2,880 | 96 | 3,888 L | 8,512 L | 139,280 | 30,352 | 195,632 L | 2,849,712 |
| | static base, LRU, N 256 | 80 | 1,997,280 L | 96 | 471,616 L | 5,440 | 96 | 3,888 L | 8,512 L | 278,544 | 30,864 | 195,632 L | 2,992,048 |
| Cyclone input/22 | full table, none, N 128 | 64 | 3,309,600 | — | — | 2,832 | 48 | 1,048,608 | 27,840 | 139,280 | 2,768 | 859,024 | 5,390,064 |
| | static base, smaller, N 128 | 64 | 3,192,128 L | 80 | 416,256 L | 2,832 | 80 | 16 | 26,048 L | 139,280 | 2,768 | 217,776 L | 3,997,328 |
| | static base, LRU, N 256 | 64 | 3,192,128 L | 80 | 416,256 L | 5,392 | 80 | 16 L | 26,048 L | 278,544 | 3,280 | 217,776 L | 4,139,664 |
| EVM Zone 2²⁹ | full table, none, N 128 | 80 | 2,011,168 | — | — | 2,880 | 48 | 1,048,608 | 8,672 | 139,280 | 31,328 | 2,585,424 | 5,827,488 |
| | static base, smaller, N 128 | 80 | 1,997,280 L | 96 | 471,616 L | 2,880 | 96 | 3,888 L | 8,560 L | 139,280 | 31,328 | 1,579,264 L | 4,234,368 |
| | static base, LRU, N 256 | 80 | 1,997,280 L | 96 | 471,616 L | 5,440 | 96 | 3,888 L | 8,560 L | 278,544 | 31,840 | 1,579,264 L | 4,376,704 |
| Cyclone 2²⁹ | full table, none, N 128 | 64 | 3,309,600 | — | — | 2,832 | 48 | 1,048,608 | 2,727,424 | 139,280 | 774,320 | 17,665,504 | 25,667,680 |
| | static base, smaller, N 128 | 64 | 3,192,128 L | 80 | 416,256 L | 2,832 | 80 | 16 | 2,435,792 L | 139,280 | 774,320 | 9,422,800 L | 16,383,648 |
| | static base, LRU, N 256 | 64 | 3,192,128 L | 80 | 416,256 L | 5,392 | 80 | 16 L | 2,435,792 L | 278,544 | 774,832 | 9,422,800 L | 16,525,984 |

The update-word frames in both forms (frame bytes; the static-base runs). Frame 6's two forms hold different words
(C7): raw skips every zero stretch of two words or more; LRU skips stretches of *T* or more, *T* the best found.

| run | frame 2: words, raw, LRU | the full table (the base's frame 5): words, raw, LRU | frame 6: words raw / LRU, raw, LRU (*T*) | frame 9: words, raw, LRU | LRU / raw: 2, table, 6, 9 |
|---|---|---|---|---|---|
| EVM Zone trace-23 | 251,393, 2,011,168, 1,997,280 | 131,073, 1,048,608, 471,616 | 1,097 / 1,128, 8,800, 8,512 (24) | 73,060, 584,496, 195,632 | 0.99, 0.45, 0.97, 0.33 |
| Cyclone input/22 | 413,698, 3,309,600, 3,192,128 | 131,073, 1,048,608, 416,256 | 3,477 / 3,559, 27,840, 26,048 (16) | 107,376, 859,024, 217,776 | 0.96, 0.40, 0.94, 0.25 |
| EVM Zone 2²⁹ | 251,393, 2,011,168, 1,997,280 | 131,073, 1,048,608, 471,616 | 1,081 / 1,111, 8,672, 8,560 (24) | 323,176, 2,585,424, 1,579,264 | 0.99, 0.45, 0.99, 0.61 |
| Cyclone 2²⁹ | 413,698, 3,309,600, 3,192,128 | 131,073, 1,048,608, 416,256 | 340,925 / 360,527, 2,727,424, 2,435,792 (12) | 2,208,186, 17,665,504, 9,422,800 | 0.96, 0.40, 0.89, 0.53 |

The page counts (without the root):

| run | log₂ K | image length (depth) | program image: pages sent, taken | data image | table k, entries | taken: the base, a full table, added | pages written: sum, max | per ASIC, max: start / final / Tracer's copy |
|---|---|---|---|---|---|---|---|---|
| EVM Zone trace-23 | 23 | 32,768 (2) | 491, 492 | 7 | 13, 49,772 | 256, 256, 2 | 533, 45 | 139 / 139 / 47 |
| Cyclone input/22 | 24 | 131,072 (2) | 808, 809 | 11 | 13, 42,388 | 256, 256, 0 | 739, 52 | 161 / 161 / 54 |
| EVM Zone 2²⁹ | 23 | 32,768 (2) | 491, 492 | 7 | 13, 49,772 | 256, 256, 2 | 1,761, 81 | 144 / 144 / 83 |
| Cyclone 2²⁹ | 24 | 131,072 (2) | 808, 809 | 712 | 13, 42,388 | 256, 256, 0 | 6,093, 1,048 | 1,994 / 1,994 / 1,052 |

At *N* 256 the per-ASIC maxima are the same: with 2²² rows a segment the runs have at most 89 segments with rows, and
the rest are past the end.

## What the numbers say

- **The program image barely compresses** (0.96-0.99): it is code and data. It is the largest frame on three of the
  four runs, and it is sent once per program.
- **A base table pays:** the run's frame 5 is 3,888 bytes (EVM Zone, 2 pages added) or nothing (Cyclone, all its
  targets are static), against 1 MB for a full table raw or 0.4 MB LRU-coded.
- **The smaller form is LRU for frames 2, 6 and 9 on every run.** Frame 5 with no pages to send goes uncompressed: 16
  bytes either way, a tie.
- **Frame 6's least data saves 5-14% raw** against one run per file (this report's first version: 9,280 to 8,800
  bytes on EVM Zone trace-23, 32,288 to 27,840 on Cyclone input/22, 2,903,120 to 2,727,424 on Cyclone 2²⁹). LRU then
  takes off another 1-11%.
- ***N* costs 1,088 bytes a segment in frame 7** and 24 in frames 3 and 8 together; *N* 256 adds 142 KB to a proof.

## Quest 197 (the frames as first specified)

PASS on the runner (it was down for an expired token, then ran): build and tests 43 s, the small inputs 24 s, v1.36
gold's `segments` byte-identical 31 s, both workloads at 2²⁹ 114 s; both 2²⁹ `segments` files equal the published v1.3
SHA-256s; every `--verify` VERIFIED.

## Quest 200 (the frames as ruled)

PENDING: updater-w1 `562fd725`, kit `bcb5497b…`. The same task passed start to finish in a copy of the queue here.
