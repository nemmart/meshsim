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
| 1 | the three controller tiles (ids 64-66, their peripherals' presence and types), the controller router and its link to tiles 0-2; the boot ROM's 64-66 branch runs with the harness landing a command | done (turn 1): CcCoresTest |
| 2 | `Cnc1Peripheral` in C&C 1's slot (READY/GO, error lines, watchdog, toast, UART); stub engines for C&C 2/3; controllers on the request tree, C&C 1's request port reading target 0; C&C 2/3's error lines | wired in turn 1 (the request path tested); the peripheral's paths are tested with the firmware (phases 3-4) |
| 3 | firmware (`sw/cc/`): C&C 1 boots C&C 2/3, waits for "started", boots the tiles, then a phase loop on harness commands; C&C 2/3 start-up and "started"; `Mach.bootCores()`; the boot test | done (turn 2): CcCoresTest's boot and phases against the model |
| 4 | `rcp_cc1.c` run on C&C 1 (RecipeTest's cc1 driver); the error path end to end | error path done (turn 3); `rcp_cc1.c` and the two-report path wait for s8 |
| 5 | measurements (boot vs the model's 120 cycles; a phase vs the model); full suites with the switch off on the runner; REPORT.md | done (turn 4): task 090 DONE |

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

- Turn 1: phase 0; phase 1 and most of phase 2's wiring.
  - New: `src/CcCores.java` (the controller tiles, their links, the host port, the stub engines, their checks),
    `test/CcCoresTest.java` (19 checks; in run_tests.sh's quick suite with a shuffled run).
  - Changed, all inert with the switch off: `Config` (`ccCores`, `ccLinkLatency`); `Router` (a controller's routing
    when me >= 64; a null link input skipped); `SRAMReadQueue.loops` (a controller's broadcast takes no loopback token:
    it never reaches itself); `Tile` (a constructor taking peripheral 1); `RequestPort` (a controller's receive side reads
    its target's queue); `RequestBus` (the controllers' request ports feed the root); `Cnc1Peripheral.unitErrors` (C&C
    1's own ERRORS latch the toast pin); `Chip` (seven hook lines, beside `parked`).
  - With the switch off: 15 quick steps rerun after the change (bus, net, mem, engine, ctrl, riscv, accounting, agtest,
    recipes, cond, regdoc, hooks, periph, reqbus, replayer), counts equal to the baseline. The rest at phase 5.
- Turn 2: merged main (b060637: CL1's queue map, D48; no conflict, nothing in my files). Phase 3.
  - `sw/cc/cc1.c` (C&C 1: the boot, recipe 22's way; then CC_PHASE / CC_GO / CC_EXPECT / CC_EXIT from the harness,
    a mailbox), `sw/cc/cc23.c` (C&C 2 and 3: "started", then a stub request loop), `sw/build_cc.sh` (the recipes'
    crt0.S and handler.S, recipes.ld, the generated mesh.h; in run_tests.sh before the recipes' build).
  - `Mach.bootCores(tile)`: stages C&C 2/3's and the tile image in C&C 1's big SRAM (back door, W14-03), fills C&C 1's
    `boot_desc` in its data image, lands C&C 1's image and CMD_JUMP through the host port; `phase()`, `cc1Command()`,
    `mbox()`.
  - CcCoresTest's boot: 34 checks, plain and shuffled; the `cmd` program's three phases give every tile the model's sum.
  - First boot measurement (`cmd`, 64 tiles): 782 cycles from reset release to all 64 running with C&C 1 as a core,
    against the model's 83 (C&C 1 itself running at 157: the host lands its 2 KB image at a packet a cycle; C&C 2/3
    started by 499). The first version took 1,376: crt0 zeroing a 64-word parity array and a 64-pass parity loop
    (now a bitmask in .data), three put_command round trips (now one counter wait), and C&C 2 and C&C 3 booted in
    two runs (now one). Phase 5 measures it properly, with `rcp_boot` against the model's 120.
- Turn 3: merged main (8224b97: DECISIONS-v1.25 section 8). Phase 4, the error path, to 8d (W10 updates 6.2 and 4.13;
  W11 rewrites bootrom.S and recipes 22/24 on branch s8; I change neither):
  - `cc1.c`: after the boot, mtvec at its own handler (`cc1_trap`) and the hard interrupt on. `cc1_toast` drains the
    reports into data memory (0x1000-0x4FFF, 1,024 reports; `build_cc.sh` fails if data and .bss pass 0x1000), counting
    each unit's against its set, told by its first report's sequence number: 0, the image's 24 (0-23); 24, the ROM's
    two (24-25). It ends when every set begun is complete and SETTLE (1,000) cycles pass quietly, or after DRAIN_QUIET
    (50,000) with nothing; then `toast_info` and the UART dump (summary, one line per unit: id, set size, received;
    then the reports), then parks. The watchdog's expiry, while C&C 1 waits for READY or for its own command, takes the
    same path with mcause 0. New harness commands: CC_WATCHDOG, CC_UART (the divisor).
  - `cc23.c`: installs recipe 24's handler and enables the hard interrupt after "started", so C&C 2's and C&C 3's
    errors send their 24 reports.
  - Tested (CcCoresTest, 61 checks): tile 9 and C&C 2 fail at once (rcp_boot's CMD_STAGE installs every tile's
    handler); C&C 1 traps 3 cycles later, drains 48 reports, both sets complete with no flags, ERROR_LINES_0 bit 9 and
    ERROR_LINES_2 [1], the toast pin, the UART dump's lines. The watchdog: no READY after a phase, toast with mcause 0.
    The two-report path waits for s8 (the ROM still sends 24). Misalignment is UB (8b): not an error path.
  - meshsim fix: a store to C&C 1's UART_TX while the TX FIFO is full now stalls the core (8.14), through a new
    `Peripheral.storeStalls` (default false) and a check in `Controller`'s store path beside REQUEST_SEND's. The Java
    model never stored through a core, so the slot treated the stall code as an undefined word. Regression subset (ctrl,
    riscv, recipes, cond, regdoc, hooks, periph, reqbus) unchanged.
  - C&C 1's image is now 3 KB (the error path): the boot measures 854 cycles (C&C 1 running at 232).
  - The dump at divisor 1 takes about 765,000 cycles for 48 reports (400 cycles a byte); at the reset divisor (434) it
    would be about 330 million. CcCoresTest takes about 30 s.
- Turn 4: phase 5.
  - `test/CcBench.java` (in the full extras): the boot, 901 cycles with C&C 1 as a core against the model's 120
    (rcp_boot), broken down in REPORT.md; a phase, 88 cycles GO to GO against the model's 43.
  - Firmware: `CC_PHASE` repeats ([22:16]); the READY wait checks all-READY before anything else (91 to 88 with the
    command copied once; 102 before).
  - Task 090 (Quest, kit d51bfc89 from 5cd78a2, Java 17): quick + full extras with CcBench. Pushed 10:30 UTC.
  - REPORT.md written; it names the UART_TX stall fix for the merge review.
  - Task 090: DONE at 10:44 UTC. Quick: 36 steps, all passing, baseline counts. Full extras: 11 steps, all passing.
    The full extras' logs and the quick sort, recipes and ctrl logs are identical to task 087's (main ba7a97c) apart
    from timing lines.
- Turn 5: merged main (668d0a8 s8, 7570153 s9, 9133397). No conflicts; my changes in Controller, PeripheralSlot,
  SRAMReadQueue and Tile merged cleanly. All builds reproduce the committed files (the new 256-byte ROM included).
  - The two-report path (8d): `sw/cc/cc_bad.c`, a tile image whose tile 12 takes a bus error before any handler of its
    own. Tested (CcCoresTest, now 77 checks):
    - the ROM sends reports 24 and 25, stamped 12;
    - C&C 1 counts a set of two, complete, with no flags;
    - report 24 carries ERRORS [9] and mcause 5;
    - the UART line reads 12, 2 of 2.
    C&C 1's UART divisor moved into its boot descriptor (`Mach.uartDivisor`), since C&C 1 traps before it would read
    a command.
  - rcp_cc1.c on C&C 1: RecipeTest's `cc1()` driver, with `sw/cc/cc_rcp.c` as the units' image (C&C 2, C&C 3 and the
    tiles in one, within the 64 + 16 packets rcp_cc1.c streams; build_cc.sh checks). `Mach.hostBoot`, `stageCc1`.
    - Works: the boot of C&C 2 and C&C 3, both "started", the tiles, boot part 2's 256 rows in every tile's big SRAM,
      and recipe fence's second CMD_JUMP to C&C 3.
    - Fails: the phase loop deadlocks (W14-06, a one-line fix proposed and tried, asked).
    - W14-07: meshsim's overlay check fixed for a running controller.
  - Regression subset after the merge (15 steps): all pass.
