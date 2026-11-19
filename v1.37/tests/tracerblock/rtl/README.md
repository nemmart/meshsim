# The RTL's figures for the Tracer, run by run

*For anyone checking the simulator's Tracer against the RTL's: what these twelve files are, where they came from, and
why they are never regenerated here.*

Each `windows-<workload>-s<segment>-c<compression>-l<latency>.txt` holds one line a run (64 runs, a window each) of
the RTL's `tracer_unit` on a real workload's segment: EVM Zone (`evm-zone-23`) segments 0 and 1 and Cyclone
(`cyclone-22`) segment 0; `tracer_compression` 0 and 7; a fill read answered 1 cycle after its request (`l1`), or 55
cycles for the guest's file and 35 for the map's (`l55-35`).

**They are the RTL's, measured on its simulation, and the expectations of `run_tests.sh`'s step `tracer-block`**
(`tracer_block --ref`, `tests/tracer_block_main.cpp`): every run's cycles, counters and counts on the simulator must
equal its line. **Never regenerate them from the simulator.** A hardware change is first made in the RTL, and these
files are then produced again by the RTL.

## Where they came from

Snarky One `main` at `370e3ac`, 2026-11-14: its acceptance test
(`rtl/blocks/tracer_unit/test/src/tracer_acceptance_test.rs`), instrumented to print these lines and changed in no
cycle (every total of the unmodified test is reproduced), on Verilator. From a scratch worktree of Snarky One with
the instrumented test, for each compression and latency:

```sh
mise run build
TRACE_DUMP=<this folder> TRACE_COMPRESSION=0 TRACE_FILL_LATENCY=55,35 TRACE_THREADS=3 \
  mise exec -- cargo --locked --offline test -p blocks_tracer_unit -- --ignored --nocapture tracer_acceptance
```

(`TRACE_COMPRESSION` 0 or 7; `TRACE_FILL_LATENCY` 1 or 55,35.) The workloads' data is read from `~/meshsim-inputs`
(`MESHSIM_INPUTS`), outside both repositories. The instrumentation also writes each row's start and completion cycle
(`rows-*.txt`, 1 MiB a run), which `tracer_block --rows DIR` compares; those are not kept here.

## The bench

The RTL test's bench is `tests/tracer_block.h`'s, rule for rule: an ideal controller making one register access a
cycle, the write port's one WRITE a cycle, the router always ready, and a memory system on the fill port answering at
the fixed latency. **Cycle *n*** is the *n*-th clock cycle after the reset is released, cycle 0 the `EVENTS_MASK`
store; the counter runs on through the segment.

## The columns

Space separated, one `#` header line.

| Column | Contents |
|---|---|
| 1–7 | `workload`, `segment`, `run` (0–63), `compression`, the two latencies, `rows` (65,536) |
| 8, 9 | the cycle of `START`'s store; the cycle its effect is seen in (the same) |
| 10, 11 | the cycles of the run's first row's start and its last row's completion |
| 12 | the cycle of the rows' end (column 11 + 1) |
| 13, 14 | the cycles of the `EVENTS` loads that first returned `state_ready`, and `done` |
| 15 | the cycles from `START` to the rows' end, as the RTL's test measures them: column 13 − column 8 − 1 |
| 16 | the drain's cycles: column 14 − column 13 + 1 |
| 17–22 | of the run, from the window's files: instructions, padding rows, M rows, loads, stores, core-fed expansions posted |
| 23 | the rows' schedules: 4 a row, 6 an M row |
| 24 | the cycles in no counter: column 15 − column 23 − the run's increase of the three stall counters |
| 25–32 | `STAT_CYCLES`, `STAT_STALL_FETCH`, `STAT_STALL_DATA`, `STAT_STALL_MAP`, `STAT_STALL_OUTPUT`, `STAT_MISS_I`, `STAT_MISS_D`, `STAT_PROBES`, loaded after `done`: cumulative over the segment |
| 33–35 | the fill port's read requests to the guest's file and to the map's, and its write requests: cumulative |
| 36 | the cycle of the `STAT_CYCLES` load (column 14 + 5) |

`tracer_block --ref` compares columns 8, 10, 11 and 13–35.
