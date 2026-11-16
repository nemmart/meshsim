# WORKER1 notes: gold writes the host's frames

Status, 2026-11-16, later: **the owner's rulings on the report (`NEXT.md`) are done**: the frames rewritten to
HOST-FRAMES.md as ruled, every check rerun and passing (80 guest runs at *N* = 64, 128, 256; both small inputs and both
workloads at 2²⁹, three configurations each), Quest 200 sent for the runner. The choices below are revised.

Earlier, 2026-11-16: **go-ahead given (owner, 03:10) with two changes:** the frame header is one 128-bit word (Q1
answered, replacing C1), and an independent check of the page counts by real copy-on-write trees in `Reference`
(section 6). **Built and checked** (2026-11-16): every guest, both small inputs and both workloads at 2²⁹ pass every
check here; Quest 197 runs the same on the runner. Results and sizes: `REPORT.md`. The inputs come from Quest task
111's kit (jolt-asic-planning `fa59d9a`, checked by its SHA-256): the repository itself is not reachable from this
session.

The specification is `v1.4_wip/planning/HOST-FRAMES.md` (HF below). Gold is `v1.36/pipeline/gold/` (unchanged from
v1.3's accepted gold).

*Sections 1 to 6 are the plan as proposed on the morning of 2026-11-16, before the rulings; where they differ from
section 7 (the frame numbers, the program id, the LRU flags, 128 segments, the root), section 7 and the code are
current.*

## 1. Where each frame's data comes from in gold today

| frame | in gold today | new |
|---|---|---|
| 1 ELF | `Program` loads the ELF's sections at or above 0x8000_0000 into `Memory`, then copies the input files into the same `Memory`: there is no program image on its own. jumpmap's `initial.pages` is both together, zero pages listed | the program image alone, as pages; its file length |
| 2 base table | `JumpTable` builds one table, the static targets and the run's merged (`HashTable.choose`); `StaticTargets` has the static keys and bases | a table of the static targets alone; frame 2's layout is v1.3 frame 6's data unchanged (k, p1, p3, lines) |
| 3 proof header | `ProofParams.frame7()`: n and the fields (log₂ K, `lowest_address`, a16z's `MemoryLayout`) ✓. Per segment: `Segment.updates` (the update words) ✓, `Segment.pages4k` (v1.3 frame 1's 4 KB pages written) ✓ | the strings and program id; frame 6's counts; frame 9's 128-bit total; the LRU flags; the per-ASIC page counts and every index page |
| 4 jump table header | k, p1, p3 from `HashTable` ✓ | the base path; frame 5's counts and pages |
| 5 jump table data | `HashTable.image()`: the lines ✓ | the table as update words by whole pages; a run's table as a base plus inserted targets; the pages that differ from the base |
| 6 data image | `Program.copyInput` writes `input.bin`, `trusted_advice.bin`, `untrusted_advice.bin` into memory and keeps no record | the three files as update words |
| 7 register states | `SegmentWriter.stateRecord(sg)`: v1.3 frame 2's 68-word record ✓ exactly | the 128 records concatenated (padding segments: section 5, choice C9) |
| 8 advice logs | `Segment.advice` (v1.3 frame 3) ✓ | the count table, each log padded to 128 bits |
| 9 memory segments | `Segment.updates` (v1.3 frame 4) ✓ | one stream. `SegmentWriter.lruEncode` leads with a u32 count and resets its table per segment: HF has no count and one table over the whole stream, so a new encoder (and a decoder, which gold has never had) |

## 2. What gold does not yet compute

- The program image without the inputs (frame 1), and the program id.
- The data image's update words (frame 6).
- A static-only table; inserting the run's targets into an existing table; the pages where two tables differ.
- **File trees:** a file's depth from its length, and the index pages above a set of data pages. These are needed for
  every "pages" count in frames 3 and 4.
- The per-ASIC counts (frame 3): for segment *s*, the pages of its start image, final image and Tracer's copy.
- LRU without the leading count, one table carried across the stream; an LRU decoder.
- The 128-bit framing, the strings, and the frames' order.
- Everything on the reading side: a decoder for the frames, the self-check, and the reference images.
- Gold's memory as 4 KB pages of the file (offset = address − `lowest_address`) at cycle 0, at every cut and at the end,
  for the image check. jumpmap reads `Memory`'s pages by reflection; in the copy, `Memory` gets a page accessor instead.

## 3. The image format (proposed)

**An image is the pages of one file by their byte offset in the file.** Where a page lands in LPDDR is not part of it.

- **Canonical form, which is also the file written:** every page holding a non-zero byte, ascending by offset, each as
  **u64 offset** (a multiple of 4,096) **then the 4,096 bytes**, little-endian. Nothing else: no header, no trailer. An
  all-zero page is left out, since a page that is not present reads as zero, so two images with the same contents have
  the same bytes whatever pages each happens to hold.
- **Digest:** SHA-256 of the canonical form, so `sha256sum` of a written image file is its digest.
- **The same record as frame 1's pages** (offset, then the page), so a person can read both with one tool.
- **Names:** `<prefix>.program.img`, `.cycle0.img`, `.start-<s>.img`, `.final.img`, `.table.img`. The Tracer's copy is
  equal to the start image by definition (HF: two identical copies), so it is not written twice; the reference checks
  that its two copies are equal and reports one digest.
- **Not jumpmap's `initial.pages`** (u64 offset, u32 flags, u32 0, the page): its flags are "owned" bits, which mean
  nothing here, and it lists zero pages, so equal contents could hash differently.

## 4. Command lines

```sh
./build.sh          # as today, plus the frame tests (section 6)

# write the frames for one program and one proof
java -cp build gold.Main frames <program dir> <input dir> <out file>
     [--cuts=0,a,...|--cuts-file=F] [--segment-rows=4194304]
     [--base=none|static]              # frame 4: a full new table, or pages added to a base (frame 2 included)
     [--lru=none|all|5,6,9]            # which update-word frames are sent LRU-coded (default none)
     [--stem=PATH] [--workdir=PATH] [--base-path=PATH]   # frame 3's and 4's strings (choice C4)
     [--verify]                        # after writing: the checks against gold's own run (section 6)
#  writes <out file> and <out file>.json (every frame's bytes, raw and LRU sizes for 5, 6 and 9, page counts, SHA-256)

# read a frame file
java -cp build gold.Main frames-read <frame file> [--check] [--seg=<v1.3 segment file>] [--brief]
#  prints every frame's fields; --check: the file against itself; --seg: frames 7, 8, 9 against gold's segment file

# the reference images
java -cp build gold.Main frames-images <frame file> <s> [--out=<prefix>]
#  prints the digests of the program image, the cycle-0, start (both copies) and final images, and the run's table;
#  --out writes them in the format of section 3
```

`segments`, `compare` and `run` are unchanged, and so is their output.

## 5. The code

In `updater_wip/pipeline/gold/`, a copy of `v1.36/pipeline/gold/` made unchanged as the first commit.

| file | what |
|---|---|
| `Frames.java` (new) | the frame header, the 128-bit padding, strings, the type numbers, little-endian helpers |
| `UpdateWords.java` (new) | building runs from cells; raw and LRU encoding (no count; one table for a stream) |
| `FileTree.java` (new) | depth from a length; the index pages above a set of data pages |
| `FrameWriter.java` (new) | runs gold once (through `SegmentWriter`), builds the nine frames, the counts and the size report |
| `FrameReader.java` (new) | decodes a frame file; prints it; `--check`; `--seg` |
| `Reference.java` (new) | plays a frame file into empty memory for a given *s*: the images and the run's table, every file a real copy-on-write tree, and the pages each run takes. Its own frame, update-word and LRU decoding, written to be read, sharing nothing with the writer, `UpdateWords` or `FileTree` |
| `PageImage.java` (new) | an image as sparse 4 KB pages; the canonical form, digest and writing |
| `Main.java` | the three new commands |
| `SegmentWriter.java` | an optional listener, called with the machine at each cut and at the end (for gold's memory and the jumps); nothing in its output changes |
| `JumpTable.java`, `HashTable.java` | the static-only table, the run's targets as a list, a table from an image, the jumps the run takes |
| `Memory.java` | a read-only page accessor (instead of jumpmap's reflection) |
| `Program.java` | the program image without the inputs (a second load with no input files) |

**jumpmap is not copied.** Its useful parts are already in gold (`HashTable`, `StaticTargets`); `Image.java`'s apply and
check are a dozen lines, rewritten in `Reference` and `PageImage` in the format above. jumpmap's guests and fixtures
are read from `v1.36/pipeline/jumpmap/`, unchanged.

## 6. The checks, and where they run

`frames --verify` (gold's own run against the frame file read back from disk, through `Reference` only):
- **images:** for every *s* from 0 to 127, `Reference`'s start image (and the Tracer's copy) equals gold's memory at the
  cut before *s*; the cycle-0 image equals gold's memory before the first row; the final image equals it at the end.
  Compared by digest in the canonical form, and on a mismatch word by word to name the first differing word.
- **the jump table:** every jump the run takes (gold's targets, as `JumpTable.observe` sees them) found by the Tracer's
  probe (`HashTable.probe`: k, p1, p3, at most three lines) in the run's table as `Reference` built it from frames 2,
  4 and 5, with the base equal to gold's next `instr_idx`. Run with `--base=none` and `--base=static`.
- **LRU:** each LRU-coded stream decoded equals the raw form word for word (all three, in every run, whatever `--lru`).

`frames-read --check`: every frame's length; the order and types; frame 3's and 4's counts recomputed from frames 5, 6
and 9 (64-bit words, 128-bit words, the LRU flag, every page count from the trees); the advice counts in frame 7's word
1 against frame 8's table. `--seg`: frames 7 and 8 against the segment file's frames 2 and 3 (the same values,
repacked), and frame 9's segments against its frames 4 or 5 (the same words).

**The page counts, a second way** (owner, 2026-11-16). The writer computes every "pages" count of frames 3 and 4 from
`FileTree` (sets of page numbers, the index pages above them). `Reference` builds every file as a real tree with
copy-on-write, sharing no code with `FileTree`: index pages of 1,024 entries, each a page number with an owned bit; a
copy is a new root whose entries point at its source's pages with the owned bit clear; a write to a page the file
doesn't own takes a fresh page, top down, copying each shared index page on its path (its entries' owned bits
cleared) and the data page. It counts the pages each run takes (the jump table run, the data image run, and for the
memory segments the start image, the final image and the Tracer's copy) and those counts must equal the header's,
for every *s*. **A page written in several segments is counted once per file:** the first write makes the page the
file's own, and later writes find it owned and take nothing; on the `FileTree` side the counts are unions of page
sets. The root (choice C11) is counted in one place in each: `FileTree.ROOT_COUNTED` and `Reference`'s equivalent.

**Nothing old broke:** `build.sh`'s tests; `test/guests/check_guests.sh`; the `segments` command's file
byte-identical to v1.36 gold's, on every guest, on both small inputs with the hard cuts, and at 2²⁹.

**Runs:**
- **The guests:** gold's (`test/guests/`, the ones that run to their end) and jumpmap's (`out/`, `jumps` included), with
  `--segment-rows` set so each run spreads over many of the 128 segments. Here, in `build.sh`.
- **The small inputs** (EVM Zone trace-23, Cyclone input/22, with their hard-cut lists) built from the pins as
  `v1.36/pipeline/inputs/README.md` says. If jolt-asic-planning can't be reached from this session, on the runner.
- **Both workloads at 2²⁹, on the runner** (one Quest task: frames twice per workload, `--base=none --lru=none` and
  `--base=static --lru=all`, each with `--verify`, then `frames-read --check --seg`, and the old `segments` file
  against its published SHA-256). The number claimed only when the task is ready, as the prompt says.
- **Sizes** from the `.json` side files into the report's tables.

## 7. Choices

Where HF is open or silent. Each: the choice, the reason, the alternative. **Revised 2026-11-16 after the owner's
rulings on the report (`NEXT.md`); HOST-FRAMES.md on main has them.** Ruled choices are marked; what they replaced is
in the history of this file.

- **C1. The header** (ruled): one 128-bit word: compression [7:0] (0 none, 1 LRU), reserved [63:8], type [95:64],
  length in 128-bit words [127:96]. Lengths and counts are 32 bits; the writer refuses a value that doesn't fit.
- **C2. Compression** (ruled): on frames 2, 5, 6 and 9 only; each is sent in whichever form takes fewer 128-bit words,
  **a tie sent uncompressed** (my reading: the simpler form). `--compression=none|lru` forces one form, for the tests
  only. The LRU table is all zeros at each frame's start and carries on across segments in frame 9.
- **C3. Lengths** (ruled): the program image's length is frame 1's, a16z's `max_ram_k` in 4 KB pages; every image of
  the proof is a copy and inherits it. A jump table's length is 2^k lines in pages, at least 1. Depth: the smallest
  *d* ≥ 1 with 1,024^*d* ≥ the length.
- **C4. The strings:** program stem `--stem` (default `/programs/<program dir's name>`), working directory `--workdir`
  (default `/work/<program>-<input>`), the base table's path `--base-path` (default `<stem>.base`).
- **C5. The program id** (ruled): dropped.
- **C6. Frame 2** (ruled): every page of the program image holding a non-zero byte, as update words in whole pages,
  maximal runs of consecutive pages, into an empty file.
- **C7. Frame 6, the least data** (ruled; the method is mine). Each input file's words, leading and trailing zero
  words dropped; a stretch of at least *T* zero words skipped by ending the run and starting another. For the raw
  form *T* = 2 is exact (a skipped word saves 8 bytes, a new run costs one header word). For LRU the cost isn't local
  (the table), so gold encodes with each *T* in {2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 128, 256, ∞} and keeps the
  smallest. Then the smaller of the two forms is sent (C2). A word is skipped only where the program image is zero
  too (checked, as the owner asked; the cycle-0 image check also covers it). *Alternative:* an exact optimum for LRU,
  which would take a search over the gaps for a few bytes.
- **C8. Frame 5's runs:** maximal runs of consecutive pages. A table smaller than a page is padded to the page with
  free entries (all ones).
- **C9. N segments** (ruled): `--asics=64|128|256`, default 128; in no frame; the reader infers *N* from frame 7's
  length. A shorter cut list is padded with segments past the run's end, as `SegmentWriter` writes them; more than *N*
  cuts is refused. The `segments` command is unchanged (its default stays 128).
- **C10. Base tables** (ruled): established by the host as frames 4 (with a path) and 5 on their own, sent after
  frames 1 and 2 and before frame 3. Gold's base is the static targets' table (`--base=static`). **A base table's own
  frame 4 names no base** (my reading: a base is always a full new table). The run's table is the base with the run's
  targets not already in it inserted in ascending key order; if one doesn't fit within three lines, a full new table
  (static and run targets), and the run's frame 4 names no base; the base is still sent. The fall-back is tested with
  a made-up full base (`FramesTest`).
- **C11. Page counts** (ruled): the pages the Updater takes from the free page queue: the data pages a run writes and
  the index pages above them, **without the root**, which the firmware makes. A page written in several segments
  counts once for each file. `FileTree.ROOT_COUNTED` and `Reference.ROOT_IS_A_PAGE_TAKEN`, one place each.
- **C12. The frame file's order:** 1, 2, then 4 and 5 for the base if there is one, then 3 to 9 (`NEXT.md`, item 2).

## 8. The order of the work, after the go-ahead

1. The unchanged copy of `v1.36/pipeline/gold/` (one commit).
2. `Frames`, `UpdateWords`, `FileTree`, `PageImage`, the hooks; `frames` writing all nine frames.
3. `frames-read` and `--check`, `--seg`.
4. `Reference` and `frames-images`; `--verify`.
5. The tests in `build.sh` on the guests; the "nothing old broke" checks.
6. The small inputs; then the Quest task at 2²⁹; `REPORT.md`.

Each a commit, pushed, `main` merged in as I go.
