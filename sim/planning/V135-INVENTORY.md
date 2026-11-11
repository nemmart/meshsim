# v1.35: the inventory and the porting plan (WORKER18)

Every capability of v1.3's Java meshsim (`sim/ref`, a copy of `v1.3/meshsim` plus D5's observer, and the siblings it
reads), its C++ equivalent today, and, under **D39** (everything off Java; v1.35 stand-alone; nothing in it reads
`sim/ref` or `v1.3/`), the port: its estimate, how it is checked against v1.3, and what it depends on. The worker-sized
pieces, their order and the owner's questions are at the end. Branch `sim-w18`; written 2026-11-10 at main 2ee8729.

**How to read a row.** *Who*: **RTL** (the RTL engineer), **proof** (the proof pipeline and its acceptance), **reg**
(regressions, the freeze check), **sw** (firmware and sort authors), **dev** (simulator developers). *Today*: the C++
equivalent and **the evidence that it matches v1.3** (a conformance driver, goldens, fingerprints, a Quest), or **gap**
(nothing), **unchecked** (code exists, nothing compares it with v1.3) or **partial**. *Port*: **S** (≤ ~300 new lines, a
day or so), **M** (~300-1,500 lines, a few days), **L** (more); then the check and the dependencies (row ids). Rows
marked **eq** need no port; under D39 they only move into the stand-alone tree with their fixtures (piece P1).

**The checks** are M1-M3's, against the oracle (`sim/ref` with the drivers in `sim/tests/ref`, both outside v1.35):
- **log-equal**: the ported suite's output equal to v1.3's log of the same step (`run_tests.sh`'s `results/<step>.log`),
  timing lines masked, every assertion passing. v1.35 is exact to v1.3 (D30's one difference, D33, is never reached by a
  reference run), so measured figures must be equal, not close.
- **fp**: the canonical state fingerprint (AUDIT-M2 6, AUDIT-M3 6) at points, against a `ref` run of the same program
  (an `M2Prog`/`M2Sort`/`M3Seg`-style driver in `sim/tests/ref`); used where a suite's output alone wouldn't localise.
- **bytes**: generated files (images, headers, key sets, deliveries, CSVs) byte-identical (sha256) to v1.3's.
- **conf**: a conformance script, `ref`'s observations against the C++ unit's, committed as fixtures.
Every reference is generated once on the oracle side and **committed as a fixture** in v1.35 (as `tests/golden*` and
`tests/*conf/` are today); the regenerators stay outside (row I3).

**Measured locally (W18, clang release, one CPU):** `sim/` without `ref/` builds; **48 of its 63 test binaries pass and
15 fail, every failure a data file read from `ref/`** (I1); with `ref/` back, all 63 pass.

## A. The stand-alone tree: data files, firmware sources, generators

| # | capability | what it is; who | today | port: estimate · check · depends |
|---|---|---|---|---|
| A1 | `hops.txt` | the routing offset table hops[k] (spec 4.6), validated at load; all | read from `ref/meshsim/hops.txt` (9 call sites) | **S** · bytes · — |
| A2 | the boot ROM: `bootrom.S`, `bootrom.ld`, `bootrom.hex` | the 256-byte ROM every core boots from (6.2); RTL, all | the hex read from `ref/meshsim/sw/` | **S**: source, script and hex · bytes (rebuilt hex) · A12 |
| A3 | riscv-tests images: `test/riscv/{bin,build.sh,link.ld,riscv_test.h}` (144 files, riscv-tests 3df703c) | rv32ui/rv32um for the controller; reg | read from `ref/meshsim/test/riscv/bin` (golden harness) | **S**: bins and build script (the riscv-tests checkout a documented prerequisite) · bytes · A12 |
| A4 | the replayer tables: `tables/replayer/`, `opcode_table.txt`, `manifest.json`; `data/vec-821e1bd.tsv` | the universal decode table, ROM and templates (jolt-cpp 821e1bd); the ALU's test vectors; proof, reg | read from `ref/tables`, `ref/meshsim/data` | **S** · bytes · — (their generator: F6) |
| A5 | the stream matrices `data/{zipfraw,trace,one99}.txt` | the all-to-all data sets (CtrlTest full, EngineTest full); reg | gap (no C++ reader) | **S** (copy; the reader is C1's) · bytes · — |
| A6 | the tile programs: `sw/tests/*.c` (hello, faults, misalign_ld/st, regs, wfi, a2a, cmd, cond, inv, blockcopy, gather, rdport, req, search, the `rcp_*` suites, `selftest.h`, `rcp.h`), `crt0.S`, `app.ld`, `loader.S/ld`, `sw/build.sh` | every program the suites boot; sw, reg | images only for hello, faults, misalign_*, regs, wfi, a2a (`tests/golden/images`, `golden-m2/images`), built by scripts that copy `ref` | **S**: sources, build script, every image prebuilt · bytes (each image equal to v1.3's build at the same toolchain) · A11, A12 |
| A7 | C&C firmware: `sw/cc/{cc1,cc23,cc_bad,cc_rcp}.c`, `sw/build_cc.sh` | C&C 1-3's programs (boot, CC_SEGMENT); RTL, proof | cc1, cc23 images in `tests/m3-images` (built from a copy of `ref`) | **S** · bytes (against `m3-images/MANIFEST`) · A12 |
| A8 | the replay and tracer controllers: `sw/replay/` (`replay_ctl.c`, `replay_ctl_oldmap.c`, `replay_layout.h`, `build.sh`), `sw/tracer/` (`tracer_ctl.c`, `tracer_layout.h`, `build.sh`) | the tiles' and C&C 3's programs in T2 and the chain; proof | images in `tests/m3-images` | **S** · bytes · A12 |
| A9 | the recipes: `sim/ref/sw/recipes/` (31 files: `rcp_*.c`, `crt0.S`, `handler.S`, `bootrom.S`, `gen_book.py`, `check_book.py`, `regions.py`), `sw/build_recipes.sh`, `recipes.ld`, `recipes_support.c` | the v1.2 recipe book's programs (RecipeTest); sw, RTL | gap | **S** · bytes · A11, A12 |
| A10 | the sort program: `sim/ref/sort/{program,split,skew}` (2.8 K lines of C, the skew planner's harness) and meshsim's copies `sw/ext/` with `tools/check_layout.sh` | the sort, the split, the planner; sw, reg | sort images in `golden-m2/images` | **S**: one copy (no `sw/ext` duplicate, so no layout check) · bytes (against the kit's `HASHES` and golden-m2's MANIFEST) · A12 |
| A11 | **the mesh.h generator**: `Registers.java` + `MeshHeader.java` (632 lines) write `sw/mesh.h`; `RegDoc.java` (338) writes REGISTERS.md's tables | the register map for every program; mesh.h and REGISTERS.md are what the RTL engineer and firmware authors read | gap (`src/units/regmap.h` is a separate hand-written table) | **M**: a non-Java generator from one register table, ideally the one `regmap.h` uses, so header and simulator can't drift · bytes (`mesh.h` and the REGISTERS.md tables identical to v1.3's) · Q2 |
| A12 | the firmware build | one command building every image above, `--check` comparing with the committed images; riscv64-unknown-elf-gcc and picolibc versions documented | per-folder scripts copying `ref` (`fw/build.sh`, `golden/build_images.sh`, `m3-images/build.sh`, `build_req.sh`) | **S** · bytes on every image · A2-A10 |
| A13 | the machine's programmer docs: `sw/CONVENTIONS.md`, `RECIPES.md`, `REGISTERS.md`, `mesh-v1.2-draft.h`, `regdoc/` | what firmware authors read; RecipeTest and RegDocTest check them against the sources | gap (in `ref/sw` only) | **S** (copy) · C12, C14 check them · A9, A11 |

## B. The chip and its units

| # | capability | what it is; who | today (and its evidence) | port |
|---|---|---|---|---|
| B1 | bus primitives (`Bus`, `Buses`, `WordBuses`): latch, delay line, the FIFOs and their rule, holds, register file, SRAM | every interaction's timing; all | **eq**: the framework's port kinds (`src/PORTS.md`); PortConf, 42 scripts × 10⁵ cycles (W3), `test_portconf` in make check | — |
| B2 | router, links, `Hops`, start skew | the mesh (spec 3, 4.6-4.7); RTL, all | **eq**: `router.*`, `net.h`; RouterConf (`test_router`), golden-m2 regs/wfi/a2a, the sorts (Quest 154) | — |
| B3 | injection point, SRAM read queue, tokens (`InjectionPath`, `SRAMReadQueue`) | 4.5; RTL | **eq**: `inject.*`; RouterConf, MemConf, the sorts | — |
| B4 | traffic director, controller memories' port B | 4.8, 4.11; RTL | **eq** on what the references reach (G1 replay, chip goldens, sorts); **unchecked**: mode 5-7 corner cases, strays, bad packets, read-port overlaps (AUDIT-M2 1.1's "never in the sort") | closed by C3, C8 |
| B5 | big SRAM (write combining included), write engine, table store and HIST | 4.1, 4.9, 4.10; RTL | **eq**: `big_sram.h`, `write_engine.h`, `table_store.h`; MemConf (a write-combining fixture), the sorts | — |
| B6 | address generator, GATHER engine | 4.2, 4.4; RTL | **eq**: `ag.h`, `gather.h`; EngConf, the sorts | — |
| B7 | the read port | 4.3; RTL | **eq** on the sort's SPLIT reads; **unchecked**: its overflow | closed by C7 |
| B8 | the controller (RV32I + Zmmul, traps, WFI, timing) | 4.11; RTL | **eq**: `rv32.h`; 67 golden traces, `fp-m1` every cycle | — |
| B9 | the register block (`TileControl`) | 8; RTL | **eq** on regs, wfi, a2a, the sorts, T2; **unchecked**: TRACE, counter wraps, done early, 128-bit sends beyond regs | closed by C8, C15 |
| B10 | C&C 1's Java sequencer (`CnC`) | the command sequences of the Java-driven runs; sw, reg | **eq**: `cnc.*`; CncConf, the sorts | — |
| B11 | C&C 1-3 as cores (`CcCores`, the controller router, units 64-66) | 6.2; RTL, proof | **eq**: boottrace (make check), T2 (Quest 170-171), the 128-segment chains (Quest 174) | — |
| B12 | C&C 1's peripheral (`Cnc1Peripheral`, `Uart`): the chip lines, toast, watchdog, UART | 6.6; RTL | **eq** with D33 (stores one cycle later); Cnc1Conf's three fixtures; **unchecked**: the UART's receive side (no source in any harness) | closed by C13 for the error lines and toast |
| B13 | request port and request bus | 3.3, 4.18; RTL | **eq** (D34: mode 0 exact, mode 3 the default): ReqConf's five fixtures, `test_cc_req`, T2's `lag_cycles` 0 | — |
| B14 | the peripheral slot, `EventCounts`, `TestPeripheral` | 4.17; RTL | **eq**: SlotConf, TileSlotConf (PeripheralSlotTest's groups on a real tile) | — |
| B15 | the replayer and its ALU | 4.19; RTL, proof | **eq**: ReplayConf (13 fixtures), ReplayerUnitTests ported (1,456 of 1,473 checks), Stage A on every window (Quest 164) | the 17 checks left: C20 |
| B16 | the Tracer, `RowCore`, `Rvc`, `MemorySystem`, `FileStore`, `LruCoder`, `BankedMemory` | 4.21; RTL, proof | **eq**: TracerConf, TracerParts, T1's calls (Quest 163), T2, the chains; **unchecked**: `reset()` and the separate map memory system (B1's sweep only; W14) | `reset()` by C23's cases if a v1.3 case reaches it, else noted |
| B17 | the chip: `Chip`, `Tile`, `Config.machine()` | the machine as specified; all | **eq**: `chip.cpp` `build_chip` (T2's machine, the sorts) | — |
| B18 | **coreless test machines** (`Config.defaults()`: tiles start in RUN with no controller, the GATHER engine a drain, engines configured from Java by `Tile.set*`) | the Java-driven suites' machine; reg | **partial**: `tile_cycle` without a control builder is a network-only stand-in; no engine-configuring or packet-feeding harness | in C1 |
| B19 | **the invariants of spec 9 and violation reporting** (`Chip.violation`, `maxViolationMessages`; FIFO overflows (1), the disjoint-buffer check (2), AG drift (3), HIST over 17 bits (6), invariant 8 strict, unexpected arrivals (9), ERRORS (15), read-port overlap, command words overwritten, item S1's K check (24)) | flags a program or model breaking the spec, per cycle, with cycle, tile and message; sw, reg | **partial**: units count their violations, the sort's harness sums them; no checker, no messages; **gap**: the disjoint-buffer check, S1's measurement, invariant 8 strict, the per-check switches | **M**: a chip-level checker, event-driven (D23), the messages as `ref`'s · log-equal on the suites that provoke violations (C3, C4, C8, C13) · C1 |
| B20 | errors end to end: ERRORS, the handler's reports, the error line, toast, **chip reset and reboot** (6.1, 6.2) | CondTest; RTL | **partial**: ERRORS by events, the error line and toast (Cnc1Conf); **gap**: the reset and the reboot | **M** · log-equal (CondTest) + fp across the reset · C1 |
| B21 | UB stops (`UndefinedBehavior`) | stops naming the UB; sw | **eq**: FATAL naming the UB (misalign goldens) | — |
| B22 | the retire-trace observer (`-Dmeshsim.observe`) | a tile's retire trace; dev, sw | **eq**: `trace_log.cpp`, `TRACE-FORMAT.md`; the 67 goldens | an option on every driver: H2 |
| B23 | idle-skip (`-Dmeshsim.skip`, default on) | ~18× on the full chip; all | **eq** by construction (ready-driven; the idle jump); off = the debug option step-all; goldens and sorts under step-all | — |
| B24 | shuffled child order (a seed per suite) | order independence; reg | **eq in purpose**: the debug shuffle within a level, by seed (its own generator, so seeds don't map to `ref`'s); every golden under seeds 1-3 | each ported suite runs shuffled too (C25) |
| B25 | the per-chip digest (`-Dmeshsim.digest=FILE`) | the proofs' fingerprint (WORKER28, 29); dev | **partial**: the canonical state fingerprint (`tests/statefp*`), equal to `ref`'s StateFp; not `Chip.digest`'s definition | **S**: StateFp offered by every driver as the fingerprint (Q5) · fp |

## C. The suites (`run_tests.sh`, quick and full)

| # | capability | what it is; who | today | port: estimate · check · depends |
|---|---|---|---|---|
| C0 | **the suite runner**: `run_tests.sh [all\|quick]` (layout check, the firmware builds, every step, logs in `results/`, the PENDING and FAIL summary), `tools/run_quick.sh`, `run_full.sh` (P at a time) | the regression; reg, RTL | gap (`make check` runs the unit tests; nothing runs suites) | **S-M**: `run_tests.sh` over the C++ suites, the same steps and log names · every step's log-equal check · every C row |
| C1 | **the machine harness** for suites: `Mach` (boot any image through C&C, run until, peek, poke), `Net` (per-tile packet queues on the read-return hold, arrival logs), `A2A` (the digit all-to-all driven from Java, its data sets), `T` (the pass/fail harness), the coreless machine (B18) | what every Java-driven suite is built on; reg, dev | **partial**: `golden_run` (images, boot ROM), `CncProgram`, the sort's and T2's harnesses, a2a's input | **M** · through the suites built on it · A1-A6 |
| C2 | BusTest | the bus primitives against their rules | covered by PortConf (B1); the suite itself not run | **S** · log-equal · C1 |
| C3 | NetTest (`-1`/`quick`/`full`, seeds 5, 11; `skewtable 40`) | links, routing, ordering, fan-in, the broadcast trees under skew, the fifth port, overflows; measured tables; RTL, reg | gap (RouterConf covers the router alone; NetTest "never run", M2's carried list) | **M-L** (607 lines) · log-equal (`net.log`, `skewtable.log`) · C1, B19 |
| C4 | MemTest (seeds 3, 8) | big SRAM, the read and write queues, `mem_idle`, the disjoint-buffer checker | gap (MemConf covers the units) | **M** · log-equal · C1, B19 |
| C5 | EngineTest (quick; `full`: full-size all-to-alls on four data sets, radix sort) | AG, write engine, table and HIST stores, augmenting, the 16-group all-to-all | gap (EngConf covers the engines; golden-m2's `engine.out`/`.dig` exist) | **M** · log-equal + fp on the all-to-all · C1, A5 |
| C6 | AgTest | AG read-ahead, the PTR/HIST port rule (figure 12) | gap | **M** · log-equal · C1 |
| C7 | ReadPortGatherTest (Java parts; `rdport.c`, `gather.c`; `full`: gather 1 at full size) | read port and GATHER | gap | **M** · log-equal · C1, A6 |
| C8 | CtrlTest (seeds -1, 4; `full`: the all-to-all from C on four data sets) | control, the controller, registers, invariants, from C (cmd, inv, regs, wfi, blockcopy, a2a ...) | **partial**: regs, wfi, a2a equal to golden-m2 (`test_chip`) | **M-L** · log-equal + fp per program · C1, A6, B19 |
| C9 | RiscvTest | the 48 riscv-tests on tile 0 of the machine | **eq**: their retire traces equal the goldens (`test_golden`, the `golden` tool) | runs as a suite step (C0) |
| C10 | AccountingTest (`full`) | PhaseLog's table against C&C's record; writes `results/phases-*.csv` | gap | **S** · bytes (the CSVs) + log-equal · H3, C5 |
| C11 | SearchBench | search steps through the read port: cycles per step | gap | **S** · log-equal · C1, A6 |
| C12 | RecipeTest (seeds -1, 4) + `SelfTest` | every recipe on 64 tiles, bit-exact, cycles; RECIPES.md's code blocks against their sources; sw | gap | **L** (751 lines) · log-equal + the snippet check · C1, A9, A13 |
| C13 | CondTest (seeds -1, 4) | the errors end to end, reset and reboot | gap | **M** · log-equal · B19, B20, A6 |
| C14 | RegDocTest | REGISTERS.md and mesh.h against their generators; `tools/header_diff.py` against the draft header | gap | **S** · log-equal · A11, A13 |
| C15 | HooksTest (seeds -1, 4) | the register map, the send port, the pulses, from Java | gap (TileSlotConf and the goldens touch parts) | **M** · log-equal · C1 |
| C16 | PeripheralSlotTest (seeds -1, 4) | the peripheral framework, events, DMA | **eq**: TileSlotConf runs its groups on a real tile, equal to `ref` (W15) | runs as a suite step (C0) |
| C17 | RequestBusTest (seeds -1, 4) | the bus's latency, ordering, fairness, credits; measured | **partial**: ReqConf (seeded traffic), `test_reqbus` | **S-M** · log-equal · C1 |
| C18 | CcCoresTest (seeds -1, 4) | C&C cores off and on, the ROM's controller branch, the link, the request tree | **partial**: `test_cc_units`, boottrace, `test_cc_req` | **S** · log-equal · C1, A7 |
| C19 | StageB (`synthetic`; `DELIVERY=` guest windows) | see E3 | — | E3 |
| C20 | ReplayerTest (the opcode table, the ALU against `vec.tsv`, ReplayerUnitTests) | the replayer from Java | **eq** except 17 of 1,473 checks (ConvertDelivery's and the 8 KB control) | **S** · the 17 checks · F3 |
| C21 | MemorySystemTest | FileStore's trees and statuses, the shared cache's timing | **partial**: TracerParts' memory fixtures | **S** · log-equal · — |
| C22 | RvcTest | `Rvc` against gold's decompressor on all 2¹⁶ encodings | **partial**: TracerParts' rvc fixture (`ref`'s Rvc, not gold's) | **S**: gold's 2¹⁶ expansions committed once as a fixture · bytes · — |
| C23 | TracerTest + TracerRig (the ideal controller) | the Tracer's ten directed cases, 180 assertions; RTL | **partial**: the C++ Tracer replays `ref`'s recorded calls of these cases (`tracerconf/cases.log.gz`); no C++ rig | **M**: TracerRig in C++, the cases and assertions · the assertions + the calls equal to the committed fixture · A4 |
| C24 | SortTest `p1`, `sort`, `sort` seed 4, the BT runs (kv32, k128 at 16K) | see D | — | D1-D4 |
| C25 | the shuffled-order steps (seeds 3, 4, 5, 8, 11) | order independence of every suite | — | each ported suite also run shuffled (B24) · output identical to the unshuffled run · its row |
| C26 | the layout check (`tools/check_layout.sh`) | `sw/ext/` against `../sort/` | — | dropped with the duplicate (A10) |

## D. The sort and its kit

| # | capability | what it is; who | today | port: estimate · check · depends |
|---|---|---|---|---|
| D1 | **SortTest `run`**: `kind=<builtin>\|keys=FILE`, `n=`, `fmt=k128\|kv32`, `shaped`, `label=`, `ready=N`, `cfg.<field>=`, `csv=`, `phases=`, `force=bt`, the seed; `-Dsort.build`, `sort.watch`, `sort.prof`, `sort.profile`, `sort.phases`, `sort.maxcycles` | one sort, measured: the log (path, stage 2, TOTAL, the step table), the per-run CSV, the phases CSV; sw, reg | **partial**: `test_sort` (M2Sort's sequence, uniform k128 at 16K and 64K, `SORT_*` variables, fp against golden-m2) and `bench_sort` (timing); no user driver, no CSVs | **M**: a `sort` driver with SortTest's arguments and outputs · log-equal and bytes (the CSVs) per run; fp at intervals where a run differs · A10, D5, F1, H1, H3 |
| D2 | the sort's other paths and forms: LB, TH, BT, PLAN, stage 2; kv32; shaped inputs; the oracle-split image | the sort as data drives it; sw | gap in checking (the units are those of the uniform sort; only uniform k128 has been compared) | **M**: one run per path at 64 × 2,000 and BT at 16K in both formats (`run_tests.sh`'s sort-bt steps) · fp at every GO + log-equal · D1, F1 |
| D3 | SortTest `p1` | the local sorts L1, L2, the keys all-to-all stood in | gap | **S** · log-equal · D1 |
| D4 | SortTest `sort [full] [only=]` | every built-in set at 64 × ≤ 2,000 (`run_tests.sh`), shuffled once | gap | **S** (on D1) · log-equal · D1, D2 |
| D5 | `Inputs`: the `inputs:` line (SHA-256 of the simulator, images, hops, keys) and `HASHES` | DEV 100's guard: every run names its exact inputs; reg | gap | **S** · bytes (every hash but the simulator's own) · A10 |
| D6 | SortSummary | the per-run CSVs → `SUMMARY.md` + `all.csv`, with the Inputs section | gap | **S** · bytes on v1.3's committed kit results and on fresh ones · D1, D5 |
| D7 | SortCompare, PhaseNeutral, VerifySummary, the verify kits (`meshsim/verify/`, `v1.3/verify-v1.0/`) | comparisons between simulator versions run by run and phase by phase (M7/M8, v1.0/M15); dev | gap | **S** each · bytes on committed inputs · D1 (Q3) |
| D8 | **the measurement kit** (`meshsim/kit/`: `assemble.sh`, `run.sh`, `jobs.txt`, `tools/make_jobs.py`, `runjob.sh`, `assemble_runner.sh`; built: `v1.3/kit/`; `v1.3/runner/` p4 and suites) | 92 runs (the 20-set catalogue in both formats, shuffled identities, a sweep), restartable, CSVs, SUMMARY; sw, reg | gap | **M**: the kit from C++ binaries (no jar), the same jobs and outputs · bytes (CSVs) on the `-n 2000` smoke against `sim/ref`'s kit run, then a full-size sample on the runner · D1, D6, F1 |
| D9 | SplitRun + `splitdrv` | the split on the machine: 25 sets, the cuts, S, cycles per search step | gap | **S-M** · log-equal · A10, C1, F2 |
| D10 | Sweep (`quick`, `full`; `results/sweep-*.csv`) | sensitivity of phase totals to the uncertain parameters | gap | **M** · bytes (the CSV) · C1, H1, H3 |

## E. The Tracer and the replay

| # | capability | what it is; who | today | port: estimate · check · depends |
|---|---|---|---|---|
| E1 | TracerT2 | a segment on the full chip, T2's acceptance; proof, RTL | **eq**: the `t2` tool (Quests 170-171, every point), the `chain` (all 128 segments of both workloads, Quest 174) | — (t2's `--sram`/`--files` come from W13's `ref` dumps; `chain --dump` makes the same forms: I7) |
| E2 | TracerT1 | the Tracer with the ideal controller over a run's segments on one memory, against the delivery and Stage A | **partial**: checked by replaying `ref`'s recorded calls (Quest 163); the chain checks the same streams and rows on the full chip | **S** on C23's rig · the windows equal the delivery (as T1) · C23 (Q6) |
| E3 | StageB (`synthetic`; a delivery's windows; the item-113 guests) | `replay_ctl.c` on one tile against a delivery, the Java loader landing the window per `replay_layout.h` | gap as a tool (replay_ctl runs on every tile in T2 and the chain, landed by C&C 3's DMA) | **M** · log-equal per window (rows, streams, end state, cycles) · A8, C1 |
| E4 | ReplayDelivery (Stage A) | the replayer model with the ideal controller, window by window | **eq**: `replay_delivery`, ref's lines exactly; every window of both workloads (Quest 164); **but** its window list comes from `ref`'s ReplayConf | **S**: read the manifest itself · cmp with v1.3's lines · — |
| E5 | **the acceptance kit**: ReplayShard, `acceptance/{run_shard,calibrate,make_kit,make_task,make_acceptance_task}.sh` | every window of a delivery, Stage A and B, the manifest's hashes, `results.csv`, `SUMMARY.txt`, `VERDICT.txt`; proof | gap | **M** · bytes (results.csv without wall time; VERDICT) on a synthetic delivery and a real range · E3, E4, F4 |
| E6 | ReplayTables, ReplayWindow | the tables and windows as delivered | **eq**: `replay_tables.*`, `tests/replay_window.h` | — |

## F. The input tools

| # | capability | what it is; who | today | port: estimate · check · depends |
|---|---|---|---|---|
| F1 | KeyGen | the sort's key sets without Python, bit-exact with numpy 2.x (SeedSequence, PCG64, Lemire, choice ...): the 20-set catalogue, `.keys` files; sw, reg | gap (the C++ reads KeyGen's files, checked by hash) | **M** · bytes on every set at 2,000, 16,384 and 65,536 per tile · — |
| F2 | SplitSets | the split's 25 data sets with the exact cuts | gap | **S** · bytes · F1 |
| F3 | ConvertDelivery | a v1.25 delivery to v1.26.1 records, manifest rewritten; proof (the input pipeline's `convert` step) | gap | **S** · bytes on a delivery's every record and manifest · — |
| F4 | SyntheticDelivery | a random RV64IMAC program expanded through the universal tables into a delivery with its own reference rows; tests and the acceptance kit's calibration | gap (W15: its last window isn't reproducible run to run) | **M** · bytes, window by window (the known window aside, Q7) · A4 |
| F5 | **the input pipeline's Java**: `v1.3/gold` (Jolt's tracer in Java, 4.8 K lines; segment files, frame 6), `v1.3/jumpmap` (0.7 K; jump table, initial image, per-segment pages), `v1.3/inputs/` (the pinned build calling both, ConvertDelivery and jolt-cpp) | every input the Tracer and the chain read; proof | gap (outside meshsim/) | **L** (gold), **S-M** (jumpmap), **S** (inputs/ re-pointed) · bytes at 2²⁹ on both workloads (`inputs/check.sh`'s manifests) · F3 (Q1) |
| F6 | `v1.3/tools/replayer-opcodes` (C++ harness against jolt-cpp, Python) | makes `vec.tsv` and the opcode tables (A4) | not Java: nothing to port | **S** (copy) · — · A4 |
| F7 | `v1.3/tracer/` (Python, C++: the template generator, inline rows) | the Tracer's templates and tables | not Java | **S** (copy) · — · A4 |
| F8 | `meshsim/guests/item113` | RV64 differential guests (built, to run through both tracers) | not Java | **S** (copy) · — · — |
| F9 | `v1.3/merge-inputs` (two diffs), `tools/v12_conv_stream.py`, `tools/logdiff.py` | history: the v1.2 port's conversions, the M10→M11 rename | — | Q3 |

## G. Benches

| # | capability | what it is; who | today | port |
|---|---|---|---|---|
| G1 | AgBench | the AG's read-ahead: ag_lookahead, zero-length groups, start to first request | gap | **S-M** · log-equal · C1, C6 |
| G2 | S1Bench | item S1: K's margin over the write path, L = 1-4, the table-window case | gap | **S** · log-equal · B19 (S1's measurement), A6 |
| G3 | CcBench | C&C as cores against the Java model: the boot and a phase | gap | **S** · log-equal · C1, A7 |
| G4 | the C++ project's own benches (`bench_controller`, `bench_threads`, `bench_chip`, `bench_sort`, `tracer_play`, `bench/w10`, `bench/quest`, `bench/w6`'s) | M1-M3's timings | v1.35's own | kept or cleaned up in PLAN step 3 (P8); `bench/w14/TracerPlay.java` moves out (I3) |

**ReplayBench** has no row: it lived only on the `sim-step-cost` branch, never in v1.3; T2's and the chains' timings
(Quests 171, 174) supersede it as the bar.

## H. Options and outputs

| # | capability | what it is; who | today | port: estimate · check · depends |
|---|---|---|---|---|
| H1 | **`Config`'s fields** (spec 10) | the machine's parameters; every suite, Sweep, `cfg.<field>=` | **in the C++ config structs** (`NetConfig`, `G1Config`, `SramConfig`, `ReqBusConfig`, `PeriphSlotConfig`): latencies (link, intra-tile, GO, READY, cm, rp, ts_engine, reg load, cc link), depths (queues, we input, gather, read port, area queues, tokens, loopback, cnc rx, request FIFO and queues, cc DMA), skew and its seed, K, ag_lookahead, mem_chunks, write combining, the FIFO rule, the loopback switches, done gate, cc cores, cc3 Tracer, peripheral 1; **refused at build**: error latency ≠ READY's, `cncGoSettle` off; **gap**: per-link latencies, per-tile start skew and peripheral types, stage_delay's staging (to verify: no v1.3 program stages), `testReadClients`, `counterStoreClearsLatch` off, the invariant switches (`strictReplies`, `checkDisjoint`, `checkReadPortOverlap`, `checkCommandRead`, `checkErrors`, `checkUnexpected`, `measureS1`, `maxViolationMessages`), `logTrace` | **M**: the gaps a suite, the kit or Sweep sets (Q4), and one `cfg.<field>=value` front end for every driver · through the suites and Sweep · B19, C1 |
| H2 | the system properties: `meshsim.skip`, `.K`, `.loopbackLast`, `.loopbackTokens`, `.loopbackDepth`, `.trace`, `.digest`, `.observe`; `replay.tables`, `replay.build`, `sort.hashes`, `stageb.profile`, `diag.addr` | run options; dev | partial (`SIM_REPLAY_TABLES`, the debug options, test variables) | **S**: the same options on the C++ drivers · each used once in a suite or tool · H1 |
| H3 | **PhaseLog** and the phases CSV (per C&C GO: cycles, the slowest tile, controller, engine, read-queue, utilisation, peak and C&C columns; `results/phases-*.csv`, SortTest `phases=`, the kit's `<id>.phases.csv`) | where a run's cycles went, phase by phase; sw, reg | **partial**: `test_sort` derives each phase's GO and end and checks them against `sort16k.phases.csv`; no other column | **M** · bytes (the CSV equal to v1.3's for the same run) · C1 |
| H4 | SortTest's per-run CSV (steps, counters, peaks) | the kit's raw data | gap | with D1 · bytes |
| H5 | `results/<step>.log` with PASS, FAIL, PENDING | the regression's record | gap | with C0 |
| H6 | the TRACE store log (`meshsim.trace`, 8.15) | firmware's printf-like trace | **partial**: stores counted, not logged | **S** · log-equal on a program that stores TRACE · — |
| H7 | profiles: tile 0's cycles per function (`sort.profile`, `sort.prof`), Stage B's (`stageb.profile`) | finding where firmware spends cycles; sw | gap | **S** · equal figures on one run · D1, E3 |
| H8 | the fingerprint and retire trace as outputs | B22, B25 | — | B22, B25 |

## I. What in `sim/` needs Java or `sim/ref` today (owner's Q3 group)

| # | what | needs | disposition |
|---|---|---|---|
| I1 | 15 test binaries (`test_golden`, `test_controllers`, `test_chip`, `test_sort`, `test_router`, `test_cc_units`, `test_cc_req`, `test_chain`, `test_t2_harness`, `test_tracer_parts`, `test_tracer_slot`, `test_replayer_alu`, `test_g1_replay`, `test_statefp_core`, `test_broken_units`) and the tools `golden`, `t2`, `chain`, `bench_chip`, `bench_sort`, `replay_delivery`'s default tables | data files read at run time from `ref/` (hops, boot ROM, riscv-tests images, tables, opcode table, vectors) | **committed files** (A1-A4); the paths repointed. Check: make check passes in a tree without `ref/` (W18's test, repeated) |
| I2 | `fw/build.sh`, `tests/golden/build_images.sh`, `tests/m3-images/build.sh`, `build_req.sh` | copy or read `ref`'s `sw/` | **repointed** to v1.35's own sources (A6-A8, A12) |
| I3 | the regenerators and conformance makers: `tests/golden/regen.sh`, `golden-m2/regen.sh`, `golden-m3/regen.sh`, `fp-m1/regen.sh`, `g1-replay/regen.sh`, every `tests/ref/*.sh` (`--check`, including `tracerconf.sh`, which makes TracerConfRig and TracerConfCases from TracerRig and TracerTest by sed), `make conformance`, `bench/w14/TracerPlay.java` | a JDK and `sim/ref` | **moved out of v1.35**: one oracle folder in `sim/` (proposed `sim/oracle/`: `tests/ref` and these scripts), left out of the copy at the freeze with `sim/ref`; every fixture they write stays committed in v1.35. The Makefile's `conformance` target goes with them |
| I4 | `sim/tests/ref/*.java` (the drivers: RouterConf, M2Sort, M3Seg, StateFp, ...) | Java | the oracle (owner's answer), with I3 |
| I5 | Quest task makers that embed `sim/ref` (`bench/w10/mk_task.sh` carries `hops.txt`, `sw/`, `test/`) and the session task scripts | `sim/ref` | `bench/`'s repointed with A (P1); the sessions' scripts are records in `planning/`, not run from v1.35 |
| I6 | the committed references (`tests/golden*`, `tests/*conf/`, `fp-m1`, `g1-replay`, `m3-images/*.ref*`) | made by `ref`, read by C++ | **committed fixtures**, kept; the large ones stay on the runner's data directory, named with their hashes (documented in P8) |
| I7 | `t2 --sram/--files` | W13's dumps, made by `ref`'s M3Seg | `chain --dump` writes the same forms in C++: documented as their source |

## J. Docs and build (PLAN-V135 step 3)

| # | what | port |
|---|---|---|
| J1 | v1.35's `README.md`, `BUILDING.md` (prerequisites: clang and gcc versions, riscv64-unknown-elf-gcc and picolibc, the riscv-tests checkout, python3 if Q2 allows), how to run every suite, driver, tool and kit, the outputs and where they go | **M** · followed on a fresh machine (a Quest task) · everything |
| J2 | stale paths and comments (`ref/...`, worker names in usage lines), leftover benchmarks (G4), `src/sketch/` | **S** · — |
