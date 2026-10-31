# WORKER14 notes: real cores for C&C 1-3 in meshsim

Branch `cc-cores` (from `main`; merge `main` in as it moves, never rebase). Plan approved by the integrator.

## Plan

The controllers are real `Tile` instances (ids 64-66), behind `Config.ccCores` (off by default): spec 6.6 makes them
tile instances, and recipe 22 needs C&C 1's address generator. The new part is the controller's router (W14-01). `Chip`
gets a few hook calls into a new `CcCores` class, beside WORKER11's `Chip.parked`; with the switch off the Java model
(`CnC.java`) runs as today.

| phase | what | status |
|---|---|---|
| 0 | branch, toolchain, the quick-suite baseline, these notes, QUESTIONS.md | done |
| 1 | the three controller tiles (ids 64-66, their peripherals' presence and types), the controller router and its link to tiles 0-2; the boot ROM's 64-66 branch runs with the harness landing a command | |
| 2 | `Cnc1Peripheral` in C&C 1's slot (READY/GO, error lines, watchdog, toast, UART); stub engines for C&C 2/3; controllers on the request tree, C&C 1's request port reading target 0; C&C 2/3's error lines | |
| 3 | firmware (`sw/cc/`): C&C 1 boots C&C 2/3, waits for "started", boots the tiles, then a phase loop on harness commands; C&C 2/3 start-up and "started"; `Mach.bootCores()`; the boot test | |
| 4 | `rcp_cc1.c` run on C&C 1 (RecipeTest's cc1 driver); the error path end to end | |
| 5 | measurements (boot vs the model's 120 cycles; a phase vs the model); full suites with the switch off on the runner; REPORT.md | |

## Environment

- The container has no toolchain by default: `apt-get install gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf
  openjdk-21-jdk-headless` (after `apt-get update`). With them, every build step reproduces the committed files
  (`sw/mesh.h`, `sw/bootrom.hex`, the recipes, the sort, the replay controller): a clean `git status` after building.
- A single tool command is cut at 300 s, so the quick suite runs in slices (a step-filtered copy of
  `tools/run_quick.sh`'s loop, logs outside the repository, not into `results/`); one CPU.

## Baseline (main at 7578384, before any change)

The quick suite, 34 steps, all passing, about 14 minutes on one CPU:
bus 154; net, net-shuf, net-shuf2 31330 each; mem x3 177; engine, eng-shuf 371; ctrl, ctrl-shuf 6893; riscv 96;
rdport, rdport-shuf 562 (55 s, 100 s); accounting 12; searchbench 56; agtest x2 239; recipes, recipes-shuf 2042;
cond x2 141; regdoc 269; hooks x2 142; periph x2 92; reqbus x2 50; stageb 18 windows; replayer 1461; sort-p1 42;
sort 104 (140 s); sort-shuf 104.

## Log

- Turn 1: phase 0.
