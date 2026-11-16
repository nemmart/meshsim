# Running v1.36

*For anyone running v1.36's suites, kits and tools: each one's command, how long it takes, what it writes, and what it
is checked against.* Building is `BUILDING.md`; the commands run from this folder.

## The suites: `./run_tests.sh`

`./run_tests.sh [all|quick] [--check] [--no-build]` builds the suites (the suite build, `build/clang-suite/`; `bus`
uses the debug build), runs every step in order and prints a summary; each step writes `results/<step>.log`. The
runner also writes `results/.build.log` and `results/.build-bus.log` (the builds), `results/.differs` (`--check`'s
differences) and, with `STEP_TIMES=1`, `results/.step-times`. `quick` leaves out the steps marked *all* below.
`CC_NAME=gcc` uses gcc; `JOBS=N` builds with N jobs.

**A step passes or fails in one of two ways.** A *test* step runs tests whose expectations are in the tests, from the
specification or the RTL: a failing test, or a program that stops, fails the run. A *log* step runs a suite whose log
is compared with its reference log under `--check`, its timings masked (cycles are never masked); a difference fails
the run. The run exits 0 only when every step passed.

**Two things are outside the repository.** A step that needs one is not run when it is not on the machine; the runner
prints it as `SKIPPED (<why>)`, lists the skipped steps again in its summary, and ends with `all passed, N steps
SKIPPED for what is not on this machine (data, a toolchain)`. With both it ends with `all passed`.
- `MESHSIM_INPUTS` (default `~/meshsim-inputs`): the real workloads' recorded data (EVM Zone, Cyclone), for
  `tracer-block` and `segment-real`.
- The RISC-V toolchain the firmware images were built with (`BUILDING.md`; `RISCV_PREFIX` names it), for `fw-build`.
  With it, `RISCV_TESTS` and `COREMARK` name the checkouts from which that step also rebuilds riscv-tests' and
  CoreMark's images; without them it rebuilds the rest and says so.

The 64K sort runs' key file (16 MiB) is not an input: `sort-64k` builds it (`fwimage keys 65536`, into
`build/sort-64k.keys`) and checks it against its committed sha256.

The times are wall times on the development host (32 threads, clang, `JOBS=16`); the whole of `all --check` takes
about 2.5 minutes after the build, `quick --check` about 40 seconds.

| step | in | what runs | time | checked against |
|---|---|---|---|---|
| `layout` | both | `sh tools/check_copies.sh` | <1 s | `COPIES.sha256`: every file copied from v1.3 still has v1.3's bytes |
| `bus` | both | `build/clang-debug/suite_bus` | <1 s | log: `tests/p2suites/bus.log` (the framework's port kinds) |
| `fw-images` | both | `fw/check_images.sh`, with `fwimage` | <1 s | the hash lists: `fw/images/MANIFEST`, `tests/core-programs/SHA256SUMS`, file by file; and the committed keys against the generator's (`fw/README.md`) |
| `fw-build` | both, with the RISC-V toolchain | `fw/build.sh --check` | 3 s | every C and assembly image rebuilt from `fw/`'s sources, byte-equal to its committed copy; each run image against its committed master |
| `core-programs` | both | `test_core_programs` | 1 s | the RTL's core programs: each pass, cycle, retirement and trap the RTL's (`tests/core-programs/`) |
| `core-tile` | both | `test_core_tile` | <1 s | the core's accesses to region 0xFF in `register_bus`'s cycles (machine-spec 5, 6.4) |
| `core-contract` | both | `test_rv32_rtl` | <1 s | the core against its contract, test by test: the core specification's §1 on a small bus (the pipeline's cycles, control transfers, multiply, the CSRs, the traps table, interrupts, WFI and the canonical wait), and the tile's environment with region 0xFF decoded by `register_bus.h` (machine-spec 5, 6.4) |
| `regs` | both | `test_regs` | <1 s | `fw/REGISTERS.md` and `fw/include/mesh.h` against their generator, the simulator's decode, the specification's tables and the RTL firmware's definitions |
| `tile-control` | both | `test_tile_control` | <1 s | the tile control unit and the register bus's decode (5, 7) |
| `router` | both | `test_router`, `test_special_router` | <1 s | the RTL's router under scripted random load, cycle by cycle (`tests/routerconf/`); chapter 16 |
| `requests` | both | `test_request_tree`, `test_reqbus` | 2 s | chapter 11: the request unit and the tree |
| `mem`, `mem-shuf`, `mem-shuf2` | both | `test_mem`; with `MEM_SHUFFLE=3` and `8` | <1 s each | the scratchpad and the inject unit (14, 15); the units of a level stepped in a shuffled order |
| `units`, `units-shuf` | both | `test_ag`, `test_receive`, `test_gather`; the first two with `SIM_SHUFFLE=4` | 1 s | the DMA unit (8.2–8.5), the write engine with `PTR` and `HIST` (12.4, 12.5), the gather unit (13) |
| `mach` | both | `test_mach` | <1 s | the tile and the machine harness: the pull boot on all 64 tiles, packets, reads, an all-to-all, the gather unit on the tile |
| `checker` | both | `test_checker` | <1 s | the checker's report for packets that break a rule (4.2, 20) |
| `periph` | both | `test_periph_slot` | 1 s | the peripheral's control block and DMA (17.1–17.4; the RTL's `peripheral_control`, `peripheral_dma`) |
| `ingress-egress` | both | `test_ingress_egress` | <1 s | Ingress/Egress (17.6): the nets, the watchdog, the error lines, the UART, the host interface |
| `rtl-tile` | both | `test_rtl_tile` | <1 s | the RTL's tile programs on one compute tile: the RTL's `base_tile_test.rs`'s verdicts, and the boot ROM's cycles |
| `sort` | both | `test_sort sort_oracle`, then `sort_16` | 5 s | the RTL's `chip_test.rs`'s result checks, under meshsim's firmware: the sort at 64 × 16 records, kv32 and k128, against the oracle, every tile's error provoked; kv32 split over 2 and 8 threads |
| `rtl-stream` | both | `test_rtl_stream` | <1 s | the RTL's `chip_stream_test.rs`, and the UART through the master's core, at the RTL chip's cycles |
| `memory` | both | `test_rw_engine`, then `test_memory` | 2 s | the RTL's `rw_engine_unit_test.rs` ported, with the RTL's outputs' fingerprints, and the RTL's `chip_memory_test.rs`'s result checks, under meshsim's firmware |
| `segment` | both | `test_segment segment_` | 2 s | the RTL's `chip_trace_test.rs`'s result checks, under meshsim's firmware: three generated segments, every row, record, stream, end state and program result against the segment's (`tests/segment/rtl/`) |
| `replayer` | both | `suite_replayer` | 2 s | log: `tests/replayer/replayer.log` |
| `memsys` | both | `suite_memsys` | <1 s | log: `tests/tracersuites/memsys.log` |
| `rvc` | both | `suite_rvc` | <1 s | log: `tests/tracersuites/rvc.log` |
| `tracer` | both | `suite_tracer` | <1 s | log: `tests/tracersuites/tracer.log` |
| `tracer-block-synth` | both | `tracer_block --synthetic` | <1 s | log: `tests/tracerblock/tracer-block-synth.log`; in the run itself, an interpreter of the synthetic guest |
| `tracer-block` | both, with `MESHSIM_INPUTS` | `tracer_block --inputs DIR --program P --segment S --compression C --fill F --quiet --ref FILE`, for EVM Zone's segments 0 and 1 and Cyclone's 0, `C` 0 and 7, `F` 1 and 55,35; then each segment with `--fill model` (15 runs) | 22 s | log: `tests/tracerblock/tracer-block.log`; in the run itself, gold's files and the RTL's figures (`tests/tracerblock/rtl/`) |
| `sort-64k` | all | `fwimage keys 65536 build/sort-64k.keys`, its sha256 checked, then `test_sort sort_64k` | 40 s | as `sort`, at 64 × 65,536 records; the key file against `fw/images/sort/sort-64k.keys.sha256` |
| `segment-real` | all, with `MESHSIM_INPUTS` | `test_segment real_` | 40 s | the real workloads' windows (EVM Zone's 0–63 and 64–127, Cyclone's 0–63): every row, record and stream against the recorded files |

The figures the chip's steps print are in `README.md` (Status); what each test is and where its reference came
from is `tests/README.md`.

## `make check`, and the kit

| run | command | time | checked against |
|---|---|---|---|
| every test, in the debug and release builds with gcc and clang, and the TSan runs | `setarch $(uname -m) -R make -j20 check` (`BUILDING.md` says why `setarch`) | about 10 minutes from a clean tree | every test passes; the last line is `make check: all passed` |
| the acceptance kit's calibration: 128 synthetic windows (seed 11) on the replayer alone, at 1, 4, 8 and 16 processes | `tests/acceptance/make_kit.sh KIT.tgz` (commit first: the kit is HEAD's); unpack it; in it `./build.sh`, then `./calibrate.sh OUT SCRATCH` | under a minute | every window's rows, and its cycles against the RTL's (`tests/acceptance/rtl/synthetic.txt`); `tests/acceptance/README.md` |
| the documents' check | `make doccheck` | seconds | every path a document cites exists (`tools/README.md`) |

## The tools

| tool | command | writes |
|---|---|---|
| a shard of the acceptance kit (the replayer alone on a delivery's windows) | `build/clang-release/replay_shard DELIVERY PROGRAM CSV [--variant V] [--first F --count N] [--manifest M]`; `shard_summary` sums the shards | `CSV` |
| a delivery's windows on the replayer, with the run's figures | `build/clang-release/replay_delivery` (`tests/acceptance/README.md`) | its report |
| the Tracer alone on a segment | `build/clang-release/tracer_block` (its usage line; `tests/README.md`) | its report |
| the delivery tools, the register tables | `tools/README.md` | |
| the benchmark | `bench/README.md` | |
