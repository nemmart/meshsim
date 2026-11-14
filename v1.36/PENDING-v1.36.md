# What v1.36 still waits for

One row an item: what, why, what closes it. (The Tracer's stage, branch `v1.36-tracer`; other stages add their rows.)

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
