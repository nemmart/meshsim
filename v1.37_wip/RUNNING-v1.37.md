# Running v1.37: the whole chip from a packet file

*For anyone who wants to run v1.37's own thing: the simulator started with one packet file, N and s, and nothing
else of the proof. What to make first, the commands, what a run prints and what it is checked against.* Building is
`BUILDING.md`; v1.36's suites and tools are `RUNNING.md`; what v1.37 adds, piece by piece, is `CHANGES-v1.37.md`.
Commands run from this folder.

## What a run is

| | |
|---|---|
| the input | **a packet file:** gold's frame file (`HOST-FRAMES.md`) wrapped in the link's packets (`HOST-LINK.md`), read by Ingress at a byte a cycle. **N and s:** how many ASICs the proof is cut for, and which segment this one runs. No frame carries them |
| the master tile | runs `fw/cc/master.c`: reads the frames out of Ingress, keeps the page map, drives the Updater through its runs, and hands the tracer tile segment s |
| Ingress's host side, the Updater | software black boxes behind their real registers (`INGRESS-INTERFACE.md`, `UPDATER-INTERFACE.md`) |
| the Updater | builds the memory images in LPDDR: the program image, the jump table, the cycle-0 image, the start image of segment s, the Tracer's copy, the final image |
| the tracer tile, the compute tiles | v1.36's, unchanged: the Tracer traces segment s on the Tracer's copy, window by window; each window is delivered to its tile and replayed |
| what is still placed by the harness | the firmware images; the tracer tile's and the tiles' layouts and tables (the decode table, the ROM, the templates); the Tracer's and the replayer's `RUN_CONFIG`; the windows a segment has and their rows |

## One run: `proof_run`

```
build/clang-suite/proof_run FRAMES --asics N --segment S [--windows W] [--rows R] [--gold IMAGES.txt]
                            [--packets FILE] [--recorded RUN] [--fast-link] [--limit CYCLES] [--quiet]
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

It prints `run_chip`'s lines (`segment_bench.h`) and the proof's (`proof_bench.h`), then each of the Updater's runs
(the cycle of its done, its cycles busy, of them waiting on data, on pages and on memory, the pages and half pages it
read and wrote, `PAGE_COUNT` at its done), the link's packets, the pages the page map marks, and the cycles the
master's core was awake. It exits 1 on any failed check.

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
`tests/proof_numbers.py` makes one table of their figures.

One run by hand, EVM Zone at 2^29, segment 42:

```
D=~/meshsim-inputs/v137/29
build/clang-suite/proof_run $D/evm-zone-29.frames --packets $D/evm-zone-29.packets --asics 128 --segment 42 \
    --gold $D/evm-zone-29-s42.images.txt
```

About 30 million cycles, half a minute.

## The guests, without any inputs

`test_master` (the step `master`) needs nothing outside the repository: gold's frame files of four small guests are
committed (`tests/updater/`, with `make_fixtures.sh`, which makes them again where there is a JDK), cut for N 64 into
segments of 50 rows. It runs them at several s, then the error path (a damaged packet, a packet left out, a page count
too small: the master's NMI, its results, its exit), a file cut inside a packet, and three runs that must fail.

```
build/clang-suite/test_master                      # all of it, 10 seconds
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

## When a run fails

- **The master's status** is the first line: 1 is a proof made; 2 its error path (the line gives the tile's,
  Ingress's and the Updater's `ERRORS`); 4 a frame that is not as `HOST-FRAMES.md` says, with the frame's type and
  the check in the detail; 5 no room (`fw/cc/master_layout.h` has every code and every word of its results).
- **A run that stopped** prints where: the Updater's `WORD_COUNT` and `PAGE_COUNT`, what is in its queues, the
  packets waiting in Ingress, the words the master read out.
- **N or s that is not the frames'**: the frame lengths are checked against N (frame 7's is 68 N), so a wrong N ends
  with status 4; a wrong s builds another segment's images, which the reference catches.
