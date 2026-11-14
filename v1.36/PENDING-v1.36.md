# What is pending in v1.36

*For whoever continues v1.36: every test, suite, tool and behaviour that the move to the RTL's hardware has not reached
yet, why, and what closes it; and every latency where the simulator still differs from the RTL.* Nothing listed here was
weakened or deleted: a pending test is not built (`pending.mk`; `make pending` lists them), a pending suite step is
printed by `./run_tests.sh` as `PENDING` and counted in its summary, and its sources and reference logs stay as they
were. An item leaves this page in the commit that ports it.
v1.36's simulator is being brought to the RTL's hardware in stages (`spec/CHANGES-v1.35-to-v1.36.md`, section 2). The
platform stage has done the register map, the register bus, the tile control unit, the units' registers and the boot,
for one compute tile, and the core stage the core: `src/units/rv32.h` is the RTL's `rv32_core`, cycle for cycle, joined
to the tile through the register bus in the RTL's cycles; v1.35's core is gone. **Acceptance A**,
`tests/test_rtl_tile.cpp`, runs the RTL's own tile programs on that tile (`fw/rtl/tile/`), and checks the boot ROM's
cycles on it against the RTL's. The stages after it are named below as they are in the plan: the NoC and topology
(P6), the special tiles and the request tree (P7), the sort end to end (P8), the units' remaining behaviour (P9), the
old suites (P11), the register tables and firmware headers (P12), the replayer (17.8) and the Tracer (17.9).

## 1. Acceptance A: what of the RTL's tile test is pending

| what | why | what closes it |
|---|---|---|
| the replayer's row port at a random ready, and its handshake | the `replayer` program and `errors` 10 run and pass on v1.35's replayer behind the register bus (its region is 17.8.9's already; the rows are compared with 17.8.11's after the pass, from the sink's buffer, which is always ready) | the row port, with the replayer stage (17.8) |
| the `special` program (kind 4, `base_tile_special_test.rs`) | a special tile's base: its straps, its receive queue, a peripheral with a DMA | the special tiles (P7) |
| the RTL's model with all three attach ports at every tile id | a simulated tile has the attach port of its place in the chip (tile 0, 7 or 15: 2.1), so the runs at an attach tile use its one port | nothing: the RTL's model is a 1 × 1 chip's tile; the chip's attach ports are checked at chip level (P6) |
| the request link's beats and its handshake monitor; the row port's | the simulator's request unit hands over whole requests; the framework's ports have no ready or valid | the request tree (P7); the row port with the replayer |
| cycle-exact expectations | `base_tile_test.rs` has none but its bounds (the barrier program's delay, the timeouts), which the test keeps. The test adds the RTL's figures for the boot ROM on the block `core` (`rtl/blocks/core/README.md`, "The boot ROM's program") and the core specification's resume timing, checked on the tile in every run | |

## 2. Tests of `make check` that are not built (`pending.mk`)

`make pending` lists them. What was ported out of this list: `test_mach`, `test_ag`, `test_gather`, `test_mem` (their
commits say what changed in each); what each of them no longer checks is in the second table.

| what | why | what closes it |
|---|---|---|
| `test_regblock`, `test_golden`, `test_controllers`, `test_broken_units`, `test_statefp_core`, `test_g1_replay`, `test_statefp_g1`; the tools `golden`, `bench_controller` | v1.35's M1 bench (`src/units/controller.h`: v1.35's core with v1.35's register block) and its goldens: retire traces and fingerprints of v1.35's images, on v1.35's ISA, traps and map. v1.35's core left the tree with the core stage (`src/units/rv32_v135.h`, and its own tests `test_rv32` and `test_core_m2`, are deleted), so none of these can run again as written. They stay, unbuilt, only for what still includes them: `tests/golden_run.h` (which includes `controller.h`) is included by the pending chip tests `test_chip`, `test_sort`, `test_cc_units`, `test_g1_replay` and `tests/t2_harness.cpp`, and `controller.h` by `tests/statefp_g1.h` (`test_memconf`'s row) | deletion, with the port or replacement of the tests that include them (P11). What they checked of the core is checked on the RTL's core by `test_core_programs` (the RTL's 49 programs, CoreMark, the interrupt programs and the boot ROM, cycle for cycle) and `test_core_tile` (the core's join to the register bus) |
| `test_g1_tile`, `test_tile_slot`, `test_tileslotconf` | one tile's control part driven by v1.35's stand: GO, the command block, v1.35's registers and boot-ROM programs | superseded by `test_rtl_tile` for the tile; the slot's part with the peripherals (P7) |
| `test_engconf` | its four fixtures (`tests/engconf/t5-s1`, `t0-s2`, `t27-s3`, `t63-s4`) give 60–81 of their 256 `STREAM` entries a gap of 1–7 at random, and 38–51 of each fixture's 45–61 stream runs of two or more streams cross streams of different gaps; v1.36 has one gap a transfer (`DMA_CONFIG.gap`), so the scripts cannot be expressed in its layouts | the units' behaviour stage (P9), which changes these units' timing and needs new references in any case |
| `test_memconf` | its observations are v1.3's fingerprint streams (`srq`, `inj`, `sram`, `sram.data`, `we`, `ts`, `hist`) through `tests/statefp_g1.*`, which no longer compiles: the same file writes v1.35's core and register block (`w_core`, `w_ctl`, `w_cmem`, `w_req`, the M1 bench's). The memory path's units and the script are not the obstacle (its `DMA_CONFIG` digits and record base would be written as `RECEIVE_CONFIG` and `GATHER_CONFIG`) | `statefp_g1.*` cut to the writers of units that are still v1.35's, then the driver's `dma` words; or P9 with new references |
| `test_checkself` | ref's `CheckSelf.java` sequence provokes the checker kind by kind through v1.35's commands, replies, strays and a test's stores to v1.35's map (`Cc::command`, `TileRef::store`, `Mach::boot`: removed from `tests/mach.h`) | the checker's rules for v1.36's hardware, with the old suites (P11) |
| `test_chip`, `test_sort`, `test_cc_units`, `test_cc_req`, `test_t2_harness`; `test_stageb`, `test_p4suites`, `test_ctl_suites`, `test_sort_suites`; the tools `replay_shard`, `stageb` | v1.3's goldens and suites of the chip: v1.35's images under C&C's commands, C&C 1–3 as cores with `cc1.c`, v1.3's fingerprints of v1.35's state; the last four run the suite programs that `./run_tests.sh` has pending | the chip (P6–P8): the master, memory and tracer tiles and the RTL's images; then each test ported or replaced (P11) |
| `fw/mesh-v1.2-draft.h`, `tools/regs/header_diff.sh`, `tests/test_header_diff.sh` | v1.35's comparison of its generated header with its v1.2 draft; it means nothing for v1.36's map (the files are kept, unused) | deletion at the firmware port (P11) |
| meshsim's own firmware (`fw/boot`, `fw/cc`, `fw/m1`, `fw/recipes`, `fw/replay`, `fw/sort`, `fw/tests`, `fw/tracer`) and `fw/build.sh` | written for v1.35's map and commands; the sources no longer compile against the generated `fw/include/mesh.h` (v1.36's). The committed images still pass `fw/check_images.sh` | the firmware port (P11): each program ported to v1.36's map, or replaced by the RTL's in `fw/rtl/` |

Checks that left a ported test, and sources that are in no target any more:

| what | why | what closes it |
|---|---|---|
| `test_mach`: ref's `MachSelf.java` sequence (`tests/mach/machself.out`) | every case boots a v1.35 image through C&C's commands or C&C 1–3 as cores, and reads `SIM_EXIT`, `TRACE` and v1.3's phase log | the chip's boot with the master tile (P7, P8); the fixture stays |
| `test_mach`: ref's `NetSelf.java` cases but `packets`, `reads` and `a2a` (the first 47 lines of `tests/mach/netself.out`) | they drive v1.35's map through the core from the test, answer v1.35's `RESULT` requests, or print cycles that depend on v1.35's 128-entry request FIFO, its attach tiles 0–2 or the register block's view at the end of a cycle | the request tree (P7), the old suites (P11) |
| `test_mach`: the replayer's row hook (`Chip::on_row`) | v1.35's replayer's row listener | the replayer's row port (17.8) |
| `test_mem`: the case `block_sets_we_in_and_table_overflow_conditions_from_events` | `ERRORS` [4] and [5] of v1.35's register block; v1.36's `ERRORS` has no such bits (7.4), and `TileCtl` stops the simulator on those overflows (section 4) | P9: the FIFOs go |
| `tests/phase_log.*` (v1.3's per-phase accounting, `Chip::phases()`) | its phases are C&C's GO stores named by command words, its counters v1.35's (the command block's overwrites, the send's stalls in the core), its CSV v1.3's byte for byte | the accounting suite (P11) |
| `tests/statefp_g1.*`, and with it `Chip::digest()` and `MESHSIM_DIGEST` | ref's `StateFp` of v1.35's state (the core, the register block, the command block) | a fingerprint of v1.36's state, if one is wanted (the timing stage) |
| `tests/golden_run.*`, `src/units/controller.*` | v1.35's M1 bench and the golden-trace driver: v1.35's core is deleted, so neither compiles again; kept for the pending tests that include them (the first table's first row) | deletion with those tests (P11) |

## 3. Steps of `./run_tests.sh` that are pending

The runner prints each as `PENDING (<reason>)` and counts them (47 in `all`, 36 in `quick`; five more, `stageb-<guest>`,
with `DELIVERY` set); each keeps its v1.3 `step` line under it in `run_tests.sh`. The steps that run: `layout`, `bus`,
`rtl-tile` (acceptance A), `mem`, `mem-shuf`, `mem-shuf2`, `core-programs` and `core-tile` (the RTL's core:
`test_core_programs`, `test_core_tile`), `replayer`, `memsys`, `rvc`, `tracer`; with `--check`, `bus`, the three `mem`
steps, `replayer`, `memsys`, `rvc` and `tracer` are compared with their v1.3 references and equal them.

| steps | why | what closes it |
|---|---|---|
| `sw-build`, `sort-build`, `cc-build`, `recipes-build` | build checks of v1.35's firmware images, which v1.36's tile does not run | the RTL's images (vendored in `fw/rtl/`) for the sort end to end (P8); the firmware headers (P12) |
| `net`, `net-shuf`, `net-shuf2`; `skewtable` (*all*) | `suite_net`: NetTest checks v1.35's unexpected-packet and command conditions and its C&C attach tiles 0–2 | the NoC and topology (P6), then the old suites (P11) |
| `engine`, `eng-shuf`, `rdport`, `rdport-shuf`, `searchbench`, `agtest`, `agtest-shuf`; `rdport-full`, `agbench`, `engine-full` (*all*) | their suites drive v1.35's `DMA_CONFIG`, record base, pulses and read port through harness hooks that went with them | the units' behaviour (P9), then the old suites (P11) |
| `ctrl`, `ctrl-shuf`, `accounting`, `recipes`, `recipes-shuf`, `cond`, `cond-shuf`, `hooks`, `hooks-shuf`; `accounting-full`, `s1bench`, `ctrl-full` (*all*) | their suites run v1.35 firmware under C&C's commands (GO, DONE, the command block) | the sort end to end (P8), then the old suites (P11) |
| `riscv` | `test_golden`: the M1 goldens of v1.35's core, which is deleted. The step `core-programs` stands where it stood: the RTL's own riscv-tests on the RTL's core, in the RTL's cycles | the step's line goes with `test_golden` (P11) |
| `regdoc` | `REGISTERS.md` and `mesh.h` are v1.35's register map | the register tables and firmware headers (P12) |
| `periph`, `periph-shuf` | `test_tileslotconf`: boot-ROM programs on v1.35's map and peripheral window | the peripherals (P7) |
| `reqbus`, `reqbus-shuf` | `suite_reqbus`: v1.35's 128-entry request FIFO and its receive protocol through the harness | the request tree (P7) |
| `cccores`, `cccores-shuf`; `ccbench` (*all*) | v1.35's C&C 1–3 as cores with `cc1.c` and `cc23.c` | the special tiles (P7) |
| `replay-build`, `stageb`; `stageb-lrd_x0`, `stageb-scd_x0`, `stageb-nop_hint`, `stageb-jalr_self`, `stageb-jalr_zero` (`DELIVERY`) | v1.35's replay controller (`replay_ctl.c`) on v1.35's replayer and map | the replayer (17.8) |
| `sort-p1`, `sort`, `sort-shuf`; `sort-bt-kv32`, `sort-bt-k128`, `sweep-quick` (*all*) | v1.35's sort firmware under C&C's commands | the sort end to end (P8) |

## 4. Behaviour the simulator still has from v1.35

Each is a difference from the RTL that acceptance A does not reach, or reaches and survives. Where the old behaviour
would silently lose what the hardware keeps, the simulator stops (noted).

| what | the RTL | the simulator | closes |
|---|---|---|---|
| the write engine | a fixed pipeline, no input FIFO, never refuses (12.4) | v1.35's input FIFO (4) and stages; **an overflow stops the simulator** (`TileCtl`) | P9 |
| `PTR`, `HIST` | no request FIFOs, no overflow error (12.5) | v1.35's 16-entry request FIFOs; **an overflow stops the simulator** | P9 |
| the gather unit | while it is busy the address generator takes no read (14.1); `gather_busy` is "the queue holds a request or a request's last read not yet taken" (13) | the read arbiter's priority alone; `STATUS.gather_busy` is v1.35's OR of three inputs, in-flight reads included | P9 |
| the scratchpad | banks, a write wins its bank, `READ_LATENCY` fixed, reads return in take order (15) | v1.35's areas with request queues | P9 |
| the DMA unit | `DMA_CONFIG` latched at the start; the lead-ins of 8.2; an empty transfer busy for one cycle | read live at every step; v1.35's lead-ins (an empty transfer: as 8.2) | P9 |
| tokens | a loopback packet's token is freed when the director takes its copy (14.2) | v1.35's: when it leaves the loopback queue | P9 |
| the router | attach ports on the attach tiles only, the arbiters' order, a reserved dst dropped, no start skew (16) | v1.35's, with the attach tiles moved to 0, 7 and 15 | P6 |
| the peripheral slot | the control block of 17.1 (`EVENTS` 3, `EVENTS_PEEK` 4, `EVENTS_MASK` 5, `START` 6, the DMA in block 1), `DMA_HDR`'s layout | v1.35's slot behind the register bus, which presents 17.1's words (`TileCore::Env::slot_word`); `DMA_HDR`'s fields are v1.35's | P7 |
| the replayer | 17.8: `ERRORS` packed from bit 0, `EXPANSION_OFFSET` latched at the post, the underflow and leftover rules, the stall costs, the reset (`spec/CHANGES-v1.35-to-v1.36.md` 2.8) | v1.35's (4.19) behind 17.1's control block: its `ERRORS` bits read one position higher than 17.8.1's; no tile program reads them | the replayer stage |
| the special tiles | the master, memory and tracer tiles (2.1) | v1.35's C&C 1–3 as units 64–66 with their v1.35 peripherals; `default_straps` gives them the specification's straps | P7 |
| the checker | | v1.35's facts about hardware that is gone are no longer recorded: the command block, the reply block, the stray check, the table-window check of the core | P11 |

## 5. Timing left for the timing stage

Every latency or ordering where the simulator still differs from the RTL, with the RTL's figure. *q* is the cycle the
core raises an access in (its instruction leaves execute), *c* a cycle. The core's own cycles and its join to the
register bus are the RTL's and are not here: the pipeline's costs, a trap's entry, `WFI`, the canonical wait and
`core_status` (`test_core_programs`), a table access made in *q*, a window access in *q* + 1, a window store in effect at
the end of *q* + 1 and answered there, a store that may stall answered the cycle after its owner takes it, every access
fault answered in *q* + 1 (`test_core_tile`), `READ_SEND`'s read queued and `REQUEST_SEND`'s request pushed in the cycle
the store is made in, its stall judged there, and the boot ROM's cycles on the tile (`test_rtl_tile`).

| what | the RTL | the simulator | the RTL's source |
|---|---|---|---|
| a send's stall | `WRITE_SEND_*` is taken unless the holding register is full and not injected that cycle (`reg_ready = !holding_valid \|\| packet_ready`) | the packet is put on the holding register in the cycle the store is made in (injected in *q* + 2 at the earliest, as the RTL), but the write unit's side in `TileCore` holds up to two packets, the one being put and the next send's, so the core runs up to two sends ahead of a full holding register before it stalls | `rtl/blocks/write_unit/src/write_unit.veryl`, header "The stall (9)" and line 134; `rtl/tile/base/spec.md` 9, 14.3 |
| a peripheral's store that may stall (Ingress/Egress's `UART_TX`, `STREAM_SEND`, `STREAM_SEND_LAST`) | answered the cycle after its owner takes it, whether or not it waits | the simulator's slot says which words stall now (`Win::R_STALL`), not which may: a store raised while its word does not stall is answered in the cycle it is made in, a cycle early; one raised while it stalls, as the RTL's | `rtl/blocks/register_bus/src/register_bus.veryl`, header "Timing"; closes with the peripherals (P7), which give the bus each type's list (`TileCore::Env::bus_kind`) |
| a peripheral's live event or error | its level in cycle *c* raises the wake or the NMI in *c* + 1, a cycle after a load would see it | in *c* | `rtl/blocks/tile_control_unit/src/tile_control_unit.veryl`, header |
| the units behind the tile control unit | the write engine's pipeline, `TABLE_LATENCY`, `READ_LATENCY`, the DMA's lead-ins and the end of a transfer, the gather unit's reads, the router's queues and links | v1.35's (`ts_engine_latency`, `sram_read_latency`, `K` = `LAND_LATENCY` = 8 and `cm_latency` = `WRITE_PORT_LATENCY` = 2 already the RTL's) | `rtl/tile/base/spec.md` 8, 12–16; stages P6 and P9 |
| the nets | `BARRIER_ARRIVE` a flip-flop's pulse the cycle after the tile is ready; `BARRIER_RELEASE` reaches every tile in the same cycle | the pulse is put on `line` in that cycle; the chip's links for it and for the release are v1.35's (`ready_latency`, `go_latency`, the start skew) | `rtl/tile/base/spec.md` 7.3, 18; P7 |

## 6. The core

`src/units/rv32.h` and `rv32_decode.h` are the RTL's `rv32_core` (`tests/core-programs/README.md` says what it is
checked against). What it leaves out, and what no test in the tree checks:

| what | why | what closes it |
|---|---|---|
| The coprocessor port | The tile has no accelerator (the RTL builds none), so custom-0 and custom-1 are illegal instructions, as on a core with `coprocessor_present` 0. The RTL's `program_test` sets it to 1, but none of its programs has a coprocessor instruction. | The field accelerator, when the RTL has one. |
| An instruction or data port that holds ready low, or answers a fetch later than the next cycle | No memory of the tile does either (IMEM, DMEM, the boot ROM and `register_bus` accept in every cycle), so the model has neither; the RTL's test runs each program a second time with randomly delayed ports, which the model cannot follow. | Nothing in the tile; a unit that needs it. |
| The core's rules one by one (`test_rv32_rtl`: 62 directed tests of the RTL's core from the contract's sentences) | Written by a sub-agent of the core stage, which could not place the file in `tests/`, so it is not in the tree: it is in the core stage's scratch folder, `build-scratch/rtl-tests/test_rv32_rtl.cpp` (untracked, outside `v1.36/`), written against the core stage's interface (`v135_platform`, v1.35's tile environment), which is gone. What the RTL's programs do not reach and only that file checks: a CSR write to `mcycle` or `minstret`, `minstret` on a trapped instruction, the store-misaligned cause, a data target slower than one cycle on a bus, and the exact cycle `core_status_canonical_wait` rises. The last is not caught by acceptance A either: with the level taken from `wfi_sleeping` alone, `test_rtl_tile` still passes (the barrier program bounds the arrival from below by 2,000 cycles only). | The owner's or coordinator's decision to add the file as `tests/test_rv32_rtl.cpp`, ported to the tile's environment as it is now (`tests/test_core_tile.cpp` shows it), after reviewing it; or the RTL's cycle for the rise of `canonical_wait` after a `WFI`, measured on the RTL, as a check of `test_core_programs`. |
