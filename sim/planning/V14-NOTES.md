# Notes for v1.4

Designs agreed during v1.35 for a later version. Nothing here is in v1.35.

## Invariant 18 for big SRAM: a check (owner, docs session, integrator, 2026-11-12)

Invariant 18: a unit never has two clients (the address generator, the GATHER engine, the send port) sending
order-placed packets (modes 1-3) into the same destination at once; breaking it gives a silently interleaved buffer.
v1.3 and v1.35 check it only for a peripheral's queue ports (`V_TWO_SOURCES`), not for big SRAM (D48).

**The hazard is in the sending tile.** Big SRAM returns each client's reads in that client's order, but reads of
different clients may complete in any order (spec 4.5), so two clients with order-placed reads in flight together can
inject their returns in an order the RTL may choose differently from meshsim. Once injected, in-order delivery per
(source, destination) (fixed routes, FIFOs never reorder) means nothing downstream reorders them. So the check is at
the sender, local to the tile, with no packet tag and nothing from other tiles. (A receiver-side check with a
client/run tag was considered and rejected: it sees only the order meshsim produced; receiver counters are shared by
many senders, so they don't bound one client's stream.)

**The agreed check (the spec's rule: two clients never run at once into the same destination, widened because RTL timing
differs from meshsim's by a few cycles, so a near miss in meshsim can be an overlap in RTL):**
- per client, keep the set of destinations its current run has sent order-placed packets to, cleared when a run starts;
- report invariant 18 when client B issues an order-placed read (or the send port sends an order-placed packet) for
  destination d while client A's run is still active (the address generator running, or the GATHER engine busy, or
  either with returns outstanding) and A's set contains d.
A program that sequences the two clients by a done event is never flagged; one that relies on timing is.

**Cost:** per tile, a small per-client destination set (64 tiles: a bitmask), updated at each order-placed issue and at
run start; compiled in only under `SIM_CHECKS` (the suite and debug builds); release unaffected. **To confirm when
building:** every order-placed packet from these clients to one destination goes as one kind (unicast); the spec's
in-order rule holds only within one kind.
