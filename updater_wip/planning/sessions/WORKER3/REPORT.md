# WORKER3 report: v1.37, the Tracer on images the Updater built

Branch `updater-w3`. One section a stage, added as each ends. The plan and the choices are in `NOTES.md`; the
questions and the owner's answers in `QUESTIONS.md`. Paths without a folder are in `v1.37_wip/`. II is
`INGRESS-INTERFACE.md`, UI `UPDATER-INTERFACE.md`, HF `HOST-FRAMES.md`, HL `HOST-LINK.md`.

## Stage 1: the units, each tested alone (2026-11-16)

**Done.** Ingress's host link and the Updater are black boxes behind their registers; the file store places pages
where it is told; each is tested without firmware. With the switch off the machine is v1.36's and every v1.36
step passes unchanged.

### What was built

| | where |
|---|---|
| the switch `MachConfig::host_link` (off by default): the master tile gets the link in Ingress, a DMA input on its router, and the Updater as peripheral 2; the tracer tile's memory system and the Updater share one file store | `tests/mach.h`, `mach.cpp`; `src/units/tile_ctl.cpp`, `periph_slot.cpp` |
| Ingress's host link | `src/units/host_link.h` (the packets' reader, CRC-32C), `src/units/ingress_egress.h` |
| the Updater, peripheral type 5 | `src/units/updater.h`, `updater.cpp` |
| the file store's pages by address | `src/units/file_store.h`, `file_store.cpp` |
| the frames' reader for tests, the proof's sequence on the Updater alone, the tool `updater_frames` | `tests/frames.*`, `tests/updater_bench.*`, `tests/updater_frames_main.cpp` |
| a provisional packet writer and the tool `packetize` | `tests/packet_writer.h`, `tests/packetize_main.cpp` |
| the firmware's header for Stage 2 | `fw/include/host.h` |

`CHANGES-v1.37.md` lists each addition with the document that specifies it.

### The checks and their results

| check | result |
|---|---|
| **v1.36's steps with the switch off:** `./run_tests.sh all --check`, here (clang, 2 CPUs), with the pinned firmware toolchain | **all passed**; `fw-build` rebuilt all 17 images equal to the committed ones; 2 steps SKIPPED (`segment-real`, `tracer-block`: the recorded windows are not in this session, see below). No golden log changed |
| **the file store** (`test_file_store`, 7 tests): a page placed at an address is read back there; pages handed out pass over placed ones; the old interface gives the addresses it gave | pass; the memory system's and the Tracer's suites print what they printed |
| **the Updater alone** (`test_updater`, 19 tests): registers, both queues and their credits, update words raw and LRU against a second decoder, copy-on-write, the Tracer's copy, the root empty and copied, a stall on each queue, each error, and four cases the simulator must stop on | pass |
| **the Updater against gold, guests:** three committed frame files (`tests/updater/`: a guest raw, the same LRU, one with a base table; N = 64, 50 rows a segment), each at *s* = 0, 20, 63, every image against gold's `frames-images` page for page | **equal**, 9 of 9; `PAGE_COUNT` 0 at every done; no error bit. `make_fixtures.sh --check` makes the fixtures again with gold and gets the same bytes |
| **the Updater against gold, the small inputs:** EVM Zone trace-23 and Cyclone input/22, frames made here by gold; 13 cases (table below) | **equal**, 13 of 13: program, table, cycle-0, start, the Tracer's copy and final, by SHA-256 of gold's own image form; `PAGE_COUNT` 0 at every done (the Updater took exactly the frames' page counts); no error bit |
| **Ingress alone** (`test_ingress_link`, 32 tests): each check of the link in HL's order, idle zeros, the rule for a service flag to the letter, reading a packet in one transfer and in several, the two streams independent, four packets held and the link waiting, length-0 packets, the number's wrap, each error and what stays readable after it, a file cut inside a packet | pass |
| **Ingress against WORKER2's vectors** (`test_packet_vectors`; `updater-w2` at `d3e07c8b`, `test/packets/`): the 8 CRC-32C values; all 43 packets, by the reader and through the black box's registers and DMA; the 5 packet files; the 32 damaged files, each with the error and the place `VECTORS.txt` gives | **all agree** |
| **the provisional writer against gold's packetizer:** `packetize` on WORKER2's `small.frames` | byte-identical to gold's `small.packets` |

Ingress was written from II and HL; WORKER2's Java was not read. HL's own header example was checked by hand.

### The Updater's figures on the small inputs

All against gold's images, all equal. "runs" are the Updater's runs for one proof (program, base table if any,
jump table, data image, and the three of the memory segments); "pages" are those taken from the free page queue,
which is what the frames said; cycles are the black box's (`NOTES.md`, section 6).

| frames | N | *s* | runs | pages | pages read | pages written | cycles busy | of them waiting on memory |
|---|---|---|---|---|---|---|---|---|
| EVM Zone 23, raw, no base | 128 | 0 | 6 | 952 | 50 | 983 | 855,433 | 426,524 |
| | 128 | 1 | 6 | 1,029 | 35 | 1,035 | 879,913 | 451,004 |
| EVM Zone 23, LRU, base | 128 | 0 | 7 | 954 | 51 | 986 | 858,009 | 428,040 |
| | 128 | 1 | 7 | 1,031 | 36 | 1,038 | 882,489 | 452,520 |
| EVM Zone 23, LRU, base, 8-window segments (12) | 64 | 0 | 7 | 908 | 192 | 1,047 | 906,926 | 467,532 |
| | 64 | 1 | 7 | 913 | 197 | 1,048 | 909,471 | 470,076 |
| | 64 | 6 | 7 | 935 | 195 | 1,051 | 913,743 | 474,348 |
| | 64 | 11 | 7 | 946 | 180 | 1,063 | 916,942 | 477,548 |
| Cyclone 22, raw, no base, 8-window segments (6) | 64 | 0 | 6 | 1,256 | 113 | 1,342 | 1,217,007 | 584,612 |
| | 64 | 1 | 6 | 1,276 | 115 | 1,352 | 1,223,920 | 591,524 |
| | 64 | 3 | 6 | 1,390 | 112 | 1,460 | 1,269,600 | 637,204 |
| | 64 | 5 | 6 | 1,268 | 112 | 1,342 | 1,220,191 | 587,796 |
| Cyclone 22, LRU, base | 256 | 0 | 7 | 1,398 | 8 | 1,405 | 1,225,800 | 599,844 |

Under a million cycles of Updater for a small proof, most of it the program image (about 460,000 for EVM Zone's
492 pages). Stalls on the queues are 0 here because the bench keeps both full; the chip's figures come in
Stage 3.

### What is not checked yet, and why

- **The steps on recorded windows, and `make check`: done on the runner.** The small inputs' build fetches a
  dependency of the tracer from GitHub's archive, which this session's network refuses; and `make check` (four
  builds and TSan) is too much for two CPUs. **Quest 202** (the small inputs built from the pinned sources, then
  `./run_tests.sh all --check`): **PASS**, every step, `segment-real` and `tracer-block` with them; one step
  SKIPPED, `fw-build` (the runner has no xPack toolchain; the images were rebuilt equal here). **Quest 203**
  (`make check`, debug and release, gcc and clang, TSan): **PASS**, 782 s. Both at `b0e64941`, made by
  `updater_wip/tasks/v137-check/make_task.sh`.
- **WORKER2's vectors are not on main.** I read them from a checkout of `updater-w2`; the step is SKIPPED where
  they are absent, and reads `../updater_wip/pipeline/gold/test/packets` once they are merged.

### For the documents (kept as a list; the full lists come with Stage 3)

- **UI:** what a run does after an error is not said. The black box stops (`busy` stays 1, no `done`).
- **UI:** a free page queue entry is "one free page, in [31:0]"; the unit is not said. I take the page's address
  in 128-byte units, the form a root and an index entry have.
- **II:** a file or link that goes quiet inside a packet has no register; nothing to change, but worth saying
  that silence is not an error.
- **HL, II:** on main since `8385dacd` they say what I built (CRC-32C defined, the order of the tests, length-0
  packets, `format_error`). Nothing more from Stage 1.

## Stage 2: the master tile's firmware (2026-11-16)

**Done.** The master tile runs real firmware that reads the frames out of Ingress, drives the Updater and hands
the tracer tile its segment. On the whole chip, from a packet file, gold's guests are traced on the images the
Updater built and replayed; every image equals gold's. `tracer_ctl.c`, `tracer_layout.h`, `replay_ctl.c` and
`cc1.c` are unchanged, and so are their images.

### What was built

| | where |
|---|---|
| the master tile's program: 7.6 KB of code | `fw/cc/master.c`, `fw/cc/master_layout.h`; `fw/images/cc/master.elf` |
| the whole chip from a packet file: `run_chip` with a proof, and the proof's checks | `tests/segment_bench.h` (`Proof`), `tests/proof_bench.h`, `proof_bench.cpp` |
| the test, on gold's fixtures | `tests/test_master.cpp`; the step `master` of `run_tests.sh` |
| one frame file with N and *s* on the chip, and its figures | `tests/proof_run_main.cpp` (`proof_run`) |
| a guest with advice and stores; segment 60 in every fixture | `tests/updater/make_div_guest.py`, `tests/updater/` |
| bugs planted in the program, each caught | `tests/fw_teeth.sh` |

### How it works

| part | what |
|---|---|
| the boot | `cc1.c`'s: every `LOAD` awaited, the compute tiles' image sent, the release; then the segment's first barrier. **The tracer tile's load is held back** |
| the page map | 128 KB in the master's scratchpad, a bit a page. The row the cursor is in is kept in DMEM, written back by a `WRITE` to the tile itself, the next fetched with the read unit. Page 0 is never given out |
| the frame loop | at a background packet's service flag, the header word into DMEM; then by type: the parser (1, 3, 4, and 8's counts), the Updater's segment queue (2, 5, 6, 9), the staging area or scrap (7, 8) |
| the Updater | block 2, `START`, `done`; a packet goes to the segment queue only when the allowance covers it; exactly `PAGE_COUNT` pages are queued for a run, from the map; every root is a page from the map, made by the Updater (`RUN_CONFIG.root`) |
| the hand-over | at the done of segment *s*'s run, with the third run started: the tracer tile's load as one transfer, record *s* (word 66 completed), record *s* + 1, advice log *s* and five header words in front of its image, the load's `COUNT` last |
| the segment | the second barrier, the 65 results, the third run's done, the exit |
| errors | an error bit is the NMI: the handler writes both peripherals' `ERRORS`, the tile's and the error lines into the results, and exits |

**The smallest change to `tracer_ctl.c` and `tracer_layout.h` is none:** the tracer tile sits in its boot ROM
until the master answers its `LOAD`, so the master sends the segment in the same transfer as the image, in front
of it, and the program finds its layout as it always did.

### The checks and their results

| check | result |
|---|---|
| **`test_master`, 20 tests** (clang and gcc, release; the suite build) | **pass** |
| the four guests from packets, N 64, segments of 50 rows: kin (keccak256 with inputs) raw at *s* = 0, 20, 60, and as two windows; kin LRU on a base table at 20, 60, 63 (the last segment); jumps on a base table at 0; divs (advice and a page written in every segment) at 0, 20, and as two windows | each: the master's status ok; every image as its run ended **equal to gold's** (program, table, cycle-0, start, the Tracer's copy, final) and again at the end; `PAGE_COUNT` 0 at every done; no error bit; the page map marks page 0 and exactly the files' pages; the record, the expected end state, the advice and the header words in the tracer tile as the frames have them; the tracer tile's status 1, its advice used up, **its last record equal to record *s* + 1**; tile 0 replayed its 50 rows |
| the link as fast as the master takes it; and with a slow memory side (the Updater waiting 3,000 cycles on each page while a frame of 334 words arrives for its queue of 256) | pass: the allowance holds the frame back |
| the error path: a bit of a packet changed (`crc_error`); a packet left out (`sequence_error`); a page count one too small (the Updater's `out_of_pages`) | each ends in the master's handler with the bit in its results, the tile's `ERRORS` showing that peripheral alone |
| a file that ends inside a packet | no error bit; the master waits |
| the checks' teeth, three runs that must fail: a page count one too large; another image's digest as the reference; another *s* than the reference's | each caught, by the checks meant for it (`PAGE_COUNT` 1 at the done and a page left in the queue and marked in no file; the image; the start image and the record) |
| **`tests/fw_teeth.sh`**, 12 bugs planted in `master.c` and `host.h` | **every one caught** (half the Tracer's copy's pages; segment *s*'s run without `continue`; the cycle-0 image from an empty root; word 66 without the table's root; the advice count one short; record *s* + 1 for record *s*; the map's last row not written; page 0 not marked; the tracer tile's result unanswered; the pump without the allowance; the segment queue's part; the service flag's bit) |
| `./run_tests.sh quick --check`; `fw/build.sh --check` (18 images); `fw/check_images.sh`; `tools/doccheck.sh` | pass; no golden changed (`run_chip`'s lines for v1.36's segments are as they were) |

`all --check` and `make check` for this stage go to the runner with Stage 3's tasks.

### Already seen, ahead of Stage 3

`proof_run` on gold's frames of the real inputs, made here (the recorded windows are not in this session, so
without the row-by-row checks; with every check above):

| frames | N | *s* | windows | result | cycles | the master's core awake |
|---|---|---|---|---|---|---|
| EVM Zone 23, LRU, base | 64 | 0, 1, 6, 11 | 8 | pass, each | 5.8 M | 31% |
| Cyclone 22, raw, no base | 64 | 0, 1, 3, 5 | 8 | pass, each | | |
| EVM Zone 23, raw, no base | 128 | 0 | 64 | pass | | |
| EVM Zone 23, LRU, base | 128 | 1 | 64 | pass | 26.0 M | 7.3% |
| Cyclone 22, LRU, base | 256 | 0 | 64 | pass | | |
| **EVM Zone 2²⁹**, base, each frame in its smaller form | 128 | 0, 42, 84 | 64 | **pass, each** | 29.8 M at 42 | 9.2% |

Each in under half a minute here. The figures are Stage 3's to report in full.

### What the frames and the packets do not carry (so far)

- **N and *s*** (by design): two words placed with the master's image.
- **A segment's rows:** the windows a segment has and the rows of a window are the tracer tile's header's
  (`TL_H_WINDOWS`, `TL_H_RUN_CONFIG`), placed by the harness; gold's `--segment-rows` must match (Q3).
- **The tables:** the decode table and the ROM, the template index and the templates, in the tracer tile's and
  the compute tiles' scratchpads, placed by the harness as today.
- **The Tracer's and the replayer's `RUN_CONFIG`** (which streams are LRU-coded, the store log): the harness's.
- **The firmware images** of the tracer tile and the compute tiles: in the master's scratchpad, placed.
- **Where things go in the tracer tile** (its layout's addresses): told to the master in `boot_desc`.

### For the documents (added to Stage 1's list)

- **HF, frame 7:** "word 66, the rest 0: the roots and depths, which C&C 3 fills". In v1.37 the master fills
  them, since it made the files; worth a word on who does in v1.4.
- **HF, frame 8:** the counts are N words of 32 bits, so the logs start on a 128-bit word only because N is a
  multiple of 4. True for 64, 128 and 256; worth saying.
- **II:** the document says "a frame ends at the end of a packet"; it could say that the master may check it (a
  frame's header word must be a packet's first word), which this firmware does.
- **UI:** with `PAGE_COUNT` exact and pages queued only for runs that have started, the free page queue is empty
  at every done. The document allows pages to be left; nothing here needs it.

