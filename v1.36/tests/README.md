# tests: how meshsim is checked against the RTL

*For anyone who needs to trust or rerun meshsim's results: what the simulator is compared with, what each test is,
and what each fixture folder is and where it came from.*

## What the simulator is compared with

v1.36's simulator models the RTL's hardware (Snarky One). Every expectation of a test comes from one of four
places, never from the simulator's own output:

- **The RTL's own simulation.** A reference recorded from the RTL under Verilator: a core program's retirements cycle
  by cycle, a unit's observations of a scripted run every cycle, a chip run's handshakes, a block's figures on real
  workloads. Each such folder has a `README.md` with the Snarky One commit, the instrumentation and the command. **A
  reference made by the RTL is never regenerated from the simulator**; a difference is the simulator's to fix.
- **The RTL's own tests, ported.** A chip or unit test of Snarky One's rewritten on the simulator, with the RTL's
  firmware images unmodified (`fw/rtl/`), its stimulus draw for draw, and the figures its chip gives.
- **The specification** (`spec/`), and, where it leaves a cycle to the implementation, the RTL block's README or
  source: `OPEN-v1.36.md`, (c), lists those.
- **`ref`**, v1.3's Java simulator, where v1.36's hardware is unchanged from v1.3's: the framework's port kinds, the
  request tree's units under load, the replayer's ALU, the Tracer's parts, a few sequences on the coreless machine,
  the checker's messages. `PROVENANCE` files name each such fixture's command; the scripts that made them are not in
  this folder (they are in the repository's history, at the commit that froze v1.35).

Tests whose plants prove they can fail: `rw_engine_teeth.sh`, `ie_teeth.sh` and `tracer_teeth.sh` each plant slips in
a model, one at a time, in a scratch copy, and require the unit's tests to fail on each (not in `make check`: a
rebuild a slip).

## Tests and suites

- **`make check`** builds and runs every `test_*.cpp` here, in the debug and release builds with gcc and clang, and
  the thread tests and the chip split over threads under ThreadSanitizer (`BUILDING.md`).
- **`./run_tests.sh`** runs the tests of the hardware as steps, in the suite build (the checker's reports on), and
  the suites whose logs are compared with reference logs (`RUNNING.md`). The suites' sources are `suite_*.cpp` and
  `tracer_block_main.cpp`; `suite.h` is their printer, `mach.h` the machine (`src/units/README.md`).

| test | what it checks | against |
|---|---|---|
| `test_core_programs` | the core alone: the RTL's 49 programs, CoreMark, the interrupt programs, the boot ROM; every retirement, trap and wait in the RTL's cycle | the RTL (`core-programs/`) |
| `test_core_tile` | the core's join to the tile's register bus, access by access | the specification (5, 6.4) and `register_bus`'s cycles |
| `test_regs` | `fw/REGISTERS.md` and `fw/include/mesh.h` | the generator, the simulator's decode, the specification's tables, the RTL firmware's definitions |
| `test_tile_control` | the tile control unit and the register bus's decode | the specification (5, 7) |
| `test_ag`, `test_receive`, `test_mem`, `test_gather` | the DMA unit's address generator (8.2–8.5), the write engine with `PTR` and `HIST` (12.4, 12.5), the scratchpad and the inject unit (14, 15), the gather unit (13) | the specification and the RTL blocks' READMEs |
| `test_mach` | the machine harness; the pull boot on all 64 tiles; v1.3's `packets`, `reads` and `a2a` sequences on the coreless machine; the gather unit and `ERRORS.drift` on the whole tile | the specification; `ref` (`mach/netself.out`) |
| `test_checker` | the checker's report for packets that break a rule | `ref`'s CheckSelf, for the rules v1.36 keeps; 7.4's names |
| `test_router`, `test_special_router` | the router under scripted random load, every cycle of every output, and every router's routes; the special tiles' routers, the attach ports, the arbiters | the RTL's router (`routerconf/`); chapter 16 |
| `test_request_tree`, `test_reqbus` | the request unit and the tree; the tree's units under five loads | chapter 11 and a beat-by-beat model from the RTL's READMEs; `ref` (`reqconf/`) |
| `test_periph_slot`, `test_tracer_slot` | the peripheral slot: the control block, the counted events, the DMA with its packet register, with the test peripheral, the replayer and the Tracer in it | 17.1–17.4 and the RTL's `peripheral_control` and `peripheral_dma` |
| `test_ingress_egress` | Ingress/Egress between a core stand, the nets and the far ends of its pins (`ie_far_end.h`): the UART bit for bit, the host interface's handshakes | 17.6 and the RTL's `ingress_egress_unit` |
| `test_rw_engine` | the RW engine: the RTL's unit test ported, on an AXI4 slave (`axi4_slave.h`) | the RTL unit's outputs' fingerprints (`rwengine/`) |
| `test_replayer`, `test_replayconf`, `test_replay_lists`, `test_replayer_alu`, `suite_replayer` | the replayer: its unit tests, scripted passes every cycle, the error lists, the ALU | the RTL's observations (`replayer/`); `ref`'s ALU sweep |
| `test_tracer`, `test_tracer_rules`, `test_tracer_rig`, `test_tracer_parts`, `suite_tracer`, `suite_memsys`, `suite_rvc` | the Tracer: its interface to the slot, 17.9's rules and timing as closed forms in the fill port's latency, its parts | 17.9 and the RTL's source; `ref` (`tracerparts/`, gold's RVC table) |
| `tracer_block` | the Tracer alone on real segments and on a synthetic guest | gold's files; the RTL's figures run by run (`tracerblock/rtl/`) |
| `test_rtl_tile` | the RTL's tile programs on one compute tile, the replayer in its slot; the boot ROM's cycles | the RTL's `base_tile_test.rs`; Snarky One's core README |
| `test_rtl_sort` | the RTL's sort images end to end on the chip, four runs; every tile's error provoked; a run split over threads | an oracle written apart from the design; the RTL chip's cycles, step by step |
| `test_rtl_segment` | the segment on the chip under the RTL's firmware: generated segments and real workloads' windows | the RTL's own run (`segment/rtl/`); the recorded files and the RTL chip's cycles |
| `test_rtl_memory`, `test_rtl_stream` | the memory run and the stream run on the chip; the UART through the master's core | the RTL chip's handshakes and cycles |
| `replay_shard`, `shard_summary`, `replay_delivery` (tools; `test_shard_summary`) | the replayer alone on a delivery's windows: the acceptance kit | the delivery's rows; the RTL's cycles (`acceptance/rtl/`) |
| `test_cnc` | the behavioural master of the coreless machine (`bench_master.h`), a test bench | `ref` (`cncconf/`) |
| the others (`test_arena`, `test_base`, `test_bitmap`, `test_bucket`, `test_debug`, `test_digest`, `test_engine`, `test_forms`, `test_kinds`, `test_link_depth`, `test_matrix`, `test_net_headers`, `test_observe`, `test_parallel`, `test_partition`, `test_portconf`, `test_registry`, `test_ring`, `test_threads`, `test_topology`, `test_value_pool`, `test_wide`, `suite_bus`, `test_suite`, `test_statefp`, `test_chain`, `test_delivery_json`, `test_java_format`, `test_suite_replayer`, `test_tracer_suites`) | the framework, its port kinds and threads; the tests' own helpers; the suites run inside `make check` | `ref` for the port kinds (`portconf/`, `p2suites/`); by hand |

## The fixture folders

| folder | what |
|---|---|
| `core-programs/` | the RTL's own core programs and what the RTL's simulation measured on each: cycles, retired instructions, retire traces, the waits (its `README.md`) |
| `routerconf/` | the router against the RTL: scripts of random load, the RTL's observations of them, the RTL's routes, and what made them (its `README.md`) |
| `replayer/` | the replayer's fixtures: the conformance scripts with the RTL's observations of them, the ALU sweep and reports (its `README.md`) |
| `acceptance/` | the acceptance kit (the replayer alone on a delivery), with the RTL's cycles for each window (`acceptance/rtl/`) (its `README.md`) |
| `rwengine/` | the RW engine against the RTL: the RTL unit's outputs' fingerprints, the RTL chip's `lpddr` handshakes, and the instrumentation that printed them (its `README.md`) |
| `tracerblock/` | the Tracer alone: the bench's parts (gold's readers, the synthetic guest, the report), the reference logs, and the RTL's figures (`tracerblock/rtl/`, its `README.md`) |
| `segment/` | the segment on the chip: the RTL's chip test's three segments as it built them, with the RTL chip's own figures (`segment/rtl/`, its `README.md`) |
| `tracersuites/` | the Tracer suites' reference logs, and gold's RVC table; `PROVENANCE` says how they were made |
| `tracerparts/` | the Tracer's parts against `ref`: the LRU coder, the memory system, the RVC expansion |
| `portconf/`, `p2suites/` | the framework's port kinds: conformance scripts and observations, and `suite_bus`'s reference log |
| `reqconf/`, `cncconf/` | the request tree's units under five loads; the behavioural master: against `ref` |
| `mach/`, `suite/`, `statefp/`, `delivery/` | expected output of the harness's own tests: `ref`'s sequences on the coreless machine; the suites' printer; three fingerprint files as examples of the format; doubles as Java prints them |

`TRACE-FORMAT.md` is the retire trace's format.
