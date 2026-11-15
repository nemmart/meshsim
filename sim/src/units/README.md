# src/units: the model of the chip

*For anyone reading or changing the model: which file models which unit, which setting is which spec parameter, and
which check is which invariant.* Section numbers are the spec's (`spec/machine-spec.md`; 4.19 and 4.21 are in
`spec/jolt/`). How a unit is written and added is `src/README.md`.

**Where the machine is put together.** `chip.h` builds the chip: `tile_cycle` adds one tile's units, `build_chip` the
whole chip from a `ChipSpec`. **The machine's settings (`MachConfig`) and the machine itself (`Mach`: the chip, its
controllers' images and the harness around it) live in `tests/mach.h`, not here**; every test, suite and tool builds
its machine through them.

## The units

| unit | file | spec |
|---|---|---|
| the link word, its helpers | `net.h`, `linkword.h` | 3.1 |
| the network's configuration: the hops table, the start skews | `net.h`, `net.cpp` | 3.1, 3.2 |
| the request bus and request ports | `reqbus.h` | 3.3, 4.18 |
| big SRAM | `big_sram.h` | 4.1 |
| the address generator | `ag.h` | 4.2 |
| the GATHER engine | `gather.h` | 4.4 |
| the read queue, return path, injection and loopback tokens | `inject.h`, `inject.cpp` | 4.5 |
| the memory path's shared ports | `mem.h` | 4.1, 4.5 |
| the router: route blocks, output queues, the fifth port | `router.h`, `router.cpp` | 4.6, 4.7, 4.16 |
| the traffic director | `director.h` | 4.8 |
| the write engine | `write_engine.h` | 4.9 |
| the table memories | `table_store.h` | 4.10 |
| the controller's core | `rv32.h` | 4.11, 4.13 |
| one tile's controller, wired | `controller.h`, `controller.cpp` | 4.11 |
| the register block, the address map | `regblock.h`, `regmap.h` | 4.12, 4.13, 8 |
| the tile's control as a unit | `tile_ctl.h`, `tile_ctl.cpp` | 4.12, 8 |
| the peripheral slot and the peripheral contract | `periph_slot.h`, `periph_slot.cpp`, `peripheral.h` | 4.17 |
| a peripheral's counted events | `event_counts.h` | 4.17.3 |
| C&C 1's peripheral (Ingress/Egress, the UART), C&C 2's and 3's stubs | `cnc1_periph.h` | 4.17.1, 4.20, 8.13, 8.14 |
| C&C 1's behavioural model, the row collectors, C&C 2-3's endpoints | `cnc.h`, `cnc.cpp` | 6.2, 6.6 |
| a controller's host port (C&C as cores) | `host_port.h` | 6.2 |
| the replayer | `replayer.h`, `replayer.cpp`, `replayer_alu.h`, `stream_word.h` | 4.19 |
| the replayer's expansion tables as delivered | `replay_tables.h`, `replay_tables.cpp` | 4.19; `data/REPLAY-INPUT-REQUIREMENTS.md` |
| what the replayer and the Tracer share | `row_core.h` | 4.19, 4.21 |
| the Tracer | `tracer.h`, `tracer.cpp`, `rvc.h`, `lru_coder.h`, `lru_index.h` | 4.21, 8.16 |
| the memory system and the memory as files, as the Tracer sees them | `mem_system.h`, `file_store.h`, `file_store.cpp` | 4.21.5, 7 |
| S1's measurement | `s1.h` | 4.8, 4.10; invariant 24 |
| the retire trace | `trace_log.h`, `trace_log.cpp` | 8.15; `tests/TRACE-FORMAT.md` |
| a memory's part of the state fingerprint | `memsum.h` | (simulator only) |
| the suites' test peripheral, type 15 | `test_peripheral.h` | (simulator only) |
| the chip's assembly | `chip.h`, `chip.cpp` | 2 |

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
| `ready_latency`, `go_latency`, `error_latency` | the same names (`error_latency` −1: equal to `ready_latency`) |
| **K** | `K`; `k_min()` is its formula, and a K below it is fatal at startup |
| request FIFO; the request bus's receive queues | `request_fifo_depth`; `receive_queue_depth` |
| eject priority | `loopback_last` |
| peripherals: `rp_latency` | `rp_latency`; `periph_type` (every mesh tile's peripheral 1) |
| the replayer's log queues (`spec/jolt/replayer.md`) | `replayer_log_entries` |

The rest are the simulator's: the machine's shape (`coreless`, `real_responder`, `cc_cores`, `cc3_tracer`,
`test_read_clients`, `reqbus_host`, `test_periph_tile`, `test_peripheral_tile`, `parked`), C&C's model (`ref_cycle`,
`run_latency`, `cnc_reaction`, `cnc_rx_depth`, `cc_dma_depth`, `cc_link_latency`, `done_gate`, `cnc_go_settle`,
`counter_store_clears_latch`), the checker (`strict_replies`, `check_*`, `measure_s1`, `max_violation_messages`), the
debug options (`shuffle_seed`, `step_all`, `instance_major`), the logs and their capacities (`log_trace`,
`*_capacity`), `image_dirs`, `write_combining`, `fifo_count_before_pop`, and the split over threads (`threads`,
`parts`). `MachConfig::set` and `set_args` take `field=value`; `dropped()` lists v1.3's fields v1.35 doesn't have
(D41; `CHANGES-v1.35.md`).

## The checker and the invariants

How each invariant of spec section 9 is checked, with every kind of `src/check.h`. **F**: fatal in every build
(`src/base/fatal.h`); **S**: the core stops; **R**: recorded as a fact where it happens and reported by name, in the
debug and suite builds (`tests/checker.cpp` has each kind's message, which names its invariant). Most reported kinds
are also counted by their unit in every build, in a counter beside the record site; the traffic director's checks of
invariants 22 and 25 are facts only. A check in brackets runs under that `MachConfig` switch (`tests/mach.h`). The
checks neither simulator has are `spec/CHANGES-v1.3-to-v1.35.md`'s list.

| invariant | kinds (`src/check.h`) | how |
|---|---|---|
| 1 no FIFO overflow | `FIFO_OVERFLOW` (the harness's scan of the units' overflow counts) | R |
| 2 one access per area per cycle; no buffer both read and written | the first by construction (the area queues); `DISJOINT`, once per tile and phase [`check_disjoint`] | R |
| 3 the timetable holds | `ROTATION_LATE`, the first of each run | R |
| 4 expected arrivals land first | `CTL_DONE_EARLY` | R |
| 5 no bad packets | `DIR_BAD_PACKET`, `INJ_BAD_PACKET`, `EP_BAD_PACKET`; size 11: `DIR_SIZE_UNDEF`, `INJ_SIZE_UNDEF`, `EP_SIZE_UNDEF`; a mode-0 packet not aligned to its size | R; F |
| 6 no `HIST` count past 17 bits | `HIST_OVERFLOW`, once per tile | R |
| 8 overwritten-unread results | `CMEM_REPLY_OVERWRITE` [`strict_replies`]; `RP_QUEUE_FULL` (a read dropped, `ERRORS` [7]) | R |
| 9 no unexpected arrival | `DIR_UNEXPECTED` [`check_unexpected`] | R |
| 10 gathers are local | `INJ_GATHER_ELSEWHERE`, `AG_GATHER_DEST` | R |
| 11 controller-memory writes keep their places | `DIR_COMMAND_BLOCK`, `DIR_REPLY_BLOCK` | R |
| 14 read-port results and incoming traffic don't overlap | `DIR_READPORT_OVERLAP` [`check_read_port_overlap`] | R |
| 15 no errors | `ERRORS_SET`, with `ERROR_LINE` (the line's rise and fall, no message) [`check_errors`] | R |
| 16 the peripherals | `SLOT_VIOLATION` (a reserved window word, `DMA_HDR` or `DMA_ADDR` changed mid-transfer, an event posted at its full count), `PERIPH_VIOLATION` (the replayer's), `TRACER_VIOLATION`; a start while busy, a DMA transfer naming a queue the peripheral lacks, the Tracer's undefined cases (a transfer past its credits among them) | R; F |
| 17 the request contract | `INJ_NO_ENDPOINT`, `AG_DEST_ENDPOINT`, `AG_BROADCAST_ON_TILE`, `REQBUS_NO_RECEIVER`; the limits on outstanding requests are not checked | R |
| 18 one source per order-placed destination | `PERIPH_VIOLATION` (the replayer's queue port); not checked for big SRAM | R |
| 19 everything in bounds | `DIR_PAST_SRAM`, `WE_PAST_SRAM`, `DIR_PAST_CMEM`, `CTL_ALIAS` | R |
| 20 pointers and runs aligned | `CTL_RECORD_BASE_ODD`, `AG_DEST_ALIGN`, `AG_SRC_ALIGN`, `RP_DEST_ALIGN`; a misaligned read-queue chunk | R; F |
| 21 no reserved encodings | `DIR_COUNT_CTR0`, `INJ_COUNT_CTR0`, `AG_COUNT_CTR0`, `INJ_RESERVED_MODE`, `EP_RESERVED_MODE`, `CTL_RESERVED_BITS`, `CTL_RD_DEST_SIZE`, `CTL_RESERVED_ACCESS`; a packet or stream in mode 6–7 at a tile, a reserved `DMA_CONFIG` digit | R; F |
| 22 uncounted traffic is mode 0 only | `DIR_UNCOUNTED_MODE`, `DIR_UNCOUNTED_REGION`, `DIR_UNCOUNTED_SRC`, `DIR_RESERVED_BYTES`, `CTL_DMEM_RESERVED` | R |
| 23 no silent traps | `CTL_HALT`, `CTL_MRET` | S, R |
| 24 K covers the write path | `S1_LATE` [`measure_s1`]; a K below its formula | R; F |
| 25 boot keeps its order | `DIR_OVERLAY_AFTER_COMMAND`, `DIR_COMMAND_KIND` | R |
| 26 no ignored start | a start while a run is running | F |

Invariants 7, 12 and 13 are withdrawn.

**Kinds for rules outside section 9:** `CMEM_COMMAND_UNREAD` (a command over an unread one, 6.3)
[`check_command_read`]; `CTL_WFI_WOKEN`, `CTL_DONE_GO_MASKED` and `CTL_COUNTER_WRAP` (4.12); `CTL_TABLE_BUSY` (a table
window used while its engine runs, 4.10); `AG_SIZE_UNDEF` and `AG_AUG128` (3.1, 4.2); `RP_SIZE_UNDEF` (8.8);
`CNC_RX_LOST` (6.6). **The simulator's own:** `CTL_REQ_FULL`
and `REQBUS_QUEUE_OVERFLOW`, which a correct simulator never raises. **Kinds nothing records:** `EJECT_WRONG_TILE`,
`ILLEGAL_FORWARD`, `CC1_PERIPH`, `SRQ_PAST_SRAM`, `READ_RETURN_REFUSED`, and `AG_START_WHILE_RUNNING` and
`AG_TABLE_WRAP`, whose cases are fatal in the address generator instead.

## Simulator-only words

Two words of the controller block are meshsim's only (spec 8.15): a store to the trace word records a trace mark, and a
store to the exit word ends the simulated test; on hardware both are reserved words. The retire trace is
`tests/TRACE-FORMAT.md`.
