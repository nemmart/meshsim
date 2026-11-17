# Changes asked of the RTL

What the v1.4 design needs changed in hardware the RTL has really built (v1.36). Such a change is written down here
and asked of Tony; it is not made on this side (owner, 2026-11-16). Units Tony pencilled in where v1.35 had a black
box are not listed: for those, this folder's design is the proposal (`V136-SCOPE.md`, section 3).

| # | what | today (v1.36) | wanted | why | status |
|---|---|---|---|---|---|
| 1 | **links between the special tiles** | each special tile has one attach port and no other link (2.1, 16.1) | **master tile to memory tile to tracer tile,** a line, on the special tiles' routers; always flow-controlled; a packet for another special tile goes along the line, everything else by the attach port | the host's stream reaches the Updater without crossing the compute mesh, so it can run during a sort | **agreed in principle** (owner, 2026-11-16: Tony can add links as desired); the ports, routes and flow control to specify |
| 2 | **the UART's registers** | four words of Ingress/Egress's own blocks: `UART_TX` 42, `UART_RX` 43 (a load pops), `UART_DIVISOR` 44, `UART_STATUS` 45 (a store clears the line errors); the divisor resets to 115,200 baud, so the UART runs from the reset (17.6) | **`UART_TX` at word 30 and `UART_RX` at word 31,** in the window's first 32 words. **`UART_CONTROL` at 32 or above,** a plain R/W register: the divider, enable read, enable write; the value 0 holds the UART in reset (FIFOs empty, line errors clear), and it is 0 at the reset. **The FIFO levels and the two line errors in `STATUS`.** `UART_DIVISOR` and `UART_STATUS` go (`INGRESS-INTERFACE.md`, "The UART") | no load of a word from 32 up then has a side effect, on any peripheral; no register at 32 or above has bits that act when written; the enables let the core ignore noise on the line; and a UART nobody polls does not run. The line, the frames, the FIFOs and the two events are unchanged. Every firmware image that prints gains one store, to `UART_CONTROL` | **asked** (owner, 2026-11-17) |

## Comments on units that were pencilled in

Not changes to finished hardware: what this design specifies where the RTL did the least that got the simulation
running.

| # | unit | today (v1.36) | this design | why |
|---|---|---|---|---|
| A | **`lpddr`, the port to the memory controller** | AXI4 with one ID, so no ID signals, and answers in order (3, 17.7) | **multiple IDs,** one for each client of the AXI arbiter | a cache line for the Tracer can come back ahead of a page requested earlier |
| B | **the path to LPDDR** | the RW engine is `lpddr`'s only master | **an AXI arbiter** between the clients (the RW engine, the Updater, the cache) and the memory controller; round robin, a turn one burst of capped length | the Updater and the cache need LPDDR too; the RW engine plugs in unchanged |
| C | **Ingress/Egress's host interface** | two stream ports with FIFOs of 4 beats, read by the core; the beats have no meaning yet (17.6, item 166) | **removed, agreed with Tony (2026-11-17):** `STREAM_OUT_W0`–`W2`, `STREAM_SEND`, `STREAM_SEND_LAST`, `STREAM_IN_W0`–`W2`, `STREAM_RECEIVE`, `STATUS.stream_last`, `EVENTS.stream_in` and `stream_out`. In their place this design's: checked packets on a link of two streams (`HOST-LINK.md`); two incoming queues read by a DMA; the forward count to the Updater; an outgoing queue with a frame count and a credit (`INGRESS-INTERFACE.md`) | the host's frames, the stream to the Updater, and the replies to the host |
| D | **where the Updater sits** | not in the RTL | **the master tile's peripheral,** beside Ingress/Egress (owner, 2026-11-16). Either it sits by the master tile, and its AXI port to the arbiter and its narrow look-up port to the cache reach across to the memory side; or it sits by the arbiter and the cache, and its control port and its feed from Ingress reach across instead | which is cheaper is a floorplan question. Latency on either does not matter: the Updater works in big bursts. The tracer tile and the cache are what should be near the memory controller |
| E | **`fill` has no status for an LPDDR error on a read** | `read_response.status` is 2 bits, and all four values are used | the cache answers such a read `beyond`, so the Tracer sets `ERRORS.page_missing` and stops, and the cache's own error flag says why (`CACHE-INTERFACE.md`, section 5) | nothing to change. A third bit of status would say it directly, if the Tracer is ever opened for another reason |

A and B are specified in `AXI-INTERFACE.md`: section 3 (the arbiter: turns of one piece of 32 beats) and section 4
(the controller's port: a 2-bit ID, one ID a client). Both are built in v1.37's simulator (WORKER5).

## For consideration, not requests

- **The Tracer's jump cache, from 64 entries to 128** (17.9.4). To look at again as more of the design is done
  (second design note, section 5).
