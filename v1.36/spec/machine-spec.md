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
- `fw/` and `src/`: the firmware and meshsim, the cycle-level simulator. **They are being brought to v1.36**
  (`README.md`): until they are, `fw/include/mesh.h`, `fw/REGISTERS.md` and the simulator's sources hold v1.35's
  names and layouts, and this document is what they are to match.

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
  simulator is to reproduce it. One marked `[meas: v1.35]`, or with an earlier version, was measured on that
  version's simulator and is to be measured again on v1.36's. `[estimate]`, `[derived]` and `[model]` are as they say.
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
- On a special tile, its attach port is its only path to the mesh. Its unused
  link inputs carry its peripherals' DMAs (16.4).

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
- **Stores.** A store takes the latency of a load from the same place, and
  takes effect as a load takes its side effect: a store issued in cycle *t*
  takes effect at the end of *t* + *L* − 1, the end of *t* + 1 in a window and
  of *t* in a table. The state at the end of a cycle includes what takes effect
  then, so a load issued after a store sees it.
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
  a destination. A packet for it leaves its attach tile by that attach port.
- **Inside a special tile's router:** a packet for the special tile ejects, and
  every other packet, from its local input or a DMA, leaves by its attach port.
  Its broadcast spreads from its attach tile as that tile's own.
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
- **An attach output** takes one packet a cycle, with no back-pressure.

### 16.4 A special tile's peripheral DMAs

Each sending peripheral's DMA enters the special tile's router on one of its
unused link inputs, as a port of its own, and its packets have the special
tile as src.
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
peripherals can, since only a special tile's router has free link inputs (16.4).

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

**Its DMA** enters the memory tile's router on a link input of its own (16.4),
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
