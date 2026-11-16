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

## Stage 3: the whole chip from a packet file, on the real workloads (2026-11-16)

**Done.** Given a packet file, N and s, the chip builds the images and traces and replays segment s, on both
workloads, small and at 2²⁹. Every image equals gold's; `PAGE_COUNT` is 0 at every done; no error bit is set; the
Tracer's last record equals record s + 1.

### What was built

| | where |
|---|---|
| the host's side: for each run gold's frame file, gold's packet file, gold's reference images for each segment to run | `pipeline/frames/build.sh`, its `README.md` |
| the steps `proof-real`, `proof-matrix`, `proof-29` | `run_tests.sh` |
| the runs' figures as one table | `tests/proof_numbers.py` |
| the running guide; v1.37 in the README and the building and running guides | `RUNNING-v1.37.md`, `README.md`, `RUNNING.md`, `BUILDING.md`, `CHANGES-v1.37.md` |
| the Quest tasks' generator, with the proof steps | `updater_wip/tasks/v137-check/` |

The packet files are gold's (`gold packets`, WORKER2's, on main since `29e6322b`), read back by gold, and each also
required equal, byte for byte, to the tests' own writer's output.

### The checks and their results

| check | where it ran | result |
|---|---|---|
| **`proof-matrix`, 24 runs:** EVM Zone trace-23 and Cyclone input/22, N 64, segments of 8 windows, each with its update words raw and LRU-coded, with a full new table and one added to a static base table, at s = 0, 1 and the middle (6 and 3) | here; the runner (Quest 204) | **24 of 24 pass**, both places, with the same cycles |
| **`proof-29`, 10 runs:** EVM Zone trace-29 (85 segments with rows) at s = 0, 1, 42, 83, 84; Cyclone input/29 (89) at s = 0, 1, 44, 87, 88; N 128, segments of 64 windows | here; the runner (Quest 204) | **10 of 10 pass**, both places, with the same cycles |
| **`proof-real`, 3 runs:** `segment-real`'s three segments (EVM Zone 23's s = 0 and 1, Cyclone 22's 0; N 128, 64 windows) from packet files, with everything `segment-real` checks: every row out of every tile's row port, every window's record and streams as delivered, against the recorded files | the runner (Quest 204): the recorded files can't be built in this session | **3 of 3 pass:** from a packet file the chip passes what `segment-real` passes |
| the same three without the recorded files' checks | here | pass |
| in every run above: the master's status; every image as its run ended against gold's (program, table, cycle-0, start, the Tracer's copy, final), and the frozen ones again at the end; `PAGE_COUNT` 0 at every done; no error bit; the link read to its end; the page map; the hand-over; the Tracer's last record against record s + 1; the tracer tile's and every tile's results; each replayer's end state against the next window's start | | |
| `./run_tests.sh all --check`, every v1.36 step with them | here (3 steps SKIPPED for the recorded files); the runner (Quest 204) | **pass**, both places. On the runner every step ran but `fw-build` (no xPack toolchain there; `fw/build.sh --check` passes here: 18 images) |
| `make check` | the runner (QUEST205) | QUEST205CHECK |
| `test_master`, `test_updater`, `test_ingress_link` in the debug build (ASan, UBSan), and with gcc | here | pass |
| Ingress against HOST-LINK.md and INGRESS-INTERFACE.md as main has them, and against gold's vectors on main | here | the documents have not changed since `8385dacd`, which Ingress was built to; `test_ingress_link` now has HL's own figures (its table's CRC values, its example packet byte for byte, its cross-check 0x48674BC7): **33 pass**; `test_packet_vectors` reads main's `test/packets/`: the 8 CRC values, 43 packets, 5 files and 32 damaged files **all agree** |

### The figures

Cycles are the simulator's, with the black boxes' timing (`NOTES.md`, section 6): the link at a byte a cycle, a
look-up 6 cycles, a page read or written 336, a half page 208, an index entry 80. They are not the RTL's.

**At 2²⁹, N 128, a static base table, each frame in its smaller form** (here; the runner's are the same cycles):

| run | s | cycles to the exit | link, bytes | pages marked | Updater busy | waiting on data | on pages | on memory | pages read | written | master awake | its share |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| EVM Zone 2²⁹ | 0 | 26,784,256 | 4,366,800 | 966 | 4,498,824 | 2,570,711 | 1,987 | 1,218,328 | 1,741 | 2,585 | 2,759,842 | 10.3% |
| | 1 | 28,979,200 | | 1,018 | 4,517,981 | 2,575,390 | 0 | 1,234,792 | 1,742 | 2,613 | 2,761,594 | 9.5% |
| | 42 | 29,831,680 | | 981 | 4,502,348 | 2,577,357 | 0 | 1,217,192 | 1,757 | 2,551 | 2,753,128 | 9.2% |
| | 83 | 30,551,552 | | 981 | 4,500,560 | 2,574,931 | 638 | 1,217,192 | 1,757 | 2,551 | 2,749,854 | 9.0% |
| | 84 | 28,864,512 | | 1,030 | 4,518,376 | 2,577,450 | 0 | 1,233,128 | 1,719 | 2,600 | 2,754,102 | 9.5% |
| Cyclone 2²⁹ | 0 | 32,100,352 | 16,895,744 | 4,048 | 17,350,692 | 10,485,522 | 0 | 3,751,668 | 3,496 | 8,151 | 10,467,576 | 32.6% |
| | 1 | 31,216,640 | | 4,100 | 17,278,434 | 10,458,703 | 0 | 3,706,228 | 3,560 | 7,939 | 10,445,548 | 33.5% |
| | 44 | 42,289,152 | | 3,918 | 17,304,467 | 10,507,920 | 0 | 3,683,044 | 3,504 | 7,940 | 10,394,901 | 24.6% |
| | 87 | 40,147,456 | | 3,827 | 17,274,458 | 10,503,503 | 856 | 3,656,596 | 3,500 | 7,906 | 10,384,354 | 25.9% |
| | 88 | 39,985,152 | | 3,832 | 17,275,617 | 10,503,503 | 0 | 3,658,612 | 3,493 | 7,914 | 10,385,063 | 26.0% |

"Pages read" and "written" count pages and half pages, as the Updater's statistics do. The full table of all 37
runs is the runner's `proof-numbers.md` (Quest 204, `results/204-v137-proof/`).

**The three recorded segments** (the runner; N 128, 64 windows, a static base table):

| run | s | cycles to the exit | link, bytes | pages marked | Updater busy | waiting on data | on pages | on memory | pages read | written | master awake | its share |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| EVM Zone 23 | 0 | 26,824,192 | 2,828,672 | 962 | 2,770,509 | 1,910,027 | 2,473 | 428,040 | 51 | 986 | 1,888,873 | 7.0% |
| | 1 | 26,035,712 | | 1,039 | 2,799,034 | 1,916,545 | 0 | 452,520 | 36 | 1,038 | 1,893,918 | 7.3% |
| Cyclone 22 | 0 | 28,297,728 | 4,022,560 | 1,406 | 4,076,115 | 2,850,315 | 0 | 599,844 | 8 | 1,405 | 2,727,654 | 9.6% |

**The matrix** (segments of 8 windows; the range over s = 0, 1 and the middle):

| run | link, bytes | cycles to the exit | Updater busy | master awake |
|---|---|---|---|---|
| EVM Zone 23, raw, full table | 3,720,864 | 6.46 to 6.89 M | 3.70 M | 2.38 M, 35 to 37% |
| EVM Zone 23, raw, on a base | 3,729,520 | 6.47 to 6.90 M | 3.71 M | 2.38 M |
| EVM Zone 23, LRU, full table | 2,788,112 | 5.83 to 6.14 M | 2.82 M | 1.84 M, 30 to 32% |
| EVM Zone 23, LRU, on a base | 2,792,272 | 5.84 to 6.15 M | 2.82 M | 1.84 M |
| Cyclone 22, raw, full table | 5,293,296 | 8.06 to 9.74 M | 5.32 M | 3.43 M, 35 to 43% |
| Cyclone 22, raw, on a base | 5,293,456 | 8.06 to 9.74 M | 5.32 M | 3.43 M |
| Cyclone 22, LRU, full table | 3,967,440 | 7.26 to 8.54 M | 4.07 to 4.12 M | 2.66 M, 31 to 37% |
| Cyclone 22, LRU, on a base | 3,967,600 | 7.26 to 8.54 M | 4.07 to 4.13 M | 2.66 M |

LRU coding takes a quarter off the link's bytes on both workloads, and the run is shorter by as much as the link
is. On these inputs a base table changes almost nothing: the table is a small part of the file.

**On the runner** (Quest 204, 16 threads): the host's side made in 99 s (the three sets; 64 files, byte-identical
to those made here); `proof-real` 47 s, `proof-matrix` 118 s, `proof-29` 216 s; `all --check` 571 s with them. A
2²⁹ run simulates at about 1.8 M cycles a second there, 1.0 M here.

**One proof, run by run** (Cyclone 2²⁹, s = 44):

| the Updater's run | done at cycle | cycles busy | waiting on data | on memory | read | written | `PAGE_COUNT` at done |
|---|---|---|---|---|---|---|---|
| program image | 3,449,891 | 3,346,925 | 2,586,648 | 346,576 | 0 | 810 | 0 |
| base table | 3,897,398 | 446,250 | 206,806 | 108,368 | 0 | 257 | 0 |
| jump table (added to the base) | 3,925,403 | 674 | 0 | 672 | 1 | 1 | 0 |
| data image | 6,485,104 | 2,559,270 | 1,893,356 | 305,384 | 1 | 713 | 0 |
| memory segments before s | 16,784,053 | 9,352,533 | 5,786,097 | 1,683,376 | 704 | 4,057 | 0 |
| segment s, with the Tracer's copy | 16,858,026 | 73,657 | 401 | 64,344 | 54 | 110 | 0 |
| memory segments after s | 18,383,321 | 1,525,158 | 34,612 | 1,174,324 | 2,744 | 1,992 | 0 |

The tracer tile's load starts in the cycle after segment s's done; tracing and the third run then go on together.
For EVM Zone 2²⁹ at s = 42 the load starts at cycle 3,917,134, the last window has landed at 29,577,348, and the
master exits at 29,831,384.

**What the figures say:**

- **The link sets the pace of the images.** At a byte a cycle a packet of 32 words takes 528 cycles, and the
  Updater spends most of its busy cycles waiting for data (57 to 60% at 2²⁹): everything before the hand-over
  takes about one cycle a byte of the packet file, 3.9 M cycles for EVM Zone 2²⁹ and 16.9 M for Cyclone.
- **The Updater never waits long for pages:** at most 1,987 cycles in a run. The master queues a run's pages at
  its `START` and the first ones arrive a few hundred cycles later.
- **The firmware's share** is the master's core awake for 2.75 M cycles on EVM Zone 2²⁹ and 10.4 M on Cyclone:
  9 to 10% and 25 to 34% of the run. It is all before the hand-over and in step with the link: about 330 cycles
  awake a packet (about 220 instructions: the service flag, the transfer's start, its two ends, a credit now and
  then), against the packet's 528 cycles on the link. So the firmware as written could take a link about 1.6
  times faster and no more; it has not been tuned.
- **A proof's pages:** 966 to 1,030 for EVM Zone 2²⁹ and 3,827 to 4,100 for Cyclone, with the roots and page 0,
  of LPDDR's 2²⁰. The page map marks exactly the pages the files hold, and page 0.
- **Tracing dominates the run's length:** a segment of 2²² rows takes about 24 to 26 M cycles from the hand-over
  to the last window's landing.

### What the frames and the packets do not carry

Stage 2's list stands. Complete:

| not carried | where it comes from in v1.37 |
|---|---|
| N and s | two words placed with the master's image (`system_config`); by design |
| the rows of a segment and of a window | the tracer tile's header (`TL_H_WINDOWS`, `TL_H_RUN_CONFIG`) and the replay layout's, placed by the harness; gold's `--segment-rows` must be their product. Needed in a frame only if segments are ever cut elsewhere than every 2²² rows (Q3) |
| the decode table and the ROM, the template index and the templates | placed in the tracer tile's and the compute tiles' scratchpads |
| the Tracer's and the replayer's `RUN_CONFIG` bits (which streams are LRU-coded; the store log) | the layouts' headers, placed |
| the tracer tile's and the compute tiles' firmware | placed in the master's scratchpad; the master loads them |
| where the record, the expected end state and the advice go in the tracer tile | told to the master in `boot_desc` |
| the end state of the last segment | none: record N does not exist, so the last ASIC's tracer tile has nothing to check its last record against |
| names: the program's stem, the working directory, the tables' paths | carried, and skipped: v1.37 has no file system. "The base table" is the last one a frame 4 with a path established |
| anything on the foreground stream | v1.37's packet files are background only |

### What to change in the documents

Collected from the three stages.

**HOST-FRAMES.md**
1. Frame 7, word 66: say who fills the roots and depths. In v1.37 it is the master tile, which made the files:
   the Tracer's copy's root and depth, the jump table's root and depth.
2. Frame 8: the counts are N 32-bit words, so the logs start on a 128-bit word only because N is a multiple of 4.
   Say so, or pad the counts.
3. "Frame 3, per ASIC": the Tracer's copy's count is the pages of the copy alone. The run that makes it takes
   twice that from the free page queue (a page for the final image and a page for the copy), and the final image's
   run takes the final count less the copy's. The firmware had to derive both; a line in the document would save
   the next reader the derivation. Gold's counts are exact for this: `PAGE_COUNT` is 0 at every done, in every run.
4. A segment's rows are in no frame (Q3): state that a segment is 2²² rows, 64 windows of 65,536, by definition, or
   add the figure to frame 3.
5. Frame 4: "a run's base is always such a table" names the base by path. Without a file system the only base is
   the last one established; with one, the firmware needs the names. Worth a sentence on which is meant for v1.4.

**HOST-LINK.md**
1. Nothing wrong was found: the CRC-32C, the byte table, the order of the tests, the length-0 rule and the idle
   zeros are exact enough to build from, and the example and the cross-check came out as written.
2. "No packet number is 0 or all ones": the receiver's check is only "the next number of its stream", so 0 and
   0xFFFFF fail as `sequence_error`. Say that this is the intended error for them.

**INGRESS-INTERFACE.md**
1. A file or link that goes quiet inside a packet sets nothing: say that silence is not an error and has no
   register (the black box's `link_cut` is the harness's).
2. "A frame ends at the end of a packet" can be checked by the master (a frame's header word must be a packet's
   first word); this firmware does, and ends the run if not. Say whether that is wanted.
3. The queues' depth: v1.37's black box holds four packets a stream before the link waits. The document leaves it
   open; the real figure decides when "stop" is sent.
4. The firmware's cost a packet (about 330 cycles awake here) bounds the link's rate if every packet needs the
   core. If the link is to run faster than about 1.5 bytes a cycle, the interface needs a way to send a whole
   frame's packets to the Updater without a wake for each: a count of words, not of one packet's.

**UPDATER-INTERFACE.md**
1. What a run does after an error is not said. The black box stops: `busy` stays 1 and no `done` is posted; the
   firmware learns of it by the NMI.
2. A free page queue entry: "one free page, in [31:0]" does not give the unit. Taken: the page's address in
   128-byte units, as a root.
3. `PAGE_COUNT` exact and pages queued only for runs that have started leave the free page queue empty at every
   done. The document allows pages left over; nothing needed it.
4. "For v1.37: making a root": used as written (`RUN_CONFIG.root`, `SOURCE_ROOT`), for all seven files. The copy
   of a root is what freezes a file: nothing else was needed for the start image.
5. The third run of the memory segments (†) was worth having: the tracer tile is loaded at the second run's done
   and tracing overlaps the third run entirely.
6. The black box's choices that the RTL will have to make its own: the order within a page (the Tracer's copy
   takes its page first), one page held across runs of values, a look-up before each page.

### At the freeze

- **The design documents have to be brought into v1.37's own spec** (the owner, 2026-11-16). v1.37's additions
  are specified by `INGRESS-INTERFACE.md`, `UPDATER-INTERFACE.md`, `HOST-FRAMES.md` and `HOST-LINK.md` in
  `v1.4_wip/planning/`, outside the version's folder, and `spec/` is still v1.36's. Until then `tools/doccheck.sh`
  takes a document of that folder cited by its name as there; that allowance goes when they move.
- `fw/include/host.h` is written by hand; with the registers in the specification's table it would be generated,
  as `mesh.h` is.
- The README's top says v1.37; the page below its line is v1.36's and was not rewritten.

### What is open

- **The black boxes' timing is this session's,** not the RTL's: the Updater's costs, the link's rate, Ingress's
  four packets a stream.
- **The master's program is not tuned** (above), and its results' cycle stamps are 32 bits.
- **One proof a run.** The program exits after frame 9; a second proof, or a base table kept for the next one, is
  not there.
- **Nothing is freed:** the page map only fills.
- **The foreground stream** is modelled in Ingress and unused by the firmware.

