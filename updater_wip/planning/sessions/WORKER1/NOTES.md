# WORKER1 notes: gold writes the host's frames

Status, 2026-11-16: **plan proposed, waiting for the owner's go-ahead.** No code yet; `updater_wip/pipeline/gold/`
does not exist yet (the unchanged copy is the first commit after the go-ahead).

The specification is `v1.4_wip/planning/HOST-FRAMES.md` (HF below). Gold is `v1.36/pipeline/gold/` (unchanged from
v1.3's accepted gold).

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
| `Reference.java` (new) | plays a frame file into empty memory for a given *s*: the images and the run's table. Its own update-word and LRU decoder, written to be read, sharing nothing with the encoder |
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

Where HF is open or silent. Each: the choice, the reason, the alternative.

- **C1. The frame header is 8 bytes** (u32 type, u32 length in 128-bit words), as written. The data after it starts 8
  bytes off a 128-bit boundary from the second frame on: raised in `QUESTIONS.md` Q1, not blocking.
- **C2. The update words' LRU table** starts all zeros at each frame's start, as written; in frame 9 it carries across
  segments. `--lru` picks per frame; frames 5 and 6 may be LRU-coded (HF's flags are there). The writer computes both
  forms of every stream for the size report and the check, whatever it sends.
- **C3. File lengths and depths.** Every image of a proof (the program image, the cycle-0, start, final images and the
  Tracer's copy) has **one length: a16z's `max_ram_k` in bytes, in 4 KB pages** (`ProofParams.maxRamK` × 8 / 4,096,
  at least 1). It follows from the program's layout alone, so frame 1 can carry it before any run, K never exceeds it
  (gold rejects the run otherwise), and every copy is a copy of a root of the same depth. A jump table's length is its
  2^k lines in pages, at least 1. **Depth: the smallest d ≥ 1 with 1,024^d ≥ the length** (the spec's capacity rule,
  2^(5 + 10·d) lines, with an index level always, as the 2026-11-15 note proposes for small tables). *Alternative:*
  the length from the run's K, which frame 1 can't know; or the whole span to 2³².
- **C4. The strings.** Program stem: `--stem`, default `/programs/<program dir's name>`. Working directory: `--workdir`,
  default `/work/<program dir's name>-<input dir's name>`. Base path: `--base-path`, default `<stem>.base`. Gold doesn't
  know the chip's paths; these are labels the reader prints. *Alternative:* required options, with no defaults.
- **C5. The program id:** SHA-256 of frame 1's data (before its padding), the image as stored. *Alternative:* the
  ELF's SHA-256, which the chip can't recompute from what it stores.
- **C6. Frame 1's pages:** every page of the program image holding a non-zero byte (section 3's rule), ascending.
  *Alternative:* every page a section covers, zeros included, as jumpmap's `initial.pages`; bigger, and the same
  contents.
- **C7. Frame 6's runs:** one run per input file present and non-empty, covering its length rounded up to 8 bytes,
  zero words included, in address order. *Alternative:* only the non-zero words, in many runs.
- **C8. Frame 5's runs:** maximal runs of consecutive pages (each a multiple of 512 words). A table smaller than a page
  is padded to the page with free entries (all ones) and sent as one page. *Alternative:* one run per page; padding
  with zeros, which are not free entries.
- **C9. Always 128 segments.** A cut list of fewer than 128 (the hard cuts: 93 and 111) is padded with segments past the
  run's end, written as `SegmentWriter` writes its own past-the-end segments: pc, `instr_idx` and offset 0, the final
  registers, no advice, no updates. More than 128 is rejected. *Alternative:* a segment count in frame 3, which HF
  doesn't have.
- **C10. The base table (`--base=static`)** is the static targets alone (`StaticTargets`, placed by `HashTable.choose`),
  the HF's "the static analysis" case. The run's table is the base, read back from its image, with the run's targets
  not already in it inserted in ascending key order by the probe's rule; if any doesn't fit within three lines, the run
  gets a full new table (static and run targets, as today's `JumpTable`) and frame 4 names no base. A target in the
  base with a different `instr_idx` is an error. *Alternative:* a base from an earlier run's table or frame file;
  left out, as a test of the same path.
- **C11. "The pages the job takes, data and index":** the data pages the job writes and every index page above them,
  the root included; no pages when it writes nothing. The same rule for frame 4's and frame 6's counts and for the
  per-ASIC counts: the start image is segments 0 to *s* − 1's written pages, the final image *s* to 127's, the Tracer's
  copy segment *s*'s (v1.3 section 16's rule), each with its index pages in that file's tree. *Alternative:* counting
  the root apart, since C&C 2 may supply it in `ROOT`.

## 8. The order of the work, after the go-ahead

1. The unchanged copy of `v1.36/pipeline/gold/` (one commit).
2. `Frames`, `UpdateWords`, `FileTree`, `PageImage`, the hooks; `frames` writing all nine frames.
3. `frames-read` and `--check`, `--seg`.
4. `Reference` and `frames-images`; `--verify`.
5. The tests in `build.sh` on the guests; the "nothing old broke" checks.
6. The small inputs; then the Quest task at 2²⁹; `REPORT.md`.

Each a commit, pushed, `main` merged in as I go.
