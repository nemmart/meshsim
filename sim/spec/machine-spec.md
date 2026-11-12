# Machine specification — 64-tile mesh processor, v1.35

This document specifies the hardware of the 64-tile mesh sorter: the tiles, the network that joins them, the control that sequences them, and the registers a tile program uses. It is written for an engineer who will build the machine.

**Companion material** (paths in the package):
- `spec/OVERVIEW.md` and `spec/GUIDELINES.md`: what is being built and where it stands, and the rules the design follows; read them first.
- `spec/jolt/`: the Jolt-specific units, the replayer (4.19) and the Tracer (4.21), with what belongs only to them and the opcode appendix (`spec/jolt/replayer-opcodes.md`).
- `spec/CHANGES-v1.3-to-v1.35.md`: v1.35 against v1.3; nothing changes for the RTL.
- `spec/history/`: what changed in each version (the `CHANGES-*` files, each version's "in brief"), the decisions logs behind them, and section 13's full record. Where a log and this document differ, this document is the authority.
- `data/tables/`: the replayer's expansion tables (4.19.5, 4.19.6), universal, built from jolt-cpp `821e1bd` by the table builder, with the opcode table and a manifest.
- `v1.3/diagrams/`: the figure set, numbered part.figure (1.0–9.3; part 9 is the Tracer, v1.3); `v1.3/diagrams/README.md` is the index. Each section below names the figures that draw it. **The figures are still at v1.1** (the 169-bit link word, modes 0–15, the peripheral slot, RUN/HALT) and are to be redrawn.
- `spec/schedule.md`: the 16-group all-to-all schedule (4.2) and the routing ties it fixes (4.6).
- `fw/include/mesh.h` and `fw/REGISTERS.md`: the tile-program interface and the programmer's register reference, generated from the register table. Both must match section 8.
- `fw/CONVENTIONS.md`, `fw/RECIPES.md`: the software conventions and the recipe book.
- `src/`: meshsim, the cycle-level simulator, which models every unit described here. v1.35's is in C++, checked against v1.3's, in Java, where the measured numbers come from (Markers, below).
- `v1.3/results/`: the measured sort.

**Markers.** An unmarked statement is decided. *(Proposed)* means suggested and not yet ruled on; *(Open)* means still to be settled; *(Deferred)* means set aside until the design it belongs to (the file system, C&C 1's firmware), listed in section 13; *(likely to change)* marks a decided value expected to move. Section 13 gathers every proposed, open and deferred item. Numbers are marked [meas: meshsim] (measured on the simulator, with the test where it matters), [meas: RV32 ISS] (measured on a 64-tile RV32 instruction-set simulator), [built] (as implemented in the simulator), [model] (from a model), [derived] or [estimate] (a rough estimate). **Every number marked [meas: meshsim] was measured on meshsim's Java implementation, v1.0 to v1.3**, and a test or driver a label names is a Java one (`spec/CHANGES-v1.3-to-v1.35.md` names what replaces each in v1.35); one labelled *v1.0 measurement; to be re-measured in v1.2* (or *in v1.1*) has not been re-measured since, and is kept as the best number known (item 24). Decisions cite the owner (O), Tony (T) for the controller core and the link, or the integrator, with a date where it helps.

**How the hardware treats software bugs** (owner, 2026-11-01). We write all the firmware ourselves, and it runs in simulation before it runs on hardware: in **meshsim** and in **full RTL simulation**. So catching software bugs is the simulators' job, and the hardware spends area on a check only when simulation can't do that job. Every rule a program must keep is one of two kinds:
- **Checked by the hardware:** a broken rule raises an `ERRORS` bit. This is reserved for what simulation can't see: hardware faults, and mistakes that depend on data or timing only real runs produce (overflows, unexpected arrival, done early, drift). The spec names the error raised. Since every error is terminal (4.13), what happens to the offending access itself is left unspecified unless it comes for free.
- **Undefined behavior (UB):** the hardware does nothing special, and the outcome is unspecified. meshsim stops with an error naming the access, and the RTL may carry simulation-only assertions for the same rule, which cost no area.

**There is no third kind:** never a prescribed outcome for a mistake (a substitute value, a dropped write still counted) that costs hardware and helps no one find the bug. **The test for a new check:** would simulation catch this on our firmware? If so, it's UB.

In the rules below, **"(hardware)"** marks a checked rule and **"(meshsim)"** marks UB; section 9 lists the invariants meshsim checks. (DECISIONS-v1.25 section 8a, replacing v1.2's "Who checks what" of 2026-10-28; the simulation-only assertions are for Tony to confirm.) **Where this document says meshsim stops, errors or flags,** v1.35's meshsim does one of three things: **undefined behaviour is fatal** in every build, the run ending with a message naming the access (`src/base/fatal.h`); **the silent traps** (4.13) **stop the core**; and **every other invariant of section 9 is counted** in every build and, in the debug and suite builds, also reported by name while the run goes on (`src/check.h`), so a test fails on a violation it doesn't expect.

**How v1.3's text is meant** (owner, 2026-11-06). It makes **reasonable default calls** so that the RTL can start, and states **what the hardware must do observably, not how**. Where the RTL finds a better way, the spec follows it; nobody should feel strongly about a default. So a statement here of a mechanism (a buffer, an order of operations, a counter's exact attribution) is the default, and an implementation that is observably the same, or better and reported back, is welcome.

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
- **Alignment.** A 64-bit access is **naturally aligned** when its byte address is a multiple of 8 (its chunk address a multiple of 2), a 128-bit access when it is a multiple of 16 (chunk address a multiple of 4). Every access in every region is naturally aligned (3.1, 4.1); a misaligned one is UB (meshsim), and only a misaligned jump target traps (4.11).
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
- **The first 16 bytes of data memory are reserved** (owner, 2026-10-28; 16 bytes since 2026-10-29), 0x00000000–0x0000000F: the linker never places anything there, so a C null-pointer store and an all-zero header (an uncounted mode-0 write at address 0, 3.1) land harmlessly, and a 128-bit overlay never covers them: **overlays start at 0x10** (meshsim flags any write to 0x00–0x0F after boot).

---

## 3. Links

### 3.1 Link word (packet)

*Figure 1.4 (at v1.1's 169-bit word).*

Every link carries one **256-bit link word** per cycle in each direction (O, T, 2026-10-28). 182 bits are fields; **74 are reserved**:

| field | bits | meaning |
|---|---|---|
| payload | [127:0] | data: a 32-bit payload in [31:0], a 64-bit payload in [63:0]; the bits above the size are 0 |
| address | [159:128] | the receiver's 32-bit address (2.2): region [31:24], byte offset [23:0]. Used by mode 0; modes 1–5 ignore it, region included (senders put 0) |
| size | [161:160] | the payload's width: 00 = 32 bits, 01 = 64, 10 = 128, so 1 << size chunks; **11 is undefined** (below) |
| counter | [164:162] | the write counter the packet decrements at the receiver (4.12): 0 uncounted, 1–7 `WRITE_COUNTER_1`–`7` |
| mode | [167:165] | what the receiver does (below) |
| src | [174:168] | the originator's endpoint id (2.1), stamped by the injecting hardware |
| dst | [181:175] | the destination's endpoint id |
| reserved | [255:182] | **sent as 0 and ignored by every receiver** |

- **The width is fixed** so later field changes don't resize the links, FIFOs and route blocks again (102 bits in v1.0, 169 in v1.1). **The full 256 bits are stored** in every FIFO and pipeline stage, reserved bits included (owner, 2026-10-28, at Tony's request); trimming the storage to the bits in use, mainly for power, is to be revisited when the design has settled (item 117).
- **Size, counter and mode** keep this order wherever they appear together: bits [167:160] of the link word are bits [24:17] of every header register (`STREAM_COUNT`, `ROUTER_HDR`, a peripheral's `DMA_HDR`; the common header layout, 8.1), so hardware copies them as one 8-bit block.
- A packet with src = 127 is an **idle packet** and carries nothing. A sender supplies dst, mode, counter, address, size and payload; the router adds src.

**Size.** Every packet names its payload's width. The address generator stamps it from the stream's read size (4.2), the send port from the send register used (4.11), the read port from `SRAM_RD_DEST_ADDR` (4.3), and a peripheral's DMA from its own choice (4.17.9). One element of any width is one packet. **Size 11 is undefined behaviour:** no hardware checks it (meshsim errors) (owner, 2026-10-29: as v1.1). Only a `STREAM` entry with read size 11 (4.2) can produce one; the send port has no size-11 register.

**Packet modes** (owner, 2026-10-28). The region names where a write lands (2.2) and the counter field does the counting (below), so the 3-bit mode keeps only what the receiver must do differently:

| mode | packet | uses the address | lands | counted |
|---|---|---|---|---|
| 0 | **write at address:** the payload, 32, 64 or 128 bits by its size, at the address, in any region a packet may reach (2.2): data or program memory through port B (4.11), big SRAM (4.9), a peripheral's region (4.17.5). **Commands and replies are mode-0 writes** to the command and reply blocks in data memory (4.12) | yes | the region named | by its counter field |
| 1 | **big-SRAM write by source:** the payload, 32, 64 or 128 bits by its size, at `PTR[src]`, then `PTR[src]` += 1, 2 or 4 (its size in chunks) | no | `PTR[src]` | by its counter field |
| 2 | **bucket write:** as mode 1, at `PTR[t]`, *t* = the payload's bucket digit (4.15) | no | `PTR[t]` | by its counter field |
| 3 | **augmented write:** a 32- or 64-bit packet's low 32 bits (the key) and the ordinal, (target chunk address − `DMA_RECORD_BASE_ADDRESS`) / 2, as one 64-bit record at `PTR[src]`, then `PTR[src]` += 2 (4.9). A 128-bit mode-3 packet is a bad packet | no | `PTR[src]` | by its counter field |
| 4 | **gather request** (local only): payload = (key, ordinal), the ordinal in bits [51:32] (sent at 64 bits); the GATHER engine reads the *k*-record value at `DMA_RECORD_BASE_ADDRESS` + 2*k*·ordinal and injects it to the tile itself as mode-1 packets: *k* of 64 bits when *k* is odd, *k*/2 of 128 bits when even (4.4) | no | GATHER engine (4.4) | **never;** its counter field is passed on to the gather values it produces, which are counted as they pass the director |
| 5 | **count only** (owner, 2026-10-29): writes nothing, any size; the payload and the address are ignored (the smallest packet is the natural choice). From a stream the address generator reads its source as usual and the payload is discarded; to the tile itself it takes a read token like any loopback packet (4.5). Its uses: **the fence** and **"done" signals between tiles** (below) | no | nothing | **by its counter field**; with counter 0 it does nothing (meshsim flags it) |
| 6–7 | reserved | — | **UB** (meshsim) (owner, 2026-11-01, under the principle at the top); v1.2 promised they were discarded at the destination with no error and no decrement | never |

The general write is 0; the writes that land through `PTR` and step it are 1–3; the gather request follows them, and count only, the other mode that writes nothing, comes last. **There is no invalid code**, so an all-zero header (a `STREAM` entry, `ROUTER_HDR` or `DMA_HDR` left at 0) is a valid, uncounted mode-0 write to address 0x00000000, in the reserved first 16 bytes of data memory (2.2) (meshsim flags it). A counted packet in a reserved mode leaves its counter short and the program waits forever with no error (accepted; meshsim flags it).

**Counting** (owner and Tony, 2026-10-28; seven counters, 2026-10-29; 4.12). **The packet names its counter.** For modes 0–3 and 5, a counter field of 1–7 decrements that `WRITE_COUNTER` at the receiver, **K cycles after the traffic director accepts the packet** (4.8, 4.12); 0 is uncounted. Counting no longer depends on the mode or the source. A gather request is never counted (above), and a bad packet is dropped uncounted (below).
- **A counted packet for a counter at 0** raises **unexpected arrival** (4.13) when its decrement happens, K cycles after acceptance (4.12).
- **Uncounted traffic is mode 0 only** (owner, 2026-10-28 and 2026-10-29): commands (4.12) and boot overlays into controller memory (6.2), all at explicit addresses. **Big-SRAM data is always counted,** staging included (6.2). **Modes 1–3 are always counted**, and **a running program sends no uncounted writes except commands**: anything whose landing matters is counted (meshsim checks both, 9).

**The fence and "done" signals** (owner, 2026-10-29). DMA run done says that a run's last table read is complete (4.10), not that its packets have left; some may still be in big SRAM's read pipeline. So **when something must follow a run's packets, the run ends with a count-only packet to the tile itself, on a spare counter**: a sequential run injects its packets in order, so when the fence's counter reaches 0, everything earlier in the run has been injected, and whatever the core sends next follows it to each destination (one kind only, 3.2). In a group run the fence goes in the stream that ends last. It costs one packet per run that needs it. Putting the packet in the run instead (as the boot does, 6.2) remains valid. **Between tiles**, one count-only packet on the receiver's armed counter says "done" or "your data is all sent", instead of a reply that writes a word nobody reads.

Where the address is unused, senders put 0.

**Bad packets** (owner and Tony, 2026-10-28). The traffic director drops a **bad packet** without writing or counting it and sets `ERRORS` [1], "bad packet" (4.13). The hardware checks only the region decode (and mode 3 at 128 bits); **a packet misaligned to its size is UB** (meshsim) (DECISIONS-v1.25 section 8b), no longer a bad packet. Whether the region-decode check passes the test of "How the hardware treats software bugs" (top) is open, for the owner and Tony *(item 142)*:

| case | hardware |
|---|---|
| a 64- or 128-bit packet not naturally aligned (2.2), mode 0 in any region | not checked: **UB** (meshsim stops) (DECISIONS-v1.25 section 8b); v1.2: bad packet |
| mode 0 to region 0x02 (the boot ROM), 0x07–0xFE, 0xFF, or an absent peripheral's region | bad packet (owner, 2026-10-29) |
| mode 3 at 128 bits | bad packet (as v1.1) |
| modes 1–5 | ignore the address, region included (as v1.1; mode 5 too) |
| an offset past the end of its memory | not checked: undefined (2.2) (meshsim errors) |
| size 11 | not checked: undefined (meshsim errors) |
| modes 6–7 | not checked: UB (above) |

A write through a misaligned `PTR` (modes 1–3) is UB too (4.9; v1.2: dropped by the write engine, still counted, [1]).

**Two principles shape the table:**
- **At most one streaming reader of big SRAM per tile at a time,** plus the dependent single reads of the GATHER engine and the controller read port. There are no remote pulls: gathers are local only (mode 4), and the controller reads only its own big SRAM (4.3).
- **Bulk movement is a push.** Anything larger than one word is requested with a small descriptor and delivered by the owner's address generator. A tile's reads of LPDDR follow the same shape: a request on the request bus (3.3), answered by the memory system's push (6.7).

### 3.2 Latency, throughput and ordering

*Figures 4.3, 8.1.*

Each link direction is a fixed-delay pipe: a packet placed on it at cycle t is available to the far end's route block at cycle t + k. One packet per cycle per direction, no backpressure, no acknowledgement. Packets are never dropped or reordered by a link.

- **Every link takes exactly 3 cycles** (`link_latency` = 3): three register stages from the output selector to the far route block's input. Shorter links are padded with pipeline stages: 256 flops per stage per link direction (3.1). The longest link must fit in 3 cycles, which is a physical-design check. Uniform latency keeps every tile's packets in lockstep, so the schedule's per-window load bounds hold exactly (4.2).
- **A hop costs `link_latency` + 1 = 4 cycles:** the route block and the FIFO write take the cycle after the packet arrives (4.6, 4.7).
- **Start skew:** tiles may start a run up to 1 cycle apart, despite the balanced GO net. On a link this adds at most one packet from an adjacent window, so queues grow by about 1 at a window boundary and drain within the next. The simulator models each tile with a random 0 or +1 cycle offset.

**In-order delivery per sending unit** (owner, 2026-10-29). **Packets from one sending unit in one tile, to one destination, of one kind (below), arrive in the order that unit sent them:** routes are fixed per (source, destination) and FIFOs never reorder (4.6, 4.7). The boot sequence (6.2), the command convention (4.12) and the landing of multi-packet values through `PTR` (4.9) rely on it.
- **A tile's sending units** are the **address generator** (stream and group runs, 4.2), the **send port** (`ROUTER_HDR`, `ROUTER_SEND_*`, 4.11), the **GATHER engine** and the **read port** (their packets to the tile itself, 4.3, 4.4), and **each sending peripheral's DMA** (on its own router port, 4.17.9). A controller's units are the same.
- **Between two units there is no ordering**, even in one tile: a send-port packet stored after a run's DMA run done can be injected before the run's last read returns, which may still be in big SRAM's pipeline (4.5, 4.10). A program that needs one unit's packet to follow another's **ends the run with a fence** (a count-only packet to the tile itself, 3.1) and waits for its counter, **or puts the packet in the run** (as the boot does, 6.2).
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
- **the receive queues, at the root, one per target** (owner, 2026-10-29): the root arbitrates and places each request in its target's **receive queue**, which belongs to the request tree, not to the receiving unit; the receiver reads it through its request port's receive registers (4.18). The root keeps **one credit counter per target**, set to its queue's depth (below).

Every tile is a leaf at the same depth, two nodes from the root. **The tree is its own unit:** many leaves, a few roots, and its queues are sized by each target's traffic rather than by the tile hardware.

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
| 3 | the Updater/Binder/RW engine, C&C 2's peripheral *(Deferred: whether it stays a target of its own, and its queue, come with the file system, item 119)* |
| 4–7 | reserved |

A request for a reserved target, or a target with no receiver, is dropped at the root (meshsim flags it, 6.7).

**Framing.** There is no header byte and no size: a request is **8 beats** of 16 bits, bits [15:0] first. Beat 0 carries the requestor and the target, so a node knows where a request goes before the rest of it arrives *(Proposed)*.

**Arbitration** (owner, 2026-10-26, for the root's skip; the rest *(Proposed)*). Each node has a **16-byte buffer per input**, holding one whole request, and one output, and is **wormhole-routed per request**: once it has chosen an input it passes that request's 8 beats, and only then chooses again. A request's beats stay together all the way to its target's queue.
- **A row node** round-robins over its inputs holding a request's first beat, from the one after the input it last served, and writes the requestor, 8*r* + *c* for its row *r* and the input's column *c*.
- **The root** round-robins the same way over its inputs (the eight row nodes and the controllers), **skipping an input whose head request's target has no credit**, writes the requestor for a controller's request, and forwards each request whole (8 beats) to its target. So a busy receiver never holds up the root's requests for another target.
- **Credits** (owner, 2026-10-29): each target's counter starts at its receive queue's depth; forwarding a request takes one, and the receiver returns one with each `REQUEST_RECEIVE` pop (4.18), so a receive queue never overflows.
- **Head-of-line blocking would stay inside a row** (owner, 2026-10-26, accepted and noted): a request waiting for a target with no credit holds its row node's link to the root, and with it every request from that row behind it, whatever their targets. **The queues' depths and the per-tile limits (below) keep normal traffic from ever exhausting a target's credits,** so in normal operation the root never skips, no row stalls, and traffic for one target never blocks traffic for another (owner, 2026-10-29).
- **Fair and starvation-free:** every tile is a leaf at the same depth and every node round-robins over its occupied inputs, so under full load each of the root's inputs gets an equal share of the requests it can take, and each tile one eighth of its row's.
- **Throughput:** the root passes one beat per cycle, so the whole chip issues at most one request every 8 cycles [derived]. That is ample: a 4 KB read every 8 cycles would be 512 B per cycle, far beyond LPDDR [derived].
- **Ordering:** a sender's requests reach their target's queue in the order the sender issued them (one path, FIFO buffers, whole requests), so requests from one sender to one target stay in order. There is no order between senders.

**The receive queues and the per-tile limits** (owner, 2026-10-29). Each target's queue is in the tree, at the root, in 128-bit entries, every depth a power of two:

| target | receive queue | requests outstanding per tile |
|---|---|---|
| 0, C&C 1 | 128 requests (16 Kbit) | 1 |
| 1, C&C 2 (the file system) | 256 requests (32 Kbit) | 3, so a tile can have several log streams in flight |
| 2, C&C 3 (the pager) | 128 requests (16 Kbit) | 1 |

- **"Outstanding" is a software convention, not a hardware state** (owner, 2026-10-29): a request is outstanding until its reply has landed, and **a read, which has no reply, until its data has landed** (its data counter done); a tile issues its next request to a target only after that. Every request names its reply's counter and address (6.7). In hardware a request holds a slot in its target's receive queue only from delivery until the target pops it, well before any answer, and the tile can't see the pop; so the convention is deliberately conservative, and it implies the bound. 64 tiles at those limits fill 64, 192 and 64 entries, and **the controllers' own requests to each other fit in the 64 entries left at each target.**
- **The limits are a software rule** (meshsim checks it, invariant 17); the hardware only counts credits.
- **Error reports are the exception** (accepted): a failed unit's handler sends its fixed set of reports (4.13: 24 from an image's handler, 2 from the boot ROM's) with no replies and no limit. In a mass failure they can fill C&C 1's queue and stall rows behind it, but the chip is going down and C&C 1 drains them one by one (6.1).
- **Units that aren't targets need no receive queue;** their receive registers read an empty queue (4.18).

**Back-pressure end to end.** When a target has no credit (its receive queue is full, or will be with the requests already forwarded) the root skips the inputs whose next request is for it; a row node whose request the root skips stalls behind it, a tile's port stops draining its request FIFO, and a send store to a full FIFO stalls that tile's controller (4.18). Nothing is dropped anywhere on the bus. Under the per-tile limits this happens only in a mass failure.

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
| 4.11 | controller, with its program and data memories and the boot ROM |
| 4.12 | the command and reply blocks, the write counters and the events |
| 4.13 | status and errors; traps |
| 4.14 | commit ordering |
| 4.15 | digits |
| 4.16 | the fifth router port (every tile; connected at tiles 0–2, to C&C 1–3) |
| 4.17 | peripherals: up to three per tile, the standard window, the DMA (4.17.1–4.17.9) |
| 4.18 | the request port (the tile's end of the request bus, 3.3), send and receive |
| 4.19 | the replayer, peripheral type 4 (4.19.1–4.19.15) |
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
- **A misaligned access is UB** (meshsim) (DECISIONS-v1.25 section 8b): the hardware does nothing special, and meshsim stops with an error naming the access. That covers **a packet** misaligned to its size at the director (3.1), **a write through a misaligned `PTR`** (4.9), **a misaligned read** by the address generator, the GATHER engine or the read port (4.2–4.5), the controller's **misaligned loads and stores** (4.11) and the replayer's misaligned window-0 writes (4.19.10). v1.2 checked them in hardware ([1]: a bad packet dropped uncounted; a write dropped but still counted; a read not performed, its packet sent with payload 0; a bus error); every such outcome is gone. Only **a misaligned jump target** still traps (4.11).

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
- **Start loads the run** described in `DMA_CONFIG`, clears `rd_done`, sets `rd_running`, clears **DMA run done** (`EVENTS` [10], 4.12), so [10] always refers to the run most recently started, and begins the table read-ahead (below). GO does not start a run; start is the only way.
- **The first request is offered as soon as its entries are in.** A start store in cycle *t* takes effect in cycle *t* + 1 (4.11).
  - **Group run:** the first request is offered **4·`ts_engine_latency` + 3 cycles after the start store** (4·`ts_engine_latency` + 2 after the cycle the start takes effect; 7 and 6 at the default). The run reads the `GROUP` entry, then the `STREAM` entry (its three arrays read in parallel, 4.10), which needs the group's start stream; each read is a round trip of 2·`ts_engine_latency` + 1 [meas: meshsim, as the read-ahead's latency].
  - **Stream run:** the first request is offered **2·`ts_engine_latency` + 2 cycles after the start store** (2·`ts_engine_latency` + 1 after the cycle the start takes effect), the time of one `STREAM` read.
  - **There is no fixed lead-in counter:** the time depends only on the run kind, the tables and the latency, so it is the same on every tile and a synchronised start stays in lockstep.
- **`rd_running` = 1 and `rd_done` = 0 from the end of the start store's cycle,** so the next instruction's load of `STATUS` already sees them; `rd_running` stays 1 through the lead-in until the run has finished (below). (A helper that polls `STATUS.rd_running` to learn whether the tables are free relies on this, 4.10.)
- **A start while a run is running is UB** (meshsim) (owner, 2026-11-01, under the principle at the top), lead-in included; v1.2 ignored it.
- **A local run** is one start store. **A synchronised start** is the same start store, executed by every tile on the same cycle after GO wakes it (6.3).
- **Finishing:** the run ends when the address generator has **walked its last entry**: every stream of a stream run, every group of a group run, including trailing count-0 streams and R = 0 groups, each at its timetable cost. The last entry's gap-after is not waited out. The sequencer cannot know a request is the last without examining the entries after it, so it simply walks them. Then, **once the run's last table read has completed** (guarantee B, 4.10), `rd_running` = 0 and `rd_done` = 1, **in the same cycle as DMA run done**, `EVENTS` [10] (4.12), so polling `rd_running` means exactly what the event means (owner, 2026-10-29); `rd_done` stays 1 until the next start.
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
| `STREAM_DEST` | destination address | [31:0] | **mode 0 only:** the first element's 32-bit byte address at the receiver (2.2), aligned to the read size, + 4, 8 or 16 bytes per element by read size; ignored for modes 1–5 |
| `STREAM_COUNT` | count | [16:0] | elements in the stream's run (0 = padding), at most 131,071 |
| `STREAM_COUNT` | read size | [18:17] | 00 32 bits, 01 64 bits, 10 128 bits (1 << size chunks per element), 11 reserved and undefined; stamped as every packet's size (3.1) |
| `STREAM_COUNT` | counter | [21:19] | the counter field stamped on every packet (3.1): the receiver's write counter, 0 uncounted |
| `STREAM_COUNT` | mode | [24:22] | the packet mode stamped on every packet of the stream: 0–5 (mode 4 only with *d* = 0; mode 5 reads its source and discards the payload, 3.1) |
| `STREAM_COUNT` | destination | [31:25] | *d*, 7 bits: 0–63 a **relative** tile offset, the packets going to (`me` + *d*) mod 64, *d* = 0 the tile itself; 64–125 a non-tile endpoint, named **absolutely** (2.1): 64–66 the controllers, 67–125 reserved (no endpoint); **126 the broadcast** (below); 127 invalid |

**A controller's relative *d*** (CL6, W14-05): on C&C *n* (endpoint 64 + *c*, *c* = *n* − 1) a relative *d* counts from its tile *c*, so C&C 1's *d* = 0 is tile 0, not itself; a controller copying into its own memories names itself absolutely, *d* = 64 + *c*.

`STREAM_COUNT` [24:17] is the link word's size, counter and mode block, bits [167:160] (3.1), laid out as in every header register (8.1); [31:25] is the relative *d*, translated into dst rather than copied. The fields that change every round are still the source and the count, one store each, with the fixed bits (gap after; size, counter, mode and *d*) ORed in.

- **Read size.** Each element is one packet of the read size:
  - **32 bits:** one chunk per element, a 32-bit packet. This is how raw 32-bit keys are sent one per packet (step 1 of the sort, 4.9).
  - **64 bits:** one record per element, a 64-bit packet.
  - **128 bits:** one row's worth per element, **one 128-bit packet**, so a visit takes 1 cycle like the others. (v1.0 sent two 64-bit packets on consecutive cycles and took 2.)
  - **Reserved encodings are errors wherever they appear:** a stream with read size 11 (undefined: what it reads and sends is not specified), with mode 6–7, with *d* = 67–127, or a mode-4 stream with *d* ≠ 0, is an error even at count 0. So is a mode-3 stream with read size 128, or a mode-0 stream whose destination address isn't aligned to its read size, and a stream whose source isn't aligned to its read size (4.1). None has a hardware check at the sender (meshsim reports them); a misaligned source or destination is UB (4.1).
- **Gap after** *(likely to change)*. In a stream run, the address generator issues nothing for this many cycles after the stream's last element, for example 4 on every stream of a one-word exchange (above). Group runs ignore it and use the group gap.
- **Element addresses are computed, never stored.** Element *e* of a stream (*e* = *j* in a stream run, *r* in a group run) is read at source + *e* × (chunks per element: 1, 2 or 4), a shift and a 21-bit add. The source is aligned to the read size, so every element is (4.1); a misaligned source is UB (meshsim) (DECISIONS-v1.25 section 8b). For mode 0 the destination address is `STREAM_DEST` + *e* × (4, 8 or 16 bytes), by plain addition; a run past the end of its memory is a software bug (2.2). The address generator never writes `STREAM`, so a run leaves its entries as the controller wrote them. There is no base register: 21 bits of chunk address cover up to 8 MB.
- **Receiver-placed destinations.** For modes 1–3 big-SRAM destinations are chosen by the receiver: modes 1 and 3 land at `PTR[src]` and mode 2 at `PTR[bucket digit]`. A local copy is a mode-1 stream to *d* = 0 with `PTR[me]` as the destination, or a mode-0 stream to an explicit address in the tile's own region 0x03. A mode-4 stream is a gather stream to the tile itself (4.4).
- **Relative destinations** make the destination, mode and size of every stream identical on every tile for the 16-group schedule, which is defined in offsets. The cost is a 6-bit adder and a select on *d*'s top bit; packets on the wire carry the absolute dst.
- **The address generator sends to tiles 0–63, to the controllers** (*d* = 64–66) **and to the broadcast** (*d* = 126; owner, 2026-10-29). In hardware a broadcast stream needs no special case: its packet to its own tile takes a read token like the send port's broadcasts (4.5). **By convention only the controllers broadcast from streams**, as C&C 1 does at boot, streaming overlays from its big SRAM at a packet per cycle (6.2); **meshsim errors loudly on a stream with *d* = 126 from a mesh tile**. *d* = 67–125 names no endpoint and 127 is invalid; both are programming errors with no hardware check (meshsim flags them).
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
- **Gap: 0 cycles** between groups. Gap 0 gives the same queue peaks as gap 4 on uniform and on all the skewed sets, with no drift [meas: meshsim], and saves ≈ 60 cycles per all-to-all. **Exception:** phases that run every stream at the full window (the two-hop layers TH and BT, section 14) need a **gap of 4 after each group**, or the receivers' eject FIFOs overflow at group boundaries [meas: meshsim, the sort program; peaks 6 on links and 4 at eject with gap 4; v1.0 measurement; to be re-measured in v1.2].
- **Measured** against a nine-group schedule [meas: meshsim, `AllToAllTest measure`; v1.0 measurement; to be re-measured in v1.2]:

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

**A visit** (modes 0–5) offers one read request per element, of any read size, and the element returns as one packet:
- payload = the element (0 if its read was misaligned, above);
- size = the stream's read size;
- dst = (`me` + *d*) mod 64 for *d* < 64, or *d* for *d* = 64–66 and 126;
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
- **Alignment** (integrator, 2026-10-29). The read address and the destination must be aligned to the size (4.1); **either misaligned is UB** (meshsim) (DECISIONS-v1.25 section 8b). (v1.2 sent a misaligned read's packet with payload 0 and set [1], and a misaligned destination was a bad packet; v1.1 dropped the read and set [7].)
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

  With the protected loopback (4.5, 4.7; required, owner, 2026-10-28) the loopback FIFO cannot overflow, so the half-rate case becomes a delay rather than a loss; and since the eject output now serves incoming packets before the loopback (4.7), results no longer take slots from a full-rate stream either. The rule stands until the simulator re-measures both cases (items 24 and 70). **meshsim has now re-measured** [meas: meshsim, part M]: **no loss** when read-port reads overlap incoming traffic, either an all-to-all or a full-rate stream, where v1.0 lost 561 packets. The rule is **unchanged for now and recorded as a candidate to relax** (item 116).

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
3. **For each request the GATHER engine puts its gather-value reads in the SRAM read queue** (4.5), **128 bits at a time when *k* is even** (DECISIONS-v1.25 section 8c): the value starts at `DMA_RECORD_BASE_ADDRESS` + 2*k*·ordinal, and
   - ***k* odd:** *k* reads of 64 bits, at that start + 2*i* (*i* = 0 … *k* − 1), as in v1.2;
   - ***k* even:** *k*/2 reads of 128 bits, at that start + 4*i* (*i* = 0 … *k*/2 − 1).
   They rank above the address generator and the controller read port. No field chooses: *k* does.
4. **Each return goes out as a mode-1 packet of its read's size (64 or 128 bits) to the tile itself** (dst = `me`, address 0), **carrying the request's counter field**: a **gather value packet**. A value is so *k* packets when *k* is odd and *k*/2 when even: its **packets per value**.
5. **The gather values land at `PTR[me]`,** which advances by 2 per 64-bit packet and by 4 per 128-bit one (4.9). The controller sets it to the start of the reorder buffer. The index arrives in sorted order and the output is one sequential run, so value *i* lands at the output start + 2*k*·*i* with no addresses.

**Parameters.**
- **`DMA_RECORD_BASE_ADDRESS`** (8.7): in a gather run, a 21-bit chunk address, the start of the value buffer, **128-bit aligned (a multiple of 4), whatever *k* is** (DECISIONS-v1.25 section 8c). Ordinals count 0, 1, 2, … from the base of the run that augmented the keys (4.9), so value *i* is at the base + 2*k*·*i* with no offset arithmetic. One base serves each run: no run both augments and gathers.
- ***k*:** the value size in records, **1–8**, a `DMA_CONFIG` field (*k* − 1 in [26:24], 8.5).
- **`PTR[me]`:** the output start, **128-bit aligned too, whatever *k* is** (DECISIONS-v1.25 section 8c). **Gather buffers are 128-bit aligned**, source and destination; misalignment is UB (4.1). The software convention is that allocations are 128-bit aligned in general (`fw/CONVENTIONS.md`).

**Datapath.**
- **Each request-queue entry holds only the 20-bit ordinal** (payload bits 51–32) and the 3-bit counter field; the key half and bits 63–52 are dropped.
- **The start address** `DMA_RECORD_BASE_ADDRESS` + 2*k*·ordinal is computed in the cycle a request reaches the queue's head, and its first read is offered the next cycle. The value's reads follow, one offered per cycle at priority 1 (4.5), each stepping the address by its size (2 or 4 chunks) and pushing a token that sends the value packet back to the tile as mode 1. With 128-bit-aligned buffers every read and every landing is aligned; anything else is UB (4.1). Running past the end of big SRAM is a software bug (4.1).
- **A mode-4 packet arriving with the queue full** is dropped and sets GATHER queue overflow, `ERRORS` [6] (4.13).
- **GATHER busy** (`STATUS` [2], 8.2): the request queue is non-empty, or a request is part-way through its reads. Reads issued but not yet landed are not included; their write counter covers their landing.

**Counting** (owner, 2026-10-28). Gather requests (mode 4) are **never** counted, whatever their counter field says: they aren't writes. **Their counter field is passed on to the gather value packets** (mode 1), which are counted like any other write as they pass the traffic director (guarantee C, 4.10): **one decrement per gather value packet**, so a gather of *n* values arms that counter with ***n* × (packets per value)**: *n*·*k* when *k* is odd, *n*·*k*/2 when even (DECISIONS-v1.25 section 8c). For even *k* the arming is not *n*·*k* (accepted, owner). Its done event, and counters done (4.12), follow K cycles after the last gather value's acceptance.

**Rate.** Every value costs 1 request packet plus its gather value packets through the one injection point and the one eject, so **≈ (packets per value + 1) cycles per value**: **2.0 at *k* = 2** (v1.2: about 3), about 131K cycles per 64K values instead of 196K [meas: meshsim, WORKER11, Quest 089: the full-size sort p4/uniform-k128 fell by 131,074 cycles, section 14] (DECISIONS-v1.25 section 8c). The value reads (packets per value, 64 or 128 bits each) and the gather values' writes fit within one access per cycle per area if index, values and output sit in three different areas (4.1).

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

**Tokens and the return queues.** There are **64 tokens**, one pool shared by the three clients, and every read the queue takes holds one. The **address generator** and the **GATHER engine** each have an **in-order return queue**: a plain queue of that client's held reads' metadata, the request's fields, in the order its reads were taken. A read-port read also holds a token; its metadata travels with the read and carries a read-port bit (4.3). The return queues replace v1.1's earlier single token FIFO in request order (item 74).
- **Each ordered client's returns come back in its own order** (4.1, owner, 2026-10-23): the big SRAM hands back the address generator's reads in the order it issued them, and the GATHER engine's in theirs, so each such return pairs with the head of its client's return queue and goes to injection with it. The order is required within a client: mode-1 packets and gather values land at `PTR` in arrival order, so a stream's packets must leave in the order the stream issued them. **Between clients there is no order,** so a gather's scattered reads never wait behind a stream's sequential ones. **Read-port returns come back in any order** (4.3). (In v1.0 each token had a 128-bit data slot and a valid bit, the areas returned rows in any order into their tokens' slots, and the head was released when its data was valid.)
- **Injection order among the clients**: in a cycle when more than one client has a return ready, the injection point takes them by the request priority above, gather value first, then the address generator, then the read port. A return that loses waits, and its client's later returns wait behind it.
- **One client per order-placed destination** (owner, 2026-10-23). Order holds only within a client, so **a tile never has two clients sending order-placed packets to the same destination at once**: modes 1, 2 and 3 into the same `PTR` entry of the same receiver, or writes into the same order-placed port of a peripheral (a replayer queue, 4.19.10). Such a mix would interleave unpredictably. The clients are the address generator, the GATHER engine and, as a source of packets, the send port (4.11) (owner, 2026-10-23, item 80). **A peripheral's order-placed port takes one source at a time** across sources too (owner, 2026-10-26): a peripheral's region may be written from any source (4.17.5), so no two tiles, or a tile and a non-tile endpoint, feed the same queue port at once (meshsim flags a violation, invariant 18).
- **A token is freed** when its packet is injected, **except a loopback packet's** (required, owner, 2026-10-28): a packet for the tile itself (dst = `me`) holds its token until it leaves the loopback FIFO for the eject selector (4.7). **The send port's loopback packets take a token too:** a send-port packet for the tile itself, or a broadcast (whose copy for the tile itself goes through the loopback), waits in the holding register until a token is free, takes it when injected and holds it the same way (4.11). A broadcast stream's read (4.2) already holds a token, which it keeps until its own copy leaves the loopback FIFO. A count-only packet to the tile itself (a fence, 3.1) takes a token like any loopback packet, from a stream or from the send port.
- **So the loopback FIFO cannot overflow,** whatever arrives from the links: at most 64 packets are ever on their way into it or in it, and it is at least 64 deep (4.7). When the eject output is busy with incoming traffic, the tile's own loopback reads wait for tokens instead. In a group run that wait is a refusal like any other (drift, 4.2), so the all-to-alls are unaffected in a correct program: a tile's stream to itself has its own slot in the schedule (`spec/schedule.md`), which leaves its eject room for it (measured: no token waits, item 62). **This exists only because the mesh has no flow control** (3.2); with flow control it would be revisited.
- **Chunk selection.** Every access is naturally aligned (4.1), so the data is taken from the one returned row, starting at the address's chunk.
  - a 32-bit read gives one chunk, a 32-bit packet (payload [31:0]);
  - a 64-bit read gives two consecutive chunks, a 64-bit packet;
  - a 128-bit read gives four consecutive chunks, **one 128-bit packet**. (v1.0 sent them as two 64-bit packets on consecutive cycles.)
- **A misaligned read** is UB (4.1; v1.2 sent its return with payload 0). **An out-of-range read** is undefined (4.1).

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

There are five outputs: to-right, to-left, to-down, to-up and eject. Each has one FIFO per possible feeder, **16 entries** *(param `queue_depth`)* of one **256-bit** link word (3.1), except the eject output's **loopback FIFO**, which is **at least as deep as the token count** (4.5): 64 entries at the default (required, owner, 2026-10-28):

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
| mode 5 | count only: nothing is written; its counter field is counted like any write's (3.1) |
| modes 6–7 | UB (3.1; meshsim) |

**Counting: the delay line** (owner and Tony, 2026-10-28). For each packet it accepts in modes 0–3 or 5 with a counter field of 1–7, the director puts the counter's id into a **K-stage delay line**, and K cycles later that `WRITE_COUNTER` is decremented (4.12). A counter field of 0 decrements nothing. At most one packet per cycle leaves the director, so a counter takes at most one decrement per cycle. Unexpected arrival is judged at the decrement (4.12).

**K** (integrator, 2026-10-29; owner, 2026-10-29) is a parameter, **default 8**, large enough that by the time a packet's counter decrements, **its write is in place**:
- **controller memory:** port B lands a write `cm_latency` = 2 cycles after the director [built];
- **a peripheral** lands it within `rp_latency` = 2 cycles (4.17.8);
- **big SRAM:** a write only has to have **entered its area's request queue**, which is served in arrival order, so a read issued after the counter reaches 0 queues behind it and cannot overtake it (4.1). With spanning gone (4.1) the write path is fixed, and its loaded worst case in meshsim's model is **2·L + 4** (W4-08) cycles, L = `ts_engine_latency` [derived];
- **the tables:** a `PTR` or `HIST` update is complete **L + 4** cycles after acceptance [derived] (guarantee A, 4.10); of the two, only `HIST` is loadable through the table window.

So in **meshsim's model**, **K ≥ max(2·L + 4, L + 4, `cm_latency`, `rp_latency`)**: 6 at the default L = 1, and K = 8 (meshsim's default) covers L ≤ 2. *(Revised 2026-11-01, W4-08: meshsim's big-SRAM write path measures 2·L + 4, one cycle more than the earlier count of 2·L + 3; at L = 3 and 4 the earlier formula left K a cycle short and the radix sort's back-to-back bucket passes read stale chunks.)* meshsim errors if K is set below its formula, and its S1 measurement checks the margin (`measure_s1`, `tests/mach.h`).

**K in hardware is the implementation's** (owner, 2026-11-01): the requirement is only that K covers the implementation's slowest write path (the four destinations above). The formula is meshsim's reference, not a hardware promise; the hardware sets its own K from its measured paths, and that comes back as part of the RTL's delta.

**One K for every destination, deliberately** (owner, 2026-10-31). Writes to controller memory and peripherals land in 2 cycles, but their counters also decrement at K = 8, so a round trip through a counter costs about 6 cycles more than v1.1, which counted those writes at landing. Measured (meshsim, a read-port search step): 35.9 + 37.42 per active search, against v1.1's 29.8 + 37.43. **The extra is per step, shared by every search active in it, not per query**: about a hundred cycles per split, a few hundred per sort. A separate 2-cycle delay for controller memory and peripherals would win it back but is **not adopted**; it would be reconsidered only if the cost became per query (the v1.3 log's candidate C1).

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
- **Stepping by size is general.** In-order delivery means a value sent as several mode-1 packets from one sender lands contiguously, whatever their sizes; the gather values of a *k*-record value are *k* 64-bit packets when *k* is odd and *k*/2 128-bit ones when even (4.4).
- **Alignment** (integrator, 2026-10-29). A `PTR` must be aligned to the size of the **writes** landing through it: for mode 3 to a record (even), whatever the packets' size (4.1). **A write through a misaligned `PTR` is UB** (meshsim) (DECISIONS-v1.25 section 8b); v1.2 dropped it, still counted, set [1] and advanced the pointer.
- **The pointer round trip.** Every write reads and updates its pointer in `PTR`; the packet waits in a **pointer stage** until the old pointer returns, across `ts_engine_latency` each way. The stage holds **2·`ts_engine_latency` + 2 entries** (4 at the default 1) [meas: meshsim, peaks 3, 5, 7, 9 at `ts_engine_latency` 1–4].
- **Mode 0 writes at an explicit address.** Its byte offset / 4 is the chunk address. It passes the pointer stage in order with the writes around it, so the engine's writes still reach the areas in arrival order, but it makes no `PTR` access and moves no pointer. It writes the payload at its size. A misaligned one is UB (4.1); out of range is undefined (4.1). Count-on-arrival ignores it. The memory system delivers reads into big SRAM this way (6.7), boot and staging images land this way (6.2), and a controller can write a chosen address from the send port without touching `PTR[me]`.
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
- **The local side** (the address generator reading `STREAM` and `GROUP`) is covered by **DMA run done**, `EVENTS` [10] (4.2). The counters alone are not enough, since a tile's arrivals can finish before its own run.
- **v1.1's `ts_busy` and `wait_tables()` are gone.** A helper that must work without knowing what ran before (a pass that starts by checking the tables are free) **may poll `STATUS.rd_running`**, a level, as a software choice (owner, 2026-10-28); otherwise `STATUS` is diagnostic.

The hardware guarantees this needs:

| # | guarantee |
|---|---|
| A | **K covers the table update:** by the time a packet's counter decrements, its `PTR` or `HIST` update is **complete**, meaning readable by a controller load through the window, `HIST`'s count-on-arrival included, behind its request FIFO (4.8) |
| B | **DMA run done fires only after the run's last table read has completed,** not when the last entry is walked, and **`STATUS.rd_running` falls in the same cycle** (owner, 2026-10-29), so polling it means exactly what the event means (4.2) |
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
- **CSR instructions are legal** for the CSRs the core implements: the machine-mode trap CSRs, and **`mcycle` and `mtime`** (Tony, 2026-10-29), so software can bound its waits by cycle count. **`mtime`'s rate is implementation-defined** (DECISIONS-v1.2 section 48, W4-07; meshsim counts reference-oscillator cycles through the `time` CSR), so software bounds its waits with `mcycle` and nothing depends on the rate. *(Open, Q1 with Tony, item 121: the rest of the list and the CSRs' reset values, `mtvec`'s in particular.)* A CSR instruction on any other CSR is illegal.
- **Illegal:** divide opcodes (`div`, `divu`, `rem`, `remu`), and CSR instructions on CSRs the core lacks, set illegal instruction, `ERRORS` [10], and trap.
- **Traps** (owner and Tony, 2026-10-28): bus errors, illegal instructions, `ecall` and `ebreak` **trap to the handler** at `mtvec` instead of halting the controller, as do a fetch outside program memory and the boot ROM and a misaligned jump target. **Every trap is terminal** (4.13): the handler never executes `mret`. `ecall`, `ebreak`, the fetch and the jump set no `ERRORS` bit (meshsim stops loudly on each, 4.13).
- **Programs** are C, built bare-metal (`gcc` for `riscv64-unknown-elf`, `-march=rv32i_zmmul_zicsr -mabi=ilp32`, with `WFI` and the CSR accesses from inline assembly) with a start-up file, a linker script and the generated register header `fw/include/mesh.h`. A C `/` or `%` by a non-power of two compiles to a library call. The register block and the peripheral windows sit in the top 2 KB of the map, so a register access compiles to an x0-relative `lw` or `sw`; through the header's struct pointers (8.1) a compiler may add one base-address instruction, the same on every unit (owner, 2026-10-29).

**Timing.** One instruction per cycle.
- Every load and store to data memory completes in one cycle, and the loaded value is usable by the next instruction (load-use 0). **Loads from program memory and the boot ROM take one cycle too, and the 32 KB memories meet one-cycle access at 1 GHz** (T, 2026-10-29; item 134).
- A taken branch or jump costs one extra cycle; a multiply takes 3 cycles.
- A store at t updates the register at the end of t; a load at t+1 and the owning unit at t+1 see it. Status computed at t is readable at t+1.
- **Register-load latency** (owner, 2026-10-26, part E). A load from the register block (0xFFFFF800 and up) or from the table window (0xFFFF2000–0xFFFF35FF) takes a fixed number of cycles (T, 2026-10-29): **a register-block load, every peripheral window word included, takes 2 cycles; a table-window load (`HIST`, `PTR`, `STREAM_*`, `GROUP`) takes 1**, like the memories. (v1.1 allowed 1 or 2 at the implementation's discretion.) It is **fixed per register, every peripheral window word included, for a given implementation: the same on every access and every tile, never data- or traffic-dependent**, so the identical code of a synchronised start (6.3) stays identical in time. Software must not depend on the value: a load returns when its data is ready. Loads from data memory, program memory and the boot ROM are single-cycle. meshsim models exactly these: 2 cycles for a register or window word, 1 for a table-window or memory load (section 10). *Rationale:* the register block's read mux spans units across the tile (router, DMA engine, read port, request port), and each peripheral's window is in its own unit, and the tables are shared with the DMA engine and the address generator, so an implementation may give such a load a second cycle rather than hold it to the core's single-cycle path; the hot paths are store-dominated, so either choice costs little.
- **The sampling rule.** A register or table-window load issued at cycle *t* with latency *L* **returns the state at the end of cycle *t* + *L* − 2** (its sample point) and **takes any side effect at the end of *t* + *L* − 1**, removing exactly what it returned: the controller's `EVENTS` clears the masked events it returned; a peripheral's `EVENTS` subtracts one count from each masked bit it returned set (4.17.3); `REQUEST_RECEIVE` and the UART's RX word pop the entry they returned. This holds for every word of every window (owner, 2026-10-28). Anything set after the sample point survives. With *L* = 1: the state at the end of *t* − 1, the effect at the end of *t*.
- **Stores** to the register block, the peripheral windows and the table window are **single-cycle and posted:** the core doesn't wait, and a store takes effect at the end of its cycle. **Register and table accesses complete in program order,** so a load sees every earlier store. The only stores that stall are a send store on the send port's occupied holding register (`ROUTER_SEND_32`, `_64` or `_128`, below), a request store on a full request FIFO (`REQUEST_SEND`, 4.18), and a store to the UART's TX word on a full TX FIFO (4.20, in C&C 1's peripheral).

**Memories (strict Harvard)** (owner, 2026-10-28; 2.2):

| address | memory | port A | port B |
|---|---|---|---|
| 0x00000000–0x00007FFF | **data memory, 32 KB**; the command block in its top 16 words, 0x00007FC0–0x00007FFF, and the reply block below it, 0x00007BC0–0x00007FBF (4.12) (owner, 2026-10-29); the first 16 bytes reserved (2.2) | controller loads and stores | the traffic director (mode 0) |
| 0x01000000–0x01007FFF | **program memory, 32 KB** | instruction fetch and **loads** (T); **a store is a bus error** | the traffic director (mode 0) |
| 0x02000000– | **boot ROM** (6.2) | instruction fetch and loads | none |
| 0xFFFF2000–0xFFFF35FF | table window (4.10, 8.9) *(Proposed)* | | |
| 0xFFFFF800–0xFFFFFFFF | the controller's register block and the peripherals' windows (8.1) | | |

- **Port B takes one 128-bit write per cycle** (owner, 2026-10-29), or a 32- or 64-bit one, at a naturally aligned address (a mode-0 packet). How the memory is built to do it (banks, macros) is the implementer's choice; the spec states the requirement only.
- **Port A** touches one bank per access.
- **Bus errors.** `ERRORS` [9] is set, and the controller traps, on: a store to program memory or the boot ROM; an access to an unmapped address (regions 0x03–0xFE, the unused parts of region 0xFF, and **any word of an absent peripheral's window**, 4.17); a sub-word access to the registers, a window or the table window; a store to a read-only register; and a load from `PTR`, the `STREAM` arrays or `GROUP` (8.1, 8.9). Write-only registers read 0, except the request port's, whose addresses a load reads as the receive side (8.10). Reserved words in the register block and the windows read 0 and ignore stores (meshsim flags the access). **An offset past the end of data or program memory is not checked** (2.2): the high bits are ignored and the access aliases (meshsim errors).
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
- **At that capture,** `ROUTER_ADDR` advances by the size in bytes (4, 8 or 16) for mode 0, **unless no-increment is set**, when it stays put (owner, 2026-10-28); modes 1–5, which ignore the address, leave it unchanged (owner, 2026-10-29). **No-increment stays set until the program clears it** (owner, 2026-10-29), which it does when it is done with a fixed address. `ROUTER_SET_W0`–`W2` all clear to 0. Both may therefore change while the packet waits. A run of consecutive words costs one `ROUTER_HDR` and one `ROUTER_ADDR` store, then one store per 32 bits of each packet, in ascending word order: `ROUTER_SEND_32` alone, `ROUTER_SET_W0` + `ROUTER_SEND_64`, or `ROUTER_SET_W0`, `W1`, `W2` + `ROUTER_SEND_128`.
- **No-increment** is for writes that go to the same address in many receivers: a reply always lands at `REPLY` + 8 × the sender's id in every receiver (4.12), so a program sets `ROUTER_ADDR` once and changes only `ROUTER_HDR`'s dst per send, keeping an all-to-all of replies at one header store and one send per reply. A change of dst alone is one store; a change of both is two. Commands write slot 0 last, below their arguments, so they store `ROUTER_ADDR` before it.
- **Timing.** A send store at *t* loads the holding register at the end of *t*; its earliest injection is *t* + 1. `send_busy` = the holding register is full. A packet for the tile itself or a broadcast also waits there for a free token (4.5), so it can keep the register full while the loopback is busy. **The store stalls only if the holding register is full and not being injected that cycle**, which gives one packet per cycle when no read return is waiting (gather value, address generator or read port, 4.5). This is a stall rule, not an overflow rule, and deliberately differs from the FIFOs' "count before the pop" rule (4.13).
- **The send port checks nothing:** reserved modes and counter codes, mode 3 at 128 bits, a misaligned or unreachable address, mode 4 to another unit and dst 67–125 go out as written, and the receiver or the simulator catches them (a misaligned address is UB, 4.1).

**The boot ROM** replaces v1.1's resident loader: every controller has a small fetch-and-load ROM in region 0x02, and its reset vector is 0x02000000 (6.2).

### 4.12 The command and reply blocks, the write counters and the events

*Figures 2.1, 2.3 (at v1.1).*

**The command block** (owner, 2026-10-26; mode 0, owner, 2026-10-28).
- **Layout:** **16 × 32-bit slots at the top of data memory,** 0x00007FC0–0x00007FFF (owner, 2026-10-29); the boot ROM reads it there, so the address is a silicon constant; slot *s* is the word at 0x00007FC0 + 4*s*. **Slot 0 is the command word; slots 1–15 are its arguments,** written first, in the alternating argument blocks of 6.3.
- **Commands are mode-0 writes, counter 0,** to the command block in region 0x00: arguments first, then the command word to slot 0. They stay uncounted because they arrive while the unit waits in `wait_for_command()` with its counters at 0. Every unit's command block is written this way, the boot ROM's first command included (6.2).
- **"Command landed"** is set when a write that comes in through the traffic director and covers slot 0 lands in port B (4.8), and posts the command-landed event, `EVENTS` [9] (below). The controller's own store to slot 0 doesn't set it. No other slot affects control. **The event stays pending after a unit's first command** (owner, 2026-10-29): the reset mask doesn't include it and the helpers don't consume it, and the boot ROM relies on that (6.2).
- **Conventions** (meshsim; invariant 11): writes into the command block come only from C&C 1–3, uncounted. The hardware doesn't check who writes the block.

**The reply block** (owner, 2026-10-26; mode 0, owner, 2026-10-28; a default, owner, 2026-10-29). **It is the default place for tile-to-tile replies, not a rule:** every request names its own reply address, anywhere in the requester's data memory (6.7), so each outstanding request can have its own buffer.
- **Layout:** **256 × 32-bit words directly below the command block,** 0x00007BC0–0x00007FBF (owner, 2026-10-29): two words for each of the 128 sources, so the pair for source *s* is at `REPLY` + 8*s*.
- **A reply is a mode-0 write of 32 or 64 bits to the receiver's reply block at the sender's own pair**, counted on the counter the receiver armed for it. A **32-bit reply writes word 0** of the pair and leaves word 1 alone; **a 64-bit reply writes both.** Because the address is the same in every receiver, a sender sets `ROUTER_ADDR` once, with no-increment, and changes only the dst per reply (4.11).
- Replies carry one-word results between tiles (cuts, counts, maxima, probe keys), and a request's value answers (6.7). Since every source has its own pair, **replies from different sources never collide**; two replies from one source to one tile overwrite each other, so a sender has at most one reply outstanding to each receiver until the receiver has read it (software's part). Which counter a reply uses is agreed by the two programs.
- **That a sender writes only its own pair is a software convention** (owner, 2026-10-28), no longer a hardware guarantee: meshsim checks that writes into the reply block are 32 or 64 bits at the sender's own pair (invariant 11).

**Below them, the stack** grows down from 0x00007BC0 (software convention, `fw/CONVENTIONS.md`).

**Seven write counters** (owner and Tony, 2026-10-28; seven since 2026-10-29), **`WRITE_COUNTER_1`–`WRITE_COUNTER_7`** (8.7): the 3-bit counter field's codes 1–7 are all write counters, and 0 is uncounted. A replayer tile can have an LPDDR fetch, a refill and replies in flight together, plus up to three file-system requests, each waited on separately. Every counted packet names one in its counter field (3.1), and **the traffic director decrements it K cycles after accepting the packet** (4.8). Counted packets are writes (modes 0–3), hence the name, and count-only packets (mode 5), which only count (3.1).
- **Each packet counts 1,** whatever its size: a 128-bit packet writes four words and counts 1.
- **Width: 20 bits each.** A gather of 64K values at *k* = 2 counts 131,072 packets, and *k* = 8 at the largest X needs 20 bits.
- **Done means landed:** by the time a packet's decrement happens, its write is in place (K, 4.8), so a counter at 0 means everything it counted is in place.
- **Which counter** is the program's choice, from any source (tiles, the controllers, the memory system). **Usage rule:** one counter per thing a program waits for separately. Adding to a counter means waiting for the union; to learn that batch A has landed while B is outstanding, put them on different counters.
- **A request's answer is counted** against what the requesting tile armed when it sent the request (6.7).
- **Unexpected arrival** (4.13): a decrement of a counter already at 0 leaves it at 0 and sets `ERRORS` [2]. It is detected **at the decrement**, K cycles after acceptance. **Accepted trade-off** (owner, 2026-10-28): a stray packet accepted at count 0 whose decrement lands after the program arms *N* uses up one of the *N*: done fires one word early, and the batch's last real word then raises unexpected arrival. The error is caught, later than in v1.1 and naming the last real word rather than the stray; only a second, cancelling bug (a batch armed one too high) would hide it. **meshsim models the hardware exactly** (its `ERRORS` bit and error line follow the decrement) and **also reports the stray as a diagnostic at acceptance**, naming it precisely.
- **Writes are additive** (owner, 2026-10-26; kept 2026-10-28). A write of *N* to a counter **adds *N* atomically, in step with that cycle's decrement**: `next = count + N − dec`, a decrement in the same cycle is judged against count + *N*, `unexpected = dec & (count + N == 0)`. **A write of *N* > 0 arms the counter:** it clears counters done ([7]) and the counter's own done event (one of [6:0]) and sets their qualifiers, whether the counter was at 0 or not. If the count reaches 0 in the same cycle as an add of *N*, it ends at *N* and done doesn't fire. So whichever comes first, the last word of an earlier batch or the add, **once the add has happened, done means everything added so far has landed**: no stale wake and no early wake. **A write of 0 does nothing.** A sum past 2²⁰ − 1 wraps; that is a programming error (meshsim).
- **A counter is never overwritten.** It returns to 0 only by counting down, or at the reset, which also clears the delay line (6.1).

**Counters done,** `EVENTS` [7] (owner, 2026-10-23).
- **It is set on the rising edge of "every counter is at 0":** the transition that leaves all seven write counters at 0, **counted only if some counter was armed since [7] was last set or cleared** (the qualifier). So [7] is set when the last outstanding counter the tile was waiting on reaches 0, and by K its writes are in place. v1.1's `mem_idle` and `cm_idle` no longer take part (owner, 2026-10-28).
- **It is sticky,** like every event: it stays pending until a load of `EVENTS` with [7] in `EVENTS_MASK` consumes it (below). An edge before `WFI` is not lost: if a copy finishes before the core reaches `WFI`, the `WFI` falls straight through. **A wake does not consume it,** so no `WFI` can swallow another's wake.
- **It is cleared,** with its qualifier, by a non-zero write to any counter (so every new wait starts clean), by the `done` pulse, and by a load that consumes it. A set and a clear in the same cycle: the clear wins.
- **Polling and `WFI`, and the end of a phase.** Within a phase a program may poll one counter to 0 and act on it, then `WFI` for the rest. [7] stays pending until a load consumes it or a non-zero counter write clears it, so that `WFI` returns at once if everything has already landed, and waits otherwise. **Rule: a phase that sets counters must end with a `WFI` for counters done before it pulses `done`.** That `WFI` proves the arrivals are in (invariant 4); `done` with a counter still non-zero is done early, `ERRORS` [8] (4.13).
- **A count of 0 never makes an edge.** A phase that expects nothing must not `WFI` for counters done.
- **Counters done waits for every counter.** To sleep on one batch while another is expected, a tile uses that counter's own done event (below).

**Per-counter done events,** `EVENTS` [6:0]: **bit *n* − 1 is write counter *n* done**, [0] for counter 1 to [6] for counter 7.
- **Each is sticky and set on its own counter's transition to 0, counted only if that counter was armed since the event was last set or cleared:** each has its own qualifier, as [7] has.
- **Each is cleared,** with its qualifier, by a non-zero write to its own counter, by the `done` pulse, and by a load that consumes it; like [7] it cannot be set while the `done` gate is closed. (The `done` pulse clears no other event: DMA run done, for one, is cleared only by `PULSE_DMA` and by a load that consumes it.)
- **[7] keeps its meaning:** all seven counters done. A program waits on [7] for everything, or on one bit for one batch. The replayer's refill wait uses its refill counter's bit alone, so an LPDDR fetch outstanding on another counter doesn't hold it up (4.19.9).

**The events** (`EVENTS`, `EVENTS_MASK`, `EVENTS_REQUIRED` and `EVENTS_PEEK`, 8.10; owner, 2026-10-28). **All wakes are events** (owner, 2026-10-23): `EVENTS` collects every one-bit event a program may sleep on, from the counters, C&C 1, the units and the peripherals, and the core's wakeup input is driven by it and nothing else (4.11). **Errors are not events:** they raise the hard interrupt (4.13).

| bit | event | set by | kind |
|---|---|---|---|
| [6:0] | **write counter *n* done**, bit *n* − 1 for counter *n* | its counter's transition to 0 (above) | sticky |
| [7] | **counters done:** all seven at 0, at least one armed since it last fired | the rising edge above | sticky |
| [8] | **GO** | C&C 1's barrier release (6.3); mesh tiles only, since C&C 2 and C&C 3 aren't wired to GO | sticky |
| [9] | **command landed** | a director write covering command slot 0 (above, 4.8) | sticky |
| [10] | **DMA run done** | the rise of `rd_done`: the address generator's run has finished (4.2); cleared by `PULSE_DMA` | sticky |
| [11] | **request FIFO no longer full** | the fall of `request_busy` (4.18) | sticky |
| [12] | **request pending:** a received request is waiting | the unit's target's receive queue holds a request (3.3, 4.18) | live |
| [13] | **peripheral 1:** its `EVENTS & EVENTS_MASK` is non-zero | the peripheral (4.17.3) | live |
| [14] | **peripheral 2** | likewise | live |
| [15] | **peripheral 3** | likewise | live |
| [31:16] | reserved, read 0 | — | — |

- **Sticky events** are set by their cause and stay pending until a load consumes them. Setting one that is already pending leaves it set. [9], [10] and [11] are set on the edge, in the cycle the matching status bit sets or falls; the status bits themselves ("command landed", `rd_done`, `request_busy` in `STATUS`) keep their meanings and are separate state. **`PULSE_DMA` clears [10]** (owner, 2026-10-28), as an arming write clears its counter's done, so [10] always refers to the run most recently started and a stale one can't satisfy the next wait.
- **Live events** are true while their condition holds; they are not stored, so no load of `EVENTS` clears them. They clear only at their source (owner, 2026-10-28): **request pending** stays true while requests are queued, and only a load of `REQUEST_RECEIVE` removes one (4.18), so queued requests never merge into one wake; **a peripheral's bit** stays true while that peripheral's `EVENTS & EVENTS_MASK` is non-zero, and its events are consumed in one place only, by a load of the peripheral's own `EVENTS` (4.17.3).
- **Spurious wakes are allowed but not wanted** (owner, 2026-10-28). After a consuming load of a peripheral's `EVENTS`, the tile's live bit for that peripheral may lag, so a following `WFI` may return with nothing to do. **Software always re-checks after a wake**, as standard RISC-V `WFI` allows. **The hardware aims to make the bit current by the next instruction,** a short path inside one unit, so in practice spurious wakes don't happen; meshsim models the hardware's actual latency.
- **`EVENTS_MASK`** chooses the events that wake `WFI` on their own and the events a load of `EVENTS` consumes. It is read/write and **hardware never writes it**, except its reset to **[8] | [7]**, GO and counters done (6.1). A program stops an event from waking it by clearing that bit, not with a pulse.
- **`EVENTS_REQUIRED`** (R/W, 0 at reset; **the controller's only**: a peripheral's window has none) adds an AND to the mask's OR: a required event wakes `WFI` only when **every** required event is pending. A program needing "peripheral 1 and write counter 2" sets both bits here.
- **The wake term,** which drives the core's wakeup input (4.11):

  `wake = gate ? EVENTS[8] : ((EVENTS & EVENTS_MASK & ~EVENTS_REQUIRED) ≠ 0) OR (EVENTS_REQUIRED ≠ 0 AND (EVENTS & EVENTS_REQUIRED) == EVENTS_REQUIRED)`

  where `gate` is the `done` gate (below). A mask bit that isn't required wakes on its own (the escape hatch: an event the program must not sleep through while it waits for a set); a required bit never wakes alone. With `EVENTS_REQUIRED` = 0 the term is `(EVENTS & EVENTS_MASK) ≠ 0`. So one `WFI` can wait for several events at once, for example the end of a DMA run and a write counter together (4.10).
- **A load of `EVENTS` consumes under the mask:** it returns **every** pending event, whatever the mask, and clears exactly the returned sticky bits that are 1 in `EVENTS_MASK`. `EVENTS_REQUIRED` plays no part in clearing, so required events meant to be consumed are put in the mask too. Software acts only on *value* & *mask*, the events it has just consumed. **An event set after the load's sample point survives** (4.11: with latency *L*, the load samples at the end of *t* + *L* − 2 and clears at the end of *t* + *L* − 1): it is not in the value returned and is not cleared, so the next load sees it.
- **`EVENTS_PEEK`** (R) returns exactly what a load of `EVENTS` would return in the same cycle and **never clears anything**. It is an alias of the same state, with no storage of its own (owner, 2026-10-23). Software reads `EVENTS_PEEK` to look and `EVENTS` to consume; the trap handler reads only `EVENTS_PEEK` (4.13).
- **A wake consumes nothing** (owner, 2026-10-23): hardware never consumes an event on wake, so a program consumes or masks an event before its next `WFI`, or that `WFI` falls straight through. **The `fw/include/mesh.h` wait helpers follow every `WFI` with a load of `EVENTS`**: `wfi()` consumes what woke it (counters done, at the reset mask), and `wait_for_command()` consumes GO (6.3). The load costs 2 cycles, as every register-block load does (DECISIONS-v1.2 section 45; 4.11), and is the same code on every tile, so synchronised starts (6.3) are unaffected.
- **Keep GO in the mask** (owner, 2026-10-23, item 79). The `done` gate lets GO wake the core whatever the mask holds, but only a load with [8] in `EVENTS_MASK` consumes it. So a program keeps [8] in `EVENTS_MASK` whenever it calls `wait_for_command()` (the reset mask has it); otherwise the GO stays pending, and the next `wait_for_command()` falls through before its own GO.
- **The `done` gate** (owner, 2026-10-23): from the `done` pulse until GO, **the wake term is overridden to "GO ([8]) pending"** and nothing else can wake `WFI`, whatever `EVENTS_MASK` and `EVENTS_REQUIRED` hold. Counters done and the per-counter done events cannot be set in that window (an edge then is ignored), and the `done` pulse clears them. So `wait_for_command()` can only ever be woken by GO, whatever a program gets wrong. The gate is a gate, not a mask setting: it writes no register, so after GO the program's own `EVENTS_MASK` is in force with nothing to restore. Any other event set in the window stays pending, and wakes the core after GO if it is still enabled. **The gate controls only `WFI` wakes:** an error raised between `done` and GO still takes the hard interrupt (4.11, 4.13). A write counter still non-zero at `done` is a program error, done early (4.13); an arrival after `done` for a counter already at 0 is unexpected arrival.
- **Timing.** An event is set at the end of the cycle its cause happens; a load sees it, and a pending event releases a waiting `WFI`, in the next cycle. So a GO pulse at cycle *t* sets [8] at the end of *t* and wakes the core at *t* + 1, and counters done set at the end of *t* releases a waiting `WFI` in *t* + 1.
- **At the reset** (6.1): every counter, the delay line, the done events and their qualifiers, the gate, `done`, "command landed" and any pending GO are 0; `EVENTS`' sticky events and `EVENTS_REQUIRED` are 0 and `EVENTS_MASK` is [8] | [7]. Live events follow their sources.

**Uses:**
- a block copy (6.5) or a receiving phase (6.4): arm a counter, start, `WFI` with [7] in the mask (the helper consumes it);
- a search step through the read port: arm the reads' counter with the number of reads, then `WFI` (4.3);
- waiting for the tables: `EVENTS_REQUIRED` = the counter's done and [10] (4.10);
- a read from LPDDR (6.7): arm a counter for the read's packets (the write adds, so other traffic for it may be in flight), send the request, `WFI` for counters done or the counter's own event; reads count against the per-tile request limits until their data has landed (3.3);
- a request with a value answer (an fd, an error code, 6.7): arm the reply's counter with 1 and name it, with the reply's address, in the request; then `WFI` on its done event or for counters done;
- one batch of arrivals while another is outstanding: its counter's done event (one of [6:0]);
- the end of a run ([10]), room in the request FIFO ([11]), a received request ([12], 4.18), and a peripheral's events ([13]–[15]; the replayer's loop, 4.19.9; the UART, 4.20).

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
| 1 | bad packet | a bad packet arrived and was dropped, uncounted (3.1: a region it can't reach, mode 3 at 128 bits; invariant 5). Misaligned accesses are UB and set nothing (4.1; v1.2's "or misaligned access") |
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
- **Any non-zero `ERRORS` raises the unit's hard interrupt** (4.11; taken once the image has enabled interrupts, below) and **its error line** into C&C 1's peripheral (2, 6.1). The line is driven by hardware from `ERRORS`, with no software involved and a fixed latency, `error_latency` (section 10), so it still reports a unit whose request path is stuck or whose handler has itself failed.
- **What earns an error:** an error exists for a mistake that would otherwise **go unnoticed or surface far from its cause**: silent corruption or lost data (done early, unexpected arrival, every overflow, drift, bad packet). Misalignment, which simulation catches, is UB (top; 4.1). A mistake that **stops the machine** (a unit that hangs) needs no error: C&C 1's READY bits and its watchdog catch it (6.1, 6.6). A misuse such as **a start while its engine is running or a pulse while a peripheral is busy is UB** (meshsim) (owner, 2026-11-01); v1.2's hardware ignored it.
- **Same-cycle rule and overflows:** an error raised in the same cycle as the reset is lost (the reset wins). **On every overflow the arriving item is dropped**: the packet, write, request or gather request that found its FIFO or queue full is lost, and only the error records it (errors 0, 4, 5, 6 and 7). **"Full" is the count before the cycle's pop:** a push into a full FIFO overflows even if an entry leaves in the same cycle (no bypass from pop to push), so no FIFO ever holds more than its depth.

**Traps are terminal** (owner and Tony, 2026-10-28).
- **An error raises the hard interrupt,** and a bus error, an illegal instruction, `ecall`, `ebreak`, a fetch outside program memory and the boot ROM, or a misaligned jump target traps synchronously (4.11). **Either way the trap is terminal:** the state is assumed bad, **the handler never executes `mret`**, and the interrupted code is never resumed. Only the reset restarts the controller (6.1). This holds for every unit, C&C 1 included (6.6).
- **No retrap.** The interrupt stays high until the reset (`ERRORS` is sticky), but `mstatus.MIE` is cleared on trap entry and never set again, so it cannot trap a second time.
- **A second trap inside the handler** (a synchronous exception) restarts the handler at `mtvec`; it is still terminal.
- **The unit's engines and peripherals carry on** with whatever they were doing, unserviced, until the reset: nothing stops them before it (owner, 2026-10-28).
- **Traps that set no `ERRORS` bit are software bugs, caught by meshsim** (owner, 2026-10-29): `ecall`, `ebreak`, the fetch, the misaligned jump and a trap inside the handler. **meshsim stops loudly on each.** The hardware adds nothing for them: such a trap parks the unit without raising its error line, and on hardware only C&C 1's watchdog notices (6.1). Accepted.

**The handler** (owner, 2026-10-28 and 2026-10-29; conventions for every unit's handler):
- **The report set is fixed and exact** (owner, 2026-10-29): every image's handler (recipe 24, in program memory) sends the same **24 reports**, in the same order, whatever the unit's peripherals, so the count is a constant of the protocol and nothing in the reports needs to carry it; C&C 1 counts each unit's reports against it (6.1). The boot ROM's handler sends a shorter, equally fixed set (below). Each report is one 128-bit request to C&C 1 (target 0, 3.3): word 0 the header (the requestor and target, the report op and the report's sequence number, 0–23; no reply is asked for), words 1–3 three data words:

| reports | data words |
|---|---|
| 0–9 | x1–x30, three registers each, in order |
| 10 | x31, `mepc`, `mcause` |
| 11 | `mtval`, `PARAMS`, `STATUS` |
| 12 | `ERRORS`, `EVENTS_PEEK`, `WRITE_COUNTER_1` |
| 13 | `WRITE_COUNTER_2`–`4` |
| 14 | `WRITE_COUNTER_5`–`7` |
| 15–17 | peripheral 1's window: `PARAMS`, `STATUS`, `ERRORS`; `EVENTS_PEEK`, `EVENTS_MASK`, `DMA_HDR`; `DMA_ADDR`, 0, 0 |
| 18–20 | peripheral 2's window, likewise |
| 21–23 | peripheral 3's window, likewise |

  **An absent peripheral's three reports carry zeros** (owner, 2026-10-29). The set is decided as listed (owner, 2026-10-29); **the header's op and sequence-number fields are settled with C&C 1's request format** *(Deferred, item 122; the draft header has op [17:10] and the sequence number in [31:21])*.
- **It checks `PARAMS` [2:0] before reading a peripheral's window:** reading an absent peripheral's window is a bus error, which would trap inside the handler.
- **It reads seven of each window's ten standard words:** **`EVENTS_PEEK`, never `EVENTS`,** at both levels, since a load of `EVENTS` consumes events and would destroy the state it is reporting; and **not the write-only pulses** (`PULSE_RUN`, `PULSE_DMA`), which read 0.
- **It assumes no interface is clean:** a register sequence the main code left half done (the send port's `W` words, the request port's `SET`/`GET` words) is never completed, since nothing resumes, so the handler writes every word of each request it sends.
- **Reports are exempt from the per-tile request limits** (3.3): they ask for no reply, and a failed unit sends all of them at once.
- **It parks** in a plain jump to itself. A `WFI` loop would also work only if `mie`'s enable is cleared first, since a pending enabled interrupt may release `WFI` with `MIE` = 0; the plain loop doesn't depend on that.
- Until a loaded image installs its own, `mtvec` points at the **boot ROM's report-and-park handler** (6.2), which **sends a two-report dump and parks** (DECISIONS-v1.25 section 8d):

  | report (sequence number) | data words |
  |---|---|
  | 24 | `ERRORS`, `mcause`, `mepc` |
  | 25 | `mtval`, `STATUS`, `ra` (x1) |

  No `PARAMS`: the request tree stamps the sender's id. The full set stays the image's handler's. Otherwise it keeps the conventions above (one 128-bit request per report, the same report op, every word of each request written, a plain jump to itself), and it never writes data memory. **A unit's first report tells C&C 1 which set is coming** (owner, 2026-11-01): sequence number 0 starts an image's 24 (0–23), 24 the ROM's two (24 and 25), so the sequence number alone names each report's layout. A unit sends only one of the sets: the ROM's handler is unreachable once an image has installed its own, and both park.
- **Interrupts are off until the image enables them** (owner, 2026-10-29): the reset leaves `MIE` = 0, the boot ROM leaves it so, and the image's boot part 2 enables them (6.2). Until then an error raises the unit's error line and the toast pin (6.1) but takes no trap; a synchronous exception still traps to the ROM's handler.

### 4.14 Commit ordering

There is no coherence (4.1), so "committed" matters only at phase boundaries. **A unit knows a write has landed when its counter has decremented,** K cycles after the traffic director accepted it (4.8), by which time:
- **controller memory:** port B has written it, and a load reads it;
- **a peripheral's region:** it has landed (4.17.8);
- **big SRAM:** it has entered its area's request queue, ahead of any read issued later (4.1);
- **the tables:** its `PTR` or `HIST` update is readable through the window (guarantee A, 4.10).

So a counter's done event, and counters done (`EVENTS` [7], 4.12), are set exactly when the writes they counted are readable. **A memory-system read** (6.7) is counted like any other data, so the same events prove it has landed.

**Uncounted writes** are commands and boot overlays into controller memory (3.1). A command signals its own landing ("command landed", 4.12); overlays are followed by the command, which lands last from the same source through port B (6.2). Big-SRAM data is always sent counted (6.2), since its write path is slower than port B's and no command could vouch for it.

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
- **Inside the controller.** The controller is a tile instance with its own router, and this link is its only link to the mesh. **This link is the controller router's down port** (owner, 2026-11-05, item 118). Its sending peripheral's DMA packets enter the controller's router as an extra input and leave through the down port, with **src = the controller's id + 8** (74 for C&C 3's Tracer; 4.17.9). *(Still deferred: the rest of the routing inside the controllers' routers, and the DMA port's flow control.)*
- **Cost:** ten extra FIFOs of 16 × 256 bits (~41 Kbit of flops) plus the wider selectors, ≈ 0.04–0.07 mm² [estimate; scaled from v1.1's 169-bit ≈ 0.03–0.05], tiles 0–2 only (section 11). Their overflows are reported like the other router FIFOs.
- **The memory system's traffic** enters at tile 1: it is C&C 2's peripheral (2, 6.7), so its deliveries come in through tile 1's "from the controller" route block, and writes into its tag windows leave through tile 1's "to the controller" output. Several tiles writing into open windows at once fan in there with no flow control; the file system keeps one writer in flight or paces its writers (6.7).
- **Pacing is policy.** Every packet a controller sends during an all-to-all adds load to its tile's links that the schedule's queue bounds don't count (one broadcast per 64 cycles raised tile 0's link queues to 12 of 16 [meas: meshsim]). So the controllers send commands and bulk data between passes, or pace them.

### 4.17 Peripherals

*No figures yet; the figure set gains a peripherals part.*

A **peripheral** is a unit that works beside its controller on a job of its own: the replayer (4.19) on every mesh tile; Ingress/Egress and the UART, the Updater/Binder/RW engine and the Tracer as C&C 1's, C&C 2's and C&C 3's (section 2, 6.6). **A unit has up to three** (owner, 2026-10-28), and **every peripheral connects to its controller the same way** (owner, 2026-10-28), so the control logic is one design whatever a unit holds:

| part | where | role | section |
|---|---|---|---|
| presence and type | the controller's `PARAMS` [2:0]; the peripheral's `PARAMS` [7:0] | which peripherals the unit has, and what each is | 4.17.1 |
| region | 0x04, 0x05, 0x06 | where packets reach the peripheral | 4.17.5 |
| register window | 0xFFFFFA00, 0xFFFFFC00, 0xFFFFFE00, 0x200 bytes each | the controller's loads and stores | 4.17.4 |
| run | `PULSE_RUN`, `STATUS` [0] | starts the peripheral's work; its ownership state | 4.17.2 |
| events | the peripheral's `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK`; one live bit in the controller's `EVENTS` [13]–[15] | the peripheral's calls for service, counted or live | 4.17.3 |
| errors | the peripheral's `ERRORS`; one live bit in the controller's `ERRORS` [11]–[13] | its errors, which raise the hard interrupt | 4.17.6 |
| output (optional) | `PULSE_DMA`, `DMA_HDR`, `DMA_ADDR`, `STATUS` [1] | the peripheral's DMA, on a router port of its own | 4.17.9 |

**What each peripheral's section defines:** its type and version; what its run does and when it ends; its peripheral-defined `STATUS`, `ERRORS` and `EVENTS` bits, each event's kind and depth; its words from 32 up; its region's map, the sizes each part takes and its ownership rules; and its DMA, if it has one. The replayer's section (4.19) is the first; C&C 1's peripheral is in 6.6 and 8.14.

#### 4.17.1 The peripherals and their types

- **Up to three per unit,** fixed when the chip is built: peripheral *n* (1–3) has region 0x03 + *n* (2.2) and the register window at 0xFFFFF800 + 0x200·*n* (8.1).
- **Presence** is the controller's `PARAMS` [2:0], bit *n* − 1 for peripheral *n* (8.2). **The type** is the peripheral's own `PARAMS` [7:0], and its version [15:8] (4.17.4) (DECISIONS-v1.2 section 46): **0** none, or nothing to say; **1** Ingress/Egress, with the UART and the chip-level lines (C&C 1's peripheral); **2** the Updater/Binder/RW engine (C&C 2's); **3** the Tracer (C&C 3's); **4 the replayer** (4.19; v1.1 called it type 1). Other values are reserved. Every mesh tile (0–63) holds a replayer as peripheral 1 (owner, 2026-10-27); C&C 1–3 each hold their engine as peripheral 1 (6.6).
- **An absent peripheral** has neither region nor window: **any word of its window is a bus error** (4.11), so a handler checks `PARAMS` [2:0] first (4.13), and **a packet to its region is a bad packet** (3.1). Its bits in the controller's `EVENTS` and `ERRORS` read 0.

#### 4.17.2 Run: `PULSE_RUN` and `STATUS` [0]

- **`PULSE_RUN`** (window word 3) starts the peripheral's work, as its section defines: for the replayer, one pass of the rows `RUN_CONFIG` names (4.19.9, 4.19.11). A store of 1 fires it; a store of 0 does nothing; a load reads 0. **Run only, no resume,** as for every engine: **a run while one is in progress is UB** (meshsim) (owner, 2026-11-01, under the principle at the top); v1.2 ignored it.
- **`STATUS` [0], run in progress,** is the peripheral's **ownership state**: 1 from the end of the run store's cycle until the work ends. While it is 1 the peripheral owns whatever its section says (the replayer: its single-ported memories, 4.19.10), and writes the ownership forbids are the peripheral's errors (4.17.6). It reads 0 while the peripheral is idle.
- **The end of the work** posts **run done**, `EVENTS` [0] (depth 1, 4.17.3).

#### 4.17.3 Events: counted and live

A peripheral's events are in its window: **`EVENTS`** (word 5), **`EVENTS_PEEK`** (word 6) and **`EVENTS_MASK`** (word 7) (owner, 2026-10-28). They replace v1.1's six service lines.

- **Every `EVENTS` bit is backed by a small counter** (owner, 2026-10-28), and the bit reads 1 while the count is above 0. **The peripheral adds 1** when the event happens; **a consuming load of `EVENTS` subtracts 1** from each bit that is set and masked (rather than clearing it). Each peripheral's section gives every bit's **depth**: **1 for an ordinary event** (v1.1's sticky behaviour: adding to a full count of 1 leaves it at 1), **2 for a double-buffered queue's credits** (4.19.8). A bit may instead be **live**: it follows its source and is never consumed, like the UART's "RX not empty" (4.20). (meshsim flags a peripheral returning a credit to a full counter.)
- **The standard bits:** [0] **run done** and [1] **DMA done**, each depth 1. The rest are peripheral-defined.
- **`EVENTS_MASK`** (R/W) chooses which events count toward the controller's bit and which a load consumes. **It is 0 at reset:** a peripheral wakes nobody until the program asks.
- **A load of `EVENTS`** returns every bit and consumes one count from each bit set in `EVENTS_MASK`, at 4.11's sampling rule. **Rule for software:** a load consumes one count from every masked bit that is set, so the program acts on every bit the load returns, or masks only what it is about to service. **`EVENTS_PEEK`** returns the same value and consumes nothing. There are no clear pulses, and there is no `EVENTS_REQUIRED` in a window: a program that needs "peripheral 1 and write counter 2" sets both in the controller's (4.12).
- **The controller's bit** for peripheral *n*, `EVENTS` [12 + *n*], is **live**: high while the peripheral's `EVENTS & EVENTS_MASK` is non-zero (4.12). **An event is consumed in one place only,** by a load of the peripheral's `EVENTS`; a load of the controller's `EVENTS` never touches it. The controller's bit may lag a consuming load by a cycle or so, so a following `WFI` may wake spuriously, and **software always re-checks after a wake** (owner, 2026-10-28; 4.12).
- **Reads have no other side effects:** every window word reads the peripheral's state at its sample point (4.11) and changes nothing, except `EVENTS` and the pops a peripheral defines for its own words (the UART's RX, 4.20).
- **Items** (v1.1's item-FIFO line kind, which presented one item per claim in a status register) are not part of the standard window. *(Deferred, item 119: a peripheral event kind for a queue of items, for returning the RW engine's closed tags and for the host interface.)* The replayer presents its one item, the instruction awaiting an expansion, in a word of its own (4.19.9).

#### 4.17.4 The standard window

Every peripheral's 0x200-byte window (owner, 2026-10-28) starts with the same words, in the same order as the controller's block (8.1): parameters, status, errors, pulses, events, DMA, then the rest. Word *n* is at the window's base + 4*n*.

| word | register | access | purpose |
|---|---|---|---|
| 0 | `PARAMS` | R | constant: [7:0] type, [15:8] version, the rest peripheral-defined (sizes); 0 if it has nothing to say |
| 1 | `STATUS` | R | live bits: [0] run in progress, [1] DMA in progress; the rest peripheral-defined |
| 2 | `ERRORS` | R | the peripheral's errors, sticky until the reset (4.17.6) |
| 3 | `PULSE_RUN` | W | store 1 to start a run (4.17.2) |
| 4 | `PULSE_DMA` | W | store 1 to start the peripheral's DMA as set up in words 8–9 (4.17.9) |
| 5 | `EVENTS` | R | counted and live events; a load consumes one count from each set bit in `EVENTS_MASK` (4.17.3) |
| 6 | `EVENTS_PEEK` | R | what a load of `EVENTS` would return, consuming nothing |
| 7 | `EVENTS_MASK` | R/W | the events that count toward the controller's bit and that a load consumes; 0 at reset |
| 8 | `DMA_HDR` | R/W | count [11:0] (**128-bit words**, up to 4,095; v1.3), output queue [13:12], spare [16:14], size [18:17] reserved (every packet is 128 bits), counter [21:19], mode [24:22], dst [31:25] (4.17.9) |
| 9 | `DMA_ADDR` | R/W | the DMA's 32-bit destination address, advancing by each packet's size as packets go out |
| 10–31 | | | reserved for future standard words |
| 32–127 | | | peripheral-defined |

**Words 32–35 of the running peripherals** (v1.26.1; DECISIONS-v1.26 items 13 and 14). The replayer (4.19) and the Tracer (C&C 3's, v1.3) have **the same four words**: 32 **`RUN_CONFIG`** (R/W: [23:0] the rows the next run runs, [24] use the store log, [25] output `rd_prev`, [28:26] the per-stream compression enables, code, read log and write log, [31:29] reserved; the Tracer honours the row count and [28:26] and ignores [24] and [25], which are the replayer's; the replayer honours [25:0] and ignores [28:26], which are the Tracer's (owner, 2026-11-05)), 33 **`ROWS_RUN`** (R, [23:0]: rows of this run completed), 34 **`EXPANSION_OFFSET`** (R) and 35 **`EXPANSION_OPCODE`** (R). `RUN_CONFIG` is written while the peripheral is stopped and used by the next `PULSE_RUN`; a write while running is UB (meshsim). It is sticky, holding the last value written across runs, and is 0 after the reset. The replayer's words are in 4.19.9 and 8.11, the Tracer's in 4.21.3 and 8.16 (its words 36–43 are its own); the other peripherals define their words from 32 as their sections say (6.6, 8.14).

**Peripheral types** (`PARAMS` [7:0]; owner, 2026-10-31; W4-05): **0** none, or nothing to say; **1** Ingress/Egress,
with the UART and the chip-level lines (C&C 1's peripheral, 6.6); **2** the Updater/Binder/RW engine (C&C 2's, 6.7);
**3** the Tracer (C&C 3's); **4** the replayer (4.19). Other values are reserved.

- **Access rules** (owner, 2026-10-28): **reserved or undefined words**, in a window or the controller's block, **read 0 and ignore stores** (meshsim flags the access); a store to a defined read-only word is a bus error, as in the controller's block (4.11); **any word of an absent peripheral's window is a bus error** (4.17.1). Every word is 32 bits, accessed one word at a time, at the register-load latency fixed for it (4.11).
- **A peripheral without a DMA** leaves word 4 (`PULSE_DMA`) unused (reserved) and `STATUS` [1] at 0. **Its words 8 and 9 (`DMA_HDR`, `DMA_ADDR`) read 0, and a load of them is not flagged; only a store is** (DECISIONS-v1.2 section 48, W4-06): they are standard words that happen to be empty, so the handler's fixed report set (4.13) reads them on every peripheral, the replayer included, without special cases. In hardware they read 0 like any reserved word; the difference is meshsim's flag.
- **The C header** (owner and integrator, 2026-10-28/29; W3-27; to be confirmed with the generator): each window is declared as a C struct overlaid on its address, a `PeripheralStd` macro listing the ten standard members at the top of each peripheral's struct, and a type per peripheral ending in `Regs` (`ReplayerRegs`). Access is through pointers (`#define peripheral1 ((volatile ReplayerRegs *)0xFFFFFA00)`, used as `peripheral1->EVENTS`), so a whole-block copy, which would consume events, needs an explicit dereference. Every member is `volatile uint32_t`. The register-table generator emits the structs (8.1).

#### 4.17.5 Writes into a peripheral's region

v1.1's mode 9 is gone: **a packet reaches a peripheral as a mode-0 write to its region** (3.1, 4.8), 0x04, 0x05 or 0x06, at a **byte offset**, naturally aligned (2.2). The peripheral's section gives the map, the sizes each part takes and when each may be written (the replayer's, 4.19.10). The traffic director only hands the packet over; the peripheral's write port judges it.

**From any source** (owner, 2026-10-26): the tile itself (its streams and its send port), another tile, or a non-tile endpoint (the memory system's deliveries, 6.7; C&C 1's boot loads, 6.2).

**Landing.** The traffic director hands each packet to the peripheral's write port, one per cycle, and the write lands a fixed **`rp_latency`** = 2 cycles later, with no queue on the path *(Proposed, item 48: the value)*. **Every peripheral, up to three per unit, lands its writes within K of acceptance** (owner, 2026-10-28; 4.8): it is what lets a counter's done prove that every counted write into the region has landed, a request's data included (6.7).

**Counting.** A write into a peripheral is counted by its counter field like any other write (3.1), K cycles after the director accepts it (4.12). **A write the peripheral's map, sizes or ownership state refuses is UB** (meshsim) (owner, 2026-11-01, under the principle at the top); v1.2 dropped it, still counted, with the peripheral's own error. **A drop the peripheral defines and checks** (the replayer's queue overrun) sets its own error (4.17.6); whether the dropped write is still counted is not promised. So a counter's done means every counted write the rules allow has landed. v1.1's `DMA_PERIPH_REMAINING` is gone: a program uses any write counter for its peripheral's refills.

**One source at a time per order-placed port** (owner, 2026-10-26; invariant 18). Where a peripheral places a port's writes in arrival order (the replayer's queue ports), only one source feeds that port at once: one of the tile's own clients (4.5), or one other tile, or one non-tile endpoint. Packets from two sources would interleave unpredictably (meshsim).

#### 4.17.6 Errors

- **A peripheral's errors are in its own `ERRORS`** (window word 2; owner, 2026-10-28), **sticky until the reset** like the controller's (one exception: the unit-error bit of C&C 1's peripheral, live, 6.6). Every bit is peripheral-defined; v1.1's generic "bad peripheral write" became an error bit of each peripheral that could refuse a write in v1.2; **in v1.25 a refused write is UB** (4.17.5), and the replayer's bit is reserved (4.19.10).
- **Errors interrupt; events wake** (owner, 2026-10-28). **The controller's `ERRORS` has one bit per peripheral,** [11]–[13], **live**: 1 while that peripheral's `ERRORS` is non-zero (4.13). So a peripheral's error raises its controller's hard interrupt and its error line, and the handler reports the window's standard words (4.13).
- **A peripheral's errors follow 4.13's principle:** each is a program error (P) or a hardware-fault detector (H), never information. Its section gives each bit's kind.

#### 4.17.7 (removed)

*Removed in v1.2: v1.1's "HALT, a halted controller and the empty slot". See `spec/history/CHANGES-v1.2.md`.*

#### 4.17.8 What every peripheral must do

So that the interface means the same everywhere, a peripheral:
- **lands its writes within `rp_latency`**, at most K, after the director, with no queue on the path (4.17.5);
- **implements the standard words** of its window (4.17.4), with the standard bits and access rules;
- **reads idle when idle:** `STATUS` [0] and [1] at 0, no event counted and no error raised by a program that never runs it;
- **resets with its unit** at the reset (6.1), and is stopped by nothing else: a trap of its controller leaves it running, unserviced, until the reset (4.13);
- **counts its events** with the depths its section states, or makes them live (4.17.3), and has no other read side effects except where its section defines a pop;
- **sends nothing unsolicited:** a peripheral with a DMA sends only when its controller has started the transfer (4.17.9);
- **keeps errors to errors** (4.17.6).

#### 4.17.9 Peripheral output: the DMA

**A peripheral may have a DMA**, its output engine (owner, 2026-10-28; v1.1's "sender"). The replayer has none: its rows stay inside the tile, for their consumer (4.19.12). **Only C&C 1–3's engines send** (6.6).

**The path.** peripheral → its **output queues** (up to 4, numbered 0–3) → its **DMA** → **a router port of its own** (below). The DMA moves one transfer at a time from one output queue, and **sends only for a transfer its controller has started** (no unsolicited traffic); how a peripheral fills its queues, and how it asks to have them drained, is its section's (the Tracer's, 4.21.6).

**Its registers** (window words 4, 8 and 9):
- **`DMA_HDR`** (R/W): count [11:0] **in 128-bit words** (up to 4,095, 65,520 bytes; v1.3, DECISIONS-v1.3 section 24), output queue [13:12], spare [16:14], size [18:17] reserved, counter [21:19], mode [24:22], dst [31:25] (absolute), in the common header layout (8.1). `DMA_HDR` [24:17] is laid out exactly as `STREAM_COUNT` [24:17]; [31:25] is the absolute dst here and the relative *d* there.
- **`DMA_ADDR`** (R/W): the destination's 32-bit address (2.2), advancing by each packet's size in bytes as packets go out.
- **`PULSE_DMA`** (W): a store of 1 starts the transfer set up in `DMA_HDR` and `DMA_ADDR`.

**A transfer** sends *n* 128-bit words, the count, from the named output queue to dst, every packet stamped with the header's counter and mode (owner, 2026-11-01; DECISIONS-v1.3 section 24; v1.3):
- **Packets:** *n* packets, **every one 128 bits**, one queue entry each, in queue order. There are no remainder packets: a stream is padded to whole 128-bit words before it reaches the queue, and its exact length travels with it (the Tracer's stream word, 4.19.2). *(v1.26.1: the count was in 32-bit words, ⌊n/4⌋ 128-bit packets then a 64-bit and a 32-bit remainder.)*
- **The address:** each packet carries `DMA_ADDR`, which then advances by 16. `DMA_ADDR` must be 16-byte aligned (a misaligned packet is UB, 4.1), and a transfer that continues where another stopped stays aligned.
- **Pacing:** the DMA takes words as the peripheral makes them available, at most one packet per cycle, and waits while its queue is empty.
- **The DMA checks nothing,** like the send port (4.11): reserved modes, a bad dst and misaligned packets go out as written, and the receiver or the simulator catches them (a misaligned packet is UB, 4.1).

**One transfer at a time.** `STATUS` [1], **DMA in progress**, is 1 from the end of the `PULSE_DMA` store's cycle until the transfer's last packet has left. When it falls the peripheral posts **DMA done**, `EVENTS` [1] (depth 1). **`PULSE_DMA` while a transfer is in progress is UB** (meshsim) (owner, 2026-11-01, under the principle at the top); v1.2 ignored it rather than stall the store, since a stalling store could deadlock (the DMA may be waiting for data its own controller hasn't yet let the peripheral produce). **A count of 0** does nothing, and DMA done is still posted. **`DMA_HDR` or `DMA_ADDR` changed mid-transfer** is undefined (meshsim flags it); a load returns the live, advancing values. A transfer naming an output queue the peripheral doesn't have is UB (meshsim); v1.2 ignored it. **A transfer asking for more words than its queue will ever hold is UB** (meshsim): the DMA would wait for data that never comes (the Tracer's sizing rule, 4.21.6).

**Its router port** (owner, 2026-10-28). **A peripheral that sends has its own router port:** it is paired with a router link the unit doesn't otherwise use, and its packets enter the router there, like a neighbour's. The unit's injection point keeps its two sources (4.5), and a DMA packet to its own unit reaches the ejector through the router like any arrival and takes no read token, so the loopback stays protected (4.5). Only a unit with a free link can have a sending peripheral; C&C 1–3 have them. **Its packets carry src = the controller's id + 8** (72–74 for C&C 1–3's engines; the Tracer's are 74), **sources only, not endpoints** (2), and **leave through the controller router's down port**, the link to its tile (owner, 2026-11-05, item 118). **Routing is by dst, with no special case** (owner, 2026-11-05; B1 Q11): a packet whose dst is its router's own unit (the Tracer's to C&C 3, dst 66, for the end-of-segment record, 4.21.6) is ejected there like any arrival; every other packet leaves through the down port. *(Still deferred: the rest of the routing inside the controllers' routers, and the port's flow control.)*

### 4.18 Request port

The tile's end of the request bus (3.3). **It has two directions in every tile,** so every tile is one design (owner, 2026-10-26):
- **the send side:** four write-only registers (8.10) in front of a **2 KB request FIFO**, which drains into the tile's row node;
- **the receive side:** four read-only registers through which a receiver reads the head of **its target's receive queue in the request tree** (3.3). **On tiles 0–63 they are connected to nothing:** they read an empty queue and request pending never rises. On C&C 1–3 they read targets 0, 1 and 2's queues; target 3's is deferred with the file system (item 119).

**Send and receive share offsets:** a store sends, a load receives (8.10).

**Sending a request** mirrors the send port (4.11):
- **`REQUEST_SET_W0`, `REQUEST_SET_W1`, `REQUEST_SET_W2`** (stores) hold the request's words 0, 1 and 2: bits [31:0], [63:32] and [95:64].
- **`REQUEST_SEND`** (a store) supplies bits [127:96] and pushes the request. **Every request is 128 bits** (3.3): software stores its words in ascending order, and the last store sends. v1.1's `REQUEST_SEND_32`, `_64` and `_128` are withdrawn, and with them the request's size.
- **Software writes** the target [9:7] and the request proper [127:10], laid out by the target (3.3), and writes 0 in requestor [6:0], which the tree overwrites.
- **At the push, `REQUEST_SET_W0`–`W2` all clear to 0** *(Proposed)*, as the send port's do, so a stale word never reaches the next request.

**The request FIFO** *(Proposed: its format)*: 2 KB, **128 entries of 128 bits**, one request each. The port drains the head request into its row node one 16-bit beat per cycle, bits [15:0] first (3.3), as fast as the bus takes them, and frees its entry when its last beat has left.
- **The stall rule** *(Proposed)*: a `REQUEST_SEND` store stalls only if the FIFO is full and no entry is freed in that cycle. This is the send port's rule (4.11): a stall, not an overflow. The FIFO never overflows, so it has no error.
- **`request_busy`** (`STATUS` [6], 8.2): the FIFO is full. A program that sees it 0 can push one request without stalling. When it falls, the FIFO posts **request FIFO no longer full**, `EVENTS` [11] (4.12), so a program can sleep instead of stalling. Heavy use stalls only the tile that sends.
- **No acknowledgement for issuing.** A request's effect shows only through its answer (6.7). The depth is for debug messages, which have no answer and come in bursts.
- **Timing:** a send store at *t* pushes at the end of *t*, and the port sends the request's first beat at *t* + 1 at the earliest; on an idle bus the request is in its target's queue about `reqbus_latency` after the store (3.3).

**Receiving a request** (a unit that is a target's receiver; on tiles 0–63 the queue is always empty):
- **`REQUEST_GET_W0`, `REQUEST_GET_W1`, `REQUEST_GET_W2`** (loads) return bits [31:0], [63:32] and [95:64] of the **oldest** request in its target's receive queue, without removing it. `REQUEST_GET_W0` carries the requestor [6:0], stamped by the tree, and the target [9:7].
- **`REQUEST_RECEIVE`** (a load) returns bits [127:96] of the oldest request and **pops it.** Software loads a request's words in ascending order, and the last load pops, mirroring the send.
- **An empty queue.** A load of `REQUEST_RECEIVE` with the queue empty returns 0, pops nothing and sets **receive underflow**, `ERRORS` [14] (4.13); on tiles 0–63 every such load does. A load of `REQUEST_GET_W0`–`W2` with the queue empty returns 0 and sets nothing *(Proposed)*.
- **Request pending**, `EVENTS` [12] (4.12), is a **live** event: true while the target's receive queue holds a request. A load of `EVENTS` changes nothing about it; only `REQUEST_RECEIVE` removes a request, so **queued requests never merge into one wake**: a `WFI` with [12] in the mask falls straight through while any request is waiting. Its `EVENTS_MASK` bit resets to 0. On tiles 0–63 it is always 0.
- **The receive queues are in the request tree** (owner, 2026-10-29, revising 2026-10-26's 16-entry queue in every tile): 128, 256 and 128 entries of 128 bits for C&C 1, 2 and 3, one credit per entry held at the root (3.3). Every `REQUEST_RECEIVE` pop returns a credit. A tile has no receive queue.
- **Timing** (4.11's sampling rule): a load returns the queue's head as it stood at its sample point; a `REQUEST_RECEIVE` load pops at the end of its last cycle, so the next load sees the next request, and [12] falls then if it popped the last one.

**After an error** the request port keeps running (4.13): the handler's reports go out through it, and a receiver's queue keeps filling while credits last. **The reset** (6.1), always the whole chip, empties every request FIFO, the tree and every receive queue together and restores the credits, so no request is cut short on one side only. The `W` registers are configuration registers: undefined at reset (8.1).

**What a program must keep to** (arming before sending, at most 1, 3 and 1 requests outstanding to C&C 1, 2 and 3, no reads across a group run) is in 3.3 and the memory system's contract (6.7).

### 4.19 The replayer (peripheral type 4)

*Moved in v1.35 to `spec/jolt/replayer.md`, with 4.19.1–4.19.15.*

### 4.20 UART

*(Owner, 2026-10-26, part D; a peripheral since 2026-10-28.)* **The UART is part of C&C 1's peripheral,** Ingress/Egress and the UART (6.6), the only unit wired to a serial port (owner, 2026-10-28). It has left every other unit, along with v1.1's tile registers `UART_TX`, `UART_RX`, `UART_STATUS` and `UART_DIVISOR` and its events [22:21]. Its registers are peripheral-defined words of C&C 1's peripheral's window (8.14).

- **Frame:** 8 data bits, no parity, 1 stop bit, fixed.
- **FIFOs:** a TX FIFO and an RX FIFO of 16 bytes each *(Proposed)*.
- **Bit timing** is counted from the board's **reference oscillator**, not the core PLL, so the rate is right before the PLL is configured. `UART_DIVISOR` holds reference-oscillator cycles per bit; its reset value gives **57,600 baud**: round(`f_ref` / 57,600), where `f_ref` is the oscillator's frequency *(param, Open: the board isn't chosen)*. A 25 MHz oscillator gives 434. A new divisor takes effect at the next idle point of each direction, so a byte in flight is never cut.
- **Line errors are status, not errors** (owner, 2026-10-28): a byte arriving with the RX FIFO full is dropped and sets **RX overrun**; a byte with a bad stop bit is dropped and sets **framing error**. Both are sticky bits in **`UART_STATUS`**, cleared by **writing 1**, and never in the peripheral's `ERRORS`: every C&C 1 error raises the toast pin (6.1), and line noise on the serial port must not. `UART_STATUS` is a word of its own, not the standard `STATUS`, which is read-only and live (4.17.4).

**The registers** (C&C 1's peripheral, 8.14): `UART_TX` (W) pushes a byte, **stalling while the TX FIFO is full** (a third stall source beside the send and request stores, 4.11; at most one byte time, about 174 µs at 57,600 baud); `UART_RX` (R) returns the oldest byte with a valid bit and pops it only when valid (a read side effect, at 4.11's sampling rule); `UART_STATUS` (R/W1C) gives both occupancies and the two line-error bits, and its reads have no side effects; `UART_DIVISOR` (R/W).

**Events** (C&C 1's peripheral's `EVENTS`, 4.17.3), both **live**, never consumed: **UART RX not empty** and **UART TX not full** (8.14). Like every peripheral event they reach C&C 1's `EVENTS` [13] while masked in the peripheral's `EVENTS_MASK`, which resets to 0.

**At the reset** (6.1) both FIFOs empty, the status bits clear and the divisor takes its reset value.

**Debug output from other units** goes to log files through the file system (owner, 2026-10-28; 6.6), whose tails are flushed to LPDDR when the chip fails (6.1). How meshsim shows each unit's output is meshsim's business.

**Cost:** two 16-byte FIFOs, a 16-bit divisor and the two shifters: about 0.002 mm², in C&C 1 only [estimate].

### 4.21 The Tracer (peripheral type 3)

*Moved in v1.35 to `spec/jolt/tracer.md`, with 4.21.1–4.21.10.*


---

## 5. Timing summary

Network: injection at cycle t → route decision (1) → output FIFO (≥ 1) → link (`link_latency`) → route block (1) → … → eject FIFO → selector → traffic director → write engine, GATHER engine or port B. Minimum latency is **3 + 4h cycles for h hops** with 3-cycle links [meas: meshsim]; queueing adds a bounded amount.

| path | timing |
|---|---|
| big SRAM | one access per area per cycle, in arrival order; a read's latency is its queueing plus the array's read latency, both *(Open)* for the hardware (4.1) |
| write counters | a counted packet decrements its counter K cycles after the traffic director accepts it, K a parameter, default 8, implementation-defined, in meshsim at least max(2·L + 4, L + 4, `cm_latency`, `rp_latency`) (4.8); its write is then readable, and counters done (`EVENTS` [7]) or the counter's own done event is set (4.12, 4.14) |
| table memories | engine requests and returns cross `ts_engine_latency` each way; controller window one cycle (4.10) |
| controller-memory path | mode-0 writes to regions 0x00 and 0x01 reach port B `cm_latency` = 2 cycles after the director [built] |
| controller | one instruction per cycle; loads and stores to data memory in one cycle, load-use 0; **loads from the register block and the peripheral windows 2 cycles, from the table window (`HIST`) 1 cycle** (T, 4.11); stores to them single-cycle and posted; taken branch +1; multiply 3; stalls only on an occupied send holding register, a full request FIFO, a full UART TX FIFO (C&C 1), or in `WFI` (4.11, 4.18, 4.20) |
| registers | a store at *t* takes effect at the end of *t*, seen by the owning unit at *t* + 1 and by any later load; a load issued at *t* with latency *L* returns the state at the end of *t* + *L* − 2 (its sample point) and takes any side effect at the end of *t* + *L* − 1 (4.11); status computed at *t* is visible to a load whose sample point is *t* or later |
| address generator | a run's first request is offered 4·`ts_engine_latency` + 3 cycles after the start store for a group run (S_0), 2·`ts_engine_latency` + 2 for a stream run; one cycle less counted from the cycle the start takes effect (4.2) |
| GO | a `PULSE_GO` store in C&C 1's peripheral reaches every tile `go_latency` cycles later; a pulse at t sets `EVENTS` [8] at the end of t, wakes every core in `WFI` at t+1, ends the `done` gate and clears `done` and "command landed"; engines start at the start store (`PULSE_DMA`) that follows (6.3) |
| READY lines | READY fires when "command landed" and `done` are both 1; a pulse at s sets the tile's bit in C&C 1's peripheral, readable from s + `ready_latency` + 2; equal for every tile |
| error lines | a unit's `ERRORS` becoming non-zero reaches its bit in C&C 1's peripheral `error_latency` cycles later, fixed (4.13, 6.1) |
| peripherals | a write into a peripheral's region lands `rp_latency` = 2 cycles after the director, on every peripheral *(Proposed: the value)*; a peripheral's DMA moves at most one packet per cycle (4.17.5, 4.17.9) |
| replayer | the record's words 0–65 are reloaded before each pass, about 66 cycles from big SRAM (v1.26; v1.25's run copied the image in 64), and a run starts at once, one row per 3 cycles (5 for a multiply) when its queues are fed; a pass is typically 65,536 rows (`RUN_CONFIG`, 4.19.9), about 216K cycles [estimate] (4.19.7, 4.19.10) |
| Tracer | a row in `tr_row_cycles` = 4 cycles, 6 for a multiply, plus stalls: a fill `ms_line_hit` = 6 or `ms_line_miss` = 80 cycles, a map probe a fill plus 2 per line (4.21.9); a run of up to 65,536 rows, then its drain, one FIFO entry per cycle per FIFO and one DMA packet per cycle, before done (4.21.6) |
| request bus | one 16-bit beat per cycle per hop; a request is 8 beats; on an idle bus about `reqbus_latency` (≈ 15–20 cycles [estimate]; 17 [meas: meshsim]) from the send store to its target's queue; the root passes at most one request per 8 cycles for the whole chip (3.3, 4.18) |
| events | every event is set at the end of the cycle its cause happens; live events follow their conditions with the same timing; a load of `EVENTS` or `EVENTS_PEEK` sees it, and a pending event releases a waiting `WFI`, in the next cycle (4.8, 4.12); a peripheral's bit in the controller's `EVENTS` aims to be current by the instruction after a consuming load (4.17.3) |
| reset | the chip is reset from outside, hard or soft; every unit then starts at 0x02000000 in its boot ROM (6.1, 6.2) |

---

## 6. Control

*Figures 1.2, 2.1, 2.2, 2.5 (at v1.1).*

C&C 1 sequences the phases. It writes commands into the tiles' command blocks, collects their READY pulses and pulses GO, all through its peripheral (6.6). It does little work but sits on the critical path of every phase transition, so it is on-die and fast rather than driven by the host over the slow bus. C&C 2 (the file system) and C&C 3 (the page manager) serve the tiles' requests and run outside the phase cycle (6.6).

### 6.1 Reset, errors and the toast pin

**There is no HALT state** (owner and Tony, 2026-10-28). v1.1's RUN/HALT net, `run_latency`, per-unit halt and the resident loader are gone. **The reset is the only way to stop anything,** and coming out of reset means running in the boot ROM (6.2). Units start one at a time, each when its own first command lands, so C&C 1 can bring up a single unit, or start C&C 2 and C&C 3 and check them before loading the mesh (6.2). What is given up is stopping a single unit to inspect it: after an error there is what the handlers' reports carry, and then the chip restarts.

**An error, end to end** (owner and Tony, 2026-10-28 and 2026-10-29; the simplest scheme that works):
1. **An error sets a bit in a unit's `ERRORS`** (for example unexpected arrival), which raises its hard interrupt. Its handler sends its reports to C&C 1 over the request bus and parks forever (4.13). Its engines and peripherals carry on, unserviced, until the reset.
2. **Its error line** sets its bit in C&C 1's peripheral's `ERROR_LINES_0`–`2`, by endpoint id, `error_latency` cycles later (2). The lines are **live**, following each unit's `ERRORS`.
3. **The unit-error bit,** C&C 1's peripheral's `ERRORS` [0] = OR(`ERROR_LINES`), live, sets C&C 1's `ERRORS` [11] (4.17.6), and C&C 1 takes its hard interrupt. C&C 1's own errors interrupt it directly. **Every error is terminal for the chip:** nothing on chip recovers.
4. **C&C 1's handler,** best effort, with nothing waiting for it: it **immediately tells the file system to start flushing its logs to LPDDR** (C&C 2; the units' log files, 6.7), **drains the units' reports** from its request port (the fixed report sets, 4.13: 24 from an image's handler, 2 from a unit still in its ROM, told apart by the first report's sequence number), and **sends the dump out: down LVDS when an FPGA is in the loop, over the UART otherwise**. It never returns to the interrupted code. It bounds its waits by cycle count (`mcycle`, `mtime`, 4.11). *(Its details are C&C 1's firmware's.)*
5. **The toast pin** rises (below), and **the outside world resets the chip** (below).

**The "I'm toast" pin** (owner and Tony, 2026-10-29). Ingress/Egress raises it **as soon as C&C 1's `ERRORS` is non-zero** (the unit-error bit included) **or its keep-alive watchdog reaches 0**, and it stays **latched until the reset**. Since nothing recovers, every error is terminal, and the one pin covers them all, unit errors included. The software pin, **`LED`** (8.14), stays: C&C 1 can blink codes on it while it is healthy.

**The keep-alive watchdog** (owner, 2026-10-29), `WATCHDOG` in C&C 1's peripheral (8.14): **0, the reset value, is off; writing a limit *N* > 0 starts it; a write or a `PULSE_GO` store restarts it from the last *N*; reaching 0 posts the watchdog event (depth 1) and latches the toast pin.** C&C 1 sets the limit above each phase's expected length, so the watchdog is the backstop for a unit that hangs without an error (a trap with no `ERRORS` bit, 4.13; a lost packet). It is the only watchdog, and **it counts core cycles** (owner, 2026-10-29).

**The outside world resets the chip** (owner and Tony, 2026-10-29). **The chip never resets itself.** Two inputs:
- **hard reset:** everything, the AXI bus and the memory side included; LPDDR's contents are lost. Power-on is a hard reset, held until power is stable.
- **soft reset:** everything except a short list: the LVDS bus, the AXI bus and what lies beyond it, and the LPDDR controller and PHY, so refresh continues and **LPDDR keeps the flushed logs**. *(Open, item 124: the exact list, with Tony.)*

**Nothing tells the outside world the flush has finished, and nothing needs to** (owner, 2026-10-29). With an FPGA in the loop, C&C 1 sends the dump down LVDS and the FPGA waits as long as it wants after the toast pin rises before applying a soft reset; **about 0.1 ms** (about 100,000 cycles at 1 GHz) is only a default. With no FPGA, a person resets the chip by hand, slowly enough for the UART dump. **After either reset** every unit boots from scratch through the boot ROM (6.2); after a soft reset the file system comes back up over LPDDR's contents and the wreckage can be read from it *(Deferred, item 119: the file system mounting existing state)*.

**What the reset does** (owner, 2026-10-29). Every unit, C&C 1 included, is reset as at power-on, **except the memories**:
- **The controller core** starts at **0x02000000**, in the boot ROM, with `mstatus.MIE` = 0 and the CSRs at their reset values *(Open, Q1, item 121)*.
- **Empty, with every credit returned:** the router FIFOs and pipeline stages, the return queues, the area queues, the write engine's input FIFO and pointer stage, the K delay line, the send holding register, the read port's queue, the GATHER engine's queue, the request FIFOs, the request tree and the receive queues; the read tokens are all free (`send_busy` and `request_busy` 0).
- **Zero:** every `ERRORS` and every peripheral's `ERRORS`, the write counters, their done events and qualifiers, `done`, the `done` gate, "command landed", any pending GO, `EVENTS`' sticky events, `EVENTS_REQUIRED`, `rd_running`, `rd_done`, the round-robin pointers. **`EVENTS_MASK` is [8] | [7]** (GO and counters done, 4.12). Every peripheral is idle, its events at 0 and its `EVENTS_MASK` 0 (4.17.3). In C&C 1's peripheral: the READY bits, `READY_EXPECT_0`–`1`, `WATCHDOG` (off), `LED` (0), and the toast latch.
- **Undefined:** the configuration registers (8.1).
- **Kept, not cleared:** the controller memories, the tables, big SRAM and the peripherals' memories keep whatever they hold (undefined after power-on). The boot ROM never writes data memory, so an image loaded after the reset starts on the state its loader gives it (6.2).
- **No hold is needed** (owner, 2026-10-29): the reset is the whole chip, so the error lines' pipeline flops are reset with everything else, and no error raised before the reset can reach C&C 1's peripheral after it. (The decisions' 21 asked C&C 1 to hold its reset for `error_latency` because C&C 1 and its peripheral then stayed outside the reset, with stale lines still in flight toward them.)

### 6.2 Boot

**The boot ROM** (owner and Tony, 2026-10-28; exact, 2026-10-29). Every controller has a small **boot ROM** in region 0x02, fetch-and-load only (2.2), and **the reset vector is 0x02000000**. One ROM serves every unit, **branching on `PARAMS`' endpoint id**. It does exactly this:
1. **set `mtvec`** to the ROM's own report-and-park handler (4.13), which never writes data memory; interrupts stay off (`MIE` = 0, as the reset leaves it);
2. **branch on `PARAMS`' endpoint id** (8.2);
3. **tiles (0–63):** pulse `done` (`PULSE_DONE`); loop `WFI` and load `EVENTS` until GO is seen (so GO is consumed); then **if the value that load returned has "command landed", `EVENTS` [9], set, go to 5; otherwise go back to the start of 3**, pulsing `done` again and waiting for the next GO (owner, 2026-10-29). It tests the **event**, not `STATUS` [5], which GO clears; the reset mask doesn't consume [9] and the ROM must not, so it stays pending (4.12);
4. **C&C 1–3 (64–66):** set `EVENTS_MASK` to "command landed" ([9]); loop `WFI` and load `EVENTS` until [9] is seen; restore `EVENTS_MASK` to its reset value, [8] | [7];
5. **load the command word (slot 0), then the jump address (slot 1), and jump.**

- **The ROM never writes data memory** (no stack, no variables), which the overlays are writing while it runs; it reads only the command block, after the command has landed. The first command has parity 0, so the jump address is in slot 1 (6.3).
- **A tile's ROM is a minimal `wait_for_command()` that jumps only if a command has landed.** Its `done` pulse and the landing command raise READY like any phase's (6.3), and the image starts after GO. GO reaches every tile, so with units started one at a time (below) a tile still waiting in the ROM sees GOs meant for others; without the test it would jump into empty memory. C&C 2 and C&C 3 stay out of READY/GO and jump when their command lands.
- **Enabling interrupts is the image's job** (owner, 2026-10-29; a software convention): **boot part 2**, the first command, which receives the big-SRAM size and loads it (step 6 below), sets `mtvec` to the image's handler and enables `mie.MEIE` and `mstatus.MIE`.
- **Size: 256 bytes** (64 words) per controller (DECISIONS-v1.25 section 8d); v1.2 said "a few dozen bytes", and meshsim's was about 728. The boot path (steps 1–5) takes 30 words, unchanged, and the handler's two-report dump (4.13) brings it to **48 words** in meshsim's ROM [meas: WORKER11]; **the rest is left free** (owner). meshsim's linker region and loader refuse an image over 256 bytes. It costs its region in the map too.

**The boot sequence** (owner, 2026-10-29):
1. **The reset is asserted** (6.1): everything is zeroed as at power-on except the memories.
2. **The reset is released:** every controller starts at 0x02000000 and waits in its ROM.
3. **C&C 1 is booted from outside** (owner, 2026-10-29): endpoint 64 takes the C&C 2/3 branch of the ROM, and **the outside world lands C&C 1's overlays and first command**: the host through Ingress/Egress on silicon, the test harness in meshsim. **In meshsim, C&C 1 is a behavioural model by default** (`src/units/cnc.h`), driving its peripheral's registers from the harness, and so are C&C 2 and C&C 3; with `cc_cores` (`tests/mach.h`) all three are cores, booted through the host port.
4. **C&C 2 and C&C 3:** C&C 1 sends each its overlays into controller memory, **uncounted**, then **lands its first command**. Overlays and command come from one sending unit to one unit through port B, so the command lands last (3.2): **ordering holds per sending unit, so they leave by one unit** (one stream run, the command as its last stream, as recipe 22 does; or the send port alone), **or the overlays' run ends with a fence** (3.1) that C&C 1 waits for before it sends the command from its send port. The unit's ROM jumps; **its image's start-up sends C&C 1 a "started" request**, and C&C 1 waits for both before loading the tiles, so the file system and the pager are up before anything uses them.
5. **Tiles:** C&C 1 sends the overlays, uncounted, then the first command. The ROM has pulsed `done` and waits for GO, so "command landed" raises READY, as in a phase. **When every expected tile is READY, C&C 1 issues GO** (6.3); GO clears `done` and the status bit "command landed" as always, and the ROM, seeing the event [9] pending, jumps to the address in the command block. The image starts clean. Overlays may be streamed as broadcasts from C&C 1's big SRAM (4.2, *d* = 126).
6. **Boot part 2, the first program, loads big SRAM** by announced staging (below) and enables interrupts (above): the command announces the data; READY; GO; each tile arms its counters in its start path; C&C 1 sends the data **counted**, no sooner than `stage_delay` after its GO store; each tile waits for counters done, then pulses `done`. The command never has to vouch for big-SRAM data, whose write path is slower than port B's.

- **The overlays and the first command are of one kind** (owner, 2026-10-29): all broadcast or all unicast, since the mesh orders packets only within a kind (3.2). meshsim checks that no overlay write lands after a unit's "command landed" and before its start.
- **C&C 1 lands a tile's first command only when that tile should start at the next GO** (owner, 2026-10-29): the ROM jumps at the first GO after a command has landed (step 3), whatever `READY_EXPECT` says, so a tile whose command has landed starts at the next GO.
- **Boot order is the order of the first commands.** Every unit leaves the reset together, but each starts only when its own command lands; the rest wait in their ROMs, so C&C 1 can bring up one unit at a time.
- **Images into controller memory** are 128-bit mode-0 packets (four words each, the address advancing by 16); an image's length is padded to whole packets, so up to three zero words land just past its end, and nothing may be placed there before the load.
- **The window between GO and arming is kept** (owner, 2026-10-29): every phase has it, and v1.1 closes it by timing (identical start paths; nothing arrives sooner than `stage_delay`). If it fails, unexpected arrival says so. *(Candidate for later, item 125: signed counters, so an arrival before arming just goes negative and the order stops mattering; strays are then caught as done early.)*

**Staging later images and overlays:**
- **How:** C&C 1 places each image **by address**, as mode-0 packets to region 0x03 (owner, 2026-10-28). v1.1's staging by source through `PTR[64]` is gone, and `PTR[64]` is no longer special (4.9).
- **Where:** in area A7 below its top 128 KB (the default geometry's eighth 512 KB block, 4.1). **The top 128 KB of A7 is sort scratch** (skew handling's values staging needs up to ≈ 4.7K values beyond areas A0 + A3; section 14), so images and overlays must fit in A7's lower 384 KB. The staging area can sit relative to the top of memory, which C&C 1 knows from the chip's build.
- **Packets are 128-bit** *(Proposed)*: C&C 1 sends a whole row per packet, with a quarter of 32-bit packets' network load. Each image starts on a row and is padded to a whole number of rows *(Proposed)*.
- **When** (owner, 2026-10-26, 2026-10-28 and 2026-10-29): big-SRAM data is **always counted**, staging included, so it reaches a unit only **after a command that tells it what to expect**: the unit arms a write counter first and waits for its done. Never during a mesh operation. Staging into a running unit is coordinated by software; there is no halted state to write into. Code overlays into controller memory follow 6.5.
- **Announced staging waits for the arming** (owner, 2026-10-27, part M). After the command that announces staging, the receiving tile arms its counter in the start path, after GO (6.4): about 10 cycles after GO at 2-cycle register loads [derived from the measured start path]. C&C 1's first staged packet can travel faster than that, so **C&C 1 sends it no sooner than `stage_delay` = 64 cycles after its GO store** (owner, 2026-11-06: a software constant, the sender's own wait, so retunable; 64 with `go_latency` at 8, about three times the arming's end, and what meshsim's harness has always used). The Tracer's start barrier uses the same rule: C&C 3 sends nothing sooner than `stage_delay` after the GO (DECISIONS-v1.3 section 24). The bound is tied to the start path: a command whose start path arms later says so, and C&C 1 waits correspondingly longer. A packet that beats the counter is an unexpected arrival (`ERRORS` [2]), so a violation cannot go unnoticed (meshsim flags it).
- **Running a staged image:** the unit copies it into controller memory with a mode-0 loopback stream (6.5).

### 6.3 `done`, READY, GO and `WFI`

**Two bits per tile** (in `STATUS`, 8.2):
- **`done`** says the tile has finished its work and can take the next command. It is:
  - set by the controller with `PULSE_DONE`;
  - cleared by GO and by the reset.
- **"Command landed"** is set when a write covering command slot 0 lands through the traffic director (4.8, 4.12), and cleared by GO and by the reset. The same write posts the command-landed event, `EVENTS` [9] (4.12), which only a load consumes; nothing in the reset mask does, so it stays pending (4.12).

**READY is automatic.** When "command landed" and `done` are both 1, the tile pulses its READY line once. The two can become true in either order, so a command may land before or after the tile finishes. The controller never pulses READY directly.

**The READY line** is its own line, one per tile into C&C 1's peripheral, at equal latency `ready_latency`: a one-cycle pulse on the rising edge of (`done` & "command landed"), setting the tile's sticky bit in `READY_LINES_0`–`1` (8.14). **READY is for the phase cycle only, tiles 0–63** (owner, 2026-10-28): C&C 2 and C&C 3 have no READY line and aren't wired to GO.

**The command convention.** C&C 1 writes any arguments first, then the command word to slot 0, all as uncounted mode-0 writes into the **command block** (4.12). In-order delivery means the whole command has landed when "command landed" is set, **provided all of a command's packets to a tile are of one kind**, all broadcast or all unicast (3.2).
- **The command word** (`fw/include/mesh.h`): [31:24] type, [23] parity, [22:0] argument.
- **Alternating argument blocks** (owner, 2026-10-26): even-parity commands put their arguments in **slots 1–7**, odd-parity commands in **slots 8–15**, seven arguments each, and the tile reads the block its command's parity names. A command sent right after GO can then never overwrite the arguments of the one the tile is still reading. Slot 0 of the next command lands at the earliest 4 cycles after GO; the simulator checks that no slot-0 write lands before the previous command word was read [meas: meshsim].

**GO** is a single global net from C&C 1's peripheral to tiles 0–63, balanced so every tile sees a pulse on the same cycle, `go_latency` after the store. C&C 1 drives it with **`PULSE_GO`** in its peripheral (8.14; owner, 2026-10-28), which **clears READY bits 0–63 and drives GO in one store** (a READY pulse arriving in the same cycle is kept) and restarts the watchdog (6.1). The controller's block has no GO register: it is the same on every unit (6.6). **GO posts the GO event, `EVENTS` [8]** (4.12), which wakes `WFI`, ends the `done` gate, and clears `done` and "command landed". It does not start the address generator; the controller starts it with `PULSE_DMA` (4.2). Like every event, GO is sticky until a load consumes it, so `wait_for_command()` consumes it on waking; it arrives only while the tile waits in `wait_for_command()` (below), so a pending GO is never stale in a program that uses the helpers.

**`WFI`** stalls the core until the wake term holds, then continues at the next instruction. **All wakes are events** (4.12): `WFI` completes on the events pending in `EVENTS` under `EVENTS_MASK` and `EVENTS_REQUIRED`, among them GO ([8]), counters done ([7], which also covers a request's data and reply, 6.7), a counter's own done ([6:0]), the end of a DMA run ([10]), a received request ([12], 4.18) and the peripherals' live bits ([15:13], 4.17.3; the replayer's, 4.19.9). From the `done` pulse until GO the `done` gate overrides the term to GO alone. A wake consumes nothing; the `fw/include/mesh.h` helpers follow every `WFI` with a load of `EVENTS` (4.12). Errors don't wake `WFI`; they trap (4.13).

**`wait_for_command()`**: the controller pulses `done` and then executes `WFI` **immediately**, as two back-to-back instructions in a `fw/include/mesh.h` inline helper; on waking it **loads `EVENTS`, consuming GO** (GO stays in `EVENTS_MASK`, 4.12), then **reads command slot 0 and returns the command word** (the simulator checks that every command word is read before the next lands). The boot ROM's tile branch is the same helper, without the read (6.2).
- The load after `WFI` takes the same fixed latency on every tile (4.11), so the path from GO to the start store stays identical everywhere.
- This is safe because GO cannot come back sooner than the READY round trip (≈ `ready_latency` + 3 cycles), so the core is always in `WFI` when GO arrives.
- `done` stays an explicit pulse rather than "being in `WFI`", because a tile also uses `WFI` to wait for arrivals, which mustn't look finished to C&C 1.
- The gate makes it robust: from the `done` pulse until GO only a pending GO can wake the core (4.12), whatever the program's `EVENTS_MASK` holds.
- READY waits for `done`, so a command that lands before the tile waits is not lost.

**A one-command phase** (a local sort, a gather):
1. The tile finishes its previous work and calls `wait_for_command()`.
2. C&C 1 writes the command, slot 0 last. When it has landed and `done` is set, READY goes out.
3. When every expected tile is READY (C&C 1's live "all tiles READY" event: `READY & READY_EXPECT == READY_EXPECT ≠ 0`, 6.6), C&C 1 stores `PULSE_GO`, which clears the READY bits and sends GO. Every core wakes, reads its command and runs the phase.

**A phase whose engines must start together** (an all-to-all) uses two commands:
1. **"Configure":** the tile wakes, writes its tables and `DMA_CONFIG` for the run, and calls `wait_for_command()`.
2. **"Start"**, a one-word command: the tile wakes, stores its counters (6.4) and pulses start (`PULSE_DMA`).

Every core leaves `WFI` on the same cycle and runs the same fixed code to the start store (no caches, single-cycle loads; the helper's load of `EVENTS` is part of that code, and so is any base-address instruction the register header adds, 4.11), so the engines start on the same cycle on every tile, and the start's lead-in (4.2) is the same everywhere. **The path from `WFI` to the start store must be identical code on every tile.**

**A traced segment** (v1.3; 4.21.10) uses the same cycle twice, around the Tracer: **READY with nothing armed, then GO**, on which each tile arms for its record's fixed pieces and C&C 1 starts C&C 3, which sends nothing sooner than `stage_delay` after it; then, once every tile's window has landed, **READY again with its counters at 0, then the final GO**, the replay. C&C 3 takes no part in READY/GO; C&C 1 sequences both barriers (meshsim's `CC_SEGMENT`).

**Costs:** each command costs one READY → GO round trip, ≈ `ready_latency` + 3 cycles [meas: meshsim] plus C&C 1's loop: ≈ 11 cycles at `ready_latency` 8, under 1K cycles over about 40 phases [estimate].

### 6.4 Phases that receive

- **A tile whose phase receives** arms its counters **in the start path, after GO: write the counters first (each write adds; at a phase start the count is 0), then pulse start.** Arming them during "configure" would break invariant 4 at configure's `done`; storing them before the start pulse means no arrival can beat its counter. The start path is identical code on every tile, so every tile's start store lands on the same cycle (plus skew), and its first request follows by the lead-in of 4.2. Measured from C&C 1's GO store to every tile's first request, at 2-cycle register loads, meshsim's reference (4.11) [meas: meshsim]: **12–13 cycles** for a stream run started by a counter write then start (8–9 to the start store, then 2L + 2), and **20–21** for a group run on the all-to-all recipe's longer start path (13–14, then 4L + 3); a group run with the shortest start path would take 15–16 [derived from the two measured parts]. The three cycles over v1.0's 9–10 and 17–18 (1-cycle loads) are GO's event becoming visible one cycle after GO is seen, since event pulses cross into the island through a flop (section 12), and `wait_for_command()`'s two loads at 2 cycles each (owner, 2026-10-27, part M). Every tile's first request lands within the 0/+1 start skew. The tile then `WFI`s for counters done (4.12) when it has nothing else to do.
- **Every expected arrival must have landed before the tile pulses `done`** (invariant 4). Counters done guarantees it: by K, every counted write is in place (4.8).
- **A tile that only sends** in a phase still counts its own loopback writes, if it has any, and waits for them before `done`.

### 6.5 Block copy into controller memory (overlays, tables)

1. Write the `STREAM` entry, one store per array that changes: `STREAM_SRC` the source (aligned to the read size); `STREAM_DEST` the destination's byte address in region 0x00 or 0x01; `STREAM_COUNT` the count, read size 128 (or 64 or 32), a write counter *n*, mode 0 and *d* = 0.
2. Write the number of packets (elements) to `WRITE_COUNTER_n`, which adds it to the count (0 here). This also clears counters done (`EVENTS` [7]), the counter's own done event and their qualifiers.
3. Write `DMA_CONFIG`: a stream run of one stream, starting at that entry.
4. Pulse start (`PULSE_DMA`).
5. `WFI` (the helper, with [7] in `EVENTS_MASK`): counters done is set K cycles after the director accepts the last packet, when it is in controller memory, and the helper's load consumes it.

The copy runs at up to 128 bits per cycle through the loopback (4.2): a source at read size 128 writes four words per packet, with no polling and no C&C 1 traffic. A copy whose length is not a whole number of rows ends with a smaller-size stream, or is padded. The code doing the swap must sit outside the region being overwritten: any code the overlay does not replace.

**Overlays from C&C 1** (stage overlays broadcast at stage boundaries) are uncounted mode-0 writes into controller memory while the unit waits for its next command, which lands after them from the same source through port B (6.2), so "command landed" says the overlay is in.

### 6.6 The controllers and their registers

- **C&C 1, C&C 2 and C&C 3 are tile instances** (owner, 2026-10-28; section 2): each has the tile's controller (4.11), the same 32 KB + 32 KB memories, the boot ROM, 2 MB of big SRAM and its engines, and **the same controller register block as every tile** (8.1), with the same toolchain and the same `mesh.h`. Each attaches to its tile's fifth port (4.16) and **has one peripheral, its engine** (2): Ingress/Egress and the UART for C&C 1, the Updater/Binder/RW engine (the memory system, 6.7, section 7) for C&C 2, the Tracer for C&C 3. **An engine's error is its controller's peripheral error**, so it raises that controller's error line (4.17.6); the engines need no lines of their own.
- **Every chip-level function lives in C&C 1's peripheral** (owner, 2026-10-28; integrator's proposal accepted 2026-10-29, to be reviewed again when C&C 1's firmware is written), so the controller block stays identical on every unit. Its peripheral-defined words, from word 32 (exact offsets in 8.14):

| register | access | meaning |
|---|---|---|
| `ERROR_LINES_0`–`2` | R | **live** unit errors, one bit per unit by endpoint id: 0–63 the mesh tiles, 64 unused (C&C 1 itself), 65 C&C 2, 66 C&C 3, the rest of the third word reserved |
| `READY_LINES_0`–`1` | R | the tiles' **sticky** READY bits, 64 (READY is tiles only) |
| `READY_EXPECT_0`–`1` | R/W | the tiles C&C 1 is waiting for. **"All tiles READY"** is a **live** event: (`READY` & `EXPECT`) == `EXPECT` **and** `EXPECT` ≠ 0, so one-at-a-time bring-up works and C&C 1 can't miss it whatever order it writes `EXPECT` in |
| `PULSE_GO` | W | clears READY bits 0–63 and drives GO, in one store; a READY pulse arriving in the same cycle is kept; also restarts the watchdog (6.1, 6.3) |
| `WATCHDOG` | R/W | the one keep-alive watchdog (6.1): 0 (the reset value) is off; writing a limit *N* > 0 starts it; a write or `PULSE_GO` restarts it from the last *N*; reaching 0 posts the watchdog event and latches the toast pin |
| `LED` | R/W | the software pin, bit 0 (6.1) |
| UART words | | `UART_TX`, `UART_RX`, `UART_DIVISOR` and `UART_STATUS`, whose line errors are **write-1-to-clear** (4.20) |
| host-interface words | | Ingress/Egress *(Deferred: with its design)* |

- **C&C 1's peripheral's `ERRORS`:** [0] is **the unit-error bit**, OR(`ERROR_LINES`), **live**; the other bits are the peripheral's own errors (Ingress/Egress), sticky. **Its `EVENTS`:** all tiles READY (live, above); the watchdog expired (depth 1); the UART's RX not empty and TX not full (live, 4.20); the host interface's events *(bit positions in 8.14, Proposed)*. **Its outputs:** GO (2, 6.3), the toast pin and `LED` (6.1).
- **The phase loop** (C&C 1's software): write `READY_EXPECT`, send the command, sleep with "all tiles READY" masked in its peripheral's `EVENTS_MASK` and its `EVENTS` [13] in its own mask, then store `PULSE_GO`. The watchdog, restarted by every `PULSE_GO`, catches a phase that never completes (6.1).
- **C&C 1 sends** through its own send port and address generator like any unit (4.11, 4.2); its packets enter the mesh at tile 0's fifth port (4.16), with src 64. **It receives** the units' reports, and the "started" requests of C&C 2 and C&C 3 (6.2), through its request port's receive registers, which read target 0's 128-entry queue in the request tree (3.3, 4.18), and any packets addressed to it land in its memories like any unit's, counted by their counter fields. v1.1's receive FIFO at C&C and its error flag are gone.
- **C&C 3's peripheral is the Tracer** (4.21; v1.3), type 3: its window is 8.16, its region holds the 68-word state record, the decode table and ROM, and the advice and expansion queue ports (4.21.2), and its DMA drains its four output queues to the tiles on C&C 3's free router link (4.21.6). C&C 3's firmware loads its record once per segment, feeds it advice and controller-fed expansions on its credits, answers its service requests, and pulses each of the segment's 64 runs at done (4.21.3); C&C 3 holds the controller-fed templates and broadcasts the segment's used set to the tiles when proving starts (4.19.9).
- **C&C 2 and C&C 3 stay out of READY/GO** (owner, 2026-10-29), which would otherwise tie them to the phase cycle while they trace and run the file system. Their first command starts them (6.2); after that they serve requests (6.7).
- **Pacing.** C&C 1 sends bulk data between all-to-all passes, or paces it (4.16). **C&C 1 waits `go_latency` + 1 cycles after each `PULSE_GO` store before sending a command.** Otherwise a command can reach a tile before GO does: GO clears its "command landed", READY is lost and the machine hangs.
- **Commands may cross the mesh during a phase:** READY needs a landed command, so the next command is sent right after GO, one broadcast packet per word, which adds at most one packet to any link at a time. It is safe because GO clears "command landed" before the next command can land.
- **Gone from v1.1:** the separate C&C controller design and its memories, C&C's RUN/HALT and GO-pulse registers, the error bit ORed from all tiles (now the error lines, 6.1), and **REPORT**, the debugging command: it worked only when a failed tile still reached `wait_for_command()` and the mesh still carried its reply, the cases that fail. The handlers' reports (4.13) and the error lines replace it.
- **In meshsim,** C&C 1 is by default a behavioural model driving these registers from the harness, and so are C&C 2 and C&C 3; as an option all three are cores (6.2).

### 6.7 The memory system's contract

The memory system serves the units' requests on the request bus (3.3), with LPDDR behind it. This section specifies what a unit sees: the packets that come back, their order, and what a program must keep to. Its internals (its cache, LPDDR scheduling, the tag pool) are out of scope (section 7). **The same tile-side rules hold for the answers of every request receiver** (3.3): a controller or an engine that answers a request does so as the memory system does here. **Much of the memory system's protocol is deferred to the file system's design** (owner, 2026-10-29; item 119); this section states what is settled and marks the rest.

**Where it sits** (owner, 2026-10-28). The memory system is **the Updater/Binder/RW engine, C&C 2's peripheral** (2): the only bulk LPDDR client. Its packets enter the mesh at tile 1's fifth port (4.16), from its own router port (4.17.9) *(Deferred, item 118: the src its packets carry)*; packets for it go to **dst 65**, in its peripheral's region. **Everything it sends is counted** like any other data (4.12), and arrives as mode-0 writes.

**The request's fields.** The hardware commits only a request's requestor [6:0] and target [9:7] (3.3); **everything in [127:10] is laid out by the target** and specified with each receiver. What this section relies on is that a request names, wherever its target puts them (owner, 2026-10-28):
- for a **read,** its LPDDR source and length, and its **destination as a full 32-bit address** (2.2) at the requesting unit (replacing v1.1's destination kind and 128-byte-unit address);
- **the data's counter and the reply's counter** (owner, 2026-10-29): every request that causes data to come back names them, so every request's answers are counted the same way. The exact request layout is settled with the file system (item 119).

**Dispatch.** The root places each request in its target's queue (3.3), and the target's receiver takes them in arrival order.

**How a request is answered** (owner, 2026-10-26). **First any bulk data,** delivered to the destination the request named and **counted against what the requesting unit armed when it sent the request**; **then, only if the request has a value answer, exactly one reply**: a 32- or 64-bit mode-0 write **to the reply address the request named**, counted on the counter the request names for its reply.
- **Every request names its own reply address** (owner and Tony, 2026-10-29), beside its reply's counter: **anywhere in the requester's data memory** (region 0x00; program memory is load-only), naturally aligned to the reply's size. With up to three requests outstanding to C&C 2 (3.3), replies to one fixed place would overwrite each other, so **each outstanding request gets its own buffer**. The reply block (4.12) is the default for tile-to-tile replies, not a rule.
- **A read's answer is its data,** with no reply. Its packets are counted, and counters done says when all have landed.
- **A value answer is a reply:** an fd, an address, an error code. Each is 32 or 64 bits, and its meaning is the target's.
- **Requests outstanding per tile** are limited by target, 1 to C&C 1, 3 to C&C 2 and 1 to C&C 3 (3.3), so the receive queues never fill in normal operation. A request is outstanding, by software convention, until its reply has landed, and a read until its data has landed (3.3).

**Reads.** A read copies its length, starting at its LPDDR source, to the unit that sent it (the requestor, stamped by the tree), starting at the destination address:
- **The data:** **mode-0 packets of 128 bits, unicast to the requesting unit, in ascending address order** *(Proposed)*, each naturally aligned (3.1), the address advancing by 16 per packet, counted on the counter the request names for its data. The destination may be any region a packet reaches (2.2): data or program memory, big SRAM, or a peripheral's region. **The requester arms** that counter with the read's packet count before it sends the request, by writing the count, which adds to whatever is already in flight (4.12), so nothing can arrive unexpected.
- **Landing.** A read has landed when its counter is done (4.14): by K every packet is in place.
- **Destination bounds** are the program's: a read past the end of its memory is undefined (2.2) (meshsim errors), and one into controller memory stays outside the command and reply blocks (invariant 11). **The replayer takes no reads into its queues** (owner, 2026-10-26): its LPDDR overflow is fetched into big SRAM and refilled locally (4.19.13).

**Order between reads.** The memory system may answer reads in any order, cache hits before misses included, and may interleave the packets of reads to the same unit; each read's own packets stay in ascending order. Software that needs one read's data before another's arms and waits for them on separate counters.

**Writes, by tags** (owner, 2026-10-28). v1.1's write tokens and mode-3 write streams to C&C are gone:
1. **The file system (C&C 2) owns a pool of tags.** For each write it picks a tag and sends the RW engine, its own peripheral, a request: the LPDDR destination and the tag.
2. **Each tag owns a window of 2–4 KB** in the RW engine's region; **a tag window is a single write.**
3. **The file system directs the units to write the window,** as plain mode-0 packets to dst 65 in the RW engine's region. No special mode is needed.
4. **When the window is complete,** the RW engine writes it to LPDDR and **returns the tag** to the file system.

- **Fan-in** (owner, 2026-10-28; a constraint on the file system's design): every write into a tag window reaches C&C 2 through one link, and the mesh has no flow control, so several units streaming into open windows at once overflow it. The file system either keeps **one writer in flight at a time** (the tag pool still lets the RW engine write one window out while the next fills) or **paces its writers** to a stated share of one packet per cycle. No hardware changes either way.
- **The source buffer** may be overwritten once the stream run that sends it has finished (`rd_done`): every read of it has then been taken by its area, ahead of any later write (4.1) [derived].
- *(Deferred, item 119: how a unit learns its window and the length the engine counts to, the writer's acknowledgement, how the tag returns (suggested: an event and a register in the engine's window), the read direction, and whether tag-window writes, which are writes from running units, are counted.)*

**Debug output** (owner, 2026-10-28): each unit opens **log files through the file system** and `printf`s into them; when the chip fails, the file system flushes their tails to LPDDR (6.1). The protocol is the file system's *(Deferred, item 119)*.

**The Tracer's fill bus** (v1.3; DECISIONS-v1.3 sections 20 and 21). The Tracer's local misses don't use the request bus: they go to the memory system over **a fill bus of its own**, each request one 128 B line of a file named by **(root, depth, line)**, with a status on every read (ok, hole, not owned, beyond the depth's capacity). 4.21.5 specifies the Tracer's side; the memory system's (the translator's walk, with no TLB; the shared cache; LPDDR) stays a black box (section 7).

**Requests to the controllers** are their software's to define (3.3); a controller reads them through its request port's receive side (4.18) and answers, if at all, by the rules of this section.

**Errors.** A request that names a reserved target, or a target with no receiver, is dropped at the root (meshsim flags it) *(Open, item 126: whether the hardware records it)*. A receiver that cannot serve a request (for the memory system: an operation or layout it doesn't define, or an LPDDR range past the device) drops it; the memory system reports it as one of its peripheral's errors, which raises C&C 2's error line and the toast pin (owner, 2026-10-29; 6.1); a controller's software defines its own. meshsim flags every case. A dropped read never lands, so its unit waits until C&C 1's watchdog fires.

**No per-unit pacing.** The memory system may deliver to a unit at up to one packet per cycle: the unit's eject output serves incoming packets before its loopback (4.7), so its link-side eject FIFO drains as fast as the delivery fills it, and the loopback, which is token-protected, waits (4.5). Other traffic to a receiving unit comes on top and is the program's to pace, as always (3.2).

**What a program must keep to** (invariant 17, the request contract):
- arm the counters for a request's data and its reply before sending it, and name them, with the reply's address, in the request;
- at most 1, 3 and 1 requests outstanding to C&C 1, 2 and 3 (3.3); error reports excepted;
- write a tag window only when the file system directs it, one writer in flight or paced (above);
- **no read outstanding and no tag-window write in flight across a group run**, so the memory system's traffic never meets the all-to-alls' schedule (4.16);
- no read-port results outstanding while deliveries arrive (invariant 14, 4.3);
- controller-memory deliveries inside controller memory and outside the command and reply blocks (invariant 11).

---

## 7. Memory subsystem

*Figures 1.1, 1.2 (at v1.1).*

The memory subsystem is **C&C 2's peripheral, the Updater/Binder/RW engine** (owner, 2026-10-28; section 2), the only bulk LPDDR client: file reads and writes, applying updates, binding. **Units reach it through the request bus** (3.3) and through tag windows in its region (6.7); its contract with them is 6.7. Behind its AXI bus are the LPDDR controller and PHY, which a soft reset leaves running, so LPDDR's contents survive it (6.1). Its internals (the cache, LPDDR scheduling, arbitration between the Tracer's misses, the Updater and the units' traffic, the tag pool) are out of scope, and come with the file system's design (item 119).

**What v1.3 fixes of it** (DECISIONS-v1.3 sections 12, 20 and 21; everything else a black box): memory is **files of 4 KB pages**, each a tree of 1,024-entry index pages whose depth follows its length, addressed in 128 B units, a hole (an entry of 0xFFFFFFFF) reading as zero; **the memory system reads a file directly** by (root, depth, line), translating it through **one translator, which walks the tree through the shared cache** (no TLB; DECISIONS-v1.3 section 21 (b)); a **512 KB cache shared by the whole chip** sits after translation; and **the Tracer's misses are served over the fill bus** (6.7), whose Tracer side, statuses and latencies are 4.21.5 and 4.21.9. The file system on C&C 2, the page manager on C&C 3 and the cache's organization come with their designs (item 119). **meshsim's model of it** (owner, 2026-11-05; B1 Q12, Q13; 4.21.9): the shared cache as 8 ways × 512 sets, LRU, write-back, a hit in 6 cycles and a miss filled from LPDDR in 80; translation the walk, each index read a cache access; the next victim in every set kept free or clean, dirty victims going early through a 4-line write buffer that drains one line per 80 cycles, a read miss waiting until it is empty; a Tracer write-back that misses allocating without an LPDDR read; one request outstanding; cold at each segment's start. It models only the Tracer's traffic, so its hit rates are optimistic. *(For the memory system's design: per segment it reports early evictions, write-backs and read waits; if the waits matter, a 4-entry address compare in the write buffer is the cheap fix.)*

---

## 8. Registers

*Figures 3.2, 3.3, 3.4 (at v1.1).*

### 8.1 Register map and general rules

**Region 0xFF** (2.2) holds everything the controller reaches that isn't memory (owner, 2026-10-28):

| address | contents | section |
|---|---|---|
| 0xFFFF2000–0xFFFF35FF | the table window *(Proposed, item 120)* | 8.9 |
| 0xFFFFF800–0xFFFFF9FF | **the controller's register block**, identical on every unit | below |
| 0xFFFFFA00–0xFFFFFBFF | peripheral 1's window | 8.11, 8.14 |
| 0xFFFFFC00–0xFFFFFDFF | peripheral 2's window | 8.11 |
| 0xFFFFFE00–0xFFFFFFFF | peripheral 3's window | 8.11 |
| the rest of region 0xFF | unmapped: bus error | 4.11 |

**The controller's block** (owner, 2026-10-28 and 2026-10-29). It starts as every peripheral's window does: parameters, status, errors, pulses, events, then the send port and the rest. Register *i* is at 0xFFFFF800 + 4·*i*; the offset below is the address's low 12 bits (owner, 2026-10-29: the addresses of the decisions' 11, 30 and 40).

| offset | register | access | function | section |
|---|---|---|---|---|
| 0x800 | `PARAMS` | R | constant: peripherals present, endpoint id, big-SRAM size | 8.2 |
| 0x804 | `STATUS` | R | live bits, diagnostic | 8.2 |
| 0x808 | `ERRORS` | R | the errors, sticky until the reset ([13:11] live) | 8.3 |
| 0x80C | `PULSE_DONE` | W | the `done` pulse | 8.6 |
| 0x810 | `PULSE_DMA` | W | start the run in `DMA_CONFIG` | 8.6 |
| 0x814 | reserved | | (was `PULSE_GO`, now in C&C 1's peripheral) | 8.14 |
| 0x818 | `EVENTS` | R | pending events; a load consumes the sticky ones under `EVENTS_MASK` | 8.10 |
| 0x81C | `EVENTS_PEEK` | R | the same, consuming nothing | 8.10 |
| 0x820 | `EVENTS_MASK` | R/W | the events that wake `WFI` on their own and that a load of `EVENTS` consumes | 8.10 |
| 0x824 | `EVENTS_REQUIRED` | R/W | the events that wake `WFI` only all together | 8.10 |
| 0x828 | `ROUTER_HDR` | R/W | no-increment, counter, mode and dst of outgoing packets | 8.4 |
| 0x82C | `ROUTER_ADDR` | R/W | the next packet's 32-bit address | 8.4 |
| 0x830 | `ROUTER_SET_W0` | W | payload bits 31–0 of the next packet | 8.4 |
| 0x834 | `ROUTER_SET_W1` | W | payload bits 63–32 | 8.4 |
| 0x838 | `ROUTER_SET_W2` | W | payload bits 95–64 | 8.4 |
| 0x83C | `ROUTER_SEND_32` | W | payload bits 31–0; sends a 32-bit packet | 8.4 |
| 0x840 | `ROUTER_SEND_64` | W | payload bits 63–32; sends a 64-bit packet | 8.4 |
| 0x844 | `ROUTER_SEND_128` | W | payload bits 127–96; sends a 128-bit packet | 8.4 |
| 0x848 | `REQUEST_SET_W0` / `REQUEST_GET_W0` | W / R | store: the request's bits 31–0; load: the oldest received request's bits 31–0 | 8.10 |
| 0x84C | `REQUEST_SET_W1` / `REQUEST_GET_W1` | W / R | bits 63–32 | 8.10 |
| 0x850 | `REQUEST_SET_W2` / `REQUEST_GET_W2` | W / R | bits 95–64 | 8.10 |
| 0x854 | `REQUEST_SEND` / `REQUEST_RECEIVE` | W / R | store: bits 127–96, sends the request; load: bits 127–96, pops the received request | 8.10 |
| 0x858 | `DMA_CONFIG` | R/W | the run and the receive-side settings | 8.5 |
| 0x85C | `DMA_RECORD_BASE_ADDRESS` | R/W | the run's record buffer | 8.7 |
| 0x860 | `WRITE_COUNTER_1` | R/W | write counter 1; a write adds | 8.7 |
| 0x864 | `WRITE_COUNTER_2` | R/W | write counter 2 | 8.7 |
| 0x868 | `WRITE_COUNTER_3` | R/W | write counter 3 | 8.7 |
| 0x86C | `WRITE_COUNTER_4` | R/W | write counter 4 | 8.7 |
| 0x870 | `WRITE_COUNTER_5` | R/W | write counter 5 | 8.7 |
| 0x874 | `WRITE_COUNTER_6` | R/W | write counter 6 | 8.7 |
| 0x878 | `WRITE_COUNTER_7` | R/W | write counter 7 | 8.7 |
| 0x87C | `SRAM_RD_ADDR_QUEUE` | R/W | store: queue a read; load: free entries | 8.8 |
| 0x880 | `SRAM_RD_DEST_ADDR` | R/W | the read port's setup: destination, size and counter | 8.8 |
| 0x884–0x9F4 | reserved | | read 0, stores ignored (meshsim flags the access) | below |
| 0x9F8 | simulator-only exit word | W | 8.15 | 8.15 |
| 0x9FC | simulator-only trace word | W | 8.15 | 8.15 |

**Gone from v1.1's block:** `TILE_PULSE`, `ROUTER_SET_HDR`, `DMA_SRAM_REMAINING`, `DMA_CM_REMAINING`, `DMA_REPLY_REMAINING`, `DMA_PERIPH_REMAINING`, the `WAKE_` names (now `EVENTS_`), `PERIPH_STATUS_0`–`7`, `SENDER_SEND`, `SENDER_ADDR` and the `UART_` registers (now C&C 1's peripheral, 8.13). Every offset moved, so every image, and every header that hard-codes addresses, is rebuilt against `mesh.h` 1.2.

**The common header layout** (owner, 2026-10-28). Every register that carries a packet's header fields lays them out the same way, in bits [31:17], in the link word's order (3.1):

| bits | field |
|---|---|
| [18:17] | size (reserved where something else picks the size) |
| [21:19] | counter |
| [24:22] | mode |
| [31:25] | dst |

It is used by `STREAM_COUNT` (4.2), `ROUTER_HDR` (8.4), a peripheral's `DMA_HDR` (4.17.9) and `SRAM_RD_DEST_ADDR` (8.8, size and counter only); any new header register follows it. Bits [24:17] are the block hardware copies straight into the link word's [167:160]. In `STREAM_COUNT`, [31:25] is the relative *d*, translated into dst rather than copied. Bits [16:0] are each register's own.

**General rules:**
- **The register block and the windows sit in the top 2 KB** (0xFFFFF800–0xFFFFFFFF), each register one `lw`/`sw` with `rs1 = x0` (4.11). The table window needs a base register.
- **Write-only registers read as 0,** not a bus error. The request port's four addresses are the exception: each is a write-only register for stores and a read-only register for loads (8.10), so a load there reads the receive side.
- **Sub-word accesses** (`lb`, `lh`, `lbu`, `lhu`, `sb`, `sh`) to a register, a window or the table window are a bus error; registers are `lw`/`sw` only.
- **A store to a read-only register** is a bus error. **Reserved and undefined words**, in the block or a window, **read 0 and ignore stores** (meshsim flags the access) (owner, 2026-10-28). **Any word of an absent peripheral's window, and the unmapped parts of region 0xFF,** are a bus error.
- **Timing** (4.11): a load from the register block or a peripheral window takes **2 cycles**, and one from the table window or the memories **1** (`reg_load_latency`; DECISIONS-v1.2 section 45), fixed on every access and tile; it returns the state at its sample point; a store is single-cycle and posted, taking effect at the end of its cycle; accesses complete in program order.
- **Reserved and spare bits** are ignored on a store and read 0.
- **Pulse registers** (`PULSE_DONE`, `PULSE_DMA`, a window's `PULSE_RUN` and `PULSE_DMA`, C&C 1's `PULSE_GO`): a store of 1 fires the action for one cycle, a store of 0 does nothing, and a load reads 0 (owner and Tony, 2026-10-28). Separate stores act in program order; firing two actions in one store is no longer possible.
- **Reset values** (6.1): `ERRORS`, the write counters, `EVENTS`' sticky events, `EVENTS_REQUIRED`, the read port's queue and the request FIFO are 0 or empty, `EVENTS_MASK` is [8] | [7] (GO and counters done), and `STATUS` shows the idle machine. **The configuration registers** (`DMA_CONFIG`, `DMA_RECORD_BASE_ADDRESS`, `ROUTER_HDR`, `ROUTER_ADDR`, `ROUTER_SET_W0`–`W2`, `REQUEST_SET_W0`–`W2`, `SRAM_RD_DEST_ADDR`, and each window's `DMA_HDR` and `DMA_ADDR`) **are undefined at the reset**; software writes each before use. The tables' contents are likewise undefined at power-on and kept across a reset.
- Configuration registers are static during a phase.
- **The C header** (owner and integrator, 2026-10-28/29; W3-16, W3-27; Tony may redo the details). The block and each window are declared as C structs overlaid on their addresses: `TileRegs` for the block, a `PeripheralStd` macro listing the ten standard members at the top of each peripheral's struct, and a type per peripheral ending in `Regs` (`ReplayerRegs`, 8.11). Access is through pointers, `#define tile ((volatile TileRegs *)0xFFFFF800)`, used as `tile->EVENTS`, so a whole-block copy, which would consume events and pop requests, needs an explicit `*tile`. Every member is `volatile uint32_t`, accessed one word at a time. `fw/include/mesh.h` (version 1.2) and `fw/REGISTERS.md` are generated from the same register table and must match this section.

### 8.2 `PARAMS` and `STATUS` (0x800, 0x804, read-only)

**`PARAMS`** (constant, strapped; owner, 2026-10-28; v1.1's positions kept where a field survives):

| field | bits | value |
|---|---|---|
| peripherals present: bit *n* − 1 for peripheral *n* (4.17.1) | [2:0] | 0b001 on tiles 0–63 (the replayer) and on C&C 1–3 (their engines) |
| spare (reads 0) | [19:3] | 0 |
| endpoint id (2.1): 0–63 on a mesh tile, 64–66 on C&C 1–3 | [26:20] | the unit's id |
| big-SRAM size in 256 KB units − 1 (0–31: 256 KB to 8 MB, 4.1); bytes = (n + 1) << 18, the first chunk past the end = (n + 1) << 16 | [31:27] | the device's size; 15 at the default 4 MB, 7 on C&C 1–3 (2 MB) |

The peripheral type (v1.1's `TILE_STATUS` [15:12]) is in each peripheral's own `PARAMS` (4.17.4). The size field sits at the top of the word, where a shift alone extracts it.

**`STATUS`** (live, **diagnostic only**: no program needs to poll it, 4.10; renumbered compactly, owner, 2026-10-28):

| field | bits | reset |
|---|---|---|
| `rd_running`: the address generator is running, from the end of the start store's cycle (lead-in included) until the run has finished (4.2) | [0] | 0 |
| `rd_done`: the last run has finished; cleared by the next start; 0 before a unit's first run (4.2) | [1] | 0 |
| GATHER busy (4.4) | [2] | 0 |
| `send_busy`: the send holding register is occupied (4.11) | [3] | 0 |
| `done` (6.3) | [4] | 0 |
| "command landed" (6.3) | [5] | 0 |
| `request_busy`: the request FIFO is full (4.18) | [6] | 0 |
| reserved, read 0 | [31:7] | 0 |

A load returns the state at its sample point (4.11); status bits are registered at their source (section 12). A store to either register is a bus error. **Gone from v1.1's `TILE_STATUS`:** `periph_running` and `sender_busy` (now each peripheral's `STATUS`), `ts_busy` (4.10), `mem_idle` and `cm_idle` (meshsim diagnostics only, 4.8), and the constants (now `PARAMS`). `done` reads 0 after the reset, where v1.1 forced it to 1 in HALT.

### 8.3 `ERRORS` (0x808, read-only)

The errors of 4.13, bit *n* as in its table: [0] router overflow, [1] bad packet, [2] unexpected arrival, [3] drift, [4] write-engine input overflow, [5] table request overflow, [6] GATHER queue overflow, [7] read-port error, [8] done early, [9] bus error, [10] illegal instruction, [11]–[13] peripherals 1–3 (live: the peripheral's `ERRORS` non-zero), [14] receive underflow; [31:15] reserved, reading 0. Every bit keeps its v1.1 number except [11]–[13], which were v1.1's peripheral slot's conditions. Bits [10:0] and [14] are sticky until the reset (6.1); [13:11] follow their peripherals. A store is a bus error. Non-zero raises the unit's hard interrupt and its error line (4.13, 6.1).

### 8.4 The send port: `ROUTER_HDR`, `ROUTER_ADDR`, `ROUTER_SET_W0`–`W2`, `ROUTER_SEND_32`, `_64`, `_128` (0x828–0x844)

| register | access | bits | function |
|---|---|---|---|
| `ROUTER_HDR` | R/W | no-increment [0], spare [16:1], size [18:17] reserved (stores ignored, loads 0), counter [21:19], mode [24:22], dst [31:25] | the header of outgoing packets, in the common layout (8.1); a configuration register; a load returns it (4.11) |
| `ROUTER_ADDR` | R/W | [31:0] | the next packet's 32-bit address (2.2); advances at each mode-0 send by the size in bytes (4, 8 or 16) unless no-increment is set; a load returns the address the next packet will use (4.11) |
| `ROUTER_SET_W0` | W | [31:0] | the payload's bits 31–0; sticky until the next send, then cleared to 0 |
| `ROUTER_SET_W1` | W | [31:0] | the payload's bits 63–32; sticky until the next send, then cleared to 0 |
| `ROUTER_SET_W2` | W | [31:0] | the payload's bits 95–64; sticky until the next send, then cleared to 0 |
| `ROUTER_SEND_32` | W | [31:0] | the payload's bits 31–0; sends a 32-bit packet (size 00) |
| `ROUTER_SEND_64` | W | [31:0] | the payload's bits 63–32; sends a 64-bit packet (size 01) with `ROUTER_SET_W0` as bits 31–0 |
| `ROUTER_SEND_128` | W | [31:0] | the payload's bits 127–96; sends a 128-bit packet (size 10) with `ROUTER_SET_W0`–`W2` as bits 95–0 |

A send store puts the packet in the send holding register, stalling if it is occupied and not being injected that cycle (4.11). Payload words beyond the size are sent as 0, `W` registers the size doesn't cover are ignored, and all three `W` registers clear at every send.

### 8.5 `DMA_CONFIG` (0x858, read/write)

One 32-bit register configures the tile for an engine run, including the run itself. It is now readable (owner, 2026-10-28): a load returns what was last stored.
- It is **written once per engine run**, before the start, and not updated while the run is running or the phase's arrivals are still landing: the write engine and the GATHER engine read the digits and *k* as packets arrive. A one-command local phase rewrites it between passes.
- It **contains no action bits;** those are the pulse registers (8.6).

| field | bits |
|---|---|
| run kind: 0 stream run, 1 group run (4.2) | [0] |
| run start: first stream (0–255) or first `GROUP` entry (0–63) | [8:1] |
| run count: streams (1–256) or groups (1–64). A count of 0 is a run with no entries: it ends at once (`rd_done`), with no requests. Stream or group numbers past the table's end are UB (meshsim) (owner, 2026-11-01, under the principle at the top); v1.2 promised they wrap within the table | [17:9] |
| bucket digit (mode 2): 0 none, 1–4 = key byte, 1 = least significant (4.15); 5–7 reserved: UB (meshsim) (owner, 2026-11-01, under the principle at the top); v1.2 treated them as 0 | [20:18] |
| histogram digit (count-on-arrival): 0 off, 1–4 (4.15); 5–7 reserved: UB (meshsim); v1.2 treated them as 0 | [23:21] |
| gather value size *k* − 1, in records, *k* = 1–8; it also chooses the copy: *k* odd, *k* 64-bit reads and packets per value; *k* even, *k*/2 128-bit ones (4.4) | [26:24] |
| spare | [31:27] |

### 8.6 The pulse registers: `PULSE_DONE`, `PULSE_DMA` (0x80C, 0x810, write-only; read 0)

One register per action (owner and Tony, 2026-10-28), replacing v1.1's `TILE_PULSE`: a store of 1 fires the action for one cycle, a store of 0 does nothing.

| register | v1.1 | action |
|---|---|---|
| `PULSE_DONE` | `TILE_PULSE` [0] | the `done` pulse (6.3); READY then fires automatically when a command has landed |
| `PULSE_DMA` | `TILE_PULSE` [1] | start: load the run in `DMA_CONFIG` and begin it (4.2), clearing DMA run done (`EVENTS` [10]); a start while a run is running is undefined behaviour (invariant 26; meshsim stops) |
| a window's `PULSE_RUN` | `TILE_PULSE` [2] | moved into each peripheral's window (4.17.2) |
| C&C 1's `PULSE_GO` | `TILE_PULSE` [3] | moved into C&C 1's peripheral (8.14) |

Every start is **start only**: no engine has a resume, and a start while its engine runs is UB (meshsim; v1.2 ignored it).

### 8.7 The write counters and `DMA_RECORD_BASE_ADDRESS` (0x85C–0x878)

| register | access | bits | function |
|---|---|---|---|
| `DMA_RECORD_BASE_ADDRESS` | R/W | chunk address [20:0] | the run's record buffer: augmenting's landing buffer, aligned to a record, ordinal = (landing chunk − base) / 2 (4.9); or a gather's value buffer, **128-bit aligned**, value address = base + 2*k*·ordinal (4.4). One base per run: no run uses both. A configuration register |
| `WRITE_COUNTER_1`–`WRITE_COUNTER_7` (0x860–0x878) | R/W | count [19:0] | the write counters (4.12): decremented K cycles after the director accepts a packet whose counter field names them |

- **A counter write** of *N* **adds** *N* to the count, atomically and in step with that cycle's decrement; *N* > 0 arms the counter (clears counters done, `EVENTS` [7], and the counter's own done event, bit *n* − 1 of [6:0], and sets their qualifiers); 0 does nothing; a sum past 2²⁰ − 1 wraps (meshsim flags it) (4.12). **A load** returns the count. The count returns to 0 only by counting down or at the reset. Bits [31:20] are spare.
- **`DMA_RECORD_BASE_ADDRESS`** holds for the run, like `DMA_CONFIG`. Bits [31:21] are spare.

### 8.8 The read port: `SRAM_RD_ADDR_QUEUE`, `SRAM_RD_DEST_ADDR` (0x87C, 0x880)

| register | access | bits | function |
|---|---|---|---|
| `SRAM_RD_ADDR_QUEUE` | R/W | store: chunk address [20:0]; load: free entries [31:0] | a store queues a read of 32, 64 or 128 bits at that chunk address, aligned to the size (a misaligned read is UB, 4.1); a load returns the free entries in the request queue, at least 64 when it is empty (4.3) |
| `SRAM_RD_DEST_ADDR` | R/W | destination [16:0], size [18:17], counter [21:19], reserved [31:22] | the read port's setup register: the byte offset in data memory of the next result, advancing by the size in bytes per queued read; the size, 00 = 32 bits, 01 = 64, 10 = 128, 11 reserved; the write counter the results decrement (4.3). Each result lands as one mode-0 packet to the tile itself. A load returns the next result's destination, as advanced, with the size and counter. A configuration register |

### 8.9 The table window (0xFFFF2000–0xFFFF35FF)

*(Proposed, item 120: the window's place in region 0xFF, keeping v1.1's low 16 bits.)*

| table | window | entries | load | store |
|---|---|---|---|---|
| `HIST` | 0xFFFF2000–0xFFFF23FF | 256 words, `HIST[i]` at +4·*i*; count [16:0] | the count | the count |
| `PTR` | 0xFFFF2400–0xFFFF27FF | 256 words, `PTR[i]` at +4·*i*; chunk address [20:0] | bus error | the pointer |
| `STREAM_SRC` | 0xFFFF2800–0xFFFF2BFF | 256 words; source chunk [20:0], gap after [23:21] (4.2) | bus error | the word |
| `STREAM_DEST` | 0xFFFF2C00–0xFFFF2FFF | 256 words; destination address [31:0], mode 0 only (4.2) | bus error | the word |
| `STREAM_COUNT` | 0xFFFF3000–0xFFFF33FF | 256 words; count [16:0], read size [18:17], counter [21:19], mode [24:22], *d* [31:25] (4.2) | bus error | the word |
| `GROUP` | 0xFFFF3400–0xFFFF35FF | 64 entries, `GROUP[g]` at +8·*g* (low word, then high word; layout in 4.2) | bus error | one word |

**Access rules:** only `HIST` can be loaded; a load from `PTR`, a `STREAM` array or `GROUP` (which the controller only writes) is a bus error, as is any sub-word access in the window. The rest of region 0xFF outside the block and the windows is unmapped (8.1). When the controller may touch the tables: 4.10.

### 8.10 The events and the request port: `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK`, `EVENTS_REQUIRED` (0x818–0x824); `REQUEST_SET_W0`–`W2` / `REQUEST_GET_W0`–`W2`, `REQUEST_SEND` / `REQUEST_RECEIVE` (0x848–0x854)

| register | access | bits | function |
|---|---|---|---|
| `EVENTS` | R | [6:0] write counter 1–7 done; [7] counters done; [8] GO; [9] command landed; [10] DMA run done; [11] request FIFO no longer full; [12] request pending (live); [15:13] peripherals 1–3 (live); [31:16] reserved, read 0 | a load returns every pending event and clears the sticky ones that are 1 in `EVENTS_MASK`; live events are unaffected; an event set after the load's sample point survives to the next load (4.12). A store is a bus error. Sticky events 0 at the reset; live events follow their sources |
| `EVENTS_PEEK` | R | as `EVENTS` | returns exactly what a load of `EVENTS` would, and consumes nothing: an alias of the same state with no storage of its own. A store is a bus error |
| `EVENTS_MASK` | R/W | [15:0], laid out as `EVENTS`; [31:16] reserved | the events that wake `WFI` on their own and that a load of `EVENTS` consumes. Never written by hardware. [8] \| [7] (GO and counters done) at the reset |
| `EVENTS_REQUIRED` | R/W | [15:0], laid out as `EVENTS`; [31:16] reserved | the events that wake `WFI` only when every one of them is pending. Plays no part in consuming. 0 at the reset. The controller's only: windows have none |
| `REQUEST_SET_W0` (0x848) | W | [31:0] | the request's bits 31–0: requestor [6:0] (write 0; the tree overwrites it), target [9:7], and the target's bits [31:10] (3.3); sticky until the next send, then cleared to 0 |
| `REQUEST_SET_W1` (0x84C) | W | [31:0] | the request's bits 63–32; sticky until the next send, then cleared to 0 |
| `REQUEST_SET_W2` (0x850) | W | [31:0] | the request's bits 95–64; sticky until the next send, then cleared to 0 |
| `REQUEST_SEND` (0x854) | W | [31:0] | the request's bits 127–96; pushes the 128-bit request with `REQUEST_SET_W0`–`W2` as bits 95–0 |
| `REQUEST_GET_W0` (0x848) | R | [31:0] | the oldest received request's bits 31–0, with its requestor [6:0] and target [9:7]; doesn't pop; 0 when the receive queue is empty |
| `REQUEST_GET_W1` (0x84C) | R | [31:0] | its bits 63–32; doesn't pop; 0 when empty |
| `REQUEST_GET_W2` (0x850) | R | [31:0] | its bits 95–64; doesn't pop; 0 when empty |
| `REQUEST_RECEIVE` (0x854) | R | [31:0] | its bits 127–96, and **pops** the request. With the receive queue empty it returns 0, pops nothing and sets receive underflow (`ERRORS` [14]) |

**The wake term** (4.12): `wake = gate ? EVENTS[8] : ((EVENTS & EVENTS_MASK & ~EVENTS_REQUIRED) ≠ 0) OR (EVENTS_REQUIRED ≠ 0 AND (EVENTS & EVENTS_REQUIRED) == EVENTS_REQUIRED)`, where `gate` is the `done` gate, 1 from the `done` pulse until GO. It drives the core's wakeup input (4.11), and a wake consumes nothing.

**The request port** (4.18). Send and receive share the four addresses: a store sends, a load receives. A `REQUEST_SEND` store pushes the request into the request FIFO, stalling only if the FIFO is full and no entry is freed in that cycle; `request_busy` is `STATUS` [6], and its fall posts `EVENTS` [11]. All three `SET` registers clear at every send. A received request is read in ascending order, `REQUEST_GET_W0`–`W2` then `REQUEST_RECEIVE`, which pops it; request pending ([12]) is up while one is waiting. The queue itself is in the request tree (3.3). On tiles 0–63 the receive side is unconnected: the `GET` registers read 0, [12] never rises, and a `REQUEST_RECEIVE` load is a receive underflow. The hardware's only fields are requestor [6:0] and target [9:7]; the rest of a request is its target's (3.3).

### 8.11 The peripheral windows: the standard words

**The standard words** of every peripheral's window (4.17.4; owner, 2026-10-28), at the window's base + 4·*n*:

| word | offset in peripheral 1's window | register | access | bits |
|---|---|---|---|---|
| 0 | 0xA00 | `PARAMS` | R | [7:0] type, [15:8] version, the rest peripheral-defined |
| 1 | 0xA04 | `STATUS` | R | [0] run in progress, [1] DMA in progress, the rest peripheral-defined |
| 2 | 0xA08 | `ERRORS` | R | peripheral-defined, sticky until the reset (4.17.6) |
| 3 | 0xA0C | `PULSE_RUN` | W | a store of 1 starts a run (4.17.2) |
| 4 | 0xA10 | `PULSE_DMA` | W | a store of 1 starts the DMA (4.17.9) |
| 5 | 0xA14 | `EVENTS` | R | [0] run done, [1] DMA done, the rest peripheral-defined; a load consumes one count from each set bit in `EVENTS_MASK` (4.17.3) |
| 6 | 0xA18 | `EVENTS_PEEK` | R | as `EVENTS`, consuming nothing |
| 7 | 0xA1C | `EVENTS_MASK` | R/W | laid out as `EVENTS`; 0 at the reset |
| 8 | 0xA20 | `DMA_HDR` | R/W | count [11:0] (128-bit words, v1.3), output queue [13:12], spare [16:14], size [18:17] reserved, counter [21:19], mode [24:22], dst [31:25] (4.17.9) |
| 9 | 0xA24 | `DMA_ADDR` | R/W | [31:0] the DMA's destination address, advancing per packet (4.17.9) |
| 10–31 | 0xA28–0xA7C | reserved | | for future standard words |
| 32–127 | 0xA80–0xBFC | peripheral-defined | | |

Peripheral 2's window is at 0xC00 and peripheral 3's at 0xE00, with the same word layout. **v1.1's `DMA_PERIPH_REMAINING`** (any write counter now counts a peripheral's writes, 4.17.5) **and `PERIPH_STATUS_0`–`7`** (now each peripheral's words) are gone.

The replayer's window (peripheral 1 on tiles 0–63): moved in v1.35 to `spec/jolt/replayer.md`.


### 8.12 (removed)

*Removed in v1.2: v1.1's sender registers, `SENDER_SEND` and `SENDER_ADDR`. A peripheral's output is its DMA (4.17.9, 8.11). See `spec/history/CHANGES-v1.2.md`.*

### 8.13 The UART: `UART_TX`, `UART_RX`, `UART_STATUS`, `UART_DIVISOR` (C&C 1's peripheral, words 42–45, in the decisions' 28 order: TX, RX, divisor, status)

The UART's registers are peripheral-defined words of C&C 1's peripheral (4.20, 8.14); no other unit has them (owner, 2026-10-29: the word numbers).

| word (offset) | register | access | bits | function |
|---|---|---|---|---|
| 42 (0xAA8) | `UART_TX` | W | [7:0] the byte; [31:8] spare | pushed into the TX FIFO; **the store stalls while the FIFO is full**. Reads 0 |
| 43 (0xAAC) | `UART_RX` | R | [7:0] the oldest received byte; [8] valid; [31:9] 0 | pops the byte only when valid; with the RX FIFO empty it returns valid = 0 and pops nothing. A store is a bus error |
| 44 (0xAB0) | `UART_DIVISOR` | R/W | [15:0] reference-oscillator cycles per bit; [31:16] spare | takes effect at the next idle point of each direction. Reset: round(`f_ref` / 57,600), 57,600 baud |
| 45 (0xAB4) | `UART_STATUS` | R/W1C | [4:0] TX occupancy (0–16); [12:8] RX occupancy; [16] RX overrun; [17] framing error; others spare | reads have no side effects; [16] and [17] are sticky and **cleared by writing 1** to them. Line errors are status, never the peripheral's `ERRORS` (4.20) |

The reset empties both FIFOs, clears [17:16] and loads the reset divisor (4.20).

### 8.14 C&C 1's peripheral: Ingress/Egress and the UART

*(New in v1.2.)* C&C 1's peripheral 1 (2, 6.6) holds every chip-level register (owner, 2026-10-28; integrator's proposal accepted 2026-10-29, to be reviewed with C&C 1's firmware). Its standard words are as 8.11's; **its peripheral-defined words**, at 0xFFFFFA00 + 4·*n* (owner, 2026-10-29: the word numbers and event bits):

| word (offset) | register | access | bits | function | reset |
|---|---|---|---|---|---|
| 32–34 (0xA80–0xA88) | `ERROR_LINES_0`–`2` | R | bit *b* of word *w* = endpoint 32*w* + *b*: word 0 tiles 0–31, word 1 tiles 32–63, word 2 [0] unused (C&C 1), [1] C&C 2, [2] C&C 3, [31:3] reserved | **live:** 1 while that unit's `ERRORS` is non-zero, `error_latency` behind it (6.1) | follows the lines |
| 35–36 (0xA8C–0xA90) | `READY_LINES_0`–`1` | R | word 0 tiles 0–31, word 1 tiles 32–63 | **sticky** READY bits, set by each tile's READY pulse (6.3), cleared by `PULSE_GO` | 0 |
| 37–38 (0xA94–0xA98) | `READY_EXPECT_0`–`1` | R/W | as `READY_LINES` | the tiles C&C 1 is waiting for; "all tiles READY" is (`READY` & `EXPECT`) == `EXPECT` and `EXPECT` ≠ 0 | 0 |
| 39 (0xA9C) | `PULSE_GO` | W | a store of 1 | clears READY bits 0–63 and drives GO in one store (a READY pulse in the same cycle is kept); restarts the watchdog (6.1, 6.3). Reads 0 | — |
| 40 (0xAA0) | `WATCHDOG` | R/W | [31:0] | 0 is off; writing *N* > 0 starts it from *N*; a write or `PULSE_GO` restarts it from the last *N*; reaching 0 posts the watchdog event and latches the toast pin (6.1). A load returns the current count | 0 (off) |
| 41 (0xAA4) | `LED` | R/W | [0] | the software pin (6.1) | 0 |
| 42–45 (0xAA8–0xAB4) | the UART | | | 8.13 | |
| 46–63 | reserved | | | | |
| 64–127 (0xB00–0xBFC) | host-interface words | | | Ingress/Egress *(Deferred: with its design)* | |

- **`ERRORS`:** [0] **the unit-error bit**, OR(`ERROR_LINES_0`–`2`), **live**; the other bits are Ingress/Egress's own errors, sticky *(Deferred: with its design)*. Any bit set makes C&C 1's `ERRORS` [11] non-zero, so it interrupts C&C 1 and raises the toast pin (6.1).
- **`EVENTS`:** [0] run done and [1] DMA done (standard); **[2] all tiles READY** (live); **[3] watchdog expired** (depth 1); **[4] UART RX not empty** and **[5] UART TX not full** (live); [31:6] the host interface's *(Deferred)*.
- **Outputs:** GO (2, 6.3), the toast pin and `LED` (6.1).

### 8.15 Simulator-only words

*(New in v1.2.)* Two words of the controller block are **meshsim's only**: on hardware they are reserved words, which read 0 and ignore stores (8.1), so a store there is harmless (integrator, 2026-10-29).
- **The trace word, 0xFFFFF9FC:** a store records a trace mark (the split's `TRACE_REG` and the skew harness's sentinel, which v1.1 kept at 0xFFFFFFF0, now inside peripheral 3's window). meshsim exempts it from its reserved-word check.
- **The exit word, beside it, at 0xFFFFF9F8** (owner, 2026-10-29): a store ends the simulated test, replacing v1.1's simulator-only `CMD_EXIT` and `ebreak`, since `ebreak` now traps and meshsim stops loudly on it (4.13).


### 8.16 The Tracer's window (C&C 3's peripheral)

*Moved in v1.35 to `spec/jolt/tracer.md`.*


---

## 9. Invariants (checked by the simulator)

**Every invariant is a meshsim check** (owner, 2026-10-28): v1.35's meshsim checks each in one of three ways (the preamble): undefined behaviour is fatal in every build, the silent traps (23) stop the core, and the rest are counted in every build and reported by name in the debug and suite builds. **The exceptions** are checks neither v1.3's simulator nor v1.35's has, to be added in v1.4 (item 159; `spec/CHANGES-v1.3-to-v1.35.md`): invariant 17's limits on outstanding requests, its tag windows and its rule for reads during a group run, and invariant 18 for modes 1–3 into big SRAM, which are not checked; and a packet of size 11 and two stream cases (4.2), which are counted, not stopped. **The hardware checks none of them as such;** where it has an error bit for the same mistake, the invariant says so (**hardware:** `ERRORS` [*n*]), and every other rule here is software's alone (the preamble's split, 4.13). Numbers survive from v1.1 where the subject does; new invariants start at 19.

1. **No FIFO overflow:** router FIFOs, including tiles 0–2's fifth-port FIFOs (4.7, 4.16; the loopback FIFO cannot overflow by design, 4.5), the write engine's input FIFO (4.9), the `PTR` and `HIST` request FIFOs (4.10) and the GATHER engine's request queue (4.4). **Hardware:** `ERRORS` [0], [4], [5], [6].
2. **At most one access per big-SRAM area per cycle,** and no buffer is both read and written within an engine run (4.1).
3. **The timetable holds:** every rotation starts on its scheduled cycle and never earlier; table read-ahead waits are part of the timetable, identical on every tile (4.2). **Hardware:** drift, `ERRORS` [3], when the SRAM read queue refused a request.
4. **Every expected arrival lands first** (an uncounted command write is ignored, since commands may cross the mesh mid-phase): a unit's write counters are all at 0 before its controller pulses `done` to end a phase (4.12, 6.4). **Hardware:** done early, `ERRORS` [8].
5. **No bad packets:** no packet is addressed to a region it can't reach (0x02, 0x07–0xFE, 0xFF, an absent peripheral), or a 128-bit mode 3 (3.1). **Hardware:** `ERRORS` [1], the packet dropped uncounted. No packet is misaligned to its size (UB, meshsim only; 4.1), and none has size 11 (undefined; meshsim only).
6. **No `HIST` count exceeds 17 bits.**
7. *(Withdrawn in v1.2: there is no RUN/HALT line. See `spec/history/CHANGES-v1.2.md`.)*
8. **Overwritten-unread results are counted, not failed:** reply-block words and read-port results overwritten before the program reads them are a reported statistic, per phase. Exchanges legitimately land words a program ignores. A strict mode (meshsim's `strict_replies` (`tests/mach.h`)) makes them violations for tests that want the guarantee. Separately, no `SRAM_RD_ADDR_QUEUE` store may find the queue full (**hardware:** `ERRORS` [7]).
9. **No unexpected arrival:** no counted packet's decrement finds its counter at 0, so counted data reaches a unit only when it has armed for it (4.12). **Hardware:** `ERRORS` [2], at the decrement; meshsim also reports the stray at its acceptance, naming it.
10. **Gathers are local:** a mode-4 packet is only ever sent to the tile itself.
11. **Controller-memory writes keep to their places** (owner, 2026-10-28): writes into the command block come only from C&C 1–3, uncounted; replies sent to the reply block (the default for tile-to-tile replies, 4.12) are 32 or 64 bits at the sender's own pair (word 2·src), and a reply to an address a request named lands there; any other mode-0 write to regions 0x00 or 0x01, read-port results and memory-system deliveries included, stays inside its memory and outside both blocks (4.11, 4.12).
12. *(Withdrawn, 2026-10-26: replies land in the reply block, so a reply can no longer address the command block.)*
13. *(Withdrawn, 2026-10-26: replies could no longer address a word outside data memory; 11 now covers every write.)*
14. **Read-port results and incoming traffic do not overlap** (4.3): no packet from outside the tile, other than C&C 1's command writes, arrives while the tile has read-port results outstanding (issued and not yet accepted by the traffic director). Checked as actual overlap in time [built: meshsim's `check_read_port_overlap` (`tests/mach.h`), on by default].
15. **No errors:** every unit's `ERRORS` and every peripheral's stay 0 (4.13, 4.17.6). A program error the hardware can see shows up here, on the unit's error line and on the toast pin (6.1).
16. **The peripherals' and the replayer's invariants** (4.17, 4.19):
    - every peripheral's: no `PULSE_RUN` while a run is in progress; no `PULSE_DMA` while its DMA is busy, no transfer naming an output queue it doesn't have, and no change to `DMA_HDR` or `DMA_ADDR` mid-transfer; no credit returned to a full counter; every write into its region landing within `rp_latency` of the director, and at most K (4.17.5, 4.17.8). Its region may be written from any source (4.17.5);
    - the replayer's and the Tracer's: in `spec/jolt/replayer.md` and `spec/jolt/tracer.md`.
17. **The request contract is kept** (3.3, 6.7):
    - a request names its data's and its reply's counters and its reply's address, and its data and reply arrive only when the requesting unit has armed those counters (4.12);
    - a tile has at most 1, 3 and 1 requests outstanding to C&C 1, 2 and 3; error reports are exempt (3.3);
    - tag windows are written only as the file system directs, one writer in flight or paced (6.7; the window protocol's own checks come with its design, item 119);
    - no read is outstanding and no tag-window write is in flight while a group run is running on any tile;
    - no stream has *d* = 67–125 or 127, no stream on a mesh tile has *d* = 126 (meshsim errors loudly; only the controllers broadcast from streams, 4.2), and no packet is sent to 67–125;
    - every request names a target with a receiver, and every request reaching a receiver is one it can serve (3.3, 6.7);
    - no load of `REQUEST_RECEIVE` finds the receive queue empty (4.18). **Hardware:** `ERRORS` [14].
18. **One source per order-placed destination** (4.1, 4.5, 4.9, 4.17.5): a unit never has two clients (the address generator, the GATHER engine and the send port, item 80) sending order-placed packets to the same destination at once: modes 1, 2 and 3 into the same `PTR` entry of the same receiver, or writes into the same order-placed port of a peripheral (a replayer queue). Big SRAM orders reads only within a client, so such a mix would interleave unpredictably. **And a peripheral's order-placed port takes one source at a time** (owner, 2026-10-26): since its region may be written from any source, no two units feed the same queue port at once. meshsim flags overlapping use in time.
19. **Everything stays in bounds** (2.2, 4.1): no packet and no load, store or fetch of a core addresses past the end of its memory, and no auto-advancing run (`STREAM_DEST`, `ROUTER_ADDR`, `DMA_ADDR`, `SRAM_RD_DEST_ADDR`, the engines' chunk addresses, the memory system's deliveries) passes the end. The hardware doesn't check (undefined: aliasing in the controller memories, nothing promised in big SRAM).
20. **Pointers and runs stay aligned** (4.1, 4.2, 4.4, 4.9, 4.17.9): `STREAM_SRC` and `STREAM_DEST` to the read size; a `PTR` to the size of the writes through it, an augmented one to a record; `DMA_RECORD_BASE_ADDRESS` to a record, and in a gather to 128 bits, as is the gather's `PTR[me]` (4.4); `SRAM_RD_DEST_ADDR` and every read address to the size; a DMA's `DMA_ADDR` to its packets, a transfer that continues another starting where a multiple of its own size left it. **Hardware:** nothing: a misaligned access is UB (4.1; DECISIONS-v1.25 section 8b; v1.2: `ERRORS` [1]).
21. **No reserved encodings:** no packet in mode 6–7 (UB), and no count-only packet with counter 0 (it does nothing); no `DMA_CONFIG` digit 5–7; no access to a reserved word of the controller block or a window other than the trace and exit words (8.15), and no 1 in a reserved bit.
22. **Uncounted traffic is mode 0 only:** modes 1–3 are always counted; a running program sends no uncounted writes except commands; boot overlays go only into controller memory, and big-SRAM data is always counted (3.1, 6.2). And **no write lands in data memory's first 16 bytes after boot** (2.2), which catches a C null-pointer store and an all-zero header alike.
23. **No silent traps** (4.13): no `ecall`, `ebreak`, fetch outside program memory and the boot ROM, misaligned jump target, or trap inside a handler. meshsim stops loudly on each; on hardware such a trap parks the unit without raising its error line, and only C&C 1's watchdog notices (6.1).
24. **K covers the write path** (4.8, 4.10): K is set at or above the implementation's slowest write path (meshsim: max(2·L + 4, L + 4, `cm_latency`, `rp_latency`)), and no read issued after a counter's done overtakes a write that counter counted, a table-window access after done against a pending `PTR` or `HIST` update included. meshsim measures the margin across workloads (item S1, 13).
25. **Boot keeps its order** (6.2): a unit's overlays and first command are all broadcast or all unicast, and no overlay write lands after the unit's "command landed" and before its start. Announced staging sends nothing sooner than `stage_delay` after the GO store (a packet that beats its counter breaks invariant 9).
26. **No ignored start:** no `PULSE_DMA` while the address generator is running (4.2).

---

## 10. Parameter summary

*Figure 8.1.*

| parameter | default |
|---|---|
| link word | **256 bits**: payload [127:0], address [159:128], size [161:160], counter [164:162], mode [167:165], src [174:168], dst [181:175], 74 reserved [255:182], stored in full (3.1); 169 in v1.1, 102 in v1.0 |
| `link_latency` | 3 on every link direction; a hop is `link_latency` + 1 = 4 cycles (3.2) |
| start skew between tiles | 0 or +1 cycle (3.2) |
| `STREAM`, `GROUP` | `STREAM`: 256 entries in three 256 × 32-bit arrays (`STREAM_SRC`, `STREAM_DEST`, `STREAM_COUNT`), 88 of 96 bits used, count 17 bits (at most 131,071); `GROUP`: 64 entries of 64 bits (4.2) |
| GATHER request queue | 16 (4.4) |
| GATHER value size *k* | 1–8 records; *k* odd: 64-bit copies, *k* even: 128-bit copies (4.4) |
| digit-all-to-all group gap | 0; 4 for the two-hop layers (4.2) |
| `mem_chunks` | 1,048,576 (4 MB: 8 areas × 128 bits × 32K); any multiple of 65,536 chunks (256 KB) up to 2,097,152 (8 MB), with areas of at most 512 KB (4.1) |
| area request-queue depth | 8 in meshsim; *(Open)* for the hardware (4.1) |
| area tie order | writes first (4.1) |
| big-SRAM array read latency | 2 in meshsim; *(Open)* for the hardware (4.1) |
| big-SRAM read latency | queueing plus the array's read latency (4.1) |
| tokens and return queues | 64 tokens, a shared pool; in-order return queues of per-read metadata for the address generator and the GATHER engine, big SRAM returning each client's reads in its issue order (4.1, 4.5) |
| `queue_depth` (router FIFOs) | 16; the loopback FIFO at least the token count, 64 (required) (4.7) |
| `intra_tile_latency` | 1 per stage |
| `ts_engine_latency` | 1 (1–4 measured) |
| `ag_lookahead` | 2·`ts_engine_latency` + 2 entries in each of the address generator's two read-ahead queues: 4 at the default; exactly enough for groups of ≥ 2 cycles; back-to-back one-visit groups would need 4·`ts_engine_latency` + 3, should one-word exchanges ever become group runs [meas: meshsim] (4.2) |
| address-generator lead-in | first request 4·`ts_engine_latency` + 3 (group run) or 2·`ts_engine_latency` + 2 (stream run) cycles after the start store; 4·`ts_engine_latency` + 2 or 2·`ts_engine_latency` + 1 after the cycle the start takes effect (4.2) |
| pointer stage depth | 2·`ts_engine_latency` + 2 (4 at the default) (4.9) |
| write-engine input FIFO | 4 (4.9) |
| `PTR`, `HIST` request FIFOs | 16 each (4.10) |
| controller read port | request queue of at least 64 entries of 43 bits (address, destination, size, counter); results as mode-0 packets of 32, 64 or 128 bits into data memory (4.3) |
| controller memories | **32 KB data + 32 KB program**; port B one 128-bit write per cycle, built as the implementer likes; program memory fetch-and-load only (4.11) |
| `cm_latency` (director to port B) | 2 [built] |
| `stage_delay` | **64 cycles** (owner, 2026-11-06; was 32 *(Proposed)*): the minimum time from a GO store to the sender's first packet of announced staging, **a software constant** (the sender's firmware waits), not hardware, so it can be retuned; set with `go_latency` at 8 (DECISIONS-v1.3, P1), the arming then ending about 18 cycles after the store, and the value meshsim's harness has always used (6.2) |
| `reg_load_latency` | **2** for the register block and the peripheral windows, **1** for the table window (`HIST`), fixed on every access and unit (T, 4.11) |
| `ready_latency` | **8 cycles** (owner, 2026-11-06, P1; was 1 in simulation and 8 for estimates): the lines cross the die like the GO net; the physical value is *(Open)* |
| `go_latency` | **8 cycles** (owner, 2026-11-06, P1; was 1 in simulation): the GO net crosses the die like the READY lines; the physical value is *(Open)*; C&C 1 waits `go_latency` + 1 cycles after each `PULSE_GO` before sending (6.6) |
| **K** | **8** (meshsim's default); a parameter, **implementation-defined**: at least the implementation's slowest write path. meshsim's model: max(2·`ts_engine_latency` + 4, `ts_engine_latency` + 4, `cm_latency`, `rp_latency`), 6 at the default `ts_engine_latency`; K = 8 covers `ts_engine_latency` ≤ 2 (4.8; W4-08) |
| `error_latency` | the fixed latency of a unit's error line into C&C 1's peripheral: **8 cycles**, as `ready_latency` (owner, 2026-11-06, P1: the same kind of chip-wide net; was 1 in simulation, Proposed); the physical value is *(Open)* (4.13, 6.1) |
| write counters | seven, `WRITE_COUNTER_1`–`7`, 20 bits each; counter field 3 bits, 0 uncounted, 1–7 the counters (3.1, 4.12) |
| boot ROM | in every controller, region 0x02, reset vector 0x02000000; **256 bytes** (64 words; 48 used in meshsim's: the boot path and a two-report dump handler) (6.2, 4.13) |
| keep-alive watchdog | `WATCHDOG` in C&C 1's peripheral, off at reset (6.1) |
| flush wait before a soft reset | about 0.1 ms (about 100,000 cycles at 1 GHz), the outside world's (6.1) |
| load-use penalty (data memory, program memory, boot ROM) | 0 each (the design target; section 12); registers and windows 2 cycles, the table window 1 (`reg_load_latency`) |
| taken-branch penalty, multiply latency | 1, 3 |
| request bus | a 16-bit, 8-ary tree: 8 row nodes and a root (with an input per sending controller), a 16-byte buffer per node input, one 16-bit beat per cycle per hop, 8 beats per request; the receive queues in the tree, at the root: 128, 256 and 128 × 128 bits for C&C 1, 2 and 3, one credit counter per target set to its depth; at most 1, 3 and 1 requests outstanding per tile; `reqbus_latency` ≈ 15–20 cycles [estimate], 17 unloaded [meas: meshsim] (3.3) |
| request FIFO | 2 KB per unit, 128 × 128 bits (4.18) |
| events | 16 of 32 bits used: [6:0] write counter 1–7 done, [7] counters done, [8] GO, [9] command landed, [10] DMA run done, [11] request FIFO no longer full, [12] request pending, [15:13] peripherals 1–3; [31:16] reserved (4.12) |
| UART | C&C 1's peripheral only; 8N1; 16-byte TX and RX FIFOs *(Proposed)*; divisor in reference-oscillator cycles per bit, reset round(`f_ref` / 57,600); `f_ref` *(param, Open)* (4.20) |
| command and reply blocks | command block 16 words at the top of data memory, 0x00007FC0 (slot 0 the command word, slots 1–15 arguments; argument blocks 1–7 and 8–15 by parity); reply block 256 words below it, 2 per source, 0x00007BC0 (4.12, 6.3) |
| addresses | 32 bits: region [31:24], byte offset [23:0]; big-SRAM engine pointers 21-bit chunk addresses (2.2) |
| requests | 128 bits; the hardware's fields requestor [6:0] and target [9:7], 8 targets; the rest laid out by each target (3.3); a request with an answer names its destination as a 32-bit address and its data and reply counters (6.7) |
| eject priority | the loopback FIFO is served only in a cycle when every other eject FIFO is empty; the others round-robin (4.7) |
| peripherals (4.17) | up to three per unit, regions 0x04–0x06, 0x200-byte windows at 0xFFFFFA00, 0xFFFFFC00, 0xFFFFFE00; ten standard words; events counted (depth 1 or 2) or live; `rp_latency` 2 for every peripheral *(Proposed)*, at most K; the DMA: up to 4 output queues, one transfer at a time, count up to 4,095 **128-bit** words, every packet 128 bits (v1.3) |
| replayer (4.19), Tracer (4.21) | in `spec/jolt/replayer.md` and `spec/jolt/tracer.md` |

---

## 11. Logic budget (per tile, 12 nm estimates)

The SRAM is ~75 % of the die; the remaining ~25 % is about 1.5 mm² per tile for everything else. Every unit is charged here, and additions must fit. Estimates are rough (±50 %) and exclude the big SRAM areas themselves. The lines are rough estimates [estimate], not yet reviewed; v1.2 scales v1.1's lines for the 256-bit link word (owner and Tony, 2026-10-28) and the 32 KB + 32 KB controller memories, and drops the sender and the UART from the tiles.

| unit | state | estimate |
|---|---|---|
| link FIFOs (4 outputs × 2–4 FIFOs + eject × 5 = 17 FIFOs of 16 × 256 bits, ~69.6 Kbit of flops, the reserved bits stored too), route blocks, selectors, traffic director with its 32-bit address decode and the K-stage delay line; padding stages on short links (256 flops each) | | ≈ 0.11 mm² (0.08 at v1.1's 169 bits; 0.05 at v1.0's 102) |
| big-SRAM area queues: 8 request queues (depth open; ~165 bits per entry with 128-bit write data, ~100 in v1.0), per-chunk write enables, routing from the clients to the areas and back; returning each client's reads in its issue order (4.1) takes over v1.0's token FIFO's 64 × 128-bit data slots (~8 Kbit), so the cost moves rather than grows | depends on depth | ~0.015–0.03 mm² |
| address generator: read-ahead queues (`ag_lookahead` × ~100 bits: a `STREAM` and a `GROUP` entry), a rotation or element counter (18 bits) and its comparator, the element-address shift-add (21 bits), its return queue (up to 64 entries of metadata), relative-destination adder, run state | | ~0.011 mm² |
| controller read port: 64-entry request queue (43 bits each: address, destination, size, counter), the setup register, read-queue client | ~2.8 Kbit | ~0.005 mm² |
| GATHER engine: 16-entry request queue (× 64 bits), address multiply-add (*k* ≤ 8: a shift-add), per-value counter | ~1 Kbit | ~0.004 mm² |
| DMA write engine: input FIFO, pointer stage (4 × ~150 bits: a 128-bit payload, its size and the pointer index), augment and count logic | | ~0.007 mm² |
| table memories: `STREAM` (three 256 × 32 macros, 3 KB) and `GROUP` (512 B) macros; `PTR` (256 × 21) and `HIST` (256 × 17) macros with request FIFOs, RMW pipelines and bypass | ~33 Kbit | ~0.017 mm² |
| controller: RV32I ~20k gates + Zmmul ~5–10k gates + `WFI`, the CSRs and trap logic; **32 KB + 32 KB** (eight 512 × 32 macros in v1.1), built as the implementer likes for one 128-bit port-B write per cycle (one-cycle access at 1 GHz confirmed by Tony, item 134, closed by DECISIONS-v1.2 section 45); the boot ROM; the register block (`PARAMS`, `STATUS`, `ERRORS`, the pulses, the four event registers, `ROUTER_HDR`/`ROUTER_ADDR` and the send words, the request port's four store and four load addresses, `DMA_CONFIG`, the record base, the seven write counters with their add ports and done events, the read port's two) | | ~0.15 mm² (~0.07 in v1.1 with 8 KB + 8 KB) |
| loopback FIFO deepened from 16 to 64 entries (required) (4.7): 48 more 256-bit entries, as flops or a small two-port macro | ~12 Kbit | ~0.015 mm² |
| request port (4.18): the 2 KB request FIFO (a 128 × 128 macro), the `SET` registers, the 16-bit serializer, and the receive registers' read path (the queues are in the tree since 2026-10-29) | ~16 Kbit | ~0.008 mm² |
| the UART (4.20): in C&C 1's peripheral only, no longer in every tile | | — |
| the peripherals' interface: the three windows' decode and the controller's live event and error bits; a DMA only where a peripheral sends (C&C 1–3) | | ~0.003 mm² |
| `done`, "command landed", READY, the `done` gate and the counters' qualifiers, the error line, flags (RUN/HALT gone) | | 0.005 mm² |
| replayer (4.19.14), peripheral 1 on tiles 0–63: ALU and comparator, 128 × 64-bit true dual-port register file, two adders, operand resolver, decode index, four queue readers and credit counters, three decompressors and their size counters; with its 32 KB of memories (queues, ROM, decode table, register file, output queue; v1.26, 41 KB at v1.25) | ~256 Kbit of SRAM | ~0.09–0.14 mm² |
| **total per tile** | | **≈ 0.52–0.61 mm²** with the replayer and the fifth port, about 35–40 % of the per-tile logic budget [estimate]: v1.1's ≈ 0.38–0.45, plus ≈ 0.065 for the 256-bit word (the FIFOs, the fifth port's and the loopback's extra entries; owner and Tony, 2026-10-28, revised with WORKER2's review) and ≈ 0.08–0.09 for the larger controller memories, less the sender and the UART. Still inside 1.5 mm² |
| fifth router port (4.16), in every tile: a sixth route block, ten 16 × 256-bit FIFOs + wider selectors; connected only at tiles 0–2 | ~41 Kbit of flops | 0.04–0.07 mm² |
| C&C 1–3 (6.6) | tile instances with 2 MB of big SRAM, each a tile's logic plus its engine; the engines are not estimated here | — |
| once per die: the request bus's nine nodes (a 16-byte buffer per input, ~10 Kbit with the controllers' root inputs), their arbiters and the root's credit counters; **the three receive queues at the root, 128, 256 and 128 × 128 bits (64 Kbit)** (3.3); plus the tree's wiring (18 wires per hop: 16 data, valid, ready) | ~74 Kbit | ~0.03 mm² plus wiring |

Rule of thumb: tables go in SRAM (cheap); comparators, muxes and per-entry flop state are what spend the budget. The tile is dominated by the big SRAM: at 0.75–0.9 MB/mm², 4 MB ≈ 4.5–5.3 mm². With the logic and link channels, a tile is roughly 6–7 mm² at 4 MB. The areas' density at one access per cycle awaits memory-compiler numbers (section 13).

---

## 12. Physical design: the control island

*Figures 1.3, 3.1.*

All the tile's small memories and its registers cluster around the controller. The engines and the router sit between this **control island** and the big SRAM and the links.

- **In the island:**
  - the controller;
  - its memories (32 KB program, 32 KB data, with the command and reply blocks) and the boot ROM;
  - the table memories (`HIST`, `PTR`, the three `STREAM` arrays, `GROUP`);
  - the register block.

  Rough 12 nm sizes: ~0.017 mm² for the table memories, ~0.14 mm² for the 64 KB of controller memory [estimate], ~0.01 mm² of controller logic. The island is **~0.17–0.2 mm², a patch ~0.4–0.45 mm across** [estimate; ~0.1 in v1.1 with 8 KB + 8 KB]. Wires inside it cost a few tens of ps; Tony has confirmed the controller's one-cycle loads at 1 GHz with the larger memories (item 134, closed by DECISIONS-v1.2 section 45).
- **The table memories go by the controller, not the engines.** The engines' table traffic (pointer and counter read-modify-writes with bypass, the `GROUP`/`STREAM` read-ahead) is pipelined and tolerates latency; the controller's loads do not. The engines pay the distance (`ts_engine_latency`, 4.10).
- **Configuration registers** live in the island although the engines use them. They are static during a phase, so the long wires are multi-cycle paths (or the engines keep local copies).
- **Status bits** are registered at their source in each unit and cross to the island with one flop, so status reads one cycle old.
- **Not in the island:**
  - everything the engines touch every cycle other than the table memories: the address generator's read-ahead queues and return queue, the pointer stage, the area queues, the router queues;
  - the GATHER engine and the read port's request queue, which sit by the big SRAM.
- **The request port** sits at the island's edge: its registers in the island, its FIFO beside them, and its 16-bit link leaving for its row node, which sits in the channels of its row of tiles; the root node and its credit counters sit by C&C 1. The receive queues sit at the root, with a read path to each receiving controller's request port, placed with the controllers (item 82). The tree's placement and `reqbus_latency` are *(Open)* (section 13).
- **The events** are registers in the island; the units' event pulses cross like status bits, with one flop.
- **Paths that cross the gap:** engine table accesses, and the traffic director's port-B writes (mode 0 to regions 0x00 and 0x01, `cm_latency`); a peripheral (the replayer) sits beside the traffic director, outside the island, with its memories as SRAM macros, and the write path to it is pipelined over `rp_latency` *(Proposed, item 48; the replayer's placement against the materializer is open with item 59)*; its window's loads and its live event and error bits cross to the island like status bits. A sending peripheral's DMA sits by its output queues and its router port (4.17.9). All are pipelined and tolerate latency, so the island costs no throughput.
- **Big SRAM:** at the default geometry (4.1), eight 512 KB areas (128 bits × 32K each) around the logic, with the link channels between them. Their arrangement awaits the compiler numbers.

---

## 13. Open items

What is open, proposed or deferred in v1.35's design, each with the section it affects; possible later additions are marked *later*. The replayer's and the Tracer's items are in `spec/jolt/replayer.md` and `spec/jolt/tracer.md`. Numbers are never reused. Every item's history (its text as first written and each version's change of status) is `spec/history/machine-spec-v1.3-section-13.md`; the decisions are in `spec/history/`. The logic budget (section 11) is unreviewed; v1.2 rescaled it for the 256-bit link word and the 32 KB controller memories (item 127).

| item | what is to be settled | section | status |
|---|---|---|---|
| 1 | Memory-compiler numbers for a big-SRAM area macro: latency, density, arrangement | 4.1 | open |
| 2 | The area request-queue depth in hardware (8 in meshsim) | 4.1 | open |
| 3 | Write combining: whether narrow writes to one row combine into one access | 4.1, 4.9 | open |
| 4 | The bound on area queueing a phase may cause, and the placement rules that keep it | 4.1, 4.2 | open |
| 5 | `PTR`/`HIST` macros: a write must land intact while port A reads the same address | 4.10 | open |
| 6 | The GATHER request queue's in-flight bound under heavier read-queue load | 4.4 | open |
| 7 | Gapless one-word exchanges: simulate them, then tighten the gap if queues stay low | 4.2 | open |
| 8 | The physical latencies of the three control lines, `ready_latency`, `go_latency` and `error_latency`, settled together | 10 | open (owner, 2026-11-12) |
| 10 | Above the chip: cards, FPGAs, links and each chip's DRAM are not specified | 2 | open |
| 13 | The sort's output format (keys carry the receive-buffer ordinal) | 14 | open |
| 24 | Re-measurement: labels in this document and in `schedule.md` still say "to be re-measured" | all | open |
| 61 | The request tree's physical design: node placement, `reqbus_latency` | 3.3, 12 | open |
| 107 | 4.25 MB of big SRAM per tile, a tentative working figure | 4.1 | open |
| 116 | Read-port reads overlapping incoming traffic: revisit the rule and invariant 14 | 4.3, 9 | open |
| 117 | Storing the reserved link bits (power) | 3.1 | open |
| 121 | The core's CSRs beyond the trap CSRs, `mcycle` and `mtime`, and their reset values | 4.11, 6.1 | open |
| 124 | The soft reset's keep list | 6.1 | open |
| 126 | Whether hardware records a request dropped at the root | 3.3, 6.7 | open |
| 128 | K's margin, validated across workloads | 4.1, 4.8, 4.10 | open |
| 129 | Whether `STREAM_SRC` stays a chunk address | 4.2 | open |
| 82 | Partly settled; left: the request receivers' attachment, the engines' type numbers; 65 as C&C 2 still proposed | 2, 2.1 | decided, part open |
| 15 | A status bit with READY, for per-owner completion | 8.14 | proposed |
| 27 | Drop the one-read-size-per-group rule | 4.2 | proposed |
| 28 | 128-bit staging from C&C | 6.2 | proposed |
| 63 | Each node wormhole-routes whole requests, round-robin | 3.3 | proposed |
| 64 | The request port's FIFO and its stall rule | 4.18 | proposed |
| 67 | A read's data: 128-bit packets in ascending address order | 6.7 | proposed |
| 120 | The table window's address | 4.10, 8.9 | proposed |
| 48 | Decided: a peripheral's fixed landing latency; still proposed: its value, `rp_latency` = 2 | 4.17.5, 10 | decided, part proposed |
| 84 | Decided: the request; still proposed: beat order, the row node writing the requestor, 16 requests per target queue | 3.3, 4.18 | decided, part proposed |
| 133 | Still proposed: the handler parks in a plain loop | 4.13 | decided, part proposed |
| 83 | The target numbering; target 3 deferred with the file system (119) | 3.3 | proposed, part deferred |
| 60 | The memory system's side, with the file system (119) | 6.7, 7 | deferred |
| 119 | The file system and the RW engine, with their design (the Tracer's side is specified) | 3.3, 6.1, 6.7, 7 | deferred |
| 122 | The handler's report header, with C&C 1's request format | 4.13 | deferred |
| 130 | A 1×4 test chip; mesh size as a parameter | 2, 4.2, 4.6 | deferred |
| 142 | The region-decode check, to the RTL | 3.1, 4.13 | deferred |
| 159 | The simulator checks to add in v1.4: invariant 17's request limits, tag windows and group-run rule; invariant 18 for big SRAM; size 11 and the two stream cases, stopped rather than counted | 9, 3.1, 4.2 | deferred (owner, 2026-11-12) |
| 118 | Decided: sending peripherals' src and port; deferred: the rest of the controllers' routing, the DMA port's flow control | 4.16, 4.17.9 | decided, part deferred |
| 17 | A direct path from the GATHER engine to the write side | 4.4 | later |
| 18 | Fusing the local gather with the values all-to-all | 4.2, 4.4 | later |
| 19 | An arrival-order form of augmenting for mode 4 | 4.9 | later |
| 20 | A search engine for the split | 4.3 | later |
| 21 | A port-B reader for bulk transfers from controller memory | 4.5 | later |
| 22 | Whole-row fetches for 64-bit streams | 4.2 | later |
| 125 | Signed counters | 4.12, 6.2 | later |

**The index:** where each item lives; *closed* means settled, its record in the history file.

1 here; 2 here; 3 here; 4 here; 5 here; 6 here; 7 here; 8 here; 9 closed; 10 here; 11 closed; 12 closed; 13 here; 14 closed; 15 here; 16 closed; 17 here; 18 here; 19 here; 20 here; 21 here; 22 here; 23 closed; 24 here; 25 closed; 26 closed; 27 here; 28 here; 29 closed; 30 closed; 31 closed; 32 closed; 33 closed; 34 closed; 35 `spec/jolt/replayer.md`; 36 `spec/jolt/replayer.md`; 37 closed; 38 `spec/jolt/replayer.md`; 39 `spec/jolt/replayer.md`; 40 closed; 41 closed; 42 `spec/jolt/replayer.md`; 43 closed; 44 closed; 45 closed; 46 closed; 47 closed; 48 here; 49 closed; 50 closed; 51 closed; 52 closed; 53 closed; 54 closed; 55 closed; 56 `spec/jolt/replayer.md`; 57 `spec/jolt/replayer.md`; 58 closed; 59 `spec/jolt/replayer.md`; 60 here; 61 here; 62 closed (owner, 2026-11-12); 63 here; 64 here; 65 closed; 66 closed; 67 here; 68 closed; 69 closed; 70 closed (owner, 2026-11-12); 71 closed; 72 closed; 73 closed; 74 closed; 75 closed; 76 closed; 77 closed; 78 closed; 79 closed; 80 closed; 81 closed; 82 here; 83 here; 84 here; 85 closed; 86 closed; 87 closed; 88 closed; 89 closed; 90 closed; 91 closed; 92 closed; 93 closed; 94 closed; 95 closed; 96 closed; 97 closed; 98 closed; 99 closed; 100 closed; 101 closed; 102 closed; 103 closed (owner, 2026-11-12); 104 closed; 105 closed; 106 closed; 107 here; 108 closed; 109 closed; 110 closed; 111 closed; 112 closed; 113 `spec/jolt/replayer.md`; 114 closed; 115 `spec/jolt/replayer.md`; 116 here; 117 here; 118 here; 119 here; 120 here; 121 here; 122 here; 123 `spec/jolt/replayer.md`; 124 here; 125 here; 126 here; 127 closed; 128 here; 129 here; 130 here; 131 closed; 132 closed; 133 here; 134 closed; 135 closed; 136 closed; 137 `spec/jolt/replayer.md`; 138 closed; 139 `spec/jolt/replayer.md`; 140 `spec/jolt/replayer.md`; 141 closed; 142 here; 143 closed; 144 closed; 145 closed; 146 closed; 147 `spec/jolt/tracer.md`; 148 `spec/jolt/tracer.md`; 149 closed; 150 closed; 151 closed; 152 closed; 153 `spec/jolt/tracer.md`; 154 closed; 155 closed; 156 closed; 157 closed; 158 `spec/jolt/replayer.md`; 159 here.

**Closed since v1.3:** 62, simulating the new paths: confirmed and measured with `tests/p4_drift.cpp` on the full-size all-to-alls (uniform, zipfraw, trace and one99, each at group gaps 0, 4 and 8) and the radix sort: 0 cycles of loopback reads waiting for a token, as the schedule predicts, since a tile's stream to itself has its own slot in the all-to-all and its eject has room for it; loopback reads can wait only against traffic outside a schedule, file delivery, which comes with the file system (item 119) (owner, 2026-11-12); 70, the eject loopback priority: confirmed on the same runs: with the eject output serving its loopback last, no drift, 0 tiles with `ERRORS` [3] and 0 drift events; the deliberate-drift control (tile 7, 158 events) shows the detector works (owner, 2026-11-12); 103, the split run log: a pass that runs without the store log never feeds the write log, so only store rows draw from it; the sizing was settled by item 143's measurement (owner, 2026-11-12).


---

## 14. Context: the sort these units serve

Not part of the hardware specification; it is here because it explains the GATHER engine and augmenting. The program is in `fw/sort/program/` (the split in `fw/sort/split/`, skew handling in `fw/sort/skew/`), and its measured times are in `v1.3/results/`. For a 32-bit key and a value of *k* records (*k* = 2 for 128-bit values), the sort has six steps:

1. **Local sort** of (key, index) words, with the index attached by augmenting (mode 3).
2. **Local gather** of the values into key order: a mode-4 stream to the tile itself (4.4).
3. **Split:** a bisection on key values. Owners exchange probe keys (replies) and counts ((lb, ub) pairs written into the receiver's data memory) from the send port, and every tile counts by binary searches of its own run through the controller read port (4.3). Exact on all 25 data sets: ≈ 203K cycles on uniform, 246K mean over the 24 non-trivial sets, 367K worst, with a seed bracket and an early exit to idle barriers [meas: RV32 ISS]. Interface and memory: `fw/sort/split/BINDING.md`.
4. **Two all-to-alls,** in modes 3 and 1:
   - **keys,** in mode 3 (augmented), with count-on-arrival on at the receiver;
   - **values,** in mode 1, with count-on-arrival off.

   Each sender's runs land contiguously at `PTR[src]`, set by the receiver from its counts; senders need no destination addresses. Key *i* and value *i* correspond because both arrive in the same sender order.
5. **Local sort at the owner,** starting with its first digit already counted.
6. **Local gather** of the values into final key order: the same operation as step 2.

The values cross the network once, and each value is read twice locally, by the two gathers. For the 32-bit-value format, key and value fit in one 64-bit record, the value travelling in the record's high word: so steps 2 and 6 disappear, nothing is augmented (the first local sort's input pass and the keys all-to-all use mode 1, not mode 3), and there is no values all-to-all.

**Skew handling** (`fw/sort/skew/`). A three-stage switch picks the transfer path for step 4: direct, loopback-as-local-copy (LB), the one-layer two-hop (TH), or the balanced two-hop (BT), with every tile making the same choice from the group maxima. K + V including planning: 215K uniform, 199K sorted and all-equal, 420K reverse, 772–786K Zipf and trace, 741K at 99 % one value [model; planner cycles measured on an RV32 ISS]. The planner is an overlay (≈ 8.5 KB). C&C 1 issues a fixed maximum number of layer phases, and tiles finish empty layers at once (≈ 30 cycles each). The switch is format-aware: for 32-bit values it costs the keys all-to-all only (trace sets take BT at 332–343K against 0.59–1.00M direct; sorted and all-equal LB at 66.7K; reverse TH at 149.0K) [model]. TH and BT run every stream at the full window and so use a group gap of 4 (4.2).

**Global totals** (the group maxima for the `GROUP` R fields, or any global sum) use a mesh reduction, not a ring scan.
- Each row collects toward its column-0 tile (4 hops from one side, 3 from the other), then column 0 collects toward tile 0 the same way, and tile 0 broadcasts.
- That's about 8 controller turnarounds instead of 63.
- **One-word totals** move as replies: a collector arms its reply counter with 2 and `WFI`s; a tile mid-chain arms it with 1.
- **Vectors** (the 16 group maxima) move as mode-0 packets into the receiver's data memory, counted on a write counter (four 128-bit packets; v1.0 sent eight 64-bit ones).
- The ring scan is kept only for exclusive prefixes.

The plan estimated ≈ 1.37M cycles for the whole sort at *k* = 2 on uniform data [model]. **Measured at v1.25: 1,229,008 cycles** for the full-size sort `p4/uniform-k128` (64 tiles × 65,536 records), against v1.2's 1,360,082: **131,074 fewer, all from the even-*k* gathers** (4.4) [meas: meshsim, WORKER11, Quest 089; v1.2: Quest 077 and 086]. Older times are in `v1.3/results/` (v1.1: `v1.3/RESULTS-full-size.md`). Since v1.1 the values all-to-all of the 128-bit-value format sends one packet per value instead of two.
