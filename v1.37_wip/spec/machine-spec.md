# Machine specification — the mesh processor, v1.36

This document specifies the hardware of the mesh processor: the tiles, the NoC that joins them, the request tree,
the peripherals, the control that sequences them, and the registers a tile's program uses. It is written for an
engineer who builds the machine, for the simulator that models it, and for the software that runs on it.

**v1.36 is the RTL's hardware.** Chapters 1 to 19 are the base tile specification of the RTL (Snarky One), which
was written against v1.3 and v1.35 and refined them where building the hardware decided a question; its chapters,
their numbers, its names and its text are taken as they stand, and are the authority on what the hardware does.
Chapters 20 to 27 are meshsim's own: the invariants the simulator checks, the timing summary, the rules and figures
v1.35 states and the RTL's specification does not, the deferred memory system, the budgets, the open items and the
sort. `spec/CHANGES-v1.35-to-v1.36.md` is the guide from v1.35: what changed, the name table, and where each of
v1.35's sections went. v1.35's numbering is not kept; a v1.35 section is always cited as "v1.35's".

**Companion material** (paths in the package):
- `spec/OVERVIEW.md` and `spec/GUIDELINES.md`: what is being built and where it stands, and the rules the design
  follows; read them first.
- `spec/core.md`: the RV32 core's contract (chapter 6 states what the machine requires of it).
- `spec/jolt/`: the Jolt-specific peripherals, the replayer (17.8, `spec/jolt/replayer.md`) and the Tracer (17.9,
  `spec/jolt/tracer.md`), and the replayer's opcode appendix (`spec/jolt/replayer-opcodes.md`).
- `spec/schedule.md`: the 16-group all-to-all schedule (8.4) and the routing ties it fixes (16.2).
- `spec/history/`: v1.35's documents as they were, each earlier version's guide and the decisions behind it.
  Where a log and this document differ, this document is the authority.
- `data/tables/`: the replayer's expansion tables (17.8.6), with the opcode table and a manifest.
- `fw/` and `src/`: the firmware and meshsim, the cycle-level simulator. **The simulator is v1.36's:** it models
  the RTL's hardware at the register, packet and cycle level, and runs the RTL's firmware images (`fw/`)
  unmodified, at the RTL chip's cycle counts. `fw/include/mesh.h` and `fw/REGISTERS.md` are generated from this
  document's tables. What is left of v1.35 in the tree, meshsim's own firmware and the old suites among it, and
  every decision the simulator takes from the RTL's sources where this document is silent, are listed in
  `OPEN-v1.36.md`.

**How it is written.**
- One fact, one place. Chapters 1 to 19 state the hardware contract only: one chapter per module (2.3), each with
  its registers, its behaviour and the errors and events it raises. Chapters 1 to 5 hold what the modules share.
- The mesh is `MESH_SIZE` × `MESH_SIZE` with `TILE_COUNT` = `MESH_SIZE`². `MESH_SIZE` is 1 to 8, and 8 × 8 (64) is
  the default and the largest. Values given for 8 × 8 are examples of the formula, never the rule.
- **Markers.** An unmarked statement is decided. In chapters 1 to 19, *Open* marks a subject still to be decided.
  In chapters 20 to 27, *(Proposed)* means suggested and not yet ruled on, *(Open)* still to be settled, and
  *(Deferred)* set aside until the design it belongs to. Chapter 26 lists every open, proposed and deferred item,
  under v1.35's item numbers, which are never reused.
- **Figures.** A number marked `[meas: RTL]` was measured on the RTL (Snarky One), in simulation, and v1.36's
  simulator reproduces it. One marked `[meas: v1.35]`, or with an earlier version, was measured on that
  version's simulator, on that version's hardware, and is not measured again on v1.36's (item 164). `[estimate]`, `[derived]` and `[model]` are as they say.
- **What is contract and what is not.** A value the contract leaves to the implementation (`READ_LATENCY`, the nets'
  latencies) is the RTL's where the simulator needs one: Appendix A and chapter 21 give them. Synthesis results are
  not part of this specification.

**Checked or undefined** (1.3). Every rule a program must keep is checked by the hardware, which sets an error, or
is undefined behaviour, on which the simulator stops and names the access; there is no third kind
(`spec/GUIDELINES.md`). Chapter 20 lists the invariants the simulator checks.

---

## 1. Terms, names and rules

### 1.1 Terms

One name per thing and one thing per name. The last column is v1.35's
name, where it differs.

| Term | Meaning | v1.35 |
| --- | --- | --- |
| tile | one instance of the base tile (2.2): a compute tile or a special tile | unit |
| compute tile | one of the `TILE_COUNT` tiles of the mesh, tile ids 0 to `TILE_COUNT` − 1 | tile, mesh tile |
| mesh | the compute tiles' arrangement, `MESH_SIZE` × `MESH_SIZE`, their ids row-major | |
| NoC | the packet network: every tile's router, the special tiles' included, and the links between them (16) | network, mesh |
| special tile | the master, memory and tracer tiles | C&C 1–3, the controllers, non-tile endpoints |
| master tile | the special tile that sequences the phases, boots the chip and owns its external IO | C&C 1 |
| memory tile | the special tile that owns LPDDR and the file system | C&C 2 |
| tracer tile | the special tile that holds the Tracer | C&C 3 (the page manager) |
| attach tile | the compute tile whose router connects a special tile to the mesh | tile 0, 1 or 2 |
| attach port | a router port joining a special tile's router to its attach tile's (16.1) | fifth port |
| east, west, south, north, local | the router's link ports and the tile's own (16.1) | right, left, down, up; injection, eject |
| loopback | the path of a tile's packet to itself, and its queue at the eject (16.3); a loopback packet has a copy for the tile itself (14.2) | loopback |
| tile id | a tile's address on the NoC, `TILE_ID_BITS` wide (4.1) | endpoint id, tile idx |
| module | a part of the tile with a chapter of its own: the core, `register_bus`, a unit, the scratchpad, the router, a peripheral (2.3) | |
| unit | a module named `_unit` (2.3) | |
| core | a tile's RV32 processor, with its memories (6) | controller |
| IMEM, DMEM | the core's instruction and data memories | program memory, data memory; together controller memory |
| scratchpad | a tile's large SRAM (15) | big SRAM |
| bank | one independently ported part of the scratchpad | area |
| peripheral | a unit beside the core: peripheral 1, 2 or 3 of its tile (17) | peripheral |
| Ingress/Egress | the master tile's peripheral: its lines, its pins and the host interface (17.6) | C&C 1's peripheral, Ingress/Egress and the UART |
| RW engine | the memory tile's peripheral: its reads and writes of LPDDR (17.7) | the Updater/Binder/RW engine, the memory system |
| write buffer | the RW engine's region, which packets fill and a write copies to LPDDR (17.7) | the tag windows |
| replayer | the compute tile's peripheral: it regenerates the trace's rows (17.8) | the replayer |
| pass | the replayer's work from one `START`: `RUN_CONFIG.rows` rows (17.8.10) | pass, run |
| row | one row of the trace the replayer sends, 640 bits (17.8.11) | row, trace row |
| record | one window's 68-word checkpoint, whose words 0–65 are the replayer's working state (17.8.2) | state record |
| expansion, template | the rows one instruction becomes, and their expansion words in the ROM or fed by the core (17.8.4, 17.8.6) | expansion, inline sequence; template |
| core-fed | a template the core sends into the expansion queue (17.8.6) | controller-fed |
| Tracer | the tracer tile's peripheral: it executes the guest and writes each window's record and streams (17.9) | the Tracer |
| guest | the RV64IMAC program the Tracer executes, whose trace Jolt proves (17.9) | the guest |
| segment | the Tracer's work from one state write: its runs, one a compute tile's window (17.9.3) | segment |
| run | the Tracer's work from one `START`: `RUN_CONFIG.rows` rows and the drain of their output (17.9.3, 17.9.6) | run |
| file, line | the memory system's storage: a tree of 4 KiB pages, named by its root and depth, read and written by 128-byte lines (17.9.5) | file, line |
| fill port | the Tracer's port to the memory system, `fill` (3, 17.9.5) | the fill bus |
| toast pin | the chip's output that says it has failed (17.6) | the "I'm toast" pin |
| beat | one item a handshake moves on a link of the request tree, a host-interface port or the `lpddr` port (3) | beat |
| window | one of the 16 parts of region 0xFF, 512 bytes each: the tile's registers, a peripheral's, or a table (5.2) | register block, window, table window |
| tile window | the tile's own registers, 0xFFFF_F800–0xFFFF_F9FF (5.2) | register block |
| block | 16 words of a window, one module's registers (5.3) | |
| control block | block 0 of every window: `PARAMS`, `STATUS`, `ERRORS`, the events and the start (5.3) | the standard words |
| read arbiter | the fixed-priority choice among the scratchpad's three readers; it has no storage (14.1) | SRAM read queue |
| region, offset | an address's bits [31:24] and [23:0] (5.1) | region, byte offset |
| access fault | a core access the map refuses (5.1) | bus error |
| DMA | an engine the core starts, which then runs a transfer alone: the tile's DMA unit (8), or a peripheral's DMA (17.4) | DMA |
| transfer | the work one `DMA_START` starts: the DMA unit walks its tables (8.2), or a peripheral's DMA sends *n* words (17.4) | DMA run, engine run; transfer |
| write unit | the unit whose registers build and send one packet (9) | send port, `ROUTER_*` |
| read unit | the unit that reads scratchpad words into DMEM for the core (10) | controller read port |
| request tree | the one-way, flow-controlled network carrying requests to the special tiles (11.3) | request bus |
| requester | a request's sender | requestor, requester |
| counter | one of the seven counters a packet may decrement (12.2) | write counter |
| land | a packet's write is in place: readable by a later access | land, arrive |
| barrier | where every phase ends: each compute tile arrives, and the master tile releases them all in the same cycle (7.3) | READY/GO |
| `BARRIER_ARRIVE`, `BARRIER_RELEASE` | the barrier's nets (18) | READY, GO |
| arrive | a tile stores to `ARRIVE`: finished, ready for the release | `done` |
| release | the master tile's pulse on `BARRIER_RELEASE`, which ends the barrier | GO |
| arrival gate | from `ARRIVE` until just after the release, only the release wakes the core (7.3) | done gate |
| early arrival | the error: `ARRIVE` with a counter non-zero | done early |
| counter underflow | the error: a decrement with nothing left to count (12.2) | unexpected arrival |
| image | a program the master tile places in a tile's IMEM, with its data in DMEM, entered at IMEM's base (6.5) | image, overlay |
| UB | undefined behaviour: a rule a program must keep that no error checks (1.3) | |
| packet | one 256-bit word on a link (4.1) | packet, link word |
| `WRITE`, `APPEND`, `BUCKET`, `AUGMENT`, `GATHER`, `COUNT` | modes 0–5 (4.2) | write at address, big-SRAM write by source, bucket write, augmented write, gather request, count only |

**Register names** (v1.35 → here).
- **The tile window.**
  - `PULSE_DONE` → `ARRIVE`, `PULSE_DMA` → `DMA_START`.
  - `DMA_RECORD_BASE_ADDRESS` → `GATHER_CONFIG.record_base`.
  - `ROUTER_HDR`/`ADDR`/`SET_W0`–`W2`/`SEND_32`/`_64`/`_128` →
    `WRITE_HDR`/`DEST_ADDR`/`W0`–`W2`/`SEND_32`/`_64`/`_128`.
  - `REQUEST_SET_W*` → `REQUEST_OUT_W*`, `REQUEST_GET_W*` → `REQUEST_IN_W*`.
  - `WRITE_COUNTER_n` → `COUNTER_n`.
  - `SRAM_RD_ADDR_QUEUE` → `READ_SEND` and, for its free entries, `READ_FREE`.
  - `SRAM_RD_DEST_ADDR` → `READ_HDR` and `READ_DEST_ADDR`.
  - `DMA_CONFIG` keeps its name. Its digits move to `RECEIVE_CONFIG`, and its *k*
    to `GATHER_CONFIG`. `EVENTS_REQUIRED` is gone.
- **Bits.** `STATUS.rd_running` → `STATUS.dma_busy`, `STATUS.done` →
  `STATUS.arrived`. `STATUS.rd_done` is gone: `EVENTS.dma_done` holds it.
  Command landed, `STATUS` [5] and `EVENTS` [9], is gone, and so is the command
  block: there are no commands (6.5).
- **The stream table.** `STREAM_SRC`/`DEST`/`COUNT` →
  `STREAM_SRC_ADDR`/`DEST_ADDR`/`HDR`, and its gap after each stream moves to
  `DMA_CONFIG`.
- **A peripheral's window.** `PULSE_RUN` → `START`, `PULSE_DMA` → `DMA_START`,
  `DMA_ADDR` → `DMA_DEST_ADDR`, and run done → `EVENTS.done`. `DMA_HDR` keeps its
  name.
- **Ingress/Egress.** `READY_LINES_0`–`1` → `ARRIVALS_0`–`1`, `READY_EXPECT_0`–`1`
  → `ARRIVE_EXPECT_0`–`1`, `PULSE_GO` → `RELEASE`, all tiles READY →
  `EVENTS.all_arrived`, and the unit-error bit → `ERRORS.tile_error`.
  `ERROR_LINES_0`–`2`, `WATCHDOG`, `LED` and the `UART_` words keep their names.
- **The replayer.** `PULSE_RUN` → `START`, run done → `EVENTS.done`, and its
  `ERRORS` bits move down one, its reserved [0] dropped (17.8.1). Its words
  32–35, `RUN_CONFIG`, `ROWS_RUN`, `EXPANSION_OFFSET` and `EXPANSION_OPCODE`,
  keep their names: v1.35's.
- **The Tracer.** `PULSE_RUN` → `START`, `PULSE_DMA` → `DMA_START`, `DMA_ADDR`
  → `DMA_DEST_ADDR`, run done → `EVENTS.done`, the advice credit →
  `EVENTS.advice_credit`, and the queues' service flags → `EVENTS.state_ready`,
  `code_ready`, `read_log_ready` and `write_log_ready`. Its `EVENTS` and `ERRORS`
  bits are packed from bit 0 (17.9.1). Its words 32–51 keep their names: v1.35's.

### 1.2 Names

- **Modules.** A standard part keeps its plain name: `core`, `register_bus`,
  `scratchpad`, `router`. Every other module is a `_unit`, and the parts inside a
  module are named for what they do: a generator, an engine, a director, an
  arbiter, a queue, a table. "Port" means only a module's port: the router's
  ports, IMEM's and DMEM's NoC ports, a peripheral's write port, the replayer's
  row port and the Tracer's fill port.
- **Registers** are `UPPER_SNAKE`. A unit's registers carry the unit's name as
  their prefix: `DMA_`, `WRITE_`, `READ_`, `REQUEST_`, `RECEIVE_`, `GATHER_`. A
  peripheral's own carry its part's, where it has parts: `UART_`, `STREAM_`,
  `RW_`. The control block's registers, which every register window has, carry
  none (5.3).
- **Arrays** are named for what they hold, with no prefix: the counters
  `COUNTER_1`–`COUNTER_7`, and the tables `STREAM_SRC_ADDR`, `STREAM_DEST_ADDR`,
  `STREAM_HDR`, `GROUP`, `PTR` and `HIST`. An entry is written `PTR[i]`.
- **Suffixes** have one meaning each:

  | Suffix | Holds |
  | --- | --- |
  | `_CONFIG` | a unit's settings, which no packet carries; one per unit that has settings |
  | `_HDR` | a packet's header fields (5.4) |
  | `_DEST_ADDR` | the address a packet writes at its receiver: its address field (4.1) |
  | `_SRC_ADDR` | the scratchpad word address an element is read from |
  | `_W0`–`_W2` | the data words below a packet's, a request's or a beat's top word |
  | `_FREE` | a queue's free entries |

  An `_ADDR` holds nothing but its address.
- **Actions.** A register whose store does something is named for what it does:
  `ARRIVE`, `START`, `DMA_START`, `WRITE_SEND_32`, `READ_SEND`, `REQUEST_SEND`,
  `RELEASE`, `STREAM_SEND`. So is a load that pops: `REQUEST_RECEIVE`,
  `STREAM_RECEIVE`. A store to `ARRIVE`, `START`, `DMA_START` or `RELEASE` acts
  when its bit 0 is 1, and does nothing when it is 0; its other bits are
  reserved. In every block an action comes after the registers it
  uses, so a block stored in ascending order ends in its action.
- **Fields and bits** are `lower_snake`, written `REGISTER.field`. Every bit of
  `STATUS`, `EVENTS` and `ERRORS` has a name, and an `EVENTS_MASK` bit has its
  event's.
- **Parameters** are `UPPER_SNAKE`, and are listed in one table (19).
- **Interfaces** are `lower_snake`, and a signal is named by its interface, then
  its role: `packet_valid`, `packet_ready` (3).

### 1.3 Checked or undefined

Every rule a program must keep is one of two kinds:
- **Checked:** breaking it sets an error: one of the tile's (7.4), or one of a
  peripheral's own (17.2).
- **Undefined behaviour (UB):** the hardware does nothing special, and the
  outcome is unspecified. meshsim stops on it, and the RTL may assert it in
  simulation, at no cost in area.

A rule stated with no error is UB. So is any use of an encoding this
specification does not define, such as a reserved mode, size or digit, or a
transfer past its table's end.

## 2. Tiles

### 2.1 Tile kinds

The chip is `TILE_COUNT` compute tiles and three special tiles, every one of them
the base tile (2.2), with its peripherals and straps.

| Kind | Tile id | Peripheral 1 | Receives requests | Barrier |
| --- | --- | --- | --- | --- |
| compute tile | 0 to `TILE_COUNT` − 1 | the replayer (17.8), with the `replay_row` port | no | sends `BARRIER_ARRIVE`, takes `BARRIER_RELEASE` |
| master tile | `TILE_COUNT` | Ingress/Egress (17.6): the error lines, the barrier's master side, the watchdog and the toast pin, `LED`, the UART and the host interface | yes | collects `BARRIER_ARRIVE`, drives `BARRIER_RELEASE` |
| memory tile | `TILE_COUNT` + 1 | the RW engine (17.7), with the `lpddr` port | yes | neither |
| tracer tile | `TILE_COUNT` + 2 | the Tracer (17.9), with the `fill` port | yes | neither |

A special tile reaches the mesh through its router's attach port, joined to an
attach port of its attach tile's router (16.1). The attach tiles are the master
tile's tile 0, the memory tile's tile `MESH_SIZE` − 1 and the tracer tile's tile
(2 · `MESH_SIZE` − 1) mod `TILE_COUNT`: 0, 7 and 15 at 8 × 8, all tile 0 at 1 ×
1. So the memory and tracer tiles sit beside each other, off the ends of the
mesh's first two rows.

**The fill link.** The tracer tile's `fill` port (3, 17.9.5) is a
point-to-point link to the memory tile, whose **memory system**, the files'
translator and the shared cache in front of LPDDR, serves it. *Open:* the memory
system, with the file system (item 119; chapter 23); until it is specified, a model
of it stands in in the simulator and in the tests of the RTL (Snarky One), and the chip leaves the port at the tracer tile's
boundary.

### 2.2 The base tile

Every tile is one design, the base tile. Tiles differ only in their straps and
in which peripherals they hold.

A tile is its modules (2.3), its peripherals 1–3, which its kind attaches (2.1),
the global nets (18): `BARRIER_ARRIVE` out, `BARRIER_RELEASE` in, the error
line out and the resets in, and its peripherals' own ports and pins (3, 18).

**Straps.** Fixed when the chip is built, and readable by the core in `PARAMS`
(7.1) and each peripheral's `PARAMS` (17.1):
- the tile id;
- the peripherals present, and in each peripheral its type;
- whether the request unit's receive side is connected, and to which target;
- the attach ports;
- whether the barrier's nets are wired.

**Router.** Every tile has the router, special tiles included.
- On a compute tile, its four link ports join the tile's neighbours in the mesh.
  An attach tile also has an attach port for each special tile attached to it.
- On a special tile, its attach port is its only path to the mesh. Its east and
  west ports are **the line** between the special tiles (16.1), and its south
  input carries its peripherals' DMAs (16.4).

**Scratchpad.** Every tile has the same scratchpad size, `SCRATCHPAD_BYTES`
(4 MiB by default), the special tiles included.

**The field accelerator.** *Open:* where it attaches, as a peripheral or at the
core's coprocessor port.

### 2.3 The module hierarchy

```
base_tile
├── core                 rv32_core, IMEM, DMEM, the boot ROM
├── register_bus         the decode of region 0xFF
├── tile_control_unit    block 0
├── dma_unit             block 1; the address generator, STREAM and GROUP
├── write_unit           block 2; the holding register
├── read_unit            block 3; the read queue
├── request_unit         block 4; the request queue; on a special tile, its receive queue
├── receive_unit         block 5; the traffic director, the counters, the write engine, PTR and HIST
├── gather_unit          block 6; the gather queue
├── inject_unit          the read arbiter, the tokens, the return queues, the injection point
├── scratchpad           its banks
├── router               its ports, queues and arbiters
└── peripherals 1–3      their kinds': ingress_egress_unit (17.6), rw_engine_unit (17.7),
                         replayer_unit (17.8), tracer_unit (17.9)
```

| Module | Its registers | Chapter |
| --- | --- | --- |
| `core` | the CSRs (the core specification) | 6 |
| `register_bus` | none | 5 |
| `tile_control_unit` | block 0: `PARAMS`, `STATUS`, `ERRORS`, `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK`, `ARRIVE` | 7 |
| `dma_unit` | block 1: `DMA_CONFIG`, `DMA_START`; the tables `STREAM_SRC_ADDR`, `STREAM_DEST_ADDR`, `STREAM_HDR`, `GROUP` | 8 |
| `write_unit` | block 2: `WRITE_HDR`, `WRITE_DEST_ADDR`, `WRITE_W0`–`W2`, `WRITE_SEND_32`, `_64`, `_128` | 9 |
| `read_unit` | block 3: `READ_HDR`, `READ_DEST_ADDR`, `READ_SEND`, `READ_FREE` | 10 |
| `request_unit` | block 4: `REQUEST_OUT_W0`–`W2`, `REQUEST_SEND`, `REQUEST_IN_W0`–`W2`, `REQUEST_RECEIVE` | 11 |
| `receive_unit` | block 5: `RECEIVE_CONFIG`, `COUNTER_1`–`7`; the tables `PTR`, `HIST` | 12 |
| `gather_unit` | block 6: `GATHER_CONFIG` | 13 |
| `inject_unit` | none | 14 |
| `scratchpad` | none | 15 |
| `router` | none | 16 |
| peripheral *n* | its window | 17 |

- **One design each.** Each module is a design of its own. It
  meets the others only through the interfaces of 3, and decodes its own block of
  registers (5.3).
- **Where a peripheral sits.** A compute tile's peripheral, the replayer, is
  inside `base_tile`. A special tile's peripherals wrap it: the master, memory
  and tracer tiles are `base_tile` beside Ingress/Egress, the RW engine or the
  Tracer, joined at its peripheral interfaces (3).
- **IP.** The core, the scratchpad, the router and the request tree (11.3) know
  nothing of the tile's map or its units. Their chapters are their contracts, and
  other designs may reuse them.
- **The chip** is the tiles, the links between their routers (16.1), the request
  tree and the global nets (18).

## 3. Interfaces

Modules connect only through the interfaces below. Each is defined here, once;
a module's chapter says what it does at them.

- **Handshakes.** A `valid`/`ready` pair moves one item in a cycle when both are
  1. A sender raises `valid` without waiting for `ready`, and holds `valid` and
  the item until it moves. An interface with no `ready` never waits: its
  receiver takes every item. One exception: a write-log hit 15 that the
  replayer's write-log decoder holds resolves to E[15] as its row takes it, so
  a load row's copy while it is held changes the item (17.8.12).
- **Signals** are named by their interface, then their role: a `packet`
  interface's are `packet_valid`, `packet_ready` and `packet`.

| Interface | From → to | Signals | Rules |
| --- | --- | --- | --- |
| `reg` | `register_bus` → the owner of a block or a table, one interface for each | `valid`, `write`, `word`, `wdata` (32); back, `ready` and `rdata` (32) | `word` is the word's index: 4 bits in a block, 8 in a table, 7 in `GROUP`. A load's `rdata` returns at its fixed latency (6.4). `ready` is read only with a store, and is 0 only for one that stalls, as 9, 11.1 or a peripheral's section defines, and comes from registered state, never from the same cycle's request (the core specification's Obligations of the enclosing unit, `spec/core.md`) |
| `packet` | `write_unit` → `inject_unit`; a peripheral's DMA → its router input | `valid`, `ready`, `packet` | the packet's defined bits (4.1) |
| `link` | `inject_unit` → the router's local input; router → router; the router's eject → `receive_unit` | `valid`, `packet`; the eject's also `loopback` | no `ready`: the receiver takes every packet (16.1). `valid` is src ≠ none (4.1). `loopback` is 1 for a packet from the loopback queue (16.3) |
| `read_request` | `dma_unit`, `gather_unit`, `read_unit` → `inject_unit` | `valid`, `ready`, `address`, `header`; back, `done` | `address` is the read's word address (5.4), and `header` its packet's size, counter, mode, dst and address (4.1). `ready` is the read arbiter taking the read (14.1). `done` pulses when one of the client's reads has been injected (14.3) |
| `scratchpad_read` | `inject_unit` → `scratchpad` | `valid`, `address`; back, `data` (128) | `address` is a row's: a word address (5.4) over 4. `data` is that row, `READ_LATENCY` cycles after `valid` (15) |
| `scratchpad_write` | `receive_unit` → `scratchpad`, seen by `inject_unit` | `valid`, `address`, `data` (128), `enable` (4) | `address` is a row's, as `scratchpad_read`'s. Never waits. `enable` has one bit per 32-bit word. The read arbiter keeps that cycle's reads off its bank (14.1) |
| `noc_write` | `receive_unit` → IMEM, DMEM, a peripheral's write port | `valid`, `address` (32), `size`, `data` (128) | never waits. IMEM and DMEM write at the end of the cycle; a peripheral lands within `PERIPHERAL_LATENCY` (17.3) |
| `gather_request` | `receive_unit` → `gather_unit` | `valid`, `ordinal` (20), `counter` (3) | never waits: a full queue loses the request (13) |
| `token_return` | `receive_unit` → `inject_unit` | `valid` | frees one token (14.2) |
| `record_base` | `gather_unit` → `receive_unit` | `record_base` (21) | `GATHER_CONFIG.record_base`, a level (12.4) |
| `status` | each unit → `tile_control_unit` | one level per `STATUS` bit it owns (7.1) | `gather_busy` also goes to `inject_unit` (14.1) |
| `event` | each unit and peripheral → `tile_control_unit` | per counted tile event, `set` and `clear` pulses; per live tile event, a level. A peripheral sends only its `peripheral_n` level | the tile's event state is the control unit's (7.2), and a peripheral's its own (17.2). A `set` and a `clear` in one cycle follow 7.2's tie rule |
| `counter_event` | `receive_unit` → `tile_control_unit` | per counter, `arm` and `drained` pulses; `all_drained`, a pulse; `nonzero`, a level | `arm`: a store of N > 0. `drained`: a decrement, not an underflow, left the counter at 0, and `all_drained`: all seven. `nonzero`: a counter is non-zero at the start of the cycle (7.2, 7.3, 12.2) |
| `error` | each module → `tile_control_unit` | per sticky error, a pulse; per peripheral, a level, its `ERRORS` ≠ 0 | 7.4 names each error's source |
| `core_status` | `core` → `tile_control_unit` | `canonical_wait`, a registered level; `access_fault` and `illegal_instruction`, pulses when the core takes that exception | 6.2, 7.3 |
| `nmi`, `meip` | `tile_control_unit` → `core` | one wire each | 6.2 |
| `request_link` | `request_unit` → the request tree → a receiving tile's `request_unit` | `valid`, `ready`, `beat` (16) | a request is 8 beats (11.3) |
| `request_credit` | a receiving tile's `request_unit` → the tree's root | `valid` | returns one credit, for a popped request (11.3) |
| `lvds_in` | the LVDS IO block → the master tile's Ingress/Egress | `tvalid`, `tready`, `tdata` (8·`STREAM_BYTES`), `tlast` | AXI4-Stream (ARM IHI 0051): its other signals take their defaults, so every byte is a data byte. A beat moves when `tvalid` and `tready` are both 1, and a sender holds a beat it offers until then. Ingress/Egress's output is a flip-flop. The LVDS IO block holds `tvalid` at 0 through the reset (18) |
| `lvds_out` | the master tile's Ingress/Egress → the LVDS IO block | the same | as `lvds_in`'s, the roles swapped: every output of Ingress/Egress is a flip-flop |
| `lpddr` | the memory tile's RW engine → the LPDDR controller | AXI4's five channels (ARM IHI 0022), by its names: 128-bit data, `LPDDR_ADDRESS_BITS` of byte address | the RW engine is the master: one ID, so no ID signals, and the controller answers the reads in order and the writes in order; INCR bursts, none crossing a 4 KiB boundary; `rready` and `bready` always 1; no lock, cache, protection, QoS, region or user signals, which take AXI4's defaults, so no access is exclusive and an `EXOKAY` is an error response; every output a flip-flop or a constant. The controller answers every burst and write in bounded time, sends no write response before both the write's address and data handshakes, and answers nothing accepted before a reset after it (17.7, 18) |
| `replay_row` | a compute tile's replayer → its row's consumer | `valid`, `ready`, `row` (640) | a row a cycle, in pass order, from the output queue: `valid` while it holds a row, the row held until its handshake (17.8.11). *Open:* its consumer, the materializer |
| `fill` | the tracer tile's Tracer → the memory tile's memory system (2.1) | four channels, AXI4's in shape: `read_request` (`valid`, `ready`, `root` (31), `depth` (2), `line` (35)); `read_response` (`valid`, `data` (1,024), `status` (2)); `write_request` (`valid`, `ready`, `root`, `depth`, `line`, `data` (1,024)); `write_response` (`valid`, `status` (2)) | a request names one 128-byte line of a file (17.9.5), and moves at its handshake. Each request has exactly one response, on its channel, with no `ready`: the Tracer takes every one, in a cycle after its request's handshake, the next at the earliest. `data` carries the line's byte *b* at bits [8*b* + 7 : 8*b*]. A read's status is `ok`, `hole`, `not_owned` or `beyond` (17.9.5); a write's is 0 `ok`, the line written, or 1 `error`, the line not written, and 2 and 3 are reserved. The Tracer has at most one read and one write outstanding, and every output of it is a flip-flop. The memory system answers every request in bounded time, keeps a read of a line after a write of it already accepted, and answers nothing accepted before a reset after it (18). *Open:* the memory system's side (2.1) and the link's physical width |

The core's own ports, to IMEM, DMEM, `register_bus` and the coprocessor, are
the core specification's; `core_status`, `nmi` and `meip` are the ones this
specification adds. `lvds_in`, `lvds_out` and `lpddr` leave the tile, and the
chip: they are its ports to the LVDS IO block and the LPDDR controller, which
are outside it. `replay_row` leaves the tile, for a consumer that is *Open*
(17.8.11), and `fill` leaves the tracer tile, for a memory system that is *Open*
(2.1).

## 4. The packet

### 4.1 Format

Every link carries one 256-bit **packet** per cycle in each direction.

| Field | Bits | Meaning |
| --- | --- | --- |
| payload | [127:0] | 32, 64 or 128 bits, by size, from bit 0; the bits above the size are 0 |
| address | [159:128] | the receiver's address (5.1). Mode `WRITE` uses it. For every other mode the sending hardware puts 0, whatever its `_DEST_ADDR` holds, and the receiver ignores it |
| size | [161:160] | the payload's width: 00 32 bits, 01 64, 10 128; 11 reserved |
| counter | [164:162] | the counter the packet decrements at the receiver (12.2): 0 none, 1–7 `COUNTER_1`–`7` |
| mode | [167:165] | what the receiver does (4.2) |
| src | [168 + `TILE_ID_BITS` − 1 : 168] | the sender's tile id, stamped by the injecting hardware |
| dst | the next `TILE_ID_BITS` bits | the destination's tile id |
| reserved | the rest, to [255] | sent as 0 and ignored |

- **Tile ids.** `TILE_ID_BITS` = ⌈log₂(`TILE_COUNT` + 5)⌉, room for the compute
  tiles, the three special tiles, broadcast and none: 7 at 8 × 8.
  - The top value means **none**: as src, an idle packet that carries nothing.
  - The one below it is **broadcast**, which is valid as a dst only and reaches
    every compute tile.
  - The special tiles are `TILE_COUNT`, `TILE_COUNT` + 1 and `TILE_COUNT` + 2.
- **The defined bits.** The format is 256 bits, and its reserved bits are 0, so
  the hardware carries and stores only the defined bits, [167 + 2·`TILE_ID_BITS` :
  0] (182 at 8 × 8): every port, queue and pipeline stage, the links' included.
- **The header block.** Size, counter and mode are one 8-bit block, [167:160].
  Every register that carries a packet header holds it in its bits [7:0] (5.4).

### 4.2 Modes

| Mode | Name | What the receiver does | Counted |
| --- | --- | --- | --- |
| 0 | `WRITE` | writes the payload at the address: DMEM or IMEM (6.3), the scratchpad, or a peripheral's region | by its counter field |
| 1 | `APPEND` | writes the payload in the scratchpad at `PTR[src]`, then steps `PTR[src]` by the payload's size | by its counter field |
| 2 | `BUCKET` | as `APPEND`, at `PTR[t]`, *t* the payload's bucket digit | by its counter field |
| 3 | `AUGMENT` | writes one 64-bit record at `PTR[src]`: the payload's low 32 bits (the key), and its ordinal. Then it steps `PTR[src]` by 2 words. 32 or 64 bits only | by its counter field |
| 4 | `GATHER` | a gather request, for the gather unit (13). Sent only to the tile itself | never: its counter field passes to the values it gathers |
| 5 | `COUNT` | writes nothing | by its counter field; with counter 0, nothing at all |
| 6, 7 | reserved | UB (1.3) | |

**A `COUNT` packet's uses.**
- **The fence.** A transfer that something must follow ends with a `COUNT` packet
  to the tile itself, on a spare counter. When that counter reaches 0,
  everything earlier in the transfer has been injected.
- **"Done" between tiles.** A `COUNT` packet tells the receiver that the sender's
  data has all been sent, without writing a word nobody reads.

**Bad packets.** The traffic director drops a bad packet, uncounted, and sets
`ERRORS.bad_packet`. A packet is bad when it is:
- a `WRITE` to the boot ROM, to region 0xFF, to an unmapped region or to an
  absent peripheral;
- an `AUGMENT` at 128 bits.

Nothing else makes a packet bad: a packet misaligned to its size, a `WRITE`
past the end of its memory and a `GATHER` from another tile are UB (1.3).

## 5. Region 0xFF and `register_bus`

### 5.1 The address map

One 32-bit map serves the core's own accesses and the addresses that packets
carry. An address is an 8-bit **region**, bits [31:24], and a 24-bit byte
**offset**, bits [23:0].

| Region | Contents | The core | A `WRITE` packet |
| --- | --- | --- | --- |
| 0x00 | DMEM | load, store | write |
| 0x01 | IMEM | fetch | write |
| 0x02 | boot ROM; the reset PC is 0x0200_0000 | fetch | bad packet |
| 0x03 | scratchpad | none: through the read unit and packets | write |
| 0x04, 0x05, 0x06 | peripherals 1, 2, 3 | none: through their windows | write, if present |
| 0xFF | the windows: the tile's, the peripherals' and the tables (5.2) | load, store | bad packet |
| any other | none | access fault | bad packet |

- **The decode.** An unmapped address is an access fault for the core and a bad
  packet at the traffic director, and so is an absent peripheral's region. An
  address past the end of its memory is UB (1.3).
- **Sizes.** `SCRATCHPAD_BYTES` is a power of two from 512 B to 8 MiB, at most
  the reach of the units' 21-bit word addresses (5.4).
- **Access faults** (6.2) are:
  - a load or store to IMEM or the boot ROM;
  - an unmapped address, region 0xFF's included (5.2, 5.3);
  - a sub-word access in region 0xFF;
  - a store to a read-only register.
- **Reads.** A load from a write-only register reads 0.

### 5.2 Region 0xFF

Region 0xFF maps only its top 8 KiB, 0xFFFF_E000–0xFFFF_FFFF: 16 **windows** of
512 bytes, window *w* at 0xFFFF_E000 + 0x200·*w*, picked by address bits [12:9].

| Window | Address | Contents |
| --- | --- | --- |
| 0, 1 | 0xFFFF_E000 | `HIST` (12.5) |
| 2, 3 | 0xFFFF_E400 | `PTR` (12.5) |
| 4, 5 | 0xFFFF_E800 | `STREAM_SRC_ADDR` (8.3) |
| 6, 7 | 0xFFFF_EC00 | `STREAM_DEST_ADDR` (8.3) |
| 8, 9 | 0xFFFF_F000 | `STREAM_HDR` (8.3) |
| 10 | 0xFFFF_F400 | `GROUP` (8.4) |
| 11 | 0xFFFF_F600 | unmapped |
| 12 | 0xFFFF_F800 | the tile window (5.3) |
| 13, 14, 15 | 0xFFFF_FA00, 0xFFFF_FC00, 0xFFFF_FE00 | the windows of peripherals 1, 2 and 3 (17.1) |

- **Unmapped:** the rest of region 0xFF, window 11, and an absent peripheral's
  window.
- **Registers.** The tile window and the peripheral windows sit in the top
  2 KiB, so each register is one `lw` or `sw` relative to `x0`. A table needs a
  base register.
- **Tables.** Entry *i* of a table is the word at +4·*i*. `GROUP[g]` is two
  words, at +8·*g* and +8·*g* + 4. A table stores only its fields' bits: a core
  store ignores the others, and a core load reads them as 0. The core may load
  and store every table, as its owner's chapter allows (8.6, 12.5).
- **`register_bus`** decodes region 0xFF: a window, then a block (5.3), and passes
  the access to the module that owns it, over the `reg` interface (3). It holds
  the whole map, every window's included, and refuses every access fault in
  region 0xFF itself (5.1).

### 5.3 Windows and blocks

Every window of registers, the tile's and each peripheral's, is eight **blocks**
of 16 words: block *b* at +0x40·*b*, picked by address bits [8:6]. A block holds
one module's registers, and that module decodes its words, address bits [5:2].

| Block | The tile window | A peripheral's window |
| --- | --- | --- |
| 0 | `tile_control_unit` (7) | its control block (17.1) |
| 1 | `dma_unit` (8) | its DMA, if it has one (17.4) |
| 2 | `write_unit` (9) | its own |
| 3 | `read_unit` (10) | its own |
| 4 | `request_unit` (11) | its own |
| 5 | `receive_unit` (12) | its own |
| 6 | `gather_unit` (13) | its own |
| 7 | unused | its own |

- **The control block**, block 0, is the same in every register window: `PARAMS`,
  `STATUS`, `ERRORS`, `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK`, then the start,
  `ARRIVE` in the tile window and `START` in a peripheral's.
  - `STATUS` [0] and `EVENTS` [0] are the start's state and its end: `arrived`
    and `release` in the tile window, `busy` and `done` in a peripheral's.
  - `STATUS` [1] and `EVENTS` [1] are the DMA's: `dma_busy` and `dma_done`.
- **Block 1 is the window's DMA**: the tile's DMA unit, or a peripheral's DMA.
- **Unmapped.** A word a block does not define, and a block a window does not
  use, is unmapped: an access fault (5.1). The one exception is the block 1 of
  a peripheral without a DMA, every word of which reads 0 (17.1).

### 5.4 Register conventions

- **Words.** Registers are 32-bit words, accessed whole.
- **Access.** R: load only, and a store is an access fault. W: store only, and a
  load reads 0. R/W: both. A load takes the fixed latency of 6.4.
- **Fields** are packed from bit 0, in the order their tables list them, and a
  field never straddles a word. The bits above the last field are reserved: a
  store ignores them and a load reads 0.
- **Header registers** (`_HDR`) share one layout:

  | Bits | Field |
  | --- | --- |
  | [7:0] | the header block, as in the packet (4.1): size [1:0], counter [4:2], mode [7:5] |
  | [14:8] | `dst`, a tile id: `TILE_ID_BITS` wide, its upper bits 0 when narrower |
  | [31:15] | the register's own fields |

  A field the unit fills itself is ignored on a store and reads 0: the size in
  `WRITE_HDR` and in a peripheral's `DMA_HDR`, and the mode and `dst` in
  `READ_HDR`.
- **Word addresses.** Inside the units, a scratchpad address is a 21-bit **word
  address** (a word is 32 bits), in bits [20:0]: `STREAM_SRC_ADDR`, a `PTR` entry,
  `READ_SEND` and `GATHER_CONFIG.record_base`. Word address *w* is the byte
  address `0x0300_0000 | w << 2`.
- **Byte addresses**, every `_DEST_ADDR`, take all 32 bits.

### 5.5 The register map

The tile window, at 0xFFFF_F800. Offsets are from the window's base.

| Offset | Block, word | Register | Access | Section |
| --- | --- | --- | --- | --- |
| 0x000 | 0, 0 | `PARAMS` | R | 7.1 |
| 0x004 | 0, 1 | `STATUS` | R | 7.1 |
| 0x008 | 0, 2 | `ERRORS` | R | 7.4 |
| 0x00C | 0, 3 | `EVENTS` | R | 7.2 |
| 0x010 | 0, 4 | `EVENTS_PEEK` | R | 7.2 |
| 0x014 | 0, 5 | `EVENTS_MASK` | R/W | 7.2 |
| 0x018 | 0, 6 | `ARRIVE` | W | 7.3 |
| 0x040 | 1, 0 | `DMA_CONFIG` | R/W | 8.1 |
| 0x044 | 1, 1 | `DMA_START` | W | 8.2 |
| 0x080 | 2, 0 | `WRITE_HDR` | R/W | 9 |
| 0x084 | 2, 1 | `WRITE_DEST_ADDR` | R/W | 9 |
| 0x088–0x090 | 2, 2–4 | `WRITE_W0`–`W2` | W | 9 |
| 0x094–0x09C | 2, 5–7 | `WRITE_SEND_32`, `WRITE_SEND_64`, `WRITE_SEND_128` | W | 9 |
| 0x0C0 | 3, 0 | `READ_HDR` | R/W | 10 |
| 0x0C4 | 3, 1 | `READ_DEST_ADDR` | R/W | 10 |
| 0x0C8 | 3, 2 | `READ_SEND` | W | 10 |
| 0x0CC | 3, 3 | `READ_FREE` | R | 10 |
| 0x100–0x108 | 4, 0–2 | `REQUEST_OUT_W0`–`W2` | W | 11.1 |
| 0x10C | 4, 3 | `REQUEST_SEND` | W | 11.1 |
| 0x110–0x118 | 4, 4–6 | `REQUEST_IN_W0`–`W2` | R | 11.1 |
| 0x11C | 4, 7 | `REQUEST_RECEIVE` | R | 11.1 |
| 0x140 | 5, 0 | `RECEIVE_CONFIG` | R/W | 12.1 |
| 0x144–0x15C | 5, 1–7 | `COUNTER_1`–`COUNTER_7`, `COUNTER_n` at word *n* | R/W | 12.2 |
| 0x180 | 6, 0 | `GATHER_CONFIG` | R/W | 13 |

A peripheral's window, at 0xFFFF_F800 + 0x200·*n*:

| Offset | Block, word | Register | Access | Section |
| --- | --- | --- | --- | --- |
| 0x000–0x018 | 0, 0–6 | `PARAMS`, `STATUS`, `ERRORS`, `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK`, `START` | as the tile's; `START` W | 17.1 |
| 0x040 | 1, 0 | `DMA_HDR` | R/W | 17.4 |
| 0x044 | 1, 1 | `DMA_DEST_ADDR` | R/W | 17.4 |
| 0x048 | 1, 2 | `DMA_START` | W | 17.4 |
| 0x080–0x1FC | 2–7 | the peripheral's own | | its section |

## 6. `core`

The core's own contract is the core specification,
`spec/core.md`. This chapter
states what the machine requires of it.

### 6.1 ISA and CSRs

- **ISA:** RV32I + C + Zmmul + Zicsr + Zicond, in machine mode only. Custom-0 and
  custom-1 instructions go to the coprocessor port. *Open:* what the coprocessor
  port serves, decided with the field accelerator (2.2).
- **Time:** `mcycle` and `mcycleh`. The chip has one clock, so the cycle count is
  the time: there is no timer and no timer interrupt.

### 6.2 Errors and wakes

The tile drives two of the core's inputs, `nmi` and `meip`, and no others.

- **The NMI is the tile's errors:** `ERRORS` is non-zero (7.4).
  - It is non-maskable: it is taken whatever `mstatus.MIE` and `mie` hold, and
    from `WFI` too.
  - **It is taken at most once:** a core that has taken any trap since reset
    takes no NMI. A synchronous exception in the same cycle wins, so the NMI is
    then never taken, the one an exception raises through `ERRORS` included.
  - **Its entry** is a trap's: `mepc` holds the next instruction's address,
    `mcause` is 0x8000_0000 (an NMI of unknown cause, as the privileged
    architecture reserves), `mtval` is 0, and `mstatus` changes as for any
    trap.
- **The wake is MEIP:** the wake term of the tile's events (7.2).
  - Software keeps `mstatus.MIE` at 0, so the wake releases `WFI` by the
    standard rule (`(mip & mie)` ≠ 0) and never traps.
  - The boot ROM sets `mie.MEIE`.

**Every trap goes to `mtvec`,** the NMI included. `mtvec` resets to the boot
ROM's handler, and the ROM sets it there again on every entry (6.5). An image
may point it at a handler of its own, which it then keeps in place until it
ends.

**Every trap is terminal.** The handler reports and parks, and only a reset
restarts the core.

**Synchronous exceptions** are:
- illegal instruction;
- access fault (5.1);
- a misaligned load or store, which traps before it reaches the map, so it is
  never an access fault;
- `ecall` and `ebreak`.

No jump target is misaligned: with C, every branch and `JAL` offset is even and
`JALR` clears bit 0.

Only an access fault or an illegal instruction also sets a bit in `ERRORS`,
`access_fault` or `illegal_instruction`, which raises the tile's error line.

### 6.3 Memories

| Memory | Size | The core | The NoC port |
| --- | --- | --- | --- |
| DMEM | `DMEM_BYTES`, 32 KiB by default | loads and stores | the traffic director's writes |
| IMEM | `IMEM_BYTES`, 32 KiB by default | fetches only | the traffic director's writes |
| boot ROM | `BOOT_ROM_BYTES`, 256 bytes | fetches only | none |

- **The core port and the NoC port.** Each of IMEM and DMEM has two ports. The
  core's port is the core's alone: its loads and stores on DMEM, its fetches on
  IMEM. The NoC port is a `noc_write` interface (3): writes only, one per cycle,
  32, 64 or 128 bits, naturally aligned. It never delays the core.
- **A collision.** A core access and a NoC write to the same word in the same
  cycle are UB (1.3).
- **Code written into IMEM through the NoC port runs** the next time it is
  fetched.
- **Reserved:** the first 16 bytes of DMEM, which no program uses, so that an
  all-zero header (an uncounted `WRITE` to address 0) and a null-pointer store
  land there harmlessly.

### 6.4 Timing the machine requires

The core's cycle counts are its own (the core specification). The machine
requires only these:
- **Load latencies.** A load from a window, the tile's or a peripheral's, takes
  `WINDOW_LOAD_LATENCY` = 2 cycles, and a load from a table
  `TABLE_LOAD_LATENCY` = 1, on every tile and whatever the data or traffic. A
  load issued in cycle *t* with latency *L* returns the state at the end of
  *t* + *L* − 2, its **sample point**, and takes its side effect (an `EVENTS`
  load's consumption, a pop) at the end of *t* + *L* − 1.
- **Stores.** A store takes effect as a load from the same place takes its side
  effect: a store issued in cycle *t* takes effect at the end of
  *t* + *L* − 1, the end of *t* + 1 in a window and of *t* in a table, and is
  answered in *t* + 1 in both. A store that may stall (9, 11.1, 17.6) takes
  effect at the end of the cycle *c* its unit takes it, *t* + 1 at the
  earliest, and is answered in *c* + 1. The state at the end of a cycle
  includes what takes effect then, so a load issued after a store sees it.
- **An access fault** (5.1) is answered in *t* + 1, a load's or a store's.
- **Lockstep.** Cores that leave the canonical wait (7.3) at the same `WFI` in
  the same cycle, and run identical code, retire identically, cycle for cycle.
  A phase's synchronised start relies on this.

### 6.5 The boot ROM

Every tile has the same boot ROM, at the reset PC. It never writes DMEM.

1. Set `mtvec` to the ROM's handler, `mie.MEIE`, and `EVENTS_MASK` to its reset
   value (18).
2. Branch on the straps, never on the tile id: if `PARAMS.boot_local` is 1,
   jump to IMEM's base, 0x0100_0000.
3. **Otherwise, the load:**
   1. Store 1 to `COUNTER_7`, then send the master tile a `LOAD` request
      (11.5).
   2. Wait for the load: `WFI`, then a load of `EVENTS`, which consumes
      `all_counters_done`, until that load returns it.
   3. If `PARAMS.barrier` is 1: store to `ARRIVE`, then wait for the release in
      the same way, the load consuming `release`.
   4. Jump to IMEM's base.

Counter 7 is the only counter armed in step 3, so `all_counters_done` is its
drain, and `EVENTS_MASK`'s reset value selects it. The compute tiles and the
tracer tile take step 3, the tracer tile without the barrier (2.1, 7.3).

**The load.** The master tile answers a `LOAD` with the tile's image and
arguments, `WRITE`s to its IMEM and DMEM, all uncounted, then one `COUNT`
packet on counter 7.
- **Order.** Every packet of the load, the `COUNT` included, comes from one of
  the master tile's sending units and is of one kind, all unicast or all
  broadcast, so it arrives in the order sent (14.4). Every write is in place
  when its counter would be decremented (12.3), so when the `COUNT` drains
  counter 7, the whole load has landed. A load from several sending units
  fences the others before the `COUNT` (4.2).
- **When.** The master tile starts a tile's load only once that tile's `LOAD`
  has reached it, so the tile's core waits in the ROM and counter 7 is armed:
  the arming store takes effect before the request is sent (6.4).
- **Rules.** UB (1.3): a packet of the load that arrives after its `COUNT`, and
  a write to the tile's IMEM or DMEM from its `LOAD`'s sending until its jump
  to IMEM's base, other than the load's. A counted packet in the load, or a `COUNT`
  that reaches the tile before its arming, drains a counter it should not, and
  sets `ERRORS.counter_underflow` (12.2).
- **Early arrival stays clean.** The tile arrives once counter 7 has drained,
  and every other counter is at 0: the reset zeroes them, and an image ends
  with them at 0.
- **After the reset** the path is the same: the master tile cannot otherwise
  know that a tile has armed counter 7. When a tile's image was placed before
  the reset ended, its load is the `COUNT` alone.

**No guard.** A compute tile jumps at every release, with no check of its own:
its load has landed before it arrives, and every compute tile takes part in
every release (7.3). The master tile releases once every compute tile has
arrived.

**What the image starts with.** `mtvec` the ROM's handler, `mie.MEIE` set and
`mstatus.MIE` 0, `EVENTS_MASK` its reset value, the release and
`all_counters_done` consumed, `counter_7_done` cleared by the `ARRIVE`, and
every counter at 0; on the tracer tile, which does not arrive,
`counter_7_done` still set. No register holds a value the image may use. An image runs
its own start-up code between the release and its first phase, so one whose
phases need lockstep ends its start-up at a barrier.

**Arguments.** An image's arguments are data the master tile writes into the
tile's DMEM in its load, at addresses the image fixes: software's, with no
hardware of their own.

**A loaded image ends** by jumping to the reset PC, its counters at 0 (7.3).
The ROM then asks for the next load.

**The boot strap.** `boot_local` is 1 on the master tile and the memory tile,
which start from an image placed in their IMEM and DMEM before the reset ends,
as the simulator, the tests of the RTL (Snarky One) and an FPGA build, whose
memories are initialised, place
them. It is 0 on the compute tiles and the tracer tile, which the master tile
loads at their `LOAD`s. *Open:* the master tile's boot over LVDS (17.6), and how
a silicon memory tile's IMEM and DMEM are filled.

**The handler** is also in the ROM, at `mtvec`'s reset value (6.2). It sends the
master tile two reports, then parks in a jump to itself.

| Report | Words 1–3 |
| --- | --- |
| 24 | `ERRORS`, `mcause`, `mepc` |
| 25 | `mtval`, `STATUS`, `ra` |

- **A report** is one `REPORT` request to the master tile (11.5), which asks for
  no reply: word 0 holds the hardware's fields, the operation and the report's
  number. The requester field names the tile.
- **The numbers** tell the master tile which set is coming: the ROM's are 24
  and 25, and a loaded image's handler sends its own reports, numbered from 0.

## 7. `tile_control_unit`

The tile control unit holds the control block, block 0 of the tile window. It
collects every unit's status bits, events and errors (3), and drives the core's
`nmi` and `meip` (6.2), the barrier's nets and the error line (18).

### 7.1 Registers

| Word | Register | Access | Fields |
| --- | --- | --- | --- |
| 0 | `PARAMS` | R | [6:0] `tile_id`; [9:7] `peripherals`, bit *n* − 1 for peripheral *n*; [10] `receiver`, 1 if the request unit's receive side is connected; [13:11] `request_target`, the target it receives for (11.2); [16:14] `attach`, the special tiles attached here, bit 0 the master tile's, 1 the memory tile's, 2 the tracer tile's (16.1), and on a special tile its own; [17] `barrier`, 1 if the barrier's nets are wired; [18] `boot_local`, 1 if the tile starts from the image already in its IMEM (6.5) |
| 1 | `STATUS` | R | [0] `arrived`, [1] `dma_busy`, [2] `gather_busy`, [3] `request_full` |
| 2 | `ERRORS` | R | one bit per error (7.4) |
| 3 | `EVENTS` | R | one bit per event (7.2); a load consumes |
| 4 | `EVENTS_PEEK` | R | the same, consuming nothing |
| 5 | `EVENTS_MASK` | R/W | one bit per event |
| 6 | `ARRIVE` | W | arrives (7.3) |

`STATUS` holds states, and `EVENTS` what has happened. It gathers its bits from
the units that own them: `arrived` from this unit, `dma_busy` from the DMA unit
(8.2), `gather_busy` from the gather unit (13), and `request_full` from the
request unit (11.1).

`PARAMS` holds the straps alone. The memories' sizes are the chip's parameters,
which its software is built with (19).

### 7.2 Events

`EVENTS` collects every event the core may wait for. **The wake** (6.2) is
`(EVENTS & EVENTS_MASK)` ≠ 0, except under the arrival gate (7.3), when it is
`EVENTS.release` alone.

**One model for every event**, the tile's and each peripheral's. An event is
one of two kinds:
- **Counted, with a depth *d*.** It holds a count from 0 to *d*, and its bit
  reads 1 while the count is above 0. Its cause adds 1, up to *d*. A consuming
  load subtracts 1. Depth 1 is an ordinary sticky event.
- **Live.** Its bit follows its source, and no load changes it.

**Reading `EVENTS`.**
- **A load of `EVENTS`** returns every bit. It consumes one count from each
  counted bit that it returns set and that `EVENTS_MASK` selects.
- **`EVENTS_PEEK`** returns the same value and consumes nothing.
- **The sample point.** A load returns the state at its sample point (6.4), and
  consumes exactly what it returned. An event set after the sample point
  survives.
- **A wake consumes nothing.**

**The one tie rule.** A clear that starts a new wait wins over a set, in the same
cycle, that ends the wait before it: `DMA_START`'s and `ARRIVE`'s clears. Arming
a counter meets no such set, since a counter at 0 in its arming cycle has ended
the new wait. A consuming load never removes an event it did not return.

**`EVENTS_MASK`** (read/write) chooses the events that wake the core and those a
load consumes. Hardware never writes it, except at the reset, to `release` and
`all_counters_done`.

**The tile's events:**

| Bit | Event | Kind | Set by | Also cleared by |
| --- | --- | --- | --- | --- |
| 0 | `release` | counted, 1 | `BARRIER_RELEASE` (7.3) | |
| 1 | `dma_done` | counted, 1 | the end of the tile's transfer (8.2) | `DMA_START` |
| 2 | `all_counters_done` | counted, 1 | a decrement that leaves all seven counters at 0 while the event is armed, which disarms it | arming any counter; `ARRIVE` |
| 3 | `request_pending` | live | a request in the tile's receive queue (11.1) | |
| 4–6 | `peripheral_1`–`peripheral_3` | live | that peripheral's `EVENTS & EVENTS_MASK` ≠ 0 (17.2) | |
| 7–13 | `counter_1_done`–`counter_7_done`, `counter_n_done` at bit 6 + *n* | counted, 1 | a decrement that leaves counter *n* at 0 while the event is armed, which disarms it (12.2) | arming counter *n*; `ARRIVE` |

### 7.3 The barrier

Every phase ends at the barrier. Each compute tile arrives, and the master tile
releases them all in the same cycle. The special tiles do not take part: an
`ARRIVE` on a tile whose `PARAMS.barrier` is 0 is UB (1.3), since no release
ends its arrival gate. The master tile's side, which collects the arrivals and
releases them with a store, is its Ingress/Egress's (17.6).

- **`STATUS.arrived`** is set by `ARRIVE`, and cleared by the release, which
  wins in the same cycle, and by the reset.
- **The tile is ready** when `STATUS.arrived` is set and its core is in the
  **canonical wait**: asleep in `WFI`, its instruction port idle (the core
  specification's Lockstep regions, `spec/core.md`). So a release
  finds the core in a state that depends only on where it waits.
- **`BARRIER_ARRIVE`** is a one-cycle pulse to the master tile, from a
  flip-flop, in the cycle after the tile becomes ready.
- **`BARRIER_RELEASE`** comes from the master tile and reaches every compute
  tile in the same cycle. Its pulse, the release, sets `EVENTS.release`, and
  clears `STATUS.arrived`.
- **The arrival gate.** From `ARRIVE` until the cycle after the release, the
  wake is `EVENTS.release` alone, so the release wakes the core whatever
  `EVENTS_MASK` holds. Every other event still sets as usual, and wakes the core
  after the gate if `EVENTS_MASK` selects it. The gate writes no register. It
  does not hold back the NMI.
- **Early arrival.** An `ARRIVE` in a cycle that starts with a counter non-zero
  sets `ERRORS.early_arrival`.
- **Every compute tile takes part in every release.** A release before every
  compute tile has arrived is UB (1.3): it leaves a pending `EVENTS.release` on
  a tile that has not, which ends that tile's next arrival gate at once.

### 7.4 Errors

`ERRORS` has one bit per error.
- **Meaning.** An error means a program did something wrong, or the hardware is
  faulty. A correct program on correct hardware never sets one.
- **Cause.** An error is a *program error* (P), or a *hardware-fault detector*
  (H), which no program can cause.
- **Sticky.** Each bit is sticky until the reset, except the peripherals' bits,
  which are live: a peripheral's bit is 1 while that peripheral's `ERRORS` is
  non-zero.
- **What it raises.** A non-zero `ERRORS` raises the NMI (6.2) and the tile's
  error line (18).
- **Overflows.** On every overflow the arriving item is lost. "Full" is the count
  before the cycle's pop.

| Bit | Error | Kind | Raised by | Defined in |
| --- | --- | --- | --- | --- |
| 0 | `router_overflow` | P | `router` | 16.3 |
| 1 | `bad_packet` | P | `receive_unit` | 4.2 |
| 2 | `counter_underflow` | P | `receive_unit` | 12.2 |
| 3 | `drift` | P | `dma_unit` | 8.4 |
| 4 | `gather_overflow` | P | `gather_unit` | 13 |
| 5 | `read_overflow` | P | `read_unit` | 10 |
| 6 | `early_arrival` | P | `tile_control_unit` | 7.3 |
| 7 | `access_fault` | P (H on a corrupted image) | `core` | 5.1 |
| 8 | `illegal_instruction` | P (H on a corrupted image) | `core` | 6.2 |
| 9 | `receive_underflow` | P | `request_unit` | 11.1 |
| 10–12 | `peripheral_1`–`peripheral_3` | live | peripherals 1–3 | 17.2 |

## 8. `dma_unit`

The DMA unit is the tile's DMA. Its address generator walks `STREAM` (and
`GROUP`), and for each element offers one read to the read arbiter (14.1). The
read's return becomes one packet. It does not interpret data.

`TABLE_LATENCY` is the cycles a unit's request or return takes to cross between
it and its tables, each way.

### 8.1 Registers

| Word | Register | Access | Fields |
| --- | --- | --- | --- |
| 0 | `DMA_CONFIG` | R/W | [0] `kind`: 0 a stream transfer, 1 a group transfer; [8:1] `start`: the first stream (0–255), or the first group (0–63); [17:9] `count`: streams (1–256), or groups (1–64), and 0 an empty transfer (8.2); [20:18] `gap`: stream transfers only, idle cycles after each stream (0–7) |
| 1 | `DMA_START` | W | starts the transfer `DMA_CONFIG` describes (8.2) |

### 8.2 A transfer

- **The start.** `DMA_START` clears `EVENTS.dma_done` and sets
  `STATUS.dma_busy`.
  - A start while a transfer is in progress is UB (1.3).
  - A start store in cycle *t* takes effect at the end of *t* + 1 (6.4).
  - An empty transfer ends in the cycle after its start takes effect:
    `STATUS.dma_busy` is 1 for that cycle alone, and `EVENTS.dma_done` is set
    at its end.
  - **The lead-in.** The transfer's first cycle, `S[start]`, is
    2·`TABLE_LATENCY` + 2 cycles after the start store in a stream transfer, and
    4·`TABLE_LATENCY` + 3 in a group transfer, whatever its first entry holds.
    It depends only on the transfer's kind and `TABLE_LATENCY`, so a
    synchronised start stays in lockstep.
- **The end.** The transfer ends when it has walked its last entry: count-0
  streams and R = 0 groups included, each at its timetable cost, but the last
  entry's gap is not waited out. Once the transfer's last table read has
  completed, `EVENTS.dma_done` is set and `STATUS.dma_busy` falls, in the same
  cycle.
  - **What it promises.** The read arbiter has taken every read of the
    transfer, and the address generator reads no table entry again. Its packets
    may still be on their way to injection: a fence (4.2) says when they have
    left.

### 8.3 `STREAM` and stream transfers

**`STREAM[0..255]`**, three arrays read in parallel at one index:

| Array | Fields |
| --- | --- |
| `STREAM_SRC_ADDR` | [20:0] the source: the first element's word address, aligned to the size |
| `STREAM_DEST_ADDR` | [31:0] `WRITE` only: the first element's byte address at the receiver, aligned to the size. Each element advances it by the size |
| `STREAM_HDR` | [7:0] the header block: size, counter and mode, stamped on every packet; [14:8] `dst`, the destination: below `TILE_COUNT`, a compute tile, (`me` + `dst`) mod `TILE_COUNT` on a compute tile, so that `dst` = 0 is the tile itself, and tile `dst` on a special tile; otherwise an absolute tile id, a special tile or broadcast; [31:15] `count`: elements, 0 to 2¹⁷ − 1 |

- **Element *e*** is read at source + *e* × (1, 2 or 4 words, by size), and sent
  as one packet of that size. The address generator never writes `STREAM` or
  `GROUP`.
- **A stream transfer** sends streams *start* … *start* + *count* − 1 one after
  another, each whole before the next.
  - **The timetable.** A stream offers one element a cycle. Stream *s* begins at
    `S[s] = S[s−1] + count[s−1] + gap`, from the lead-in's `S[start]`, and the
    gap is `DMA_CONFIG.gap`.
  - **A count-0 stream** sends nothing and takes only the gap. With a gap of 0,
    or as the transfer's last stream, it takes no cycle: in one cycle, every such
    stream whose entry is in passes, up to the first other stream, whose first
    element or gap cycle is that cycle's.
  - **A refused read** holds its element until taken, and shifts every later
    time. In a stream transfer that is not an error.

### 8.4 `GROUP` and group transfers

**`GROUP[0..63]`**, two words per entry:

| Word | Fields |
| --- | --- |
| 0 | [17:0] `rotations`, R, at most 2¹⁸ − 1; [25:18] `first_stream`; [29:26] `streams`, *t* − 1, *t* the streams in the group (1–16) |
| 1 | [7:0] `gap`: idle cycles after the group (0–255) |

**A group transfer** sends its groups in order. For each group:
- **The rotations.** The group makes R rotations over its *t* streams, one visit a
  cycle. On rotation *r*, a stream sends its element *r* if *r* is below its
  count, and is idle otherwise. Its period is *t* cycles.
- **The timetable.** Group *g* begins at cycle `S[g] = S[g−1] + max(R[g−1] ·
  t[g−1], 1) + gap[g−1]`, from the lead-in's `S[start]`. A group with R = 0
  takes one cycle and its gap.
- **Drift.** A read the read arbiter refuses holds its visit until taken, shifts
  every later time, and sets `ERRORS.drift`.

### 8.5 Read-ahead

The address generator reads `GROUP` and `STREAM` entries ahead of their visits,
into two queues of `READ_AHEAD` = 2·`TABLE_LATENCY` + 2 places: `GROUP` entries in
group order, and `STREAM` entries in visit order (a group's streams, R times
over; an R = 0 group reads none).
- **Reads.** Each table takes one read a cycle, issued whenever its queue has a
  free place. A group's `STREAM` reads wait for its `GROUP` entry, and the
  `STREAM` reads cross at most one group boundary a cycle. An entry is in
  2·`TABLE_LATENCY` + 1 cycles after its read is issued.
- **Places.** An entry holds its place from its read's issue until its visit is
  made, when the read arbiter takes its read (14.1), a `GROUP` entry until its
  group ends.
- **Waits.** A stream's or a group's start, or a visit, whose entries are not yet
  in waits for them, and every later time shifts. The wait depends only on the
  tables and `TABLE_LATENCY`, the same on every tile: it is part of the
  timetable, not drift.

### 8.6 The tables' ports

| Table | Size | Ports |
| --- | --- | --- |
| `STREAM_SRC_ADDR` | 256 × 21 bits | dual-port |
| `STREAM_DEST_ADDR` | 256 × 32 bits | dual-port |
| `STREAM_HDR` | 256 × (25 + `TILE_ID_BITS`) bits, 32 at 8 × 8 | dual-port |
| `GROUP` | 64 × 38 bits: word 0's 64 × 30 and word 1's 64 × 8 | dual-port, each |

- **One SRAM each,** of the size above, but `GROUP`, in two, one for each word
  of its entries (8.4), 64 × 30 and 64 × 8 bits. Each SRAM has two ports: one is
  the core's, through `register_bus`, and the other the address generator's,
  which only reads. A store to `GROUP` writes its word's SRAM alone, and the
  address generator reads an entry from both.
- **The core** may load any entry at any time. It may store any entry the running
  transfer does not read, so it can write the next transfer's entries during
  this one.
  - A stream transfer reads `STREAM[`*start* … *start* + *count* − 1`]`. A group
    transfer reads `GROUP[`*start* … *start* + *count* − 1`]` and each of those
    groups' streams.
  - A store to an entry the running transfer reads leaves what the transfer
    reads of it undefined.
- **Knowing when.** `EVENTS.dma_done` says the transfer has read its last table
  entry (8.2).

## 9. `write_unit`

The write unit's registers fill a one-packet **holding register** in front of
the injection point (14.3).

| Word | Register | Access | Fields |
| --- | --- | --- | --- |
| 0 | `WRITE_HDR` | R/W | [7:0] the header block, its size the send's; [14:8] `dst`, an absolute tile id; [15] `no_increment` |
| 1 | `WRITE_DEST_ADDR` | R/W | [31:0] the next `WRITE`'s address. A packet of any other mode carries 0 (4.1) |
| 2–4 | `WRITE_W0`–`WRITE_W2` | W | payload bits [31:0], [63:32] and [95:64] |
| 5–7 | `WRITE_SEND_32`, `WRITE_SEND_64`, `WRITE_SEND_128` | W | the packet's top word, payload bits [31:0], [63:32] or [127:96]; sends a packet of that size |

- **A packet.** Software stores a packet's words in ascending order, and the
  send, the last store, puts the top word above `WRITE_W0`–`W2`.
- **At the send:**
  - a `WRITE` advances `WRITE_DEST_ADDR` by the size, unless `no_increment` is
    set;
  - `WRITE_W0`–`W2` clear to 0, as at the reset (18), so a word not stored
    since the last send is 0;
  - `no_increment` stays set until the program clears it.
- **Stalls.** A send stalls only while the holding register is full and is not
  being injected that cycle.
- **No checks.** The write unit checks nothing: whatever is written goes out.

## 10. `read_unit`

The read unit reads scratchpad words into DMEM, so the core can read the
scratchpad without stalling: the core queues reads, and each lands in DMEM.

| Word | Register | Access | Fields |
| --- | --- | --- | --- |
| 0 | `READ_HDR` | R/W | [7:0] the header block: the next read's size and counter; its mode is the unit's (5.4) |
| 1 | `READ_DEST_ADDR` | R/W | [31:0] the next read's address in DMEM; any other is UB (1.3) |
| 2 | `READ_SEND` | W | [20:0] a word address: queues a read of `READ_HDR`'s size from it |
| 3 | `READ_FREE` | R | [15:0] the read queue's free entries |

- **A read.** A store to `READ_SEND` queues a read, then `READ_DEST_ADDR`
  advances by the size.
- **The read queue** has `READ_QUEUE_DEPTH` entries, at least 64. An entry is
  freed when its packet is injected (`read_request.done`, 3).
- **The return.** The read goes through the read arbiter (14.1) and comes back as
  one `WRITE` packet to the tile itself, at its destination, counted on its
  counter. Reads may land in any order: each lands where its entry named.
- **A full queue.** A `READ_SEND` with no free entry drops the read and sets
  `ERRORS.read_overflow`. `READ_DEST_ADDR` still advances, as v1.35's does, so
  the reads after it land where the program placed them.

## 11. `request_unit`

Requests travel one way, from any tile to a special tile, over the **request
tree**, which is separate from the NoC. It has back-pressure and drops nothing.
Every answer comes back over the NoC (11.4). The request unit is the tile's end
of the tree, in both directions.

### 11.1 Registers

| Word | Register | Access | Fields |
| --- | --- | --- | --- |
| 0–2 | `REQUEST_OUT_W0`–`REQUEST_OUT_W2` | W | bits [31:0], [63:32] and [95:64] of the next request |
| 3 | `REQUEST_SEND` | W | bits [127:96]; sends the request |
| 4–6 | `REQUEST_IN_W0`–`REQUEST_IN_W2` | R | bits [31:0], [63:32] and [95:64] of the oldest received request |
| 7 | `REQUEST_RECEIVE` | R | bits [127:96]; pops it |

- **Sending.** `REQUEST_SEND` pushes the request into the tile's **request
  queue** of `REQUEST_QUEUE_DEPTH` requests. The push clears
  `REQUEST_OUT_W0`–`W2`.
  - **The stall.** A `REQUEST_SEND` stalls while the queue is full and no entry
    leaves that cycle.
  - **`STATUS.request_full`** is 1 while the queue is full.
- **Receiving.** On a special tile, `REQUEST_IN_W0`–`W2` and `REQUEST_RECEIVE`
  read the oldest request in the tile's receive queue (11.3), and a load of
  `REQUEST_RECEIVE` pops it, returning its credit to the root.
  - **`EVENTS.request_pending`** is live: 1 while the queue holds a request
    (7.2).
  - **A tile that receives no requests** (`PARAMS.receiver` 0) has no receive
    queue, and reads an empty one.
  - **An empty queue.** A `REQUEST_RECEIVE` on an empty queue returns 0, pops
    nothing, and sets `ERRORS.receive_underflow`. A load of
    `REQUEST_IN_W0`–`W2` on an empty queue returns 0.
- **After an error** the request unit keeps running, so the handler's reports go
  out. The reset empties every request queue, the tree and the receive queues
  together, and restores the credits.

### 11.2 The request

A request is 128 bits.
- **The hardware's fields** are in word 0: [6:0] the **requester**, the
  sender's tile id (`TILE_ID_BITS` wide, its upper bits 0 when narrower), and
  [9:7] the **target**, naming the receiver.
- **The target's fields.** Every other bit is the target's to define: its
  operation, addresses, length, the counters and reply address it answers on
  (11.4).

| Target | Receiver |
| --- | --- |
| 0 | the master tile |
| 1 | the memory tile |
| 2 | the tracer tile |
| 3–7 | reserved: a request for one is dropped at the root |

### 11.3 The tree

- **Nodes.** There is one **row node** per row of `MESH_SIZE` tiles, with an input
  from each, and one **root**, with an input from each row node and each special
  tile.
- **Links.** Every link is a `request_link` (3): 16 bits, one beat a cycle, with
  ready/valid, pipelined as its length needs. A request is 8 beats, bits [15:0]
  first.
- **Arbitration.** Each node buffers one whole request per input, round-robins
  over its inputs as the router's arbiters do (16.3), a row node's in column
  order and the root's in row order, then the master, memory and tracer tiles,
  and passes a whole request before it chooses again.
- **Cut-through.** A node may choose a request once its first beat is in, and
  passes each beat on as it arrives, as v1.35's do. A request whose sender
  pauses holds its node until its last beat has passed.
- **The requester field.** A row node writes the requester for a tile, and the
  root for a special tile. Software writes 0 there, so no request can claim
  another sender's id.
- **Receive queues.** Each target's **receive queue** is in its tile's request
  unit (11.1), at the end of a `request_link` from the root: 128-bit entries,
  `RECEIVE_QUEUE_DEPTH` deep, a power of two at least `TILE_COUNT` × the
  target's per-tile limit + 64:

  | Target | Per-tile limit | Depth at 8 × 8 |
  | --- | --- | --- |
  | master | 1 | 128 |
  | memory | 3 | 256 |
  | tracer | 1 | 128 |

- **Credits.** The root keeps one credit per entry of each target's queue. It
  skips an input whose next request's target has no credit, and takes one when
  it starts to forward a request, holding it through all eight beats. A request
  enters the receive queue, and can be read, once its last beat arrives. The
  receiving tile returns the credit (`request_credit`, 3) when the request is
  popped; a `REQUEST_RECEIVE` on an empty queue returns none. A stalled input
  holds up its row behind it.
- **Order.** A sender's requests to one target arrive in the order sent. There is
  no order between senders.

**The per-tile limits are a software convention.** A tile counts a request as
outstanding until its reply has landed, or until its data has landed for a read
with no reply; a request that asks for no reply, until the target pops it. Error
reports (6.5) are exempt.

### 11.4 Answers

The receiver answers over the NoC, for every target:
- **Arming first.** The requester arms the counters for the request's data and
  reply before sending it, and names them in the request with the reply's
  address.
- **The data.** Any bulk data comes first, as `WRITE` packets to the destination
  the request named, counted on its data counter.
- **The reply.** Then, if the request has a value answer, exactly one reply
  follows: a 32-, 64- or 128-bit `WRITE` to the reply address the request
  named, anywhere in the requester's DMEM and aligned to its size, counted on its
  reply counter. Its value is in its low bits, the bits above it 0. The memory
  tile's replies are 128-bit (11.5, 17.7), so the requester keeps 16 bytes for
  each.
- **Order between answers.** A receiver may answer requests in any order. The
  data packets of one answer stay in ascending address order.

### 11.5 The special tiles' requests

The special tiles' requests lay out word 0 alike, the hardware's fields (11.2) first:

| Bits | Field |
| --- | --- |
| [6:0] | the requester (11.2) |
| [9:7] | the target (11.2) |
| [17:10] | `op`, the operation. 0 is reserved, so a request whose word 0 was never stored (11.1) is never a valid one |
| [31:18] | the operation's own fields |

Words 1–3 are the operation's. An `op` the target does not define is UB (1.3).

**The master tile's** (target 0):

| `op` | Request | Word 0 [31:18] | Words 1–3 |
| --- | --- | --- | --- |
| 1 | `REPORT` | [25:18] `number`, 0–255 | three data words |
| 3 | `LOAD` | 0 | 0 |

- **A report** asks for no reply. Its number says which data words it carries:
  24 and 25 are the boot ROM's (6.5), and a loaded image's handler numbers its
  own from 0.
- **`LOAD`** is the boot ROM's: the tile waits in the ROM, counter 7 armed, for
  its next image. Its answer is the load (6.5), not 11.4's, and it is
  outstanding (11.3) until the load's `COUNT` lands.
- Every other `op` is the master tile's firmware's.

**The memory tile's** (target 1), which its core serves with the RW engine
(17.7):

| `op` | Request | Word 0 [31:18] | Word 1 | Word 2 | Word 3 |
| --- | --- | --- | --- | --- | --- |
| 1 | `READ` | [27:18] `beats`, 0–1023; [30:28] `data_counter` | the first beat's LPDDR beat address (17.7) | the data's byte address at the requester | [23:0] `reply_addr`, a byte offset in the requester's DMEM; [26:24] `reply_counter`, 0 for no reply |
| 2 | `WRITE` | [27:18] `beats`, 0–1023 | the first beat's LPDDR beat address | 0 | as `READ`'s, `reply_counter` non-zero |

- **`READ`.** The memory tile sends `beats` beats of LPDDR as 128-bit `WRITE`s,
  from word 2's address up, counted on `data_counter`, then, if `reply_counter`
  is non-zero, a 128-bit reply of 0 (11.4).
- **`WRITE`** asks for a part of the write buffer (17.7). Its 128-bit reply
  holds the part's address, in the memory tile's region 0x04, in [31:0], and in
  [34:32] the memory tile's counter, already armed, that counts the part's
  packets. The requester then sends exactly `beats` 128-bit `WRITE`s into the
  part, counted on that counter, and the memory tile writes them to LPDDR once
  they have landed.
- Every other `op` is the memory tile's firmware's: its file system's (item 119;
  chapter 23).

**The tracer tile's** (target 2): every `op` is its firmware's (17.9.10).

## 12. `receive_unit`

The receive unit is the only way into a tile. Its traffic director takes the
router's eject, counts what it accepts, and hands each write to IMEM, DMEM, the
write engine or a peripheral's write port (17.3), and each `GATHER` to the
gather unit (13).

### 12.1 Registers

| Word | Register | Access | Fields |
| --- | --- | --- | --- |
| 0 | `RECEIVE_CONFIG` | R/W | [2:0] `bucket_digit`, [5:3] `histogram_digit` (12.5) |
| 1–7 | `COUNTER_1`–`COUNTER_7` | R/W | [19:0] the count (12.2); `COUNTER_n` at word *n* |

### 12.2 Counters

Seven **counters**, `COUNTER_1` to `COUNTER_7`, `COUNTER_BITS` = 20 bits each.

- **Which counter.** A packet's counter field names one of them (1–7), or none
  (0).
- **The decrement.** The traffic director decrements the named counter once
  the packet's write has landed (12.3). A counter takes at most one decrement a
  cycle.
- **Each packet counts 1,** whatever its size.
- **A store of N adds N,** in step with the decrement of the cycle it takes
  effect (6.4): next = count + N − decrement.
  - A store of N > 0 **arms** the counter. It clears its `EVENTS.counter_n_done`
    and `EVENTS.all_counters_done`, and arms both (7.2).
  - A store of 0 does nothing.
  - A sum past 2²⁰ − 1 is UB (1.3).
- **A load** returns the count.
- **Counter underflow.** A decrement when count + N is 0 leaves the counter at 0
  and sets `ERRORS.counter_underflow`, and no event: it drains nothing.
- **No overwrite.** A counter is never overwritten. It returns to 0 only by
  counting down, or at the reset.

### 12.3 The traffic director

The traffic director takes at most one packet a cycle from the router's eject
output and never stalls.

| Packet | Goes to |
| --- | --- |
| a bad packet (4.2) | dropped, uncounted |
| `WRITE` to DMEM or IMEM | that memory's NoC port (6.3) |
| `WRITE` to the scratchpad | the write engine, at its address |
| `WRITE` to a peripheral's region | that peripheral's write port (17.3) |
| `APPEND`, `BUCKET`, `AUGMENT` | the write engine, through `PTR` |
| `GATHER` | the gather unit's queue (13) |
| `COUNT` | nothing is written |
| modes 6, 7 | UB (1.3) |

**The NoC ports.** The director presents a `WRITE` to DMEM, IMEM or a
peripheral on that one's `noc_write` (3) `WRITE_PORT_LATENCY` = 2 cycles after
accepting it, as v1.35 does.

**Counting.** For each `WRITE`, `APPEND`, `BUCKET`, `AUGMENT` or `COUNT` packet
it accepts with a non-zero counter field, the director puts the counter's
number into a **delay line** of `LAND_LATENCY` stages. `LAND_LATENCY` cycles
later, that counter is decremented (12.2).
- **`LAND_LATENCY`** is 8 cycles: every write the director takes is in place
  that long after its acceptance, and an implementation's pipelines and
  `TABLE_LATENCY` must fit within it:
  - DMEM and IMEM: written;
  - a peripheral: landed;
  - the scratchpad: written in its bank, so a read taken after the decrement
    sees it (15);
  - `PTR` and `HIST`: updated, and seen by a load whose sample point follows the
    decrement.
- **Bad packets.** A packet dropped as bad is not counted.
- **Dropped writes.** A write a peripheral drops by a rule of its own (17.3) is
  still counted.

### 12.4 The write engine

The write engine is the scratchpad's only writer. It takes the director's
scratchpad writes one a cycle. Its write wins its bank (15), so no write waits,
and no queue holds one back.

| Mode | Where it writes |
| --- | --- |
| `WRITE` | at the packet's address. It reads no `PTR` entry and moves none |
| `APPEND` | at `PTR[src]`, then `PTR[src]` += 1, 2 or 4 words, by size |
| `BUCKET` | at `PTR[t]`, *t* the payload's bucket digit (12.5), then `PTR[t]` += the size |
| `AUGMENT` | the record (key, ordinal) at `PTR[src]`, then `PTR[src]` += 2 |

- **Order.** Every write passes through a pointer stage in arrival order, so the
  engine's writes reach the scratchpad in the order the director accepted them.
- **Augmenting.** The key is the payload's low 32 bits. The ordinal, in the
  record's bits [63:32], is (the record's word address −
  `GATHER_CONFIG.record_base` (13)) / 2: a position in the buffer, counting 0,
  1, 2, …
- **Alignment.** Each `PTR` entry stays aligned to the size of the writes
  through it, a record for `AUGMENT` (15).
- **Count-on-arrival.** When `RECEIVE_CONFIG.histogram_digit` is non-zero,
  every `APPEND`, `BUCKET` or `AUGMENT` payload the engine writes increments
  `HIST` at that digit. The bucket and the histogram updates can happen in the
  same cycle, for different digits.

### 12.5 Digits, `PTR` and `HIST`

**Digits.** `RECEIVE_CONFIG` holds two digits, `bucket_digit` and
`histogram_digit`. Each is 0 for none, or 1–4 for byte *d* of the payload's low
32 bits, 1 being the least significant; 5–7 are reserved.

**The tables** are SRAMs, one memory each:

| Table | Fields | Size | Ports | Its engine |
| --- | --- | --- | --- | --- |
| `HIST` | [16:0] the count | 256 × `HIST_BITS` (17) bits | two-port, shared | count-on-arrival: a read-modify-write per cycle |
| `PTR` | [20:0] a word address | 256 × 21 bits | two-port, shared | the write engine: a read-modify-write per cycle |

- **Two ports.** Each has a read port and a write port, which its engine's
  read-modify-write uses every cycle.
  - The update has a bypass, so no update to one entry is lost at any spacing,
    and no queue holds an update back.
  - The core shares the two ports, through `register_bus`, so a program uses
    `PTR` or `HIST` only while its engine is idle. In a cycle the engine uses a
    port, the engine wins: a core store is lost, and a core load returns an
    undefined value.
- **A `HIST` count** past 17 bits is UB (1.3).
- **Knowing when.** `EVENTS.counter_n_done` says the arrivals' `PTR` and `HIST`
  updates are done, by `LAND_LATENCY`.

## 13. `gather_unit`

The gather unit reorders values by a sorted index, on the tile itself. `GATHER`
packets drive it: the core never starts it.

| Word | Register | Access | Fields |
| --- | --- | --- | --- |
| 0 | `GATHER_CONFIG` | R/W | [20:0] `record_base`: the word address of the values' first record, aligned to a row (128 bits) whatever *k* is; [23:21] `value_size`: *k* − 1, *k* the 64-bit records in a value (1–8) |

The receive unit's write engine also reads `record_base`, for `AUGMENT`'s
ordinals (12.4). A store to `GATHER_CONFIG` while the unit is busy, or while an
`AUGMENT` that reads it has not yet decremented its counter, is UB (1.3).
- **The queue.** A `GATHER` packet's payload carries its ordinal in bits
  [51:32]. The traffic director passes the ordinal and the counter field to the
  **gather queue**, of `GATHER_QUEUE_DEPTH` = `TOKEN_COUNT` requests (14.2).
  - While the unit is busy, the address generator takes no read (14.1), so it
    sends no further `GATHER`. Each one already sent holds a token until it
    enters the queue (14.2), so they fit: with the address generator as the only
    source, the queue never overflows.
  - `GATHER`s from the tile's other senders, the write unit and a peripheral's
    DMA, can fill it. A `GATHER` from any source that arrives to a full queue
    is lost, and sets `ERRORS.gather_overflow`.
- **The reads.** A request's value starts at word address *v* = `record_base`
  + 2*k*·ordinal. When *k* is even, the unit reads it as *k*/2 128-bit rows, at
  *v* + 4*j* for *j* = 0 … *k*/2 − 1. When *k* is odd, it reads *k* 64-bit
  records, at *v* + 2*i* for *i* = 0 … *k* − 1.
- **The values.** Each read goes back to the tile itself as one `APPEND` of its
  size, carrying the request's counter field, so the values land in order at
  `PTR[me]`, which, like `record_base`, is aligned to a row whatever *k* is. A
  value counts *k*/2 packets when *k* is even, and *k* when it is odd.
- **`STATUS.gather_busy`** is 1, the unit **busy**, while its queue holds a
  request or the read arbiter has yet to take a request's last read.

## 14. `inject_unit`

The inject unit is every sender's way into the router: the read arbiter, the
read tokens with the return queues, and the injection point. All the tile sends
passes through it, a scratchpad read turned into a packet or the write unit's
packet, except a peripheral DMA's (16.4).

### 14.1 The read arbiter

The arbiter chooses one read a cycle, in fixed priority: the gather unit, then
the address generator, then the read unit.
- It takes a read only if no write meets the read at its bank (15) and a **read
  token** is free (14.2).
- Otherwise it takes nothing that cycle, and a lower client does not get the
  cycle.
- While the gather unit is busy (13), the address generator takes no read, even
  in a cycle when the gather unit takes none.

### 14.2 Tokens

There are `TOKEN_COUNT` = 64 tokens. A **loopback packet** is one with a copy for
the tile itself: a packet to the tile itself, or a compute tile's broadcast.
- **What holds one.** Every read holds a token, and so does the write unit's
  loopback packet. The inject unit keeps an in-order return queue for each
  client, holding each taken read's address and header until its packet is
  injected (`read_request`, 3).
- **When it is freed.** A token is freed when its packet is injected, except a
  loopback packet's, which is freed when the traffic director takes its copy
  (12.3, `token_return`). For a `GATHER`, that is the cycle it enters the
  gather queue or is lost at it (13).
- **Two takers.** In a cycle when both want one, the read arbiter takes a token
  first, and the write unit only if another is free. A token freed in a cycle
  can be taken from the next.
- **Why.** So the loopback queue can never overflow.

### 14.3 Injection

The injection point takes one packet a cycle: a read return first, otherwise the
write unit's holding register. Read returns are taken in the arbiter's priority,
and each pulses its client's `read_request.done` (3). The router stamps src.
A packet is injected in the cycle the injection point takes it, and reaches the
router's local input in the next, through the link's register stage.

### 14.4 Order

**One sending unit, one order.** A tile's **sending units** are:
- the DMA unit's address generator;
- the write unit;
- the gather unit;
- the read unit;
- each peripheral's DMA.

Packets from one sending unit to one destination, of one kind (all unicast, or
all broadcast), arrive in the order sent.
- **Between units there is no order.** To make one unit's packet follow
  another's, a program ends the transfer with a fence (4.2) and waits for it, or
  puts the packet in the transfer. A fence orders only the units that share the
  injection point. A peripheral's DMA enters the router on an input of its own
  (16.4), so only its destination orders it against the others: a later packet
  waits until the destination says the earlier ones have landed.
- **One sender per `PTR` entry.** A tile never has two sending units feeding one
  `PTR` entry of one receiver, or one of a peripheral's ordered ports, at the
  same time.

## 15. `scratchpad`

The scratchpad is `SCRATCHPAD_BYTES` of SRAM, region 0x03. This chapter is its
contract: what the units and software rely on. How it meets the contract is
the implementation's.

- **Banks.** The scratchpad is built from banks. Each bank is contiguous, a power
  of two in size, aligned to its size, and at most `MAX_BANK_BYTES` = 512 KiB. So
  every aligned 512 KiB block is served by banks of its own, and software that
  keeps concurrent streams in different 512 KiB blocks gets a separate bank for
  each.
- **Width.** A bank does one access per cycle, a read or a write, of one 128-bit
  row. A write enables each 32-bit word separately, so a 32- or 64-bit write is
  one access.
- **Alignment.** Every access is naturally aligned: a 64-bit access at a multiple
  of 8 bytes, a 128-bit access at a multiple of 16. No access straddles a row.
  Asking for a misaligned access, or one past the end, is UB (1.3).
- **Clients.** The scratchpad's readers reach it through the read arbiter (14.1),
  over `scratchpad_read`, and the write engine (12.4) is its only writer, over
  `scratchpad_write` (3). *Open:* the accelerator's access.
- **The core** has no access of its own. It reads the scratchpad through the read
  unit, and writes it by sending itself packets.
- **A write wins its bank.** In a cycle when the write engine writes a bank, the
  bank does no read: a read that would reach it then is not taken, and waits
  (14.1). Software that keeps a transfer's reads and the writes arriving
  meanwhile in different banks never meets this.
- **Fixed read latency.** A read returns its row `READ_LATENCY` cycles after the
  read arbiter takes it, whatever its bank and whatever else the scratchpad
  does. `READ_LATENCY` is the implementation's, the same on every tile. So reads
  come back in the order they were taken, and each client's in its own order.
- **Writes in order.** The write engine's writes reach their banks in the order
  the director accepted them, one a cycle, and never wait.
- **Writes before later reads.** A write is in place before every read taken
  after its counter's decrement (12.3).
- **No coherence.** Within one transfer, no buffer is both read and written.
  Phases are ordered by their counters: a phase that waits for its counters
  before it arrives has its writes ordered before every read of the next phase.

## 16. `router`

Every tile has the same router (2.2).

### 16.1 Ports and links

| Port | In | Out |
| --- | --- | --- |
| east, west | from tile id ± 1 (mod `TILE_COUNT`) | to tile id ± 1 |
| south, north | from tile id ± `MESH_SIZE` (mod `TILE_COUNT`) | to tile id ± `MESH_SIZE` |
| local | the tile's injection point (14.3) | eject, to the traffic director (12.3) |
| attach, one per special tile attached | from that special tile | to that special tile |
| east, west, on a special tile | from the special tile id ± 1, on the line | to the special tile id ± 1 |
| south, on a special tile | its peripherals' DMAs (16.4) | none |

- **The ring.** East and west form one ring through every tile in id order: the
  east neighbour of a row's last tile is the next row's first. South and north
  are the columns, which wrap.
- **The loopback** is the path of a tile's packet to itself, from local in to
  local out.
- **Links.** Each link is a `link` interface (3) of three register stages,
  carrying one packet per cycle each way, with no flow control, no
  acknowledgement and no loss. A route block and its queue's write take one
  cycle, so an unloaded hop costs four cycles. Waiting in a queue adds to it.
- **Attach ports.** A compute tile has an attach port for each special tile whose
  attach tile it is (2.1). At 8 × 8 that is one each on tiles 0, 7 and 15; at 1 ×
  1, three on tile 0. A special tile's router has one attach port, joined to its
  attach tile's.
- **The line** joins the three special tiles, `TILE_COUNT` to `TILE_COUNT` + 2
  (64, 65 and 66 at 8 × 8), as the ring joins the compute tiles: east is the tile
  id + 1 and west the id − 1. **The master tile's east is joined to the memory
  tile's west, and the memory tile's east to the tracer tile's west** (owner). The
  master tile's west and the tracer tile's east are not connected, and no route
  uses them. Each of the line's links is a `link` (3), as the mesh's are: three
  register stages, one packet a cycle each way, **no flow control,** no
  acknowledgement and no loss. *Asked of the RTL* (RCR 1).
- **The south input** of a special tile's router is its peripherals' DMAs' (16.4);
  its north port and its south output are not connected.

### 16.2 Routes

**Unicast.** A unicast's route is fixed by its source and destination.
- **The offset.** The packet's offset is the pair (*a*, *b*) with
  `MESH_SIZE`·*a* + *b* ≡ dst − src (mod `TILE_COUNT`), |*a*| and |*b*| each at
  most ⌊`MESH_SIZE`/2⌋, and |*a*| + |*b*| least.
- **The route.** The packet makes |*b*| hops east (*b* > 0) or west, then |*a*|
  hops south (*a* > 0) or north.
- **Ties.** A column move of exactly half a side goes north when the packet
  first moves east (*b* > 0), and south otherwise (at 8 × 8, the all-to-all
  schedule's choice).

**Broadcast** reaches every compute tile exactly once, and no special tile. From
its source tile:
- one copy is ejected;
- copies spread ⌊`MESH_SIZE`/2⌋ hops east and ⌈`MESH_SIZE`/2⌉ − 1 hops west
  along the ring;
- each tile reached on the ring, the source included, sends copies south as far
  as the row ⌊`MESH_SIZE`/2⌋ below the source's, and north as far as the row
  ⌈`MESH_SIZE`/2⌉ − 1 above it, rows counted around the column. Where the ring
  crosses a row's end, its tiles are a row below or above the source's, and so
  send one hop fewer one way and one more the other, as v1.35 does;
- every tile a copy reaches ejects one.

**Special tiles.**
- **In the mesh,** a special tile is routed as its attach tile, as a source and as
  a destination. A packet for it leaves its attach tile by that attach port. A
  compute tile's packet for a special tile never takes the line.
- **Inside a special tile's router:**
  - a packet for the special tile ejects;
  - **a packet for another special tile takes the line,** by the ring's rule on the
    difference of the ids: east if dst > the tile's id, west if it is less, one
    hop a special tile, so the master tile's packet for the tracer tile passes
    through the memory tile's router (owner);
  - every other packet, from its local input or a DMA, leaves by its attach port.
    Its broadcast spreads from its attach tile as that tile's own.
- **A packet arriving on the line** is always for a special tile: it ejects there,
  or goes on along the line the way it came.
- **A packet for a reserved tile id** is dropped where it enters the NoC, with
  no error.

**Order.** Routes are fixed and queues never reorder, so packets from one source
that enter the NoC at one input of its router, for one destination, of one
kind, arrive in the order they entered. A unicast and a
broadcast between the same pair may take different paths.

### 16.3 Queues and arbiters

- **Queues.** Each output has a queue of `ROUTER_QUEUE_DEPTH` = 16 whole packets
  for each input that a route can bring to it.
- **Arbiters.** Each output's arbiter takes one packet a cycle, round-robin: it
  serves the first non-empty queue at or after its pointer, then moves the
  pointer one past it. Its queues are in their inputs' order, local, west, east,
  north, south, then the attach ports (master, memory, tracer), and the pointer
  is 0 at the reset.
- **The loopback queue** is the eject output's queue from local in. It holds
  `LOOPBACK_QUEUE_DEPTH` packets, at least `TOKEN_COUNT` (14.2), so it cannot
  overflow. The eject arbiter serves it only in a cycle when every other eject
  queue is empty, and without moving its pointer: a tile's own packets never
  hold up the packets reaching it. Its packets leave the eject marked `loopback`
  (3), so the traffic director can return their tokens (14.2).
- **Overflow.** A packet pushed into a full queue (7.4) sets
  `ERRORS.router_overflow`. A correct program never overflows one: the NoC has no
  flow control, so many-to-one traffic is the program's to pace.
- **The line is the mesh's regime** (owner): no flow control, the program paces
  what it sends along it, and a full queue is `router_overflow`, on the router it
  is in. In v1.37 the master tile's loads into the tracer tile ride it, from one
  sending unit at a time. Flow control on the line belongs with the regime that
  needs it, the file system's traffic, and is designed with it *(item 182)*.
- **An attach output** takes one packet a cycle, with no back-pressure.

### 16.4 A special tile's peripheral DMAs

Each sending peripheral's DMA enters the special tile's router on its **south
input**, the +8 port, as a port of its own (owner; *asked of the RTL*, RCR 3:
v1.36's RTL has it on the west input of the memory and tracer tiles), and its
packets have the special tile as src. A special tile has one sending peripheral
in v1.37: Ingress/Egress on the master tile (17.6), the RW engine on the memory
tile (17.7) and the Tracer on the tracer tile (17.9).
- **Local flow control.** A DMA sends a packet only while the queue that packet
  enters has room: the `ready` of its `packet` interface (3). This is the only
  flow control in the router.
- **Its packets to its own tile** reach the eject like any arrival, and take no
  read token.

v1.35's src for a DMA, the tile's id + 8 (item 118), is not taken: its
routing by dst, and its ejection of a packet for the tile itself, are this rule.

## 17. Peripherals

A **peripheral** works beside the core on a job of its own. A tile has up to
three, peripherals 1, 2 and 3, which its kind attaches (2.1). Every peripheral
connects the same way, so the tile's logic is one design whatever peripherals
it has.

**A peripheral's section** defines its type and version; what `START` does and
when its work ends; its own `STATUS`, `ERRORS` and `EVENTS` bits, each event's
kind and depth; its words, blocks 2–7; its region's map, the sizes each part
takes and its ownership; its DMA, if it has one; its ports and pins (3, 18); its
reset; and its parameters (19). Ingress/Egress's is 17.6, the RW engine's 17.7,
the replayer's 17.8 and the Tracer's 17.9.

### 17.1 Window, presence and type

- **Peripheral *n*** (1–3) has region 0x03 + *n* (5.1) and window 12 + *n*, at
  0xFFFF_F800 + 0x200·*n* (5.2).
- **Presence.** `PARAMS.peripherals` in the tile window says which are present.
- **An absent peripheral.** Any word of an absent peripheral's window is an
  access fault, and a `WRITE` to its region is a bad packet. Its bits in the
  tile's `EVENTS` and `ERRORS` read 0.
- **Its window** has the control block in block 0 and its DMA in block 1, as
  every register window does (5.3). Blocks 2–7 are its own, as its section
  defines. A peripheral without a DMA has no DMA block: every word of its block
  1 reads 0, so a handler reads the same words on every peripheral, and a store
  there is UB (1.3). Its `STATUS.dma_busy` is 0.

| Block, word | Register | Access | Fields |
| --- | --- | --- | --- |
| 0, 0 | `PARAMS` | R | [7:0] `type`; [15:8] `version`; [31:16] the peripheral's constants |
| 0, 1 | `STATUS` | R | [0] `busy`, [1] `dma_busy`; above, the peripheral's |
| 0, 2 | `ERRORS` | R | the peripheral's errors, sticky until the reset, but for Ingress/Egress's `tile_error`, which is live (17.6) |
| 0, 3 | `EVENTS` | R | [0] `done`, [1] `dma_done`; above, the peripheral's; a load consumes (7.2) |
| 0, 4 | `EVENTS_PEEK` | R | the same, consuming nothing |
| 0, 5 | `EVENTS_MASK` | R/W | the events that count toward the tile's bit and that a load consumes; 0 at the reset |
| 0, 6 | `START` | W | starts the peripheral's work (17.2) |
| 1, 0 | `DMA_HDR` | R/W | [7:0] the header block, its size the DMA's; [14:8] `dst`; [26:15] `count`, 128-bit packets (0–4095); [28:27] `queue`, the output queue (17.4) |
| 1, 1 | `DMA_DEST_ADDR` | R/W | [31:0] the DMA's destination address |
| 1, 2 | `DMA_START` | W | starts the DMA's transfer (17.4) |

**Types**, `PARAMS.type`: 0 none, 1 Ingress/Egress (17.6), 2 the RW engine
(17.7; v1.35's Updater/Binder/RW engine), 3 the Tracer (17.9), 4 the replayer
(17.8); the others are reserved.

### 17.2 Start, events and errors

- **Start.** `START` starts the peripheral's work, as its own section defines.
  - A `START` while the peripheral is busy is UB (1.3): there is no resume.
  - **`STATUS.busy`** is 1 from the store's effect (6.4) until the work ends,
    and is the peripheral's **ownership state**. While it is 1, the
    peripheral owns what its section says.
  - The end of the work posts **`EVENTS.done`**.
- **Events** follow the one model (7.2).
  - The standard ones are `done` and `dma_done`, each counted with depth 1. The
    rest are the peripheral's, each with its kind and depth stated.
  - The tile's `EVENTS.peripheral_n` is live: 1 while peripheral *n*'s
    `EVENTS & EVENTS_MASK` is non-zero.
  - A peripheral's events are consumed only by a load of its own `EVENTS`: no
    store clears one, so `DMA_START` leaves `dma_done` as it is, unlike the
    tile's (7.2).
  - The tile's bit may lag a consuming load by a cycle, so software re-checks
    after every wake.
- **Errors.** Each of the peripheral's errors is a program error or a
  hardware-fault detector (7.4). The tile's `ERRORS.peripheral_n` is live: 1
  while its `ERRORS` is non-zero. So a peripheral's error raises the NMI and the
  tile's error line.

### 17.3 The region and the write port

`WRITE` packets reach a peripheral's region from any source: the tile itself,
another tile, or a special tile. The traffic director hands each to the
peripheral's **write port**, a `noc_write` interface (3).
- **Landing.** The port takes one packet a cycle, with no queue on the path, and
  the write lands within `PERIPHERAL_LATENCY` of the port, so it is in place by
  the decrement (12.3).
- **Its rules.** The peripheral's section gives the writes its region takes: its
  map, the sizes each part takes, and when each part may be written, its
  ownership. Any other write is UB (1.3). A drop the peripheral defines and
  checks, such as a queue's overrun, sets one of its own errors, and the write
  is still counted.
- **Order.** Where a peripheral places a port's writes in arrival order, one
  source at a time feeds that port (14.4).

### 17.4 The DMA

A peripheral may have a **DMA**, its output engine. Only a special tile's
peripherals can, since only a special tile's router has a free input for it, its south (16.4).

- **The path.** The peripheral → its output queues (up to 4), of 128-bit
  entries → its DMA → its own router input, over a `packet` interface (3).
- **A transfer.** `DMA_START` starts a transfer of *n* 128-bit packets
  (`DMA_HDR.count`, up to 4,095, 64 KiB less 16 bytes) from the output queue
  `DMA_HDR.queue` to `DMA_HDR.dst`. Every packet is stamped with `DMA_HDR`'s
  counter and mode. A queue the peripheral does not have is UB (1.3), and so is
  a transfer of more entries than its queue will hold: the DMA would wait for
  ever.
  - **Packets.** Every packet is 128 bits, one queue entry, in queue order, so
    a peripheral pads what it sends to whole entries, and a smaller value goes
    in an entry's low bits, the rest 0.
  - **Addresses.** A `WRITE` packet carries `DMA_DEST_ADDR`, 16-byte aligned,
    which then advances by 16. A misaligned one is UB (4.2). A packet of any
    other mode carries 0 (4.1) and leaves `DMA_DEST_ADDR` as it is.
  - **Pace.** The DMA sends at most one packet a cycle. It waits while its queue is
    empty or its router input has no room.
- **One transfer at a time.** **`STATUS.dma_busy`** is 1 from the store's
  effect (6.4) until the last packet has left. When it falls, `EVENTS.dma_done`
  is posted.
  - A `DMA_START`, or a store to `DMA_HDR` or `DMA_DEST_ADDR`, during a
    transfer is UB (1.3).
  - A count of 0 sends nothing: `STATUS.dma_busy` is 1 for one cycle, and
    `EVENTS.dma_done` is posted at its end.
- **No checks.** The DMA checks nothing, like the write unit.

This is v1.35's DMA (v1.35's 4.17.9): its count in 128-bit words. Ingress/Egress has
no DMA (17.6); the RW engine's data and replies are whole 128-bit packets
(17.7), and the Tracer's streams are padded to whole entries (17.9.6).

### 17.5 What every peripheral must do

- Land its writes within `PERIPHERAL_LATENCY` of its write port, with no queue
  on the path (17.3).
- Implement the control block, and the DMA block if it has a DMA, with their
  bits and access rules.
- Read idle when idle: `STATUS.busy` and `STATUS.dma_busy` at 0, and no events
  or errors from a program that never runs it.
- Reset with its tile, and stop for nothing else: a trap leaves it running,
  unserviced, until the reset.
- Have no read side effects, except a consuming load of `EVENTS` and the pops its
  section defines.
- Send nothing unsolicited: its DMA sends only a transfer the core started.
- Raise errors only for errors.

### 17.6 Ingress/Egress, the master tile's peripheral

Ingress/Egress is the master tile's peripheral 1, type 1, version 0. It holds
the chip's control lines and its external IO: every other tile's error line,
the barrier's master side, the watchdog and the toast pin, `LED`, the UART and
the host interface. It does no work of its own and sends nothing.
- **No work.** A `START` is UB (1.3), so `STATUS.busy` and `EVENTS.done` stay 0.
- **No DMA.** Its block 1 reads 0 (17.1), and `EVENTS.dma_done` stays 0.
- **No writes.** Its region takes none: a `WRITE` to the master tile's region
  0x04 is UB (17.3).
- **`PARAMS`:** type 1, version 0, and no constants, [31:16] 0.

Its words, at offsets from its window's base, 0xFFFF_FA00:

| Word | Offset | Register | Access | Fields |
| --- | --- | --- | --- | --- |
| 32–34 | 0x080–0x088 | `ERROR_LINES_0`–`ERROR_LINES_2` | R | bit *b* of word *w*: the error line of tile 32*w* + *b* |
| 35, 36 | 0x08C, 0x090 | `ARRIVALS_0`, `ARRIVALS_1` | R | bit *b* of word *w*: compute tile 32*w* + *b* has arrived |
| 37, 38 | 0x094, 0x098 | `ARRIVE_EXPECT_0`, `ARRIVE_EXPECT_1` | R/W | as `ARRIVALS`: the compute tiles the master tile waits for |
| 39 | 0x09C | `RELEASE` | W | releases the barrier |
| 40 | 0x0A0 | `WATCHDOG` | R/W | [31:0] a store sets the limit, and a load returns the count |
| 41 | 0x0A4 | `LED` | R/W | [0] the `led` pin |
| 42 | 0x0A8 | `UART_TX` | W | [7:0] a byte to send |
| 43 | 0x0AC | `UART_RX` | R | [7:0] the oldest byte received; [8] `valid`. Pops it |
| 44 | 0x0B0 | `UART_DIVISOR` | R/W | [15:0] the cycles per bit |
| 45 | 0x0B4 | `UART_STATUS` | R/W | [4:0] the TX FIFO's bytes; [12:8] the RX FIFO's; [16] `rx_overrun`; [17] `framing_error` |
| 64–66 | 0x100–0x108 | `STREAM_OUT_W0`–`STREAM_OUT_W2` | W | words 0–2 of the next beat out |
| 67 | 0x10C | `STREAM_SEND` | W | the beat's top word; pushes the beat, TLAST 0 |
| 68 | 0x110 | `STREAM_SEND_LAST` | W | the beat's top word; pushes the beat, TLAST 1 |
| 69–71 | 0x114–0x11C | `STREAM_IN_W0`–`STREAM_IN_W2` | R | words 0–2 of the oldest beat in |
| 72 | 0x120 | `STREAM_RECEIVE` | R | its top word. Pops it |

Every other word of blocks 2–7 is unmapped (5.3).

**Its bits.**
- **`STATUS`:** [2] `stream_last`, the TLAST of the oldest beat in, 0 while
  there is none.
- **`ERRORS`:** [0] `tile_error`, live: 1 while any bit of `ERROR_LINES` is 1.
  It is the one live bit of a peripheral's `ERRORS`. Its kind is that of the
  tile's error behind it (7.4).
- **`EVENTS`:**

  | Bit | Event | Kind | Set by |
  | --- | --- | --- | --- |
  | 0, 1 | `done`, `dma_done` | counted, 1 | nothing: it has no work and no DMA |
  | 2 | `all_arrived` | live | (`ARRIVALS` & `ARRIVE_EXPECT`) = `ARRIVE_EXPECT` ≠ 0 |
  | 3 | `watchdog` | counted, 1 | the watchdog's expiry |
  | 4 | `uart_rx` | live | a byte in the UART's RX FIFO |
  | 5 | `uart_tx` | live | room in the UART's TX FIFO |
  | 6 | `stream_in` | live | a beat in the host interface's receive FIFO |
  | 7 | `stream_out` | live | room in the host interface's transmit FIFO |

  `uart_tx` and `stream_out` read 1 while it is idle, and wake nothing while
  `EVENTS_MASK` keeps its reset value, 0 (17.5).

**The error lines.**
- Bit *i* of `ERROR_LINES` is tile *i*'s error line (18): 1 while that tile's
  `ERRORS` is non-zero, `ERROR_LATENCY` behind it. At 8 × 8, `ERROR_LINES_2`'s
  [1] is the memory tile's and [2] the tracer tile's.
- The master tile's own bit, `TILE_COUNT`, and the bits past `TILE_COUNT` + 2
  read 0. The master tile's own line drives the toast pin instead.
- Through `tile_error`, any other tile's error raises the master tile's
  `ERRORS.peripheral_1` (17.2), and with it the master's NMI (6.2) and the toast
  pin.

**The barrier, the master tile's side** (7.3).
- **Arrivals.** Bit *i* of `ARRIVALS` is set at the end of the cycle in which
  compute tile *i*'s `BARRIER_ARRIVE` pulse reaches Ingress/Egress,
  `ARRIVE_LATENCY` after the tile sent it (18). A `RELEASE` and the reset clear
  it. The bits past `TILE_COUNT` − 1 read 0.
- **`ARRIVE_EXPECT`** names the compute tiles the master tile waits for, its
  bits laid out as `ARRIVALS`'. The bits past `TILE_COUNT` − 1 are reserved
  (5.4).
- **`all_arrived`** is live, so it is current whatever order the master writes
  `ARRIVE_EXPECT`'s two words in.
- **`RELEASE`**, a store with bit 0 set, takes effect at the end of *t* + 1 for
  a store issued in *t* (6.4). Then it:
  - clears `ARRIVALS`, but keeps an arrival that reaches Ingress/Egress in that
    cycle;
  - restarts the watchdog (below);
  - releases: its pulse is on every compute tile's `BARRIER_RELEASE`
    `RELEASE_LATENCY` cycles later, in the same cycle on every tile (18).

**The watchdog and the toast pin.**
- **`WATCHDOG`** counts the chip's cycles down, from a **limit** the program
  sets. The limit and the count are 0 at the reset, and a count of 0 is off.
  - A store of *N* sets the limit and the count to *N*: *N* > 0 starts it, and 0
    stops it.
  - A `RELEASE` sets the count to the limit.
  - A load returns the count.
  - At the end of every cycle in which the count is non-zero and nothing sets it,
    it decreases by 1. When it reaches 0, the watchdog **expires**: it posts
    `EVENTS.watchdog` and sets the toast pin. The count then stays at 0 until a
    store or a `RELEASE` sets it. So a store of *N* > 0 issued in cycle *t*,
    with nothing after it, expires at the end of *t* + 1 + *N*.
- **The toast pin** (18) says the chip has failed. It is a flip-flop, set at
  the end of a cycle in which the master tile's own error line is high or the
  watchdog expires, and cleared only by the reset. The chip never resets
  itself: the outside world does (18).

**`LED`** drives the `led` pin (18) from a flip-flop, its bit 0.

**The UART** is the chip's serial port, on the pins `uart_tx` and `uart_rx`
(18).
- **Frames.** 8 data bits, the least significant first, no parity and 1 stop
  bit; the line is 1 while idle. Each bit lasts `UART_DIVISOR` cycles. A new
  divisor takes effect at each direction's next idle point, so no frame is cut.
  The divisor counts the chip's clock (6.1), where v1.35 counts a reference
  oscillator's, and resets to `UART_RESET_DIVISOR`, 115,200 baud. A divisor of 0
  is UB (1.3).
- **FIFOs.** A TX FIFO and an RX FIFO of `UART_FIFO_DEPTH` = 16 bytes each, as
  v1.35 proposes.
- **Sending.** A store to `UART_TX` pushes its byte into the TX FIFO, and
  stalls while the FIFO is full (the `reg` interface's `ready`, 3). The
  transmitter sends the FIFO's bytes in order.
- **Receiving.** `uart_rx` is asynchronous to the chip's clock, and the receiver
  synchronises it. A frame starts at a falling edge of the line, and each of its
  bits is sampled at its middle; the start bit is not checked, so a glitch
  starts a frame. A frame whose stop bit is 0 is dropped and sets
  `framing_error`, and one that ends with the RX FIFO full is dropped and sets
  `rx_overrun`.
- **`UART_RX`** returns the oldest byte with `valid` 1, and pops it. With the
  RX FIFO empty it returns 0 and pops nothing.
- **`UART_STATUS`** has no read side effect. A store clears each of
  `rx_overrun` and `framing_error` that it writes 1, and ignores its other bits.
  An error raised in the store's cycle stays set.
- **Line errors are status:** they never reach `ERRORS`, so noise on the line
  never raises the toast pin.

**The host interface** is the chip's link to its host: two AXI4-Stream ports,
`lvds_in` from the LVDS IO block and `lvds_out` to it (3), each behind a FIFO of
`STREAM_DEPTH` beats of TDATA and TLAST.
- **A beat** is TDATA, `STREAM_BYTES` bytes, as `STREAM_BEAT_WORDS` =
  ⌈`STREAM_BYTES` / 4⌉ words: word *w* is its bits [32*w* + 31 : 32*w*], and its
  **top word** is word `STREAM_BEAT_WORDS` − 1. A word's bits past TDATA, and the
  words past the beat, are ignored on a store and read 0.
- **No meaning.** The ports move beats and give them none. *Open:* the master
  tile's boot over LVDS, and every meaning of the beats and of TLAST.
- **Sending**, as the write unit sends (9): a program stores the words below the
  beat's top in `STREAM_OUT_W0`–`W2`, then its top word in `STREAM_SEND` or
  `STREAM_SEND_LAST`, which pushes the beat into the transmit FIFO with TLAST 0
  or 1 and clears `STREAM_OUT_W0`–`W2`. A beat of one word is one store. A send
  stalls while the transmit FIFO is full.
- **Receiving**, as the request unit receives (11.1): `STREAM_IN_W0`–`W2` read
  the words below the oldest beat's top, and `STREAM_RECEIVE` its top word,
  which pops it. With the receive FIFO empty they read 0, and
  `STREAM_RECEIVE` pops nothing.
- **The ports.** `lvds_in_tready` is 1 while the receive FIFO is not full, from
  the cycle after the reset, and
  `lvds_out_tvalid` while the transmit FIFO holds a beat, whose TDATA and TLAST
  it offers until its handshake.

**Reset.** The reset zeroes `ARRIVALS`, `ARRIVE_EXPECT`, the watchdog's limit
and count, `LED`, the toast pin, the UART's line errors and
`STREAM_OUT_W0`–`W2`; empties the four FIFOs, so `lvds_in_tready` and
`lvds_out_tvalid` are 0; sets `UART_DIVISOR` to `UART_RESET_DIVISOR`; and leaves
`uart_tx` idle, at 1. `ERROR_LINES` follow the tiles' `ERRORS`, which the reset
zeroes too (18).

### 17.7 The RW engine, the memory tile's peripheral

The RW engine is the memory tile's peripheral 1, type 2, version 0: the chip's
one path to LPDDR, over the `lpddr` port (3). Its work, one job at a time, is a
**read**, from LPDDR into its output queue 0, which its DMA sends, or a
**write**, from its **write buffer**, its region, to LPDDR. It knows nothing of
requests: the memory tile's core receives them (11.5) and starts the work.
v1.35's Updater and Binder, and its file system (item 119), are not part of
it.
- **`PARAMS`:** type 2, version 0, and no constants, [31:16] 0.
- **A beat** is 16 bytes: an `lpddr` data beat, a row of the write buffer and a
  128-bit packet's payload. An LPDDR **beat address** is a byte address over 16.

Its words, at offsets from its window's base, 0xFFFF_FA00:

| Word | Offset | Register | Access | Fields |
| --- | --- | --- | --- | --- |
| 32 | 0x080 | `RW_CONFIG` | R/W | [0] `kind`: 0 a read, 1 a write; [10:1] `beats`: the beats to move, 0–1023 |
| 33 | 0x084 | `RW_LPDDR_ADDR` | R/W | [`LPDDR_ADDRESS_BITS` − 5 : 0] the first beat's LPDDR beat address |
| 34 | 0x088 | `RW_BUFFER_ADDR` | R/W | [log₂ `RW_BUFFER_BYTES` − 5 : 0] a write's first beat in the write buffer: its offset over 16 |
| 35 | 0x08C | `RW_REPLY_W0` | W | [31:0] the next reply's bits [31:0] |
| 36 | 0x090 | `RW_REPLY_SEND` | W | [31:0] the reply's bits [63:32]; pushes the reply into output queue 1 |

Every other word of blocks 2–7 is unmapped (5.3).

**Its bits.** `STATUS` and `EVENTS` have the standard bits alone (17.1), and
`ERRORS` one: [0] `lpddr_error`, below.

**The work.** `START` starts the job `RW_CONFIG` describes, at the addresses its
registers hold (17.2). The job takes `RW_CONFIG`, `RW_LPDDR_ADDR` and
`RW_BUFFER_ADDR` as it starts: a store to them during the job changes nothing
of it.
- `STATUS.busy` is 1 until the job ends, and then `EVENTS.done` is posted. A
  job of 0 beats moves nothing: `STATUS.busy` is 1 for the cycle after its
  start takes effect alone, and `EVENTS.done` is posted at its end.
- A beat address advances by plain addition: a job past LPDDR's end, or past
  the write buffer's, is UB (1.3).

**A read** (`kind` 0) reads `beats` beats of LPDDR, from `RW_LPDDR_ADDR` up, in
ascending order, into output queue 0, of `RW_QUEUE_BEATS` beats.
- **The end.** The read ends when its last beat has entered the queue. It
  raises a burst only while the queue has room for every beat in flight, so it
  ends only as the DMA drains the queue: a program starts the DMA's transfer
  without waiting for `EVENTS.done`.
- **Its DMA transfer** (17.4) sends the beats: `DMA_HDR.queue` 0, mode `WRITE`
  and `count` `beats`, a beat a packet, at `DMA_DEST_ADDR` and up by 16. The transfers from queue 0 take exactly the
  beats the reads put there, and any other count is UB (1.3). The `DMA_START`
  may come before or after the `START`.
- **As an answer** (11.4), the program names the requester in `DMA_HDR.dst`,
  the data counter as its counter and the destination in `DMA_DEST_ADDR`. One
  read's packets leave in ascending address order, and reads in the order they
  started.

**The reply** (11.4) is one 128-bit packet, its value in its low 64 bits and
its bits [127:64] 0. A store to `RW_REPLY_SEND` pushes the entry
{0, `RW_REPLY_SEND`, `RW_REPLY_W0`} into **output queue 1**, which holds one
entry, and clears `RW_REPLY_W0`, as at the reset; a push to a full queue is UB
(1.3). A 32-bit value is `RW_REPLY_W0` with a `RW_REPLY_SEND` of 0. A transfer
from queue 1 of `count` 1 sends the reply, to the reply address on the reply
counter. The DMA runs one transfer at a time (17.4)
on one router input (16.4), so a reply sent after a read's data, to the same
requester, arrives after it (16.2).

**A write** (`kind` 1) writes `beats` beats of the write buffer, from beat
`RW_BUFFER_ADDR` up, to LPDDR, from `RW_LPDDR_ADDR` up, in ascending order.
- **The end.** The write ends when LPDDR has answered its last write, so a read
  started after it reads what it wrote.
- **Ownership.** While the write runs, a `WRITE` packet into a beat it reads is
  UB (1.3).

**The write buffer** is the RW engine's region, 0x04 on the memory tile:
`RW_BUFFER_BYTES` of SRAM, at offsets 0 to `RW_BUFFER_BYTES` − 1, in rows of 16
bytes.
- **Writes.** It takes `WRITE` packets of 32, 64 and 128 bits from any source,
  the memory tile itself included, each landing within `PERIPHERAL_LATENCY`
  (17.3). A write past its end is UB (1.3).
- **Counted at the memory tile.** Each write is counted on the memory tile's
  counter its counter field names (12.2, 12.3), so the memory tile's core knows
  by that counter when a part of the buffer is in place, and then writes it out.
- **Tag windows.** v1.35's tag windows (v1.35's 6.7) are parts of the buffer,
  which the memory tile's software allots, one to each `WRITE` request (11.5).
  The writer sends the part's beats, counted on the memory tile's counter
  the reply names. When that counter drains, the memory tile writes the part to
  LPDDR, and on `EVENTS.done` the part is free again: v1.35's tag, returned.
  Writers into the buffer are one at a time, or paced, since nothing paces the
  memory tile's attach link but the program (16.3).

**The `lpddr` port** (3).
- **Bursts.** A job moves its beats by INCR bursts of 16-byte beats, none
  crossing a 4 KiB boundary; their lengths are the implementation's. A write's
  beats enable every byte.
- **LPDDR errors.** A read beat or a write response that is not OKAY sets
  `ERRORS.lpddr_error`, and its beat or write completes all the same. It is a
  program error for an address the device does not have, and a hardware fault
  otherwise (7.4).

**Its DMA** enters the memory tile's router on its south input (16.4),
so its packets have the memory tile as src.

**Reset.** The reset ends any job, empties both output queues and the
`lpddr` port's channels, and zeroes `RW_REPLY_W0`. The write buffer, a peripheral's memory, keeps its
contents (18). `RW_CONFIG`, `RW_LPDDR_ADDR` and `RW_BUFFER_ADDR` are undefined
until written. *Open:* the soft reset, which v1.35 keeps away from the LPDDR
controller (18).

### 17.8 The replayer, the compute tile's peripheral

The replayer is every compute tile's peripheral 1, type 4, version 0: it regenerates the tile's share of the
proof's execution trace, one row at a time, from the tile's record and three streams, and sends each row out of
its `replay_row` port (3). Its specification, 17.8 and 17.8.1 to 17.8.14 with what is kept from v1.35 after them,
is `spec/jolt/replayer.md`; what each row computes is `spec/jolt/replayer-opcodes.md`.

### 17.9 The Tracer, the tracer tile's peripheral

The Tracer is the tracer tile's peripheral 1, type 3, version 0: it executes the guest, an RV64IMAC program, and
writes each window's record and streams, which its DMA sends to the compute tiles' replayers; it reads and writes
the guest's memory over its `fill` port (3). Its specification, 17.9 and 17.9.1 to 17.9.12 with what is kept from
v1.35 after them, is `spec/jolt/tracer.md`.

### 17.11 The memory system, the memory tile's peripheral 1

The memory system is the memory tile's peripheral 1, type 6, version 0: the cache (23.6), the AXI arbiter (23.11)
and the chip's `lpddr` port (23.12). **It is a peripheral so that the cache's error reaches the tile's error line,**
and so the toast pin, as every peripheral's does (owner); nothing in it is started, configured or fed by a
program. Its hardware is chapter 23's.

- **`PARAMS`:** type 6, version 0, and no constants, [31:16] 0.
- **No work.** A `START` is UB (1.3), so `STATUS.busy` and `EVENTS.done` stay 0.
- **No DMA.** Its block 1 reads 0 (17.1), and `EVENTS.dma_done` stays 0.
- **No writes.** Its region takes none: a `WRITE` to the memory tile's region 0x04 is UB (17.3).
- **Its own words:** none. Every word of blocks 2–7 is unmapped (5.3).
- **`STATUS` and `EVENTS`** have the standard bits alone, which read 0.
- **`ERRORS`**, sticky until the reset:

  | Bit | Error | Kind | Set by |
  | --- | --- | --- | --- |
  | 0 | `lpddr_error` | H (P for an address the device does not have) | an LPDDR error response the cache had no client to tell of: a dirty line's write-back, or a line fetched for `fill`'s read (23.6.9) |

**Its ports** (3): `fill`, from the tracer tile's Tracer; the look-up ports, 1 from the master tile's Updater
(23.6.2); the arbiter's client ports, 1 from the Updater and 2 from the memory tile's RW engine (23.10), client 0
being the cache's inside it; and `lpddr`, which leaves the chip for the LPDDR controller.

**Reset.** The reset empties the cache (23.6.10) and the arbiter (23.11) and clears `ERRORS`.

## 18. Nets and reset

| Net | Tiles | Meaning |
| --- | --- | --- |
| `BARRIER_ARRIVE` | out, compute tiles; in, the master tile | one line per compute tile to the master tile's Ingress/Egress, a one-cycle pulse, at equal latency `ARRIVE_LATENCY` (7.3, 17.6) |
| `BARRIER_RELEASE` | out, the master tile; in, compute tiles | one line from the master tile's Ingress/Egress, reaching every compute tile in the same cycle, `RELEASE_LATENCY` after its `RELEASE` store takes effect (7.3, 17.6) |
| error line | out, every tile; in, the master tile | high while the tile's `ERRORS` is non-zero, with no software involved. Every other tile's reaches the master tile's Ingress/Egress `ERROR_LATENCY` later (`ERROR_LINES`), and the master tile's own its toast pin (17.6) |
| reset | in, every tile | the chip's reset (*Open:* hard and soft, with the chip) |

**Pins.** Besides its clock, its reset and its peripherals' ports (3), the chip
has the master tile's pins:

| Pin | Direction | Meaning |
| --- | --- | --- |
| `toast` | out | the chip has failed: latched until the reset (17.6) |
| `led` | out | `LED`'s bit 0 (17.6) |
| `uart_tx`, `uart_rx` | out, in | the UART's line (17.6) |

**The reset.** Every tile is reset as at power-on, except its memories.
- **The core** starts at the reset PC, in the boot ROM (6.5).
- **Emptied, with every credit returned:** every queue, pipeline stage and
  delay line in the tile and the request tree.
- **Zeroed:**
  - `ERRORS`, the counters, `EVENTS.counter_n_done` and
    `EVENTS.all_counters_done`;
  - `STATUS.arrived`;
  - the other counted events;
  - `STATUS.dma_busy`;
  - `WRITE_W0`–`W2` and `REQUEST_OUT_W0`–`W2`;
  - the round-robin pointers.
- **Idle:** `counter_n_done` and `all_counters_done` disarmed; the arrival gate
  inactive, so the wake is `(EVENTS & EVENTS_MASK)` ≠ 0 until an `ARRIVE`; all
  `TOKEN_COUNT` tokens free; and every unit and peripheral idle. Each
  peripheral's own state resets as its section says (17.6–17.9).
- **Set:** the tile's `EVENTS_MASK` resets to `release` and `all_counters_done`,
  and each peripheral's to 0.
- **Undefined until written:** `DMA_CONFIG`, `RECEIVE_CONFIG`, `GATHER_CONFIG`,
  `WRITE_HDR`, `WRITE_DEST_ADDR`, `READ_HDR`, `READ_DEST_ADDR`, and each
  window's `DMA_HDR` and `DMA_DEST_ADDR`.
- **Kept:** IMEM, DMEM, the scratchpad, the tables and the peripherals' memories
  keep their contents. They are undefined after power-on. A store the core had
  raised when the reset began may still be written to DMEM: the core does not
  undo an accepted store (the core specification).

## 19. Parameters

| Parameter | Value | Meaning | Section |
| --- | --- | --- | --- |
| `MESH_SIZE` | 1–8; 8 by default | the mesh's side | 2.1 |
| `TILE_COUNT` | `MESH_SIZE`² | the compute tiles | 2.1 |
| `TILE_ID_BITS` | ⌈log₂(`TILE_COUNT` + 5)⌉; 7 at 8 × 8 | a tile id's width | 4.1 |
| `IMEM_BYTES`, `DMEM_BYTES` | 32 KiB each by default; `DMEM_BYTES` a multiple of 16 of at least 32, its reserved 16 bytes and a row | IMEM and DMEM | 6.3 |
| `BOOT_ROM_BYTES` | 256 | the boot ROM | 6.3 |
| `SCRATCHPAD_BYTES` | 4 MiB by default; a power of two from 512 B to 8 MiB | the scratchpad | 15 |
| `MAX_BANK_BYTES` | 512 KiB | the largest a scratchpad bank may be; banks are powers of two up to it | 15 |
| `WORD_ADDRESS_BITS` | 21 | a unit's scratchpad word address | 5.4 |
| `WINDOW_LOAD_LATENCY` | 2 | a core load from a window | 6.4 |
| `TABLE_LOAD_LATENCY` | 1 | a core load from a table | 6.4 |
| `TABLE_LATENCY` | 1 by default | a unit's request or return to its tables, each way | 8, 12.3 |
| `READ_AHEAD` | 2·`TABLE_LATENCY` + 2 | the places of each read-ahead queue | 8.5 |
| `READ_LATENCY` | the implementation's | a scratchpad read, from the read arbiter to its row | 15 |
| `LAND_LATENCY` | 8 | from the director's acceptance to the decrement | 12.3 |
| `WRITE_PORT_LATENCY` | 2 | from the director's acceptance to its `noc_write` | 12.3 |
| `PERIPHERAL_LATENCY` | at most `LAND_LATENCY` − `WRITE_PORT_LATENCY` | a write's landing in a peripheral, from its write port | 17.3 |
| `COUNTER_BITS` | 20 | a counter | 12.2 |
| `HIST_BITS` | 17 | a `HIST` count | 12.5 |
| `TOKEN_COUNT` | 64 | the read tokens | 14.2 |
| `GATHER_QUEUE_DEPTH` | `TOKEN_COUNT` | the gather queue | 13 |
| `LOOPBACK_QUEUE_DEPTH` | at least `TOKEN_COUNT` | the loopback queue | 16.3 |
| `ROUTER_QUEUE_DEPTH` | 16 | each of the router's other queues | 16.3 |
| `READ_QUEUE_DEPTH` | 64 to 2¹⁶ − 1 | the read unit's queue | 10 |
| `REQUEST_QUEUE_DEPTH` | 4 | the request unit's queue | 11.1 |
| `RECEIVE_QUEUE_DEPTH` | per target, a power of two, at least `TILE_COUNT` × its per-tile limit + 64 | a target's receive queue | 11.3 |
| `ARRIVE_LATENCY`, `RELEASE_LATENCY`, `ERROR_LATENCY` | the chip's | the global nets | 18 |
| `STREAM_BYTES` | 1–16; 4 by default | a host-interface beat's TDATA | 17.6 |
| `STREAM_DEPTH` | 4 | each host-interface FIFO's beats | 17.6 |
| `UART_FIFO_DEPTH` | 16 | each UART FIFO's bytes | 17.6 |
| `UART_RESET_DIVISOR` | the chip's: its clock's frequency over 115,200, rounded | `UART_DIVISOR`'s reset value | 17.6 |
| `LPDDR_ADDRESS_BITS` | 24–36; 32 by default | an LPDDR byte address on `lpddr` | 17.7 |
| `RW_QUEUE_BEATS` | at least 4; 16 by default | the RW engine's output queue 0 | 17.7 |
| `RW_BUFFER_BYTES` | a power of two from 4 KiB to 16 MiB; 16 KiB by default | the RW engine's write buffer | 17.7 |
| `REPLAY_QUEUE_ENTRIES` | 256 | each of the replayer's input queues, in 128-bit entries; a credit is half of it | 17.8.8 |
| `REPLAY_OUTPUT_ROWS` | 32 | the replayer's output queue, in rows | 17.8.11 |
| `TRACER_WINDOW_ROWS` | 65,536 | each of the Tracer's FIFOs, in entries, and a run's largest `rows` | 17.9.6 |
| `MAP_PROBE_LINES` | 3 | the most lines a probe of the jump-target map reads | 17.9.4 |

### 19.1 The simulator's parameters

The table above is the machine's. This section maps v1.35's parameter summary (v1.35's section 10) onto it, row by
row, and lists what the simulator sets beside it. Three kinds of value appear:
- **contract:** the value is the table's, and the simulator has no choice;
- **the implementation's:** the contract leaves it to the implementation, and the simulator takes the RTL's
  (Snarky One's) value, marked `[meas: RTL]`, which Appendix A also lists;
- **the simulator's:** a value only the model has. v1.36's simulator reproduces the RTL's cycle counts
  (chapter 21; item 163).

**Mapped onto chapter 19.**

| v1.35 parameter | v1.35 value | v1.36 | v1.36 value |
|---|---|---|---|
| link word | 256 bits, stored in full | the packet (4.1) | 256 bits in format; only the defined bits are carried and stored, 168 + 2·`TILE_ID_BITS` (182 at 8 × 8) |
| mesh | 8 × 8, fixed | `MESH_SIZE`, `TILE_COUNT`, `TILE_ID_BITS` | 1–8, 8 by default; `MESH_SIZE`²; 7 at 8 × 8 |
| `link_latency` | 3; a hop 4 cycles | a link's register stages (16.1) | 3, a hop 4 cycles: contract, and the attach links' too (v1.35's simulator had `cc_link_latency` 1) |
| `mem_chunks` | 1,048,576 (4 MiB); any multiple of 256 KiB up to 8 MiB | `SCRATCHPAD_BYTES` | 4 MiB by default; a power of two from 512 B to 8 MiB (item 107) |
| areas of at most 512 KiB | 8 areas at the default | `MAX_BANK_BYTES` | 512 KiB |
| controller memories | 32 KiB + 32 KiB | `IMEM_BYTES`, `DMEM_BYTES` | 32 KiB each by default |
| boot ROM | 256 bytes, 48 words used | `BOOT_ROM_BYTES` | 256; the ROM's program is 6.5's, 44 words in the RTL `[meas: RTL]` |
| big-SRAM engine pointers | 21-bit chunk addresses | `WORD_ADDRESS_BITS` | 21 |
| `reg_load_latency` | 2 (register block, windows), 1 (table window) | `WINDOW_LOAD_LATENCY`, `TABLE_LOAD_LATENCY` | 2, 1; a store takes effect at the end of *t* + *L* − 1 (6.4), a cycle later in a window than v1.35's |
| `ts_engine_latency` | 1 (1–4 measured) | `TABLE_LATENCY` | 1 by default |
| `ag_lookahead` | 2·`ts_engine_latency` + 2 | `READ_AHEAD` | 2·`TABLE_LATENCY` + 2 |
| address-generator lead-in | 4L + 3 (group), 2L + 2 (stream) after the start store | the lead-in (8.2) | the same, and the group transfer's whatever its first entry holds (v1.35's simulator started an R = 0 first group at 2L + 2) |
| big-SRAM array read latency | 2 in meshsim, *(Open)* | `READ_LATENCY` | the implementation's: 3 from the read arbiter's take to the row `[meas: RTL]` (item 170). It includes what v1.35 counted as queueing: there is none |
| **K** | 8 | `LAND_LATENCY` | 8: contract. The RTL's slowest write path is 2·`TABLE_LATENCY` + 2 cycles after the acceptance, so 8 covers `TABLE_LATENCY` ≤ 3 `[meas: RTL]` (item 128) |
| `cm_latency` | 2 | `WRITE_PORT_LATENCY` | 2 |
| `rp_latency` | 2 for every peripheral *(Proposed)*, at most K | `PERIPHERAL_LATENCY` | a bound, at most `LAND_LATENCY` − `WRITE_PORT_LATENCY`; every peripheral specified lands at 2 from the acceptance (item 48) |
| write counters | seven, 20 bits | `COUNTER_BITS` | 20 |
| `HIST` count | 17 bits | `HIST_BITS` | 17 |
| tokens | 64 | `TOKEN_COUNT` | 64 |
| GATHER request queue | 16 | `GATHER_QUEUE_DEPTH` | `TOKEN_COUNT`, 64 (item 169) |
| `queue_depth` | 16; the loopback FIFO at least 64 | `ROUTER_QUEUE_DEPTH`, `LOOPBACK_QUEUE_DEPTH` | 16; at least `TOKEN_COUNT` |
| controller read port's request queue | at least 64 entries of 43 bits | `READ_QUEUE_DEPTH` | 64 to 2¹⁶ − 1; an entry is 58 bits, with its full 32-bit destination (10) |
| request FIFO | 128 × 128 bits | `REQUEST_QUEUE_DEPTH` | 4 (item 64) |
| request bus's receive queues | 128, 256, 128, at the root | `RECEIVE_QUEUE_DEPTH` | the same depths at 8 × 8, in the receiving tile's request unit (11.3) |
| `ready_latency`, `go_latency`, `error_latency` | 8 each; the physical values *(Open)* | `ARRIVE_LATENCY`, `RELEASE_LATENCY`, `ERROR_LATENCY` | the chip's (below) |
| UART | 16-byte FIFOs *(Proposed)*; reset divisor round(`f_ref` / 57,600), of a reference oscillator | `UART_FIFO_DEPTH`, `UART_RESET_DIVISOR` | 16; the chip clock's frequency over 115,200 (item 172) |
| `STREAM`, `GROUP` | three 256 × 32-bit arrays; 64 entries of 64 bits | the tables (8.6) | 256 entries, 21, 32 and 25 + `TILE_ID_BITS` bits; 64 entries of 38 bits |
| GATHER value size *k* | 1–8 | `GATHER_CONFIG.value_size` (13) | *k* − 1, *k* 1–8 |
| events | 16 bits used | the tile's events (7.2) | 14 bits, at new positions |
| peripherals | ten standard words; DMA count up to 4,095 128-bit words | the control block and the DMA block (17.1, 17.4) | seven words and three; the count unchanged |
| replayer, Tracer | in their documents | `REPLAY_QUEUE_ENTRIES`, `REPLAY_OUTPUT_ROWS`, `TRACER_WINDOW_ROWS`, `MAP_PROBE_LINES` (v1.35's `tr_probe_lines`) | 256, 32, 65,536, 3 |
| digit-all-to-all group gap | 0; 4 for the two-hop layers | software's (27, `spec/schedule.md`) | unchanged |
| eject priority | the loopback FIFO last | the loopback queue (16.3) | unchanged |
| keep-alive watchdog | off at reset | `WATCHDOG` (17.6) | unchanged; a store of *N* in cycle *t* expires at the end of *t* + 1 + *N* |

**New in chapter 19, with no v1.35 parameter:** `STREAM_BYTES` and `STREAM_DEPTH` (the host interface, 17.6),
`LPDDR_ADDRESS_BITS`, `RW_QUEUE_BEATS` and `RW_BUFFER_BYTES` (the RW engine, 17.7).

**Removed with their hardware.**

| v1.35 parameter | v1.35 value | why it is gone |
|---|---|---|
| area request-queue depth | 8 in meshsim, *(Open)* | no bank has a queue: a write wins its bank and a colliding read is not taken (14.1, 15); items 2 and 4 |
| area tie order | writes first | the same rule, now the only one (15) |
| write-engine input FIFO | 4 | the write engine is a fixed pipeline and refuses nothing (12.4) |
| `PTR`, `HIST` request FIFOs | 16 each | the updates have a bypass and no queue (12.5) |
| pointer stage depth | 2·`ts_engine_latency` + 2 | the implementation's: the contract is `LAND_LATENCY` (12.3) |
| `stage_delay` | 64 cycles, a software constant | no announced staging: a tile's load follows its `LOAD` request (6.5), and the segment's deliveries follow requests (17.9.10; item 162) |
| start skew between tiles | 0 or +1 cycle | none: the release reaches every compute tile in the same cycle (7.3, 18; item 171) |
| command block | 16 words at the top of data memory | no commands: a phase is the image's schedule (6.5). The reply block stays, as a software convention, its address the image's (22.18) |
| `mtime`, load from program memory | | the core has `mcycle` alone, and IMEM and the boot ROM are fetch-only (6.1, 5.1) |
| flush wait before a soft reset | about 0.1 ms | the hard and soft reset are *Open* (18; item 124) |

**The simulator's.** Values only the model has. Each is set so that the simulator reproduces the RTL's figure.

| v1.35's simulator | v1.35 value | v1.36's simulator |
|---|---|---|
| `intra_tile_latency` | 1 per stage | the simulator's; the paths' cycle counts are chapter 21's |
| load-use penalty | 0 | 1 cycle `[meas: RTL]` (`spec/core.md`) |
| taken-branch penalty (`branch_penalty`) | 1 | by kind `[meas: RTL]`: 3 after a taken branch or a `JALR` other than a ready return; 2 or fewer after a `JAL` or a ready return (`spec/core.md`, Core contract, the Control transfers row) |
| multiply latency | 3 | two pipelined stages, with their interlocks `[meas: RTL]` (`spec/core.md`) |
| wake to the next instruction | the same cycle | the instruction after the `WFI` retires 3 cycles after the wake; a `WFI` with a wake pending costs 2 (the core specification's Lockstep regions, `spec/core.md`) |
| the request tree's `hop_latency` | 3 a hop; 17 cycles unloaded, 18 to loadable | a cycle a node, its links wires: a request is loadable at its target 11 cycles after its `REQUEST_SEND` store from a compute tile, 10 from a special tile `[meas: RTL]`. The links' pipeline stages come with a floorplan (item 61) |
| the request tree's credit and ready latency | 3 | a node's ready in the same cycle; a returned credit usable the next cycle `[meas: RTL]` |
| `cc_link_latency` | 1 | none: an attach link is a link, 3 stages (16.1) |
| `cc_dma_depth` | a 4-deep credit FIFO | none: a peripheral DMA's `ready` is room in the router queue its packet enters (16.4) |
| `strict_replies` | off | unchanged: the simulator's check of the reply block's convention (22.18; invariant 8), with the block's address from the image |
| Tracer and replayer stall costs | the models' | the RTL's, cycle for cycle: chapter 21, `spec/jolt/replayer.md` and `spec/jolt/tracer.md` (item 163) |

**The global nets.** `ARRIVE_LATENCY`, `RELEASE_LATENCY` and `ERROR_LATENCY` are the chip's, and their physical
values are *(Open)* (item 8). Nothing in the machine depends on their values but the cycle counts.
- **The RTL's chip** holds placeholders: `BARRIER_ARRIVE` and the error lines are wires from the tiles'
  flip-flops (latency 0), and `BARRIER_RELEASE` is one flip-flop (`RELEASE_LATENCY` 1) `[meas: RTL]`. Its empty
  phase, from one release to the next, is 35 cycles `[meas: RTL]`.
- **v1.35's simulator** defaulted all three to 8, refused a `ready_latency` below 3, and tied `error_latency` to
  `ready_latency`. v1.36's simulator takes each of the three on its own, from 0 up, so that it can run at the
  RTL chip's values and at a floorplan's.

## 20. Invariants (checked by the simulator)

**Every invariant is a meshsim check.** A rule a program must keep is checked by the hardware or is undefined
behaviour (1.3), and the simulator checks both kinds: undefined behaviour is fatal in every build, the run ending
with a message naming the access, and the other invariants are counted in every build and, in the debug and suite
builds, also reported by name while the run goes on. **The exceptions** are the checks v1.35's simulator lacks
(item 159): invariant 17's limits on outstanding requests, its write-buffer rule and its rule for reads during a
group transfer, and invariant 18 for `APPEND`, `BUCKET` and `AUGMENT` into the scratchpad; and the checks v1.36's
simulator does not make yet, which `OPEN-v1.36.md` lists (the stray at its acceptance of invariant 9, invariant 22,
the reply block's convention of invariants 8 and 11, a reserved bit stored, a load's order). **The hardware checks
none of them as such.** Where it has an error for the same mistake, the invariant names it (**hardware:**
`ERRORS.name`); every other rule here is software's alone.

Numbers are v1.35's and are never reused: an invariant whose subject is gone keeps its number as a one-line stub.
Invariants 27 to 34 are new in v1.36: rules the machine states as UB that v1.35 did not have.

1. **No queue overflow:** the router's queues, an attach tile's attach-port queues included (16.3; the loopback
   queue cannot overflow by design, 14.2), and the gather queue (13). **Hardware:** `ERRORS.router_overflow`,
   `ERRORS.gather_overflow`. *(v1.35's write-engine input FIFO and `PTR` and `HIST` request FIFOs are gone, 12.4
   and 12.5, and with them v1.35's `ERRORS` [4] and [5].)*
2. **No buffer is both read and written within one transfer** (15). The hardware keeps each bank to one access a
   cycle by itself: a write wins its bank, and a read that would meet it is not taken (14.1). The simulator checks
   that its model does.
3. **The timetable holds:** every stream, group and rotation starts on its scheduled cycle and never earlier, and
   read-ahead waits are part of the timetable, identical on every tile (8.3, 8.4, 8.5). **Hardware:**
   `ERRORS.drift`, when the read arbiter refuses a group transfer's read (8.4).
4. **Every expected arrival lands first:** a tile's counters are all at 0 when its core stores to `ARRIVE` (7.3).
   **Hardware:** `ERRORS.early_arrival`.
5. **No bad packets** (4.2): no `WRITE` to the boot ROM, to region 0xFF, to an unmapped region or to an absent
   peripheral, and no `AUGMENT` at 128 bits. **Hardware:** `ERRORS.bad_packet`, the packet dropped uncounted. No
   packet is misaligned to its size, and none has size 11 (UB, the simulator only).
6. **No `HIST` count exceeds `HIST_BITS`,** 17 bits (12.5).
7. *(Withdrawn in v1.2: there is no RUN/HALT line. See `spec/history/CHANGES-v1.2.md`.)*
8. **No `READ_SEND` store finds the read queue full** (10). **Hardware:** `ERRORS.read_overflow`. And, in the
   simulator's `strict_replies` mode, no reply-block word is overwritten unread: a check of the reference
   firmware's convention (22.18), not of the hardware, which has no reply block.
9. **No counter underflow:** no counted packet's decrement finds its counter at 0, so counted data reaches a tile
   only when it has armed for it (12.2). **Hardware:** `ERRORS.counter_underflow`, at the decrement; the simulator
   also reports the stray at its acceptance, naming it.
10. **Gathers are local:** a `GATHER` packet is only ever sent to the tile itself (4.2).
11. **A reply lands where its request named:** in the requester's DMEM, aligned to its size, and nowhere else
    (11.4); a reply by the reference firmware's convention is 32 or 64 bits at the sender's own pair of the
    receiver's reply block (22.18). *(v1.35's rules for the command block are removed in v1.36: there are no
    commands, 6.5. Its rule that every other `WRITE` to DMEM or IMEM stays inside its memory is invariant 19's,
    and the load's writes are invariant 25's.)*
12. *(Withdrawn, 2026-10-26: replies land in the reply block, so a reply can no longer address the command block.)*
13. *(Withdrawn, 2026-10-26: replies could no longer address a word outside data memory; 11 now covers every write.)*
14. **Read-unit results and incoming traffic do not overlap:** no packet from outside the tile arrives while the
    tile has read-unit reads outstanding (queued by `READ_SEND` and not yet accepted back by the traffic director).
    Checked as actual overlap in time. *(Not stated by the RTL's specification; kept from v1.35. Item 116.)*
15. **No errors:** every tile's `ERRORS` and every peripheral's stay 0 (7.4, 17.2). A program error the hardware
    can see shows up here, on the tile's error line and on the toast pin (17.6, 18).
16. **The peripherals' invariants** (17):
    - every peripheral's: no `START` while it is busy (17.2); no `DMA_START` while its DMA is busy, no transfer
      naming an output queue it does not have or more entries than that queue will hold, and no store to `DMA_HDR`
      or `DMA_DEST_ADDR` during a transfer (17.4); no store to block 1 of a peripheral without a DMA (17.1); no
      credit returned to a full counter; every write into its region landing within `PERIPHERAL_LATENCY` of its
      write port, and so by `LAND_LATENCY` (17.3, 17.5). Its region may be written from any source (17.3), with
      only the writes its section allows;
    - the replayer's and the Tracer's: in `spec/jolt/replayer.md` (17.8.13, 17.8.24) and `spec/jolt/tracer.md` (17.9.11, 17.9.18);
    - Ingress/Egress's and the RW engine's: invariant 34.
17. **The request contract is kept** (11):
    - a request names its data's and its reply's counters and its reply's address, and its data and reply arrive
      only when the requester has armed those counters (11.4);
    - a tile has at most 1, 3 and 1 requests outstanding to the master, memory and tracer tiles; error reports are
      exempt (11.3);
    - a part of the write buffer is written only as the memory tile's reply to a `WRITE` directs, exactly its
      `beats` packets on the counter the reply names, one writer in flight or paced (11.5, 17.7);
    - no `READ` is outstanding and no write into the write buffer is in flight while a group transfer runs on any
      tile. *(Not stated by the RTL's specification; kept from v1.35.)*;
    - no `STREAM_HDR.dst`, `WRITE_HDR.dst` or `DMA_HDR.dst` names a reserved tile id, `TILE_COUNT` + 3 up to the
      one below broadcast, or none (4.1). A packet for a reserved tile id is dropped where it enters the NoC, with
      no error (16.2); none as a dst is an encoding the specification does not define, UB (1.3).
      No stream on a compute tile broadcasts: only the special tiles broadcast from streams. *(The second rule is
      not stated by the RTL's specification, whose hardware sends a compute tile's broadcast like any loopback
      packet (8.3, 14.2); kept from v1.35 as a convention, 22.13, and a question for the owner in
      `spec/CHANGES-v1.35-to-v1.36.md`.)*;
    - every request names target 0, 1 or 2 (a request for 3–7 is dropped at the root, 11.2), carries an `op` its
      target defines (11.5), and has 0 in its requester field (11.3);
    - no load of `REQUEST_RECEIVE` finds the receive queue empty (11.1). **Hardware:**
      `ERRORS.receive_underflow`.
18. **One source per order-placed destination** (14.4, 17.3): a tile never has two sending units (the address
    generator, the gather unit, the write unit, a peripheral's DMA) sending `APPEND`, `BUCKET` or `AUGMENT` packets
    through the same `PTR` entry of the same receiver at once, or writes into the same ordered port of a
    peripheral (a replayer queue). And a peripheral's ordered port takes one source at a time: since its region
    may be written from any source, no two tiles feed the same port at once. The simulator flags overlapping use
    in time.
19. **Everything stays in bounds** (5.1, 15): no packet and no load, store or fetch of a core addresses past the
    end of its memory, and no auto-advancing address (`STREAM_DEST_ADDR`, `WRITE_DEST_ADDR`, `READ_DEST_ADDR`, a
    peripheral's `DMA_DEST_ADDR`, the units' word addresses, a `READ` request's deliveries) passes the end. A
    `READ_DEST_ADDR` is in DMEM (10). An RW engine job stays inside LPDDR and the write buffer (17.7). No transfer
    walks past the end of its table (1.3). The hardware does not check: an address past the end of its memory is UB.
20. **Pointers and transfers stay aligned** (5.4, 8.3, 12.4, 13, 15, 17.4): `STREAM_SRC_ADDR` and
    `STREAM_DEST_ADDR` to the stream's size; a `PTR` entry to the size of the writes through it, a record for
    `AUGMENT`; `GATHER_CONFIG.record_base` and the gather's `PTR[me]` to a row, whatever *k* is; `READ_DEST_ADDR`
    and every `READ_SEND` address to the read's size; a peripheral's `DMA_DEST_ADDR` to 16 bytes. **Hardware:**
    nothing for a packet or a unit, where a misaligned access is UB. The core's own misaligned load or store traps
    (6.2; invariant 23).
21. **No reserved encodings:** no packet in mode 6 or 7 (UB), and no `COUNT` packet with counter 0 (it does
    nothing); no `RECEIVE_CONFIG` digit 5–7 (12.5); no 1 stored in a reserved bit; no access to a word a block
    does not define or to a block a window does not use (5.3). **Hardware**, for the last:
    `ERRORS.access_fault`. *(v1.35's simulator-only trace and exit words are gone: such a word is unmapped. A
    program's result is a request to the master tile.)*
22. **Uncounted traffic is `WRITE` only:** `APPEND`, `BUCKET` and `AUGMENT` packets are always counted, and so is
    every write into the scratchpad. The only uncounted `WRITE`s are a load's (6.5). *(Neither rule is stated by
    the RTL's specification; kept from v1.35.)* And **no write lands in DMEM's first 16 bytes** (6.3), which catches a
    C null-pointer store and an all-zero header alike.
23. **No traps** (6.2): no `ecall` or `ebreak`, no misaligned load or store, no illegal instruction, no access
    fault, a fetch's included, and no trap after the first. Every trap goes to the handler, which reports to the
    master tile and parks (6.5). **Hardware:** `ERRORS.access_fault` and `ERRORS.illegal_instruction`, which raise
    the tile's error line; the other traps raise no error, so only the handler's reports and the master tile's
    watchdog show them (17.6). *(v1.35's misaligned jump target cannot occur, 6.2, and its fetch outside program
    memory is now an access fault.)*
24. **`LAND_LATENCY` covers the write path** (12.3): it is at or above the implementation's slowest write path,
    and no read taken after a counter's decrement overtakes a write that counter counted, a core load of `PTR` or
    `HIST` whose sample point follows the decrement included. The simulator measures the margin across workloads
    (item 128).
25. **A load keeps its order** (6.5): every packet of a tile's load, its `COUNT` included, comes from one sending
    unit of the master tile and is of one kind, or the other units are fenced before the `COUNT`; no packet of a
    load arrives after its `COUNT`; the master tile starts a load only once that tile's `LOAD` has reached it; and
    from a tile's `LOAD` until its jump to IMEM's base nothing but the load writes its IMEM or DMEM. *(v1.35's
    overlays, "command landed" and `stage_delay` staging are gone.)*
26. **No ignored start:** no `DMA_START` while the tile's transfer is in progress (8.2).

**New in v1.36.**

27. **Every compute tile takes part in every release** (7.3): the master tile stores to `RELEASE` only when every
    compute tile has arrived.
28. **Only a tile on the barrier arrives:** no `ARRIVE` on a tile whose `PARAMS.barrier` is 0 (7.3).
29. **A table is the core's only while its engine does not use it:** no core store to a `STREAM` or `GROUP` entry
    the running transfer reads (8.6), and no core load or store of `PTR` or `HIST` in a cycle its engine uses a
    port (12.5): a program uses them only once the arrivals' counters are done.
30. **`GATHER_CONFIG` is static while it is read** (13): no store to it while the gather unit is busy, or while
    an `AUGMENT` that reads it has not yet decremented its counter.
31. **No collision in a core's memories** (6.3): no core access and NoC write to the same word of IMEM or DMEM in
    the same cycle. Code written into IMEM is fetched only after it has landed.
32. **Counters stay in range** (12.2): no store brings a counter past 2²⁰ − 1.
33. **An image keeps the core's conventions** (6.2, 6.5): `mstatus.MIE` stays 0; a handler of its own stays in
    place until the image ends; and a loaded image ends by jumping to the reset PC with every counter at 0.
34. **Ingress/Egress's and the RW engine's rules** (17.6, 17.7): no `START` on Ingress/Egress and no `WRITE` to
    the master tile's region 0x04; no `UART_DIVISOR` of 0; no push into the RW engine's full output queue 1; the
    transfers from its output queue 0 take exactly the beats its reads put there; no `WRITE` packet into a beat
    of the write buffer a running write reads, and none past the buffer's end.

## 21. Timing summary

Every latency a program or the simulator relies on, in one place. Each row names the section that defines it, and
is one of two kinds:

- **contract:** stated by chapters 1–19 or by the core specification (`spec/core.md`). Every implementation keeps it.
- **the RTL's:** the contract leaves it to the implementation, and the figure is the RTL's (Snarky One's), marked
  `[meas: RTL]` where it was measured there. v1.36's simulator reproduces it. Software must not depend on it, except
  through lockstep (6.4): it is the same on every tile.

*t* is the cycle in which the core issues a store or a load (6.4), *L* is `TABLE_LATENCY` (1 by default), and *a* is
the cycle in which the traffic director accepts a packet (12.3). Where v1.35's simulator differs, its figure is in the
last column; `spec/CHANGES-v1.35-to-v1.36.md` has the full table.

**The NoC.** A packet injected in cycle *i* (14.3) is at the router's local input in *i* + 1, and crosses each link,
an attach link included, in four cycles: three register stages and one for the route block and its queue's write
(16.1). With no queueing, a packet that crosses *h* links is ejected to the traffic director in cycle
*i* + 4*h* + 3. Queueing adds a bounded amount (22.24).

### 21.1 The core and the registers

| Path | Timing | Kind | Section | v1.35 |
|---|---|---|---|---|
| a load from a window | `WINDOW_LOAD_LATENCY` = 2 cycles; it returns the state at the end of *t*, and takes its side effect at the end of *t* + 1 | contract | 6.4 | the same |
| a load from a table | `TABLE_LOAD_LATENCY` = 1; it returns the state at the end of *t* − 1 | contract | 6.4 | the same |
| a store to a window | takes effect at the end of *t* + 1, and is answered in *t* + 1: a store costs the core one cycle, as a DMEM store | contract | 6.4 | the end of *t* |
| a store to a table | takes effect at the end of *t*, and is answered in *t* + 1 | contract | 6.4 | the same |
| a store that stalls | `WRITE_SEND_32`, `_64`, `_128`, `REQUEST_SEND`, and Ingress/Egress's `UART_TX`, `STREAM_SEND` and `STREAM_SEND_LAST`: held until its owner takes it, in a cycle *c*, *t* + 1 at the earliest, at whose end it takes effect; answered in *c* + 1; no other store or load waits | contract | 6.4, 9, 11.1, 17.6 | the same three, without the host interface's |
| an access fault in region 0xFF | answered in *t* + 1, a load's or a store's | contract | 6.4, 5.1 | — |
| a trap or an `MRET` | the handler's first instruction, or the one at `mepc`, retires 4 cycles after the trap is taken or the `MRET` retires `[meas: RTL]` | the RTL's | the core specification's "The core's cycle costs" | not stated |
| a DMEM load or store | answered the next cycle; an instruction that uses a load's result right after it waits one cycle | contract | the core specification's Core contract | load-use 0 |
| a taken branch, or a `JALR` that is not a ready return | the next instruction retires 4 cycles after it (3 more than after one not taken) | contract | the core specification's Core contract | 1 more |
| a `JAL`, or a ready return | 3 cycles after it retires, or fewer if it waited in execute | contract | the core specification's Core contract | 1 more |
| a multiply | two pipelined stages; one cycle's wait for an operand still in writeback, and one for an instruction that uses its result right after it | contract | the core specification's Core contract | 3 cycles |
| a wake | an event set at the end of cycle *e* is on `meip` in *e* + 1, cycle R; the instruction after the `WFI` retires in R + 3 | contract | 6.2, the core specification's Lockstep regions | wakes in *e* + 1 |
| a `WFI` whose wake is already pending | the next instruction retires 3 cycles after the `WFI` | contract | the core specification's Wait for interrupt (WFI) | 1 cycle more than an ordinary instruction |
| a live event or a peripheral's error, to the wake or the NMI | one cycle later than a counted event: a live level of cycle *e* is on `meip` or `nmi` in *e* + 1 | the RTL's | 7.2, 17.2 | "aims to be current by the next instruction" |
| the core's other cycle costs | the core specification's "The core's cycle costs"; 1.312 cycles an instruction on CoreMark `[meas: RTL]` | the RTL's | `spec/core.md` | one instruction a cycle |

### 21.2 The barrier, the nets and the boot

| Path | Timing | Kind | Section | v1.35 |
|---|---|---|---|---|
| ready → `BARRIER_ARRIVE` | a one-cycle pulse, in the cycle after the tile becomes ready: `STATUS.arrived` set and the core in the canonical wait | contract | 7.3 | READY when "command landed" and `done` were both 1 |
| `BARRIER_ARRIVE` → `ARRIVALS` | the bit is set at the end of the cycle in which the pulse reaches Ingress/Egress, `ARRIVE_LATENCY` after the tile sent it | contract; `ARRIVE_LATENCY` the chip's | 17.6, 18 | `ready_latency` + 2 to readable |
| `RELEASE` → the release | the store takes effect at the end of *t* + 1; the pulse is on every compute tile's `BARRIER_RELEASE` `RELEASE_LATENCY` cycles later, in the same cycle on every tile, with no skew | contract; `RELEASE_LATENCY` the chip's | 17.6, 18 | `go_latency`, with a random skew of 0 or 1 |
| the release → the core | `EVENTS.release` is set and `STATUS.arrived` cleared at the end of the pulse's cycle; the core resumes as at any wake (21.1) | contract | 7.3 | wakes the next cycle |
| the arrival gate | from `ARRIVE` until the end of the cycle after the release | contract | 7.3 | until the release |
| an error → `ERROR_LINES` | a tile's line is high while its `ERRORS` is non-zero; its bit follows `ERROR_LATENCY` later | contract; `ERROR_LATENCY` the chip's | 17.6, 18 | `error_latency` |
| the nets' latencies | the RTL's chip: `ARRIVE_LATENCY` 0 (a wire), `RELEASE_LATENCY` 1 (Ingress/Egress's flip-flop), `ERROR_LATENCY` 0. Placeholders: their physical values are *(Open)* (item 8) | the RTL's | 19 | 8, 8, 8 by default; no value below 3 can be set |
| an empty phase | 35 cycles from one release to the next, with those nets `[meas: RTL]` | the RTL's | 7.3 | 57 at its defaults `[meas: v1.35]` |
| the watchdog | a store of *N* > 0 issued in *t*, with nothing after it, expires at the end of *t* + 1 + *N* | contract | 17.6 | *t* + *N* |
| the toast pin | set at the end of a cycle in which the master tile's own error line is high or the watchdog expires | contract | 17.6 | — |
| the boot ROM | 44 words. From the reset to its `LOAD`'s `REQUEST_SEND` 26 cycles; from the `COUNT`'s decrement of counter 7 to the `ARRIVE` 9, or with no barrier to the image's first instruction 17; from the release to the image's first instruction 13; from the reset to it with `boot_local` 27 `[meas: RTL]` | the RTL's | 6.5 | another ROM (48 words), another protocol |
| the reset | from outside; every core then starts at 0x0200_0000, in its boot ROM | contract | 18, 6.5 | hard or soft *(Open, item 124)* |

### 21.3 The tile's units

| Path | Timing | Kind | Section | v1.35 |
|---|---|---|---|---|
| `DMA_START` → a stream transfer's first cycle | 2*L* + 2 cycles after the start store | contract | 8.2 | the same |
| `DMA_START` → a group transfer's first cycle | 4*L* + 3, whatever its first entry holds | contract | 8.2 | 2*L* + 2 when the first group has R = 0 |
| an offer → the take | the read arbiter takes an offered read in the cycle it is offered, unless it refuses it | the RTL's | 14.1 | one cycle later |
| the start store → a stream's first packet at the router's local input | 2*L* + 6, nothing refused | the RTL's (derived) | 8.2, 14.3, 15 | 2*L* + 9 |
| `EVENTS.dma_done` | set, with `STATUS.dma_busy` falling, in the cycle after the walk completes; an empty transfer is busy for one cycle | contract (the empty transfer); the RTL's (the cycle) | 8.2 | three offsets, by case |
| a table entry read ahead | in 2*L* + 1 cycles after its read is issued; `READ_AHEAD` = 2*L* + 2 places | contract | 8.5 | the same |
| a send store → injection | the holding register is loaded at the end of *t* + 1 and the packet is injected in *t* + 2 at the earliest | contract (the store's effect); the RTL's (the injection) | 6.4, 9, 14.3 | *t* + 1 |
| `READ_SEND` → offered | queued at the end of *t* + 1, offered to the read arbiter from *t* + 2 | the RTL's | 10 | *t* + 1 |
| a scratchpad read, take → its row | `READ_LATENCY`: 3 in the RTL, the same for every bank and every tile | contract (fixed); the RTL's (3) | 15 | 5; its spec said 4, *(Open)* |
| a read, take → injection → the router's local input | take + 3, take + 4 | the RTL's | 14.3 | take + 6 at the router |
| acceptance → a write presented to DMEM, IMEM or a peripheral | *a* + `WRITE_PORT_LATENCY` = *a* + 2; DMEM and IMEM write at the end of that cycle | contract | 12.3 | the same (`cm_latency`) |
| a write's landing in a peripheral | within `PERIPHERAL_LATENCY` of its write port, at most `LAND_LATENCY` − `WRITE_PORT_LATENCY` = 6; every peripheral built lands at the end of the cycle it is presented | contract (the bound); the RTL's | 17.3 | `rp_latency` = 2, fixed |
| acceptance → the scratchpad write | presented to its bank in *a* + 2*L* + 2 | the RTL's | 12.4 | in its area's queue by *a* + 2*L* + 4 |
| acceptance → `PTR` and `HIST` | read at the end of *a* + *L*, written at the end of *a* + *L* + 1 | the RTL's | 12.5 | complete by *a* + *L* + 4 |
| acceptance → the decrement | the end of *a* + `LAND_LATENCY` = *a* + 8; every write is in place by then, for *L* ≤ 3 | contract | 12.3 | 8; its formula covered *L* ≤ 2 |
| a `GATHER` accepted → its first read | offered, and taken if nothing refuses it, in *a* + 2 | the RTL's | 13 | the same |
| a loopback packet's token | freed in the cycle the traffic director takes its copy | contract | 14.2 | when it left the loopback queue |
| a router overflow | `ERRORS.router_overflow` is set at the end of the cycle of the push | the RTL's | 16.3 | counted a cycle later |

### 21.4 Requests

| Path | Timing | Kind | Section | v1.35 |
|---|---|---|---|---|
| `REQUEST_SEND` → the tree | the request is pushed at the end of *t* + 1, and its first beat is offered in *t* + 2 | contract (the store's effect); the RTL's | 11.1 | *t* + 1 |
| a beat through the tree | one beat a cycle on each link; a cycle a node, the links wires, so two cycles from a compute tile to its target and one from a special tile. Link stages are added where a floorplan needs them *(Open, item 61)* | the RTL's | 11.3 | `hop_latency` 3 |
| a request, send store → loadable at its target | on an idle tree, a load of the target's `REQUEST_IN` words issued in *t* + 11 returns it for a compute tile's request, in *t* + 10 for a special tile's: its eighth beat enters the receive queue at the end of that cycle | the RTL's | 11.3 | 18 (17 on the tree and 1) |
| the tree's back-pressure | `ready` is each buffer's room at the start of the cycle | the RTL's | 11.3 | a credit seen 3 cycles late |
| a credit's return | pulsed in the cycle of the `REQUEST_RECEIVE`'s pop; the root may use it from the next | the RTL's | 11.3 | 3 by default |
| the root's rate | one request per 8 cycles for the whole chip, with no cycle between two | contract (8 beats); the RTL's | 11.3 | the same rate |

### 21.5 Peripherals

| Path | Timing | Kind | Section | v1.35 |
|---|---|---|---|---|
| `START` → `STATUS.busy` | 1 from the end of *t* + 1 | contract | 17.2, 6.4 | the end of *t* |
| `DMA_START` → the first packet | `STATUS.dma_busy` from the end of *t* + 1; the first packet is offered to the router's input in *t* + 3 if its entry is in the queue | contract (the store's effect); the RTL's | 17.4 | in the DMA's queue from *t* + 2 |
| a DMA's pace | one packet a cycle while its queue keeps up and its router input has room (`ROUTER_QUEUE_DEPTH`) | contract | 17.4, 16.4 | a 4-deep credit queue |
| a UART frame | 10 × `UART_DIVISOR` cycles of the chip's clock: 86,810 at 1 GHz and 115,200 baud | contract | 17.6 | 10 × the divisor × the reference oscillator's cycle; 57,600 baud |
| `UART_TX` → the line | a byte pushed into an empty FIFO has its start bit on `uart_tx` from *t* + 3 | the RTL's | 17.6 | no line modelled |
| `uart_rx` | the falling edge is seen two cycles after the pin's; bit *k* is sampled *k* · *D* + ⌊*D* / 2⌋ cycles after that, *D* the divisor | the RTL's | 17.6 | no receiver |
| the host interface | a beat sent is in the transmit FIFO at the end of *t* + 1, and `lvds_out_tvalid` rises the next cycle if it was empty; a beat received is readable the cycle after its handshake | the RTL's | 17.6 | not modelled |
| the RW engine | a `START` issued in *t* raises the job's first burst address on AR or AW from *t* + 3, and a write's first beat on W from *t* + 4; then a beat a cycle while the port takes them. A read ends in the cycle its last beat comes, a write in the cycle its last burst is answered | the RTL's | 17.7 | no model |
| the replayer | 3 cycles a row, 5 for a multiply, plus stalls | contract (the schedules); the RTL's (the stalls) | 17.8.7 (`spec/jolt/replayer.md`) | the same schedules |
| the Tracer | 4 cycles a row, 6 for a multiply, plus stalls and the memory system's waits | contract (the schedules); the RTL's (the stalls) | 17.9.7 (`spec/jolt/tracer.md`) | the same schedules; no pipeline costs |

### 21.6 Events

A counted event is set at the end of the cycle its cause happens. A load whose sample point is that cycle or later
returns it (6.4), and it is on the core's `meip` in the next cycle (21.1). A live event follows its source; the
tile's `EVENTS.peripheral_n` and `EVENTS.request_pending` reach the wake one cycle after a load could read them, so a
live event that falls may leave the wake up for one more cycle, and software re-checks after every wake (17.2).

## 22. Rules and figures kept from v1.35

What v1.35's specification states and chapters 1–19 do not: the rules a program keeps on top of the hardware's,
the conventions of the reference firmware, the reasons behind some choices, and measured figures. Each section
names the chapter it supplements and the section of v1.35 it comes from, rewritten in v1.36's terms and corrected
where the hardware changed. Where it and a chapter of 1–19 differ, the chapter is the authority.

- A rule marked "*(Not stated by the RTL's specification; kept from v1.35.)*" is one the RTL's specification is
  silent on: it stands as v1.35 decided it, and is listed in `spec/CHANGES-v1.35-to-v1.36.md` for the owner.
- A figure marked `[meas: v1.35]` or with an earlier version was measured on that version's simulator, on v1.35's
  hardware, and is to be measured again on v1.36's simulator (item 164). The driver each label names is in
  `spec/history/CHANGES-v1.3-to-v1.35.md`.
- What v1.35 stated and the hardware no longer has is not here: `spec/CHANGES-v1.35-to-v1.36.md` lists it.

### 22.1 Conventions of the kept text (supplements 1)

Kept from v1.35's section 1. Chapters 1–19 define their terms in 1.1; the kept chapters also use these.

- **Record.** 64 bits, 2 words: one (key, ordinal) pair, or one of the *k* parts of a value (12.4, 13). Record
  *i* of a buffer is at word address 2*i* from its base. (The replayer's and the Tracer's **record**, a window's
  checkpoint, is another thing: 17.8.2. The sort's text never means it.)
- **Row.** 128 bits, 4 words: one access of a scratchpad bank (15).
- **`me`.** A compute tile's own tile id. A relative offset *d* names tile (`me` + *d*) mod `TILE_COUNT`; *d* = 0
  is the tile itself (8.3).
- **Naturally aligned.** A 64-bit access at a byte address that is a multiple of 8 (an even word address), a
  128-bit access at a multiple of 16 (a word address that is a multiple of 4). Every access in every region is
  naturally aligned (5.4, 15).
- `x mod n` is the non-negative remainder. Bits are written [*hi*:*lo*].

### 22.2 How the simulator treats a broken rule (supplements 1)

Kept from v1.35's preamble ("How the hardware treats software bugs" and what v1.35's meshsim does). The two
kinds of rule are 1.3's. What the simulator does with each:

- **Undefined behaviour is fatal** in every build: the run ends with a message naming the access. Chapters 1–19
  mark a rule UB by "UB (1.3)"; the kept chapters also write "(meshsim)" where v1.35 did.
- **A checked rule** sets its `ERRORS` bit, as the hardware does, and the run goes on to the handler.
- **Every other invariant of chapter 20 is counted** in every build and, in the debug and suite builds, also
  reported by name while the run goes on, so a test fails on a violation it does not expect.
- **The test for a new hardware check** is unchanged: would simulation catch this on our firmware? If so, it is
  UB (`spec/GUIDELINES.md`).

The traps that set no `ERRORS` bit, and what the simulator does on each, are 22.7's.

### 22.3 Counted and uncounted traffic (supplements 4)

Kept from v1.35's 3.1. The packet's fields, modes and bad packets are 4.1 and 4.2; these are the rules a program
keeps on top of them.

- **Anything whose landing matters is counted.** A program sends no uncounted `APPEND`, `BUCKET` or `AUGMENT`,
  and no uncounted write into a scratchpad: scratchpad data is always counted, staging included (invariants,
  chapter 20).
- **Uncounted traffic is `WRITE` only,** at explicit addresses: the master tile's load of a tile's image and
  arguments (6.5). v1.35's other uncounted traffic, the commands, is gone (6.5).
- **There is no invalid header.** An all-zero header (a `STREAM_HDR` entry, `WRITE_HDR` or `DMA_HDR` left at 0)
  is a valid, uncounted `WRITE` to address 0, in the reserved first 16 bytes of DMEM (6.3). The simulator flags
  it.
- **A counted packet in a reserved mode** (6, 7) is UB (4.2). On hardware it would leave its counter short, and
  the program would wait for ever with no error.
- **A `COUNT` with counter 0** does nothing at all (4.2). The simulator flags it.
- **A `COUNT` from a stream** reads its source as any element does, and its payload is discarded at the
  receiver. A `COUNT` of any size is valid; the smallest is the natural choice.
- **The fence in a group transfer** goes in the stream that ends last (4.2, 8.4).
- **Where the address is unused,** the hardware sends 0 (4.1): software need not clear a `_DEST_ADDR` for a
  mode that ignores it.

**Two principles shape the modes.**
- **At most one streaming reader of the scratchpad per tile at a time,** plus the dependent single reads of the
  gather unit and the read unit. There are no remote pulls: gathers are local (13), and a core reads only its
  own scratchpad (10).
- **Bulk movement is a push.** Anything larger than one word is asked for with a small descriptor and delivered
  by its owner's DMA: a tile's read of LPDDR is a request on the request tree (11.5), answered by the memory
  tile's push (11.4).

### 22.4 Staying in bounds (supplements 5)

Kept from v1.35's 2.2. An address past the end of its memory is UB (5.1).

- **Auto-advancing past the end of a memory is a software bug.** `STREAM_DEST_ADDR`'s elements,
  `WRITE_DEST_ADDR`, `READ_DEST_ADDR`, a peripheral's `DMA_DEST_ADDR` and the engines' word addresses
  (`STREAM_SRC_ADDR`, a `PTR` entry, the gather unit's reads) advance by plain addition. What happens past the
  end (a wrap, an alias, a carry into the next region) is undefined. The simulator errors on every out-of-bounds
  access, a packet's or a core's.
- **The core's own accesses** past IMEM's or DMEM's size fault (`spec/core.md`); v1.35's aliasing of the high
  offset bits is gone.
- **The reserved first 16 bytes of DMEM** (6.3): the linker places nothing there, and an image's load starts at
  0x10. The simulator flags any write to 0x00–0x0F after the load.
- **A C pointer is the packet address,** with no shifts: one map serves both (5.1).

### 22.5 Register conventions and the C header (supplements 5)

Kept from v1.35's 8.1. The map, the windows, the blocks and the access rules are 5.1–5.5; these are the rules
beside them.

- **Configuration registers are static during a phase.** `DMA_CONFIG` is stored once per transfer, before its
  `DMA_START` (a store during the transfer does not change it, 8.2 and Appendix A); `RECEIVE_CONFIG` and
  `GATHER_CONFIG` are not stored while a phase's arrivals are still landing, since the write engine and the
  gather unit read them as packets arrive (12.5, 13). A one-barrier local phase rewrites them between passes.
  *(13 makes a `GATHER_CONFIG` store while it is in use UB; for `RECEIVE_CONFIG` the rule is not stated by the
  RTL's specification; kept from v1.35.)*
- **Actions act in program order.** One register, one action (1.2): two actions are two stores.
- **Every start is start only.** No engine has a resume: a `DMA_START` during a transfer (8.2, invariant 26), a
  peripheral's `START` while it is busy (17.2) and its `DMA_START` during its transfer (17.4) are UB.
- **Programs wait on events.** `STATUS` is diagnostic: no program needs to poll it (7.1, 7.2).
- **The C header.** The tile window and each peripheral's window are declared as C structs overlaid on their
  addresses, one member per word, each `volatile uint32_t` and accessed one word at a time; access is through a
  pointer to the struct, so a whole-window copy, which would consume events and pop requests, has to be written
  out. `fw/include/mesh.h` and `fw/REGISTERS.md` are generated from the simulator's register table and must match
  chapters 5, 7–13 and 17. **Both are v1.35's until the simulator's register table is brought to this
  specification** (`spec/CHANGES-v1.35-to-v1.36.md`): every address but the peripheral windows' bases and words
  32 and up moves, and every header register's layout changes (5.4).
- **An undefined word is an access fault** (5.3), where v1.35's read 0. So a struct's padding must never be
  loaded or stored, and the simulator-only exit and trace words of v1.35's 8.15 are gone: a tile's result is a
  request to the master tile (11.5, its firmware's `op`), and a trace mark is the simulator's own facility,
  outside the map.

### 22.6 Programs and the toolchain (supplements 6)

Kept from v1.35's 4.11 and 4.17.4. *(Not stated by the RTL's specification; kept from v1.35.)*

- **Programs are C, built bare-metal:** `gcc` for `riscv64-unknown-elf`, with `-march=rv32ic_zmmul_zicsr_zicond
  -mabi=ilp32` (6.1; v1.35's was `rv32i_zmmul_zicsr`), `WFI` and the CSR accesses from inline assembly, a start-up
  file, a linker script and the generated register header `fw/include/mesh.h`. There is no divide: a C `/` or `%` by
  a non-power of two compiles to a library call.
- **Constants and initialised data** (`.rodata`, `.data`) **live in DMEM.** IMEM is fetched only (5.1), so v1.35's
  choice of keeping them in program memory is gone, and the load writes them into DMEM (6.5).
- **Register accesses.** Each register is one `lw` or `sw` relative to `x0` (5.2). Through the header's struct
  pointers a compiler may add one base-address instruction, the same on every tile.
- **The C header.** Each window is declared as a C struct overlaid on its address, one member per defined word, every
  member `volatile uint32_t`, and a type per peripheral ending in `Regs`. Access is through pointers, never a
  whole-struct copy: a copy would consume events, and in v1.36 it would also load the words a block does not define,
  an access fault (5.3). The register-table generator emits the structs from the register table, which must match
  5.5, 7–13 and 17.

### 22.7 Traps the hardware does not report (supplements 6)

Kept from v1.35's 4.13, corrected to 6.2.

Only an access fault and an illegal instruction set a bit in `ERRORS` (6.2). **Every other trap is silent:** `ecall`,
`ebreak`, a misaligned load or store, and any trap taken inside the handler. v1.35's other two silent traps are
gone: a fetch outside IMEM and the boot ROM is an access fault, which sets `ERRORS.access_fault`, and no jump target
can be misaligned (6.2).

- **A silent trap** goes to `mtvec` like any trap, so the handler reports and parks (6.5), but the tile's error line
  stays low and the toast pin does not rise. After it the core takes no NMI (6.2), so a later error on that tile
  raises its error line and no trap.
- **A trap inside the handler** restarts the handler at `mtvec`. It is still terminal.
- **They are software bugs, caught in simulation:** the simulator stops on each. The hardware adds nothing for them;
  on hardware the master tile sees the reports, and its watchdog is the backstop (17.6).
  *(Not stated by the RTL's specification; kept from v1.35.)*

### 22.8 The handler's conventions (supplements 6)

Kept from v1.35's 4.13. The boot ROM's handler and its two reports are 6.5's. A loaded image may install a handler of
its own, which sends its own reports, numbered from 0 (6.5, 11.5). **The hardware fixes no report set for it.**
v1.35 fixed one, 24 reports, as a constant of its protocol; in v1.36 it is the reference firmware's convention.
*(Not stated by the RTL's specification; kept from v1.35.)*

The conventions every handler keeps:
- **It reads `EVENTS_PEEK`, never `EVENTS`,** in the tile window and in each peripheral's: a load of `EVENTS` would
  consume the state it is reporting. It does not read the write-only words, which read 0.
- **It reads `PARAMS.peripherals` before a peripheral's window:** any word of an absent peripheral's window is an
  access fault (17.1), which would trap inside the handler. A peripheral without a DMA reads 0 in its block 1 (17.1),
  so the handler reads the same words on every peripheral present.
- **It assumes no interface is clean.** A register sequence the main code left half done (`WRITE_W0`–`W2`,
  `REQUEST_OUT_W0`–`W2`) is never completed, since nothing resumes, so the handler stores every word of each request
  it sends.
- **Each report is one `REPORT` request** (11.5), which asks for no reply and is exempt from the per-tile limits
  (11.3). The request queue holds `REQUEST_QUEUE_DEPTH` = 4 requests, so a handler that sends more is stalled at its
  `REQUEST_SEND`s while the tree drains them (11.1).
- **It never executes `mret`,** and parks in a plain jump to itself (6.5).

**The reference image's set** is v1.35's 24 reports, in v1.36's names, three data words each:

| Reports | Data words |
|---|---|
| 0–9 | x1–x30, three registers each, in order |
| 10 | x31, `mepc`, `mcause` |
| 11 | `mtval`, `PARAMS`, `STATUS` |
| 12 | `ERRORS`, `EVENTS_PEEK`, `COUNTER_1` |
| 13 | `COUNTER_2`–`COUNTER_4` |
| 14 | `COUNTER_5`–`COUNTER_7` |
| 15–17 | peripheral 1's window: `PARAMS`, `STATUS`, `ERRORS`; `EVENTS_PEEK`, `EVENTS_MASK`, `DMA_HDR`; `DMA_DEST_ADDR`, 0, 0 |
| 18–20 | peripheral 2's window, likewise |
| 21–23 | peripheral 3's window, likewise |

An absent peripheral's three reports carry zeros. A report's number is in word 0's `number` field (11.5), and a
tile's first report tells the master tile which set is coming: 0 starts an image's, 24 the ROM's two.

### 22.9 The load, as software sees it (supplements 6)

Kept from v1.35's 6.2, where it is still true of 6.5's load.

- **An image's packets.** The master tile sends an image into IMEM and DMEM as 128-bit `WRITE`s, four words a
  packet, the address advancing by 16. An image's length is padded to whole packets, so up to three zero words
  land just past its end, and nothing may be placed there before the load.
- **One kind.** The load's packets are all unicast or all broadcast (6.5), since the NoC orders packets only
  within a kind (14.4). One image for every compute tile may go as a broadcast from the master tile, once every tile's `LOAD` has
  reached it (6.5); a load of one tile, the tracer tile's among them, is
  unicast.
- **Read-only data lives in DMEM.** The core only fetches from IMEM (5.1), so an image's `.rodata` is linked,
  and loaded, into DMEM. v1.35's images could load from program memory.
- **The image enables nothing.** The ROM has set `mtvec` and `mie.MEIE` (6.5), and an image keeps
  `mstatus.MIE` at 0 (6.2). v1.35's "boot part 2", which enabled interrupts, is gone: the NMI needs no enable.
- **Counted data comes only after its counter is armed.** Scratchpad data and every other counted delivery
  reaches a tile only once the tile has armed the counter it decrements; a packet that beats its counter sets
  `ERRORS.counter_underflow` (12.2), so a violation cannot go unnoticed. The sender knows the counter is armed in
  one of two ways:
  - **by lockstep:** the receivers armed in the start path of the same phase, identical code from the same
    release (6.4), before any tile's transfer can deliver (22.12);
  - **by a request:** the receiver arms, then tells the sender with a request (the boot ROM's `LOAD`, 6.5; the
    segment's `ARMED` and `STREAMS_ARMED`, 17.9.10), and the sender sends only after it has popped it.
  v1.35 closed this window by timing, the sender waiting `stage_delay` = 64 cycles after its release store. No
  timing constant is assumed in v1.36, and `stage_delay` is gone. *(Later, item 125: signed counters, so that an
  arrival before the arming just goes negative. The requests make it unnecessary at the load and in the segment.)*
- **Staged images and overlays are gone.** v1.35 staged later images in the scratchpad and copied them into a
  running tile's memory. In v1.36 an image ends by jumping to the reset PC, and the ROM asks for the next load
  (6.5); nothing is written into a running image's code. A block copy into DMEM remains (22.15).
- **The ROM's size.** `BOOT_ROM_BYTES` is 256 (19). The RTL (Snarky One)'s ROM, the boot path and the handler's
  two reports, is 44 words [meas: RTL]; v1.35's simulator ROM was 48 [meas: v1.35].

### 22.10 Waiting: what a program keeps (supplements 7)

Kept from v1.35's 4.12 and 4.13. The events, the wake and the arrival gate are 7.2 and 7.3; these are the rules
software keeps on top of them. *(Not stated by the RTL's specification; kept from v1.35.)*

- **Programs wait on events.** `STATUS` is diagnostic: no program needs to poll it.
- **A wake consumes nothing** (7.2), so a program consumes or masks an event before its next `WFI`, or that `WFI`
  falls straight through. The wait helpers of `fw/include/mesh.h` follow every `WFI` with a load of `EVENTS`, and act
  only on *value* & *mask*, the events that load consumed. The load is the same code on every tile, so a
  synchronised start is unaffected (6.4).
- **Keep `release` in `EVENTS_MASK`.** The arrival gate lets the release wake the core whatever the mask holds
  (7.3), but only a load with `release` in the mask consumes it. A release left pending ends the tile's next arrival
  gate at once (item 79).
- **Waiting for several events.** `EVENTS_MASK` wakes the core on any one of its events. A program that needs all of
  a set (a counter's drain and `dma_done`, a peripheral's event and a counter's) masks them, and after each wake
  loads `EVENTS`, keeps what it has seen, and waits again until it has seen them all. v1.35's `EVENTS_REQUIRED`,
  which made that one `WFI`, is gone.
- **A phase that arms counters ends with a wait for `all_counters_done` before it arrives.** That wait proves the
  arrivals are in (invariant 4); an `ARRIVE` with a counter non-zero is `ERRORS.early_arrival` (7.3).
- **A count of 0 arms nothing** (12.2), so no event follows it: a phase that expects nothing does not wait for
  `all_counters_done`.
- **`all_counters_done` waits for every counter.** To sleep on one batch while another is outstanding, a tile waits on
  that counter's own `counter_n_done`. Within a phase a program may poll one counter to 0 and act on it, then `WFI`
  for the rest.

**Uses.**
- A copy or a receiving phase: arm a counter, start, wait for `all_counters_done`.
- A search step through the read unit (10): arm the reads' counter with the number of reads, then wait.
- The tables after a transfer: the arrivals' `counter_n_done` (12.5) and `dma_done` (8.6), both seen.
- A request with data (11.4): arm a counter for its packets, send the request, wait for the counter's event. A
  store adds, so other traffic on that counter may be in flight.
- A request with a reply (11.4): arm the reply's counter with 1 and name it, with the reply's address, in the
  request; then wait for its event.
- A received request (`request_pending`), and a peripheral's events (`peripheral_n`).

### 22.11 What earns an error (supplements 7)

Kept from v1.35's 4.13 and its preamble. *(Not stated by the RTL's specification; kept from v1.35.)*

- **An error exists for a mistake that would otherwise go unnoticed, or surface far from its cause:** silent
  corruption or lost data (early arrival, counter underflow, every overflow, drift, a bad packet). A mistake that
  simulation catches on our firmware is UB (1.3): misalignment, a start while a transfer is in progress, a `START`
  while a peripheral is busy. A mistake that stops the machine needs no error: the master tile's `ARRIVALS` and its
  watchdog catch a tile that hangs (17.6).
- **`ERRORS` is never information.** On a correct program and correct hardware it reads 0 for ever; anything
  informational belongs in the simulator's reports.
- **An error raised in a cycle of the reset is lost:** the reset wins.

### 22.12 The phase, as software sees it (supplements 7)

Kept from v1.35's 6.3 and 6.4. The barrier's hardware is 7.3 and 17.6, and the lockstep contract 6.4 and the core
specification's Lockstep regions (`spec/core.md`). There are no commands (6.5): a phase is a step of a fixed
schedule in the image, the same on every compute tile, and what v1.35 passed in a command's arguments is data in
DMEM, written in the load.

**The barrier, on a compute tile** is three instructions with nothing between them: the store to `ARRIVE`, the
`WFI`, and a load of `EVENTS`, which consumes the release. v1.35's `wait_for_command()` was the same without the
command's read.
- The tile is ready, and its `BARRIER_ARRIVE` pulses, only once the core is asleep in that `WFI` (7.3). So the
  release always finds the core in the canonical wait, whatever the nets' latencies; v1.35 relied on the round
  trip being longer than the core's way into `WFI`.
- `ARRIVE` stays an explicit store rather than "being in `WFI`": a tile also uses `WFI` to wait for arrivals,
  which must not look finished to the master tile.
- Under the arrival gate only the release wakes the core, whatever `EVENTS_MASK` holds (7.3).
- Every `WFI` in a program is followed at once by a load of `EVENTS`: a wake consumes nothing (7.2).

**A phase with one barrier** (a local sort, a gather): the tile finishes its previous work and arrives; when
every expected tile has arrived (the master tile's live `EVENTS.all_arrived`, 17.6), the master tile stores
`RELEASE`; every core wakes in the same cycle and runs the phase.

**A phase whose engines must start together** (an all-to-all) is two steps with a barrier between them:
1. **configure:** the tile writes its tables and `DMA_CONFIG` for the transfer, then arrives;
2. **start:** on the release, the tile stores its counters, then `DMA_START`.

Every core leaves the `WFI` in the same cycle and runs the same code to the start store, so the transfers start
in the same cycle on every tile, and the lead-in (8.2) is the same everywhere. **The path from the `WFI` to the
start store must be identical code on every tile,** its loads and stores those of DMEM, the tile window and the
tables, at their fixed latencies (6.4).

**A phase that receives:**
- **Arm in the start path, after the release: store the counters first** (each store adds; at a phase's start
  the count is 0), **then `DMA_START`.** Arming them during "configure" would set `ERRORS.early_arrival` at
  configure's `ARRIVE` (7.3, invariant 4); storing them before the start store means no arrival of the phase can
  beat its counter.
- **Every expected arrival must have landed before the tile arrives** (invariant 4). The counters' events
  guarantee it: by `LAND_LATENCY`, every counted write is in place (12.3).
- **A tile that only sends** in a phase still counts its own loopback writes, if it has any, and waits for them
  before it arrives.
- **From the release to the first read.** v1.35's simulator measured, from the master's release store to every
  tile's first read, 12–13 cycles for a stream transfer started by a counter store then the start, and 20–21
  for a group transfer on the all-to-all's longer start path [meas: v1.35], with a start skew of 0 or +1. Both
  move in v1.36: the core's timing is the RTL's (`spec/core.md`; 21), a window store takes effect a cycle later
  (6.4), and there is no start skew: the release reaches every compute tile in the same cycle (7.3). To be
  re-measured on v1.36's simulator.

**A traced segment** uses three releases, sequenced by the master tile, with the handshakes between as requests
to the tracer tile (17.9.10). The tracer tile takes no part in the barrier (7.3).

**Cost.** A phase that does nothing costs 35 cycles from its release to the next on the RTL (Snarky One)'s chip,
whose global nets are a wire, one flip-flop and a wire [meas: RTL]; v1.35's simulator took `ready_latency` + 3
plus the master's loop, ≈ 11 cycles a command at its 8-cycle nets by its estimate, 57 release to release at its
defaults [meas: v1.35]. The figure scales with `ARRIVE_LATENCY` and `RELEASE_LATENCY` (19), which are the chip's.

### 22.13 The DMA unit's transfers: uses and conventions (supplements 8)

Kept from v1.35's 4.2. The tables, the timetable and the read-ahead are 8.1–8.6.

**What a stream transfer is used for.**
- A copy into the tile's own DMEM or IMEM: a one-stream `WRITE` transfer to `dst` = 0 on a compute tile. At 128
  bits a cycle a 1,024-word image takes about 256 cycles plus the lead-in [estimate].
- A copy into a peripheral: a `WRITE` transfer into its region (17.3).
- A local scratchpad copy: an `APPEND` transfer with `PTR[me]` as the destination, or a `WRITE` transfer at an
  explicit address in region 0x03.
- The local sort's scatter passes: `BUCKET` transfers to the tile itself.
- Gather streams: `GATHER` transfers to the tile itself (13).
- One-word exchanges (below).
- Writes at explicit addresses into another tile's memory or a peripheral's region, the RW engine's write buffer
  among them (17.7).

A stream transfer reads the scratchpad only. Values the core computes in DMEM go from the write unit (9).

**Conventions.**
- **Receiver-placed destinations.** For `APPEND`, `BUCKET` and `AUGMENT` the receiver chooses where the data
  lands, through `PTR` (12.4), so the senders need no destination addresses.
- **Relative destinations** make every stream's `dst`, mode and size the same on every compute tile for the
  all-to-all schedule, which is defined in offsets (`spec/schedule.md`). Packets on the NoC carry the absolute
  dst.
- **What changes every round** is a stream's source and its count: one store each, to `STREAM_SRC_ADDR` and to
  `STREAM_HDR`, the latter with the stream's fixed fields (the header block and `dst`) ORed in.
- **Count is 17 bits** because the full-size sort's local streams have exactly 65,536 elements. Longer moves are
  split into several streams.
- **Padding.** A count-0 stream sends nothing (8.3). A lockstep round that must keep a fixed shape sends dummy
  elements instead.
- **Reserved encodings are errors wherever they appear,** even at count 0: a stream with size 11, mode 6 or 7,
  a reserved tile id or none as `dst`, a `GATHER` stream whose `dst` is not the tile itself, an `AUGMENT` stream
  at 128 bits, or a source or a `WRITE` destination not aligned to the stream's size. None has a check at the
  sender: a packet for a reserved tile id is dropped where it enters the NoC, with no error (16.2), a 128-bit
  `AUGMENT` is a bad packet at its receiver (4.2), the others are UB (1.3), and the simulator reports them all.
- **Broadcast streams.** The hardware sends a stream to the broadcast id like any other: its read holds a token
  until the tile's own copy is taken (14.2). By convention only the special tiles broadcast from streams, and the
  simulator errors on a broadcast stream from a compute tile.
  *(Not stated by the RTL's specification; kept from v1.35.)*
- **A start is the only way a transfer begins:** the release starts none. A synchronised start is the same
  `DMA_START` store, executed by every compute tile in the same cycle after the release wakes it (6.4, 7.3).
- **Packets trail the timetable by a constant.** The timetable (8.3, 8.4) gives the cycles the reads are taken;
  each packet is injected `READ_LATENCY` cycles after its read (14.3, 15).

**A one-word exchange of scratchpad data** is `TILE_COUNT` − 1 streams of count 1, their `dst` in the schedule's
offset order, with `DMA_CONFIG.gap` = 4: a round takes about 63 × 5 ≈ 315 cycles at 8 × 8, with loads far below
any overflow [derived]. v1.35 set the gap on each stream; the one per-transfer gap serves it unchanged.

**The gapless form** *(Open)* would run the streams back to back in about 63 cycles. Every tile sends to the
same offset in the same cycle, so each step is a permutation, and each 4-stream window has link loads of at most
4 in each direction. It relies on the links' uniform latency (16.1) and on the release reaching every compute
tile in the same cycle (7.3). The queue peaks at window boundaries are still to be confirmed in simulation
(chapter 26).

### 22.14 Group transfers and the all-to-all schedule (supplements 8)

Kept from v1.35's 4.2. `GROUP`, the rotations and drift are 8.4; the schedule itself is `spec/schedule.md`.

- **Groups name their own streams,** so groups can share streams and several programs can sit in the tables at
  once (the keys' and the values' all-to-alls, or a 64-bit and a 128-bit form), each started by its number.
- **Each pass rewrites one word per group:** word 0, which holds R. The store also writes `first_stream` and
  `streams`, which share the word, so a program ORs them in. (v1.35 kept R alone in its low word.)
- **R is a global maximum:** every tile uses the same R for a group, so a zero-length group is zero-length on
  every tile, and its one cycle (8.4) is the same everywhere.
- **One stream per destination per group.** The schedule's link-load bound assumes a tile sends at most one
  element a rotation to each destination.
- **A longer period** is made with padding streams.
- **Sizes may differ within a group** *(Proposed, item 27)*: every visit takes one cycle whatever its size.
  *(Not stated by the RTL's specification; kept from v1.35.)*
- **Very short groups.** `READ_AHEAD` = 2·`TABLE_LATENCY` + 2 keeps a group's entries in before its start when
  every group lasts at least 2 cycles [meas: v1.35, `TABLE_LATENCY` 1–4]. Groups of a single one-cycle visit
  back to back wait for their entries (8.5): the wait is part of the timetable, the same on every tile. No group
  of the sort is that short, and one-word exchanges are stream transfers.

**The digit all-to-all schedule.** 16 groups of 4 streams, every group with a period of 4 for every value size,
with loads of at most 4 in all four directions; the periods sum to 64, the lower bound at 8 × 8.
- **Gap 0 between groups.** Gap 0 gives the same queue peaks as gap 4 on uniform and on all the skewed sets,
  with no drift [meas: v1.35], and saves about 60 cycles an all-to-all.
- **Exception:** phases that run every stream at the full window (the two-hop layers TH and BT, chapter 27) need
  a gap of 4 after each group, or the receivers' eject queues overflow at group boundaries [meas: v1.0; peaks 6
  on links and 4 at the eject with gap 4].
- **Against a nine-group schedule** [meas: v1.0]: uniform −3.5 %, trace −11.7 %, 99 % one value −24.5 %.
- **A tile's stream to itself** has its own place in the schedule, which leaves its eject room for it: no token
  waits were measured (item 62). The eject arbiter serves the loopback queue last (16.3), so a self-stream gets
  only the eject cycles its links leave free; that this causes no drift in group transfers is to be confirmed in
  simulation *(Open, item 70)*.

### 22.15 Block copy into DMEM or IMEM (supplements 8)

Kept from v1.35's 6.5, without its overlays. A tile copies a block of its scratchpad into its own DMEM (a
table, staged data) with a loopback stream transfer:

1. Write the `STREAM` entry, one store per array that changes: `STREAM_SRC_ADDR` the source, aligned to the
   size; `STREAM_DEST_ADDR` the destination's byte address in region 0x00; `STREAM_HDR` the count, size 128 bits
   (or 64 or 32), a counter *n*, mode `WRITE` and `dst` 0, the tile itself.
2. Store the number of packets to `COUNTER_n`, which adds it to the count (0 here) and arms the counter (12.2).
3. Store `DMA_CONFIG`: a stream transfer of one stream, starting at that entry.
4. Store `DMA_START`.
5. Wait for `counter_n_done` (or `all_counters_done`): it is set `LAND_LATENCY` cycles after the director
   accepts the last packet, when it is in DMEM (12.3).

The copy runs at up to 128 bits a cycle through the loopback, with no polling and no traffic on the NoC. A copy
whose length is not a whole number of rows ends with a smaller-size stream, or is padded.

**Into IMEM** the same transfer works (region 0x01), and code written there runs the next time it is fetched
(6.3). The code doing the copy must sit outside what it overwrites, and the core must not be fetching a word in
the cycle it is written (6.3: a collision is UB). v1.35's stage overlays, which the master tile broadcast into
running tiles between commands, are gone: an image is replaced whole, by the next load (6.5).

### 22.16 The read unit in use (supplements 10)

Kept from v1.35's 4.3. The registers, the queue and the return are chapter 10.

- **Why it exists.** The core queues reads by stores and never stalls on the scratchpad; the results land in
  DMEM as `WRITE` packets to the tile itself, through the loopback and the traffic director, counted
  `LAND_LATENCY` after the director accepts them (12.3). The read unit has no path of its own into DMEM.
- **Its reads rank last** at the read arbiter (14.1), so they never delay the address generator's timetable or
  the gather unit. During an all-to-all they wait.
- **No stalls.** The queue holds at least 64 entries (10), so a whole round of one search per owner (63 reads at
  8 × 8) can always be issued at once into an empty queue. `READ_FREE` gives the free entries, and the counter
  the program armed gives the results still outstanding.
- **Free is not landed.** An entry is freed when its packet is injected. When the reads' counter is done, every
  result is readable and the queue is empty. The converse does not hold: a full `READ_FREE` says every return
  has left, and the last results can still be in the loopback. Only the counter says they have landed.
- **A dropped read** (`ERRORS.read_overflow`) sends no packet, so its counter stays short; the error says why.
- **Never between the wake and a start store.** The path from the release to a synchronised `DMA_START` is
  identical on every tile (6.4): read-unit traffic belongs before the `ARRIVE` or after the start.

**Reads against arriving traffic.** *(Not stated by the RTL's specification; kept from v1.35, where it is a
candidate to relax: item 116.)* Results return through the tile's injection point, loopback queue and eject,
which they share with the tile's other returns and with every packet arriving from the links. The NoC has no
flow control (16.3).

**Software rule: a tile issues no read-unit reads while packets are arriving for it, and starts no phase that
receives packets while read-unit results are outstanding.** In practice:
- read-unit reads run in steps of their own;
- before a search step, every expected arrival has landed (its counter is done);
- before the next exchange, every result has landed (its counter is done).

The simulator checks it: a packet from another tile arriving while the tile has read-unit results outstanding
is a violation (invariant 14). v1.35's exception, the master tile's command writes, is gone with the commands
(6.5). The memory tile's deliveries (11.4) are arriving traffic like any other.

What the hardware tolerates: the loopback queue cannot overflow (14.2, 16.3) and the eject serves arriving
packets before the loopback, and v1.35's simulator measured **no loss** when read-unit reads overlapped an
all-to-all or a full-rate stream [meas: v1.35, its part M], where v1.0 lost packets (15 results against a
full-rate stream, 32 against a half-rate one, before the loopback was protected) [meas: v1.0]. The rule stands
until it is ruled on (item 116); if the NoC ever gains flow control it is to be revisited.

**The search loop** (the split), in v1.36's names:

```c
while (READ_FREE < 64) ;                 // once, before the first step: never stalls (below)
for each step, while searches are active:
    COUNTER_2      = n_active;           // at most 63
    READ_HDR       = SIZE_32 | COUNTER(2);
    READ_DEST_ADDR = (uint32_t)results;  // in DMEM
    for each active search: READ_SEND = its midpoint;
    wait for counter_2_done;             // WFI, then a load of EVENTS, until it returns the bit (7.2)
    for each active search: compare results[i], update its bracket;
```

Each step issues at most 63 reads into an empty queue, so the loop needs no free-count checks. The first poll
is the only one, and it never stalls: any earlier reads were in a phase that armed a counter for them and
waited for it before it arrived. Converged searches drop out. Results may land in any order (10), so a loop
that needs particular results waits for its whole batch.

Costs, on v1.35's simulator, to be re-measured: a search step's fixed cost through the loopback 27.9 cycles,
2 more than a direct path into DMEM would cost; +0.12 % on a uniform split; the split's search loop 12.35
cycles a search on uniform data [meas: v1.35; 1-cycle register loads].

### 22.17 The request tree: rates and the limits' arithmetic (supplements 11)

Kept from v1.35's 3.3. The tree, its credits and the per-tile limits are 11.3.

- **Every compute tile is a leaf at the same depth,** two nodes from the root, and every node round-robins over
  its inputs, so under full load each of the root's inputs gets an equal share, and each tile its share of its
  row's: fair and starvation-free.
- **Throughput.** The root passes one beat a cycle, so the chip issues at most one request every 8 cycles
  [derived].
- **Why the depths.** `TILE_COUNT` tiles at the per-tile limits fill 64, 192 and 64 entries at 8 × 8, and the
  special tiles' own requests to each other fit in the 64 entries left at each target. So in normal operation
  no target runs out of credits, the root never skips an input, and no row stalls behind one.
- **Outstanding is deliberately conservative.** In hardware a request holds an entry of its target's receive
  queue only from its arrival until the target pops it, well before any answer, and the requester cannot see
  the pop. The software convention (11.3) implies the bound.
- **The limits are a software rule;** the simulator checks them (invariant 17). The hardware only counts
  credits.
- **Error reports are the exception** (11.3): a failed tile's handler sends its reports with no replies and no
  limit. In a mass failure they can fill the master tile's queue and stall rows behind it, and with a request
  queue of `REQUEST_QUEUE_DEPTH` = 4 a handler's `REQUEST_SEND` then stalls its core (11.1); the chip is going
  down, and the master tile drains them one by one.
- **Back-pressure end to end.** A target with no credit stops the root for its requests, the root stops the row
  node, the row node the tile's request queue, and a full request queue stalls the `REQUEST_SEND` (11.1).
  Nothing is dropped anywhere on the tree, except a request for a reserved target, at the root (11.2); the
  simulator flags that one.

### 22.18 The reply block (supplements 11)

Kept from v1.35's 4.11 and 4.12. A software convention, with no hardware of its own: a request names its own reply
address, anywhere in the requester's DMEM (11.4), and the reply block is the default place for replies between
compute tiles. *(Not stated by the RTL's specification; kept from v1.35.)*

- **Layout.** 256 words of DMEM, two for each of the 128 tile ids: the pair for source *s* is at `REPLY` + 8*s*.
  v1.35 placed `REPLY` at 0x0000_7BC0, below its command block; the command block is gone (6.5), and the address is
  now the image's. The stack grows down from below it.
- **A reply** is a 32- or 64-bit `WRITE` to the receiver's reply block at the sender's own pair, counted on the
  counter the receiver armed for it. A 32-bit reply writes word 0 of the pair, and a 64-bit reply both. A 128-bit
  reply, the memory tile's (11.4), does not fit a pair: its requester names a 16-byte buffer of its own.
- **One address in every receiver.** A sender stores `WRITE_DEST_ADDR` once, sets `WRITE_HDR.no_increment` (9), and
  changes only `WRITE_HDR.dst` per reply: one header store and one send a reply.
- **No collisions between sources.** Two replies from one source to one tile overwrite each other, so a sender has at
  most one reply outstanding to each receiver until the receiver has read it. Which counter a reply uses is agreed
  by the two programs.
- **That a sender writes only its own pair is software's to keep.** The simulator checks that a write into the reply
  block is 32 or 64 bits at the sender's own pair (invariant 11).

### 22.19 Requests: what a program keeps (supplements 11)

Kept from v1.35's 4.18. *(Not stated by the RTL's specification; kept from v1.35.)*

- **No acknowledgement for issuing.** A request's effect shows only through its answer (11.4).
- **A burst stalls its sender.** The request queue holds 4 requests (v1.35's held 128, for bursts of debug
  messages), and there is no event for room in it: a `REQUEST_SEND` on a full queue stalls the core until an entry
  leaves (11.1). Heavy use stalls only the tile that sends.
- **`STATUS.request_full`** at 0 means one request can be pushed without a stall.

### 22.20 Scratchpad writes in use: `PTR`, `HIST` and augmenting (supplements 12)

Kept from v1.35's 4.8–4.10. The traffic director, the write engine and the tables are 12.3–12.5.

- **The receiver decides where each sender's data lands.** For an all-to-all it sets `PTR[p]` from the counts
  it already has (prefix sums).
- **One user per phase:** `PTR[src]` (`APPEND` and `AUGMENT`, which may mix) or `PTR[t]` (`BUCKET`), never both.
  *(Not stated by the RTL's specification; kept from v1.35.)*
- **`PTR[me]`** is where a tile's own packets and its gather values land. A tile's own stream into `PTR[me]`
  and its gather values never run at once (14.4).
- **Stepping by size is general.** A value sent as several `APPEND` packets from one sending unit lands
  contiguously, whatever their sizes (14.4).
- **Keeping a pointer aligned.** A `PTR` entry stays aligned (12.4) only if every write through it has the same
  size, or if the sizes descend from an aligned start (128s, then a 64, then a 32). A transfer that continues
  where another stopped needs the earlier one's length to be a multiple of its own size.
- **`WRITE` to the scratchpad** makes no `PTR` access and is not counted in `HIST` (12.4): the memory tile's
  deliveries (11.4) and staged data land this way, and a core can write a chosen address from the write unit
  without touching `PTR[me]`.
- **No hardware clear.** The core clears `HIST`, and sets `PTR`, by stores (5.2), while their engine is idle
  (12.5).

**Count-on-arrival in use.** In a `BUCKET` pass the pointer's and the count's updates happen in the same cycle,
so a pass buckets by the current digit and counts the next at one payload a cycle [meas: v1.35]. In an
all-to-all the receiver counts the first digit of its next sort while the keys arrive, which saves one full
counting pass a sort.

**Augmenting in use.**
- **The ordinal is a position in the buffer, not an arrival count** (12.4). With `APPEND` and `AUGMENT` each
  sender's keys land in a run of their own at `PTR[src]`, and that sender's values land in the same order in the
  value buffer, so key *i*'s value is value *i* only if *i* counts buffer positions. Packets from different
  senders interleave on arrival.
- The keys' all-to-all sends `AUGMENT`, so keys arrive as (key, *i*), ready to sort; the values' sends
  `APPEND`.
- **Step 1** (local keys): raw 32-bit keys are streamed to the tile itself at 32 bits in `AUGMENT`, each landing
  as a 64-bit record, with `PTR[me]` = `GATHER_CONFIG.record_base` = a new buffer. One pass, a key a cycle.
- **`BUCKET` has no augmented form:** augmenting stamps the landing position, which in a bucket write is the
  position after bucketing. An arrival-order form is a possible later addition (chapter 26).
- **One base serves a transfer:** none both augments and gathers (13).

**Waiting for the tables.** Software waits by events, as for everything else (7.2):
- **the arrival side** (packets updating `PTR` and `HIST`, gather values landing at `PTR[me]`) is covered by
  the counter the program armed for them (12.5);
- **the local side** (the address generator reading `STREAM` and `GROUP`) is covered by `EVENTS.dma_done`
  (8.6). The counters alone are not enough: a tile's arrivals can finish before its own transfer.

v1.35's `EVENTS_REQUIRED` is gone (1.1): a program that needs both waits in a loop until a load of `EVENTS` has
returned both. A helper that must work without knowing what ran before may poll `STATUS.dma_busy`, a level;
otherwise `STATUS` is diagnostic. A table access right after a counter's done is safe by `LAND_LATENCY` alone
(12.3); the simulator checks that no core access to `PTR` or `HIST` after a counter's done precedes a pending
update.

**One `LAND_LATENCY` for every destination, deliberately.** Writes to DMEM, IMEM and a peripheral land sooner
than scratchpad writes, but their counters also decrement at `LAND_LATENCY`. On v1.35's simulator a read-unit
search step cost 35.9 + 37.42 cycles per active search, against 29.8 + 37.43 when those writes were counted at
landing [meas: v1.35]: the extra is per step, about a hundred cycles a split. A shorter delay for those
destinations is not adopted.

### 22.21 Counters: what a program keeps (supplements 12)

Kept from v1.35's 4.12 and 4.14. *(Not stated by the RTL's specification; kept from v1.35.)*

- **One counter per thing a program waits for separately.** A store adds (12.2), so one counter waits for the union
  of what was added to it. To learn that batch A has landed while B is outstanding, put them on different counters.
  Which counter is the program's choice, whatever the source.
- **A stray packet is caught late.** Underflow is judged at the decrement, `LAND_LATENCY` after acceptance (12.2,
  12.3). A stray counted packet accepted at count 0, whose decrement comes after the program arms *N*, uses up one of
  the *N*: the event fires one packet early, and the batch's last real packet then sets `ERRORS.counter_underflow`.
  The error is caught, naming the last packet rather than the stray; only a second, cancelling bug would hide it.
  The simulator models the hardware exactly, and also reports the stray at its acceptance, as a diagnostic.
- **There is no flush.** A tile knows a write has landed only by its counter (12.3), or, for an uncounted write, by
  a counted packet that follows it from the same sending unit (4.2, 14.4).

### 22.22 Gathers in use (supplements 13)

Kept from v1.35's 4.4. The gather unit is chapter 13.

1. The address generator streams the sorted (key, ordinal) records to the tile itself as `GATHER`, at 64 bits:
   the ordinal is in bits [63:32], where augmenting put it (12.4), and the gather unit keeps its bits [51:32].
   *(The `GATHER` packet's size is not stated by the RTL's specification; kept from v1.35.)*
2. The gather unit reads each value and sends it to the tile itself as `APPEND`s (13).
3. The values land at `PTR[me]`, which the core set to the start of the reorder buffer. The index arrives in
   sorted order, so value *i* lands at the output's start + 2*k*·*i* words, with no addresses.

- **Arming.** `GATHER` packets are never counted; their counter field passes to the values (4.2). A gather of
  *n* values arms that counter with *n* × (packets per value): *n*·*k* when *k* is odd, *n*·*k*/2 when even.
- **Alignment.** Gather buffers, source and destination, are aligned to a row whatever *k* is (13). The
  software convention is that allocations are row-aligned in general (v1.35's page of control conventions said so).
- **Placement.** The value reads and the values' writes fit within one access a cycle per bank if the index,
  the values and the output sit in three different banks (15).
- **Rate.** Every value costs its request packet and its value packets through the one injection point and the
  one eject: about (packets per value + 1) cycles a value, 2.0 at *k* = 2 [meas: v1.25, the full-size sort's
  uniform-k128]. While the unit is busy the address generator now takes no read at all (14.1), where v1.35's
  took one whenever no gather read was waiting: the rate is to be re-measured on v1.36's simulator. The RTL's
  full-size uniform-k128 gather steps run 2.2 % and 2.5 % over v1.25's simulator [meas: RTL].
- **Overflow.** With the address generator as the only source the queue cannot overflow (13). `GATHER`s from
  the write unit can fill it, so `ERRORS.gather_overflow` is a program error (7.4), where v1.35 classed it a
  hardware-fault detector.
- **Why gathers are local.** A remote gather would work mechanically, but its values would land grouped by
  owner at `PTR[owner]`, not in one sorted sequence, and the owner's address generator would stall, breaking
  its timetable (invariant 10).
- A gather phase sends only to the tile itself, so no NoC timetable is disturbed by its waits.

### 22.23 Scratchpad placement and coherence (supplements 15)

Kept from v1.35's 4.1. The scratchpad's contract is chapter 15.

- **Throughput comes from placement.** Buffers that one phase uses together sit in different banks: a bank does
  one access a cycle, and a write wins it (15). A bank asked for a read and a write in one cycle costs time,
  never data: the read waits, and in a group transfer that wait is drift (8.4). A sequential stream stays in one
  bank at up to 4 words an access.
- **What follows the no-coherence rule** (15): the local sort's passes, which alternate between two buffers; the
  gathers (index and values in, output out); the all-to-alls (send buffer out, receive buffer in); the split's
  searches through the read unit, which read sorted keys nothing writes during the split.
- **The rule is per transfer, not per phase:** a local phase may run several passes, each reading what the one
  before wrote, after those writes' counters are done.
- **There is no flush, no merge logic and no line state.**
- **The placement rules** that keep each phase's traffic within one access a cycle per bank are *(Open)*
  (chapter 26). Placement rules and measurements that depend on the banks assume 8 banks of 512 KiB at 4 MiB:
  in a word address, bits [19:17] the bank (bit 20 is 0 at 4 MiB), [16:2] the row and [1:0] the word in the row.

### 22.24 The links and fan-in (supplements 16)

Kept from v1.35's 3.2 and 4.5. The router is chapter 16; order is 14.4 and 16.2.

- **Why every link is three stages** (16.1). Shorter links are padded with register stages; the longest must
  fit in three, which is a physical-design check. Uniform latency keeps every tile's packets in lockstep, so the
  schedule's per-window load bounds hold exactly.
- **Unicast and broadcast take different paths** between some pairs (16.2): at 8 × 8, offsets 33–39, 44, 52 and
  60 [meas: v1.35]. So a sender's packets that must arrive in order go all unicast or all broadcast.
- **Between sending units there is no order** (14.4), even in one tile: a write-unit packet stored after
  `EVENTS.dma_done` can be injected before the transfer's last read returns.
- **Injection never stalls.** The injection point's packets enter the router whatever its queues hold: a full
  output queue is `ERRORS.router_overflow` (16.3).

**Fan-in.** The NoC has no flow control, so many-to-one traffic is the program's to pace (16.3). Measured limits
[meas: v1.0; to be re-measured on v1.36's simulator], every sender bursting W packets to one compute tile in the
same cycle; the limits are in packets:

| Senders | Largest safe W per sender, nearest senders, no start skew | With v1.35's 0/+1 start skew, the minimum over five sender sets |
| --- | --- | --- |
| 63 | none: 63 × 1 is not safe | none |
| 32–48 | 1 | 1 |
| 24 | 2 | 2 |
| 16 | 3 | 2 |
| 12 | 5 | 3 |
| 8 | 5 | 5 |
| 6 | 10 | 7 |
| 4 | 20 | 15 |
| 3 | 23 | 15 |
| 2 | 30 | 30 |
| 1 | no overflow | no overflow |

- v1.36 has no start skew: the release reaches every compute tile in the same cycle (7.3). The second column is
  kept as the margin v1.35 measured.
- 63 × 1 overflows in every case measured. A queue is full by its count before the cycle's pop (7.4), so a push
  into a full queue is lost even in a cycle that pops it. The first queue to overflow is one of the receiver's
  eject queues.
- Schedule-ordered exchanges (stream transfers in the schedule's offset order) are permutations, so they do
  not fan in.

### 22.25 Pacing the special tiles' traffic (supplements 16)

Kept from v1.35's 4.16. *(Not stated by the RTL's specification; kept from v1.35.)*

- **Pacing is policy.** A packet a special tile sends enters the mesh at its attach tile through a route of its own,
  and does not wait behind that tile's injection point (16.1, 16.2). So every packet a special tile sends during an
  all-to-all adds load to its attach tile's links that the schedule's queue bounds do not count: one broadcast per
  64 cycles raised tile 0's link queues to 12 of 16 `[meas: v1.35]`. The special tiles send bulk data between
  passes, or pace it.
- **Into a special tile** there is no flow control either: an attach output takes one packet a cycle (16.3). Several
  tiles writing into the memory tile's write buffer at once fan in at its attach tile, so writers are one at a time,
  or paced (17.7).

### 22.26 Peripherals: what a program keeps (supplements 17)

Kept from v1.35's 4.17.3. *(Not stated by the RTL's specification; kept from v1.35.)*

- **Act on every bit a load returns.** A load of a peripheral's `EVENTS` consumes one count from every counted bit it
  returns set and `EVENTS_MASK` selects (7.2, 17.2). So a program acts on every such bit, or masks only what it is
  about to service.
- **A queue of items** is not an event kind. *(Deferred, item 119: an event kind for a queue of items, for the host
  interface.)* The RW engine returns a part of its write buffer by `EVENTS.done` (17.7).

### 22.27 The master tile's sequencing (supplements 17)

Kept from v1.35's 6.6: the master tile's software conventions around 17.6's registers.

- **The phase loop.** Store `ARRIVE_EXPECT_0`–`1`; sleep with `all_arrived` selected in Ingress/Egress's
  `EVENTS_MASK` and `peripheral_1` in the tile's own; then store `RELEASE`. `all_arrived` is live and needs
  `ARRIVE_EXPECT` ≠ 0, so the master tile cannot miss it whatever order it writes the two words in (17.6).
- **Every compute tile is expected at every release** (7.3). v1.35's one-at-a-time bring-up, by `READY_EXPECT`
  and first commands, is gone: a compute tile's ROM jumps at every release, with no guard (6.5).
- **The watchdog.** The master tile sets `WATCHDOG`'s limit above each phase's expected length; every `RELEASE`
  restarts it (17.6). It is the only watchdog, it counts the chip's clock, and it is the backstop for a tile that
  hangs without an error: a trap that sets no `ERRORS` bit (6.2), a lost packet, a request never answered (23).
- **The master tile sends** through its own write unit and DMA unit, like any tile (9, 8); its packets enter the
  mesh at tile 0's attach port, with its own tile id as src (16.2). **It receives** the tiles' requests, `LOAD`s
  and reports among them, from its receive queue (11.1, 11.5), and packets addressed to it land in its memories
  like any tile's, counted by their counter fields.
- **Pacing.** The master tile sends bulk data between all-to-all passes, or paces it: nothing but the program
  paces an attach link (16.3). A load sent as a broadcast is full-rate and has the mesh to itself, every compute
  tile waiting in its ROM (6.5).
- **Gone with the commands:** v1.35's wait of `go_latency` + 1 cycles after a release before sending a command,
  and commands crossing the mesh during a phase.
- **The special tiles and the barrier.** The memory and tracer tiles take no part in the barrier (7.3). The
  memory tile starts from its placed image (`PARAMS.boot_local`, 6.5) and serves requests (23); the tracer tile
  is loaded at its `LOAD` and then serves the segment (17.9.10). v1.35's "started" requests are not needed: a
  special tile's first request to the master tile, or the master's first to it, is served whenever its receiver
  is up, since the tree holds a request until it is popped (11.3).

### 22.28 An error, end to end, and the reset (supplements 18)

Kept from v1.35's 6.1. The hardware is 6.2 (the NMI, every trap terminal), 7.4 (`ERRORS`), 17.6 (the error
lines, the watchdog, the toast pin) and 18 (the nets, the reset).

**There is no HALT state.** The reset is the only way to stop anything, and coming out of it means running in
the boot ROM (6.5). What is given up is stopping one tile to inspect it: after an error there is what the
handlers' reports carry, and then the chip restarts.

**An error, end to end:**
1. **An error sets a bit in a tile's `ERRORS`** (7.4), which raises its NMI (6.2). Its handler sends its reports
   to the master tile on the request tree and parks (6.5). Its units and peripherals carry on, unserviced, until
   the reset (17.5).
2. **Its error line** sets its bit in the master tile's `ERROR_LINES_0`–`2`, `ERROR_LATENCY` later; the lines are
   live (17.6, 18).
3. **`tile_error`,** Ingress/Egress's live `ERRORS` [0], sets the master tile's `ERRORS.peripheral_1`, and the
   master tile takes its NMI. The master tile's own errors raise it directly. **Every error is terminal for the
   chip:** nothing on the chip recovers.
4. **The master tile's handler,** best effort, with nothing waiting for it: it tells the file system to flush its
   logs to LPDDR *(Deferred, item 119)*; **drains the tiles' reports** from its receive queue, told apart by
   their numbers (24 and 25 from a tile still on the ROM's handler, an image's own from 0; 6.5, 11.5); and
   **sends the dump out,** over the UART, or over the host interface once its beats have a meaning *(Open: the
   host interface's meaning, 17.6)*. It never returns to the interrupted code, and bounds its waits by
   `mcycle` (6.1). Its details are the master tile's firmware's.
5. **The toast pin** rises (17.6), and **the outside world resets the chip**: the chip never resets itself (18).

- **The report set.** v1.35 fixed 24 reports for an image's handler *(Deferred, item 122: their header)*; in
  v1.36 the header is fixed (`REPORT`, `op` 1, the number in word 0, 11.5) and an image's set is its own (6.5).
  The request queue holds `REQUEST_QUEUE_DEPTH` = 4 requests (11.1), so a handler's burst of reports stalls its
  core on the tree's pace, which the reports' exemption from the per-tile limit allows (11.3).
- **`LED`** is the software pin: the master tile can blink codes on it while it is healthy (17.6).

**The reset inputs.** The RTL's specification has one reset (18), and marks hard and soft *Open*. v1.35's
intent stands as the proposal *(Open, item 124)*:
- **hard reset:** everything, the `lpddr` port and the memory side included; LPDDR's contents are lost.
  Power-on is a hard reset, held until power is stable;
- **soft reset:** everything except a short list: the host interface's link, the `lpddr` port's far side, and
  the LPDDR controller and PHY, so refresh continues and **LPDDR keeps the flushed logs**. The exact list is
  open, and 3's `lpddr` and `fill` contracts constrain it: neither far side may answer after a reset anything it
  accepted before it (3, 17.7).

**Nothing tells the outside world that the flush has finished, and nothing needs to.** A host waits as long as it
wants after the toast pin rises before it resets the chip; about 0.1 ms (about 100,000 cycles at 1 GHz) is a
default. With no host, a person resets the chip by hand, slowly enough for the UART's dump. After a soft reset
the file system is to come back up over LPDDR's contents *(Deferred, item 119)*.

**No hold is needed.** The reset is the whole chip's, so the error lines' pipeline stages are reset with
everything else (18), and no error raised before the reset reaches Ingress/Egress after it.

## 23. The memory system and the file system

*v1.37. 23.2–23.4, 23.7–23.9 are v1.36's, kept from v1.35's 6.7 and section 7: what a tile sees of the memory tile's
answers. 23.5, 23.6 and 23.10–23.13 are new: the files as the hardware sees them, the cache, the clients' AXI ports,
the AXI arbiter and LPDDR, which v1.36 deferred. They are meshsim's design and not yet built in the RTL. The file
system stays deferred (item 119).*

The **memory side** of the chip is everything between LPDDR and the units that use it:

| Part | Where | What it is |
| --- | --- | --- |
| the memory system, the memory tile's peripheral 1 | 17.11 | the cache, the AXI arbiter and the `lpddr` port, behind a window that carries the cache's error |
| the cache | 23.6 | 512 KiB of lines of LPDDR, shared by the chip: the Tracer's `fill` and the Updater's look-up port |
| the AXI arbiter and the clients' ports | 23.10, 23.11 | every client of LPDDR on a port of its own: the cache, the Updater, the RW engine |
| `lpddr` | 23.12 | the arbiter's port to the LPDDR controller, outside the chip, AXI4 with IDs |
| the Updater, the master tile's peripheral 2 | 17.10 | builds the files from the host's update words: client 1 of the arbiter and of the cache |
| the RW engine, the memory tile's peripheral 2 | 17.7 | v1.36's: one read or one write of LPDDR at a time, for the memory tile's requests; client 2 |
| the memory tile's requests, `READ` and `WRITE` | 11.5, 23.2–23.4 | served by the memory tile's core with the RW engine |
| the files | 23.5 | trees of pages, read and written by lines |

```
 the Tracer ──── fill ─────────────────────────┐
 the Updater ─── look-up port 1 ──────────────►│ the cache │── client 0 ──┐
 (master tile)   AXI, client 1 ─────────────────────────────────────────►│ the AXI  │── lpddr ──► the LPDDR
 the RW engine ─ AXI, client 2 ─────────────────────────────────────────►│ arbiter  │            controller
 (memory tile)                                                           └──────────┘
```

Deferred, as in v1.35: the file system, the Binder, the log files and their flush on a failure (23.5).

### 23.1 Where it sits

- **The memory system is the memory tile's peripheral 1** (17.11): the cache, the arbiter and the `lpddr` port are
  its hardware, and its window carries the cache's error, so it reaches the toast pin as any peripheral's error
  does (owner). Nothing in it is started or configured by a program.
- **Its clients reach it from where they are.** The Tracer's `fill` comes from the tracer tile, beside it (2.1).
  The Updater is the master tile's peripheral, and its AXI port and its look-up port reach across to the memory
  side; latency on them does not matter to it (owner). Where each unit sits on the floorplan is the RTL's.
- **The memory tile** is tile id `TILE_COUNT` + 1 (65 at 8 × 8), attached to tile `MESH_SIZE` − 1 (2.1, 16.1), and
  on the line between the special tiles (16.1). Packets for it go to that tile id; a `WRITE` into the RW engine's
  write buffer is addressed to its region 0x05 (17.7).
- **Its packets' src is the memory tile's id** (16.4), its core's and its RW engine's DMA's alike. v1.35 deferred
  the DMA's src here and decided it elsewhere as the id + 8 (item 118); 16.4 does not take that
  (`spec/CHANGES-v1.35-to-v1.36.md`, the reversals).
- **Requests** reach it on the request tree as target 1 (11.2). Targets 3–7 are reserved (11.2): v1.35's target 3,
  kept for the file system, is not assigned *(Deferred, items 83 and 119)*.
- **The same tile-side rules hold for the answers of every request receiver** (11.4): a special tile that answers
  a request does so as the memory tile does here.

### 23.2 How a request is answered

The rules are 11.4's; this section adds only what v1.35 states beside them.

- **Arming first, data, then the reply:** 11.4. A reply is a 32-, 64- or 128-bit `WRITE` (11.4), and **the memory
  tile's replies are always 128-bit** (11.5, 17.7): they leave by the RW engine's DMA, whose packets are whole
  128-bit entries (17.4). So a requester keeps 16 bytes of DMEM for each reply. v1.35 said 32 or 64 bits.
- **Every request names its own reply address,** beside its reply's counter: anywhere in the requester's DMEM,
  aligned to the reply's size (11.4). With up to three requests outstanding to the memory tile (11.3), replies to
  one fixed place would overwrite each other, so each outstanding request gets its own buffer.
- **A `READ`'s answer is its data.** It asks for a reply only if its `reply_counter` is non-zero, and the reply
  is then 0 (11.5); v1.35 had no reply for a read. The data's packets are counted on `data_counter`, and that
  counter's event says when all have landed (12.3).
- **A value answer is a reply:** for `WRITE`, the part's address and the memory tile's counter (11.5). The
  meaning of any other reply (an fd, an address, an error code) is its target's *(Deferred, item 119)*.
- **Requests outstanding per tile** are limited by target: 1 to the master tile, 3 to the memory tile and 1 to the
  tracer tile (11.3), so the receive queues never fill in normal operation. When a request stops being
  outstanding is 11.3's convention.

### 23.3 Reads

A `READ` (11.5) copies `beats` 16-byte beats of LPDDR, from its beat address up, to the tile that sent it, the
requester the tree stamped, from the destination address up.

- **The data** is `WRITE` packets of 128 bits, unicast to the requester, in ascending address order, the address
  advancing by 16 a packet, counted on the request's `data_counter` (11.5, 17.7). This closes v1.35's *(Proposed)*
  item 67. The destination may be any region a packet reaches (5.1): DMEM or IMEM, the scratchpad, or a
  peripheral's region.
- **The requester arms** that counter with the read's packet count before it sends the request. A store adds to
  what is already in flight (12.2), so nothing can arrive uncounted for.
- **Landing.** A read has landed when its counter has drained: by `LAND_LATENCY` every packet is in place (12.3).
- **Destination bounds** are the program's: a read past the end of its memory is UB (5.1). **The replayer takes
  no reads into its queues:** what it needs from LPDDR is fetched into the scratchpad and refilled locally
  (17.8.13).
- **Order between reads.** One read's packets leave in ascending address order, and the RW engine sends reads
  in the order they started (17.7), one transfer at a time. A receiver may still answer *requests* in any order
  (11.4): software that needs one read's data before another's arms and waits for them on separate counters.
  v1.35 also let the packets of two reads to one tile interleave; the RW engine does not.
  *(Deferred, item 119: whether a cache in front of LPDDR may answer hits before misses.)*

### 23.4 Writes

v1.35's writes by tags are 11.5's `WRITE` request and 17.7's write buffer:

1. The writer sends a `WRITE` request: `beats` and the LPDDR beat address (11.5).
2. The memory tile's software allots a **part** of the write buffer to it, arms one of its own counters with
   `beats`, and replies with the part's address and that counter's number (11.5): v1.35's tag.
3. The writer sends exactly `beats` 128-bit `WRITE`s into the part, counted on that counter.
4. When the counter drains, the memory tile's core starts the RW engine's write, and on its `EVENTS.done` the
   part is free again (17.7): the tag, returned.

- **Fan-in** (a constraint on the memory tile's software). Every write into the buffer reaches the memory tile
  through its one attach link, and the NoC has no flow control (16.3), so several tiles streaming into parts at
  once overflow a queue. The software keeps **one writer in flight at a time**, or **paces its writers** to a
  stated share of one packet a cycle (17.7).
- **The source buffer** may be overwritten once the transfer that sends it has ended (`EVENTS.dma_done`): the
  read arbiter has then taken every read of it (8.2), ahead of any later write (15) [derived].
- *(Deferred, item 119: how the file system chooses and reuses parts, the pool's size, and the writer's
  acknowledgement beyond the reply.)*

### 23.5 Files, and the file system *(the file system deferred, item 119)*

**What the hardware fixes.** LPDDR holds **files**, which the Updater (17.10) writes, the cache (23.6) walks and the
Tracer (17.9.5) reads and writes through it.

| Term | Meaning |
| --- | --- |
| page | 4 KiB of LPDDR at a multiple of 4,096 |
| line | 128 bytes of LPDDR at a multiple of 128. Its **line address** is its byte address over 128 |
| unit | 128 bytes: a root, an entry's address and a free page are in units, so a page's address is a multiple of 32 |
| file | a tree of pages: index pages of 1,024 entries above the data pages. Its **depth**, 0 to 3, is fixed when it is made; a file of depth *d* has room for 2^(10·*d*) data pages, and a file of depth 0 is its root alone, which is its data page |
| root | the address of a file's top page, 31 bits in units. The root is always the file's own |
| entry | 32 bits in an index page: [30:0] the address of the page it names, in units; [31] **owned**, 1 when that page is the file's own and 0 when it is shared with another file. **0xFFFF_FFFF is no page, a hole,** at every level, and a hole reads as zeros |
| level | counts from the top: the entry at level 0 is in the root; the entry at level *L* < depth − 1 names the index page of level *L* + 1; the entry at level depth − 1 names the data page |
| page number | a data page's place in its file, from 0 |
| the path | page *p*'s entries, one a level: at level *L*, entry *i* = (*p* >> 10·(depth − 1 − *L*)) & 1023 of that level's index page, which is 32-bit word *i* & 31 of the page's line *i* >> 5, at bits [32·*w* + 31 : 32·*w*] of the line, *w* = *i* & 31, little-endian |
| a file's line | line *l* of a file is line *l* & 31 of its data page *l* >> 5 |

- **Owned all the way down.** Once an entry on a path is not owned, nothing below it is the file's own: a file that
  shares a page shares everything under it. A file **owns** a page when every entry on its path is owned.
- **Copy on write.** A file is copied by copying its root with every entry's owned bit cleared. A writer makes a
  shared page its own, top down, before it changes it: it copies the page to a free page, clears the copy's
  entries' owned bits if it is an index page, and changes the entry above it to name the copy, owned. A missing
  page is built the same way from zeros, or from holes for an index page.

**Deferred, as in v1.35** (item 119): directories and names, the page map and who owns it, freeing pages, the
superblock, recovery, and every request the file system serves. **Every `op` of the memory tile other than `READ`
and `WRITE` is its firmware's** (11.5). In v1.37 the master tile keeps a page map of its own and frees nothing
(30).

- **The Binder** (binding) is not designed, with the file system *(item 60)*.
- **Debug output.** Each tile is to open **log files through the file system** and `printf` into them; when the
  chip fails, the file system flushes their tails to LPDDR (22.28). The protocol is the file system's.
- **Mounting existing state** after a soft reset, so that the wreckage can be read: deferred, with the soft
  reset itself *(Open, item 124)*.

### 23.6 The cache

The cache is the memory system's (17.11): 512 KiB of lines of LPDDR, shared by the whole chip, in front of LPDDR
for two kinds of client.

| Client | Its port | What it asks |
| --- | --- | --- |
| the Tracer | `fill` (3, 17.9.5) | lines of its files, read and written, index and data alike |
| the Updater (17.10) | look-up port 1 (23.6.2) | the entries on a page's path, and changes to one; never a data line |

- **Two ways into a file** (owner). The Tracer goes through the cache for everything. The Updater uses it for the
  index levels only, and moves data pages directly on its own AXI port (23.10). They never meet on a page: a file
  is finished by the Updater before the Tracer starts on it, and the only pages the Tracer writes are its own
  copy's (23.6.8).
- **It never handles a page fault.** It never allocates a page and never builds one: a hole, or a page that is not
  the file's own, is reported to the client, which builds the page and sends the update (owner).
- **It keeps nothing about files:** no lengths, no list of roots. Every request carries its root and depth.
- **It writes LPDDR** for an update's line, which goes through at once (23.6.6), and for a dirty line it evicts,
  which only the Tracer's writes make. It reads LPDDR only for a line it lacks.

#### 23.6.1 Its parts and its memories

| Part | What it does |
| --- | --- |
| **the cache memory** | the tag memory and the data memory: plain memories, the kind a memory macro gives |
| **the front end** | takes requests at the six sources (the four look-up ports, `fill`'s read and its write) into a register each, and picks the next for the walker |
| **the walker** | one request at a time, a level a step: steps down a file's index from its root, reads the line it needs, and answers |
| **the miss handler** | one line at a time: chooses a victim, writes it back first if it is dirty, fetches the line, and installs it |
| **the AXI writer** | one line's write at a time on the cache's AXI port, client 0 (23.10): an update's line or a dirty victim |
| **the update slot** | the one update in flight, from its line's write to its response |
| **the page sequencer** | the walker's discard: a page's 32 lines, each looked up and invalidated |

| Memory | Size | Ports | What a row holds |
| --- | --- | --- | --- |
| tag memory | 512 rows × 216 bits, 13.5 KiB | one read, one write a cycle | a set: for each of 8 ways a valid bit, a dirty bit and a 22-bit tag; the set's replacement order, an age of 3 bits a way |
| data memory | 4,096 rows × 1,024 bits, 512 KiB | one read, one write a cycle; a write has 8 enables of 128 bits | a line: row set × 8 + way |

- **The geometry.** 8 ways by 512 sets of 128-byte lines, keyed by line address: the set is its bits [8:0], the
  tag its bits [30:9]. A page's 32 lines fall in 32 sets.
- **A memory's read** returns the row as it stood at the start of the cycle it was issued, at the memory's output
  from the next cycle; a write lands at the edge that ends its cycle. A read and a write of the same row in one
  cycle: the read sees the old row.
- **The ages** are a permutation of 0 to 7, 0 the most recently used. A way **touched** (a hit, an install, a write)
  becomes 0, and every way younger than it ages by one: true LRU.

**A line's states**, held in the tag memory and in two registers compared with a set and way:

| State | Where | Meaning |
| --- | --- | --- |
| invalid | tag: valid 0 | no line in the way |
| clean | tag: valid 1, dirty 0 | the line as LPDDR has it |
| dirty | tag: valid 1, dirty 1 | a data line the Tracer wrote through `fill`: newer than LPDDR |
| write in flight | the update slot | an update's line being written to LPDDR. It holds the newest contents and is read as any line is; it is not evicted, not discarded and not changed until its write's response |
| being evicted, being filled | the miss handler | the miss handler's set and way, from the victim's choice to the install. No other request walks that set meanwhile |

**An index line is never dirty:** only the Tracer's data lines are, since an update writes its line through
(23.6.6). So LPDDR's index pages are always current but for a line whose update has not been answered.

#### 23.6.2 The look-up port

A request port for each client and one **reply bus** shared by all (owner). `LOOKUP_PORTS` = 4: port 1 is the
Updater's; ports 0, 2 and 3 have no client in v1.37 and their `valid` is held at 0. A client's port number is its
AXI ID (23.10).

**The request,** client to cache, but `ready`:

| Signal | Bits | Meaning |
| --- | --- | --- |
| `valid` | 1 | a request is offered |
| `ready` | 1, cache to client | the cache takes it |
| `op` | 2 | 0 look up, 1 update, 2 discard a page; 3 is reserved, and UB |
| `root` | 31 | the file's root, in units. For a discard, the page's address, a multiple of 32; its bits [4:0] are ignored |
| `depth` | 2 | the file's depth, 0 to 3 |
| `page` | 30 | the page number; at depth 3, 30 bits reach every page |
| `level` | 2 | an update's: the level of the entry to change. Ignored otherwise |
| `entry` | 32 | an update's: the new entry, in the stored form (23.5). Ignored otherwise |

**The reply,** on the bus, to every client:

| Signal | Bits | Meaning |
| --- | --- | --- |
| `valid` | 1 | a reply is on the bus in this cycle |
| `client` | 2 | whose reply: its port's number |
| `status` | 3 | 0 `ok`, 1 `hole`, 2 `not_owned`, 3 `beyond`, 4 `lpddr_error`; 5 to 7 reserved. 0 to 3 are `fill`'s numbers for the same cases |
| `level` | 2 | where the walk stopped, or the level changed (23.6.3) |
| `entry` | 32 | the entry there |

- **A request moves in a cycle in which `valid` and `ready` are both 1;** the client holds `valid` and the request
  until then (3).
- **A client has one request outstanding** (owner). Its port's `ready` is 0 from the cycle after its handshake
  until the cycle after its reply, and may be 0 at other times, for a bounded time.
- **Every request has exactly one reply,** in a cycle after its handshake, the next at the earliest.
- **The bus has no `ready`:** a client takes its reply in the cycle it is on the bus with the client's number. At
  most one reply a cycle; replies to different clients come in any order.
- **Every output is a flip-flop:** the cache's `ready`s and the bus, and the client's request.

#### 23.6.3 Look up, update and discard

**Look up.** The walk goes down from the root a level at a time, and **stops at the first entry that is a hole or is
not the file's own.** It reads index lines only, fetching those it lacks, and changes nothing.

| Case | `status` | `level` | `entry` |
| --- | --- | --- | --- |
| **found:** every entry on the path owned | `ok` | depth − 1 | the data page's entry, owned |
| **a hole** at level *L*, every entry above it owned | `hole` | *L* | 0xFFFF_FFFF |
| **not the file's own:** the entry at level *L* not owned, every entry above it owned | `not_owned` | *L* | that entry: the shared page's address, owned bit 0 |
| **beyond:** the page number is 2^(10·depth) or more, or the depth is 0 | `beyond` | 0 | 0 |
| **an LPDDR error:** a line on the path could not be fetched | `lpddr_error` | that line's level | 0 |

At `not_owned` nothing below level *L* is the file's own (23.5), so the walk stops there. This walk is not
`fill`'s: the Tracer's goes on through a shared page (23.6.4).

**Update.** The request's `level` *L* and `entry` *E*. The walk goes down to level *L* as a look-up does, checking
every entry above it; then the entry at level *L* becomes *E*, whatever it was.

| Case | `status` | `level` | `entry` | What is written |
| --- | --- | --- | --- | --- |
| **every entry above *L* owned:** the entry changed | `ok` | *L* | the entry as it was before | the line, through to LPDDR, before the reply (23.6.6) |
| a hole at level *k* < *L* | `hole` | *k* | 0xFFFF_FFFF | nothing |
| an entry not owned at level *k* < *L* | `not_owned` | *k* | that entry | nothing: a client's mistake cannot change a frozen file |
| beyond: the page number past the depth's reach, *L* ≥ depth, or depth 0 | `beyond` | 0 | 0 | nothing |
| an LPDDR error fetching a line on the path, the target's included | `lpddr_error` | that line's level | 0 | nothing |
| **the line's write answered with an error** | `lpddr_error` | *L* | the entry as it was before | the cache holds *E*; LPDDR's line is undefined |

- The index page holding the entry at level *L* is the file's own whenever every entry above it is, since the root
  is.
- An update changes 32 bits of a line, and writes the whole line to LPDDR: 8 beats, whole beats only (23.10).

**Discard a page.** The request's `root` is the page's address; its other fields are ignored. The reply is always
`ok`, level 0, entry 0.

- **Every line of the page in the cache is invalidated, written or not:** a clean line and a dirty one alike, and
  nothing is written to LPDDR. A page handed out again is discarded, not flushed: its old contents are dead
  (owner). The discard makes no LPDDR traffic.
- **A line with a write in flight** is not discarded until its write's response has come; the discard waits.
- **The reply comes when none of the page's 32 lines is in the cache.**

There is no flush (owner): updates write through, and nothing reads the Tracer's dirty lines in LPDDR *(item 178)*.

#### 23.6.4 The Tracer's port

`fill` is v1.36's, unchanged (3, 17.9.5); the cache is its other side. Its line is the file's line, line *l* & 31 of
page *l* >> 5 (23.5), its `data` the line's byte *b* at bits [8*b* + 7 : 8*b*].

**A read** walks (root, depth, line) to the data line, through every index line on the path, and answers with the
line:

| Case | `status` | `data` |
| --- | --- | --- |
| every entry on the path owned | `ok` | the line |
| a hole on the path | `hole` | zeros |
| an entry on the path not owned, none a hole: **the walk goes on through a shared page** | `not_owned` | the line |
| the line is 2^(5 + 10·depth) or more | `beyond` | undefined |
| **an LPDDR error fetching a line on the path** | `beyond`, and the memory system's `ERRORS.lpddr_error` is set | undefined |

`fill`'s read has no status for an LPDDR error: its four are used. Answered `beyond`, the Tracer sets
`ERRORS.page_missing` and stops (17.9.5), and the memory system's error says why (17.11). A depth-0 file is its root
alone: its line *l* < 32 is the root's line *l*.

**A write** walks the same path:

| Case | Response | What is written |
| --- | --- | --- |
| every entry on the path owned | `ok` | the line, into the cache whole, **marked dirty, and allocated without an LPDDR read** if it was not there |
| a hole on the path, an entry not owned, or the line beyond | `error` | nothing |
| an LPDDR error fetching an index line on the path | `error` | nothing |

- **The Tracer's writes stay dirty in the cache** until their line is evicted, and are then written back on the
  cache's AXI port (owner). They are never written through.
- **A write-back answered with an error** loses the line, and sets the memory system's `ERRORS.lpddr_error`: the
  Tracer had its `ok` long before.
- **What `fill` requires** (3) the cache keeps: every request answered in bounded time; a read of a line sees a
  write of it already accepted, since the line is in the cache, dirty, from the write's response on; nothing
  accepted before a reset answered after it.

#### 23.6.5 The controller: its registers and its states

**Its registers,** as the code that is the specification's authority holds them (`src/units/cache_ctl.h`), at
`LPDDR_ADDRESS_BITS` 32:

| Register | Bits | What it holds |
| --- | --- | --- |
| a look-up port's request register, × 4 | 135 | `valid`; `op` (2), `root` (31), `depth` (2), `page` (30), `level` (2), `entry` (32); three waits: **parked** (an update in the slot), **for the miss handler**, **for the slot**; **the failed line** (31) and its valid bit: a line whose fetch for this request failed |
| `fill`'s read register | 102 | `valid`; root (31), depth (2), line (35); the wait for the miss handler; the failed line and its bit |
| `fill`'s write register | 1,127 | as the read's, with the line's 1,024 bits and the wait for the slot |
| the front end | 7 | the round-robin pointer over the six sources (3); the source a miss was just handled for, and its valid bit |
| the walker | 1,358 | its state (2); the source (3); the level *k* (2); the discard's line count (6); the page at level *k* (31); the line being looked up (31); the hit way (3); whether every entry so far was owned; the line's dirty bit; a read pending; the line register (1,024); the set's tag row as read (216); a reply waiting for the bus: status (3), level (2), entry (32) |
| the miss handler | 1,321 | its state (3); the line to install (31) and its set (9) and way (3); the victim's tag (22), valid and dirty bits; whether it allocates without a read; its error bit; `arvalid`; a copy made; the source it serves (3); the beats in (4); the set's tag row (216); the line buffer (1,024) |
| the AXI writer | 1,070 | busy; the address taken; the beat (4); the line address (31); the line (1,024); a queue of 4 kinds, update or write-back, in the order of their responses, with its head and count |
| the update slot | 81 | valid; its source (3), set (9), way (3), level (2), line (31), and the entry before (32) |
| the error | 1 | sticky: an LPDDR error no client was told of (23.6.9) |
| the outputs | 1,259 | the four `ready`s; the reply bus (40); `fill`'s two `ready`s, its read response (`valid`, `status`, 1,024 bits of `data`) and its write response (`valid`, `status`); the AXI port's AR (`arvalid`, 25 bits of line address), AW (the same) and W (`wvalid`, `wdata`, `wlast`); the error |

**In all about 6,870 flip-flops** (6,866 as counted), five of them line registers of 1,024 bits: the request
register of `fill`'s write, the walker's line, the miss handler's buffer, the AXI writer's line and `fill`'s read
response. An implementation may share them where their uses never overlap.

**The front end.** A request taken at a source's port goes into that source's register, so its `ready` is 0 until
it has been answered (23.6.2). When the walker is idle it picks the next request: **first the source whose miss
was just installed or failed, then round robin** among the sources, from the one after the last picked, whose
request is valid and waits for nothing. An update waits while the slot is full. So every request is taken in
bounded time.

**The walker.** Its states:

| State | What it does in the cycle | Next |
| --- | --- | --- |
| idle | picks a request (front end): a discard's first line, or the first index line of a look-up's, an update's or a fill's path, and **reads its tags** (a request found beyond, 23.6.3, is answered at once instead) | compare |
| compare | **compares** the tag row read with the line's tag. A hit **reads the line** from the data memory, and touches the way | read, or a step below |
| read | the line is at the data memory's output: **the entry at the request's level** is examined, and the request answered, or **the next level's line's tags read** | compare, idle or reply |
| reply | the bus was taken this cycle by the update slot: the reply waits a cycle | idle |

A step is two cycles on hits, compare then read: a look-up of a depth-2 file whose two index lines are in the cache
is answered on the bus 6 cycles after its handshake (one to be picked, two levels of two, one for the bus).

In compare, besides the hit:

- **The miss handler's set.** While the miss handler works on this set, the request **waits for it** and is
  walked again from the top when the miss handler is idle: a **replay**. A walk changes nothing until its last
  step, so a replay is always safe.
- **A tag row read before the miss handler's write to the same set landed** is read again, a cycle later.
- **A miss** with the miss handler busy waits for it the same way. With it idle, the walker chooses **the victim**:
  the first invalid way of the set, else the way of the greatest age that is not the update slot's line. It gives
  the miss handler the line, the set, the way and the source, and the request waits for it. A request whose own
  fetch failed is answered at its replay, at that line: `lpddr_error` at its level for a look-up or an update,
  `beyond` with the error set for a fill read, `error` for a fill write.
- **A hit, for `fill`'s write at its data line:** the line is written whole and marked dirty, the way touched, and
  the write answered `ok` (unless the line is the slot's, which it waits for).
- **A discard's line:** a hit is invalidated (unless it is the slot's line, which waits for the response); after
  the page's 32nd line, the reply.

In read, by the request's operation: a look-up answers at a hole, an entry not owned, or the bottom (23.6.3); an
update goes on above its level, answering at a hole or an entry not owned, and **at its level** changes the
entry (23.6.6); a fill read goes on through a shared page, answers `hole` at a hole and answers with the data line
at the bottom; a fill write answers `error` at a hole or an entry not owned. Going on, the next level's line is the
entry's page plus the next entry's line: (entry & 0x7FFF_FFFF) + (*i* >> 5), or for the data line + (line & 31).

**Hits under a miss.** While the miss handler works, the walker serves other requests, as long as they hit and touch
no line of the miss handler's set. A request that misses waits for the miss handler.

**The miss handler,** one line at a time:

| State | What it does | Next |
| --- | --- | --- |
| idle | nothing | — |
| read the victim | a dirty victim: reads its line from the data memory | hand over |
| hand over | copies the victim into its buffer, and hands it to the AXI writer when the writer is free | wait |
| wait | waits for the write-back's response. An error loses the line and sets the cache's error | fetch, or install for an allocation |
| fetch | raises the line's read on AR, 8 beats | fill, at AR's handshake |
| fill | takes the 8 beats into its buffer, noting any error response | install, or fail with an error |
| install | writes the line and its tag row in one cycle: valid, dirty for a fill write's allocation and clean otherwise, the way touched; the requests waiting for it wake, the one it served first | idle |
| fail | invalidates the way; the request it served is answered at its replay (above); the others wake | idle |

A clean victim, or an invalid way, needs no write-back: a fetch starts at once, and a fill write's allocation goes
straight to install with its own line. **There is no list of pending writes** (owner): a dirty victim is written
back in its place, and the fetch that wants the place waits for the response.

**The AXI writer** writes one line at a time: AW, then the line's 8 beats a beat a cycle, then it is free for the
next, with the kinds of the writes outstanding in a queue of 4, whose responses come in that order (one ID, 23.10).
An update's response goes to the update slot, and a write-back's to the miss handler.

#### 23.6.6 An update, its sequence and what it guarantees

1. **The request is taken** at the client's port; the port is busy.
2. **The walk to the level:** a look-up in the cache memory for each index line on the path, a line it lacks
   fetched from LPDDR.
3. **The target line** is in the cache, at the walker's read state. The update waits while the AXI writer is busy.
4. **The entry is changed in the cache,** 32 bits of the line, by one write of its 128-bit lane, and **the line is
   the update slot's:** write in flight. It is never marked dirty.
5. **The whole line goes to the AXI writer,** and from it to LPDDR: an 8-beat burst.
6. **The update is parked,** and the walker serves other requests meanwhile.
7. **The write's response arrives.** The slot empties; the line is clean.
8. **The reply,** in the cycle after the response, the bus the slot's first: `ok`, or `lpddr_error` if the
   response was an error, with the entry as it was before. The client's port is free again.

**Nothing before step 7 can produce the reply.**

| Guarantee | |
| --- | --- |
| **write through** (owner) | an update's line reaches LPDDR before its reply. **Once a client has the reply, LPDDR holds the new entry:** a read raised after the reply sees it, through the cache or straight from LPDDR on any client's port |
| why a direct read sees it | because of the wait, not because of any order on the bus: the cache's write and a client's later read are on different ports, which AXI does not order (23.10) |
| **one update in flight** | in the whole cache. A second waits in its register, unpicked, until the first's reply |
| other requests go on | look-ups and the Tracer's requests are served while an update waits for its response. One that reads the slot's line sees the new entry, from the cache |
| the slot's line stays | it is not evicted, not discarded and not changed until its response |

#### 23.6.7 Timing

**A client may assume:** every request answered in bounded time; a reply in a cycle after its handshake, the next at
the earliest; an update's reply after LPDDR holds the new entry; its own requests answered in order, since it has
one at a time.

**A client may not assume:** any latency; a `ready` of 1 in any cycle, even with nothing outstanding; that a line
stays in the cache; an order between its replies and another client's; that a look-up sees, or does not see,
another client's update not yet answered.

**What the cache gives** `[model]`, idle, on the simulator's LPDDR at its defaults (23.12), in cycles from the
handshake to the answer:

| Request | Cycles |
| --- | --- |
| a look-up of a depth-2 file, both index lines in the cache | 6 |
| the same, both missing | 173 |
| an update whose path is in the cache: the walk, the line's write and its response | 87 |
| a fill read of a depth-3 file, all four lines in the cache | 10 |
| the same, all four missing | 351 |
| a fill write that hits; one that allocates, with no LPDDR read | 9; 17 |

- **A hit costs 2 cycles a level;** v1.36's model of the memory system charged 6 (v1.36's 23.6).
- **A miss whose victim is dirty waits for the victim's write first,** about 80 cycles more. On the proof runs a
  segment that writes much is 3 % to 4 % slower than on v1.36's model, and one that mostly hits up to 2.7 % faster
  `[meas: v1.37's simulator]`. Writing a set's dirty least recently used line back early, in place, would recover
  it and keeps no list *(item 179)*.

#### 23.6.8 What the cache asks of its clients

| Rule | |
| --- | --- |
| **the data is in LPDDR before the index names it** | a client writes a page on its own AXI port, waits for that write's response, and only then sends the update that names it. Whoever sees the new entry then finds the page behind it |
| **one writer for a file at a time** | two clients updating one index page would each read it directly while the other's update was in flight. The file system keeps it from happening; the cache does not check |
| **an owned index page changes only by `update`** | a client writes an index page whole, on its AXI port, only while the page is new: named by no entry yet, and with no line of it in the cache (never used, or discarded since its last use). A page from the free page queue (17.10) is new; so is a root the Updater makes, which does not come from that queue |
| **a page is used only after its discard's reply** | so a write-back of an old dirty line cannot land on its new contents. In v1.37 nothing is freed, so no page is handed out twice and nobody discards; who discards with the file system is *item 180* |
| **a data page the Tracer reads is not written directly while it does** | the Updater moves data pages on its own port, and the Tracer reads them through the cache; they never meet on a page (owner) |
| **a page the Tracer has written is not read directly** | its newest lines may be in the cache alone, dirty |

Breaking one is UB (1.3): the firmware that hands out the pages and starts the runs is what keeps them. The
simulator catches the third and the fifth, by comparing every clean line the walker reads with LPDDR (23.6.11). **An update answered `hole` or `beyond`** cannot come to a client that looked up
first, so a client may treat one as fatal.

#### 23.6.9 Errors

| What | The client's answer | The memory system's `ERRORS.lpddr_error` (17.11) |
| --- | --- | --- |
| an index line on a look-up's or an update's path fetched with an error | `lpddr_error` | — |
| an update's line written with an error | `lpddr_error`; LPDDR's line undefined, the cache's holds the new entry | — |
| a line on a fill read's path fetched with an error | `beyond` | set |
| an index line on a fill write's path fetched with an error | `error` | — |
| a dirty line's write-back answered with an error | — (the line is lost) | set |

The error is sticky until the reset (17.11), and raises the memory tile's error line, so the toast pin (17.6). A
client's own error, when it is told, is the client's: the Updater's `ERRORS.lpddr_error` (17.10), the Tracer's
`ERRORS.page_missing` or `write_back_error` (17.9.5). Every one is a hardware fault, H (7.4), but for an address the
device does not have, which a program error made.

#### 23.6.10 Reset

The reset (18) empties the cache: every tag row invalid, its ages 0 to 7 by way; every request register, the walker,
the miss handler, the AXI writer, the slot and the error cleared; every `ready` 1 and every response output 0.
**It is empty at the reset and at no other time:** every line it holds is current, so nothing makes it cold between
segments. Nothing accepted before a reset is answered after it.

#### 23.6.11 The simulator's checks and parameters

- **The stale-line check:** every clean line the walker reads, but the slot's, is compared with LPDDR, and a
  difference stops the simulator. It catches a client that broke 23.6.8: a page written directly while it was in the
  cache, or an index page changed other than by `update`. A line whose update's write failed is left out.
- **Every dirty line written back** must be one a fill write made.
- A look-up port's `op` 3, an R beat the miss handler did not ask for, a line of other than 8 beats, a B response
  with no write outstanding: each stops the simulator.
- **Settings:** `hits_under_miss`, on (the specification's); off, the front end takes nothing while the miss
  handler works, a setting for comparison only.
- **What it counts:** hits and misses by class (the memory file's index and data lines, the map's, the look-up
  ports'), evictions, write-backs and their errors, fetch errors, allocations, updates, discards and the lines they
  dropped, replays, the requests served under a miss, and each kind's latency.
### 23.7 Errors

- **A request for a reserved target** (3–7) is dropped at the root (11.2), with nothing recorded
  *(Open, item 126: whether the hardware records it; the RTL records nothing)*. The simulator flags it.
- **An `op` the target does not define is UB** (11.5). v1.35 had the memory system drop such a request and raise
  one of its peripheral's errors; the RW engine knows nothing of requests (17.7), so the check, if any, is the
  memory tile's firmware's.
- **An LPDDR response that is not OKAY** sets the `ERRORS.lpddr_error` of the unit it was for: the RW engine's
  (17.7), the Updater's (17.10), or the memory system's for the cache's errors that no client is told of (23.6.9).
  Each raises its tile's error line and so the toast pin (17.6). A job past LPDDR's end is UB (17.7).
- **Every error stops the chip.** What follows, the host's reset and the next proof, is not designed in v1.37
  *(item 181)*.
- **A dropped or unanswered read never lands,** so its tile waits until the master tile's watchdog expires (17.6).

### 23.8 No per-tile pacing

The memory tile may deliver to a tile at up to one packet a cycle: the tile's eject serves arriving packets
before its loopback queue (16.3), so its eject queues drain as fast as the delivery fills them, and the
loopback, which the tokens protect (14.2), waits. Other traffic to a receiving tile comes on top and is the
program's to pace (16.3).

### 23.9 What a program must keep to (invariant 17, the request contract)

- arm the counters for a request's data and its reply before sending it, and name them, with the reply's address,
  in the request (11.4);
- at most 1, 3 and 1 requests outstanding to the master, memory and tracer tiles (11.3); error reports excepted;
- write a part of the write buffer only when the memory tile's reply directs it, exactly `beats` packets, one
  writer in flight or paced (23.4);
- **no `READ` outstanding and no write into the write buffer in flight across a group transfer**, so the memory
  tile's traffic never meets the all-to-alls' schedule;
- no read-unit reads outstanding while deliveries arrive (invariant 14)
  *(Not stated by the RTL's specification; kept from v1.35; item 116)*;
- deliveries into DMEM inside DMEM, clear of its reserved first 16 bytes (6.3) and of what the image keeps
  (invariant 11).

### 23.10 The path to LPDDR: a client's port

Every unit that reads or writes LPDDR is a **client** of the AXI arbiter (23.11), on a port of its own, an
`lpddr_client` interface (3). The arbiter is the only thing on the chip's port to the LPDDR controller, `lpddr`
(23.12).

| Client, and its ID on `lpddr` | Unit | What it moves |
| --- | --- | --- |
| 0 | the cache's controller (23.6) | lines: a fetch, a dirty line's write-back, an update's write-through |
| 1 | the Updater (17.10) | pages and half pages, read and written |
| 2 | the RW engine (17.7) | its jobs' bursts |
| 3 | none | its port's inputs are held at 0 |

- **A client has one number everywhere:** its ID on `lpddr` and, for a client of the cache, its look-up port's
  number (23.6.2).
- **The clients' ports are one design,** the arbiter's, whichever unit is on them. The RW engine's `lpddr` port of
  v1.36 is an `lpddr_client` port unchanged: its one address and length register drives both AR and AW (17.7).

**The signals,** AXI4's (ARM IHI 0022) by its names; the client is the master and the arbiter the slave:

| Channel | Signal | Bits | From | Meaning |
| --- | --- | --- | --- | --- |
| AR | `arvalid`, `arready` | 1, 1 | client, arbiter | |
| | `araddr` | `LPDDR_ADDRESS_BITS` | client | the burst's first byte, a multiple of 16 |
| | `arlen` | 8 | client | the burst's beats less one, 0 to 255 |
| | `arsize` | 3 | constant 4 | 16 bytes a beat |
| | `arburst` | 2 | constant 1 | INCR |
| R | `rvalid` | 1 | arbiter | |
| | `rready` | 1 | constant 1 | the client takes every beat in the cycle it comes |
| | `rdata` | 128 | arbiter | the beat at byte address *a*: LPDDR's bytes *a* to *a* + 15, byte *b* at bits [8*b* + 7 : 8*b*] |
| | `rresp` | 2 | arbiter | 0 `OKAY`, 1 `EXOKAY`, 2 `SLVERR`, 3 `DECERR` |
| | `rlast` | 1 | arbiter | the burst's last beat |
| AW | `awvalid`, `awready`, `awaddr`, `awlen`, `awsize`, `awburst` | as AR's | | a write burst's address |
| W | `wvalid`, `wready` | 1, 1 | client, arbiter | |
| | `wdata` | 128 | client | as `rdata` |
| | `wstrb` | 16 | constant all ones | every byte of every beat written |
| | `wlast` | 1 | client | the burst's last beat |
| B | `bvalid` | 1 | arbiter | |
| | `bready` | 1 | constant 1 | the client takes every response in the cycle it comes |
| | `bresp` | 2 | arbiter | as `rresp` |

- **AXI4's other signals are not on the port** and take their defaults: no ID signals, and no lock, cache,
  protection, QoS, region or user signals. So no access is exclusive, and an `EXOKAY` is an error response.
- **Every output is a flip-flop or a constant,** the client's and the arbiter's. A `valid`/`ready` pair moves one
  item in a cycle in which both are 1 (3).

**A burst** is one AXI transaction: an address, then its beats, and for a write its response.

| Rule | |
| --- | --- |
| type | INCR, 16-byte beats |
| length | 1 to 256 beats |
| address | a byte address, a multiple of 16, below 2^`LPDDR_ADDRESS_BITS` |
| boundary | **no burst crosses a 4 KiB boundary** |
| a write's beats | exactly `awlen` + 1, `wlast` on the last and on no other |
| a write's pace | once a client's write address has been taken, it offers that burst's beats one a cycle, `wvalid` 1 from the first to the last, waiting for nothing but `wready`. It may offer them before the address is taken |
| outstanding | at most `CLIENT_BURSTS` bursts in each direction; the arbiter holds `arready` or `awready` at 0 beyond, so a client needs no count of its own |

A burst that breaks one of the first five rules is a fault of the client's design; the simulator stops on it
(23.13). A client that pauses inside a write burst holds up every client's writes (23.11).

**The sizes in use,** each aligned to its own size, so none crosses 4 KiB:

| Burst | Bytes | Beats | Aligned to | Used by |
| --- | --- | --- | --- | --- |
| a line | 128 | 8 | 128 | the cache: a fetch, a write-back, an update's write-through |
| a half page | 2,048 | 128 | 2,048 | the Updater |
| a page | 4,096 | 256 | 4,096 | the Updater |
| any other | 16 *n* | *n* ≤ 256 | 16 | the RW engine's jobs (17.7) |

**What a client may count on, and what it may not:**

| A client may count on | A client may not count on |
| --- | --- |
| every burst answered in bounded time: each beat of a read, and a write's response | any latency |
| its reads answered in the order it raised them, and its writes likewise | any order between a read and a write, its own included, even of the same bytes |
| a write's response once LPDDR holds the write: a read raised after the response sees it, on any client's port | any order against another client's bursts |
| a read's beats in address order, `rlast` on the burst's last alone | a burst's beats back to back: other clients' beats come between |
| its `arready` and `awready` rising again in bounded time | a `ready` in any given cycle |

- **A client that reads what it, or another client, wrote** raises the read only after the write's response. The
  cache's update holds its reply until its write's response for this reason (23.6.6).
- **An error response** is an `rresp` or `bresp` other than `OKAY`. A read beat with one carries undefined data,
  and its burst still runs to its `rlast`; a write answered with one leaves LPDDR's bytes under the burst
  undefined. What a client does about it is its own: 17.7, 17.10 and 23.6.9 say.

### 23.11 The AXI arbiter

The AXI arbiter chooses whose request goes to the LPDDR controller next, and steers each beat and response back. It
holds no data but a write's beats in passing, and has no registers in any window: it sits in the memory system,
the memory tile's peripheral 1 (17.11), with the cache.

**Pieces.** A burst of at most `TURN_BEATS` beats goes to the controller as it is, one **piece**. A longer one is cut:
a burst of *n* beats at byte address *a* is ⌈*n* / `TURN_BEATS`⌉ pieces, piece *k* at *a* + 16·`TURN_BEATS`·*k*
of min(`TURN_BEATS`, *n* − `TURN_BEATS`·*k*) beats. At the default, 32, a line is one piece, a half page 4 and a
page 8. Every piece carries its client's number as its ID, and none crosses 4 KiB, since its burst does not.

**What a client sees of the cutting:** nothing but time.
- **A read:** its burst's beats in address order, each with the `rresp` the controller gave it; `rlast` on the
  burst's last beat alone, the pieces' other `rlast`s dropped.
- **A write:** it sends *n* beats with `wlast` on the *n*th; the arbiter sends them on as the pieces' beats, with
  `wlast` on each piece's last. The client gets one response, when every piece's has come: `OKAY` if every piece's
  was, else the first piece's, in the order the responses came, that was not.

**Its registers,** counted at the default parameters, `LPDDR_ADDRESS_BITS` 32 among them, and at the signals'
widths:

| Register | Each | Bits | What it holds |
| --- | --- | --- | --- |
| AR slot | client | 1 + 28 + 9 | the burst being cut: valid; its next piece's beat address (the byte address's bits [31:4]); the beats left to cut, 1 to 256 |
| AW slot | client | 1 + 28 + 9 | the same for a write |
| read list | client | `CLIENT_BURSTS` × 4, + 2 + 3 | the outstanding read bursts, oldest first, each with the pieces whose last beat has still to come (0 to 8); its head and count |
| write list | client | `CLIENT_BURSTS` × (4 + 2), + 2 + 3 | the outstanding write bursts, each with the pieces whose response has still to come and the response so far |
| the client's outputs | client | 138 | `arready`, `awready`, `wready`, `rvalid`, `rdata`, `rresp`, `rlast`, `bvalid`, `bresp` |
| the round-robin pointers | 2 | 2 each | the last client served, on AR and on AW; client 3 at the reset |
| the write piece in turn | 1 | 1 + 2 + 6 + 1 | valid; its client; the beats still to take from the client, 1 to 32; whether it is its burst's last piece |
| the W queue | 1 | 2 × 129, + 1 + 2 | two beats, each with its `wlast`, on their way to the controller; its head and count |
| the outputs to `lpddr` | 1 | 39 + 39 + 130 | AR's and AW's `valid`, address (28 bits), `len` and ID; W's `valid`, `wdata`, `wlast` |

**In all: 1,539 flip-flops** at four clients, `CLIENT_BURSTS` 4 and `TURN_BEATS` 32 (264 a client, 483 shared).

**A cycle.** Every output is a register, so an address, a beat and a response each take one cycle through the
arbiter. At the edge that ends cycle *t*, in this order:

1. **The controller's side of *t*.** A W beat the controller took leaves the W queue. An R beat goes to the client
   its `rid` names, in *t* + 1; at its piece's `rlast` that burst's count of pieces falls by one, and when it
   reaches 0 the beat is the burst's last, given `rlast`, and the burst leaves the read list. A B response counts
   down its burst's pieces the same way and keeps the first response that was not `OKAY`; at the last, the
   client's `bvalid` and `bresp` are set for *t* + 1. A beat or a response whose ID has nothing outstanding is a
   fault of the controller's; the simulator stops on it.
2. **The clients' side of *t*.** A burst taken on AR (`arvalid` and `arready` both 1) fills the client's AR slot
   and joins its read list; one taken on AW fills the AW slot and joins the write list. A W beat taken (only the
   client of the write piece in turn has `wready`) goes into the W queue, and the piece's beats still to take fall
   by one; at 0 the piece is no longer in turn.
3. **The AR turn.** Unless the AR output still holds an address the controller has not taken, the turn goes to the
   first client after the AR pointer whose AR slot holds a burst. Its next piece goes on the AR output for *t* + 1,
   the slot's address and count move on, the slot empties when nothing is left to cut, and the pointer moves to
   that client. **At most one piece a cycle,** whatever its length.
4. **The AW turn.** Unless the AW output still holds an address not taken, **or a write piece is still in turn,**
   the turn goes the same way on AW, and that piece becomes the write piece in turn. So the arbiter passes **one
   write piece at a time:** its address, then all its beats from its client, and only then the next turn. W beats
   reach the controller in the order of their addresses, as AXI4 requires.
5. **The outputs of *t* + 1.** The W queue's oldest beat is offered on W. A client's `arready` is 1 when its AR
   slot is empty and its read list holds fewer than `CLIENT_BURSTS` bursts, and `awready` likewise. Its `wready` is
   1 when its piece is in turn and the W queue holds at most one beat.

- **A burst taken in *t*** has its first piece's address on `lpddr` in *t* + 1 at the earliest, since steps 2 and 3
  are one edge. A burst of at most `TURN_BEATS` beats leaves its slot at once, so its client's `arready` or
  `awready` is 1 again in *t* + 1 if its list has room.
- **A long burst's pieces** take one turn each. A client moving a page alone has its eight turns in a row; with
  another client waiting, the two alternate, so a line waits behind at most one piece of each other client.
- **The arbiter is fair and never deadlocks:** each turn goes round, a slot is served within `LPDDR_CLIENTS` turns
  of its direction, and the only waits are on the controller's `ready`s and, on AW, on the client in turn sending
  its beats.

**Errors.** The arbiter raises none and checks nothing in hardware: a client's error responses are passed through,
and the client acts on them.

**Reset.** The reset (18) empties the slots, the lists, the W queue and every output; `arready` and `awready` are 1
on every client's port, and nothing is offered on `lpddr`; the pointers name client 3, so client 0 is served
first. The clients, the arbiter and the controller's port are reset together: nothing accepted before a reset is
answered after it (23.12).

### 23.12 The LPDDR controller's port, and the simulator's LPDDR

**`lpddr`,** the chip's port to the LPDDR controller and its PHY, outside the chip (3): the arbiter is its master,
and it is a client's port (23.10) with IDs added (*Asked of the RTL: v1.36's `lpddr` has one ID, RCR comment A*):

| Channel | Added signal | Bits | Meaning |
| --- | --- | --- | --- |
| AR | `arid` | `ID_BITS` | the client's number |
| R | `rid` | `ID_BITS` | whose beat: the arbiter steers it by this |
| AW | `awid` | `ID_BITS` | the client's number |
| B | `bid` | `ID_BITS` | whose response |

- There is no `wid`: AXI4 has none, and the W beats follow the order of the write addresses.
- `rready` and `bready` are 1: the arbiter takes every beat and response, and its client takes it in turn.
- A piece is at most `TURN_BEATS` beats, so `arlen` and `awlen` are at most `TURN_BEATS` − 1 here.
- **One address space:** the controller drives both of the package's LPDDR4X channels as one memory, of
  2^`LPDDR_ADDRESS_BITS` bytes at most.

**The controller's rules,** what the arbiter and through it every client rely on:

- it answers every burst in bounded time;
- it answers the reads of one ID in the order it took them, and the writes of one ID likewise;
- between IDs it may answer in any order, and may interleave the read beats of different IDs beat by beat;
- it sends no write response before both the write's address and its last beat have been taken, and none before
  LPDDR holds the write: a read taken after the response sees it;
- it answers nothing accepted before a reset after it.

The controller's own policy and figures come with the controller chosen; nothing in a client depends on them.

**The simulator's LPDDR.** LPDDR and its controller are not the chip's: `lpddr` is the simulator's boundary, and a
model stands behind it. Its values are the model's, not promises (19.1).

- **One memory.** The model's memory is the file store's pages, so the images the Updater writes, the lines the
  cache reads and the Tracer's files are one memory. A read of bytes nothing has written returns zeros and is
  counted as a fault of the run: a test fails on it.
- **Reads.** A read burst's first beat comes `read_latency` cycles after its address's handshake at the earliest,
  then a beat a cycle, back to back. When a burst ends, the next is the oldest of one ID, the IDs taken round robin
  among those whose oldest read is due.
- **Writes.** W beats are taken a beat a cycle in the order of the write addresses, once that write's address has
  been taken. A write's response is due `write_latency` cycles after the later of its address's handshake and its
  last beat's; responses go one a cycle, the earliest due first, in order within an ID.
- **Limits.** At most `max_reads` read bursts and `max_writes` write bursts taken and not answered; beyond,
  `arready` or `awready` is 0.
- **When the memory changes.** A write's beats reach the memory at the edge before the cycle its response is sent,
  and a read beat takes its data at the edge before the cycle it is sent, a write first in the same cycle. A read
  and a write of the same bytes that overlap may see either; only the wait for the response orders them.
- **Two modes.** *Fixed*, the default: every `ready` 1 but at a limit, every latency as above. *Jitter*, with a
  seed: each burst's latency drawn from `jitter_lo` to `jitter_hi`, each `ready` 0 one cycle in four, and R's
  choice among the IDs that are due at random; every rule above is kept. It catches a client that counts on a
  latency or on an order between a read and a write.
- **Faults.** A test sets up to 8 byte ranges, each with a direction (reads, writes or both), a response (`SLVERR`
  or `DECERR`) and a number of bursts (0: every one). Every burst that touches one answers that response until the
  number is used: each beat of a read, its data 0xDEADBEEF repeated; a write on its response, writing nothing. A
  burst past 2^`LPDDR_ADDRESS_BITS` is answered `DECERR` the same way.

**What a line costs,** at the model's defaults with nothing else running: a client's line read taken in cycle *t*
has its first beat at the client in *t* + 72 and its last in *t* + 79; a line write taken in *t*, its beats
offered at once, has its response at the client in *t* + 80. These are the model's, a guide for a client's
designer and not a promise.

### 23.13 The memory system's checks and parameters

**What the simulator checks** on the arbiter's and the model's ports (each a fault of a unit's design, on which it
stops, not a program's UB):

- a burst whose address is not a multiple of 16, that crosses 4 KiB, or whose size or type is not 16 bytes and
  INCR;
- a W beat whose `wlast` is not on its burst's last beat, and one with no write address waiting for it;
- a beat or a response for an ID with nothing outstanding, and an ID that is no client's;
- a request withdrawn, or changed, while it waited for its `ready`, on any channel of `lpddr`.

**The parameters,** the machine's in 19 and the model's in 19.1:

| Parameter | Value | Meaning |
| --- | --- | --- |
| `LPDDR_CLIENTS` | 4 | the arbiter's client ports (23.10) |
| `ID_BITS` | 2 | an ID on `lpddr`: log₂ `LPDDR_CLIENTS` |
| `TURN_BEATS` | 32, from 1 to 256 | a piece's most beats (23.11) |
| `CLIENT_BURSTS` | 4, from 1 to 16 | a client's bursts outstanding in each direction (23.10) |
| `read_latency`, `write_latency` | 70 each (the model's) | 23.12 |
| `max_reads`, `max_writes` | 16 each, from 1 to 64 (the model's) | 23.12 |
| `jitter`, `jitter_lo`, `jitter_hi`, `seed` | off, 20, 200, 1 (the model's) | 23.12 |

**What it counts,** per client, for the reports: the bursts and pieces taken in each direction, and the cycles a
slot held a piece that did not get its turn; per model, the bursts and beats each way, the bursts answered with an
error, the reads of unwritten bytes, and the cycles with a beat on R and on W.

## 24. Logic budget (per tile, 12 nm estimates)

The SRAM is about 75 % of the die; the remaining 25 % is about 1.5 mm² per tile for everything else. Every module
is charged here, and additions must fit. The estimates exclude the scratchpad's banks themselves.

**Every figure in mm² is v1.35's `[estimate]`,** rough (±50 %) and unreviewed, made for v1.35's hardware: 256-bit
stored packets, bank request queues, a 128-entry request queue, an attach port on every tile. Where v1.36's
hardware differs, the line says how, and its bit counts are derived from v1.36's sizes `[derived]`; the areas are
to be redone. The RTL's (Snarky One's) synthesis results are on another process and are not signoff, so they are
not used here.

| module | state | v1.35's estimate, and what changed |
|---|---|---|
| `router` (16): 17 queues of 16 packets (4 link outputs × 2–4 queues, the eject × 5), the route blocks, the arbiters; the links' register stages | 17 × 16 × 182 bits ≈ 50 Kbit of flops at 8 × 8 | ≈ 0.11 mm² at 256 stored bits (70 Kbit). v1.36 stores a packet's defined bits only, 182 at 8 × 8 (4.1) |
| the loopback queue at `LOOPBACK_QUEUE_DEPTH` ≥ 64 (16.3): 48 entries more than a router queue, as flops or a small two-port SRAM | ≈ 9 Kbit | ≈ 0.015 mm² at 256 stored bits (12 Kbit) |
| an attach port (16.1), on an attach tile only, one per special tile attached: a route block, its queues at each output it reaches and at its own output, wider arbiters | | 0.04–0.07 mm² in v1.35, which charged it to every tile; v1.36 builds it on the attach tiles alone (0, 7 and 15 at 8 × 8) |
| `receive_unit` (12): the traffic director with its address decode and the `LAND_LATENCY`-stage delay line; the counters with their add ports; the write engine's pipeline, its augment and count logic | | the director was in v1.35's router line; the write engine ≈ 0.007 mm², with an input FIFO that v1.36 does not have (12.4) |
| `inject_unit` (14) and the scratchpad's routing (15): the read arbiter, the tokens, the return queues of per-read address and header, per-word write enables, routing from the clients to the banks and back | | ≈ 0.015–0.03 mm² in v1.35, with eight bank request queues of about 165 bits an entry; v1.36 has no bank queue (14.1, 15) |
| `dma_unit` (8): the read-ahead queues (`READ_AHEAD` × about 100 bits: a `STREAM` and a `GROUP` entry), a rotation or element counter (18 bits) and its comparator, the element-address shift-add (21 bits), the relative-destination adder, the transfer's state | | ≈ 0.011 mm² |
| `read_unit` (10): the read queue, `READ_QUEUE_DEPTH` (64) entries of 58 bits (address, 32-bit destination, size, counter), its registers | ≈ 3.7 Kbit | ≈ 0.005 mm² at v1.35's 43-bit entries (2.8 Kbit) |
| `gather_unit` (13): the gather queue, `GATHER_QUEUE_DEPTH` (64) entries of an ordinal and a counter field (23 bits), the address multiply-add (*k* ≤ 8: a shift-add), the per-value counter | ≈ 1.5 Kbit | ≈ 0.004 mm² at v1.35's 16 entries of 64 bits (1 Kbit) |
| the tables (8.6, 12.5): `STREAM_SRC_ADDR`, `STREAM_DEST_ADDR`, `STREAM_HDR` (256 × 21, 32 and 32 bits), `GROUP` (64 × 38 bits), `PTR` (256 × 21) and `HIST` (256 × 17) with their read-modify-write pipelines and bypass | ≈ 34 Kbit of SRAM | ≈ 0.017 mm², with two 16-entry request FIFOs that v1.36 does not have (12.5) |
| `core` (6): RV32I + C + Zmmul + Zicsr + Zicond with `WFI`, the CSRs, the trap logic and the coprocessor port; IMEM and DMEM, 32 KiB each, each with its NoC port (one 128-bit write a cycle); the boot ROM | | ≈ 0.15 mm² for the core, its memories and its registers together, without C, Zicond or the coprocessor port |
| `register_bus`, `tile_control_unit` and `write_unit` (5, 7, 9): the decode of region 0xFF; `PARAMS`, `STATUS`, `ERRORS`, the three event registers, `ARRIVE`, the arrival gate, the error line; the write unit's registers and holding register | | in v1.35's controller line, and 0.005 mm² for the barrier, the gate and the flags |
| `request_unit` (11): the request queue, `REQUEST_QUEUE_DEPTH` (4) × 128 bits, the `REQUEST_OUT` registers, the 16-bit serializer; on a special tile, its receive queue (below) | ≈ 0.5 Kbit on a compute tile | ≈ 0.008 mm² in v1.35, with a 128 × 128-bit queue (16 Kbit) |
| the peripherals' interface (17.1–17.3): the three windows' decode, the tile's live event and error bits, the write ports; a DMA only where a peripheral sends, on a special tile (17.4) | | ≈ 0.003 mm² |
| the replayer (17.8), peripheral 1 on every compute tile: ALU and comparator, the 128 × 64-bit true dual-port register file, two adders, the operand resolution, the decode index, four queue readers and credit counters, three decoders and their size counters; with its memories (queues, ROM, decode table, register file, output queue) | ≈ 256 Kbit of SRAM | ≈ 0.09–0.14 mm² (`spec/jolt/replayer.md`) |
| **total per compute tile** | | **≈ 0.52–0.61 mm² in v1.35,** with the replayer and an attach port, about 35–40 % of the per-tile logic budget. v1.36's changes all reduce it but the core's two extensions and the read and gather queues' entries. Still inside 1.5 mm² |
| the special tiles (2.1) | tile instances with the compute tile's scratchpad, `SCRATCHPAD_BYTES`, each a tile's logic plus its peripheral and, with a sending peripheral, its DMA | not estimated here. v1.35 gave them 2 MiB of big SRAM each |
| once per die: the request tree's nodes (11.3), `MESH_SIZE` row nodes and the root, a 16-byte buffer per input, their arbiters and the root's credit counters; the three receive queues, 128, 256 and 128 × 128 bits at 8 × 8, each in its receiving tile's request unit; the tree's wiring (18 wires a link: 16 data, valid, ready) and a credit wire from each receiving tile | ≈ 74 Kbit | ≈ 0.03 mm² plus wiring |

Rule of thumb: tables go in SRAM (cheap); comparators, multiplexers and per-entry flop state are what spend the
budget. The tile is dominated by the scratchpad: at 0.75–0.9 MiB/mm², 4 MiB is about 4.5–5.3 mm². With the logic
and the link channels, a tile is roughly 6–7 mm² at 4 MiB `[estimate]`. The banks' density at one access a cycle
awaits memory-compiler numbers (item 1).

## 25. Physical design: the control island

A default arrangement, not a contract: what the latencies of chapter 19 were chosen to allow. Sizes are v1.35's
`[estimate]`, at 12 nm, to be redone with chapter 24.

All the tile's small memories and its registers cluster around the core. The units and the router sit between
this **control island** and the scratchpad and the links.

- **In the island:**
  - the core;
  - its memories, IMEM and DMEM (32 KiB each), and the boot ROM;
  - the tables (`HIST`, `PTR`, the three `STREAM` arrays, `GROUP`);
  - `register_bus` and the tile window's registers.

  Rough sizes: about 0.017 mm² for the tables, 0.14 mm² for the 64 KiB of IMEM and DMEM, 0.01 mm² of core logic.
  The island is **about 0.17–0.2 mm², a patch 0.4–0.45 mm across** `[estimate]`. Wires inside it cost a few tens
  of ps.
- **The tables go by the core, not the units.** The units' table traffic (the `PTR` and `HIST` read-modify-writes
  with bypass, the `GROUP` and `STREAM` read-ahead) is pipelined and tolerates latency; the core's loads do not.
  The units pay the distance: `TABLE_LATENCY` (8, 12.3).
- **Configuration registers** live in the island although the units use them. They are static while they are
  used, so the long wires are multi-cycle paths, or the units keep local copies.
- **Status bits** are registered at their source in each unit and cross to the island with one flop (`status`,
  3).
- **The events** are registers in the island; the units' event pulses cross like status bits, with one flop.
- **Not in the island:**
  - everything the units touch every cycle other than the tables: the address generator's read-ahead queues,
    the inject unit's return queues, the write engine's pipeline, the router's queues;
  - the gather unit and the read unit's queue, which sit by the scratchpad.
- **The request unit** sits at the island's edge: its registers in the island, its request queue beside them, and
  its `request_link` leaving for its row node, which sits in the channels of its row of tiles. The root and its
  credit counters sit by the master tile. A receive queue sits in its receiving tile's request unit, at the end
  of a `request_link` from the root, with a `request_credit` wire back (11.3). The tree's placement and its links'
  pipeline stages are *(Open)* (item 61).
- **Paths that cross the gap:** the units' table accesses, and the traffic director's writes to the NoC ports of
  IMEM and DMEM (`WRITE_PORT_LATENCY`). A peripheral (the replayer) sits beside the traffic director, outside
  the island, with its memories as SRAMs, and the write path to it is pipelined within `PERIPHERAL_LATENCY`
  (17.3; item 48; the replayer's placement against the materializer is open with item 59). Its window's loads
  and its live event and error bits cross to the island like status bits. A sending peripheral's DMA sits by its
  output queues and its router input (16.4). All are pipelined and tolerate latency, so the island costs no
  throughput.
- **The scratchpad:** at the default size, eight 512 KiB banks (128 bits × 32K rows each) around the logic, with
  the link channels between them. Their arrangement awaits the memory compiler's numbers (item 1).

## 26. Open items

What is open, proposed or deferred in v1.36's design, each with the section it affects; possible later additions
are marked *later*. The replayer's and the Tracer's items are in `spec/jolt/replayer.md` and `spec/jolt/tracer.md`.
Numbers are v1.35's and are never reused; items 160 and up are new in v1.36 (174 to 177 are the Tracer's). Every earlier item's history is
`spec/history/machine-spec-v1.3-section-13.md`, and the decisions are in `spec/history/`.

v1.36 takes the RTL's (Snarky One's) hardware. Where that hardware settles an item, the item is closed below,
the way v1.35 closed its own, and where it bears on one without settling it, the status says *informed* and the
row says how. Chapters 1 to 19 mark an open subject *Open* in place; each is an item here.

| item | what is to be settled | section | status |
|---|---|---|---|
| 1 | Memory-compiler numbers for a scratchpad bank's SRAM: latency, density, arrangement. The RTL builds one single-port SRAM per bank of at most 512 KiB, on placeholder macros | 15 | open, informed |
| 7 | Gapless one-word exchanges: simulate them, then tighten the gap if queues stay low. The gap is now one per transfer, `DMA_CONFIG.gap`, 0 allowed | 8.1, 8.3 | open, informed |
| 8 | The physical latencies of the three global nets, `ARRIVE_LATENCY`, `RELEASE_LATENCY` and `ERROR_LATENCY`, settled together. They are the chip's parameters; the RTL's chip holds wires and one flip-flop (19.1) | 18, 19 | open, informed |
| 10 | Above the chip: cards, FPGAs, links and each chip's DRAM are not specified | 2 | open |
| 13 | The sort's output format (keys carry the receive-buffer ordinal) | 27 | open |
| 24 | Re-measurement: labels in this document and in `spec/schedule.md` still say "to be re-measured"; with item 164 | all | open |
| 61 | The request tree's physical design: node placement and the links' pipeline stages. The RTL has one register a node, cut-through, and wires for links until there is a floorplan | 11.3, 25 | open, informed |
| 107 | 4.25 MiB of scratchpad per tile, a tentative working figure. `SCRATCHPAD_BYTES` is a power of two, which cannot express it | 5.1, 15, 19 | open: needs a decision |
| 116 | Read-unit reads overlapping incoming traffic: revisit the rule and invariant 14. The RTL's specification states no rule; v1.35's is kept | 10, 20 | open |
| 124 | The hard and the soft reset, the soft reset's keep list, the RW engine's and the LPDDR controller's among them. The machine has one reset; the `lpddr` port's contract (3) constrains a soft one | 18, 17.7 | open |
| 128 | `LAND_LATENCY`'s margin, validated across workloads. The RTL's slowest write path is 2·`TABLE_LATENCY` + 2 cycles after the acceptance, so 8 covers `TABLE_LATENCY` ≤ 3: the figure v1.35 asked the RTL for. The validation on the simulator is left | 12.3, 20 | open, informed |
| 160 | **A peripheral DMA's packets carry the tile's own id as src** (16.4), where item 118 decided src = the id + 8 (owner, 2026-11-05). *(Reversed in v1.36, for the owner to rule on: see `spec/CHANGES-v1.35-to-v1.36.md`)* | 16.4, 17.4 | open: reverses a ruling |
| 161 | **The Tracer's `EVENTS` and `ERRORS` are packed from bit 0** (17.9.1), where item 144 kept the positions shared with the replayer. *(Reversed in v1.36, for the owner to rule on: see `spec/CHANGES-v1.35-to-v1.36.md`)* | 17.9.1 | open: reverses a ruling |
| 162 | **The segment's handshakes are requests to the tracer tile** (17.9.10), where v1.35's 4.21.10 staged them by a software delay, with "no 'booted' packets and no request-tree barrier". *(Reversed in v1.36, for the owner to rule on: see `spec/CHANGES-v1.35-to-v1.36.md`)* | 17.9.10, 11.5 | open: reverses a ruling |
| 163 | How exact the simulator must be: the RTL's core, Tracer and replayer stall costs cycle for cycle, or exact rows and results with cycle counts close. v1.36's simulator is the first: the RTL's firmware images run on it unmodified at the RTL chip's cycle counts, and where the contract is silent it takes the RTL's sources (`OPEN-v1.36.md` lists each such decision, and where a path still differs) | 21, 19.1 | built cycle for cycle; for the owner to confirm |
| 164 | Every `[meas: v1.35]` figure, and every older one, re-measured on v1.36's simulator. Done for the RTL's own runs, which the simulator reproduces: the sort's four runs (27), the segment and the replay (17.8.16, 17.9.17). Left: the figures of meshsim's own firmware, which is not ported (`OPEN-v1.36.md`) | all | open, part done |
| 165 | The field accelerator: where it attaches, as a peripheral or at the core's coprocessor port; what the coprocessor port serves; its access to the scratchpad; and, before those, its prime, operations and representation | 2.2, 6.1, 15 | open |
| 166 | The host interface's meaning: the master tile's boot over LVDS, the beats and TLAST | 6.5, 17.6 | open |
| 167 | How a silicon memory tile's IMEM and DMEM are filled (`boot_local`) | 6.5 | open |
| 168 | A core load or store of `PTR` or `HIST` that meets its engine: the store lost and the load undefined (12.5), where v1.35 defined both | 12.5 | open: for the owner to confirm |
| 169 | The gather queue at `TOKEN_COUNT`, its overflow a program error that only the tile's other senders can cause, and the address generator taking no read while the gather unit is busy. It changes the gather steps' cycle counts | 13, 14.1 | open: for the owner to confirm |
| 170 | Whether `READ_LATENCY` and "a write wins its bank, with no bank queue" are contract values or stay the implementation's. The RTL's 3 cycles rest on placeholder SRAMs | 15, 19 | open |
| 171 | No start skew: the release reaches every compute tile in the same cycle, where v1.35 allowed 0 or +1. How the net does it is physical, with item 8 | 7.3, 18 | open |
| 172 | The UART's divisor counts the chip's clock and resets to 115,200 baud. v1.35 counted a reference oscillator, at 57,600, so that the UART works before the PLL locks | 17.6, 19 | open: for the owner to confirm |
| 173 | Bring-up with every compute tile in every release, no one-at-a-time start and no overlays: what a partial or debug bring-up uses | 6.5, 7.3 | open |
| 15 | A status bit with the arrival, for per-owner completion | 17.6 | proposed |
| 27 | Drop the one-read-size-per-group rule. The RTL neither forbids mixed sizes nor tests them | 8.4 | proposed |
| 28 | 128-bit staging from the master tile. A load is `WRITE` packets straight into IMEM and DMEM (6.5), of any size | 6.5 | proposed, informed |
| 64 | The request queue and its stall rule. The stall rule is kept (11.1); the queue is `REQUEST_QUEUE_DEPTH` = 4, where v1.35 had 128, and there is no "no longer full" event, so a burst of reports stalls the core | 11.1, 19 | proposed: needs a decision |
| 48 | Decided: a peripheral's landing is bounded. `PERIPHERAL_LATENCY` is a bound, at most `LAND_LATENCY` − `WRITE_PORT_LATENCY`, and every peripheral specified lands 2 cycles after the director's acceptance; still proposed: whether 2 is the contract | 17.3, 19 | decided, part proposed |
| 60 | The memory system's side: the memory system behind `fill`, on the memory tile, its translator and shared cache, its latencies, and the link's physical width; with the file system (119) | 2.1, 3, 17.9.5, 23 | deferred |
| 119 | The file system, with its design. The RW engine, the `READ` and `WRITE` requests, the write buffer and the `lpddr` port are specified (11.5, 17.7); the Tracer's side is specified | 11.5, 17.7, 23 | deferred, part decided |
| 130 | A 1 × 4 test chip. Decided: the mesh is square, `MESH_SIZE` 1 to 8; a 1 × 4 is not one | 2.1, 16.2, 19 | deferred, part decided |
| 159 | The simulator checks to add: invariant 17's request limits, its write-buffer rule and its group-transfer rule; invariant 18 for the scratchpad; size 11 and the two stream cases, stopped rather than counted | 20, 4.1, 8.3 | deferred (owner, 2026-11-12) |
| 17 | A direct path from the gather unit to the write engine | 13 | later |
| 18 | Fusing the local gather with the values all-to-all | 8.4, 13 | later |
| 19 | An arrival-order form of augmenting for `GATHER` | 12.4 | later |
| 20 | A search engine for the split | 10 | later |
| 21 | A NoC-port reader for bulk transfers from DMEM | 14 | later |
| 22 | Whole-row fetches for 64-bit streams | 8.3 | later |
| 125 | Signed counters. Requests now remove the window between the release and the arming at boot (6.5) and in the segment (17.9.10), which was its use | 12.2 | later, informed |

**The index:** where each item lives; *closed* means settled, its record in the history file or below.

1 here; 2 closed (v1.36); 3 closed (v1.36); 4 closed (v1.36); 5 closed (v1.36); 6 closed (v1.36); 7 here; 8 here; 9 closed; 10 here; 11 closed; 12 closed; 13 here; 14 closed; 15 here; 16 closed; 17 here; 18 here; 19 here; 20 here; 21 here; 22 here; 23 closed; 24 here; 25 closed; 26 closed; 27 here; 28 here; 29 closed; 30 closed; 31 closed; 32 closed; 33 closed; 34 closed; 35 `spec/jolt/replayer.md`; 36 `spec/jolt/replayer.md`; 37 closed; 38 `spec/jolt/replayer.md`; 39 `spec/jolt/replayer.md`; 40 closed; 41 closed; 42 `spec/jolt/replayer.md`; 43 closed; 44 closed; 45 closed; 46 closed; 47 closed; 48 here; 49 closed; 50 closed; 51 closed; 52 closed; 53 closed; 54 closed; 55 closed; 56 `spec/jolt/replayer.md`; 57 `spec/jolt/replayer.md`; 58 closed; 59 `spec/jolt/replayer.md`; 60 here; 61 here; 62 closed (owner, 2026-11-12); 63 closed (v1.36); 64 here; 65 closed; 66 closed; 67 closed (v1.36); 68 closed; 69 closed; 70 closed (owner, 2026-11-12); 71 closed; 72 closed; 73 closed; 74 closed; 75 closed; 76 closed; 77 closed; 78 closed; 79 closed; 80 closed; 81 closed; 82 closed (v1.36); 83 closed (v1.36); 84 closed (v1.36); 85 closed; 86 closed; 87 closed; 88 closed; 89 closed; 90 closed; 91 closed; 92 closed; 93 closed; 94 closed; 95 closed; 96 closed; 97 closed; 98 closed; 99 closed; 100 closed; 101 closed; 102 closed; 103 closed (owner, 2026-11-12); 104 closed; 105 closed; 106 closed; 107 here; 108 closed; 109 closed; 110 closed; 111 closed; 112 closed; 113 `spec/jolt/replayer.md`; 114 closed; 115 `spec/jolt/replayer.md`; 116 here; 117 closed (v1.36); 118 closed (v1.36), its decided half reversed (160); 119 here; 120 closed (v1.36); 121 closed (v1.36); 122 closed (v1.36); 123 closed (v1.36), its record in `spec/jolt/replayer.md`; 124 here; 125 here; 126 closed (v1.36); 127 closed; 128 here; 129 closed (v1.36); 130 here; 131 closed; 132 closed; 133 closed (v1.36); 134 closed; 135 closed; 136 closed; 137 `spec/jolt/replayer.md`; 138 closed; 139 `spec/jolt/replayer.md`; 140 closed (v1.36), its record in `spec/jolt/replayer.md`; 141 closed; 142 closed (v1.36); 143 closed; 144 closed, and reversed (161); 145 closed; 146 closed; 147 `spec/jolt/tracer.md`; 148 `spec/jolt/tracer.md`; 149 closed; 150 closed; 151 closed; 152 closed; 153 `spec/jolt/tracer.md`; 154 closed; 155 closed; 156 closed; 157 closed (v1.36), its record in `spec/jolt/tracer.md`; 158 `spec/jolt/replayer.md`; 159 here; 160 here; 161 here; 162 here; 163 here; 164 here; 165 here; 166 here; 167 here; 168 here; 169 here; 170 here; 171 here; 172 here; 173 here; 174 `spec/jolt/tracer.md`; 175 `spec/jolt/tracer.md`; 176 `spec/jolt/tracer.md`; 177 `spec/jolt/tracer.md`.

**Closed in v1.36,** each by the RTL's hardware, which v1.36 takes:
- **2,** the bank request-queue depth, and **4,** the bound on bank queueing: there is no bank queue. A write
  wins its bank and a read that would meet it is not taken, so a read waits only for a write to its own bank;
  the placement rule is 15's, concurrent streams in different 512 KiB blocks (14.1, 15).
- **3,** write combining: no. A 32- or 64-bit write is one access of its row, with per-word enables (15).
- **5,** the `PTR` and `HIST` macros: no requirement on them. Each read-modify-write has a bypass, so no update
  is lost at any spacing and nothing depends on read-during-write data (12.5).
- **6,** the gather queue's in-flight bound: the queue holds `TOKEN_COUNT` requests and the address generator
  takes no read while the gather unit is busy, so with the address generator as the only source it cannot
  overflow (13, 14.1). What follows from it is item 169.
- **63,** each node passes whole requests, round-robin: as proposed (11.3).
- **67,** a read's data: 128-bit `WRITE` packets in ascending address order (11.4, 11.5).
- **82,** the special tiles' attachment: tile ids `TILE_COUNT`, + 1 and + 2; attach tiles 0, `MESH_SIZE` − 1 and
  (2·`MESH_SIZE` − 1) mod `TILE_COUNT`; the request receivers by straps, `PARAMS.receiver` and
  `PARAMS.request_target`; the peripherals' type numbers (2.1, 7.1, 17.1).
- **83,** the target numbering: 0 the master tile, 1 the memory tile, 2 the tracer tile; 3–7 reserved, a request
  for one dropped at the root. v1.35's target 3 is not kept: the file system's requests are the memory tile's
  (11.2, 11.5; item 119).
- **84,** the request: beats in ascending order, bits [15:0] first; the row node writes the requester; the receive
  queues are `RECEIVE_QUEUE_DEPTH`, 128, 256 and 128 at 8 × 8 (11.3). v1.35's "16 requests per target queue" was
  stale.
- **117,** storing the reserved packet bits: they are not stored. Every port, queue and stage carries the defined
  bits alone (4.1).
- **118,** its deferred half: a special tile's routing (16.2), and its DMAs' flow control, the room of the queue
  a packet enters (16.4). Its decided half, src = the id + 8, is reversed: item 160.
- **120,** the tables' address: 0xFFFF_E000 to 0xFFFF_F5FF, in 512-byte windows (5.2).
- **121,** the core's CSRs and their reset values: `spec/core.md`, CSRs. There is no `mtime`.
- **122,** the handler's report: a `REPORT` request, `op` 1, its number in word 0's [25:18] (6.5, 11.5).
- **123,** the replayer's event and error bits: `ERRORS` packed from bit 0, v1.35's bits one lower, the events
  v1.35's (17.8.1). Its row is in `spec/jolt/replayer.md`.
- **126,** a request dropped at the root: nothing is recorded (11.2).
- **129,** `STREAM_SRC_ADDR` stays a 21-bit word address (8.3).
- **133,** the handler parks in a jump to itself (6.5).
- **140,** the PEXT method: a contiguous mask keeps the schedule, and a mask of two or more runs takes a cycle a
  mask bit (17.8.16); the contract stays "as many as the implementation's" (17.8.7). Its row is in
  `spec/jolt/replayer.md`.
- **142,** the region-decode check: kept, as the bad packet's first case (4.2).
- **157,** the Tracer's statistics counters' attribution: the RTL's (17.9.8), which the simulator follows. Its row
  is in `spec/jolt/tracer.md`.

Item 144 is closed and reversed (161), and item 153 part closed: their rows are in `spec/jolt/tracer.md`.

## 27. Context: the sort these tiles serve

Not part of the hardware specification; it is here because it explains the gather unit and augmenting. The
program is in `fw/sort/program/sw/`, on the RTL's port layer (the split in `fw/sort/split/`,
skew handling in `fw/sort/skew/`), and v1.3's
measured times are in `v1.3/results/`. Every figure below marked `[meas: …]` or `[model]` for a version before
v1.36 was made on that version's hardware and simulator. v1.36's simulator runs the RTL's port of the program
(`fw/sort/`; its image `fw/images/sort/tile/sort.elf`), and its figures are the RTL's (below; item 164).

For a 32-bit key and a value of *k* records (*k* = 2 for 128-bit values), the sort has six steps:

1. **Local sort** of (key, index) words, with the index attached by augmenting (`AUGMENT`, 12.4).
2. **Local gather** of the values into key order: a `GATHER` stream to the tile itself (13).
3. **Split:** a bisection on key values. Owners exchange probe keys and counts ((lb, ub) pairs), as `WRITE`
   packets from the write unit into the receiver's DMEM, and every tile counts by binary searches of its own
   sorted keys through the read unit (10). Exact on all 25 data sets: ≈ 203K cycles on uniform, 246K mean over
   the 24 non-trivial sets, 367K worst, with a seed bracket and an early exit to idle barriers [meas: RV32 ISS].
   Interface and memory: the split's binding document, in v1.35 (`v1.35/fw/sort/split/BINDING.md`).
4. **Two all-to-alls,** group transfers on the 16-group schedule (`spec/schedule.md`):
   - **keys,** as `AUGMENT`, with count-on-arrival on at the receiver;
   - **values,** as `APPEND`, with count-on-arrival off.

   Each sender's packets land contiguously at `PTR[src]`, set by the receiver from its counts; senders need no
   destination addresses. Key *i* and value *i* correspond because both arrive in the same sender order.
5. **Local sort at the owner,** starting with its first digit already counted.
6. **Local gather** of the values into final key order: the same operation as step 2.

The values cross the NoC once, and each value is read twice locally, by the two gathers. For the 32-bit-value
format, key and value fit in one 64-bit record, the value travelling in the record's high word: so steps 2 and 6
disappear, nothing is augmented (the first local sort's input pass and the keys all-to-all use `APPEND`, not
`AUGMENT`), and there is no values all-to-all.

**Phases.** Each step is one or more phases, and every phase ends at the barrier (7.3). In v1.36 a phase is not
a command from the master tile: the image holds the schedule of phases, every compute tile runs it, and the
arguments of a phase are data in DMEM (6.5).

**Skew handling** (`fw/sort/skew/`). A three-stage switch picks the transfer path for step 4: direct,
loopback-as-local-copy (LB), the one-layer two-hop (TH), or the balanced two-hop (BT), with every tile making
the same choice from the group maxima. K + V including planning: 215K uniform, 199K sorted and all-equal, 420K
reverse, 772–786K Zipf and trace, 741K at 99 % one value [model; planner cycles measured on an RV32 ISS].
The schedule holds a fixed maximum number of layer phases, and tiles finish empty layers at once: an empty
phase, from one release to the next, is 35 cycles on the RTL's chip [meas: RTL]. The switch is format-aware: for
32-bit values it costs the keys all-to-all only (trace sets take BT at 332–343K against 0.59–1.00M direct;
sorted and all-equal LB at 66.7K; reverse TH at 149.0K) [model]. TH and BT run every stream at the full
window and so use a group gap of 4 (8.4). In v1.35 the planner was an overlay (≈ 8.5 KiB); v1.36 has no
overlays, so it is part of the image (6.5).

**Global totals** (the group maxima for the `GROUP` `rotations` fields, or any global sum) use a mesh reduction,
not a ring scan.
- Each row collects toward its column-0 tile (4 hops from one side, 3 from the other), then column 0 collects
  toward tile 0 the same way, and tile 0 broadcasts.
- That is about 8 core turnarounds instead of 63.
- **One-word totals** move as counted `WRITE`s into the collector's DMEM: a collector arms the counter with 2
  and waits in `WFI`; a tile mid-chain arms it with 1.
- **Vectors** (the 16 group maxima) move as four 128-bit `WRITE` packets into the receiver's DMEM, counted on a
  counter.
- The ring scan is kept only for exclusive prefixes.

**The whole sort.** The plan estimated ≈ 1.37M cycles at *k* = 2 on uniform data [model]. Measured, for the
full-size sort `uniform-k128` (64 tiles × 65,536 records, 128-bit values):

| version | cycles | what it was measured on |
|---|---|---|
| v1.2 | 1,360,082 | [meas: v1.2] |
| v1.25 | 1,229,008 | [meas: v1.25]: 131,074 fewer, all from the even-*k* gathers (13) |
| v1.35 | 1,229,687 | [meas: v1.35], its simulator at its defaults, the three global nets at 8 cycles (`RUNNING.md`) |
| v1.36 | 1,306,907 | [meas: v1.36], the RTL's image and run on v1.36's simulator, from the first step's release to the last step's all arrived: the RTL's figure, below. v1.35's program is not ported, so its own run is not measured again (item 164) |

Since v1.1 the values all-to-all of the 128-bit-value format sends one packet per value instead of two.

**The RTL's sort** [meas: RTL]. The RTL (Snarky One) runs this sort end to end on its chip, in simulation, on a
port layer over the same program. Its figures run from the first step's release to the last step's all
arrived. **Their baseline is not v1.35:** the RTL measured them against v1.25's simulator with the nets'
latencies at 1, on the same input. The sort's sources are the same in v1.25 and v1.35, but v1.35's defaults are
8, so its 1,229,687 above is not comparable with the baseline column.

| sort | the RTL | v1.25's simulator, nets at 1 | ratio |
|---|---|---|---|
| 64 × 16 records, 32-bit values | 150,261 | 123,473 | 1.22 |
| 64 × 16 records, 128-bit values | 153,013 | 125,980 | 1.21 |
| 64 × 65,536 records, 32-bit values | 952,154 | 894,163 | 1.06 |
| 64 × 65,536 records, 128-bit values | 1,306,907 | 1,229,959 | 1.06 |

- **The transfers agree.** The steps the tile's engines and the NoC drive (the local sorts, K and V at 65,536
  records, 32-bit values) are within 0.2 % of the simulator's.
- **The core accounts for the gap.** The steps bound by code take longer: the split, 66 barriers round a
  bisection through the read unit, 1.22 to 1.26 times; the exchanges driven a word at a time by the core, 1.3
  times. v1.25's and v1.35's simulators retire an instruction a cycle with no load-use delay; the RTL's core's
  costs are `spec/core.md`'s, and v1.36's simulator has them (19.1; item 163).
- **The gathers.** With 128-bit values the two local sorts with their gathers take 2.2 % and 2.5 % longer at
  65,536 records: the gather in 128-bit rows on the scratchpad's fixed read latency, with a write winning its
  bank (13, 14.1, 15). Measured, and not attributed further.
- **An empty phase** is 35 cycles from release to release, against 43 for v1.25's command at latency 1.

v1.36's simulator reproduced the RTL's column, step for step, under the RTL's own images
(meshsim's commit 5a87d658; chapter 21); it now runs meshsim's firmware (`tests/test_sort.cpp`). The comparison with v1.25's and v1.35's sort is not re-run: their
program is not ported to v1.36's hardware (item 164).

## Appendix A. The RTL's implementation

Facts of the RTL (Snarky One) that are not contract: what it holds where chapters 1–19 leave a choice to the
implementation, and how it builds some parts. They are here because v1.36's simulator reproduces the RTL's
cycle counts (21, item 163), and because a reader of v1.35 will look for them. Another implementation may differ
in any of them and still meet the contract. Synthesis results are not part of this specification: there is no
PDK, and the RTL's are on a predictive process with placeholder SRAM macros.

**The scratchpad** (the RTL's scratchpad README).
- Banks are as large as chapter 15 allows, the fewest: min(`SCRATCHPAD_BYTES`, 512 KiB) each. 8 banks of
  32,768 × 128 bits at 4 MiB, 16 at 8 MiB, one at 512 bytes. Each is one single-port SRAM with a write enable
  per 32-bit word.
- `READ_LATENCY` is 3: access registers, the SRAMs, an output register. Both ports pass through the same
  stages, so one cycle's accesses reach a bank at one edge.
- A read presented at the bank of the same cycle's write (which the read arbiter never does) returns an
  undefined row; the write is done.

**The inject unit** (its README).
- The return queues are one delay line of `READ_LATENCY` stages: a read taken in cycle *t* is injected in
  *t* + 3 and is at the router's local input, through the link register, in *t* + 4.

**The DMA unit** (its README).
- `GROUP` is two dual-port SRAMs, word 0's 64 × 30 bits and word 1's 64 × 8, so a core store needs no write
  mask; the address generator reads both. (v1.35 had one SRAM; the split changes no behaviour.)
- `STREAM_SRC_ADDR` 256 × 21, `STREAM_DEST_ADDR` 256 × 32, `STREAM_HDR` 256 × (25 + `TILE_ID_BITS`), one
  dual-port SRAM each.
- The end of a transfer (8.2) is the cycle after the walk completes, so `EVENTS.dma_done` and
  `STATUS.dma_busy` come from registers.
- A store to `DMA_CONFIG` during a transfer changes nothing of it: the transfer takes `DMA_CONFIG` at its start.
- The address generator's offer is taken by the read arbiter in the cycle it is offered; there is no holding
  register between them.
- Tested at `TABLE_LATENCY` 1 and 2.

**The read unit** (its README).
- `READ_QUEUE_DEPTH` is 64, the least chapter 10 allows; the queue is a two-port SRAM of 58-bit entries.
- Reads are offered in the order queued, each from the cycle after its `READ_SEND` takes effect.

**The gather unit** (its README).
- The gather queue is a two-port SRAM of 64 × 23 bits: the ordinal and the counter field.
- The read offered is a register: for a request the director passes in cycle *a*, the first read is offered in
  *a* + 2, and reads follow one a cycle, value to value without a gap, while the arbiter takes each.

**The receive unit** (its README).
- For a packet the director accepts in cycle *a*: the `noc_write` in *a* + 2; the `PTR` and `HIST` entry read
  at the end of *a* + `TABLE_LATENCY` and written at the end of *a* + `TABLE_LATENCY` + 1; the scratchpad write
  presented in *a* + 2·`TABLE_LATENCY` + 2. So `LAND_LATENCY` = 8 covers `TABLE_LATENCY` 1 to 3, and 4 is
  refused.
- `PTR` and `HIST` are one two-port SRAM each; the read-modify-write's bypass is a register, so no SRAM is
  asked to read an address it is writing.
- The write engine is a fixed pipeline, with no queue.

**The router** (its README).
- 17 queues on a compute tile, plus the attach ports' on an attach tile. `LOOPBACK_QUEUE_DEPTH` is 64.
- A packet at the local input in cycle *t* is ejected *h* hops away in *t* + 4*h* + 2 when no queue holds it.

**The request tree** (its README).
- One register a node, cut-through, and links that are wires until a floorplan gives their lengths: unloaded, a
  beat a compute tile offers in cycle *t* is offered to its target in *t* + 2, and a special tile's in *t* + 1.
  From the `REQUEST_SEND` store to the request being loadable at its target that is 11 cycles from a compute
  tile and 10 from a special tile (21.4; derived from the figures above and the request unit's, not a measurement).
- A popped request's credit is usable by the root from the next cycle.

**The request unit** (its README).
- A `REQUEST_SEND` the core issues in cycle *t* pushes its request at the end of *t* + 1, and the request's beat 0
  is offered to the tree from *t* + 2, one beat a cycle as the link takes them.
- `REQUEST_QUEUE_DEPTH` is 4. `STATUS.request_full` and the store's stall come from the queue's count at the
  start of the cycle.

**The global nets** (the RTL's chip).
- `ARRIVE_LATENCY` 0 (a wire), `RELEASE_LATENCY` 1 (Ingress/Egress's flip-flop) and `ERROR_LATENCY` 0: placeholders
  until a floorplan gives the nets' lengths (19.1, item 8).

**The core** is the core specification's (`spec/core.md`): its pipeline, its cycle costs and its memories' ports
are stated there, as contract or as the RTL's, each marked.

**The peripherals.** Each of the four built (Ingress/Egress, the RW engine, the replayer, the Tracer) lands a
write at the end of the cycle its write port presents it, 2 cycles after the director's acceptance (17.3, 21.3).
The replayer's and the Tracer's implementation figures are in `spec/jolt/replayer.md` and `spec/jolt/tracer.md`.

**Mesh sizes.** The RTL is tested at `MESH_SIZE` 1, 2, 5 and 8.
