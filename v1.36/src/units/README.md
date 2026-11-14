# src/units: the model of the chip

*For anyone reading or changing the model: which file models which unit, which setting is which spec parameter, and
which check is which invariant.* How a unit is written and added is `src/README.md`.

**v1.36 is brought to the RTL's hardware in stages** (`PENDING-v1.36.md`). Done: the core, the replayer and the Tracer
(each the RTL's block, cycle for cycle), and the platform: the register map, the register bus, the tile control unit,
the units' registers, the boot, the peripheral slot, and the chip (the special tiles, the request tree, the nets). Those
rows below cite v1.36's machine-spec (the RTL's `rtl/tile/base/spec.md`, the same section numbers), marked **v1.36**.
The other rows (the memory path's units, whose behaviour is still v1.35's: `PENDING-v1.36.md`, the platform stage,
section 5), the settings table and the invariants table are still v1.35's, with v1.35's section numbers
(`spec/history/`; 4.19 and 4.21 in `spec/jolt/`), until their stage.

**Where the machine is put together.** `chip.h` builds the chip: `tile_cycle` adds one tile's units, `build_chip` the
whole chip from a `ChipSpec`. **The machine's settings (`MachConfig`) and the machine itself (`Mach`: the chip, its
controllers' images and the harness around it) live in `tests/mach.h`, not here**; every test, suite and tool builds
its machine through them.

## The units

| unit | file | spec |
|---|---|---|
| the link word, its helpers | `net.h`, `linkword.h` | 3.1 |
| the network's configuration: the hops table, the start skews | `net.h`, `net.cpp` | 3.1, 3.2 |
| the request unit's queues, the request tree (a cycle a node, the root's credits) and the receive queues | `reqbus.h` | **v1.36** 11 |
| big SRAM | `big_sram.h` | 4.1 |
| the address generator | `ag.h` | 4.2 |
| the GATHER engine | `gather.h` | 4.4 |
| the read queue, return path, injection and loopback tokens | `inject.h`, `inject.cpp` | 4.5 |
| the memory path's shared ports | `mem.h` | 4.1, 4.5 |
| the router: route blocks, output queues, the attach port (tiles 0, 7, 15), a special tile's router with its DMA input; a reserved dst dropped | `router.h`, `router.cpp` | 4.6, 4.7, 4.16; **v1.36** 16.2, 16.4 |
| the traffic director | `director.h` | 4.8 |
| the write engine | `write_engine.h` | 4.9 |
| the table memories | `table_store.h` | 4.10 |
| the tile's core: the RTL's `rv32_core`, cycle for cycle, with its decode; it reaches region 0xFF through its environment, in `register_bus`'s cycles | `rv32.h`, `rv32_decode.h` | **v1.36** 6; `spec/core.md` §1 |
| the address map, region 0xFF, the registers' words and bits | `regmap.h` | **v1.36** 5, 7–13, 17.1 |
| the register bus: the decode of region 0xFF and its access faults | `register_bus.h` | **v1.36** 5 |
| the tile control unit as plain code, with the units' registers it holds (`DMA_CONFIG`, `RECEIVE_CONFIG`, the counters, `GATHER_CONFIG`) | `regblock.h` | **v1.36** 7, 8.1, 12.1, 12.2, 13 |
| the tile control unit as a unit; the core with the register bus and the core's side of the write, read and request units; a tile's control part and its straps | `tile_ctl.h`, `tile_ctl.cpp` | **v1.36** 6, 7, 9, 10, 11.1 |
| v1.35's M1 bench (v1.35's core, which is deleted, with its register block): not built, kept for the pending tests that include it (`PENDING-v1.36.md`, section 2) | `controller.h`, `controller.cpp` | 4.11 |
| the peripheral slot: each peripheral's control block and DMA (the RTL's `peripheral_control`, `peripheral_dma`), the write port, and the contract a peripheral's model meets (`periph_slot.h`, "THE CONTRACT") | `periph_slot.h`, `periph_slot.cpp`, `peripheral.h` | **v1.36** 17.1–17.5 |
| a peripheral's counted events: a consuming load, a set and a fill in one cycle | `event_counts.h` | **v1.36** 7.2, 17.2 |
| Ingress/Egress, the master tile's peripheral 1 (the nets, `RELEASE`, the watchdog, the UART's transmit side); the RW engine's stand-in on the memory tile, which stops the simulator if used | `ingress_egress.h` | **v1.36** 17.6, 17.7, 18 |
| (v1.35's behavioural model of C&C and its host port are a test bench's now: `tests/bench_master.*`, `tests/host_port.h`) | | |
| the replayer, as the RTL's `replayer_unit` builds it: its queues, decoders, fetch, row engine, pass and output queue, cycle for cycle | `replayer.h`, `replayer.cpp`, `replayer_alu.h`, `stream_word.h` | 17.8 (`spec/jolt/replayer.md`) |
| the replayer's expansion tables as delivered | `replay_tables.h`, `replay_tables.cpp` | 4.19; `data/REPLAY-INPUT-REQUIREMENTS.md` |
| what the replayer and the Tracer share | `row_core.h` | 4.19, 4.21 |
| the Tracer: the RTL's (Snarky One's `tracer_unit`), cycle for cycle | `tracer.h`, `tracer.cpp`, `rvc.h`, `lru_coder.h`, `lru_index.h` | 17.9 (`spec/jolt/tracer.md`) |
| the memory system behind the fill port, and the memory as files, as the Tracer sees them | `mem_system.h`, `file_store.h`, `file_store.cpp` | 17.9.5, 17.9.14 |
| S1's measurement | `s1.h` | 4.8, 4.10; invariant 24 |
| the retire trace | `trace_log.h`, `trace_log.cpp` | 8.15; `tests/TRACE-FORMAT.md` |
| a memory's part of the state fingerprint | `memsum.h` | (simulator only) |
| the suites' test peripheral, type 15 | `test_peripheral.h` | (simulator only) |
| the chip's assembly: the compute tiles and their links, the master, memory and tracer tiles on their attach links, the request tree, the nets | `chip.h`, `chip.cpp` | **v1.36** 2, 11.3, 18 |

The `*.mk` files are the Makefile's rules for these units' tests and suites, one per group (`cc`, `g4`, `mem`, `net`,
`tracer`, `units`).

## `MachConfig` and the spec's parameters

`MachConfig` (`tests/mach.h`) holds every setting; `MachConfig::defaults()` and `machine()` are v1.3's two standard
configurations. Fields named as section 10's parameters:

| section 10 | `MachConfig` |
|---|---|
| `link_latency`, `queue_depth` (router FIFOs), `intra_tile_latency` | the same names |
| start skew between tiles | `skew`, `skew_seed`, `start_skew` |
| GATHER request queue | `gather_depth` |
| `mem_chunks`; area request-queue depth | `mem_chunks`; `area_queue_depth` |
| big-SRAM array read latency | `sram_read_latency` |
| tokens and return queues | `read_tokens`, `loopback_depth`, `loopback_tokens` |
| `ts_engine_latency`, `ag_lookahead` | the same names (`with_ts_latency`, `ag_lookahead_used()`) |
| write-engine input FIFO; controller read port | `we_in_depth`; `read_port_depth` |
| `cm_latency`, `stage_delay`, `reg_load_latency` | the same names |
| the nets (**v1.36** 18) | the chip's: `arrive_latency` 0, `release_latency` 1, `error_latency` 0 (the RTL chip's wires and flip-flop); the bench machine's: `ready_latency`, `go_latency` |
| **K** | `K`; `k_min()` is its formula, and a K below it is fatal at startup |
| `REQUEST_QUEUE_DEPTH`; the receive queues (**v1.36** 19) | `request_queue_depth` (4); `receive_queue_depth` (128, 256, 128) |
| eject priority | `loopback_last` |
| peripherals: `WRITE_PORT_LATENCY` (**v1.36** 12.3, 17.3) | `cm_latency` (2), and the slot's `region` link of `cm_latency` + 1; `rp_latency` is read by `k_min` alone; `periph_type` (every compute tile's peripheral 1) |
| the replayer's log queues (`spec/jolt/replayer.md`) | `replayer_log_entries`: 256 only, `REPLAY_QUEUE_ENTRIES` (v1.36) |

The rest are the simulator's: the machine's shape (`special_tiles`: the chip; `coreless`, `real_responder`, `cc_cores`,
`test_read_clients`, `reqbus_host`, `test_periph_tile`, `test_peripheral_tile`, `parked`), C&C's model (`ref_cycle`,
`run_latency`, `cnc_reaction`, `cnc_rx_depth`, `cc_dma_depth`, `done_gate`, `cnc_go_settle`,
`counter_store_clears_latch`), the checker (`strict_replies`, `check_*`, `measure_s1`, `max_violation_messages`), the
debug options (`shuffle_seed`, `step_all`, `instance_major`), the logs and their capacities (`log_trace`,
`*_capacity`), `image_dirs`, `write_combining`, `fifo_count_before_pop`, and the split over threads (`threads`,
`parts`). `MachConfig::set` and `set_args` take `field=value`; `dropped()` lists v1.3's fields v1.35 doesn't have
(D41; `CHANGES-v1.35.md`).

## The checker and the invariants

How each invariant of spec section 9 is checked. **F**: fatal in every build (`src/base/fatal.h`); **S**: the core
stops; **C**: counted in every build by the unit; **R**: reported by name, as a `check.h` kind, in the debug and suite
builds. The checks neither simulator has are `spec/CHANGES-v1.3-to-v1.35.md`'s list.

| invariant | kinds (`src/check.h`) | how |
|---|---|---|
| 1 no FIFO overflow | `FIFO_OVERFLOW` | C, R |
| 2 one access per area per cycle | (by construction: the area queues) | |
| 3 the timetable holds | `ROTATION_LATE` | C, R |
| 4 expected arrivals land first | `CTL_DONE_EARLY` | C, R |
| 5 no bad packets | `DIR_BAD_PACKET`, `INJ_BAD_PACKET`, `DIR_SIZE_UNDEF`, `INJ_SIZE_UNDEF`; a misaligned packet | C, R; F |
| 6 no `HIST` count past 17 bits | `HIST_OVERFLOW` | C, R |
| 8 overwritten-unread results | `CMEM_REPLY_OVERWRITE` (under `strict_replies`), `CMEM_COMMAND_UNREAD` | C, R |
| 9 no unexpected arrival | `DIR_UNEXPECTED` | C, R |
| 10 gathers are local | `INJ_GATHER_ELSEWHERE` | C, R |
| 11 controller-memory writes keep their places | `DIR_COMMAND_BLOCK`, `DIR_REPLY_BLOCK`, `DIR_PAST_CMEM`, `DIR_COMMAND_KIND` | C, R |
| 14 read-port results and incoming traffic don't overlap | `DIR_READPORT_OVERLAP` (under `check_read_port_overlap`) | C, R |
| 15 no errors | `ERRORS_SET` | C, R |
| 16 the peripherals | `PERIPH_VIOLATION`, `TRACER_VIOLATION`; a start while busy | C, R; F |
| 17 the request contract | `DIR_UNEXPECTED`, `INJ_NO_ENDPOINT`; the request bus's own counts | C, R |
| 18 one source per order-placed destination | `PERIPH_VIOLATION` (the replayer's queue port) | C, R |
| 19 everything in bounds | `DIR_PAST_SRAM`, `WE_PAST_SRAM` | C, R |
| 20 pointers and runs aligned | `CTL_RECORD_BASE_ODD`; a misaligned read-queue chunk | C, R; F |
| 21 no reserved encodings | `DIR_COUNT_CTR0`, `INJ_COUNT_CTR0`, `CTL_RESERVED_ACCESS`; modes 6-7, a reserved `DMA_CONFIG` digit | C, R; F |
| 22 uncounted traffic is mode 0 only | `DIR_UNCOUNTED_*` | R |
| 23 no silent traps | (the core) | S |
| 24 K covers the write path | `S1_LATE`; a K below its formula | C, R; F |
| 25 boot keeps its order | `DIR_OVERLAY_AFTER_COMMAND`, `DIR_COMMAND_KIND` | C, R |
| 26 no ignored start | (a start while running) | F |

Invariants 7, 12 and 13 are withdrawn.

## No simulator-only words

v1.36 has none (v1.35's exit and trace words are gone with its map: machine-spec 5.5). A program reports to the master
tile by requests (11.5), and every trap is an ordinary trap to `mtvec` (6.2): nothing in an image stops the simulator.
The retire trace (`tests/TRACE-FORMAT.md`) is still written by the core's model when a test gives it a log.
