# Running v1.35

*For anyone running v1.35's suites, kits and tools: each one's command, how long it takes, what it writes, and what it is
checked against.* Building is `BUILDING.md`; the commands run from this folder.

## The suites: `./run_tests.sh`

`./run_tests.sh [all|quick] [--check] [--no-build]` builds the suites (the suite build, `build/clang-suite/`; `bus` uses
the debug build), runs every step in order and prints v1.3's summary; each step writes `results/<step>.log`. The runner also writes `results/.build.log` and `results/.build-bus.log` (the
builds), `results/.differs` (`--check`'s differences) and, with `STEP_TIMES=1`, `results/.step-times`. `quick`
leaves out the steps marked *all* below. `--check` compares each log with its reference, its timings masked; a
difference fails the run. An *equivalent* step runs the `make check` test that stands for a v1.3 suite and logs its
verdict. `CC_NAME=gcc` uses gcc. With `DELIVERY=DIR` (a tracer delivery's replay inputs), five more Stage B steps run
on its guest windows (`stageb-lrd_x0`, `scd_x0`, `nop_hint`, `jalr_self`, `jalr_zero`), unchecked.

The times are wall times on the runner (an Intel i5-14400F: 6 P cores and 4 E cores, 16 threads), from the freeze check (Quest 195: `./run_tests.sh all --check`, 712 s in all; Quests 194 and 196 for the kit and `make check`); `planning/TIMES-V135.md` has them all.

| step | in | command | time | writes | checked against |
|---|---|---|---|---|---|
| `layout` | both | `sh tools/check_copies.sh` | <1 s | `results/layout.log` | `COPIES.sha256`: every file copied from v1.3 still has v1.3's bytes (v1.3's own layout check, `tools/check_layout.sh`, became this one) |
| `sw-build` | both | `fw/build.sh --check` for boot tests m1 m3 replay tracer, or `fw/check_images.sh` without the toolchain | 4 s | `results/sw-build.log` | none (a build step) |
| `sort-build` | both | `fw/build.sh --check` for sort, or `fw/check_images.sh` without the toolchain | <1 s | `results/sort-build.log` | none (a build step) |
| `bus` | both | `build/clang-debug/suite_bus` | 2 s | `results/bus.log` | `tests/p2suites/bus.log` |
| `net` | both | `build/clang-suite/suite_net -1` (quick: `quick`) | 3 s | `results/net.log` | `tests/p4suites/net.log` (quick: `net-quick.log`) |
| `net-shuf` | both | `build/clang-suite/suite_net 5 quick` | 3 s | `results/net-shuf.log` | `tests/p4suites/net-shuf.log` |
| `net-shuf2` | both | `build/clang-suite/suite_net 11 quick` | 2 s | `results/net-shuf2.log` | `tests/p4suites/net-shuf2.log` |
| `mem` | both | `build/clang-suite/suite_mem -1` | 2 s | `results/mem.log` | `tests/p4suites/mem.log` |
| `mem-shuf` | both | `build/clang-suite/suite_mem 3` | 1 s | `results/mem-shuf.log` | `tests/p4suites/mem-shuf.log` |
| `mem-shuf2` | both | `build/clang-suite/suite_mem 8` | 2 s | `results/mem-shuf2.log` | `tests/p4suites/mem-shuf2.log` |
| `engine` | both | `build/clang-suite/suite_engine -1` | 2 s | `results/engine.log` | `tests/p4suites/engine.log` |
| `eng-shuf` | both | `build/clang-suite/suite_engine 4` | 2 s | `results/eng-shuf.log` | `tests/p4suites/eng-shuf.log` |
| `ctrl` | both | `build/clang-suite/suite_ctrl -1` | 1 s | `results/ctrl.log` | `tests/ctlsuites/ctrl.log` |
| `ctrl-shuf` | both | `build/clang-suite/suite_ctrl 4` | 1 s | `results/ctrl-shuf.log` | `tests/ctlsuites/ctrl-shuf.log` |
| `riscv` | both | `build/clang-suite/test_golden` | <1 s | `results/riscv.log`; `results/riscv.out` | equivalent: test_golden (every program's retire trace equal to ref's) |
| `rdport` | both | `build/clang-suite/suite_rdport -1` | 6 s | `results/rdport.log` | `tests/p4suites/rdport.log` |
| `accounting` | both | `build/clang-suite/suite_accounting -1` | <1 s | `results/accounting.log`; `results/phases-a2a.csv` (compared with `tests/ctlsuites/phases-a2a.csv`) | `tests/ctlsuites/accounting.log` |
| `searchbench` | both | `build/clang-suite/suite_search` | 4 s | `results/searchbench.log` | `tests/p4suites/searchbench.log` |
| `rdport-shuf` | both | `build/clang-suite/suite_rdport 4` | 5 s | `results/rdport-shuf.log` | `tests/p4suites/rdport-shuf.log` |
| `agtest` | both | `build/clang-suite/suite_ag -1` | 2 s | `results/agtest.log` | `tests/p4suites/agtest.log` |
| `agtest-shuf` | both | `build/clang-suite/suite_ag 4` | 2 s | `results/agtest-shuf.log` | `tests/p4suites/agtest-shuf.log` |
| `cc-build` | both | `fw/build.sh --check` for cc, or `fw/check_images.sh` without the toolchain | <1 s | `results/cc-build.log` | none (a build step) |
| `recipes-build` | both | `fw/build.sh --check` for recipes, or `fw/check_images.sh` without the toolchain | 3 s | `results/recipes-build.log` | none (a build step) |
| `recipes` | both | `build/clang-suite/suite_recipes -1` | 2 s | `results/recipes.log` | `tests/ctlsuites/recipes.log` |
| `recipes-shuf` | both | `build/clang-suite/suite_recipes 4` | 2 s | `results/recipes-shuf.log` | `tests/ctlsuites/recipes-shuf.log` |
| `cond` | both | `build/clang-suite/suite_cond -1` | <1 s | `results/cond.log` | `tests/ctlsuites/cond.log` |
| `cond-shuf` | both | `build/clang-suite/suite_cond 4` | <1 s | `results/cond-shuf.log` | `tests/ctlsuites/cond-shuf.log` |
| `regdoc` | both | `build/clang-suite/suite_regdoc` | 19 s | `results/regdoc.log` | `tests/ctlsuites/regdoc.log` |
| `hooks` | both | `build/clang-suite/suite_hooks -1` | <1 s | `results/hooks.log` | `tests/ctlsuites/hooks.log` |
| `hooks-shuf` | both | `build/clang-suite/suite_hooks 4` | <1 s | `results/hooks-shuf.log` | `tests/ctlsuites/hooks-shuf.log` |
| `periph` | both | `build/clang-suite/test_tileslotconf` | <1 s | `results/periph.log`; `results/periph.out` | equivalent: test_tileslotconf (W15's TileSlotConf: PeripheralSlotTest's groups on a real tile, against ref) |
| `periph-shuf` | both | `TILESLOTCONF_SEED=4 build/clang-suite/test_tileslotconf` | <1 s | `results/periph-shuf.log`; `results/periph-shuf.raw` | equivalent: test_tileslotconf shuffled (seed 4), its output the plain run's |
| `reqbus` | both | `build/clang-suite/suite_reqbus -1` | 1 s | `results/reqbus.log` | `tests/p4suites/reqbus.log` |
| `reqbus-shuf` | both | `build/clang-suite/suite_reqbus 4` | <1 s | `results/reqbus-shuf.log` | `tests/p4suites/reqbus-shuf.log` |
| `cccores` | both | `build/clang-suite/suite_cccores -1` | 2 s | `results/cccores.log` | `tests/ctlsuites/cccores.log` (D44 masked) |
| `cccores-shuf` | both | `build/clang-suite/suite_cccores 4` | 3 s | `results/cccores-shuf.log` | `tests/ctlsuites/cccores-shuf.log` (D44 masked) |
| `replay-build` | both | `tests/stageb/replay_build.sh` | <1 s | `results/replay-build.log` | `tests/stageb/replay-build.log` |
| `stageb` | both | `build/clang-suite/stageb synthetic` | 1 s | `results/stageb.log` | `tests/stageb/stageb.log` |
| `replayer` | both | `build/clang-suite/suite_replayer` | 2 s | `results/replayer.log` | `tests/replayer/replayer.log` |
| `memsys` | both | `build/clang-suite/suite_memsys` | <1 s | `results/memsys.log` | `tests/tracersuites/memsys.log` |
| `rvc` | both | `build/clang-suite/suite_rvc` | <1 s | `results/rvc.log` | `tests/tracersuites/rvc.log` |
| `tracer` | both | `build/clang-suite/suite_tracer` | <1 s | `results/tracer.log` | `tests/tracersuites/tracer.log` |
| `sort-p1` | both | `build/clang-suite/sort_test -1 p1` | 7 s | `results/sort-p1.log` | `tests/sort/ref/sort-p1.log` |
| `sort` | both | `build/clang-suite/sort_test -1 sort` | 46 s | `results/sort.log` | `tests/sort/ref/sort.log` |
| `sort-shuf` | both | `build/clang-suite/sort_test 4 sort` | 49 s | `results/sort-shuf.log` | `tests/sort/ref/sort-shuf.log` |
| `sort-bt-kv32` | all | `build/clang-suite/sort_test -1 run … fmt=kv32 ready=8 force=bt` on `tests/sort/keys.sh 16384 trace10-10` | 22 s | `results/sort-bt-kv32.log` | `tests/sort/ref/sort-bt-kv32.log` |
| `sort-bt-k128` | all | `build/clang-suite/sort_test -1 run … fmt=k128 ready=8 force=bt` on `tests/sort/keys.sh 16384 trace10-10` | 28 s | `results/sort-bt-k128.log` | `tests/sort/ref/sort-bt-k128.log` |
| `rdport-full` | all | `build/clang-suite/suite_rdport -1 full` | 20 s | `results/rdport-full.log` | `tests/p4suites/rdport-full.log` |
| `accounting-full` | all | `build/clang-suite/suite_accounting -1 full` | 5 s | `results/accounting-full.log`; `results/phases-a2a-full.csv` (compared with `tests/ctlsuites/phases-a2a-full.csv`) | `tests/ctlsuites/accounting-full.log` |
| `agbench` | all | `build/clang-suite/suite_agbench` | 11 s | `results/agbench.log` | `tests/p4suites/agbench.log` |
| `s1bench` | all | `build/clang-suite/suite_s1bench` | 1 s | `results/s1bench.log` | `tests/ctlsuites/s1bench.log` |
| `ccbench` | all | `build/clang-suite/suite_ccbench` | <1 s | `results/ccbench.log` | `tests/ctlsuites/ccbench.log` |
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
| T2 | `build/clang-release/t2 --sram RUN.sram.gz --files RUN.files.gz --segment --parts N --fp-points RUN.fpchip.gz --rows-ref tests/freeze/t2-rows.txt --program P --hint-exit E --hint-end C`, for evm-zone-29 and cyclone-29 segment 10, `N` 1 and 6; the `RUN.*` files are golden-m3's data | 17 s (all four runs) | its log | golden-m3's unit lines, `tests/freeze/t2-rows.txt`, the exits, lag 0 |
| the chains | `build/clang-release/chain --w26 DIR --delivery DIR --program P --segments 0-127`, for both workloads; the inputs are the pipeline's segment files and a tracer delivery (`pipeline/README.md`) | about 17 min each (1,047 s and 1,021 s; they can run side by side) | its log | every start against `tests/golden-chain/`, every TracerT2 check, the ends |
| the acceptance kit's calibration | `tests/acceptance/make_kit.sh KIT.tgz`, then in the kit `./calibrate.sh OUT DELIVERY` (128 synthetic windows, seed 11; 1, 4, 8 and 16 processes) | 37 s | `OUT/` | `tests/acceptance/README.md`'s expectations |
| the sort kit, full size | `tests/sort/kit/assemble.sh OUTDIR [KEYDIR]`, then `OUTDIR/run.sh -j 10 -o results` | 22 min (1,312 s, `-j 10`) | per run `<id>.csv`, `<id>.phases.csv`, `<id>.log`; `SUMMARY.md`, `all.csv` | every run exits 0 or is refused as recorded (`tests/sort/README.md`) |
| `make check` on a clean tree | `make check` | 24 min (1,428 s, `-j16`, both compilers, debug and release, TSan included) | `build/` | every test passes (`BUILDING.md`) |

## The tools

| tool | command | writes |
|---|---|---|
| the sort as v1.3's SortTest | `build/clang-release/sort_test [SEED] p1\|sort\|run …` (`cfg.<field>=value` sets a `MachConfig` field) | its log |
| the sweep, full | `build/clang-suite/sweep full` | `results/sweep-full.csv` |
| two sort runs compared | `build/clang-release/sort_compare NEW_DIR REF_DIR [out.md]` | `out.md` |
| a shard of the acceptance kit | `build/clang-release/replay_shard DELIVERY PROGRAM CSV [--stages ABF] [--variant V] [--first F --count N] [--manifest M]`; `shard_summary` sums the shards | `CSV` |
| Stage B on a delivery | `build/clang-suite/stageb DELIVERY VARIANT PROGRAM [WINDOW …]` | its log |
| the delivery tools, the register tables | `tools/README.md` | |
| the benchmarks | `bench/README.md` | |
