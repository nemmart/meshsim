# Changes asked of the RTL

What the v1.4 design needs changed in hardware the RTL has really built (v1.36). Such a change is written down here
and asked of Tony; it is not made on this side (owner, 2026-11-16). Units Tony pencilled in where v1.35 had a black
box are not listed: for those, this folder's design is the proposal (`V136-SCOPE.md`, section 3).

| # | what | today (v1.36) | wanted | why | status |
|---|---|---|---|---|---|
| 1 | **links between the special tiles** | each special tile has one attach port and no other link (2.1, 16.1) | **master tile to memory tile to tracer tile,** a line, on the special tiles' routers; always flow-controlled; a packet for another special tile goes along the line, everything else by the attach port | the host's stream reaches the Updater without crossing the compute mesh, so it can run during a sort | **agreed in principle** (owner, 2026-11-16: Tony can add links as desired); the ports, routes and flow control to specify |

## Comments on units that were pencilled in

Not changes to finished hardware: what this design specifies where the RTL did the least that got the simulation
running.

| # | unit | today (v1.36) | this design | why |
|---|---|---|---|---|
| A | **`lpddr`, the port to the memory controller** | AXI4 with one ID, so no ID signals, and answers in order (3, 17.7) | **multiple IDs,** one for each client of the AXI arbiter | a cache line for the Tracer can come back ahead of a page requested earlier |
| B | **the path to LPDDR** | the RW engine is `lpddr`'s only master | **an AXI arbiter** between the clients (the RW engine, the Updater, the cache) and the memory controller; round robin, a turn one burst of capped length | the Updater and the cache need LPDDR too; the RW engine plugs in unchanged |
| C | **Ingress/Egress's host interface** | two stream ports with FIFOs of 4 beats, read by the core; the beats have no meaning yet (17.6, item 166) | this design's to specify in full; set aside for now | the host's frames and the stream to the Updater |
| D | **where the Updater sits** | not in the RTL | **the master tile's peripheral,** beside Ingress/Egress (owner, 2026-11-16). Either it sits by the master tile, and its AXI port to the arbiter and its narrow look-up port to the cache reach across to the memory side; or it sits by the arbiter and the cache, and its control port and its feed from Ingress reach across instead | which is cheaper is a floorplan question. Latency on either does not matter: the Updater works in big bursts. The tracer tile and the cache are what should be near the memory controller |
| E | **`fill` has no status for an LPDDR error on a read** | `read_response.status` is 2 bits, and all four values are used | the cache answers such a read `beyond`, so the Tracer sets `ERRORS.page_missing` and stops, and the cache's own error flag says why (`CACHE-INTERFACE.md`, section 5) | nothing to change. A third bit of status would say it directly, if the Tracer is ever opened for another reason |

## For consideration, not requests

- **The Tracer's jump cache, from 64 entries to 128** (17.9.4). To look at again as more of the design is done
  (second design note, section 5).
