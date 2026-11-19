# Release note: v1.37, for the RTL

*Written 2026-11-19, with the freeze, for Tony. It is the one page to read first: what v1.37 asks of the RTL, what
it asks you to tell us, what it leaves to you, and what is only for your information. It changes nothing in the
frozen folder; the specification is the authority, and each line below names its section.*

**What v1.37 is.** v1.36 with the full system round it, the first end-to-end path: the host's packets come in
over the link, the master tile's firmware reads the frames and drives the Updater, the Updater builds the memory
images in LPDDR through the cache and the AXI arbiter, the Tracer traces on them, the compute tiles replay, and
the reply goes back up the link. It passes at 2²⁹ on both workloads, as one chip and as a chain of two.

**Where things are.** The specification is `spec/machine-spec.md` (the router, chapter 16; Ingress/Egress, 17.6;
the Updater, 17.10; the memory system, 17.11 and chapter 23), `spec/host-link.md` (chapter 28),
`spec/host-frames.md` (29) and `spec/master-firmware.md` (30). What changed since v1.36, section by section, is
`spec/CHANGES-v1.36-to-v1.37.md`. The requests below are kept, with their history, in `RTL-CHANGE-REQUESTS.md`.

## 1. Asked of you: changes to hardware you have built

| # | what | today (v1.36) | asked | where |
|---|---|---|---|---|
| 1 | **links between the special tiles** | each special tile has its attach port and no other link | a line, master tile ↔ memory tile ↔ tracer tile, on the special tiles' routers, **on east and west as in the mesh:** the special tiles are 64, 65 and 66, east is id + 1 and west is id − 1. No link wraps round from the tracer tile to the master tile: there is little traffic between those two. The line's links are the mesh's, with **no flow control** | 16.1, 16.2; RCR 1 |
| 2 | **the UART's registers** | `UART_TX` 42, `UART_RX` 43, `UART_DIVISOR` 44, `UART_STATUS` 45; running from the reset | `UART_TX` at word 30 and `UART_RX` at 31; **`UART_CONTROL` at word 42,** a plain register (the divider and two enables; 0 holds the UART in reset, and it is 0 at the reset); the FIFO levels and the two line errors in `STATUS`. The rule behind it: **a word from 32 up is a real register,** not the end of a queue, so no load there has a side effect. The line, the characters and the FIFOs are unchanged | 17.6.9; RCR 2 |
| 3 | **a special tile's peripheral DMA** | enters the router on the west input (the memory and tracer tiles) | on the **south input, the +8 port,** on every special tile. East and west now carry request 1's line | 16.4; RCR 3 |
| 4 | **the memory tile's peripherals** | the RW engine is peripheral 1 | **the memory system (the cache, the AXI arbiter, the `lpddr` port) is peripheral 1,** type 6, with the least a peripheral can have: it is there so that the cache's error reaches the tile's error line. **The RW engine becomes peripheral 2.** We understand the RW engine is to be redesigned in full, so its move comes with that and is not asked for on its own | 17.11, 17.7; RCR 4 |
| 5 | **an assertion on `fill`** | the port is v1.36's, unchanged | an assertion in the RTL that the Tracer never holds a request channel's `valid` a cycle past its handshake. Every cycle with `valid` and `ready` both 1 is one request; a second is served, its response reaches nobody, and nothing in the cache can tell. A check on logic you have built, not a change to it | 23.6.4, 17.9.5 |

## 2. A question for you: the port to the LPDDR controller

We do not know what the AXI interface to the memory controller will look like, and have specified what the
design needs (23.12, 23.10). **Tell us where the controller you expect differs.**

What the specification assumes of `lpddr`:

- AXI4, 128-bit data, INCR bursts of 16-byte beats, none crossing a 4 KiB boundary;
- **IDs, 2 bits,** one for each client of the arbiter (v1.36's port has one ID): the reads of one ID answered in
  order and its writes in order, and any order between IDs, so that a line for the Tracer can come back ahead of
  a page asked for earlier;
- a burst on `lpddr` of at most 32 beats (the arbiter cuts a client's longer burst into turns);
- every burst answered in bounded time; no write response before LPDDR holds the write.

What we would like to know: whether the controller has IDs and how many; its longest burst; its latency for a
read and for a write; how it reports an error.

## 3. Yours to settle

Nothing here needs an answer before you build. Tell us what you chose, and the specification's figures follow.

- **The link's clock** (open item 186). The link ends count in link cycles, a byte on the eight LVDS lines each
  cycle: the 1,024 zeros, the status timer, the waits. The specification does not say how that clock relates to
  the chip's, and in the simulator they are the same. Whether it is the chip's clock, a multiple of it or
  recovered from the line, where the crossing sits, and the cycle counts that follow are the RTL's and the back
  end's.
- **The header's CRC while the fast unit is busy** (28.13.4). A link packet of no data may start while the fast
  CRC unit is passing over the next packet's data. The requirement is only that `header_crc` is on the line by
  the header's byte 12. Whether the pass waits a cycle or the header's CRC has logic of its own is yours.
- **Where the Updater sits** (17.10; RCR D). It is the master tile's peripheral 2. Either it sits by the master
  tile, and its AXI port and its look-up port reach across the chip to the arbiter and the cache; or it sits by
  them, and its control port and its feed from Ingress reach across. The owner's guess, and no more than a
  guess, is that it will work best close to the master tile, with the AXI port and the cache's request bus
  running across. Latency on either does not matter much: the Updater works in big bursts.
- **Register stages on the long runs.** The specification has none on the Updater's AXI port or its look-up
  port. Put in what the floorplan needs and tell us how many: the cycle figures for those paths will be redone.

## 4. For your information

- **Units specified here that the RTL does not have.** Ingress/Egress with the host link (17.6, chapter 28; the
  link end's registers and state machines are also in `link/HARDWARE.md`), the Updater (17.10), the cache's
  controller (23.6) and the AXI arbiter (23.11). The cache's controller, the Updater's controller and the link
  end are state machines in the simulator, and the specification mirrors them register for register. The
  arbiter you know is coming: the RW engine plugs into it unchanged, as client 2.
- **The host interface of v1.36 is gone,** as agreed on 2026-11-17 (RCR C): the two stream ports and their words.
  In their place are the host link, two incoming queues read by a DMA, and an outgoing queue (17.6).
- **The pins, and their names.** In place of `lvds_in` and `lvds_out` an ASIC has two links, each a byte in and
  a byte out a link cycle, and one strap. The specification calls them `link_up`, `link_down` and `chain_next`.
  **From the next version they are `lvds_parent`** (towards the FPGA), **`lvds_child`** (to the next ASIC on the
  chain) **and `chain_end`** (owner). `chain_end` has the opposite sense to `chain_next`: 1 says this ASIC is
  the last, so its child link is unused and nothing is forwarded down. Please use the new names.
- **One reset net.** v1.37 adds no second reset. Everything after an error stops the chip is not designed (open
  item 181).
- **Where the simulator's cycles no longer match your tests** (21.5). Two places, both from the requests above:
  the RW engine, now behind the arbiter, whose bursts reach `lpddr` a cycle later and are answered by our LPDDR
  model; and a special tile's peripheral DMA, on south where the RTL has it on west. Everything else is still
  cycle for cycle. Nothing is asked: when the RTL has the arbiter and the DMA on south, send us the tests and we
  will match them again.
- **A preliminary sizing of the state machines and control circuits of the cache and the Updater** is in
  `CONTROLLER-SIZING.md` (in `v1.4_wip/planning/`), with the full count in
  `planning/updater/sessions/SIZING/REPORT.md`. In short: the cache's controller is 12 states, a 110-bit control
  word, 6,652 flip-flops, 41 to 84 kGE; the Updater's controller 18 states, 95 bits, 4,193 flip-flops, 42 to
  59 kGE; the arbiter 1,539 flip-flops, 10 to 17 kGE. Each controller is about one and a half cores of logic.
  These are estimates from the simulator's code, not synthesis; yours will replace them.
- **What is coming next,** so that room can be kept for it (`TO-ADD.md`): twelve statistics counters for each
  link in Ingress/Egress's window, the parent link's at words 96 to 107 and the child link's at 112 to 123; a
  change to `WATCHDOG`, so that running out raises an error in `ERRORS`; and the next project, the RW peripheral,
  with two regimes for a full router queue on a compute tile, an error or flow control.
- **What is open** in the design is chapter 26 of the specification and 28.17 and 29.16 of the link's and the
  frames'; what the simulator owes is `OPEN-v1.37.md`.
