# v1.35's run times (for RUNNING.md)

Measured on the runner (Intel i5-14400F: 6 P cores, 12 threads, and 4 E cores; 64 GB; Linux 6.8; gcc 13.3.0, clang 18.1.3,
OpenJDK 21; the firmware toolchain riscv64-unknown-elf-gcc 13.2.0, binutils 2.42, picolibc 1.8.6, image builds under
LC_ALL=C). Quest 190 (main 3e5b90ab), each item run alone; Quest 191 the sort's measurement kit at full size, -j 10.
ctrl, ctrl-shuf, ctrl-full, recipes, recipes-shuf and periph-shuf were still PENDING in 190; locally (one CPU) ctrl-full
took 75 s and the others a few seconds each.

## run_tests.sh all --check (655 s in total, run alone; STEP_TIMES=1)

| step | seconds |
|---|---|
| (build) | 17 |
| sw-build | 4 |
| sort-build | 0 |
| bus | 2 |
| net | 6 |
| net-shuf | 2 |
| net-shuf2 | 2 |
| mem | 2 |
| mem-shuf | 2 |
| mem-shuf2 | 1 |
| engine | 2 |
| eng-shuf | 2 |
| riscv | 0 |
| rdport | 6 |
| accounting | 0 |
| searchbench | 3 |
| rdport-shuf | 6 |
| agtest | 2 |
| agtest-shuf | 1 |
| cc-build | 1 |
| recipes-build | 2 |
| cond | 1 |
| cond-shuf | 0 |
| regdoc | 18 |
| hooks | 1 |
| hooks-shuf | 0 |
| periph | 0 |
| reqbus | 0 |
| reqbus-shuf | 0 |
| cccores | 2 |
| cccores-shuf | 3 |
| replay-build | 0 |
| stageb | 1 |
| replayer | 2 |
| memsys | 0 |
| rvc | 0 |
| tracer | 0 |
| sort-p1 | 7 |
| sort | 45 |
| sort-shuf | 50 |
| sort-bt-kv32 | 21 |
| sort-bt-k128 | 29 |
| rdport-full | 20 |
| accounting-full | 5 |
| agbench | 11 |
| s1bench | 1 |
| ccbench | 0 |
| sweep-quick | 41 |
| engine-full | 82 |
| skewtable | 252 |

## The freeze check's items (Quest 190, 191)

| item | seconds |
|---|---|
| build | 13 |
| run_tests | 655 |
| sort-one | 33 |
| sort-split6 | 139 |
| t2 | 18 |
| chain-evm | 1033 |
| chain-cyclone | 1007 |
| calibration | 36 |
| sort's measurement kit, 92 runs, -j 10 (191) | 1305 |
| make check from a clean tree | pending (the final freeze run) |
