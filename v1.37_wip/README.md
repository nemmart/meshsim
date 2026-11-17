# meshsim v1.37

> **v1.37 is v1.36 with the first end-to-end path** (`../updater_wip/`, WORKER3). The simulator is started with one
> file, the packets that come over the link from the host, and with N and s; the master tile's firmware
> (`fw/cc/master.c`) reads the frames out of Ingress and drives the Updater; the Updater builds the memory images;
> the Tracer traces its segment on them, and the compute tiles replay it. Ingress's host side and the Updater are
> software black boxes behind their real registers. **The forward count** (WORKER7, the owner's design; in place of
> WORKER4's threshold): Ingress moves a frame's bulk data to the Updater itself, over a flow-controlled word port, or
> drops it; the master writes the frame's length once and wakes a few times a frame, not for its words.
>
> | to | read |
> |---|---|
> | run it | `RUNNING-v1.37.md` |
> | see what v1.37 adds, piece by piece, and what specifies each | `CHANGES-v1.37.md` |
> | see how it was checked, and its figures | `../updater_wip/planning/sessions/WORKER3-REPORT.md`; for the threshold, `../updater_wip/planning/sessions/WORKER4-REPORT.md`; for the forward count, `WORKER7/REPORT.md` there |
>
> **Everything else in this folder is v1.36's, copied unchanged (main, 67aaeb86), and reads as v1.36:** this page
> below the line, `BUILDING.md` and `RUNNING.md` but for what they mark v1.37, `OPEN-v1.36.md`, `spec/`,
> `tools/freeze/README.md`. v1.36's machine is what the folder builds unless a machine asks for v1.37's master tile
> (`MachConfig::host_link`), so every test, suite and figure of v1.36 below still holds here. The specification is
> not changed: what v1.37 adds is specified by the design documents of `../v1.4_wip/planning/`.

---

# meshsim v1.36

*For anyone arriving here: what v1.36 is, how it was checked, where to start for what you want to do, and what of
v1.35 it does not carry over.*

**v1.36 is the mesh processor as the RTL builds it: the RTL's hardware, in the RTL's terms, as a specification and a
simulator.** The machine is a 64-tile mesh processor; the RTL is Snarky One, the hardware design. The RTL was written
against v1.3 and v1.35, and building it decided what they left open and changed some of what they decided. v1.36
takes all of it back:

- **the specification** (`spec/`) is the RTL's (the platform and the core, with their names, register layouts and
  vocabulary), with what meshsim's specification has beyond it kept in the same terms;
- **the simulator** (`src/`) models the RTL's hardware at the register, the packet and the cycle: the RTL's own
  programs ran on it unmodified with the RTL's results and the RTL chip's cycle counts, and the chip now runs
  meshsim's own firmware, v1.35's upgraded to v1.36 (`fw/`: every image built here from sources here);
- **the suites** (`tests/`, `run_tests.sh`, `make check`) check the simulator against the specification and against
  the RTL's own simulation. The RTL is the truth about the hardware: where the specification and the RTL differ, the
  RTL wins and the specification is corrected (`spec/GUIDELINES.md`).

What only made sense on v1.35's hardware is not here: "What v1.36 does not carry over", below.
`spec/CHANGES-v1.35-to-v1.36.md` is the guide from v1.35: what changed, unit by unit, the timing table and the name
table from v1.35's terms. `OPEN-v1.36.md` is the record of what is still open: where the simulator differs from the
RTL, what of the RTL's tests has no counterpart here, and what the simulator takes from the RTL's sources where the
specification is silent.

## Where to start

| you are | you want | start at |
|---|---|---|
| the RTL engineer | what the specification says of the RTL, and what waits for a ruling | `spec/README.md`, then `spec/CHANGES-v1.35-to-v1.36.md` |
| a reader of v1.35 | what changed, and what is gone | `spec/CHANGES-v1.35-to-v1.36.md`; "What v1.36 does not carry over", below |
| a firmware author | the register interface, and the firmware the simulator runs | `fw/README.md` |
| someone running the simulator | to build it and run its suites | `BUILDING.md`, `RUNNING.md` |
| the proof-pipeline user | to make inputs and run a replay or a segment | `RUNNING.md`, `pipeline/README.md` |
| someone adding or changing a unit | which file models which unit, and how a unit is written | `src/units/README.md`, `src/README.md` |
| someone who needs to trust a result | what each test is checked against | `tests/README.md`, `OPEN-v1.36.md` |
| the owner | to know it is right without reading it all | `spec/OVERVIEW.md`, then this page's Status |

## Layout

| folder | what |
|---|---|
| `spec/` | the machine specification: the platform, the core, the Jolt-specific peripherals, the schedule, the guide from v1.35, and the history |
| `src/` | the simulator: the framework (`src/`, `src/base/`) and the model of the chip (`src/units/`) |
| `tests/` | the tests and suites, their fixtures, and the RTL's references they are compared with |
| `fw/` | the register interface (`fw/include/mesh.h`, `fw/REGISTERS.md`) and the firmware: v1.35's upgraded to v1.36, the RTL's boot ROM, tile and core programs, the script that builds them (`fw/build.sh`) and the committed images |
| `data/` | the replayer's tables and the data sets |
| `pipeline/` | the offline input tools (Java) |
| `tools/` | helper scripts and tools: the register-table generator, the delivery converters, the sort runs' keys' generator, the documents' check |
| `bench/` | the simulator's speed benchmark |
| `planning/` | history: v1.35's decisions, plans and session reports |

`Makefile` and `run_tests.sh` are the build and the suite runner (`BUILDING.md`, `RUNNING.md`).

## Status

**Every stage of the guide is done.** The simulator is the RTL's hardware, unit by unit, and each unit was checked
against the RTL's own block and then on the chip under the RTL's firmware; the chip now runs meshsim's firmware. v1.35
(`../v1.35/`) is the version before.

| what | the model | checked by | against |
|---|---|---|---|
| the core | the RTL's `rv32_core`, cycle for cycle | `test_core_programs`, `test_core_tile`, `test_rv32_rtl` | 48 of the RTL's 49 programs (not its Rust one), CoreMark, the interrupt programs and the boot ROM: every retirement and trap in the RTL's cycle (`tests/core-programs/`); the core specification's §1 and machine-spec 6.4, test by test |
| the tile | the register map, the register bus, the tile control unit, the boot; the DMA, write, read, request, receive, gather and inject units and the scratchpad (machine-spec 5–15) | `test_regs`, `test_tile_control`, `test_ag`, `test_receive`, `test_mem`, `test_gather`, `test_mach`, `test_rtl_tile` | the specification and the RTL blocks' READMEs; the RTL's tile programs on a tile, with the boot ROM's cycles |
| the NoC | the RTL's `router`; the request tree | `test_router`, `test_special_router`, `test_request_tree`, `test_reqbus` | the RTL's router under scripted random load, cycle by cycle (`tests/routerconf/`); chapters 11 and 16 |
| the peripherals | the control block and the DMA; Ingress/Egress with its UART and host interface; the RW engine | `test_periph_slot`, `test_ingress_egress`, `test_rw_engine` | 17.1–17.7; the RTL's `rw_engine_unit` under its own test, cycle for cycle (`tests/rwengine/`) |
| the replayer | the RTL's `replayer_unit` | `test_replayer`, `test_replayconf`, `suite_replayer`, `replay_shard` | the RTL's observations of scripted passes, every cycle (`tests/replayer/`), and its cycles on real windows (`tests/acceptance/rtl/`) |
| the Tracer | the RTL's `tracer_unit` | `test_tracer`, `test_tracer_rules`, `suite_tracer`, `tracer_block` | 17.9; the RTL's figures run by run on real segments (`tests/tracerblock/rtl/`) |
| the chip | 64 compute tiles, the master, memory and tracer tiles, the request tree, the nets | `test_sort`, `test_segment`, `test_memory`, `test_rtl_stream` | the RTL's chip tests' result checks, under meshsim's firmware; the stream run in the RTL chip's cycles: below |

`./run_tests.sh all --check` runs all of it (`RUNNING.md`); `make check` runs every test in the debug and release
builds with gcc and clang (`BUILDING.md`).

### The acceptance: every figure equal to the RTL's

The figures of the blocks below are the RTL's, from Snarky One's simulation or its READMEs, and the test fails unless
the simulator gives the same. None was made by tuning: each unit was brought to the specification and the RTL's
block, and the chip's figures followed: **chip-level cycle equality with the RTL, under the RTL's own firmware (its
sort runs, its segments, its memory run), was established at meshsim's commit 5a87d658, and is no longer checked**,
since the chip now runs meshsim's own firmware (`fw/`: v1.35's, upgraded to v1.36). The chip's tests check its
results; their cycles are printed, the simulator's own figures for that firmware:

| run (`test_sort`, `test_segment`, `test_memory`) | checked | cycles, as printed |
|---|---|---|
| the sort, 64 × 16 records, kv32; k128 | every tile's records (and values) against an oracle written apart from the design; every tile's error provoked and followed to the toast pin | L2's arrivals at the master's mcycle 171,472; 174,406 |
| the sort, 64 × 64K records, kv32; k128 | the same | 973,263; 1,328,198 |
| the segment: 4 × 3,000 rows; 64 × 1,500; 1 × 65,536 | every row, record, stream, end state and program result against the RTL test's segment | to C&C 1's exit, polled every 512 cycles: 140,288; 1,050,624; 749,056 |
| the real windows: EVM Zone 0–63, 64–127; Cyclone 0–63 | every row, record and stream against the recorded files | the replay a row: 3.4458, 3.3949, 3.4751 |
| the memory run (a `READ`, a `WRITE`, a `READ` back, 300 `WRITE`s of 0 beats) | every reply and beat against the LPDDR model, and the bursts | the done word after 36,058 cycles (the RTL test's slave) |

**The stream run and the UART** (`test_rtl_stream`: the RTL's `chip_stream_test.rs`, its master program written in
the test), still against the RTL chip's cycles:

| | the RTL chip, and the simulator |
|---|---|
| the stream run (the master echoing beats on its AXI4-Stream ports): the done word | after 513 cycles; every handshake's cycle on `lvds_in` and `lvds_out` |
| the UART through the master's core (20 bytes at a divisor of 20) | the first start bit in cycle 34; the done word after 5,210 cycles |

**The blocks.**

| | the RTL, and the simulator |
|---|---|
| the core: CoreMark, 10 iterations (`tests/core-programs/BENCH`) | 3,602,722 cycles, 2,746,880 instructions: 2.776 CoreMark/MHz, 1.312 cycles an instruction |
| the replayer alone on the recorded windows (`replay_shard`; `tests/acceptance/rtl/`) | EVM Zone, 128 windows: 25,818,054 cycles, 3.0778 a row; Cyclone, 64 windows: 13,010,346 cycles, 3.1019 a row; every pass's cycles, in each of the three record variants |
| the Tracer alone on real segments, a fill read answered in 1 cycle (`tracer_block`; `tests/tracerblock/rtl/`) | from `START` to the rows' end: EVM Zone's segment 0, 19,661,953 cycles, 4.6878 a row; its segment 1, 17,777,578, 4.2385; Cyclone's segment 0, 18,525,430, 4.4168; every run's cycles and counters |

**What is open** is `OPEN-v1.36.md`; what is open in the design is `spec/machine-spec.md` chapter 26.

## What v1.36 does not carry over

v1.35 was v1.3's machine in C++, with its own firmware and v1.3's suites. v1.36 is the RTL's machine. What ran only
on v1.35's hardware (C&C's commands, GO and DONE, the command and reply blocks, v1.35's map and core) could not be
kept running, and is not in this folder; it is all in `../v1.35/`. For a reader of v1.35 looking for it:

| v1.35 had | in v1.36 |
|---|---|
| meshsim's own firmware in C and assembly | C&C 1's program, the replay and Tracer controllers, the sort, `crt0.S` and `app.ld`, upgraded to v1.36 under their v1.35 names, built here (`fw/README.md`, "What changed"). Not carried: the boot ROM (the RTL's is hardware content), C&C 2's and 3's program and the two test variants of C&C 1's, the M1 programs, the tile test programs, the replay controller's old-map and 8 KB-log builds, v1.35's riscv-tests build (the RTL's programs are in `fw/core/`) |
| the recipe book and the page of control conventions, both under v1.35's `fw/` | nothing: they are v1.2's draft header, commands, GO and the command block. The conventions a program follows are the specification's (6.5, 7.3, 11.5), and the firmware is the example (`fw/`) |
| the sort's other key sets (sorted, reverse, equal, few, ragged, the trace and Zipf sets), `sort_test`, the sort kit with its 92 runs | nothing beyond the RTL's four runs: uniform keys, 64 × 16 and 64 × 65,536 records, 32- and 128-bit values (`test_sort`) |
| the BT path forced (the steps `sort-bt-kv32` and `sort-bt-k128`), the parameter sweep (`sweep`) | nothing |
| the full-size sort's figure, 1,229,687 cycles | superseded: the RTL chip's table above (64 × 64K k128: 1,306,907 cycles, from L1's release to L2's all arrived) |
| the per-phase accounting (the phase log, `suite_accounting`, the phases' CSV) | nothing |
| the search benchmark, the `rdport` and `gather` programs (`suite_search`, `suite_rdport`) | nothing: their programs are v1.35's tile test programs, on commands, and their suites v1.35's. The read unit, the gather unit and the inject unit are tested from the specification (`test_mem`, `test_gather`, `test_mach`) |
| Stage B (the replay controller on one tile) and Stage F (on the full chip) of the acceptance kit | `test_segment`: v1.35's replay controller, upgraded, replays the Tracer's deliveries on the chip. Stage A, the replayer alone, stays (`replay_shard`, the kit) |
| T2 and the chains (v1.35's tracer-tile firmware on the full chip; segments 0–127 back to back) | `test_segment`'s real segments, on v1.35's tracer-tile firmware upgraded, one host segment a run. Nothing carries a segment's end memory into the next: the memory system is *Open* (17.9.14) |
| the Java reference Tracer's call logs as the Tracer's conformance (TracerConf) | the RTL's figures and the specification: `tracer_block`, `test_tracer_rules`, `suite_tracer` |
| start skews (the step `skewtable`, the skew settings) | nothing: the release is one flip-flop, in every tile in the same cycle (18) |
| C&C's commands, C&C 1–3 as cores, the command block (`suite_ctrl`, `suite_cccores`, `test_chip`) | no commands (7.3): C&C 1's program takes its harness's commands from its image's data. The special tiles are tiles: the tracer tile boots through the RTL's boot ROM, the master and memory tiles from placed images (`test_sort`, `test_memory`, `test_segment`) |
| the M1 bench and its goldens (retire traces and fingerprints of v1.35's core) | the RTL's core programs, in the RTL's cycles (`test_core_programs`) |
| the conformance fixtures of v1.35's units against v1.3's Java simulator (EngConf, MemConf, SlotConf, C&C 1's peripheral, CheckSelf) | tests from the specification and the RTL's blocks (the Status table). CheckSelf's cases on hardware v1.36 keeps are `test_checker` |
| the speed benchmarks of the sort and the chip (`bench_sort`, `bench_chip`) | nothing; `bench_threads` (the framework's) stays |
| the v1.2 draft header and its comparison with the generated one | nothing: v1.36's header is generated from v1.36's register table and compared with the RTL firmware's own definitions (`test_regs`) |
| v1.35's freeze tools (the copy from the work folder, the comment strip, the freeze check's task) | `tools/freeze/README.md`: v1.36 is frozen in place |
| v1.35's page of changes from v1.3 and its errata | they are v1.35's pages, in `../v1.35/`. v1.36's are the guide (`spec/CHANGES-v1.35-to-v1.36.md`) and `OPEN-v1.36.md` |

## Glossary

| term | meaning |
|---|---|
| meshsim | the simulator, whatever its implementation |
| the RTL | Snarky One, the hardware design; v1.36's specification is its specification. A path written as Snarky One's (its `rtl/` folder, its READMEs) is in its repository, not in this one |
| `ref` | the reference v1.35 was checked against: v1.3's Java simulator. A few of its fixtures are still used where v1.36's hardware is unchanged (`tests/README.md`) |
| D*n* | decision *n* of v1.35, in `planning/DECISIONS.md`; v1.3's are in `spec/history/` |
| `[meas: …]` | in the spec, a measured figure and what it was measured on: the RTL, or a version of the simulator (`spec/machine-spec.md`'s preamble) |
| acceptance A, B, C | in the tests' comments: the RTL's tile programs on a tile (`test_rtl_tile`), the sort on the chip (`test_sort`), the segment on the chip (`test_segment`) |
