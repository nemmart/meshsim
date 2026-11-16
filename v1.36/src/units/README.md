# src/units: the model of the chip

*For anyone reading or changing the model: which file models which unit, which setting is which spec parameter, and
which check is which invariant.* How a unit is written and added is `src/README.md`.

**The model is the RTL's hardware** (Snarky One), unit by unit: each row below cites v1.36's machine-spec, which has
the RTL's section numbers, and each unit is compared with the specification and with the RTL's own block
(`tests/README.md`). What the model takes from the RTL's sources where the specification is silent, and where it
still differs from the RTL, is `OPEN-v1.36.md`.

**Where the machine is put together.** `chip.h` builds the chip: `tile_cycle` adds one tile's units, `build_chip` the
whole chip from a `ChipSpec`. **The machine's settings (`MachConfig`) and the machine itself (`Mach`: the chip, its
images and the harness around it) live in `tests/mach.h`, not here**; every test, suite and tool builds
its machine through them.

## The units

| unit | file | spec |
|---|---|---|
| the link word, its helpers | `net.h`, `linkword.h` | 3.1 |
| the network's configuration: the hops table, the start skews | `net.h`, `net.cpp` | 3.1, 3.2 |
| the request unit's queues, the request tree (a cycle a node, the root's credits) and the receive queues | `reqbus.h` | 11 |
| the scratchpad: banks of min(size, 512 KiB), a write done in its cycle and winning its bank, a read's row `READ_LATENCY` after its take, no queue | `big_sram.h` | 15 |
| the DMA unit's address generator: the read-ahead and its places, the lead-ins, the timetable, a refused read shifting every later time, the end | `ag.h` | 8.2–8.5 |
| the gather unit: the queue, the offer register (a request leaves the queue when the register takes its first read), the queue's room for the traffic director | `gather.h` | 13 |
| the inject unit: the read arbiter (a write at the read's bank, a token, the gather unit busy; `ERRORS.drift`'s pulse for a group transfer's read it does not take), the tokens, the return line, the injection point with the write unit's `packet_ready` | `inject.h`, `inject.cpp` | 14 |
| the read clients' request words, and the ports between a tile's control part and its inject unit | `mem.h` | 14.1 |
| the router: the RTL's `router`: route blocks, each output's queues and round-robin arbiter, the loopback queue, the overflow in the push's cycle, the attach port (tiles 0, 7, 15), a special tile's router with its DMA inputs and their ready | `router.h`, `router.cpp` | 16 |
| the traffic director: the switch, the counters' delay line, `PTR`'s and `HIST`'s requests in the cycle it accepts a packet | `director.h` | 12.3–12.5 |
| the write engine: a fixed pipeline of 2L + 2 cycles to the scratchpad's write, never refusing | `write_engine.h` | 12.4 |
| the table memories: `STREAM` and `GROUP`; `PTR` and `HIST` with their engines' read-modify-write and its bypass, a core store lost in the cycle of the engine's write | `table_store.h` | 8.6, 12.5 |
| the tile's core: the RTL's `rv32_core`, cycle for cycle, with its decode; it reaches region 0xFF through its environment, in `register_bus`'s cycles | `rv32.h`, `rv32_decode.h` | 6; `spec/core.md` §1 |
| the address map, region 0xFF, the registers' words and bits | `regmap.h` | 5, 7–13, 17.1 |
| the register bus: the decode of region 0xFF and its access faults | `register_bus.h` | 5 |
| the tile control unit as plain code, with the units' registers it holds (`DMA_CONFIG`, `RECEIVE_CONFIG`, the counters, `GATHER_CONFIG`) | `regblock.h` | 7, 8.1, 12.1, 12.2, 13 |
| the tile control unit as a unit; the core with the register bus and the core's side of the write, read and request units; a tile's control part and its straps | `tile_ctl.h`, `tile_ctl.cpp` | 6, 7, 9, 10, 11.1 |
| the peripheral slot: each peripheral's control block and DMA (the RTL's `peripheral_control`, `peripheral_dma`, its packet register on the router's DMA input), the write port, and the contract a peripheral's model meets (`periph_slot.h`, "THE CONTRACT") | `periph_slot.h`, `periph_slot.cpp`, `peripheral.h` | 17.1–17.5 |
| a peripheral's counted events: a consuming load, a set and a fill in one cycle | `event_counts.h` | 7.2, 17.2 |
| Ingress/Egress, the master tile's peripheral 1 (the nets, `RELEASE`, the watchdog; the UART and the host interface as the RTL's `uart.veryl` and `ingress_egress_unit.veryl`, cycle for cycle at the pins and the AXI4-Stream ports, which a test bench plays through its per-cycle `boundary`) | `ingress_egress.h` | 17.6, 17.7, 18 |
| the RW engine, the memory tile's peripheral 1: the RTL's (Snarky One's `rw_engine_unit`), cycle for cycle: its words, the job and its bursts, the write buffer, the reply, both output queues behind the slot's DMA, and the `lpddr` port, an AXI4 master, which is the model's boundary: a harness places the LPDDR controller on it (`LpddrSlave`; the tests' are `tests/axi4_slave.h`), and a job that raises a burst without one stops the simulator | `rw_engine.h` | 17.7, 3, 19 |
| (the bench machines' behavioural master is a test bench's: `tests/bench_master.h`) | | |
| the replayer, as the RTL's `replayer_unit` builds it: its queues, decoders, fetch, row engine, pass and output queue, cycle for cycle | `replayer.h`, `replayer.cpp`, `replayer_alu.h`, `stream_word.h` | 17.8 (`spec/jolt/replayer.md`) |
| the replayer's expansion tables as delivered | `replay_tables.h`, `replay_tables.cpp` | 17.8 (`spec/jolt/replayer.md`); `data/REPLAY-INPUT-REQUIREMENTS.md` |
| what the replayer and the Tracer share | `row_core.h` | 17.8, 17.9 |
| the Tracer: the RTL's (Snarky One's `tracer_unit`), cycle for cycle | `tracer.h`, `tracer.cpp`, `rvc.h`, `lru_coder.h`, `lru_index.h` | 17.9 (`spec/jolt/tracer.md`) |
| the memory system behind the fill port, and the memory as files, as the Tracer sees them: the simulator's own model (17.9.14 is *Open*). A harness may place its own model on the fill port instead (`Tracer::FillPort`: the two request channels' readies and each request's answer), as the RTL's chip tests do; the chip's tests place the RTL test's (`tests/segment_bench.h`) | `mem_system.h`, `file_store.h`, `file_store.cpp`; `tracer.h` | 17.9.5, 17.9.14 |
| S1's measurement | `s1.h` | 12.3; invariant 24 |
| the retire trace | `trace_log.h`, `trace_log.cpp` | 8.15; `tests/TRACE-FORMAT.md` |
| a memory's part of the state fingerprint | `memsum.h` | (simulator only) |
| the suites' test peripheral, type 15 | `test_peripheral.h` | (simulator only) |
| the chip's assembly: the compute tiles and their links, the master, memory and tracer tiles on their attach links, the request tree, the nets | `chip.h`, `chip.cpp` | 2, 11.3, 18 |

The `*.mk` files are the Makefile's rules for these units' tests, one per group (`mem`, `net`, `tracer`, `units`).

## `MachConfig` and the spec's parameters

`MachConfig` (`tests/mach.h`) holds every setting; `MachConfig::chip()` is the chip (the 64 compute tiles and the
three special tiles), `machine()` and `defaults()` the two test-bench machines without special tiles (every tile's
core running, and the coreless machine a test feeds). Fields named by chapter 19's parameters:

| chapter 19 (and where it is specified) | `MachConfig` |
|---|---|
| a link's latency; the router's queues (16.1, 16.3) | `link_latency` (3: a hop is 4 cycles), `queue_depth` (16) |
| `SCRATCHPAD_BYTES` (15) | `mem_chunks`, its 32-bit words: a power of two, 128 to 2²¹ |
| `READ_LATENCY` (15) | `read_latency` (3, the RTL's) |
| `TOKEN_COUNT`, `LOOPBACK_QUEUE_DEPTH` (14.2, 16.3) | `read_tokens` (64), `loopback_depth`; `loopback_tokens` |
| `TABLE_LATENCY` (8) | `ts_engine_latency` (1; 1 to 3: `with_ts_latency`); `READ_AHEAD` is 2 `TABLE_LATENCY` + 2, not a setting |
| `READ_QUEUE_DEPTH` (10) | `read_port_depth` |
| the gather queue (13) | `gather_depth` (64) |
| `LAND_LATENCY` (12.3) | `K` (8); `k_min()` is the write path's formula, and a `K` below it is fatal at startup |
| `WRITE_PORT_LATENCY` (12.3, 17.3) | `cm_latency` (2), and the slot's `region` link of `cm_latency` + 1 |
| the nets (18) | the chip's: `arrive_latency` 0, `release_latency` 1, `error_latency` 0 (the RTL chip's wires and flip-flop; `OPEN-v1.36.md`, (a), for the error line); the bench machines': `ready_latency`, `go_latency` |
| `REQUEST_QUEUE_DEPTH`; the receive queues (11) | `request_queue_depth` (4); `receive_queue_depth` (128, 256, 128) |
| a load's latency in a window (6.4) | `reg_load_latency` |
| the peripherals (17) | `periph_type` (every compute tile's peripheral 1: 4 the replayer, 0 none); `test_peripheral_tile` |
| `REPLAY_QUEUE_ENTRIES` (17.8) | `replayer_log_entries`: 256 only |

The rest are the simulator's: the machine's shape (`special_tiles`: the chip; `coreless`, `real_responder`,
`test_read_clients`, `reqbus_host`, `boot_local`), the bench machines' behavioural master (`ref_cycle`,
`cnc_reaction`, `cnc_rx_depth`, `cnc_go_settle`, `skew`, `skew_seed`: v1.35's start skew on its release links, which
the chip does not have), the checker (`check_errors`, `check_read_port_overlap`, `check_disjoint`, `measure_s1`,
`max_violation_messages`), the debug options (`shuffle_seed`, `step_all`, `instance_major`), the logs and their
capacities (`log_trace`, `*_capacity`), `image_dirs`, and the split over threads (`threads`, `parts`).
`MachConfig::set` and `set_args` take `field=value`; `dropped()` lists the settings of v1.3 and v1.35 that v1.36 does
not have, each refused by name with its reason: the area queues' depth, write combining, the write engine's input
FIFO, the generator's lookahead, the done gate, the checks of the command and reply blocks among them.

## The checker and the invariants

How the invariants of machine-spec chapter 20 are checked. **F**: fatal in every build (`src/base/fatal.h`): the
simulator stops, naming the access; **C**: counted in every build by the unit; **R**: reported by name, as a
`check.h` kind, in the debug and suite builds (`tests/checker.cpp` has each kind's message; `tests/test_checker.cpp`
provokes the packet's). **H**: the hardware's own error, an `ERRORS` bit, which invariant 15's check reports.
An invariant that is not in the table has no check in the simulator: `OPEN-v1.36.md`, (a), lists those the chapter
promises, and the chapter's preamble those it does not.

| invariant | kinds (`src/check.h`) | how |
|---|---|---|
| 1 no queue overflow | `FIFO_OVERFLOW` (the router's queues); `ERRORS.router_overflow`, `ERRORS.gather_overflow` | C, R; H |
| 2 no buffer read and written within one transfer | `DISJOINT` (under `check_disjoint`); a read at a bank a write is at is not taken (14.1), and one that would be is fatal in the scratchpad | R; F |
| 3 the timetable holds | `ERRORS.drift` | H |
| 4 every expected arrival lands first | `CTL_DONE_EARLY`; `ERRORS.early_arrival` | C, R; H |
| 5 no bad packets | `DIR_BAD_PACKET`, `INJ_BAD_PACKET`, `AG_AUG128`; `DIR_SIZE_UNDEF`, `INJ_SIZE_UNDEF`, `AG_SIZE_UNDEF`; `ERRORS.bad_packet`; a misaligned packet | C, R; H; F |
| 6 no `HIST` count past 17 bits | `HIST_OVERFLOW` | C, R |
| 8 no `READ_SEND` finds the read queue full | `RP_QUEUE_FULL`; `ERRORS.read_overflow` | C, R; H |
| 9 no counter underflow | `ERRORS.counter_underflow`, at the decrement | H |
| 10 gathers are local | `INJ_GATHER_ELSEWHERE`, `AG_GATHER_DEST` | C, R |
| 14 read-unit results and incoming traffic do not overlap | `DIR_READPORT_OVERLAP` (under `check_read_port_overlap`) | C, R |
| 15 no errors | `ERRORS_SET` (under `check_errors`): each tile's `ERRORS` bits as they are set, by 7.4's names | R |
| 16 the peripherals | `SLOT_VIOLATION`, `PERIPH_VIOLATION` (the replayer's), `TRACER_VIOLATION`; a `START` while busy | C, R; F |
| 17 the request contract | `INJ_NO_ENDPOINT`, `AG_DEST_ENDPOINT`, `AG_BROADCAST_ON_TILE`, `REQBUS_NO_RECEIVER`, `REQBUS_QUEUE_OVERFLOW`, `CTL_REQ_FULL`; `ERRORS.receive_underflow` | C, R; H |
| 18 one source per order-placed destination | `PERIPH_VIOLATION` (the replayer's queue port) | C, R |
| 19 everything stays in bounds | `DIR_PAST_SRAM`, `WE_PAST_SRAM` | C, R |
| 20 pointers and transfers stay aligned | `AG_DEST_ALIGN`, `AG_SRC_ALIGN`, `RP_DEST_ALIGN`; a misaligned read | C, R; F |
| 21 no reserved encodings | `DIR_COUNT_CTR0`, `INJ_COUNT_CTR0`, `AG_COUNT_CTR0`, `INJ_RESERVED_MODE`, `RP_SIZE_UNDEF`; `ERRORS.access_fault` for a word a block does not define | C, R; H |
| 23 no traps | every trap is an ordinary trap to `mtvec`; `ERRORS.access_fault`, `ERRORS.illegal_instruction` | H |
| 24 `LAND_LATENCY` covers the write path | `S1_LATE` (under `measure_s1`); a `K` below its formula | C, R; F |
| 26 no ignored start | a `DMA_START` while the transfer runs | F |
| 29 a table is the core's only while its engine does not use it | a load of `PTR` or `HIST` in a cycle its engine reads it | F |
| 32 counters stay in range | `CTL_COUNTER_WRAP` | C, R |
| 34 the RW engine's rules | a `START` while busy (but in the cycle a job ends), a burst with no slave on `lpddr` | F |

Invariants 7, 12 and 13 are withdrawn. The bench machines' behavioural master records its own (`CNC_RX_LOST`,
`EP_BAD_PACKET`, `EP_SIZE_UNDEF`, `EP_RESERVED_MODE`): a test bench's, not the chip's.

## No simulator-only words

v1.36 has none (v1.35's exit and trace words are gone with its map: machine-spec 5.5). A program reports to the master
tile by requests (11.5), and every trap is an ordinary trap to `mtvec` (6.2): nothing in an image stops the simulator.
The retire trace (`tests/TRACE-FORMAT.md`) is still written by the core's model when a test gives it a log.
