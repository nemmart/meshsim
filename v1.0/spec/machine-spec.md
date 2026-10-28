# Machine specification — 64-tile mesh processor, v1.0

This document specifies the hardware of the 64-tile mesh sorter: the tiles, the network that joins them, the control that sequences them, and the registers a tile program uses. It is written for an engineer who will build the machine.

**Companion material** (paths in the v1.0 package):
- `diagrams/`: the figure set, numbered part.figure (1.0–8.1); `diagrams/README.md` is the index. Each section below names the figures that draw it.
- `spec/schedule.md`: the 16-group all-to-all schedule (4.2) and the routing ties it fixes (4.6).
- `sw/mesh.h` (version 1.0): the tile-program interface, generated from the same register table as `sw/REGISTERS.md`, the programmer's register reference. Both must match section 8.
- `sw/CONVENTIONS.md`, `sw/RECIPES.md`: the software conventions and the recipe book.
- `meshsim/`: the cycle-level simulator, which models every unit described here and is where the measured numbers come from.
- `results/`: the measured sort.

**Markers.** An unmarked statement is decided. *(Proposed)* means suggested and not yet ruled on; *(Open)* means still to be settled; *(likely to change)* marks a decided value expected to move. Section 13 gathers every proposed and open item. Numbers are marked [meas: meshsim] (measured on the simulator, with the test where it matters), [meas: RV32 ISS] (measured on a 64-tile RV32 instruction-set simulator), [built] (as implemented in the simulator), [model] (from a model) or [estimate] (a rough estimate).

---

## 1. Conventions

- **Cycle.** One global clock. Every unit updates once per cycle. All tiles run at the same frequency; there is no requirement that they agree on phase, which is modelled by per-link latency (section 3).
- **Packet.** The unit of link transfer: a 64-bit *payload* plus header fields (3.1).
- **Chunk.** 32 bits. The big SRAM is addressed in chunks.
- **Record.** 64 bits = 2 chunks. Record index *i* corresponds to chunk address 2*i* within a region.
- **Row.** 128 bits = 4 chunks, one access of a big-SRAM area (4.1).
- **Tile.** One processor, idx = 0..63; tile idx is at column `col = idx mod 8`, row `row = idx div 8`.
- **Relative tile offset.** *d* names tile (`me` + *d*) mod 64; *d* = 0 is the tile itself.
- **Controller.** The tile's RV32 processor (4.11). C&C's processor is the **C&C controller** (section 6).
- **Addresses.** Big-SRAM addresses are 20-bit chunk addresses: at most 2²⁰ chunks = 4 MB per tile. All 20-bit addresses are ordinary; none is reserved.
- **Bits** are written [*hi*:*lo*].
- `x mod n` is the non-negative remainder.
- **Names.** `HIST` is the histogram table, `PTR` the landing pointers, `STREAM` the stream table and `GROUP` the group table (4.10). Registers are named by owner (section 8): `TILE_`, `ROUTER_`, `DMA_` and `SRAM_`.

---

## 2. System

*Figures 1.1, 1.2.*

64 tiles (section 4), identical except tile 0's router, which has a fifth port to C&C (4.16). Tile idx has four bidirectional links to tiles (idx+1) mod 64, (idx−1) mod 64, (idx+8) mod 64 and (idx−8) mod 64, called **right, left, down, up** respectively.

The ±1 links form one ring through all 64 tiles in index order (tile 7 is left of tile 8, tile 63 is left of tile 0). The ±8 links are columns with wraparound (tile 56+m is above tile m).

One more endpoint is attached to tile 0's router: **command and control (C&C)**, address 64 (section 6), through the fifth port (4.16). The **memory subsystem** (section 7) hangs off C&C, not off the mesh.

**Global nets** (section 6), besides the links:
- **RUN/HALT:** one line from C&C to every tile, fixed latency `run_latency`.
- **GO:** one line from C&C to every tile, balanced so every tile sees a pulse on the same cycle.
- **READY:** one line per tile to C&C, 64 in all, at equal latency `ready_latency`.
- **Error line:** one line per tile, ORed into one sticky error bit at C&C (6.6).

What sits above the chip (the cards, their FPGAs and each chip's DDR4, figure 1.1) is not specified here.

### 2.1 Address space (7 bits)

| value | meaning |
|---|---|
| 0–63 | tile idx |
| 64 | C&C |
| 65 | unused (reserved) |
| 66–125 | reserved |
| 126 | broadcast (valid only as a destination) |
| 127 | none (as source: idle packet) |

---

## 3. Links

### 3.1 Link word (packet)

*Figure 1.4.*

Every link carries one **102-bit** packet per cycle in each direction:

| field | bits | meaning |
|---|---|---|
| src | 7 | address of the originator, stamped by the injecting tile's hardware |
| dst | 7 | destination address (absolute, section 2.1) |
| mode | 4 | what the packet is (table below) |
| addr | 20 | receiver-local address; its meaning depends on the mode |
| payload | 64 | data |

A packet with src = 127 is an **idle packet** and carries nothing. A sender supplies dst, mode, addr and payload; the router adds src. Bits on the mesh are cheap: the mode field costs 4 bits per FIFO entry and per link, and its fourth bit leaves codes 8–15 free for packet types still to come.

**Packet modes.** The receiving tile's traffic director (4.8) routes every packet by its mode alone. The last column is the down-counter the packet decrements (4.12):

| mode | packet | uses addr | lands | counted in |
|---|---|---|---|---|
| 0 | **invalid** | — | dropped; sets the bad-mode condition (4.13) | — |
| 1 | **gather request** (local only): payload = (key, ordinal); the GATHER engine reads *k* records at `DMA_RECORD_BASE_ADDRESS` + 2*k*·ordinal and injects them as mode-2 packets to the tile itself | no | GATHER engine (4.4) | — (its gather values count) |
| 2 | **big-SRAM write by source:** 64 bits at `PTR[src]`, then `PTR[src]` += 2 | no | `PTR[src]` | `DMA_SRAM_REMAINING` |
| 3 | **bucket write:** 64 bits at `PTR[t]`, *t* = the payload's bucket digit (4.15), then `PTR[t]` += 2 | no | `PTR[t]` | `DMA_SRAM_REMAINING` |
| 4 | **augmented write:** as mode 2, with the payload's high 32 bits replaced by the ordinal, (target chunk address − `DMA_RECORD_BASE_ADDRESS`) / 2 (4.9) | no | `PTR[src]` | `DMA_SRAM_REMAINING` |
| 5 | **controller-memory write:** 2 consecutive words at addr | yes | instruction or data memory (4.11) | `DMA_CM_REMAINING` |
| 6 | **mailbox write:** the low 32 bits to slot = addr[7:0] | yes | data memory (4.12); slot 0 sets "command landed" (6.3) | — |
| 7 | **reply:** the low 32 bits to data-memory word addr | yes | data memory, outside the mailbox (4.12) | `DMA_REPLY_REMAINING` |
| 8–15 | reserved | — | dropped; sets the bad-mode condition (4.13) | — |

Where the address is unused, senders put 0. Two principles shape the table:
- **At most one streaming reader of big SRAM per tile at a time,** plus the dependent single reads of the GATHER engine and the controller read port. There are no remote pulls: gathers are local only (mode 1), and the controller reads only its own big SRAM (4.3).
- **Bulk movement is a push.** Anything larger than one word is requested with a small descriptor and delivered by the owner's address generator.

### 3.2 Latency, throughput and ordering

*Figures 4.3, 8.1.*

Each link direction is a fixed-delay pipe: a packet placed on it at cycle t is available to the far end's route block at cycle t + k. One packet per cycle per direction, no backpressure, no acknowledgement. Packets are never dropped or reordered by a link.

- **Every link takes exactly 3 cycles** (`link_latency` = 3): three register stages from the output selector to the far route block's input. Shorter links are padded with pipeline stages: 102 flops per stage per link direction. The longest link must fit in 3 cycles, which is a physical-design check. Uniform latency keeps every tile's packets in lockstep, so the schedule's per-window load bounds hold exactly (4.2).
- **A hop costs `link_latency` + 1 = 4 cycles:** the route block and the FIFO write take the cycle after the packet arrives (4.6, 4.7).
- **Start skew:** tiles may start a run up to 1 cycle apart, despite the balanced GO net. On a link this adds at most one packet from an adjacent window, so queues grow by about 1 at a window boundary and drain within the next. The simulator models each tile with a random 0 or +1 cycle offset.

**In-order delivery per sender.** Packets from one source to one destination arrive in the order sent: routes are fixed per (source, destination) and FIFOs never reorder (4.6, 4.7). The boot handshake, the command convention (section 6) and the mode-2 landing of multi-packet values (4.9) rely on it.
- **Within one kind only.** A unicast and a broadcast from the same source to the same tile can take different paths (offsets 33–39, 44, 52 and 60), so they may arrive out of order relative to each other [meas: meshsim]. **Rule:** a sender's packets that must arrive in order go all unicast or all broadcast; a command's packets to a tile are never mixed.

**No flow control.** Many-to-one traffic must be paced by the program. Measured fan-in limits with the 0/+1 start skew [meas: meshsim, NetTest skewtable; the minimum over nearest, farthest and three random sender sets and 40 skew patterns], every sender bursting W packets to one tile in the same cycle:

| senders | largest safe W per sender | without skew (nearest senders) |
|---|---|---|
| 63 | **none: 63 × 1 is not safe** | none |
| 32–48 | 1 | 1 |
| 24 | 2 | 2 |
| 16 | 2 | 3 |
| 12 | 3 | 5 |
| 8 | 5 | 5 |
| 6 | 7 | 10 |
| 4 | 15 | 20 |
| 3 | 15 | 23 |
| 2 | 30 | 30 |
| 1 | no overflow | no overflow |

63 × 1 overflows in every case measured, with or without skew (120 of 120 skew patterns, receivers 27, 0 and 63) [meas: NetTest fan-in]: a FIFO is full by its count before the cycle's pop (4.13), so a push into a full FIFO is lost even in a cycle that pops it. The first FIFO to overflow is one of the receiver's eject FIFOs. Schedule-ordered exchanges (stream runs in the 16-group order) are permutations, so they don't fan in.

---

## 4. Tile

*Figure 1.3.*

**Units:**

| section | unit |
|---|---|
| 4.1 | big SRAM: 8 areas, no coherence |
| 4.2 | address generator |
| 4.3 | controller read port |
| 4.4 | GATHER engine |
| 4.5 | SRAM read queue, return path and injection |
| 4.6 | route blocks |
| 4.7 | output queue sets and selectors |
| 4.8 | traffic director |
| 4.9 | DMA write engine |
| 4.10 | table memories (`HIST`, `PTR`, `STREAM`, `GROUP`) |
| 4.11 | controller, with its instruction and data memories |
| 4.12 | mailbox, counters and the local wake |
| 4.13 | status and conditions |
| 4.14 | commit ordering |
| 4.15 | digits |
| 4.16 | tile 0's fifth router port |

Where each sits on the die: section 12. Unless stated otherwise, every unit-to-unit transfer inside a tile takes one cycle (*(param)* `intra_tile_latency`, default 1 per stage; exact values affect latency only).

### 4.1 Big SRAM

*Figures 7.1, 7.2.*

**The array.**
- Capacity *(param)* `mem_chunks`, default 1,048,576 chunks (4 MB): **8 memory areas**, each **128 bits × 32,768 rows** (512 KB).
- **The areas are contiguous blocks,** not interleaved: area *a* holds 128-bit rows *a*·32K to (*a* + 1)·32K − 1. In a 20-bit chunk address:
  - bits 19–17 are the area;
  - bits 16–2 are the row;
  - bits 1–0 are the chunk within the row.
- **Each area does one read or one write per cycle,** of one 128-bit row. The 8 areas work independently, so up to 8 accesses happen per cycle in total when the clients' data sits in different areas. A sequential stream stays in one area at 4 chunks per access.
- **Service is in arrival order:** reads and writes to an area are served in the order they arrive, with no priority.
- **Unloaded read latency: 4 cycles** with an array latency of 2 [meas: meshsim]. The array's read latency for the hardware is *(Open)*.
- **Partial writes.** A 64-bit write fills half a row. The areas have **write enables per 32-bit chunk**, so a partial write is one access, not a read-modify-write. Every area access is a whole 128-bit row; reads select their chunks in the return path (4.5).
- **Alignment: none required.** A read or write may start at **any chunk address**, for every width (32, 64 or 128 bits). An access that doesn't fit in one row (a 64-bit access starting at chunk 3 of a row, or a 128-bit access not starting at chunk 0) **spans two rows**, row and row + 1, possibly in the next area. The sort lays records and values out so that this rarely happens in streams, but it is always allowed.
- **A spanning access is one request from its client** (and, for a read, one token, 4.5), **but two area accesses:**
  - **part 0** goes when its area accepts it;
  - **part 1** follows, ahead of every client, waiting while its area is full;
  - **the client's next request waits meanwhile.** In a group run that wait is lateness and sets drift (4.13): whether an access spans depends on each tile's own buffer addresses, so tiles would fall out of lockstep. **Group runs should therefore use row-aligned elements** (4.2).

  For a read, the two rows' chunks are merged in the return path (4.5); for a write, the write engine splits the payload between the two accesses with per-chunk enables (4.9). For invariant 2 and the token count a spanning access is one access; for the areas it is two.

**The area interface.**
- **Each area has one request queue** holding its reads and writes in arrival order. It accepts up to **two pushes a cycle**, one read (from the SRAM read queue, 4.5) and one write (from the write engine, 4.9), and serves one whole row a cycle.
- **Room is judged before the pop.** An area serves the head of its queue in every cycle it holds a request, and a push is accepted only if the queue's count *before that cycle's pop* leaves room (the write first: `ready_w` = count < depth; `ready_r` = count + `push_w` < depth; 4.13's rule for every FIFO). So after any cycle an area's queue holds at most **depth − 1** requests: 7 at the depth of 8 [meas: MemTest, depths 4, 8 and 16, one read and one write to one area every cycle]. A request refused for room waits in its holding register (a read in a group run sets drift).
- **Same-cycle ties: the write goes in first.** Writes can't wait, since nothing behind the SRAM write queue has flow control, but reads can: the address generator simply waits. A tie therefore costs time, not data. An area asked for more than one access per cycle is a placement error; under writes-first it shows as a late or drifting address generator, not as lost writes.
- **Acceptance.** An area accepts when its count before the cycle's pop is below its depth (4.13's rule). With two pushes in one cycle the read is accepted only if there is room after the write: `ready_r` = count + `push_w` < depth.
- **The queue depth** is a parameter: 8 in meshsim, *(Open)* for the hardware.
- **The traffic director never stalls.** A sustained overload of one area fills the write engine's 4-entry input FIFO, which overflows (the write-engine-input-overflow condition, 4.13); the eject FIFOs stay clear. Blocking the director would also block commands and replies, so it is not done.

**No coherence.** The memory system makes no ordering promise between a run's reads and its writes. **The rule is that within one engine run, no buffer is both read and written.** The rule is per engine run, not per GO-to-GO phase, since a one-command local phase runs several passes, each reading what the previous one wrote after its writes have landed. A phase reads from some buffers and writes to others, and as long as they never overlap, the result is correct.
- **What follows the rule:**
  - the local sort's passes, which ping-pong between two buffers;
  - the gathers (index and values in, output out);
  - the all-to-alls (send buffer out, receive buffer in);
  - the split's searches through the read port, which read sorted keys that nothing writes during the split.
- **Ordering across phases comes from the phase boundary:** everything written in a phase is in the array before the tile reports done (`mem_idle` and the counters, 4.12). The next phase reads it after GO.
- There is no flush, no merge logic and no line state.

**`mem_idle`** (a status bit): the whole big-SRAM write path is empty — the write engine's input FIFO, the pointer stage and every area's request queue. (The GATHER engine's pending reads are covered by `DMA_SRAM_REMAINING`, since each gather value counts on arrival.) A tile checks it, through its counters and the local wake (4.12), before it reports done.

**Wrap and out-of-range.**
- Addresses are modulo 2²⁰, so an access near the top of area 7 wraps to the bottom of area 0.
- **A smaller device** (not expected, but defined): a whole number of areas, 1 to 7 (`TILE_STATUS` gives the size in areas, 8.2). Any chunk at or above the end of the memory, up to 0xFFFFF, **reads as 0**, and **writes to it are dropped**. Past 0xFFFFF an access wraps to address 0, which is real memory again.
- Neither case sets a condition.

**Clients.**
- **Readers:** the address generator (4.2), the GATHER engine (4.4) and the controller read port (4.3).
- **Writer:** the write engine (4.9), the only writer. The GATHER engine writes through it, by sending its gather values as mode-2 packets.
- **There is one SRAM read queue and one SRAM write queue** (4.5). They meet only at the areas.
- All clients queue at the areas they address, so **several clients can be active at once** without static selectors.
- The controller reads big SRAM only through the **controller read port** (4.3) and writes it only by sending itself packets (mode 2); its copies go through the address generator and the loopback (4.11).
- **Throughput comes from placement:** buffers that one phase uses together should sit in different areas. A read can wait in its area's queue behind writes and other clients' reads, so its latency depends on the phase's placement and traffic; the token FIFO absorbs it (4.2, 4.5). The bound, and the placement rules that keep each phase's traffic per area within one access per cycle, are *(Open)* (section 13).

### 4.2 Address generator

*Figures 6.1–6.4.*

The address generator is a state machine that walks `STREAM` (and `GROUP`) and, for each visit, offers one request to the SRAM read queue (4.5). It does not interpret data: the memory supplies the payload, and the return becomes a packet. All interpretation is on the receive side, in the write engine and the GATHER engine.

The address generator ranks **below the GATHER engine and above the controller read port** in the SRAM read queue (4.5). It advances when the queue takes its request. In a digit all-to-all nothing outranks it, so its timetable is exact. In gather phases it waits its turn.

**Runs.** `DMA_CONFIG` (8.5) describes the run:

| field | bits | meaning |
|---|---|---|
| kind | [0] | 0 a **stream run**, 1 a **group run** |
| start | [8:1] | the first stream (0–255), or the first `GROUP` entry (0–63) |
| count | [17:9] | streams (1–256), or groups (1–64) |

**Start** (`DMA_PULSE` [1], 8.6) acts on it:
- **Start loads the run** described in `DMA_CONFIG`, clears `rd_done`, sets `rd_running` and begins the table read-ahead (below). GO does not start a run; start is the only way.
- **The first request is offered as soon as its entries are in.** A start store in cycle *t* takes effect in cycle *t* + 1 (4.11).
  - **Group run:** the first request is offered **4·`ts_engine_latency` + 3 cycles after the start store** (4·`ts_engine_latency` + 2 after the cycle the start takes effect; 7 and 6 at the default). The run reads the `GROUP` entry, then the `STREAM` entry, which needs the group's start stream; each read is a round trip of 2·`ts_engine_latency` + 1 [meas: meshsim, as the read-ahead's latency].
  - **Stream run:** the first request is offered **2·`ts_engine_latency` + 2 cycles after the start store** (2·`ts_engine_latency` + 1 after the cycle the start takes effect), the time of one `STREAM` read.
  - **There is no fixed lead-in counter:** the time depends only on the run kind, the tables and the latency, so it is the same on every tile and a synchronised start stays in lockstep.
- **`rd_running` = 1 and `rd_done` = 0 from the end of the start store's cycle,** so the next instruction's load of `TILE_STATUS` already sees them; `rd_running` stays 1 through the lead-in until the run has walked its last entry. (A program that polls the tables right after a start, as `wait_tables()` does, relies on this.)
- **A start while a run is running is ignored**, lead-in included, with no condition: the run the program expected doesn't happen, so its arrivals never come and the tile hangs, which C&C's watchdog catches next to the cause (4.13's principle). meshsim flags it as a program violation.
- **A local run** is one start store. **A synchronised start** is the same start store, executed by every tile on the same cycle after GO wakes it (6.3).
- **Finishing:** the run ends when the address generator has **walked its last entry**: every stream of a stream run, every group of a group run, including trailing count-0 streams and R = 0 groups, each at its timetable cost. The last entry's gap-after is not waited out. The sequencer cannot know a request is the last without examining the entries after it, so it simply walks them. Then `rd_running` = 0 and `rd_done` = 1; `rd_done` stays 1 until the next start.
- **Drift.** In a group run, a request refused by the SRAM read queue holds its visit until taken, every later time shifts by the stall, and drift is set (4.13). The timetable's times are the cycles a request is offered; read latency after that is separate (4.5).

**Stream runs** go through streams *start* … *start* + *count* − 1 **one after another**. Each stream's whole run is sent before the next stream begins.
- **Read ahead, no working state.** `STREAM` entries are read in stream order into the read-ahead queue (below), one `STREAM` read per cycle from its own memory (4.10), so consecutive streams follow without a bubble. An **element counter** *j* runs 0 … count − 1 within the current stream, and the stream ends when *j* reaches its count.
- **A count-0 stream** issues nothing and takes only its gap-after cycles; its table read-ahead waits, which matter only for long runs of them, are part of the timetable (below). Lockstep rounds that must keep a fixed shape send dummy requests instead.
- **A stream run reads big SRAM only.** Values the controller computes in data memory (cuts, counts, maxima) go from the send port instead (4.11).
- **Uses:**
  - block copies (mode 5);
  - local passes (modes 2, 3 and 4 to the tile itself);
  - gather streams (mode 1);
  - one-word exchanges.
- **A one-word exchange of big-SRAM data** is 63 streams of count 1, with *d* in the 16-group offset order, and **gap after = 4 on every stream**, so a round takes ≈ 63 × 5 ≈ 315 cycles with loads far below any overflow.
- **The gapless form** *(Open)* would run the streams back to back in about 63 cycles. Every tile sends to the same offset on the same cycle, so each step is a permutation, and each 4-stream window has link loads ≤ 4 per direction. Running gapless relies on **uniform 3-cycle links** and **at most 1 cycle of start skew** between tiles (3.2), which keep the windows together. The queue peaks at window boundaries are still to be confirmed in simulation (section 13).

**Group runs** are for the digit all-to-alls. They run groups *start* … *start* + *count* − 1, each a round-robin over its streams for R rotations, then its gap. The address generator keeps **no per-stream working state**. One **rotation counter** *r* runs 0 … R − 1 for the group, and each visit reads its stream's `STREAM` entry (read ahead, below): the stream sends element *r* if *r* < count, and its time is idle otherwise. Every stream of a group is on the same rotation, so one counter serves the whole group. A group's streams all have the same read size, which sets its period: a stream whose size differs from the group's is a programming error, with no hardware check (meshsim reports it).

**Stream table** `STREAM[0..255]`, one 64-bit entry per stream, written as two 32-bit stores and read as one access. The fields that change every round are in the low word:

| word | field | bits | meaning |
|---|---|---|---|
| low | source | [19:0] | the run's first element, as a **chunk address** (any chunk: 4.1) |
| low | controller-memory word | [31:20] | mode 5 only: the start word in controller memory, packed map (4.11), +2 per 64-bit packet; ignored for other modes |
| high | count | [17:0] | elements in the stream's run (0 = padding) |
| high | destination | [23:18] | **relative** tile offset *d*: the packets go to (`me` + *d*) mod 64, and *d* = 0 is the tile itself |
| high | mode | [26:24] | the packet mode stamped on every packet of the stream, zero-extended to the packet's 4 bits: 1–5 (mode 1 only with *d* = 0). Codes 0, 6 and 7 are reserved |
| high | read size | [28:27] | 00 reserved, 01 32 bits, 10 64 bits, 11 128 bits |
| high | gap after | [31:29] | stream runs only: idle cycles (0–7) after this stream's run *(likely to change)* |

- **Read size.**
  - **32 bits:** one chunk per element, sent in a payload's low half with the high half 0. This is how raw 32-bit keys are sent one per packet (step 1 of the sort, 4.9).
  - **64 bits:** one record per element.
  - **128 bits:** one row's worth per element, sent as **two packets on consecutive cycles** (low 64 bits, then high), so a visit takes 2 cycles.
  - **Reserved encodings are errors wherever they appear:** a stream with read size 00, or with mode 0, 6 or 7, is an error even at count 0.
- **Gap after** *(likely to change)*. In a stream run, the address generator issues nothing for this many cycles after the stream's last element, for example 4 on every stream of a one-word exchange (above). Group runs ignore it and use the group gap.
- **Element addresses are computed, never stored.** Element *e* of a stream (*e* = *j* in a stream run, *r* in a group run) is read at source + *e* × (chunks per element: 1, 2 or 4), a shift and a 20-bit add, wrapping mod 2²⁰. No alignment is required (4.1), but **group runs should use row-aligned elements**: a 64-bit element at chunk 3 of a row, or a 128-bit element not at chunk 0, is two area accesses, and the wait for the second sets drift (4.1). For mode 5 the controller-memory word is the field + 2 per packet before it. The address generator never writes `STREAM`, so a run leaves its entries as the controller wrote them. There is no base register: 20 bits of chunk address cover the whole 4 MB.
- **No destination offset.** Big-SRAM destinations are chosen by the receiver: modes 2 and 4 land at `PTR[src]` and mode 3 at `PTR[bucket digit]`. A local copy is a mode-2 stream to *d* = 0 with `PTR[me]` as the destination. A mode-1 stream is a gather stream to the tile itself (4.4).
- **Relative destinations** make the destination, mode and size of every stream identical on every tile for the 16-group schedule, which is defined in offsets. The cost is a 6-bit adder; packets on the wire carry the absolute dst.
- **The address generator sends only to tiles 0–63,** never to C&C, the memory subsystem or broadcast.
- **Count** is in elements: 18 bits covers a tile's 128-bit values as well as its keys.

**Group table** `GROUP[0..63]`, one 64-bit entry per group:

| word | field | bits |
|---|---|---|
| low | R: rotations, the group's global maximum count | [17:0] |
| low | spare | [31:18] |
| high | start stream | [7:0] |
| high | *t* − 1: streams in the group (1–16) | [11:8] |
| high | gap: idle cycles after the group | [19:12] |
| high | spare | [31:20] |

- **Groups name their own streams,** so groups can share streams and several programs can sit in the tables at once (for example the keys and values all-to-alls, or 64-bit and 128-bit forms), each run by number.
- **Each pass rewrites only the low words:** one store per group.
- **Uniform size per group.** All streams in a group have the same read size. A visit takes 1 cycle, or 2 for 128 bits, so a group's period is *t* or 2*t*.
- **Timing.** A group occupies exactly R·(period) + gap cycles, or 1 + gap when R = 0 (below). A longer period is made with padding streams.
- **R = 0:** the group sends nothing and takes **1 cycle** plus its gap, the address generator's pass over its `GROUP` entry. R is a global maximum, so a zero-length group is zero-length on every tile and the cycle is the same everywhere: it is timetable, not lateness. (A run of more than about `ag_lookahead` − 1 consecutive zero-length groups costs a further 2·`ts_engine_latency` + 1 once, also identical on every tile [meas: meshsim].)
- **The link-load bound** assumes a tile sends at most one element per rotation to each destination; the program keeps one stream per destination per group.

**The digit all-to-all schedule** (`spec/schedule.md`). 16 groups of 4 streams, every group at T = 4 (T = 8 for 128-bit values, with loads ≤ 8 per period of 8 and no padding) with loads ≤ 4 in all four directions; Σ T = 64, the lower bound.
- **Gap: 0 cycles** between groups. Gap 0 gives the same queue peaks as gap 4 on uniform and on all the skewed sets, with no drift [meas: meshsim], and saves ≈ 60 cycles per all-to-all. **Exception:** phases that run every stream at the full window (the two-hop layers TH and BT, section 14) need a **gap of 4 after each group**, or the receivers' eject FIFOs overflow at group boundaries [meas: meshsim, the sort program; peaks 6 on links and 4 at eject with gap 4].
- **Measured** against a nine-group schedule [meas: meshsim, `AllToAllTest measure`]:

| data set | change |
|---|---|
| uniform | −3.5 % |
| trace | −11.7 % |
| 99 % one value | −24.5 % |



**Read-ahead** (both kinds of run). The address generator reads ahead of its timetable into two queues of *(param)* `ag_lookahead` entries: `GROUP` entries in group order, and `STREAM` entries in visit order (a stream run: stream by stream; a group run: the group's streams in order, R times over; an R = 0 group reads none). An entry holds its place from the cycle its read is issued until its visit uses it (a `GROUP` entry: until its group ends). Each table gives one read per cycle from its own memory (4.10), and the order depends only on the entries, never on the data.
- A table read is a round trip of 2·L_ts + 1 cycles (L_ts = `ts_engine_latency` each way, and a cycle in the memory), so `ag_lookahead` ≥ 2·L_ts + 2 keeps the queues full when every group lasts at least 2 cycles: then **a group's entries are in before its scheduled start, however short the group before it**, with read-queue stalls or without [meas: meshsim, L_ts = 1–4].
- Groups of a single one-cycle visit back to back need `ag_lookahead` ≥ 4·L_ts + 3, since each `STREAM` read waits for its `GROUP` entry [meas: meshsim]. No group of the sort is that short, and one-word exchanges are stream runs, so `ag_lookahead` is 2·L_ts + 2.
- **A group starts** when its `GROUP` entry is in and, if R > 0, its first `STREAM` entry, **one group per cycle**: a zero-length group (R = 0 with gap 0) takes one cycle. A run of them longer than about `ag_lookahead` − 1 fills the `GROUP` queue, and the next group then also waits about 2·L_ts + 1 cycles for its entries [meas: meshsim].
- In a stream run, count-0 streams with gap after 0 take no cycles while their entries are in; a run of them longer than the queue takes about one cycle each.
- **Such waits are part of the timetable,** not lateness: the read timing depends only on the tables and `ts_engine_latency`, which are the same on every tile, so every tile waits the same and the run stays in lockstep. This holds because the address generator is the only engine reader of `STREAM` and `GROUP`, each its own memory (4.10). Only a wait caused by the SRAM read queue refusing a request is lateness, reported as the **drift** condition (4.13).

**Timetable** (group runs). Group *g* begins at cycle `S_g = S_{g−1} + max(R_{g−1}·P_{g−1}, 1) + gap_{g−1}`, where **S_0 is the first-request cycle: 4·L_ts + 3 cycles after the start store** (4·L_ts + 2 after the cycle the start takes effect; the lead-in, above). A stream run has no timetable; its first request is offered 2·L_ts + 2 cycles after the start store. Its rotations begin at `S_g`, `S_g + P_g`, `S_g + 2·P_g`, …
- A rotation visits the group's streams in order, one visit time each.
- A stream with elements left performs a visit; otherwise the time is idle.
- The timetable governs when requests are *issued*. Each request's token waits in a 64-entry **token FIFO** (4.5) until its data returns, so emitted packets trail the timetable by a constant while nothing stalls.
- During a gap the address generator issues nothing.
- A visit whose `STREAM` entry is not in yet waits; that wait is part of the timetable, identical on every tile, not lateness (above). With `ag_lookahead` at its bound it doesn't happen [meas: meshsim].
- **The timetable assumes a bounded read latency.** A read can wait in its area's queue (4.1). The token FIFO must absorb the queueing, which the phase's placement bounds *(Open)*.

**A visit** (modes 1–5) offers one read request per 64-bit or 32-bit element, or one per 128-bit element that returns as two packets. Each packet has:
- payload = the element;
- dst = (`me` + *d*) mod 64;
- mode = the stream's mode;
- addr = 0, or for mode 5 the stream's current controller-memory word.

**One read per element.** Each element is one read-queue request; its return goes straight to injection (4.5). A 64-bit stream therefore uses one access per cycle in its source area, within budget when sources and destinations are in different areas, which placement already requires.

**Loopback runs.** A stream with *d* = 0 sends to the tile itself.
- **A copy into controller memory** (an overlay, a table) is a one-stream mode-5 stream run. At 64 bits per cycle a 1K-word image takes ≈ 512 cycles plus the stream's start [estimate]. Section 6.5 gives the sequence.
- **A local big-SRAM copy** is a mode-2 run with `PTR[me]` as the destination.
- **The local sort's scatter passes** are mode-3 runs to the tile itself.
- **Gather streams** are mode-1 runs to the tile itself (4.4).

### 4.3 Controller read port

*Figure 3.3.*

The controller reads its own big SRAM without stalling: it queues reads by stores, and the results land in data memory.

| register | write | read |
|---|---|---|
| `SRAM_RD_DEST_ADDR` | the data-memory word for the next result: 12 bits, a packed data-memory word (4.11); higher bits ignored | 0 |
| `SRAM_RD_ADDR_QUEUE` | queue a 32-bit read of the 20-bit big-SRAM chunk address written | the number of free entries in the request queue |

- **Issuing.** A store to `SRAM_RD_ADDR_QUEUE` puts an entry (the read address, and the current `SRAM_RD_DEST_ADDR`) into the **request queue**, then `SRAM_RD_DEST_ADDR` advances by 1, wrapping within 12 bits. **The store never stalls.**
- **The request queue** is a parameter of **at least 64 entries** (64 × 32 bits = 2 Kbit; 64 in v1.0).
- **Free entries** = depth − (queued + in flight). An in-flight counter rises when the SRAM read queue takes a read-port read and falls when that read's token is injected (the token carries a read-port bit, 4.5).
- **Reading.** The queue drains into the SRAM read queue at the **lowest priority** (4.5), so it never delays the address generator's timetable or the GATHER engine. During an all-to-all it simply waits.
- **Results come back as packets.** Each return becomes a **mode-7 packet to the tile itself**: dst = `me`, addr [11:0] = the entry's packed `SRAM_RD_DEST_ADDR` value (upper bits 0), payload [31:0] = the chunk read (high half 0). It leaves through the injection point like any other read return, loops back through the router into the traffic director, which writes it at its addr through port B and counts **`DMA_REPLY_REMAINING`** (4.12), as for any mode-7 packet. Returns are released in request order, so results land in the order issued. The read port has no path of its own into port B.
- **The loopback's cost** is small: a search step's fixed cost through it is 27.9 cycles [meas: meshsim, SearchBench], 2 cycles more than a direct path into port B, with the per-read cost unchanged; on a uniform split it costs ≈ 0.25K cycles (200,749 against 200,509, +0.12 % [meas: meshsim, SplitRun]).
- **No stalls.** The queue is guaranteed to hold at least 64 entries, so a whole round of one search per owner (63 reads) can always be issued at once into an empty queue. Reading `SRAM_RD_ADDR_QUEUE` gives the free entries, and `DMA_REPLY_REMAINING` (readable) gives the results still outstanding. Software issues as many reads as are free and processes what has landed. Everything issued minus everything outstanding has landed, as a prefix of the results, so the loop needs no batch size. A deeper queue only makes such a loop more efficient.
- **Read-port results and incoming traffic.** Results return as packets through the tile's own injection point, loopback FIFO and eject output, sharing them with the tile's other outgoing read returns and with every packet arriving from the links. The network has no flow control (3.2): a full FIFO drops the packet, and that is an error.

  **Software rule: a tile does not issue read-port reads while packets are arriving for it, and does not start a phase that receives packets while read-port results are still outstanding.** In practice:
  - read-port reads run in steps of their own;
  - before a search step, every expected arrival has landed (its counter is done);
  - before the next exchange, every read-port result has landed (`DMA_REPLY_REMAINING` is done).

  The split works this way. C&C's next command word (mode 6), sent right after each GO (6.6), may land during a search step; it is one packet, and it is allowed.

  The hardware tolerates little overlap, and programs must not rely on it [meas: meshsim, ReadPortGatherTest read-port tolerance; R results issued one per cycle at offsets 0–24 cycles from a mode-5 stream's start]:
  - **Against a stream at one packet per cycle: 15 results, cumulatively over the whole stream.** The eject output serves one packet per cycle, so each result that takes an eject slot leaves one incoming packet waiting in its link's eject FIFO, and the stream never lets it drain. 16 results overflow the 16-entry FIFO at most offsets, however they are spread out.
  - **Against a stream at half rate: 32 results in one burst.** The link's FIFO does not grow (peak 1); the results' own loopback FIFO fills at half a packet per cycle and overflows at 33. The backlog drains between bursts.

  meshsim checks the rule: a packet from outside the tile, other than C&C's mode-6 command words, arriving while the tile has read-port results outstanding is a violation (invariant 14).

  *This rule exists because the routes have no flow control. If flow control is added, it should be revisited, together with the deadlock questions that flow control raises.*
- **Overflow:** a store to `SRAM_RD_ADDR_QUEUE` with no free entry is a programming error: the read is dropped and the read-port-overflow condition is set (4.13). `SRAM_RD_DEST_ADDR` advances on every store, a dropped read included (the simplest hardware: the increment needs no gating).
- **An entry is freed when its return is injected.** `DMA_REPLY_REMAINING` counts the landing, so when it reaches 0 for every read a program has issued, every result is in data memory and the queue is empty, with at least 64 entries free. The converse does not hold: 64 free entries say every return has left, and the last results can still be in the loopback for a few cycles. Only `DMA_REPLY_REMAINING` says they have landed.
- **The search loop** (the split):

  ```c
  while (SRAM_RD_ADDR_QUEUE < 64) ;      // once, before the first step: never stalls (below)
  for each step, while searches are active:
      DMA_REPLY_REMAINING = n_active;    // at most 63
      SRAM_RD_DEST_ADDR   = results;     // results[i] belongs to the i-th read issued
      for each active search: SRAM_RD_ADDR_QUEUE = its midpoint;
      wfi();                             // every result has landed: the queue is empty again
      for each active search: compare results[i], update its bracket;
  ```

  Each step issues at most 63 reads into an empty queue, so the loop needs no free-count checks. The initial poll is the only one, and it never stalls: any earlier read-port reads were in a phase that set `DMA_REPLY_REMAINING` for them, and **a phase that sets counters ends with a `WFI` before it pulses `done`** (4.12), so their results have all landed and the queue is empty. The poll guards against a program that breaks that rule; it cannot catch results still in flight, so it is no substitute for the rule. Converged searches drop out (an early exit), so a step's reads scale with the searches still active. The split's search loop takes 12.35 cycles per search on uniform data, counted between the split's trace marks 0x600 and 0x601 (the step loop and the final key read, over all tiles, per search step) [meas: meshsim, SplitRun].
- **Never between `WFI` and a start store.** The synchronised-start path must be identical on every tile (6.3); read-port traffic belongs before `done` or after the start.

### 4.4 GATHER engine

*Figures 7.4, 7.5.*

The sort reorders values by a sorted index stream twice (section 14, steps 2 and 6), and both operations have the same shape. **The address generator only streams the index, and a small GATHER engine after the traffic director does the value reads.** Gathers are **local only**.

**How it works.**
1. **The address generator streams the sorted (key, ordinal) words to the tile itself as mode 1** (a mode-1 stream with *d* = 0, 4.2). The ordinal is in bits 63–32, where augmenting stamps it (mode 4, 4.9). Only these 64-bit words cross the loopback.
2. **The traffic director passes each mode-1 packet to the GATHER engine** through a **16-entry request queue**.
3. **For each request the GATHER engine puts *k* gather-value reads in the SRAM read queue** (4.5), at `DMA_RECORD_BASE_ADDRESS` + 2*k*·ordinal + 2*i* for *i* = 0 … *k* − 1, 64 bits each. They rank above the address generator and the controller read port.
4. **Each return goes out as a mode-2 packet to the tile itself** (dst = `me`, addr 0): a **gather value**.
5. **The gather values land at `PTR[me]`,** which advances by 2 per packet. The controller sets it to the start of the reorder buffer. The index arrives in sorted order and the output is one sequential run, so value *i* lands at base + 2*k*·*i* with no addresses.

**Parameters.**
- **`DMA_RECORD_BASE_ADDRESS`** (8.7): in a gather run, a 20-bit chunk address, the start of the value buffer. Ordinals count 0, 1, 2, … from the base of the run that augmented the keys (4.9), so value *i* is at the base + 2*k*·*i* with no offset arithmetic. One base serves each run: no run both augments and gathers.
- ***k*:** the value size in records, **1–8**, a `DMA_CONFIG` field (*k* − 1 in [26:24], 8.5).
- **`PTR[me]`:** the output start.

**Datapath.**
- **Each request-queue entry holds only the 20-bit ordinal** (payload bits 51–32); the key half and bits 63–52 are dropped.
- **The start address** `DMA_RECORD_BASE_ADDRESS` + 2*k*·ordinal is computed in the cycle a request reaches the queue's head, and its first read is offered the next cycle. The *k* reads follow, one offered per cycle at priority 1 (4.5), each stepping the address by 2 and pushing a token that sends the value back to the tile as mode 2. Addresses wrap mod 2²⁰, as every chunk address does, and need no alignment (4.1).
- **A mode-1 packet arriving with the queue full** is dropped and sets the GATHER-queue-overflow condition (4.13).
- **GATHER busy** (`TILE_STATUS`, 8.2): the request queue is non-empty, or a request is part-way through its *k* reads. Reads issued but not yet landed are not included; `DMA_SRAM_REMAINING` covers their landing.

**Counting.** Mode-1 requests are **not** counted. Only the gather values (mode 2) count down `DMA_SRAM_REMAINING`, as any mode-2 arrival does, so a gather of *n* values sets it to *n*·*k*. The local wake (4.12) fires once the last gather value has been written and `mem_idle` is set.

**Rate.** Every value costs 1 request packet plus *k* gather-value packets through the one injection point and the one eject, so **≈ *k* + 1 cycles per value**: 3 at *k* = 2, about 196K cycles per 64K values [estimate]. The value reads (*k* per value, 64 bits each) and the gather values' writes fit within one access per cycle per area if index, values and output sit in three different areas (4.1).

**Back-pressure is implicit.** The address generator gets a turn in the SRAM read queue only when no gather value is waiting, so once requests are queued its index reads stop and the queue drains. The queue then holds at most what was already in flight: index reads issued before the first request looped back, about the read latency plus the loopback, ≈ 10 at one per cycle, under 16 [estimate]. A tile's own sources cannot outrun the engine: requests from the address generator wait for the SRAM read queue, where the GATHER engine outranks it, and requests from the send port share the injection point with the gather values. With *k* = 8 and 64 requests sent as fast as the send port goes, the queue peaked at 0 of 16 [meas: meshsim, CondTest's GATHER probe]. So the queue overflows only on a fault (4.13, condition 8, H). The rate under a heavier read-queue load from the tile's other clients, which can only slow the sources further, is *(Open)* (section 13). A gather phase sends only to the tile itself, so there is no network timetable for the waiting to disturb.

**Why gathers are local.** Mode 1 is sent only to the tile itself (invariant 10). A remote gather would work mechanically, but its gather values would land grouped by owner at `PTR[owner]`, not in one sorted sequence, and the requester's eject would see *k* packets per request with the owner's address generator stalling, which breaks the timetable.

### 4.5 SRAM read queue, return path and injection

*Figures 7.2, 7.3.*

**The SRAM read queue.** Everything that leaves a tile, except the controller's own packets, is a big-SRAM read return turned into a packet. So the big SRAM's read side is a single queue of requests, each carrying:

| field | meaning |
|---|---|
| source address | the chunk address to read, consumed by the memory (kept in the return for debugging) |
| destination address | the outgoing packet's addr field: 0, the controller-memory word for mode 5, or for a read-port read the data-memory word for its result (a mode-7 packet to the tile itself, 4.3) |
| width | 32, 64 or 128 bits |
| mode | the outgoing packet's mode |
| dst | the outgoing packet's tile |

**Request priority,** highest first:
1. **gather value:** the GATHER engine's value reads (4.4);
2. **the address generator's** next request (4.2);
3. **the controller read port** (4.3).

The address generator is never starved in a digit all-to-all, where it is the only source besides a waiting read port.

**The SRAM read queue has no storage of its own.** It is the fixed-priority choice among its three clients plus the steering to the area (addr [19:17] the area, [16:2] the row, [1:0] the chunk). It takes the chosen request only if **the area has room** (4.1) **and a token entry is free** (below). Otherwise nobody is served that cycle: lower-priority clients do not get the slot (no bypass). A refusal of the address generator in a group run is drift (4.2, 4.13). **Part 1 of a spanning read** (4.1) goes ahead of every client, waiting while its area is full; the read is one request and one token.

**The token FIFO** (64 entries) holds every taken read until its data returns.
- Each entry has a **128-bit data slot and a valid bit**, plus the request's fields. A read-port read's token carries a read-port bit (4.3).
- An area's return tag is the entry index; areas return rows in any order into their tokens' slots.
- **The head is released when its data is valid,** so **returns are released in request order**. A stream whose requests span areas can complete out of order, so this is required, not a convenience: mode-2 packets and gather values land at `PTR` in arrival order, so a stream's packets must leave in request order.
- **Chunk selection,** for an access starting at **any chunk** (no alignment, 4.1): the data is taken from the returned row, or from two consecutive rows for an access that spans a row boundary, starting at the address's chunk.
  - a 32-bit read gives payload [31:0], with the high half 0;
  - a 64-bit read gives two consecutive chunks;
  - a 128-bit read gives four consecutive chunks, as **two packets on consecutive cycles, low half first**; the second packet's addr is the first's + 2 for mode 5, and 0 for modes 2–4.
- **An out-of-range read** (a smaller device, 4.1) is a token marked zero, released in order with payload 0.

A return carries the request's fields plus the data, and goes straight to the injection point as a packet. A read-port return goes there like the others, as a mode-7 packet to the tile itself (4.3).

**The SRAM write queue** is the other half: one queue of 64-bit writes from the traffic director (modes 2–4, 4.9). The two queues meet only at the areas, which serve in arrival order (4.1). There are no client selectors and no priority bits.

**The injection point** takes at most one packet per cycle, from:
1. **the SRAM read queue's returns;**
2. **the controller's send port** (4.11).

Each supplies dst, mode, addr and payload (7 + 4 + 20 + 64 bits); the router adds src.
- **Priority:** read returns first, and a 128-bit return holds the injection point for both its cycles; the controller's packet waits in its holding register, with no fairness. Injection never stalls (the network has no flow control; a full output FIFO is a router overflow, condition 1).
- **C&C does not use this path:** its packets enter tile 0's router through the fifth port (4.16).

The injected packet takes the same route decision as a link input (4.6, with "arriving direction" = none):
- a packet whose dst is `me` or 126 goes to the eject queues via the **loopback** input;
- on tile 0, a packet for dst 64 goes to the "to C&C" output (4.16);
- otherwise it goes to a link output queue.

### 4.6 Route blocks

*Figures 4.1–4.3.*

There is one route block per link input (from left, from right, from up, from down) and one for the loopback; tile 0 has a sixth, for the input from C&C (4.16). Each cycle a route block takes the packet arriving on its input (idle packets are discarded) and forwards it, in one cycle, to one or more output queues (4.7) or the eject queue.

**Unicast (dst in 0–63, 64).** The route is fixed per (source tile s, destination d): horizontal first, then vertical.
- Let k = (d − s) mod 64 (for d = 64 use d = 0).
- The machine holds a 64-entry **offset table** `hops[k] = (b_k, a_k)`, a hard-wired ROM identical on every tile, with k ≡ 8·a_k + b_k (mod 64), |b_k| ≤ 4, |a_k| ≤ 4 and |a_k| + |b_k| minimal.
- Where the minimum is not unique (only for a_k = ±4), the table records the schedule's choice: offsets 29–32 go down and 33–35 go up (`spec/schedule.md`).
- A packet travels |b_k| hops horizontally (right if b_k > 0, left if b_k < 0), then |a_k| hops vertically (down if a_k > 0, up if a_k < 0).

A route block decides from src, dst and `me`:
1. k = (dst − src) mod 64 (dst 64 → 0); look up (b_k, a_k).
2. Horizontal progress: if b_k ≥ 0, `hb = (col(me) − col(src)) mod 8`, else `hb = −((col(src) − col(me)) mod 8)`.
3. If hb ≠ b_k: forward horizontally in the direction of sign(b_k).
4. Else if me ≠ d: forward vertically in the direction of sign(a_k).
5. Else: EJECT (place in this route block's eject FIFO) — except on tile 0 for dst = 64, which goes to this route block's FIFO of the "to C&C" output (4.16).

Packets from C&C carry src = 64 and are treated as tile 0 for routing. C&C's route block on tile 0 decides as an injection at tile 0 (arriving direction none).

**Broadcast (dst = 126).** Every tile receives exactly one copy. With s = src (64 treated as 0):
- **On injection at the source** (for C&C, at C&C's route block on tile 0): eject a copy, and send copies right, left, down and up. A broadcast is not delivered to C&C.
- **Arriving from the left:** eject a copy; if `(col(me) − col(s)) mod 8 ≠ 4`, send a copy right; send copies down and up.
- **Arriving from the right:** eject a copy; if `(col(s) − col(me)) mod 8 ≠ 3`, send a copy left; send copies down and up.
- **Arriving from up:** eject a copy; if `(row(me) − row(s)) mod 8 ≠ 4`, send a copy down.
- **Arriving from down:** eject a copy; if `(row(s) − row(me)) mod 8 ≠ 3`, send a copy up.

### 4.7 Output queue sets and selectors

*Figures 4.1–4.3.*

There are five outputs: to-right, to-left, to-down, to-up and eject. Each has one FIFO per possible feeder, **16 entries** *(param `queue_depth`)* of one **102-bit** packet:

| output | feeders |
|---|---|
| to-right | injection, from-left |
| to-left | injection, from-right |
| to-down | injection, from-left, from-right, from-up |
| to-up | injection, from-left, from-right, from-down |
| eject | loopback, from-left, from-right, from-up, from-down |

That is 17 FIFOs per tile.
- **Selection.** Each cycle each output's **selector** removes one packet from one non-empty FIFO and sends it out (onto the link for link outputs, 3.2; to the traffic director for eject). It is round-robin with a rotating pointer: it serves the first non-empty FIFO at or after the pointer, then moves the pointer one past it. Pointers are 0 at reset.
- **Overflow.** A FIFO receives at most one packet per cycle from its feeder. A push to a full FIFO (full by its count before the cycle's pop, 4.13) is an **overflow**: the packet is lost and the router-overflow condition (4.13) records it. Overflow never occurs under a correct program.
- **Order.** Packets are never reordered within a FIFO, so packets from one source to one destination arrive in the order sent.
- **Tile 0** has a "from C&C" feeder FIFO on each of these five outputs and a sixth output, "to C&C" (4.16).

### 4.8 Traffic director

*Figure 5.1.*

One unit straight after the eject selector, and **the only path into the tile**. It takes at most one packet per cycle and switches on the packet's **mode** alone:

| mode | goes to |
|---|---|
| 0, 8–15 | dropped; sets the bad-mode condition (4.13) |
| 1 | the GATHER engine's request queue (4.4) |
| 2, 3, 4 | the write engine (4.9) |
| 5 | port B of instruction or data memory, 64 bits (4.11) |
| 6 | port B of data memory: the mailbox (4.12) |
| 7 | port B of data memory: the word at addr (4.12) |

**Consequences:**
- **Port B has one writer.** Port B of both controller memories is written only by the director, at most once per cycle (64 bits across its two banks), so it needs no arbitration and no ownership setting.
- **Routing is the same in HALT and in RUN.** Nothing is forced in HALT: C&C can load the controller memories (modes 5 and 6) while the tiles are halted; big SRAM is staged with mode 2 once the loader has set `PTR[64]` (6.2).
- **Unicast and broadcast** are handled alike at the director; dst does not matter here.
- **src is used** by the router (routes, the broadcast tree), by modes 2 and 4 (`PTR[src]`) and by C&C (READY bits are per line, not per packet). It is never stored in memory.
- **On tile 0,** packets for C&C (dst 64) leave through the "to C&C" output instead of the eject path (4.16).

The controller-memory path carries modes 5–7 to port B with a fixed latency (`cm_latency` = 2 [built]) and needs no queue. **The director decrements the down-counters** (4.12) as it accepts each packet, and keeps `cm_idle`.

### 4.9 DMA write engine

*Figures 5.1, 5.2.*

The write engine receives modes 2, 3 and 4 from the director (at most one per cycle, through a 4-entry input FIFO) and queues each payload as a 64-bit write at its area (4.1). **The packet's mode says where every payload goes; the engine has no mode of its own.**

| mode | target chunk address |
|---|---|
| 2 | `PTR[src]`, then `PTR[src]` += 2 |
| 3 | `PTR[t]`, *t* = the payload's bucket digit (4.15), then `PTR[t]` += 2 |
| 4 | `PTR[src]`, then `PTR[src]` += 2; the payload's high 32 bits become the ordinal (augmenting, below) |

**`PTR[0..255]`** is its own memory (4.10). Each entry is a 20-bit chunk address set by the controller between phases.
- **Modes 2, 3 and 4 are the same write:** at `PTR[i]`, then `PTR[i]` += 2, with *i* = src for modes 2 and 4 (0–63, or 64 for C&C) and the payload's bucket digit for mode 3 (0–255). The mode picks the index, and mode 4 also augments.
- **One user per phase:** `PTR[src]` (modes 2 and 4, which may mix) or `PTR[t]` (mode 3), never both.
- **`PTR[64]` is C&C's staging pointer** (6.2). Each tile's loader sets it to the tile's own staging area. C&C stages only while tiles are halted or waiting for a command, never during a mesh operation, so staging never meets a mode-3 phase. But a mode-3 phase uses all 256 entries and overwrites `PTR[64]`, so **the loader re-sets `PTR[64]` after any phase with mode 3**, before C&C stages again.
- **The receiver decides where each sender's stream lands.** For an all-to-all it sets `PTR[p]` from the counts it already has (prefix sums), so senders need no destination addresses.
- **+= 2 is general.** In-order delivery means a value of *k* records, sent as *k* mode-2 packets from one sender, lands contiguously.
- **No alignment.** `PTR` may hold any chunk address (4.1). A write whose two chunks span a row boundary (`PTR` at chunk 3 of a row) is split by the write engine into two area accesses with per-chunk enables: part 0 goes when its area accepts it, part 1 follows ahead of every other write, and the engine's next write waits meanwhile (4.1).
- **The pointer round trip.** Every write reads and updates its pointer in `PTR`; the packet waits in a **pointer stage** until the old pointer returns, across `ts_engine_latency` each way. The stage holds **2·`ts_engine_latency` + 2 entries** (4 at the default 1) [meas: meshsim, peaks 3, 5, 7, 9 at `ts_engine_latency` 1–4].
- **There is no write at an explicit address.** Every big-SRAM write lands through `PTR`; a controller writes to a chosen address by setting `PTR[me]` and sending itself mode 2.
- **A 128-bit value** is two 64-bit packets written one after the other. Combining them into one row access is *(Open)* (section 13).

**Count-on-arrival** (`DMA_CONFIG`'s histogram digit, 0 = off). For every accepted payload, the counter at the payload's histogram digit (4.15) is incremented in `HIST` (4.10).
- It works with modes 2, 3 and 4.
- In a mode-3 pass, the pointer update and the counter update are taken in the same cycle, so a pass can bucket by the current digit and count the next one at one payload per cycle [meas: meshsim].
- **In an all-to-all**, the receiver counts the first digit of its next sort while the keys arrive, which saves a counting pass over the records: one full pass per sort.

**Augmenting** (mode 4). For every mode-4 payload, the high 32 bits are replaced by the **ordinal**: (target chunk address − `DMA_RECORD_BASE_ADDRESS`) / 2, which counts **0, 1, 2, …** across the buffer. Records land 2 chunks apart starting at the base, so the difference is always even, whether the buffer starts at an even or an odd chunk. The sender chooses augmenting per stream; the receiver sets `DMA_RECORD_BASE_ADDRESS` (8.7) for the run.
- **The ordinal is a position in the buffer, not an arrival count.** With mode 2, each sender's keys land in their own run at `PTR[src]`, and that sender's values land in the same order in the value buffer, so key *i*'s value is value *i* only if *i* counts buffer positions. Packets from different senders interleave on arrival, so an arrival counter would break the match.
- **Use:**
  - the keys all-to-all's streams send mode 4, so keys arrive as (key, *i*), ready to sort;
  - the values all-to-all's streams send mode 2;
  - **step 1** (local keys): raw 32-bit keys are streamed to the tile itself with read size 32 (4.2), one key per packet, in mode 4, with `PTR[me]` = `DMA_RECORD_BASE_ADDRESS` = a new buffer. Each key gets *j* = 0, 1, 2, … One pass, one key per cycle.
- **The ordinal sits in bits 63–32,** where the GATHER engine reads it (4.4).
- **Mode 3 has no augmented form:** augmenting stamps the landing position, which in mode 3 is the position after bucketing, not the input order. An arrival-order form is a possible later addition (section 13).

**Counting.** Modes 2, 3 and 4 decrement `DMA_SRAM_REMAINING` in the director, on acceptance, including the gather values. Together with `mem_idle`, that tells the tile when its writes are in memory (4.12, 4.14).

**Overflow** of the input FIFO is an error (4.13). It would overflow if an area's queue backed up for longer than the FIFO can absorb, so placement must keep a phase's writes and reads to one area within its one access per cycle.

### 4.10 Table memories

*Figures 3.1, 5.2.*

The tables are **four separate macro memories**, each with its own ports.

| memory | contents | size | written by | read or updated by |
|---|---|---|---|---|
| `HIST` | `HIST[0..255]`, 17 bits each | 256 × 17 bits | controller (clears) | count-on-arrival: one read-modify-write per cycle with bypass |
| `PTR` | `PTR[0..255]`, 20 bits each | 256 × 20 bits | controller (sets starts) | write engine, modes 2–4: one read-modify-write per cycle with bypass |
| `STREAM` | `STREAM[0..255]`, 64 bits each (4.2) | 2 KB | controller | address generator: one entry per visit, read ahead (4.2) |
| `GROUP` | `GROUP[0..63]`, 64 bits each (4.2) | 512 B | controller | address generator: one entry per group, read ahead (4.2) |

**Ports.** Each table is effectively **dual-ported**: a controller side (through the window, one cycle) and an engine side.

| memory | controller side | engine side |
|---|---|---|
| `STREAM`, `GROUP` | write | read (address generator) |
| `PTR` | write | read, increment, write (write engine) |
| `HIST` | **read and write** (the prefix sum between radix passes reads all 256 counts) | read, increment, write (count-on-arrival) |

**`PTR` and `HIST`: how the increment uses the ports** (figure 5.2):
- **Port A** is the engine's read port only. **Port B** is a read/write port shared by the increment's write-back and the controller, **with the increment first**.
- **One read-modify-write per cycle:** port A reads the entry on cycle *t*, and port B writes the incremented value (+2 for `PTR`, +1 for `HIST`) on *t* + 1. The engine's request always starts a read and loads a last-address register; the previous cycle's request drives the write.
- **Back-to-back increments of one entry:** the read result is ignored and the last value is used. So the macro must let the write land intact when port A reads the address port B is writing (to confirm with the memory-compiler numbers, section 13).
- **A controller access that meets an increment:** the increment wins. A controller write is lost, and a controller load returns the value the engine wrote, at the engine's address. Programs avoid it by the access rule below; the behaviour is defined rather than undefined.

**No contention between tables.** The address generator's `STREAM`/`GROUP` reads, the write engine's `PTR` updates and count-on-arrival's `HIST` updates each use their own memory, so none waits for another. Gapless stream runs therefore don't starve the pointer updates. The address generator is the only engine reader of `STREAM` and `GROUP`, so its read-ahead timing is the same on every tile (4.2).

**Placement:** next to the controller (section 12). The controller's accesses through its window take one cycle, and the engines pay the distance: every engine request and return crosses *(param)* `ts_engine_latency` cycles each way (default 1; 1–4 measured).

**Read-modify-writes** (`PTR` and `HIST`) take up to one per cycle each. A **bypass** covers the whole read-to-write-back pipeline, including the write-back cycle, so updates to the same entry at any spacing are not lost. The pipeline is inside each memory, so `ts_engine_latency` doesn't lengthen it. Each has a 16-entry request FIFO (overflow is the table-request-overflow condition, 4.13). A `HIST` count that would exceed 17 bits is an error (invariant 6).

**Controller access.** Through its window (0x2000–0x3FFF, 8.9), decoded to the four memories: `HIST` at 0x2000, `PTR` at 0x2400, `STREAM` at 0x2800, `GROUP` at 0x3000. Only `HIST` can be loaded; `PTR`, `STREAM` and `GROUP` take stores only. One cycle per access, only when no engine is using that memory: **when the address generator is not running (`rd_running` = 0) and `ts_busy` = 0** (8.2). `rd_running` covers a run's lead-in from its start (4.2); `rd_done` is no guide, since it is 0 before a tile's first run. A tile's arrivals can finish before its own address generator does, so the counters alone don't say the tables are free. A window access that meets an engine access is a programming error, with no hardware check: for `PTR` and `HIST` the result is defined (the increment wins), for `STREAM` and `GROUP` it is undefined. There is no hardware clear; the controller writes zeros.

### 4.11 Controller

*Figures 3.1, 3.2, 3.4.*

The tile's processor: an in-order **RV32I + Zmmul + `WFI`** core (hardware multiply `mul`, `mulh`, `mulhsu`, `mulhu`; no divide; no C, A or CSR extensions; no cache).

**Instruction set.**
- **`WFI`** is the only privileged instruction: no CSRs, no traps, no interrupts, no `mtvec`, `mepc` or `mret`. `WFI` stalls the core until a wake event (4.12, 6.3), then continues at the next instruction.
- **Illegal:** divide opcodes (`div`, `divu`, `rem`, `remu`) and CSR instructions set the illegal-instruction condition, and the controller halts. `ecall` and `ebreak` halt it.
- **Programs** are C, built bare-metal (`gcc` for `riscv64-unknown-elf`, `-march=rv32i_zmmul -mabi=ilp32`, with `WFI` from inline assembly) with a start-up file, a linker script and the generated register header `sw/mesh.h`. A C `/` or `%` by a non-power of two compiles to a library call. Register accesses compile to single x0-relative `lw`/`sw`.

**Timing.** One instruction per cycle.
- Every load and store — to data memory, to any register and to the table window — completes in one cycle, and the loaded value is usable by the next instruction (load-use 0).
- A taken branch or jump costs one extra cycle; a multiply takes 3 cycles.
- A store at t updates the register at the end of t; a load at t+1 and the owning unit at t+1 see it. Status computed at t is readable at t+1.
- The only extra cycles a load or store can cost are stalls on the send port's occupied holding register (`ROUTER_SEND_LO`, below). `send_busy` lets a program avoid it.

**Memories (strict Harvard).**

| address | memory | port A | port B |
|---|---|---|---|
| 0x0000–0x1FFF | **instruction memory, 8 KB**; resident loader at 0x1F00–0x1FFF | instruction fetch only | the traffic director (mode 5) |
| 0x2000–0x3FFF | table window (4.10, 8.9) | | |
| 0x4000–0x5FFF | **data memory, 8 KB**; mailbox in its top 1 KB, 0x5C00–0x5FFF | controller loads and stores | the traffic director (modes 5, 6, 7) |
| 0xFFFFF800–0xFFFFFFFF | peripheral registers (section 8) | | |

- **Banks.** Each memory is **two 1K × 32-bit banks** (4 KB each), interleaved by word: even words in one bank, odd words in the other. That makes four macros in all.
- **Port B** writes 2 consecutive words per cycle at any word address (mode 5, 64 bits). Modes 6 and 7 write one word.
- **Port A** touches one bank per access.
- **Bus errors.** The bus-error condition is set and the controller halts on: loads and stores to instruction memory; misaligned and unmapped accesses; sub-word accesses to the registers or the table window; stores to read-only registers; and loads from `PTR`, `STREAM` or `GROUP` (8.1, 8.9). Write-only registers read 0.
- Constants (`.rodata`) and initialised data (`.data`) live in data memory. Every instruction is fetched as stored, so code written into instruction memory through port B runs the next time it is fetched.

**Controller-memory word addresses** (mode 5, mode 7, the `STREAM` field, `SRAM_RD_DEST_ADDR`, `ROUTER_SET_HDR`'s addr). These are 12-bit **packed** word addresses: words 0x000–0x7FF are instruction memory and 0x800–0xFFF data memory, with the mailbox at 0xF00–0xFFF.
- A mode-5 packet whose address is outside both memories, or inside the mailbox range, is a **programming error**, with no hardware check; the simulator checks it (invariant 11).

**No big-SRAM port.** The controller has no memory port onto big SRAM.
- **Copies** from big SRAM into instruction or data memory are mode-5 address-generator runs to the tile itself (4.2; sequence in 6.5).
- **Writes** to big SRAM are mode-2 packets the controller sends to itself, landing at `PTR[me]`.
- **Reads** of big SRAM go through the controller read port (4.3): queued by stores, with results returning as mode-7 packets to the tile itself.

**Send port.** `ROUTER_SET_HDR`, `ROUTER_SET_HI` and `ROUTER_SEND_LO` (8.4) fill a one-packet **holding register** in front of the injection point (4.5).
- **`ROUTER_SET_HDR`** holds dst [6:0] (absolute; 126 = broadcast, 64 = C&C), mode [10:7] and addr [30:11]: 31 bits, 1 spare. The router adds src, making the 102-bit link word.
- **`ROUTER_SET_HI`** holds the payload's high 32 bits.
- **`ROUTER_SEND_LO`** supplies the low 32 bits and sends: the store captures {`ROUTER_SET_HDR`, `ROUTER_SET_HI`, its own data} into the holding register.
- **At that capture,** addr auto-increments by the words written, **+2 for mode 5, +1 for modes 6 and 7** (unchanged for modes that don't use addr), and `ROUTER_SET_HI` clears to 0. Both may therefore change while the packet waits. A run of consecutive words costs one `ROUTER_SET_HDR`, then `ROUTER_SET_HI` + `ROUTER_SEND_LO` per 64-bit packet, or `ROUTER_SEND_LO` alone for 32-bit modes.
- **Timing.** A store at *t* loads the holding register at the end of *t*; its earliest injection is *t* + 1. `send_busy` = the holding register is full. **The store stalls only if the holding register is full and not being injected that cycle**, which gives one packet per cycle when no read return is waiting (gather value, address generator or read port, 4.5). This is a stall rule, not an overflow rule, and deliberately differs from the FIFOs' "count before the pop" rule (4.13).
- **The sender checks nothing:** invalid modes, mode 1 to another tile and dst 65 (unused) go out as written, and the receiver or the simulator catches them.
- C&C's port ignores mode and addr, so packets to C&C (errors, results) may carry anything there.

**The resident loader.** A 64-word loader sits at 0x1F00–0x1FFF , loaded by C&C while the tiles are halted. It is where HALT → RUN starts (6.1). Overlay swaps are done by local mode-5 copies, so the loader's job is boot and image management; its interface is a software convention (the boot document, section 13).

### 4.12 Mailbox, counters and the local wake

*Figures 2.1, 2.3.*

**The mailbox.**
- **Layout:** 256 × 32-bit slots in the top 1 KB of data memory; slot *s* is the word at 0x5C00 + 4*s*.
- **Mode 6** writes slot = addr[7:0]. It is effectively **C&C's command channel**.
- **Mode 7 does not write the mailbox.** Replies go to the data-memory word their addr names:
  - read-port results are mode-7 packets the tile sends itself (4.3), to the word their `SRAM_RD_DEST_ADDR` named;
  - send-port replies (cuts, counts, maxima) go to an address set by convention, typically an array base + the sender's own index.

  A mode-7 write into the mailbox range, or outside data memory, is a **programming error**, with no hardware check; the simulator checks it (invariants 12 and 13). Only mode 6 writes the mailbox.
- **A mode-6 write to slot 0 sets "command landed"** (6.3). No other mailbox write affects control.

| slots | use (software convention) |
|---|---|
| 0 | C&C: the command word, written last |
| 1–63 | C&C: the command's arguments, written before slot 0 |
| 64–255 | free for other mode-6 uses; the network will carry more than the sort |

- **Mode 5 and the mailbox range:** a mode-5 write into the mailbox range is likewise a programming error (invariant 11).
- Commands are signalled by "command landed", and replies are counted by `DMA_REPLY_REMAINING`.

**Three down-counters.** Each is written by software with the number of arrivals it expects. Each arrival of its kind decrements it when the traffic director accepts the packet:

| counter | counts down | done when |
|---|---|---|
| `DMA_SRAM_REMAINING` | modes 2–4 (big-SRAM writes, including gather values) | 0 and `mem_idle` (4.1) |
| `DMA_CM_REMAINING` | mode 5 (controller-memory writes) | 0 and `cm_idle` |
| `DMA_REPLY_REMAINING` | mode 7 (replies) | 0 and `cm_idle` |

- **`cm_idle`** (a status bit): the port-B pipeline is empty. It drains within `cm_latency` cycles of the last accepted packet.
- **Each packet counts 1,** including a 64-bit mode-5 packet.
- **Width: 20 bits each.** A gather of 64K values at *k* = 2 counts 131,072 packets, and *k* = 8 at the largest X needs 20 bits.
- **Modes 1 and 6 are never counted.** Mode 1 goes to the GATHER engine (its gather values count), and mode 6 is signalled by "command landed". Read-port results are mode-7 packets, so they count in `DMA_REPLY_REMAINING`.
- **Separate counters let different traffic overlap and be tracked on its own,** for example a split's searches and an overlay copy.
- **An arrival at a counter already at 0** is unexpected traffic. It leaves the counter at 0 and sets the unexpected-arrival condition (4.13).
- **Packets from C&C (src 64) are never counted, in any of the three counters.** Boot loads controller memory by mode 5 from C&C while the counters are 0, and staging arrives while tiles wait for a command; counting either would trip unexpected arrival. C&C's data is known to have landed because its command follows it (in-order delivery).
- **A store and an arrival in the same cycle:** the store wins, and the arrival is judged against the **old** count, so it sets unexpected arrival if the old count was 0: `next = store ? N : (arrive ? max(count − 1, 0) : count)`, `unexpected = arrive & (count == 0)`.

**The local wake.** `WFI` has two wake sources: **GO** (the global wake, 6.3) and the **local wake**.
- **The local wake is the rising edge of "every counter is done":** all three at 0, with `mem_idle` and `cm_idle`, **counted only if some counter was non-zero since the latch last fired** (the qualifier). Without the qualifier, an uncounted packet passing through port B (a command, C&C traffic) drops `cm_idle` briefly and makes a spurious edge that wakes `wait_for_command()` without a GO. So the tile wakes when the last outstanding counter it was waiting on reaches 0 and the writes have cleared.
- **The edge is latched,** so an edge before `WFI` is not lost: if a copy finishes before the core reaches `WFI`, the `WFI` falls straight through. `WFI` consumes the latch.
- **The latch is cleared** by a store to any counter (so every new wait starts clean), by the `done` pulse and by HALT → RUN. A set and a clear in the same cycle: the clear wins. The qualifier is cleared by the same three events.
- **Polling and `WFI`, and the end of a phase.** Within a phase a program may poll one counter to 0 and act on it, then `WFI` for the rest. The latch persists until a `WFI` consumes it or a counter store clears it, so that `WFI` returns at once if everything has already landed, and waits otherwise. **Rule: a phase that sets counters must end with a `WFI` before it pulses `done`.** That `WFI` proves the arrivals are in (invariant 4); `done` with a counter still non-zero is the done-early condition (4.13). No store-to-clear idiom and no wake mask are needed.
- **A count of 0 never makes an edge.** A phase that expects nothing must not `WFI` for a local wake.
- **Reset and HALT.** At chip reset and in HALT the latch, the qualifier and the `done` gate (below) are all 0; the gate is not held in HALT (the core does not run `WFI` there), and on HALT → RUN no counter is set, so nothing can make an edge before the first GO.
- **The wake term:** `WFI` completes when **GO arrives or the local-wake latch is set**: `wake = GO | local_latch`. The `done` gate acts on the latch's set input, not on this term, so GO always wakes. A wake consumes the latch. GO enters the term through one register (`go_q`), so a GO pulse at cycle *t* wakes the core at *t* + 1, and a latch set at the end of cycle *t* releases a waiting `WFI` in cycle *t* + 1.
- **The `done` gate:** from the `done` pulse until GO, **only GO can wake the core.** The local-wake latch cannot be set in that window (an edge then is ignored), and the `done` pulse clears it. So `wait_for_command()` can only ever be woken by GO, whatever a program gets wrong. A down-counter still non-zero at `done` is a program error, reported as the **done early** condition (4.13); an arrival after `done` for a counter already at 0 is **unexpected arrival**.

**Uses:**
- a block copy (6.5);
- a receiving phase (6.4);
- a search step through the read port: `DMA_REPLY_REMAINING` = the number of reads, then `WFI` (4.3).

**The local wake waits for every counter.** A tile cannot `WFI` for one class of arrivals while another is expected; it polls that counter instead (the rule above). Polling covers this at no hardware cost, so there is no per-counter wake mask.

### 4.13 Status and conditions

*Figure 2.4.*

Two read-only registers: **`TILE_STATUS`** (8.2) holds what a program reads to steer itself (the live bits, the tile index, the big-SRAM size); **`TILE_CONDITIONS`** (8.3) holds the error conditions.

**Live bits** (in `TILE_STATUS`):
- `rd_running`, `rd_done` (4.2);
- `send_busy` (4.11);
- `ts_busy` (4.14);
- `mem_idle`, `cm_idle` (4.1, 4.12);
- `done`, "command landed" (6.3);
- GATHER busy (4.4).

**Conditions** (`TILE_CONDITIONS`). **A condition means the software did something wrong** (or the hardware is faulty): in a correct program on correct hardware, `TILE_CONDITIONS` reads 0 forever. Anything informational belongs in the simulator's reports, not here. Each condition is one of two kinds, marked in the table: a **program error** (P), which a program can cause, and a **hardware-fault detector** (H), which no program on a correct design can cause, so it can only mean a fault. Both raise the error line alike.

| bit | condition | set when |
|---|---|---|
| 0 | drift | in a group run, the SRAM read queue refused an address-generator request, so the tile fell behind its timetable (4.2): a placement error, too much traffic on one area. Zero-length groups and group starts are timetable, not lateness (4.2) |
| 1 | router overflow | a link, eject or C&C FIFO overflowed (4.7, 4.16) |
| 2 | write-engine input overflow | 4.9 |
| 3 | read-port overflow | a `SRAM_RD_ADDR_QUEUE` store with no free entry (4.3) |
| 4 | unexpected arrival | a packet arrived for a down-counter already at 0 (4.12) |
| 5 | table request overflow | the `PTR` or `HIST` request FIFO (4.10). **(H)**: every engine issues at most one request per cycle and the FIFO drains one per cycle [meas: meshsim] |
| 6 | bus error | 4.11 |
| 7 | illegal instruction | 4.11 |
| 8 | GATHER queue overflow | a mode-1 packet arrived with the GATHER engine's 16-entry request queue full (4.4). **(H)**: mode 1 only comes from the tile itself, and both of its sources share a resource with the engine: the address generator yields the SRAM read queue to it, and the send port shares injection with its gather values (4.4) |
| 9 | bad mode | a packet with mode 0 or 8–15 arrived, and was dropped (3.1, 4.8) |
| 10 | done early | `done` was pulsed while a down-counter was non-zero: the tile declared itself finished with arrivals outstanding (4.12, 6.3) |

Bits 11–31 are reserved and read 0. **Kinds:** 5 and 8 are hardware-fault detectors (H); the rest are program errors (P): 0, 1, 2, 3, 4, 9 and 10 always; 6 and 7 also on a fault (a corrupted image).

- **Read-only to the controller.** Nothing in RUN clears a condition, so a program cannot hide one. A store to `TILE_CONDITIONS` is a bus error (condition 6). **Entering HALT clears them all** (the RUN → HALT edge, and chip reset); conditions from loading a halted tile therefore survive into RUN, where the loader can check them.
- **Any non-zero condition raises the tile's error line** to C&C (6.6), so C&C learns of it at once.
- **A program can still detect new conditions in a phase** by reading `TILE_CONDITIONS` at its start and comparing at its end.
- **What earns a condition:** a condition exists for a mistake that would otherwise **go unnoticed or surface far from its cause**: silent corruption or lost data (done early, unexpected arrival, every overflow, drift, bad mode). A mistake that **stops the machine** (a tile that hangs) needs no condition: C&C's watchdog and READY bits catch it and name the tile. A misuse the hardware **ignores**, leading to such a hang (a start while a run is running, a 1 written to a reserved `DMA_PULSE` bit), is flagged by the simulator only.
- **Same-cycle rule and overflows:** a condition set in the same cycle as the clear on entering HALT is cleared (the clear wins, as everywhere else). **On every overflow the arriving item is dropped**: the packet, write, request or gather request that found its FIFO or queue full is lost, and only the condition records it (conditions 1, 2, 3, 5 and 8). **"Full" is the count before the cycle's pop:** a push into a full FIFO overflows even if an entry leaves in the same cycle (no bypass from pop to push), so no FIFO ever holds more than its depth.
- **Setting a condition does not stop the machine.** Bus errors, illegal instructions, `ecall` and `ebreak` halt the tile's controller only; its engines and router keep running.

### 4.14 Commit ordering

There is no coherence (4.1), so "committed" matters only at phase boundaries. A tile knows a phase's writes are readable when its counters are done:
- **big SRAM:** `DMA_SRAM_REMAINING` = 0 and `mem_idle`, which means every expected payload was accepted and every write has reached its area;
- **controller memory and the mailbox:** `DMA_CM_REMAINING` or `DMA_REPLY_REMAINING` = 0 and `cm_idle`, after which a load reads the word.

The local wake (4.12) fires on exactly this condition.

**Counters-ready:** `ts_busy` → 0 (4.10) is one bit for the four table memories. It is the OR of: an engine put a table request within the last `ts_engine_latency` + 1 cycles; `PTR` or `HIST` has a request arriving or queued, or took its last read-modify-write too recently for the result to be readable.

There is no flush.

### 4.15 Digits (shared definition)

Mode 3 and count-on-arrival select a **digit** of the 32-bit key in the payload's bits 31–0, with a 3-bit selector in `DMA_CONFIG` (8.5):

| selector | meaning |
|---|---|
| 0 | none (count-on-arrival off; mode 3 not used) |
| 1–4 | byte *d* of the key, bits [8*d* − 1 : 8(*d* − 1)]; **1 is the least significant byte** |

There are two selectors: the **bucket digit** (mode 3, `DMA_CONFIG` [20:18]) and the **histogram digit** (count-on-arrival, `DMA_CONFIG` [23:21]).

### 4.16 Tile 0's fifth router port (C&C)

*Figure 4.1.*

Tile 0's router has a fifth input and a fifth output, both for C&C (section 6). **All differences are in tile 0's router**: the controller, the memories, the engines and every other tile are the same as on tiles 1–63.

- **Input "from C&C":** a sixth route block (4.6). A packet from C&C (src = 64) is routed as a packet injected at tile 0: unicast as from tile 0, and a broadcast rooted at tile 0. It does not pass through tile 0's injection point (4.5), so it never waits behind tile 0's address generator.
- **"From C&C" FIFOs:** each of tile 0's four link outputs and its eject output gains a "from C&C" FIFO (five FIFOs, 16 entries), and each of those selectors gains an input. C&C therefore gets a fair round-robin share of every output, even while tile 0's address generator injects every cycle.
- **Output "to C&C":** FIFOs from left, right, up and down and from tile 0's own injection (five FIFOs, 16 entries), with their own round-robin selector, one packet per cycle to C&C. Every packet for dst 64 leaves here instead of through tile 0's eject path. With READY lines (section 6), it carries only errors and results. The output has no back-pressure: C&C accepts one packet a cycle into its receive FIFO (6.6).
- **Cost:** ten extra FIFOs of 16 × 102 bits (~16 Kbit of flops) plus the wider selectors, ≈ 0.02–0.03 mm², tile 0 only (section 11). Their overflows are reported like the other router FIFOs.
- **Pacing is policy.** Every packet C&C sends during an all-to-all adds load to tile 0's links that the schedule's queue bounds don't count (one broadcast per 64 cycles raised tile 0's link queues to 12 of 16 [meas: meshsim]). So C&C sends commands and bulk data between passes, or paces them.

---

## 5. Timing summary

Network: injection at cycle t → route decision (1) → output FIFO (≥ 1) → link (`link_latency`) → route block (1) → … → eject FIFO → selector → traffic director → write engine, GATHER engine or port B. Minimum latency is **3 + 4h cycles for h hops** with 3-cycle links [meas: meshsim]; queueing adds a bounded amount.

| path | timing |
|---|---|
| big SRAM | one access per area per cycle, in arrival order; a read's latency is its queueing plus the array's read latency, both *(Open)* for the hardware (4.1) |
| counters and the local wake | a phase's data is readable when every counter is done (4.12, 4.14) |
| table memories | engine requests and returns cross `ts_engine_latency` each way; controller window one cycle (4.10) |
| controller-memory path | modes 5–7 reach port B `cm_latency` = 2 cycles after the director [built] |
| controller | one instruction per cycle; loads and stores to data memory, registers and the table window in one cycle, load-use 0; taken branch +1; multiply 3; stalls only on an occupied send holding register, or in `WFI` (4.11) |
| registers | a store at t is seen at t+1 by loads and by the owning unit; status computed at t is readable at t+1 |
| address generator | a run's first request is offered 4·`ts_engine_latency` + 3 cycles after the start store for a group run (S_0), 2·`ts_engine_latency` + 2 for a stream run; one cycle less counted from the cycle the start takes effect (4.2) |
| GO | a pulse at t wakes every core in `WFI` at t+1 and clears `done` and "command landed"; engines start at the start store (`DMA_PULSE` [1]) that follows (6.3) |
| READY lines | READY fires when "command landed" and `done` are both 1; a pulse at s sets the tile's bit at C&C, readable from s + `ready_latency` + 2; equal for every tile |
| RUN/HALT | a change at C&C reaches every tile `run_latency` cycles later |

---

## 6. Control

*Figures 1.2, 2.1, 2.2, 2.5.*

C&C sequences the phases. It holds the tiles in HALT or lets them RUN, writes commands into their mailboxes, collects their READY pulses and pulses GO. It does little work but sits on the critical path of every phase transition, so it is on-die and fast rather than driven by the host over the slow bus.

### 6.1 RUN/HALT and reset

- **The line.** One line from C&C to every tile, with a fixed latency like GO (`run_latency`). Chip reset puts every tile in HALT.
- **The tile's edge.** Both RUN/HALT edges come from one register, `run_q`. HALT → RUN gives a one-cycle pulse; RUN → HALT clears the conditions.
- **In HALT:**
  - the controller does not fetch;
  - the address generator, the GATHER engine, the read port and the token FIFO are held in reset: the address generator is idle, the GATHER engine's queue and its request in progress are cleared, the read port's queue and in-flight count are cleared, and the token FIFO is empty. `rd_done` keeps its value;
  - the routers, the traffic director and the write engine run, so packets land exactly as in RUN (4.8);
  - `done` is forced to 1 (6.3).
- **HALT → RUN resets the core only:** PC = **0x1F00** (the resident loader) and x1–x31 = 0. Memories, registers and engines keep their values; `done` and "command landed" are cleared (6.3), and so are the local-wake latch and its qualifier (4.12).
- **A controller halted** by a bus error, an illegal instruction, `ecall` or `ebreak` stops fetching while the tile stays in RUN, with `done` not forced. It restarts only on HALT → RUN.
- **Entering HALT clears every condition** (`TILE_CONDITIONS`, 4.13).
- **There is no boot ROM.** C&C loads the loader and the first images while the tiles are halted.
- **No packet in flight** when the RUN/HALT line changes: a simulator check [built].

**Chip reset.** Every tile starts halted. The router FIFOs, the token FIFO, the area queues, the write engine's input FIFO and the send holding register are empty (`send_busy` 0); the round-robin pointers, `rd_done` and `rd_running` are 0; the down-counters, `TILE_CONDITIONS`, the local-wake latch and the read port's queue are 0 or empty, and `TILE_STATUS` shows the idle machine. The configuration registers are undefined (8.1). The tables, the controller memories and big SRAM are undefined.

### 6.2 Boot

1. **Load while halted.** With every tile halted, C&C broadcasts the loader and the first image by mode 5 into instruction and data memory. Staging into big SRAM waits for RUN, when the loader has set `PTR[64]` (below).
2. **The marker.** C&C then sends **one marker word to mailbox slot 0 (mode 6)**. It sets "command landed", and since `done` is forced to 1 in HALT, READY fires at once: boot uses the general rule (6.3), with no special case.
3. **RUN.** C&C waits for all 64 READY bits, then raises RUN, which clears `done` and "command landed".

- **No timing assumption.** In-order delivery (3.2) guarantees that everything sent before the marker has landed when the marker has.
- **The marker can be the loader's first command;** it is in slot 0 when the loader starts.

**Staging later images and overlays:**
- **How:** C&C broadcasts images and overlays into big SRAM as **mode-2 packets**. Since C&C's src is 64, they land at each tile's `PTR[64]`, which the loader sets at boot to its own staging area.
- **Where:** in area A7 below its top 128 KB. **The top 128 KB of A7 is sort scratch** (skew handling's values staging needs up to ≈ 4.7K values beyond areas A0 + A3; section 14), so images and overlays must fit in A7's lower 384 KB.
- **C&C needs no addresses.** The staging area can sit relative to the top of memory, which depends on the device's size (`TILE_STATUS`).
- **Packets are 64-bit:** `ROUTER_SET_HI`, then `ROUTER_SEND_LO`, so two stores per packet and the same number of stores per word as 32-bit packets, with half the network load.
- **Images land back to back,** `PTR[64]` advancing by 2 per packet. Each image starts on an even chunk address and is padded to an even length. The loader learns each image's start from the command that names it, or resets `PTR[64]` per image.
- **When:** only while the tiles are halted or waiting for a command, never during a mesh operation. Staging packets are never counted, since C&C's packets (src 64) never are (4.12).
- **Running a staged image:** the tile copies it into controller memory with a mode-5 loopback stream (6.5).
- **At first boot** `PTR[64]` isn't set yet, so the loader and the first image go in by mode 5, as above.

### 6.3 `done`, READY, GO and `WFI`

**Two bits per tile:**
- **`done`** says the tile has finished its work and can take the next command. It is:
  - forced to 1 in HALT;
  - cleared by GO and by HALT → RUN;
  - set by the controller with `DMA_PULSE` [0].
- **"Command landed"** is set by a mode-6 write to mailbox slot 0, and cleared by GO and by HALT → RUN.

**READY is automatic.** When "command landed" and `done` are both 1, the tile pulses its READY line once. The two can become true in either order, so a command may land before or after the tile finishes. The controller never pulses READY directly.

**The READY line** is its own line, one per tile to C&C, at equal latency `ready_latency`: a one-cycle pulse on the rising edge of (`done` & "command landed"), setting a sticky bit at C&C.

**The command convention.** C&C writes any arguments first, then the command word to slot 0. In-order delivery means the whole command has landed when "command landed" is set, **provided all of a command's packets to a tile are of one kind**, all broadcast or all unicast (3.2).
- **The command word** (`sw/mesh.h`): [31:24] type, [23] parity, [22:0] argument.
- **Alternating argument blocks:** even-parity commands put their arguments in slots 1–31, odd-parity commands in slots 32–63, and the tile reads the block its command's parity names. A command sent right after GO can then never overwrite the arguments of the one the tile is still reading. Slot 0 of the next command lands at the earliest 4 cycles after GO; the simulator checks that no slot-0 write lands before the previous command word was read [meas: meshsim].

**GO** is a single global net from C&C, balanced so every tile sees a pulse on the same cycle. **GO only wakes `WFI`**, and clears `done` and "command landed". It does not start the address generator; the controller starts it with the start pulse (`DMA_PULSE` [1], 4.2). GO is not latched: it wakes only a core that is in `WFI`, so there is no stale wake.

**`WFI`** stalls the core until a wake event, then continues at the next instruction. There are two wake events:
- **GO;**
- **the local wake** (4.12), the latched rising edge of every counter being done.

**`wait_for_command()`**: the controller pulses `done` and then executes `WFI` **immediately**, and on waking **reads mailbox slot 0 and returns the command word** (the simulator checks that every command word is read before the next lands), as two back-to-back instructions in a `sw/mesh.h` inline helper.
- This is safe because GO cannot come back sooner than the READY round trip (≈ `ready_latency` + 3 cycles), so the core is always in `WFI` when GO arrives.
- `done` stays an explicit pulse rather than "being in `WFI`", because a tile also uses `WFI` to wait for arrivals, which mustn't look finished to C&C.
- READY waits for `done`, so a command that lands before the tile waits is not lost.

**A one-command phase** (a local sort, a gather):
1. The tile finishes its previous work and calls `wait_for_command()`.
2. C&C writes the command, slot 0 last. When it has landed and `done` is set, READY goes out.
3. When all 64 READY bits are set, C&C clears them and sends GO. Every core wakes, reads its command and runs the phase.

**A phase whose engines must start together** (an all-to-all) uses two commands:
1. **"Configure":** the tile wakes, writes its tables and `DMA_CONFIG` for the run, and calls `wait_for_command()`.
2. **"Start"**, a one-word command: the tile wakes, stores its counters (6.4) and pulses start (`DMA_PULSE` [1]).

Every core leaves `WFI` on the same cycle and runs the same fixed code to the start store (no caches, single-cycle loads), so the engines start on the same cycle on every tile, and the start's lead-in (4.2) is the same everywhere. **The path from `WFI` to the start store must be identical code on every tile.**

**Costs:** each command costs one READY → GO round trip, ≈ `ready_latency` + 3 cycles [meas: meshsim] plus C&C's poll loop: ≈ 11 cycles at `ready_latency` 8, under 1K cycles over about 40 phases [estimate].

### 6.4 Phases that receive

- **A tile whose phase receives** sets its counters **in the start path, after GO: store the counters first, then pulse start.** Setting them during "configure" would break invariant 4 at configure's `done`; storing them before the start pulse means no arrival can beat its counter. The start path is identical code on every tile, so every tile's start store lands on the same cycle (plus skew), and its first request follows by the lead-in of 4.2. Measured from C&C's GO store to every tile's first request [meas: meshsim, L = 1]: **9–10 cycles** for a stream run started by a counter store then start (5–6 to the start store, then 2L + 2), and **17–18** for a group run on the all-to-all recipe's longer start path (10–11, then 4L + 3); a group run with the shortest start path would take 12–13 [derived from the two measured parts]. Every tile's first request lands within the 0/+1 start skew. The tile then `WFI`s for the local wake (4.12) when it has nothing else to do.
- **Every expected arrival must have landed and every write cleared before the tile pulses `done`** (invariant 4). The local wake guarantees it.
- **A tile that only sends** in a phase still waits for `mem_idle` before `done`, because its own loopback writes count.

### 6.5 Block copy into controller memory (overlays, tables)

1. Write a `STREAM` entry (two stores, or one if only the source changes): count, source, *d* = 0, mode 5, read size 64, controller-memory destination.
2. Write `DMA_CM_REMAINING` = the number of packets (records). This also clears the local-wake latch.
3. Write `DMA_CONFIG`: a stream run of one stream, starting at that entry.
4. Pulse start (`DMA_PULSE` [1]).
5. `WFI`: the local wake fires when `DMA_CM_REMAINING` reaches 0 and the port-B pipeline is empty.

The copy runs at 64 bits per cycle through the loopback (4.2), with no polling and no C&C traffic. The code doing the swap must sit outside the region being overwritten: the resident loader, or any code the overlay does not replace.

### 6.6 The C&C controller and its registers

- **The C&C controller** reuses the tile controller design (4.11): RV32I + Zmmul + `WFI`, the same toolchain, its own `mesh.h` from the same register-table generator.
  - Its memories differ: a large program RAM holding the whole sequencer with no overlays (proposal: 16–32 KB), loaded once from the FPGA at boot, and a small data RAM (a few KB).
  - No big SRAM, no engines.
  - About 0.04 mm², once (section 11).
- **Registers** (the block is not fully specified; names *(Open)*, section 13):
  - a **send** holding register into tile 0's "from C&C" route block (4.16): one packet per cycle, any dst (126 for broadcast), any mode and address, src = 64;
  - the **READY bits:** 64 sticky bits, bit s set by a pulse on tile s's READY line, read as two 32-bit halves and cleared together by a reset store, where a pulse arriving in the cycle the reset takes effect is kept;
  - the **GO-pulse** and **RUN/HALT** registers;
  - host-interface registers.
- **The error line:** each tile drives "my `TILE_CONDITIONS` is non-zero"; the lines are ORed (a tree across the tiles, like the READY lines but without the equal-latency requirement) into **one sticky error bit at C&C**, which is also a wake source for C&C's `WFI`. C&C learns *that* something is wrong at once, and *when* from the phase it is running.
- **REPORT** (a debugging command; a correct program never needs it): when the error bit is set, C&C stops issuing GOs, so every tile finishes its phase and waits in `wait_for_command()` with its conditions intact; C&C then broadcasts a REPORT command.
  - **The settle.** REPORT usually lands on the command C&C sent right after the last GO, which the tiles have not read; that command is discarded, and its READY has already fired, so READY cannot say when REPORT has landed. C&C therefore waits a fixed settle between sending REPORT and its GO: **twice the worst-case latency of a one-packet broadcast from C&C to its farthest tile** (tile 36, 8 hops from tile 0's root), 2 + `intra_tile_latency` + `cm_latency` + 8 × (`link_latency` + 1) = 37 cycles at the defaults [meas: meshsim, exact at `link_latency` 2–4, `cm_latency` 2–4, `intra_tile_latency` 1–2, with and without start skew], so 74 cycles. The mesh is idle then, since every tile has reported READY (a crashed tile excepted, whose wait C&C's watchdog ends).
  - **The replies.** Each tile, on waking, waits in a software delay loop of about (`me` + 1) × 4 cycles (the core's timing is deterministic), then sends C&C one packet: its tile index and `TILE_CONDITIONS`. The replies are thus paced one every 4 cycles, about 260 cycles in all, with no fan-in risk.
  - C&C logs them, then halts the machine, which clears every condition. A tile stuck in an error that never reaches `wait_for_command()` does not reply; C&C's watchdog and READY bits identify it. Recipe: `sw/RECIPES.md`.
- **Receive path:** packets for dst 64 arrive from tile 0's "to C&C" output (4.16), at most one per cycle, and land in a **receive FIFO at C&C** (at least 64 entries of 64-bit payload plus the 7-bit src), read through a register pair (data, and src with a valid bit). C&C's port ignores the mode and address fields. A packet arriving with the FIFO full is lost and sets a C&C error flag (its own flag, not the tiles' error bit; meshsim reports each loss). Room freed by a read is room from the next cycle.
- **Pacing.** C&C sends bulk data between all-to-all passes, or paces it (4.16). **C&C waits `run_latency` + 1 cycles after a RUN/HALT store, and `go_latency` + 1 cycles after each GO store, before sending.** Otherwise a command can reach tile 0 before RUN or GO does: before RUN, it breaks invariant 7 or is cleared by HALT → RUN; before GO (at `go_latency` ≥ 6), GO clears its "command landed", READY is lost and the machine hangs.
- **Commands may cross the mesh during a phase:** READY needs a landed command, so the next command is sent right after GO, one broadcast packet, which adds at most one packet to any link. It is safe because GO clears "command landed" before the next command can land.
- **Wake sources for C&C's `WFI`.** C&C waits through every phase, so it sleeps rather than polls. **The error bit** (any tile's `TILE_CONDITIONS` non-zero) is a wake source. Three more are *(Proposed)*:
  - **all 64 READY bits set** (the edge), so the sequencer loop is: send the command, `WFI`, clear the READY bits, GO;
  - **a packet arriving on C&C's receive path** (an error or result from a tile);
  - **a watchdog timer,** set by C&C per phase, so a tile that never reports READY wakes C&C instead of hanging it.

  The wake events and the watchdog register belong to C&C's register block, which is not yet specified.

---

## 7. Memory subsystem

*Figures 1.1, 1.2.*

The memory subsystem **hangs off C&C**, not off the mesh: tiles never address it, and it has no mesh address (65 is unused). C&C moves data between it and the tiles with its ordinary traffic (mode-2 broadcasts and unicasts, mode-5 loads, 6.2, 6.6). Its internals and its link to C&C are out of scope.

---

## 8. Registers

*Figures 3.2, 3.3, 3.4.*

### 8.1 Register map and general rules

The tile has **13 registers**. Register *i* is at 0xFFFFF800 + 4·*i*; the offset below is the address's low 12 bits. They are grouped by owner: `TILE_` for the whole tile, `ROUTER_` for the send port (it puts packets into the network), `DMA_` for the engines and their arrival counters, and `SRAM_` for the read port (the controller's reads of big SRAM). `sw/mesh.h` (version 1.0) and `sw/REGISTERS.md` are generated from the same register table and match this section.

| offset | register | access | function | section |
|---|---|---|---|---|
| 0x800 | `TILE_STATUS` | R | live bits, tile index, big-SRAM size | 8.2 |
| 0x804 | `TILE_CONDITIONS` | R | the error conditions | 8.3 |
| 0x808 | `ROUTER_SET_HDR` | W | dst, mode and addr of outgoing packets | 8.4 |
| 0x80C | `ROUTER_SET_HI` | W | high 32 bits of the next payload | 8.4 |
| 0x810 | `ROUTER_SEND_LO` | W | low 32 bits; sends | 8.4 |
| 0x814 | `DMA_CONFIG` | W | the run and the receive-side settings | 8.5 |
| 0x818 | `DMA_PULSE` | W | `done` and start pulses | 8.6 |
| 0x81C | `DMA_SRAM_REMAINING` | R/W | down-counter for modes 2–4 | 8.7 |
| 0x820 | `DMA_CM_REMAINING` | R/W | down-counter for mode 5 | 8.7 |
| 0x824 | `DMA_REPLY_REMAINING` | R/W | down-counter for mode 7 | 8.7 |
| 0x828 | `DMA_RECORD_BASE_ADDRESS` | W | the run's record buffer, for augmenting and gathers | 8.7 |
| 0x82C | `SRAM_RD_ADDR_QUEUE` | R/W | write: queue a read; read: free entries | 8.8 |
| 0x830 | `SRAM_RD_DEST_ADDR` | W | the data-memory word for the next read-port result | 8.8 |

**General rules:**
- Registers sit in the top 2 KB (0xFFFFF800–0xFFFFFFFF), each one `lw`/`sw` with `rs1 = x0`.
- **Write-only registers read as 0,** not a bus error.
- **Sub-word accesses** (`lb`, `lh`, `lbu`, `lhu`, `sb`, `sh`) to a register are a bus error; registers are `lw`/`sw` only.
- **A store to a read-only register, or any access to an unmapped offset** (0x834–0xFFC), is a bus error.
- **Reserved and spare bits** are ignored on a store and read 0; meshsim also flags a 1 written to either of `DMA_PULSE`'s reserved bits [3:2].
- **Reset values:** at chip reset the down-counters, `TILE_CONDITIONS`, the local-wake latch and the read port's queue are 0 or empty, and `TILE_STATUS` reflects the idle machine (6.1). **The configuration registers** (`DMA_CONFIG`, `DMA_RECORD_BASE_ADDRESS`, `ROUTER_SET_HDR`, `ROUTER_SET_HI`, `SRAM_RD_DEST_ADDR`) **are undefined at chip reset and kept across HALT → RUN**; software writes each before use. The tables' contents are likewise undefined at reset.
- Configuration registers are static during a phase.

### 8.2 `TILE_STATUS` (0x800, read-only)

| field | bits | reset |
|---|---|---|
| `rd_running`: the address generator is running, from the end of the start store's cycle (lead-in included) until it has walked the run's last entry (4.2) | [0] | 0 |
| `rd_done`: the last run has finished; cleared by the next start; 0 before a tile's first run (4.2) | [1] | 0 |
| spare (reads 0) | [2] | 0 |
| `send_busy`: the send holding register is occupied (4.11) | [3] | 0 |
| `ts_busy`: a table memory is in use by an engine (4.14) | [4] | 0 |
| `mem_idle`: the big-SRAM write path is empty (4.1) | [5] | 1 |
| `cm_idle`: the port-B pipeline is empty (4.12) | [6] | 1 |
| `done` (6.3); forced to 1 in HALT | [7] | 1 |
| "command landed" (6.3) | [8] | 0 |
| GATHER busy (4.4) | [9] | 0 |
| spare (reads 0) | [20:10] | 0 |
| tile index (0–63) | [26:21] | the tile's index |
| spare (reads 0) | [28:27] | 0 |
| big-SRAM size in 512 KB areas − 1 (0–7: 1 to 8 areas, 4.1) | [31:29] | the device's size |

A load returns the state at the end of the previous cycle; status bits are registered at their source (section 12). A store is a bus error. The size field sits at the top of the word, where a shift alone extracts it.

### 8.3 `TILE_CONDITIONS` (0x804, read-only)

The conditions of 4.13, bit *n* as in its table: 11 conditions in [10:0]; [31:11] reserved, reading 0. Read-only: a store is a bus error. Cleared only on entering HALT. Non-zero raises the tile's error line (6.6).

### 8.4 The send port: `ROUTER_SET_HDR`, `ROUTER_SET_HI`, `ROUTER_SEND_LO` (0x808–0x810, write-only)

| register | bits | function |
|---|---|---|
| `ROUTER_SET_HDR` | dst [6:0], mode [10:7], addr [30:11], spare [31] | for outgoing packets; sticky; addr auto-increments at each send (+2 mode 5, +1 modes 6 and 7, 4.11) |
| `ROUTER_SET_HI` | [31:0] | the payload's bits 63–32; sticky until the next send, then cleared to 0 |
| `ROUTER_SEND_LO` | [31:0] | the payload's bits 31–0; puts the packet in the send holding register, stalling if it is occupied and not being injected that cycle (4.11) |

### 8.5 `DMA_CONFIG` (0x814, write-only; reads 0)

One 32-bit register configures the tile for an engine run, including the run itself.
- It is **written once per engine run**, before the start, and not updated while the run is running or the phase's arrivals are still landing: the write engine and the GATHER engine read the digits and *k* as packets arrive. A one-command local phase rewrites it between passes.
- It **contains no action bits;** those are in `DMA_PULSE`.

| field | bits |
|---|---|
| run kind: 0 stream run, 1 group run (4.2) | [0] |
| run start: first stream (0–255) or first `GROUP` entry (0–63) | [8:1] |
| run count: streams (1–256) or groups (1–64). A count of 0 is a run with no entries: it ends at once (`rd_done`), with no requests. Stream or group numbers past the table's end wrap within the table (meshsim flags them) | [17:9] |
| bucket digit (mode 3): 0 none, 1–4 = key byte, 1 = least significant (4.15); 5–7 reserved, treated as 0 (meshsim flags them) | [20:18] |
| histogram digit (count-on-arrival): 0 off, 1–4 (4.15); 5–7 reserved, treated as 0 (meshsim flags them) | [23:21] |
| gather value size *k* − 1, in records, *k* = 1–8 (4.4) | [26:24] |
| spare | [31:27] |

### 8.6 `DMA_PULSE` (0x818, write-only; reads 0)

Writing 1 to a bit fires that action for one cycle; writing 0 does nothing.

| bit | pulse |
|---|---|
| [0] | `done` (6.3); READY then fires automatically when a command has landed |
| [1] | start: load the run in `DMA_CONFIG` and begin it (4.2); ignored while a run is running |
| [3:2] | reserved |

In one store, start acts before `done`.

### 8.7 The down-counters and `DMA_RECORD_BASE_ADDRESS` (0x81C–0x828)

| register | access | bits | function |
|---|---|---|---|
| `DMA_SRAM_REMAINING` | R/W | count [19:0] | down-counter for modes 2–4 (4.12) |
| `DMA_CM_REMAINING` | R/W | count [19:0] | down-counter for mode 5 (4.12) |
| `DMA_REPLY_REMAINING` | R/W | count [19:0] | down-counter for mode 7 (4.12) |
| `DMA_RECORD_BASE_ADDRESS` | W | chunk address [19:0] | the run's record buffer: augmenting's landing buffer, ordinal = (landing chunk − base) / 2 (4.9); or a gather's value buffer, value address = base + 2*k*·ordinal (4.4). One base per run: no run uses both |

- **A counter store** of *N* sets the count to *N* and clears the local-wake latch and its qualifier (4.12). **A load** returns the count. Bits [31:20] are spare.
- **`DMA_RECORD_BASE_ADDRESS`** holds for the run, like `DMA_CONFIG`. Bits [31:20] are spare.

### 8.8 The read port: `SRAM_RD_ADDR_QUEUE`, `SRAM_RD_DEST_ADDR` (0x82C, 0x830)

| register | access | bits | function |
|---|---|---|---|
| `SRAM_RD_ADDR_QUEUE` | R/W | store: chunk address [19:0]; load: free entries [31:0] | a store queues a 32-bit read of that chunk; a load returns the free entries in the request queue, at least 64 when it is empty (4.3) |
| `SRAM_RD_DEST_ADDR` | W | packed word [11:0] | the packed data-memory word for the next read-port result; advances by 1 per queued read, wrapping within 12 bits (4.3) |

### 8.9 The table window (0x2000–0x3FFF)

| table | window | entries | load | store |
|---|---|---|---|---|
| `HIST` | 0x2000–0x23FF | 256 words, `HIST[i]` at 0x2000 + 4·*i*; count [16:0] | the count | the count |
| `PTR` | 0x2400–0x27FF | 256 words, `PTR[i]` at 0x2400 + 4·*i*; chunk address [19:0] | bus error | the pointer |
| `STREAM` | 0x2800–0x2FFF | 256 entries, `STREAM[i]` at 0x2800 + 8·*i* (low word, then high word; layout in 4.2) | bus error | one word |
| `GROUP` | 0x3000–0x31FF | 64 entries, `GROUP[g]` at 0x3000 + 8·*g* (low word, then high word; layout in 4.2) | bus error | one word |
| unmapped | 0x3200–0x3FFF | — | bus error | bus error |

**Access rules:** only `HIST` can be loaded; a load from `PTR`, `STREAM` or `GROUP` (which the controller only writes) is a bus error, as is any sub-word access in the window and any access to 0x3200–0x3FFF. When the controller may touch the tables: 4.10.

---

## 9. Invariants (to be checked by the simulator)

1. **No FIFO overflow:** router FIFOs including tile 0's C&C FIFOs (4.7, 4.16), the write engine's input FIFO (4.9), and the `PTR` and `HIST` request FIFOs (4.10).
2. **At most one access per big-SRAM area per cycle,** and no buffer is both read and written within an engine run (4.1). An access that spans two rows counts as one access here (4.1).
3. **The timetable holds:** every rotation starts on its scheduled cycle and never earlier; table read-ahead waits are part of the timetable, identical on every tile (4.2). A tile that falls behind because the SRAM read queue refused a request sets **drift** (4.13).
4. **Every expected arrival lands first** (an uncounted mailbox command sitting in port B is ignored, since commands may cross the mesh mid-phase): a tile's counters are all done (with `mem_idle` and `cm_idle`) before its controller pulses `done` to end a phase.
5. **No bad packets:** no packet with mode 0 or 8–15 is ever sent; the director drops one and sets the bad-mode condition.
6. **No `HIST` count exceeds 17 bits.**
7. **No packet is in flight when the RUN/HALT line changes** [built].
8. **Overwritten-unread results are counted, not failed:** mode-7 words and read-port results overwritten before the program reads them are a reported statistic, per phase. Exchanges legitimately land words a program ignores. A strict mode (meshsim's `Config.strictReplies`) makes them violations for tests that want the guarantee. Separately, no `SRAM_RD_ADDR_QUEUE` store may find the queue full.
9. **No unexpected arrival:** no packet arrives for a down-counter already at 0.
10. **Gathers are local:** a mode-1 packet is only ever sent to the tile itself.
11. **Mode 5 stays in controller memory:** no mode-5 packet addresses a word outside instruction and data memory, or inside the mailbox range.
12. **Mode 7 stays out of the mailbox:** no mode-7 packet addresses the mailbox range.
13. **Mode 7 stays in data memory:** no mode-7 packet addresses a word outside data memory.
14. **Read-port results and incoming traffic do not overlap** (4.3): no packet from outside the tile, other than C&C's mode-6 command words, arrives while the tile has read-port results outstanding (issued and not yet accepted by the traffic director). Checked as actual overlap in time [built: meshsim's `Config.checkReadPortOverlap`, on by default].
15. **No conditions:** every tile's `TILE_CONDITIONS` stays 0 (4.13). A program error that the hardware can see shows up here, and on C&C's error bit.

---

## 10. Parameter summary

*Figure 8.1.*

| parameter | default |
|---|---|
| `link_latency` | 3 on every link direction; a hop is `link_latency` + 1 = 4 cycles (3.2) |
| start skew between tiles | 0 or +1 cycle (3.2) |
| `STREAM`, `GROUP` | 256 and 64 entries of 64 bits (4.2) |
| GATHER request queue | 16 (4.4) |
| GATHER value size *k* | 1–8 records (4.4) |
| digit-all-to-all group gap | 0; 4 for the two-hop layers (4.2) |
| `mem_chunks` | 1,048,576 (8 areas × 128 bits × 32K); a smaller device is defined (4.1) |
| area request-queue depth | 8 in meshsim; *(Open)* for the hardware (4.1) |
| area tie order | writes first (4.1) |
| big-SRAM array read latency | 2 in meshsim; *(Open)* for the hardware (4.1) |
| big-SRAM read latency | queueing plus the array's read latency (4.1) |
| token FIFO | 64 entries (4.5) |
| `queue_depth` (router FIFOs) | 16 |
| `intra_tile_latency` | 1 per stage |
| `ts_engine_latency` | 1 (1–4 measured) |
| `ag_lookahead` | 2·`ts_engine_latency` + 2 entries in each of the address generator's two read-ahead queues: 4 at the default; exactly enough for groups of ≥ 2 cycles; back-to-back one-visit groups would need 4·`ts_engine_latency` + 3, should one-word exchanges ever become group runs [meas: meshsim] (4.2) |
| address-generator lead-in | first request 4·`ts_engine_latency` + 3 (group run) or 2·`ts_engine_latency` + 2 (stream run) cycles after the start store; 4·`ts_engine_latency` + 2 or 2·`ts_engine_latency` + 1 after the cycle the start takes effect (4.2) |
| pointer stage depth | 2·`ts_engine_latency` + 2 (4 at the default) (4.9) |
| write-engine input FIFO | 4 (4.9) |
| `PTR`, `HIST` request FIFOs | 16 each (4.10) |
| controller read port | request queue of at least 64 entries; 64 in v1.0 (4.3) |
| `cm_latency` (director to port B) | 2 [built] |
| `ready_latency` | 1 in simulation (the GO net's); the physical lines cross the die, more than 1 (8 used for estimates); the physical value is *(Open)* |
| `go_latency` | 1 in simulation; the GO net crosses the die like the READY lines, so physically more than 1 *(Open)*; C&C waits `go_latency` + 1 cycles after each GO before sending (6.6) |
| `run_latency` | as GO |
| C&C receive FIFO | at least 64 entries (6.6) |
| load-use penalty (data memory, registers, table window) | 0 each (the design target; section 12) |
| taken-branch penalty, multiply latency | 1, 3 |

---

## 11. Logic budget (per tile, 12 nm estimates)

The SRAM is ~75 % of the die; the remaining ~25 % is about 1.5 mm² per tile for everything else. Every unit is charged here, and additions must fit. Estimates are rough (±50 %) and exclude the big SRAM areas themselves. The lines are rough estimates [estimate], not yet reviewed.

| unit | state | estimate |
|---|---|---|
| link FIFOs (4 outputs × 2–4 FIFOs + eject × 5 = 17 FIFOs of 16 × 102 bits, ~27.7 Kbit of flops), route blocks, selectors, traffic director; padding stages on short links (102 flops each) | | 0.05 mm² |
| big-SRAM area queues: 8 request queues (depth open; ~100 bits per entry), per-chunk write enables, routing from the clients to the areas and back | depends on depth | ~0.01–0.02 mm² |
| address generator: read-ahead queues (`ag_lookahead` × ~100 bits: a `STREAM` and a `GROUP` entry), a rotation or element counter (18 bits) and its comparator, the element-address shift-add (20 bits), 64-entry token FIFO, relative-destination adder, run state | | ~0.011 mm² |
| controller read port: 64-entry request queue (32 bits each), `SRAM_RD_DEST_ADDR` counter, read-queue client | ~2 Kbit | ~0.004 mm² |
| GATHER engine: 16-entry request queue (× 64 bits), address multiply-add (*k* ≤ 8: a shift-add), per-value counter | ~1 Kbit | ~0.004 mm² |
| DMA write engine: input FIFO, pointer stage (4 × ~84 bits), augment and count logic | | ~0.005 mm² |
| table memories: `STREAM` (2 KB) and `GROUP` (512 B) macros; `PTR` (256 × 20) and `HIST` (256 × 17) macros with request FIFOs, RMW pipelines and bypass | ~25 Kbit | ~0.015 mm² |
| controller: RV32I ~20k gates + Zmmul ~5–10k gates + `WFI`; 8 KB + 8 KB as four 1K × 32 dual-port macros; peripheral registers; the three down-counters | | ~0.06 mm² |
| RUN/HALT, `done`, "command landed", READY, GO, the local-wake latch, flags | | 0.005 mm² |
| **total per tile** | | **≈ 0.17 mm²**, about 11 % of the per-tile logic budget |
| tile 0 only: fifth router port (4.16), ten 16 × 102-bit FIFOs + wider selectors | ~16 Kbit of flops | 0.02–0.03 mm² |
| once per die: C&C controller (6.6) | | ~0.04 mm² |

Rule of thumb: tables go in SRAM (cheap); comparators, muxes and per-entry flop state are what spend the budget. The tile is dominated by the big SRAM: at 0.75–0.9 MB/mm², 4 MB ≈ 4.5–5.3 mm². With the logic and link channels, a tile is roughly 6–7 mm² at 4 MB. The areas' density at one access per cycle awaits memory-compiler numbers (section 13).

---

## 12. Physical design: the control island

*Figures 1.3, 3.1.*

All the tile's small memories and its registers cluster around the controller. The engines and the router sit between this **control island** and the big SRAM and the links.

- **In the island:**
  - the controller;
  - its four 1K × 32 dual-port memory banks (8 KB instruction, 8 KB data, with the mailbox);
  - the four table memories (`HIST`, `PTR`, `STREAM`, `GROUP`);
  - the peripheral registers.

  Rough 12 nm sizes: ~0.015 mm² for the table memories, ~0.012 mm² per 4 KB dual-port bank (~0.05 mm² for four), ~0.01 mm² of controller logic. The island is **~0.09 mm², a patch ~0.3 mm across** [estimate]. Wires inside it cost a few tens of ps, so the controller's one-cycle loads of all of these are realistic at 1 GHz.
- **The table memories go by the controller, not the engines.** The engines' table traffic (pointer and counter read-modify-writes with bypass, the `GROUP`/`STREAM` read-ahead) is pipelined and tolerates latency; the controller's loads do not. The engines pay the distance (`ts_engine_latency`, 4.10).
- **Configuration registers** live in the island although the engines use them. They are static during a phase, so the long wires are multi-cycle paths (or the engines keep local copies).
- **Status bits** are registered at their source in each unit and cross to the island with one flop, so status reads one cycle old.
- **Not in the island:**
  - everything the engines touch every cycle other than the table memories: the address generator's read-ahead queues and token FIFO, the pointer stage, the area queues, the router queues;
  - the GATHER engine and the read port's request queue, which sit by the big SRAM.
- **Paths that cross the gap:** engine table accesses, and the traffic director's port-B writes (modes 5–7, `cm_latency`). Both are pipelined and tolerate latency, so the island costs no throughput.
- **Big SRAM:** eight 512 KB areas (128 bits × 32K each) around the logic, with the link channels between them. Their arrangement awaits the compiler numbers.

---

## 13. Open items

Every item still to be settled, and every proposal not yet ruled on, with the section it affects. Possible later additions are improvements considered and not part of v1.0.

**Open: to be settled for the hardware**

1. **Big-SRAM areas** (4.1): memory-compiler numbers for a 128 × 32K area macro at 1 GHz with one access per cycle, at the sign-off corner; the array's read latency; the areas' density (section 11) and arrangement (section 12).
2. **The area request-queue depth** for the hardware (8 in meshsim), and so the area queues' cost (section 11).
3. **Write combining** (4.1, 4.9): whether two 64-bit writes to the same row are combined into one access before the queue.
4. **Queueing against the timetable** (4.1, 4.2): the token FIFO's 64 entries must absorb the area queueing a phase's placement allows; the bound, and the placement rules that keep each phase's traffic per area within one access per cycle.
5. **The `PTR` and `HIST` macros** (4.10): the write must land intact when port A reads the address port B is writing.
6. **The GATHER request queue under load** (4.4): the in-flight bound under a heavier SRAM-read-queue load from the tile's other clients. Local sources alone peak at 0 of 16 at *k* = 8 [meas: meshsim]; overflow is a hardware-fault detector (4.13).
7. **Gapless one-word exchanges** (4.2): they run with gap 4 on every stream. Simulate gapless stream runs in schedule order, confirm the link-queue peaks at window boundaries, and tighten if the queues stay low.
8. **The physical `ready_latency`** (section 10).
9. **The memory subsystem's attachment** (section 7).
10. **Above the chip:** the cards, the card and controller FPGAs, their links, and each chip's DDR4 and how it attaches are not specified (figure 1.1 draws them as assumptions; only 6.6 mentions an FPGA).
11. **C&C's register block** (6.6): names and layout of the send, READY, GO-pulse, RUN/HALT, receive and host-interface registers, the watchdog register and the wake events.
12. **The boot document** (4.11, 6.2): the loader's interface and the image layout.
13. **The output format** of the sort (section 14): the final keys carry the receive-buffer ordinal in bits 63–32; bare keys or (key, value) records need another pass.

**Proposed, awaiting a ruling**

14. **C&C's wake sources:** all READY bits set, a packet on the receive path, and a watchdog timer (6.6).
15. **A status bit with READY,** so C&C can learn per-owner completion without its receive path.
16. **128-bit gather-value reads** (4.4): read a gather value as one row when it is row-aligned, which would halve the reads but not the injection cost.

**Possible later additions**

17. **A direct path from the GATHER engine to the write side.** The two gathers cost ≈ 393K of ≈ 1.37M, injection-bound at *k* + 1 cycles per value. A direct path would bring a gather to ≈ 2 cycles per value, or ≈ 1 with write combining: 130–260K per sort [estimate]. The biggest hardware lever found.
18. **Fusing the local gather with the values all-to-all** (the GATHER engine firing gather values straight at each owner). It would save about *n*·(*k* + 1) cycles, ≈ 196K per tile at *k* = 2 [estimate]. But each visit would have to cost a fixed *k* + 1 cycles to keep the timetable exact, and the 16-group schedule's periods and link loads would have to be redone for *k* packets per visit.
19. **An arrival-order form of augmenting for mode 3** (4.9), for streams from the tile to itself, where arrival order is the input order. It would let step 1's augmenting fold into the local sort's first scatter pass, saving one pass (≈ 64K cycles) [estimate], but only together with counting digit 1 during the input load, since step 1's augmenting pass is also what counts digit 1 for the first bucket pass.
20. **A search engine:** a big-SRAM read client running up to 64 binary searches over sorted records, round-robin, with a 64-entry context table, one comparator and an adder (~5 Kbit, ≈ 0.005 mm² [estimate]). It would make the split faster: ≈ 43–67K cycles on uniform [model], against the software split's ≈ 203K through the controller read port (4.3).
21. **A port-B reader** for bulk transfers from controller memory to big SRAM (or another tile). Today the only path is the send port, mode 2 to `PTR[me]`, at ≈ 2–3 cycles per 64 bits. A reader on port B would stream up to 64 bits per cycle into the injection point. The traffic director keeps priority on port B, because its writes (commands, replies, copies) can't wait; the reader takes port B only on cycles with no director write. It is a priority mux, not a design yet; add it only if a bulk case becomes critical.
22. **Whole-row fetches** for 64-bit streams (4.2): fetching whole rows into holding registers would halve the source area's accesses.

The logic budget (section 11) is unreviewed.

---

## 14. Context: the sort these units serve

Not part of the hardware specification; it is here because it explains the GATHER engine and augmenting. The program is in `sort/program/` (the split in `sort/split/`, skew handling in `sort/skew/`), and its measured times are in `results/`. For a 32-bit key and a value of *k* records (*k* = 2 for 128-bit values), the sort has six steps:

1. **Local sort** of (key, index) words, with the index attached by augmenting (mode 4).
2. **Local gather** of the values into key order: a mode-1 stream to the tile itself (4.4).
3. **Split:** a bisection on key values. Owners exchange probe keys (mode-7 words) and counts (mode-5 (lb, ub) pairs) from the send port, and every tile counts by binary searches of its own run through the controller read port (4.3). Exact on all 25 data sets: ≈ 203K cycles on uniform, 246K mean over the 24 non-trivial sets, 367K worst, with a seed bracket and an early exit to idle barriers [meas: RV32 ISS]. Interface and memory: `sort/split/BINDING.md`.
4. **Two all-to-alls,** in modes 4 and 2:
   - **keys,** in mode 4 (augmented), with count-on-arrival on at the receiver;
   - **values,** in mode 2, with count-on-arrival off.

   Each sender's runs land contiguously at `PTR[src]`, set by the receiver from its counts; senders need no destination addresses. Key *i* and value *i* correspond because both arrive in the same sender order.
5. **Local sort at the owner,** starting with its first digit already counted.
6. **Local gather** of the values into final key order: the same operation as step 2.

The values cross the network once, and each value is read twice locally, by the two gathers. For the 32-bit-value format, key and value fit in one 64-bit record, the value travelling in the record's high word: so steps 2 and 6 disappear, nothing is augmented (the first local sort's input pass and the keys all-to-all use mode 2, not mode 4), and there is no values all-to-all.

**Skew handling** (`sort/skew/`). A three-stage switch picks the transfer path for step 4: direct, loopback-as-local-copy (LB), the one-layer two-hop (TH), or the balanced two-hop (BT), with every tile making the same choice from the group maxima. K + V including planning: 215K uniform, 199K sorted and all-equal, 420K reverse, 772–786K Zipf and trace, 741K at 99 % one value [model; planner cycles measured on an RV32 ISS]. The planner is an overlay (≈ 8.5 KB). C&C issues a fixed maximum number of layer phases, and tiles finish empty layers at once (≈ 30 cycles each). The switch is format-aware: for 32-bit values it costs the keys all-to-all only (trace sets take BT at 332–343K against 0.59–1.00M direct; sorted and all-equal LB at 66.7K; reverse TH at 149.0K) [model]. TH and BT run every stream at the full window and so use a group gap of 4 (4.2).

**Global totals** (the group maxima for the `GROUP` R fields, or any global sum) use a mesh reduction, not a ring scan.
- Each row collects toward its column-0 tile (4 hops from one side, 3 from the other), then column 0 collects toward tile 0 the same way, and tile 0 broadcasts.
- That's about 8 controller turnarounds instead of 63.
- **One-word totals** move as mode-7 replies: a collector sets `DMA_REPLY_REMAINING` = 2 and `WFI`s; a tile mid-chain sets it to 1.
- **Vectors** (the 16 group maxima) move as eight 64-bit **mode-5** packets into the receiver's data memory, counted by `DMA_CM_REMAINING`.
- The ring scan is kept only for exclusive prefixes.

The plan estimated ≈ 1.37M cycles for the whole sort at *k* = 2 on uniform data [model]; the measured times are in `results/`.
