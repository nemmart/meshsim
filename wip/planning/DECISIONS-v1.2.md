# Decisions toward v1.2

One entry per decision, newest section last. Labels follow the repository's convention: O owner decision,
T confirmed by Tony (controller core). The spec session works from this log; `spec/machine-spec.md` stays
the authority once an entry is written into it. Starting point: `PLANNING-NOTES-2026-10-27.md`.

---

## 1. Condition handling (2026-10-28)

Closes planning notes section 7, "Condition handling", and to-do item 3.

### 1.1 The controller core (T)

- Tony's RV32 core has **two inputs**: a **hard interrupt**, which takes a trap to the handler, and a
  **wakeup**, which resumes the core from `WFI` without a trap.
- The wakeup input is **level-sensitive**: if it is high when the core reaches `WFI`, the core does not
  sleep, even if it went high before the `WFI`.
- The hard interrupt **takes the trap even while the core is in `WFI`**.
- The core implements the **CSR instructions and the machine-mode trap CSRs**, but **no privilege modes**:
  machine mode only, without the CSRs for privilege levels.
- **Open (T):** the exact list of implemented CSRs, to define which CSR instructions are legal and which
  still raise the illegal-instruction condition.

### 1.2 Spec changes to the controller (O; 4.11)

- **CSR instructions become legal** for the CSRs the core implements (1.1). 4.11 currently makes every CSR
  instruction illegal.
- **Bus errors, illegal instructions, `ecall` and `ebreak` trap to the handler** instead of halting the
  controller. 4.11 currently halts the controller on each.

### 1.3 Wake (O; 4.12)

- **The `EVENTS_*` registers are unchanged.** The existing wake term (4.12's formula over `EVENTS`,
  `EVENTS_MASK`, `EVENTS_REQUIRED` and the `done` gate) drives the core's wakeup input directly.

### 1.4 The handler (O; 4.13)

- **A condition raises the core's hard interrupt.** The handler reads the state (conditions, `TILE_STATUS`,
  counters, `mepc`/`mcause`/`mtval`, `PERIPH_STATUS_0`-`7`, x1-x31: about 50 words) and sends it to C&C 1
  as request-bus reports (about 17; header word: op, sequence number; three data words each).
- **Parking:** the handler then **clears `EVENTS_MASK` and `EVENTS_REQUIRED` and loops on `WFI`**, so no event
  and no GO can wake it. Only HALT recovers the tile (HALT → RUN resets `EVENTS_MASK`, 4.12).
- **Conditions during the `done` gate:** the gate controls only `WFI` wakes. A condition raised between
  `done` and GO still takes the hard interrupt.

### 1.5 Error lines and the C&C pin (O)

- **Every tile has a dedicated error line to C&C 1**, driven by hardware from its conditions (no software
  involved), with a **fixed latency**. It still reports a tile whose request path is stuck or whose handler
  has itself failed.
- C&C 1 sees the lines as **three 32-bit words**: two for the 64 tiles (one bit per tile) and one for the
  C&C controllers and other non-tile units. Their bit assignment is part of the endpoint map (to-do item 11).
- **The three words fold into C&C 1's conditions**, and C&C 1's conditions drive its own hard interrupt, so
  C&C 1 is interrupted whenever any core fails, itself included.
- **The fold is live:** it follows the error lines; it does not latch. C&C 1's own conditions stay sticky,
  as everywhere.
- **The C&C pin:** an output pin on the ASIC, high while C&C 1 has any condition.
- **C&C 1's HALT halts everyone but itself.** Entering HALT clears the halted units' conditions (4.13), so
  their error lines drop, the fold clears and the pin goes low.
- **So the pin stays high only when C&C 1 itself has failed**, and the host steps in. The pin also pulses
  during normal recovery, so the host treats only a sustained high as a failure.
- **C&C 1's phase watchdog stays** as the backstop for a core that is stuck without raising a condition.

### 1.6 REPORT is withdrawn (O; 6.6)

- **The REPORT debugging command is removed.** It works only when a failed tile still reaches
  `wait_for_command()` and the mesh still carries its reply, which are the cases that fail. The handler's
  request-bus reports (1.4) and the error lines (1.5) replace it.

### 1.7 Spec text these decisions change

- **Section 2 (global nets) and 6.6:** the error line is now one line per tile at fixed latency into C&C 1's
  three words (1.5), replacing "ORed in a tree into one sticky error bit at C&C".
- **4.11:** CSR instructions and traps (1.2).
- **4.13** ("bus errors, illegal instructions, `ecall` and `ebreak` halt the tile's controller only"),
  **4.17.7, 6.1 and 4.19.11** ("a halted controller" stops the peripheral and the sender): with traps the
  controller no longer halts itself, so each needs rewording. What stops the peripheral is pending (Q3).
- **6.6:** REPORT removed (1.6). C&C reuses the tile controller design, so C&C 1 gets the same traps and
  can take its own interrupt (1.5).

---

## 2. The link word (2026-10-28)

Decided with Tony (O, T). Replaces 3.1's 169-bit word.

- **The link word is 256 bits on every router link**, fixed, so later field changes don't resize the
  links, FIFOs and route blocks again (102 bits in v1.0, 169 in v1.1).
- **Fields (181 bits):** payload 128, address 32, size 2, counter 2, mode 3, src 7, dst 7. **75 bits
  reserved:** sent as 0 and ignored by every receiver; FIFOs and route blocks carry all 256 bits without
  interpreting them.
- **Bit layout:** payload [127:0], address [159:128], size [161:160], counter [163:162], mode [166:164],
  src [173:167], dst [180:174], reserved [255:181].
- **Size, counter and mode keep this order everywhere** they appear together (`STREAM_COUNT`, the send
  port's header, the sender), so hardware copies them as one 7-bit block into link word [166:160].
- **Cost:** router FIFOs about 50 % larger than at 169 bits, roughly +0.04-0.06 mm² per tile [estimate];
  256 wires per link direction, with the channel width to confirm in physical design.
- **Pending:** the 32-bit address (Q6) and the 3-bit mode's code assignment (Q7).

## 3. Counters (2026-10-28)

Decided with Tony (O, T). Replaces 4.12's four named down-counters.

- **The packet names its counter** in the link word's 2-bit counter field: **0 uncounted; 1, 2 and 3
  general-purpose counters**, assigned to whatever a program needs. Counting no longer depends on the mode.
- **A fixed delay K:** the traffic director accepts a packet, and its counter is decremented **K cycles
  later**, through a K-stage delay line carrying the counter id. K is large enough that the packet's write
  has reached its destination by then. **K = 8 for now** (a parameter).
- At most one packet per cycle leaves the director, so a counter takes at most one decrement per cycle.
  `mem_idle` and `cm_idle` drop out of the done rule: a counter at 0 means everything it counted is in place.
- **Unexpected arrival stays:** a counted packet for a counter already at 0 still sets the condition. A
  packet wrongly sent with counter 0 lands silently, a sender error like a wrong mode today.
- **What K must cover** (S1 below): port B and the peripheral are fixed at 2 cycles. For big SRAM, a write
  only has to have **entered its area's queue** within K: the queue is served in arrival order, so a read
  issued after the counter reaches 0 queues behind the write and cannot overtake it.
- **Follow-on:** the wake events' per-counter done bits ([3:0] today) become done events for counters 1-3;
  [9] counters done stays. HALT flushes the delay line with the counters.
- **HALT no longer suppresses counting** (replacing 3.1 and 4.12's "a halted tile counts nothing"). Boot
  loads and staging into a halted tile are sent with counter 0. A counted packet arriving at a halted tile
  hits a counter at 0 and sets unexpected arrival, which survives into RUN where the loader can see it.
- **Pending:** the counter field in every packet source (Q8).

## 4. The `STREAM` entry (2026-10-28)

Decided (O). Still three 32-bit arrays, 96 bits, 88 used:

| array | fields |
|---|---|
| `STREAM_SRC` | source [20:0], gap after [23:21], spare [31:24] |
| `STREAM_DEST` | destination address [31:0] |
| `STREAM_COUNT` | count [17:0], size [19:18], counter [21:20], mode [24:22], *d* [31:25] |

- `STREAM_COUNT` [24:18] is the link word's size, counter and mode block, [166:160] (2).
- The fields that change every round are still the source and the count, one store each, with fixed bits
  ORed in (gap after; size, counter, mode and *d*).
- Assumes the source stays a 21-bit big-SRAM address (Q9).

## 5. The 32-bit address map (2026-10-28)

Decided (O). Answers Q6. One map for packet addresses and the controller's own loads and stores.

- **An address is an 8-bit region [31:24] and a 24-bit offset [23:0].**

| region | contents |
|---|---|
| 0x00 | controller data memory, 32 KB |
| 0x01 | controller program memory, 32 KB |
| 0x02 | big SRAM |
| 0x03 | peripheral 1 |
| 0x04 | peripheral 2 |
| 0x05 | peripheral 3 |
| 0x06-0xFE | reserved |
| 0xFF | controller only: registers (9) and the table window |

- **The region names where a packet lands**, so the mode no longer has to (Q7).
- **The controller memories become 32 KB + 32 KB** (planning notes to-do 2, spec part N).
- **A tile may have up to three peripherals** (9), where 4.17 has one slot.
- **The controller sees 0x00, 0x01 and 0xFF only.** It still cannot load or store big SRAM or a
  peripheral directly (current plan): every existing mechanism for reading and writing big SRAM carries
  forward (the read port, sending itself packets, address-generator copies).
- **Region 0xFF is controller only:** a packet addressed to it is a bad packet.
- **Follow-on:** the controller's map moves (today program memory is at 0x0000, data at 0x4000, the table
  window at 0x2000 and the registers at 0xFFFFF800), so the reset PC, the resident loader's address, the
  linker script and `mesh.h` change.
- **Pending:** whether the offset counts bytes or 32-bit words (Q10).

## 6. Modes (2026-10-28)

Decided (O). Answers Q7. With the region choosing the destination (5) and the counter field doing the
counting (3), the 3-bit mode keeps only what the receiver must do differently:

| code | mode |
|---|---|
| 0 | **write at address**, any region the packet may reach |
| 1 | big-SRAM write by source, at `PTR[src]` |
| 2 | bucket write, at `PTR[digit]` |
| 3 | augmented write, at `PTR[src]` |
| 4 | gather request (local only) |
| 5-7 | reserved: **discarded at the destination, with no error** |

- **Numbering** (owner, 2026-10-28): the general write is 0; the writes that land through `PTR` and step it
  are adjacent, 1-3; the gather request, the only mode that isn't a write, follows them. There is no
  invalid code: v1.1's mode 0 is gone.
- **Reserved modes are discarded silently** (owner, 2026-10-28): the receiver drops the packet, uncounted,
  and raises no error. meshsim still flags each one.
- **Accepted consequence:** an all-zero header (a `STREAM` entry, `ROUTER_HDR` or `DMA_HDR` left at 0) is a
  valid, uncounted write at address 0x00000000, the first word of data memory, and lands silently. v1.1's
  mode 0 caught that as a bad packet.
- **Merged into mode 0:** v1.1's explicit-address big-SRAM write (3), controller-memory write (6),
  command write (7), reply (8) and peripheral write (9).
- **Replies** are mode-0 writes to the reply block at word 2·src in the receiver's data memory, counted
  as the receiver armed. That a sender writes only its own pair becomes a software convention, no longer
  a hardware guarantee.
- **Commands** are mode-0 writes, **counter 0**, to the command block in region 0x00: arguments first,
  then the command word to slot 0. **A write to command slot 0 sets "command landed"** and posts its wake
  event, decoded from the address by the traffic director. READY, GO and the alternating argument blocks
  work as today. Commands stay uncounted because they arrive while the tile waits in
  `wait_for_command()` with its counters at 0.
- **Follow-on:** v1.1's bad-packet rules for modes 0, 7, 8 and 10-15 go (reserved modes are now discarded
  silently); invariant 11 and "only command writes write
  the command block, only replies the reply block" become software conventions. Whether a 64- or 128-bit
  write that covers slot 0 also sets "command landed" is a detail to settle with the spec text.

## 7. The send port's header (2026-10-28)

Decided (O). The 32-bit `ROUTER_SET_HDR` (dst 7, mode 4, addr 21) can't hold dst, mode, counter and a
32-bit address (44 bits), so it splits in two, both read/write as planning notes to-do 13 describes:

- **`ROUTER_ADDR`:** the full 32-bit address (5). A store sets it; it auto-advances at each send as today;
  a load returns the address the next packet will use.
- **`ROUTER_HDR`:** counter, mode and dst. A store sets them; a load returns them. Layout (owner,
  2026-10-28): [17:0] spare, size [19:18] reserved (the size comes from the send register), counter
  [21:20], mode [24:22], dst [31:25].
- **Size stays with the send register** (`ROUTER_SEND_32`, `_64`, `_128`); `ROUTER_SET_W0`-`W2` are
  unchanged and stay write-only.
- One extra store when both change; a change of dst alone, the common case in replies and one-word
  exchanges, is still one store. The register map is renumbered anyway when it moves into region 0xFF.
- **Toward Q8:** `SENDER_SEND` has room for the counter field (dest 7, mode 3, counter 2, queue 2, count
  16 = 30 bits), and `SENDER_ADDR` becomes a full 32-bit address.

## 8. Pulse registers (2026-10-28)

Decided (O, T). `TILE_PULSE` is replaced by **one register per action**: a store of 1 fires the action
for one cycle, a store of 0 does nothing, and a load reads 0.

| v1.1 | register | action |
|---|---|---|
| `TILE_PULSE` [0] | `PULSE_DONE` | the `done` pulse (6.3) |
| `TILE_PULSE` [1] | `PULSE_DMA` | start the run in `DMA_CONFIG` (4.2) |
| `TILE_PULSE` [2] | ~~`PERIPH_1_RUN_PULSE`, ...~~ | moved: `PULSE_RUN`, word 3 of each peripheral's window (10) |
| `TILE_PULSE` [3] | `PULSE_GO` | drive the GO net where it is wired (6.3) |

- **Names** (owner, 2026-10-28): `PULSE_DONE`, `PULSE_DMA`, `PULSE_GO`, a `PULSE_` family like the
  `WAKE_` and `ROUTER_` families (first proposed as `TILE_DONE_PULSE`, `DMA_START_PULSE`, `TILE_GO_PULSE`).
- Firing two actions in one store is no longer possible; 8.6's "in one store, start and run act before
  `done`" goes. Separate stores act in program order, and the synchronised start path is still one store.

## 9. Peripherals and the register space (2026-10-28)

Decided (O). Answers most of Q11.

- **A tile has at most three peripherals**, in regions 0x03-0x05 (5). Packets reach a peripheral through
  its region.
- **Each peripheral has a 0x200-byte register window** in region 0xFF, which the controller loads and
  stores directly:

| address | owner |
|---|---|
| 0xFFFFF800-0xFFFFF9FF | the controller's own registers |
| 0xFFFFFA00-0xFFFFFBFF | peripheral 1 |
| 0xFFFFFC00-0xFFFFFDFF | peripheral 2 |
| 0xFFFFFE00-0xFFFFFFFF | peripheral 3 |

- **Each peripheral has one wake bit** in `EVENTS`, replacing v1.1's six service lines. What the bit
  means, and how software learns why it fired, is the peripheral's own business, through its window.
- **The UART becomes a peripheral** on the tiles wired to a serial port, with TX and RX as registers in its
  window. It leaves every other tile, along with v1.1's `UART_TX`/`UART_RX`/`UART_STATUS`/`UART_CONTROL`
  and wake events [22:21]. meshsim's per-tile console needs a replacement (Q12).
- **Follow-on:** v1.1's slot interface (4.17) shrinks: `PERIPH_STATUS_0`-`7` and the service lines move
  into each peripheral's window; the per-peripheral run pulses (8) could move there too. The replayer
  (4.19) becomes peripheral 1 on the tiles that have one.

## 10. The standard peripheral window (2026-10-28)

Decided (O). Every peripheral's 0x200-byte register window (9) starts with the same words, in the same
order as the controller's block (11): parameters, status, conditions, pulses, wakes, DMA, then the rest.
Word *n* is at window base + 4*n*.

| word | register | access | purpose |
|---|---|---|---|
| 0 | `PARAMS` | R | constant: the peripheral's type, version, sizes; 0 if it has nothing to say |
| 1 | `STATUS` | R | live bits: run in progress, DMA in progress, and the peripheral's own |
| 2 | `CONDITIONS` | R | error bits, sticky until HALT like `TILE_CONDITIONS` |
| 3 | `PULSE_RUN` | W | store 1 to start a run |
| 4 | `PULSE_DMA` | W | store 1 to start the peripheral's DMA as set up in words 8-9 |
| 5 | `EVENTS` | R | sticky events; a load consumes those in `EVENTS_MASK`, as on the tile |
| 6 | `EVENTS_PEEK` | R | what a load of `EVENTS` would return, consuming nothing |
| 7 | `EVENTS_MASK` | R/W | the events that count |
| 8 | `DMA_HDR` | R/W | count [11:0] (32-bit words, up to 4,095), output queue [13:12], spare [17:14], size [19:18] reserved (the DMA picks sizes), counter [21:20], mode [24:22], dst [31:25] |
| 9 | `DMA_ADDR` | R/W | the DMA's 32-bit destination address, advancing by each packet's size as packets go out |
| 10-31 | | | reserved for future standard words |
| 32-127 | | | peripheral-defined |

- **Conditions interrupt; wake events wake.** `TILE_CONDITIONS` has one bit per peripheral, set while
  that peripheral's `CONDITIONS` is non-zero, so a peripheral error raises the hard interrupt. The tile's
  `EVENTS` has one bit per peripheral, **live** (not sticky): high while the peripheral's
  `EVENTS & EVENTS_MASK` is non-zero. An event is consumed in one place only, by a load of the
  peripheral's `EVENTS`. (Answers Q13.)
- **`EVENTS_REQUIRED` is tile-only:** a program needing "peripheral 1 and counter 2" sets both in the tile's.
- **No clear pulses:** loading `EVENTS` consumes events, as on the tile.
- **Naming** (owner, 2026-10-28): a peripheral's output engine is its **DMA** (v1.1's "sender"), with
  `PULSE_DMA`, `DMA_HDR` and `DMA_ADDR`; its run is `PULSE_RUN`. Pulses are a `PULSE_` family, as on the
  controller (8).
- **`DMA_HDR` and `DMA_ADDR`** (owner, 2026-10-28) parallel the controller's `ROUTER_HDR` and
  `ROUTER_ADDR`. `DMA_HDR` [31:18] is laid out exactly as `STREAM_COUNT` [31:18] (4), so the size,
  counter, mode and dst block sits at the same bits in both. As with v1.1's sender, a DMA transfer is 128-bit packets, then a
  64-bit and/or a 32-bit packet for the remainder. v1.1's word count was 16 bits; 12 is enough.
- **The DMA is optional.** A peripheral without output (the replayer, for one) leaves words 4, 8 and 9
  unused (`PULSE_DMA`, `DMA_HDR`, `DMA_ADDR`). This replaces v1.1's tile-level sender (`SENDER_SEND`, `SENDER_ADDR`, `sender_busy`, sender
  idle at `EVENTS` [20]; 4.17.9, 8.12).
- **The run pulses move into the windows:** section 8's `PERIPH_1_RUN_PULSE`-`PERIPH_3_RUN_PULSE` go.
- Replaces v1.1's `PERIPH_STATUS_0`-`7`, `periph_running`, the six service lines and the peripheral
  conditions [13:11] in the controller's registers (4.17). The peripheral type moves from `TILE_STATUS`
  into each peripheral's `PARAMS`.

## 11. The controller's register block (2026-10-28)

Decided (O): the order (parameters, status, conditions, pulses, wakes, send, the rest) and the three
first words. *(Proposed: the addresses below.)* In region 0xFF; the low 12 bits shown.

| addr | register | access | notes |
|---|---|---|---|
| 0x800 | `TILE_PARAMS` | R | **new**, constant: endpoint id, big-SRAM size, peripheral-present mask [2:0]; split out of v1.1's `TILE_STATUS` |
| 0x804 | `TILE_STATUS` | R | live bits only: `rd_running`, `rd_done`, `send_busy`, `ts_busy`, `mem_idle`, `cm_idle`, `done`, `request_busy` |
| 0x808 | `TILE_CONDITIONS` | R | gains one bit per peripheral (10); loses the peripheral conditions [13:11] |
| 0x80C | `PULSE_DONE` | W | 8 |
| 0x810 | `PULSE_DMA` | W | 8 |
| 0x814 | `PULSE_GO` | W | 8 |
| 0x818 | `EVENTS` | R | new bit layout (Q14) |
| 0x81C | `EVENTS_PEEK` | R | |
| 0x820 | `EVENTS_MASK` | R/W | |
| 0x824 | `EVENTS_REQUIRED` | R/W | tile only |
| 0x828 | `ROUTER_HDR` | R/W | 7 |
| 0x82C | `ROUTER_ADDR` | R/W | 7 |
| 0x830-0x838 | `ROUTER_SET_W0`-`W2` | W | unchanged |
| 0x83C-0x844 | `ROUTER_SEND_32`, `_64`, `_128` | W | unchanged |
| 0x848-0x854 | `REQUEST_GET_W0`-`W2`, `REQUEST_RECEIVE` / `REQUEST_SET_W0`-`W2`, `REQUEST_SEND` | R / W | unchanged |
| 0x858 | `DMA_CONFIG` | R/W | readable (planning to-do 13) |
| 0x85C | `DMA_RECORD_BASE_ADDRESS` | R/W | |
| 0x860-0x868 | `WRITE_COUNTER_1`-`WRITE_COUNTER_3` | R/W | 3 |
| 0x86C | `SRAM_RD_ADDR_QUEUE` | R/W | unchanged |
| 0x870 | `SRAM_RD_DEST_ADDR` | R/W | needs a counter field and the new data-memory address |
| 0x874-0x9FC | | | reserved (`ERROR_LINES_0`-`2` moved to C&C 1's peripheral, 15) |

- **Gone:** `TILE_PULSE`, `ROUTER_SET_HDR`, `DMA_SRAM_`/`DMA_CM_`/`DMA_REPLY_`/`DMA_PERIPH_REMAINING`,
  `PERIPH_STATUS_0`-`7`, `SENDER_SEND`, `SENDER_ADDR`, `UART_TX`/`RX`/`STATUS`/`DIVISOR`.
- **The table window** (`HIST`, `PTR`, `STREAM_*`, `GROUP`; 0x1600 bytes) doesn't fit in the block.
  *(Proposed:)* 0xFFFF2000-0xFFFF35FF, keeping v1.1's low 16 bits.

## 12. One header layout (2026-10-28)

Decided (O). Answers Q15. Every register that carries a packet's header fields lays them out the same way,
in bits [31:18], in the link word's order (2):

| bits | field |
|---|---|
| [19:18] | size (reserved where something else picks the size) |
| [21:20] | counter |
| [24:22] | mode |
| [31:25] | dst |

Used by `STREAM_COUNT` (4), `ROUTER_HDR` (7) and a peripheral's `DMA_HDR` (10); any new header register
follows it. The low bits [17:0] are each register's own (a count, a queue, or spare).

## 13. The read port's destination (2026-10-28)

Decided (O). `SRAM_RD_DEST_ADDR` keeps a minimal header; a full header like the sender's was considered and
judged not useful, since a read-port result always goes to the tile itself as a mode-0 write.

| bits | field |
|---|---|
| [17:0] | the destination offset in **data memory only** (region 0x00), advancing by the result's size per queued read; units per Q10 |
| [19:18] | size: 32, 64 or 128 bits (the common layout, 12) |
| [21:20] | counter |
| [31:22] | reserved: mode is always 0 and dst always the tile itself |

- R/W (planning notes to-do 13): a load returns the next result's offset as advanced.
- **Data memory only** (owner, 2026-10-28): v1.1 let results land anywhere in controller memory, program
  memory included; that goes.

## 14. Names: events and errors (2026-10-28)

Decided (O). **The wake registers are renamed** at both levels, the tile's and each peripheral's:

| v1.1 | v1.2 |
|---|---|
| `WAKE_EVENTS` | `EVENTS` |
| `WAKE_PEEK` | `EVENTS_PEEK` |
| `WAKE_MASK` | `EVENTS_MASK` |
| `WAKE_REQUIRED` (tile only) | `EVENTS_REQUIRED` |

- **The counters are `WRITE_COUNTER_1`-`WRITE_COUNTER_3`** (owner, 2026-10-28): every counted packet is a
  write (modes 0-3), so the name says what they count. The link word's field stays "counter".
- "Interrupts" was considered and rejected: events only release `WFI` and take no trap, while errors raise
  the hard interrupt (1) and the core's `mie`/`mip` CSRs use the word in the RISC-V sense.
- This log uses the new names throughout, including where it cites v1.1's bit positions.

## 15. C&C 1's peripheral: error and run lines (2026-10-28)

Decided (O). Revises 1.5 and settles part of Q4.

- **C&C 1 has one peripheral, Ingress/Egress and the UART.** The error lines and the run lines are
  registers in its window (peripheral-defined words), not in the controller's block: `ERROR_LINES_0`-`2`
  leave 0x874-0x87C (11).
- **Bits follow the endpoint id:** 0-63 the mesh tiles, **64 unused** (C&C 1 itself), 65 C&C 2 (the file
  system), 66 C&C 3 (the pager); the rest of the third word reserved.

| registers | access | per unit |
|---|---|---|
| `ERROR_LINES_0`-`2` | R | 1 while that unit's errors are non-zero; **live**, following the lines |
| `RUN_LINES_0`-`2` | R/W | **0 halt, 1 run**: one RUN/HALT line per unit |

- **Per-unit halt:** C&C 1 halts and runs each controller on its own (v1.1 had one line to every tile).
  Bit 64 is unused, so C&C 1 cannot halt itself.
- **The unit-error bit** (owner, 2026-10-28): the peripheral's `ERRORS` word (10, word 2) has one bit
  that is 1 while any of `ERROR_LINES_0`-`2` is non-zero, **live**, the OR of every line. *(Proposed: bit
  0.)* The peripheral's own errors (Ingress/Egress, the UART) take the other bits and stay sticky.
- **Errors reach C&C 1 through its peripheral:** the unit-error bit is an error of the peripheral, which sets
  C&C 1's per-peripheral bit in its tile errors (10) and takes the hard interrupt (1). Because the lines are
  live, halting the failed units clears them, the peripheral's error, C&C 1's bit and the C&C pin (1.5).
  This peripheral's tile-error bit is live, an exception to 10's "sticky until HALT".
- **The engines are their controllers' peripherals:** 72 (Ingress/Egress) is C&C 1's, 73 (Updater/Binder/RW)
  C&C 2's and 74 (the Tracer) C&C 3's. An engine's error is its controller's peripheral error, so it
  raises that controller's error line; halting a controller halts its peripheral. They need no bits of
  their own.
- **Q4 still open:** what HALT clears, which units a halted tile still runs (routers, request port), the
  hold before RUN.

---

## Big SRAM: to validate

| # | what | source |
|---|---|---|
| S1 | **A write reaches its area's queue within K = 8 cycles** of the director accepting it (input FIFO, pointer stage, a spanning write's second part). Tony estimates about 6. meshsim to flag any counter decrement before its write is queued | 3 |
| S2 | **Reads come back in order per requesting client** (address generator, GATHER engine), whichever areas they hit; read-port reads in any order | 4.1, 4.5 |
| S9 | **The protected loopback:** reads are limited by credits so the loopback queue cannot overflow. 64 tokens; a packet for the tile itself (a loopback read, or a send-port packet to itself or broadcast) holds its token until it leaves the loopback FIFO, which is at least 64 deep, so an excess read waits for a token. **Required** (owner, 2026-10-28), no longer proposed; meshsim part M measured no loss with it | 4.5, 4.7, spec items 74, 116 |
| S3 | The area macros: compiler numbers for a 128 × 32K area at 1 GHz, one access per cycle; the array's read latency; density and arrangement | spec item 1 |
| S4 | The area request-queue depth for the hardware (8 in meshsim) | spec item 2 |
| S5 | Queueing against the group-run timetable: the bound the 64 tokens and return queues must absorb, and the placement rules that keep each phase within one access per area per cycle | spec item 4 |
| S6 | The `PTR` and `HIST` macros: a write lands intact when port A reads the address port B is writing | spec item 5 |
| S7 | Write combining of consecutive 32- or 64-bit writes to one row | spec item 3 |
| S8 | The big-SRAM size per tile (4.25 MB tentative), set by the sumcheck memory squeeze | spec item 107 |

---

## Pending questions

A running list; each entry names where it came from. Resolved entries move into a decision section above.

| # | question | from | status |
|---|---|---|---|
| Q1 | Tony's exact list of implemented CSRs, to define which CSR instructions are legal | 1.1 | waiting on Tony |
| Q2 | How long a high on the C&C pin counts as "sustained" for the host | 1.5 | open |
| Q3 | What stops the peripheral and the sender when a condition traps: the hardware on taking the interrupt, or the handler? (Today a halted controller stops them at once.) | 1.7 | open; suggestion: the hardware, so a failed handler can't leave them running |
| Q4 | The HALT design | planning notes section 7 | **partly resolved: section 15** (one line per unit, set by C&C 1; a halted controller halts its peripheral). Left: what HALT clears, what keeps running, the hold before RUN |
| Q5 | Should an area serve writes ahead of queued reads? Not needed for S1 if K covers only the path to the area queue | 3 | open; for Tony |
| Q6 | The link word's 32-bit address: what it covers | 2 | **resolved: section 5** |
| Q7 | The 3-bit mode: which of today's nine modes merge or go, now that counting is separate | 2 | **resolved: section 6** |
| Q9 | Does a stream's source stay a 21-bit big-SRAM chunk address, or does the 32-bit address apply to reads too? | 4 | open |
| Q10 | Is the 24-bit offset in bytes or in 32-bit words? | 5 | open |
| Q11 | How several peripherals share the slot's interface | 5 | **resolved: sections 9 and 10** |
| Q13 | Which peripheral flags only wake the controller and which raise a tile condition | 10 | **resolved: section 10** (conditions interrupt, wake events wake) |
| Q15 | Should `ROUTER_HDR` use the same upper-half layout as `STREAM_COUNT` and `DMA_HDR`? | 7, 10 | **resolved: section 12** |
| Q14 | The tile's `EVENTS` bit layout: counters 1-3 done, GO, counters done, command landed, peripherals 1-3 (live), the request port's events | 11 | next |
| Q12 | meshsim's per-tile debug console without a UART on every tile: request-bus debug messages (6.7) or a simulator-only print register | 9 | open |
| Q8 | The counter field in every packet source: `STREAM` entries, the send port's header, the sender, the read port's setup, the GATHER engine's gather values, and memory-system requests | 3 | open |
