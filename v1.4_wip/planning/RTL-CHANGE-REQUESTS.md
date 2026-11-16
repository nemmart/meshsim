# Changes asked of the RTL

What the v1.4 design needs changed in hardware the RTL has really built (v1.36). Such a change is written down here
and asked of Tony; it is not made on this side (owner, 2026-11-16). Units Tony pencilled in where v1.35 had a black
box are not listed: for those, this folder's design is the proposal (`V136-SCOPE.md`, section 3).

| # | what | today (v1.36) | wanted | why | status |
|---|---|---|---|---|---|
| 1 | **links between the special tiles** | each special tile has one attach port and no other link (2.1, 16.1) | **master tile to memory tile to tracer tile,** a line, on the special tiles' routers; always flow-controlled; a packet for another special tile goes along the line, everything else by the attach port | the host's stream reaches the Updater without crossing the compute mesh, so it can run during a sort | **agreed in principle** (owner, 2026-11-16: Tony can add links as desired); the ports, routes and flow control to specify |

## For consideration, not requests

- **The Tracer's jump cache, from 64 entries to 128** (17.9.4). To look at again as more of the design is done
  (second design note, section 5).
