# Running v1.35

*For anyone running v1.35's suites, kits and tools: each one's command, how long it takes, what it writes, and what it is
checked against.* Building is `BUILDING.md`; the commands run from this folder.

## The suites: `./run_tests.sh`

`./run_tests.sh [all|quick] [--check] [--no-build]` builds the suites (the suite build, `build/clang-suite/`; `bus` uses
the debug build), runs every step in order and prints v1.3's summary; each step writes `results/<step>.log`. The runner also writes `results/.build.log` and `results/.build-bus.log` (the
builds), `results/.differs` (`--check`'s differences) and, with `STEP_TIMES=1`, `results/.step-times`. `quick`
leaves out the steps marked *all* below. `--check` compares each log with its reference, its timings masked; a
difference fails the run. An *equivalent* step runs the `make check` test that stands for a v1.3 suite and logs its
verdict. `CC_NAME=gcc` uses gcc.
The step `tracer-block` reads the real workloads' data from `MESHSIM_INPUTS` (`~/meshsim-inputs` by default), outside the
repository; without it the step is not run and is printed and counted as `PENDING`, with or without `--check`.
The step `rtl-sort-64k` (*all*) reads the RTL's 64K key file, which `RTL_SORT_64K_KEYS` names (16 MiB, not vendored:
`fw/rtl/README.md`; Snarky One's `mise run sort-images` writes it as `build/sort/sort-64k.keys`); without it the step is
printed and counted as `PENDING` too.
The step `rtl-segment-real` (*all*) reads the real workloads' data from `MESHSIM_INPUTS` as `tracer-block` does, and is
`PENDING` without it.

**v1.36: v1.3's steps run, are replaced, or are pending.** The suites that run v1.35's firmware or drive v1.35's
register map cannot run on the RTL's hardware. The runner still prints every v1.3 step, in v1.3's order under v1.3's
name: a step that **runs** as before (or with a test of v1.36's hardware in its suite's place: `mem`); a step
**`REPLACED (<by what>)`**, which runs the tests of v1.36's hardware that stand for its suite, or names the step
above that already ran them; a step **`PENDING (<reason>)`**, which nothing stands for yet. Its summary counts the
pending steps by reason and ends with `N steps REPLACED …; M steps PENDING` (25 and 22 in `all`, 23 pending without
the 64K key file; 22 and 14 in `quick`), and the run exits 0 when every step that runs passed. `PENDING-v1.36.md` (the
platform stage, section 4) has both tables, and what each replacement does not cover; in `run_tests.sh` a replaced
or pending step keeps its v1.3 `step` line under it as a comment. The steps that run: `layout`, `bus`, `rtl-images`,
`rtl-tile`, `rtl-sort` (and `rtl-sort-64k` in `all`), `rtl-segment` (and `rtl-segment-real` in `all`), `rtl-stream`, `rtl-memory`, `mem`, `mem-shuf`, `mem-shuf2`, `core-programs`, `core-tile`,
`replayer`, `memsys`, `rvc`, `tracer`, `tracer-block-synth` and `tracer-block`. Those with no v1.3 step are v1.36's own, with no reference log: a
failing test fails the run. `rtl-tile`, right after `bus`, is acceptance A: the RTL's tile programs on one compute
tile (`build/clang-suite/test_rtl_tile`); what the test itself leaves pending is counted apart. `rtl-sort` is
acceptance B: the RTL's sort images end to end on the chip (`build/clang-suite/test_rtl_sort`), 64 × 16 records with
32-bit and with 128-bit values and the first split over 2 and 8 threads; `rtl-sort-64k` the same at 64 × 65,536.
`rtl-segment` is acceptance C: the segment on the chip, run by the RTL's own firmware (the master's trace run, the tracer
tile's image and the replay image), as the RTL's chip tests run it (`build/clang-suite/test_rtl_segment`): the RTL test's
three generated segments, traced by the Tracer, delivered and replayed, against the RTL's models' expectations and, cycle
for cycle, the RTL chip's own figures (`tests/segment/rtl/`); `rtl-segment-real` the real workloads' windows, 192, a
segment of 64 at a time, against their recorded files and the RTL chip's published cycles.
`core-programs` and `core-tile` are the RTL's core programs on the core in the RTL's cycles (`test_core_programs`) and
the core's join to the tile's register bus (`test_core_tile`). The rows below for the pending steps, and their
times, are v1.35's; a replaced step's row is v1.36's.

The times are wall times on the runner (an Intel i5-14400F: 6 P cores and 4 E cores, 16 threads), from the freeze check (Quest 195: `./run_tests.sh all --check`, 712 s in all; Quests 194 and 196 for the kit and `make check`); `planning/TIMES-V135.md` has them all.

| step | in | command | time | writes | checked against |
|---|---|---|---|---|---|
| `layout` | both | `sh tools/check_copies.sh` | <1 s | `results/layout.log` | `COPIES.sha256`: every file copied from v1.3 still has v1.3's bytes (v1.3's own layout check, `tools/check_layout.sh`, became this one) |
| `sw-build` | both | `fw/build.sh --check` for boot tests m1 m3 replay tracer, or `fw/check_images.sh` without the toolchain | 4 s | `results/sw-build.log` | none (a build step) |
| `sort-build` | both | `fw/build.sh --check` for sort, or `fw/check_images.sh` without the toolchain | <1 s | `results/sort-build.log` | none (a build step) |
| `bus` | both | `build/clang-debug/suite_bus` | 2 s | `results/bus.log` | `tests/p2suites/bus.log` |
| `rtl-tile` | both | `build/clang-suite/test_rtl_tile` | <1 s | `results/rtl-tile.log` | the RTL's `base_tile_test.rs`'s verdicts and the boot ROM's cycles, in the test |
| `rtl-sort` | both | `build/clang-suite/test_rtl_sort sort_oracle`, then `… sort_16` | 5 s | `results/rtl-sort.log` | the RTL's `chip_test.rs`'s checks against the oracle, in the test; the split runs' digests against one domain's |
| `rtl-segment` | both | `build/clang-suite/test_rtl_segment segment_` | 3 s | `results/rtl-segment.log` | the RTL's `chip_trace_test.rs`'s checks against the RTL's models' segments, and every cycle against the RTL chip's own figures (`tests/segment/rtl/`), in the test |
| `rtl-stream` | both | `build/clang-suite/test_rtl_stream` | <1 s | `results/rtl-stream.log` | the RTL's `chip_stream_test.rs`'s checks, and the RTL chip's cycles for the same far ends (the host interface's handshakes, the UART's frames through the core), in the test |
| `rtl-memory` | both | `build/clang-suite/test_rw_engine`, then `…/test_rtl_memory` | 2 s | `results/rtl-memory.log` | the RTL's `rw_engine_unit_test.rs` ported, with the RTL's outputs' fingerprints (`tests/rwengine/unit.fp`), and the RTL's `chip_memory_test.rs`'s checks with the RTL chip's handshakes on `lpddr` (`tests/rwengine/chip-memory-*.ev`), in the tests |
| `net` | both | `build/clang-suite/test_router`, then `…/test_special_router` | <1 s | `results/net.log` | **replaced**: v1.3's NetTest cannot run; ref's router observations with v1.36's two rules, and 16.2–16.4, in the tests |
| `net-shuf` | both | — | <1 s | `results/net-shuf.log` | **replaced**: in the step `net` (`test_router`'s own shuffled and instance-major runs) |
| `net-shuf2` | both | — | <1 s | `results/net-shuf2.log` | **replaced**: in the step `net` |
| `mem` | both | `build/clang-suite/test_mem` | <1 s | `results/mem.log` | the specification's 14 and 15 and the RTL's scratchpad and inject unit, in the test (v1.3's MemTest and its logs are retired: `PENDING-v1.36.md`) |
| `mem-shuf` | both | `MEM_SHUFFLE=3 build/clang-suite/test_mem` | <1 s | `results/mem-shuf.log` | the same, the units of a level stepped in a shuffled order |
| `mem-shuf2` | both | `MEM_SHUFFLE=8 build/clang-suite/test_mem` | <1 s | `results/mem-shuf2.log` | the same |
| `engine` | both | `build/clang-suite/test_ag`, `…/test_receive`, `…/test_gather` | <1 s | `results/engine.log` | **replaced**: v1.3's EngineTest cannot run; 8.2–8.5, 12.4, 12.5 and 13, in the tests |
| `eng-shuf` | both | `SIM_SHUFFLE=4` `build/clang-suite/test_ag`, `…/test_receive` | <1 s | `results/eng-shuf.log` | **replaced**: the same, the units of a level stepped in a shuffled order |
| `ctrl` | both | `build/clang-suite/suite_ctrl -1` | 1 s | `results/ctrl.log` | `tests/ctlsuites/ctrl.log` |
| `ctrl-shuf` | both | `build/clang-suite/suite_ctrl 4` | 1 s | `results/ctrl-shuf.log` | `tests/ctlsuites/ctrl-shuf.log` |
| `riscv` | both | — | <1 s | `results/riscv.log` | **replaced**: the step `core-programs` |
| `rdport` | both | `build/clang-suite/suite_rdport -1` | 6 s | `results/rdport.log` | `tests/p4suites/rdport.log` |
| `accounting` | both | `build/clang-suite/suite_accounting -1` | <1 s | `results/accounting.log`; `results/phases-a2a.csv` (compared with `tests/ctlsuites/phases-a2a.csv`) | `tests/ctlsuites/accounting.log` |
| `searchbench` | both | `build/clang-suite/suite_search` | 4 s | `results/searchbench.log` | `tests/p4suites/searchbench.log` |
| `rdport-shuf` | both | `build/clang-suite/suite_rdport 4` | 5 s | `results/rdport-shuf.log` | `tests/p4suites/rdport-shuf.log` |
| `agtest` | both | — | <1 s | `results/agtest.log` | **replaced**: in the step `engine` (`test_ag`'s read-ahead, `test_receive`'s `PTR` and `HIST` ports) |
| `agtest-shuf` | both | — | <1 s | `results/agtest-shuf.log` | **replaced**: in the step `eng-shuf` |
| `cc-build` | both | `fw/build.sh --check` for cc, or `fw/check_images.sh` without the toolchain | <1 s | `results/cc-build.log` | none (a build step) |
| `recipes-build` | both | `fw/build.sh --check` for recipes, or `fw/check_images.sh` without the toolchain | 3 s | `results/recipes-build.log` | none (a build step) |
| `recipes` | both | `build/clang-suite/suite_recipes -1` | 2 s | `results/recipes.log` | `tests/ctlsuites/recipes.log` |
| `recipes-shuf` | both | `build/clang-suite/suite_recipes 4` | 2 s | `results/recipes-shuf.log` | `tests/ctlsuites/recipes-shuf.log` |
| `cond` | both | `build/clang-suite/test_ingress_egress`, `…/test_tile_control` | <1 s | `results/cond.log` | **replaced**: v1.3's suite cannot run; 7.4 and 17.6 in the tests, and the errors end to end in `rtl-sort` |
| `cond-shuf` | both | — | <1 s | `results/cond-shuf.log` | **replaced**: in the steps `cond` and `rtl-sort` |
| `regdoc` | both | `build/clang-suite/test_regs` | <1 s | `results/regdoc.log` | **replaced**: v1.3's RegDocTest cannot run; `REGISTERS.md` and `mesh.h` against the generator, the simulator's decode, the specification and the RTL firmware's definitions, in the test |
| `hooks` | both | — | <1 s | `results/hooks.log` | **replaced**: the steps `regdoc`, `core-tile`, `rtl-tile` and `mem` |
| `hooks-shuf` | both | — | <1 s | `results/hooks-shuf.log` | **replaced**: in those steps |
| `periph` | both | `build/clang-suite/test_periph_slot` | <1 s | `results/periph.log` | **replaced**: v1.3's suite cannot run; 17.1–17.4 and the RTL's `peripheral_control` and `peripheral_dma`, in the test |
| `periph-shuf` | both | — | <1 s | `results/periph-shuf.log` | **replaced**: in the step `periph` |
| `reqbus` | both | `build/clang-suite/test_request_tree`, `…/test_reqbus` | <1 s | `results/reqbus.log` | **replaced**: v1.3's suite cannot run; chapter 11 and the RTL's request unit and tree, in the tests |
| `reqbus-shuf` | both | — | <1 s | `results/reqbus-shuf.log` | **replaced**: in the step `reqbus` |
| `cccores` | both | — | <1 s | `results/cccores.log` | **replaced**: the step `rtl-sort` (the special tiles boot the RTL's images) |
| `cccores-shuf` | both | — | <1 s | `results/cccores-shuf.log` | **replaced**: in the step `rtl-sort` (its split run) |
| `replay-build` | both | — | <1 s | `results/replay-build.log` | **replaced**: the step `rtl-images` (the RTL's replay image, vendored, against its hash list) |
| `stageb` | both | — | <1 s | `results/stageb.log` | **replaced**: the steps `rtl-segment` and `rtl-segment-real` (the RTL's replay image replays the Tracer's deliveries on the chip) |
| `replayer` | both | `build/clang-suite/suite_replayer` | 2 s | `results/replayer.log` | `tests/replayer/replayer.log` |
| `memsys` | both | `build/clang-suite/suite_memsys` | <1 s | `results/memsys.log` | `tests/tracersuites/memsys.log` |
| `rvc` | both | `build/clang-suite/suite_rvc` | <1 s | `results/rvc.log` | `tests/tracersuites/rvc.log` |
| `tracer` | both | `build/clang-suite/suite_tracer` | <1 s | `results/tracer.log` | `tests/tracersuites/tracer.log` |
| `tracer-block-synth` | both | `build/clang-suite/tracer_block --synthetic` | <1 s | `results/tracer-block-synth.log` | `tests/tracerblock/tracer-block-synth.log`; in the run itself, an interpreter of the synthetic guest (`tests/tracerblock/guest.h`) |
| `tracer-block` | both, with the data | `build/clang-suite/tracer_block --inputs DIR --program P --segment S --compression C --fill F --quiet --ref tests/tracerblock/rtl/windows-….txt`, for EVM Zone's segments 0 and 1 and Cyclone's 0, `C` 0 and 7, `F` 1 and 55,35; then each segment with `--fill model` | 22 s (15 runs; on the development host) | `results/tracer-block.log` | `tests/tracerblock/tracer-block.log`; in the run itself, gold's files and the RTL's figures (`tests/tracerblock/rtl/`) |
| `sort-p1` | both | — | <1 s | `results/sort-p1.log` | **replaced**: the step `rtl-sort` (L1 and L2 of the RTL's runs) |
| `sort` | both | `build/clang-suite/sort_test -1 sort` | 46 s | `results/sort.log` | `tests/sort/ref/sort.log` |
| `sort-shuf` | both | `build/clang-suite/sort_test 4 sort` | 49 s | `results/sort-shuf.log` | `tests/sort/ref/sort-shuf.log` |
| `rtl-sort-64k` | all, with the key file | `RTL_SORT_64K_KEYS=FILE build/clang-suite/test_rtl_sort sort_64k` | 44 s | `results/rtl-sort-64k.log` | as `rtl-sort`, at 64 × 65,536 records |
| `rtl-segment-real` | all, with the data | `MESHSIM_INPUTS=DIR build/clang-suite/test_rtl_segment real_` | 55 s | `results/rtl-segment-real.log` | the windows' recorded files under `DIR` (every row, record and stream), and the RTL chip's cycles for the same windows (Snarky One's `rtl/chip/README.md`, written in the test) |
| `sort-bt-kv32` | all | `build/clang-suite/sort_test -1 run … fmt=kv32 ready=8 force=bt` on `tests/sort/keys.sh 16384 trace10-10` | 22 s | `results/sort-bt-kv32.log` | `tests/sort/ref/sort-bt-kv32.log` |
| `sort-bt-k128` | all | `build/clang-suite/sort_test -1 run … fmt=k128 ready=8 force=bt` on `tests/sort/keys.sh 16384 trace10-10` | 28 s | `results/sort-bt-k128.log` | `tests/sort/ref/sort-bt-k128.log` |
| `rdport-full` | all | `build/clang-suite/suite_rdport -1 full` | 20 s | `results/rdport-full.log` | `tests/p4suites/rdport-full.log` |
| `accounting-full` | all | `build/clang-suite/suite_accounting -1 full` | 5 s | `results/accounting-full.log`; `results/phases-a2a-full.csv` (compared with `tests/ctlsuites/phases-a2a-full.csv`) | `tests/ctlsuites/accounting-full.log` |
| `agbench` | all | — | <1 s | `results/agbench.log` | **replaced**: in the step `engine` (`READ_AHEAD` is not a setting; the lead-ins and waits are `test_ag`'s) |
| `s1bench` | all | — | <1 s | `results/s1bench.log` | **replaced**: in the step `engine` (the write path's fixed 2L + 2 cycles: `test_receive`) |
| `ccbench` | all | — | <1 s | `results/ccbench.log` | **replaced**: the step `rtl-sort` (the load's and each phase's cycles against the RTL chip's) |
| `sweep-quick` | all | `build/clang-suite/sweep quick` | 41 s | `results/sweep-quick.log`; `results/sweep-quick.csv` | `tests/sort/ref/sweep-quick.log` |
| `ctrl-full` | all | `build/clang-suite/suite_ctrl -1 full` | 52 s | `results/ctrl-full.log` | `tests/ctlsuites/ctrl-full.log` |
| `engine-full` | all | `build/clang-suite/suite_engine -1 full` | 79 s | `results/engine-full.log` | `tests/p4suites/engine-full.log` |
| `skewtable` | all | `build/clang-suite/suite_net -1 skewtable 40` | 4 min 13 s | `results/skewtable.log` | `tests/p4suites/skewtable.log` |

## The long runs and the kits

These are the freeze check's items (`tests/freeze/body.sh` runs each exactly as below, from this folder, in the
release build). The inputs they need beyond this folder are named; the freeze check's task fetches them.

| run | command | time | writes | checked against |
|---|---|---|---|---|
| the full-size sort, one thread | `SORT_SIZE=64k SORT_PARTS=1 SORT_KEYS=DIR/uniform.keys SORT_FP=FILE build/clang-release/test_sort sort_runs`; the keys from `KeyGen … --n 65536 --only uniform` | 34 s | its log | every `sort64k-fp` point, the result, the phases, 1,229,687 cycles |
| the same, 6 domains | `build/clang-release/bench_sort --keys DIR/uniform.keys --threads 6 --partition tiles --reps 1` (and `--threads 1 --partition one`) | 138 s (both runs) | one result line | the digest at L2's all-READY equal on both; the span 1,229,687 |
| the acceptance kit's calibration | `tests/acceptance/make_kit.sh KIT.tgz`, then in the kit `./calibrate.sh OUT DELIVERY` (128 synthetic windows, seed 11; 1, 4, 8 and 16 processes; Stage A only in v1.36) | 37 s (v1.35's, with Stages B and F) | `OUT/` | `tests/acceptance/README.md`'s expectations |
| the sort kit, full size | `tests/sort/kit/assemble.sh OUTDIR [KEYDIR]`, then `OUTDIR/run.sh -j 10 -o results` | 22 min (1,312 s, `-j 10`) | per run `<id>.csv`, `<id>.phases.csv`, `<id>.log`; `SUMMARY.md`, `all.csv` | every run exits 0 or is refused as recorded (`tests/sort/README.md`) |
| `make check` on a clean tree | `make check` | 24 min (1,428 s, `-j16`, both compilers, debug and release, TSan included) | `build/` | every test passes (`BUILDING.md`) |

## The tools

| tool | command | writes |
|---|---|---|
| the sort as v1.3's SortTest | `build/clang-release/sort_test [SEED] p1\|sort\|run …` (`cfg.<field>=value` sets a `MachConfig` field) | its log |
| the sweep, full | `build/clang-suite/sweep full` | `results/sweep-full.csv` |
| two sort runs compared | `build/clang-release/sort_compare NEW_DIR REF_DIR [out.md]` | `out.md` |
| a shard of the acceptance kit (Stage A: the replayer alone) | `build/clang-release/replay_shard DELIVERY PROGRAM CSV [--variant V] [--first F --count N] [--manifest M]`; `shard_summary` sums the shards | `CSV` |
| the delivery tools, the register tables | `tools/README.md` | |
| the benchmarks | `bench/README.md` | |
