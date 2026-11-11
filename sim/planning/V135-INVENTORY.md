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
