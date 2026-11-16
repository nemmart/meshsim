# Registers: the programmer's reference

**v1.36 (`mesh.h` 1.36).** Everything a tile's program reaches with loads and stores, and the words it builds: the
address map, region 0xFF, the tile window, the tables, the peripherals' windows and regions, the packet and the
requests. `spec/machine-spec.md` is the source of truth (chapters 2, 4, 5, 7-13, 17 and 19; `spec/jolt/replayer.md`
and `spec/jolt/tracer.md` for the replayer's and the Tracer's, 17.8 and 17.9); where the two differ, this document is
wrong. Every name is the specification's, and every section number below is its. **This file is generated** by
`tools/regs/gen_regs.cpp`, from the register table in `tools/regs/regtable.h`, which also generates
`fw/include/mesh.h`; edit the table, not the file. `tests/test_regs.cpp` checks both files against the generator, the
table against the simulator's decoding (`src/units/regmap.h`), and the table against the specification's tables.

## 1. The address map

An address is an 8-bit region [31:24] and a 24-bit byte offset [23:0] (5.1). One map serves the core's own accesses
and the addresses that `WRITE` packets carry.

| region | contents | the core | a `WRITE` packet | `mesh.h` |
|---|---|---|---|---|
| `0x00` | DMEM, `DMEM_BYTES`; its first 16 bytes reserved (6.3) | load, store | write | `DMEM_BASE` |
| `0x01` | IMEM, `IMEM_BYTES` | fetch; a load or store is an access fault | write | `IMEM_BASE` |
| `0x02` | the boot ROM, `BOOT_ROM_BYTES`; the reset PC is its base (6.5) | fetch; a load or store is an access fault | bad packet | `BOOT_ROM_BASE` |
| `0x03` | the scratchpad, `SCRATCHPAD_BYTES`: word address *w* is byte offset 4 *w* (5.4) | none (an access fault): through the read unit and packets | write | `SCRATCHPAD_BASE` |
| `0x04`-`0x06` | peripherals 1, 2, 3: region 0x03 + *n* (17.1) | none (an access fault): through their windows | write, if present | `PERIPHERAL_BASE(n)` |
| `0x07`-`0xFE` | unmapped | access fault | bad packet |  |
| `0xFF` | the windows: the tables, the tile's and the peripherals' (section 2) | load, store, whole words | bad packet |  |

**Access rules** (5.1, 5.4, 6.4):
- **Access faults** (`ERRORS.access_fault`, and a trap): a load or store to IMEM or the boot ROM; an unmapped address,
  region 0xFF's included (window 11, an absent peripheral's window, a word a block does not define, a block a window
  does not use); a sub-word access in region 0xFF; a store to a read-only register.
- **A misaligned load or store** traps before it reaches the map, and sets no bit in `ERRORS` (6.2).
- **A load from a write-only register reads 0.** Reserved bits, above a word's last field, ignore a store and read 0.
- **An address past the end of its memory** is undefined behaviour (1.3), and so is a `WRITE` there.
- **Timing.** A load from a window takes `WINDOW_LOAD_LATENCY` = 2 cycles, a load from a table `TABLE_LOAD_LATENCY`
  = 1: issued in cycle *t* with latency *L*, it returns the state at the end of *t* + *L* - 2 and takes its side
  effect (a consuming load of `EVENTS`, a pop) at the end of *t* + *L* - 1. A store takes effect at the end of
  *t* + *L* - 1: the end of *t* + 1 in a window, of *t* in a table.
- **Stalls.** Only a `WRITE_SEND_*` on a full holding register that is not being injected, a `REQUEST_SEND` on a full
  request queue from which nothing leaves, and Ingress/Egress's `UART_TX` and `STREAM_SEND` on a full FIFO stall.

## 2. Region 0xFF

Region 0xFF maps only its top 8 KiB, `0xFFFFE000`-`0xFFFFFFFF`: 16 windows of 512 bytes, window *w* at
`0xFFFFE000 + 0x200 w`, picked by address bits [12:9] (5.2). The tile window and the peripherals' are in the top
2 KiB, so each register is one `lw` or `sw` relative to `x0` (`X0REL(off)`, `P_X0REL(n, off)`); a table needs a base
register.

| window | address | contents | in C |
|---|---|---|---|
| 0, 1 | `0xFFFFE000` | `HIST` (12.5) | `tables->HIST` |
| 2, 3 | `0xFFFFE400` | `PTR` (12.5) | `tables->PTR` |
| 4, 5 | `0xFFFFE800` | `STREAM_SRC_ADDR` (8.3) | `tables->STREAM_SRC_ADDR` |
| 6, 7 | `0xFFFFEC00` | `STREAM_DEST_ADDR` (8.3) | `tables->STREAM_DEST_ADDR` |
| 8, 9 | `0xFFFFF000` | `STREAM_HDR` (8.3) | `tables->STREAM_HDR` |
| 10 | `0xFFFFF400` | `GROUP` (8.4) | `tables->GROUP` |
| 11 | `0xFFFFF600` | unmapped | |
| 12 | `0xFFFFF800` | the tile window (section 3) | `tile->` (`TileRegs`) |
| 13 | `0xFFFFFA00` | peripheral 1's window, if present (section 5) | `peripheral(1)->` (`PeripheralRegs`) |
| 14 | `0xFFFFFC00` | peripheral 2's window, if present (section 5) | `peripheral(2)->` (`PeripheralRegs`) |
| 15 | `0xFFFFFE00` | peripheral 3's window, if present (section 5) | `peripheral(3)->` (`PeripheralRegs`) |

A register window is eight blocks of 16 words: block *b* at + `0x40 b`, picked by address bits [8:6], and a word by
bits [5:2] (5.3). A block holds one module's registers.

| block | the tile window | a peripheral's window |
|---|---|---|
| 0 | `tile_control_unit` (7) | its control block (17.1) |
| 1 | `dma_unit` (8) | its DMA, if it has one (17.4) |
| 2 | `write_unit` (9) | its own |
| 3 | `read_unit` (10) | its own |
| 4 | `request_unit` (11) | its own |
| 5 | `receive_unit` (12) | its own |
| 6 | `gather_unit` (13) | its own |
| 7 | unused | its own |

Block 0, the control block, is the same in every register window: `PARAMS`, `STATUS`, `ERRORS`, `EVENTS`,
`EVENTS_PEEK`, `EVENTS_MASK`, then the start, `ARRIVE` in the tile window and `START` in a peripheral's. `STATUS` [0]
and `EVENTS` [0] are the start's state and its end, and `STATUS` [1] and `EVENTS` [1] the DMA's. A word a block does
not define, and a block a window does not use, is unmapped: an access fault. The one exception is block 1 of a
peripheral without a DMA, every word of which reads 0.

## 3. The tile window

At `0xFFFFF800`, the same on every tile (5.5). In C: `tile->NAME` (`TileRegs`); in assembly `X0REL(OFF_NAME)(zero)`.

| offset | block, word | register | access | reset | function | section | `mesh.h` offset |
|---|---|---|---|---|---|---|---|
| `0x000` | 0, 0 | `PARAMS` | R | the straps | the straps alone: tile_id, peripherals, receiver, request_target, attach, barrier, boot_local | 7.1 | `OFF_PARAMS` |
| `0x004` | 0, 1 | `STATUS` | R | 0 | the states: arrived [0], dma_busy [1], gather_busy [2], request_full [3] | 7.1 | `OFF_STATUS` |
| `0x008` | 0, 2 | `ERRORS` | R | 0 | one bit per error, sticky until the reset; the peripherals' bits live | 7.4 | `OFF_ERRORS` |
| `0x00C` | 0, 3 | `EVENTS` | R | 0 | one bit per event; a load consumes one count from each counted bit it returns set that EVENTS_MASK selects | 7.2 | `OFF_EVENTS` |
| `0x010` | 0, 4 | `EVENTS_PEEK` | R | 0 | the same value, consuming nothing | 7.2 | `OFF_EVENTS_PEEK` |
| `0x014` | 0, 5 | `EVENTS_MASK` | R/W | 0x5: release, all_counters_done | the events that wake the core and that a load of EVENTS consumes | 7.2 | `OFF_EVENTS_MASK` |
| `0x018` | 0, 6 | `ARRIVE` | W | - | a store arrives at the barrier: sets STATUS.arrived; UB where PARAMS.barrier is 0 | 7.3 | `OFF_ARRIVE` |
| `0x040` | 1, 0 | `DMA_CONFIG` | R/W | undefined | the transfer: kind [0], start [8:1], count [17:9], gap [20:18] | 8.1 | `OFF_DMA_CONFIG` |
| `0x044` | 1, 1 | `DMA_START` | W | - | a store starts the transfer DMA_CONFIG describes: clears EVENTS.dma_done, sets STATUS.dma_busy | 8.2 | `OFF_DMA_START` |
| `0x080` | 2, 0 | `WRITE_HDR` | R/W | undefined | the header block [7:0], its size the send's; dst [14:8], an absolute tile id; no_increment [15] | 9 | `OFF_WRITE_HDR` |
| `0x084` | 2, 1 | `WRITE_DEST_ADDR` | R/W | undefined | the next WRITE's address; advances by the size at each WRITE sent, unless no_increment | 9 | `OFF_WRITE_DEST_ADDR` |
| `0x088` | 2, 2 | `WRITE_W0` | W | 0 | payload bits [31:0]; cleared by each send | 9 | `OFF_WRITE_W0` |
| `0x08C` | 2, 3 | `WRITE_W1` | W | 0 | payload bits [63:32]; cleared by each send | 9 | `OFF_WRITE_W1` |
| `0x090` | 2, 4 | `WRITE_W2` | W | 0 | payload bits [95:64]; cleared by each send | 9 | `OFF_WRITE_W2` |
| `0x094` | 2, 5 | `WRITE_SEND_32` | W | - | the top word, payload bits [31:0]; sends a 32-bit packet | 9 | `OFF_WRITE_SEND_32` |
| `0x098` | 2, 6 | `WRITE_SEND_64` | W | - | the top word, payload bits [63:32]; sends a 64-bit packet, WRITE_W0 below | 9 | `OFF_WRITE_SEND_64` |
| `0x09C` | 2, 7 | `WRITE_SEND_128` | W | - | the top word, payload bits [127:96]; sends a 128-bit packet, WRITE_W0-W2 below | 9 | `OFF_WRITE_SEND_128` |
| `0x0C0` | 3, 0 | `READ_HDR` | R/W | undefined | the header block [7:0]: the next read's size and counter; its mode and dst are the unit's | 10 | `OFF_READ_HDR` |
| `0x0C4` | 3, 1 | `READ_DEST_ADDR` | R/W | undefined | the next read's address in DMEM; advances by the size at each READ_SEND | 10 | `OFF_READ_DEST_ADDR` |
| `0x0C8` | 3, 2 | `READ_SEND` | W | - | [20:0] a word address: queues a read of READ_HDR's size from it | 10 | `OFF_READ_SEND` |
| `0x0CC` | 3, 3 | `READ_FREE` | R | READ_QUEUE_DEPTH | [15:0] the read queue's free entries | 10 | `OFF_READ_FREE` |
| `0x100` | 4, 0 | `REQUEST_OUT_W0` | W | 0 | bits [31:0] of the next request; cleared by the push | 11.1 | `OFF_REQUEST_OUT_W0` |
| `0x104` | 4, 1 | `REQUEST_OUT_W1` | W | 0 | bits [63:32] of the next request; cleared by the push | 11.1 | `OFF_REQUEST_OUT_W1` |
| `0x108` | 4, 2 | `REQUEST_OUT_W2` | W | 0 | bits [95:64] of the next request; cleared by the push | 11.1 | `OFF_REQUEST_OUT_W2` |
| `0x10C` | 4, 3 | `REQUEST_SEND` | W | - | bits [127:96]; sends the request (stalls while the request queue is full and no entry leaves) | 11.1 | `OFF_REQUEST_SEND` |
| `0x110` | 4, 4 | `REQUEST_IN_W0` | R | 0 (empty) | bits [31:0] of the oldest received request; 0 on an empty queue | 11.1 | `OFF_REQUEST_IN_W0` |
| `0x114` | 4, 5 | `REQUEST_IN_W1` | R | 0 (empty) | bits [63:32] of the oldest received request | 11.1 | `OFF_REQUEST_IN_W1` |
| `0x118` | 4, 6 | `REQUEST_IN_W2` | R | 0 (empty) | bits [95:64] of the oldest received request | 11.1 | `OFF_REQUEST_IN_W2` |
| `0x11C` | 4, 7 | `REQUEST_RECEIVE` | R | 0 (empty) | bits [127:96], and pops it; on an empty queue 0, and ERRORS.receive_underflow | 11.1 | `OFF_REQUEST_RECEIVE` |
| `0x140` | 5, 0 | `RECEIVE_CONFIG` | R/W | undefined | bucket_digit [2:0], histogram_digit [5:3] | 12.1 | `OFF_RECEIVE_CONFIG` |
| `0x144` | 5, 1 | `COUNTER_1` | R/W | 0 | [19:0] the count; a store of N adds N, and N > 0 arms the counter | 12.2 | `OFF_COUNTER_1` |
| `0x148` | 5, 2 | `COUNTER_2` | R/W | 0 | counter 2 | 12.2 | `OFF_COUNTER_2` |
| `0x14C` | 5, 3 | `COUNTER_3` | R/W | 0 | counter 3 | 12.2 | `OFF_COUNTER_3` |
| `0x150` | 5, 4 | `COUNTER_4` | R/W | 0 | counter 4 | 12.2 | `OFF_COUNTER_4` |
| `0x154` | 5, 5 | `COUNTER_5` | R/W | 0 | counter 5 | 12.2 | `OFF_COUNTER_5` |
| `0x158` | 5, 6 | `COUNTER_6` | R/W | 0 | counter 6 | 12.2 | `OFF_COUNTER_6` |
| `0x15C` | 5, 7 | `COUNTER_7` | R/W | 0 | counter 7 | 12.2 | `OFF_COUNTER_7` |
| `0x180` | 6, 0 | `GATHER_CONFIG` | R/W | undefined | record_base [20:0], value_size [23:21] | 13 | `OFF_GATHER_CONFIG` |

`COUNTER_n` is at word *n* of block 5: `OFF_COUNTER(n)`, `*counter(n)`. "Undefined" at the reset means undefined
until written (18). Every other word of the window is unmapped.

### 3.1 `PARAMS`

`PARAMS` holds the straps alone (7.1). The memories' sizes are the chip's parameters, which its software is built with
(section 8).

| bits | field | meaning | `mesh.h` |
|---|---|---|---|
| [6:0] | `tile_id` | the tile id | `PARAMS_TILE_ID(w)` |
| [9:7] | `peripherals` | bit n - 1 for peripheral n | `PARAMS_PERIPHERALS(w)` |
| [10] | `receiver` | 1 if the request unit's receive side is connected | `PARAMS_RECEIVER(w)` |
| [13:11] | `request_target` | the target it receives for (11.2) | `PARAMS_REQUEST_TARGET(w)` |
| [16:14] | `attach` | the special tiles attached here: bit 0 the master tile's, 1 the memory tile's, 2 the tracer tile's; on a special tile its own | `PARAMS_ATTACH(w)` |
| [17] | `barrier` | 1 if the barrier's nets are wired | `PARAMS_BARRIER(w)` |
| [18] | `boot_local` | 1 if the tile starts from the image already in its IMEM (6.5) | `PARAMS_BOOT_LOCAL(w)` |

| tile | `tile_id` | `peripherals` | `receiver` | `request_target` | `attach` | `barrier` | `boot_local` |
|---|---|---|---|---|---|---|---|
| a compute tile | 0-63 | 0b001, the replayer | 0 | 0 | the special tiles attached to it: tile 0 0b001, tile 7 0b010, tile 15 0b100, the others 0 | 1 | 0 |
| the master tile | 64 | 0b001, Ingress/Egress | 1 | 0 | 0b001 | 0 | 1 |
| the memory tile | 65 | 0b001, the RW engine | 1 | 1 | 0b010 | 0 | 1 |
| the tracer tile | 66 | 0b001, the Tracer | 1 | 2 | 0b100 | 0 | 0 |

### 3.2 `STATUS`

`STATUS` holds states, and `EVENTS` what has happened (7.1).

| bit | state | kind | meaning | `mesh.h` |
|---|---|---|---|---|
| 0 | `arrived` | state | set by ARRIVE; cleared by the release, which wins, and the reset (7.3) | `STATUS_ARRIVED` |
| 1 | `dma_busy` | state | the DMA unit's transfer is in progress (8.2) | `STATUS_DMA_BUSY` |
| 2 | `gather_busy` | state | the gather queue holds a request, or a request's last read is not yet taken (13) | `STATUS_GATHER_BUSY` |
| 3 | `request_full` | state | the request queue is full (11.1) | `STATUS_REQUEST_FULL` |

### 3.3 `EVENTS`, `EVENTS_PEEK` and `EVENTS_MASK`

| bit | event | kind | set by | `mesh.h` |
|---|---|---|---|---|
| 0 | `release` | counted, 1 | BARRIER_RELEASE (7.3) | `EVENT_RELEASE` |
| 1 | `dma_done` | counted, 1 | the end of the tile's transfer (8.2); also cleared by DMA_START | `EVENT_DMA_DONE` |
| 2 | `all_counters_done` | counted, 1 | a decrement that leaves all seven counters at 0 while the event is armed; also cleared by arming any counter, and by ARRIVE | `EVENT_ALL_COUNTERS_DONE` |
| 3 | `request_pending` | live | a request in the tile's receive queue (11.1) | `EVENT_REQUEST_PENDING` |
| 4-6 | `peripheral_1`-`peripheral_3` | live | peripheral n's EVENTS & EVENTS_MASK is non-zero (17.2) | `EVENT_PERIPHERAL(n)` |
| 7-13 | `counter_1_done`-`counter_7_done` | counted, 1 | counter_n_done: a decrement that leaves counter n at 0 while the event is armed (12.2); also cleared by arming counter n, and by ARRIVE | `EVENT_COUNTER_DONE(n)` |

- **One model for every event**, the tile's and each peripheral's (7.2). A counted event of depth *d* holds a count
  from 0 to *d*: its bit reads 1 while the count is above 0, its cause adds 1, up to *d*, and a consuming load
  subtracts 1. A live event's bit follows its source, and no load changes it.
- **A load of `EVENTS`** returns every bit, and consumes one count from each counted bit that it returns set and that
  `EVENTS_MASK` selects. `EVENTS_PEEK` returns the same value and consumes nothing. An event set after the load's
  sample point survives.
- **The wake** is `(EVENTS & EVENTS_MASK) != 0`, except under the arrival gate, from `ARRIVE` until the cycle after
  the release, when it is `EVENTS.release` alone. A wake consumes nothing.
- **`EVENTS_MASK`** resets to `release` and `all_counters_done` (`EVENTS_MASK_RESET`, 0x5); a peripheral's to 0.
- **The tie rule.** A clear that starts a new wait wins over a set, in the same cycle, that ends the wait before it:
  `DMA_START`'s and `ARRIVE`'s clears.

### 3.4 `ERRORS`

One bit per error (7.4). A correct program on correct hardware never sets one. P is a program error, H a
hardware-fault detector. Each bit is sticky until the reset, except the peripherals', which are live. A non-zero
`ERRORS` raises the NMI, taken at most once, and the tile's error line.

| bit | error | kind | raised by | `mesh.h` |
|---|---|---|---|---|
| 0 | `router_overflow` | P | router (16.3) | `ERROR_ROUTER_OVERFLOW` |
| 1 | `bad_packet` | P | receive_unit (4.2) | `ERROR_BAD_PACKET` |
| 2 | `counter_underflow` | P | receive_unit (12.2) | `ERROR_COUNTER_UNDERFLOW` |
| 3 | `drift` | P | dma_unit (8.4) | `ERROR_DRIFT` |
| 4 | `gather_overflow` | P | gather_unit (13) | `ERROR_GATHER_OVERFLOW` |
| 5 | `read_overflow` | P | read_unit (10) | `ERROR_READ_OVERFLOW` |
| 6 | `early_arrival` | P | tile_control_unit (7.3) | `ERROR_EARLY_ARRIVAL` |
| 7 | `access_fault` | P (H on a corrupted image) | core (5.1) | `ERROR_ACCESS_FAULT` |
| 8 | `illegal_instruction` | P (H on a corrupted image) | core (6.2) | `ERROR_ILLEGAL_INSTRUCTION` |
| 9 | `receive_underflow` | P | request_unit (11.1) | `ERROR_RECEIVE_UNDERFLOW` |
| 10-12 | `peripheral_1`-`peripheral_3` | live | peripheral n's ERRORS is non-zero (17.2) | `ERROR_PERIPHERAL(n)` |

### 3.5 Header registers

Every register that carries a packet header (`_HDR`) shares one layout (5.4): the header block in [7:0], as in the
packet, `dst` in [14:8], and the register's own fields in [31:15]. `HEADER(mode, counter, size)` builds the header
block.

| bits | field | meaning | `mesh.h` |
|---|---|---|---|
| [1:0] | `size` | the size: 0 32 bits, 1 64, 2 128 (ignored where the unit fills it: WRITE_HDR, a peripheral's DMA_HDR) | `HDR_SIZE(v)`, `HDR_SIZE_OF(w)` |
| [4:2] | `counter` | the counter: 0 none, 1-7 COUNTER_1-7 at the receiver | `HDR_COUNTER(v)`, `HDR_COUNTER_OF(w)` |
| [7:5] | `mode` | the mode (4.2) (ignored in READ_HDR) | `HDR_MODE(v)`, `HDR_MODE_OF(w)` |
| [14:8] | `dst` | dst, a tile id (ignored in READ_HDR) | `HDR_DST(v)`, `HDR_DST_OF(w)` |

| register | bits | field | meaning | `mesh.h` |
|---|---|---|---|---|
| `WRITE_HDR` | [15] | `no_increment` | WRITE_DEST_ADDR stays as it is after a send, until the program clears the bit | `WRITE_HDR_NO_INCREMENT` |
| `STREAM_HDR` | [31:15] | `count` | elements, 0 to 2^17 - 1 | `STREAM_HDR_COUNT(v)`, `STREAM_HDR_COUNT_OF(w)` |
| `DMA_HDR` | [26:15] | `count` | 128-bit packets, 0-4095 | `DMA_HDR_COUNT(v)`, `DMA_HDR_COUNT_OF(w)` |
| `DMA_HDR` | [28:27] | `queue` | the peripheral's output queue | `DMA_HDR_QUEUE(v)`, `DMA_HDR_QUEUE_OF(w)` |

A field the unit fills itself is ignored on a store and reads 0: the size in `WRITE_HDR` and in a peripheral's
`DMA_HDR`, and the mode and `dst` in `READ_HDR`. `WRITE_HDR`'s `dst` is an absolute tile id. `STREAM_HDR`'s `dst`
below `TILE_COUNT` is a compute tile: on a compute tile, tile (me + `dst`) mod `TILE_COUNT`, so that 0 is the tile
itself, and on a special tile, tile `dst`; otherwise it is an absolute tile id, a special tile or broadcast (8.3).

A scratchpad address inside the units is a 21-bit word address, in bits [20:0] (`STREAM_SRC_ADDR`, a `PTR` entry,
`READ_SEND`, `GATHER_CONFIG.record_base`): word address *w* is the byte address `0x03000000 | w << 2`
(`SCRATCHPAD_ADDRESS(w)`). Every `_DEST_ADDR` is a byte address of 32 bits.

### 3.6 The units' fields

| register | bits | field | meaning | `mesh.h` |
|---|---|---|---|---|
| `DMA_CONFIG` | [0] | `kind` | 0 a stream transfer, 1 a group transfer | `DMA_CONFIG_KIND(v)`, `DMA_CONFIG_KIND_OF(w)` |
| `DMA_CONFIG` | [8:1] | `start` | the first stream (0-255), or the first group (0-63) | `DMA_CONFIG_START(v)`, `DMA_CONFIG_START_OF(w)` |
| `DMA_CONFIG` | [17:9] | `count` | streams (1-256), or groups (1-64); 0 an empty transfer | `DMA_CONFIG_COUNT(v)`, `DMA_CONFIG_COUNT_OF(w)` |
| `DMA_CONFIG` | [20:18] | `gap` | stream transfers only: idle cycles after each stream (0-7) | `DMA_CONFIG_GAP(v)`, `DMA_CONFIG_GAP_OF(w)` |
| `READ_SEND` | [20:0] | `address` | a scratchpad word address, aligned to the size |  |
| `READ_FREE` | [15:0] | `free` | the read queue's free entries |  |
| `RECEIVE_CONFIG` | [2:0] | `bucket_digit` | 0 none, 1-4 byte d of the payload's low 32 bits, 1 the least significant; 5-7 reserved | `RECEIVE_CONFIG_BUCKET_DIGIT(v)`, `RECEIVE_CONFIG_BUCKET_DIGIT_OF(w)` |
| `RECEIVE_CONFIG` | [5:3] | `histogram_digit` | count-on-arrival's digit, as bucket_digit | `RECEIVE_CONFIG_HISTOGRAM_DIGIT(v)`, `RECEIVE_CONFIG_HISTOGRAM_DIGIT_OF(w)` |
| `COUNTER_n` | [19:0] | `count` | the count, COUNTER_BITS bits |  |
| `GATHER_CONFIG` | [20:0] | `record_base` | the word address of the values' first record, aligned to a row (128 bits) | `GATHER_CONFIG_RECORD_BASE(v)`, `GATHER_CONFIG_RECORD_BASE_OF(w)` |
| `GATHER_CONFIG` | [23:21] | `value_size` | k - 1, k the 64-bit records in a value (1-8) | `GATHER_CONFIG_VALUE_SIZE(v)`, `GATHER_CONFIG_VALUE_SIZE_OF(w)` |

- **The write unit** (9). Software stores a packet's words in ascending order, and the send, the last store, puts the
  top word above `WRITE_W0`-`W2`: `WRITE_SEND_32` alone, `WRITE_W0` then `WRITE_SEND_64`, or `WRITE_W0`-`W2` then
  `WRITE_SEND_128`. At the send a `WRITE` advances `WRITE_DEST_ADDR` by the size unless `no_increment` is set, and
  `WRITE_W0`-`W2` clear. A packet of any other mode carries address 0. The write unit checks nothing.
- **The read unit** (10). A store to `READ_SEND` queues a read of `READ_HDR`'s size at a word address; then
  `READ_DEST_ADDR` advances by the size. Each read comes back as one `WRITE` to the tile itself, at its destination
  in DMEM, counted on `READ_HDR`'s counter. With no free entry the read is dropped, `ERRORS.read_overflow` is set, and
  `READ_DEST_ADDR` still advances.
- **The counters** (12.2). A packet's counter field names one; the traffic director decrements it once the packet's
  write has landed. A store of *N* adds *N*, and *N* > 0 arms the counter: it clears its `counter_n_done` and
  `all_counters_done`, and arms both. A decrement of a counter at 0 sets `ERRORS.counter_underflow`.
- **The DMA unit** (8). `DMA_START` starts the transfer `DMA_CONFIG` describes; a start during a transfer is undefined
  behaviour. `EVENTS.dma_done` says the transfer has read its last table entry.
- **The request unit** (11.1). Words 0-2 in `REQUEST_OUT_W0`-`W2`, then `REQUEST_SEND`, word 3, which pushes the
  request and clears them. On a special tile `REQUEST_IN_W0`-`W2` read the oldest received request and
  `REQUEST_RECEIVE` pops it; a tile whose `PARAMS.receiver` is 0 reads an empty queue.

## 4. The tables

Entry *i* of a table is the word at + 4 *i*; `GROUP[g]` is two words, at + 8 *g* and + 8 *g* + 4 (5.2). A table stores
only its fields' bits: a core store ignores the others, and a core load reads them as 0. In C: `tables->HIST[i]`,
`set_stream(i, src_addr, dest_addr, hdr)`, `set_group(g, w0, w1)`.

| table | addresses | entries | fields | section | `mesh.h` |
|---|---|---|---|---|---|
| `HIST` | `0xFFFFE000`-`0xFFFFE3FF` | 256 | [16:0] the count | 12.5 | `HIST_BASE` |
| `PTR` | `0xFFFFE400`-`0xFFFFE7FF` | 256 | [20:0] a word address | 12.5 | `PTR_BASE` |
| `STREAM_SRC_ADDR` | `0xFFFFE800`-`0xFFFFEBFF` | 256 | [20:0] the source: the first element's word address, aligned to the size | 8.3 | `STREAM_SRC_ADDR_BASE` |
| `STREAM_DEST_ADDR` | `0xFFFFEC00`-`0xFFFFEFFF` | 256 | [31:0] `WRITE` only: the first element's byte address at the receiver; each element advances it by the size | 8.3 | `STREAM_DEST_ADDR_BASE` |
| `STREAM_HDR` | `0xFFFFF000`-`0xFFFFF3FF` | 256 | the header block [7:0], `dst` [14:8], `count` [31:15] | 8.3 | `STREAM_HDR_BASE` |
| `GROUP` | `0xFFFFF400`-`0xFFFFF5FF` | 64 of 2 words | word 0: `rotations` [17:0], `first_stream` [25:18], `streams` [29:26]; word 1: `gap` [7:0] | 8.4 | `GROUP_BASE` |

| word | bits | field | meaning | `mesh.h` |
|---|---|---|---|---|
| `GROUP[g]` word 0 | [17:0] | `rotations` | R, the rotations, at most 2^18 - 1 | `GROUP_W0_ROTATIONS(v)`, `GROUP_W0_ROTATIONS_OF(w)` |
| `GROUP[g]` word 0 | [25:18] | `first_stream` | the group's first stream | `GROUP_W0_FIRST_STREAM(v)`, `GROUP_W0_FIRST_STREAM_OF(w)` |
| `GROUP[g]` word 0 | [29:26] | `streams` | t - 1, t the streams in the group (1-16) | `GROUP_W0_STREAMS(v)`, `GROUP_W0_STREAMS_OF(w)` |
| `GROUP[g]` word 1 | [7:0] | `gap` | idle cycles after the group (0-255) | `GROUP_W1_GAP(v)`, `GROUP_W1_GAP_OF(w)` |

**When the core may touch them.** `STREAM` and `GROUP` (8.6): the core may load any entry at any time, and store any
entry the running transfer does not read. `PTR` and `HIST` (12.5): only while their engine is idle; in a cycle the
engine uses a port, a core store is lost and a core load returns an undefined value. `EVENTS.counter_n_done` says the
arrivals' `PTR` and `HIST` updates are done.

## 5. The peripherals' windows

Peripheral *n* (1-3) has region 0x03 + *n* and window 12 + *n*, at `0xFFFFF800 + 0x200 n`, if present
(`PARAMS.peripherals`); any word of an absent peripheral's window is an access fault, and a `WRITE` to its region a
bad packet (17.1). Blocks 0 and 1 are the same on every peripheral; blocks 2-7 are its own. In C:
`peripheral(n)->NAME` (`PeripheralRegs`), and one struct and pointer for each peripheral below.

| offset | block, word | register | access | reset | function | section | `mesh.h` offset |
|---|---|---|---|---|---|---|---|
| `0x000` | 0, 0 | `PARAMS` | R | the peripheral's constant | type [7:0], version [15:8], the peripheral's constants [31:16] | 17.1 | `P_OFF_PARAMS` |
| `0x004` | 0, 1 | `STATUS` | R | 0 | busy [0], dma_busy [1]; above, the peripheral's | 17.1 | `P_OFF_STATUS` |
| `0x008` | 0, 2 | `ERRORS` | R | 0 | the peripheral's errors, sticky until the reset (Ingress/Egress's tile_error is live) | 17.1 | `P_OFF_ERRORS` |
| `0x00C` | 0, 3 | `EVENTS` | R | 0 | done [0], dma_done [1]; above, the peripheral's; a load consumes (7.2) | 17.1 | `P_OFF_EVENTS` |
| `0x010` | 0, 4 | `EVENTS_PEEK` | R | 0 | the same value, consuming nothing | 17.1 | `P_OFF_EVENTS_PEEK` |
| `0x014` | 0, 5 | `EVENTS_MASK` | R/W | 0 | the events that count toward the tile's EVENTS.peripheral_n and that a load consumes | 17.1 | `P_OFF_EVENTS_MASK` |
| `0x018` | 0, 6 | `START` | W | - | a store starts the peripheral's work; while STATUS.busy is 1, UB | 17.2 | `P_OFF_START` |
| `0x040` | 1, 0 | `DMA_HDR` | R/W | undefined | the header block [7:0], its size the DMA's; dst [14:8]; count [26:15]; queue [28:27] | 17.4 | `P_OFF_DMA_HDR` |
| `0x044` | 1, 1 | `DMA_DEST_ADDR` | R/W | undefined | the DMA's destination address, 16-byte aligned; advances by 16 at each WRITE | 17.4 | `P_OFF_DMA_DEST_ADDR` |
| `0x048` | 1, 2 | `DMA_START` | W | - | a store starts the DMA's transfer; during a transfer, UB | 17.4 | `P_OFF_DMA_START` |

| `PARAMS` | field | meaning | `mesh.h` |
|---|---|---|---|
| [7:0] | `type` | the peripheral's type | `P_PARAMS_TYPE(w)` |
| [15:8] | `version` | its version | `P_PARAMS_VERSION(w)` |
| [31:16] | `constants` | the peripheral's constants | `P_PARAMS_CONSTANTS(w)` |

| bit | state | kind | meaning | `mesh.h` |
|---|---|---|---|---|
| 0 | `busy` | state | the peripheral's work is in progress: its ownership state (17.2) | `P_STATUS_BUSY` |
| 1 | `dma_busy` | state | its DMA's transfer is in progress (17.4) | `P_STATUS_DMA_BUSY` |

| bit | event | kind | set by | `mesh.h` |
|---|---|---|---|---|
| 0 | `done` | counted, 1 | the end of the peripheral's work (17.2) | `P_EVENT_DONE` |
| 1 | `dma_done` | counted, 1 | the end of its DMA's transfer (17.4) | `P_EVENT_DMA_DONE` |

| value | `mesh.h` | `PARAMS.type` |
|---|---|---|
| 0 | `PERIPHERAL_TYPE_NONE` | none |
| 1 | `PERIPHERAL_TYPE_INGRESS_EGRESS` | Ingress/Egress (17.6) |
| 2 | `PERIPHERAL_TYPE_RW_ENGINE` | the RW engine (17.7) |
| 3 | `PERIPHERAL_TYPE_TRACER` | the Tracer (17.9) |
| 4 | `PERIPHERAL_TYPE_REPLAYER` | the replayer (17.8) |

- **Start** (17.2). `START` starts the peripheral's work; `STATUS.busy` is 1 until the work ends, which posts
  `EVENTS.done`. A `START` while busy is undefined behaviour.
- **Events.** A peripheral's events are consumed only by a load of its own `EVENTS`: no store clears one. The tile's
  `EVENTS.peripheral_n` is live: 1 while the peripheral's `EVENTS & EVENTS_MASK` is non-zero; it may lag a consuming
  load by a cycle, so software re-checks after every wake.
- **Errors.** The tile's `ERRORS.peripheral_n` is live: 1 while the peripheral's `ERRORS` is non-zero.
- **The DMA** (17.4), on a special tile's peripheral: `DMA_START` sends `DMA_HDR.count` 128-bit packets from the
  output queue `DMA_HDR.queue` to `DMA_HDR.dst`, each stamped with `DMA_HDR`'s counter and mode. A `WRITE` carries
  `DMA_DEST_ADDR`, 16-byte aligned, which then advances by 16. `STATUS.dma_busy` is 1 until the last packet has left,
  and then `EVENTS.dma_done` is posted. A peripheral without a DMA reads 0 in every word of block 1.

### 5.1 Ingress/Egress

Peripheral 1 of the master tile, type 1, version 0 (17.6); it has no DMA: its block 1 reads 0. In C: `ingress_egress->NAME` (`IngressEgressRegs`).

| offset | block, word | register | access | reset | function | section | `mesh.h` offset |
|---|---|---|---|---|---|---|---|
| `0x080` | 2, 0 | `ERROR_LINES_0` | R | 0 | bit b: the error line of tile b (0-31) | 17.6 | `IE_OFF_ERROR_LINES_0` |
| `0x084` | 2, 1 | `ERROR_LINES_1` | R | 0 | bit b: the error line of tile 32 + b | 17.6 | `IE_OFF_ERROR_LINES_1` |
| `0x088` | 2, 2 | `ERROR_LINES_2` | R | 0 | bit b: the error line of tile 64 + b: [1] the memory tile's, [2] the tracer tile's; [0], the master tile's own, reads 0 | 17.6 | `IE_OFF_ERROR_LINES_2` |
| `0x08C` | 2, 3 | `ARRIVALS_0` | R | 0 | bit b: compute tile b has arrived | 17.6 | `IE_OFF_ARRIVALS_0` |
| `0x090` | 2, 4 | `ARRIVALS_1` | R | 0 | bit b: compute tile 32 + b has arrived | 17.6 | `IE_OFF_ARRIVALS_1` |
| `0x094` | 2, 5 | `ARRIVE_EXPECT_0` | R/W | 0 | as ARRIVALS_0: the compute tiles the master tile waits for | 17.6 | `IE_OFF_ARRIVE_EXPECT_0` |
| `0x098` | 2, 6 | `ARRIVE_EXPECT_1` | R/W | 0 | as ARRIVALS_1 | 17.6 | `IE_OFF_ARRIVE_EXPECT_1` |
| `0x09C` | 2, 7 | `RELEASE` | W | - | a store with bit 0 set releases the barrier: clears ARRIVALS, restarts the watchdog | 17.6 | `IE_OFF_RELEASE` |
| `0x0A0` | 2, 8 | `WATCHDOG` | R/W | 0 (off) | a store sets the limit and the count, 0 stops it; a load returns the count | 17.6 | `IE_OFF_WATCHDOG` |
| `0x0A4` | 2, 9 | `LED` | R/W | 0 | [0] the led pin | 17.6 | `IE_OFF_LED` |
| `0x0A8` | 2, 10 | `UART_TX` | W | - | [7:0] a byte to send; stalls while the TX FIFO is full | 17.6 | `IE_OFF_UART_TX` |
| `0x0AC` | 2, 11 | `UART_RX` | R | 0 (empty) | [7:0] the oldest byte received, [8] valid; pops it | 17.6 | `IE_OFF_UART_RX` |
| `0x0B0` | 2, 12 | `UART_DIVISOR` | R/W | UART_RESET_DIVISOR | [15:0] the cycles per bit; 0 is UB | 17.6 | `IE_OFF_UART_DIVISOR` |
| `0x0B4` | 2, 13 | `UART_STATUS` | R/W | 0 | the TX FIFO's bytes [4:0], the RX FIFO's [12:8], rx_overrun [16], framing_error [17]; a store clears each error it writes 1 | 17.6 | `IE_OFF_UART_STATUS` |
| `0x100` | 4, 0 | `STREAM_OUT_W0` | W | 0 | word 0 of the next beat out; cleared by the push | 17.6 | `IE_OFF_STREAM_OUT_W0` |
| `0x104` | 4, 1 | `STREAM_OUT_W1` | W | 0 | word 1 of the next beat out | 17.6 | `IE_OFF_STREAM_OUT_W1` |
| `0x108` | 4, 2 | `STREAM_OUT_W2` | W | 0 | word 2 of the next beat out | 17.6 | `IE_OFF_STREAM_OUT_W2` |
| `0x10C` | 4, 3 | `STREAM_SEND` | W | - | the beat's top word; pushes the beat, TLAST 0; stalls while the transmit FIFO is full | 17.6 | `IE_OFF_STREAM_SEND` |
| `0x110` | 4, 4 | `STREAM_SEND_LAST` | W | - | the beat's top word; pushes the beat, TLAST 1 | 17.6 | `IE_OFF_STREAM_SEND_LAST` |
| `0x114` | 4, 5 | `STREAM_IN_W0` | R | 0 (empty) | word 0 of the oldest beat in | 17.6 | `IE_OFF_STREAM_IN_W0` |
| `0x118` | 4, 6 | `STREAM_IN_W1` | R | 0 (empty) | word 1 of the oldest beat in | 17.6 | `IE_OFF_STREAM_IN_W1` |
| `0x11C` | 4, 7 | `STREAM_IN_W2` | R | 0 (empty) | word 2 of the oldest beat in | 17.6 | `IE_OFF_STREAM_IN_W2` |
| `0x120` | 4, 8 | `STREAM_RECEIVE` | R | 0 (empty) | the oldest beat's top word; pops it | 17.6 | `IE_OFF_STREAM_RECEIVE` |

Every other word of blocks 2-7 is unmapped.

| register | bits | field | meaning | `mesh.h` |
|---|---|---|---|---|
| `LED` | [0] | `led` | the led pin |  |
| `UART_TX` | [7:0] | `byte` | a byte to send |  |
| `UART_RX` | [7:0] | `byte` | the oldest byte received | `UART_RX_BYTE(w)` |
| `UART_RX` | [8] | `valid` | 1 with a byte; 0 with the RX FIFO empty | `UART_RX_VALID` |
| `UART_DIVISOR` | [15:0] | `divisor` | the cycles per bit |  |
| `UART_STATUS` | [4:0] | `tx_bytes` | the TX FIFO's bytes | `UART_STATUS_TX_BYTES(w)` |
| `UART_STATUS` | [12:8] | `rx_bytes` | the RX FIFO's bytes | `UART_STATUS_RX_BYTES(w)` |
| `UART_STATUS` | [16] | `rx_overrun` | a frame ended with the RX FIFO full; a store of 1 clears it | `UART_STATUS_RX_OVERRUN` |
| `UART_STATUS` | [17] | `framing_error` | a frame's stop bit was 0; a store of 1 clears it | `UART_STATUS_FRAMING_ERROR` |

| bit | state | kind | meaning | `mesh.h` |
|---|---|---|---|---|
| 2 | `stream_last` | state | the TLAST of the oldest beat in; 0 while there is none | `IE_STATUS_STREAM_LAST` |

| bit | error | kind | raised by | `mesh.h` |
|---|---|---|---|---|
| 0 | `tile_error` | live | any bit of ERROR_LINES is 1 | `IE_ERROR_TILE_ERROR` |

| bit | event | kind | set by | `mesh.h` |
|---|---|---|---|---|
| 2 | `all_arrived` | live | (ARRIVALS & ARRIVE_EXPECT) = ARRIVE_EXPECT, and not 0 | `IE_EVENT_ALL_ARRIVED` |
| 3 | `watchdog` | counted, 1 | the watchdog's expiry | `IE_EVENT_WATCHDOG` |
| 4 | `uart_rx` | live | a byte in the UART's RX FIFO | `IE_EVENT_UART_RX` |
| 5 | `uart_tx` | live | room in the UART's TX FIFO | `IE_EVENT_UART_TX` |
| 6 | `stream_in` | live | a beat in the host interface's receive FIFO | `IE_EVENT_STREAM_IN` |
| 7 | `stream_out` | live | room in the host interface's transmit FIFO | `IE_EVENT_STREAM_OUT` |

It does no work and sends nothing: a `START` is undefined behaviour, and its region takes no write. Bit *i* of
`ERROR_LINES` is tile *i*'s error line; the master tile's own bit, 64, and the bits past 66 read 0. Bit *i* of
`ARRIVALS` is set when compute tile *i*'s arrival reaches it, and cleared by a `RELEASE`. A beat of the host interface
is `STREAM_BYTES` bytes, `STREAM_BEAT_WORDS` = ceil(`STREAM_BYTES` / 4) words: software stores the words below the
beat's top in `STREAM_OUT_W0`-`W2`, then its top word in `STREAM_SEND` or `STREAM_SEND_LAST`; `STREAM_IN_W0`-`W2` read
the words below the oldest beat's top, and `STREAM_RECEIVE` its top word, which pops it.

### 5.2 The RW engine

Peripheral 1 of the memory tile, type 2, version 0 (17.7); it has a DMA. In C: `rw_engine->NAME` (`RwEngineRegs`).

| offset | block, word | register | access | reset | function | section | `mesh.h` offset |
|---|---|---|---|---|---|---|---|
| `0x080` | 2, 0 | `RW_CONFIG` | R/W | undefined | kind [0]: 0 a read, 1 a write; beats [10:1] | 17.7 | `RW_OFF_RW_CONFIG` |
| `0x084` | 2, 1 | `RW_LPDDR_ADDR` | R/W | undefined | the first beat's LPDDR beat address (a byte address over 16) | 17.7 | `RW_OFF_RW_LPDDR_ADDR` |
| `0x088` | 2, 2 | `RW_BUFFER_ADDR` | R/W | undefined | a write's first beat in the write buffer: its offset over 16 | 17.7 | `RW_OFF_RW_BUFFER_ADDR` |
| `0x08C` | 2, 3 | `RW_REPLY_W0` | W | 0 | the next reply's bits [31:0]; cleared by the push | 17.7 | `RW_OFF_RW_REPLY_W0` |
| `0x090` | 2, 4 | `RW_REPLY_SEND` | W | - | the reply's bits [63:32]; pushes the reply into output queue 1 | 17.7 | `RW_OFF_RW_REPLY_SEND` |

Every other word of blocks 2-7 is unmapped.

| register | bits | field | meaning | `mesh.h` |
|---|---|---|---|---|
| `RW_CONFIG` | [0] | `kind` | 0 a read, 1 a write | `RW_CONFIG_KIND(v)`, `RW_CONFIG_KIND_OF(w)` |
| `RW_CONFIG` | [10:1] | `beats` | the beats to move, 0-1023 | `RW_CONFIG_BEATS(v)`, `RW_CONFIG_BEATS_OF(w)` |
| `RW_LPDDR_ADDR` | [27:0] | `address` | [LPDDR_ADDRESS_BITS - 5 : 0], 28 bits at 32: the first beat's LPDDR beat address |  |
| `RW_BUFFER_ADDR` | [9:0] | `address` | [log2 RW_BUFFER_BYTES - 5 : 0], 10 bits at 16 KiB: the first beat's offset over 16 |  |

| value | `mesh.h` | `RW_CONFIG.kind` |
|---|---|---|
| 0 | `RW_KIND_READ` | RW_CONFIG.kind: a read, from LPDDR into output queue 0 |
| 1 | `RW_KIND_WRITE` | RW_CONFIG.kind: a write, from the write buffer to LPDDR |

`STATUS` and `EVENTS` have the standard bits alone.

| bit | error | kind | raised by | `mesh.h` |
|---|---|---|---|---|
| 0 | `lpddr_error` | P or H | a read beat or a write response that is not OKAY | `RW_ERROR_LPDDR_ERROR` |

Its region, 0x04 on the memory tile:

| offset | contents | `mesh.h` |
|---|---|---|
| `0x000000` | the write buffer, RW_BUFFER_BYTES in rows of 16 bytes; WRITEs of 32, 64 and 128 bits | `RW_BUFFER` |

`START` starts the job `RW_CONFIG` describes. A read's beats enter output queue 0, which a DMA transfer of
`DMA_HDR.queue` 0, mode `WRITE` and `count` `beats` sends. A store to `RW_REPLY_SEND` pushes the reply
{0, `RW_REPLY_SEND`, `RW_REPLY_W0`} into output queue 1, which holds one entry and which a transfer of `count` 1
sends.

### 5.3 The Tracer

Peripheral 1 of the tracer tile, type 3, version 0 (17.9); it has a DMA. In C: `tracer->NAME` (`TracerRegs`).

| offset | block, word | register | access | reset | function | section | `mesh.h` offset |
|---|---|---|---|---|---|---|---|
| `0x080` | 2, 0 | `RUN_CONFIG` | R/W | 0 | rows [23:0] (0-65,536), store_log [24] and rd_prev [25] (held, no effect here), tracer_compression [28:26] | 17.9.1 | `TR_OFF_RUN_CONFIG` |
| `0x084` | 2, 1 | `ROWS_RUN` | R | 0 | [23:0] the rows of this run traced | 17.9.1 | `TR_OFF_ROWS_RUN` |
| `0x088` | 2, 2 | `EXPANSION_OFFSET` | R | 0 | [15:0] the row offset at which the last posted core-fed expansion starts | 17.9.1 | `TR_OFF_EXPANSION_OFFSET` |
| `0x08C` | 2, 3 | `EXPANSION_OPCODE` | R | 0 | the code value of the instruction awaiting a core-fed expansion | 17.9.1 | `TR_OFF_EXPANSION_OPCODE` |
| `0x090` | 2, 4 | `STATE_LENGTH` | R | 68 | [15:0] output queue 0's length in entries: 68, a constant | 17.9.1 | `TR_OFF_STATE_LENGTH` |
| `0x094` | 2, 5 | `CODE_LENGTH` | R | 0 | [15:0] the code stream's 128-bit words; 0 from START until the run's rows end | 17.9.1 | `TR_OFF_CODE_LENGTH` |
| `0x098` | 2, 6 | `READ_LOG_LENGTH` | R | 0 | [15:0] the read log's | 17.9.1 | `TR_OFF_READ_LOG_LENGTH` |
| `0x09C` | 2, 7 | `WRITE_LOG_LENGTH` | R | 0 | [15:0] the write log's | 17.9.1 | `TR_OFF_WRITE_LOG_LENGTH` |
| `0x0A0` | 2, 8 | `HASH_MUL` | R/W | 0 | p1, the map's multiplier, odd | 17.9.1 | `TR_OFF_HASH_MUL` |
| `0x0A4` | 2, 9 | `HASH_STEP` | R/W | 0 | [23:0] p3, the probe's step, odd | 17.9.1 | `TR_OFF_HASH_STEP` |
| `0x0A8` | 2, 10 | `HASH_LINES` | R/W | 0 | [4:0] k: the map has 2^k lines | 17.9.1 | `TR_OFF_HASH_LINES` |
| `0x0AC` | 2, 11 | `EXPANSION_TEMPLATE` | W | - | template [6:0], a core-fed template's number; valid [31] | 17.9.1 | `TR_OFF_EXPANSION_TEMPLATE` |
| `0x0B0` | 2, 12 | `STAT_CYCLES` | R | 0 | a statistics counter (17.9.8) | 17.9.8 | `TR_OFF_STAT_CYCLES` |
| `0x0B4` | 2, 13 | `STAT_STALL_FETCH` | R | 0 | a statistics counter | 17.9.8 | `TR_OFF_STAT_STALL_FETCH` |
| `0x0B8` | 2, 14 | `STAT_STALL_DATA` | R | 0 | a statistics counter | 17.9.8 | `TR_OFF_STAT_STALL_DATA` |
| `0x0BC` | 2, 15 | `STAT_STALL_MAP` | R | 0 | a statistics counter | 17.9.8 | `TR_OFF_STAT_STALL_MAP` |
| `0x0C0` | 3, 0 | `STAT_STALL_OUTPUT` | R | 0 | a statistics counter | 17.9.8 | `TR_OFF_STAT_STALL_OUTPUT` |
| `0x0C4` | 3, 1 | `STAT_MISS_I` | R | 0 | a statistics counter | 17.9.8 | `TR_OFF_STAT_MISS_I` |
| `0x0C8` | 3, 2 | `STAT_MISS_D` | R | 0 | a statistics counter | 17.9.8 | `TR_OFF_STAT_MISS_D` |
| `0x0CC` | 3, 3 | `STAT_PROBES` | R | 0 | a statistics counter | 17.9.8 | `TR_OFF_STAT_PROBES` |

Every other word of blocks 2-7 is unmapped.

| register | bits | field | meaning | `mesh.h` |
|---|---|---|---|---|
| `RUN_CONFIG` | [23:0] | `rows` | the rows the next pass or run does | `RUN_CONFIG_ROWS(v)`, `RUN_CONFIG_ROWS_OF(w)` |
| `RUN_CONFIG` | [24] | `store_log` | the replayer's: the pass uses the write log (17.8.3) | `RUN_CONFIG_STORE_LOG` |
| `RUN_CONFIG` | [25] | `rd_prev` | the replayer's: the rows carry rd_prev (17.8.11) | `RUN_CONFIG_RD_PREV` |
| `RUN_CONFIG` | [28:26] | `tracer_compression` | the Tracer's: [26] the code stream, [27] the read log, [28] the write log may go LRU-coded (17.9.6) | `RUN_CONFIG_TRACER_COMPRESSION(v)`, `RUN_CONFIG_TRACER_COMPRESSION_OF(w)` |
| `ROWS_RUN` | [23:0] | `rows` | the rows completed |  |
| `EXPANSION_OFFSET` | [15:0] | `offset` | a row offset, 0-3,120 |  |
| `HASH_STEP` | [23:0] | `step` | p3 |  |
| `HASH_LINES` | [4:0] | `k` | the map has 2^k lines |  |
| `EXPANSION_TEMPLATE` | [6:0] | `template` | a core-fed template's number | `EXPANSION_TEMPLATE_TEMPLATE(v)` |
| `EXPANSION_TEMPLATE` | [31] | `valid` | the number is valid | `EXPANSION_TEMPLATE_VALID` |

`STATUS` has the standard bits alone.

| bit | event | kind | set by | `mesh.h` |
|---|---|---|---|---|
| 2 | `advice_credit` | counted, 2 | the read queue's credit (17.9.3) | `TR_EVENT_ADVICE_CREDIT` |
| 3 | `expansion_credit` | counted, 2 | the expansion queue's, as the replayer's (17.8.8) | `TR_EVENT_EXPANSION_CREDIT` |
| 4 | `new_expansion` | counted, 1 | an instruction of decode class 1 or 2, whose expansion the Tracer waits for (17.9.4) | `TR_EVENT_NEW_EXPANSION` |
| 5 | `state_ready` | live | output queue 0 holds a credit (17.9.6) | `TR_EVENT_STATE_READY` |
| 6 | `code_ready` | live | output queue 1 holds a credit | `TR_EVENT_CODE_READY` |
| 7 | `read_log_ready` | live | output queue 2 holds a credit | `TR_EVENT_READ_LOG_READY` |
| 8 | `write_log_ready` | live | output queue 3 holds a credit | `TR_EVENT_WRITE_LOG_READY` |

| bit | error | kind | raised by | `mesh.h` |
|---|---|---|---|---|
| 0 | `queue_overrun` | P | a push into a full read or expansion queue (17.9.3) | `TR_ERROR_QUEUE_OVERRUN` |
| 1 | `invalid_expansion` | P (H on a corrupted table, ROM or guest) | 17.9.4 | `TR_ERROR_INVALID_EXPANSION` |
| 2 | `advice_underflow` | P (H on corrupted advice) | an advice row with the advice field at 0 (17.9.4) | `TR_ERROR_ADVICE_UNDERFLOW` |
| 3 | `map_miss` | H (P on a bad map) | a jump target not in the map (17.9.4) | `TR_ERROR_MAP_MISS` |
| 4 | `store_not_writable` | H (P on a bad file) | a store to a line that is not writable (17.9.5) | `TR_ERROR_STORE_NOT_WRITABLE` |
| 5 | `page_missing` | H (P on a bad file) | a fill beyond its file's depth, or a hole in the map (17.9.5) | `TR_ERROR_PAGE_MISSING` |
| 6 | `out_of_range` | H (P on bad advice or a bad guest) | a memory row outside the provable range (17.9.5) | `TR_ERROR_OUT_OF_RANGE` |
| 7 | `failed_assertion` | H (P on bad advice) | a VirtualAssert row whose condition is false (17.9.4) | `TR_ERROR_FAILED_ASSERTION` |
| 8 | `misaligned` | H (P on a bad guest) | a memory row whose address is misaligned (17.9.5) | `TR_ERROR_MISALIGNED` |
| 9 | `write_back_error` | H | a write-back's response error (17.9.5) | `TR_ERROR_WRITE_BACK_ERROR` |

Its region, 0x04 on the tracer tile, takes 128-bit `WRITE`s, 16-byte aligned; a queue's part is one address in effect:

| offset | contents | `mesh.h` |
|---|---|---|
| `0x000000` | the record, 68 words of 16 bytes: word w at + 16 w | `TR_RECORD` |
| `0x001000` | the decode table, 4 KiB, laid out as the replayer's | `TR_DECODE_TABLE` |
| `0x002000` | the ROM, 8 KiB: word j at + 16 j | `TR_ROM` |
| `0x200000` | part 2: the read queue's pushes, the segment's advice | `TR_QUEUE_ADVICE` |
| `0x400000` | part 4: the expansion queue's pushes | `TR_QUEUE_EXPANSION` |

The files word, the record's word 66 (17.9.2), 128 bits:

| bits | field | meaning | `mesh.h` |
|---|---|---|---|
| [30:0] | `memory_root` | the root of the guest's memory file, in 128-byte units |  |
| [32:31] | `memory_depth` | 0-3 |  |
| [63:33] | `map_root` | the root of the jump-target map's file |  |
| [65:64] | `map_depth` | 0-3 |  |
| [71:66] | `log_k` | log2 K, K the proof's RAM in 8-byte cells, 0-32 |  |
| [102:72] | `io_size` | 0x8000_0000 - lowest_address, in bytes |  |

### 5.4 The replayer

Peripheral 1 of every compute tile, type 4, version 0 (17.8); it has no DMA: its block 1 reads 0. In C: `replayer->NAME` (`ReplayerRegs`).

| offset | block, word | register | access | reset | function | section | `mesh.h` offset |
|---|---|---|---|---|---|---|---|
| `0x080` | 2, 0 | `RUN_CONFIG` | R/W | 0 | rows [23:0], store_log [24], rd_prev [25], tracer_compression [28:26] (held, no effect here) | 17.8.1 | `RP_OFF_RUN_CONFIG` |
| `0x084` | 2, 1 | `ROWS_RUN` | R | 0 | [23:0] the rows of this pass completed | 17.8.1 | `RP_OFF_ROWS_RUN` |
| `0x088` | 2, 2 | `EXPANSION_OFFSET` | R | 0 | [15:0] the row offset at which the last posted core-fed expansion starts | 17.8.1 | `RP_OFF_EXPANSION_OFFSET` |
| `0x08C` | 2, 3 | `EXPANSION_OPCODE` | R | 0 | the code value of the instruction awaiting a core-fed expansion | 17.8.1 | `RP_OFF_EXPANSION_OPCODE` |

Every other word of blocks 2-7 is unmapped.

| register | bits | field | meaning | `mesh.h` |
|---|---|---|---|---|
| `RUN_CONFIG` | [23:0] | `rows` | the rows the next pass or run does | `RUN_CONFIG_ROWS(v)`, `RUN_CONFIG_ROWS_OF(w)` |
| `RUN_CONFIG` | [24] | `store_log` | the replayer's: the pass uses the write log (17.8.3) | `RUN_CONFIG_STORE_LOG` |
| `RUN_CONFIG` | [25] | `rd_prev` | the replayer's: the rows carry rd_prev (17.8.11) | `RUN_CONFIG_RD_PREV` |
| `RUN_CONFIG` | [28:26] | `tracer_compression` | the Tracer's: [26] the code stream, [27] the read log, [28] the write log may go LRU-coded (17.9.6) | `RUN_CONFIG_TRACER_COMPRESSION(v)`, `RUN_CONFIG_TRACER_COMPRESSION_OF(w)` |
| `ROWS_RUN` | [23:0] | `rows` | the rows completed |  |
| `EXPANSION_OFFSET` | [15:0] | `offset` | a row offset, 0-3,120 |  |

`STATUS` has the standard bits alone.

| bit | event | kind | set by | `mesh.h` |
|---|---|---|---|---|
| 2 | `code_credit` | counted, 2 | the code queue's credit (17.8.8) | `RP_EVENT_CODE_CREDIT` |
| 3 | `read_log_credit` | counted, 2 | the read-log queue's | `RP_EVENT_READ_LOG_CREDIT` |
| 4 | `write_log_credit` | counted, 2 | the write-log queue's | `RP_EVENT_WRITE_LOG_CREDIT` |
| 5 | `expansion_credit` | counted, 2 | the expansion queue's | `RP_EVENT_EXPANSION_CREDIT` |
| 6 | `new_expansion` | counted, 1 | the pass reached an instruction of decode class 1 or 2, whose expansion it waits for (17.8.6) | `RP_EVENT_NEW_EXPANSION` |

| bit | error | kind | raised by | `mesh.h` |
|---|---|---|---|---|
| 0 | `queue_overrun` | P | a push into a full queue (17.8.8) | `RP_ERROR_QUEUE_OVERRUN` |
| 1 | `invalid_expansion` | P (H on a corrupted table or ROM) | 17.8.10 | `RP_ERROR_INVALID_EXPANSION` |
| 2 | `code_underflow` | P (H on a corrupted stream) | the code stream (17.8.12) | `RP_ERROR_CODE_UNDERFLOW` |
| 3 | `read_log_underflow` | P (H on a corrupted stream) | the read log (17.8.12) | `RP_ERROR_READ_LOG_UNDERFLOW` |
| 4 | `write_log_underflow` | P (H on a corrupted stream) | the write log (17.8.12) | `RP_ERROR_WRITE_LOG_UNDERFLOW` |
| 5 | `leftover` | P (H on a corrupted stream) | a pass ending with input left over (17.8.10) | `RP_ERROR_LEFTOVER` |

Its region, 0x04 on a compute tile, takes 128-bit `WRITE`s, 16-byte aligned; a queue's part is one address in effect:

| offset | contents | `mesh.h` |
|---|---|---|
| `0x000000` | the state word, 16 bytes; its write resets the pass (17.8.8) | `RP_STATE` |
| `0x000010` | the stream word, 16 bytes | `RP_STREAMS` |
| `0x000020` | the register image: image word i at + 16 i, 1 KiB | `RP_IMAGE` |
| `0x000420` | the record's words 66 and 67, 32 bytes: accepted and ignored | `RP_RECORD_END` |
| `0x001000` | the decode table, 4 KiB: entries 8 r to 8 r + 7 at + 16 r | `RP_DECODE_TABLE` |
| `0x002000` | the ROM, 8 KiB: word j at + 16 j | `RP_ROM` |
| `0x100000` | part 1: the code queue's pushes | `RP_QUEUE_CODE` |
| `0x200000` | part 2: the read-log queue's pushes | `RP_QUEUE_READ_LOG` |
| `0x300000` | part 3: the write-log queue's pushes | `RP_QUEUE_WRITE_LOG` |
| `0x400000` | part 4: the expansion queue's pushes | `RP_QUEUE_EXPANSION` |

A credit is half a queue, 128 entries (`REPLAY_QUEUE_ENTRIES` / 2); the state write fills all four (17.8.8).

## 6. The packet

Every link carries one 256-bit packet a cycle in each direction (4.1); its reserved bits, [255:182] at 8 x 8, are 0.

| bits | field | meaning | `mesh.h` |
|---|---|---|---|
| [127:0] | `payload` | 32, 64 or 128 bits, by size, from bit 0; the bits above the size are 0 |  |
| [159:128] | `address` | the receiver's address (5.1); mode WRITE uses it, every other mode carries 0 |  |
| [161:160] | `size` | the payload's width: 0 32 bits, 1 64, 2 128; 3 reserved |  |
| [164:162] | `counter` | the counter the packet decrements at the receiver (12.2): 0 none, 1-7 COUNTER_1-7 |  |
| [167:165] | `mode` | what the receiver does (4.2) |  |
| [174:168] | `src` | the sender's tile id, stamped by the injecting hardware |  |
| [181:175] | `dst` | the destination's tile id |  |

| value | `mesh.h` | size |
|---|---|---|
| 0 | `SIZE_32` | 32 bits |
| 1 | `SIZE_64` | 64 bits |
| 2 | `SIZE_128` | 128 bits; 3 is reserved |

| value | `mesh.h` | what the receiver does (4.2) |
|---|---|---|
| 0 | `MODE_WRITE` | writes the payload at the address: DMEM or IMEM, the scratchpad, or a peripheral's region |
| 1 | `MODE_APPEND` | writes the payload in the scratchpad at PTR[src], then steps PTR[src] by the payload's size |
| 2 | `MODE_BUCKET` | as APPEND, at PTR[t], t the payload's bucket digit |
| 3 | `MODE_AUGMENT` | writes one 64-bit record at PTR[src]: the payload's low 32 bits and its ordinal; 32 or 64 bits only |
| 4 | `MODE_GATHER` | a gather request, to the tile itself only; never counted: its counter field passes to the values |
| 5 | `MODE_COUNT` | writes nothing; counted by its counter field; 6 and 7 are reserved |

| value | `mesh.h` | tile id (2.1, 4.1), at 8 x 8 |
|---|---|---|
| 64 | `TILE_MASTER` | the master tile: TILE_COUNT |
| 65 | `TILE_MEMORY` | the memory tile: TILE_COUNT + 1 |
| 66 | `TILE_TRACER` | the tracer tile: TILE_COUNT + 2 |
| 126 | `TILE_BROADCAST` | broadcast: valid as a dst only; reaches every compute tile |
| 127 | `TILE_NONE` | none: as src, an idle packet |
| 0 | `ATTACH_MASTER` | the master tile's attach tile |
| 7 | `ATTACH_MEMORY` | the memory tile's: MESH_SIZE - 1 |
| 15 | `ATTACH_TRACER` | the tracer tile's: (2 MESH_SIZE - 1) mod TILE_COUNT |

**Bad packets.** The traffic director drops a bad packet, uncounted, and sets `ERRORS.bad_packet`: a `WRITE` to the
boot ROM, to region 0xFF, to an unmapped region or to an absent peripheral; and an `AUGMENT` at 128 bits. A packet
misaligned to its size, a `WRITE` past the end of its memory and a `GATHER` from another tile are undefined behaviour.
A `COUNT` packet writes nothing: a fence, or "done" between tiles.

## 7. Requests

A request is 128 bits, from any tile to a special tile over the request tree (11). Word 0 holds the hardware's fields
and, on the special tiles, the operation (11.2, 11.5); `REQUEST_WORD_0(target, op)` builds it.

| word 0 | field | meaning | `mesh.h` |
|---|---|---|---|
| [6:0] | `requester` | the sender's tile id, written by the tree; software writes 0 | `REQUEST_REQUESTER(w)` |
| [9:7] | `target` | the receiver: 0 the master tile, 1 the memory tile, 2 the tracer tile; 3-7 dropped at the root | `REQUEST_TARGET(v)`, `REQUEST_TARGET_OF(w)` |
| [17:10] | `op` | the operation; 0 is reserved | `REQUEST_OP(v)`, `REQUEST_OP_OF(w)` |
| [31:18] | `own` | the operation's own fields | `REQUEST_OWN(v)`, `REQUEST_OWN_OF(w)` |

| value | `mesh.h` | target (11.2) |
|---|---|---|
| 0 | `TARGET_MASTER` | the master tile |
| 1 | `TARGET_MEMORY` | the memory tile |
| 2 | `TARGET_TRACER` | the tracer tile; targets 3-7 are reserved: dropped at the root |

| value | `mesh.h` | operation, report number or counter (11.5, 6.5) |
|---|---|---|
| 1 | `OP_REPORT` | the master tile's: a report, no reply; word 0 [25:18] its number, words 1-3 its data |
| 3 | `OP_LOAD` | the master tile's: the boot ROM's, the tile waits for its next image (6.5) |
| 1 | `OP_READ` | the memory tile's: beats of LPDDR to the requester |
| 2 | `OP_WRITE` | the memory tile's: asks for a part of the write buffer |
| 24 | `REPORT_ERRORS` | the boot ROM's handler's first report: words 1-3 ERRORS, mcause, mepc (6.5) |
| 25 | `REPORT_MTVAL` | its second: words 1-3 mtval, STATUS, ra |
| 7 | `LOAD_COUNTER` | the counter the boot ROM arms for the load, which the load's COUNT drains (6.5) |

| request | word | bits | field | meaning | `mesh.h` |
|---|---|---|---|---|---|
| `REPORT` | 0 | [25:18] | `number` | word 0: which data words the report carries; 24 and 25 are the boot ROM's | `REPORT_NUMBER(v)`, `REPORT_NUMBER_OF(w)` |
| `READ`, `WRITE` | 0 | [27:18] | `beats` | word 0 of READ and WRITE: the beats, 0-1023 | `MEMORY_BEATS(v)`, `MEMORY_BEATS_OF(w)` |
| `READ`, `WRITE` | 0 | [30:28] | `data_counter` | word 0 of READ: the requester's counter that counts the data | `MEMORY_DATA_COUNTER(v)`, `MEMORY_DATA_COUNTER_OF(w)` |
| `READ`, `WRITE` | 3 | [23:0] | `reply_addr` | word 3: the reply's byte offset in the requester's DMEM | `MEMORY_W3_REPLY_ADDR(v)`, `MEMORY_W3_REPLY_ADDR_OF(w)` |
| `READ`, `WRITE` | 3 | [26:24] | `reply_counter` | word 3: the requester's counter that counts the reply; 0 for no reply | `MEMORY_W3_REPLY_COUNTER(v)`, `MEMORY_W3_REPLY_COUNTER_OF(w)` |
| a `WRITE`'s reply | 1 | [2:0] | `counter` | a WRITE's reply, bits [34:32]: the memory tile's counter, armed, that counts the part's packets | `MEMORY_REPLY_W1_COUNTER(w)` |

- **`REPORT`** (the master tile's): words 1-3 are three data words; it asks for no reply. `REPORT_WORD_0(number)`.
- **`LOAD`** (the master tile's): the boot ROM's; the tile waits in the ROM, counter 7 armed, for its next image.
- **`READ`** (the memory tile's): word 1 the first beat's LPDDR beat address, word 2 the data's byte address at the
  requester, word 3 the reply. The data comes as 128-bit `WRITE`s counted on `data_counter`, then, if `reply_counter`
  is non-zero, a 128-bit reply of 0.
- **`WRITE`** (the memory tile's): word 1 the first beat's LPDDR beat address, word 2 0, word 3 the reply,
  `reply_counter` non-zero. The 128-bit reply holds the part's address, in the memory tile's region 0x04, in [31:0],
  and in [34:32] the memory tile's counter that counts the part's packets.
- **Answers** (11.4): the requester arms the counters for the request's data and reply before sending it. A reply is
  one `WRITE` to the reply address, in the requester's DMEM, aligned to its size.
- Every other `op` is the target's firmware's.

## 8. Parameters

Chapter 19's parameters that software needs, at their defaults, the 8 x 8 mesh's.

| value | `mesh.h` | parameter (19) |
|---|---|---|
| 8 | `MESH_SIZE` | the mesh's side |
| 64 | `TILE_COUNT` | the compute tiles: MESH_SIZE squared |
| 7 | `TILE_ID_BITS` | a tile id's width |
| 0x8000 | `IMEM_BYTES` | IMEM, 32 KiB |
| 0x8000 | `DMEM_BYTES` | DMEM, 32 KiB |
| 256 | `BOOT_ROM_BYTES` | the boot ROM |
| 0x400000 | `SCRATCHPAD_BYTES` | the scratchpad, 4 MiB |
| 21 | `WORD_ADDRESS_BITS` | a unit's scratchpad word address |
| 2 | `WINDOW_LOAD_LATENCY` | a core load from a window, in cycles (6.4) |
| 1 | `TABLE_LOAD_LATENCY` | a core load from a table |
| 20 | `COUNTER_BITS` | a counter |
| 17 | `HIST_BITS` | a HIST count |
| 64 | `TOKEN_COUNT` | the read tokens |
| 64 | `GATHER_QUEUE_DEPTH` | the gather queue: TOKEN_COUNT |
| 64 | `READ_QUEUE_DEPTH` | the read unit's queue: at least 64; READ_FREE says |
| 4 | `REQUEST_QUEUE_DEPTH` | the request unit's queue |
| 128 | `RECEIVE_QUEUE_DEPTH_MASTER` | the master tile's receive queue (11.3): a tile has at most 1 request outstanding |
| 256 | `RECEIVE_QUEUE_DEPTH_MEMORY` | the memory tile's: at most 3 outstanding |
| 128 | `RECEIVE_QUEUE_DEPTH_TRACER` | the tracer tile's: at most 1 outstanding |
| 4 | `STREAM_BYTES` | a host-interface beat's TDATA, in bytes (17.6) |
| 4 | `STREAM_DEPTH` | each host-interface FIFO's beats |
| 16 | `UART_FIFO_DEPTH` | each UART FIFO's bytes |
| 32 | `LPDDR_ADDRESS_BITS` | an LPDDR byte address (17.7) |
| 16 | `RW_QUEUE_BEATS` | the RW engine's output queue 0 |
| 0x4000 | `RW_BUFFER_BYTES` | the RW engine's write buffer, 16 KiB |
| 256 | `REPLAY_QUEUE_ENTRIES` | each of the replayer's input queues, in 128-bit entries; a credit is half of it (17.8.8) |
| 65536 | `TRACER_WINDOW_ROWS` | a run's largest rows (17.9.6) |
| 68 | `RECORD_WORDS` | a window's record, in 128-bit words (17.8.2) |

## 9. `mesh.h`'s helpers

- `tile_id()`: `PARAMS.tile_id`. `counter(n)`: a pointer to `COUNTER_n`.
- `wfi()`: `WFI`, then a load of `EVENTS`, which it returns.
- `arrive()`: `ARRIVE` and the canonical wait, a `WFI` right after the store, then the load of `EVENTS` that consumes
  the release (7.3).
- `dma_start()`, `set_stream()`, `set_group()`: the DMA unit and its tables.
- `write_to(hdr, dest_addr)`, `write32()`, `write64()`, `write128()`: the write unit.
- `request_send(w3, w2, w1, w0)`, `report(number, w1, w2, w3)`: the request unit.
- `mcycle()`: the core's cycle counter, its low 32 bits.
