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
  runs the RTL's sort images end to end on the chip (`fw/rtl/sort/`), four runs, against the oracle;
- **the segment**: **acceptance C**, `tests/test_rtl_segment.cpp`, runs the segment on the chip under the RTL's trace run,
  tracer tile's image and replay image, traced, delivered and replayed, in the RTL chip's cycles.

The tile's units (the DMA, write, read, receive, gather and inject units and the scratchpad) are the specification's
(8-15), each with a test written from it (`test_ag`, `test_receive`, `test_mem`, `test_gather`, `test_mach`), and the
sort's cycles on the chip are the RTL chip's, step for step, which `test_rtl_sort` checks. The router (16) is the
RTL's `router` at the packet and the cycle, which `test_router` checks against the RTL's own router under scripted
random load (`tests/routerconf/README.md`); the segment on the chip is the RTL's (the segment stage), and so are the
special tiles' peripherals, the RW engine and Ingress/Egress (sections 8 and 9). What is left is named below as in the
plan: the old suites (P11), meshsim's own firmware (P12), and what is left of the timing.

## The core stage

`src/units/rv32.h` and `rv32_decode.h` are the RTL's `rv32_core` (`tests/core-programs/README.md` says what it is
checked against). What it leaves out, and what no test in the tree checks:

| what | why | what closes it |
|---|---|---|
| The coprocessor port | The tile has no accelerator (the RTL builds none), so custom-0 and custom-1 are illegal instructions, as on a core with `coprocessor_present` 0. The RTL's `program_test` sets it to 1, but none of its programs has a coprocessor instruction. | The field accelerator, when the RTL has one. |
| An instruction or data port that holds ready low, or answers a fetch later than the next cycle | No memory of the tile does either (IMEM, DMEM, the boot ROM and `register_bus` accept in every cycle), so the model has neither; the RTL's test runs each program a second time with randomly delayed ports, which the model cannot follow. | Nothing in the tile; a unit that needs it. |
| The core's rules one by one (`test_rv32_rtl`: 62 directed tests of the RTL's core from the contract's sentences) | Written by a sub-agent of the core stage, which could not place the file in `tests/`, so it is not in the tree: it is in the core stage's scratch folder, `build-scratch/rtl-tests/test_rv32_rtl.cpp` (untracked, outside `v1.36/`), written against the core stage's interface (`v135_platform`, v1.35's tile environment), which is gone. What the RTL's programs do not reach and only that file checks: a CSR write to `mcycle` or `minstret`, `minstret` on a trapped instruction, the store-misaligned cause, and a data target slower than one cycle on a bus. (The cycle `core_status_canonical_wait` rises and falls is checked now: `test_core_programs` requires the RTL's cycles of every change of `wfi_sleeping` and of the level in the interrupt programs, `tests/core-programs/WAITS`, and `test_rtl_tile` every `BARRIER_ARRIVE` two cycles after the wait begins, which fails with the level taken from `wfi_sleeping` alone.) | The owner's or coordinator's decision to add the file as `tests/test_rv32_rtl.cpp`, ported to the tile's environment as it is now (`tests/test_core_tile.cpp` shows it), after reviewing it. |

## The replayer stage

The replayer's model is the RTL's `replayer_unit` at the block level: its rows, its cycles and every port's
observations equal the RTL's (`tests/replayer/README.md`, `tests/acceptance/README.md`). In the chip it is every
compute tile's peripheral 1, in the slot (`src/units/periph_slot.h`, "THE CONTRACT"): its control block's words are
17.1's, its write port lands as the RTL's, and acceptance A's `replayer` program passes on it with the row port at a
random ready.

| what | why | what closes it |
|---|---|---|
| `test_slotconf` (`tests/slotconf/seed1-5`) | the slot's conformance replays ref's seeded scripts, which store and load v1.35's control-block words and reserved words at random, on a slot with the replayer in it; its reference is ref's observations of v1.35's replayer and v1.35's DMA under them (busy, errors, events and queue occupancies cycle by cycle), which neither the RTL's replayer nor 17.1's slot gives | a conformance of the slot against the RTL's `peripheral_control` and `peripheral_dma` (scripts replayed on the RTL's blocks in a Snarky One scratch worktree, as `tests/replayer/` does for the replayer). Until then every rule of 17.1–17.4 the slot implements is tested from the specification and the RTL's headers in `test_periph_slot` and `test_ingress_egress` (the control block's words and reserved bits, `START` and `DMA_START` with bit 0 clear, the counted events of depth 1, a set in a consuming load's cycle, the DMA's packets, cycles and wait for room, peripheral 3's window, region and tile bit, the DMA's packet register under a router input that is not ready, `DMA_HDR` taken at the transfer's start), but: a sticky error through the slot, the DMA waiting on an empty queue, and a store to `DMA_DEST_ADDR` during a transfer (UB: the slot flags it; the store wins over an advance in its cycle, as the RTL's). What the conformance would add is the random interleavings; the replayer's side is `test_replayconf`'s |
| the row port's handshake | the model's consumer is its sink, ready one cycle in `sink_interval` (1: always, as the chip ties it); no unit reads `replay_row` | the materializer, *Open* (17.8.11) |
| `spec/CHANGES-v1.35-to-v1.36.md` 5.2.6 item 1: the timing rules the specification does not state | the model takes them from the RTL's sources: they are listed in `src/units/replayer.h`'s header and in the stage's report | a ruling: write them down (the guide suggests an appendix), or leave them to the RTL |

## The Tracer stage

The Tracer's model is the RTL's `tracer_unit` at the block level (`tests/tracerblock/`, `tests/test_tracer_rules.cpp`).
In the chip it is the tracer tile's peripheral 1, in the slot with the DMA (`src/units/periph_slot.h`, "THE
CONTRACT"): 17.1's control and DMA blocks, `dma_done` the slot's (bit 1), the DMA's cycles `peripheral_dma.veryl`'s,
a consuming load applied so that a set of its cycle survives it (`test_tracer_slot`, `test_periph_slot`). On the chip
it traces segments under the RTL's tracer tile's image (the segment stage, below).

### Pending

| What | Why | What closes it |
| --- | --- | --- |
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

## The segment stage

The segment on the chip is the RTL's: `tests/test_rtl_segment.cpp` (**acceptance C**; `tests/segment_bench.h`) runs the
RTL's chip tests of the segment, `chip_trace_test.rs` and `chip_replay_test.rs`, on the simulator, under the RTL's own
firmware, unmodified: the master's trace run, the tracer tile's image and the replay image (`fw/rtl/sort/trace/`,
`fw/rtl/sort/segment/`). The Tracer on tile 15's attach port traces the guest window by window, its DMA delivers each
window's record and streams over the NoC, and the compute tiles' replayers replay them. The test takes every row out
of every tile's row port (the replayer's sink: `set_row_buffer`, `set_row_times`), which closes the platform stage's
row for a consumer of `replay_row` in the chip.

**The memory system** behind the Tracer's fill port is *Open* (17.9.14), and the RTL's tests place a model of it at the
chip's boundary. The simulator's tests place the same model (`segment::Fill`, the RTL test's `Fill`: its latencies, its
readies 0 one cycle in four, its one xorshift32 stream, draw for draw) on the Tracer's fill port
(`Tracer::FillPort`). It is the test's, not hardware; the simulator's own `MemSystem` stays the block-level tests'.

**What is checked, and against what.** Nothing comes from the simulator:
- the RTL test's three generated segments (4 windows of 3,000 rows; 64 windows of 1,500 rows, a guest built to miss; a
  window of 65,536 rows), each from a fixture the RTL's test wrote as it ran (`tests/segment/rtl/`, its README): the
  guest and the RTL's models' expectations (every row, record and stream), and the RTL chip's own figures. All of
  `run_chip`'s checks, and every cycle: the run's cycles from the reset, the master's summary's 285 words, the tracer
  tile's report, every fill request's cycle and answer, and the cycle each row left its tile's port;
- the real workloads' windows (`MESHSIM_INPUTS`): EVM Zone's 0–63 and 64–127 and Cyclone's 0–63, the RTL's three
  segments of 64 windows, every row against `rows.bin`, every record and stream against the recorded files, each
  replayer's end registers against the next window's start, and the cycles against the RTL chip's published ones.

**The cycles.** Every figure is the RTL chip's; no unit was changed for one (the stage changed one thing in the model:
a fill request is presented until its channel is ready, `tracer_fill.veryl`'s handshake, which the simulator's own
memory system, always ready, never showed). The generated segments, from the fixtures (the RTL's and the simulator's
are equal in every row of the table, and in every word and cycle behind it):

| | 4 × 3,000 rows | 64 × 1,500 rows | 1 × 65,536 rows |
|---|---|---|---|
| from the reset to the poll that sees the summary | 113,152 | 764,928 | 609,280 |
| the load | 13,953 | 13,953 | 13,953 |
| the trace with its windows' delivery | 68,302 | 722,628 | 333,770 |
| the Tracer's own, its first `START` to its last `done` | 62,823 | 717,161 | 328,299 |
| the replay, to the passes' end | 11,898 | 9,114 | 242,477 |
| fill requests compared, cycle for cycle | 22 | 6,363 | 22 |
| rows' cycles out of the row ports compared | 12,000 | 96,000 | 65,536 |

The real workloads, against Snarky One's `rtl/chip/README.md` (Real workloads' windows), each figure equal:

| | EVM Zone, windows 0–63 | EVM Zone, 64–127 | Cyclone, 0–63 |
|---|---|---|---|
| the replay, the release to each window's last row, summed: cycles; a row | 13,045,233; 3.1102 | 12,854,434; 3.0647 | 13,106,834; 3.1249 |
| to the windows' first rows, summed | 13,235 | 8,938 | 12,151 |
| the passes' end, from the release | 230,435 | 210,878 | 228,441 |
| the trace, the Tracer's first `START` to its last `done` (the tracer tile's report) | 23,108,239 | 22,055,425 | 22,586,242 |
| from the reset to the poll that sees the summary (the simulator's; the RTL's README rounds its own to 23.8 M, 22.4 M and 23.1 M, which its parts above do not add up to) | 23,377,408 | 22,305,280 | 22,853,632 |

Over each whole run the replay is 25,899,667 cycles, 3.0875 a row, for EVM Zone, and 13,106,834, 3.1249 a row, for
Cyclone: the RTL chip's.

### Pending

| what | why | what closes it |
|---|---|---|
| a segment that starts inside a host segment (the RTL test's six smaller ranges: EVM Zone's 33–36 and 86–89, Cyclone's 30–33 and 40–42 among them) | the RTL's test starts a segment at any window by running its model of the Tracer's execution from row 0 to it; the simulator's tests have no such model, and take none from the simulator, so `tests/segment_real.h` starts at a host segment's start, which the recorded files give (the start state, the update lists, the advice) | a model of the execution in the tests (a port of the RTL's `tracer_execution_model.rs`), or the RTL test's fixtures for those ranges |
| the RTL test's reports (`print_trace`, `print_replay`, `print_instructions`, `chip_replay_test`'s stalls by kind) | reports, not checks: the test compares the figures they are made from (each run's `START`, rows' end and `done`, the statistics counters, every row's cycle) with the RTL's and prints the run's parts only | nothing needed; a port of the reports if they are wanted on the simulator |
| the fill port's handshake monitors | the model has no valid without a request and no response without a handshake to break; the test's model checks one read and one write outstanding, and a write only of a writable line that was read | nothing in the simulator's ports |
| `rtl-segment-real` without the workloads' data | the recorded runs are outside the repository (`MESHSIM_INPUTS`); the runner prints the step as PENDING, and the test binary run by hand or by `make check` runs the real segments only when `MESHSIM_INPUTS` is set | the data on the host |
| `test_cc_req` (the platform stage's row, P11) has lost its harness | it includes `tests/t2_harness.h`, retired below; it was not built before either (v1.35's C&C cores) | its port to `tests/mach.h`'s chip, or its deletion (P11) |
| `tests/m3-images/` (`cc1`, `cc23`, `tracer_ctl`, `replay_ctl`), `fw/images/log8k`, `fw/images/oldmap`, `fw/replay/`, `fw/tracer/`; `tests/golden-m3/` (`HASHES`, `cover/`, `baseline/`) | v1.35's controllers' images and sources, whose only loaders (T2, the chains, Stage B) are retired, and ref's records of T2; kept because `fw/build.sh`, `fw/check_images.sh` and their hash lists name them | the firmware stage (P12) and the old suites' deletion (P11) |

### Retired, with what replaces it

v1.3's and v1.35's tools for the segment ran meshsim's own controllers' firmware (`replay_ctl.c`, `tracer_ctl.c`,
`cc1.c`) on v1.35's map and commands, on a machine that is gone, and their references were ref's (the Java
simulator's) of that machine. None can run again, and none is regenerated: the RTL's firmware on the RTL's hardware,
checked against the RTL, replaces them.

| Retired | Was | Replaced by | What of it nothing checks now |
|---|---|---|---|
| `stageb` (`tests/stageb.cpp`, `stageb.h`, `stageb_main.cpp`), `test_stageb`, `tests/stageb/` (`stageb.log`, `replay-build.log`, `replay_build.sh`, `PROVENANCE`); the steps `replay-build`, `stageb`, `stageb-<guest>` | Stage B: `replay_ctl.c` on one tile replaying synthetic or delivered windows against Stage A, its cycles a row; the replay controller's build check | `test_rtl_segment`: the RTL's replay image replays the Tracer's deliveries on the chip, every row against the RTL's models or `rows.bin`, in the RTL chip's cycles (3.0875 and 3.1249 a row on the workloads); the image is vendored and hash-checked (`rtl-images`) | a controller on a wrong queue map failing as starved (CL4); a lone tile against the full chip with the 63 others untouched; the five delivered guests (`lrd_x0`, `scd_x0`, `nop_hint`, `jalr_self`, `jalr_zero`) on a tile, which were never on the runner (the replayer alone on a delivery is `replay_delivery`'s) |
| Stage B and Stage F of `replay_shard` (`--stages`) and of the acceptance kit (`tests/acceptance/`) | the kit's shards ran Stage A (the replayer alone), B (a tile) and F (the full chip) | Stage A stays: `replay_shard` and the kit run it against the RTL's cycles (`tests/acceptance/rtl/`); B and F are `test_rtl_segment`'s real windows | B against F, the same cycles alone and on the full chip |
| `t2` (`tests/t2_main.cpp`), `tests/t2_harness.*`, `test_t2_harness` and its TSan run; `tests/golden-m3/` `RUNS`, `evm.chip`, `cyclone.chip`, `boot/`; `tests/freeze/t2-rows.txt`; `tests/m3-images/bootstage.ref`, `boottrace.ref.gz`; `tests/freeze/body.sh`'s item 3 | T2: v1.35's tracer-tile firmware on C&C 3 with the full chip, segment 10 of two workloads, every unit's fingerprint at every 100,000 cycles against ref's, on one and six domains | `test_rtl_segment`: the segment on v1.36's chip against the RTL chip's cycles (the tables above), and split over 2 and 8 threads (`segment_small_split`) | v1.35's boot staging against ref; a fingerprint of every unit's state along the run (v1.36 has no reference for one: the summary, the report, the fill requests and the rows' cycles stand for it); a TSan run of the segment |
| `chain` (`tests/chain_main.cpp`); `tests/golden-chain/`; of `tests/chain.*` and `test_chain` the landings (`Landings`), the `M3FILES1` and `M3SRAM01` streams and the `.starts` oracle; `tests/freeze/body.sh`'s item 4 | the chains: segments 0–127 of both workloads back to back, each segment's start memory and landings against ref's hashes | `test_rtl_segment`'s real segments: host segments 0 and 1 of EVM Zone and 0 of Cyclone, each started from the recorded files and traced on the chip; `tests/chain.*` is cut back to the readers of the pipeline's files, which `tracer_block` and `tests/segment_real.cpp` use | a segment's end memory (the write-backs and the data cache's dirty lines) carried into the next segment's start on one machine: the RTL's firmware runs one segment a run, and the memory system between segments is *Open* (17.9.14) |

### Notes: what the segment's tests take from the RTL's tests where the specification is silent

1. **The memory system's timing** on the chip is the RTL test's model's, not a design's: reads answered 2 to 24 (a
   code line), 32 (a data line) or 40 (a map line) cycles after their handshake, writes 2 to 30, each ready 0 one
   cycle in four. The trace's cycles depend on it, and are the RTL chip's only under it.
2. **The scratchpads' layout** of a segment (the header's addresses, the tables at word 0x40, the replay region at
   0x1_0000) is the RTL test's, "as the host would"; the firmware reads it from the header.
3. **A cycle** of the RTL's test is the simulator's less one (the framework counts from 1); the fill port's draws and
   the rows' cycles are compared in the RTL test's.
4. **The tracer tile's report** is compared in the words its image writes (0–4, 8–15, three a window from 16): the
   others are the RTL's memory model's start values.
5. **A real segment's advice** is the values its windows' advice rows take (host segment k's first), its loaded
   record's advice field their number: the tracer tile's image requires the last record's field 0 (`tracer.rs`,
   `Status::AdviceLeft`).

## The platform stage

### 1. Acceptance A: what of the RTL's tile test is pending

| what | why | what closes it |
|---|---|---|
| the `special` program (kind 4, `base_tile_special_test.rs`) | it tests the base tile's boundary with its peripherals, which the simulator does not have as a boundary: the RTL's bench plays peripherals 1, 2 and 3 as bare `reg` interfaces, each returning a constant of its own for every word, the control block's and the DMA block's among them (`CONTROL_RDATA`, `DMA_RDATA`: `special.S`, checks 10-18), and takes the stores and the write port's packets itself; in the simulator the control block and the DMA block of every peripheral are the slot's own (`periph_slot.h`: `peripheral_control` and `peripheral_dma` are modelled once, in the slot, not per peripheral), so no peripheral of the slot can answer those loads with the bench's constants, and the program's part 5 sends on two DMA inputs (east and north) where the simulator's special tile has one (section 5, the router's row). Looked at again with the RW engine and Ingress/Egress modelled (the peripherals' stage): they do not bring it in reach, since the program wants no peripheral behind the interfaces | a slot whose blocks 0 and 1 a test can replace by a bare interface, and the router's second DMA input (the router's conformance); or nothing: what the program checks of the tile is checked piece by piece on the simulator: the straps and `boot_local` (`test_rtl_sort`, `test_rtl_memory`, `test_rtl_stream`: the master's and the memory tile's images start so), the peripherals' windows through the bus with the stores that stall (`test_core_tile`, `test_periph_slot`, `test_ingress_egress`, `test_rw_engine`), the write port (`test_periph_slot`, `test_rw_engine`), a DMA's packets through the special tile's router (`test_special_router`, `test_rtl_memory`), a request received with its credit (`test_request_tree`, `test_rtl_memory`'s 300 requests) |
| the RTL's model with all three attach ports at every tile id | a simulated tile has the attach port of its place in the chip (tile 0, 7 or 15: 2.1), so the runs at an attach tile use its one port | nothing: the RTL's model is a 1 × 1 chip's tile; the chip's attach ports are checked at chip level (P6) |
| the request link's beats and its handshake monitor; the row port's monitor | the simulator's request unit hands over whole requests, and the framework's ports have no ready or valid to watch. The bench takes a request's eight beats one a cycle at the RTL test's random ready, and the replayer's rows leave the row port at its random ready | nothing in the simulator's ports; the request tree's beats are the chip's (11.3) |
| cycle-exact expectations | `base_tile_test.rs` has none but its bounds (the barrier program's delay, the timeouts), which the test keeps. The test adds the RTL's figures for the boot ROM on the block `core` (`rtl/blocks/core/README.md`, "The boot ROM's program") and the core specification's resume timing, checked on the tile in every run | |

### 2. Acceptance B: what of the RTL's chip tests is pending

`tests/test_rtl_sort.cpp` is `chip_test.rs` (the sort, four runs), `tests/test_rtl_segment.cpp` is `chip_trace_test.rs`
and `chip_replay_test.rs` (the segment stage), and `tests/test_rtl_memory.cpp` is `chip_memory_test.rs` (the master's
requests to the memory tile and LPDDR: every check of the RTL's, and every handshake on the `lpddr` port and the done
word's cycle equal to the RTL chip's, `tests/rwengine/`). The RTL's last chip test:

| what | why | what closes it |
|---|---|---|
| `chip_stream_test`: the master echoing beats on its AXI4-Stream ports | CLOSED: `tests/test_rtl_stream.cpp` (the step `rtl-stream`) is the RTL's test, its program instruction for instruction and its LVDS IO block from the same xorshift32 sequence, with every check it makes; and every handshake's cycle (12 on `lvds_in`, 13 on `lvds_out`) and the done word's (513 cycles) equal to the RTL chip's, taken from a run of the RTL's test with its `Bench::cycle` printing them (Snarky One 076e647: `cargo test -p chip chip_stream_test -- --nocapture`). The same file runs the UART through the master's core (what `master_tile_test.rs`'s part 6 adds: 20 bytes stored back to back at a divisor of 20, the last three stores stalled on a full TX FIFO; four bytes polled in `UART_RX`), at the RTL chip's cycles for the same program and line (a scratch test of the RTL chip, `chip_uart_ref`, written beside `chip_stream_test` in a scratch worktree and not landed: the first start bit in cycle 34; the led pin's 20 changes, one after each store, the stalled ones' at 237, 437 and 637; `UART_STATUS` in DMEM after 643 cycles; the done word after 5,210). So a stalled `UART_TX` store through the core and `register_bus` is the RTL's to the cycle. Of `master_tile_test.rs` the rest is not ported: its parts 0–5 and 8 are the tile's and the nets' (`test_rtl_tile`, `test_request_tree`, `test_ingress_egress`, `rtl-sort`'s errors), and its part 7, the six beats with the fifth stalled, is `test_ingress_egress`'s stream cases at the unit and `chip_stream`'s on the chip | nothing |

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
| `test_cnc1_periph` (`tests/cnc1conf/`) | SUPERSEDED, kept unbuilt. v1.35's conformance of C&C 1's peripheral against ref (`READY_LINES`, `PULSE_GO`, the row collectors' words, a toast pin on any error line); v1.36's master tile has Ingress/Egress (17.6) | nothing: `test_ingress_egress` (17.6 from the specification and the RTL's `ingress_egress_unit`: the arrivals, `RELEASE`, the watchdog and its ties, the error lines, the toast pin, the LED, the window's map, the UART and the host interface) stands for it; deletion with the old suites (P11) |
| `test_slotconf` | the replayer stage's first row | there |
| `test_chip`, `test_sort`, `test_cc_units`, `test_cc_req`; `test_p4suites`, `test_ctl_suites`, `test_sort_suites` | v1.3's goldens and suites of the chip: v1.35's images under C&C's commands, C&C 1–3 as cores with `cc1.c`, v1.3's fingerprints of v1.35's state; the last three run the suite programs that `./run_tests.sh` has pending. What they checked of the chip end to end is checked on v1.36's by `test_rtl_sort` | each test ported to v1.36's chip or deleted (P11) |
| `fw/mesh-v1.2-draft.h`, `tools/regs/header_diff.sh`, `tests/test_header_diff.sh` | v1.35's comparison of its generated header with its v1.2 draft; it means nothing for v1.36's map (the files are kept, unused) | deletion at the firmware port (P11) |
| meshsim's own firmware (`fw/boot`, `fw/cc`, `fw/m1`, `fw/recipes`, `fw/replay`, `fw/sort`, `fw/tests`, `fw/tracer`) and `fw/build.sh` | written for v1.35's map and commands; the sources no longer compile against the generated `fw/include/mesh.h` (v1.36's). The committed images still pass `fw/check_images.sh` | a firmware stage. `fw/README.md` ("meshsim's own programs") says, program by program, what the RTL's firmware in `fw/rtl/` replaces (the boot ROM, the sort on its port layer for the RTL's four runs, the master's and the memory tile's programs, the core's programs, the tile programs the RTL has) and what waits (the recipes, `rdport.c`, `gather.c`, the search benchmark, the accounting programs, the other sort sets). Not started here: the runner has no `riscv64-unknown-elf-gcc`, which `fw/build.sh` pins |
| `bench/sort`'s `bench_sort` and `bench_chip` (`bench/sort/sort.mk`); `tests/sort_main.cpp` (`sort_test`), `sweep`, the sort kit (`tests/sort/kit/`) | they time or sweep v1.35's sort images on v1.35's machine (the M1 bench, C&C's commands, the phases from GO): they do not compile. `make build/<cc>-<mode>/bench_sort` prints PENDING and fails, so nothing builds a v1.35 machine by accident; `bench_threads` (the framework's own mesh) still builds and runs | the sort's runs for v1.36 beyond the RTL's four (section 4, "The sort's measurements"), then the benchmark on `tests/mach.h`'s chip |

Checks that left a ported test, and sources that are in no target any more:

| what | why | what closes it |
|---|---|---|
| `test_mach`: ref's `MachSelf.java` sequence (`tests/mach/machself.out`) | every case boots a v1.35 image through C&C's commands or C&C 1–3 as cores, and reads `SIM_EXIT`, `TRACE` and v1.3's phase log | deletion with the old suites (P11): the chip's boot is `test_rtl_sort`'s; the fixture stays until then |
| `test_mach`: ref's `NetSelf.java` cases but `packets`, `reads` and `a2a` (the first 47 lines of `tests/mach/netself.out`) | they drive v1.35's map through the core from the test, answer v1.35's `RESULT` requests, or print cycles that depend on v1.35's 128-entry request FIFO, its attach tiles 0–2 or the register block's view at the end of a cycle | the old suites (P11) |
| `test_router`: each of ref's runs past its first packet that v1.36's router routes otherwise; ref's three runs of a controller's router; ref's routing tables (`tests/routerconf/t*.gz`, `tables.txt`) | v1.36's router is the RTL's: it drops a packet for a reserved id (16.2), has no DMA source ids (16.4), and sends a packet at a tile its route does not pass the RTL's way; ref's scripts hold such packets from their first cycles (1 to 27 are compared) | nothing: the RTL's own router replaces ref as the reference (`tests/routerconf/rtl/`: ten scripts of random load, every cycle of every output, and all 67 routers' routes); ref's fixtures go with the old suites (P11) |
| `tests/phase_log.*` (v1.3's per-phase accounting, `Chip::phases()`) | its phases are C&C's GO stores named by command words, its counters v1.35's (the command block's overwrites, the send's stalls in the core), its CSV v1.3's byte for byte | the accounting suite (P11) |
| `tests/statefp_g1.*`, and with it `Chip::digest()` and `MESHSIM_DIGEST` | ref's `StateFp` of v1.35's state (the core, the register block, the command block) | a fingerprint of v1.36's state, if one is wanted (the timing stage) |
| `tests/golden_run.*`, `src/units/controller.*` | v1.35's M1 bench and the golden-trace driver: v1.35's core is deleted, so neither compiles again; kept for the pending tests that include them (the first table's first row) | deletion with those tests (P11) |

### 4. The steps of `./run_tests.sh`: ported, replaced, pending

Every step of v1.3's runner is still printed, in v1.3's order under v1.3's name, as one of three:
- it **runs** (ported, or v1.36's own): `layout`, `bus`, `rtl-images`, `rtl-tile` (acceptance A), `rtl-sort`
  (acceptance B: the 16-record runs and the run split over threads; `rtl-sort-64k` in `all`), `rtl-segment` (acceptance
  C: the generated segments; `rtl-segment-real` in `all`), `rtl-stream` (the master's host interface and UART on the
  chip), `rtl-memory` (the RW engine and the memory run on the chip), `mem`, `mem-shuf`,
  `mem-shuf2` (`test_mem`: v1.3's MemTest is retired, section 7), `core-programs` and `core-tile` (the RTL's core),
  `replayer`, `memsys`, `rvc`, `tracer`, `tracer-block-synth`, `tracer-block`; with `--check`, `bus`, `replayer`,
  `memsys`, `rvc`, `tracer` and the two `tracer-block` steps are compared with their reference logs and equal them;
- it is **`REPLACED (<by what>)`**: its v1.3 suite cannot run on v1.36's hardware, and tests written for that hardware
  stand for it, which the step runs (or names, when a step above already ran them); 22 in `quick`, 25 in `all`. A
  replaced step has no reference log: its tests' expectations are the specification's and the RTL's, in the tests, and
  a failing test fails the run;
- it is **`PENDING (<reason>)`**: nothing stands for it yet; 14 in `quick`, 22 in `all` (one more in `all` without
  `RTL_SORT_64K_KEYS`; without the workloads' data at `MESHSIM_INPUTS` one more in either, `tracer-block`, and another
  in `all`, `rtl-segment-real`).
Each of v1.3's keeps its `step` line under it in `run_tests.sh`; its suite's source and reference log stay as they
were until the old suites are deleted (P11).

**Replaced:**

| steps | v1.3's suite, and why it cannot run | replaced by | what of it is not covered |
|---|---|---|---|
| `net`, `net-shuf`, `net-shuf2` | `suite_net`, NetTest: v1.35's unexpected-packet and command conditions, C&C's attach tiles 0–2, start skews | `test_router` (the router against the RTL's own under random many-to-one load, cycle by cycle, and its routes; its shuffled and instance-major runs) and `test_special_router` (16.2–16.4 from the specification), run by `net`; the packets end to end are `test_mach`'s `packets` case (ref's, equal) and `rtl-sort` | NetTest's other fan-in tables on the whole mesh (its figures for receiver 27, which the RTL's `router_test` holds on the RTL's NoC, are `test_special_router`'s) |
| `engine`, `eng-shuf`, `agtest`, `agtest-shuf`; `agbench`, `s1bench` (*all*) | `suite_engine`, `suite_ag`, `suite_agbench`, `suite_s1bench`: v1.35's generator (per-stream gaps, lookahead as a setting), its write engine's FIFOs and the S1 margin over them | `test_ag` (8.2–8.5), `test_receive` (12.4, 12.5: the write path's fixed 2L + 2 cycles, the tables' two ports shared with the core), `test_gather`, run by `engine` and, in a shuffled order, by `eng-shuf`; the all-to-all is `test_mach`'s `a2a` case | the full-size all-to-alls (`engine-full`, pending) |
| `riscv` | `test_golden`: v1.35's core, deleted | the step `core-programs`: the RTL's own riscv-tests on the RTL's core, in the RTL's cycles | nothing |
| `cond`, `cond-shuf` | `suite_cond`: v1.35's handler and reports under C&C's commands | `rtl-sort` (every tile's error provoked: the NMI, the reports 24 and 25, `ERROR_LINES`, the toast pin, on the chip), `test_tile_control` (7.4), `test_ingress_egress` (17.6), run by `cond` | the reset after an error (the hard and soft resets are *Open*) |
| `regdoc` | `suite_regdoc`, RegDocTest: v1.35's tables and draft header | `test_regs`: `REGISTERS.md` and `mesh.h` are the generator's output; the table against the simulator's decode, the specification and the RTL firmware's definitions | the document's reset values read back from the simulator one by one (the RTL's `registers` tile program checks them on the tile: `rtl-tile`) |
| `hooks`, `hooks-shuf` | `suite_hooks`: v1.35's map, send port and pulses through the harness | the steps `regdoc`, `core-tile`, `rtl-tile` (the RTL's `registers` and `noc` programs) and `mem` (the write unit's send) | nothing of v1.36's hardware |
| `periph`, `periph-shuf` | `test_tileslotconf`: boot-ROM programs on v1.35's map and peripheral window | `test_periph_slot` (17.1–17.4 and the RTL's `peripheral_control` and `peripheral_dma`), run by `periph`; the RTL's `replayer` tile program in `rtl-tile` | the special tile's base with every kind of peripheral interface (acceptance A's `special` program, section 1) |
| `reqbus`, `reqbus-shuf` | `suite_reqbus`: v1.35's 128-entry request FIFO and its receive protocol | `test_request_tree` (chapter 11 from the specification and the RTL's request unit and tree) and `test_reqbus`, run by `reqbus` | the request link's beats (section 1) |
| `cccores`, `cccores-shuf`; `ccbench` (*all*) | `suite_cccores`, `suite_ccbench`: v1.35's C&C 1–3 as cores with `cc1.c` and `cc23.c` | `rtl-sort`: the master and memory tiles boot the RTL's images through the RTL's boot ROM, the tracer tile waits in it; the load's and each phase's cycles are the RTL chip's | nothing: the tracer tile's image runs in `rtl-segment` |
| `sort-p1` | `sort_test p1`: L1 and L2 under C&C's commands | `rtl-sort`: L1 and L2 of the RTL's runs, against the oracle and the RTL chip's cycles | the other key sets (`sort`, pending) |
| `replay-build`, `stageb` | Stage B: v1.35's replay controller on v1.35's tile, and its build check (retired: the segment stage) | `rtl-segment` and `rtl-segment-real`: the RTL's replay image on the chip; `rtl-images` for its build | the segment stage's Retired table |

**Pending:**

| steps | why | what closes it |
|---|---|---|
| `sw-build`, `sort-build`, `cc-build`, `recipes-build` | build checks of meshsim's own firmware, which is on v1.35's map and commands and is not ported (no `riscv64-unknown-elf-gcc` on the runner either); the images the chip runs are the RTL's, vendored with their hash list (the step `rtl-images`) | a firmware stage: each program ported or deleted (`fw/README.md` says which the RTL's replace) |
| `ctrl`, `ctrl-shuf`, `recipes`, `recipes-shuf`; `ctrl-full` (*all*) | `suite_ctrl`, `suite_recipes`: v1.35 firmware under C&C's commands (GO, DONE, the command block), which v1.36 does not have (7.3) | a firmware stage (the recipes rewritten for v1.36's map), or deletion (P11) |
| `rdport`, `rdport-shuf`, `searchbench`; `rdport-full` (*all*) | `suite_rdport`, `suite_search`: `fw/tests/rdport.c`, `gather.c` and the search benchmark, v1.35 programs. The read unit's reads through the arbiter are `test_mem`'s and the gather unit is `test_gather`'s and `test_mach`'s, without firmware | those programs on v1.36's map (a firmware stage) |
| `accounting`; `accounting-full` (*all*) | `suite_accounting`: v1.3's per-phase accounting (`tests/phase_log.*`) of phases named by C&C's command words | an accounting of v1.36's phases (the barrier's releases), with the measurements (section 6) |
| `sort`, `sort-shuf`; `sort-bt-kv32`, `sort-bt-k128`, `sweep-quick` (*all*) | `sort_test` and `sweep`: v1.35's sort firmware under C&C's commands, on v1.3's built-in key sets, the BT path forced, and the parameter sweep. `rtl-sort` covers the sort's correctness on the RTL's four runs (uniform keys, the direct and the LB paths) | `sort_test`'s sets as runs of the RTL's image (the master's firmware takes its run from its link: a firmware stage), and the measurements re-established (below) |
| `engine-full` (*all*) | EngineTest's full-size all-to-alls on four data sets, driven through v1.35's harness hooks and compared with ref's cycles | a port of those sets to `test_mach`'s all-to-all, with expectations from the specification's timetable |
| `skewtable` (*all*) | NetTest's fan-in under start skews: v1.36's chip has no start skew (the release is one flip-flop, in every tile in the same cycle: 18) | deletion (P11) |
| `rtl-sort-64k` (*all*) without the key file; `tracer-block` and `rtl-segment-real` (*all*) without the workloads' data | the RTL's 64K key file (16 MiB) and the real segments are outside the repository: `RTL_SORT_64K_KEYS`, `MESHSIM_INPUTS` (`RUNNING.md`) | the data on the host |

**The sort's measurements.** v1.3's and v1.35's figures for the sort are superseded and none is re-established here:
the phase tables and fingerprints of `tests/sort/` and `tests/golden-m2/` (v1.35's phases, from GO, of v1.35's
images), the full-size reference of 1,229,687 cycles (`RUNNING.md`, the long runs), the sweep's tables, the sort kit's
summaries, and `bench/sort`'s `bench_sort` and `bench_chip` (not built: `pending.mk`; `make` says so when asked for
them). What stands in v1.36 is the RTL chip's table (section 6), which `test_rtl_sort` checks, for the four runs the
RTL builds. Re-establishing the rest (the other key distributions, the BT path, the sweep over the parameters, the
kit, the speed benchmark on v1.36's chip) needs the sort's runs built for v1.36 beyond the RTL's four, which is the
firmware stage's (`spec/CHANGES-v1.35-to-v1.36.md` 2.10, "Measurements").

### 5. Behaviour the simulator still has from v1.35

Each is a difference from the RTL that the acceptances do not reach, or reach and survive. Where the old behaviour
would silently lose what the hardware keeps, the simulator stops (noted). The tile's units of chapters 8-15 are no
longer here: the DMA unit, the write and read units, the receive unit's write engine with `PTR` and `HIST`, the gather
unit, the inject unit and the scratchpad are the specification's (`src/units/README.md`).

| what | the RTL | the simulator | closes |
|---|---|---|---|
| the memory tile alone | `memory_tile_test`: the tile with a program of the test's: a read under a stalled attach output (the DMA waiting for the router), region `WRITE`s of 64 and 32 bits through the tile, an LPDDR error to `ERRORS.peripheral_1`, the NMI, the reports 24 and 25 and the error line | the tile's composition is checked in the chip (`test_rtl_memory`: the straps, the requests and their credits, the registers, the write port, the DMA's input, the events), and those three at the unit only (`test_rw_engine`: the room under a stall with the router input played, the three sizes, the engine's error bit and the slot's live bit); no one-tile bench for a special tile | the bench of section 1's `special` row |
| Ingress/Egress | the UART both ways, the host interface (AXI4-Stream) both ways, the LED and the toast pin (17.6) | the same, register for register (`uart.veryl`, `ingress_egress_unit.veryl`; section 8's note 10): the UART's line at the pins, both FIFOs, the stalls, the line errors, the host interface's FIFOs and ports at `STREAM_BYTES` 1 to 16. The serial line's far end and the LVDS IO block are not chip hardware: a test bench plays them through the model's per-cycle boundary (`IngressEgress::boundary`; `tests/ie_far_end.h`), and without one the inputs are idle, as the RTL chip's tests' `idle_inputs`. What is left is the nets' (section 6: an error line's level a cycle late) | closed (`test_ingress_egress`, `test_rtl_stream`, `tests/ie_teeth.sh`) |
| the tracer tile's memory system | *Open* behind the `fill` port (17.9.14); the RTL's chip tests place a model of it | on the chip the tests place the RTL test's model on the Tracer's fill port (`Tracer::FillPort`; the segment stage); the simulator's own `MemSystem` over a `FileStore` is the block-level tests'; a run with neither stops the simulator | the memory system's design |
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
| the router | a packet at an input in *t* is in its output's register in *t* + 2 unloaded; each output's queues of 16 (the loopback queue 64), full by the count at the start of the cycle, and its round-robin arbiter; `overflow` in the lost push's cycle; a DMA input's ready, the room of the queue its packet enters (16; `rtl/blocks/router`) | `test_router`, against the RTL's router cycle by cycle; `test_special_router` |
| a peripheral's DMA under a router input that is not ready | the packet register takes an entry when it is empty or its packet leaves, and offers it until the router takes it; the transfer ends the cycle its last packet leaves (17.4; `rtl/blocks/peripheral`, `peripheral_dma.veryl`) | `test_periph_slot` |
| `ERRORS.drift` | set at the end of each cycle a group transfer's read is offered and not taken (8.4; `rtl/blocks/dma_unit`, `address_generator.veryl`: `drift`) | `test_mach` (the whole tile), `test_ag` |
| the gather queue | a request leaves it in the cycle the offer register is empty or its read is taken, so one behind a read not taken stays; full by its count at the start of the cycle, the request then lost and `ERRORS.gather_overflow` set at that cycle's end (13; `rtl/blocks/gather_unit`, `gather_unit.veryl`) | `test_gather`, `test_mach` (the whole tile) |
| `core_status_canonical_wait` and `BARRIER_ARRIVE` | the level 1 in the cycle after each cycle of the canonical wait; the arrival's pulse two cycles after `wfi_sleeping` rises with `STATUS.arrived` set (the core specification's §1, Core ports; `rtl/blocks/tile_control_unit`) | `test_core_programs` (`WAITS`, the RTL's cycles), `test_rtl_tile` |
| a write's landing in a peripheral | presented on the write port `WRITE_PORT_LATENCY` = 2 cycles after the director takes it, and landed within `PERIPHERAL_LATENCY` of that; the replayer and the Tracer land it at the end of the port's cycle (12.3, 17.3; `rtl/blocks/replayer_unit/src/replayer_unit.veryl`, `rtl/blocks/tracer_unit/src/tracer_control.veryl`); the director's acceptance on the tile is the RTL's for a loopback packet (injected in T, accepted in T + 3) | `test_periph_slot`, `test_tracer_slot`; `test_mach`'s gather case |

**The sort on the chip.** `test_rtl_sort` checks every step, the empty phases, the total, the load and the link's
latency equal to the RTL chip's (`rtl/chip/README.md`, Results), in the four runs; the simulator is not tuned to them.
Cycles, a step from its release to the next step's, the total from L1's release to L2's all arrived:

| | 64 × 16 kv32 | 64 × 16 k128 | 64 × 64K kv32 | 64 × 64K k128 |
|---|---|---|---|---|
| from the reset to the load's release | 20,784 | 20,784 | 20,784 | 20,784 |
| the image's start-up: from the load's release to every tile's arrival, as the master records it | 16,033 | 16,033 | 16,033 | 16,033 |
| the same, to L1's release | 16,036 | 16,036 | 16,036 | 16,036 |
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

Every figure is the RTL's, the start-up's two among them, and `test_rtl_sort` checks each. **The start-up was never 3
cycles off:** the RTL's README gives "the image's start-up 16,033", which is `chip_test`'s line for release 1, from the
load's release to the master's record of every tile's arrival; this page compared it with the simulator's 16,036 from
the load's release to L1's release. The RTL's chip gives both: its master's summary in each of the four runs holds
phase 0 arrived at 20,780 and released at 20,784, phase 1 arrived at 36,817 and released at 36,820, the simulator's
words exactly (Snarky One's main at 076e647, a scratch test that runs `chip_test`'s set-up and reads the summary from
the master's DMEM; `BARRIER_RELEASE`'s flip-flop is 1 in the RTL's cycles 20,784 and 36,820, a cycle sooner than the
simulator numbers them). In the kv32 run of 16 records, every retirement of the master's core and of tiles 0, 27 and
63's, 52,810 in the first 39,990 cycles, is at the same pc in the same cycle in both (the RTL's `rvfi_valid` and
`rvfi_pc_rdata`, the simulator's retire trace). How the steps' figures came to be, each change a unit brought to the specification and
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
| the nets | `BARRIER_ARRIVE` a flip-flop's pulse the cycle after the tile is ready, a wire to Ingress/Egress; `BARRIER_RELEASE` a flip-flop of Ingress/Egress, in every tile in the same cycle; the error lines wires | the chip's nets at those latencies (`arrive_latency` 0, `release_latency` 1, `error_latency` 0), but a compute tile's or the memory or tracer tile's error line reaches `ERROR_LINES`, `ERRORS.tile_error` and so the master's NMI a cycle later than the RTL's wire: its level in cycle *t* is known at the tile control unit's step of *t* and crosses a Link of 1 + `error_latency`. KEPT, for the framework's reasons: a Wire cannot cross thread domains (most tiles are in another domain than the master tile); a Wire only where both ends share a domain would make a run's cycles depend on how it is split over threads, which the framework forbids (one result whatever the thread count: `src/README.md`); and within one domain the wire would run from a tile control unit through the row's net and the master's slot to the master's tile control unit, a loop for the framework, which gives every unit of a type one level (`src/config.h`, levels). The master tile's own line, to the toast pin, is exact (set at the end of the cycle the line is 1 in) | `rtl/tile/base/spec.md` 7.3, 18; `rtl/chip/src/chip.veryl` |

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
10. **Ingress/Egress's UART and host interface** (17.6 gives the frames, the FIFOs and the rules, and no cycle; all of
    these are `rtl/blocks/ingress_egress_unit`'s: `src/uart.veryl`, `src/ingress_egress_unit.veryl`, `README.md`,
    "Timing" and "Decisions"):
    - the transmitter's idle point is no frame or the last cycle of a stop bit; a byte is popped at the end of a cycle
      at an idle point with the TX FIFO not empty, and its start bit is on `uart_tx` from the next cycle: *p* + 2 for a
      store presented in *p* into an empty FIFO; frames are back to back, 10 *D* apart; `uart_tx` is a flip-flop;
    - each direction takes `UART_DIVISOR` at a frame's start, as it stands at the start of that cycle (the pop's cycle;
      the cycle the receiver sees the falling edge in); a divisor of 0, UB, wraps the counters (16 bits; the
      receiver's 17);
    - the receiver: two synchronising flip-flops and a third for the line's last value, so a falling edge on the pin in
      *c* starts a frame in *e* = *c* + 2; bit *k* is sampled in *e* + *k D* + ⌊*D* / 2⌋; the frame ends at the stop
      bit's sample, and the receiver is idle again from the next cycle; the start bit is not sampled;
    - "full" for `rx_overrun` and for both stalls is the FIFO's count at the start of the cycle, a pop in that cycle
      notwithstanding (the library's `queue`); a stalled store is taken in the first cycle at whose start its FIFO
      has room (`register_bus` presents it in every cycle; `lines_ready`, `stream_ready` from registers);
    - a line error raised at the end of a clearing store's cycle stays set; `UART_STATUS`'s TX count is the FIFO's
      alone (the byte on the line has left it);
    - a pop (`UART_RX`, `STREAM_RECEIVE`) is at the end of the load's cycle and only if the FIFO was not empty at its
      start: a byte or a beat entering at that cycle's end stays;
    - `lvds_out_tvalid` and `lvds_in_tready` are flip-flops loaded with the FIFO's next state: `tvalid` from *p* + 1
      for a send presented in *p* into an empty FIFO, the next beat (or 0) the cycle after a handshake; `tready` 0 in
      the first cycle after the reset, and the cycle after a handshake fills the FIFO, 1 the cycle after a pop;
      `lvds_out_tdata` and `_tlast` are the FIFO's head register; a beat in is read from the cycle after its handshake;
    - `STREAM_OUT_W0`–`W2` hold only the words below the beat's top; a store at or above the top, and the top word's
      bits past TDATA, change nothing; `STREAM_BYTES` is the chip's 4 (`Veryl.toml`), a setting of the model (1–16).
    In the simulator a load that pops is new to the slot: the peripheral's `load_pops` and `load_effect`, the window's
    static bits `Win::R_POPS` and the core's `CoreOp::LOAD` (`periph_slot.h`, `peripheral.h`, `tile_ctl.h`).
11. **The router** (`rtl/blocks/router`; `src/units/router.h`): a queue is full by its count at the start of the cycle,
    its push then lost even if the head leaves in that cycle, and `overflow` is in the push's cycle (16.3 says only
    that the push sets `ERRORS.router_overflow`); the eject's arbiter does not see the loopback queue; a packet for
    none as its dst is dropped like one for a reserved id, and a DMA input is always ready for either; on a special
    tile a packet from its attach port for another tile goes nowhere. A packet at a tile its route does not pass, which
    no link delivers, goes where the RTL's route functions send it (a unicast with no ring move east, with no column
    move south; a broadcast's ring copy by its rows): the conformance scripts hold such packets, the NoC never.
12. **A peripheral's DMA** (`rtl/blocks/peripheral`, `peripheral_dma.veryl`): a packet register between the queue and
    the router's DMA input; the transfer takes `DMA_HDR` at its start; it ends in the cycle its last packet leaves.
13. **`ERRORS.drift`** is set at the end of every cycle a group transfer's read is offered and not taken, whatever holds
    it back: a higher client, the gather unit busy, a write at its bank, no token (`address_generator.veryl`: `drift =
    group_valid && !read_request_ready`).
14. **The gather queue** (`rtl/blocks/gather_unit`, `gather_unit.veryl`): a request leaves it in the cycle the offer
    register is empty or its read is taken; the overflow's pulse is in the lost request's cycle, judged on the count at
    the start of that cycle.

### 9. Notes: what the RW engine's model takes from the RTL where the specification is silent

`src/units/rw_engine.h` is the RTL's `rw_engine_unit` (`rtl/blocks/rw_engine_unit/src/rw_engine_unit.veryl`, its
README's Timing and Decisions), on the slot's control block and DMA (`rtl/blocks/peripheral`). 17.7 leaves these to
the implementation, and the model takes the RTL's:

1. **The bursts** are the job's 64-byte groups: each burst runs from the job's next beat to the end of that beat's
   aligned group of 4 beats, or to the job's last beat (17.7: "their lengths are the implementation's").
2. **A read's room** (17.7: "only while the queue has room for every beat in flight"): a burst of *L* beats is
   raised only if the beats raised and not yet received, with *L*, fit in output queue 0's free entries at the start
   of the cycle; the entry the DMA takes in a cycle is still counted as held in that cycle.
3. **The cycles**: `START` presented in *p* takes the job's registers at the end of *p*; `STATUS.busy` from *p* + 1;
   the first burst raised at the end of *p* + 1, on AR or AW from *p* + 2, and a next burst at the edge after the
   address channel frees; a write's first beat on W in *p* + 3 and a beat a cycle while WREADY is 1 (the buffer's row
   read in every cycle, the pending one again until the W register takes it); a job of 0 beats ends in *p* + 1; a
   read ends in the cycle its last beat comes and a write in the cycle of its last response, `EVENTS.done` posted
   then. 21.5's row gives the first three of these.
4. **The write data may run ahead of its address**, and the writes outstanding have no bound of their own; AR and AW
   share their address and length register; RREADY and BREADY are 1 and RLAST is not read.
5. **The write port** lands a `WRITE` at the edge that ends the cycle the port presents it (the RTL's README counts
   that as 1 of `PERIPHERAL_LATENCY`'s 6); a write past the buffer's end lands at its row within the buffer, a misaligned
   one lands aligned, one of the reserved size writes nothing (all UB: the model also flags them).
6. **The reply**: `RW_REPLY_W0` is a register, 0 at the reset; a store to `RW_REPLY_SEND` pushes at the end of its
   cycle, into room at its start, and clears `RW_REPLY_W0`; a push to a full queue 1 (UB) is lost, `RW_REPLY_W0`
   cleared all the same (flagged). Queue 1 holds one entry.
7. **`lpddr_error`** is set by any response but OKAY, EXOKAY among them, at the end of its cycle.
8. **A `START` in the cycle a job ends** starts the next (the RTL's test does it); a `START` while busy otherwise is
   UB and stops the simulator. `RW_CONFIG`, `RW_LPDDR_ADDR` and `RW_BUFFER_ADDR` are 0 before their first store and
   keep their values through a reset (the RTL does not reset them: undefined, 17.7).
9. **The slot's DMA** (`peripheral_dma.veryl`): the packet register is the slot's (`SlotCore::Dma`: `full`, `pkt`), one
   packet in front of the router's DMA input, offered until the router is ready for it (16.4: the room of the queue it
   enters; `router.h`, A DMA INPUT). A transfer ends in the cycle its last packet leaves the register; with a count of 0
   it ends the cycle after `DMA_START`'s. So under a stalled router input a read's beats stay in output queue 0 but the
   one in the register, as the RTL's: `test_rw_engine` checks the engine with the slot's register against its reference
   under every stall (the bench gives `packet_ready`), and `test_rtl_memory` the chip, cycle for cycle.
10. **The LPDDR controller is not chip hardware**: the `lpddr` port is the simulator's boundary, a harness places an
    AXI4 slave on it (`LpddrSlave`), and a job that raises a burst with none stops the simulator. The reset is not
    the port's: a harness that resets the engine resets its slave (3: "the LPDDR controller, which shares the
    reset").
