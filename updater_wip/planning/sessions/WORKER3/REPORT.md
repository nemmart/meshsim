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

- **The steps on recorded windows, and `make check`.** The small inputs' build fetches a dependency of the
  tracer from GitHub's archive, which this session's network refuses; and `make check` (four builds and TSan) is
  too much for two CPUs. Both go to the runner: **Quest 202** (the small inputs built, then `all --check`) and
  **Quest 203** (`make check`), made by `updater_wip/tasks/v137-check/make_task.sh`. Their results are added
  here when they are in. The new tests were also built and run here with gcc, debug and release.
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
