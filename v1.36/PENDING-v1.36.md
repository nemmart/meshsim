# What is pending in v1.36

*For whoever continues v1.36: every test, suite, tool and behaviour that the move to the RTL's hardware has not reached
yet, why, and what closes it; and every latency where the simulator still differs from the RTL.* Nothing listed here was
weakened or deleted: a pending test is not built (`pending.mk`; `make pending` lists them), a pending suite step is
printed by `./run_tests.sh` as `PENDING` and counted in its summary, and its sources and reference logs stay as they
were. An item leaves this page in the commit that ports it.

v1.36's simulator is brought to the RTL's hardware in stages (`spec/CHANGES-v1.35-to-v1.36.md`, section 2), a section
of this page each. Where they stand:
- **the core** (`src/units/rv32.h`): the RTL's `rv32_core`, cycle for cycle; v1.35's core and the option that kept
  it (`v135_platform`, `src/units/rv32_v135.h`) are gone from the tree, and nothing needs them;
- **the replayer** and **the Tracer** (`src/units/replayer.*`, `tracer.*`): the RTL's `replayer_unit` and
  `tracer_unit`, cycle for cycle at the block level, each in the peripheral slot of its tile;
- **the platform**: the register map, the register bus, the tile control unit, the units' registers, the boot, the
  peripheral slot (17.1–17.5), and the chip: the 64 compute tiles, the master, memory and tracer tiles, the request
  tree and the nets. **Acceptance A**, `tests/test_rtl_tile.cpp`, runs the RTL's own tile programs on one tile
  (`fw/rtl/tile/`) and checks the boot ROM's cycles against the RTL's; **acceptance B**, `tests/test_rtl_sort.cpp`,
  runs the RTL's sort images end to end on the chip (`fw/rtl/sort/`), four runs, against the oracle.

What is left is named below as in the plan: the units' remaining behaviour (P9), the memory tile's RW engine, the host
interface and the segment on the chip (P10), the old suites (P11), meshsim's own firmware (P12), and the timing stage.

## The core stage

`src/units/rv32.h` and `rv32_decode.h` are the RTL's `rv32_core` (`tests/core-programs/README.md` says what it is
checked against). What it leaves out, and what no test in the tree checks:

| what | why | what closes it |
|---|---|---|
| The coprocessor port | The tile has no accelerator (the RTL builds none), so custom-0 and custom-1 are illegal instructions, as on a core with `coprocessor_present` 0. The RTL's `program_test` sets it to 1, but none of its programs has a coprocessor instruction. | The field accelerator, when the RTL has one. |
| An instruction or data port that holds ready low, or answers a fetch later than the next cycle | No memory of the tile does either (IMEM, DMEM, the boot ROM and `register_bus` accept in every cycle), so the model has neither; the RTL's test runs each program a second time with randomly delayed ports, which the model cannot follow. | Nothing in the tile; a unit that needs it. |
| The core's rules one by one (`test_rv32_rtl`: 62 directed tests of the RTL's core from the contract's sentences) | Written by a sub-agent of the core stage, which could not place the file in `tests/`, so it is not in the tree: it is in the core stage's scratch folder, `build-scratch/rtl-tests/test_rv32_rtl.cpp` (untracked, outside `v1.36/`), written against the core stage's interface (`v135_platform`, v1.35's tile environment), which is gone. What the RTL's programs do not reach and only that file checks: a CSR write to `mcycle` or `minstret`, `minstret` on a trapped instruction, the store-misaligned cause, a data target slower than one cycle on a bus, and the exact cycle `core_status_canonical_wait` rises. The last is not caught by acceptance A either: with the level taken from `wfi_sleeping` alone, `test_rtl_tile` still passes (the barrier program bounds the arrival from below by 2,000 cycles only). | The owner's or coordinator's decision to add the file as `tests/test_rv32_rtl.cpp`, ported to the tile's environment as it is now (`tests/test_core_tile.cpp` shows it), after reviewing it; or the RTL's cycle for the rise of `canonical_wait` after a `WFI`, measured on the RTL, as a check of `test_core_programs`. |

## The replayer stage

The replayer's model is the RTL's `replayer_unit` at the block level: its rows, its cycles and every port's
observations equal the RTL's (`tests/replayer/README.md`, `tests/acceptance/README.md`). In the chip it is every
compute tile's peripheral 1, in the slot (`src/units/periph_slot.h`, "THE CONTRACT"): its control block's words are
17.1's, its write port lands as the RTL's, and acceptance A's `replayer` program passes on it with the row port at a
random ready.

| what | why | what closes it |
|---|---|---|
| `test_slotconf` (`tests/slotconf/seed1-5`) | the slot's conformance replays ref's seeded scripts, which store and load v1.35's control-block words and reserved words at random, on a slot with the replayer in it; its reference is ref's observations of v1.35's replayer and v1.35's DMA under them (busy, errors, events and queue occupancies cycle by cycle), which neither the RTL's replayer nor 17.1's slot gives | a conformance of the slot against the RTL's `peripheral_control` and `peripheral_dma` (scripts replayed on the RTL's blocks in a Snarky One scratch worktree, as `tests/replayer/` does for the replayer). Until then the slot's rules are tested from the specification and the RTL's headers in `test_periph_slot` (the DMA's cycles, a set in a consuming load's cycle, the DMA block), and the replayer's side by `test_replayconf` |
| Stage B and Stage F (`test_stageb`, the tools `stageb` and `replay_shard`, the steps `replay-build`, `stageb`, `stageb-<guest>`; `tests/stageb/stageb.log`, the acceptance kit's B and F) | they run v1.35's replay controller (`fw/replay/replay_ctl.c`) on v1.35's tile, map and commands through `tests/mach.h`'s old machine (`Mach::boot`, C&C's commands), which the platform replaced: they passed on the replayer stage's branch only because its tile was still v1.35's. Not built (`pending.mk`) | a replay controller on v1.36's map: the RTL's replay image (`rtl/chip/test/programs/firmware`, `replay.elf`) on the chip with the master's and the tracer tile's (the segment's stage, P10), then the RTL chip's figures (3.0875 and 3.1249 cycles a row) |
| the row port's handshake | the model's consumer is its sink, ready one cycle in `sink_interval` (1: always, as the chip ties it); no unit reads `replay_row` | the materializer, *Open* (17.8.11) |
| `spec/CHANGES-v1.35-to-v1.36.md` 5.2.6 item 1: the timing rules the specification does not state | the model takes them from the RTL's sources: they are listed in `src/units/replayer.h`'s header and in the stage's report | a ruling: write them down (the guide suggests an appendix), or leave them to the RTL |

## The Tracer stage

The Tracer's model is the RTL's `tracer_unit` at the block level (`tests/tracerblock/`, `tests/test_tracer_rules.cpp`).
In the chip it is the tracer tile's peripheral 1, in the slot with the DMA (`src/units/periph_slot.h`, "THE
CONTRACT"): 17.1's control and DMA blocks, `dma_done` the slot's (bit 1), the DMA's cycles `peripheral_dma.veryl`'s,
a consuming load applied so that a set of its cycle survives it (`test_tracer_slot`, `test_periph_slot`). On the chip
the tracer tile boots its ROM and waits for a load the sort's master never answers: no test runs a segment there yet.

### Pending

| What | Why | What closes it |
| --- | --- | --- |
| `chain` (`tests/chain_main.cpp`) and `t2` (`tests/t2_main.cpp`), with `test_t2_harness` and `tests/t2_harness.*`: the segment on the full chip | They boot v1.35's tracer-tile firmware (`fw/tracer/tracer_ctl.c`) on v1.35's C&C 3 through `tests/mach.h`'s old machine, which the platform replaced (the tracer tile is a base tile with the RTL's boot ROM now; its `EVENTS` bits are 17.9.1's, packed from bit 0, as `fw/include/mesh.h`'s `TR_EV_*` already are: `tools/regs/regtable.h`). Neither tool is built: `t2` is a target of `src/units/cc.mk` outside `all`, `chain` of `tests/chain.mk`, and both need `tests/t2_harness.cpp`, which includes the deleted M1 bench. They are in no suite (`RUNNING.md`, "by hand"), nor are `tests/freeze/body.sh`'s items 3 and 4, which run them | The segment on v1.36's chip (P10): the RTL's tracer-tile, master and replay images (17.9.10, 17.9.15: the handshakes by requests) and the memory system on the fill port, as the RTL's `chip_trace_test` |
| `tests/golden-m3/` (`RUNS`, `*.chip`, `boot/*.boot.fp.gz`) and `tests/golden-chain/`: T2's and the chain's recorded figures and fingerprints | They are v1.35's Tracer's on the full chip, cycle for cycle (`tr`, `tr.ic`, `tr.dc`, `tr.mem`, `tr.files` at every 100,000 cycles); no test in a suite reads them | Regenerated with the segment's stage, from the RTL's chip figures where it has them |
| `tracer_block`'s step `tracer-block` without the workloads' data | The real segments are read from `$MESHSIM_INPUTS` (`~/meshsim-inputs`), outside the repository; without it the runner prints the step as PENDING. `tracer-block-synth` always runs | The data on the host |
| `tools/freeze/strip.sh` line 49 names `bench/tracer/tracer_play.cpp` and `bench/tracer/tracer.mk` | Both are retired (below) | The freeze tool's owner drops the pair |
| The memory system's model with a read and a write outstanding together (item 174) | Decided here, for the simulator only: the model serves one access at a time, so a request taken while another is being served starts when that one ends (`MemSystem::port_read`, `port_write`); a write to a line that is not the file's own answers `error` a cycle after its handshake | The memory system's design (items 60, 119) |

### Retired, with what replaces it

The Java reference's Tracer (`sim/ref/meshsim`, outside v1.36) modelled v1.35's hardware cycle for cycle, and its logs were
the Tracer's conformance fixtures. The RTL (Snarky One's `tracer_unit`) is now the hardware, the Java model cannot be made
to follow it, and the fixtures cannot be regenerated. The RTL's own figures and the specification replace them:
`tests/tracerblock/rtl/` (the RTL's per-run cycles and counters for three real segments, which `tracer_block` must equal),
`tests/test_tracer_rules.cpp` (17.9's rules, each from the specification or the RTL's source), and the ported suite.

| Retired | Was |
| --- | --- |
| `tests/tracerconf/`: `cases.log.gz`, `mix-s1`, `mix-s3`, `mix-s4`, `mix-s5`, `mix-s9`, `mix-s16`, `mix-s21`, `wb-s1`, `wbover-s1` (`.log.gz`) | TracerConf's call logs of the Java Tracer |
| `test_tracer.cpp`: `tracer_test_cases_equal_ref_every_cycle`, `tracerconf_mix_seed1`, `_seed3`, `_seed4`, `_seed5_unimp`, `_seed9_store_hole`, `_seed16`, `_seed21_range`, `tracerconf_wb_seed1_buffer_past_depth`, `tracerconf_wbover_seed1_ring_overflow`, `stream_from_tracerconf_stream`, `long_runs_from_tracerconf_long` | The replay of those logs, call by call |
| `test_tracer_rig.cpp`: `rig_calls_equal_refs_rig`; `RecTracer` and `suite_tracer --calls` | The rig's calls against the Java rig's |
| `bench/tracer/tracer_play.cpp`, `bench/tracer/tracer.mk` | A replay of the same logs, for speed |

### Notes: what the Tracer's model takes from the RTL where the specification is silent or differs

1. **The rows' end** is the cycle after the last row completes: the control sees `row_done` through a register
   (`tracer_unit.veryl`); 17.9.3 says "the cycle in which the run's last row completes".
2. **After a stopping error** the scans and a transfer in progress go on; only the events, the rows' end and the run's end
   are withheld (`tracer_control.veryl`, `tracer_run.veryl`); 17.9.9 says "drains nothing more".
3. **A NOP policy** costs 2 cycles before its row (`replayer_fetch.veryl`: the policy's read, then ROM word 1); the RTL's
   Tracer README and inventory B6 say 1.
4. **A core-fed instruction** costs a cycle before its first row; its successor's fetch follows its last word's pop, which
   for an expansion of several words comes rows ahead and costs nothing.
5. **A redirect to pc 0, and a run's first padding row**: the padding word is fetched from nowhere, a cycle sooner than a
   fetch's hit (`tracer_instruction.veryl`'s `issue_padding`): 4 past the schedule, and 5 after START's effect.
6. **A run's first row inside an expansion** starts 3 cycles after START's effect (the fetch holds its words).
7. **An instruction miss** costs its fill's latency + 3, **a data miss** + 5 (+ the wait for the write-back buffer's
   place), **a probe** of n lines n × (the map's latency + 4) + 2 (`tracer_fetch.veryl`, `tracer_data.veryl`,
   `tracer_fill.veryl`, `tracer_memory.veryl`, `tracer_jump_lookup.veryl`).
8. **The write-back register**: a row's rd lands in the register file a cycle late, on the port the next row's rs2 read
   uses, so a row whose word arrives exactly the cycle after its predecessor's completion starts a cycle later
   (`tracer_rows.veryl`, `write_back_pending`).
9. **`state_ready`** rises 3 cycles after the last row completes (the registered `row_done`, the output's input register,
   the choice); **done** comes 2 cycles after the DMA's last pop (`peripheral_dma.veryl`'s busy).
10. **A fetch's read waits** while a data access or a map read is presented or in progress, and the fill port's one read
    goes to the data access, then the map, then the fetch (`tracer_memory.veryl`, `tracer_fill.veryl`).
11. **The record's spare bits** are not held: the state word's [127:112], the stream word's [127:125], the files word's
    [127:103]; register 0 reads 0 (`tracer_record.veryl`, `tracer_rows.veryl`); 17.9.2 says "spare, 0" of the files word only.
12. **A row that moves pc without `next_from_value`** (UB, 17.8.7) runs on sequentially, as the RTL's ALU; meshsim flags it.
13. **A row that is not its expansion's last keeps its pc** whatever its opcode (inventory A9.4): gated in `tracer.cpp`,
    not in the shared `replayer_alu.h`.
14. **A non-contiguous `VirtualPext`** takes a cycle for each bit of its mask up to the highest set one, + 2
    (`replayer_alu.veryl`).
15. **A key of 0xFFFF_FFFF** matches a free entry and takes its base, as v1.35's probe did (`tracer_jump_lookup.veryl`).

## The platform stage

### 1. Acceptance A: what of the RTL's tile test is pending

| what | why | what closes it |
|---|---|---|
| the `special` program (kind 4, `base_tile_special_test.rs`) | a special tile's base alone, with every kind of peripheral interface behind a test peripheral; the simulator's special tiles are built and run only in the chip (acceptance B boots the master's and the memory tile's and loads through the tracer tile's ROM) | a one-tile bench for a special tile (its straps, its receive queue with the root's credit, a peripheral with a DMA) running `fw/rtl/tile/base_tile/special.elf` |
| the RTL's model with all three attach ports at every tile id | a simulated tile has the attach port of its place in the chip (tile 0, 7 or 15: 2.1), so the runs at an attach tile use its one port | nothing: the RTL's model is a 1 × 1 chip's tile; the chip's attach ports are checked at chip level (P6) |
| the request link's beats and its handshake monitor; the row port's monitor | the simulator's request unit hands over whole requests, and the framework's ports have no ready or valid to watch. The bench takes a request's eight beats one a cycle at the RTL test's random ready, and the replayer's rows leave the row port at its random ready | nothing in the simulator's ports; the request tree's beats are the chip's (11.3) |
| cycle-exact expectations | `base_tile_test.rs` has none but its bounds (the barrier program's delay, the timeouts), which the test keeps. The test adds the RTL's figures for the boot ROM on the block `core` (`rtl/blocks/core/README.md`, "The boot ROM's program") and the core specification's resume timing, checked on the tile in every run | |

### 2. Acceptance B: what of the RTL's chip tests is pending

`tests/test_rtl_sort.cpp` is `chip_test.rs` (the sort, four runs). The RTL's other chip tests have no counterpart:

| what | why | what closes it |
|---|---|---|
| `chip_memory_test`: the master's requests to the memory tile and LPDDR | the memory tile's peripheral 1 is a stand-in that stops the simulator if it is started, written or made to send (`RwEngineStub`, `src/units/ingress_egress.h`): the RW engine (17.7), its write buffer and its AXI4 port are not modelled | the RW engine's model (P10), with the RTL's AXI4 slave model's timing |
| `chip_stream_test`: the master echoing beats on its AXI4-Stream ports | Ingress/Egress's host interface (17.6) is not modelled: its receive side reads empty and a store to its transmit side stops the simulator; the UART has a transmit FIFO only | the host interface's and the UART's receive side in `IngressEgress` (P10) |
| `chip_trace_test`, `chip_replay_test`: a segment from the Tracer to the replayers | no test boots the tracer tile's image, and the Tracer's memory system on the chip is the harness's (`Tracer::own_memory`), not placed by any chip test | the segment on the chip (P10; the Tracer stage's first row) |
| the cycle counts of the sort | printed beside the RTL's, not asserted (the table in section 6) | the timing stage |

### 3. Tests of `make check` that are not built (`pending.mk`)

`make pending` lists them. What was ported out of this list: `test_mach`, `test_ag`, `test_gather`, `test_mem`,
`test_router`, `test_periph_slot`, `test_tracer_slot`, `test_cnc` (their commits say what changed in each); what each
of them no longer checks is in the second table.

| what | why | what closes it |
|---|---|---|
| `test_regblock`, `test_golden`, `test_controllers`, `test_broken_units`, `test_statefp_core`, `test_g1_replay`, `test_statefp_g1`; the tools `golden`, `bench_controller` | v1.35's M1 bench (`src/units/controller.h`: v1.35's core with v1.35's register block) and its goldens: retire traces and fingerprints of v1.35's images, on v1.35's ISA, traps and map. v1.35's core left the tree with the core stage (`src/units/rv32_v135.h`, and its own tests `test_rv32` and `test_core_m2`, are deleted), so none of these can run again as written. They stay, unbuilt, only for what still includes them: `tests/golden_run.h` (which includes `controller.h`) is included by the pending chip tests `test_chip`, `test_sort`, `test_cc_units`, `test_g1_replay` and `tests/t2_harness.cpp`, and `controller.h` by `tests/statefp_g1.h` (`test_memconf`'s row) | deletion, with the port or replacement of the tests that include them (P11). What they checked of the core is checked on the RTL's core by `test_core_programs` (the RTL's 49 programs, CoreMark, the interrupt programs and the boot ROM, cycle for cycle) and `test_core_tile` (the core's join to the register bus) |
| `test_g1_tile`, `test_tile_slot`, `test_tileslotconf` | one tile's control part driven by v1.35's stand: GO, the command block, v1.35's registers and boot-ROM programs | superseded by `test_rtl_tile` for the tile and by `test_periph_slot` for the slot; deletion with the old suites (P11) |
| `test_engconf` | its four fixtures (`tests/engconf/t5-s1`, `t0-s2`, `t27-s3`, `t63-s4`) give 60–81 of their 256 `STREAM` entries a gap of 1–7 at random, and 38–51 of each fixture's 45–61 stream runs of two or more streams cross streams of different gaps; v1.36 has one gap a transfer (`DMA_CONFIG.gap`), so the scripts cannot be expressed in its layouts | the units' behaviour stage (P9), which changes these units' timing and needs new references in any case |
| `test_memconf` | its observations are v1.3's fingerprint streams (`srq`, `inj`, `sram`, `sram.data`, `we`, `ts`, `hist`) through `tests/statefp_g1.*`, which no longer compiles: the same file writes v1.35's core and register block (`w_core`, `w_ctl`, `w_cmem`, `w_req`, the M1 bench's). The memory path's units and the script are not the obstacle (its `DMA_CONFIG` digits and record base would be written as `RECEIVE_CONFIG` and `GATHER_CONFIG`) | `statefp_g1.*` cut to the writers of units that are still v1.35's, then the driver's `dma` words; or P9 with new references |
| `test_checkself` | ref's `CheckSelf.java` sequence provokes the checker kind by kind through v1.35's commands, replies, strays and a test's stores to v1.35's map (`Cc::command`, `TileRef::store`, `Mach::boot`: removed from `tests/mach.h`) | the checker's rules for v1.36's hardware, with the old suites (P11) |
| `test_cnc1_periph` | v1.35's conformance of C&C 1's peripheral against ref (`READY_LINES`, `PULSE_GO`, the row collectors' words, a toast pin on any error line); v1.36's master tile has Ingress/Egress (17.6) | a test of `IngressEgress` from 17.6 and the RTL's `ingress_egress_unit` (it is exercised only through the chip now: `test_rtl_sort`, `test_mach`); then deletion (P11) |
| `test_slotconf`; `test_stageb` and the tools `replay_shard`, `stageb`; `test_t2_harness` | the replayer stage's and the Tracer stage's rows above | there |
| `test_chip`, `test_sort`, `test_cc_units`, `test_cc_req`; `test_p4suites`, `test_ctl_suites`, `test_sort_suites` | v1.3's goldens and suites of the chip: v1.35's images under C&C's commands, C&C 1–3 as cores with `cc1.c`, v1.3's fingerprints of v1.35's state; the last three run the suite programs that `./run_tests.sh` has pending. What they checked of the chip end to end is checked on v1.36's by `test_rtl_sort` | each test ported to v1.36's chip or deleted (P11) |
| `fw/mesh-v1.2-draft.h`, `tools/regs/header_diff.sh`, `tests/test_header_diff.sh` | v1.35's comparison of its generated header with its v1.2 draft; it means nothing for v1.36's map (the files are kept, unused) | deletion at the firmware port (P11) |
| meshsim's own firmware (`fw/boot`, `fw/cc`, `fw/m1`, `fw/recipes`, `fw/replay`, `fw/sort`, `fw/tests`, `fw/tracer`) and `fw/build.sh` | written for v1.35's map and commands; the sources no longer compile against the generated `fw/include/mesh.h` (v1.36's). The committed images still pass `fw/check_images.sh` | the firmware port (P11): each program ported to v1.36's map, or replaced by the RTL's in `fw/rtl/` |

Checks that left a ported test, and sources that are in no target any more:

| what | why | what closes it |
|---|---|---|
| `test_mach`: ref's `MachSelf.java` sequence (`tests/mach/machself.out`) | every case boots a v1.35 image through C&C's commands or C&C 1–3 as cores, and reads `SIM_EXIT`, `TRACE` and v1.3's phase log | deletion with the old suites (P11): the chip's boot is `test_rtl_sort`'s; the fixture stays until then |
| `test_mach`: ref's `NetSelf.java` cases but `packets`, `reads` and `a2a` (the first 47 lines of `tests/mach/netself.out`) | they drive v1.35's map through the core from the test, answer v1.35's `RESULT` requests, or print cycles that depend on v1.35's 128-entry request FIFO, its attach tiles 0–2 or the register block's view at the end of a cycle | the old suites (P11) |
| `test_mach`: the replayer's row hook (`Chip::on_row`) | v1.35's replayer's row listener; the RTL's replayer gives its rows to its sink (`set_row_buffer`, `set_row_times`), which `test_rtl_tile` reads | a consumer of `replay_row` in the chip, when a test needs one (the segment, P10) |
| `test_router`: ref's three runs of a controller's router, and each run past its first packet for a reserved id | v1.36's special tile's router has no host port, drops a packet for a reserved id (16.2) and stamps the tile's own id (16.4); the whole runs are compared with references REGENERATED from the simulator for those two rules (`tests/routerconf/v1.36/README.md`) | a conformance of the router against the RTL's `router` (`router_test`'s model, or scripts replayed on the RTL) |
| `test_mem`: the case `block_sets_we_in_and_table_overflow_conditions_from_events` | `ERRORS` [4] and [5] of v1.35's register block; v1.36's `ERRORS` has no such bits (7.4), and `TileCtl` stops the simulator on those overflows (section 4) | P9: the FIFOs go |
| `tests/phase_log.*` (v1.3's per-phase accounting, `Chip::phases()`) | its phases are C&C's GO stores named by command words, its counters v1.35's (the command block's overwrites, the send's stalls in the core), its CSV v1.3's byte for byte | the accounting suite (P11) |
| `tests/statefp_g1.*`, and with it `Chip::digest()` and `MESHSIM_DIGEST` | ref's `StateFp` of v1.35's state (the core, the register block, the command block) | a fingerprint of v1.36's state, if one is wanted (the timing stage) |
| `tests/golden_run.*`, `src/units/controller.*` | v1.35's M1 bench and the golden-trace driver: v1.35's core is deleted, so neither compiles again; kept for the pending tests that include them (the first table's first row) | deletion with those tests (P11) |

### 4. Steps of `./run_tests.sh` that are pending

The runner prints each as `PENDING (<reason>)` and counts them (48 in `all`, 36 in `quick`; five more,
`stageb-<guest>`, with `DELIVERY` set; one less in `all` with `RTL_SORT_64K_KEYS` set, one more in either without the
workloads' data at `MESHSIM_INPUTS`); each of v1.3's keeps its `step` line under it in `run_tests.sh`. The steps
that run: `layout`, `bus`, `rtl-tile` (acceptance A), `rtl-sort` (acceptance B, the 16-record runs and the run split
over threads; `rtl-sort-64k` in `all`), `mem`, `mem-shuf`, `mem-shuf2`, `core-programs` and `core-tile` (the RTL's
core), `replayer`, `memsys`, `rvc`, `tracer`, `tracer-block-synth`, `tracer-block`; with `--check`, `bus`, the three
`mem` steps, `replayer`, `memsys`, `rvc`, `tracer` and the two `tracer-block` steps are compared with their
reference logs and equal them.

| steps | why | what closes it |
|---|---|---|
| `sw-build`, `sort-build`, `cc-build`, `recipes-build` | build checks of v1.35's firmware images, which v1.36's tile does not run; the images the chip runs are the RTL's, vendored with their hash list (`fw/rtl/`, `fw/check_images.sh`'s part `rtl`) | meshsim's own firmware ported to v1.36's map, or deleted (P12) |
| `net`, `net-shuf`, `net-shuf2`; `skewtable` (*all*) | `suite_net`: NetTest checks v1.35's unexpected-packet and command conditions, its C&C attach tiles 0–2 and its start skews | the old suites (P11) |
| `engine`, `eng-shuf`, `rdport`, `rdport-shuf`, `searchbench`, `agtest`, `agtest-shuf`; `rdport-full`, `agbench`, `engine-full` (*all*) | their suites drive v1.35's `DMA_CONFIG`, record base, pulses and read port through harness hooks that went with them | the units' behaviour (P9), then the old suites (P11) |
| `ctrl`, `ctrl-shuf`, `accounting`, `recipes`, `recipes-shuf`, `cond`, `cond-shuf`, `hooks`, `hooks-shuf`; `accounting-full`, `s1bench`, `ctrl-full` (*all*) | their suites run v1.35 firmware under C&C's commands (GO, DONE, the command block) | the old suites (P11): each rewritten for v1.36's hardware, or deleted |
| `riscv` | `test_golden`: the M1 goldens of v1.35's core, which is deleted. The step `core-programs` stands where it stood: the RTL's own riscv-tests on the RTL's core, in the RTL's cycles | the step's line goes with `test_golden` (P11) |
| `regdoc` | `suite_regdoc` compares `REGISTERS.md` and `mesh.h` with their generators through ref's `RegDocTest` and v1.35's draft header; the files themselves are v1.36's now, and `test_regs` checks them against the generator and the specification's offsets | the suite ported to v1.36's tables, or deleted for `test_regs` (P11) |
| `periph`, `periph-shuf` | `test_tileslotconf`: boot-ROM programs on v1.35's map and peripheral window | deletion (P11): the slot is `test_periph_slot`'s and `test_rtl_tile`'s |
| `reqbus`, `reqbus-shuf` | `suite_reqbus`: v1.35's 128-entry request FIFO and its receive protocol through the harness | a test of the request tree (11.3) from the specification, or against the RTL's `request_tree_test` model (it is exercised only through the chip now); then the old suites (P11) |
| `cccores`, `cccores-shuf`; `ccbench` (*all*) | v1.35's C&C 1–3 as cores with `cc1.c` and `cc23.c` | deletion (P11): the special tiles run the RTL's images in `test_rtl_sort` |
| `replay-build`, `stageb`; `stageb-lrd_x0`, `stageb-scd_x0`, `stageb-nop_hint`, `stageb-jalr_self`, `stageb-jalr_zero` (`DELIVERY`) | the replayer stage's second row | there |
| `sort-p1`, `sort`, `sort-shuf`; `sort-bt-kv32`, `sort-bt-k128`, `sweep-quick` (*all*) | v1.35's sort firmware under C&C's commands, on `sort_test` and `sweep`; the sort end to end on v1.36's chip is the step `rtl-sort` | deletion, or `sort_test`'s sets as runs of the RTL's image (P11) |
| `rtl-sort-64k` (*all*) without the key file; `tracer-block` without the workloads' data | the RTL's 64K key file (16 MiB) and the real segments are outside the repository: `RTL_SORT_64K_KEYS`, `MESHSIM_INPUTS` (`RUNNING.md`) | the data on the host |

### 5. Behaviour the simulator still has from v1.35

Each is a difference from the RTL that the acceptances do not reach, or reach and survive. Where the old behaviour
would silently lose what the hardware keeps, the simulator stops (noted).

| what | the RTL | the simulator | closes |
|---|---|---|---|
| the write engine | a fixed pipeline, no input FIFO, never refuses (12.4) | v1.35's input FIFO (4) and stages; **an overflow stops the simulator** (`TileCtl`) | P9 |
| `PTR`, `HIST` | no request FIFOs, no overflow error (12.5) | v1.35's 16-entry request FIFOs; **an overflow stops the simulator** | P9 |
| the gather unit | while it is busy the address generator takes no read (14.1); `gather_busy` is "the queue holds a request or a request's last read not yet taken" (13) | the read arbiter's priority alone; `STATUS.gather_busy` is v1.35's OR of three inputs, in-flight reads included | P9 |
| the scratchpad | banks, a write wins its bank, `READ_LATENCY` fixed, reads return in take order (15) | v1.35's areas with request queues | P9 |
| the DMA unit | `DMA_CONFIG` latched at the start; the lead-ins of 8.2; an empty transfer busy for one cycle | read live at every step; v1.35's lead-ins (an empty transfer: as 8.2) | P9 |
| tokens | a loopback packet's token is freed when the director takes its copy (14.2) | v1.35's: when it leaves the loopback queue | P9 |
| the router | the arbiters' order and the queues of 16.2, 16.3 | v1.35's router with v1.36's rules added: the attach ports on tiles 0, 7 and 15, a reserved dst dropped, a special tile's router with its DMA input and the tile's own id, no start skew in the chip. Its conformance references are v1.35's up to the first such packet and the simulator's own after it (section 3) | a conformance against the RTL's router (P9) |
| the memory tile | the RW engine (17.7): its region, its DMA's two queues, LPDDR over AXI4 | a stand-in with 17.7's `PARAMS` and words, idle; **a `START`, a store to its words, a transfer or a `WRITE` into its region stops the simulator** | P10 |
| Ingress/Egress | the UART both ways, the host interface (AXI4-Stream) both ways, the LED and the toast pin (17.6) | the nets, `RELEASE`, the watchdog, the LED, the toast pin, the UART's transmit FIFO; the UART's and the host interface's receive sides read empty; **a store to the host interface's transmit words stops the simulator** | P10 |
| the tracer tile's memory system | *Open* behind the `fill` port (17.9.14) | the simulator's `MemSystem` over a `FileStore`, given by the harness; on the chip no test gives one, and a run without it stops the simulator | the segment (P10) |
| the peripheral's DMA under a full router input | the packet register takes an entry whenever it is empty or its packet leaves, and offers it until the router takes it (`peripheral_dma.veryl`) | an entry is taken only in a cycle the router's input has room (the credit); the same packets a cycle apart at most, and the transfer's end the same | P9, with the router |
| the checker | | v1.35's facts about hardware that is gone are no longer recorded: the command block, the reply block, the stray check, the table-window check of the core | P11 |

### 6. Timing left for the timing stage

Every latency or ordering where the simulator still differs from the RTL, with the RTL's figure. *q* is the cycle the
core raises an access in (its instruction leaves execute), *c* a cycle. What is the RTL's already and is not here:
the core's own cycles and its join to the register bus (the pipeline's costs, a trap's entry, `WFI`, the canonical
wait and `core_status`: `test_core_programs`; a table access made in *q*, a window access in *q* + 1, a window store
in effect at the end of *q* + 1 and answered there, a store that may stall, the tile's or a peripheral's by its
type's list, answered the cycle after its owner takes it, every access fault answered in *q* + 1: `test_core_tile`,
`test_mach`), `READ_SEND`'s read queued and `REQUEST_SEND`'s request pushed in the cycle the store is made in, its
stall judged there, the boot ROM's cycles on the tile (reset to the `LOAD` request 26 cycles, `boot_local` 27, the
release to the image's first instruction 13: `test_rtl_tile`), a peripheral's control block and DMA (a store's
effect, a consuming load, `dma_busy` and `dma_done`: `test_periph_slot`), the replayer's and the Tracer's own cycles
(their stages), a link's hop (4 cycles), an empty phase from release to release (35).

**The sort on the chip**, the simulator beside the RTL's chip test (`rtl/chip/README.md`; `test_rtl_sort` prints the
simulator's, untuned; cycles, a step from its release to the next step's, the total from L1's release to L2's all
arrived):

| | 64 × 16 kv32 | RTL | 64 × 16 k128 | RTL | 64 × 64K kv32 | RTL | 64 × 64K k128 | RTL |
|---|---|---|---|---|---|---|---|---|
| from the reset to the load's release | 20,783 | 20,784 | 20,783 | | 20,783 | | 20,783 | |
| the image's start-up, to L1's release | 16,035 | 16,033 | 16,035 | | 16,035 | | 16,035 | |
| L1 | 7,537 | 7,518 | 7,665 | 7,646 | 335,137 | 335,118 | 466,305 | 475,646 |
| SPLIT | 118,449 | 118,367 | 118,449 | 118,367 | 259,663 | 259,473 | 259,663 | 259,473 |
| XB | 9,855 | 9,856 | 10,175 | 10,176 | 9,855 | 9,856 | 10,175 | 10,176 |
| RED | 6,817 | 6,818 | 7,473 | 7,474 | 6,663 | 6,664 | 7,259 | 7,260 |
| K | 266 | 263 | 1,050 | 1,047 | 71,527 | 71,524 | 72,311 | 72,308 |
| V | | | 270 | 267 | | | 71,531 | 71,528 |
| L2 | 6,856 | 6,841 | 6,983 | 6,968 | 268,936 | 268,921 | 400,103 | 409,448 |
| the empty phases together | 598 | 598 | 1,068 | 1,068 | 598 | 598 | 1,068 | 1,068 |
| total | 150,378 | 150,261 | 153,133 | 153,013 | 952,379 | 952,154 | 1,288,415 | 1,306,907 |

So the load, the start-up, XB, RED and the empty phases are within 2 cycles of the RTL's, K and V 3 over, in every
run; L1 is 19 over and L2 15 over in every run but 64 × 64K k128, where they are 9,341 and 9,345 under (2 %, about
one cycle in every seven records): the gathers of 128-bit values, which the RTL's README gives as its one
difference from v1.35 there ("the gather in 128-bit rows, the scratchpad's fixed read latency with a write winning
its bank": `rtl/chip/README.md`, "Where the cycles differ"), and which are still v1.35's here (section 5: the
gather unit, the scratchpad); SPLIT is 82 over at 16 records and 190 over at 64K. Each is a unit's latency of
the rows below, not the core's: before the RTL's core the 16-record kv32 total was 125,913.

| what | the RTL | the simulator | the RTL's source |
|---|---|---|---|
| a send's stall | `WRITE_SEND_*` is taken unless the holding register is full and not injected that cycle (`reg_ready = !holding_valid \|\| packet_ready`) | the packet is put on the holding register in the cycle the store is made in (injected in *q* + 2 at the earliest, as the RTL), but the write unit's side in `TileCore` holds up to two packets, the one being put and the next send's, so the core runs up to two sends ahead of a full holding register before it stalls | `rtl/blocks/write_unit/src/write_unit.veryl`, header "The stall (9)" and line 134; `rtl/tile/base/spec.md` 9, 14.3 |
| a peripheral's live event or error | its level in cycle *c* raises the wake or the NMI in *c* + 1, a cycle after a load would see it | in *c* | `rtl/blocks/tile_control_unit/src/tile_control_unit.veryl`, header |
| the link's latency as the master measures it (a `COUNT` to tile 0 and its arrival back) | 36 cycles, 4 more a hop | 35, 4 more a hop: one cycle short somewhere on the special tile's path (its router, its attach link, the director's acceptance or the counter event), the same at every distance | `rtl/chip/README.md`; `rtl/tile/base/spec.md` 16 |
| the units behind the tile control unit | the write engine's pipeline, `TABLE_LATENCY`, `READ_LATENCY`, the DMA's lead-ins and the end of a transfer, the gather unit's reads, the router's queues | v1.35's (`ts_engine_latency`, `sram_read_latency`; `K` = `LAND_LATENCY` = 8 and `cm_latency` = `WRITE_PORT_LATENCY` = 2 already the RTL's): the sort's L1, L2, SPLIT, K and V above | `rtl/tile/base/spec.md` 8, 12–16; P9 |
| a write's landing in a peripheral | within `PERIPHERAL_LATENCY` (at most `LAND_LATENCY` − `WRITE_PORT_LATENCY` = 6) of the write port, which presents it `WRITE_PORT_LATENCY` = 2 cycles after the director takes it; the replayer and the Tracer land it at the end of the port's cycle | the same (the slot's `region`, a Link of `WRITE_PORT_LATENCY` + 1; `accept(t)` presenting in *t* + 2). What is not checked against the RTL is the director's own acceptance cycle on the tile (the cycle after the router's ejection), which the write port's cycle counts from | `rtl/tile/base/spec.md` 12.3, 17.3; `rtl/blocks/replayer_unit/src/replayer_unit.veryl` line 47, `rtl/blocks/tracer_unit/src/tracer_control.veryl` line 53 |
| the nets | `BARRIER_ARRIVE` a flip-flop's pulse the cycle after the tile is ready, a wire to Ingress/Egress; `BARRIER_RELEASE` a flip-flop of Ingress/Egress, in every tile in the same cycle; the error lines wires | the chip's nets at those latencies (`arrive_latency` 0, `release_latency` 1, `error_latency` 0), but an error line's level reaches `ERROR_LINES` a cycle later than the RTL's wire (a Link of 1 + `error_latency`: a Wire cannot cross thread domains) | `rtl/tile/base/spec.md` 7.3, 18; `rtl/chip/src/chip.veryl` |
