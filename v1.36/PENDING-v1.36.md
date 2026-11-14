# Pending in v1.36

*For whoever merges or continues v1.36: what is not ported yet, why, and what closes each item. One row per item;
a row is removed by the change that closes it.*

## The core stage

| What | Why | What closes it |
|---|---|---|
| The chip, the sort and every suite on the RTL's core (`run_tests.sh`'s step `core-chip`) | The tile's register block, C&C and firmware are still v1.35's, and they do not run on the RTL's core: C&C lands a tile's commands a fixed time apart, which the slower core misses (a command lands before the last is read: on the pipeline with v1.35's function kept, commit 8a444d1c, `ctrl`'s `wfi` group read a wrong command on all 64 tiles and every sort set failed "no invariant violations"), and the M1 goldens replay their events at `ref`'s cycles. So the chip runs v1.35's core, whole, behind the one option `v135_platform` (`src/units/rv32.h`, `rv32_v135.h`), and every v1.35 reference stands unchanged. | The platform stage: with the RTL's register map, boot ROM and firmware in place, clear `v135_platform` where the tile's core is made, delete `src/units/rv32_v135.h`, the option and the lines of `rv32.h` marked `v135_platform`, with `tests/test_rv32.cpp` and `tests/test_core_m2.cpp` (v1.35's core's own tests), and regenerate or retire the references that hold v1.3's cycles (the M1 goldens and fingerprints first). |
| The tile's environment of the core in the RTL's map (`rv32.h`'s `load`, `read_reg`, `store`, `may_stall`) | Those functions are v1.35's register map (`regmap.h`), the platform stage's. The RTL's core runs on them as they are (`tests/test_core_m2_rtl.cpp`), but an access fault in region 0xFF is found only when the access is made, so a faulting register-window load is answered in q + 2 where the RTL's `register_bus` answers every fault in q + 1; and which stores may stall is v1.35's list (`REQUEST_SEND`, the sends). | The platform stage: the RTL's map decides a fault from the address and the straps alone, so `route` can answer it in q + 1 without making the access, and `may_stall` takes the RTL's list (`WRITE_SEND_32/64/128`, `REQUEST_SEND`, Ingress/Egress's `UART_TX`, `STREAM_SEND`, `STREAM_SEND_LAST`). |
| The coprocessor port | The tile has no accelerator (the RTL builds none), so custom-0 and custom-1 are illegal instructions, as on a core with `coprocessor_present` 0. The RTL's `program_test` sets it to 1, but none of its programs has a coprocessor instruction. | The field accelerator, when the RTL has one. |
| An instruction or data port that holds ready low, or answers a fetch later than the next cycle | No memory of the tile does either (IMEM, DMEM, the boot ROM and `register_bus` accept in every cycle), so the model has neither; the RTL's test runs each program a second time with randomly delayed ports, which the model cannot follow. | Nothing in the tile; a unit that needs it. |
| The core's rules one by one (`test_rv32_rtl`: 62 directed tests of the RTL's core from the contract's sentences, on a bus and on the tile's environment) | Written by a sub-agent, which could not place the file in `tests/` (its write there was refused), so it is not in the tree: it is in the core stage's scratch folder, `build-scratch/rtl-tests/test_rv32_rtl.cpp` (untracked, outside `v1.36/`). 60 of its tests passed on the model; one found SIM_EXIT's store retiring twice (fixed since, with `test_core_m2_rtl`'s own test), one expects a faulting register-window load answered in q + 1 (the row above). What the RTL's programs do not reach and only that file checks: a CSR write to `mcycle` or `minstret`, `minstret` on a trapped instruction, the store-misaligned cause, a data target slower than one cycle on a bus, and the exact cycle `core_status_canonical_wait` rises. | The owner's or coordinator's decision to add the file as `tests/test_rv32_rtl.cpp` (link rule: `$(B)/test_rv32_rtl: $(B)/src/units/trace_log.o`), after reviewing it. |

## The replayer stage

The replayer's model is the RTL's `replayer_unit` at the block level: its rows, its cycles and every port's
observations equal the RTL's (`tests/replayer/README.md`, `tests/acceptance/README.md`).

| what | why | what closes it |
|---|---|---|
| `test_slotconf` (`tests/slotconf/seed1-5`) | the slot's conformance replays ref's seeded scripts on a slot with the replayer in it; its reference is ref's observations of v1.35's replayer under them (busy, errors, events and queue occupancies cycle by cycle), which the RTL's replayer does not give | the platform stage's slot (17.1-17.3): a conformance of the new slot, whose replayer side is `test_replayconf`'s |
| the replayer's window as the slot presents it: `START` at word 6, `EVENTS` at 3, `EVENTS_PEEK`, `EVENTS_MASK` (17.1) | the slot is the platform's; the model keeps v1.35's calls (`run`, `consume_events`, `events`, `write_word`, `word`) and its own words 32-35 | the merge with the platform stage: `Replayer::W_*` are the only word numbers here |
| the write port's latency in a chip: `accept(t)` presents the packet at `t + RP_LATENCY` | the director's and the slot's links are the platform's (`PERIPHERAL_LATENCY`, 17.3) | the platform stage; `Replayer::write(t, ...)` is the port itself |
| Stage B and Stage F's cycles (`tests/stageb/stageb.log`, the acceptance kit's B and F) | they run v1.35's `replay_ctl.c` on v1.35's tile with the new replayer in its slot: rows and end state pass; the cycles are a mixture | the platform and firmware stages (17.8.21, 17.8.22), then the RTL chip's figures (3.0875 and 3.1249 a row) |
| the row port's handshake | the model's consumer is its sink, ready one cycle in `sink_interval` (1: always, as the chip ties it); no unit reads `replay_row` | the materializer, *Open* (17.8.11) |
| `spec/CHANGES-v1.35-to-v1.36.md` 5.2.6 item 1: the timing rules the specification does not state | the model takes them from the RTL's sources: they are listed in `src/units/replayer.h`'s header and in the stage's report | a ruling: write them down (the guide suggests an appendix), or leave them to the RTL |

## The Tracer stage


## Pending

| What | Why | What closes it |
| --- | --- | --- |
| `chain` (`tests/chain_main.cpp`) and `t2` (`tests/t2_main.cpp`): the segment on the full chip | They boot the tracer tile's firmware (`fw/tracer/tracer_ctl.c`, `fw/include/mesh.h`'s `TR_EV_*`), which is v1.35's: its `EVENTS` bits are 0, 1, 3, 5, 6, 7–10, the Tracer's are now packed from bit 0 (17.9.1, item 161). Both build; `chain` on EVM Zone's segment 0 stops with 0 rows. They are in no suite (`RUNNING.md`, "by hand"), nor is `tests/freeze/body.sh`'s item 3 and 4, which run them | The platform's register map and the segment's firmware (17.9.10, 17.9.15: the handshakes by requests), the later stage |
| `tests/golden-m3/` (`RUNS`, `*.chip`, `boot/*.boot.fp.gz`) and `tests/golden-chain/`: T2's and the chain's recorded figures and fingerprints | They are v1.35's Tracer's on the full chip, cycle for cycle (`tr`, `tr.ic`, `tr.dc`, `tr.mem`, `tr.files` at every 100,000 cycles); no test in a suite reads them | Regenerated with the segment's stage, from the RTL's chip figures where it has them |
| `tracer_block`'s step `tracer-block` without the workloads' data | The real segments are read from `$MESHSIM_INPUTS` (`~/meshsim-inputs`), outside the repository; without it the runner prints the step as PENDING. `tracer-block-synth` always runs | The data on the host |
| `tools/freeze/strip.sh` line 49 names `bench/tracer/tracer_play.cpp` and `bench/tracer/tracer.mk` | Both are retired (below) | The freeze tool's owner drops the pair |
| The memory system's model with a read and a write outstanding together (item 174) | Decided here, for the simulator only: the model serves one access at a time, so a request taken while another is being served starts when that one ends (`MemSystem::port_read`, `port_write`); a write to a line that is not the file's own answers `error` a cycle after its handshake | The memory system's design (items 60, 119) |

## Retired, with what replaces it

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

## Notes: what the Tracer's model takes from the RTL where the specification is silent or differs

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
