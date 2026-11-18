# Running v1.37: the whole chip from a packet file

*For anyone who wants to run v1.37's own thing: the simulator started with one packet file, N and s, and nothing
else of the proof. What to make first, the commands, what a run prints and what it is checked against.* Building is
`BUILDING.md`; v1.36's suites and tools are `RUNNING.md`; what v1.37 adds, piece by piece, is `CHANGES-v1.37.md`.
Commands run from this folder.

## What a run is

| | |
|---|---|
| the input | **a packet file:** gold's frame file (`HOST-FRAMES.md`) wrapped in the link's packets (`HOST-LINK.md`), read by Ingress at a byte a cycle (or 2, 4, 8: `--link-rate`) into a queue of 1,024 words a stream (16 KiB, 17.6.3; `INGRESS_QUEUE_WORDS`, the chip's setting `ingressQueueWords`), a link packet's words landing a word a cycle. **N and s:** how many ASICs the proof is cut for, and which segment this one runs. No frame carries them |
| the master tile | runs `fw/cc/master.c`: reads the frames out of Ingress, keeps the page map, drives the Updater through its runs, and hands the tracer tile segment s. **The forward count** (`INGRESS-INTERFACE.md`; WORKER7): it copies a frame's header word by Ingress's DMA, and for a frame of update words starts the Updater's run and writes the frame's length to `DMA_FORWARD_COUNT` once: Ingress sends the words to the Updater's word port itself. Frames 7 and 8 are dropped up to the ASIC's own record or log, which is copied, and the rest dropped |
| Ingress's host side | Ingress/Egress at 17.6's registers; its link, until stage 4, a software black box reading the packet file (`src/units/ingress_egress.h`, THE LINK), and Egress's link packets going nowhere |
| the Updater | its controller, the state machine (spec 17.10), on the memory system's client port 1 and look-up port 1 |
| the memory tile | its peripheral 1 the memory system (the cache's controller, the AXI arbiter, the LPDDR model: spec 17.11, 23), peripheral 2 the RW engine on the arbiter's client 2; nothing runs on it in a proof |
| the Updater | builds the memory images in LPDDR: the program image, the jump table, the cycle-0 image, the start image of segment s, the Tracer's copy, the final image |
| the tracer tile, the compute tiles | v1.36's, unchanged: the Tracer traces segment s on the Tracer's copy, window by window; each window is delivered to its tile and replayed |
| what is still placed by the harness | the firmware images; the tracer tile's and the tiles' layouts and tables (the decode table, the ROM, the templates); the Tracer's and the replayer's `RUN_CONFIG`; the windows a segment has and their rows |

## One run: `proof_run`

```
build/clang-suite/proof_run FRAMES --asics N --segment S [--windows W] [--rows R] [--gold IMAGES.txt]
                            [--packets FILE] [--recorded RUN] [--fast-link] [--link-rate B]
                            [--lpddr-seed S] [--limit CYCLES] [--quiet]
```

| argument | |
|---|---|
| `FRAMES` | gold's frame file. The expectations are read from it; the chip gets its packets |
| `--asics N`, `--segment S` | the system's configuration: N is 64, 128 or 256 and must be what gold cut the proof for |
| `--windows W`, `--rows R` | a segment as the tiles run it: W windows of R rows (default 64 and 65,536). Their product must be gold's `--segment-rows` |
| `--packets FILE` | gold's packet file of the same frames (`gold packets`). Without it the tests' writer makes the packets: the same bytes |
| `--gold IMAGES.txt` | gold's reference for this s (`gold frames-images FRAMES S`). Without it the images are not checked, and the run fails for that |
| `--recorded RUN` | `evm-zone-23` or `cyclone-22`: every row, record and stream of windows 64 S to 64 S + 63 against the recorded files of `MESHSIM_INPUTS`, as `segment-real` |
| `--fast-link` | the link's bytes as fast as the master takes them, in place of a byte a cycle |
| `--link-rate B` | the link at B bytes a cycle: 1 (the default, HL's 1 GB a second at 1 GHz), 2, 4, 8 |
| `--lpddr-seed S` | the LPDDR model's jitter mode with seed S (0, the default: its timing fixed) |

It prints `run_chip`'s lines (`segment_bench.h`) and the proof's (`proof_bench.h`), then each of the Updater's runs
(the cycle of its done, its cycles busy, of them waiting on data, on pages and on memory, the pages and half pages it
read and wrote, `PAGE_COUNT` at its done), the link's packets, the pages the page map marks, and the cycles the
master's core was awake; then the link's rate, the master's wakes and Ingress's DMA transfers in all and to the
hand-over, the count's stores, the words forwarded and dropped, its cycles awake to the hand-over and their share of
the hand-over's cycle, the cycle of the link's last byte and the cycles it waited for room in Ingress, its cycles
awake at the first frame (the boot), and each frame's wakes and transfers. It exits 1 on any failed check.

**The checks** (`tests/proof_bench.h` has the list): the master's status and every request answered; `PAGE_COUNT` 0
at every done; no error bit anywhere; the link read to its end; every image as its run ended against gold's, and
the frozen ones again at the end; the page map against the files' pages; what was handed to the tracer tile against
the frames; the Tracer's last record against record s + 1; and `run_chip`'s own: the tracer tile's and every compute
tile's results, each replayer's end state against the next window's start, and with `--recorded` every row and
delivery.

## The real workloads

```
# once: the workloads' sources and, for --recorded, their recorded windows (pipeline/inputs/README.md)
# then the host's side, about three minutes:
OUT=~/meshsim-inputs JAP_DIR=~/meshsim-inputs/src/jolt-asic-planning bash pipeline/frames/build.sh matrix real 29
MESHSIM_INPUTS=~/meshsim-inputs ./run_tests.sh all --check
python3 tests/proof_numbers.py results/proof-real.log results/proof-matrix.log results/proof-29.log
```

`pipeline/frames/README.md` says what `build.sh` makes: for each run a frame file, a packet file and gold's
reference for each segment to run, with `RUNS` listing them. The steps `proof-real`, `proof-matrix` and `proof-29`
of `run_tests.sh` run `proof_run` on every line of a set's `RUNS` (`RUNNING.md` has their table);
`tests/proof_numbers.py` makes tables of their figures (`--frames`: with each frame's wakes).

One run by hand, EVM Zone at 2^29, segment 42:

```
D=~/meshsim-inputs/v137/29
build/clang-suite/proof_run $D/evm-zone-29.frames --packets $D/evm-zone-29.packets --asics 128 --segment 42 \
    --gold $D/evm-zone-29-s42.images.txt
```

About 30 million cycles, half a minute.

## The guests, without any inputs

`test_master` (the step `master`) needs nothing outside the repository: gold's frame files of five small guests are
committed (`tests/updater/`, with `make_fixtures.sh`, which makes them again where there is a JDK), cut for N 64 into
segments of 50 rows. It runs them at several s, then the error path (a damaged packet, a packet left out, a page count
too small: the master's NMI, its results, its exit), a file cut inside a packet, and three runs that must fail; and
the forward count (WORKER7): `kin-big`'s data image of 1,584 words, three times Ingress's queue; `divs` at s 60, whose
log is empty after logs that are not; every guest with the timing varied (the link from 4 cycles a byte to as fast
as taken, the Updater's memory side fast and slow); frames ending inside packets; links of 2, 4 and 8 bytes a cycle.

```
build/clang-suite/test_master                      # all of it, 20 seconds
build/clang-suite/test_master master_divs          # the guest with advice and stores
build/clang-suite/proof_run tests/updater/divs.frames --asics 64 --segment 20 --windows 1 --rows 50 \
    --gold tests/updater/divs-s20.images.txt
```

## The pieces alone

| | command |
|---|---|
| a frame file on the Updater alone, no firmware, against gold's image files | `build/clang-suite/updater_frames FRAMES --asics N --segment S --ref PREFIX` (`gold frames-images FRAMES S --out=PREFIX` writes the reference) |
| a frame file in the link's packets, and back | `build/clang-suite/packetize FRAMES PACKETS`, `packetize --read PACKETS [FRAMES]` |
| the Updater, Ingress's host link, the file store: their tests | `build/clang-suite/test_updater`, `test_ingress_link`, `test_packet_vectors`, `test_file_store` |
| bugs planted in the master's program, each of which `test_master` must catch | `tests/fw_teeth.sh master:` (the RISC-V toolchain; three minutes) |

## The memory side

**The memory tile's peripheral 1 is the memory system** (spec 17.11, 23; v1.37 stage 1): the cache's controller, the AXI
arbiter and the LPDDR model on the chip's file store (`src/units/memory_system.h`, its hardware `src/units/cache_system.h`).
Its clients are wired as 23.10 has them: the Tracer's `fill` port; **the Updater, its controller** (`src/units/updater_top.h`,
`updater_ctl.h`), on the arbiter's client 1 and the cache's look-up port 1; the RW engine, now the memory tile's
peripheral 2 (window 0xFFFF_FC00, region 0x05), on client 2. There is no setting: it is the machine. (WORKER5's
`--cache`, `CACHE_CTL` and `MachConfig::cache_ctl`, and WORKER6's `--updater-ctl` and `MESHSIM_UPDATER_CTL`, are gone.)
The Updater's black box (`src/units/updater.*`) and v1.37's memory model (`src/units/mem_system.h`) stay only where a
test compares against them.

Every proof run prints the cache's figures (hits and misses by kind of line, evictions, write-backs, the walks, a fill's
and a look-up's cycles, LPDDR's bursts) and the clients' (the Updater's look-ups and updates, the cycles its look-up and a
fill request were both outstanding, each client's bursts at the arbiter and the cycles its pieces waited), and fails on
the cache's error flag, a read of LPDDR nothing wrote, an LPDDR error response, any of **the joint's checks**
(`cache_system.h`: an update before its page's write is answered; a direct write to a page the cache holds a line of, or
a read of one it holds a dirty line of; an update's reply with LPDDR not holding the entry; a look-up out of turn), the
Updater's bursts not on client 1 alone, or none of its requests on look-up port 1.

| | command |
|---|---|
| the LPDDR model's timing varied | `proof_run ... --lpddr-seed S`; `MESHSIM_LPDDR_SEED=S` for every chip of a process (`test_master`); `LPDDR_SEED=S ./run_tests.sh ...` (each proof run with S + its number) |
| the memory system in the chip: its window, its error on the error line, the reset, the RW engine on client 2 | `test_memory_system` (the step `memory-system`) |
| the Updater's controller alone on its test pieces, against the black box, and **joined to the memory system** (gold's fixtures, the 90 random scenarios, jittered, a second client on `fill` at once) | `test_updater_ctl` (the step `updater-ctl`); `test_updater_ctl ctl_figures_a_page` prints what a page costs by kind on the test pieces and joined |
| the units alone | `test_lpddr_model`, `test_axi_arbiter`, `test_cache` (the step `cache`) |
| the Tracer alone on the cache | `TRACER_CACHE=1 tracer_block --synthetic SEED` (the step `cache-synth`); `tracer_block ... --fill cache` on the real segments (the step `tracer-block-cache`) |
| bugs planted in the controllers and the joints, each caught | `tests/updater_ctl_teeth.sh` (WORKER6's 14); `tests/joined_teeth.sh` (the joints'; a rebuild a bug) |

## When a run fails

- **The master's status** is the first line: 1 is a proof made; 2 its error path (the line gives the tile's,
  Ingress's and the Updater's `ERRORS`); 4 a frame that is not as `HOST-FRAMES.md` says, with the frame's type and
  the check in the detail; 5 no room (`fw/cc/master_layout.h` has every code and every word of its results).
- **A run that stopped** prints where: the Updater's `WORD_COUNT` and `PAGE_COUNT`, what is in its queues, the
  packets waiting in Ingress, the words the master read out.
- **N or s that is not the frames'**: the frame lengths are checked against N (frame 7's is 68 N), so a wrong N ends
  with status 4; a wrong s builds another segment's images, which the reference catches.
