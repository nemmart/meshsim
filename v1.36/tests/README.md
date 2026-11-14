# tests: how meshsim is checked against v1.3

*For anyone who needs to trust or rerun meshsim's results: how exactness against v1.3 is established, and what each
fixture folder is and how it was made.*

## Three kinds of evidence

v1.35's meshsim reproduces v1.3's Java simulator (`ref`: v1.3's with the trace hooks, outside the frozen copy; v1.3's own is
`v1.3/meshsim/`): the same results, and the same cycle counts
except for the deliberate differences (D30; `spec/CHANGES-v1.3-to-v1.35.md`). Each fixture below was made by running
`ref`; each test compares meshsim with it.

- **Goldens:** what `ref` printed or traced for a whole program or run (a retire trace, a suite's log, a sort's
  fingerprints), compared line for line. A suite's log is compared with its timing masked (`./run_tests.sh --check`);
  cycles are never masked.
- **Conformance drivers:** one unit, or one kind of port, driven by a scripted sequence of inputs every cycle, the same
  script in `ref` and in meshsim; the observations (`*.obs`, from `*.script`) must be equal. They cover the edges a
  whole program rarely reaches. The port kinds' rules are `src/PORTS.md`.
- **Fingerprints:** a canonical hash of the machine's state at fixed points (`*.corefp`, the sort's `fp`, the chains'
  `*.starts`), so a long run is checked without a full trace. The formats are `TRACE-FORMAT.md`.

**v1.36, against the RTL.** The core, the replayer and the Tracer are the RTL's blocks, cycle for cycle, and their
evidence is the RTL's own: Snarky One's core programs with the cycles, retired instructions and retire traces its
simulation gave (`core-programs/`, which `test_core_programs` requires of the model exactly), the replayer's recorded
windows and per-cycle scripts (`replayer/`, `acceptance/rtl/`), the Tracer's per-run figures (`tracerblock/rtl/`).
The platform around them is checked by the RTL's own firmware, unmodified (`fw/rtl/`): `test_core_tile` checks
the core's join to the tile's register bus, access by access, in `register_bus`'s cycles; `test_rtl_tile`
(acceptance A) runs the RTL's tile programs on a tile, with the replayer in its slot, and checks the boot ROM's
cycles there; `test_rtl_sort` (acceptance B) runs the RTL's sort images end to end on the chip, against an oracle
written apart from the design, and checks the sort's cycles, step by step, against the RTL chip's published
figures. `test_rtl_segment` (acceptance C) runs the segment on the chip under the RTL's own firmware, as the RTL's
chip tests do: a guest traced by the tracer tile's Tracer, each window delivered to its compute tile and replayed there,
against segments the RTL's test built from its own models, and every cycle against the RTL chip's own figures for the
same run (`segment/rtl/`); on real workloads' windows, against their recorded files and the RTL chip's published cycles. A reference made by the RTL is never regenerated from the simulator. v1.35's core
and tile are gone, and with them what below runs on them (`../PENDING-v1.36.md` says which fixtures and tests are
pending, and why).

## Tests and suites

- **`make check`** builds and runs every `test_*.cpp` here, in the debug and release builds with gcc and clang
  (`BUILDING.md`). Most compare against a fixture folder; the `*conf` and `golden*` tests are the conformance drivers
  and goldens above.
- **`./run_tests.sh`** runs v1.3's suites, ported, with v1.3's step names and logs; `--check` compares each log with its
  reference (`RUNNING.md`). The suites' sources are `suite_*.cpp`, `ctl/`, `sort_test.cpp` and the
  `*.mk` files; `suite.h` is their shared harness, `mach.h` the machine (`src/units/README.md`).
- **v1.36: what is pending.** The tile is now the RTL's (`spec/CHANGES-v1.35-to-v1.36.md`), and the tests and suites
  that run v1.35's firmware or drive v1.35's register map have not been ported: `pending.mk` takes those tests out of
  the build (`make pending` lists them), `./run_tests.sh` prints their steps as `PENDING (<reason>)` and counts them,
  and `../PENDING-v1.36.md` has a row for each with what closes it. Their sources, fixtures and reference logs stay as
  they were; nothing was weakened to pass. What runs on v1.36's hardware today: `test_rtl_tile` (acceptance A: the
  RTL's own tile programs on one compute tile, with the boot ROM's cycles; the runner's step `rtl-tile`),
  `test_rtl_sort` (acceptance B: the RTL's sort images on the chip, 64 × 16 records always, 64 × 65,536 with
  `RTL_SORT_64K_KEYS`, and a run split over threads; the steps `rtl-sort` and `rtl-sort-64k`),
  `test_rtl_segment` (acceptance C: the RTL's trace run, tracer tile's image and replay image on the chip; the RTL test's
  three generated segments always, the real workloads' windows with `MESHSIM_INPUTS`; the steps `rtl-segment` and
  `rtl-segment-real`),
  `test_core_programs` and `test_core_tile` (the RTL's core; the steps `core-programs` and `core-tile`), `test_tile_control`
  (the tile control unit and the register bus's decode against the specification's text), `test_periph_slot` and
  `test_tracer_slot` (the peripheral slot: 17.1's blocks and the RTL's cycles of a consuming load and of the DMA,
  with the test peripheral, the replayer and the Tracer in it), `test_rw_engine` and `test_rtl_memory` (the memory tile:
  the RW engine against the RTL's unit, the RTL's unit test ported with the RTL's outputs' fingerprints, and the
  RTL's memory run on the chip with an AXI4 slave, `axi4_slave.h`, on the `lpddr` port, against the RTL chip's
  handshakes: `rwengine/`; the runner's step `rtl-memory`; `tests/rw_engine_teeth.sh` plants bugs in the model, one at
  a time, and requires these tests to fail on each), `test_mach` (the machine harness: the
  boot ROM's pull boot on all 64 tiles, the chip's build, and v1.3's `packets`, `reads` and `a2a` sequences on the
  coreless machine), `test_router` (the router against the RTL's own, under scripted random load, cycle by cycle, and every router's routes: `routerconf/README.md`), the unit stages'
  tests (`test_replayer`, `test_replayconf`, `test_replay_lists`, `test_tracer*`), and the tests of the tile's units, each written from
  the specification and, where it leaves a cycle to the implementation, the RTL block's README: `test_ag` (the DMA
  unit's address generator, 8.2–8.5), `test_receive` (the write engine with `PTR` and `HIST`, 12.4, 12.5), `test_mem` (the
  scratchpad and the inject unit, 14, 15; the runner's steps `mem*`), `test_gather` (the gather unit) and `test_mach`'s
  gather case (13 on the whole tile); and of the chip's parts: `test_special_router` (16.1–16.4: the special tile's
  router, the attach ports, the reserved ids, the DMA input, the arbiters), `test_request_tree` (chapter 11: the request
  unit and the tree, against a beat-by-beat model written from the RTL's READMEs), `test_ingress_egress` (17.6: the unit
  between a core stand, the nets and the far ends of its pins and ports, `ie_far_end.h`: the UART's line bit for bit, the
  host interface's handshakes, each rule with its cycle) and `test_rtl_stream` (the RTL's `chip_stream_test.rs` and the
  UART through the master's core, on the chip, at the RTL chip's cycles for the same far ends; the runner's step
  `rtl-stream`). v1.3's references for those units (MemTest's logs, EngConf's and MemConf's
  fixtures) measured v1.35's FIFOs and queues and are retired or superseded, never regenerated
  (`../PENDING-v1.36.md`, the platform stage, sections 3 and 7).

## The fixture folders

| folder | what |
|---|---|
| `core-programs/` | the RTL's own core programs (riscv-tests, firmware tests, CoreMark) and what the RTL's simulation measured on each: cycles, retired instructions, retire traces (its `README.md`) |
| `golden/` | the M1 programs' retire traces (`RUNS` lists each: tile, images, schedule), and their images |
| `golden-m2/` | M2's references: the programs' outputs and hashes (its `README.md`) |
| `golden-m3/` | what is left of M3's references (its `README.md`); T2's and the chains' are retired with their tools (`../PENDING-v1.36.md`, the segment's stage) |
| `fp-m1/` | the core's fingerprints for the M1 programs (`*.corefp`) |
| `g1-replay/` | one tile's control boundary, cycle by cycle, for two programs |
| `m3-images/` | M3's controller images (C&C 1-3, the replayer's and Tracer's controls) and their boot references |
| `portconf/`, `routerconf/`, `memconf/`, `engconf/`, `slotconf/`, `cncconf/`, `cnc1conf/` | conformance scripts and observations: the port kinds, the router, the memory path, the engines, the peripheral slot, C&C's model, C&C 1's peripheral |
| `tileslotconf/` | a tile's controller with its slot: programs and observations |
| `reqconf/` | the request bus under five loads (calm, drop, fan-in, full, mix) |
| `replayer/` | the replayer's fixtures: the conformance scripts with the RTL's observations of them, the ALU sweep and reports (its `README.md`) |
| `rwengine/` | the RW engine against the RTL: the RTL unit's outputs' fingerprints under its own test, the RTL chip's `lpddr` handshakes in `chip_memory_test`, and the instrumentation that printed them (its `README.md`) |
| `tracerparts/` | the Tracer's parts (the LRU coder, the memory system, the RVC expansion) |
| `ctlsuites/`, `p2suites/`, `p4suites/`, `tracersuites/` | v1.3's suite logs, the references of `run_tests.sh --check`; `PROVENANCE` in each says how they were made |
| `ctl/` | the control suites' sources (code, not fixtures) |
| `sort/` | the sort's fingerprints and its kit's references (its `README.md`) |
| `acceptance/` | the acceptance kit: build, calibration and its task (its `README.md`) |
| `segment/` | the segment on the chip: the RTL's chip test's three segments as it built them, with the RTL chip's own figures for each run (`segment/rtl/`, its `README.md`) |
| `delivery/` | doubles as Java prints them, for the delivery tools' output |
| `mach/`, `suite/` | the harness's self-tests' expected output |
| `freeze/` | the freeze check: its body and the task that runs it |

The `p4_*.sh` and `tracer_teeth.sh` scripts are checks run on the suites (debug-build checks, the logs' line counts,
that the Tracer's tests fail when the Tracer is broken). `ie_teeth.sh` plants slips of the UART and the host interface
in Ingress/Egress, one at a time, and requires `test_ingress_egress` or `test_rtl_stream` to fail on each.

## How the fixtures were made

Every fixture was generated from v1.3's Java simulator by the scripts of the oracle folder, which is not in this
frozen copy; they are in the repository's history at the commit that froze this folder
(`git log --diff-filter=A --format=%h -- v1.35/README.md` names it). `PROVENANCE` files and the `RUNS` lists name each
fixture's command and `ref`'s commit. **The frozen copy can verify its fixtures but not regenerate them.**

## The Tracer in v1.36

The Tracer is a model of the RTL's (Snarky One's `rtl/blocks/tracer_unit`), so its evidence is the RTL and spec 17.9,
not v1.3's Java model:
- **`tracer_block`** (`tests/tracer_block_main.cpp`, `tests/tracerblock/`): real workloads' segments on the RTL
  acceptance test's bench (`tests/tracer_block.h`), every run's cycles and counters against the RTL's own.
- **`suite_tracer`** and **`test_tracer_rules`**: hand-built guests on the same bench (`tests/tracer_rig.h`), each
  expectation from spec 17.9 or, where it gives no number, the RTL's source, as closed forms in the fill port's
  latency; `test_tracer_rules` holds the rules v1.36 changed and the timing of 17.9.7.
- **`test_tracer`**: the interface to the slot, the wake rule, `observe()` and the fingerprint.
- **`tests/tracer_teeth.sh`** plants v1.35's behaviour and timing slips in the Tracer, one at a time, and requires
  these tests to fail on each.
The replay of the Java reference Tracer's call logs (the tracerconf fixtures) is retired with the v1.35 hardware it
modelled.

## The segment in v1.36

The segment on the chip is checked against the RTL's chip, not against v1.3's T2 or its chains, which ran v1.35's
controllers' firmware and are retired (`../PENDING-v1.36.md`, the segment's stage):
- **`test_rtl_segment`** (`tests/segment_bench.h`): the chip under the RTL's firmware, unmodified. The master tile's
  trace run loads the tracer tile's image and the replay image; the Tracer traces the guest window by window and its
  DMA delivers each window's record and streams to its compute tile; the replayers replay them. The test places the
  segment as the RTL's test does, plays the memory system on the Tracer's fill port as the RTL's test does (it is Open
  in the hardware; the model is the test's, draw for draw), and takes every row out of every tile's row port.
- **The generated segments** (`tests/segment/rtl/`): the RTL test's own three, written by it with its models'
  expectations and its chip's figures. Every row, delivery, summary word, report word, fill request and row's cycle
  must be the RTL's.
- **The real workloads** (`tests/segment_real.h`): EVM Zone's and Cyclone's recorded windows, a host segment of 64 at
  a time, each window's rows, record and streams from its files; the replay's and the trace's cycles must be the RTL
  chip's, as its README gives them for the same windows.
