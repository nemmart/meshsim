# Machine specification — 64-tile mesh processor, v1.2

This document specifies the hardware of the 64-tile mesh sorter: the tiles, the network that joins them, the control that sequences them, and the registers a tile program uses. It is written for an engineer who will build the machine.

**Companion material** (paths in the package):
- `spec/CHANGES-v1.2.md`: what changed from v1.1, section by section, each item citing its decision. A section removed in v1.2 keeps its number here as a one-line stub pointing there.
- `planning/DECISIONS-v1.2.md`: the decisions log behind v1.2, kept as the history. Where the log and this document differ, this document is the authority.
- `diagrams/`: the figure set, numbered part.figure (1.0–8.1); `diagrams/README.md` is the index. Each section below names the figures that draw it. **The figures are still at v1.1** (the 169-bit link word, modes 0–15, the peripheral slot, RUN/HALT) and are to be redrawn.
- `spec/schedule.md`: the 16-group all-to-all schedule (4.2) and the routing ties it fixes (4.6).
- `sw/mesh.h` and `sw/REGISTERS.md`: the tile-program interface and the programmer's register reference, generated from the register tables of section 8. Both must match section 8; **they are still at v1.1** until regenerated.
- `sw/CONVENTIONS.md`, `sw/RECIPES.md`: the software conventions and the recipe book (being rewritten for v1.2).
- `meshsim/`: the cycle-level simulator, which models every unit described here and is where the measured numbers come from. **meshsim is still at v1.1**; its port to this document follows.
- `results/`: the measured sort.

**Markers.** An unmarked statement is decided. *(Proposed)* means suggested and not yet ruled on; *(Open)* means still to be settled; *(Deferred)* means set aside until the design it belongs to (the file system, C&C 1's firmware), listed in section 13; *(likely to change)* marks a decided value expected to move. Section 13 gathers every proposed, open and deferred item. Numbers are marked [meas: meshsim] (measured on the simulator, with the test where it matters), [meas: RV32 ISS] (measured on a 64-tile RV32 instruction-set simulator), [built] (as implemented in the simulator), [model] (from a model), [derived] or [estimate] (a rough estimate). **Every measurement in this document was taken on meshsim v1.1 or earlier**; one that v1.2's changes may invalidate carries **v1.1 measurement; to be re-measured in v1.2** (or v1.0) in its label, and is kept as the best number known until the simulator is updated. Decisions cite the owner (O), Tony (T) for the controller core and the link, or the integrator, with a date where it helps.

**Who checks what** (owner, 2026-10-28). The hardware checks only what is cheap and catches a bug far from its cause: the overflows, unexpected arrival, done early, drift, alignment and the region decode. Every other rule a program must keep is checked by **meshsim**, which stops with an error naming the access, and is ignored by the hardware. Rules in this document say which side checks them: **(hardware)**, or **(meshsim)** for a rule the hardware does not check. Section 9 lists the invariants, all of them meshsim checks.

**v1.2 in brief** (in full, with the decisions behind each item: `CHANGES-v1.2.md`):
- **The link word** (3.1) is 256 bits: a 128-bit payload, a 32-bit byte address, size, a 3-bit counter field, a 3-bit mode, src and dst; 74 bits reserved.
- **One 32-bit address map** (2.2) for packets and for the controller's own loads and stores: data memory, program memory, the boot ROM, big SRAM, up to three peripherals, and the registers, each a region named by the top byte.
- **Five modes** (3.1): write at address (commands and replies included), big-SRAM write by source, bucket write, augmented write, gather request; 5–7 are reserved and discarded.
- **Four write counters** (4.12): a packet names the counter it decrements, K cycles after the traffic director accepts it; counting no longer depends on the mode.
- **Natural alignment everywhere**, checked in hardware; spanning big-SRAM accesses are gone (3.1, 4.1).
- **Registers** (section 8): the controller's block and each peripheral's window start with the same words (`PARAMS`, `STATUS`, `ERRORS`, pulses, events, DMA). Pulses are one register per action. v1.1's conditions are now **errors**, and the wake registers are `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK` and `EVENTS_REQUIRED`.
- **Peripherals** (4.17): up to three per tile, each with a region, a standard register window, counted or live events, and optionally a DMA. The sender, mode 9 and the service lines are gone. The replayer (4.19) is peripheral 1 where present, its low-water lines now credits.
- **Errors are terminal** (4.13): an error raises the controller's hard interrupt; the handler reports to C&C 1 and parks. **There is no HALT state** (6.1): the chip raises its "toast" pin and the outside world resets it, hard or soft; every unit then starts in its **boot ROM** (6.2).
- **The three controllers** (2, 6.6) are tile instances at endpoints 64–66, with their engines as their peripherals; every chip-level function of C&C 1 is in its peripheral.

---

## 1. Conventions

- **Cycle.** One global clock. Every unit updates once per cycle. All tiles run at the same frequency; there is no requirement that they agree on phase, which is modelled by per-link latency (section 3).
- **Packet.** The unit of link transfer: a *payload* of 32, 64 or 128 bits, named by the packet's 2-bit *size*, plus header fields, in one 256-bit link word (3.1). One element of any width is one packet.
- **Chunk.** 32 bits. Inside a tile's engines the big SRAM is addressed in chunks.
- **Record.** 64 bits = 2 chunks. Record index *i* corresponds to chunk address 2*i* within a region.
- **Row.** 128 bits = 4 chunks, one access of a big-SRAM area (4.1).
- **Tile.** One processor, idx = 0..63; tile idx is at column `col = idx mod 8`, row `row = idx div 8`. The three controllers C&C 1–3 are tile instances too (2), at endpoints 64–66; "tile" without qualification means one of the 64 mesh tiles, and "unit" any of the 67.
- **Relative tile offset.** *d* names tile (`me` + *d*) mod 64; *d* = 0 is the tile itself.
- **Controller.** A unit's RV32 processor (4.11). The controllers of C&C 1–3 are the **C&C controllers** (section 6).
- **Addresses** (2.2). **Anything that travels in a packet or that the controller reaches through its map is a 32-bit byte address:** an 8-bit region [31:24] and a 24-bit byte offset [23:0]. **Anything inside the tile's engines that points into big SRAM is a 21-bit chunk address** (`STREAM_SRC`, `PTR`, `SRAM_RD_ADDR_QUEUE`, `DMA_RECORD_BASE_ADDRESS`): at most 2²¹ chunks = 8 MB per tile. The packet address of chunk *c* is `0x03000000 | c << 2`. The big SRAM's size is a multiple of 256 KB; the default is 4 MB.
- **Alignment.** A 64-bit access is **naturally aligned** when its byte address is a multiple of 8 (its chunk address a multiple of 2), a 128-bit access when it is a multiple of 16 (chunk address a multiple of 4). Every access in every region is naturally aligned (3.1, 4.1).
- **Bits** are written [*hi*:*lo*].
- `x mod n` is the non-negative remainder.
- **Names.** `HIST` is the histogram table, `PTR` the landing pointers, `STREAM` the stream table (three arrays, `STREAM_SRC`, `STREAM_DEST` and `STREAM_COUNT`) and `GROUP` the group table (4.10). The controller's first registers are `PARAMS`, `STATUS` and `ERRORS`, the same names as the first words of every peripheral's window; the other registers are named by family (section 8): `PULSE_`, `EVENTS_`, `ROUTER_`, `REQUEST_`, `DMA_`, `WRITE_COUNTER_` and `SRAM_`. "DMA" names both the tile's address-generator run (`PULSE_DMA` in the controller's block) and a peripheral's output engine (`PULSE_DMA` in its window).
- **Errors.** v1.1's *conditions* are **errors** (4.13), a bit is named without repeating the word (`ERRORS_BUS`), and an error raises the controller's hard interrupt. **Events** only release `WFI` (4.12).

---

## 2. System

*Figures 1.1, 1.2 (at v1.1).*

64 tiles (section 4), **identical in design; differing only in big-SRAM size and in what is attached and strapped** (owner, 2026-10-26): every router has a fifth port, connected only where something is attached (4.16), every tile has a request port (4.18), and the endpoint id and the peripherals present are strapped (`PARAMS`, 8.2). Tile idx has four bidirectional links to tiles (idx+1) mod 64, (idx−1) mod 64, (idx+8) mod 64 and (idx−8) mod 64, called **right, left, down, up** respectively.

The ±1 links form one ring through all 64 tiles in index order (tile 7 is left of tile 8, tile 63 is left of tile 0). The ±8 links are columns with wraparound (tile 56+m is above tile m).

**The three controllers** (owner, 2026-10-28). Command and control is three units, **C&C 1, C&C 2 and C&C 3, at endpoints 64, 65 and 66**, attached to the fifth ports of tiles 0, 1 and 2 (4.16). **Each is a normal tile instance**, with 2 MB of big SRAM, so the address map (2.2), the registers (section 8) and the controller (4.11) mean at C&C 1–3 exactly what they mean on a tile. Each has **one peripheral, its engine** (4.17, 6.6):

| unit | endpoint | role | its peripheral (peripheral 1) |
|---|---|---|---|
| C&C 1 | 64 | the proof flow (READY/GO), boot, the outside world | **Ingress/Egress and the UART**: the host link, and every chip-level register (6.6, 8.14) |
| C&C 2 | 65 | the file system | **the Updater/Binder/RW engine**, the only bulk LPDDR client: the memory system (section 7) |
| C&C 3 | 66 | the page manager | **the Tracer** |

A packet reaches an engine at its controller's endpoint, in that peripheral's region (2.2); each controller's link to its engine is internal to the unit. *(Proposed: the assignment of 65 and 66, to be confirmed with the endpoint map, item 82.)*

**The request bus** (3.3): a 16-bit, 8-ary tree from every tile to the units that serve requests (its targets), separate from the mesh. It is the tiles' only path for requests to the controllers and the memory system; answers come back over the mesh (6.7).

**Global nets** (section 6), besides the links:
- **GO:** one line from C&C 1's peripheral (`PULSE_GO`, 8.14) to every mesh tile, balanced so every tile sees a pulse on the same cycle. C&C 2 and C&C 3 aren't wired to it.
- **READY:** one line per mesh tile into C&C 1's peripheral (`READY_LINES_0`–`1`, 8.14), 64 in all, at equal latency `ready_latency`.
- **Error lines:** one line per unit into C&C 1's peripheral (`ERROR_LINES_0`–`2`, by endpoint id; 8.14), each high while that unit's `ERRORS` is non-zero, driven by hardware at a fixed latency `error_latency`; bit 64 (C&C 1 itself) is unused (4.13, 6.1).
- **The reset inputs:** two chip inputs, **hard reset** and **soft reset**; the chip never resets itself (6.1).
- **Two output pins:** the **"toast" pin**, raised by C&C 1's peripheral when anything has failed (6.1), and the software **`LED`** pin (8.14).

There is no RUN/HALT net (6.1).

What sits above the chip (the cards, their FPGAs and each chip's DDR4, figure 1.1) is not specified here.

### 2.1 Address space (7 bits)

| value | meaning |
|---|---|
| 0–63 | tile idx |
| 64 | C&C 1 |
| 65 | C&C 2 (the file system; its peripheral is the memory system) |
| 66 | C&C 3 (the page manager; its peripheral is the Tracer) |
| 67–125 | reserved |
| 126 | broadcast to tiles 0–63 (valid only as a destination) |
| 127 | none (as source: idle packet) |

Addresses 64–66 are the **controllers**, and 64–125 the **non-tile endpoints**. A packet for 67–125 has no endpoint and is a programming error (meshsim). v1.1's engine endpoints 72–74 are gone: the engines are their controllers' peripherals (2). A stream may name a controller absolutely (4.2). Their packets are counted like any other (4.12).

### 2.2 The 32-bit address map

*(New in v1.2.)* One map serves **packet addresses** and **the controller's own loads, stores and fetches** (owner, 2026-10-28). An address is an **8-bit region** [31:24] and a **24-bit byte offset** [23:0]; a C pointer is the packet address, with no shifts.

| region | contents | the controller | a packet |
|---|---|---|---|
| 0x00 | **data memory**, 32 KB (4.11) | load, store | mode 0 |
| 0x01 | **program memory**, 32 KB (4.11) | fetch, load; **a store is a bus error** | mode 0 |
| 0x02 | **boot ROM**; the reset vector is 0x02000000 (6.2) | fetch, load | bad packet |
| 0x03 | **big SRAM** (4.1) | none: reached through the read port and packets (4.3) | mode 0; modes 1–3 land through `PTR` |
| 0x04 | **peripheral 1** (4.17) | none: reached through its window | mode 0, if present |
| 0x05 | **peripheral 2** | none | mode 0, if present |
| 0x06 | **peripheral 3** | none | mode 0, if present |
| 0x07–0xFE | reserved | bus error | bad packet |
| 0xFF | **controller only:** the controller's register block, the peripherals' register windows and the table window (8.1) | load, store | bad packet |

- **Packets** reach regions 0x00, 0x01, 0x03 and a present peripheral's region; a packet addressed anywhere else is a **bad packet** (3.1; hardware, an 8-bit compare). Program memory is written only by packets, through the traffic director's port B (4.11).
- **The controller** sees regions 0x00, 0x01, 0x02 and 0xFF. It cannot load or store big SRAM or a peripheral directly: it reads big SRAM through the read port (4.3), writes it by sending itself packets, and copies through the address generator (4.11). An access to an unmapped address is a bus error (4.11).
- **Offsets are in bytes.** 24 bits cover 16 MB, above big SRAM's 8 MB maximum. The engines' big-SRAM pointers stay in 21-bit chunks (1, Addresses).
- **Out of bounds is not checked** (Tony, 2026-10-28): an offset past the end of a memory, in a packet or in the core's own access, is **undefined behaviour**. In the 32 KB controller memories the high offset bits are ignored, so the access aliases into the memory; big SRAM, whose size need not be a power of two, promises nothing (4.1). Staying in bounds is a program obligation (meshsim errors on every out-of-bounds access, packet or core).
- **Auto-advancing past the end** of a memory (`STREAM_DEST`, `ROUTER_ADDR`, a peripheral's `DMA_ADDR`, `SRAM_RD_DEST_ADDR`, the memory system's deliveries) is a software bug: they advance by plain addition, and what happens past the end (wrap, alias, or carry into the next region) is undefined (meshsim errors).
- **Data memory word 0 is reserved** (owner, 2026-10-28): the linker never places anything at address 0x00000000, so a C null-pointer store and an all-zero header (an uncounted mode-0 write at address 0, 3.1) both land there harmlessly (meshsim flags any write to it).

---

## 3. Links

### 3.1 Link word (packet)

*Figure 1.4 (at v1.1's 169-bit word).*

Every link carries one **256-bit link word** per cycle in each direction (O, T, 2026-10-28). 182 bits are fields; **74 are reserved**:

| field | bits | meaning |
|---|---|---|
| payload | [127:0] | data: a 32-bit payload in [31:0], a 64-bit payload in [63:0]; the bits above the size are 0 |
| address | [159:128] | the receiver's 32-bit address (2.2): region [31:24], byte offset [23:0]. Used by mode 0; modes 1–4 ignore it, region included (senders put 0) |
| size | [161:160] | the payload's width: 00 = 32 bits, 01 = 64, 10 = 128, so 1 << size chunks; **11 is undefined** (below) |
| counter | [164:162] | the write counter the packet decrements at the receiver (4.12): 0 uncounted, 1–4 `WRITE_COUNTER_1`–`4`, 5–7 reserved (below) |
| mode | [167:165] | what the receiver does (below) |
| src | [174:168] | the originator's endpoint id (2.1), stamped by the injecting hardware |
| dst | [181:175] | the destination's endpoint id |
| reserved | [255:182] | **sent as 0 and ignored by every receiver** |

- **The width is fixed** so later field changes don't resize the links, FIFOs and route blocks again (102 bits in v1.0, 169 in v1.1). **The full 256 bits are stored** in every FIFO and pipeline stage, reserved bits included (owner, 2026-10-28, at Tony's request); trimming the storage to the bits in use, mainly for power, is to be revisited when the design has settled (item 117).
- **Size, counter and mode** keep this order wherever they appear together: bits [167:160] of the link word are bits [24:17] of every header register (`STREAM_COUNT`, `ROUTER_HDR`, a peripheral's `DMA_HDR`; the common header layout, 8.1), so hardware copies them as one 8-bit block.
- A packet with src = 127 is an **idle packet** and carries nothing. A sender supplies dst, mode, counter, address, size and payload; the router adds src.

**Size.** Every packet names its payload's width. The address generator stamps it from the stream's read size (4.2), the send port from the send register used (4.11), the read port from `SRAM_RD_DEST_ADDR` (4.3), and a peripheral's DMA from its own choice (4.17.9). One element of any width is one packet. **Size 11 is undefined behaviour:** no hardware checks it (meshsim errors) *(Proposed: as v1.1)*. Only a `STREAM` entry with read size 11 (4.2) can produce one; the send port has no size-11 register.

**Packet modes** (owner, 2026-10-28). The region names where a write lands (2.2) and the counter field does the counting (below), so the 3-bit mode keeps only what the receiver must do differently:

| mode | packet | uses the address | lands | counted |
|---|---|---|---|---|
| 0 | **write at address:** the payload, 32, 64 or 128 bits by its size, at the address, in any region a packet may reach (2.2): data or program memory through port B (4.11), big SRAM (4.9), a peripheral's region (4.17.5). **Commands and replies are mode-0 writes** to the command and reply blocks in data memory (4.12) | yes | the region named | by its counter field |
| 1 | **big-SRAM write by source:** the payload, 32, 64 or 128 bits by its size, at `PTR[src]`, then `PTR[src]` += 1, 2 or 4 (its size in chunks) | no | `PTR[src]` | by its counter field |
| 2 | **bucket write:** as mode 1, at `PTR[t]`, *t* = the payload's bucket digit (4.15) | no | `PTR[t]` | by its counter field |
| 3 | **augmented write:** a 32- or 64-bit packet's low 32 bits (the key) and the ordinal, (target chunk address − `DMA_RECORD_BASE_ADDRESS`) / 2, as one 64-bit record at `PTR[src]`, then `PTR[src]` += 2 (4.9). A 128-bit mode-3 packet is a bad packet | no | `PTR[src]` | by its counter field |
| 4 | **gather request** (local only): payload = (key, ordinal), the ordinal in bits [51:32] (sent at 64 bits); the GATHER engine reads *k* records at `DMA_RECORD_BASE_ADDRESS` + 2*k*·ordinal and injects them as 64-bit mode-1 packets to the tile itself (4.4) | no | GATHER engine (4.4) | **never;** its counter field is passed on to the gather values it produces, which are counted as they pass the director |
| 5–7 | reserved | — | **discarded at the destination, with no error and no decrement**, whatever the counter field (meshsim flags every one) | never |

The general write is 0; the writes that land through `PTR` and step it are 1–3; the gather request, the only mode that isn't a write, follows them. **There is no invalid code**, so an all-zero header (a `STREAM` entry, `ROUTER_HDR` or `DMA_HDR` left at 0) is a valid, uncounted mode-0 write to address 0x00000000, the reserved first word of data memory (2.2) (meshsim flags it). A counted packet in a reserved mode leaves its counter short and the program waits forever with no error (accepted; meshsim flags it).

**Counting** (owner and Tony, 2026-10-28; 4.12). **The packet names its counter.** For modes 0–3, a counter field of 1–4 decrements that `WRITE_COUNTER` at the receiver, **K cycles after the traffic director accepts the packet** (4.8, 4.12); 0 is uncounted. Counting no longer depends on the mode or the source. A gather request is never counted (above), and a bad packet is dropped uncounted (below).
- **Counter codes 5–7 are reserved:** the packet is accepted and treated as uncounted (hardware) (meshsim errors) (integrator, 2026-10-29).
- **A counted packet for a counter at 0** raises **unexpected arrival** (4.13) when its decrement happens, K cycles after acceptance (4.12).
- **Uncounted traffic is mode 0 only** (owner, 2026-10-28): commands (4.12), and boot and staging loads (6.2), all at explicit addresses. **Modes 1–3 are always counted**, and **a running program sends no uncounted writes except commands**: anything whose landing matters is counted (meshsim checks both, 9).

Where the address is unused, senders put 0.

**Bad packets** (owner and Tony, 2026-10-28). The traffic director drops a **bad packet** without writing or counting it and sets `ERRORS` [1], "bad packet or misaligned access" (4.13). The hardware checks only the cheap cases, the alignment and the region decode:

| case | hardware |
|---|---|
| a 64- or 128-bit packet not naturally aligned (2.2), mode 0 in any region | bad packet |
| mode 0 to region 0x02 (the boot ROM), 0x07–0xFE, 0xFF, or an absent peripheral's region | bad packet *(Proposed)* |
| mode 3 at 128 bits | bad packet *(Proposed: as v1.1)* |
| modes 1–4 | ignore the address, region included *(Proposed: as v1.1)* |
| an offset past the end of its memory | not checked: undefined (2.2) (meshsim errors) |
| size 11 | not checked: undefined (meshsim errors) *(Proposed)* |
| modes 5–7 | not bad packets: discarded silently (above) |

A write through a misaligned `PTR` (modes 1–3) passes the director as a good packet; the write engine drops it, still counted, and sets [1] (4.9).

**Two principles shape the table:**
- **At most one streaming reader of big SRAM per tile at a time,** plus the dependent single reads of the GATHER engine and the controller read port. There are no remote pulls: gathers are local only (mode 4), and the controller reads only its own big SRAM (4.3).
- **Bulk movement is a push.** Anything larger than one word is requested with a small descriptor and delivered by the owner's address generator. A tile's reads of LPDDR follow the same shape: a request on the request bus (3.3), answered by the memory system's push (6.7).

### 3.2 Latency, throughput and ordering

*Figures 4.3, 8.1.*

Each link direction is a fixed-delay pipe: a packet placed on it at cycle t is available to the far end's route block at cycle t + k. One packet per cycle per direction, no backpressure, no acknowledgement. Packets are never dropped or reordered by a link.

- **Every link takes exactly 3 cycles** (`link_latency` = 3): three register stages from the output selector to the far route block's input. Shorter links are padded with pipeline stages: 256 flops per stage per link direction (3.1). The longest link must fit in 3 cycles, which is a physical-design check. Uniform latency keeps every tile's packets in lockstep, so the schedule's per-window load bounds hold exactly (4.2).
- **A hop costs `link_latency` + 1 = 4 cycles:** the route block and the FIFO write take the cycle after the packet arrives (4.6, 4.7).
- **Start skew:** tiles may start a run up to 1 cycle apart, despite the balanced GO net. On a link this adds at most one packet from an adjacent window, so queues grow by about 1 at a window boundary and drain within the next. The simulator models each tile with a random 0 or +1 cycle offset.

**In-order delivery per sender.** Packets from one source to one destination arrive in the order sent: routes are fixed per (source, destination) and FIFOs never reorder (4.6, 4.7). The boot sequence (6.2), the command convention (4.12) and the landing of multi-packet values through `PTR` (4.9) rely on it.
- **Within one kind only.** A unicast and a broadcast from the same source to the same tile can take different paths (offsets 33–39, 44, 52 and 60), so they may arrive out of order relative to each other [meas: meshsim]. **Rule:** a sender's packets that must arrive in order go all unicast or all broadcast; a command's packets to a tile are never mixed.

**No flow control.** Many-to-one traffic must be paced by the program. Measured fan-in limits with the 0/+1 start skew [meas: meshsim, NetTest skewtable; the minimum over nearest, farthest and three random sender sets and 40 skew patterns; v1.0 measurement; to be re-measured in v1.2], every sender bursting W packets to one tile in the same cycle. The limits are in packets: in v1.1 a 128-bit value is one packet, not two, so a burst of 128-bit values is half v1.0's packets.

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

63 × 1 overflows in every case measured, with or without skew (120 of 120 skew patterns, receivers 27, 0 and 63) [meas: NetTest fan-in; v1.0 measurement; to be re-measured in v1.2]: a FIFO is full by its count before the cycle's pop (4.13), so a push into a full FIFO is lost even in a cycle that pops it. The first FIFO to overflow is one of the receiver's eject FIFOs. Schedule-ordered exchanges (stream runs in the 16-group order) are permutations, so they don't fan in.

### 3.3 Request bus

The request bus carries requests from the tiles, and from the controllers, to the endpoints that serve them: reads and writes of LPDDR, debug messages and requests to the controllers. It is separate from the mesh, it has back-pressure, and it never drops anything. Requests travel one way only; every answer comes back over the mesh (6.7). A tile's side, the request port, is 4.18.

**Topology.** An 8-ary tree with the tiles as leaves:
- **eight row nodes,** one per row of 8 tiles (tiles 8*r* … 8*r* + 7), each with one input per tile;
- **a root node,** with one input per row node and one per controller that sends requests: **requests from the controllers enter at the root**;
- **no queues at the root** (owner, 2026-10-26): it arbitrates and forwards each request, over **one 16-bit link per target** *(Proposed)*, to its target's receiver, which holds the **receive queue** in its tile (4.18). The root keeps **one credit counter per target** for back-pressure (below).

Every tile is a leaf at the same depth, two nodes from the root. Which controllers and engines attach, and where, is specified with the endpoint map *(Open, item 82)*.

**Links.** Every hop is **16 bits wide** and moves two bytes per cycle, with **ready/valid back-pressure**: a beat moves only when the next stage can take it, so the bus never drops one. Long wires are pipelined. *(param)* `reqbus_latency`, the unloaded time from a send store to the request being in its target's queue, is about 15–20 cycles [estimate]; meshsim measures **17 cycles** unloaded [meas: meshsim, part M].

**The request.** Every request is **128 bits**. The hardware owns its low ten bits; everything above them belongs to the target:

| bits | field | set by | meaning |
|---|---|---|---|
| [6:0] | requestor | the tree | the sender's 7-bit endpoint id (2.1). Software writes 0; the row node overwrites it for a tile and the root for a controller, so a request cannot claim another sender's id, as a packet cannot claim another src |
| [9:7] | target | software | which receiver the root delivers the request to: 8 targets (below) |
| [127:10] | the request proper | software | **defined by the target,** and specified with each receiver: its operation, tag, addresses or (fd, offset), length, destination and so on |

The rules every target's answers follow are in 6.7.

**Targets** *(Proposed: the numbering, to be confirmed with the endpoint map, item 82)*:

| target | receiver |
|---|---|
| 0 | C&C 1 (endpoint 64) |
| 1 | C&C 2 (endpoint 65) |
| 2 | C&C 3 (endpoint 66) |
| 3 | the Updater/Binder/RW engine, C&C 2's peripheral *(Deferred: whether it stays a target of its own, and where its receive queue sits, now that the engine is inside C&C 2, come with the file system's window protocol, item 119)* |
| 4–7 | reserved |

A request for a reserved target, or a target with no receiver, is dropped at the root (meshsim flags it, 6.7).

**Framing.** There is no header byte and no size: a request is **8 beats** of 16 bits, bits [15:0] first. Beat 0 carries the requestor and the target, so a node knows where a request goes before the rest of it arrives *(Proposed)*.

**Arbitration** (owner, 2026-10-26, for the root's skip; the rest *(Proposed)*). Each node has a **16-byte buffer per input**, holding one whole request, and one output, and is **wormhole-routed per request**: once it has chosen an input it passes that request's 8 beats, and only then chooses again. A request's beats stay together all the way to its target's queue.
- **A row node** round-robins over its inputs holding a request's first beat, from the one after the input it last served, and writes the requestor, 8*r* + *c* for its row *r* and the input's column *c*.
- **The root** round-robins the same way over its inputs (the eight row nodes and the controllers), **skipping an input whose head request's target has no credit**, writes the requestor for a controller's request, and forwards each request whole (8 beats) to its target. So a busy receiver never holds up the root's requests for another target.
- **Credits** (owner, 2026-10-26): each target's counter starts at 16, the receive queue's depth; forwarding a request takes one, and the receiver returns one with each `REQUEST_RECEIVE` pop (4.18), so a receive queue never overflows.
- **Head-of-line blocking stays inside a row** (owner, 2026-10-26, accepted and noted). The skip isolates targets only at the root. A request waiting for a target with no credit holds its row node's link to the root, and with it every request from that row behind it, whatever their targets; and a tile's requests stay in order, so one for a full target holds up that tile's later requests. Full isolation would need per-target credits back to the leaves, which is not built.
- **Fair and starvation-free:** every tile is a leaf at the same depth and every node round-robins over its occupied inputs, so under full load each of the root's inputs gets an equal share of the requests it can take, and each tile one eighth of its row's.
- **Throughput:** the root passes one beat per cycle, so the whole chip issues at most one request every 8 cycles [derived]. That is ample: a 4 KB read every 8 cycles would be 512 B per cycle, far beyond LPDDR [derived].
- **Ordering:** a sender's requests reach their target's queue in the order the sender issued them (one path, FIFO buffers, whole requests), so requests from one sender to one target stay in order. There is no order between senders.

**The receive queues:** 16 requests of 128 bits (2 Kbit) in every tile's request port (4.18), fed only on the targets' receivers. Whether 16 credits cover the round trip to a receiver is open with the tree's physical design (item 61).

**Back-pressure end to end.** When a target has no credit (its receive queue is full, or will be with the requests already forwarded) the root skips the inputs whose next request is for it; a row node whose request the root skips stalls behind it, a tile's port stops draining its request FIFO, and a send store to a full FIFO stalls that tile's controller (4.18). Nothing is dropped anywhere on the bus, and heavy use by one tile stalls only that tile and the requests of its row behind it.

**What a target does with a request,** debug messages included, is the target's to define (6.7).

---

## 4. Tile

*Figure 1.3.*

**Units:**

| section | unit |
|---|---|
| 4.1 | big SRAM: areas (banks of at most 512 KB), no coherence |
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
| 4.12 | the command and reply blocks, the write counters and the events |
| 4.13 | status and errors; traps |
| 4.14 | commit ordering |
| 4.15 | digits |
| 4.16 | the fifth router port (every tile; connected at tiles 0–2, to C&C 1–3) |
| 4.17 | peripherals: up to three per tile, the standard window, the DMA (4.17.1–4.17.9) |
| 4.18 | the request port (the tile's end of the request bus, 3.3), send and receive |
| 4.19 | the replayer, peripheral type 1 (4.19.1–4.19.15) |
| 4.20 | the UART (part of C&C 1's peripheral) |

Where each sits on the die: section 12. Unless stated otherwise, every unit-to-unit transfer inside a tile takes one cycle (*(param)* `intra_tile_latency`, default 1 per stage; exact values affect latency only).

### 4.1 Big SRAM

*Figures 7.1, 7.2.*

**The array.**
- **Areas are banks.** The big SRAM is built from **areas**, each a bank with its own port that does one read or one write of one 128-bit row per cycle.
- **The guarantee software may rely on:** every area is contiguous (not interleaved), power-of-two sized, aligned to its size, and **at most 512 KB**. So every 512 KB-aligned block of addresses is served by its own area or areas and never shares one with another block, and a 4 MB device has at least 8 areas. Software that keeps concurrent streams in different 512 KB blocks gets a separate port for each.
- **Beyond that the geometry is *(Open)* for the hardware:** the areas' size, depth and count. **meshsim's default** is 8 areas, each **128 bits × 32,768 rows** (512 KB); area *a* holds 128-bit rows *a*·32K to (*a* + 1)·32K − 1, so in a chunk address bits 20–17 are the area, bits 16–2 the row and bits 1–0 the chunk within the row. Placement rules and measurements in this spec that depend on the areas assume this default.
- **Capacity** *(param)* `mem_chunks`: a multiple of 256 KB (65,536 chunks), default 1,048,576 chunks (4 MB), at most 2,097,152 (8 MB). When the size isn't a whole number of areas, the last area is partial (for example a half-height 512 KB area, 16K rows).
- **Each area does one read or one write per cycle,** of one 128-bit row. The areas work independently, so up to one access per area happens per cycle in total when the clients' data sits in different areas. A sequential stream stays in one area at 4 chunks per access.
- **Service is in arrival order:** reads and writes to an area are served in the order they arrive, with no priority.
- **Read order is per client, not global** (owner, 2026-10-23): the big SRAM hands back **each client's reads in the order that client issued them**, whichever areas they hit: the address generator's in its order, the GATHER engine's in its order (4.5). **Reads of different clients may complete in any order** relative to each other, so a gather's scattered reads never wait behind a stream's sequential ones. **The read port's reads need no order:** each result carries its own destination address (4.3), so they may come back in any order. Areas still serve their own queues in arrival order and may finish out of order; putting each ordered client's returns back in order is the big SRAM's job, a requirement on its design rather than on its clients, so the clients' return queues (4.5) hold only per-read metadata. Because order holds only within a client, two clients never send order-placed packets to the same destination at once (4.5, invariant 18).
- **Unloaded read latency: 4 cycles** with an array latency of 2 [meas: meshsim]. The array's read latency for the hardware is *(Open)*.
- **Partial writes.** A 32-bit write fills a quarter of a row and a 64-bit write half; a 128-bit write fills the whole row. The areas have **write enables per 32-bit chunk**, so a partial write is one access, not a read-modify-write. Every area access is a whole 128-bit row; reads select their chunks in the return path (4.5).
- **Natural alignment** (owner and Tony, 2026-10-28). **Every 64- or 128-bit big-SRAM access, read or write, is naturally aligned:** a 64-bit access at an even chunk, a 128-bit access at chunk 0 of a row. So **no access ever straddles a row**: v1.1's spanning access, its two area accesses and its extra cycle are gone, and every access is one request, one token (for a read, 4.5) and one area access. The pointers and runs that generate addresses keep them aligned (integrator, 2026-10-29): a stream's source is aligned to its read size and every step keeps it aligned (4.2); a `PTR` is aligned to the size of the writes landing through it, an augmented `PTR` to a record (4.9); `DMA_RECORD_BASE_ADDRESS` to a record (4.4). A pointer stays aligned only if every write through it has the same size, or if sizes descend from an aligned start (a DMA's 128s, then a 64, then a 32, 4.17.9); a run that continues where another stopped needs the earlier run's length to be a multiple of its own size.
- **A misaligned access is checked in hardware** and sets `ERRORS` [1], "bad packet or misaligned access" (4.13) (integrator, 2026-10-29):
  - **a packet** (mode 0) not aligned to its size is a bad packet, dropped uncounted by the traffic director (3.1);
  - **a write through a misaligned `PTR`** (modes 1–3) is dropped by the write engine but **still counted**, since its decrement was scheduled when the director accepted it (4.9);
  - **a misaligned read** by the address generator, the GATHER engine or the read port **is not performed, but its packet still goes out as usual with payload 0**, so the receiver's director counts it (one decrement point, and lockstep timing kept); [1] is set at the tile that issued the read (4.2–4.4).

  So nothing hangs and the error names the tile. meshsim flags every case.

**The area interface.**
- **Each area has one request queue** holding its reads and writes in arrival order. It accepts up to **two pushes a cycle**, one read (from the SRAM read queue, 4.5) and one write (from the write engine, 4.9), and serves one whole row a cycle.
- **Room is judged before the pop.** An area serves the head of its queue in every cycle it holds a request, and a push is accepted only if the queue's count *before that cycle's pop* leaves room (the write first: `ready_w` = count < depth; `ready_r` = count + `push_w` < depth; 4.13's rule for every FIFO). So after any cycle an area's queue holds at most **depth − 1** requests: 7 at the depth of 8 [meas: MemTest, depths 4, 8 and 16, one read and one write to one area every cycle]. A request refused for room waits in its holding register (a read in a group run sets drift).
- **Same-cycle ties: the write goes in first.** Writes can't wait, since nothing behind the SRAM write queue has flow control, but reads can: the address generator simply waits. A tie therefore costs time, not data. An area asked for more than one access per cycle is a placement error; under writes-first it shows as a late or drifting address generator, not as lost writes.
- **Acceptance.** An area accepts when its count before the cycle's pop is below its depth (4.13's rule). With two pushes in one cycle the read is accepted only if there is room after the write: `ready_r` = count + `push_w` < depth.
- **The queue depth** is a parameter: 8 in meshsim, *(Open)* for the hardware.
- **The traffic director never stalls.** A sustained overload of one area fills the write engine's 4-entry input FIFO, which overflows (write-engine input overflow, `ERRORS` [4], 4.13); the eject FIFOs stay clear. Blocking the director would also block commands and replies, so it is not done.

**No coherence.** The memory system makes no ordering promise between a run's reads and its writes. **The rule is that within one engine run, no buffer is both read and written.** The rule is per engine run, not per GO-to-GO phase, since a one-command local phase runs several passes, each reading what the previous one wrote after its writes have landed. A phase reads from some buffers and writes to others, and as long as they never overlap, the result is correct.
- **What follows the rule:**
  - the local sort's passes, which ping-pong between two buffers;
  - the gathers (index and values in, output out);
  - the all-to-alls (send buffer out, receive buffer in);
  - the split's searches through the read port, which read sorted keys that nothing writes during the split.
- **Ordering across phases comes from the phase boundary:** by the time a write's counter decrements, the write has entered its area's request queue (K, 4.12), and areas serve in arrival order, so a read issued after the counter is done queues behind it. A phase that waits for its counters before it reports done has its writes ordered before every read of the next phase, which starts after GO.
- There is no flush, no merge logic and no line state.

**`mem_idle` is gone** (owner, 2026-10-28): no rule depends on it any more, since a counter's done now covers the write path by K (4.12); meshsim keeps it as a diagnostic.

**Out of range** (integrator, 2026-10-29). An access at or past the end of the memory, including the missing rows of a partial last area, is **undefined in hardware**: big SRAM's size need not be a power of two, so it doesn't promise aliasing. The engines' chunk addresses advance by plain addition, and a run that passes the end is a software bug (2.2) (meshsim errors on both). No error bit is set.

**Clients.**
- **Readers:** the address generator (4.2), the GATHER engine (4.4) and the controller read port (4.3).
- **Writer:** the write engine (4.9), the only writer. The GATHER engine writes through it, by sending its gather values as mode-1 packets, and every sender at an explicit address, the memory system included (6.7), by sending mode-0 packets to region 0x03.
- **There is one SRAM read queue and one SRAM write queue** (4.5). They meet only at the areas.
- All clients queue at the areas they address, so **several clients can be active at once** without static selectors.
- The controller reads big SRAM only through the **controller read port** (4.3) and writes it only by sending itself packets (mode 1 at `PTR[me]`, or mode 0 at an explicit address in region 0x03); its copies go through the address generator and the loopback (4.11).
- **Throughput comes from placement:** buffers that one phase uses together should sit in different areas. A read can wait in its area's queue behind writes and other clients' reads, so its latency depends on the phase's placement and traffic; the tokens and the return queues absorb it (4.2, 4.5). The bound, and the placement rules that keep each phase's traffic per area within one access per cycle, are *(Open)* (section 13).

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

**Start** (`PULSE_DMA`, 8.6) acts on it:
- **Start loads the run** described in `DMA_CONFIG`, clears `rd_done`, sets `rd_running`, clears **DMA run done** (`EVENTS` [7], 4.12), so [7] always refers to the run most recently started, and begins the table read-ahead (below). GO does not start a run; start is the only way.
- **The first request is offered as soon as its entries are in.** A start store in cycle *t* takes effect in cycle *t* + 1 (4.11).
  - **Group run:** the first request is offered **4·`ts_engine_latency` + 3 cycles after the start store** (4·`ts_engine_latency` + 2 after the cycle the start takes effect; 7 and 6 at the default). The run reads the `GROUP` entry, then the `STREAM` entry (its three arrays read in parallel, 4.10), which needs the group's start stream; each read is a round trip of 2·`ts_engine_latency` + 1 [meas: meshsim, as the read-ahead's latency].
  - **Stream run:** the first request is offered **2·`ts_engine_latency` + 2 cycles after the start store** (2·`ts_engine_latency` + 1 after the cycle the start takes effect), the time of one `STREAM` read.
  - **There is no fixed lead-in counter:** the time depends only on the run kind, the tables and the latency, so it is the same on every tile and a synchronised start stays in lockstep.
- **`rd_running` = 1 and `rd_done` = 0 from the end of the start store's cycle,** so the next instruction's load of `STATUS` already sees them; `rd_running` stays 1 through the lead-in until the run has finished (below). (A helper that polls `STATUS.rd_running` to learn whether the tables are free relies on this, 4.10.)
- **A start while a run is running is ignored**, lead-in included, with no error: the run the program expected doesn't happen, so its arrivals never come and the tile hangs, which C&C 1's watchdog catches (4.13's principle) (meshsim flags it).
- **A local run** is one start store. **A synchronised start** is the same start store, executed by every tile on the same cycle after GO wakes it (6.3).
- **Finishing:** the run ends when the address generator has **walked its last entry**: every stream of a stream run, every group of a group run, including trailing count-0 streams and R = 0 groups, each at its timetable cost. The last entry's gap-after is not waited out. The sequencer cannot know a request is the last without examining the entries after it, so it simply walks them. Then, **once the run's last table read has completed** (guarantee B, 4.10), `rd_running` = 0 and `rd_done` = 1; `rd_done` stays 1 until the next start. The rise of `rd_done` also posts **DMA run done**, `EVENTS` [7] (4.12).
- **Drift.** In a group run, a request refused by the SRAM read queue holds its visit until taken, every later time shifts by the stall, and drift is set (4.13). A loopback read that waits for a token because earlier loopback packets still hold theirs in the loopback FIFO (4.5) is refused like any other. The timetable's times are the cycles a request is offered; read latency after that is separate (4.5).

**Stream runs** go through streams *start* … *start* + *count* − 1 **one after another**. Each stream's whole run is sent before the next stream begins.
- **Read ahead, no working state.** `STREAM` entries are read in stream order into the read-ahead queue (below), one `STREAM` read per cycle from its own memories (the three arrays, read in parallel, 4.10), so consecutive streams follow without a bubble. An **element counter** *j* runs 0 … count − 1 within the current stream, and the stream ends when *j* reaches its count.
- **A count-0 stream** issues nothing and takes only its gap-after cycles; its table read-ahead waits, which matter only for long runs of them, are part of the timetable (below). Lockstep rounds that must keep a fixed shape send dummy requests instead.
- **A stream run reads big SRAM only.** Values the controller computes in data memory (cuts, counts, maxima) go from the send port instead (4.11).
- **Uses:**
  - block copies into controller memory (mode 0 to the tile's own region 0x00 or 0x01);
  - local passes (modes 1, 2 and 3 to the tile itself);
  - gather streams (mode 4);
  - one-word exchanges;
  - writes at explicit addresses into another unit's memory or a peripheral's region (mode 0), the file system's tag windows among them (to C&C 2, *d* = 65, 6.7).
- **A one-word exchange of big-SRAM data** is 63 streams of count 1, with *d* in the 16-group offset order, and **gap after = 4 on every stream**, so a round takes ≈ 63 × 5 ≈ 315 cycles with loads far below any overflow.
- **The gapless form** *(Open)* would run the streams back to back in about 63 cycles. Every tile sends to the same offset on the same cycle, so each step is a permutation, and each 4-stream window has link loads ≤ 4 per direction. Running gapless relies on **uniform 3-cycle links** and **at most 1 cycle of start skew** between tiles (3.2), which keep the windows together. The queue peaks at window boundaries are still to be confirmed in simulation (section 13).

**Group runs** are for the digit all-to-alls. They run groups *start* … *start* + *count* − 1, each a round-robin over its streams for R rotations, then its gap. The address generator keeps **no per-stream working state**. One **rotation counter** *r* runs 0 … R − 1 for the group, and each visit reads its stream's `STREAM` entry (read ahead, below): the stream sends element *r* if *r* < count, and its time is idle otherwise. Every stream of a group is on the same rotation, so one counter serves the whole group. Every visit takes one cycle whatever its read size, so a group's period is its stream count *t*, and its streams may have different read sizes *(Proposed)*: v1.0 required one size per group because a 128-bit visit took two cycles.

**Stream table** `STREAM[0..255]`: entry *i* is word *i* of three 256 × 32-bit arrays, each its own memory, read in parallel at the same index as one access (4.10). The controller writes each word with one store (8.9). 96 bits, 88 used (owner, 2026-10-28):

| array | field | bits | meaning |
|---|---|---|---|
| `STREAM_SRC` | source | [20:0] | the run's first element, as a 21-bit **chunk address** in big SRAM, **aligned to the read size** (4.1) |
| `STREAM_SRC` | gap after | [23:21] | stream runs only: idle cycles (0–7) after this stream's run *(likely to change)* |
| `STREAM_SRC` | spare | [31:24] | |
| `STREAM_DEST` | destination address | [31:0] | **mode 0 only:** the first element's 32-bit byte address at the receiver (2.2), aligned to the read size, + 4, 8 or 16 bytes per element by read size; ignored for modes 1–4 |
| `STREAM_COUNT` | count | [16:0] | elements in the stream's run (0 = padding), at most 131,071 |
| `STREAM_COUNT` | read size | [18:17] | 00 32 bits, 01 64 bits, 10 128 bits (1 << size chunks per element), 11 reserved and undefined; stamped as every packet's size (3.1) |
| `STREAM_COUNT` | counter | [21:19] | the counter field stamped on every packet (3.1): the receiver's write counter, 0 uncounted |
| `STREAM_COUNT` | mode | [24:22] | the packet mode stamped on every packet of the stream: 0–4 (mode 4 only with *d* = 0) |
| `STREAM_COUNT` | destination | [31:25] | *d*, 7 bits: 0–63 a **relative** tile offset, the packets going to (`me` + *d*) mod 64, *d* = 0 the tile itself; 64–125 a non-tile endpoint, named **absolutely** (2.1): 64–66 the controllers, 67–125 reserved (no endpoint); 126–127 invalid for streams |

`STREAM_COUNT` [24:17] is the link word's size, counter and mode block, bits [167:160] (3.1), laid out as in every header register (8.1); [31:25] is the relative *d*, translated into dst rather than copied. The fields that change every round are still the source and the count, one store each, with the fixed bits (gap after; size, counter, mode and *d*) ORed in.

- **Read size.** Each element is one packet of the read size:
  - **32 bits:** one chunk per element, a 32-bit packet. This is how raw 32-bit keys are sent one per packet (step 1 of the sort, 4.9).
  - **64 bits:** one record per element, a 64-bit packet.
  - **128 bits:** one row's worth per element, **one 128-bit packet**, so a visit takes 1 cycle like the others. (v1.0 sent two 64-bit packets on consecutive cycles and took 2.)
  - **Reserved encodings are errors wherever they appear:** a stream with read size 11 (undefined: what it reads and sends is not specified), with mode 5–7, with *d* = 67–127, or a mode-4 stream with *d* ≠ 0, is an error even at count 0. So is a mode-3 stream with read size 128, or a mode-0 stream whose destination address isn't aligned to its read size, whose packets are bad packets at the receiver (3.1), and a stream whose source isn't aligned to its read size (4.1). None has a hardware check at the sender (meshsim reports them); a misaligned source is caught when it is read (below).
- **Gap after** *(likely to change)*. In a stream run, the address generator issues nothing for this many cycles after the stream's last element, for example 4 on every stream of a one-word exchange (above). Group runs ignore it and use the group gap.
- **Element addresses are computed, never stored.** Element *e* of a stream (*e* = *j* in a stream run, *r* in a group run) is read at source + *e* × (chunks per element: 1, 2 or 4), a shift and a 21-bit add. The source is aligned to the read size, so every element is (4.1). **A misaligned element is not read:** its packet still goes out, with payload 0, and `ERRORS` [1] is set (4.1). For mode 0 the destination address is `STREAM_DEST` + *e* × (4, 8 or 16 bytes), by plain addition; a run past the end of its memory is a software bug (2.2). The address generator never writes `STREAM`, so a run leaves its entries as the controller wrote them. There is no base register: 21 bits of chunk address cover up to 8 MB.
- **Receiver-placed destinations.** For modes 1–3 big-SRAM destinations are chosen by the receiver: modes 1 and 3 land at `PTR[src]` and mode 2 at `PTR[bucket digit]`. A local copy is a mode-1 stream to *d* = 0 with `PTR[me]` as the destination, or a mode-0 stream to an explicit address in the tile's own region 0x03. A mode-4 stream is a gather stream to the tile itself (4.4).
- **Relative destinations** make the destination, mode and size of every stream identical on every tile for the 16-group schedule, which is defined in offsets. The cost is a 6-bit adder and a select on *d*'s top bit; packets on the wire carry the absolute dst.
- **The address generator sends to tiles 0–63 and to the controllers** (*d* = 64–66), never to broadcast. *d* = 67–125 names no endpoint and 126–127 are invalid; both are programming errors with no hardware check (meshsim flags them).
- **Count** is in elements, **17 bits**: at most 131,071 per stream (owner, 2026-10-29). The full-size sort's local streams have exactly 65,536 elements, the keys per tile, which 16 bits would truncate to an empty stream. Longer moves are split into several streams; moves are better broken into smaller transfers anyway (about 64K cycles each already).

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
- **Period *t*.** Every visit takes 1 cycle, whatever its read size, so a group's period is *t*. (In v1.0 a 128-bit visit took 2 cycles, and a 128-bit group's period was 2*t*.)
- **Timing.** A group occupies exactly R·(period) + gap cycles, or 1 + gap when R = 0 (below). A longer period is made with padding streams.
- **R = 0:** the group sends nothing and takes **1 cycle** plus its gap, the address generator's pass over its `GROUP` entry. R is a global maximum, so a zero-length group is zero-length on every tile and the cycle is the same everywhere: it is timetable, not lateness. (A run of more than about `ag_lookahead` − 1 consecutive zero-length groups costs a further 2·`ts_engine_latency` + 1 once, also identical on every tile [meas: meshsim].)
- **The link-load bound** assumes a tile sends at most one element per rotation to each destination; the program keeps one stream per destination per group.

**The digit all-to-all schedule** (`spec/schedule.md`). 16 groups of 4 streams, every group at T = 4 for every value size, with loads ≤ 4 in all four directions; Σ T = 64, the lower bound. (v1.0's 128-bit values ran at T = 8, two packets per visit; in v1.1 a 128-bit visit is one packet, so the 64-bit schedule serves them unchanged.)
- **Gap: 0 cycles** between groups. Gap 0 gives the same queue peaks as gap 4 on uniform and on all the skewed sets, with no drift [meas: meshsim], and saves ≈ 60 cycles per all-to-all. **Exception:** phases that run every stream at the full window (the two-hop layers TH and BT, section 14) need a **gap of 4 after each group**, or the receivers' eject FIFOs overflow at group boundaries [meas: meshsim, the sort program; peaks 6 on links and 4 at eject with gap 4; v1.0 measurement; to be re-measured in v1.1].
- **Measured** against a nine-group schedule [meas: meshsim, `AllToAllTest measure`; v1.0 measurement; to be re-measured in v1.1]:

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
- **Such waits are part of the timetable,** not lateness: the read timing depends only on the tables and `ts_engine_latency`, which are the same on every tile, so every tile waits the same and the run stays in lockstep. This holds because the address generator is the only engine reader of `STREAM` and `GROUP`, each in memories of its own (4.10). Only a wait caused by the SRAM read queue refusing a request is lateness, reported as the **drift** error, `ERRORS` [3] (4.13).

**Timetable** (group runs). Group *g* begins at cycle `S_g = S_{g−1} + max(R_{g−1}·P_{g−1}, 1) + gap_{g−1}`, where **S_0 is the first-request cycle: 4·L_ts + 3 cycles after the start store** (4·L_ts + 2 after the cycle the start takes effect; the lead-in, above). A stream run has no timetable; its first request is offered 2·L_ts + 2 cycles after the start store. Its rotations begin at `S_g`, `S_g + P_g`, `S_g + 2·P_g`, …
- A rotation visits the group's streams in order, one visit time each.
- A stream with elements left performs a visit; otherwise the time is idle.
- The timetable governs when requests are *issued*. Each request holds a token, and its metadata waits in the address generator's **return queue** (4.5) until its data returns, so emitted packets trail the timetable by a constant while nothing stalls.
- During a gap the address generator issues nothing.
- A visit whose `STREAM` entry is not in yet waits; that wait is part of the timetable, identical on every tile, not lateness (above). With `ag_lookahead` at its bound it doesn't happen [meas: meshsim].
- **The timetable assumes a bounded read latency.** A read can wait in its area's queue (4.1). The 64 tokens and the return queue must absorb the queueing, which the phase's placement bounds *(Open)*.

**A visit** (modes 0–4) offers one read request per element, of any read size, and the element returns as one packet:
- payload = the element (0 if its read was misaligned, above);
- size = the stream's read size;
- dst = (`me` + *d*) mod 64 for *d* < 64, or *d* for *d* = 64–66;
- mode and counter = the stream's;
- address = for mode 0 the stream's current destination address, otherwise 0.

**One read per element.** Each element is one read-queue request; its return goes straight to injection (4.5). A stream of elements of any size therefore uses one access per cycle in its source area, within budget when sources and destinations are in different areas, which placement already requires.

**Loopback runs.** A stream with *d* = 0 sends to the tile itself.
- **A copy into controller memory** (an overlay, a table) is a one-stream mode-0 stream run to the tile's own region 0x00 or 0x01. At 128 bits per cycle (read size 128) a 1K-word image takes ≈ 256 cycles plus the stream's start [estimate]; v1.0's 64-bit packets took ≈ 512. Section 6.5 gives the sequence.
- **A copy into a peripheral** is a mode-0 stream run into its region (4.17.5): for the replayer, one of its memories or queue ports (4.19.10).
- **A local big-SRAM copy** is a mode-1 run with `PTR[me]` as the destination, or a mode-0 run at an explicit address.
- **The local sort's scatter passes** are mode-2 runs to the tile itself.
- **Gather streams** are mode-4 runs to the tile itself (4.4).

### 4.3 Controller read port

*Figure 3.3.*

The controller reads its own big SRAM without stalling: it queues reads by stores, and the results land in data memory as mode-0 writes to the tile itself (owner, 2026-10-26; destination, owner, 2026-10-28).

| register | store | load |
|---|---|---|
| `SRAM_RD_DEST_ADDR` | the **setup register**, R/W (8.8): **destination** [16:0], the byte offset in **data memory** (region 0x00) of the next result; **size** [18:17], 00 = 32 bits, 01 = 64, 10 = 128, 11 reserved; **counter** [21:19], the write counter the results decrement; [31:22] reserved (the mode is always 0 and the dst the tile itself). The layout is the common header's (8.1) | the next result's destination, as advanced, with the size and counter |
| `SRAM_RD_ADDR_QUEUE` | queue a read of 32, 64 or 128 bits (the setup register's size) at the 21-bit big-SRAM chunk address written, which must be aligned to the size | the number of free entries in the request queue |

- **Issuing.** A store to `SRAM_RD_ADDR_QUEUE` puts an entry (the read address, the current destination, the size and the counter) into the **request queue**, then the destination advances by the size in bytes (4, 8 or 16). **The store never stalls.** The destination stays in data memory (owner, 2026-10-28: v1.1 let results land anywhere in controller memory, program memory included); running past its end is a software bug (2.2). Size 11 is undefined behaviour (meshsim).
- **Alignment** (integrator, 2026-10-29). The read address must be aligned to the size (4.1). **A misaligned read is not performed, but its packet still goes out as usual with payload 0**, so the director counts it, and `ERRORS` [1], "bad packet or misaligned access", is set (4.13). (v1.1 dropped it and set the read-port error, [7].) The destination must be aligned to the size too; a misaligned destination makes a misaligned mode-0 packet, a bad packet at the director (3.1).
- **The request queue** is a parameter of **at least 64 entries** (64 × 43 bits, ≈ 2.8 Kbit: a 21-bit address, a 17-bit destination, a 2-bit size and a 3-bit counter).
- **Free entries** = depth − (queued + in flight). An in-flight counter rises when the SRAM read queue takes a read-port read and falls when that read's token is injected (the read carries a read-port bit, 4.5). Read-port results may come back in any order (4.1): each carries its own destination.
- **Reading.** The queue drains into the SRAM read queue at the **lowest priority** (4.5), so it never delays the address generator's timetable or the GATHER engine. During an all-to-all it simply waits.
- **Results come back as packets.** Each return becomes **one mode-0 packet to the tile itself**: dst = `me`, address = 0x00000000 + the entry's destination, size and counter = the entry's, payload = the 32, 64 or 128 bits read. It leaves through the injection point like any other read return, loops back through the router into the traffic director, which writes it through port B and decrements the entry's counter K cycles later (4.12). It must stay out of the command and reply blocks (invariant 11, meshsim). Results may land in any order (4.1, 4.5): each lands where its entry named, so the order of issue still decides where each result goes. The read port has no path of its own into port B.
- **The loopback's cost** is small: a search step's fixed cost through it is 27.9 cycles [meas: meshsim, SearchBench; 1-cycle register loads, to be re-measured at 2], 2 cycles more than a direct path into port B, with the per-read cost unchanged; on a uniform split it costs ≈ 0.25K cycles (200,749 against 200,509, +0.12 % [meas: meshsim, SplitRun; 1-cycle register loads, to be re-measured at 2]).
- **No stalls.** The queue is guaranteed to hold at least 64 entries, so a whole round of one search per owner (63 reads) can always be issued at once into an empty queue. Reading `SRAM_RD_ADDR_QUEUE` gives the free entries, and the counter the program armed for the reads (readable, 8.7) gives the results still outstanding. Software issues as many reads as are free. Results may land in any order (4.1), so a loop that needs particular results waits for its whole batch (its counter done), as the search loop below does. A deeper queue only makes such a loop more efficient.
- **Read-port results and incoming traffic.** Results return as packets through the tile's own injection point, loopback FIFO and eject output, sharing them with the tile's other outgoing read returns and with every packet arriving from the links. The network has no flow control (3.2): a full FIFO drops the packet, and that is an error.

  **Software rule: a tile does not issue read-port reads while packets are arriving for it, and does not start a phase that receives packets while read-port results are still outstanding.** In practice:
  - read-port reads run in steps of their own;
  - before a search step, every expected arrival has landed (its counter is done);
  - before the next exchange, every read-port result has landed (its counter is done).

  The split works this way. C&C 1's next command (uncounted mode-0 writes to the command block, 4.12), sent right after each GO (6.6), may land during a search step; it is allowed.

  Memory-system deliveries (6.7) are incoming traffic like any other. The hardware tolerates little overlap, and programs must not rely on it [meas: meshsim, ReadPortGatherTest read-port tolerance; R results issued one per cycle at offsets 0–24 cycles from a controller-memory stream's start; v1.0 measurement; to be re-measured in v1.2]:
  - **Against a stream at one packet per cycle: 15 results, cumulatively over the whole stream.** The eject output serves one packet per cycle, so each result that takes an eject slot leaves one incoming packet waiting in its link's eject FIFO, and the stream never lets it drain. 16 results overflow the 16-entry FIFO at most offsets, however they are spread out.
  - **Against a stream at half rate: 32 results in one burst.** The link's FIFO does not grow (peak 1); the results' own loopback FIFO fills at half a packet per cycle and overflows at 33. The backlog drains between bursts.

  With the v1.1 loopback (4.5, 4.7) *(Proposed)* the loopback FIFO cannot overflow, so the half-rate case becomes a delay rather than a loss; and since the eject output now serves incoming packets before the loopback (4.7), results no longer take slots from a full-rate stream either. The rule stands until the simulator re-measures both cases (items 24 and 70). **meshsim has now re-measured** [meas: meshsim, part M]: **no loss** when read-port reads overlap incoming traffic, either an all-to-all or a full-rate stream, where v1.0 lost 561 packets. The rule is **unchanged for now and recorded as a candidate to relax** (item 116).

  meshsim checks the rule: a packet from outside the tile, other than C&C 1's command writes, arriving while the tile has read-port results outstanding is a violation (invariant 14).

  *This rule exists because the routes have no flow control. If flow control is added, it should be revisited, together with the deadlock questions that flow control raises.*
- **Overflow:** a store to `SRAM_RD_ADDR_QUEUE` with no free entry is a programming error: the read is dropped and the read-port error, `ERRORS` [7], is set (4.13). The destination advances on every store, a dropped read included (the simplest hardware: the increment needs no gating). A dropped read sends no packet, so its counter stays short and the program waits; the error says why.
- **An entry is freed when its return is injected.** Its counter counts the result K cycles after the director accepts it, by which time the result is in data memory (4.12), so when the counter is done every result is readable and the queue is empty, with at least 64 entries free. The converse does not hold: 64 free entries say every return has left, and the last results can still be in the loopback for a few cycles. Only the counter's done says they have landed.
- **The search loop** (the split):

  ```c
  while (tile->SRAM_RD_ADDR_QUEUE < 64) ;     // once, before the first step: never stalls (below)
  for each step, while searches are active:
      tile->WRITE_COUNTER_2   = n_active;      // at most 63
      tile->SRAM_RD_DEST_ADDR = (uint32_t)results | SIZE_32 | COUNTER_2;
      for each active search: tile->SRAM_RD_ADDR_QUEUE = its midpoint;
      wfi();                                   // every result has landed: the queue is empty again
      for each active search: compare results[i], update its bracket;
  ```

  Each step issues at most 63 reads into an empty queue, so the loop needs no free-count checks. The initial poll is the only one, and it never stalls: any earlier read-port reads were in a phase that armed a counter for them, and **a phase that arms counters ends with a `WFI` before it pulses `done`** (4.12), so their results have all landed and the queue is empty. The poll guards against a program that breaks that rule; it cannot catch results still in flight, so it is no substitute for the rule. Converged searches drop out (an early exit), so a step's reads scale with the searches still active. The split's search loop takes 12.35 cycles per search on uniform data, counted between the split's trace marks 0x600 and 0x601 (the step loop and the final key read, over all tiles, per search step) [meas: meshsim, SplitRun; measured with v1.1's results and 1-cycle register loads, to be re-measured at 2]. The split's other traffic on the same counter (its vectors, an overlay copy) is kept out of a search step.
- **Never between `WFI` and a start store.** The synchronised-start path must be identical on every tile (6.3); read-port traffic belongs before `done` or after the start.

### 4.4 GATHER engine

*Figures 7.4, 7.5.*

The sort reorders values by a sorted index stream twice (section 14, steps 2 and 6), and both operations have the same shape. **The address generator only streams the index, and a small GATHER engine after the traffic director does the value reads.** Gathers are **local only**.

**How it works.**
1. **The address generator streams the sorted (key, ordinal) words to the tile itself as mode 4** (a mode-4 stream with *d* = 0, 4.2), the gather request. The ordinal is in bits 63–32, where augmenting stamps it (mode 3, 4.9). Only these 64-bit words cross the loopback.
2. **The traffic director passes each mode-4 packet to the GATHER engine** through a **16-entry request queue**, with its counter field.
3. **For each request the GATHER engine puts *k* gather-value reads in the SRAM read queue** (4.5), at `DMA_RECORD_BASE_ADDRESS` + 2*k*·ordinal + 2*i* for *i* = 0 … *k* − 1, 64 bits each. They rank above the address generator and the controller read port.
4. **Each return goes out as a 64-bit mode-1 packet to the tile itself** (dst = `me`, address 0), **carrying the request's counter field**: a **gather value**. The value reads stay 64-bit in v1.1; reading a row-aligned 128-bit value as one packet is section 13's item.
5. **The gather values land at `PTR[me]`,** which advances by 2 per packet. The controller sets it to the start of the reorder buffer. The index arrives in sorted order and the output is one sequential run, so value *i* lands at base + 2*k*·*i* with no addresses.

**Parameters.**
- **`DMA_RECORD_BASE_ADDRESS`** (8.7): in a gather run, a 21-bit chunk address, the start of the value buffer, **aligned to a record** (even). Ordinals count 0, 1, 2, … from the base of the run that augmented the keys (4.9), so value *i* is at the base + 2*k*·*i* with no offset arithmetic. One base serves each run: no run both augments and gathers.
- ***k*:** the value size in records, **1–8**, a `DMA_CONFIG` field (*k* − 1 in [26:24], 8.5).
- **`PTR[me]`:** the output start.

**Datapath.**
- **Each request-queue entry holds only the 20-bit ordinal** (payload bits 51–32) and the 3-bit counter field; the key half and bits 63–52 are dropped.
- **The start address** `DMA_RECORD_BASE_ADDRESS` + 2*k*·ordinal is computed in the cycle a request reaches the queue's head, and its first read is offered the next cycle. The *k* reads follow, one offered per cycle at priority 1 (4.5), each stepping the address by 2 and pushing a token that sends the value back to the tile as mode 1. With an even base every read is aligned; with an odd one each read is not performed, its gather value goes out with payload 0, and `ERRORS` [1] is set (4.1). Running past the end of big SRAM is a software bug (4.1).
- **A mode-4 packet arriving with the queue full** is dropped and sets GATHER queue overflow, `ERRORS` [6] (4.13).
- **GATHER busy** (`STATUS` [2], 8.2): the request queue is non-empty, or a request is part-way through its *k* reads. Reads issued but not yet landed are not included; their write counter covers their landing.

**Counting** (owner, 2026-10-28). Gather requests (mode 4) are **never** counted, whatever their counter field says: they aren't writes. **Their counter field is passed on to the gather values** (mode 1), which are counted like any other write as they pass the traffic director (guarantee C, 4.10), so a gather of *n* values arms that counter with *n*·*k*. Its done event, and counters done (4.12), follow K cycles after the last gather value's acceptance.

**Rate.** Every value costs 1 request packet plus *k* gather-value packets through the one injection point and the one eject, so **≈ *k* + 1 cycles per value**: 3 at *k* = 2, about 196K cycles per 64K values [estimate]. The value reads (*k* per value, 64 bits each) and the gather values' writes fit within one access per cycle per area if index, values and output sit in three different areas (4.1).

**Back-pressure is implicit.** The address generator gets a turn in the SRAM read queue only when no gather value is waiting, so once requests are queued its index reads stop and the queue drains. The queue then holds at most what was already in flight: index reads issued before the first request looped back, about the read latency plus the loopback, ≈ 10 at one per cycle, under 16 [estimate]. A tile's own sources cannot outrun the engine: requests from the address generator wait for the SRAM read queue, where the GATHER engine outranks it, and requests from the send port share the injection point with the gather values. With *k* = 8 and 64 requests sent as fast as the send port goes, the queue peaked at 0 of 16 [meas: meshsim, CondTest's GATHER probe]. So the queue overflows only on a fault (4.13, `ERRORS` [6], H). The rate under a heavier read-queue load from the tile's other clients, which can only slow the sources further, is *(Open)* (section 13). A gather phase sends only to the tile itself, so there is no network timetable for the waiting to disturb.

**Why gathers are local.** Mode 4 is sent only to the tile itself (invariant 10). A remote gather would work mechanically, but its gather values would land grouped by owner at `PTR[owner]`, not in one sorted sequence, and the requester's eject would see *k* packets per request with the owner's address generator stalling, which breaks the timetable.

### 4.5 SRAM read queue, return path and injection

*Figures 7.2, 7.3.*

**The SRAM read queue.** Everything that leaves a tile, except the controller's own packets, is a big-SRAM read return turned into a packet. So the big SRAM's read side is a single queue of requests, each carrying:

| field | meaning |
|---|---|
| source address | the chunk address to read, consumed by the memory (kept in the return for debugging) |
| destination address | the outgoing packet's 32-bit address: for mode 0 the stream's current destination address, for a read-port read its result's place in data memory (a mode-0 packet to the tile itself, 4.3); otherwise 0 |
| width | 32, 64 or 128 bits; stamped as the outgoing packet's size |
| counter | the outgoing packet's counter field |
| mode | the outgoing packet's mode |
| dst | the outgoing packet's destination |

**Request priority,** highest first:
1. **gather value:** the GATHER engine's value reads (4.4);
2. **the address generator's** next request (4.2);
3. **the controller read port** (4.3).

The address generator is never starved in a digit all-to-all, where it is the only source besides a waiting read port.

**The SRAM read queue has no storage of its own.** It is the fixed-priority choice among its three clients plus the steering to the area (chunk-address bits [20:17] the area, [16:2] the row, [1:0] the chunk, at meshsim's default geometry). It takes the chosen request only if **the area has room** (4.1) **and a token is free** (below). Otherwise nobody is served that cycle: lower-priority clients do not get the slot (no bypass). A refusal of the address generator in a group run is drift (4.2, 4.13).

**Tokens and the return queues.** There are **64 tokens**, one pool shared by the three clients *(Proposed, accepted)*, and every read the queue takes holds one. The **address generator** and the **GATHER engine** each have an **in-order return queue**: a plain queue of that client's held reads' metadata, the request's fields, in the order its reads were taken. A read-port read also holds a token; its metadata travels with the read and carries a read-port bit (4.3). The return queues replace v1.1's earlier single token FIFO in request order (item 74).
- **Each ordered client's returns come back in its own order** (4.1, owner, 2026-10-23): the big SRAM hands back the address generator's reads in the order it issued them, and the GATHER engine's in theirs, so each such return pairs with the head of its client's return queue and goes to injection with it. The order is required within a client: mode-1 packets and gather values land at `PTR` in arrival order, so a stream's packets must leave in the order the stream issued them. **Between clients there is no order,** so a gather's scattered reads never wait behind a stream's sequential ones. **Read-port returns come back in any order** (4.3). (In v1.0 each token had a 128-bit data slot and a valid bit, the areas returned rows in any order into their tokens' slots, and the head was released when its data was valid.)
- **Injection order among the clients** *(Proposed, accepted)*: in a cycle when more than one client has a return ready, the injection point takes them by the request priority above, gather value first, then the address generator, then the read port. A return that loses waits, and its client's later returns wait behind it.
- **One client per order-placed destination** (owner, 2026-10-23). Order holds only within a client, so **a tile never has two clients sending order-placed packets to the same destination at once**: modes 1, 2 and 3 into the same `PTR` entry of the same receiver, or writes into the same order-placed port of a peripheral (a replayer queue, 4.19.10). Such a mix would interleave unpredictably. The clients are the address generator, the GATHER engine and, as a source of packets, the send port (4.11) (owner, 2026-10-23, item 80). **A peripheral's order-placed port takes one source at a time** across sources too (owner, 2026-10-26): a peripheral's region may be written from any source (4.17.5), so no two tiles, or a tile and a non-tile endpoint, feed the same queue port at once (meshsim flags a violation, invariant 18).
- **A token is freed** when its packet is injected, **except a loopback packet's** *(Proposed)*: a packet for the tile itself (dst = `me`) holds its token until it leaves the loopback FIFO for the eject selector (4.7). **The send port's loopback packets take a token too:** a send-port packet for the tile itself, or a broadcast (whose copy for the tile itself goes through the loopback), waits in the holding register until a token is free, takes it when injected and holds it the same way (4.11).
- **So the loopback FIFO cannot overflow,** whatever arrives from the links: at most 64 packets are ever on their way into it or in it, and it is at least 64 deep (4.7). When the eject output is busy with incoming traffic, the tile's own loopback reads wait for tokens instead. In a group run that wait is a refusal like any other (drift, 4.2), so the all-to-alls, which send little to the tile itself, are unaffected in a correct program. **This exists only because the mesh has no flow control** (3.2); with flow control it would be revisited.
- **Chunk selection.** Every access is naturally aligned (4.1), so the data is taken from the one returned row, starting at the address's chunk.
  - a 32-bit read gives one chunk, a 32-bit packet (payload [31:0]);
  - a 64-bit read gives two consecutive chunks, a 64-bit packet;
  - a 128-bit read gives four consecutive chunks, **one 128-bit packet**. (v1.0 sent them as two 64-bit packets on consecutive cycles.)
- **A misaligned read** is not performed: the return carries payload 0 and goes out as usual, in its client's order (4.1). **An out-of-range read** is undefined (4.1).

A return carries the request's fields, its size among them, plus the data, and goes straight to the injection point as one packet. A read-port return goes there like the others, as a mode-0 packet to the tile itself (4.3).

**The SRAM write queue** is the other half: one queue of 32-, 64- and 128-bit writes from the traffic director (mode 0 to region 0x03, and modes 1–3, 4.9). The two queues meet only at the areas, which serve in arrival order (4.1). There are no client selectors and no priority bits.

**The injection point** takes at most one packet per cycle, from:
1. **the SRAM read queue's returns;**
2. **the controller's send port** (4.11).

Each supplies dst, mode, counter, address, size and payload (7 + 3 + 3 + 32 + 2 + 128 bits); the router adds src and the reserved zeros (3.1).
- **Priority:** read returns first; the controller's packet waits in its holding register, with no fairness, and a send-port packet for the tile itself or a broadcast also waits for a free token (above). Every return, 128-bit ones included, takes the injection point for one cycle. Injection never stalls (the network has no flow control; a full output FIFO is a router overflow, `ERRORS` [0]).
- **The injection point keeps these two sources** (owner, 2026-10-28). A controller's packets for the mesh enter tiles 0–2's routers through their fifth ports (4.16), and a peripheral that sends uses a router port of its own (4.17.9); neither passes through a tile's injection point.

The injected packet takes the same route decision as a link input (4.6, with "arriving direction" = none):
- a packet whose dst is `me` or 126 goes to the eject queues via the **loopback** input;
- a packet for the endpoint attached at this tile's fifth port goes to the fifth-port output (4.16): dst 64, 65 and 66 at tiles 0, 1 and 2;
- otherwise it goes to a link output queue.

### 4.6 Route blocks

*Figures 4.1–4.3.*

There is one route block per link input (from left, from right, from up, from down) and one for the loopback; tiles 0, 1 and 2 each have a sixth, for the input from their controller (4.16). Each cycle a route block takes the packet arriving on its input (idle packets are discarded) and forwards it, in one cycle, to one or more output queues (4.7) or the eject queue.

**Unicast (dst in 0–66).** The route is fixed per (source tile s, destination d): horizontal first, then vertical. A controller's id stands for the tile it is attached to, id − 64, as source and as destination.
- Let k = (d − s) mod 64 (for d = 64–66 use d − 64).
- The machine holds a 64-entry **offset table** `hops[k] = (b_k, a_k)`, a hard-wired ROM identical on every tile, with k ≡ 8·a_k + b_k (mod 64), |b_k| ≤ 4, |a_k| ≤ 4 and |a_k| + |b_k| minimal.
- Where the minimum is not unique (only for a_k = ±4), the table records the schedule's choice: offsets 29–32 go down and 33–35 go up (`spec/schedule.md`).
- A packet travels |b_k| hops horizontally (right if b_k > 0, left if b_k < 0), then |a_k| hops vertically (down if a_k > 0, up if a_k < 0).

A route block decides from src, dst and `me`:
1. k = (dst − src) mod 64 (src or dst 64–66 → id − 64); look up (b_k, a_k).
2. Horizontal progress: if b_k ≥ 0, `hb = (col(me) − col(src)) mod 8`, else `hb = −((col(src) − col(me)) mod 8)`.
3. If hb ≠ b_k: forward horizontally in the direction of sign(b_k).
4. Else if me ≠ d (d = dst, or dst − 64 for a controller): forward vertically in the direction of sign(a_k).
5. Else: EJECT (place in this route block's eject FIFO) — except a packet for the controller attached at this tile's fifth port (dst = 64 + me at tiles 0–2), which goes to this route block's FIFO of the fifth-port output (4.16).

Packets from a controller carry its id (64–66) as src and are routed as from its tile (src − 64). Its route block on that tile decides as an injection there (arriving direction none).

**Broadcast (dst = 126).** Every tile 0–63 receives exactly one copy. With s = src (64–66 treated as src − 64):
- **On injection at the source** (for a controller, at its route block on its tile): eject a copy, and send copies right, left, down and up. A broadcast is not delivered to the controllers.
- **Arriving from the left:** eject a copy; if `(col(me) − col(s)) mod 8 ≠ 4`, send a copy right; send copies down and up.
- **Arriving from the right:** eject a copy; if `(col(s) − col(me)) mod 8 ≠ 3`, send a copy left; send copies down and up.
- **Arriving from up:** eject a copy; if `(row(me) − row(s)) mod 8 ≠ 4`, send a copy down.
- **Arriving from down:** eject a copy; if `(row(s) − row(me)) mod 8 ≠ 3`, send a copy up.

### 4.7 Output queue sets and selectors

*Figures 4.1–4.3.*

There are five outputs: to-right, to-left, to-down, to-up and eject. Each has one FIFO per possible feeder, **16 entries** *(param `queue_depth`)* of one **256-bit** link word (3.1), except the eject output's **loopback FIFO**, which is **at least as deep as the token count** (4.5): 64 entries at the default *(Proposed)*:

| output | feeders |
|---|---|
| to-right | injection, from-left |
| to-left | injection, from-right |
| to-down | injection, from-left, from-right, from-up |
| to-up | injection, from-left, from-right, from-down |
| eject | loopback, from-left, from-right, from-up, from-down |

That is 17 FIFOs per tile.
- **Selection.** Each cycle each output's **selector** removes one packet from one non-empty FIFO and sends it out (onto the link for link outputs, 3.2; to the traffic director for eject). It is round-robin with a rotating pointer: it serves the first non-empty FIFO at or after the pointer, then moves the pointer one past it. Pointers are 0 at reset.
- **The eject output serves its loopback FIFO last.** Its selector takes from the loopback FIFO only in a cycle when every other eject FIFO is empty (from-left, from-right, from-up, from-down, and on tiles 0–2 from their controller); those are served round-robin among themselves, as above. Incoming packets therefore never wait behind the tile's own traffic, so an eject FIFO fed by a link drains as fast as its link fills it, whatever the tile sends itself. The loopback can wait safely because it is token-protected (4.5): its sources simply hold back. A tile's self-stream now gets only the eject slots its links leave free; that this causes no drift in group runs is to be confirmed in simulation *(Open*, item 70*)*. (Before the request bus, the loopback FIFO was one of the round-robin's five.)
- **Overflow.** A FIFO receives at most one packet per cycle from its feeder. A push to a full FIFO (full by its count before the cycle's pop, 4.13) is an **overflow**: the packet is lost and router overflow, `ERRORS` [0] (4.13), records it. Overflow never occurs under a correct program, and the loopback FIFO cannot overflow at all: every packet entering it holds a token until it leaves (4.5).
- **Order.** Packets are never reordered within a FIFO, so packets from one source to one destination arrive in the order sent.
- **Every tile** has a fifth-port feeder FIFO on each of these five outputs and a sixth output, the fifth port's (4.16); they carry traffic only where something is attached (tiles 0–2's, from and to C&C 1–3).

### 4.8 Traffic director

*Figure 5.1 (at v1.1).*

One unit straight after the eject selector, and **the only path into the tile**. It takes at most one packet per cycle and switches on the packet's **mode**, and for mode 0 on its address's **region** (2.2):

| packet | goes to |
|---|---|
| a bad packet (3.1) | dropped, uncounted; sets `ERRORS` [1] (4.13) |
| mode 0, region 0x00 or 0x01 | port B of data or program memory: 32, 64 or 128 bits by size (4.11) |
| mode 0, region 0x03 | the write engine: a write at the packet's address (4.9) |
| mode 0, region 0x04–0x06 | that peripheral's write port (4.17.5) |
| modes 1, 2 and 3 | the write engine, at `PTR` (4.9) |
| mode 4 | the GATHER engine's request queue (4.4); never counted |
| modes 5–7 | discarded: no write, no count, no error (meshsim flags them) |

**Counting: the delay line** (owner and Tony, 2026-10-28). For each packet it accepts in modes 0–3 with a counter field of 1–4, the director puts the counter's id into a **K-stage delay line**, and K cycles later that `WRITE_COUNTER` is decremented (4.12). A counter field of 0, or a reserved 5–7, decrements nothing. At most one packet per cycle leaves the director, so a counter takes at most one decrement per cycle. Unexpected arrival is judged at the decrement (4.12).

**K** (integrator, 2026-10-29; owner, 2026-10-29) is a parameter, **default 8**, large enough that by the time a packet's counter decrements, **its write is in place**:
- **controller memory:** port B lands a write `cm_latency` = 2 cycles after the director [built];
- **a peripheral** lands it within `rp_latency` = 2 cycles (4.17.8);
- **big SRAM:** a write only has to have **entered its area's request queue**, which is served in arrival order, so a read issued after the counter reaches 0 queues behind it and cannot overtake it (4.1). With spanning gone (4.1) the write path is fixed, and its loaded worst case is **2·L + 3** cycles, L = `ts_engine_latency` [derived];
- **the tables:** a `PTR` or `HIST` update is readable through the table window **L + 4** cycles after acceptance [derived] (guarantee A, 4.10).

So **K ≥ max(2·L + 3, L + 4, `cm_latency`, `rp_latency`)**: 5 at the default L = 1, and K = 8 covers L ≤ 2. meshsim errors if K is set below the formula, and its measurement of the margin (item S1, 13) becomes a check of the formula. Write combining (item 3) stays out unless it fits under K.

**Command landed** (owner, 2026-10-28). The director decodes command slot 0 from the address: **a write that comes in through the director and covers command slot 0 sets "command landed" when it lands in port B**, not when the director accepts it, so the event (and READY, and the boot ROM's jump, 6.2) follows the write. Any such write sets it, a 64- or 128-bit write starting at slot 0 included (it also writes slots 1–3, harmlessly). The controller's own store to slot 0 doesn't (4.12).

**Consequences:**
- **Port B has one writer.** Port B of both controller memories is written only by the director, at most once per cycle (up to 128 bits across its four banks, 4.11), so it needs no arbitration and no ownership setting.
- **No state of the unit changes routing or counting.** A unit asleep in its boot ROM lands uncounted writes like any other (6.2); there is no HALT state (6.1).
- **Unicast and broadcast** are handled alike at the director; dst does not matter here.
- **src is used** by the router (routes, the broadcast tree) and by modes 1 and 3 (`PTR[src]`). It is never stored in memory.
- **At a connected fifth port,** packets for the attached controller leave through the fifth-port output instead of the eject path (4.16).

The controller-memory path carries mode-0 packets for regions 0x00 and 0x01 to port B with a fixed latency (`cm_latency` = 2 [built]) and needs no queue. A peripheral's write port takes one packet per cycle and lands it within `rp_latency` = 2 cycles with no queue, a requirement on every peripheral (4.17.5, 4.17.8) *(Proposed, item 48: the value)*; the replayer's address map, sizes and ownership are in 4.19.10. v1.1's `cm_idle` and `mem_idle` are gone from the hardware (owner, 2026-10-28): K covers both paths, so counters done waits for the counters only (4.12).

### 4.9 DMA write engine

*Figures 5.1, 5.2 (at v1.1).*

The write engine receives big-SRAM writes from the director (at most one per cycle, through a 4-entry input FIFO), mode 0 to region 0x03 and modes 1–3, and queues each as one write at its area (4.1): the payload at its size for modes 0–2, a 64-bit record for mode 3. **The packet's mode says where every payload goes; the engine has no mode of its own.**

| mode | target chunk address |
|---|---|
| 0 | the packet's address: its byte offset / 4; no `PTR` access (below) |
| 1 | `PTR[src]`, then `PTR[src]` += 1, 2 or 4 (the size in chunks) |
| 2 | `PTR[t]`, *t* = the payload's bucket digit (4.15), then `PTR[t]` += 1, 2 or 4 |
| 3 | `PTR[src]`, then `PTR[src]` += 2: the record (key, ordinal), from a 32- or 64-bit packet's low 32 bits (augmenting, below) |

**`PTR[0..255]`** is its own memory (4.10). Each entry is a 21-bit chunk address set by the controller between phases.
- **Modes 1, 2 and 3 are the same write:** at `PTR[i]`, then `PTR[i]` += the chunks written, with *i* = src for modes 1 and 3 (0–63, or 64–66 for a controller) and the payload's bucket digit for mode 2 (0–255). Modes 1 and 2 write the payload at its size, 1, 2 or 4 chunks; mode 3 always writes a 2-chunk record. The mode picks the index, and mode 3 also augments.
- **A 32-bit packet in mode 1 or 2 writes 32 bits** and steps `PTR` by 1.
- **One user per phase:** `PTR[src]` (modes 1 and 3, which may mix) or `PTR[t]` (mode 2), never both.
- **One sending client per entry at a time** (owner, 2026-10-23): a sender's packets into one `PTR` entry land in order only if one of its clients sends them, since big SRAM orders reads per client, not globally (4.1, 4.5, invariant 18). A tile's own stream into `PTR[me]` and its gather values into `PTR[me]`, for example, never run at once.
- **`PTR[64]` is no longer special** (owner, 2026-10-28): boot and staging images go to region 0x03 by address (6.2), so uncounted traffic never touches `PTR` or `HIST`, and modes 1–3 are always counted (3.1).
- **The receiver decides where each sender's stream lands.** For an all-to-all it sets `PTR[p]` from the counts it already has (prefix sums), so senders need no destination addresses.
- **Stepping by size is general.** In-order delivery means a value sent as several mode-1 packets from one sender lands contiguously, whatever their sizes; the gather values of a *k*-record value are *k* 64-bit packets (4.4).
- **Alignment** (integrator, 2026-10-29). A `PTR` must be aligned to the size of the **writes** landing through it: for mode 3 to a record (even), whatever the packets' size (4.1). **A write through a misaligned `PTR` is dropped by the engine but still counted**, since its decrement was scheduled when the director accepted it, and sets `ERRORS` [1] (4.13). *(Proposed: the pointer still advances by the size, as for a good write.)* meshsim flags it.
- **The pointer round trip.** Every write reads and updates its pointer in `PTR`; the packet waits in a **pointer stage** until the old pointer returns, across `ts_engine_latency` each way. The stage holds **2·`ts_engine_latency` + 2 entries** (4 at the default 1) [meas: meshsim, peaks 3, 5, 7, 9 at `ts_engine_latency` 1–4].
- **Mode 0 writes at an explicit address.** Its byte offset / 4 is the chunk address. It passes the pointer stage in order with the writes around it, so the engine's writes still reach the areas in arrival order, but it makes no `PTR` access and moves no pointer. It writes the payload at its size. Its alignment is checked at the director, where a misaligned one is a bad packet (3.1); out of range is undefined (4.1). Count-on-arrival ignores it. The memory system delivers reads into big SRAM this way (6.7), boot and staging images land this way (6.2), and a controller can write a chosen address from the send port without touching `PTR[me]`.
- **A 128-bit packet is one write,** one full-row access.

**Count-on-arrival** (`DMA_CONFIG`'s histogram digit, 0 = off). For every accepted payload, the counter at the payload's histogram digit (4.15) is incremented in `HIST` (4.10).
- It works with modes 1, 2 and 3, not with mode 0.
- In a mode-2 pass, the pointer update and the counter update are taken in the same cycle, so a pass can bucket by the current digit and count the next one at one payload per cycle [meas: meshsim].
- **In an all-to-all**, the receiver counts the first digit of its next sort while the keys arrive, which saves a counting pass over the records: one full pass per sort.

**Augmenting** (mode 3). A mode-3 packet is 32 or 64 bits; the write engine keeps its low 32 bits, the key, and writes the 64-bit record (key, **ordinal**), the ordinal in bits 63–32: (target chunk address − `DMA_RECORD_BASE_ADDRESS`) / 2, which counts **0, 1, 2, …** across the buffer. A 128-bit mode-3 packet is a bad packet, dropped by the traffic director before it reaches the engine (3.1). Records land 2 chunks apart starting at the base, which is aligned to a record, so the difference is always even. The sender chooses augmenting per stream; the receiver sets `DMA_RECORD_BASE_ADDRESS` (8.7) for the run.
- **The ordinal is a position in the buffer, not an arrival count.** With mode 1, each sender's keys land in their own run at `PTR[src]`, and that sender's values land in the same order in the value buffer, so key *i*'s value is value *i* only if *i* counts buffer positions. Packets from different senders interleave on arrival, so an arrival counter would break the match.
- **Use:**
  - the keys all-to-all's streams send mode 3, so keys arrive as (key, *i*), ready to sort;
  - the values all-to-all's streams send mode 1;
  - **step 1** (local keys): raw 32-bit keys are streamed to the tile itself with read size 32 (4.2), one key per 32-bit packet, in mode 3, each landing as a 64-bit record, with `PTR[me]` = `DMA_RECORD_BASE_ADDRESS` = a new buffer. Each key gets *j* = 0, 1, 2, … One pass, one key per cycle.
- **The ordinal sits in bits 63–32,** where the GATHER engine reads it (4.4).
- **Mode 2 has no augmented form:** augmenting stamps the landing position, which in a bucket write is the position after bucketing, not the input order. An arrival-order form is a possible later addition (section 13).

**Counting.** Every write the engine takes was counted by its counter field at the director (4.8), the gather values included, K cycles after acceptance; K covers the engine's path into its area's queue, so a counter's done tells the tile its writes are ordered before any later read (4.12, 4.14).

**Overflow** of the input FIFO is an error, `ERRORS` [4] (4.13). It would overflow if an area's queue backed up for longer than the FIFO can absorb, so placement must keep a phase's writes and reads to one area within its one access per cycle.

### 4.10 Table memories

*Figures 3.1, 5.2.*

The tables are **separate macro memories**, each with its own ports: `HIST`, `PTR`, the three `STREAM` arrays and `GROUP`.

| memory | contents | size | written by | read or updated by |
|---|---|---|---|---|
| `HIST` | `HIST[0..255]`, 17 bits each | 256 × 17 bits | controller (clears) | count-on-arrival: one read-modify-write per cycle with bypass |
| `PTR` | `PTR[0..255]`, 21 bits each | 256 × 21 bits | controller (sets starts) | write engine, modes 1, 2 and 3: one read-modify-write per cycle with bypass |
| `STREAM_SRC`, `STREAM_DEST`, `STREAM_COUNT` | `STREAM[0..255]` as three arrays of 256 × 32 bits, read in parallel at the same index (4.2) | 3 KB (3 × 1 KB) | controller | address generator: one entry (one word of each array) per visit, read ahead (4.2) |
| `GROUP` | `GROUP[0..63]`, 64 bits each (4.2) | 512 B | controller | address generator: one entry per group, read ahead (4.2) |

**Ports.** Each table is effectively **dual-ported**: a controller side (through the window, one cycle) and an engine side.

| memory | controller side | engine side |
|---|---|---|
| `STREAM`, `GROUP` | write | read (address generator) |
| `PTR` | write | read, increment, write (write engine) |
| `HIST` | **read and write** (the prefix sum between radix passes reads all 256 counts) | read, increment, write (count-on-arrival) |

**`PTR` and `HIST`: how the increment uses the ports** (figure 5.2):
- **Port A** is the engine's read port only. **Port B** is a read/write port shared by the increment's write-back and the controller, **with the increment first**.
- **One read-modify-write per cycle:** port A reads the entry on cycle *t*, and port B writes the incremented value (+1, +2 or +4 for `PTR`, the chunks written; +1 for `HIST`) on *t* + 1. The engine's request always starts a read and loads a last-address register; the previous cycle's request drives the write.
- **Back-to-back increments of one entry:** the read result is ignored and the last value is used. So the macro must let the write land intact when port A reads the address port B is writing (to confirm with the memory-compiler numbers, section 13).
- **A controller access that meets an increment:** the increment wins. A controller write is lost, and a controller load returns the value the engine wrote, at the engine's address. Programs avoid it by the access rule below; the behaviour is defined rather than undefined.

**No contention between tables.** The address generator's `STREAM`/`GROUP` reads, the write engine's `PTR` updates and count-on-arrival's `HIST` updates each use their own memory, so none waits for another. Gapless stream runs therefore don't starve the pointer updates. The address generator is the only engine reader of `STREAM` and `GROUP`, so its read-ahead timing is the same on every tile (4.2).

**Placement:** next to the controller (section 12). The controller's accesses through its window take one cycle, and the engines pay the distance: every engine request and return crosses *(param)* `ts_engine_latency` cycles each way (default 1; 1–4 measured).

**Read-modify-writes** (`PTR` and `HIST`) take up to one per cycle each. A **bypass** covers the whole read-to-write-back pipeline, including the write-back cycle, so updates to the same entry at any spacing are not lost. The pipeline is inside each memory, so `ts_engine_latency` doesn't lengthen it. Each has a 16-entry request FIFO (overflow is table request overflow, `ERRORS` [5], 4.13). A `HIST` count that would exceed 17 bits is an error (invariant 6).

**Controller access.** Through its window in region 0xFF, **0xFFFF2000–0xFFFF35FF** *(Proposed: v1.1's low 16 bits kept, item 120)* (8.9), decoded to the memories: `HIST` at 0xFFFF2000, `PTR` at 0xFFFF2400, `STREAM_SRC` at 0xFFFF2800, `STREAM_DEST` at 0xFFFF2C00, `STREAM_COUNT` at 0xFFFF3000, `GROUP` at 0xFFFF3400. Only `HIST` can be loaded; `PTR`, the `STREAM` arrays and `GROUP` take stores only. One access at a time, at the register-load latency (4.11), and only when no engine is using that memory. A window access that meets an engine access is a programming error, with no hardware check (meshsim): for `PTR` and `HIST` the result is defined (the increment wins), for `STREAM` and `GROUP` it is undefined. There is no hardware clear; the controller writes zeros.

**Waiting for the tables** (owner, 2026-10-28). Software waits for the tables **by events**, like every other wait: it sets `EVENTS_REQUIRED` to "write counter *n* done and DMA run done" and executes `WFI` (4.12).
- **The arrival side** (incoming packets updating `PTR` for modes 1–3 and `HIST` for count-on-arrival, and gather values landing at `PTR[me]`) is covered by the write counter the program armed for them.
- **The local side** (the address generator reading `STREAM` and `GROUP`) is covered by **DMA run done**, `EVENTS` [7] (4.2). The counters alone are not enough, since a tile's arrivals can finish before its own run.
- **v1.1's `ts_busy` and `wait_tables()` are gone.** A helper that must work without knowing what ran before (a pass that starts by checking the tables are free) **may poll `STATUS.rd_running`**, a level, as a software choice (owner, 2026-10-28); otherwise `STATUS` is diagnostic.

The hardware guarantees this needs:

| # | guarantee |
|---|---|
| A | **K covers the table update:** by the time a packet's counter decrements, its `PTR` or `HIST` update is **complete**, meaning readable by a controller load through the window, `HIST`'s count-on-arrival included, behind its request FIFO (4.8) |
| B | **DMA run done fires only after the run's last table read has completed,** not when the last entry is walked (4.2) |
| C | **Gather values pass the traffic director like any loopback packet and are counted there** (4.4, 4.5): their K delay starts at the director like every other packet's, and a counter still takes at most one decrement per cycle |

**A window access right after done** is safe by K alone (integrator, 2026-10-29): there is no interlock hardware, and meshsim checks that no table-window access after a counter's done precedes a pending `PTR` or `HIST` update.

### 4.11 Controller

*Figures 3.1, 3.2, 3.4 (at v1.1).*

The unit's processor (Tony's core): an in-order **RV32I + Zmmul** core with **`WFI`**, the **CSR instructions** and the **machine-mode trap CSRs** (T, 2026-10-28): hardware multiply (`mul`, `mulh`, `mulhsu`, `mulhu`); no divide; no C or A extensions; **machine mode only**, with no privilege levels or their CSRs; no cache. Every unit's controller is the same design, C&C 1–3's included.

**Inputs** (T, 2026-10-28). The core has two:
- **the hard interrupt,** which takes a trap to the handler at `mtvec`, **even while the core is in `WFI`**. It is driven by the unit's errors, `ERRORS` ≠ 0 (4.13), a **level** that stays high until the reset. It is gated by `mstatus.MIE` and its `mie` enable, and the same gating applies to its release of `WFI`;
- **the wakeup,** which resumes the core from `WFI` **without a trap**. It is driven by the wake term (4.12) and is **level-sensitive**: if it is high when the core reaches `WFI`, the core does not sleep, even if it went high before the `WFI`.

**Instruction set.**
- **`WFI`** stalls the core until the wakeup input is high (4.12, 6.3), then continues at the next instruction.
- **CSR instructions are legal** for the CSRs the core implements. *(Open, Q1 with Tony, item 121: the exact list and the CSRs' reset values, `mtvec`'s in particular.)* A CSR instruction on any other CSR is illegal.
- **Illegal:** divide opcodes (`div`, `divu`, `rem`, `remu`), and CSR instructions on CSRs the core lacks, set illegal instruction, `ERRORS` [10], and trap.
- **Traps** (owner and Tony, 2026-10-28): bus errors, illegal instructions, `ecall` and `ebreak` **trap to the handler** at `mtvec` instead of halting the controller, as do a fetch outside program memory and the boot ROM and a misaligned jump target. **Every trap is terminal** (4.13): the handler never executes `mret`. `ecall`, `ebreak`, the fetch and the jump set no `ERRORS` bit (meshsim stops loudly on each, 4.13).
- **Programs** are C, built bare-metal (`gcc` for `riscv64-unknown-elf`, `-march=rv32i_zmmul_zicsr -mabi=ilp32`, with `WFI` and the CSR accesses from inline assembly) with a start-up file, a linker script and the generated register header `sw/mesh.h`. A C `/` or `%` by a non-power of two compiles to a library call. The register block and the peripheral windows sit in the top 2 KB of the map, so a register access compiles to an x0-relative `lw` or `sw`; through the header's struct pointers (8.1) a compiler may add one base-address instruction, the same on every unit (owner, 2026-10-29).

**Timing.** One instruction per cycle.
- Every load and store to data memory completes in one cycle, and the loaded value is usable by the next instruction (load-use 0). Loads from program memory and the boot ROM take one cycle too *(Proposed)*.
- A taken branch or jump costs one extra cycle; a multiply takes 3 cycles.
- A store at t updates the register at the end of t; a load at t+1 and the owning unit at t+1 see it. Status computed at t is readable at t+1.
- **Register-load latency** (owner, 2026-10-26, part E). A load from the register block (0xFFFFF800 and up) or from the table window (0xFFFF2000–0xFFFF35FF) takes **1 or 2 cycles**, at the implementation's discretion, and may differ between registers. It is **fixed per register, every peripheral window word included, for a given implementation: the same on every access and every tile, never data- or traffic-dependent**, so the identical code of a synchronised start (6.3) stays identical in time. Software must not depend on the value: a load returns when its data is ready. Loads from data memory, program memory and the boot ROM are single-cycle. meshsim models 2 cycles for every register and table-window load, the worst case allowed, so its measured timings are upper bounds; 2 is its reference setting (section 10). *Rationale:* the register block's read mux spans units across the tile (router, DMA engine, read port, request port), and each peripheral's window is in its own unit, and the tables are shared with the DMA engine and the address generator, so an implementation may give such a load a second cycle rather than hold it to the core's single-cycle path; the hot paths are store-dominated, so either choice costs little.
- **The sampling rule.** A register or table-window load issued at cycle *t* with latency *L* **returns the state at the end of cycle *t* + *L* − 2** (its sample point) and **takes any side effect at the end of *t* + *L* − 1**, removing exactly what it returned: the controller's `EVENTS` clears the masked events it returned; a peripheral's `EVENTS` subtracts one count from each masked bit it returned set (4.17.3); `REQUEST_RECEIVE` and the UART's RX word pop the entry they returned. This holds for every word of every window (owner, 2026-10-28). Anything set after the sample point survives. With *L* = 1: the state at the end of *t* − 1, the effect at the end of *t*.
- **Stores** to the register block, the peripheral windows and the table window are **single-cycle and posted:** the core doesn't wait, and a store takes effect at the end of its cycle. **Register and table accesses complete in program order,** so a load sees every earlier store. The only stores that stall are a send store on the send port's occupied holding register (`ROUTER_SEND_32`, `_64` or `_128`, below), a request store on a full request FIFO (`REQUEST_SEND`, 4.18), and a store to the UART's TX word on a full TX FIFO (4.20, in C&C 1's peripheral).

**Memories (strict Harvard)** (owner, 2026-10-28; 2.2):

| address | memory | port A | port B |
|---|---|---|---|
| 0x00000000–0x00007FFF | **data memory, 32 KB**; the command block in its top 16 words, 0x00007FC0–0x00007FFF, and the reply block below it, 0x00007BC0–0x00007FBF (4.12) *(Proposed: the blocks at the top of the 32 KB memory, as in v1.1's 8 KB one)*; word 0 reserved (2.2) | controller loads and stores | the traffic director (mode 0) |
| 0x01000000–0x01007FFF | **program memory, 32 KB** | instruction fetch and **loads** (T); **a store is a bus error** | the traffic director (mode 0) |
| 0x02000000– | **boot ROM** (6.2) | instruction fetch and loads | none |
| 0xFFFF2000–0xFFFF35FF | table window (4.10, 8.9) *(Proposed)* | | |
| 0xFFFFF800–0xFFFFFFFF | the controller's register block and the peripherals' windows (8.1) | | |

- **Banks.** Each memory is **four word-interleaved banks**: word *w* is in bank *w* mod 4 *(Proposed: four 2K × 32-bit banks, 8 KB each; the macros are the core's implementer's choice)*.
- **Port B** writes 32, 64 or 128 bits per cycle at a naturally aligned address (a mode-0 packet), so the words of a 128-bit write fall in four different banks.
- **Port A** touches one bank per access.
- **Bus errors.** `ERRORS` [9] is set, and the controller traps, on: a store to program memory or the boot ROM; a misaligned access; an access to an unmapped address (regions 0x03–0xFE, the unused parts of region 0xFF, and **any word of an absent peripheral's window**, 4.17); a sub-word access to the registers, a window or the table window; a store to a read-only register; and a load from `PTR`, the `STREAM` arrays or `GROUP` (8.1, 8.9). Write-only registers read 0, except the request port's, whose addresses a load reads as the receive side (8.10). Reserved words in the register block and the windows read 0 and ignore stores (meshsim flags the access). **An offset past the end of data or program memory is not checked** (2.2): the high bits are ignored and the access aliases (meshsim errors).
- Constants (`.rodata`) and initialised data (`.data`) live in data memory; program memory is readable, so they may live there instead *(a software choice)*. Every instruction is fetched as stored, so code written into program memory through port B runs the next time it is fetched.

**Controller-memory addresses** are byte addresses (2.2): a mode-0 packet to region 0x00 or 0x01 writes at its offset, naturally aligned, and port B decodes the region. v1.1's packed 14-bit word addresses are gone. A mode-0 packet into the command or reply block outside their conventions (4.12) is a programming error (invariant 11, meshsim).

**No big-SRAM port.** The controller has no memory port onto big SRAM.
- **Copies** from big SRAM into program or data memory are mode-0 address-generator runs to the tile itself (4.2; sequence in 6.5).
- **Writes** to big SRAM are packets the controller sends itself: mode 1, landing at `PTR[me]`, or mode 0, landing at its address in region 0x03 (4.9).
- **Reads** of big SRAM go through the controller read port (4.3): queued by stores, with results returning as mode-0 packets to the tile itself.

**Send port.** Seven registers (8.4) fill a one-packet **holding register** in front of the injection point (4.5).
- **`ROUTER_ADDR`** (R/W; owner, 2026-10-28): the packet's full 32-bit address (2.2). A store sets it; a load returns the address the next packet will use.
- **`ROUTER_HDR`** (R/W; owner, 2026-10-28): **no-increment** [0], spare [16:1], size [18:17] reserved (the send register chooses the size; stores ignore it, loads return 0), **counter** [21:19], **mode** [24:22], **dst** [31:25] (absolute: 0–66, 126 = broadcast), in the common header layout (8.1). The router adds src, making the 256-bit link word.
- **`ROUTER_SET_W0`, `ROUTER_SET_W1`, `ROUTER_SET_W2`** (write-only) hold the payload's words 0, 1 and 2: bits [31:0], [63:32] and [95:64].
- **`ROUTER_SEND_32`, `ROUTER_SEND_64`, `ROUTER_SEND_128`** (write-only) each supply the packet's **top word** and send a packet of that size: `ROUTER_SEND_32` supplies bits [31:0], `ROUTER_SEND_64` bits [63:32] (below them `ROUTER_SET_W0`), `ROUTER_SEND_128` bits [127:96] (below them `ROUTER_SET_W0`–`W2`). Software therefore stores a packet's words in ascending order, and the last store sends. The store captures {`ROUTER_HDR`, `ROUTER_ADDR`, the size, the `W` words below its own word, its own data} into the holding register; `W` words the size doesn't cover are not used. The send port cannot send size 11.
- **At that capture,** `ROUTER_ADDR` advances by the size in bytes (4, 8 or 16) for mode 0, **unless no-increment is set**, when it stays put (owner, 2026-10-28); modes 1–4, which ignore the address, leave it unchanged *(Proposed: as v1.1, which advanced only modes that use the address)*. `ROUTER_SET_W0`–`W2` all clear to 0. Both may therefore change while the packet waits. A run of consecutive words costs one `ROUTER_HDR` and one `ROUTER_ADDR` store, then one store per 32 bits of each packet, in ascending word order: `ROUTER_SEND_32` alone, `ROUTER_SET_W0` + `ROUTER_SEND_64`, or `ROUTER_SET_W0`, `W1`, `W2` + `ROUTER_SEND_128`.
- **No-increment** is for writes that go to the same address in many receivers: a reply always lands at `REPLY` + 8 × the sender's id in every receiver (4.12), so a program sets `ROUTER_ADDR` once and changes only `ROUTER_HDR`'s dst per send, keeping an all-to-all of replies at one header store and one send per reply. A change of dst alone is one store; a change of both is two. Commands write slot 0 last, below their arguments, so they store `ROUTER_ADDR` before it.
- **Timing.** A send store at *t* loads the holding register at the end of *t*; its earliest injection is *t* + 1. `send_busy` = the holding register is full. A packet for the tile itself or a broadcast also waits there for a free token (4.5), so it can keep the register full while the loopback is busy. **The store stalls only if the holding register is full and not being injected that cycle**, which gives one packet per cycle when no read return is waiting (gather value, address generator or read port, 4.5). This is a stall rule, not an overflow rule, and deliberately differs from the FIFOs' "count before the pop" rule (4.13).
- **The send port checks nothing:** reserved modes and counter codes, mode 3 at 128 bits, a misaligned or unreachable address, mode 4 to another unit and dst 67–125 go out as written, and the receiver or the simulator catches them.

**The boot ROM** replaces v1.1's resident loader: every controller has a small fetch-and-load ROM in region 0x02, and its reset vector is 0x02000000 (6.2).

### 4.12 The command and reply blocks, the write counters and the events

*Figures 2.1, 2.3 (at v1.1).*

**The command block** (owner, 2026-10-26; mode 0, owner, 2026-10-28).
- **Layout:** **16 × 32-bit slots at the top of data memory,** 0x00007FC0–0x00007FFF *(Proposed, 4.11)*; slot *s* is the word at 0x00007FC0 + 4*s*. **Slot 0 is the command word; slots 1–15 are its arguments,** written first, in the alternating argument blocks of 6.3.
- **Commands are mode-0 writes, counter 0,** to the command block in region 0x00: arguments first, then the command word to slot 0. They stay uncounted because they arrive while the unit waits in `wait_for_command()` with its counters at 0. Every unit's command block is written this way, the boot ROM's first command included (6.2).
- **"Command landed"** is set when a write that comes in through the traffic director and covers slot 0 lands in port B (4.8), and posts the command-landed event, `EVENTS` [6] (below). The controller's own store to slot 0 doesn't set it. No other slot affects control.
- **Conventions** (meshsim; invariant 11): writes into the command block come only from C&C 1–3, uncounted. The hardware doesn't check who writes the block.

**The reply block** (owner, 2026-10-26; mode 0, owner, 2026-10-28).
- **Layout:** **256 × 32-bit words directly below the command block,** 0x00007BC0–0x00007FBF *(Proposed, 4.11)*: two words for each of the 128 sources, so the pair for source *s* is at `REPLY` + 8*s*.
- **A reply is a mode-0 write of 32 or 64 bits to the receiver's reply block at the sender's own pair**, counted on the counter the receiver armed for it. A **32-bit reply writes word 0** of the pair and leaves word 1 alone; **a 64-bit reply writes both.** Because the address is the same in every receiver, a sender sets `ROUTER_ADDR` once, with no-increment, and changes only the dst per reply (4.11).
- Replies carry one-word results between tiles (cuts, counts, maxima, probe keys), and a request's value answers (6.7). Since every source has its own pair, **replies from different sources never collide**; two replies from one source to one tile overwrite each other, so a sender has at most one reply outstanding to each receiver until the receiver has read it (software's part). Which counter a reply uses is agreed by the two programs.
- **That a sender writes only its own pair is a software convention** (owner, 2026-10-28), no longer a hardware guarantee: meshsim checks that writes into the reply block are 32 or 64 bits at the sender's own pair (invariant 11).

**Below them, the stack** grows down from 0x00007BC0 (software convention, `sw/CONVENTIONS.md`).

**Four write counters** (owner and Tony, 2026-10-28; four since 2026-10-28), **`WRITE_COUNTER_1`–`WRITE_COUNTER_4`** (8.7). Every counted packet names one in its counter field (3.1), and **the traffic director decrements it K cycles after accepting the packet** (4.8). Every counted packet is a write (modes 0–3), hence the name.
- **Each packet counts 1,** whatever its size: a 128-bit packet writes four words and counts 1.
- **Width: 20 bits each.** A gather of 64K values at *k* = 2 counts 131,072 packets, and *k* = 8 at the largest X needs 20 bits.
- **Done means landed:** by the time a packet's decrement happens, its write is in place (K, 4.8), so a counter at 0 means everything it counted is in place.
- **Which counter** is the program's choice, from any source (tiles, the controllers, the memory system). **Usage rule:** one counter per thing a program waits for separately. Adding to a counter means waiting for the union; to learn that batch A has landed while B is outstanding, put them on different counters.
- **A request's answer is counted** against what the requesting tile armed when it sent the request (6.7).
- **Unexpected arrival** (4.13): a decrement of a counter already at 0 leaves it at 0 and sets `ERRORS` [2]. It is detected **at the decrement**, K cycles after acceptance. **Accepted trade-off** (owner, 2026-10-28): a stray packet accepted at count 0 whose decrement lands after the program arms *N* uses up one of the *N*: done fires one word early, and the batch's last real word then raises unexpected arrival. The error is caught, later than in v1.1 and naming the last real word rather than the stray; only a second, cancelling bug (a batch armed one too high) would hide it. **meshsim models the hardware exactly** (its `ERRORS` bit and error line follow the decrement) and **also reports the stray as a diagnostic at acceptance**, naming it precisely.
- **Writes are additive** (owner, 2026-10-26; kept 2026-10-28). A write of *N* to a counter **adds *N* atomically, in step with that cycle's decrement**: `next = count + N − dec`, a decrement in the same cycle is judged against count + *N*, `unexpected = dec & (count + N == 0)`. **A write of *N* > 0 arms the counter:** it clears counters done ([4]) and the counter's own done event ([0]–[3]) and sets their qualifiers, whether the counter was at 0 or not. If the count reaches 0 in the same cycle as an add of *N*, it ends at *N* and done doesn't fire. So whichever comes first, the last word of an earlier batch or the add, **once the add has happened, done means everything added so far has landed**: no stale wake and no early wake. **A write of 0 does nothing.** A sum past 2²⁰ − 1 wraps; that is a programming error (meshsim).
- **A counter is never overwritten.** It returns to 0 only by counting down, or at the reset, which also clears the delay line (6.1).

**Counters done,** `EVENTS` [4] (owner, 2026-10-23).
- **It is set on the rising edge of "every counter is at 0":** the transition that leaves all four write counters at 0, **counted only if some counter was armed since [4] was last set or cleared** (the qualifier). So [4] is set when the last outstanding counter the tile was waiting on reaches 0, and by K its writes are in place. v1.1's `mem_idle` and `cm_idle` no longer take part (owner, 2026-10-28).
- **It is sticky,** like every event: it stays pending until a load of `EVENTS` with [4] in `EVENTS_MASK` consumes it (below). An edge before `WFI` is not lost: if a copy finishes before the core reaches `WFI`, the `WFI` falls straight through. **A wake does not consume it,** so no `WFI` can swallow another's wake.
- **It is cleared,** with its qualifier, by a non-zero write to any counter (so every new wait starts clean), by the `done` pulse, and by a load that consumes it. A set and a clear in the same cycle: the clear wins.
- **Polling and `WFI`, and the end of a phase.** Within a phase a program may poll one counter to 0 and act on it, then `WFI` for the rest. [4] stays pending until a load consumes it or a non-zero counter write clears it, so that `WFI` returns at once if everything has already landed, and waits otherwise. **Rule: a phase that sets counters must end with a `WFI` for counters done before it pulses `done`.** That `WFI` proves the arrivals are in (invariant 4); `done` with a counter still non-zero is done early, `ERRORS` [8] (4.13).
- **A count of 0 never makes an edge.** A phase that expects nothing must not `WFI` for counters done.
- **Counters done waits for every counter.** To sleep on one batch while another is expected, a tile uses that counter's own done event (below).

**Per-counter done events,** `EVENTS` [3:0]: **[0] write counter 1 done, [1] write counter 2 done, [2] write counter 3 done, [3] write counter 4 done.**
- **Each is sticky and set on its own counter's transition to 0, counted only if that counter was armed since the event was last set or cleared:** each has its own qualifier, as [4] has.
- **Each is cleared,** with its qualifier, by a non-zero write to its own counter, by the `done` pulse, and by a load that consumes it; like [4] it cannot be set while the `done` gate is closed.
- **[4] keeps its meaning:** all four counters done. A program waits on [4] for everything, or on one bit for one batch. The replayer's refill wait uses its refill counter's bit alone, so an LPDDR fetch outstanding on another counter doesn't hold it up (4.19.9).

**The events** (`EVENTS`, `EVENTS_MASK`, `EVENTS_REQUIRED` and `EVENTS_PEEK`, 8.10; owner, 2026-10-28). **All wakes are events** (owner, 2026-10-23): `EVENTS` collects every one-bit event a program may sleep on, from the counters, C&C 1, the units and the peripherals, and the core's wakeup input is driven by it and nothing else (4.11). **Errors are not events:** they raise the hard interrupt (4.13).

| bit | event | set by | kind |
|---|---|---|---|
| [0] | **write counter 1 done** | its counter's transition to 0 (above) | sticky |
| [1] | **write counter 2 done** | likewise | sticky |
| [2] | **write counter 3 done** | likewise | sticky |
| [3] | **write counter 4 done** | likewise | sticky |
| [4] | **counters done:** all four at 0, at least one armed since it last fired | the rising edge above | sticky |
| [5] | **GO** | C&C 1's barrier release (6.3); mesh tiles only, since C&C 2 and C&C 3 aren't wired to GO | sticky |
| [6] | **command landed** | a director write covering command slot 0 (above, 4.8) | sticky |
| [7] | **DMA run done** | the rise of `rd_done`: the address generator's run has finished (4.2); cleared by `PULSE_DMA` | sticky |
| [8] | **request FIFO no longer full** | the fall of `request_busy` (4.18) | sticky |
| [9] | **request pending:** a received request is waiting | the request port's receive side, while its queue holds a request (4.18) | live |
| [10] | **peripheral 1:** its `EVENTS & EVENTS_MASK` is non-zero | the peripheral (4.17.3) | live |
| [11] | **peripheral 2** | likewise | live |
| [12] | **peripheral 3** | likewise | live |
| [31:13] | reserved, read 0 | — | — |

- **Sticky events** are set by their cause and stay pending until a load consumes them. Setting one that is already pending leaves it set. [6], [7] and [8] are set on the edge, in the cycle the matching status bit sets or falls; the status bits themselves ("command landed", `rd_done`, `request_busy` in `STATUS`) keep their meanings and are separate state. **`PULSE_DMA` clears [7]** (owner, 2026-10-28), as an arming write clears its counter's done, so [7] always refers to the run most recently started and a stale one can't satisfy the next wait.
- **Live events** are true while their condition holds; they are not stored, so no load of `EVENTS` clears them. They clear only at their source (owner, 2026-10-28): **request pending** stays true while requests are queued, and only a load of `REQUEST_RECEIVE` removes one (4.18), so queued requests never merge into one wake; **a peripheral's bit** stays true while that peripheral's `EVENTS & EVENTS_MASK` is non-zero, and its events are consumed in one place only, by a load of the peripheral's own `EVENTS` (4.17.3).
- **Spurious wakes are allowed but not wanted** (owner, 2026-10-28). After a consuming load of a peripheral's `EVENTS`, the tile's live bit for that peripheral may lag, so a following `WFI` may return with nothing to do. **Software always re-checks after a wake**, as standard RISC-V `WFI` allows. **The hardware aims to make the bit current by the next instruction,** a short path inside one unit, so in practice spurious wakes don't happen; meshsim models the hardware's actual latency.
- **`EVENTS_MASK`** chooses the events that wake `WFI` on their own and the events a load of `EVENTS` consumes. It is read/write and **hardware never writes it**, except its reset to **[5] | [4]**, GO and counters done (6.1). A program stops an event from waking it by clearing that bit, not with a pulse.
- **`EVENTS_REQUIRED`** (R/W, 0 at reset; **the controller's only**: a peripheral's window has none) adds an AND to the mask's OR: a required event wakes `WFI` only when **every** required event is pending. A program needing "peripheral 1 and write counter 2" sets both bits here.
- **The wake term,** which drives the core's wakeup input (4.11):

  `wake = gate ? EVENTS[5] : ((EVENTS & EVENTS_MASK & ~EVENTS_REQUIRED) ≠ 0) OR (EVENTS_REQUIRED ≠ 0 AND (EVENTS & EVENTS_REQUIRED) == EVENTS_REQUIRED)`

  where `gate` is the `done` gate (below). A mask bit that isn't required wakes on its own (the escape hatch: an event the program must not sleep through while it waits for a set); a required bit never wakes alone. With `EVENTS_REQUIRED` = 0 the term is `(EVENTS & EVENTS_MASK) ≠ 0`. So one `WFI` can wait for several events at once, for example the end of a DMA run and a write counter together (4.10).
- **A load of `EVENTS` consumes under the mask:** it returns **every** pending event, whatever the mask, and clears exactly the returned sticky bits that are 1 in `EVENTS_MASK`. `EVENTS_REQUIRED` plays no part in clearing, so required events meant to be consumed are put in the mask too. Software acts only on *value* & *mask*, the events it has just consumed. **An event set after the load's sample point survives** (4.11: with latency *L*, the load samples at the end of *t* + *L* − 2 and clears at the end of *t* + *L* − 1): it is not in the value returned and is not cleared, so the next load sees it.
- **`EVENTS_PEEK`** (R) returns exactly what a load of `EVENTS` would return in the same cycle and **never clears anything**. It is an alias of the same state, with no storage of its own (owner, 2026-10-23). Software reads `EVENTS_PEEK` to look and `EVENTS` to consume; the trap handler reads only `EVENTS_PEEK` (4.13).
- **A wake consumes nothing** (owner, 2026-10-23): hardware never consumes an event on wake, so a program consumes or masks an event before its next `WFI`, or that `WFI` falls straight through. **The `sw/mesh.h` wait helpers follow every `WFI` with a load of `EVENTS`**: `wfi()` consumes what woke it (counters done, at the reset mask), and `wait_for_command()` consumes GO (6.3). The load costs 1 or 2 cycles, fixed for the register on every tile (4.11), and is the same code on every tile, so synchronised starts (6.3) are unaffected.
- **Keep GO in the mask** (owner, 2026-10-23, item 79). The `done` gate lets GO wake the core whatever the mask holds, but only a load with [5] in `EVENTS_MASK` consumes it. So a program keeps [5] in `EVENTS_MASK` whenever it calls `wait_for_command()` (the reset mask has it); otherwise the GO stays pending, and the next `wait_for_command()` falls through before its own GO.
- **The `done` gate** (owner, 2026-10-23): from the `done` pulse until GO, **the wake term is overridden to "GO ([5]) pending"** and nothing else can wake `WFI`, whatever `EVENTS_MASK` and `EVENTS_REQUIRED` hold. Counters done and the per-counter done events cannot be set in that window (an edge then is ignored), and the `done` pulse clears them. So `wait_for_command()` can only ever be woken by GO, whatever a program gets wrong. The gate is a gate, not a mask setting: it writes no register, so after GO the program's own `EVENTS_MASK` is in force with nothing to restore. Any other event set in the window stays pending, and wakes the core after GO if it is still enabled. **The gate controls only `WFI` wakes:** an error raised between `done` and GO still takes the hard interrupt (4.11, 4.13). A write counter still non-zero at `done` is a program error, done early (4.13); an arrival after `done` for a counter already at 0 is unexpected arrival.
- **Timing.** An event is set at the end of the cycle its cause happens; a load sees it, and a pending event releases a waiting `WFI`, in the next cycle. So a GO pulse at cycle *t* sets [5] at the end of *t* and wakes the core at *t* + 1, and counters done set at the end of *t* releases a waiting `WFI` in *t* + 1.
- **At the reset** (6.1): every counter, the delay line, the done events and their qualifiers, the gate, `done`, "command landed" and any pending GO are 0; `EVENTS`' sticky events and `EVENTS_REQUIRED` are 0 and `EVENTS_MASK` is [5] | [4]. Live events follow their sources.

**Uses:**
- a block copy (6.5) or a receiving phase (6.4): arm a counter, start, `WFI` with [4] in the mask (the helper consumes it);
- a search step through the read port: arm the reads' counter with the number of reads, then `WFI` (4.3);
- waiting for the tables: `EVENTS_REQUIRED` = the counter's done and [7] (4.10);
- a read from LPDDR (6.7): arm a counter for the read's packets (the write adds, so other traffic for it may be in flight), send the request, `WFI` for counters done or the counter's own event; reads may be outstanding in any number;
- a request with a value answer (an fd, an error code, 6.7): arm the reply's counter with 1, then `WFI` on its done event or for counters done;
- one batch of arrivals while another is outstanding: its counter's done event ([0]–[3]);
- the end of a run ([7]), room in the request FIFO ([8]), a received request ([9], 4.18), and a peripheral's events ([10]–[12]; the replayer's loop, 4.19.9; the UART, 4.20).

### 4.13 Status and errors; traps

*Figure 2.4 (at v1.1).*

The controller's block starts with three read-only registers (owner, 2026-10-28), the same three words that start every peripheral's window (4.17.4): **`PARAMS`** (8.2), the unit's constants; **`STATUS`** (8.2), its live bits; and **`ERRORS`** (8.3), its errors. v1.1's `TILE_STATUS` is split into `PARAMS` and `STATUS`, and `TILE_CONDITIONS` is `ERRORS`.

**`PARAMS`** (constant, strapped): the peripherals present [2:0] (bit *n* − 1 for peripheral *n*), the endpoint id [26:20] and the big-SRAM size [31:27] (8.2). Each peripheral's type is in its own `PARAMS` (4.17.1).

**`STATUS`** (live, **diagnostic only**: no program needs to poll it, 4.10, 4.12):
- `rd_running`, `rd_done` (4.2);
- GATHER busy (4.4);
- `send_busy` (4.11);
- `done`, "command landed" (6.3);
- `request_busy` (4.18).

v1.1's `periph_running` and `sender_busy` are now each peripheral's `STATUS` (4.17.4); `ts_busy` is gone (4.10); and `mem_idle` and `cm_idle` have left the hardware, kept in meshsim only as diagnostics (owner, 2026-10-28).

**Errors** (`ERRORS`). **An error means the software did something wrong** (or the hardware is faulty): in a correct program on correct hardware, `ERRORS` reads 0 forever. Anything informational belongs in the simulator's reports, not here. Each error is one of two kinds, marked in the table: a **program error** (P), which a program can cause, and a **hardware-fault detector** (H), which no program on a correct design can cause, so it can only mean a fault. Both raise the hard interrupt and the error line alike.

| bit | error | set when |
|---|---|---|
| 0 | router overflow | a link, eject or fifth-port FIFO overflowed (4.7, 4.16) |
| 1 | bad packet or misaligned access | a bad packet arrived and was dropped, uncounted (3.1: a misaligned packet, a region it can't reach, mode 3 at 128 bits; invariant 5); a write through a misaligned `PTR` was dropped, still counted (4.9); or a misaligned read by the address generator, the GATHER engine or the read port was not performed, its packet sent with payload 0 (4.1) |
| 2 | unexpected arrival | a counted packet's decrement found its write counter already at 0 (4.12) |
| 3 | drift | in a group run, the SRAM read queue refused an address-generator request, so the tile fell behind its timetable (4.2): a placement error, too much traffic on one area. Zero-length groups and group starts are timetable, not lateness (4.2) |
| 4 | write-engine input overflow | 4.9 |
| 5 | table request overflow | the `PTR` or `HIST` request FIFO (4.10). **(H)**: every engine issues at most one request per cycle and the FIFO drains one per cycle [meas: meshsim] |
| 6 | GATHER queue overflow | a mode-4 packet arrived with the GATHER engine's 16-entry request queue full (4.4). **(H)**: mode 4 only comes from the tile itself, and both of its sources share a resource with the engine: the address generator yields the SRAM read queue to it, and the send port shares injection with its gather values (4.4) |
| 7 | read-port error | a `SRAM_RD_ADDR_QUEUE` store with no free entry; the read is dropped (4.3). A misaligned read-port read sets [1] instead |
| 8 | done early | `done` was pulsed while a write counter was non-zero: the unit declared itself finished with arrivals outstanding (4.12, 6.3) |
| 9 | bus error | 4.11 |
| 10 | illegal instruction | 4.11 |
| 11 | peripheral 1 | **live:** 1 while peripheral 1's `ERRORS` is non-zero (4.17.6). The replayer's: 4.19.11 |
| 12 | peripheral 2 | likewise, live |
| 13 | peripheral 3 | likewise, live |
| 14 | receive underflow | a load of `REQUEST_RECEIVE` with the receive queue empty: it returned 0 and popped nothing (4.18). On a unit that is no target's receiver, every such load |

Bits 15–31 are reserved and read 0. The bits are grouped by unit: the network (0, 1), the arrivals (2), the address generator (3), the write engine (4), the tables (5), the GATHER engine (6), the read port (7), control (8), the controller (9, 10), the peripherals (11–13) and the request port (14). The peripheral bits take [13:11] so every other error keeps its v1.1 number. **Kinds:** 5 and 6 are hardware-fault detectors (H); the rest are program errors (P): 0, 1, 2, 3, 4, 7, 8 and 14 always; 9 and 10 also on a fault (a corrupted image). 11–13 take the kinds of the peripheral's own errors (4.17.6).

- **Sticky until the reset** (owner, 2026-10-28). Bits 0–10 and 14 stay set until the reset (6.1); nothing else clears them, so a program cannot hide one. Bits 11–13 are **live**: they follow the peripheral's `ERRORS`, whose own bits are sticky until the reset (one exception: the unit-error bit of C&C 1's peripheral, which is live, 6.6). A store to `ERRORS` is a bus error ([9]).
- **Any non-zero `ERRORS` raises the unit's hard interrupt** (4.11) and **its error line** into C&C 1's peripheral (2, 6.1). The line is driven by hardware from `ERRORS`, with no software involved and a fixed latency, `error_latency` (section 10), so it still reports a unit whose request path is stuck or whose handler has itself failed.
- **What earns an error:** an error exists for a mistake that would otherwise **go unnoticed or surface far from its cause**: silent corruption or lost data (done early, unexpected arrival, every overflow, drift, bad packet or misaligned access). A mistake that **stops the machine** (a unit that hangs) needs no error: C&C 1's READY bits and its watchdog catch it (6.1, 6.6). A misuse the hardware **ignores**, leading to such a hang (a start while its engine is running, a pulse while a peripheral is busy), is flagged by meshsim only.
- **Same-cycle rule and overflows:** an error raised in the same cycle as the reset is lost (the reset wins). **On every overflow the arriving item is dropped**: the packet, write, request or gather request that found its FIFO or queue full is lost, and only the error records it (errors 0, 4, 5, 6 and 7). **"Full" is the count before the cycle's pop:** a push into a full FIFO overflows even if an entry leaves in the same cycle (no bypass from pop to push), so no FIFO ever holds more than its depth.

**Traps are terminal** (owner and Tony, 2026-10-28).
- **An error raises the hard interrupt,** and a bus error, an illegal instruction, `ecall`, `ebreak`, a fetch outside program memory and the boot ROM, or a misaligned jump target traps synchronously (4.11). **Either way the trap is terminal:** the state is assumed bad, **the handler never executes `mret`**, and the interrupted code is never resumed. Only the reset restarts the controller (6.1). This holds for every unit, C&C 1 included (6.6).
- **No retrap.** The interrupt stays high until the reset (`ERRORS` is sticky), but `mstatus.MIE` is cleared on trap entry and never set again, so it cannot trap a second time.
- **A second trap inside the handler** (a synchronous exception) restarts the handler at `mtvec`; it is still terminal.
- **The unit's engines and peripherals carry on** with whatever they were doing, unserviced, until the reset: nothing stops them before it (owner, 2026-10-28).
- **Traps that set no `ERRORS` bit are software bugs, caught by meshsim** (owner, 2026-10-29): `ecall`, `ebreak`, the fetch, the misaligned jump and a trap inside the handler. **meshsim stops loudly on each.** The hardware adds nothing for them: such a trap parks the unit without raising its error line, and on hardware only C&C 1's watchdog notices (6.1). Accepted.

**The handler** (owner, 2026-10-28; conventions for every unit's handler):
- **It reports** the unit's `PARAMS`, `STATUS`, `ERRORS`, `EVENTS_PEEK`, the four write counters, `mepc`, `mcause` and `mtval`, x1–x31, and for each present peripheral its window's standard words 0–9 (4.17.4): about 50 words, plus ten per peripheral. It sends them to C&C 1 as **request-bus reports** (target 0, 3.3), each a header word (op, sequence number) and three data words: about 17 reports for a unit with no peripheral.
- **The report set is fixed:** every handler sends the same reports, in the same order, so the count is a constant of the protocol and nothing in the reports needs to carry it. C&C 1 counts each unit's reports against it (6.1). *(Proposed: the ops, the order and the count are written with the handler and `mesh.h`, item 122.)*
- **It checks `PARAMS` [2:0] before reading a peripheral's window:** reading an absent peripheral's window is a bus error, which would trap inside the handler.
- **It reads `EVENTS_PEEK`, never `EVENTS`,** at both levels: a load of `EVENTS` consumes events and would destroy the state it is reporting.
- **It assumes no interface is clean:** a register sequence the main code left half done (the send port's `W` words, the request port's `SET`/`GET` words) is never completed, since nothing resumes, so the handler writes every word of each request it sends.
- **It parks** in a plain jump to itself *(Proposed)*. A `WFI` loop would also work only if `mie`'s enable is cleared first, since a pending enabled interrupt may release `WFI` with `MIE` = 0; the plain loop doesn't depend on that.
- Until a loaded image installs its own, `mtvec` points at the **boot ROM's report-and-park handler**, which follows the same conventions and never writes data memory (6.2).

### 4.14 Commit ordering

There is no coherence (4.1), so "committed" matters only at phase boundaries. **A unit knows a write has landed when its counter has decremented,** K cycles after the traffic director accepted it (4.8), by which time:
- **controller memory:** port B has written it, and a load reads it;
- **a peripheral's region:** it has landed (4.17.8);
- **big SRAM:** it has entered its area's request queue, ahead of any read issued later (4.1);
- **the tables:** its `PTR` or `HIST` update is readable through the window (guarantee A, 4.10).

So a counter's done event, and counters done (`EVENTS` [4], 4.12), are set exactly when the writes they counted are readable. **A memory-system read** (6.7) is counted like any other data, so the same events prove it has landed.

**Uncounted writes** are commands, and boot and staging loads (3.1). A command signals its own landing ("command landed", 4.12); boot and staging images into controller memory are followed by the command, which lands last from the same source through port B (6.2); big-SRAM data after boot is sent counted (6.2).

There is no flush.

### 4.15 Digits (shared definition)

The bucket write (mode 2) and count-on-arrival select a **digit** of the 32-bit key in the payload's bits 31–0, with a 3-bit selector in `DMA_CONFIG` (8.5):

| selector | meaning |
|---|---|
| 0 | none (count-on-arrival off; mode 2 not used) |
| 1–4 | byte *d* of the key, bits [8*d* − 1 : 8(*d* − 1)]; **1 is the least significant byte** |

There are two selectors: the **bucket digit** (mode 2, `DMA_CONFIG` [20:18]) and the **histogram digit** (count-on-arrival, `DMA_CONFIG` [23:21]).

### 4.16 The fifth router port

*Figure 4.1 (at v1.1).*

**Every tile's router has a fifth input and a fifth output** (owner, 2026-10-26), so all tiles are one design. **A port is connected only where something is attached;** an unconnected port never receives, and routing never chooses it. **Tiles 0, 1 and 2's are connected, to C&C 1, C&C 2 and C&C 3** (endpoints 64, 65 and 66, section 2). The rest of this section describes a connected port at tile *c*, with C&C *c* + 1 (endpoint 64 + *c*) as its endpoint:

- **Input "from the controller":** a sixth route block (4.6). A packet from the controller (src = 64 + *c*) is routed as a packet injected at tile *c*: unicast as from tile *c*, and a broadcast rooted at tile *c*. It does not pass through tile *c*'s injection point (4.5), so it never waits behind tile *c*'s address generator.
- **"From the controller" FIFOs:** each of the tile's four link outputs and its eject output gains a "from the controller" FIFO (five FIFOs, 16 entries), and each of those selectors gains an input. The controller therefore gets a fair round-robin share of every output, even while the tile's address generator injects every cycle.
- **Output "to the controller":** FIFOs from left, right, up and down and from the tile's own injection (five FIFOs, 16 entries), with their own round-robin selector, one packet per cycle to the controller. Every packet for dst 64 + *c* leaves here instead of through the tile's eject path. The output has no back-pressure: the controller takes one packet a cycle.
- **Inside the controller.** The controller is a tile instance with its own router, and this link is its only link to the mesh. *(Deferred, item 118: which port of the controller's router the link enters, routing inside the controllers' routers, and the router ports of their sending peripherals, 4.17.9.)*
- **Cost:** ten extra FIFOs of 16 × 256 bits (~41 Kbit of flops) plus the wider selectors, ≈ 0.04–0.07 mm² [estimate; scaled from v1.1's 169-bit ≈ 0.03–0.05], tiles 0–2 only (section 11). Their overflows are reported like the other router FIFOs.
- **The memory system's traffic** enters at tile 1: it is C&C 2's peripheral (2, 6.7), so its deliveries come in through tile 1's "from the controller" route block, and writes into its tag windows leave through tile 1's "to the controller" output. Several tiles writing into open windows at once fan in there with no flow control; the file system keeps one writer in flight or paces its writers (6.7).
- **Pacing is policy.** Every packet a controller sends during an all-to-all adds load to its tile's links that the schedule's queue bounds don't count (one broadcast per 64 cycles raised tile 0's link queues to 12 of 16 [meas: meshsim]). So the controllers send commands and bulk data between passes, or pace them.

### 4.17 The peripheral slot

*No figures yet; the figure set gains a peripheral-slot part (handoff in `CHANGES-v1.1.md`).*

Every tile has **one peripheral slot**: a place for one unit that works beside the controller on a job of its own. The replayer (4.19) is today's only peripheral; the tracer, Ingress/Egress and the Updater/Binder/RW engine are expected later. **Every peripheral connects to its tile's controller through the same small interface** (owner, 2026-10-26), so the replayer is no longer a special case and the tile's control logic is one design whatever its slot holds:

| part | where | role | section |
|---|---|---|---|
| type | `TILE_STATUS` [15:12] | which peripheral the slot holds; 0 none | 4.17.1 |
| run | `TILE_PULSE` [2], `PERIPH_RUN` | starts the peripheral's work | 4.17.2 |
| running | `TILE_STATUS` [3], `periph_running` | the peripheral's ownership state | 4.17.2 |
| service lines | `WAKE_EVENTS` [18:13], `PERIPH_SERVICE_1`–`6` | the peripheral's calls for service | 4.17.3 |
| status registers | `PERIPH_STATUS_0`–`7` (0x870–0x88C) | read-only state | 4.17.4 |
| peripheral write | mode 9, counted in `DMA_PERIPH_REMAINING` (0x83C) | data into the peripheral's own address space | 4.17.5 |
| conditions | `TILE_CONDITIONS` [13:11] | [11] bad peripheral write; [12] and [13] the peripheral's | 4.17.6 |
| output | the sender: `SENDER_SEND`, `SENDER_ADDR` (0x868, 0x86C), `sender_busy` (`TILE_STATUS` [4]), sender idle (`WAKE_EVENTS` [20]) | bulk output, for a peripheral that has it | 4.17.9 |

**What each peripheral's section defines:** what its run does and when it ends; what each service line means, its kind and the unit a claim takes; which status registers it uses and what they hold; its mode-9 address map, the sizes each region takes and its ownership rules; conditions 12 and 13; and its output queues, if it has any. The replayer's section (4.19) is the first; it keeps v1.1's behaviour bit for bit under the new names.

#### 4.17.1 The slot and its type

- **One slot, one peripheral.** A tile's slot holds at most one peripheral, fixed when the chip is built.
- **The type** is `TILE_STATUS` [15:12] (8.2), a constant of the tile: **0 none, 1 the replayer** (4.19), 2–15 reserved for future peripherals. Every mesh tile (0–63) holds a replayer, type 1 (confirmed, owner, 2026-10-27).
- **A tile with no peripheral** reads type 0 and never raises a line (4.17.7).

#### 4.17.2 Run and `periph_running`

- **`TILE_PULSE` [2], `PERIPH_RUN`** (8.6), starts the peripheral's work, as its section defines: for the replayer, one pass (4.19.11). **Run only, no resume,** as for every engine: a run while `periph_running` is 1 is ignored with no condition, and meshsim flags it (4.13's principle). In one store, start and run act before `done`.
- **`periph_running`** (`TILE_STATUS` [3]) is the peripheral's **ownership state**: 1 from the end of the run store's cycle until the peripheral's work ends or a halt stops it. While it is 1 the peripheral owns whatever its section says (the replayer: its single-ported memories, 4.19.10), and mode-9 writes the ownership forbids are bad peripheral writes (4.17.6). It reads 0 while the peripheral is idle, so a program that never runs it sees v1.0's behaviour.
- **The end of the work** is reported on a service line if the peripheral defines one (the replayer: line 5, done).

#### 4.17.3 The service lines

**Six lines** (owner, 2026-10-26; five until part D). `PERIPH_SERVICE_1`–`6` are six standard `WAKE_EVENTS` bits, **[13]–[18]** (4.12, 8.10). What each line means, and its kind, the peripheral's section defines. The controller chooses what to sleep on with `WAKE_MASK` and `WAKE_REQUIRED`, as for any event.

**Consuming a line claims one unit of service** (owner, 2026-10-26). A load of `WAKE_EVENTS` that returns a line under `WAKE_MASK` consumes it, as for every event (4.12); for a service line that load is **the controller's commitment to handle one unit of service**, and the peripheral responds by giving that unit up. A line is one of three kinds, by the peripheral's definition:

| kind | while it is up | a claim |
|---|---|---|
| **event** (sticky) | the peripheral has posted it | clears it in the peripheral (the replayer's new expansion and done) |
| **queue condition** (live) | a condition on the peripheral's state holds | gives one unit, for example half a queue credited, which may end the condition (the replayer's low-water lines, 4.19.8) |
| **item FIFO** (live) | the peripheral holds items for the controller | pops one item into the line's status register (below); the line stays up while more remain |

- **A load with the line's mask bit clear claims nothing,** and neither does a load of `WAKE_PEEK`, which is how software looks at the lines without claiming anything (4.12, 8.10). A line the peripheral raises in the same cycle as a load survives to the next load, as every event does.
- **One presented line** (owner, 2026-10-26). At most one of a peripheral's lines **presents an item**, and it presents it in **one designated status register** (4.17.4), so a single load of `WAKE_EVENTS` can never leave a status register describing two claims. A presented item is in its register from the claim at the latest until the next claim of that line; a peripheral may show it earlier (the replayer sets it with the event, 4.19.9).
- **Lines are 0 while the peripheral is held in reset** (4.17.7), and its sticky events are cleared there.

#### 4.17.4 The status registers

`PERIPH_STATUS_0`–`PERIPH_STATUS_7` (0x870–0x88C, 8.11) are eight **read-only** registers whose meanings each peripheral defines.
- **Reads never have side effects:** a load returns the peripheral's state at its sample point (4.11) and changes nothing. Claims are made only through `WAKE_EVENTS` (4.17.3).
- A register the peripheral doesn't define reads 0; on a tile with no peripheral all eight read 0. A store is a bus error.
- **The replayer** uses two: `PERIPH_STATUS_0`, rows run [15:0] and next offset [31:16] (v1.1's `REPLAYER_STATUS`), and `PERIPH_STATUS_1`, the instruction awaiting a controller-fed expansion, the designated register of its presented line, new expansion (v1.1's `REPLAYER_OPCODE`) (4.19.9).

#### 4.17.5 Mode 9, the peripheral write

**Mode 9 writes data into the tile's peripheral's own address space** (3.1, 4.8): addr is a 21-bit word address, and the peripheral's section gives the map, the sizes each region takes, and when each may be written (the replayer's, 4.19.10). The traffic director only hands the packet over; the peripheral's write port judges it.

**From any source** (owner, 2026-10-26): the tile itself (its streams and its send port), another tile, or a non-tile endpoint (the memory system's deliveries for a request's destination kind 2, C&C's boot loads). A mode-9 packet from another tile is no longer an error.

**Landing.** The traffic director hands each mode-9 packet to the peripheral's write port, one per cycle, and the write lands a fixed **`rp_latency`** = 2 cycles later, with no queue on the path *(Proposed, item 48: the value)*. **This fixed latency is a requirement on every peripheral** (4.17.8): it is what lets `DMA_PERIPH_REMAINING` count each write when it lands, so a count of 0 proves that every counted mode-9 write has landed, a request's data included (6.7).

**Counting** (owner, 2026-10-26). **`DMA_PERIPH_REMAINING`** (0x83C, 8.11) counts **every mode-9 packet, from any source** (the tile itself, other tiles, C&C, the memory system), **when it lands, or when it is dropped** (a bad peripheral write, 4.17.6, or a drop the peripheral defines, such as the replayer's queue overrun). A count of 0 therefore means every counted write has landed or been refused, and no idle bit is needed. This follows the counting policy (4.12, owner, 2026-10-26, replacing this section's earlier "tile sources only"): nothing is counted while the tile is halted, so C&C's boot loads are uncounted, and any other mode-9 data reaches a running tile only when it has armed the counter. An arrival for the counter at 0 sets unexpected arrival (4.12). A program that expects mode-9 packets from other sources, as well as its own, counts them all.

**One source at a time per order-placed port** (owner, 2026-10-26; invariant 18). Where a peripheral places a port's writes in arrival order (the replayer's queue ports), only one source feeds that port at once: one of the tile's own clients (4.5), or one other tile, or one non-tile endpoint. Packets from two sources would interleave unpredictably.

#### 4.17.6 The conditions

The slot owns three `TILE_CONDITIONS` bits (4.13, 8.3), raise and run, as in v1.0:

| bit | condition | set when | kind |
|---|---|---|---|
| 11 | **bad peripheral write** (generic, owner, 2026-10-26) | a mode-9 packet was dropped by the peripheral's write port: an address its map doesn't have, a size its region doesn't take, or a write its ownership state forbids, as its section lists; and every mode-9 packet on a tile with no peripheral (4.17.7) | P |
| 12 | peripheral condition 2 | defined by the peripheral (the replayer: queue overrun, 4.19.8) | per peripheral |
| 13 | peripheral condition 3 | defined by the peripheral (the replayer: invalid expansion, 4.19.11) | per peripheral |

A peripheral's conditions follow 4.13's principle: each is a program error or a hardware-fault detector, never information. A peripheral that defines no condition 12 or 13 never sets it.

#### 4.17.7 HALT, a halted controller and the empty slot

- **HALT holds the peripheral and the sender in reset** (6.1), and **a halted controller stops them at once** (a bus error, an illegal instruction, `ecall`, `ebreak`): `periph_running` and `sender_busy` drop, the peripheral's service lines and events clear, its own state resets as its section says, and a send in progress is abandoned with no sender-idle event. Memories keep their contents, and in HALT take mode-9 writes as the peripheral's section allows (the replayer: boot loads, 4.19.10). The peripheral stays idle until the next run.
- **A tile with no peripheral (type 0):** `PERIPH_RUN` is ignored (meshsim flags it); `periph_running` is 0; the service lines and `PERIPH_STATUS_0`–`7` read 0; **every mode-9 packet is a bad peripheral write** (condition 11), dropped and counted in `DMA_PERIPH_REMAINING` at once (in RUN); conditions 12 and 13 never set; and the sender has no output (4.17.9).

#### 4.17.8 What every peripheral must do

So that the slot's interface means the same on every tile, a peripheral:
- **lands mode 9 a fixed `rp_latency` after the director,** with no queue on the path (4.17.5);
- **reads idle when idle:** `periph_running` 0, its live lines down, and no condition raised by a program that never runs it;
- **is held in reset by HALT and stopped by a halted controller** (4.17.7);
- **presents at most one line's items,** in one designated status register (4.17.3), and its status registers have no read side effects (4.17.4);
- **sends nothing unsolicited:** a peripheral with output produces it only when its controller has started the transfer (4.17.9);
- **keeps conditions to errors** (4.17.6).

#### 4.17.9 Peripheral output and the sender

**Mesh tiles' peripherals have no output.** The replayer's rows stay inside the tile, for their consumer (4.19.12), and it never injects a packet. Peripherals that send bulk data belong to non-mesh tiles, which connect through an extra router port, as C&C does at tile 0 (4.16) (owner, 2026-10-26); that connection and the peripheral's endpoint id are specified with the endpoint map *(Open, item 82)*. This section specifies the tile's side up to **the sender's output port**.

**The path.** peripheral → its **output queues** (up to 4, numbered 0–3) → the **sender** → the sender's output port. The sender is a separate unit of the tile; it moves one transfer at a time from one output queue, and the peripheral fills its queues only for a transfer its controller has started (no unsolicited traffic).

**Its registers** (8.12):

| register | access | bits | function |
|---|---|---|---|
| `SENDER_SEND` (0x868) | W | dest [6:0] (7-bit endpoint id), mode [10:7], output queue [12:11], word count [28:13] (32-bit words), spare [31:29] | **a store starts one send** (it is the pulse); reads 0 |
| `SENDER_ADDR` (0x86C) | W | [20:0] word address; [31:21] spare | the first packet's addr for modes 3, 6 and 9; sticky, advancing per packet; a configuration register (8.1) |

**A send.** A store to `SENDER_SEND` sends *n* words, the word count, from the named output queue to dest, every packet stamped with the mode:
- **Packets:** ⌊*n*/4⌋ 128-bit packets, then the remainder: a 64-bit packet for 2, a 32-bit packet for 1, and **a 64-bit packet then a 32-bit packet for 3** (owner, 2026-10-26). Words go in queue order, the first in a packet's bits [31:0].
- **addr:** for modes 3, 6 and 9 the packet carries `SENDER_ADDR`, which then advances by the packet's size in words (+4, +2 or +1), so consecutive sends continue where the last stopped; other modes carry addr 0 *(Proposed)*. A mode-3 send that starts at a multiple of 4 keeps every packet aligned (3.1).
- **Pacing:** the sender takes words as the peripheral makes them available, at most one packet per cycle, and waits while its queue is empty.
- **The sender checks nothing,** like the send port (4.11): bad modes, a bad dest and misaligned mode-3 packets go out as written, and the receiver or the simulator catches them.

**One send at a time.** **`sender_busy`** (`TILE_STATUS` [4]) is 1 from the end of the `SENDER_SEND` store's cycle until the send's last packet has left through the output port. **A store to `SENDER_SEND` while `sender_busy` is 1 is ignored**, with no condition, and meshsim flags it (owner, 2026-10-26): a stalling store could deadlock, since the sender may be waiting for data its own controller hasn't yet let the peripheral produce. When `sender_busy` falls the tile posts **sender idle**, `WAKE_EVENTS` [20] (sticky, 4.12). A count of 0 ends at once: `sender_busy` rises and falls, and [20] is posted.

**No output attached.** On a tile whose peripheral has no output (every tile 0–63 today, and every type-0 slot) the registers exist but nothing is attached: **a send there is ignored** (`sender_busy` stays 0 and no event is posted) and meshsim flags it, so it cannot hang silently. So is a send naming an output queue the peripheral doesn't have.

**HALT** holds the sender in reset with the peripheral (4.17.7).

### 4.18 Request port

The tile's end of the request bus (3.3). **It has two directions in every tile,** so every tile is one design (owner, 2026-10-26):
- **the send side:** four write-only registers (8.10) in front of a **2 KB request FIFO**, which drains into the tile's row node;
- **the receive side:** four read-only registers through which a receiver reads the requests addressed to it. **On tiles 0–63 it is connected to nothing:** its queue is always empty and `REQUEST_PENDING` never rises. On the tiles that receive requests (the controllers, later) it is fed by the root's link for their target (3.3); which tiles those are is specified with the endpoint map *(Open, item 82)*.

**Send and receive share offsets:** a store sends, a load receives (8.10).

**Sending a request** mirrors the send port (4.11):
- **`REQUEST_SET_W0`, `REQUEST_SET_W1`, `REQUEST_SET_W2`** (stores) hold the request's words 0, 1 and 2: bits [31:0], [63:32] and [95:64].
- **`REQUEST_SEND`** (a store) supplies bits [127:96] and pushes the request. **Every request is 128 bits** (3.3): software stores its words in ascending order, and the last store sends. v1.1's `REQUEST_SEND_32`, `_64` and `_128` are withdrawn, and with them the request's size.
- **Software writes** the target [9:7] and the request proper [127:10], laid out by the target (3.3), and writes 0 in requestor [6:0], which the tree overwrites.
- **At the push, `REQUEST_SET_W0`–`W2` all clear to 0** *(Proposed)*, as the send port's do, so a stale word never reaches the next request.

**The request FIFO** *(Proposed: its format)*: 2 KB, **128 entries of 128 bits**, one request each. The port drains the head request into its row node one 16-bit beat per cycle, bits [15:0] first (3.3), as fast as the bus takes them, and frees its entry when its last beat has left.
- **The stall rule** *(Proposed)*: a `REQUEST_SEND` store stalls only if the FIFO is full and no entry is freed in that cycle. This is the send port's rule (4.11): a stall, not an overflow. The FIFO never overflows, so it has no condition.
- **`request_busy`** (`TILE_STATUS` [11], 8.2): the FIFO is full. A program that sees it 0 can push one request without stalling. When it falls, the FIFO posts **request FIFO no longer full**, `WAKE_EVENTS` [12] (4.12), so a program can sleep instead of stalling. Heavy use stalls only the tile that sends.
- **No acknowledgement for issuing.** A request's effect shows only through its answer (6.7). The depth is for debug messages, which have no answer and come in bursts.
- **Timing:** a send store at *t* pushes at the end of *t*, and the port sends the request's first beat at *t* + 1 at the earliest; on an idle bus the request is in its target's queue about `reqbus_latency` after the store (3.3).

**Receiving a request** (a tile that is a target's receiver; on tiles 0–63 the queue is always empty):
- **`REQUEST_GET_W0`, `REQUEST_GET_W1`, `REQUEST_GET_W2`** (loads) return bits [31:0], [63:32] and [95:64] of the **oldest** request in the receive queue, without removing it. `REQUEST_GET_W0` carries the requestor [6:0], stamped by the tree, and the target [9:7].
- **`REQUEST_RECEIVE`** (a load) returns bits [127:96] of the oldest request and **pops it.** Software loads a request's words in ascending order, and the last load pops, mirroring the send.
- **An empty queue.** A load of `REQUEST_RECEIVE` with the queue empty returns 0, pops nothing and sets **receive underflow**, `TILE_CONDITIONS` [14] (4.13); on tiles 0–63 every such load does. A load of `REQUEST_GET_W0`–`W2` with the queue empty returns 0 and sets nothing *(Proposed)*.
- **`REQUEST_PENDING`**, `WAKE_EVENTS` [19] (4.12), is a **live** event: true while the receive queue holds a request. A load of `WAKE_EVENTS` that consumes it changes nothing; only `REQUEST_RECEIVE` removes a request, so **queued requests never merge into one wake**: a `WFI` with [19] in the mask falls straight through while any request is waiting. Its `WAKE_MASK` bit resets to 0. On tiles 0–63 it is always 0.
- **The receive queue is in every tile** (owner, 2026-10-26, reversing part A's "no storage in the tile"): **16 entries of 128 bits** (2 Kbit), fed by the root's link for the tile's target, with one credit per entry held at the root (3.3). Every `REQUEST_RECEIVE` pop returns a credit. On tiles 0–63 nothing feeds it.
- **Timing** (4.11's sampling rule): a load returns the queue's head as it stood at its sample point; a `REQUEST_RECEIVE` load pops at the end of its last cycle, so the next load sees the next request, and [19] falls then if it popped the last one.

**HALT** *(Proposed, accepted for the receive queue, owner, 2026-10-26)*: the send side and the tree are not held in reset. Like the routers (6.1) they keep running, so a halted tile's FIFO drains and no request is ever cut short. **HALT keeps the receive queue's contents**, and requests may still arrive while credits last; chip reset empties it and restores the credits. The `W` registers are configuration registers: undefined at chip reset and kept across HALT → RUN (8.1).

**What a program must keep to** (arming before sending, one reply-producing request per responder, no reads across a group run) is in the memory system's contract (6.7).

### 4.19 The replayer (peripheral type 1)

*No figures yet; the figure set gains a replayer part (handoff in `CHANGES-v1.1.md`).*

The **replayer** is a tile unit that regenerates the tile's share of a proof's execution trace, 2¹⁶ rows of Jolt's expanded trace, from the run log the tracer engine writes, and streams the rows to the unit that consumes them (the materializer, outside this specification). It recomputes every row it can with a small ALU and takes from the log only the words it cannot compute. It is **peripheral type 1** in the tile's peripheral slot (4.17) and uses the slot's interface throughout: its pass is a run, its requests are the six service lines, and it reports in two status registers. This section specifies it in full; it follows the owner's design notes *Replayer and mesh v1.1* (updated 2026-10-23) and their shared-resource allocation table.

**Ground truth** (owner, 2026-10-26, parts G and H; item 113). The trace the replayer reproduces is upstream Jolt's: the 80-byte trace row and the sequence of pc and `instr_idx`. "Upstream Jolt" means **the version our baseline is built against: `LayerZero-Research/jolt` at commit `735355e43026e9ef392b36b4ab5ed0f5e680127f`** (vendored by jolt-cpp at `309b4c4`; see jolt-asic-planning `docs/provenance.md`). Later upstream versions, `a16z/jolt` HEAD included, are tracked as future compatibility, not as the authority. **For now jolt-cpp is the gold standard** (owner, 2026-10-26, part H): the replayer, its tests and the reference rows match **jolt-cpp's interpreter rows** (`execute_instruction` and `expand_compressed_chunk`), the path of the tracer port from which this section's file:line references and `spec/replayer-opcodes.md` are derived (owner, 2026-10-27). jolt-cpp's JIT is not a reference; where it disagrees, that is recorded as a finding (item 113). **Upstream at 735355e overrides jolt-cpp only once a difference is confirmed by running both;** then the appendix, and if need be jolt-cpp, is corrected. `spec/replayer-opcodes.md` is normative for the replayer as built. The formats that are ours (the run log and its streams, the LRU coder, the checkpoint record, how template words are delivered) are ours to define, provided the rows that come out match. Every rule with a potential difference from upstream, found so far by reading source and not yet confirmed (item 113 lists them), is tagged *(pending the upstream check, item 113)*.

#### 4.19.1 Overview

**What a pass is.** A run of the replayer (`TILE_PULSE` [2], `PERIPH_RUN`, 4.19.11) loads the tile's checkpoint and runs exactly **65,536 rows**, then stops; that is one **pass**. The replayer runs many passes per proof, once for every sumcheck stage that needs the trace (46 per Akita proof in the repo's kernel scratchpad plan [estimate]), and every pass starts from the same checkpoint, because replay never writes it.

**The parts.**

| part | size | role | section |
|---|---|---|---|
| code queue | 4 KB, 256 × 128 bits | the opcode stream, raw or compressed | 4.19.8 |
| expansion queue | 4 KB, 256 × 128 bits | expansion words the controller supplies (inlines, system instructions) | 4.19.8 |
| read-log queue | 8 KB, 512 × 128 bits | the read log, raw or compressed: LD values, advice, the next `instr_idx` of non-sequential rows | 4.19.8 |
| write-log queue | 8 KB, 512 × 128 bits | the write log, raw or compressed: SD and AMO pre-values | 4.19.8 |
| decompressors | three: code, read log, write log | 16-entry LRU decoding between each stream's queue and the core | 4.19.15 |
| decode table | 4 KB, 2K × 16 bits | instruction → template | 4.19.5 |
| ROM | 8 KB, 512 × 128 bits, dual-ported | the templates: one expansion word per row | 4.19.6 |
| state word, stream word, register image | 16 B + 16 B + 1 KB | the checkpoint, in that order | 4.19.2 |
| working register file | 1 KB, 128 × 64 bits, true dual-port | the registers a pass reads and writes | 4.19.7 |
| ALU, comparator, two adders | — | row results, branch outcomes, memory address, pc + imm | 4.19.7 |
| output queue | 32 rows of 80 B | rows waiting for the consumer | 4.19.12 |

**Its interfaces** are the peripheral slot's (4.17). Every replayer memory and queue is written by **mode 9** (4.19.10). The controller starts a pass with `PERIPH_RUN` (`TILE_PULSE` [2]), follows it with `periph_running` (`TILE_STATUS` [3]), reads `PERIPH_STATUS_0` and `PERIPH_STATUS_1` (8.11), counts its refills in `DMA_PERIPH_REMAINING`, and is woken by its six service lines, `PERIPH_SERVICE_1`–`6` in `WAKE_EVENTS` [18:13] (4.19.9). Its errors are `TILE_CONDITIONS` [13:11]: the slot's bad peripheral write, and its own queue overrun and invalid expansion (4.19.11). It has no output: its rows go to their consumer inside the tile (4.19.12), and it never uses the sender (4.17.9). Under the slot's names it keeps v1.1's behaviour bit for bit, except that v1.1's `replay_wait` is gone: the new-expansion line already says the replayer is waiting.

**Rate.** A row takes a fixed 3 cycles, or 5 for a multiply (4.19.7): about 3.3 cycles per row with about 10 % multiply rows [estimate; to be measured, item 37], so a pass is about 216K cycles plus about 1.3K cycles of controller-fed expansion stalls, about 0.22 ms at 1 GHz with everything local [estimate]. The consuming kernels likely need 3–6 cycles per row anyway [estimate].

**The reference.** The rows must match Jolt's bit for bit. The reference is the jolt-cpp tracer copy on the `tracer-standalone` branch of `jolt-asic-planning` (PR #1, commit `4f36b3d`); citations in this section are to files under its `tracer/src/tracer/`. Three of its functions are normative: `expand_virt_seq` (`isa/virtual.cpp:132`) for what an instruction expands to, `execute_instruction` (`trace_owner.cpp:145`) for what a row computes, and `expand_compressed_chunk` (`trace_owner.cpp:868`) for what a row records. Where this section departs from them (log-fed values, unchecked assertions), it says so.

**Status.** The design notes record most of this design as candidate and some of it as decided (the run log, 2026-10-22 and 2026-10-23; the fixed row schedules, every instruction a template, the queues as true FIFOs and the resident region, all 2026-10-23; the allocation table, confirmed 2026-10-23). This section states the notes' design unmarked, as the design to build; its own additions are marked *(Proposed)* and gathered in section 13 (items 33–59), and so are the details the owner accepted as proposals on 2026-10-23. Facts about Jolt's expansions are cited to the tracer; the template enumeration that would confirm them across real programs is open (item 34).

#### 4.19.2 The run log and the checkpoint

The tracer engine writes, per tile, **three streams** and a checkpoint (owner, 2026-10-26: the run log is split in two). As much of them as a stage's free big SRAM allows stays resident (4.19.13); they reach the replayer through its queues, each stream raw or compressed (4.19.15).

| stream | granularity | contents | per tile |
|---|---|---|---|
| **opcode** | one 32-bit word per native instruction, held while its expansion runs | the RISC-V instruction; a compressed one is stored decompressed with bit 0 cleared as its flag | about 43K words (175 KB) at 1.5 rows per native instruction; 256 KB at most [estimate] |
| **read log** | one 64-bit word per row that needs one from it, in row order | LD values, advice, and the next `instr_idx` of every row that doesn't continue sequentially | about 0.19–0.20 words per row on average [derived from the densities below] |
| **write log** | one 64-bit word per row that needs one from it, in row order | SD and AMO pre-values | about 0.09–0.12 words per row on average [derived] |

Together the two logs are 0.28–0.32 words per row on average and up to 0.833 in the densest windows: about 160 KB on average, 437 KB at most [meas: design notes]. A pass that needs only one log fetches only that one.

Every stored instruction has bit 1 set, so **bits [1:0] = 00 mark a padding word** (below) and bits [1:0] = 10 a compressed instruction.

**Which rows take a log word, and from which log.** At most one per row, in row order; **the row kind decides the log,** and follows from the expansion word, so no extra encoding is needed *(Proposed: the rule)*: a row whose opcode is SD takes the **write log**, every other log row the **read log**.
- **LD:** the value loaded.
- **SD** (including the SD rows of AMO expansions): the memory's pre-value; the post-value is rs2's value. **Write log.**
- **Advice:** `VirtualAdvice` and the DIV/REM advice rows (4.19.7, class L): the advice value.
- **A control transfer:** a row with `next_from_value` set (4.19.3) whose new pc differs from its pc (4.19.7): the next `instr_idx`.

LD, advice and control transfers take the **read log**.

The first three are the rows whose expansion word has `from_log` set (4.19.3).

The measured densities (words per row, LD / SD / advice / non-sequential) are 0.170 / 0.115 / 0.0001 / 0.030 on EVM Zone (total 0.315) and 0.147 / 0.090 / 0.0002 / 0.039 on Cyclone (total 0.277); per 64K-row window the median is 0.29–0.32 and the maximum 0.833 on both, and the worst sliding 512-, 1,024- and 4,096-row spans are all about 0.835, so dense stretches are sustained, not bursts [meas: design notes]. A replay pass reads 9.1–11.1 MB of log per chip on EVM Zone and 4.0–15.4 MB on Cyclone [meas: design notes].

**A requirement on our tracer engine.** The run log differs from jolt-cpp's memory log on purpose. jolt-cpp logs only LD, SD and `VirtualAdvice` (`trace_owner.cpp:822–855`): it computes the DIV/REM advice in its interpreter (`trace_owner.cpp:545–630`) and looks a jump's next index up in a pc-to-index table (`finish_instruction`, `trace_owner.cpp:111–126`). The replayer has no divider and no such table, so **the tracer engine must write the DIV/REM advice and the next `instr_idx` of every control transfer into the run log**, in row order with the other log words (item 33). The validation harness (item 58) proves the format, the tracer engine's extra words and the replayer together.

**The checkpoint.** Per tile, loaded by mode 9 into region 0 (4.19.10), **state first** (owner, 2026-10-26), so a sequential load writes the state, then the streams' descriptor, then the registers:
- the **state word** (128-bit word 0): pc [63:0], `instr_idx` [95:64], next offset [111:96] (the rows of the first instruction's expansion that the previous tile already ran), and the **per-run flags** (owner, 2026-10-26): [112] code stream LRU-compressed, [113] read log LRU-compressed, [114] write log LRU-compressed, [115] **includes store log**; spare [127:116];
- the **stream word** (word 1): the three streams' exact lengths in bits (4.19.15): code [23:0], read log [55:32], write log [87:64]; the rest spare;
- the **register image** (words 2–65): all 128 registers, as 64 words of 128 bits, each a pair, the low register in the low half: image word *i* (128-bit word 2 + *i*) holds register 2*i* in bits [63:0] and 2*i* + 1 in [127:64], so registers 0 and 1 are in the first and 126 and 127 in the last.

**The tracer's state record** (owner, 2026-10-26, part G) is one record per window, 67 × 128-bit words (1,072 bytes): **word 0 is the window's template bitmap** (bit *i* set if the window uses controller-fed template *i*, 4.19.5), and **words 1–66 are the region-0 image** exactly as above (state word, stream word, register image). The replayer never sees word 0: a controller sends the record **from byte 16 onward**, so the rest still streams straight into region 0, which itself is unchanged. A future tracer peripheral's state queue emits records in this same form.

**Includes store log** (state word [115], owner, 2026-10-26). When 1, SD rows pop the write log as above. **When 0, the pass doesn't use stores' pre-values:** SD rows pop nothing and output a pre-value of 0, the write-log queue is never fed, and its low-water line stays quiet for the whole pass (4.19.8). Write-log entry 15 may keep updating on LDs (4.19.15); it is never read. An SD's pre-value never feeds a register, so register state is unaffected. The tracer emits [115] = 1; a controller clears it for a pass whose consumer doesn't need pre-values, and so never fetches the write log.

Sending all 128 registers avoids special-casing a mid-sequence start, at 1 KB per tile. **A tile that starts inside an expansion** has that instruction as its first opcode-stream entry, and the next offset says where in the expansion to begin. jolt-cpp's chunked replay instead rewinds to the start of the expansion when inline scratch registers are live (`trace_owner.cpp:722–739`); the full image makes that unnecessary. **Register 0** of the image is ignored: the working file's register 0 always reads 0 *(Proposed, item 41)*.

**Re-runs.** A run copies the state word into working copies of pc, `instr_idx` and the offset, **latches the state word's four flags and the stream word's three lengths** (owner, 2026-10-26), and copies the image into the working register file (64 cycles, two registers per cycle). Replay never writes region 0, so every pass of the same window starts identically.

**Inline scratch.** Registers 48–127 hold an expansion's scratch values and are 0 between expansions (`isa/virtual.hpp:25–27`). Inline templates end with explicit `ADDI vN, x0, 0` rows that zero each scratch register they used (for example the end of `inlines/blake3.inc`), so the replayer needs no state for them. jolt-cpp also zeroes them defensively at chunk starts (`normalize_inline_vregs`, `trace_owner.cpp:128`); with the full register image the replayer doesn't need to.

**End of trace** *(pending the upstream check, item 113)*. The trace is padded to 2²⁹ rows after the program ends. The program's last real row jumps to pc 0: a JAL with immediate 0 sends pc to 0 (`trace_owner.cpp:281`), and its log word sets `instr_idx` to 0. The tracer writes the padding as real opcode entries: one opcode word 0x00000000 per padding row, with no log words. The padding word maps to the ROM's padding template (4.19.6), an all-zero NOP row with `hold` set, so pc and `instr_idx` stay 0, as in the tracer's padding, where every padded row is zero and decodes as the zero-filled NOP at bytecode index 0 (`trace_owner.hpp:160–163`; real instructions start at index 1, `isa/virtual.cpp:1359`).

#### 4.19.3 The expansion word and the operand codes

Every row is described by one **128-bit expansion word**, in place of the tracer's 48-byte `VirtualInstruction` (`isa/virtual.hpp:89–126`). The same format serves the ROM and the words the controller supplies.

| bits | field | contents |
|---|---|---|
| [7:0] | `opcode` | the row opcode, 0–167, numbered in the tracer's order (`isa/riscv.inc`, enum `OpCode`, `isa/riscv.hpp:17`); 4.19.7 lists the ones that occur in rows |
| [15:8] | `rd` | operand code (below) |
| [23:16] | `rs1` | operand code |
| [31:24] | `rs2` | operand code |
| [43:32] | `seq_remaining` | rows left in the expansion after this one; 0 on its last row. At most 3,120 (`KECCAK256_ABSORB_PERMUTE` expands to 3,121 rows, `isa/virtual.cpp:1310`) |
| [44] | `next_from_value` | this row can change control flow: when it does (4.19.7), the next `instr_idx` comes from the log |
| [45] | `hold` | pc and `instr_idx` don't advance after this row (padding) |
| [47:46] | `rd0` | the rd = x0 policy of the template (4.19.4): 0 none, 1 NOP (the NOP row is virtual *(pending the upstream check, item 113)*), 2 remap, 3 NOP (the NOP row is not virtual: the source is ADDI). Read from the **first word delivered** for an expansion only: ROM[start] on the ROM path, the first word the controller sends on the controller-fed path (4.19.4) |
| [50:48] | `format` | the source instruction's immediate format, for code 132: 0 R, 1 I, 2 S, 3 B, 4 U, 5 J; 6 and 7 reserved |
| [51] | `virt` | the row's opcode differs from the source instruction's *(Proposed, item 35)* |
| [52] | `imm0_pc0` | Jolt's program-end convention: if this row is a JAL whose immediate is 0, its new pc is 0 (4.19.7). **Set in every JAL word, ROM or controller-fed** (owner, 2026-10-26, item 56) |
| [53] | `report_c` | report `is_compressed` on this row when the instruction is compressed (4.19.12) *(Proposed, item 56)* |
| [54] | `from_log` | the row consumes one log word: rd's value, or an SD row's memory pre-value (4.19.7) *(Proposed, item 56)* |
| [55] | `untraced` | used only by a tracer (owner, 2026-10-26): the row is not traced. **An untraced word must never reach a replayer;** one that does is an invalid expansion (4.19.11) |
| [62:56] | `advice_reg` | used only by a tracer: the second-bank register a traced advice row reads. A replayer ignores it |
| [63] | spare | 0 |
| [127:64] | `const` | a constant immediate (code 128) |

**`virt`** *(Proposed, item 35)*. Jolt marks a row virtual when its expansion has more than one row **or its opcode differs from the source instruction's** (`isa/virtual.cpp:183`). The second half is not derivable in hardware, since the replayer never sees the source instruction as a row opcode: SLLI becomes one `VirtualMULI` row and is virtual, while ADD stays one ADD row and is not. The template builder therefore sets `virt` on every word whose opcode isn't the source's, and the replayer derives the rest (below).

`seq_remaining` = 0 marks the end of an expansion; there is no separate end bit. The per-instruction fields (`rd0`, `format`, `next_from_value`) live in the ROM words, not the decode table, because every instruction is a template (4.19.5).

**Operand codes.** Each of `rd`, `rs1` and `rs2` names where the slot's value comes from:

| code | meaning | slot kind |
|---|---|---|
| 0–127 | register: x0–x31, then virtual registers 32–127 | register |
| 128 | `const` (also the zero placeholder, with `const` = 0) | immediate |
| 129, 130, 131 | the source instruction's rd, rs1, rs2 field (bits [11:7], [19:15], [24:20]) | register |
| 132 | the source instruction's immediate, decoded by `format` and sign-extended to 64 bits (`isa/decoder.cpp:74–212`); 0 for format R | immediate |
| 133 | `mask_trailing_zeros(imm & 0x3f)`: ones in bits [63:*s*], *s* = `imm & 0x3f` (SRLI, SRAI; `isa/virtual.cpp:565–573`, `../utils/math.hpp:134`) | immediate |
| 134 | `1 << (imm & 0x3f)` (SLLI → `VirtualMULI`; `isa/virtual.cpp:596–600`) | immediate |
| 135 | `1 << (imm & 0x1f)`, as a 32-bit value (SLLIW → `VirtualMULIW`; `isa/virtual.cpp:616–620`) | immediate |
| 136 | `(1 << 32) − (1 << (imm & 0x1f))`, the 32-bit window bitmask (SRLIW, SRAIW; `isa/virtual.cpp:575–579`, `629–633`) | immediate |
| 137–144 | temporary *k* = code − 137, resolved as register 40 + *k* + offset (4.19.4) | register |
| 145 | **immediate 0**: an immediate slot whose value is 0, independent of `const` and of the source instruction (owner, 2026-10-27, part L) | immediate |
| 146–255 | reserved: an invalid expansion (4.19.11) | — |

In codes 133–136, `imm` is the source instruction's immediate, the same value as code 132; the masks make the shift-immediate formats' funct bits irrelevant (the tracer decodes shift immediates with their own shamt format, `isa/decoder.cpp:98`, and only these codes ever use them). Templates never contain literal registers 40–47; they use the temporary codes.

**Two immediate slots** (part L). A word holds only one constant, so a row with two immediate slots, a non-zero constant and a zero, writes the zero slot as **code 145**, in ROM and controller-fed words alike. Such rows occur in the controller-fed secp256k1 templates (`LUI rd, #0x1000003d1, #0`) and in the ROM, as the first row of the LR/SC and AMO templates (`LUI v40, #0x80000000, #0`): **17 rows in EVM Zone's tables and 28 in Cyclone's** (15 and 26 controller-fed, plus 2 ROM each) [meas: tracer enumeration]. A slot that is 0 in every instance, with no other constant in the row, is written as code 128 with `const` = 0, as the tracer now does. Before part L the zero slot borrowed code 132, which reads 0 only because every inline is R-format; tables built that way still replay correctly, but new tables use 145.

**The slot kind is committed.** A register slot reads its register; an immediate slot carries its value, and Jolt records which slots are immediates (`rd_is_imm`, `rs1_is_imm`, `rs2_is_imm`, `isa/virtual.hpp:100–102`). So code 0 (x0) and code 128 with `const` = 0 differ: a zero placeholder must not replay as x0.

**The row's immediate** is the sum of its immediate slots' values, as Jolt computes it (`trace_owner.cpp:900`); in every template the tracer emits, at most one immediate slot is non-zero (to be confirmed by the enumeration, item 34).

**Derived, not stored:**
- `is_first`: the row is the first of its expansion and the expansion is virtual (`isa/virtual.cpp:186`);
- `is_virtual`: `virt` is set, or the expansion has more than one row (`seq_remaining` > 0, or the row is not the first); for the shared NOP row, `rd0` = 1 (4.19.4);
- `pc_increment`: 0 unless `seq_remaining` = 0, then 2 for a compressed instruction (opcode word bit 0 = 0) and 4 otherwise; only an expansion's last row carries it (`isa/virtual.cpp:1416`);
- `is_compressed`: the instruction is compressed and the word has `report_c` set. Jolt reports it only where `pc_increment` = 2 (`trace_owner.hpp:184`, `trace_owner.cpp:899`), that is on an expansion's last row, so the template builder sets `report_c` on last words only;
- `risc_pc`: the pc register, the same for every row of an expansion (`isa/virtual.cpp:1413–1415`).

**Jolt's conventions are data where they can be** *(Proposed, item 56)*. Each place where the replayer must copy a Jolt convention rather than plain RISC-V is a flag in the expansion word or a code in it, set by whoever builds the tables, so a change in Jolt means new tables rather than new silicon: `virt` (which rows Jolt calls virtual), `rd0` 1 and 3 (whether the rd = x0 NOP row is virtual), `imm0_pc0` (JAL to immediate 0 ends the program), `report_c` (which rows carry `is_compressed`), `from_log` (which rows take a log word), and the advice rows' operands (4.19.7). What stays in logic is listed in 4.19.7 and item 57.

#### 4.19.4 The rd = x0 policy and the temporary offset

Jolt rewrites an instruction whose rd is x0 before expanding it (`isa/virtual.cpp:307–319`):
- with **no side effect beyond writing rd**, it becomes one NOP row, `ADDI x0, x0, 0` (`isa/virtual.cpp:307–311`);
- with **side effects**, rd is remapped to a throwaway virtual register, v40, and the template's own temporaries then start at v41 (`isa/virtual.cpp:315–319`; temporaries are allocated upward from 40, `isa/virtual.cpp:140`, `148–153`);
- **a few instructions handle rd = x0 themselves** and are expanded unchanged (`rd0_is_handled_internally`, `isa/virtual.cpp:57–102`).

The replayer reproduces this with the template's `rd0` policy and a one-bit **offset**.

**The rule** *(pending the upstream check, item 113)*. For one expansion, offset = 1 when the source instruction's rd field (bits [11:7]) is 0 and the template's `rd0` is remap; otherwise 0. Then:
- temporary *k* (codes 137–144) resolves to register 40 + *k* + offset;
- under remap, the source rd (code 129) resolves to register 40.

Under a **NOP** policy with rd = x0, the instruction becomes the one shared NOP row (4.19.6) in place of its template. The tracer marks that row virtual unless the source instruction is ADDI itself (`isa/virtual.cpp:183`), so the policy says which: `rd0` = 1 makes the NOP row virtual (and so `is_first`) *(pending the upstream check, item 113)*, and `rd0` = 3, used only by ADDI's template, makes it not virtual *(Proposed, item 35)*. The row's virtual-ness is data, not an opcode compare.

**Example.** `lb a5, 4(a0)` against `lb x0, 4(a0)` (`expand_load`, `isa/virtual.cpp:237–250`):

| row | normal (offset 0) | remapped (offset 1) |
|---|---|---|
| 1 | `VirtualAlignAddr v41, a0, 4` | `VirtualAlignAddr v42, a0, 4` |
| 2 | `LD v41, v41, 0` | `LD v42, v42, 0` |
| 3 | `VirtualWindowMaskB v40, a0, 4` | `VirtualWindowMaskB v41, a0, 4` |
| 4 | `VirtualPextSigned a5, v41, v40` | `VirtualPextSigned v40, v42, v41` |

**Which instructions remap**: those in `has_side_effects_beyond_rd` and not in `rd0_is_handled_internally` (`isa/virtual.cpp:19–55`, `57–102`):

| instructions | served by | effect |
|---|---|---|
| JAL, JALR, LD, `VirtualHostIO` | one-word ROM templates | rd becomes 40; e.g. `ret` becomes `JALR v40, ra, 0` |
| LB, LBU, LH, LHU, LW, LWU | ROM templates | rd becomes 40, temporaries + 1 |
| the 18 AMOs | ROM templates | rd becomes 40, temporaries + 1; no stored variants |

LR/SC *(pending the upstream check, item 113)*, CSRRW, CSRRS, ECALL, EBREAK, MRET and the inlines are exempt (`rd0` = 0), as is every instruction whose rd field is an immediate (stores, branches, `VirtualAssertEQ`, FENCE; `isa/virtual.cpp:104–106`). JAL, JALR and `VirtualHostIO` also allocate their own throwaway register when rd is x0 (`isa/virtual.cpp:1195–1226`); after the generic remap that case never arises, and both paths give rd = 40, so one-word remap templates reproduce them. With the decode table's AMO index (4.19.5), every atomic reaches its own template and the offset handles the remap in hardware.

**What the remap and the exemptions produce** (owner, 2026-10-26; `spec/replayer-opcodes.md` section 5) *(pending the upstream check, item 113)*:
- **The remapped link is really written:** a JAL or JALR with rd = x0 leaves its link (pc + `pc_increment`) in v40, which is not an inline temporary and keeps that value across instructions (it is below 48, so the tracer never zeroes it); the record shows rd = 40 and the link.
- **The exempt instructions emit rows that name x0:** `LD x0` (LR.D), `VirtualPextSigned x0` (LR.W) and `XORI x0` (SC.W, SC.D). Register 0 drops the write (4.19.7), and an LD to x0 reports a memory value of 0 (4.19.7, 4.19.12).
- **EBREAK's single row is `JAL v40, 0`** (4.19.5): it allocates v40 itself, and ends the program.

**The temporaries stay in 40–47.** The tracer asserts that no expansion, remap included, allocates beyond register 47 (`isa/virtual.cpp:148–153`), so every correct template keeps 40 + *k* + offset ≤ 47. The replayer doesn't check it *(Proposed, item 36)*.

**Mid-sequence start.** The offset is recomputed from the opcode-stream entry's rd field and the `rd0` of the template's **first word**, ROM[start], not ROM[start + offset].

**Every expansion word, ROM or controller-fed** (owner, 2026-10-26, part G, ruling N3). The rd = x0 handling applies to every expansion the same way: the ROM is a cache of templates (4.19.6), so a word means the same whichever path delivers it. Today's controller-fed expansions (the inlines and system instructions) happen to be exempt, so their offset is 0, but that describes the current templates, not a rule. The controller-fed path follows the same rule with two mechanisms:
- **`rd0` is read from the first word delivered** for an expansion (4.19.3): on a mid-sequence start the controller sends words from the next offset, so it **copies the template's `rd0` into the first word it sends**;
- **under a NOP policy with rd = x0,** the controller supplies **the single NOP word** (the shared NOP row, 4.19.6) instead of the template. It knows the instruction word (`PERIPH_STATUS_1`) and the template's policy. **That word carries the template's `rd0` (1 or 3) in bits [47:46]**; otherwise the word is ROM word 1's. So the NOP row's virtual flag comes out as on the ROM path: `rd0` is read from the first word delivered (4.19.3), and the NOP word is the only word delivered (owner, 2026-10-26, part I) *(pending the upstream check, item 113)*.

#### 4.19.5 The decode table and its index

**Every instruction is a template.** A pass-through instruction is a one-word template whose operands are codes 129–132 (copy the source instruction's registers or immediate), so there is one datapath: every row comes from an expansion word.

**The decode table** is 2K × 16 bits (4 KB), single-ported. Its **11-bit index** is formed by a small hardware decoder from the opcode-stream word *w* (bit 0, the compressed flag, plays no part):

| *w*[6:0] | index[10] | index[9:0] |
|---|---|---|
| any other opcode | 0 | {*w*[6:2], *w*[14:12] (funct3), *w*[30], *w*[25]} |
| 0101111 (AMO) | 1 | {00, 00, *w*[12] (W/D), *w*[31:27] (funct5)}: separates LR, SC and every AMO |
| 0001011 (custom-0, the inlines) | 1 | {01, *w*[29:25] (funct7[4:0]), *w*[14:12] (funct3)}: one entry per inline |
| 1110011 (SYSTEM) | 1 | {10, 0000, *sel*}: *sel* = funct3 when funct3 ≠ 0 (the CSR instructions); when funct3 = 0, *sel* = 8 for ECALL (*w* = 0x00000073), 9 for EBREAK (0x00100073), 10 for MRET (0x30200073), 11 otherwise |

*(Proposed, item 38: the special groups' bit positions; the notes fix only which instruction bits each group uses.)* Index[9:8] = 11 with index[10] = 1 is never formed. The general group's entries for the three special opcodes are never read.

The jolt instruction encodings it must separate (`isa/decoder.cpp`): the inlines are custom-0 with funct7 0x00–0x05 and funct3 0–7 (`isa/decoder.cpp:1210–1249`); custom-2 (0x5B) carries `VirtualRev8W` (funct3 0, funct7 0x05), `VirtualAssertEQ` (funct3 1) and `VirtualHostIO` (funct3 2) (`isa/decoder.cpp:1251–1264`), which the general group separates by funct3; MRET is recognised by its exact encoding (`isa/decoder.cpp:603–606`).

**The entry** uses 11 of 16 bits:

| bits | field |
|---|---|
| [1:0] | `class`: 0 ROM template, 1 controller (inline), 2 controller (system), 3 reserved (an invalid expansion) |
| [10:2] | `start`: the template's first ROM word |
| [15:11] | spare |

There is no length field: each ROM word's `seq_remaining` marks the template's end, including on a mid-sequence start (start + offset). **The padding word** (bits [1:0] = 00) is caught before the lookup and goes to the padding template (4.19.6).

**The table builder's rules** (software contract): every entry is written, because the table isn't reset; unused entries are controller class, so an unknown instruction becomes a question for software rather than an error; and an instruction whose index bits include immediate bits (the U, J and most I formats) has its entry **replicated** across every index those bits can form. The table is per program, which also absorbs program-level choices: **the code stream carries whichever instruction word the tracer's bytecode decodes,** and the decode table and the template index are built for that word (owner, 2026-10-27). jolt-cpp builds the bytecode from the raw bytes but rewrites them at decode time (`apply_host_io_encoding`, `host_io_encoding.cpp:79–92`): under the legacy raw-ECALL host-I/O encoding a host-I/O ECALL becomes `VirtualHostIO x0, x0, 0`, and the tracer puts its **canonical word 0x0000205B** in the code stream, not ECALL's 0x00000073. So that word's entry is the custom-2 funct3-2 entry, a one-word `VirtualHostIO` template under the rd = x0 remap (rd becomes v40, 4.19.4) *(pending the upstream check, item 113)*, and ECALL's own entry is unaffected (controller-fed).

#### 4.19.6 The ROM and its contents

**The ROM is 8 KB, 512 × 128-bit words, dual-ported,** loaded by mode 9 (4.19.10); one port serves the replayer's reads and the other mode-9 writes, which it accepts even while a pass runs. It is a RAM; "ROM" says that replay never writes it. **It is a cache of the program's most frequent templates,** filled by the table builder (owner, 2026-10-26): **any instruction may be controller-fed** through its decode class (4.19.5), so what the ROM holds is a choice, not a requirement. Today's fill:

| content | words |
|---|---|
| ROM words 0 and 1: the **padding** row (word 0: opcode NOP, every operand code 0, `seq_remaining` 0, `hold` 1) and the **shared NOP** row (word 1: `ADDI x0, x0, 0`, i.e. opcode ADDI, `rd` 0, `rs1` 0, `rs2` 128 with `const` 0, `report_c` 1, since it is its instruction's only row) *(Proposed, item 39: their fixed places)* | 2 |
| emulation sequences: shifts, sub-word loads and stores, the M extension (`isa/virtual.cpp:321–640`) | about 172 |
| pass-through instructions, one word each | about 45 |
| atomics: LR.D 5, LR.W 9, SC.D 12, SC.W 26, the 18 AMOs 237 (`isa/virtual.cpp:1287–1306`) | 289 |
| **total** | **about 508 of 512** |

The fit is tight; reusing identical words or a slightly larger ROM restores headroom (item 40), and when a program's templates don't fit, the table builder moves the least frequent out (DIV/REM and the atomics first) and the controller serves them. rd = x0 remaps need no variant words: the offset rule applies in hardware. With today's fill, the controller serves only the inlines and the system instructions.

**How a template is read.** A ROM template is consecutive words from `start` (from `start` + offset on a mid-sequence start), each word one row, until the word with `seq_remaining` = 0. A template that would run past word 511 (a word at 511 with `seq_remaining` > 0, or `start` + offset > 511) is an invalid expansion (4.19.11).

**Loading.** The table and ROM load through mode 9, broadcast by C&C at boot (4.19.10).

#### 4.19.7 Executing a row: the register file, the ALU and the schedules

**Per row** the replayer:
1. takes the current native instruction from the code queue and finds its row: the next ROM word of its template, or the next word of the expansion queue for a controller-fed instruction;
2. resolves the row's operand codes (4.19.3, 4.19.4) and reads rs1, rs2 and rd_prev from the register file;
3. computes the result with the ALU, or takes it from the log (class L, below); writes rd; forms the memory address as rs1 + immediate;
4. pushes the row record into the output queue (4.19.12), and advances pc and `instr_idx` (below).

An instruction's opcode word leaves the code queue when its expansion's last row (`seq_remaining` = 0) completes. Fetch and decode for the next row overlap the current row; only execution is strictly sequential *(Proposed, item 42)*.

**The register file** is 128 × 64 bits (1 KB), true dual-port, and holds the working copy of all 128 registers. **Register 0 reads 0 and writes to it are dropped**, as the tracer's `save_rd` drops them (`trace_owner.cpp:85–93`). It is not addressable by mode 9; each start fills it from the image.

**What a row computes is defined in `spec/replayer-opcodes.md`** (owner, 2026-10-26, part F; item 112), which is **normative** for it (its sections 1–5): for each of the 91 row opcodes, its slots, the exact rd formula (widths, extension, wraparound, shift masks), its control transfer, its log word and, for LD and SD, its memory fields, all derived from `execute_instruction` (`trace_owner.cpp:145–669`, tracer commit 4f36b3d) and checked against it with about 240 test vectors. This section keeps the classes, the schedules and the pc and `instr_idx` rules. A row reads each slot's value as `read_reg_vals` does (a register slot reads its register, an immediate slot carries its value, `trace_owner.cpp:78–83`), with four departures from the tracer:
- **class L rows take the log word** (below) instead of reading memory or computing advice;
- **assertions are not evaluated:** the `VirtualAssert…` rows write nothing, and the tracer aborts on a failed assertion while tracing (`release_assert`, e.g. `trace_owner.cpp:334–351`), so a traced program never contains one that fails;
- **`VirtualPext` and `VirtualPextSigned` take only contiguous, non-zero masks** (owner, 2026-10-26, item 43): every mask they receive comes from a `VirtualWindowMask…` row (`expand_load`, `isa/virtual.cpp:237–250`), so the tracer guarantees it. The replayer builds only (rs1 & m) >> tz(m) and its sign-extended form, not the general bit-extract the appendix defines for any mask; **the result for any other mask is undefined**, and meshsim flags one (invariant 16);
- **control flow** follows the pc rules below, with the next `instr_idx` from the log rather than from a pc-to-index table.

**Row classes and their schedules.** Of the tracer's 168 opcodes, 91 occur in Jolt's rows; 74 occur only as source instructions, which their templates replace, and 3 never occur (`VirtualMove` 126, `VirtualAdviceRemU` 161, `VirtualAdviceRemUW` 165) [derived from `isa/virtual.cpp` and `inlines/`; the enumeration, item 34, confirms it]. Codes are the tracer's (`isa/riscv.inc`):

| class | cycles | opcodes (code) |
|---|---|---|
| **L**, log | 3 | LD 54 (rd = the log word), SD 53 (the log word is the memory pre-value; rd is an immediate slot), `VirtualAdvice` 122 (rd = the log word), and the DIV/REM advice opcodes `VirtualAdviceDivU` 160, `VirtualAdviceDiv` 162, `VirtualAdviceRem` 163, `VirtualAdviceDivUW` 164, `VirtualAdviceDivW` 166, `VirtualAdviceRemW` 167, which the templates write as `VirtualAdvice` (below) |
| **M**, multiply | 5 | MUL 43 and `VirtualMULI` 114 (low 64 bits of the product), MULHU 45 (high 64 bits, unsigned), MULW 65 and `VirtualMULIW` 115 (low 32 bits, sign-extended) (`trace_owner.cpp:213–231`) |
| **S**, simple | 3 | every other row opcode: NOP 0, ADD 1, ADDI 2, SUB 3, XOR 4, XORI 5, OR 6, ORI 7, AND 8, ANDI 9, SLT 16, SLTI 17, SLTU 18, SLTIU 19, BEQ 28, BNE 29, BLT 30, BGE 31, BLTU 32, BGEU 33, JAL 34, JALR 35, AUIPC 36, LUI 37, FENCE 52, SUBW 62, ADDW 63, ADDIW 64, and the virtual opcodes 107–113, 116–121, 123–125, 127–159 |
| source only | — | the shifts SLL–SRAI 10–15 and SLLW–SRAIW 56–61, the sub-word loads and stores 20–27 and LWU 55, ECALL–MRET 38–42, MULH 44, MULHSU 46, DIV–REMU 47–50, UNIMP 51, DIVW–REMUW 66–69, LR/SC and the AMOs 70–91, the inlines 92–106 |

*(Proposed, item 37: the class of every opcode; the notes settle only the principle.)* The class L rows are exactly those whose word has `from_log` set (4.19.3): the flag, not the opcode, decides that a row takes a log word. The replayer **executes 85 opcodes**: the S and M classes, LD, SD and `VirtualAdvice`. A word with any other opcode is an invalid expansion *(Proposed, item 36)*.

**Advice rows are data** *(Proposed, item 56)*. Jolt reports every advice row as `VirtualAdvice` with rs1, rs2 and the immediate all 0 (`trace_owner.cpp:912–917`), though the tracer's DIV/REM advice rows name real source registers (`isa/virtual.cpp:331`, for example) so that its interpreter can compute them. The replayer takes the value from the log, so the template builder writes those rows as `VirtualAdvice rd, x0, x0` with `from_log` set: the record then comes out exactly as Jolt reports it, with no masking logic. (The inlines' own advice rows are already `VirtualAdvice` with two zero immediates, `isa/virtual.cpp:216–218`, and are recorded the same way.)

**The multiplier needs no signed high half.** MULH and MULHSU never reach a row: the tracer expands them through MULHU and MUL (`isa/virtual.cpp:485–503`). So the multiplier produces the low product, the unsigned high product and the low 32 bits sign-extended; the sign corrections the notes provide for MULH are not needed *(Proposed, item 37)*. `VirtualAssertMulUNoOverflow` multiplies only to check, so it is class S.

**The fixed row schedules** (decided 2026-10-23). Rows are strictly sequential, never overlapped, so there are no hazards and no bypass. The register file is true dual-port; every read of a row happens before its write, so a row whose rs1 or rs2 is also its rd reads the old value, as the tracer does (`trace_owner.cpp:903–908`).

| row kind | cycles | schedule |
|---|---|---|
| simple (class S) and log rows (class L) | 3 | 1: read rs1 and rs2. 2: compute (or take the log word), read rd_prev. 3: write rd |
| multiply (class M) | 5 | 1: read rs1 and rs2. 2: four 16 × 64 partial products, read rd_prev. 3–4: carry-save and final add into the 128-bit product. 5: write rd (low or high half, or the low word sign-extended) |

The multiplier is four 16 × 64 multipliers in parallel, a full 64 × 64 array, which is cheap in area. There is no divider: Jolt's DIV and REM use advice plus checks, and the advice comes from the log. **Stalls** (an empty code, expansion or log queue, a full output queue) delay a row's start; they never change its schedule.

**The edge cases** (AUIPC's use of the source instruction's pc, JAL and JALR linking pc + `pc_increment`, `VirtualHostIO` writing rd's own value back, `VirtualROTRIW`'s min(32, tz(imm)), the shift masks, MULHU, the advice values) are in `spec/replayer-opcodes.md`. **Extension is per opcode:** several word forms zero-extend rather than sign-extend (`VirtualPow2W`, `VirtualShiftRightBitmaskW`, `VirtualROTRIW` and the `VirtualXORROTW…` forms among them; the appendix's flag F5 lists them), so the appendix's rd formula, not the opcode's name, decides.

**Zero shift masks** (owner, 2026-10-26). The masks reaching `VirtualSRL`, `VirtualSRLI`, `VirtualSRA` and `VirtualSRAI` are **never 0**: each comes from `VirtualShiftRightBitmask` or from the expansion's 2⁶⁴ − 2^shamt, and the tracer guarantees it. The shift is tz(mask), and **the result for a zero mask (tz = 64) is undefined**, as it is in the tracer's C++; meshsim flags one (invariant 16).

**LD's memory fields** (owner, 2026-10-26) *(pending the upstream check, item 113)*. An LD row's record forms its memory value (pre = post) from **the value written to rd**, not from the log word, as the tracer does (`trace_owner.cpp:922–925`). So an LD to x0, which only LR.D with rd = x0 produces (4.19.4), consumes its read-log word but reports a memory value of 0 (4.19.12).

**pc and `instr_idx`.** pc is computed, never stored per row; `instr_idx` increments by 1 on every row that continues sequentially, because the committed bytecode lays each expansion out contiguously in program order (`isa/virtual.cpp:1405–1420`). The **new pc** of a row:

| row | new pc |
|---|---|
| not its expansion's last row | pc (unchanged) |
| last row, no control transfer | pc + `pc_increment` |
| conditional branch | pc + imm if the comparator says taken, else as above |
| JAL | pc + imm; **0 if imm = 0 and `imm0_pc0` is set**: Jolt's program-end convention (`trace_owner.cpp:281`). The template builder sets `imm0_pc0` in **every JAL word, ROM or controller-fed** (owner, 2026-10-26, item 56), so the replayer matches the tracer, which ends the program on every JAL with imm = 0 |
| JALR | (rs1 + imm) & ~1 (`trace_owner.cpp:287`); MRET and traps are JALR rows through mepc (v36) or mtvec (v34) (`isa/virtual.hpp:36–38`; `isa/virtual.cpp:1098–1162`) |
| `hold` set | pc (unchanged), and `instr_idx` unchanged |
| **new pc = 0, from any row** | **the program ends:** pc = 0 and `instr_idx` = 0, checked before the rules below (`trace_owner.cpp:117–119`). Besides a JAL with imm 0, a JALR whose target is 0 or a branch whose pc + imm wraps to 0 also ends it (owner, 2026-10-26, item 57) *(pending the upstream check, item 113)* |

Then, as `finish_instruction` does (`trace_owner.cpp:111–126`): **if the new pc differs from pc,** the row transfers control: pc takes the new pc and the next `instr_idx` is the row's log word (`next_from_value` must be set; a new pc of 0 has the log word 0). **Otherwise** pc += `pc_increment` and `instr_idx` += 1, with no log word, **even for a taken branch or a jump whose target is its own pc** *(pending the upstream check, item 113)* (`trace_owner.cpp:117–124`): Jolt treats a jump to itself as sequential, so the row's pc advances past it.

**Hardwired Jolt behaviour** *(Proposed, item 57)*. Three conventions can't be table data, because they depend on values only known while the row runs; the replayer builds them in, and a change in Jolt would change silicon:
- **the self-jump compare:** a control transfer whose new pc equals its pc is sequential (above) *(pending the upstream check, item 113)*;
- **register 0** reads 0 and drops writes (`trace_owner.cpp:85–93`);
- **the row record's memory fields** are formed for LD and SD only, the normalized ISA's only memory opcodes (`trace_owner.hpp:219–235`), an LD's from the value written to rd (so 0 for an LD to x0), and JALR clears bit 0 of its target (`trace_owner.cpp:287`);
- **a new pc of 0 ends the program** from any row (the pc table above) *(pending the upstream check, item 113)*.
The ALU's per-opcode arithmetic is hardwired too, by nature: a new Jolt opcode needs new logic. Which rows can transfer control is explicit, not inferred from the row opcode: `next_from_value` is set in the ROM for branches, JAL and JALR, and in the controller's words for the JALR rows of MRET, ECALL and trap returns, and for EBREAK's JAL (4.19.5). No pc-to-index table is needed.

**Rate.** About 3.3 cycles per row on average with about 10 % multiply rows (item 37 measures the density), so a 64K-row pass is about 216K cycles plus about 1.3K cycles of controller-fed expansion stalls: about 0.22 ms at 1 GHz with everything local, or about 10 ms for the 46 passes of an Akita proof [estimate].

#### 4.19.8 The queues, their mode-9 ports and credits

Four queues feed the replayer. They are **true FIFOs**: producers push at the tail and the replayer pops, with no slot addresses (decided 2026-10-23). **Every queue holds 128-bit entries** (owner, 2026-10-26): the code and log queues hold their streams as delivered, raw or compressed, and their decompressors unpack them (4.19.15).

| queue | size | entries | half (the refill unit) | normal source |
|---|---|---|---|---|
| code | 4 KB | 256 × 128-bit entries of the code stream | 128 | local (resident or staged, 4.19.13) |
| expansion | 4 KB | 256 × 128-bit expansion words | 128 | always local |
| read log | 8 KB | 512 × 128-bit entries of the read log | 256 | local (resident or staged) |
| write log | 8 KB | 512 × 128-bit entries of the write log | 256 | local (resident or staged) |

All four are dual-ported, so pushes land while the replayer runs. Every refill is local (4.19.13), and a half covers the local refill latency (a few hundred cycles) with room to spare. The two logs are 8 KB each *(Proposed, owner, 2026-10-26; item 103: the tracer to measure the read/write split)*.

**The ports.** In mode 9 an address window names a queue (4.19.10): the packet is appended at the tail and the rest of its address is ignored, so the address generator's per-packet stepping stays inside the port. **Every queue port takes 128-bit packets only,** one entry each (owner, 2026-10-26); a 32- or 64-bit packet is a bad peripheral write. Each stream is padded to a whole entry, and its exact bit length marks its end (4.19.15).

**Credits.** Each queue tracks its **occupancy** (entries landed and not yet popped) and its **pending** count (entries promised but not landed). Its **low-water line** (`PERIPH_SERVICE_1` [13] code, `_2` [14] read log, `_3` [15] write log, `_4` [16] expansion; 4.19.9) is a live condition (for the write log, only in a pass with includes-store-log set, 4.19.2), true while the replayer runs and

  capacity − occupancy − pending ≥ half,

and, for the expansion queue only, while the current controller-fed expansion still has words that nobody has asked for: the newest landed word's `seq_remaining` (the words after it) exceeds pending *(Proposed, item 51: how "still has words to deliver" is judged)*.
- **A read of `WAKE_EVENTS` that returns a low-water line under `WAKE_MASK` claims it (4.17.3) and adds half to that queue's pending,** which drops the condition. Each landed entry moves one entry from pending to occupancy (pending stops at 0). A read with the line's mask bit clear changes nothing, and neither does `WAKE_PEEK` (4.19.9).
- **Up to two refills can be outstanding,** which keeps a draining queue fed; a queue that empties simply stalls the replayer until data lands.
- **The controller sends min(half, entries remaining).** A short final push overstates pending, which only suppresses requests that aren't needed; the count resets at the next expansion or start. It may also deliver a half as several smaller chunks; the queue doesn't care.
- **A new expansion** (4.19.9): claiming the new-expansion line credits the expansion queue's first 128 words (pending = 128), so the controller sends min(128, the expansion's words from the start offset) at once, before any word has landed to judge the low-water condition by.

**Overrun.** A push into a full queue (occupancy at capacity before the cycle's pop, as for every FIFO, 4.13) is dropped and sets **queue overrun** (`TILE_CONDITIONS` [12]).

**Reset: the run clears the queues** (owner, 2026-10-23). A run (a pass's start) empties all four queues: their contents, occupancy and pending. There is **no preloading and no end-of-pass reset**: done leaves the queues as they are, and whatever is in them, or lands in them while the replayer is stopped, is discarded by the next start (4.19.10). The queues are also cleared **in HALT** and **when the controller halts** (4.19.11), and the expansion queue's pending at each new expansion. **Software rule:** before a start, every queue push has landed, whatever its source: counters done for the controller's refill runs and for any other pushes it counts (4.19.13). Over-pushed or late data from one pass can then never leak into the next. After the start the empty queues raise their low-water lines at once, and the controller fills them, a few hundred cycles per pass.

#### 4.19.9 The service lines and the controller loop

**The service lines.** The replayer's six service lines (4.17.3; owner, 2026-10-26) are the slot's `PERIPH_SERVICE_1`–`6` in the shared `WAKE_EVENTS` register (4.12, 8.10), which wakes `WFI` through `WAKE_MASK` and `WAKE_REQUIRED`:

| bit | line | meaning | kind (4.17.3) | a claim takes |
|---|---|---|---|---|
| [13] | `PERIPH_SERVICE_1` | code low-water | queue condition (live, 4.19.8) | half the code queue credited |
| [14] | `PERIPH_SERVICE_2` | read-log low-water | queue condition (live, 4.19.8) | half the read-log queue credited |
| [15] | `PERIPH_SERVICE_3` | write-log low-water | queue condition (live, 4.19.8) | half the write-log queue credited |
| [16] | `PERIPH_SERVICE_4` | expansion low-water | queue condition (live, 4.19.8) | half the expansion queue credited |
| [17] | `PERIPH_SERVICE_5` | new expansion: the replayer has reached an instruction whose decode class is controller (1 or 2), presents it in `PERIPH_STATUS_1` and waits. **Its presented line** (4.17.3) | event (sticky) | the event clears, and the first 128 expansion words are credited |
| [18] | `PERIPH_SERVICE_6` | done: the pass has ended (`periph_running` has fallen, 4.19.11) | event (sticky) | the event clears |

A load of `WAKE_EVENTS` returns every pending event and **consumes** only those under `WAKE_MASK`; consuming a line claims one unit of service, the last column (4.17.3). **A read with a bit's mask clear** changes nothing for that bit, and `WAKE_PEEK` (8.10) changes nothing at all, so credits move only on a read of `WAKE_EVENTS` that returns the event under the mask *(Proposed, item 50)*. To look without consuming, software reads `WAKE_PEEK`. A sticky event the replayer sets in the same cycle as the read survives to the next read. Software acts on value & mask. Nothing but the tile's own controller reads the register.

**What the controller reads with them** (the slot's status registers, 4.17.4; no read has a side effect). `PERIPH_STATUS_1` (8.11) holds the instruction word of the expansion it must supply (the stored opcode-stream word, bit 0 clear for a compressed instruction), from the cycle the new-expansion line is raised until the next one; it was v1.1's `REPLAYER_OPCODE`. `PERIPH_STATUS_0` (8.11), v1.1's `REPLAYER_STATUS`, gives the rows run in this pass and the **next offset**: the row within the current expansion where the next row starts, which is where the controller begins sending the expansion's words (non-zero only for a tile that starts mid-expansion).

**The controller loop.** Every `WFI` has exactly the sources its mask allows. `PERIPH` below is the replayer's lines, [18:13], and `PERIPH_DONE` is [3], `DMA_PERIPH_REMAINING` done (4.12); GO, [8], stays in the mask throughout (4.12) but can't arrive mid-pass, and `WAKE_REQUIRED` is 0.

```
    WAKE_MASK = GO | PERIPH            ; only a service line can come
    TILE_PULSE = PERIPH_RUN            ; a pass; empties the queues: low-water at once
loop:
    WFI                                ; only a service line can wake it
    ev = WAKE_EVENTS & PERIPH          ; claims the lines: credits taken
    if ev.done: exit
    if ev.new_expansion: decode PERIPH_STATUS_1 (inlines and system only),
                         start at PERIPH_STATUS_0.next_offset
    WAKE_MASK = GO | PERIPH_DONE       ; squelch: only the copy's completion can wake it
    write DMA_PERIPH_REMAINING (adds); write STREAM entries; start the run
    WFI; load WAKE_EVENTS              ; consumes PERIPH_DONE
    WAKE_MASK = GO | PERIPH            ; lines raised meanwhile fall through
    goto loop
```

The wake term is `(WAKE_EVENTS & WAKE_MASK) ≠ 0` here, `WAKE_REQUIRED` being 0 (4.12). The squelch is a mask setting: while the run streams, the replayer's lines accumulate, and only `DMA_PERIPH_REMAINING` done ([3]) can wake the core. `DMA_PERIPH_REMAINING` counts the run's mode-9 packets as they land (4.19.10), so the second `WFI` returns when every word is in its queue, and the load after it consumes [3]. Waiting on [3] rather than counters done (owner, 2026-10-26) means an LPDDR fetch outstanding in `DMA_SRAM_REMAINING` (4.19.13) doesn't hold the refill up. Events posted during the run stay pending and, the wake being a level, make the next `WFI` fall through once the mask is restored. GO can't arrive mid-pass, no other mode-9 traffic is armed, and no other tile sends the tile mode 9 during a pass (4.19.13), so `DMA_PERIPH_REMAINING` counts only the loop's own refills.

**Data from LPDDR** (4.19.13) never goes straight into a queue (owner, 2026-10-26): the overflow is fetched into big SRAM and refilled locally like resident data, so every refill is a local run and the loop is the same for all of them.

**Between passes.** The loop exits only at the top, after its last refill run has landed ([3] consumed), so every queue push has landed when the next start empties the queues (4.19.8).

**Short runs, not interruption.** The address generator is not interruptible. Instead every run is short: a 1 KB chunk is 64 packets, about 64 cycles, so nothing waits long for the engine, and the controller re-checks the queues after each chunk. Streaming occupies about 11–20 % of a pass [estimate]. Group runs (the sort's all-to-alls) stay exclusive and don't coincide with replay.

**Controller-fed templates and their numbers** (owner, 2026-10-26, part G; item 114). The template builder numbers the program's controller-fed templates **0–127**. The program's **template set** is the template words plus an **index** giving each number's word offset and length. A controller-fed instruction maps to its number by its **exact instruction word**, since what it expands to can depend on its registers (CSRRW and CSRRS take 1–3 rows by register). **A number is a distinct instruction word, and numbers may share template words:** two numbers whose expansions are identical point at the same words, so the 128 limit counts index entries, not template storage. A program may have **at most 128** numbers. A tile that keeps only the templates its window uses keeps every word that any of its numbers references (item 115).

**EBREAK's expansion** is one controller-fed word (owner, 2026-10-26): **JAL, rd = temporary 0 (v40), immediate 0, with `imm0_pc0` and `next_from_value` set**, as the tracer emits it (`JAL v40, 0`, `isa/virtual.cpp:1164–1192`). EBREAK is exempt from the rd = x0 remap, so the offset is 0 and temporary 0 is v40. It ends the program, which happens once per proof, so it stays controller-fed rather than taking a ROM word *(pending the upstream check, item 113)*.

**Controller-fed expansions** are only the inlines (custom-0) and ECALL, EBREAK, MRET, CSRRW and CSRRS: about 13 per 64K-row window on Cyclone, about 0.4 % of a pass in stalls [meas: design notes]. CSRRW and CSRRS expand to 1–3 rows depending on their registers (`num_cycles`, `isa/virtual.cpp:1324–1345`), which is one reason they stay with the controller. The program's controller-fed template set is resident in big SRAM: for now every tile preloads the whole set (4.19.13; item 115).

#### 4.19.10 Mode 9: the address map, landing and ownership

The slot's rules for mode 9 (4.17.5) apply: any source, landing `rp_latency` after the director, counting in `DMA_PERIPH_REMAINING`. This subsection is the replayer's address space.

**The map.** Everything the replayer reads is written by mode 9, whose addr is a 21-bit word address stepped by 1, 2 or 4 per packet (3.1, 4.2, 4.11). addr[20:18] selects a window *(Proposed, item 44)*:

| addr (32-bit words) | addr[20:18] | region | size | sizes taken | ports |
|---|---|---|---|---|---|
| 0x00000–0x00003 | 0 | **state word:** pc [63:0], `instr_idx` [95:64], next offset [111:96]; flags [112] code compressed, [113] read log compressed, [114] write log compressed, [115] includes store log; [127:116] spare | 16 B | 128 | single |
| 0x00004–0x00007 | 0 | **stream word** (owner, 2026-10-26): code length [23:0], read-log length [55:32], write-log length [87:64], exact, in bits (4.19.15); the rest spare | 16 B | 128 | single |
| 0x00008–0x00107 | 0 | **register image:** 64 × 128-bit words, image word *i* (registers 2*i* low, 2*i* + 1 high) at 0x00008 + 4*i* | 1 KB | 128 | single |
| 0x00400–0x007FF | 0 | decode table: entry *e* in the packet at 0x400 + 4·(*e* >> 3), bits [16·(*e* & 7) + 15 : 16·(*e* & 7)] | 4 KB | 128 | single |
| 0x00800–0x00FFF | 0 | ROM: word *j* at 0x800 + 4*j* | 8 KB | 128 | dual |
| 0x40000–0x7FFFF | 1 | code queue port | 4 KB queue | 128 | dual |
| 0x80000–0x9FFFF | 2 | read-log queue port | 8 KB queue | 128 | dual |
| 0xA0000–0xBFFFF | 2 | write-log queue port | 8 KB queue | 128 | dual |
| 0xC0000–0xFFFFF | 3 | expansion queue port | 4 KB queue | 128 | dual |
| everything else | 0, 4–7 | unmapped | — | — | — |

*(Proposed, items 44–46: the windows, the offsets in window 0 and the sizes each region takes.)* The notes' map is in 128-bit words (packet addr = 4 ×); in those units the state word is 0, the stream word 1, the register image 2–65 (owner, 2026-10-26: state first; it was image 0–63 and state 64), the decode table 256–511 and the ROM 512–1023. A 128-bit write in window 0 must start at a multiple of 4.

**How a controller loads and edits region 0** (owner, 2026-10-26, recommended practice; worked example in `REGISTERS.md` 2.8.2). The controller keeps its window's checkpoint in data memory: the state and stream words (32 bytes), and the register image too if it loads it this way (1,056 bytes in all). With the replayer stopped (`periph_running` = 0): `ROUTER_SET_HDR` once (dst = its own id, mode 9, addr 0); a write of the number of 128-bit words to `DMA_PERIPH_REMAINING`; per word `ROUTER_SET_W0`–`W2` then `ROUTER_SEND_128`, addr advancing by 4 each send (4.11); then wait on `WAKE_EVENTS` [3] and pulse `PERIPH_RUN`. A full checkpoint is 66 words, about 264 stores; a per-pass change such as flipping [115] resends only the state word, about seven stores. Region 0 persists across passes, so the image isn't resent. A controller may instead stream the checkpoint from big SRAM with the address generator, starting at the record's byte 16 (its word 1), since word 0 is the template bitmap (4.19.2). The working register file (1 KB, true dual-port) isn't addressable; each start fills it from the image.

**A bad peripheral write** (`TILE_CONDITIONS` [11], 4.17.6) is, for the replayer, a mode-9 packet that is dropped because it addresses an unmapped word, has a size its region doesn't take, starts a 128-bit write in window 0 off a multiple of 4, writes a single-ported memory while a pass runs, or writes a queue port in HALT (below) *(Proposed, item 47)*.

**Landing.** The traffic director hands each mode-9 packet to the replayer's write port, one per cycle; the write lands a fixed **`rp_latency`** = 2 cycles later, with no queue on the path, as the slot requires (4.17.5) *(Proposed, item 48: the value)*.

**Counting** follows the slot's rule (4.17.5, owner, 2026-10-26): a mode-9 packet **from any source** counts 1 in **`DMA_PERIPH_REMAINING`** (8.11) **when it lands**, or when it is dropped as a bad peripheral write or a queue overrun, so a count of 0 means every counted write has landed or been refused and no idle bit is needed *(Proposed, item 48)*. Nothing counts while the tile is halted (4.12), so C&C's boot loads, made in HALT, are uncounted. A mode-9 packet from another tile is allowed and counted (item 49 is superseded); since a queue port takes one source at a time (invariant 18) and the controller loop counts only its own refills (4.19.9), software keeps other tiles' mode 9 out of a running pass (4.19.13). An arrival for the counter at 0 sets unexpected arrival (4.12).

**Ownership** follows `periph_running` and the RUN/HALT line:

| state | state word, stream word, image, decode table | ROM | queue ports |
|---|---|---|---|
| **stopped** (in RUN, `periph_running` = 0) | written | written | pushed, and discarded by the next run |
| **running** (`periph_running` = 1) | bad peripheral write | written (second port) | pushed |
| **HALT** | written (boot loads) | written | bad peripheral write |

*(Proposed, item 54: the HALT row; the notes give the first two.)* A halt hands everything back. **A push while stopped** is accepted like any other, with no condition, and simply lands in the queue; there is no preloading, so the next start discards it (4.19.8) *(Proposed, accepted, item 76)*. This is what happens to a late push from the previous pass, which the software rule of 4.19.8 keeps from crossing a start.

#### 4.19.11 Run, done, halt and the conditions

**`TILE_PULSE` [2], `PERIPH_RUN`: a pass** (4.17.2). Sets `periph_running` (`TILE_STATUS` [3]), giving the replayer its single-ported memories until it finishes or halts; **empties all four queues** (contents, occupancy and pending, 4.19.8) and resets the decompressors' tables (4.19.15); copies the state word into working pc, `instr_idx` and offset, latches the state word's four flags and the stream word's three lengths, and copies the register image into the working file (64 cycles); clears the rows-run count. Replay never writes region 0, so every pass of the same window starts identically. The first row starts when the copy is done. **Run only, no resume,** as for every engine: a run while the replayer runs is ignored (meshsim flags it, 4.13).

**`TILE_STATUS` [3] `periph_running`.** 1 from the end of the run store's cycle until the pass's last row has been pushed into the output queue, or a halt; it reads 0 while the replayer is idle, so a program that never runs it sees v1.0's behaviour. v1.1's `replay_wait` ([4]) is gone (owner, 2026-10-26): the new-expansion line says the replayer is waiting, and [4] is now the sender's `sender_busy` (4.17.9).

**Done.** The pass ends after its **65,536th row**: `periph_running` falls and the done line (`PERIPH_SERVICE_6`, [18]) is posted. The queues are not reset: anything left in them stays until the next start empties them (4.19.8). Rows still in the output queue drain to the consumer. Done is `periph_running` = 0.

**Halt.** C&C's HALT, or the controller halting itself (bus error, illegal instruction, `ecall`, `ebreak`; 6.1), stops the replayer at once (4.17.7): `periph_running` drops, its queues, its service lines and its stop state reset, and it stays idle until the next start. HALT holds it in reset. Its memories keep their contents and, in HALT, take mode-9 writes (4.19.10).

**The conditions** (raise and run, as in v1.0):

| bit | condition | set when | kind |
|---|---|---|---|
| 11 | bad peripheral write (the slot's, 4.17.6) | a mode-9 packet was dropped (4.19.10) | P |
| 12 | queue overrun | a push found its queue full and was dropped (4.19.8) | P |
| 13 | invalid expansion | a decode-table class 3; an expansion word with a reserved operand code (146–255), `format` 6 or 7, an opcode the replayer doesn't execute (4.19.7), or `untraced` set (4.19.3); a template running past ROM word 511 (4.19.6); or a pop past a stream's length (4.19.15) | P, and H on a corrupted table or ROM |

(Item 55, accepted as written by the owner on 2026-10-23: the kinds and the opcode check.) **An invalid expansion stops the pass** as if it had ended: `periph_running` falls and the done line is posted, so the controller loop exits instead of waiting for rows that can't be right; the condition says why. The owner expects it never to happen, since the tracer catches bad expansions. The other two conditions leave the replayer running.

#### 4.19.12 The output row

**The record** *(Proposed, item 59: the layout; the field semantics are upstream Jolt's)*. Each row is 640 bits, five 128-bit words, carrying what Jolt's 80-byte trace row `RiscCycle` carries (jolt-cpp's port: `trace_owner.hpp:19–94`) plus `instr_idx`, which Jolt's provers take from a separate column (`trace_columns.inc:53`), and less the memory post-value, which the consumer derives (an LD's is rd's value after the row, an SD's is rs2's value; `trace_owner.hpp:230–235`). The values are those jolt-cpp's `expand_compressed_chunk` records (`trace_owner.cpp:893–927`), the gold standard for now; upstream Jolt at 735355e overrides once a difference is confirmed by running both (4.19, item 113). **Serialized, the record is 80 bytes: words 0–4 in order, each 128-bit word little-endian** (byte 0 is word 0's bits [7:0]; owner, 2026-10-26, ruling N11). This is our layout, not `RiscCycle`'s field order:

| word | bits | field | value |
|---|---|---|---|
| 0 | [31:0] | `instr_idx` | the row's bytecode index (0 in padding) |
| 0 | [39:32] | `opcode` | the row opcode; `VirtualAdvice` (122) for every advice row (4.19.7) |
| 0 | [47:40] | `rd_reg` | rd's register number, 0 if rd is an immediate slot |
| 0 | [55:48] | `rs1_reg` | rs1's register number, 0 if an immediate slot |
| 0 | [63:56] | `rs2_reg` | rs2's register number, 0 if an immediate slot |
| 0 | [79:64] | `inline_sequence_remaining` | `seq_remaining` |
| 0 | [80] | `is_compressed` | 4.19.3 |
| 0 | [81] | `is_virtual` | 4.19.3 |
| 0 | [82] | `is_first_in_sequence` | 4.19.3 |
| 0 | [85:83] | `rd_is_imm`, `rs1_is_imm`, `rs2_is_imm` | the slot kinds (4.19.3), as the bytecode commits them |
| 0 | [127:86] | spare | 0 |
| 1 | [63:0] | `pc` | the source instruction's pc (0 in padding) |
| 1 | [127:64] | `imm` | the sum of the immediate slots (4.19.3); Jolt's raw `_imm`, which consumers normalize by opcode (`imm()`, `trace_owner.hpp:80–92`) |
| 2 | [63:0] | `rs1` | rs1's value before the row; 0 if an immediate slot |
| 2 | [127:64] | `rs2` | rs2's value before the row; 0 if an immediate slot |
| 3 | [63:0] | `rd_prev` | rd's value before the row; 0 if an immediate slot |
| 3 | [127:64] | `rd` | rd's value after the row; 0 if an immediate slot, and 0 for x0 |
| 4 | [63:0] | `mem_addr` | rs1 + imm for LD and SD, else 0 |
| 4 | [127:64] | `mem_value` | for LD, **rd's value after the row**: the value loaded, and 0 for an LD to x0 (4.19.7) *(pending the upstream check, item 113)*; for SD, the memory pre-value from the log (0 in a pass without the store log, 4.19.2); else 0 |

A padding row's record is all zeros, as Jolt's padded rows are (4.19.2).

**The output queue** holds 32 rows (about 2.5 KB) between the replayer and its consumer, with ready/valid backpressure: when it is full, the replayer stops before its next row. It absorbs consumer slowdowns and the replayer's own stalls on expansion misses (tens of cycles). The consumer's side of it, its width and its rate, belong to the materializer and are *(Open, item 59)*; until it is specified, meshsim drains it with a sink that takes one row per cycle.

#### 4.19.13 The resident replay region (the software contract)

The trace stays in big SRAM for the whole proof, in a **replay region** per tile, a software-managed cache. **Residency is best effort** (owner, 2026-10-26): each stage keeps as much of a window's streams resident as its free big SRAM allows, and the rest streams from LPDDR through a **staging buffer in big SRAM** and is refilled locally. No fixed size is guaranteed; normal programs fit, and adversarial ones (dense windows) stream, which the host prices. The hardware knows nothing of it: it sees only the mode-9 packets the controller's streams and the memory system send. This subsection is the contract software keeps so that the hardware above works.

**Layout.**
1. **This window's templates first:** only the controller-fed templates its 64K rows actually use (often none; a window of BLAKE3 calls needs about 20 KB [estimate]). The tracer engine lists the set with the checkpoint.
2. **Then code and the two logs** as 4 KB blocks in consumption order, code, read-log and write-log blocks interleaved, so "resident" is a single boundary. A pass that needs only one log keeps and fetches only that one.
3. **The rest,** beyond what the stage's free big SRAM holds (with a 512 KB region, at most about 190 KB in the densest windows [meas: design notes]), stays in LPDDR and is **fetched over the request bus into big SRAM** (mode 3, 4 KB requests, counted in `DMA_SRAM_REMAINING`) ahead of the replayer, then **refilled locally like resident data** (owner, 2026-10-26). The direct read from LPDDR into a queue port is no longer part of the replayer's contract. A fetch counts in `DMA_SRAM_REMAINING` while it is outstanding; the controller loop's refill waits on `DMA_PERIPH_REMAINING` done alone ([3], 4.19.9), so a fetch in flight never holds a refill up (owner, 2026-10-26, item 94). Before refilling from a fetched block, the controller waits for that fetch (`DMA_SRAM_REMAINING` done, [0], or its own count).

The median window's trace is about 335 KB and the densest about 700 KB [meas: design notes]. At the stages whose scratchpad margin is too small, the controller shrinks or evicts the region and reloads it afterwards (at 512 KB per tile, about 33 MB per chip, about 2 ms [estimate]). The same mechanism can spill compact, expensive-to-rebuild intermediates; anything the trace can regenerate is better replayed (about 0.2 ms per pass) than spilled. In sumcheck tails the controller can prefetch the next pass's streams from LPDDR (about 21 MB per chip, about 1.3 ms at 16–17 GB/s [estimate]), into areas the remaining rounds don't use, with C&C pacing the fill so it never fans in.

**The rules software keeps:**
- **Stream to a queue port from its window's base** (4.19.10), in 128-bit packets, with runs of at most a half; a refill is min(half, entries remaining) (4.19.8). Local refills may have two outstanding.
- **No LPDDR read targets a queue** (part C): fetches go into the staging buffer, and the loop refills from there.
- **No other tile sends the tile mode 9 during a pass,** and a queue port takes one source at a time (invariant 18): mode 9 may come from any source (4.17.5), but the controller loop counts only its own refills (4.19.9).
- **Load the checkpoint, the decode table and the ROM while the replayer is stopped or the tile is halted** (4.19.10). There is no preloading of the queues: the start empties them (4.19.8).
- **Before a start, every queue push has landed** (owner, 2026-10-23): `DMA_PERIPH_REMAINING` done for the controller's refill runs (4.19.9). meshsim flags a start with a push still in flight (invariant 16).

**Code is not in big SRAM.** Each tile controller has its own instruction and data memories, and stage overlays are broadcast by C&C at stage boundaries into a parked controller (6.5), so the earlier plan's 256 KiB code reservation in big SRAM is reclaimed for data.

#### 4.19.14 Cost

**Memory per tile:** 4 + 4 + 8 + 8 KB of queues (code, expansion, read log, write log), an 8 KB ROM, a 4 KB decode table, 1 KB of working registers, about 1 KB of image and state, about 2.5 KB of output queue (32 × 80 B) and 384 B of decompressor tables: **about 41 KB** [estimate]. Across 64 tiles that is about 2.6 MB, roughly 7–10 mm² per chip with the logic [estimate], against 48 MB for the 768 KB-per-tile design planned earlier.

**Logic per tile:** the ALU and comparator, the 128 × 64-bit true dual-port register file, two adders (memory address, pc + imm), the operand resolver, the decode index logic, four queue readers and the credit counters, three decompressors (4.19.15): about 0.1–0.15 mm² with its memories [estimate]. Section 11 adds it to the tile's budget; its placement is in section 12.

#### 4.19.15 Stream decompression

*(Owner, 2026-10-26, part D.)* Each of the three streams (code, read log, write log, 4.19.2) arrives **raw or compressed**, chosen per window and per stream by the tracer and recorded in the checkpoint: the compressed flags in the **state word** [114:112], the exact lengths in the **stream word** (4.19.2, 4.19.10). A **decompressor** per stream sits between its queue and the replayer core: the queue holds the stream as delivered, in 128-bit entries, and credits and low-water count those entries (4.19.8).

**Bit order.** A stream is read **least significant bit first within each 128-bit entry,** entries in queue order; every field and value is read least significant bit first. An entry leaves its queue when its last bit has been consumed, or when the stream ends inside it.

**Raw streams.** Code: 32-bit opcode words, four per entry. Logs: 64-bit words, two per entry. Low word first in both.

**Compressed streams** are bit-packed against a **16-entry table** of 64-bit values, E[0] the most recent:
- **hit:** a flag bit 1, then a 4-bit index *i*; the value is E[*i*];
- **miss:** a flag bit 0, a size bit (0 = 32 bits, 1 = 64 bits), then the value, 32 or 64 bits. **A 32-bit miss is zero-extended to 64 bits.** Code values are opcode words, always 32-bit misses or hits.

**True LRU, move to front** (the replacement rule, which the tracer's encoder must match exactly). For the code and read-log streams it runs over all sixteen entries; for the write log, over entries 0–14 (below):
- **hit *i*:** output E[*i*]; then E[1..*i*] ← E[0..*i* − 1] and E[0] ← the old E[*i*] (for *i* = 0 nothing moves);
- **miss *v*:** output *v*; then E[1..15] ← E[0..14] (the old E[15] is dropped) and E[0] ← *v*. A miss whose value is already in the table is legal: it is inserted again, and the older copy stays where it moved to.
- **Reset:** all sixteen entries are 0 at the start of every run (each window start), so checkpoints and mid-window starts need no table state.

**The write log's last-load entry** (owner, 2026-10-26). The pre-values of sub-word stores, AMOs and SC, and of many plain read-modify-write SDs, equal the value of the LD just before them, which the read log has already delivered. So the **write-log table splits into 15 LRU entries (E[0]–E[14]) and one last-load entry, E[15]**; the code and read-log tables stay plain 16-entry LRUs.
- **Update:** on every traced LD row, the value it loads (popped from the read log) is copied into write-log E[15]. Rows that aren't LDs (advice, next indices) don't update it, nor do untraced rows, which a replayer never sees (4.19.3). The tracer's encoder applies exactly the same rule.
- **An LD to x0** (owner, 2026-10-26, part G, ruling N5) *(pending the upstream check, item 113)*: **E[15] takes the value popped from the read log,** as for any LD, even though the row writes nothing and **its record reports a memory value of 0** (4.19.7, flag F4 of `spec/replayer-opcodes.md`). The two rules are separate: the decompressor sees the log value, the record sees what rd holds. The tracer's encoder applies the same rule.
- **hit 15:** output E[15]; nothing is reordered.
- **hit *i* < 15:** output E[*i*]; then E[1..*i*] ← E[0..*i* − 1] and E[0] ← the old E[*i*].
- **miss *v*:** output *v*; then E[1..14] ← E[0..13] (the old E[14] is dropped) and E[0] ← *v*. E[15] is untouched.
- **Reset:** E[15] is 0 at each run start, with the rest.
- **Encoder rule:** a value matching both E[15] and an LRU entry is coded as **index 15**, so the LRU order isn't disturbed. The decoder doesn't depend on that choice; the rule makes the tracer's encoding deterministic.
- **Order within the pass:** an LD row's read-log pop updates E[15] before any later row's write-log pop, since rows are strictly sequential (4.19.7).

The table update happens as each value is produced; the core takes one code value per instruction and at most one log value per row, so each unpacker produces at most one value per row.

**The stream's end.** The stream word gives each stream's **exact length in bits** (24 bits, so up to 2²⁴ − 1, against a bound of about 8.6 Mbit per stream per window [derived: 64K rows × at most two log words × 66 bits]). The tracer pads each stream with zero bits to a whole 128-bit entry; the decompressor ignores the bits after the length. **A pop past a stream's length is an invalid expansion** (condition 13): the pass stops (4.19.11).

**Cost:** three 16 × 64-bit tables (about 384 B of registers; the write log's E[15] is its last-load register, with a mux from the read-log path), three bit unpackers and three length counters (4.19.14).

### 4.20 UART

*(Owner, 2026-10-26, part D.)* **Every tile has the same small UART.** Only a tile whose pins are wired to a serial port uses it: C&C's, once C&C is a tile (item 82). Every compute tile's is unwired.

- **Frame:** 8 data bits, no parity, 1 stop bit, fixed.
- **FIFOs:** a TX FIFO and an RX FIFO of 16 bytes each *(Proposed)*.
- **Bit timing** is counted from the board's **reference oscillator**, not the core PLL, so the rate is right before the PLL is configured. `UART_DIVISOR` holds reference-oscillator cycles per bit; its reset value gives **57,600 baud**: round(`f_ref` / 57,600), where `f_ref` is the oscillator's frequency *(param, Open: the board isn't chosen)*. A 25 MHz oscillator gives 434. A new divisor takes effect at the next idle point of each direction, so a byte in flight is never cut.
- **Receive errors:** a byte arriving with the RX FIFO full is dropped and sets **RX overrun**; a byte with a bad stop bit is dropped and sets **framing error** (both sticky in `UART_STATUS`, cleared by writing 1).

**The registers** (0x890–0x89C, 8.13): `UART_TX` (W) pushes a byte, **stalling while the TX FIFO is full** (a third stall source beside the send and request stores, 4.11; at most one byte time, about 174 µs at 57,600 baud); `UART_RX` (R) returns the oldest byte with a valid bit and pops it only when valid; `UART_STATUS` (R/W) gives both occupancies and the two error bits, and its reads have no side effects; `UART_DIVISOR` (R/W).

**Wake events** (4.12), both **live**, both mask bits reset to 0, and a load that consumes them changes nothing: **[21] UART RX not empty** and **[22] UART TX not full**.

**On an unwired tile** (every compute tile today) the registers decode, so no access is a bus error, but they are no-ops: `UART_TX` writes are dropped and never stall; `UART_DIVISOR` and `UART_STATUS` writes are ignored; `UART_DIVISOR` reads its reset value, round(`f_ref` / 57,600); `UART_RX` reads valid = 0; `UART_STATUS` reads empty FIFOs and no errors. So TX not full is always true and RX not empty never is.

**HALT doesn't reset the UART:** its FIFOs, status and divisor keep running across HALT, like the routers (6.1), so a console survives a halt. **Chip reset** empties both FIFOs, clears the status bits and loads the reset divisor.

**In simulation,** meshsim prints every tile's `UART_TX` writes to that tile's console log, wired or not: a debug print channel for every tile.

**Cost:** two 16-byte FIFOs, a 16-bit divisor and the two shifters: about 0.002 mm² per tile [estimate].

---

## 5. Timing summary

Network: injection at cycle t → route decision (1) → output FIFO (≥ 1) → link (`link_latency`) → route block (1) → … → eject FIFO → selector → traffic director → write engine, GATHER engine or port B. Minimum latency is **3 + 4h cycles for h hops** with 3-cycle links [meas: meshsim]; queueing adds a bounded amount.

| path | timing |
|---|---|
| big SRAM | one access per area per cycle, in arrival order; a read's latency is its queueing plus the array's read latency, both *(Open)* for the hardware (4.1) |
| counters and counters done | a phase's data is readable when every counter is done, which sets `WAKE_EVENTS` [9] (4.12, 4.14) |
| table memories | engine requests and returns cross `ts_engine_latency` each way; controller window one cycle (4.10) |
| controller-memory path | modes 6–8 reach port B `cm_latency` = 2 cycles after the director [built] |
| controller | one instruction per cycle; loads and stores to data memory in one cycle, load-use 0; **loads from the registers and the table window 1 or 2 cycles**, fixed per register by the implementation (meshsim's reference 2; 4.11); stores to them single-cycle and posted; taken branch +1; multiply 3; stalls only on an occupied send holding register, a full request FIFO, a full `UART_TX` FIFO on a wired tile, or in `WFI` (4.11, 4.18, 4.20) |
| registers | a store at *t* takes effect at the end of *t*, seen by the owning unit at *t* + 1 and by any later load; a load issued at *t* with latency *L* returns the state at the end of *t* + *L* − 2 (its sample point) and takes any side effect at the end of *t* + *L* − 1 (4.11); status computed at *t* is visible to a load whose sample point is *t* or later |
| address generator | a run's first request is offered 4·`ts_engine_latency` + 3 cycles after the start store for a group run (S_0), 2·`ts_engine_latency` + 2 for a stream run; one cycle less counted from the cycle the start takes effect (4.2) |
| GO | a pulse at t sets `WAKE_EVENTS` [8] at the end of t, wakes every core in `WFI` at t+1, ends the `done` gate and clears `done` and "command landed"; engines start at the start store (`TILE_PULSE` [1]) that follows (6.3) |
| READY lines | READY fires when "command landed" and `done` are both 1; a pulse at s sets the tile's bit at C&C, readable from s + `ready_latency` + 2; equal for every tile |
| RUN/HALT | a change at C&C reaches every tile `run_latency` cycles later |
| peripheral slot | a mode-9 write lands `rp_latency` = 2 cycles after the director, on every peripheral *(Proposed: the value)*; the sender moves at most one packet per cycle (4.17.5, 4.17.9) |
| replayer | a run copies the register image in 64 cycles, then runs one row per 3 cycles (5 for a multiply) when its queues are fed; a mode-9 write lands `rp_latency` = 2 cycles after the director *(Proposed)*; a pass is 65,536 rows, about 216K cycles [estimate] (4.19.7, 4.19.10) |
| request bus | one 16-bit beat per cycle per hop; a request is 8 beats; on an idle bus about `reqbus_latency` (≈ 15–20 cycles [estimate]; 17 [meas: meshsim]) from the send store to its target's queue; the root passes at most one request per 8 cycles for the whole chip (3.3, 4.18) |
| wake events | every event is set at the end of the cycle its cause happens ; live events follow their conditions with the same timing; a load of `WAKE_EVENTS` or `WAKE_PEEK` sees it, and a pending event releases a waiting `WFI`, in the next cycle (4.8, 4.12) |

---

## 6. Control

*Figures 1.2, 2.1, 2.2, 2.5.*

C&C sequences the phases. It holds the tiles in HALT or lets them RUN, writes commands into their command blocks, collects their READY pulses and pulses GO. It does little work but sits on the critical path of every phase transition, so it is on-die and fast rather than driven by the host over the slow bus.

### 6.1 RUN/HALT and reset

- **The line.** One line from C&C to every tile, with a fixed latency like GO (`run_latency`). Chip reset puts every tile in HALT.
- **The tile's edge.** Both RUN/HALT edges come from one register, `run_q`. HALT → RUN gives a one-cycle pulse; RUN → HALT clears the conditions.
- **In HALT:**
  - the controller does not fetch;
  - the address generator, the GATHER engine, the read port, the return queues, the peripheral and the sender (4.17.7) are held in reset: the address generator is idle, the GATHER engine's queue and its request in progress are cleared, the read port's queue and in-flight count are cleared, the return queues are empty and every token is free, the peripheral is idle with its service lines clear and its own state reset as its section says (the replayer: its queues empty, 4.19.11; its memories keep their contents and take mode-9 writes, 4.19.10), and the sender is idle with any send abandoned. `rd_done` keeps its value;
  - the routers, the traffic director and the write engine run, so packets land exactly as in RUN (4.8), and so do the request port, the request bus and the UART (4.20), so a halted tile's request FIFO drains and a receive queue keeps its requests (4.18);
  - `done` is forced to 1 (6.3).
- **HALT → RUN resets the core only:** PC = **0x1F00** (the resident loader) and x1–x31 = 0. Memories, registers and engines keep their values; `done` and "command landed" are cleared (6.3), and so are the `done` gate, the counters-done qualifier, `WAKE_EVENTS`' sticky events and `WAKE_REQUIRED`, and `WAKE_MASK` is set to [8] | [9] (4.12).
- **A controller halted** by a bus error, an illegal instruction, `ecall` or `ebreak` stops fetching while the tile stays in RUN, with `done` not forced. It restarts only on HALT → RUN.
- **Entering HALT clears every condition** (`TILE_CONDITIONS`, 4.13), **and the four down-counters with their done events and qualifiers** (4.12; owner, 2026-10-26, part E). They stay 0 while the tile is halted, since a halted tile counts nothing.
- **There is no boot ROM.** C&C loads the loader and the first images while the tiles are halted.
- **No packet in flight** when the RUN/HALT line changes: a simulator check [built].

**Chip reset.** Every tile starts halted. The router FIFOs, the return queues, the area queues, the write engine's input FIFO, the send holding register, the request FIFO and the receive queues are empty with every credit returned (`send_busy` and `request_busy` 0); the round-robin pointers, `rd_done`, `rd_running`, `periph_running` and `sender_busy` are 0; the down-counters, `TILE_CONDITIONS`, the counters-done qualifier, the `done` gate, `WAKE_EVENTS`, `WAKE_REQUIRED` and the read port's queue are 0 or empty, `WAKE_MASK` is [8] | [9], and `TILE_STATUS` shows the idle machine. The configuration registers are undefined (8.1). The tables, the controller memories and big SRAM are undefined.

### 6.2 Boot

1. **Load while halted.** With every tile halted, C&C broadcasts the loader and the first image by mode 6 into instruction and data memory, as **128-bit packets** (four words each, addr + 4 per packet), like 128-bit staging (item 28); an image's length is padded to whole packets, so up to three zero words land just past its end, and nothing may be placed there before the load. Staging into big SRAM waits for RUN, when the loader has set `PTR[64]` (below).
2. **The marker.** C&C then sends **one marker word to command slot 0 (a 32-bit command write, mode 7)**. It sets "command landed", and since `done` is forced to 1 in HALT, READY fires at once: boot uses the general rule (6.3), with no special case.
3. **RUN.** C&C waits for all 64 READY bits, then raises RUN, which clears `done` and "command landed".

- **No timing assumption.** In-order delivery (3.2) guarantees that everything sent before the marker has landed when the marker has.
- **The marker can be the loader's first command;** it is in slot 0 when the loader starts.

**Staging later images and overlays:**
- **How:** C&C broadcasts images and overlays into big SRAM as **mode-2 packets**. Since C&C's src is 64, they land at each tile's `PTR[64]`, which the loader sets at boot to its own staging area.
- **Where:** in area A7 below its top 128 KB (the default geometry's eighth 512 KB block, 4.1). **The top 128 KB of A7 is sort scratch** (skew handling's values staging needs up to ≈ 4.7K values beyond areas A0 + A3; section 14), so images and overlays must fit in A7's lower 384 KB.
- **C&C needs no addresses.** The staging area can sit relative to the top of memory, which depends on the device's size (`TILE_STATUS`).
- **Packets are 128-bit** *(Proposed)*: C&C sends a whole row per packet, one store per 32 bits as a tile's send port does (4.11), with a quarter of 32-bit packets' network load and half of v1.0's 64-bit packets'.
- **Images land back to back,** `PTR[64]` advancing by each packet's size in chunks. With 128-bit packets, each image starts on a row (a chunk address that is a multiple of 4) and is padded to a whole number of rows *(Proposed)*; v1.0's 64-bit packets needed even chunk addresses and even lengths. The loader learns each image's start from the command that names it, or resets `PTR[64]` per image.
- **When** (owner, 2026-10-26): staging is counted data (mode 2), so it reaches a tile **only while the tile is halted** (nothing is counted) **or after a command that tells it what to expect**, so the tile arms `DMA_SRAM_REMAINING` first and waits for counters done. Never during a mesh operation. Code overlays follow the same rule (6.5).
- **An uncoordinated write** (owner, 2026-10-26): if C&C must write a tile that hasn't armed for it, it **halts the tile first**; while halted, nothing is counted.
- **Announced staging waits for the arming** (owner, 2026-10-27, part M). After the command that announces staging, the receiving tile arms its counter in the start path, after GO (6.4): about 10 cycles after GO at 2-cycle register loads [derived from the measured start path]. C&C's first staged packet can travel faster than that, so **C&C sends it no sooner than `stage_delay` = 32 cycles after its GO store** *(param, Proposed)*, about three times the arming time; meshsim's harness has used 64. The bound is tied to the start path: a command whose start path arms later says so, and C&C waits correspondingly longer. A packet that beats the counter is an unexpected arrival (condition 2), so a violation cannot go unnoticed, and meshsim flags it.
- **Running a staged image:** the tile copies it into controller memory with a mode-6 loopback stream (6.5).
- **At first boot** `PTR[64]` isn't set yet, so the loader and the first image go in by mode 6, as above.

### 6.3 `done`, READY, GO and `WFI`

**Two bits per tile:**
- **`done`** says the tile has finished its work and can take the next command. It is:
  - forced to 1 in HALT;
  - cleared by GO and by HALT → RUN;
  - set by the controller with `TILE_PULSE` [0].
- **"Command landed"** is set by a command write (mode 7) to command slot 0, and cleared by GO and by HALT → RUN. The same write posts the command-landed event, `WAKE_EVENTS` [10] (4.12), which only a load consumes.

**READY is automatic.** When "command landed" and `done` are both 1, the tile pulses its READY line once. The two can become true in either order, so a command may land before or after the tile finishes. The controller never pulses READY directly.

**The READY line** is its own line, one per tile to C&C, at equal latency `ready_latency`: a one-cycle pulse on the rising edge of (`done` & "command landed"), setting a sticky bit at C&C.

**The command convention.** C&C writes any arguments first, then the command word to slot 0, all as 32-bit command writes into the **command block** (4.12). In-order delivery means the whole command has landed when "command landed" is set, **provided all of a command's packets to a tile are of one kind**, all broadcast or all unicast (3.2).
- **The command word** (`sw/mesh.h`): [31:24] type, [23] parity, [22:0] argument.
- **Alternating argument blocks** (owner, 2026-10-26): even-parity commands put their arguments in **slots 1–7**, odd-parity commands in **slots 8–15**, seven arguments each, and the tile reads the block its command's parity names. A command sent right after GO can then never overwrite the arguments of the one the tile is still reading. Slot 0 of the next command lands at the earliest 4 cycles after GO; the simulator checks that no slot-0 write lands before the previous command word was read [meas: meshsim].

**GO** is a single global net from C&C, balanced so every tile sees a pulse on the same cycle. C&C drives it with its GO pulse, `TILE_PULSE` [3] in the common tile design (8.6); in every tile whose pulse isn't wired to the net, that bit is ignored (meshsim flags it). **GO posts the GO event, `WAKE_EVENTS` [8]** (4.12), which wakes `WFI`, ends the `done` gate, and clears `done` and "command landed". It does not start the address generator; the controller starts it with the start pulse (`TILE_PULSE` [1], 4.2). Like every event, GO is sticky until a load consumes it, so `wait_for_command()` consumes it on waking; it arrives only while the tile waits in `wait_for_command()` (below), so a pending GO is never stale in a program that uses the helpers.

**`WFI`** stalls the core until the wake term holds, then continues at the next instruction. **All wakes are events** (4.12): `WFI` completes on the events pending in `WAKE_EVENTS` under `WAKE_MASK` and `WAKE_REQUIRED`, among them GO ([8]), counters done ([9], v1.0's local wake, which also covers a request's data and reply, 6.7), the peripheral's service lines ([18:13], 4.17.3; the replayer's, 4.19.9), a received request ([19], 4.18), the end of a send ([20], 4.17.9) and the UART ([21], [22], 4.20). From the `done` pulse until GO the `done` gate overrides the term to GO alone. A wake consumes nothing; the `sw/mesh.h` helpers follow every `WFI` with a load of `WAKE_EVENTS` (4.12).

**`wait_for_command()`**: the controller pulses `done` and then executes `WFI` **immediately**, as two back-to-back instructions in a `sw/mesh.h` inline helper; on waking it **loads `WAKE_EVENTS`, consuming GO** (GO stays in `WAKE_MASK`, 4.12), then **reads command slot 0 and returns the command word** (the simulator checks that every command word is read before the next lands).
- The load after `WFI` takes the same fixed latency on every tile (4.11), so the path from GO to the start store stays identical everywhere.
- This is safe because GO cannot come back sooner than the READY round trip (≈ `ready_latency` + 3 cycles), so the core is always in `WFI` when GO arrives.
- `done` stays an explicit pulse rather than "being in `WFI`", because a tile also uses `WFI` to wait for arrivals, which mustn't look finished to C&C.
- The gate makes it robust: from the `done` pulse until GO only a pending GO can wake the core (4.12), whatever the program's `WAKE_MASK` holds.
- READY waits for `done`, so a command that lands before the tile waits is not lost.

**A one-command phase** (a local sort, a gather):
1. The tile finishes its previous work and calls `wait_for_command()`.
2. C&C writes the command, slot 0 last. When it has landed and `done` is set, READY goes out.
3. When all 64 READY bits are set, C&C clears them and sends GO. Every core wakes, reads its command and runs the phase.

**A phase whose engines must start together** (an all-to-all) uses two commands:
1. **"Configure":** the tile wakes, writes its tables and `DMA_CONFIG` for the run, and calls `wait_for_command()`.
2. **"Start"**, a one-word command: the tile wakes, stores its counters (6.4) and pulses start (`TILE_PULSE` [1]).

Every core leaves `WFI` on the same cycle and runs the same fixed code to the start store (no caches, single-cycle loads; the helper's load of `WAKE_EVENTS` is part of that code), so the engines start on the same cycle on every tile, and the start's lead-in (4.2) is the same everywhere. **The path from `WFI` to the start store must be identical code on every tile.**

**Costs:** each command costs one READY → GO round trip, ≈ `ready_latency` + 3 cycles [meas: meshsim] plus C&C's poll loop: ≈ 11 cycles at `ready_latency` 8, under 1K cycles over about 40 phases [estimate].

### 6.4 Phases that receive

- **A tile whose phase receives** arms its counters **in the start path, after GO: write the counters first (each write adds; at a phase start the count is 0), then pulse start.** Arming them during "configure" would break invariant 4 at configure's `done`; storing them before the start pulse means no arrival can beat its counter. The start path is identical code on every tile, so every tile's start store lands on the same cycle (plus skew), and its first request follows by the lead-in of 4.2. Measured from C&C's GO store to every tile's first request, at 2-cycle register loads, meshsim's reference (4.11) [meas: meshsim]: **12–13 cycles** for a stream run started by a counter write then start (8–9 to the start store, then 2L + 2), and **20–21** for a group run on the all-to-all recipe's longer start path (13–14, then 4L + 3); a group run with the shortest start path would take 15–16 [derived from the two measured parts]. The three cycles over v1.0's 9–10 and 17–18 (1-cycle loads) are GO's event becoming visible one cycle after GO is seen, since event pulses cross into the island through a flop (section 12), and `wait_for_command()`'s two loads at 2 cycles each (owner, 2026-10-27, part M). Every tile's first request lands within the 0/+1 start skew. The tile then `WFI`s for counters done (4.12) when it has nothing else to do.
- **Every expected arrival must have landed and every write cleared before the tile pulses `done`** (invariant 4). Counters done guarantees it.
- **A tile that only sends** in a phase still waits for `mem_idle` before `done`, because its own loopback writes count.

### 6.5 Block copy into controller memory (overlays, tables)

1. Write the `STREAM` entry, one store per array that changes: `STREAM_SRC` the source; `STREAM_DEST` the controller-memory destination word, *d* = 0; `STREAM_COUNT` the count, mode 6 and read size 128 (or 64 or 32).
2. Write the number of packets (elements) to `DMA_CM_REMAINING`, which adds it to the count (0 here). This also clears counters done (`WAKE_EVENTS` [9]) and its qualifier.
3. Write `DMA_CONFIG`: a stream run of one stream, starting at that entry.
4. Pulse start (`TILE_PULSE` [1]).
5. `WFI` (the helper, with [9] in `WAKE_MASK`): counters done is set when `DMA_CM_REMAINING` reaches 0 and the port-B pipeline is empty, and the helper's load consumes it.

The copy runs at up to 128 bits per cycle through the loopback (4.2): a row-aligned source at read size 128 writes four words per packet, with no polling and no C&C traffic. A copy whose length is not a whole number of rows ends with a smaller-size stream, or is padded. The code doing the swap must sit outside the region being overwritten: the resident loader, or any code the overlay does not replace.

### 6.6 The C&C controller and its registers

- **The C&C controller** reuses the tile controller design (4.11): RV32I + Zmmul + `WFI`, the same toolchain, its own `mesh.h` from the same register-table generator.
  - Its memories differ: a large program RAM holding the whole sequencer with no overlays (proposal: 16–32 KB), loaded once from the FPGA at boot, and a small data RAM (a few KB).
  - No big SRAM, no engines.
  - About 0.04 mm², once (section 11).
- **Registers** (the block is not fully specified; names *(Open)*, section 13):
  - a **send** holding register into tile 0's "from C&C" route block (4.16): one packet per cycle, any dst (126 for broadcast), any mode, address and size, src = 64;
  - the **READY bits:** 64 sticky bits, bit s set by a pulse on tile s's READY line, read as two 32-bit halves and cleared together by a reset store, where a pulse arriving in the cycle the reset takes effect is kept;
  - the **GO-pulse** and **RUN/HALT** registers;
  - host-interface registers.
- **C&C's port is shared with the memory system** (6.7): the memory system's deliveries enter tile 0's "from C&C" route block like C&C's own packets, at most one packet per cycle between them, and a tile's write stream arrives on C&C's receive path. How the two share the port is inside C&C (item 9).
- **The error line:** each tile drives "my `TILE_CONDITIONS` is non-zero"; the lines are ORed (a tree across the tiles, like the READY lines but without the equal-latency requirement) into **one sticky error bit at C&C**, which is also a wake source for C&C's `WFI`. C&C learns *that* something is wrong at once, and *when* from the phase it is running.
- **REPORT** (a debugging command; a correct program never needs it): when the error bit is set, C&C stops issuing GOs, so every tile finishes its phase and waits in `wait_for_command()` with its conditions intact; C&C then broadcasts a REPORT command.
  - **The settle.** REPORT usually lands on the command C&C sent right after the last GO, which the tiles have not read; that command is discarded, and its READY has already fired, so READY cannot say when REPORT has landed. C&C therefore waits a fixed settle between sending REPORT and its GO: **twice the worst-case latency of a one-packet broadcast from C&C to its farthest tile** (tile 36, 8 hops from tile 0's root), 2 + `intra_tile_latency` + `cm_latency` + 8 × (`link_latency` + 1) = 37 cycles at the defaults [meas: meshsim, exact at `link_latency` 2–4, `cm_latency` 2–4, `intra_tile_latency` 1–2, with and without start skew], so 74 cycles. The mesh is idle then, since every tile has reported READY (a crashed tile excepted, whose wait C&C's watchdog ends).
  - **The replies.** Each tile, on waking, waits in a software delay loop of about (`me` + 1) × 4 cycles (the core's timing is deterministic), then sends C&C one packet: its tile index and `TILE_CONDITIONS`. The replies are thus paced one every 4 cycles, about 260 cycles in all, with no fan-in risk.
  - C&C logs them, then halts the machine, which clears every condition. A tile stuck in an error that never reaches `wait_for_command()` does not reply; C&C's watchdog and READY bits identify it. Recipe: `sw/RECIPES.md`.
- **Receive path:** packets for dst 64 arrive from tile 0's "to C&C" output (4.16), at most one per cycle, and land in a **receive FIFO at C&C** (at least 64 entries of 128-bit payload with its 2-bit size, plus the 7-bit src *(Proposed)*; v1.0's held 64-bit payloads), read through registers (the payload's words, and src with the size and a valid bit). C&C's port ignores the mode and address fields, except mode 3: a mode-3 packet is a write stream's data and goes to the memory system, not to the receive FIFO (6.7). A packet arriving with the FIFO full is lost and sets a C&C error flag (its own flag, not the tiles' error bit; meshsim reports each loss). Room freed by a read is room from the next cycle.
- **Pacing.** C&C sends bulk data between all-to-all passes, or paces it (4.16). **C&C waits `run_latency` + 1 cycles after a RUN/HALT store, and `go_latency` + 1 cycles after each GO store, before sending.** Otherwise a command can reach tile 0 before RUN or GO does: before RUN, it breaks invariant 7 or is cleared by HALT → RUN; before GO (at `go_latency` ≥ 6), GO clears its "command landed", READY is lost and the machine hangs.
- **Commands may cross the mesh during a phase:** READY needs a landed command, so the next command is sent right after GO, one broadcast packet, which adds at most one packet to any link. It is safe because GO clears "command landed" before the next command can land.
- **Wake sources for C&C's `WFI`.** C&C waits through every phase, so it sleeps rather than polls. **The error bit** (any tile's `TILE_CONDITIONS` non-zero) is a wake source. Three more are *(Proposed)*:
  - **all 64 READY bits set** (the edge), so the sequencer loop is: send the command, `WFI`, clear the READY bits, GO;
  - **a packet arriving on C&C's receive path** (an error or result from a tile);
  - **a watchdog timer,** set by C&C per phase, so a tile that never reports READY wakes C&C instead of hanging it.

  The wake events and the watchdog register belong to C&C's register block, which is not yet specified.

### 6.7 The memory system's contract

The memory system serves the tiles' requests on the request bus (3.3), with LPDDR behind it. This section specifies only what a tile sees: the packets that come back, their order, and what a program must keep to. Its internals (its cache, LPDDR scheduling, the write-token scheduler, and how it shares C&C's port with C&C's controller) are out of scope (section 7; item 9). **The same tile-side rules hold for the answers of every request receiver** (3.3): a controller or an engine that answers a request does so as the memory system does here.

**Where it sits.** The memory system is owned by C&C and, in v1.1, **served at C&C's port**: its packets enter the mesh through tile 0's "from C&C" route block with **src 64** (4.16), and a tile's write stream goes to **dst 64**. Address 65 is reserved for it, should it get a port of its own (2.1); the endpoint map will settle who serves LPDDR and where (item 82). It sends modes 3, 6, 8 and 9, and **everything it sends is counted** like any other data (4.12): there is no wake-event mode.

**The request's fields.** The hardware commits only a request's requestor [6:0] and target [9:7] (3.3); **everything in [127:10] is laid out by the target** and specified with each receiver. What this section relies on is only that the request names, wherever its target puts them:
- for a **read**, its LPDDR source and length (in 128 B units), its **destination kind** and its **destination address** in 128 B units (the *tile address* below);
- for a **write**, its LPDDR destination and length, and whether it asks for an **acknowledgement**.

**Dispatch.** The root places each request in its target's queue (3.3), and the target's receiver takes them in arrival order.

**How a request is answered** (owner, 2026-10-26). **First any bulk data,** delivered to the destination the request named and **counted against what the requesting tile armed when it sent the request**; **then, only if the request has a value answer, exactly one reply** (mode 8), counted in `DMA_REPLY_REMAINING` and landing in the reply block at the responder's pair of words (4.12).
- **A read's answer is its data,** with no reply. Reads may be outstanding in any number: their packets are counted, and counters done says when all have landed.
- **A value answer is a reply:** a write's token, a write's acknowledgement, an fd, an address, an error code. Each is 32 or 64 bits, and its meaning is the target's.
- **At most one reply-producing request is outstanding per responder** (owner, 2026-10-26): replies from one responder land in one place (its pair of words in the reply block), so a second would overwrite the first. A program reads a reply before it issues the next reply-producing request to that responder. **A write's token reply already serializes writes to one responder.** Until the endpoint map (item 82) the memory system and C&C both answer as src 64, so they count as one responder.

**Reads.** A read copies its length, starting at its LPDDR source, to the endpoint that sent it (the requestor, stamped by the tree), into the place its destination kind names, starting at **tile address × 32**:

| destination kind | packets | the first packet's addr | step per packet | counted in |
|---|---|---|---|---|
| big SRAM | mode 3 (4.9) | chunk address = tile address × 32 | +4 chunks | `DMA_SRAM_REMAINING` |
| controller memory | mode 6 (4.11) | packed word = tile address × 32 | +4 words | `DMA_CM_REMAINING` |
| the peripheral | mode 9 (4.17.5) | peripheral word address = tile address × 32 | +4 words | `DMA_PERIPH_REMAINING` |

- **The data:** one packet per 16 bytes, **every one 128 bits, unicast to the requesting tile, in ascending address order** *(Proposed)*. Each is aligned to its size, as mode 3 requires (3.1). **The requester arms** the destination's counter with the read's packet count (length × 8) before it sends the request, by writing the count, which adds to whatever is already in flight (4.12), so nothing can arrive unexpected.
- **Landing.** A read has landed when its counter is done: `DMA_SRAM_REMAINING` = 0 with `mem_idle`, `DMA_CM_REMAINING` = 0 with `cm_idle`, or `DMA_PERIPH_REMAINING` = 0 (4.14); counters done says all of them have (4.12). v1.1's completion packet, and the separate `mem_idle`/`cm_idle` step after it, are gone.
- **Destination bounds:** big SRAM wraps mod 2²¹ and follows 4.1's out-of-range rule. Controller memory obeys invariant 11 like any mode-6 packet *(Proposed)*: the block lies inside instruction and data memory and below the reply block, so tile address + length in 128 B units ≤ 119 (the block ends at or below packed word 0xEF0). The peripheral's map is its own (the replayer's windows, 4.19.10). **The replayer takes no reads into its queues** (owner, 2026-10-26): its LPDDR overflow is fetched into big SRAM and refilled locally (4.19.13).

**Order between reads.** The memory system may answer reads in any order, cache hits before misses included, and may interleave the packets of reads to the same tile; each read's own packets stay in ascending order. Software that needs one read's data before another's arms and waits for them one at a time.

**Writes** are token-gated, so that only one tile at a time streams into C&C's port and nothing fans in:
1. The tile arms `DMA_REPLY_REMAINING` for the token (writing 1, which adds) and sends a write request with its LPDDR destination, its length and whether it wants an acknowledgement.
2. When the memory system is ready for that write, it grants a **write token**: a **reply** (mode 8) to the tile. It grants one token at a time across the chip.
3. When the token has landed (`DMA_REPLY_REMAINING` done, `WAKE_EVENTS` [2]), the tile reads it and **streams the block** to C&C: a stream run with *d* = 64, mode 3 and `STREAM_DEST`'s destination word 0 (4.2), so **each packet's addr is its chunk offset within the write**, aligned to its size (3.1). Any read size works; 128 bits is the natural one. The memory system places each packet by its offset and never depends on arrival order. The stream must cover the block exactly: its length, each chunk once.
4. When the memory system has the whole block, the grant ends and the next token can go out. If the write asked for an acknowledgement, it then sends a **write acknowledgement**, a second reply, once any read that reaches it afterwards will see the data *(Proposed)*. The tile arms `DMA_REPLY_REMAINING` for it, and reads the token before the ack can land on the same pair of words.

- **The acknowledgement is needed only before the region is read again,** by this tile or another. A program that hands a written region to another tile waits for the ack first.
- **The source buffer** may be overwritten once the stream run has finished (`rd_done`): every read of it has then been taken by its area, ahead of any later write (4.1) [derived].
- **A tile that never streams** after its token stalls every later write on the chip: a hang, which C&C's watchdog catches (4.13's principle).
- **At C&C's port,** a mode-3 packet from the token holder, within the block and aligned to its size, goes to the memory system; any other mode-3 packet (from another tile, before the token, past the block or misaligned) is dropped and sets a C&C error flag *(Proposed)*, and meshsim flags it (invariant 17).

**Debug messages** *(Proposed)*: tokenized printf, a format id and its arguments, laid out by the target, with the format strings kept on the host. A message to the memory system is appended, with its requestor, to a debug log in LPDDR, in arrival order; one to a controller goes to that controller's software (item 11). There is no answer. A message longer than one request is several requests, which arrive in order (3.3). Heavy printing stalls only the tile that prints, and the requests of its row behind it (3.3, 4.18).

**Requests to the controllers** are their software's to define (3.3); a controller reads them through its request port's receive side (4.18) and answers, if at all, by the rules of this section.

**Errors** *(Proposed)*. A request that names a reserved target, or a target with no receiver, is dropped at the root and sets a C&C error flag (3.3). A receiver drops a request it cannot serve (for the memory system: an operation, destination kind or layout it doesn't define, or an LPDDR range past the device) and sets a C&C error flag. That is C&C's own flag, not the tiles' error bit: the request bus has no tile condition on the sending side. meshsim flags every case. A dropped read never lands, so its tile waits until C&C's watchdog acts, and the flag tells C&C why.

**No per-tile pacing.** The memory system may deliver to a tile at up to one packet per cycle: the tile's eject output serves incoming packets before its loopback (4.7), so its link-side eject FIFO drains as fast as the delivery fills it, and the loopback, which is token-protected, waits (4.5). Other traffic to a receiving tile (C&C's own, other tiles') comes on top and is the program's to pace, as always (3.2).

**What a program must keep to** (invariant 17, the request contract):
- arm the counters for a request's data, and `DMA_REPLY_REMAINING` for its reply, before sending it;
- at most one reply-producing request outstanding per responder, its reply read before the next;
- a write stream only after its token, covering its block exactly;
- **no read outstanding and no write token pending across a group run**, so the memory system's traffic never meets the all-to-alls' schedule (4.16);
- no read-port results outstanding while deliveries arrive (invariant 14, 4.3);
- controller-memory deliveries inside controller memory and outside the command and reply blocks (invariant 11).

---

## 7. Memory subsystem

*Figures 1.1, 1.2.*

The memory subsystem **hangs off C&C**, not off the mesh: it has no mesh port of its own (address 65 is reserved for one, 2.1). **Tiles reach it through the request bus** (3.3), and it answers through C&C's port as src 64, with mode-3, mode-6 and mode-9 data and mode-8 replies; its contract with the tiles is 6.7. C&C also moves data between it and the tiles with its ordinary traffic (mode-2 broadcasts and unicasts, mode-6 loads, 6.2, 6.6; and mode-9 loads for the peripheral, 4.17.5). Its internals (the cache, LPDDR scheduling, the write-token scheduler) and its link to C&C are out of scope.

---

## 8. Registers

*Figures 3.2, 3.3, 3.4.*

### 8.1 Register map and general rules

The tile's registers fill **0x800–0x89C with every family contiguous and no holes** (owner, 2026-10-26, part E, reversing part j's "no renumbering"; offsets 0x800–0x828 are unchanged): `TILE_` 0x800–0x808, `ROUTER_` 0x80C–0x824, `DMA_` 0x828–0x83C (the record base and the four counters), `SRAM_` 0x840–0x844, `WAKE_` 0x848–0x854, `REQUEST_` 0x858–0x864 (each offset a register for stores and another for loads, 8.10), `SENDER_` 0x868–0x86C, `PERIPH_STATUS_` 0x870–0x88C and `UART_` 0x890–0x89C. Register *i* is at 0xFFFFF800 + 4·*i*; the offset below is the address's low 12 bits. They are grouped by owner: `TILE_` for the whole tile, `ROUTER_` for the send port (it puts packets into the network), `DMA_` for the engines and their arrival counters, `SRAM_` for the read port (the controller's reads of big SRAM), `WAKE_` for the wake events, `PERIPH_` for the peripheral slot (its arrival counter is `DMA_PERIPH_REMAINING`), `REQUEST_` for the request port, `SENDER_` for the sender and `UART_` for the UART. `sw/mesh.h` (version 1.1) and `sw/REGISTERS.md` are generated from the same register table and match this section. v1.1 reorders the map by family, for sanity rather than compatibility: every offset after `TILE_CONDITIONS` moved, so every tile image, and every header that hard-codes addresses (`sort/split/split_regs.h`), is rebuilt against `mesh.h` 1.1.

| offset | register | access | function | section |
|---|---|---|---|---|
| 0x800 | `TILE_STATUS` | R | live bits, peripheral type, endpoint id, big-SRAM size | 8.2 |
| 0x804 | `TILE_CONDITIONS` | R | the error conditions | 8.3 |
| 0x808 | `TILE_PULSE` | W | `done`, start, peripheral-run and GO pulses (v1.0's `DMA_PULSE`) | 8.6 |
| 0x80C | `ROUTER_SET_HDR` | W | dst, mode and addr of outgoing packets | 8.4 |
| 0x810 | `ROUTER_SET_W0` | W | payload bits 31–0 of the next packet | 8.4 |
| 0x814 | `ROUTER_SET_W1` | W | payload bits 63–32 | 8.4 |
| 0x818 | `ROUTER_SET_W2` | W | payload bits 95–64 | 8.4 |
| 0x81C | `ROUTER_SEND_32` | W | payload bits 31–0; sends a 32-bit packet | 8.4 |
| 0x820 | `ROUTER_SEND_64` | W | payload bits 63–32; sends a 64-bit packet | 8.4 |
| 0x824 | `ROUTER_SEND_128` | W | payload bits 127–96; sends a 128-bit packet | 8.4 |
| 0x828 | `DMA_CONFIG` | W | the run and the receive-side settings | 8.5 |
| 0x82C | `DMA_RECORD_BASE_ADDRESS` | W | the run's record buffer, for augmenting and gathers (was 0x838) | 8.7 |
| 0x830 | `DMA_SRAM_REMAINING` | R/W | down-counter for modes 2–5; a write adds (was 0x82C) | 8.7 |
| 0x834 | `DMA_CM_REMAINING` | R/W | down-counter for mode 6, read-port results included; a write adds (was 0x830) | 8.7 |
| 0x838 | `DMA_REPLY_REMAINING` | R/W | down-counter for mode 8; a write adds (was 0x834) | 8.7 |
| 0x83C | `DMA_PERIPH_REMAINING` | R/W | down-counter for mode 9; a write adds (was 0x854) | 8.11 |
| 0x840 | `SRAM_RD_ADDR_QUEUE` | R/W | write: queue a read; read: free entries (was 0x83C) | 8.8 |
| 0x844 | `SRAM_RD_DEST_ADDR` | W | the read port's setup: the next result's controller-memory word and the read size (was 0x840) | 8.8 |
| 0x848 | `WAKE_EVENTS` | R | pending wake events; a load clears those under `WAKE_MASK` (was 0x844) | 8.10 |
| 0x84C | `WAKE_PEEK` | R | pending wake events, read without clearing anything (was 0x874) | 8.10 |
| 0x850 | `WAKE_MASK` | R/W | the events that wake `WFI` on their own and that a load of `WAKE_EVENTS` clears (was 0x848) | 8.10 |
| 0x854 | `WAKE_REQUIRED` | R/W | the events that wake `WFI` only all together (was 0x870) | 8.10 |
| 0x858 | `REQUEST_SET_W0` / `REQUEST_GET_W0` | W / R | store: the request's bits 31–0; load: the oldest received request's bits 31–0 | 8.10 |
| 0x85C | `REQUEST_SET_W1` / `REQUEST_GET_W1` | W / R | bits 63–32 | 8.10 |
| 0x860 | `REQUEST_SET_W2` / `REQUEST_GET_W2` | W / R | bits 95–64 | 8.10 |
| 0x864 | `REQUEST_SEND` / `REQUEST_RECEIVE` | W / R | store: bits 127–96, sends the request; load: bits 127–96, pops the received request | 8.10 |
| 0x868 | `SENDER_SEND` | W | starts one send of peripheral output | 8.12 |
| 0x86C | `SENDER_ADDR` | W | the first packet's addr for modes 3, 6 and 9 | 8.12 |
| 0x870–0x88C | `PERIPH_STATUS_0`–`PERIPH_STATUS_7` | R | the peripheral's status, meanings per peripheral (was 0x880–0x89C) | 8.11 |
| 0x890 | `UART_TX` | W | push a byte (was 0x8B0) | 8.13 |
| 0x894 | `UART_RX` | R | the oldest byte and its valid bit; pops when valid | 8.13 |
| 0x898 | `UART_STATUS` | R/W | occupancies and sticky errors (write 1 to clear) | 8.13 |
| 0x89C | `UART_DIVISOR` | R/W | reference-oscillator cycles per bit | 8.13 |
| 0x8A0– | unmapped | — | bus error | 8.1 |

**General rules:**
- Registers sit in the top 2 KB (0xFFFFF800–0xFFFFFFFF), each one `lw`/`sw` with `rs1 = x0`.
- **Write-only registers read as 0,** not a bus error. The request port's four offsets are the exception: each is a write-only register for stores and a read-only register for loads (8.10), so a load there reads the receive side.
- **Sub-word accesses** (`lb`, `lh`, `lbu`, `lhu`, `sb`, `sh`) to a register are a bus error; registers are `lw`/`sw` only.
- **A store to a read-only register, or any access to an unmapped offset** (0x8A0–0xFFC; the map has no holes), is a bus error.
- **Timing** (4.11): a load takes 1 or 2 cycles, fixed per register, and returns the state at its sample point; a store is single-cycle and posted, taking effect at the end of its cycle; accesses complete in program order.
- **Reserved and spare bits** are ignored on a store and read 0; meshsim also flags a 1 written to `TILE_PULSE` [3] on a tile whose GO pulse isn't wired (8.6).
- **Reset values:** at chip reset the down-counters, `TILE_CONDITIONS`, the counters-done qualifier, the `done` gate, `WAKE_EVENTS`' sticky events, `WAKE_REQUIRED`, the read port's queue and the request FIFO are 0 or empty, `WAKE_MASK` is [8] | [9] (GO and counters done), and `TILE_STATUS` reflects the idle machine (6.1), with the peripheral and the sender idle (4.17.7). **The configuration registers** (`DMA_CONFIG`, `DMA_RECORD_BASE_ADDRESS`, `ROUTER_SET_HDR`, `ROUTER_SET_W0`–`W2`, `REQUEST_SET_W0`–`W2`, `SENDER_ADDR`, `SRAM_RD_DEST_ADDR`) **are undefined at chip reset and kept across HALT → RUN**; software writes each before use. The tables' contents are likewise undefined at reset.
- Configuration registers are static during a phase.

### 8.2 `TILE_STATUS` (0x800, read-only)

| field | bits | reset |
|---|---|---|
| `rd_running`: the address generator is running, from the end of the start store's cycle (lead-in included) until it has walked the run's last entry (4.2) | [0] | 0 |
| `rd_done`: the last run has finished; cleared by the next start; 0 before a tile's first run (4.2) | [1] | 0 |
| GATHER busy (4.4) | [2] | 0 |
| `periph_running`: the peripheral is running, its ownership state, from the end of the run store's cycle until its work ends or a halt (4.17.2; the replayer: until a pass's last row is in the output queue, 4.19.11). v1.1's `replay_running` | [3] | 0 |
| `sender_busy`: a send is in progress, from the end of the `SENDER_SEND` store's cycle until its last packet has left (4.17.9). v1.1's `replay_wait` is gone | [4] | 0 |
| `send_busy`: the send holding register is occupied (4.11) | [5] | 0 |
| `ts_busy`: a table memory is in use by an engine (4.14) | [6] | 0 |
| `mem_idle`: the big-SRAM write path is empty (4.1) | [7] | 1 |
| `cm_idle`: the port-B pipeline is empty (4.12) | [8] | 1 |
| `done` (6.3); forced to 1 in HALT | [9] | 1 |
| "command landed" (6.3) | [10] | 0 |
| `request_busy`: the request FIFO is full (4.18) | [11] | 0 |
| peripheral type: 0 none, 1 the replayer, 2–15 reserved (4.17.1) | [15:12] | the tile's peripheral; 1 on tiles 0–63 |
| spare (reads 0) | [19:16] | 0 |
| endpoint id (2.1): the tile index, 0–63, on a mesh tile | [26:20] | the tile's id |
| big-SRAM size in 256 KB units − 1 (0–31: 256 KB to 8 MB, 4.1); bytes = (n + 1) << 18, the first chunk past the end = (n + 1) << 16 | [31:27] | the device's size; 15 at the default 4 MB |

A load returns the state at its sample point (4.11); status bits are registered at their source (section 12). A store is a bus error. The size field sits at the top of the word, where a shift alone extracts it. v1.1 groups the live bits: the engines first ([0]–[4]), then the send port, the tables, the write paths and control. v1.0 had `send_busy` [3], `ts_busy` [4], `mem_idle` [5], `cm_idle` [6], `done` [7], "command landed" [8], GATHER busy [9], and [2] spare. **The endpoint id** (owner, 2026-10-26) is 7 bits at [26:20], widened from v1.1's 6-bit tile index at [26:21], so that every endpoint built on the tile's design reads its own id; **its low bit moves from [21] to [20]**, so code that extracts the tile index changes (`mesh.h`'s field macro).

### 8.3 `TILE_CONDITIONS` (0x804, read-only)

The conditions of 4.13, bit *n* as in its table: 15 conditions in [14:0], the peripheral slot's in [13:11] ([11] bad peripheral write; [12] and [13] the peripheral's, for the replayer queue overrun and invalid expansion; 4.17.6), receive underflow at [14] (4.18), and [31:15] reserved, reading 0. The bits moved from v1.0 (v1.0 → v1.1): drift 0 → 3, router overflow 1 → 0, write-engine input overflow 2 → 4, read-port overflow 3 → 7, unexpected arrival 4 → 2, table request overflow 5 → 5, bus error 6 → 9, illegal instruction 7 → 10, GATHER queue overflow 8 → 6, bad mode 9 → 1 (now bad mode or size), done early 10 → 8. Condition 7 is renamed **read-port error** (2026-10-26): an overflow or a misaligned read (4.3). Read-only: a store is a bus error. Cleared only on entering HALT. Non-zero raises the tile's error line (6.6).

### 8.4 The send port: `ROUTER_SET_HDR`, `ROUTER_SET_W0`–`W2`, `ROUTER_SEND_32`, `_64`, `_128` (0x80C–0x824, write-only)

| register | bits | function |
|---|---|---|
| `ROUTER_SET_HDR` | dst [6:0], mode [10:7], addr [31:11] | for outgoing packets; sticky; addr auto-increments at each send: +1, +2 or +4 for modes 3, 6 and 9 (by the size sent), +1 for mode 7 (4.11) |
| `ROUTER_SET_W0` | [31:0] | the payload's bits 31–0; sticky until the next send, then cleared to 0 |
| `ROUTER_SET_W1` | [31:0] | the payload's bits 63–32; sticky until the next send, then cleared to 0 |
| `ROUTER_SET_W2` | [31:0] | the payload's bits 95–64; sticky until the next send, then cleared to 0 |
| `ROUTER_SEND_32` | [31:0] | the payload's bits 31–0; sends a 32-bit packet (size 00) |
| `ROUTER_SEND_64` | [31:0] | the payload's bits 63–32; sends a 64-bit packet (size 01) with `ROUTER_SET_W0` as bits 31–0 |
| `ROUTER_SEND_128` | [31:0] | the payload's bits 127–96; sends a 128-bit packet (size 10) with `ROUTER_SET_W0`–`W2` as bits 95–0 |

A send store puts the packet in the send holding register, stalling if it is occupied and not being injected that cycle (4.11). Payload words beyond the size are sent as 0, `W` registers the size doesn't cover are ignored, and all three `W` registers clear at every send.

### 8.5 `DMA_CONFIG` (0x828, write-only; reads 0)

One 32-bit register configures the tile for an engine run, including the run itself.
- It is **written once per engine run**, before the start, and not updated while the run is running or the phase's arrivals are still landing: the write engine and the GATHER engine read the digits and *k* as packets arrive. A one-command local phase rewrites it between passes.
- It **contains no action bits;** those are in `TILE_PULSE`.

| field | bits |
|---|---|
| run kind: 0 stream run, 1 group run (4.2) | [0] |
| run start: first stream (0–255) or first `GROUP` entry (0–63) | [8:1] |
| run count: streams (1–256) or groups (1–64). A count of 0 is a run with no entries: it ends at once (`rd_done`), with no requests. Stream or group numbers past the table's end wrap within the table (meshsim flags them) | [17:9] |
| bucket digit (mode 4): 0 none, 1–4 = key byte, 1 = least significant (4.15); 5–7 reserved, treated as 0 (meshsim flags them) | [20:18] |
| histogram digit (count-on-arrival): 0 off, 1–4 (4.15); 5–7 reserved, treated as 0 (meshsim flags them) | [23:21] |
| gather value size *k* − 1, in records, *k* = 1–8 (4.4) | [26:24] |
| spare | [31:27] |

### 8.6 `TILE_PULSE` (0x808, write-only; reads 0)

Writing 1 to a bit fires that action for one cycle; writing 0 does nothing. v1.0 called this register `DMA_PULSE`; it is renamed because it now carries more than engine pulses.

| bit | pulse |
|---|---|
| [0] | `done` (6.3); READY then fires automatically when a command has landed |
| [1] | start: load the run in `DMA_CONFIG` and begin it (4.2); ignored while a run is running |
| [2] | `PERIPH_RUN`, peripheral run: start the peripheral's work (4.17.2); for the replayer, empty its queues, load the checkpoint and run one pass (4.19.11). Ignored while `periph_running` is 1, and on a tile with no peripheral (meshsim flags both) |
| [3] | **GO** (owner, 2026-10-26): drives the global GO net where it is wired (C&C's, today, 6.3); in every other tile it is ignored and meshsim flags a 1 |
| [31:4] | spare |

In one store, start and run act before `done`. Every start is **start only**: no engine has a resume, and a start while its engine runs is ignored.

### 8.7 The down-counters and `DMA_RECORD_BASE_ADDRESS` (0x82C–0x83C)

| register | access | bits | function |
|---|---|---|---|
| `DMA_SRAM_REMAINING` | R/W | count [19:0] | down-counter for modes 2–5 (4.12) |
| `DMA_CM_REMAINING` | R/W | count [19:0] | down-counter for mode 6, read-port results included (4.12) |
| `DMA_REPLY_REMAINING` | R/W | count [19:0] | down-counter for mode 8 (4.12) |
| `DMA_RECORD_BASE_ADDRESS` | W | chunk address [20:0] | the run's record buffer: augmenting's landing buffer, ordinal = (landing chunk − base) / 2 (4.9); or a gather's value buffer, value address = base + 2*k*·ordinal (4.4). One base per run: no run uses both |

- **A counter write** of *N* **adds** *N* to the count, atomically and in step with arrivals; *N* > 0 arms the counter (clears counters done, `WAKE_EVENTS` [9], and the counter's own done event, [0]–[3], and sets their qualifiers); 0 does nothing; a sum past 2²⁰ − 1 wraps, and meshsim flags it (4.12). **A load** returns the count. The count returns to 0 only by counting down, at chip reset or on entering HALT. Bits [31:20] are spare.
- **`DMA_RECORD_BASE_ADDRESS`** holds for the run, like `DMA_CONFIG`. Bits [31:21] are spare.

### 8.8 The read port: `SRAM_RD_ADDR_QUEUE`, `SRAM_RD_DEST_ADDR` (0x840, 0x844)

| register | access | bits | function |
|---|---|---|---|
| `SRAM_RD_ADDR_QUEUE` | R/W | store: chunk address [20:0]; load: free entries [31:0] | a store queues a read of 1, 2 or 4 words at that chunk address, which must be aligned to the size (a misaligned read is dropped with the read-port-error condition); a load returns the free entries in the request queue, at least 64 when it is empty (4.3) |
| `SRAM_RD_DEST_ADDR` | W | destination [13:0], size [15:14], spare [31:16] | the read port's setup register: the packed controller-memory word of the next result (14 bits; upper bits 0 at today's sizes), advancing by the size in words per queued read and wrapping within 14 bits; the size, 00 = 32 bits, 01 = 64, 10 = 128, 11 reserved (4.3). Each result lands as one mode-6 packet, counted in `DMA_CM_REMAINING` |

### 8.9 The table window (0x2000–0x3FFF)

| table | window | entries | load | store |
|---|---|---|---|---|
| `HIST` | 0x2000–0x23FF | 256 words, `HIST[i]` at 0x2000 + 4·*i*; count [16:0] | the count | the count |
| `PTR` | 0x2400–0x27FF | 256 words, `PTR[i]` at 0x2400 + 4·*i*; chunk address [20:0] | bus error | the pointer |
| `STREAM_SRC` | 0x2800–0x2BFF | 256 words, `STREAM_SRC[i]` at 0x2800 + 4·*i*; source chunk [20:0] (4.2) | bus error | the word |
| `STREAM_DEST` | 0x2C00–0x2FFF | 256 words, `STREAM_DEST[i]` at 0x2C00 + 4·*i*; destination word [20:0], *d* [27:21] (4.2) | bus error | the word |
| `STREAM_COUNT` | 0x3000–0x33FF | 256 words, `STREAM_COUNT[i]` at 0x3000 + 4·*i*; count [17:0], mode [21:18], read size [23:22], gap after [26:24] (4.2) | bus error | the word |
| `GROUP` | 0x3400–0x35FF | 64 entries, `GROUP[g]` at 0x3400 + 8·*g* (low word, then high word; layout in 4.2) | bus error | one word |
| unmapped | 0x3600–0x3FFF | — | bus error | bus error |

**Access rules:** only `HIST` can be loaded; a load from `PTR`, a `STREAM` array or `GROUP` (which the controller only writes) is a bus error, as is any sub-word access in the window and any access to 0x3600–0x3FFF. v1.0 had `STREAM` as one 64-bit entry per stream at 0x2800 + 8·*i* and `GROUP` at 0x3000. When the controller may touch the tables: 4.10.

### 8.10 The wake registers and the request port: `WAKE_EVENTS`, `WAKE_MASK`, `WAKE_REQUIRED`, `WAKE_PEEK` (0x848, 0x850, 0x854, 0x84C); `REQUEST_SET_W0`–`W2` / `REQUEST_GET_W0`–`W2`, `REQUEST_SEND` / `REQUEST_RECEIVE` (0x858–0x864)

| register | access | bits | function |
|---|---|---|---|
| `WAKE_EVENTS` | R | [3:0] each down-counter done (`DMA_SRAM_`, `DMA_CM_`, `DMA_REPLY_`, `DMA_PERIPH_REMAINING`); [7:4] reserved, read 0; [8] GO; [9] counters done; [10] command landed; [11] engine run done; [12] request FIFO no longer full; [18:13] `PERIPH_SERVICE_1`–`6`, the peripheral's service lines (4.17.3); [19] `REQUEST_PENDING`; [20] sender idle; [21] UART RX not empty; [22] UART TX not full; [31:23] reserved for future units, read 0 | a load returns every pending event and consumes those that are 1 in `WAKE_MASK`: a sticky event clears, a service line is claimed (4.17.3), and `REQUEST_PENDING` is unaffected (4.18); an event set in the load's own cycle survives to the next load (4.12). A store is a bus error. Sticky events 0 at chip reset and after HALT → RUN; live events follow their conditions |
| `WAKE_MASK` | R/W | [22:0], laid out as `WAKE_EVENTS`; [31:23] reserved | the events that wake `WFI` on their own and that a load of `WAKE_EVENTS` consumes. Never written by hardware. [8] \| [9] (GO and counters done) at chip reset and after HALT → RUN, so [19]–[22] reset to 0 |
| `WAKE_REQUIRED` | R/W | [22:0], laid out as `WAKE_EVENTS`; [31:23] reserved | the events that wake `WFI` only when every one of them is pending. Plays no part in consuming. 0 at chip reset and after HALT → RUN |
| `WAKE_PEEK` | R | as `WAKE_EVENTS` | returns exactly what a load of `WAKE_EVENTS` would, and consumes, claims and credits nothing: an alias of the same state with no storage of its own. A store is a bus error |
| `REQUEST_SET_W0` (0x858) | W | [31:0] | the request's bits 31–0: requestor [6:0] (write 0; the tree overwrites it), target [9:7], and the target's bits [31:10] (3.3); sticky until the next send, then cleared to 0 |
| `REQUEST_SET_W1` (0x85C) | W | [31:0] | the request's bits 63–32; sticky until the next send, then cleared to 0 |
| `REQUEST_SET_W2` (0x860) | W | [31:0] | the request's bits 95–64; sticky until the next send, then cleared to 0 |
| `REQUEST_SEND` (0x864) | W | [31:0] | the request's bits 127–96; pushes the 128-bit request with `REQUEST_SET_W0`–`W2` as bits 95–0 |
| `REQUEST_GET_W0` (0x858) | R | [31:0] | the oldest received request's bits 31–0, with its requestor [6:0] and target [9:7]; doesn't pop; 0 when the receive queue is empty |
| `REQUEST_GET_W1` (0x85C) | R | [31:0] | its bits 63–32; doesn't pop; 0 when empty |
| `REQUEST_GET_W2` (0x860) | R | [31:0] | its bits 95–64; doesn't pop; 0 when empty |
| `REQUEST_RECEIVE` (0x864) | R | [31:0] | its bits 127–96, and **pops** the request. With the receive queue empty it returns 0, pops nothing and sets receive underflow (`TILE_CONDITIONS` [14]) |

**The wake term** (4.12): `wake = gate ? WAKE_EVENTS[8] : ((WAKE_EVENTS & WAKE_MASK & ~WAKE_REQUIRED) ≠ 0) OR (WAKE_REQUIRED ≠ 0 AND (WAKE_EVENTS & WAKE_REQUIRED) == WAKE_REQUIRED)`, where `gate` is the `done` gate, 1 from the `done` pulse until GO. It is the only way `WFI` completes, and a wake consumes nothing.

**The request port** (4.18). Send and receive share the four offsets: a store sends, a load receives. A `REQUEST_SEND` store pushes the request into the request FIFO, stalling only if the FIFO is full and no entry is freed in that cycle; `request_busy` is `TILE_STATUS` [11], and its fall posts `WAKE_EVENTS` [12]. All three `SET` registers clear at every send. A received request is read in ascending order, `REQUEST_GET_W0`–`W2` then `REQUEST_RECEIVE`, which pops it; `REQUEST_PENDING` ([19]) is up while one is waiting. On tiles 0–63 the receive side is unconnected: the `GET` registers read 0, [19] never rises, and a `REQUEST_RECEIVE` load is a receive underflow. The hardware's only fields are requestor [6:0] and target [9:7]; the rest of a request is its target's (3.3). v1.1's `REQUEST_SEND_32`, `_64` and `_128`, and its 64-bit request layout, are withdrawn.

### 8.11 The peripheral registers: `DMA_PERIPH_REMAINING` (0x83C), `PERIPH_STATUS_0`–`7` (0x870–0x88C)

| register | offset | access | bits | function | reset |
|---|---|---|---|---|---|
| `DMA_PERIPH_REMAINING` | 0x83C | R/W | count [19:0] | down-counter for mode 9 from any source, decremented when each lands or is dropped (4.12, 4.17.5); nothing counts in HALT. v1.1's `DMA_REPLAYER_REMAINING` | 0 |
| `PERIPH_STATUS_0` … `PERIPH_STATUS_7` | 0x870 + 4·*i* | R | [31:0] | the peripheral's status, meanings its section defines; no side effects; 0 where the peripheral defines none, and on a tile with no peripheral (4.17.4) | per peripheral; 0 when idle |

`DMA_PERIPH_REMAINING` behaves like the other counters (8.7): **a write of *N* adds *N*** to the count, atomically and in step with arrivals; *N* > 0 arms it (clears counters done, `WAKE_EVENTS` [9], and its own done event, [3], and sets their qualifiers), 0 does nothing, and a sum past 2²⁰ − 1 wraps (meshsim flags it). The count returns to 0 only by counting down, at chip reset or on entering HALT (4.12). A load returns the count, and bits [31:20] are spare. The status registers are read-only: a store is a bus error, and a load returns the state at its sample point (4.11).

**The replayer's use** (4.19.9):

| register | bits | function | reset |
|---|---|---|---|
| `PERIPH_STATUS_0` | rows run [15:0], next offset [31:16] | rows run: rows of this pass completed, modulo 2¹⁶, so 0 again after a full pass; rows left = 65,536 − rows run while `periph_running` is 1. Next offset: the row within the current expansion where the next row starts (0 to 3,120), where the controller begins sending a controller-fed expansion's words. v1.1's `REPLAYER_STATUS` | 0 |
| `PERIPH_STATUS_1` | [31:0] | the instruction awaiting a controller-fed expansion: the stored opcode-stream word, bit 0 clear for a compressed instruction (4.19.2). Set when the new-expansion line (`PERIPH_SERVICE_5`, [17]) is raised and held until the next: the designated register of the replayer's presented line (4.17.3). v1.1's `REPLAYER_OPCODE` | 0 |
| `PERIPH_STATUS_2`–`7` | — | not used; read 0 | 0 |

*(Proposed, item 53: the rows-run field read modulo 2¹⁶.)* **The replayer's service lines** are `WAKE_EVENTS` [18:13] (8.10): [13] code low-water, [14] read-log low-water, [15] write-log low-water, [16] expansion low-water, [17] new expansion, [18] done. Their meaning, and the unit of service a claim takes, are in 4.19.8 and 4.19.9.

### 8.12 The sender: `SENDER_SEND`, `SENDER_ADDR` (0x868, 0x86C, write-only)

| register | bits | function |
|---|---|---|
| `SENDER_SEND` | dest [6:0], mode [10:7], output queue [12:11], word count [28:13], spare [31:29] | **a store starts one send** (4.17.9): word count 32-bit words from the peripheral's output queue to dest (a 7-bit endpoint id), as 128-bit packets and then a 64-bit, a 32-bit, or a 64-bit and a 32-bit packet for the remainder, every packet stamped with the mode. Ignored while `sender_busy` (`TILE_STATUS` [4]) is 1, and on a tile whose peripheral has no output or no such queue (meshsim flags each). Reads 0 |
| `SENDER_ADDR` | word address [20:0], spare [31:21] | the first packet's addr for modes 3, 6 and 9; sticky, advancing by each packet's size in words (+4, +2, +1); other modes send addr 0. A configuration register: undefined at chip reset, kept across HALT → RUN. Reads 0 |

A send's end posts sender idle, `WAKE_EVENTS` [20], as `sender_busy` falls. A store to `SENDER_SEND` never stalls. On tiles 0–63, whose replayer has no output, both registers exist and nothing is attached.


### 8.13 The UART: `UART_TX`, `UART_RX`, `UART_STATUS`, `UART_DIVISOR` (0x890–0x89C)

| register | access | bits | function |
|---|---|---|---|
| `UART_TX` | W | [7:0] the byte; [31:8] spare | pushed into the TX FIFO; **the store stalls while the FIFO is full**. On an unwired tile, dropped and never stalls. Reads 0 |
| `UART_RX` | R | [7:0] the oldest received byte; [8] valid; [31:9] 0 | pops the byte only when valid; with the RX FIFO empty it returns valid = 0 and pops nothing. On an unwired tile, always valid = 0. A store is a bus error |
| `UART_STATUS` | R/W | [4:0] TX occupancy (0–16); [12:8] RX occupancy; [16] RX overrun; [17] framing error; others spare | reads have no side effects; [16] and [17] are sticky and **cleared by writing 1** to them. On an unwired tile reads empty FIFOs and no errors, and writes are ignored |
| `UART_DIVISOR` | R/W | [15:0] reference-oscillator cycles per bit; [31:16] spare | takes effect at the next idle point of each direction. Reset: round(`f_ref` / 57,600), 57,600 baud. On an unwired tile writes are ignored and a read returns the reset value |

The UART keeps running in HALT; chip reset empties its FIFOs, clears [17:16] and loads the reset divisor (4.20).


---

## 9. Invariants (to be checked by the simulator)

1. **No FIFO overflow:** router FIFOs including tile 0's C&C FIFOs (4.7, 4.16; the loopback FIFO cannot overflow by design, 4.5), the write engine's input FIFO (4.9), and the `PTR` and `HIST` request FIFOs (4.10).
2. **At most one access per big-SRAM area per cycle,** and no buffer is both read and written within an engine run (4.1). An access that spans two rows counts as one access here (4.1).
3. **The timetable holds:** every rotation starts on its scheduled cycle and never earlier; table read-ahead waits are part of the timetable, identical on every tile (4.2). A tile that falls behind because the SRAM read queue refused a request sets **drift** (4.13).
4. **Every expected arrival lands first** (an uncounted command write sitting in port B is ignored, since commands may cross the mesh mid-phase): a tile's counters are all done (with `mem_idle` and `cm_idle`) before its controller pulses `done` to end a phase.
5. **No bad packets:** no bad packet (mode 0 or 10–15, mode 5 at 128 bits, mode 3 with an addr not aligned to its size, mode 7 above 32 bits or mode 8 at 128 bits, 3.1) is ever sent; the director drops one, uncounted, and sets the bad-mode-or-size condition. No packet has size 11 (undefined; meshsim flags it).
6. **No `HIST` count exceeds 17 bits.**
7. **No packet is in flight when the RUN/HALT line changes** [built].
8. **Overwritten-unread results are counted, not failed:** mode-8 words and read-port results overwritten before the program reads them are a reported statistic, per phase. Exchanges legitimately land words a program ignores. A strict mode (meshsim's `Config.strictReplies`) makes them violations for tests that want the guarantee. Separately, no `SRAM_RD_ADDR_QUEUE` store may find the queue full.
9. **No unexpected arrival:** no packet arrives for a down-counter already at 0.
10. **Gathers are local:** a mode-1 packet is only ever sent to the tile itself.
11. **Mode 6 stays in controller memory, outside both blocks:** no word of a mode-6 packet (1, 2 or 4 words by its size), read-port results included, falls beyond instruction and data memory, or inside the command block or the reply block (4.11, 4.12).
12. *(Withdrawn, 2026-10-26: replies land by source in the reply block, so a reply can no longer address the command block.)*
13. *(Withdrawn, 2026-10-26: replies land by source, so a reply can no longer address a word outside data memory.)*
    Counted data reaches a running tile only when the tile expects it (4.12), and no command write is wider than 32 bits nor any reply 128 bits (invariant 5).
14. **Read-port results and incoming traffic do not overlap** (4.3): no packet from outside the tile, other than C&C's command writes (mode 7), arrives while the tile has read-port results outstanding (issued and not yet accepted by the traffic director). Checked as actual overlap in time [built: meshsim's `Config.checkReadPortOverlap`, on by default].
15. **No conditions:** every tile's `TILE_CONDITIONS` stays 0 (4.13). A program error that the hardware can see shows up here, and on C&C's error bit.
16. **The peripheral slot's and the replayer's invariants** (4.17, 4.19):
    - the slot's: no bad peripheral write (condition 11); no run while `periph_running` is 1, and none on a tile with no peripheral; no `SENDER_SEND` store while `sender_busy` is 1, or on a tile without peripheral output; and a mode-9 packet's landing `rp_latency` after the director (4.17.5, 4.17.8). Mode 9 may come from any source (4.17.5): v1.1's "only the tile itself and C&C" is withdrawn;
    - the replayer's: no queue overrun and no invalid expansion (conditions 12 and 13); no other tile sends the tile mode 9 during a pass (4.19.13); no LPDDR read targets a replayer queue (its overflow goes through big SRAM, 4.19.13); every opcode word and log word a pass is given is consumed in order, and a pass consumes exactly its tile's log; every queue push, whatever its source, has landed before a run (4.19.8); no zero mask reaches `VirtualSRL`, `VirtualSRLI`, `VirtualSRA` or `VirtualSRAI`, and no mask that is zero or not contiguous reaches `VirtualPext` or `VirtualPextSigned` (4.19.7; the results are undefined, owner, 2026-10-26); and the replayer's rows equal jolt-cpp's (the gold standard for now; upstream Jolt at 735355e where a difference has been confirmed by running both; the validation harness, item 58; the vectors of `spec/replayer-opcodes.md` section 9; item 113).
17. **The request contract is kept** (2.1, 3.3, 4.2, 6.7; v1.1's tag clauses withdrawn, owner, 2026-10-26):
    - a request's data and reply arrive only when the requesting tile has armed its counters for them (4.12);
    - at most one reply-producing request is outstanding per responder, and its reply is read before the next arrives;
    - a mode-3 packet reaches C&C only from the tile holding the write token, after the token, within its block, each chunk of the block exactly once;
    - no read is outstanding and no write token is pending while a group run is running on any tile;
    - no stream has *d* = 65–127, or *d* = 64 with a mode other than 3, and no packet is sent to 65–125;
    - every request names a target with a receiver, and every request reaching a receiver is one it can serve (3.3, 6.7, errors);
    - no load of `REQUEST_RECEIVE` finds the receive queue empty (4.18; condition 14).
18. **One source per order-placed destination** (4.1, 4.5, 4.9, 4.17.5): a tile never has two clients (the address generator, the GATHER engine and the send port, item 80) sending order-placed packets to the same destination at once: modes 2, 4 and 5 into the same `PTR` entry of the same receiver, or mode 9 into the same order-placed port of a peripheral (a replayer queue). Big SRAM orders reads only within a client, so such a mix would interleave unpredictably. **And a peripheral's order-placed port takes one source at a time** (owner, 2026-10-26): since mode 9 may come from any source, no two tiles, nor a tile and a non-tile endpoint, feed the same queue port at once. meshsim flags overlapping use in time.

---

## 10. Parameter summary

*Figure 8.1.*

| parameter | default |
|---|---|
| link word | 169 bits: src 7, dst 7, mode 4, addr 21, size 2, payload 128 (3.1); 102 in v1.0 |
| `link_latency` | 3 on every link direction; a hop is `link_latency` + 1 = 4 cycles (3.2) |
| start skew between tiles | 0 or +1 cycle (3.2) |
| `STREAM`, `GROUP` | `STREAM`: 256 entries in three 256 × 32-bit arrays (`STREAM_SRC`, `STREAM_DEST`, `STREAM_COUNT`); `GROUP`: 64 entries of 64 bits (4.2) |
| GATHER request queue | 16 (4.4) |
| GATHER value size *k* | 1–8 records (4.4) |
| digit-all-to-all group gap | 0; 4 for the two-hop layers (4.2) |
| `mem_chunks` | 1,048,576 (4 MB: 8 areas × 128 bits × 32K); any multiple of 65,536 chunks (256 KB) up to 2,097,152 (8 MB), with areas of at most 512 KB (4.1) |
| area request-queue depth | 8 in meshsim; *(Open)* for the hardware (4.1) |
| area tie order | writes first (4.1) |
| big-SRAM array read latency | 2 in meshsim; *(Open)* for the hardware (4.1) |
| big-SRAM read latency | queueing plus the array's read latency (4.1) |
| tokens and return queues | 64 tokens, a shared pool; in-order return queues of per-read metadata for the address generator and the GATHER engine, big SRAM returning each client's reads in its issue order (4.1, 4.5) |
| `queue_depth` (router FIFOs) | 16; the loopback FIFO at least the token count, 64 *(Proposed)* (4.7) |
| `intra_tile_latency` | 1 per stage |
| `ts_engine_latency` | 1 (1–4 measured) |
| `ag_lookahead` | 2·`ts_engine_latency` + 2 entries in each of the address generator's two read-ahead queues: 4 at the default; exactly enough for groups of ≥ 2 cycles; back-to-back one-visit groups would need 4·`ts_engine_latency` + 3, should one-word exchanges ever become group runs [meas: meshsim] (4.2) |
| address-generator lead-in | first request 4·`ts_engine_latency` + 3 (group run) or 2·`ts_engine_latency` + 2 (stream run) cycles after the start store; 4·`ts_engine_latency` + 2 or 2·`ts_engine_latency` + 1 after the cycle the start takes effect (4.2) |
| pointer stage depth | 2·`ts_engine_latency` + 2 (4 at the default) (4.9) |
| write-engine input FIFO | 4 (4.9) |
| `PTR`, `HIST` request FIFOs | 16 each (4.10) |
| controller read port | request queue of at least 64 entries of 37 bits (address, destination, size); 64 in v1.0; results as mode-6 packets of 32, 64 or 128 bits (4.3) |
| controller memories | 8 KB instruction + 8 KB data, each four 512 × 32-bit banks interleaved by word (4.11) |
| `cm_latency` (director to port B) | 2 [built] |
| `stage_delay` | 32 cycles *(Proposed)*: the minimum time from C&C's GO store to its first packet of announced staging, about 3× the receiving start path's arming time (6.2) |
| `reg_load_latency` | 1 or 2 per register, implementation-defined and fixed (the same on every access and tile); covers the register block and the table window; meshsim's reference setting 2 (4.11) |
| `ready_latency` | 1 in simulation (the GO net's); the physical lines cross the die, more than 1 (8 used for estimates); the physical value is *(Open)* |
| `go_latency` | 1 in simulation; the GO net crosses the die like the READY lines, so physically more than 1 *(Open)*; C&C waits `go_latency` + 1 cycles after each GO before sending (6.6) |
| `run_latency` | as GO |
| C&C receive FIFO | at least 64 entries (6.6) |
| load-use penalty (data memory, registers, table window) | 0 each (the design target; section 12) |
| taken-branch penalty, multiply latency | 1, 3 |
| request bus | a 16-bit, 8-ary tree: 8 row nodes and a root (with an input per sending controller), a 16-byte buffer per node input, one 16-bit beat per cycle per hop, 8 beats per request; no queues at the root, one credit counter and one forwarding link per target; a 16 × 128-bit receive queue in every tile; `reqbus_latency` ≈ 15–20 cycles [estimate], 17 unloaded [meas: meshsim] (3.3) |
| request FIFO | 2 KB per tile, 128 × 128 bits (4.18); the receive queue 16 × 128 bits per tile |
| wake events | 19 of 32 bits used: [3:0] each down-counter done, [12:8] GO, counters done, command landed, engine run done, request FIFO no longer full, [18:13] the peripheral's six service lines, [19] `REQUEST_PENDING`, [20] sender idle, [21] UART RX not empty, [22] UART TX not full; [7:4] and [31:23] reserved (4.12) |
| UART | every tile; 8N1; 16-byte TX and RX FIFOs *(Proposed)*; divisor in reference-oscillator cycles per bit, reset round(`f_ref` / 57,600); `f_ref` *(param, Open)* (4.20) |
| command and reply blocks | command block 16 words at the top of data memory (slot 0 the command word, slots 1–15 arguments; argument blocks 1–7 and 8–15 by parity); reply block 256 words below it, 2 per source (4.12, 6.3) |
| packed controller-memory word | up to 14 bits; 12 used at today's 8 KB + 8 KB (4.11) |
| requests | 128 bits; the hardware's fields requestor [6:0] and target [9:7], 8 targets; the rest laid out by each target (3.3); memory answers in 128-byte units (6.7) |
| eject priority | the loopback FIFO is served only in a cycle when every other eject FIFO is empty; the others round-robin (4.7) |
| peripheral slot (4.17) | one per tile; type in `TILE_STATUS` [15:12]; six service lines; eight status registers; `rp_latency` 2 for every peripheral *(Proposed)*; the sender: up to 4 output queues, one send at a time |
| replayer (4.19), peripheral type 1 | a pass of 65,536 rows; code, expansion, read-log and write-log queues of 256, 256, 512 and 512 × 128 bits (halves 128, 128, 256, 256); three LRU decompressors of 16 × 64 bits; ROM 512 × 128 bits; decode table 2K × 16 bits; register file 128 × 64 bits; output queue 32 rows of 640 bits; rows of 3 or 5 cycles; start copy 64 cycles; `rp_latency` 2 *(Proposed)*; about 41 KB of memory per tile |

---

## 11. Logic budget (per tile, 12 nm estimates)

The SRAM is ~75 % of the die; the remaining ~25 % is about 1.5 mm² per tile for everything else. Every unit is charged here, and additions must fit. Estimates are rough (±50 %) and exclude the big SRAM areas themselves. The lines are rough estimates [estimate], not yet reviewed; v1.1 scaled v1.0's lines for the 169-bit link word, the 128-bit write path, the three `STREAM` arrays and the eight controller-memory banks.

| unit | state | estimate |
|---|---|---|
| link FIFOs (4 outputs × 2–4 FIFOs + eject × 5 = 17 FIFOs of 16 × 169 bits, ~46.0 Kbit of flops), route blocks, selectors, traffic director; padding stages on short links (169 flops each) | | ≈ 0.08 mm² (0.05 at v1.0's 102 bits) |
| big-SRAM area queues: 8 request queues (depth open; ~165 bits per entry with 128-bit write data, ~100 in v1.0), per-chunk write enables, routing from the clients to the areas and back; returning each client's reads in its issue order (4.1) takes over v1.0's token FIFO's 64 × 128-bit data slots (~8 Kbit), so the cost moves rather than grows | depends on depth | ~0.015–0.03 mm² |
| address generator: read-ahead queues (`ag_lookahead` × ~100 bits: a `STREAM` and a `GROUP` entry), a rotation or element counter (18 bits) and its comparator, the element-address shift-add (21 bits), its return queue (up to 64 entries of metadata), relative-destination adder, run state | | ~0.011 mm² |
| controller read port: 64-entry request queue (37 bits each: address, destination, size), the setup register's counter, read-queue client | ~2.4 Kbit | ~0.004 mm² |
| GATHER engine: 16-entry request queue (× 64 bits), address multiply-add (*k* ≤ 8: a shift-add), per-value counter | ~1 Kbit | ~0.004 mm² |
| DMA write engine: input FIFO, pointer stage (4 × ~150 bits: a 128-bit payload, its size and the pointer index), augment and count logic | | ~0.007 mm² |
| table memories: `STREAM` (three 256 × 32 macros, 3 KB) and `GROUP` (512 B) macros; `PTR` (256 × 21) and `HIST` (256 × 17) macros with request FIFOs, RMW pipelines and bypass | ~33 Kbit | ~0.017 mm² |
| controller: RV32I ~20k gates + Zmmul ~5–10k gates + `WFI`; 8 KB + 8 KB as eight 512 × 32 dual-port macros (four 1K × 32 in v1.0); the peripheral registers (17, plus the 3 wake registers with storage and the `WAKE_PEEK` alias, the peripheral counter and the eight status-register read paths, the request port's four store and four load offsets, and the sender's two); the four down-counters (one is the peripheral's) with their add ports and per-counter done events | | ~0.07 mm² |
| loopback FIFO deepened from 16 to 64 entries *(Proposed)* (4.7): 48 more 169-bit entries, as flops or a small two-port macro | ~8 Kbit | ~0.01 mm² |
| request port (4.18): the 2 KB request FIFO (a 128 × 128 macro), the `SET` registers, the 16-bit serializer; and the receive side's 16 × 128-bit queue (2 Kbit) with its read path | ~18 Kbit | ~0.009 mm² |
| the UART (4.20): two 16-byte FIFOs, divisor, shifters | ~0.3 Kbit | ~0.002 mm² |
| the sender (4.17.9): `SENDER_ADDR` and the send's counter, the output-queue select and the packet former; unattached on tiles 0–63 | ~0.1 Kbit | ~0.003 mm² |
| RUN/HALT, `done`, "command landed", READY, GO, the `done` gate and the counters-done qualifier, flags | | 0.005 mm² |
| replayer (4.19.14), the slot's peripheral on tiles 0–63: ALU and comparator, 128 × 64-bit true dual-port register file, two adders, operand resolver, decode index, four queue readers and credit counters, three decompressors; with its 41 KB of memories (queues, ROM, decode table, image, output queue) | ~330 Kbit of SRAM | ~0.11–0.16 mm² |
| **total per tile** | | **≈ 0.38–0.45 mm²** with the replayer and the fifth port, about 25–30 % of the per-tile logic budget; ≈ 0.28–0.30 mm² without the replayer (before the fifth port and the receive queue, ≈ 0.35–0.40 and ≈ 0.25; before the sender, ≈ 0.34–0.39 and ≈ 0.24) (≈ 0.22 mm² before the request port and the deeper loopback FIFO; ≈ 0.17 mm² in v1.0) |
| fifth router port (4.16), in every tile since 2026-10-26: a sixth route block, ten 16 × 169-bit FIFOs + wider selectors; connected only at tile 0 | ~27 Kbit of flops | 0.03–0.05 mm² |
| once per die: C&C controller (6.6) | | ~0.04 mm² |
| once per die: the request bus's nine nodes (a 16-byte buffer per input, ~10 Kbit with the controllers' root inputs), their arbiters and the root's eight credit counters, plus the tree's wiring (18 wires per hop: 16 data, valid, ready) and a forwarding link per target (3.3) | ~10 Kbit | ~0.01 mm² plus wiring |

Rule of thumb: tables go in SRAM (cheap); comparators, muxes and per-entry flop state are what spend the budget. The tile is dominated by the big SRAM: at 0.75–0.9 MB/mm², 4 MB ≈ 4.5–5.3 mm². With the logic and link channels, a tile is roughly 6–7 mm² at 4 MB. The areas' density at one access per cycle awaits memory-compiler numbers (section 13).

---

## 12. Physical design: the control island

*Figures 1.3, 3.1.*

All the tile's small memories and its registers cluster around the controller. The engines and the router sit between this **control island** and the big SRAM and the links.

- **In the island:**
  - the controller;
  - its eight 512 × 32 dual-port memory banks (8 KB instruction, 8 KB data, with the command and reply blocks);
  - the table memories (`HIST`, `PTR`, the three `STREAM` arrays, `GROUP`);
  - the peripheral registers.

  Rough 12 nm sizes: ~0.017 mm² for the table memories, ~0.007 mm² per 2 KB dual-port bank (~0.055 mm² for eight), ~0.01 mm² of controller logic. The island is **~0.1 mm², a patch ~0.3 mm across** [estimate; ~0.09 in v1.0]. Wires inside it cost a few tens of ps, so the controller's one-cycle loads of all of these are realistic at 1 GHz.
- **The table memories go by the controller, not the engines.** The engines' table traffic (pointer and counter read-modify-writes with bypass, the `GROUP`/`STREAM` read-ahead) is pipelined and tolerates latency; the controller's loads do not. The engines pay the distance (`ts_engine_latency`, 4.10).
- **Configuration registers** live in the island although the engines use them. They are static during a phase, so the long wires are multi-cycle paths (or the engines keep local copies).
- **Status bits** are registered at their source in each unit and cross to the island with one flop, so status reads one cycle old.
- **Not in the island:**
  - everything the engines touch every cycle other than the table memories: the address generator's read-ahead queues and return queue, the pointer stage, the area queues, the router queues;
  - the GATHER engine and the read port's request queue, which sit by the big SRAM.
- **The request port** sits at the island's edge: its registers in the island, its FIFO beside them, and its 16-bit link leaving for its row node, which sits in the channels of its row of tiles; the root node and its credit counters sit by C&C. Each tile's receive queue sits by its request port; the root's per-target links go with the receivers' placement and the endpoint map (item 82). The tree's placement and `reqbus_latency` are *(Open)* (section 13).
- **The wake events** are registers in the island; the units' event pulses cross like status bits, with one flop.
- **Paths that cross the gap:** engine table accesses, and the traffic director's port-B writes (modes 6–8, `cm_latency`); the peripheral (the replayer) sits beside the traffic director, outside the island, with its memories as SRAM macros, and mode 9's path to it is pipelined over `rp_latency` *(Proposed, item 48; the replayer's placement against the materializer is open with item 59)*; its status registers and service lines cross to the island like status bits. The sender sits by the peripheral's output queues. Both are pipelined and tolerate latency, so the island costs no throughput.
- **Big SRAM:** at the default geometry (4.1), eight 512 KB areas (128 bits × 32K each) around the logic, with the link channels between them. Their arrangement awaits the compiler numbers.

---

## 13. Open items

Every item still to be settled, and every proposal not yet ruled on, with the section it affects. Possible later additions are improvements considered and not part of v1.1. Items 1–22 are v1.0's numbering; v1.1's new items start at 23, the request bus's at 60, the wake-event and read-order pass's at 76, the request-port and peripheral-slot pass's at 82, the modes-and-counting pass's at 90, part D's at 102, part E's at 109, part F's at 112, part G's at 113 (extended by part H), and part M's at 116.

**Open: to be settled for the hardware**

1. **Big-SRAM areas** (4.1): memory-compiler numbers for a 128 × 32K area macro at 1 GHz with one access per cycle, at the sign-off corner; the array's read latency; the areas' density (section 11) and arrangement (section 12).
2. **The area request-queue depth** for the hardware (8 in meshsim), and so the area queues' cost (section 11).
3. **Write combining** (4.1, 4.9): v1.1 writes a 128-bit value as one packet and one row access, which was the main case. Whether consecutive 32- or 64-bit writes to the same row are still combined into one access before the queue is open.
4. **Queueing against the timetable** (4.1, 4.2): the 64 tokens and the return queues must absorb the area queueing a phase's placement allows; the bound, and the placement rules that keep each phase's traffic per area within one access per cycle.
5. **The `PTR` and `HIST` macros** (4.10): the write must land intact when port A reads the address port B is writing.
6. **The GATHER request queue under load** (4.4): the in-flight bound under a heavier SRAM-read-queue load from the tile's other clients. Local sources alone peak at 0 of 16 at *k* = 8 [meas: meshsim]; overflow is a hardware-fault detector (4.13).
7. **Gapless one-word exchanges** (4.2): they run with gap 4 on every stream. Simulate gapless stream runs in schedule order, confirm the link-queue peaks at window boundaries, and tighten if the queues stay low.
8. **The physical `ready_latency`** (section 10).
9. **The memory subsystem's attachment** (section 7). What a tile sees of it is now specified (6.7); its side is item 60.
10. **Above the chip:** the cards, the card and controller FPGAs, their links, and each chip's DDR4 and how it attaches are not specified (figure 1.1 draws them as assumptions; only 6.6 mentions an FPGA).
11. **C&C's register block** (6.6): names and layout of the send, READY, GO-pulse, RUN/HALT, receive and host-interface registers, the watchdog register and the wake events.
12. **The boot document** (4.11, 6.2): the loader's interface and the image layout.
13. **The output format** of the sort (section 14): the final keys carry the receive-buffer ordinal in bits 63–32; bare keys or (key, value) records need another pass.

**Proposed, awaiting a ruling**

14. **C&C's wake sources:** all READY bits set, a packet on the receive path, and a watchdog timer (6.6).
15. **A status bit with READY,** so C&C can learn per-owner completion without its receive path.
16. **128-bit gather-value reads** (4.4): read a gather value as one row when it is row-aligned. In v1.0 this would have halved the reads but not the injection cost; in v1.1 a 128-bit value is one packet, so it would halve both, bringing a gather at *k* = 2 from ≈ 3 to ≈ 2 cycles per value [estimate].

**Possible later additions**

17. **A direct path from the GATHER engine to the write side.** The two gathers cost ≈ 393K of ≈ 1.37M, injection-bound at *k* + 1 cycles per value. A direct path would bring a gather to ≈ 2 cycles per value, or ≈ 1 with write combining: 130–260K per sort [estimate]. The biggest hardware lever found.
18. **Fusing the local gather with the values all-to-all** (the GATHER engine firing gather values straight at each owner). It would save about *n*·(*k* + 1) cycles, ≈ 196K per tile at *k* = 2 [estimate]. But each visit would have to cost a fixed *k* + 1 cycles to keep the timetable exact, and the 16-group schedule's periods and link loads would have to be redone for *k* packets per visit.
19. **An arrival-order form of augmenting for mode 4** (4.9), for streams from the tile to itself, where arrival order is the input order. It would let step 1's augmenting fold into the local sort's first scatter pass, saving one pass (≈ 64K cycles) [estimate], but only together with counting digit 1 during the input load, since step 1's augmenting pass is also what counts digit 1 for the first bucket pass.
20. **A search engine:** a big-SRAM read client running up to 64 binary searches over sorted records, round-robin, with a 64-entry context table, one comparator and an adder (~5 Kbit, ≈ 0.005 mm² [estimate]). It would make the split faster: ≈ 43–67K cycles on uniform [model], against the software split's ≈ 203K through the controller read port (4.3).
21. **A port-B reader** for bulk transfers from controller memory to big SRAM (or another tile). Today the only path is the send port, mode 2 to `PTR[me]`, at ≈ 2–3 cycles per 64 bits (v1.0 [estimate]; v1.1's 128-bit packets save a little). A reader on port B would stream up to 128 bits per cycle into the injection point. The traffic director keeps priority on port B, because its writes (commands, replies, copies) can't wait; the reader takes port B only on cycles with no director write. It is a priority mux, not a design yet; add it only if a bulk case becomes critical.
22. **Whole-row fetches** for 64-bit streams (4.2): fetching whole rows into holding registers would halve the source area's accesses.

**v1.1: open, to be settled**

23. **The replayer** (4.19): specified in v1.1's replayer pass, following the design notes and their allocation table. It is now peripheral type 1 in the tile's peripheral slot (4.17, item 86). What remains is in items 33–59: the open ones are the tracer engine's run log (33), the template enumeration (34), the ROM's fit (40), the validation harness (58) and the consumer's interface (59); the rest are proposals.
24. **Re-measurement** (all sections): every number labelled *v1.0 measurement; to be re-measured in v1.1*, chiefly the fan-in table (3.2), the read-port tolerances (4.3), the queue peaks and schedule comparison of the all-to-alls (4.2) and the sort's results (section 14), once meshsim models the 169-bit link word.
25. **The `STREAM` count's width** (4.2). `STREAM_COUNT` keeps 18 bits. In the sort and the skew planner every stream lies inside one buffer of at most 65,536 elements: an area of 64-bit records, or two areas of 128-bit values (`sort/program/sw/sort.h`). Every count written (`local.c`, `transfer.c`, `config.c`, the planner's `write_stream`) is at most the keys per tile, 65,536 at 64 × 65,536, and the overlay copies are far smaller. So **17 bits (up to 131,071) would serve the sort, 128-bit streams included, with a factor of two to spare.** But the field is in elements: 17 bits would cap a 128-bit stream at half the big SRAM and a 32- or 64-bit stream lower still, and 18 bits already falls one element short of a whole-memory 128-bit stream (2¹⁸ rows). `STREAM_COUNT` has 5 spare bits, so narrowing it frees nothing needed today. No change proposed.

**v1.1: proposed, awaiting a ruling**

26. **Fixed payload bits** (3.1): superseded (owner, 2026-10-26): only mode 1 reads fixed bits ([51:32]) at any size; a command write is 32 bits only and a reply 32 or 64 bits, other sizes bad packets (item 93).
27. **Mixed read sizes within a group** (4.2): every visit now takes one cycle, so v1.0's one-size-per-group rule no longer sets the period, and dropping it costs nothing. meshsim's mixed-size report would go.
28. **128-bit staging from C&C** (6.2): one row per packet halves v1.0's staging traffic; images would start on a row and be padded to whole rows, a change to the boot document's image layout (item 12).
29. **C&C's receive FIFO** (6.6) holds the full 128-bit payload and its size, so a result or REPORT reply may use a whole packet; the register pair becomes the payload's words plus src, size and valid, part of item 11.
30. **The replayer wake and the `done` gate** (4.12, 4.19): superseded. All wakes are events (owner, 2026-10-23): the replayer's events wake `WFI` through the one wake term (4.12, 8.10), and from the `done` pulse until GO the `done` gate overrides that term to GO alone, writing no register. The replayer's controller loop never runs inside the `done` window.
31. **The interim replayer hooks** (4.19): superseded by the full replayer (4.19). meshsim's stub (a mode-9 packet dropped, the replayer bits 0) is replaced by the model the handoff describes.
32. **What the hooks require of the replayer** (4.19): kept, generalised to every peripheral (4.17.8) and met by 4.19.11: HALT holds the peripheral in reset and a halted controller stops it, like the other engines, and its live bits read 0 when it is idle, so a program that never runs it sees v1.0's behaviour.

**v1.1, the replayer (4.19): open and proposed**

33. **The run log, a requirement on our tracer engine** *(Open)* (4.19.2). The tracer engine must write, per tile, the opcode stream (decompressed instructions with bit 0 as the compressed flag, the padding word 0) and a memory log with one 64-bit word for every LD, SD and advice row **and every control transfer's next `instr_idx`**, in row order, plus the checkpoint. It differs from jolt-cpp's memory log on purpose: jolt-cpp computes DIV/REM advice in its interpreter and finds jump targets with a pc-to-index table (`trace_owner.cpp:111–126`, `545–630`), and the replayer has neither.
34. **The template enumeration** *(Open; partly addressed)* (4.19.3–4.19.7). Enumerate `expand_virt_seq` against the generated bytecode of both ELFs: the template contents and ROM words, the single non-zero immediate per row, `virt` on rewrites and remapped rows, the inline scratch zeroing, the padding rows, and the 91 opcodes that occur in rows. **Partly addressed** (owner, 2026-10-26, part F): `spec/replayer-opcodes.md` section 6 maps every slot form of every row opcode to the source instructions and emit sites that produce it, and its harness (`tools/replayer-opcodes/`) confirms the 91 opcodes and the last-row status of each. **What remains:** the full template and ROM-word listing. **Found** (part L, 2026-10-27): template rows with two immediate slots, a non-zero constant and a zero (17 rows in EVM Zone's tables and 28 in Cyclone's: 15 and 26 in the controller-fed secp256k1 templates, `LUI rd, #0x1000003d1, #0`, plus 2 each in the ROM, the first row of the LR/SC and AMO templates, `LUI v40, #0x80000000, #0`); the zero slot uses operand code 145 (4.19.3). At most one immediate slot is non-zero, so the row's immediate sum is unchanged.
35. **The `virt` bit and the NOP row's virtual-ness** *(Proposed)* *(pending the upstream check, item 113)* (4.19.3, 4.19.4). Jolt's `is_virtual` depends on whether the row opcode differs from the source's (`isa/virtual.cpp:183`), which the replayer can't see, so it is stored per word (bit 51); `rd0` = 3 marks ADDI, whose rd = x0 NOP row is not virtual. Accepted in principle 2026-10-23.
36. **What the replayer executes and checks** (4.19.4, 4.19.7): **decided** (owner, 2026-10-26, part F, citing `spec/replayer-opcodes.md`): it executes 85 opcodes (classes S and M, LD, SD, `VirtualAdvice`), each as the appendix defines it; any other opcode is an invalid expansion. *(Still Proposed:)* it doesn't check that temporaries stay in 40–47; the tracer guarantees it.
37. **The row classes and the multiplier** (4.19.7): **decided** (owner, 2026-10-26, part F, citing `spec/replayer-opcodes.md`, whose flag F9 finds every class assignment consistent with the tracer): the per-opcode class table (L, M, S, source-only, never); MULH and MULHSU never reach a row, so the multiplier drops the signed high half and the sign corrections (accepted 2026-10-23). *(Still Open:)* measure the multiply and MULHU density per 64K-row window, which sets the 3.3-cycle average.
38. **The decode index's special groups** *(Proposed)* (4.19.5): index[10] = 1 for AMO, custom-0 and SYSTEM, with index[9:8] choosing the group and the bit positions given there.
39. **The padding and shared NOP rows at ROM words 0 and 1** *(Proposed)* (4.19.6), so the hardware needs no pointer to them.
40. **The ROM's fit** *(Open)* (4.19.6): about 508 of 512 words; share identical words or grow the ROM slightly.
41. **Register 0 of the image is ignored** *(Proposed)* (4.19.2): the working file's register 0 always reads 0.
42. **Fetch and decode overlap the previous row** *(Proposed)* (4.19.7); only execution is strictly sequential, so the 3- and 5-cycle schedules hold with the table and ROM lookups hidden.
43. **`VirtualPext` with contiguous masks only** (4.19.7): **decided** (owner, 2026-10-26, part F, citing `spec/replayer-opcodes.md`): every mask comes from a `VirtualWindowMask…` row and is contiguous and non-zero; a general bit-extract isn't built, and the result for any other mask is undefined (meshsim flags it, invariant 16).
44. **The queue ports by addr[20:18]** *(Proposed, accepted 2026-10-23)* (4.19.10): window 0 the memories, 1 code, 2 value, 3 expansion, 4–7 unmapped; a port ignores addr[17:0].
45. **The sizes each region takes** *(Proposed, accepted)* (4.19.8, 4.19.10): the memories and the expansion port 128 bits, the code port 32, 64 or 128, the value port 64 or 128; a packet's entries are pushed low first.
46. **The offsets in window 0** *(Proposed, accepted)* (4.19.10), with eight decode-table entries per 128-bit packet, entry *e* in bits [16·(*e* & 7) + 15 : 16·(*e* & 7)].
47. **What is a bad peripheral write for the replayer** *(Proposed, accepted)* (4.17.6, 4.19.10): an unmapped word, a wrong size, a misaligned window-0 write, a single-ported memory while running, a queue port in HALT.
48. **Mode 9's landing and counting** *(Proposed, accepted)* (4.8, 4.12, 4.19.10): a fixed `rp_latency` of 2 with no queue, now required of every peripheral (4.17.5, 4.17.8; owner, 2026-10-26, the value still proposed); `DMA_PERIPH_REMAINING` counts every tile's packets when they land or are dropped (item 86), so 0 means landed and no idle bit is needed.
49. **Mode 9 from another tile** (4.19.10): superseded (owner, 2026-10-26): mode 9 may come from any source and is counted from any tile (4.17.5, item 86); the replayer's software rule keeps other tiles' mode 9 out of a running pass (4.19.13).
50. **Consuming wake events** *(Proposed, accepted)* (4.19.9): a read of `WAKE_EVENTS` moves credits and clears sticky events only for bits under `WAKE_MASK`; a read of `WAKE_PEEK` (8.10) changes nothing. Generalised by the slot: consuming a service line claims one unit of service (4.17.3, item 86).
51. **The expansion queue's low-water rule** *(Proposed)* (4.19.8): set only while the newest landed word's `seq_remaining` exceeds pending; consuming the new-expansion event credits the first 128 words.
52. **Queue reset and preload** (4.19.8): superseded by the owner's ruling (2026-10-23): the replay start clears all three queues; there is no preloading and no end-of-pass reset, and before a start every queue push has landed. Item 76 covers pushes while stopped.
53. **Rows run in `PERIPH_STATUS_0`** *(Proposed)* (4.19.11, 8.11): rows run reads modulo 2¹⁶. (`replay_wait`, the rest of this item, is gone: owner, 2026-10-26.)
54. **Ownership in HALT** *(Proposed, accepted)* (4.19.10): the memories take mode-9 writes for boot loads, and a queue-port write is a bad peripheral write.
55. **The replayer's conditions** (4.13, 4.19.11): decided, accepted as written by the owner on 2026-10-23: all three are program errors, invalid expansion also a fault detector; an invalid expansion stops the pass and posts done, so the controller loop exits. (The earlier "raise and run" ruling for replayer conditions doesn't apply to it; the owner expects it never to happen.)
56. **Jolt's conventions as table data** *(Proposed)* (4.19.3, 4.19.4, 4.19.7). Each convention that can be data is: `virt` (bit 51), `rd0` 1 and 3, `imm0_pc0` (bit 52: a JAL with immediate 0 goes to pc 0, Jolt's program end, `trace_owner.cpp:281`), `report_c` (bit 53: `is_compressed` only on an expansion's last row, `trace_owner.cpp:899`), `from_log` (bit 54: which rows take a log word), and the advice rows written as `VirtualAdvice rd, x0, x0` so the record matches Jolt's masking (`trace_owner.cpp:912–917`) with no logic. A change in Jolt then means new tables, not new silicon. The owner's preference, 2026-10-23. **Added** (owner, 2026-10-26, part F): the template builder sets `imm0_pc0` in **every JAL word, ROM or controller-fed** (EBREAK's included), so the replayer matches the tracer, which ends the program on every JAL with imm = 0 (`spec/replayer-opcodes.md` flag F1).
57. **The replayer's hardwired Jolt behaviour** *(Proposed)* (4.19.7). What depends on values known only while a row runs, and so can't be table data: **a control transfer whose new pc equals its pc is sequential** *(pending the upstream check, item 113)*, advancing pc by `pc_increment` and `instr_idx` by 1 with no log word (`trace_owner.cpp:117–124`); register 0 reads 0 and drops writes; the record's memory fields for LD and SD only; JALR's cleared bit 0; and the ALU's arithmetic per opcode. A Jolt change to any of these is a silicon change. **Added** (owner, 2026-10-26, part F): **a new pc of 0 ends the program from any row** *(pending the upstream check, item 113)* (a JAL with imm 0, a JALR whose target is 0, a branch whose pc + imm wraps to 0): pc = 0 and `instr_idx` = 0, checked before the other pc rules (`trace_owner.cpp:117–119`).
58. **A validation harness** *(Open; building it is a later session)*. It proves the replayer, the run-log format and the tracer engine's extra log words together. It runs the tracer on both 2²⁹ workloads (EVM Zone and Cyclone), produces our run log, decode table, ROM and per-tile checkpoints, and replays every tile's 65,536 rows in a software model of the replayer that sees only those inputs (4.19.2–4.19.12). It then compares, **row by row and byte for byte**, against jolt-cpp's expanded trace (jolt-cpp is the gold standard for now; where a difference from upstream Jolt at 735355e has been confirmed by running both, upstream's; item 113):
    - every field of jolt-cpp's `RiscCycle` (`trace_owner.hpp:19–94`): pc, opcode, the three register numbers, `inline_sequence_remaining`, `is_compressed`, the virtual flags, `_imm`, rd, rs1, rs2, rd_prev, the memory address, pre-value and post-value, the last derived from our record as 4.19.12 says;
    - `instr_idx` against jolt-cpp's `instr_idx` column, and the record's three `*_is_imm` bits against the bytecode's at that index;
    - the run log itself: each tile consumes exactly its opcode words and log words, in order, with none left over and none missing; the extra words (DIV/REM advice, next `instr_idx`) equal what jolt-cpp computes for those rows;
    - each tile boundary: a tile's registers 0–47 at its last row equal the next tile's checkpoint image, registers 48–127 are 0 outside expansions, and mid-expansion starts resume at the right row;
    - and it covers, and reports separately, every rd = x0 case, every controller-fed expansion, every control transfer (including self-jumps and the JAL to pc 0) and the padding rows.
    On a mismatch it reports the tile, the row, the field and both values.
59. **The output row and its consumer** (4.19.12). The 640-bit record's layout *(Proposed, accepted 2026-10-23: it keeps `instr_idx` and drops the memory post-value)*; the output queue's consumer side, its width and rate, and the replayer's placement against the materializer *(Open)*, with the materializer.

**v1.1 request bus: open, to be settled**

60. **The memory system's side** (6.7, section 7): its cache, LPDDR scheduling and the order it serves reads in, the write-token scheduler, how it shares C&C's port with C&C's controller (and so its share of the one packet per cycle into tile 0), and the C&C registers through which C&C's software reads requests addressed to it and the memory system's error flags (with item 11). Only what a tile sees is specified here.
61. **The request tree's physical design** (3.3, 12): where the nodes sit, the pipeline stages per hop and so `reqbus_latency` (15–20 cycles is an estimate; meshsim measures 17 unloaded, part M, though the physical design can still move it), and whether 16 credits per target cover the round trip to its receiver.
62. **Simulating the new paths** (4.5, 4.7, 6.7): once meshsim models the request bus, the deliveries and the protected loopback, confirm item 70's eject priority (no drift in group runs), measure how often loopback reads wait for tokens, and check that the all-to-alls' group runs see no new drift.

**v1.1 request bus: proposed, awaiting a ruling**

63. **Framing, sender stamping and arbitration** (3.3): superseded by item 84 for the 128-bit request, the 16-bit tree and the root's skip. Still proposed: each node wormhole-routes whole requests, round-robin over the inputs holding a request's first beat. Whole requests keep each sender's requests in order and make the tree fair.
64. **The request port** (4.18): a 128 × 128-bit FIFO, one request per entry (was 256 × 64); a `REQUEST_SEND` store stalls only if the FIFO is full and no entry is freed that cycle, the send port's rule; `SET_W0`–`W2` clear at every push; HALT doesn't touch the port or the tree, which keep draining like the routers.
65. **Request sizes other than 64 bits** (3.3, 4.18, 6.7): superseded (owner, 2026-10-26): every request is 128 bits, and `REQUEST_SEND_32`, `_64` and `_128` are replaced by one `REQUEST_SEND` (item 84).
66. **Mode 3's path** (4.9): through the write engine, passing the pointer stage in order with no `PTR` access, at any size, ignored by count-on-arrival (now counted in `DMA_SRAM_REMAINING`, item 91). Keeping one writer and one order into the areas costs nothing; a separate path would need its own area port or arbitration.
67. **A read's data and order** (6.7): every data packet 128 bits, unicast, in ascending address order; reads may complete in any order and interleave. One fixed shape keeps the memory side simple. v1.1's completion packet is gone (item 92): the counters prove landing.
68. **Controller-memory deliveries and invariant 11** (6.7): a destination-kind-1 read must lie inside controller memory and below the reply block (tile address + count ≤ 119), as any mode-6 packet must.
69. **The optional write acknowledgement** (3.3, 6.7): requested by the write, where its target's layout says, sent as a reply once a later read will see the data (it was mode 10 with kind 2 until item 92); needed only before the region is read again.
70. **The eject output's loopback priority** (4.7) *(Open)*: the eject selector takes from the loopback FIFO only in a cycle when every other eject FIFO is empty (on tile 0 including from-C&C), and round-robins among the others. Decided by the owner (2026-10-23) in place of per-tile delivery pacing: with round-robin, a tile running a loopback stream gave incoming data every other eject slot, so a full-rate delivery overflowed its link FIFO within about 32 cycles (v1.0's read-port tolerances, 4.3, show the same shape). With incoming traffic first, link-side eject FIFOs cannot overflow from the tile's own traffic, and the loopback waits on its tokens (4.5), so the memory system needs no pacing. **To be confirmed in simulation:** NetTest must show that group runs see no drift under this priority, since a tile's self-stream now gets only the eject slots its links leave free.
71. **Debug messages** (6.7): a format id and arguments, laid out by the target; appended with the requestor to a debug log in LPDDR, or handed to a controller's software; longer messages are several requests, in order.
72. **Errors at the memory system** (6.7): a request it can't serve, or one for a reserved target or a target with no receiver (dropped at the root), is dropped and sets a C&C error flag, with no condition on the sending tile ([14] is the receive side's underflow); the requesting tile's wait is ended by C&C's watchdog. meshsim flags every case.
73. **The wake events' details** (4.12, 8.10): superseded by the owner's rulings (2026-10-23): all wakes are events, `WAKE_MASK` resets to [8] | [9] at chip reset and HALT → RUN and is never written by hardware, and the `done` gate overrides the wake term to GO alone. (The tag clause that stood here went with the tags, item 92.)
74. **The protected loopback** (4.1, 4.5, 4.7). *The first half, in-order big-SRAM returns with one token FIFO, is superseded* by the owner's ruling that read order is per client (2026-10-23; 4.1, 4.5, item 77). Still proposed: the loopback FIFO is at least as deep as the token count (64); a loopback packet holds its token until it leaves the loopback FIFO, so the address generator issues a loopback read only when its packet has a place, and the loopback cannot overflow whatever arrives from the links. **The send port's packets for the tile itself and its broadcasts take a token too** (4.5, 4.11): otherwise they could fill the loopback FIFO without tokens and break the guarantee. This exists only because the mesh has no flow control; with flow control it would be revisited.
75. **Write-stream checks at C&C** (6.7): a mode-3 packet at C&C's port from a tile without the token, before it, or outside its block is dropped and sets a C&C error flag, so a stray write stream cannot corrupt another tile's write.

**v1.1 wake events and read order: accepted and decided**

76. **Queue pushes while the replayer is stopped** *(Proposed, accepted 2026-10-23)* (4.19.8, 4.19.10): accepted like any push, with no condition, and discarded by the next start; there is no preloading.
77. **Per-client return queues** *(Proposed, accepted 2026-10-23)* (4.1, 4.5, 6.1, 10): the address generator and the GATHER engine each have an in-order return queue of per-read metadata; the 64 tokens are one shared pool; when several clients have a return ready, injection takes them by the request priority (gather value, address generator, read port); read-port returns come back in any order.
78. **Landing of a read into the peripheral** *(Proposed, accepted 2026-10-23)* (6.7): superseded (owner, 2026-10-26): a read into the peripheral is counted in `DMA_PERIPH_REMAINING` like any mode-9 data (item 91), and the replayer takes no reads into its queues (item 94). The fixed `rp_latency` stays a requirement on every peripheral (4.17.8).
79. **Keep GO in `WAKE_MASK`** (4.12, 4.19.9, 6.3): decided (owner, 2026-10-23): the `done` gate lets a pending GO wake the core whatever the mask, but only a load with [8] in the mask consumes it, so software keeps [8] in `WAKE_MASK` at every `wait_for_command()`; the replayer's loop keeps it throughout. meshsim flags a `wait_for_command()` with [8] clear in the mask.
80. **The send port as a client for invariant 18** (4.5, 9): decided (owner, 2026-10-23): order holds only within a client, and the send port's packets are not ordered against the read clients' returns, so it counts as a client in the one-client-per-destination rule.
81. **The new events' details** (4.12, 8.10): decided (owner, 2026-10-23): command landed ([10]), engine run done ([11]) and request FIFO no longer full ([12]) are set on the edge, in the cycle the matching status bit sets or falls; `WAKE_REQUIRED` is cleared by HALT → RUN as well as at chip reset; `WAKE_MASK`, `WAKE_REQUIRED` and `WAKE_PEEK` cover [17:0], with [31:18] reserved; extended by the 2026-10-26 pass to [19:0], with [18] `REQUEST_PENDING`, [19] sender idle (set on the fall of `sender_busy`, like [10]–[12]) and [31:20] reserved; moved by part D (owner, 2026-10-26): six service lines [18:13], `REQUEST_PENDING` [19], sender idle [20], the UART's [21] and [22], coverage [22:0], [31:23] reserved.

**v1.1, the request port and the peripheral slot (2026-10-26)**

82. **The endpoint map** *(Open)* (2.1, 3.3, 4.16, 4.17.9, 4.18, 6.6, 6.7, 7). Deferred by the owner to a later part: the controllers C&C 1–3 (endpoints 64–66) and the Updater/Binder/RW engine (73); who serves LPDDR and where the memory system sits (2.1 still has 65 as the memory system, now proposed as C&C 2); how the request receivers attach to the root and to the mesh, and where each target's forwarding link runs; the non-mesh tiles, the extra router port through which their peripherals' senders connect (as C&C's does at tile 0), and those peripherals' endpoint ids (the src the sender stamps; a peripheral's mode-2 or mode-5 packets land at `PTR[its id]`).
83. **The target numbering** *(Proposed)* (3.3): 0 C&C 1 (64), 1 C&C 2 (65), 2 C&C 3 (66), 3 the Updater/Binder/RW engine (73), 4–7 reserved; to be confirmed with item 82.
84. **The request** (3.3, 4.18, 6.7): decided (owner, 2026-10-26): every request is 128 bits; the hardware commits only requestor [6:0], overwritten by the tree, and target [9:7]; [127:10] is each target's, specified with its receiver; v1.1's 64-bit request and its field table are withdrawn, not moved, and 6.7's tile-side answer rules stand with the fields named abstractly. The 16-bit tree, 8 beats per request, a 16-byte buffer per node input, the root's per-target queues (replaced by per-target credits and receive queues in every tile, item 99), controllers entering at the root, and the root skipping an input whose head request's target cannot take it, with head-of-line blocking inside a row accepted and noted, are decided too. *(Proposed:)* beat order (bits [15:0] first), the row node writing the whole requestor, and 16 requests per target queue.
85. **The receive side** (4.18, 8.10): decided (owner, 2026-10-26): every tile has it, connected only on the receiving controllers; send and receive share offsets; `REQUEST_GET_W0`–`W2` read the oldest request without popping, `REQUEST_RECEIVE` reads its top word and pops it; a `REQUEST_RECEIVE` with the queue empty returns 0 and sets receive underflow, `TILE_CONDITIONS` [14]; `REQUEST_PENDING` is `WAKE_EVENTS` [19] (moved from [18] by part D), live, with its mask bit reset to 0; the receive queue was the root's queue for the target (reversed by item 99: a 16-entry queue in every tile). *(Proposed:)* a `GET` load with the queue empty returns 0 with no condition.
86. **The peripheral slot** (4.17): decided (owner, 2026-10-26): one slot per tile with the interface of 4.17 (run, `periph_running`, five service lines, six since part D, in which a claim takes one unit of service, eight side-effect-free status registers with at most one presented line, mode 9 as the peripheral's own address space from any source, conditions [13:11] with [11] the generic bad peripheral write); the type in `TILE_STATUS` [15:12]; the renames (`PERIPH_RUN`, `periph_running`, `PERIPH_SERVICE_1`–`5` (`_6` since part D), `PERIPH_STATUS_0`–`7`, `DMA_PERIPH_REMAINING`); `replay_wait` dropped; the counting rule (every mode-9 packet counts on landing or drop; from any source since item 91); the fixed `rp_latency` with no queue as a requirement on every peripheral; the register map with `PERIPH_STATUS_0`–`7` at 0x880–0x89C and no renumbering (reversed by part E: `PERIPH_STATUS_` at 0x870–0x88C, `DMA_PERIPH_REMAINING` at 0x83C, item 110). *(Proposed:)* the empty slot's behaviour (4.17.7): run ignored and flagged, every mode-9 packet a bad peripheral write, counted at once from a tile.
87. **The sender** (4.17.9, 8.12): decided (owner, 2026-10-26): `SENDER_SEND` and `SENDER_ADDR`, up to four output queues, 128-bit packets then a 64-bit and/or a 32-bit one for the remainder (64 then 32 for 3 words), one send at a time with a store while `sender_busy` ignored and flagged, `sender_busy` at `TILE_STATUS` [4], sender idle at `WAKE_EVENTS` [19] (sticky; [20] since part D), and a send on a tile without peripheral output ignored and flagged; the sender's link attachment deferred to item 82, mesh tiles' peripherals having no output. *(Proposed:)* modes other than 3, 6 and 9 send addr 0; a count of 0 ends at once and posts sender idle.
88. **Mode 3 aligned to its size** (3.1, 4.1, 4.9): decided (owner, 2026-10-26): a mode-3 packet's addr is a multiple of 2 at 64 bits and of 4 at 128 bits; a misaligned one is a bad packet, dropped with the bad-mode-or-size condition, so the mistake can't go unnoticed. A mode-3 write therefore never spans two rows. `ROUTER_SET_HDR`'s addr is unchanged: it was already a 21-bit word address.
89. **The endpoint id in `TILE_STATUS`** (8.2): decided (owner, 2026-10-26): 7 bits at [26:20], widened from the 6-bit tile index at [26:21], so its low bit moves and every extraction of the tile index changes.

**v1.1, modes, counting, commands, replies and the read port (2026-10-26)**

90. **The mode renumbering** (3.1): decided (owner, 2026-10-26): 0 invalid, 1 gather request, 2 big-SRAM write by source, 3 big-SRAM write at an explicit address (v1.1's 9), 4 bucket write (v1.1's 3), 5 augmented write (v1.1's 4), 6 controller-memory write (v1.1's 5), 7 command write (v1.1's 6, the mailbox write), 8 reply (v1.1's 7), 9 peripheral write (v1.1's 8), 10–15 reserved; v1.1's mode 10 (the wake event) withdrawn. Groups: big SRAM 2–5, controller memory 6–8, the peripheral 9. Every mode number in this document is in this numbering, items included, except where a sentence names v1.0's or v1.1's.
91. **The counting policy** (3.1, 4.8, 4.12, 6.2): decided (owner, 2026-10-26): every mode is counted, from any source, except the command write and the gather request; bad packets are dropped uncounted; a halted tile counts nothing and sets no unexpected arrival, which covers boot loads; counted data reaches a running tile only when the tile expects it, so staging and overlays go into a halted tile or follow a command that arms the counters; C&C halts a tile for an uncoordinated write. The counters-done qualifier stays. Replaces v1.1's "packets from non-tile sources are never counted" and its uncounted staging.
92. **Answers without wake events** (6.7, invariant 17): decided (owner, 2026-10-26): a request is answered by its bulk data, counted against what the requester armed, then, only if it has a value answer, by exactly one reply. A read's answer is its data alone, so reads may be outstanding in any number. Write tokens, write acks and value answers (an fd, an address, an error code) are replies; at most one reply-producing request is outstanding per responder, and a write's token reply already serializes writes to one responder. `WAKE_EVENTS` [7:0] are reserved; invariant 17 becomes the request contract.
93. **The command and reply blocks** (4.11, 4.12, 6.3): decided (owner, 2026-10-26): a 16-word command block at the top of data memory (0x5FC0–0x5FFF), slot 0 the command word, slots 1–15 arguments with even-parity commands in 1–7 and odd in 8–15; command writes 32 bits only, at slot addr[3:0] with the rest of addr ignored; a 256-word reply block below it (0x5BC0–0x5FBF), where a reply (32 or 64 bits; 128 a bad packet) lands at word 2·src, a 32-bit reply writing word 0 and leaving word 1 alone; only command writes write the command block and only replies the reply block; the stack below. `MAILBOX_BASE` becomes `COMMAND_BASE` in `sw/mesh.h`, with a new `REPLY_BASE`.
94. **The replayer's LPDDR fetches and its refill wait** (4.12, 4.19.9, 4.19.13): decided (owner, 2026-10-26): the overflow is fetched into big SRAM (mode 3) and refilled locally, and the direct read into a queue port leaves the replayer's contract. A fetch in flight counts in `DMA_SRAM_REMAINING`, so the loop's refill wait uses the per-counter event `DMA_PERIPH_REMAINING` done ([3]) instead of counters done, and the loop text changes only in naming that bit. Per-counter done events: `WAKE_EVENTS` [0]–[3] for the SRAM, CM, reply and peripheral counters, sticky, each with its own qualifier and waiting for `mem_idle` (SRAM) or `cm_idle` (CM and reply, as their "done when" says); [9] keeps its meaning; [7:4] reserved.
95. **The read port's results** (4.3, 8.8): decided (owner, 2026-10-26): each read fetches 1, 2 or 4 words aligned to the size and lands them as one mode-6 packet to the tile itself, counted in `DMA_CM_REMAINING`; `SRAM_RD_DEST_ADDR` is a setup register, destination [13:0] and size [15:14]; a misaligned read is dropped and sets condition 7, renamed read-port error; the split's search loop waits on `DMA_CM_REMAINING`. Its other `DMA_CM_REMAINING` traffic is kept out of a search step.
96. **Packed controller-memory words of up to 14 bits** (4.11): decided (owner, 2026-10-26), with words beyond the memories an error (invariant 11).
97. **`TILE_PULSE` [3], GO** (6.3, 8.6): decided (owner, 2026-10-26): every tile has the GO pulse bit, driving the global GO net only where it is wired (C&C, today); elsewhere it is ignored and meshsim flags it. C&C's READY and error inputs and its other controls wait for the endpoint map (item 82).
98. **`HIST` with mode 3** (4.9, 4.15): decided (owner, 2026-10-26): count-on-arrival counts only modes 2, 4 and 5; mode 3 is counted in `DMA_SRAM_REMAINING` but not histogrammed.

99. **A receive queue in every tile** (3.3, 4.18): decided (owner, 2026-10-26): 16 × 128 bits (2 Kbit) in every tile's request port, fed only on a target's receiver; the root holds no target queues, arbitrates and forwards with **one credit counter per target** (16, returned on each `REQUEST_RECEIVE` pop), skipping inputs whose target has no credit; HALT keeps the queue's contents. Reverses item 85's "the root's queue, no storage in the tile". *(Proposed:)* one 16-bit forwarding link per target.
100. **One tile design** (2, 4.16): decided (owner, 2026-10-26): tiles are **identical in design, differing only in big-SRAM size and in what is attached and strapped**. Every tile has the fifth router port (connected only at tile 0, to C&C; an unconnected port never receives and is never chosen by routing), the request receive queue (item 99) and the GO pulse (`TILE_PULSE` [3], item 97); the endpoint id and peripheral type are strapped. The fifth port in every tile adds about 0.03–0.05 mm² per tile [estimate].
101. **The counter add aliases** (4.12, 8.7): decided (owner, 2026-10-26), then **superseded by part E** (owner, 2026-10-26, item 109): the aliases at 0x8A0–0x8AC are removed, and a write to the counter itself adds, with the same semantics.


**v1.1, the UART, the split run log and the resident region (2026-10-26, part D)**

102. **The UART** (4.20, 8.13): decided (owner, 2026-10-26): every tile; 8N1; registers at 0x8B0–0x8BC (0x890–0x89C since part E, item 110) (`UART_TX` stalling while full, `UART_RX` with a valid bit, `UART_STATUS` with write-1-to-clear errors, `UART_DIVISOR` in reference-oscillator cycles per bit, reset 57,600 baud); live wake events [21] RX not empty and [22] TX not full, mask bits reset 0; no-ops on an unwired tile; overrun and framing errors drop the byte; HALT doesn't reset it, chip reset does; meshsim prints every tile's TX. *(Proposed:)* 16-byte FIFOs. *(Open:)* `f_ref`, the board's oscillator (25 MHz would give a divisor of 434).
103. **The split run log and its queues** (4.19.2, 4.19.8, 4.19.10): decided (owner, 2026-10-26): a read log (LD, advice, non-sequential next `instr_idx`) and a write log (SD and AMO pre-values); four queues of 128-bit entries, ports taking 128-bit packets only, streams padded to a whole entry; the logs 8 KB each; six service lines. *(Proposed:)* the rule that a row whose opcode is SD takes the write log, every other log row the read log. *(Open:)* the tracer to measure the read/write split, which may let the two log queues differ in size. Amended (owner, 2026-10-26): a pass with includes-store-log 0 (state word [115]) doesn't feed the write log; its SD rows pop nothing and output a pre-value of 0.
104. **Stream decompression** (4.19.15): decided (owner, 2026-10-26): per window and per stream, raw or 16-entry true-LRU (move-to-front) coded; hit = 1 + 4-bit index, miss = 0 + size bit + 32 or 64 bits, a 32-bit miss zero-extended; tables reset at each run; bits least significant first within each entry; exact lengths of 24 bits in the stream word at 0x00004–0x00007, the compressed flags in the state word [114:112] (item 108); a pop past a stream's length is an invalid expansion. **The write-log table's E[15] is a last-load entry** (owner, 2026-10-26): every traced LD row copies its loaded value there; a hit at 15 reorders nothing; the LRU runs over E[0]–E[14]; reset to 0 at each run; the encoder prefers index 15 when a value matches both.
105. **The expansion word's tracer fields and the ROM policy** (4.19.3, 4.19.6): decided (owner, 2026-10-26): [55] `untraced` (an invalid expansion if it reaches a replayer) and [62:56] `advice_reg` (ignored by a replayer); [63] spare. The ROM is a cache of the most frequent templates, filled by the table builder; any instruction may be controller-fed; DIV/REM and the atomics may move out if it is full.
106. **Best-effort residency** (4.19.13): decided (owner, 2026-10-26): no fixed resident size; each stage keeps what its free big SRAM allows, and the rest streams from LPDDR through a big-SRAM staging buffer and is refilled locally.
107. **4.25 MB of big SRAM per tile** *(Open; a tentative working figure, owner, 2026-10-26)*: to be settled once the sumcheck memory squeeze shows what fits and at what cost. It is representable as `TILE_STATUS` size 16 (17 × 256 KB) and in areas of at most 512 KB (eight of 512 KB and one of 256 KB). The spec's default big-SRAM size stays as it is for now.

108. **Region 0 order and the latched stream word** (4.19.2, 4.19.10, 4.19.11): decided (owner, 2026-10-26): state word at 128-bit word 0 (0x00000–0x00003), stream word at 1 (0x00004–0x00007), register image at 2–65 (0x00008–0x00107), the decode table and ROM unmoved; the state word's spare bits carry the per-run flags, [112]–[114] each stream's compressed flag and [115] includes store log (0: SD rows pop nothing and output a pre-value of 0, the write log is never fed and its low-water line stays quiet; the tracer emits 1), and the stream word only the three lengths; a run latches the four flags and three lengths as it copies the state word and the image. Recommended practice: the controller loads or edits region 0 from data memory through its send port (mode 9 to itself, armed by writing `DMA_PERIPH_REMAINING`, waiting on [3]); a per-pass flag change resends only the state word. The tracer's checkpoint record (and a future tracer peripheral's state queue) emits the same order, so a record streams straight into region 0. **Amended** (owner, 2026-10-26, part G): the tracer's record is the window's template bitmap (word 0) followed by the region-0 image (words 1–66); a controller sends it from byte 16, so the region-0 part still streams straight in, and a future tracer peripheral's state queue emits the same form (4.19.2).


**v1.1, additive counters, the renumbered map and register-load latency (2026-10-26, part E)**

109. **Additive counter writes** (4.12, 6.7, 8.7): decided (owner, 2026-10-26): a write of *N* to any `DMA_*_REMAINING` counter adds *N* atomically, in step with arrivals (the add aliases' semantics; the aliases at 0x8A0–0x8AC removed, item 101); a counter is never overwritten and returns to 0 only by counting down, at chip reset, or **on entering HALT, which clears all four counters, their done events and their qualifiers**; writes to a non-zero counter are legitimate and not flagged; the wrap past 2²⁰ − 1 is flagged. At 0 a write is v1.1's store, so existing arming code is unchanged.
110. **The renumbered register map** (8.1): decided (owner, 2026-10-26): every family contiguous, no holes, 0x800–0x89C; offsets 0x800–0x828 unchanged; moved: the record base (0x838 → 0x82C), the counters (0x82C–0x834 → 0x830–0x838; `DMA_PERIPH_REMAINING` 0x854 → 0x83C), the read port (0x83C–0x840 → 0x840–0x844), the wake registers (to 0x848–0x854), `PERIPH_STATUS_` (to 0x870–0x88C) and the UART (to 0x890–0x89C); unmapped from 0x8A0. Reverses part j's "no renumbering" (items 86, 101, 102).
111. **Register-load latency** (4.11, 8.1, 10): decided (owner, 2026-10-26): register-block and table-window loads take 1 or 2 cycles, implementation-defined, fixed per register on every access and tile; a load at *t* with latency *L* samples at the end of *t* + *L* − 2 and takes its side effect at the end of *t* + *L* − 1, removing exactly what it returned; stores are single-cycle and posted (taking effect at the end of their cycle) and accesses complete in program order; meshsim's reference is 2. Every [meas: meshsim] figure that includes controller register loads was taken with 1-cycle loads and is to be re-measured at 2.


**v1.1, the replayer's opcode semantics (2026-10-26, part F)**

112. **The opcode appendix** (4.19.7): decided (owner, 2026-10-26): `spec/replayer-opcodes.md`, derived from the jolt-cpp tracer at commit 4f36b3d, defines what each of the 91 row opcodes computes; its sections 1–5 are **normative** for that, its sections 6–9 (producers, source-only and never lists, test vectors) informative. 4.19.7 keeps the classes, schedules and pc rules; 4.19.12 the record layout. Its flags F1–F6 are resolved in the spec (F1: `imm0_pc0` in every JAL word, item 56; F2: EBREAK's controller-fed word, 4.19.5; F3: a new pc of 0 from any row, item 57; F4: LD's memory fields from rd, 4.19.7 and 4.19.12; F5: per-opcode extension, 4.19.7; F6: zero shift masks undefined, 4.19.7 and invariant 16). The harness that produced its tables is in `tools/replayer-opcodes/`; regenerating them for a later tracer commit is how the appendix is kept current.


**v1.1, ground truth, template numbering and the replayer-model rulings (2026-10-26, part G)**

113. **Ground truth for the trace** (4.19, 4.19.12; invariant 16; item 58): decided (owner, 2026-10-26, parts G and H): the trace is upstream Jolt's, the 80-byte trace row and the pc and `instr_idx` sequence, where "upstream Jolt" means **the version our baseline is built against: `LayerZero-Research/jolt` at commit `735355e43026e9ef392b36b4ab5ed0f5e680127f`** (vendored by jolt-cpp at `309b4c4`; see jolt-asic-planning `docs/provenance.md`); later upstream versions, `a16z/jolt` HEAD included, are tracked as future compatibility, not as the authority. **For now jolt-cpp is the gold standard:** the replayer, its tests and the reference rows match **jolt-cpp's interpreter rows** (`execute_instruction` and `expand_compressed_chunk`), the path `spec/replayer-opcodes.md` is derived from and the tracer session takes its reference rows from; jolt-cpp's JIT is not a reference, and its disagreements are recorded as findings (owner, 2026-10-27); **upstream at 735355e overrides jolt-cpp only once a difference is confirmed by running both**, and then the appendix (and if need be jolt-cpp) is corrected. The formats that are ours (the run log, the LRU coder, the checkpoint record, template delivery) are ours to define, provided the rows match.
    - **Potential differences found so far** (part H; each **unconfirmed**, found by reading source; the rules they touch are tagged *(pending the upstream check, item 113)*):
      - **LR/SC with rd = x0:** upstream may remap rd to v40 where jolt-cpp emits rows naming x0 (4.19.4's exempt list). Reading 735355e suggests it differs there too. **F4** (an LD to x0 reports a memory value of 0, 4.19.7, 4.19.12) and **N5** (write-log E[15] for an LD to x0, 4.19.15) exist only because of these rows and depend on this question.
      - **The shared NOP row's virtual flag** for an rd = x0 instruction under `rd0` = 1: upstream may mark it not virtual where jolt-cpp marks it virtual unless the source is ADDI (4.19.3, 4.19.4). Reading 735355e suggests it differs there too. This is **item 35**'s question.
      - **The self-jump rule** (a JALR that overwrites its base register and jumps to itself; 4.19.7, item 57): differs at `a16z/jolt` HEAD; probably unobservable at 735355e.
      - **A new pc of 0 from a JALR or a branch** (ruling N6; 4.19.7, item 57, flag F3): not yet determined upstream. N6 (whether such a row logs a next-index word of 0 and ends the program) waits for it.
    - **Future compatibility only:** `a16z/jolt` HEAD (`bd9aef4` when checked) adds the runtime advice tape (`AdviceLB`, `AdviceLH`, `AdviceLW`, `AdviceLD`, expanding to `VirtualAdviceLoad`), and defines but never emits `VirtualPow2I` and `VirtualPow2IW`. Neither is in 735355e or in jolt-cpp.
    - **Findings from running the guests through jolt-cpp** (part K, 2026-10-27; **unconfirmed against 735355e**):
      - **A self-targeting JALR:** jolt-cpp's fast JIT executes it twice (8 cycles where the interpreter's rows have 7). That agrees with `a16z/jolt` HEAD rather than with jolt-cpp's own interpreter rows, which remain the reference.
      - **A JALR to 0:** jolt-cpp's JIT crashes; the interpreter handles it (a new pc of 0 ends the program).
      - **A branch that wraps to 0 can't be built as a guest:** branches reach ±4 KiB and code sits at 0x80000000, so that case can only be checked at the unit level.
    - *(Open: the plan, part H.)* Write small guest programs that exercise each potential difference, run them through Rust Jolt at 735355e and through jolt-cpp, and diff the rows. Where they differ, decide whether jolt-cpp or the spec changes, and remove that case's tags as it settles. Until then the earlier part-G tags (LD to x0, the rd = x0 remap, EBREAK, padding) also stay.
114. **Controller-fed template numbers** (4.19.5): decided (owner, 2026-10-26): the template builder numbers the program's controller-fed templates 0–127; the template set is the words plus an index of number, word offset and length; an instruction maps to its number by its exact instruction word; numbers may share words; at most 128 per program. *(To be measured:)* the number of **distinct controller-fed instruction words per program**, not distinct templates, on the workloads: programs with many distinct CSR accesses, or inlines called with many register combinations, could approach the limit.
115. **The chip's used set of templates** *(Proposed; belongs with the deferred controllers, item 82)* (4.19.2, 4.19.5, 4.19.13): a controller ORs the 64 windows' template bitmaps (word 0 of each state record) into the chip's used set and broadcasts those templates and their index once to every tile, as a coordinated transfer (a command giving the size first, so each tile arms its counter, 4.12); each tile then keeps only the templates its own bitmap names, with every word any of its numbers references. **For now every tile simply preloads the whole set.**


**v1.1, measurements from meshsim (2026-10-27, part M)**

116. **Read-port reads overlapping incoming traffic** *(Open, a candidate)* (4.3, invariant 14; items 24, 70): meshsim now measures **no loss** when read-port reads overlap an all-to-all or a full-rate stream, where v1.0 lost 561 packets, as 4.3 predicted for the v1.1 loopback (the protected loopback FIFO, and the eject output serving incoming packets first, 4.5, 4.7). The rule and invariant 14 are unchanged for now; revisit them once the reason for the measured change is confirmed, and decide whether to relax the rule.

The logic budget (section 11) is unreviewed.

---

## 14. Context: the sort these units serve

Not part of the hardware specification; it is here because it explains the GATHER engine and augmenting. The program is in `sort/program/` (the split in `sort/split/`, skew handling in `sort/skew/`), and its measured times are in `results/`. For a 32-bit key and a value of *k* records (*k* = 2 for 128-bit values), the sort has six steps:

1. **Local sort** of (key, index) words, with the index attached by augmenting (mode 5).
2. **Local gather** of the values into key order: a mode-1 stream to the tile itself (4.4).
3. **Split:** a bisection on key values. Owners exchange probe keys (mode-8 words) and counts (mode-6 (lb, ub) pairs) from the send port, and every tile counts by binary searches of its own run through the controller read port (4.3). Exact on all 25 data sets: ≈ 203K cycles on uniform, 246K mean over the 24 non-trivial sets, 367K worst, with a seed bracket and an early exit to idle barriers [meas: RV32 ISS]. Interface and memory: `sort/split/BINDING.md`.
4. **Two all-to-alls,** in modes 2 and 5:
   - **keys,** in mode 5 (augmented), with count-on-arrival on at the receiver;
   - **values,** in mode 2, with count-on-arrival off.

   Each sender's runs land contiguously at `PTR[src]`, set by the receiver from its counts; senders need no destination addresses. Key *i* and value *i* correspond because both arrive in the same sender order.
5. **Local sort at the owner,** starting with its first digit already counted.
6. **Local gather** of the values into final key order: the same operation as step 2.

The values cross the network once, and each value is read twice locally, by the two gathers. For the 32-bit-value format, key and value fit in one 64-bit record, the value travelling in the record's high word: so steps 2 and 6 disappear, nothing is augmented (the first local sort's input pass and the keys all-to-all use mode 2, not mode 5), and there is no values all-to-all.

**Skew handling** (`sort/skew/`). A three-stage switch picks the transfer path for step 4: direct, loopback-as-local-copy (LB), the one-layer two-hop (TH), or the balanced two-hop (BT), with every tile making the same choice from the group maxima. K + V including planning: 215K uniform, 199K sorted and all-equal, 420K reverse, 772–786K Zipf and trace, 741K at 99 % one value [model; planner cycles measured on an RV32 ISS]. The planner is an overlay (≈ 8.5 KB). C&C issues a fixed maximum number of layer phases, and tiles finish empty layers at once (≈ 30 cycles each). The switch is format-aware: for 32-bit values it costs the keys all-to-all only (trace sets take BT at 332–343K against 0.59–1.00M direct; sorted and all-equal LB at 66.7K; reverse TH at 149.0K) [model]. TH and BT run every stream at the full window and so use a group gap of 4 (4.2).

**Global totals** (the group maxima for the `GROUP` R fields, or any global sum) use a mesh reduction, not a ring scan.
- Each row collects toward its column-0 tile (4 hops from one side, 3 from the other), then column 0 collects toward tile 0 the same way, and tile 0 broadcasts.
- That's about 8 controller turnarounds instead of 63.
- **One-word totals** move as mode-8 replies: a collector arms `DMA_REPLY_REMAINING` with 2 and `WFI`s; a tile mid-chain arms it with 1.
- **Vectors** (the 16 group maxima) move as eight 64-bit **mode-6** packets into the receiver's data memory, counted by `DMA_CM_REMAINING` (in v1.1, four 128-bit packets would do).
- The ring scan is kept only for exclusive prefixes.

The plan estimated ≈ 1.37M cycles for the whole sort at *k* = 2 on uniform data [model]; the measured times are in `results/` [meas: meshsim; v1.0 measurement; to be re-measured in v1.1]. In v1.1 the values all-to-all of the 128-bit-value format sends one packet per value instead of two.
