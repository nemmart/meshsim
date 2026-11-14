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

The tile's units (the DMA, write, read, receive, gather and inject units and the scratchpad) are the specification's
(8-15), each with a test written from it (`test_ag`, `test_receive`, `test_mem`, `test_gather`, `test_mach`), and the
sort's cycles on the chip are the RTL chip's, step for step, which `test_rtl_sort` checks. What is left is named below as
in the plan: the router's conformance, the memory tile's RW engine, the host interface and the segment on the chip
(P10), the old suites (P11), meshsim's own firmware (P12), and what is left of the timing.

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
| the image's start-up, from the load's release to L1's release | 16,036 cycles here, 16,033 on the RTL's chip: printed, not checked (every step of the sort, the total, the load and the link's latency are checked equal to the RTL's) | the timing stage (section 6) |

### 3. Tests of `make check` that are not built (`pending.mk`)

`make pending` lists them. What was ported out of this list: `test_mach`, `test_ag`, `test_gather`, `test_mem`,
`test_router`, `test_periph_slot`, `test_tracer_slot`, `test_cnc` (their commits say what changed in each); what each
of them no longer checks is in the second table.

| what | why | what closes it |
|---|---|---|
| `test_regblock`, `test_golden`, `test_controllers`, `test_broken_units`, `test_statefp_core`, `test_g1_replay`, `test_statefp_g1`; the tools `golden`, `bench_controller` | v1.35's M1 bench (`src/units/controller.h`: v1.35's core with v1.35's register block) and its goldens: retire traces and fingerprints of v1.35's images, on v1.35's ISA, traps and map. v1.35's core left the tree with the core stage (`src/units/rv32_v135.h`, and its own tests `test_rv32` and `test_core_m2`, are deleted), so none of these can run again as written. They stay, unbuilt, only for what still includes them: `tests/golden_run.h` (which includes `controller.h`) is included by the pending chip tests `test_chip`, `test_sort`, `test_cc_units`, `test_g1_replay` and `tests/t2_harness.cpp`, and `controller.h` by `tests/statefp_g1.h` (`test_memconf`'s row) | deletion, with the port or replacement of the tests that include them (P11). What they checked of the core is checked on the RTL's core by `test_core_programs` (the RTL's 49 programs, CoreMark, the interrupt programs and the boot ROM, cycle for cycle) and `test_core_tile` (the core's join to the register bus) |
| `test_g1_tile`, `test_tile_slot`, `test_tileslotconf` | one tile's control part driven by v1.35's stand: GO, the command block, v1.35's registers and boot-ROM programs | superseded by `test_rtl_tile` for the tile and by `test_periph_slot` for the slot; deletion with the old suites (P11) |
| `test_engconf` (`tests/engconf/`) | SUPERSEDED, kept unbuilt. ref's EngConf scripts and observations are of v1.35's address generator and gather unit, cycle by cycle: per-stream gaps (60–81 of each fixture's 256 `STREAM` entries), v1.35's request FIFOs in front of the tables, its offers through a holding register and its lead-ins. v1.36's DMA unit is the specification's (8.2–8.5), so no line of them can be produced again | nothing: `test_ag` (8.2–8.5 from the specification and the RTL's unit), `test_gather` and `test_mach` (13) stand for it; deletion with the old suites (P11) |
| `test_memconf` (`tests/memconf/`) | SUPERSEDED, kept unbuilt. ref's MemConf observations are v1.3's fingerprint streams of v1.35's memory path (`srq`, `inj`, `sram`, `we`, `ts`, `hist`): the read queue's token slots, the area queues, write combining, the write engine's input FIFO and the tables' request FIFOs, none of which v1.36's hardware has (12.4, 12.5, 14, 15). Its writer, `tests/statefp_g1.*`, does not compile, and `tests/statefp_g2.cpp`'s `inj` and `srq` writers now stop if called | nothing: `test_mem` (14, 15) and `test_receive` (12.4, 12.5) stand for it; deletion with the old suites (P11) |
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
would silently lose what the hardware keeps, the simulator stops (noted). The tile's units of chapters 8-15 are no
longer here: the DMA unit, the write and read units, the receive unit's write engine with `PTR` and `HIST`, the gather
unit, the inject unit and the scratchpad are the specification's (`src/units/README.md`).

| what | the RTL | the simulator | closes |
|---|---|---|---|
| the router | the arbiters' order and the queues of 16.2, 16.3 | v1.35's router with v1.36's rules added: the attach ports on tiles 0, 7 and 15, a reserved dst dropped, a special tile's router with its DMA input and the tile's own id, no start skew in the chip. Its conformance references are v1.35's up to the first such packet and the simulator's own after it (section 3) | a conformance against the RTL's router |
| the memory tile | the RW engine (17.7): its region, its DMA's two queues, LPDDR over AXI4 | a stand-in with 17.7's `PARAMS` and words, idle; **a `START`, a store to its words, a transfer or a `WRITE` into its region stops the simulator** | P10 |
| Ingress/Egress | the UART both ways, the host interface (AXI4-Stream) both ways, the LED and the toast pin (17.6) | the nets, `RELEASE`, the watchdog, the LED, the toast pin, the UART's transmit FIFO; the UART's and the host interface's receive sides read empty; **a store to the host interface's transmit words stops the simulator** | P10 |
| the tracer tile's memory system | *Open* behind the `fill` port (17.9.14) | the simulator's `MemSystem` over a `FileStore`, given by the harness; on the chip no test gives one, and a run without it stops the simulator | the segment (P10) |
| the peripheral's DMA under a full router input | the packet register takes an entry whenever it is empty or its packet leaves, and offers it until the router takes it (`peripheral_dma.veryl`) | an entry is taken only in a cycle the router's input has room (the credit); the same packets a cycle apart at most, and the transfer's end the same | with the router |
| the gather queue's count while a read is refused | a request leaves the queue in the cycle its first read enters the read register, which is empty or taken then (`gather_unit.veryl`) | the request leaves the queue when its first read is put, a cycle before the register is known to take it: with a read refused, the queue holds one request fewer than the RTL's for those cycles. Only an overflow at exactly 64 queued sees it, which the address generator alone cannot produce (13) | the timing stage |
| a store to a `STREAM` or `GROUP` entry the running transfer reads | undefined (8.6): the RTL's read takes the entry `TABLE_LATENCY` cycles after its issue | the read takes the entry at its issue (`ag.h`) | nothing: undefined |
| `PTR` or `HIST` loaded in a cycle its engine reads it | the load's value is undefined (12.5) | **the simulator stops**, naming the tile and the table: a program uses them only while their engines are idle | nothing: undefined |
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
(their stages), a link's hop (4 cycles), and the tile's units, each from the specification or the RTL block's README
and checked by its test:

| what | the figure | the test |
|---|---|---|
| a scratchpad read | taken in the cycle it is offered, injected `READ_LATENCY` = 3 cycles later, at the router's local input in 4; a write wins its bank (14.1, 14.3, 15; `rtl/blocks/scratchpad`, `rtl/blocks/inject_unit`) | `test_mem` |
| a loopback packet's token | free from the cycle after the director takes its copy (14.2) | `test_mem` |
| the write unit's send | taken unless the holding register is full and not injected that cycle; a packet a cycle (9; `rtl/blocks/write_unit`) | `test_mem` |
| a write the director accepts in *a* | `PTR` and `HIST` read at the end of *a* + L and written at the end of *a* + L + 1; the scratchpad written in *a* + 2L + 2 (12.4, 12.5; `rtl/blocks/receive_unit`, Timing) | `test_receive` |
| a `GATHER` accepted in *a* | busy from *a* + 1, its first read taken in *a* + 2, the reads one a cycle, busy 0 the cycle after the last take (13; `rtl/blocks/gather_unit`, Timing) | `test_mach` |
| a transfer | the lead-in 2L + 2 (stream) and 4L + 3 (group) after the start store, the reads' issue and the places of 8.5, a refused read shifting every later time, the end the cycle after the walk completes (8.2–8.5; `rtl/blocks/dma_unit`) | `test_ag` |
| a live event or error | reaches the core the cycle after a load reads it (`rtl/blocks/tile_control_unit`, Timing) | `test_tile_control` |

**The sort on the chip.** `test_rtl_sort` checks every step, the empty phases, the total, the load and the link's
latency equal to the RTL chip's (`rtl/chip/README.md`, Results), in the four runs; the simulator is not tuned to them.
Cycles, a step from its release to the next step's, the total from L1's release to L2's all arrived:

| | 64 × 16 kv32 | 64 × 16 k128 | 64 × 64K kv32 | 64 × 64K k128 |
|---|---|---|---|---|
| from the reset to the load's release | 20,784 | 20,784 | 20,784 | 20,784 |
| the image's start-up, to L1's release (**the RTL's: 16,033**) | 16,036 | 16,036 | 16,036 | 16,036 |
| L1 | 7,518 | 7,646 | 335,118 | 475,646 |
| SPLIT | 118,367 | 118,367 | 259,473 | 259,473 |
| XB | 9,856 | 10,176 | 9,856 | 10,176 |
| RED | 6,818 | 7,474 | 6,664 | 7,260 |
| K | 263 | 1,047 | 71,524 | 72,308 |
| V | | 267 | | 71,528 |
| L2 | 6,841 | 6,968 | 268,921 | 409,448 |
| the empty phases together | 598 | 1,068 | 598 | 1,068 |
| total | 150,261 | 153,013 | 952,154 | 1,306,907 |
| the link's latency at tile 0, as the master measures it | 36 | 36 | 36 | 36 |

Every figure but the start-up's is the RTL's. How they came to be, each change a unit brought to the specification and
none made for a figure (the 64 × 16 kv32 total and the 64 × 64K k128 total, the RTL's 150,261 and 1,306,907): before
the units, 150,378 and 1,288,415; the scratchpad and the inject unit (`READ_LATENCY` 3, a write winning its bank, the
arbiter), 150,238 at 16 records; the write engine's pipeline and `PTR`'s and `HIST`'s timing, no change (a phase waits
for the counters' decrement, at `LAND_LATENCY`); the DMA unit (the table reads from the start's cycle, the offer taken
in its cycle, the places), 150,218 and 1,306,857, every step the engines drive one cycle under the RTL's; the write
unit's stall, no change; a live level reaching the core a cycle after a load reads it, which the master's wake on
`all_arrived` waits for in every step it sleeps through, 150,261 and 1,306,907.

What still differs:

| what | the RTL | the simulator | the RTL's source |
|---|---|---|---|
| the image's start-up, from the load's release to L1's release | 16,033 cycles | 16,036 (16,035 before the live levels' cycle): not traced to a unit; the master's start-up runs its UART and its requests, whose receive sides and tree are the candidates | `rtl/chip/README.md`, Results |
| `ERRORS.drift` | set at the end of the cycle a group transfer's read is refused in | set two cycles later: the generator learns of the refusal the cycle after (the Hold), and its pulse crosses a Link(1) | `rtl/blocks/dma_unit/src/address_generator.veryl` (`drift`) |
| a write's landing in a peripheral | within `PERIPHERAL_LATENCY` (at most `LAND_LATENCY` − `WRITE_PORT_LATENCY` = 6) of the write port, which presents it `WRITE_PORT_LATENCY` = 2 cycles after the director takes it; the replayer and the Tracer land it at the end of the port's cycle | the same (the slot's `region`, a Link of `WRITE_PORT_LATENCY` + 1; `accept(t)` presenting in *t* + 2). The director's acceptance cycle on the tile is the RTL's for a loopback packet (injected in T, accepted in T + 3: `test_mach`'s gather case) | `rtl/tile/base/spec.md` 12.3, 17.3; `rtl/blocks/replayer_unit/src/replayer_unit.veryl` line 47, `rtl/blocks/tracer_unit/src/tracer_control.veryl` line 53 |
| the nets | `BARRIER_ARRIVE` a flip-flop's pulse the cycle after the tile is ready, a wire to Ingress/Egress; `BARRIER_RELEASE` a flip-flop of Ingress/Egress, in every tile in the same cycle; the error lines wires | the chip's nets at those latencies (`arrive_latency` 0, `release_latency` 1, `error_latency` 0), but an error line's level reaches `ERROR_LINES` a cycle later than the RTL's wire (a Link of 1 + `error_latency`: a Wire cannot cross thread domains) | `rtl/tile/base/spec.md` 7.3, 18; `rtl/chip/src/chip.veryl` |
| the router's queues and arbiters under load | 16.3's round-robin order and queue depths | v1.35's router (section 5): the sort's traffic is the RTL's cycle for cycle through it, but no test compares a loaded router with the RTL's | `rtl/blocks/router` |

### 7. Retired, with what replaces it

v1.3's references for the memory path measured v1.35's hardware (16 areas with request queues and write combining, a
read queue with numbered tokens and a holding register, the write engine's input FIFO, the tables' request FIFOs).
v1.36's units have none of it (12.4, 12.5, 14, 15), so those references cannot be produced again by any simulator of
v1.36, and they are not regenerated: tests written from the specification and the RTL blocks' READMEs replace them.

| Retired | Was | Replaced by |
| --- | --- | --- |
| `tests/suite_mem.cpp`; `tests/p4suites/mem.log`, `mem-shuf.log`, `mem-shuf2.log` | ref's MemTest and its logs: the areas' queues and their depth, write combining, the read latency through a queue, the token window at an array latency of 80, an area oversubscribed | `tests/test_mem.cpp`, which the steps `mem`, `mem-shuf` and `mem-shuf2` run (the last two with the units of a level stepped in a shuffled order) |
| `test_mem.cpp`'s v1.35 cases (the write engine's input overflow as an event, the SRAM Hold's refusal, the request FIFO's judgment, write combining, the read port's same-cycle tie) | hand-derived cases of v1.35's FIFOs | `test_mem` (14, 15) and `test_receive` (12.4, 12.5) |
| `test_ag.cpp`'s v1.35 cases (the lead-in through the request FIFO, `done` when the holding register empties, refusals absorbed by a gap) | hand-derived cases of ref's generator | `test_ag` (8.2–8.5) |
| `test_mach`'s NetSelf cycles for `reads` and `a2a` | ref's cycles through v1.35's read queue and generator | the same fixture, each cycle 2 earlier for a read (`READ_LATENCY` 3 and no holding stage: the RTL's inject unit README gives both figures) and 2 more for the group transfer (the offer taken in its cycle, the table reads from the start's cycle); the values are ref's |
| `tests/statefp_g2.cpp`'s `inj` and `srq` writers | ref's fingerprint of the read queue | nothing: they stop if called (their users are pending tests) |
| the settings `areaQueueDepth`, `writeCombining`, `fifoCountBeforePop`, `sramReadLatency`, `weInDepth`, `agLookahead` | v1.35's parameters | refused by name (`MachConfig::dropped()`); `readLatency` is `READ_LATENCY` |

### 8. Notes: what the tile's units take from the RTL where the specification is silent

1. **`READ_LATENCY` is 3**, and a read taken in *t* is injected in *t* + 3 and at the router's local input in *t* + 4
   (`rtl/blocks/scratchpad/src/scratchpad_pkg.veryl`; `rtl/blocks/inject_unit/README.md`, "The inject unit"). 15 leaves
   the figure to the implementation.
2. **The return queues are one line** of `READ_LATENCY` stages, not a queue per client (the same README, its decisions).
3. **The banks a write meets** are the aligned `MAX_BANK_BYTES` blocks, the whole scratchpad when it is smaller (the
   same README; `rtl/blocks/scratchpad/README.md`: `min(SCRATCHPAD_BYTES, MAX_BANK_BYTES)`).
4. **A write the director accepts in *a*** is in the scratchpad in *a* + 2L + 2, `PTR` and `HIST` read at the end of
   *a* + L and written at the end of *a* + L + 1 (`rtl/blocks/receive_unit/README.md`, "Timing"); 12.3 says only "within
   `LAND_LATENCY`". `TABLE_LATENCY` above 3 is refused, as that unit does.
5. **A core load of `PTR` or `HIST` in the cycle of the engine's write** reads the old value, and one in the cycle of
   the engine's read is undefined (the same README: "the engine's read-modify-write uses the read port in its read's
   cycle and the write port in the next"); 12.5 says only that the engine wins a port it uses.
6. **`STATUS.gather_busy`** rises in *a* + 1, the first read is offered in *a* + 2, and it falls the cycle after the last
   read's take (`rtl/blocks/gather_unit/README.md`, "Timing").
7. **The DMA unit**: `DMA_CONFIG` is taken at the start (8.2 does not say; the RTL's README, its decisions); the table
   reads begin in the cycle the start is presented in; a place is free for a read from the cycle after its visit is
   made; a count-0 stream that takes no cycle keeps its place until it passes, and passes when the generator looks for
   its next stream, with the first other stream's first element when that stream's entry is in, alone otherwise; the
   transfer ends the cycle after its walk completes (`rtl/blocks/dma_unit/README.md`, "A transfer";
   `src/address_generator.veryl`'s header).
8. **The write unit's stall** is `reg_ready = !holding_valid || packet_ready` (`rtl/blocks/write_unit/README.md`, "The
   stall"; `src/write_unit.veryl`).
9. **The live levels** (`EVENTS.request_pending`, a peripheral's event and error) reach `meip` and `nmi` a cycle after a
   load reads them (`rtl/blocks/tile_control_unit/README.md`, "Timing").
