# pipeline/frames: the host's side of v1.37's proof steps

*What the simulator is started with in v1.37: one packet file, with N and s. This folder makes the packet files of
the real workloads, and gold's references for them.*

In v1.37 the chip gets a proof as the host's frames over the link (`../../CHANGES-v1.37.md`). `build.sh` makes, for
each run the proof steps of `../../run_tests.sh` use (`proof-real`, `proof-matrix`, `proof-29`):

| file | what | made by |
|---|---|---|
| `NAME.frames` | gold's frame file: the program image, the jump table, the data image, the register states, the advice logs and the memory segments | `gold frames`, with `--verify` |
| `NAME.packets` | the frame file in the link's packets: what Ingress reads | `gold packets`; read back with `gold packets-read --compare`; and, where the tests' writer is built (`build/clang-suite/packetize`), required equal to its output |
| `NAME-sS.images.txt` | gold's reference for segment S: the SHA-256 of the program image, the jump table, the cycle-0 image, the start image, the Tracer's copy and the final image | `gold frames-images` |
| `RUNS` | a line a run: its name, N, the windows a segment has, the segments to run | |
| `SHA256SUMS` | every file's SHA-256 | |

Gold here is the frames' gold, `../../../updater_wip/pipeline/gold` (HOST-FRAMES.md's and HOST-LINK.md's), not
`../gold`, which is v1.36's. The workloads are `../inputs/pins.sh`'s, from jolt-asic-planning at its pin.

| set | runs | segments |
|---|---|---|
| `matrix` | EVM Zone trace-23 and Cyclone input/22, N 64, segments of 8 windows; each four ways: update words raw or LRU-coded, a full new jump table or one added to a static base table (8 frame files) | 0, 1 and the middle one: 24 runs |
| `real` | the same inputs as the chip's tests run them, N 128, segments of 64 windows, a static base table, each frame in its smaller form | EVM Zone's 0 and 1, Cyclone's 0: the segments whose windows are recorded (`../inputs`) |
| `29` | EVM Zone trace-29 and Cyclone input/29, likewise | 0, 1, the middle one, and the last two with rows: 10 runs |

```
OUT=~/meshsim-inputs JAP_DIR=/path/to/jolt-asic-planning bash pipeline/frames/build.sh matrix real 29
MESHSIM_INPUTS=~/meshsim-inputs ./run_tests.sh all --check
```

The files go to `OUT/v137/SET/`. All three sets take about three minutes: gold is an emulator, and a frame file is a
few megabytes even at 2^29 (EVM Zone 4.2 MB, Cyclone 16.4 MB). `build.sh`'s header has its environment
(`GOLD_CLASSES` for a machine without javac). No frame carries how many rows a segment has: `build.sh` cuts with
gold's `--segment-rows`, and `RUNS` gives the same figure to the chip's harness.
