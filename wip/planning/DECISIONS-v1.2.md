# Decisions toward v1.2

One entry per decision, newest section last. Labels follow the repository's convention: O owner decision,
T confirmed by Tony (controller core). The spec session works from this log; `spec/machine-spec.md` stays
the authority once an entry is written into it. Starting point: `PLANNING-NOTES-2026-10-27.md`.

## Where the design stands (read this first)

The sections below are in the order they were decided, and later ones revise earlier ones; superseded points
are marked in place. In brief:

- **Errors** (v1.1's conditions) raise the controller core's hard interrupt; its handler reports to C&C 1
  over the request bus and parks forever: traps are terminal (20). Each unit's error line reaches C&C 1's
  peripheral (`ERROR_LINES_0`-`2`); C&C 1 drains the reports, has the file system flush its logs, then
  fires the **global reset**, the only way to stop anything: there is no HALT state (21, 23). Every unit
  boots from a **boot ROM** and starts itself when its image's marker lands (22). REPORT is withdrawn.
- **The link word** is 256 bits: payload, a 32-bit address (8-bit region, 24-bit offset), size, a 2-bit
  counter, a 3-bit mode, src, dst; 75 bits reserved. (2, 5)
- **Counters:** the packet names one of three general write counters (0 uncounted), decremented K = 8
  cycles after the traffic director accepts it; additive arming. (3)
- **Modes:** 0 write at address (replacing five v1.1 modes, including commands and replies), 1 by source,
  2 bucket, 3 augmented, 4 gather request, 5-7 reserved and discarded silently. (6)
- **Address map:** 0x00 data memory, 0x01 program memory (32 KB each), 0x02 the boot ROM, 0x03 big SRAM,
  0x04-0x06 up to
  three peripherals, 0xFF the controller's registers and table window; the controller reaches only 0x00,
  0x01 and 0xFF. (5, 9)
- **Registers:** the controller's block and each peripheral's window start the same way: `PARAMS`,
  `STATUS`, `ERRORS`, pulses, events, send, then the rest. One header layout everywhere. (10-14, 16, 18)
- **Waiting** is by events throughout, including for the tables (`ts_busy` goes). (16, 17)
- **Open:** the pending questions at the end; big-SRAM guarantees to validate, S1-S9.

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

- **A condition raises the core's hard interrupt.** The handler reads the state (conditions, `STATUS`,
  counters, `mepc`/`mcause`/`mtval`, `PERIPH_STATUS_0`-`7` *(now each peripheral's window, 10)*, x1-x31:
  about 50 words) and sends it to C&C 1
  as request-bus reports (about 17; header word: op, sequence number; three data words each).
- **Parking:** ~~the handler then clears `EVENTS_MASK` and `EVENTS_REQUIRED` and loops on `WFI`~~
  *superseded by 20: traps are terminal; the handler parks in a plain loop.* Only HALT recovers the tile.
- **Conditions during the `done` gate:** the gate controls only `WFI` wakes. A condition raised between
  `done` and GO still takes the hard interrupt.

### 1.5 Error lines and the C&C pin (O)

- **Every tile has a dedicated error line to C&C 1**, driven by hardware from its conditions (no software
  involved), with a **fixed latency**. It still reports a tile whose request path is stuck or whose handler
  has itself failed.
- C&C 1 sees the lines as **three 32-bit words**: two for the 64 tiles (one bit per tile) and one for the
  C&C controllers and other non-tile units. Their bit assignment is part of the endpoint map (to-do item 11).
- **The three words fold into C&C 1's conditions**, and C&C 1's conditions drive its own hard interrupt, so
  C&C 1 is interrupted whenever any core fails, itself included. *Superseded by 15: the words are
  `ERROR_LINES_0`-`2` in C&C 1's peripheral, and they reach C&C 1 through that peripheral's unit-error bit.*
- **The fold is live:** it follows the error lines; it does not latch. C&C 1's own conditions stay sticky,
  as everywhere.
- **The C&C pin:** an output pin on the ASIC, high while C&C 1 has any condition. *Superseded by 24: two
  pins, a hardware error pin for C&C 1's own errors only and a software pin.*
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
  256 wires per link direction, with the channel width to confirm in physical design. *Revised (W2-14):*
  with the fifth port's 160 entries and the loopback's extra entries, the FIFOs alone add about **+0.065 mm²**
  per tile *(inferred)*, plus the padding stages, the send holding register, the director's input stage and
  metadata growing with the 32-bit address. Still well inside 1.5 mm² per tile; section 11 is redone line by
  line with the spec update.
- **The full 256 bits are stored** (owner, 2026-10-28; Tony's request), reserved bits included, about 36 Kbit
  of constant-zero flops per tile. **To revisit when the design has settled:** trim the storage to the bits in
  use while the links and interfaces stay 256 wide. **Power is the main reason** (owner): constant-zero flops
  don't toggle their data but are still clocked every cycle unless gated, which matters more than their area.
- The 32-bit address is in 5 and the mode codes in 6.

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
- **Follow-on:** the global reset clears the delay line with the counters (21, 23; W2-18: with no HALT
  state, its edge questions don't arise). The counters' events are in 16.
- **Additive arming** (owner, 2026-10-28; v1.1's rule kept): a store of *N* adds *N*, atomically and in step
  with that cycle's decrement; *N* > 0 arms the counter, which **clears its done event** and counters done.
  So whichever comes first, the last word of an earlier batch or the add, once the add has happened done
  means **everything added so far has landed**: no stale wake and no early wake. If the count reaches 0 in
  the same cycle as an add of *N*, it ends at *N* and done doesn't fire.
- **Usage rule:** one counter per thing a program waits for separately. Adding to a counter means waiting
  for the union; to learn that batch A has landed while B is outstanding, put them on different counters.
- **Accepted trade-off of the delay** (owner, 2026-10-28): unexpected arrival is detected when the
  decrement happens, K cycles after acceptance. A stray packet accepted at count 0 whose decrement lands
  after the program arms *N* uses up one of the *N*: done fires one word early, and the batch's last real
  word then raises unexpected arrival. The error is caught, later than in v1.1 and naming the last real
  word rather than the stray; only a second, cancelling bug (a batch armed one too high) would hide it.
  Considered and not taken: decrement at acceptance and delay only the done. **meshsim models the
  hardware exactly** (owner, 2026-10-28; W2-16): its `ERRORS` bit and error line follow the decrement, as in
  hardware, and it **reports the stray separately, as a diagnostic at acceptance**, naming it precisely. The
  simulated chip never gets out of step with the real one.
- **HALT no longer suppresses counting** (replacing 3.1 and 4.12's "a halted tile counts nothing"). Boot
  loads and staging into a halted tile are sent with counter 0. ~~A counted packet arriving at a halted
  tile hits a counter at 0 and sets unexpected arrival, which survives into RUN where the loader can see
  it.~~ *Superseded by 19: a halted tile's ejector drops counted packets silently.*
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
| 0x02 | **boot ROM**, read and execute only; the reset vector is 0x02000000 (22) |
| 0x03 | big SRAM |
| 0x04 | peripheral 1 |
| 0x05 | peripheral 2 |
| 0x06 | peripheral 3 |
| 0x07-0xFE | reserved |

*Renumbered 2026-10-28 (22): the boot ROM took region 0x02; big SRAM was 0x02 and the peripherals 0x03-0x05.*
| 0xFF | controller only: registers (9) and the table window |

- **The region names where a packet lands**, so the mode no longer has to (Q7).
- **The controller memories become 32 KB + 32 KB** (planning notes to-do 2, spec part N).
- **A tile may have up to three peripherals** (9), where 4.17 has one slot.
- **Program memory is load-only for the controller** (Tony; W2-19): it fetches from and loads from region
  0x01, but a store to it is a bus error. Only the traffic director writes it (port B), as in v1.1, where a
  load was a bus error too. The boot ROM (0x02) is fetch and load only.
- **The controller sees 0x00, 0x01 and 0xFF only.** It still cannot load or store big SRAM or a
  peripheral directly (current plan): every existing mechanism for reading and writing big SRAM carries
  forward (the read port, sending itself packets, address-generator copies).
- **Region 0xFF is controller only:** a packet addressed to it is a bad packet.
- **Follow-on:** the controller's map moves (today program memory is at 0x0000, data at 0x4000, the table
  window at 0x2000 and the registers at 0xFFFFF800), so the reset PC, the resident loader's address, the
  linker script and `mesh.h` change.
- **The offset counts bytes** (owner, 2026-10-28; Q10, W2-07): the 24-bit offset is in the controller's
  byte space, so a C pointer *is* the packet address, with no `CM_WORD()`-style shifts. 24 bits cover
  16 MB, above big SRAM's 8 MB maximum. **Big SRAM keeps its 32-bit chunk addresses internally**: `PTR`,
  `STREAM_SRC`, `SRAM_RD_ADDR_QUEUE` and `DMA_RECORD_BASE_ADDRESS` stay in chunks, so code building a
  big-SRAM packet address from a chunk address shifts it by 2 (the spec says so in 4.2-4.4). The read
  port's destination offset (13) is in bytes.

## 6. Modes (2026-10-28)

Decided (O). Answers Q7. With the region choosing the destination (5) and the counter field doing the
counting (3), the 3-bit mode keeps only what the receiver must do differently:

| code | mode |
|---|---|
| 0 | **write at address**, any region the packet may reach |
| 1 | big-SRAM write by source, at `PTR[src]` |
| 2 | bucket write, at `PTR[digit]` |
| 3 | augmented write, at `PTR[src]` |
| 4 | gather request (local only); **never counted**: its counter field names the counter its gather values decrement |
| 5-7 | reserved: **discarded at the destination, with no error** |

- **The counter field counts for modes 0-3 only** (owner, 2026-10-28; W2-03). Mode 4 passes it on to its
  gather values (below). **Reserved modes 5-7 are discarded with no decrement**, whatever their counter
  field: a counted one leaves its counter short and the program waits forever, with no error (accepted;
  meshsim flags every reserved-mode packet). Section 3's "counting no longer depends on the mode" holds for
  the writes.
- **A gather request never decrements a counter** (owner, 2026-10-28; W2-02), whatever its counter field
  says: it isn't a write. **Its counter field is passed on to the gather values it produces**, which are
  counted like any other write when they pass the director. (Answers Q8 for the GATHER engine.)
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
- **When "command landed" is set** (owner, 2026-10-28; W2-12): **when the slot-0 write lands in port B**,
  not when the traffic director accepts it, so the event (and READY, and the boot ROM's jump, 22) follows
  the write. **Only writes that come in through the traffic director set it**; any such write covering
  slot 0 does, a 64- or 128-bit write starting there included (it also writes slots 1-3, harmlessly). The
  controller's own store to slot 0 doesn't. (Settles the "detail to settle" below.)
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
  2026-10-28): [0] no-increment (below), [17:1] spare, size [19:18] reserved (the size comes from the send register), counter
  [21:20], mode [24:22], dst [31:25].
- **A no-increment bit** (owner, 2026-10-28; W2-09) in `ROUTER_HDR`'s spare low bits *(proposed: [0])*:
  when set, `ROUTER_ADDR` stays put after a send. A reply always goes to the same address in every
  receiver (`REPLY` + 8 × the sender's id, 6), so a program sets `ROUTER_ADDR` once and changes only the
  header per send, keeping the split's all-to-all exchanges at today's cost (one header store and one
  send per reply). Without it, every reply would need a `ROUTER_ADDR` store too. The same holds for
  one-word exchanges to many tiles at one address; commands write slot 0 last, below their arguments, so
  they still store `ROUTER_ADDR` before it.
- **Size stays with the send register** (`ROUTER_SEND_32`, `_64`, `_128`); `ROUTER_SET_W0`-`W2` are
  unchanged and stay write-only.
- One extra store when both change; a change of dst alone, the common case in replies and one-word
  exchanges, is still one store. The register map is renumbered anyway when it moves into region 0xFF.
- **Toward Q8** *(superseded by 10: the sender is now a peripheral's DMA, with `DMA_HDR` and `DMA_ADDR`)*:
  `SENDER_SEND` has room for the counter field (dest 7, mode 3, counter 2, queue 2, count
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
  `EVENTS_` and `ROUTER_` families (first proposed as `TILE_DONE_PULSE`, `DMA_START_PULSE`, `TILE_GO_PULSE`).
- Firing two actions in one store is no longer possible; 8.6's "in one store, start and run act before
  `done`" goes. Separate stores act in program order, and the synchronised start path is still one store.

## 9. Peripherals and the register space (2026-10-28)

Decided (O). Answers most of Q11.

- **A tile has at most three peripherals**, in regions 0x04-0x06 (5). Packets reach a peripheral through
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
  into each peripheral's window, and so do the run pulses (10). The replayer
  (4.19) becomes peripheral 1 on the tiles that have one.

## 10. The standard peripheral window (2026-10-28)

Decided (O). Every peripheral's 0x200-byte register window (9) starts with the same words, in the same
order as the controller's block (11): parameters, status, conditions, pulses, wakes, DMA, then the rest.
Word *n* is at window base + 4*n*.

| word | register | access | purpose |
|---|---|---|---|
| 0 | `PARAMS` | R | constant: the peripheral's type, version, sizes; 0 if it has nothing to say |
| 1 | `STATUS` | R | live bits: run in progress, DMA in progress, and the peripheral's own |
| 2 | `ERRORS` | R | error bits, sticky until HALT like the tile's `ERRORS` (one exception: 15) |
| 3 | `PULSE_RUN` | W | store 1 to start a run |
| 4 | `PULSE_DMA` | W | store 1 to start the peripheral's DMA as set up in words 8-9 |
| 5 | `EVENTS` | R | sticky events; a load consumes those in `EVENTS_MASK`, as on the tile |
| 6 | `EVENTS_PEEK` | R | what a load of `EVENTS` would return, consuming nothing |
| 7 | `EVENTS_MASK` | R/W | the events that count |
| 8 | `DMA_HDR` | R/W | count [11:0] (32-bit words, up to 4,095), output queue [13:12], spare [17:14], size [19:18] reserved (the DMA picks sizes), counter [21:20], mode [24:22], dst [31:25] |
| 9 | `DMA_ADDR` | R/W | the DMA's 32-bit destination address, advancing by each packet's size as packets go out |
| 10-31 | | | reserved for future standard words |
| 32-127 | | | peripheral-defined |

- **Conditions interrupt; wake events wake.** `ERRORS` has one bit per peripheral, set while
  that peripheral's `ERRORS` is non-zero, so a peripheral error raises the hard interrupt. The tile's
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
- **A peripheral that sends has its own router port** (owner, 2026-10-28; W2-20): it is paired with a router
  link the tile doesn't otherwise use, and its packets enter the router there, like a neighbour's, as
  v1.1's sender did (4.17.9). The tile's injection point keeps its two sources (the address generator and
  the send port); a DMA packet to its own tile reaches the ejector through the router like any arrival and
  takes no read token, so S9 holds. Only a tile with a free link can have a sending peripheral; C&C 1-3 have
  them (25).
- **The DMA is optional.** A peripheral without output (the replayer, for one) leaves words 4, 8 and 9
  unused (`PULSE_DMA`, `DMA_HDR`, `DMA_ADDR`). This replaces v1.1's tile-level sender (`SENDER_SEND`, `SENDER_ADDR`, `sender_busy`, sender
  idle at `EVENTS` [20]; 4.17.9, 8.12).
- **The run pulses move into the windows:** section 8's `PERIPH_1_RUN_PULSE`-`PERIPH_3_RUN_PULSE` go.
- Replaces v1.1's `PERIPH_STATUS_0`-`7`, `periph_running`, the six service lines and the peripheral
  conditions [13:11] in the controller's registers (4.17). The peripheral type moves from `STATUS`
  into each peripheral's `PARAMS`.

## 11. The controller's register block (2026-10-28)

Decided (O): the order (parameters, status, conditions, pulses, wakes, send, the rest) and the three
first words. *(Proposed: the addresses below.)* In region 0xFF; the low 12 bits shown.

| addr | register | access | notes |
|---|---|---|---|
| 0x800 | `PARAMS` | R | **new**, constant: endpoint id, big-SRAM size, peripheral-present mask [2:0]; split out of v1.1's `TILE_STATUS` |
| 0x804 | `STATUS` | R | live bits only; layout in 18 (`ts_busy` goes, 17) |
| 0x808 | `ERRORS` | R | gains one bit per peripheral (10); loses the peripheral conditions [13:11] |
| 0x80C | `PULSE_DONE` | W | 8 |
| 0x810 | `PULSE_DMA` | W | 8 |
| 0x814 | `PULSE_GO` | W | 8 |
| 0x818 | `EVENTS` | R | layout in 16 |
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

Used by `STREAM_COUNT` (4), `ROUTER_HDR` (7), a peripheral's `DMA_HDR` (10) and `SRAM_RD_DEST_ADDR` (13,
size and counter only); any new header register follows it. *(W2-21:)* the block hardware copies straight
into the link word is size, counter and mode, [24:18]. In `STREAM_COUNT`, [31:25] is *d*, which is relative
for 0-63 and absolute for 64-125 (4.2), so it is translated into dst, not copied. The low bits [17:0] are each register's own (a count, a queue, or spare).

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

## 14. Names: events, errors and the first words (2026-10-28)

Decided (O). **The wake registers are renamed** at both levels, the tile's and each peripheral's:

| v1.1 | v1.2 |
|---|---|
| `WAKE_EVENTS` | `EVENTS` |
| `WAKE_PEEK` | `EVENTS_PEEK` |
| `WAKE_MASK` | `EVENTS_MASK` |
| `WAKE_REQUIRED` (tile only) | `EVENTS_REQUIRED` |

- **The counters are `WRITE_COUNTER_1`-`WRITE_COUNTER_3`** (owner, 2026-10-28): every counted packet is a
  write (modes 0-3), so the name says what they count. The link word's field stays "counter".
- **Errors** (owner, 2026-10-28): v1.1's *conditions* are now **errors**, as a concept and in names: the
  tile's `TILE_CONDITIONS` is `ERRORS`, a peripheral's `CONDITIONS` is `ERRORS`, and a bit is named without
  repeating the word (`ERRORS_BUS`, not `TILE_COND_BUS_ERROR`). The memory system's "C&C error flags"
  are a separate thing and keep their name.
- **No `TILE_` prefix** (owner, 2026-10-28): the tile's first registers are `PARAMS`, `STATUS`, `ERRORS`,
  the same names as a peripheral's first three words (10), as `EVENTS` already is.
- "Interrupts" was considered and rejected: events only release `WFI` and take no trap, while errors raise
  the hard interrupt (1) and the core's `mie`/`mip` CSRs use the word in the RISC-V sense.
- This log uses the new names throughout, including where it cites v1.1's bit positions.

## 15. C&C 1's peripheral: error and run lines (2026-10-28)

*Revised by 23: there is no HALT state, so `RUN_LINES_0`-`2` and per-unit halt are gone; the unit-error
bit is `OR(ERROR_LINES)`. The error lines, `RESET` (21) and the READY lines (21, 22) remain.*

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

## 16. The tile's `EVENTS` (2026-10-28)

Decided (O). Answers Q14. The layout of `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK` and `EVENTS_REQUIRED`:

| bit | event | kind |
|---|---|---|
| [0] | write counter 1 done | sticky |
| [1] | write counter 2 done | sticky |
| [2] | write counter 3 done | sticky |
| [3] | counters done: all three at 0, at least one armed since it last fired | sticky |
| [4] | GO | sticky |
| [5] | command landed | sticky |
| [6] | DMA run done (v1.1's "engine run done") | sticky |
| [7] | request FIFO no longer full | sticky |
| [8] | request pending | live |
| [9] | peripheral 1: its `EVENTS & EVENTS_MASK` non-zero (10) | live |
| [10] | peripheral 2 | live |
| [11] | peripheral 3 | live |
| [31:12] | reserved, read 0 | |

- **Gone from v1.1:** the fourth counter's done, the six service lines, sender idle and the UART's two
  events; [7:4]'s gap closes.
- **Unchanged rules:** `EVENTS_MASK` resets to GO | counters done ([4] | [3]) at chip reset and HALT → RUN;
  from the `done` pulse until GO only GO wakes `WFI`; a load of `EVENTS` consumes the sticky events under
  the mask, and one set in the load's own cycle survives. The live events clear only at their source:
  popping the request, or loading the peripheral's own `EVENTS`.

## 17. Waiting for the tables by events (2026-10-28)

Decided (O). Replaces `wait_tables()`, which spin-polls `rd_running` and `ts_busy` before the program
touches `STREAM`, `GROUP`, `PTR` or `HIST` (about 15 calls in the sort). Software waits for the tables with
events, like every other wait:

- **The arrival side** (incoming packets updating `PTR` for modes 1-3 and `HIST` for count-on-arrival,
  and local gather values landing at `PTR[me]`) is covered by the write counter the program armed.
- **The local side** (the address generator reading `STREAM` and `GROUP`) is covered by DMA run done,
  `EVENTS` [6].
- **Software** sets `EVENTS_REQUIRED` to "write counter *n* done and DMA run done" and executes `WFI`. The
  counters alone are not enough, since arrivals can finish before the tile's own run.

Hardware guarantees this needs:

| # | guarantee |
|---|---|
| A | **K covers the table update:** by the time a packet's counter decrements, its `PTR` or `HIST` update is complete (S1) |
| B | **DMA run done fires only after the run's last table read has completed**, not when the last entry is walked |
| C | **Gather values pass the traffic director like any loopback packet and are counted there** (corrected 2026-10-28, W2-02: they go out as packets to the tile itself and loop back through the router, 4.4, 4.5). Their K delay starts at the director like every other packet's, and a counter still takes at most one decrement per cycle |

- **Uncounted traffic is mode 0 only** (owner, 2026-10-28; W2-11): uncounted packets are used for
  **commands** and **boot and staging**, and all of them are mode-0 writes at explicit addresses. **Boot and
  staging images for big SRAM go to region 0x03 by address**, no longer by source through `PTR[64]` (v1.1's
  6.2), which the 32-bit address makes possible. So uncounted traffic never touches `PTR` or `HIST`, the
  convention is simply **modes 1-3 are always counted**, and `PTR[64]` stops being special: C&C 1 places
  each image by address.
- **A running program sends no uncounted writes except commands** (owner, 2026-10-28; W2-17): anything
  whose landing matters is counted. v1.1's counters done waited for `mem_idle` and `cm_idle`, so uncounted
  writes had landed by the end of a phase; v1.2's done is the counters only. Commands signal their own
  landing ("command landed", W2-12); boot and staging end with the marker, which lands last (22).
- **Guarantee A's "complete"** (W2-11) means **readable by a controller load through the table window**,
  `HIST`'s count-on-arrival included, behind its request FIFO; validated with S1's table-window case.
- **`PULSE_DMA` clears DMA run done** (`EVENTS` [6]) (owner, 2026-10-28; W2-10), as an arming write clears
  its counter's done (3), so [6] always refers to the run most recently started and a stale one can't
  satisfy the next wait.
- **`ts_busy` and `wait_tables()` go.** Programs wait by events. A helper that must work without knowing
  what ran before (a pass that starts by checking the tables are free) **may poll `STATUS.rd_running`**, a
  level, as a software choice (W2-10); otherwise `STATUS` is diagnostic.

## 18. `PARAMS`, `STATUS` and `ERRORS` (2026-10-28)

*(Proposed.)* v1.1's positions kept wherever a field survives.

**`PARAMS`** (constant)

| bits | field |
|---|---|
| [2:0] | peripherals present: bit *n*-1 for peripheral *n* |
| [19:3] | spare |
| [26:20] | endpoint id (v1.1's position) |
| [31:27] | big-SRAM size in 256 KB units - 1 (v1.1's position) |

The peripheral type (v1.1's `TILE_STATUS` [15:12]) moves into each peripheral's `PARAMS`.

**`STATUS`** (live, diagnostic only: no program needs to poll it, 17)

| bit | field |
|---|---|
| [0] | `rd_running` |
| [1] | `rd_done` |
| [2] | GATHER busy |
| [3] | `send_busy`: the send port's holding register is occupied |
| [4] | `done` |
| [5] | command landed |
| [6] | `request_busy` |

Gone: `periph_running` and `sender_busy` (now each peripheral's `STATUS`), `ts_busy` (17), the constants
(`PARAMS`), and **`mem_idle` and `cm_idle`** (owner, 2026-10-28; W2-17): no rule depends on them any more, so
they leave the hardware and are kept in meshsim only, as diagnostics.

**`ERRORS`** (sticky until HALT, except [13:11])

| bit | error | v1.1 |
|---|---|---|
| [0] | router overflow | |
| [1] | bad packet or misaligned access: a misaligned packet or big-SRAM access, or a region it can't reach (26) | reserved modes no longer count (6) |
| [2] | unexpected arrival | |
| [3] | drift | |
| [4] | write-engine input overflow | |
| [5] | table request overflow | |
| [6] | GATHER queue overflow | |
| [7] | read-port error | |
| [8] | done early | |
| [9] | bus error | |
| [10] | illegal instruction | |
| [11] | peripheral 1: its `ERRORS` non-zero (live) | was bad peripheral write |
| [12] | peripheral 2 (live) | was peripheral condition 2 |
| [13] | peripheral 3 (live) | was peripheral condition 3 |
| [14] | receive underflow | |

The peripheral bits take [13:11] so every other error keeps its v1.1 number.

## 19. What a halted tile does (2026-10-28)

*Superseded by 23: there is no HALT state; the global reset (21) clears everything instead. Kept for the
record. Still true: big SRAM's in-flight accesses complete, and the router's link FIFOs are never cleared
except by the reset.*

Decided (O), from WORKER1's review (W1-01, and parts of W1-05 and W1-06). Part of Q4.

- **Halting a tile stops its sending engines:** the address generator, the GATHER engine, the read port,
  the send port's holding register and its peripherals' DMAs. A run in progress is abandoned. The traffic
  director, the write engine and the routers keep running, so a halted tile can still be loaded.
- **Throughout HALT every queue feeding the tile's injector is held empty** (owner, 2026-10-28): the
  loopback FIFO and the tile's outgoing queues before injection are emptied at the RUN → HALT edge and
  anything that enters them during HALT (a read result still in big SRAM's pipeline at the edge, say) is
  discarded. **The read-token count is held at 64** (the loopback space tracker simply reinitialised), so
  every RUN starts with empty queues and every token free. A halted tile has nothing to send on the mesh.
  Packets already in the injector or the router flow on and clear the mesh. **The router's link FIFOs are
  never cleared:** they carry other tiles' transit traffic, boot loads and, on tiles 0-2, the controllers'
  fifth links. **The request FIFO is not part of this:** it keeps draining in HALT (4.18), so reports
  already queued still reach C&C 1.
- **Big SRAM finishes what it has** (owner, 2026-10-28): accesses already in its area queues and pipeline
  at the RUN → HALT edge complete. Writes land; reads finish, and their results are discarded at the
  held-empty queues above.
- **In HALT the ejector drops counted packets** (counter 1-3) silently: no write, no count, no error. It
  accepts uncounted packets (counter 0) as usual, so boot loads, staging and the boot marker still land
  (6.2), and 6.2's "halt a tile before an uncoordinated write" still works.
- **Why:** a failed phase's late traffic is counted, so it is dropped instead of raising unexpected arrival
  in a halted tile and holding its error line high (W1-01).
- **Accepted trade-off:** a boot load or staging packet sent with a non-zero counter by mistake is dropped
  silently; the loader finds missing data rather than an error. (Section 3 had it raise unexpected arrival.)
- **C&C 1's unit-error bit (15) is `OR(ERROR_LINES & RUN_LINES)`** (owner, 2026-10-28; W1-01's fix),
  so the errors a halted unit can still raise (bad packet, router overflow) don't interrupt C&C 1;
  `ERROR_LINES` stays raw, and the error interrupts C&C 1 when it sets the unit running.

## 20. Traps are terminal (2026-10-28)

Decided (O, T), from WORKER1's review (W1-02, W1-04). Revises 1.4's parking rule.

- **The handler never executes `mret`.** A trap, whether from an error (the hard interrupt) or a
  synchronous exception, is a terminal event: the state is assumed bad, and the interrupted code is never
  resumed. Only HALT → RUN restarts the controller (6.1).
- **This holds for C&C 1 too.** C&C 1 takes the hard interrupt for another unit's error (15), and its
  handler runs the abort itself: collect the reports, halt the units, tell the host over Ingress/Egress.
  It never returns to the code it interrupted.
- **Tony's answers:** the hard interrupt **stays high until cleared** (level: our `ERRORS` source is
  sticky, so it stays high until HALT); it is **gated by `mstatus.MIE` and its `mie` enable**, and the same
  gating applies to `WFI` wake-ups.
- **So no retrap:** `MIE` is cleared on trap entry and never set again, so the still-asserted interrupt
  cannot trap a second time. W1-02's report storm cannot happen, and its edge/level question no longer
  matters for correctness.
- **The park** *(proposed)*: after its reports the handler loops forever on a plain jump to itself. (A
  `WFI` loop would also work only if `mie`'s enable is cleared first, since a pending enabled interrupt
  may release `WFI` with `MIE` = 0; the plain loop doesn't depend on that.) Replaces 1.4's "clear
  `EVENTS_MASK` and `EVENTS_REQUIRED` and loop on `WFI`", which also failed in the `done` gate (W1-12).
- **No torn sequences matter** (W1-04): nothing resumes, so a register sequence the main code left half
  done (the send port's `W` words, the request port's `SET`/`GET` words, a read-modify-write of
  `RUN_LINES`) is never completed. The handler must not assume those interfaces are clean: it writes
  every word of each packet and request it sends.
- **A second trap inside the handler** (a synchronous exception) restarts the handler at `mtvec`; it is
  still terminal.
- **`ecall` and `ebreak`** (owner, 2026-10-28; W1-11): no `ERRORS` bit and no hardware for them. We write
  every controller program and don't use them; if one is ever executed it traps like any exception, the
  handler's reports carry `mcause`, and the tile parks. The loader's "anything else -> `ebreak`" path
  changes (it can simply stay in its wait loop), and `CMD_EXIT`, simulator only, needs another way for
  meshsim to end a test: a meshsim matter, not a spec one.
- **Handler conventions** (owner, 2026-10-28; W1-17):
  - **What it reports:** the tile's `PARAMS`, `STATUS`, `ERRORS`, `EVENTS_PEEK`, the counters,
    `mepc`/`mcause`/`mtval`, x1-x31, and for each present peripheral its window's standard words (10),
    about ten words per peripheral, in place of v1.1's `PERIPH_STATUS_0`-`7`. The set is fixed (21).
  - **It checks `PARAMS` [2:0] before reading a peripheral's window:** reading an absent peripheral's
    window is a bus error, which would trap inside the handler.
  - **It reads `EVENTS_PEEK`, never `EVENTS`**, at both levels: a load of `EVENTS` consumes events and
    would destroy the state it is reporting.
- **For Q1 (Tony):** the CSR list and their reset values, in particular `mtvec`'s (W1-03). Where `mepc`
  points after a trap taken in `WFI` no longer matters.

## 21. Reset and error recovery (2026-10-28)

Decided (O, T). Answers the recovery policy (WORKER1's owner question 1), Q3 and most of Q4; settles
W1-03, W1-06 and W1-07.

**The global reset line.** One line resets tiles 0-63 and their peripherals, and C&C 2 and C&C 3 and
their peripherals (the Updater/Binder/RW and the Tracer). **C&C 1 and its peripheral (Ingress/Egress, the
UART) are outside it**, so the host link survives. The same line is the power-on reset:

- **Power-on** holds it until power is stable.
- **C&C 1** drives it through a register in its peripheral, between the error words and the run words
  (15): `RESET` *(name proposed)*, **0 asserts reset, 1 releases it**; software holds it at 0 for tens of
  cycles.
- **Units come out of reset halted:** C&C 1 clears `RUN_LINES_0`-`2` (they are in its peripheral, outside
  the domain), loads them, then sets their run bits, as in boot (6.2).
- A unit in the domain comes out of reset with its controller core reset, `mstatus.MIE` = 0 (W1-03), and
  every queue, FIFO and error cleared, so a failed phase's traffic is gone (W1-06).

**An error in a tile:**

1. An error sets a bit in the tile's `ERRORS` (for example unexpected arrival: a counted packet for a
   counter at 0), which raises the tile's hard interrupt.
2. The tile's handler sends its state to C&C 1 as request-bus reports, then loops forever (20). Its
   peripherals and engines carry on with whatever they were doing, unserviced by the controller, until the
   global reset (**answers Q3: nothing stops them before the reset**).
3. The tile's error line sets its bit in C&C 1's `ERROR_LINES_0`-`2`; the unit-error bit of C&C 1's
   peripheral (`OR(ERROR_LINES & RUN_LINES)`, 19) sets that peripheral's bit in C&C 1's `ERRORS`, and C&C 1
   takes its hard interrupt.
4. C&C 1's handler **drains every report** (owner, 2026-10-28) while it **flushes the file system's logs
   to LPDDR** through C&C 2. The flush procedure is TBD.
5. When the flush is complete, C&C 1 asserts the global reset and releases it. *(23: there are no run
   bits to clear.)*
6. C&C 1 loads the software into tiles 0-63 and into C&C 2 (the file system) and C&C 3 (the pager), with
   uncounted packets, while each unit sleeps in its boot ROM (22).
7. Each unit starts itself when its marker lands (22): **C&C 2's and C&C 3's markers first, then the
   mesh tiles'.**

- **The policy is abort and restart:** no job continues with a unit missing. Per-unit run bits remain
  for boot order and for loading.
- **C&C 1's handler is a recovery manager** (owner, 2026-10-28), more involved than a tile's. Its duties
  *(proposed; details for the C&C 1 firmware)*: tell its causes apart (another unit's error starts the
  recovery; an error of C&C 1 itself is fatal, 1.5); poll `ERROR_LINES_0`-`2` during the recovery, since
  with `MIE` = 0 further failures don't interrupt it; bound the draining, the flush and any host report
  with timeouts; restart cleanly.
- **C&C 1 after the restart** (owner, 2026-10-28): it never returns to the interrupted code (20); its
  handler continues into the restart, resets its stack, sets `mstatus.MIE` again and enters its main loop.
  This is safe because **the reset clears every unit's errors**: the lines drop, the live unit-error bit
  and C&C 1's per-peripheral bit follow, and C&C 1's hard interrupt input goes low before `MIE` is set.
  **Exception:** C&C 1's own errors are sticky and outside the reset domain; if any is set, C&C 1 does not
  re-enable and the failure stays fatal (the pin, 1.5).
- **The report set is fixed** (owner, 2026-10-28; W1-08): every handler sends the same reports, in the same
  order, so the count is a constant of the protocol and nothing in the reports needs to carry it. The spec
  defines the set (ops, order, count); C&C 1 counts each unit's reports against it, and its drain timeout
  covers a unit whose reports never all arrive. A trap inside the handler is not designed for: the
  handler is short resident code that only reads registers and writes the request port.
- **The UART's line errors are status, not errors** (owner, 2026-10-28; W1-10): framing and overrun are
  bits in C&C 1's peripheral's `STATUS`, never in its `ERRORS`. C&C 1's own errors are permanent (it is
  outside the reset domain) and fatal, so line noise on the serial port must not raise one.
- **Timing of the lines** (owner, 2026-10-28; W1-13):
  - **The reset is held long enough** for every unit in the domain to be reporting no error on its line
    when C&C 1 releases it: the hold covers the error line's fixed latency (a spec parameter). C&C 1 just
    waits; software doesn't check the lines. The run-bit mask (19) makes a stale line harmless anyway.
  - **`RUN_LINES` are 0 at power-on**; at a C&C 1-driven reset C&C 1 clears them itself (step 5), since
    they are outside the domain.
  - **Every per-unit run line has the same latency, `run_latency`**, as v1.1's single line. Boot takes
    three stores (two words of tiles, one for C&C 2 and 3), and 6.6's "wait `run_latency` + 1 after a
    RUN/HALT store" applies to the last.
  - **An error raised in the cycle of a HALT edge is lost** (the clear wins, 4.13). Accepted.
- **READY lines plug into C&C 1's peripheral** (owner, 2026-10-28; W1-14): `READY_LINES_0`-`2`, by
  endpoint id like the error and run lines (64 unused), with **C&C 2 and C&C 3 at bits 65 and 66**. Every
  unit boots the same way: its image, then the marker to command slot 0, then READY (6.2); since packets
  from one source to one destination arrive in order, READY means the image has landed, and only then
  does C&C 1 set the unit's run bit. *(Open: how the READY bits clear, as GO does in v1.1, and an "all
  expected units ready" event in the peripheral's `EVENTS`.)*
- **Draining matters:** reports left in C&C 1's receive queue back up the request-bus tree, and the
  flush's own requests to C&C 2 would queue behind them in the same rows (W1-09). With all 64 tiles
  failing at once (one bad broadcast), that is about 1,000 reports.

*(Proposed, for confirmation:)*


- **The boundary queues:** before releasing the reset, C&C 1 empties the queues it holds facing the reset
  domain (its request receive queue, the FIFOs on tiles 0-2's fifth links), or they reset with the domain,
  so the credits on both sides start out matching.
- **A flush that can't finish** (the failed unit is C&C 2 or C&C 3, or their peripherals): C&C 1 times out
  and resets anyway.
- **Open:** whether C&C 1 also reports the failure to the host over Ingress/Egress, or the flushed log is
  the whole report.

## 22. Booting from ROM (2026-10-28)

Decided (O, T), from W1-14. Replaces 6.2's halted loading with READY and section 21's READY-based start
of C&C 2 and C&C 3.

- **Every controller has a small boot ROM** in region 0x02 (5), read and execute only. **The reset vector
  is 0x02000000.**
- **The boot ROM's job:** set `mtvec`, sleep in `WFI` until "command landed", then **jump to the address
  in the command block**. *(Proposed: it also holds a minimal trap handler, report and park (20), for
  `mtvec` to point at until the loaded image installs its own.)*
- **Boot:** C&C 1 releases the global reset (21); every unit in the domain starts in its boot ROM and
  sleeps. C&C 1 sends each unit its image, uncounted, then **the marker to command slot 0, last**.
  Packets from one source to one destination arrive in order, so when the marker lands the whole image is
  in place, and **the unit starts itself**.
- **No handshake is needed:** no READY round trip for booting, no stale READY bits across a reset, no
  waiting by C&C 1. C&C 2, C&C 3 and the mesh tiles boot the same way. **Boot order is the order of the
  markers:** C&C 2's and C&C 3's first, then the tiles'.
- **READY and GO remain for the phase cycle only** (6.3).
- **Cost:** a few dozen bytes of ROM per controller, and the ROM's region in the map.

## 23. No HALT state (2026-10-28)

Decided (O, T). With booting from ROM (22), **units have no HALT state at all: the global reset (21) is the
only way to stop anything**, and coming out of reset means running in the boot ROM. Settles Q4.

- **Gone:** v1.1's RUN/HALT nets and `run_latency`, `RUN_LINES_0`-`2` (15), per-unit halt, and section 19's
  halted-tile rules (no halted tile exists to drop counted packets or hold its queues empty).
- **C&C 1's unit-error bit is `OR(ERROR_LINES)`** (15): the run-bit mask (19) isn't needed, since the reset
  clears every stale error.
- **Errors are sticky until the reset**, not until a HALT edge (4.13). `EVENTS_MASK` resets at reset.
- **Loading needs no special state:** a unit asleep in its boot ROM accepts uncounted writes normally. 6.2's
  "halt a tile before an uncoordinated write" goes; staging into a running unit is coordinated by software.
- **READY is for the phase cycle only**, tiles 0-63: `READY_LINES` bits 65 and 66 (21) aren't needed.
- **What we gain: starting units one at a time** (owner). Every unit leaves the reset together, but each
  starts only when its own marker lands; the rest sleep in their boot ROMs. C&C 1 can bring up a single
  tile at a time (chip bring-up, testing units one by one), or start C&C 2 and C&C 3 and check them before
  loading the mesh.
- **What we give up:** stopping a single unit to inspect it. After an error we get what the handler's
  reports carry, then the chip restarts.
- **Debugging instead** (owner): each tile opens log files through the file system and `printf`s into them;
  before the reset fires, the file system flushes their tails to LPDDR (21, step 4). This also answers Q12
  for the hardware; how meshsim shows them (a console per tile) is meshsim's business.

## 24. The two status pins (2026-10-28)

Decided (O), from W1-16. Replaces 1.5's C&C pin; settles Q2.

- **The error pin**, driven by hardware: high while **C&C 1 has any error of its own**, that is, C&C 1's
  `ERRORS` with the unit-error bit of its peripheral (15) excluded: C&C 1's own error bits, plus its
  peripheral's errors other than the unit-error bit. Those are sticky and outside the reset domain (21),
  so once high the pin stays high until the host acts.
  - **A unit fails:** only the unit-error bit is set; C&C 1 is interrupted and recovers, and the pin stays
    low. The pin never pulses, so **Q2's "sustained high" threshold goes away**.
  - **C&C 1 or its peripheral fails:** the pin goes high and stays high, and the host steps in (chip reset).
  - It can drive an LED directly: steady on means C&C 1 is dead.
- **The software pin**, driven by C&C 1 through a register in its peripheral *(name and bit proposed:
  `LED`, bit 0)*: C&C 1 can blink codes on it while it is healthy (boot progress, which unit failed, a
  recovery in progress).

## 25. The engines and the LPDDR write path (2026-10-28)

Decided (O), from W2-04. Settles what addresses mean at the controllers and engines.

- **The controllers are normal tile instances** (2 MB of big SRAM; planning notes section 5), so the
  address map (5) means at C&C 1-3 exactly what it means on a tile.
- **The engines are their controllers' peripherals** (15): Ingress/Egress is C&C 1's, the Updater/Binder/RW
  engine C&C 2's, the Tracer C&C 3's. A packet reaches an engine at its controller's endpoint, in that
  peripheral's region. **Endpoints 72-74 go**; each controller's link to its engine is internal to the unit.
  **Endpoint 65 is C&C 2**, with the memory system as its peripheral (resolving the clash WORKER1 found
  with 2.1 and 6.7). *(Proposed; to confirm with the endpoint map, item 82.)*
- **Writing LPDDR, by tags:**
  1. The file system (C&C 2) owns a pool of **tags**. For each write it picks a tag and sends the RW engine
     (its own peripheral) a request: the LPDDR destination and the tag.
  2. **Each tag owns a window of 2-4 KB** in the RW engine's region; **a tag window is a single write.**
  3. The file system directs the tiles to write the window, as plain mode-0 packets to dst 65 in the RW
     engine's region. No special mode is needed (v1.1's mode 3 to d = 64 goes).
  4. When the window is complete, the RW engine writes it to LPDDR and **returns the tag** to the file
     system. *(How is open, a detail of the engine's design; suggested: an event in the engine's `EVENTS`
     and a register presenting the closed tag, rather than a request-bus message.)*
- **Requests that cause data to come back** (W2-05; owner: as needed, details to settle with 6.7's request
  format): the request names the destination as a full 32-bit address (replacing v1.1's destination kind
  and 128-byte-unit address), and, if the answer must be counted, the data's counter and the reply's
  counter. Whether requests carry counters at all, or answers arrive uncounted under a convention, is open.
  Between tiles, which counter a reply uses is agreed by the two programs, a software convention.
- **Follow-on:** 4.2's "*d* = 64 only with mode 3" and "65-125 reserved", invariant 17's mode-3 clauses, and
  6.6-6.7's description of C&C's port and the memory system change.

## 26. Bad packets under the new map (2026-10-28)

From W2-06. Defines `ERRORS` [1], "bad packet" (18).

- **Natural alignment everywhere** (owner and Tony, 2026-10-28): a **128-bit write must be 4-word aligned
  and a 64-bit write 2-word aligned**, in every region. A misaligned write is a bad packet. v1.1 let port B
  (controller memory) and the peripherals take 1, 2 or 4 words at any word address; that goes. Read-port
  results, block copies, and DMA or send-port runs must start aligned to their size. The software is
  believed to align already *(to check: WORKER3's inventory, meshsim)*. **The hardware checks alignment.**
- **The same rule inside big SRAM** (owner, 2026-10-28): **every 64- or 128-bit big-SRAM access, read or
  write, is naturally aligned**. A `PTR` must be aligned to the size of the packets landing through it;
  `STREAM_SRC` and each `GROUP` source to the stream's read size; `DMA_RECORD_BASE_ADDRESS` to a record.
  A misaligned access raises the alignment error (`ERRORS` [1], which becomes "bad packet or misaligned
  access"). **Spanning accesses go away:** no access ever straddles a row, so 4.1's two-part access, 4.9's
  spanning write and their extra cycle are removed (one less variable term in W2-01's write path). v1.1
  allowed any chunk for every width (4.1). A pointer stays aligned only if every packet through it has the
  same size: mixing 32- and 64-bit packets through one `PTR` breaks it.
- **Units** (W2-07): anything that travels in a packet or that the controller reaches through its map is in
  **bytes**; anything inside the tile's engines that points into big SRAM is in **32-bit chunks**
  (`STREAM_SRC`, `PTR`, `SRAM_RD_ADDR_QUEUE`, `DMA_RECORD_BASE_ADDRESS`, `GROUP` sources, the augmented
  ordinal).

- **Out of bounds is not checked in hardware** (Tony, 2026-10-28): the check was dropped to make timing on
  the controller core, with the 32 KB data and program memories. **An offset past the end of a memory is
  undefined behaviour**: the high offset bits are ignored, so the access aliases into the memory. This
  holds for packets and for the core's own loads and stores. Staying in bounds is a program obligation,
  like avoiding size 11.
- **meshsim checks both strictly** (owner): misalignment and out-of-bounds, for packets and for the core's
  loads and stores, stop the simulation with an error naming the access. Both are common bugs in kernels
  of this kind and must be caught in simulation.
- **Auto-advancing past the end is a software bug** (owner, 2026-10-28; W2-08): `STREAM_DEST`,
  `ROUTER_ADDR`, `DMA_ADDR`, `SRAM_RD_DEST_ADDR` and the memory system's deliveries advance by plain
  addition; what happens when a run passes the end of its memory (wrap, alias, or carry into the next
  region) is undefined and not specified. meshsim errors on it, like any out-of-bounds access.
- **The cheap checks stay in hardware:** alignment (above), and the region decode, an 8-bit compare.

| case | hardware |
|---|---|
| a 64- or 128-bit write not aligned to its size | bad packet |
| an offset past the end of its memory | not checked: undefined (aliases); meshsim errors |
| regions 0x07-0xFE, an absent peripheral, region 0xFF, the boot ROM (0x02, read-only) | bad packet *(proposed)* |
| size 11 | undefined, not checked (as v1.1, 3.1); meshsim errors *(proposed)* |
| modes 1-4 | ignore the address, region included (as v1.1) *(proposed)* |
| mode 3 (augmented) at 128 bits | bad packet (as v1.1) *(proposed)* |

## 27. What the hardware checks and what meshsim checks (2026-10-28)

Decided (O), from W2-13; states a principle applied throughout today's decisions.

- **The hardware checks only what is cheap and catches a bug far from its cause:** the overflows,
  unexpected arrival, done early, drift, alignment (26), and the region decode (26).
- **Every other software convention or program obligation is validated in meshsim and ignored by the
  hardware:** out-of-bounds offsets and running past the end of a memory (26), reserved modes (6), the
  reply-block and command-block conventions (6), uncounted traffic being mode 0 (17), and the spec's
  invariants (section 9), which are simulator checks.
- **Data memory word 0 is reserved** (owner, 2026-10-28; W2-23): the linker script never places anything at
  address 0x00000000, and **meshsim flags any write to it**. A C null-pointer store and an all-zero header (a
  mode-0 write at address 0, uncounted, 6) both land there, so one check catches both. Costs 4 bytes.
- **Invariant 11's checkable form** (meshsim only): writes into the command block come only from C&C 1-3;
  writes into the reply block are 32 or 64 bits at the sender's own pair (word 2·src); any other mode-0
  write to regions 0x00 or 0x01 stays inside its memory and outside both blocks.
- **The other invariants W2-13 lists** (4, 5, 8, 9, 10, 13, 14, 17, 18) are rewritten mechanically with the
  spec update.

---

## Big SRAM: to validate

| # | what | source |
|---|---|---|
| S1 | **Every read a program issues in response to a counter's done arrives after the writes it was waiting for** (restated 2026-10-28, W2-01). A write needn't have landed when its counter decrements (K = 8 after the director): the margin is K, plus the controller's reaction (waking from `WFI`, loading `EVENTS`, at least 4 cycles), plus the read's own path through its queues to the area queue. The write path's loaded worst case is about 12 cycles at `ts_engine_latency` 1, more at higher L. Thinnest case: the controller reading `PTR` or `HIST` through the table window right after done (17 A), while the last update may sit in a 16-entry request FIFO. meshsim measures the margin across workloads and flags any read that overtakes the write it waited for | 3, 17, W2-01 |
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
| Q2 | How long a high on the C&C pin counts as "sustained" for the host | 1.5 | **resolved: section 24** (the error pin never pulses) |
| Q3 | What stops the peripheral and the sender when a condition traps | 1.7 | **resolved: section 21** (nothing, until the global reset) |
| Q4 | The HALT design | planning notes section 7 | **resolved: sections 21-23** (no HALT state; the global reset is the only stop) |
| Q5 | Should an area serve writes ahead of queued reads? | 3 | **resolved: no** (owner, 2026-10-28; W2-15): areas keep strict arrival order. Writes jumping ahead of queued reads would break read-before-write reuse: a stream's source buffer overwritten after its run, and the local sort's ping-pong passes, where pass *n* + 1 writes the buffer pass *n* is still reading |
| Q6 | The link word's 32-bit address: what it covers | 2 | **resolved: section 5** |
| Q7 | The 3-bit mode: which of today's nine modes merge or go, now that counting is separate | 2 | **resolved: section 6** |
| Q8 | The counter field in every packet source | 3 | **mostly resolved:** `STREAM` (4), the send port (7), a peripheral's DMA (10), the read port (13), gather values (6, W2-02). **Left:** memory-system requests, the data and reply counters (25, W2-05) |
| Q9 | Does a stream's source stay a 21-bit big-SRAM chunk address, or does the 32-bit address apply to reads too? | 4 | open |
| Q10 | Is the 24-bit offset in bytes or in 32-bit words? | 5 | **resolved: bytes (5)** |
| Q11 | How several peripherals share the slot's interface | 5 | **resolved: sections 9 and 10** |
| Q12 | meshsim's per-tile debug console without a UART on every tile | 9 | **resolved for the hardware: section 23** (log files through the file system); meshsim's display is its own business |
| Q13 | Which peripheral flags only wake the controller and which raise a tile condition | 10 | **resolved: section 10** (conditions interrupt, wake events wake) |
| Q14 | The tile's `EVENTS` bit layout | 11 | **resolved: section 16** |
| Q15 | Should `ROUTER_HDR` use the same upper-half layout as `STREAM_COUNT` and `DMA_HDR`? | 7, 10 | **resolved: section 12** |
