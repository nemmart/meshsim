# WORKER4 notes: meshsim to v1.2

Branch `v1.2-meshsim`, from `main` at `4e012ad`. Inputs read against `wip/` (identical to `v1.2_wip/`'s snapshot at
`6decc23` for the spec, recipes, draft header and decisions).

## Environment

- The container needs `apt-get install gcc-riscv64-unknown-elf openjdk-21-jdk-headless` (java is present, javac and
  the RISC-V toolchain are not). `test/riscv/bin` (the riscv-tests binaries) is committed, so no riscv-tests checkout.
- One CPU, 3 GB: `run_tests.sh quick` takes about 25 minutes here; the full run goes to the Quest runner.
- A single tool command is capped at 5 minutes, so long runs go in the background (`nohup bash ./run_tests.sh quick`).
- `run_tests.sh` rewrites the committed `results/*.log`; those are restored (`git checkout -- wip/meshsim/results`)
  before every commit, so a commit carries only deliberate result changes.

## v1.1 baseline (`run_tests.sh quick`, 2026-10-30)

Every suite passes except `RecipeTest`'s 46 `[snippets]` checks (both seeds): `wip/sw/RECIPES.md` is now the v1.2
book, and RecipeTest still checks it against v1.1's `sw/tests/rcp_*`. Expected; it goes away when phase 6 replaces the
v1.1 recipe suite with the v1.2 recipes.

| suite | passed | failed |
|---|---|---|
| BusTest | 154 | 0 |
| NetTest (−1, 5, 11) | 30,602 each | 0 |
| MemTest (−1, 3, 8) | 173 each | 0 |
| EngineTest (−1, 4) | 356 | 0 |
| CtrlTest (−1, 4) | 7,158 | 0 |
| RiscvTest | 96 | 0 |
| ReadPortGatherTest (−1, 4) | 499 | 0 |
| AccountingTest | 11 | 0 |
| SearchBench | 56 | 0 |
| AgTest (−1, 4) | 222 | 0 |
| RecipeTest (−1, 4) | 2,277 | **46** (the `[snippets]` checks, above) |
| CondTest (−1, 4) | 396 | 0 |
| RegDocTest | 300 | 0 |
| HooksTest (−1, 4) | 153 | 0 |
| PeripheralSlotTest (−1, 4) | 43 | 0 |
| SenderTest (−1, 4) | 48 | 0 |
| RequestBusTest (−1, 4) | 49 (reqbus_latency 17) | 0 |
| ReplayerTest | 1,183 | 0 |
| SortTest (L1/L2; built-in sets at 64 × ≤ 2,000; seed 4) | 54; 124; 124 | 0 |

## Decisions of my own (to be listed in REPORT.md)

- **The phase-1 line** (agreed with the owner, 2026-10-30): phase 1 ends with the Java-level hardware suites rewritten
  to v1.2 and passing (BusTest, NetTest, MemTest, EngineTest, AgTest, the Java side of ReadPortGatherTest); the C-driven
  suites report "PENDING (phase 2)", since the v1.1 programs use v1.1's modes, addresses and register map until phase 2
  regenerates `mesh.h` and rewrites them; the sort's steps report "PENDING (sort port)".
- **PHASE-1 INTERIM** marks code that keeps the build whole between phase 1 and the phase that replaces it: TileControl's
  send header, read-port setup and counter registers (v1.1's map, counters 1-4), CnC's command and image addresses,
  Sender's address step, PeripheralSlot's hand-over (the replayer still takes v1.1's word address).
- **The sort's suites** (`check_layout.sh`, `sw/ext/sort/build.sh`, SortTest and its runs) leave `run_tests.sh` when
  the packet path changes in phase 1: the sort is not mine and is ported onto this simulator afterwards. Listed in
  QUESTIONS.md (W4-01) for confirmation.

## Log

- **Phase 1** (2026-10-30, **closed**: owner's agreement; W4-02 and W4-03 agreed): LinkWord (256-bit word, 32-bit address, counter field, modes 0-7, bad-packet
  rules), Fields (STREAM layout, the common header layout), SRAMReadQueue (counter field, 32-bit destination, misaligned
  reads not performed with payload 0, no spanning), AddressGenerator (STREAM fields, d = 64-66 and 126, entry checks),
  ReadPort (byte destination, counter), GatherEngine (counter pass-through), InjectionPath (checks), Router and Chip (C&C
  1-3 at tiles 0-2), Endpoint, TrafficDirector (rewritten: region switch, K delay line, seven counters, checks), WriteEngine
  (mode 0 by address, misaligned PTR dropped but counted, no spanning), ControllerMemories and BankedMemory (32 KB + 32 KB,
  byte-addressed port B, command landed on covering slot 0), Config (K, rp_latency, kMin, withTsLatency).
  Tests: NetTest, MemTest, EngineTest (+ A2A, a fence test), AgTest, ReadPortGatherTest (Java parts), BusTest, AgBench
  rewritten or rechecked; run_tests.sh reworked (test/PENDING lists the sources left out of the build; each pending suite
  prints and logs "PENDING (<phase>)").
  Found while testing: quiescence must include the K delay line (runAll stopped before the last decrements); a sweep of
  ts_engine_latency must raise K to its bound (Config.withTsLatency); v1.1's mixed-size group (64, 32, 128 into one PTR)
  now misaligns the PTR and is dropped and flagged, so AgTest uses 128, 64, 32, 32.
  Not yet testable from Java and left to phase 2: additive arming through the counter registers (TileControl's
  register path; counters 5-7 have no v1.1 register), the done gate's effect on the per-counter events.
- **Phase 0** (2026-10-30): branch, toolchain, baseline (above); read the prompt, CHANGES-v1.2, spec 1–4.18 and 5–6.5,
  WORKER3-REVIEW-2 section 6. Still to read in full: 4.19, 6.6–6.7, 8, 9, 10, 13, the recipes and the draft header.

- **Phase 2** (2026-10-30/31, **closed**). Done: Registers.java's v1.2 table and MeshHeader.java (mesh.h 1.2: every
  macro and struct offset shared with the draft agrees, tools/header_diff.py; the generated header adds OFF_* for every
  register, TABLE_BASE and sim_trace()); RegDoc.java generates the v1.2 wip/sw/REGISTERS.md (W4-04) and wip/sw/mesh.h
  is the generated header; TileControl's v1.2 block; the Controller's v1.2 map and latencies; SIM_EXIT and TRACE
  (moved here from phase 5 because the suites were rewritten here); the receive queues in the request tree (128, 256,
  128; credits; controllers' requests at the root); sw/ on the v1.2 map (interim loader, app.ld, crt0 through SIM_EXIT,
  12 programs); the riscv-tests relinked (riscv-tests 3df703c). Suites back: CtrlTest, HooksTest, RegDocTest,
  RiscvTest, RequestBusTest, ReadPortGatherTest's C parts, AccountingTest, SearchBench, Sweep. To later phases:
  CondTest and cond.c (phase 4: REPORT, HALT recovery), PeripheralSlotTest, SenderTest, StageB (phase 3), RecipeTest
  (phase 6). Found: register decode must go by access (a store to STATUS wasn't a bus error); the table-window check
  must use rd_running (a child-order-dependent miss). Measured: a read-port search step costs ~6 cycles more than in
  v1.1 (35.9 + 37.42/active vs 29.8 + 37.43): K = 8 against v1.1's landing at cm_latency 2 (the spec notes this is
  deliberate, 4.7); the sweep's bands are within 0.1% of v1.1's except the searches (+97 cycles at the default).
  The original plan: Registers.java's v1.2 table and a generator (MeshHeader.java) that
  emits the draft's struct-based mesh.h; Conditions as ERRORS; TileControl (PARAMS/STATUS/ERRORS, the pulses, EVENTS,
  the done gate, the send port with no-increment, the counters' registers, the read port's setup, DMA_CONFIG readable,
  reserved words, SIM_EXIT and TRACE); the Controller's v1.2 map and load latencies (2 for the block and windows, 1 for
  the table window and memories); the request port and the receive queues in the tree; sw/ (crt0, linker script at the
  v1.2 map, the test programs ending through SIM_EXIT); the C-driven suites back. **Interim until phase 4:** a resident
  loader in program memory and the RUN line still start the programs (the boot ROM and the reset are phase 4).

- **Phase 3** (2026-10-31, **closed**: the quick run and the full extras pass; the sweep's output identical to phase 2's): EventCounts (counted events with depths, live bits); Peripheral (v1.2
  interface: PARAMS, STATUS, ERRORS, PULSE_RUN, counted events, words 32-127, the DMA's source); PeripheralSlot (up to
  three per unit, the standard window and its access rules, EVENTS_MASK and the consuming load, the controller's live
  bits EVENTS [15:13] and ERRORS [13:11], the DMA engine with DMA_HDR, DMA_ADDR, PULSE_DMA, STATUS [1], DMA done);
  the replayer moved onto it (byte offsets; credits depth 2, run done, new expansion; ERRORS [2:0]; words 32-34);
  Sender retired (its logic is the DMA); the mesh tiles' UART removed (the UART is C&C 1's peripheral, next).
  Then: C&C 1's peripheral (Cnc1Peripheral: ERROR_LINES, READY_LINES/EXPECT, PULSE_GO as the model's GO path, the
  watchdog and toast pin, LED, the UART with 16-byte FIFOs, divisor timing at Config.refCycle core cycles per
  reference cycle, line errors as status); peripheral types per D46 (replayer 4, Ingress/Egress 1; W4-05);
  PeripheralSlotTest rewritten (window, events, region writes, the DMA, the replayer's window, C&C 1's peripheral);
  SenderTest retired. The replayer port (scope update 2026-10-31, mine end to end): ReplayDriver and ReplayerUnitTests
  on v1.2 (ReplayerTest 1,188, the program pass's timing unchanged); sw/replay/replay_ctl.c on mesh.h 1.2 and the credit
  loop (replay_regs.h, its crt0 and linker script gone); StageB on v1.2, with `StageB synthetic` (Stage A's windows on
  a tile, 18, each matching row for row) in run_tests; DELIVERY runs as before when a delivery is given. The replayer
  recipe (wip/sw/recipes/rcp_replay.c) runs in phase 6 with the other recipes.

- **WORKER8's requests** (WORKER8/REQUESTS.md on v1.2-sort): R1, the sort's test files (SortTest, Inputs, SortSummary,
  VerifySummary) and run_tests.sh's sort steps are WORKER8's: I don't edit them here, and when the branches meet,
  v1.2-sort's versions stand. R2: when phase 4's boot ROM replaces the interim loader, this file says so (below, in the
  phase 4 entry). R3: SplitRun and splitdrv.c stay unported (owner), stale, listed in REPORT.md.

- **Phase 4** (2026-10-31, **closed**: the quick suite on the Quest runner, task 075 at `2f45c7f`, 29 steps all
  passed; the full extras, task 074 at `92ddf47`: CtrlTest full 6,902, AccountingTest full 12, ReadPortGatherTest full
  567, EngineTest full 398; the sweep rerun locally after Sweep's boot-phase fix (`35cc4b4`), its bands equal to
  phase 2's. Task 073's three attempts failed on a kit without hops.txt: kits are smoke-tested locally since). **R2 for WORKER8: the boot ROM has replaced the interim loader** (commit
  of this entry): `sw/loader.S`, `sw/loader.ld`, `build/loader.bin` and `Registers.LOADER_BASE` are gone, and there is no
  RUN/HALT line. Every core starts at 0x02000000 in the boot ROM (sw/bootrom.S, its image sw/bootrom.hex, loaded by
  Controller.bootRom) and waits there with done pulsed. `Mach.boot` now: broadcast the code to program memory
  (0x01000000) and the data to data memory (from the image's `_data_start`), uncounted mode 0; then the first command
  (`CMD_JUMP`, slot 1 the entry); then `CnC.runWhenReady()`, which is GO when every tile in `READY_EXPECT` is READY
  (C&C 1's model sets all 64; after `CnC.reset()` it sets them again); the ROM jumps. To start only some tiles, set
  `chip.cc1p.expect` to their mask (RiscvTest does: tile 0). The reset is `CnC.reset(hard)` (a step) or
  `Chip.reset(hard)`, taking effect at the end of the cycle.
  Done: traps (bus errors and illegal instructions to mtvec), the trap CSRs, mcycle/mtime (D48), the hard interrupt
  (ERRORS != 0, MIE and mie.MEIE, WFI included), meshsim's loud stops (ecall, ebreak, a fetch fault, a misaligned jump, a
  trap in the handler); the boot ROM; the reset (every bus emptied through a registry, every unit as at power-on except
  the memories); RUN/HALT and the interim loader removed; D48's words 8-9 (W4-06); v1.1's REPORT flow removed (the
  handlers' reports replace it; CnC.drainReports models C&C 1's draining); meshsim flags an overlay landing after a
  unit's first command and before its start (6.2). Suites: CtrlTest (faults on traps, the reports, the reset and reboot),
  CondTest back (cond.c rewritten: errors 0, 1, 2, 3, 7, 8, 9, 10, 14 from C, 4-6 injected, each through the handler's
  24 reports, the error lines, the toast pin, then a soft reset and reboot), RiscvTest, HooksTest, PeripheralSlotTest,
  RequestBusTest, AccountingTest (the boot is a phase now), SearchBench, StageB (READY_EXPECT for tile 0 only).
  Not exercised: the ROM's controller branch (C&C 1-3 are Java models without cores) and C&C 2/3's start-up requests.
  Test machines now settle in the ROM's WFI before Java drives a tile's registers (the ROM's own stores would race).

- **Phase 6** (2026-10-31, **closed**): the recipes as suites (all 19, RecipeTest 2,042), S1 measured (S1Bench) and
  K's formula per D49 (W4-08), RECIPES.md section 14 filled, REPORT.md written. Task 076 on the runner: the quick
  suite and the full extras; AgBench's failure fixed as the table says.

- **The merge with v1.2-sort** (2026-10-31, `c85f372`, merge not rebase): WORKER8's sort files and run_tests.sh's
  sort steps stand (R1); test/PENDING is empty (the only conflict: it listed the sort's files on one side and v1.1's
  RecipeTest on the other, both now built). check_layout passes; the sort image builds; SortTest p1 passes (42).
  WORKER8/runner's kit rebuilt on the combined branch (the boot ROM in place of the interim loader, HASHES
  regenerated) and smoke-tested at n = 2,000. Verification on the runner: task 077 (the quick suite with the sort's
  steps, the full extras with its BT pair, one full-size sort: p4/uniform-k128 at 64 x 65,536).

## Phase 1's results (`results/`, committed at the close)

`run_tests.sh quick` all passed (PENDING suites aside); the full-mode extras run separately on this box:

| suite | v1.2 |
|---|---|
| BusTest | 154 |
| NetTest (−1, 5, 11; quick) | 31,329 each |
| MemTest (−1, 3, 8) | 177 each |
| EngineTest (−1, 4) | 371 each; full 398 |
| AgTest (−1, 4) | 239 each |
| ReadPortGatherTest (−1, 4; Java parts) | 40 each; full 45 |
| ReplayerTest | 1,183 |
| AgBench | output identical to v1.1's |
| NetTest skewtable (40 patterns) | output identical to v1.1's |

Every other suite logs "PENDING (phase 2 / 3 / 6 / sort port)". Still v1.1's in `results/`, until their phases rerun them:
`phases-a2a*.csv`, `sweep-quick.csv` (phase 2), `replay-build.log`, `stageb-*.log` (phase 3).

## Known-failing at each push

| push | known-failing |
|---|---|
| phase 0 (no code changed) | RecipeTest `[snippets]` (46, both seeds), as in the baseline |
| phase 1, simulator rewritten (`src/` compiles) | **the tests don't compile** (about 230 errors): every suite fails until the Java suites are rewritten |
| phase 1, closed (`651c15c` and after) | none: the quick run passes; the C-driven suites are PENDING (phase 2), not failing |
| phase 2, Registers.java rewritten | **src/ doesn't compile** (Registers' callers and the header generator are being rewritten) |
| phase 2, `0e4ee0c` to `e7a2106` | src/ compiles; the test tree compiles only without the suites still being rewritten (see the log) |
| phase 2, closed | none: the quick run and the full extras pass; PENDING: phase 3, 4, 6 and the sort port |
| phase 3 (WIP), the peripheral framework | src/ compiles; the replayer's tests (ReplayDriver, ReplayerUnitTests) and TestPeripheral don't yet (v1.1's lines and claims are gone) |
| phase 3, closed (`39facff` and the close) | none: the quick run and the full extras pass; PENDING: phase 4 (CondTest), phase 6 (recipes), the sort port |
| phase 4 (WIP), `2adfe2a` to `83c4142` | src/ compiles; a few suites failed while they moved to the ROM boot (fixed by `f7c694f`) |
| phase 4, closed (tasks 074, 075) | none: the quick suite and the full extras pass; PENDING: phase 6 (the recipes, until `6c1cea1`) and the sort port |
| phase 5 (`0318159` to `92ddf47`) | none: the new checks are off only where a test makes errors on purpose |
| phase 6 (WIP, `6c1cea1`) | none known; RecipeTest passes, five recipes (boot, cc1, replay, signal, tables) have no driver yet |
| phase 6, closed (task 076 at `d121db5`; AgBench and S1Bench rerun at the close) | none: the quick suite (31 steps) and 7 of 8 full extras on the runner; AgBench tripped S1's check (a write delayed in a contended area queue past K + 4, which no later read can overtake: the queue serves in order), so the check moved to the ordering point (the write entering its area's queue); AgBench and S1Bench then pass. PENDING: the sort port (until the merge) |
