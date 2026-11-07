# WORKER9 (G2: the network and the chip's assembly): notes

Branch `sim-w9`. The prompt is `PROMPT.md`; the specification is `sim/tests/AUDIT-M2.md` (sections 0, 1.2, 2, 3.1, 3.2's
send side, 4, 5). **Plan approved by the owner, 2026-11-07** (with the answers below).

## The ports with W8 (agreed, owner 2026-11-07; both build to this list)

| port | kind | from -> to | words | latency |
|---|---|---|---|---|
| link word | — | everywhere | 3: payload [63:0], the header as `LinkWord` lays it out (simulator-only bits included), payload [127:64] (D17) | — |
| send | Hold | W8's core -> my injection unit | 3 | (Hold) a refusal is retried at t + 1, the core stalls |
| send-hold status | Reg | my injection unit -> W8's register block | 1: send hold empty at the end of t | (Reg) |
| eject | Link | my router -> W8's director | 3 | `intra_tile_latency` (1) |
| GO | Link, width 0 | my C&C -> each register block | 0 | `go_latency + skew[k] + 1` (the +1: the block's shifted end of cycle) |
| READY, error line | Link, width 1 | each register block -> my row collector | 1 | `ready_latency - 2`, then `Link(1)` collector -> C&C |
| port B | — | W8's alone | | |

Where a latency is in doubt, the boot's fingerprint decides it.

## The plan

**0. Port headers first** (declarations only), published on `sim-w9` for W8: the router, the injection unit, C&C, the
row collector, the endpoints, the tile's external ports (`TileIO`), the chip builder's interface.

**1. The router and the links** (AUDIT 3.1; `Router.java`, `Chip.java`). One unit per tile. Its queues are **inside the
router, exactly as ref**: one 16-deep FIFO per (output, feeder), `FEEDERS_TILE0` on every tile, a put at t visible at
t + 1, full judged by the occupancy at the start of t (`WordFIFO`'s count before pop), an overflow dropped and counted.
In a step: the selectors (one word per output from the FIFOs as at the start of t, round-robin `rr`, the eject
selector's loopback-last), then the route blocks (four links, the loopback from the injection unit with src stamped, and
on tile 0 C&C's from-controller Hold). The to-C&C output is selected only on tiles 0-2 (`attached`). Routing: `unicast`
and `broadcast` tabulated at configuration from `hops.txt` (a read-only table outside the unit). Links are `Link(3)`,
three words; the router takes every arrival. A token-holding packet leaving the loopback FIFO sends `loopbackLeft` to
the injection unit (width-0 `Link(1)`). Overflows go to a `Count` (ROUTER_OVF, read by W8's block `through_last`); eject
and route errors are counted where they happen and reported after the run (D24). Returns `next()` while any FIFO holds a
word. *Later (owner):* W10's Link whose consumer holds up to a declared depth (ref's `WordDelayLine` with capacity
L + depth); when it lands, check with W10 that it reproduces the router's inputs exactly (full judged at the start of the
cycle) and, if so, switch to it (no word held twice).

**2. The injection unit, send side** (AUDIT 3.2; `InjectionPath.java`, the token part of `SRAMReadQueue.java`). The
read return before a send; a send for the tile itself or a broadcast takes a loopback token (`takeForSend`: the count at
the start of t) or waits; `TAG_TOKEN` set; frees from `loopbackLeft` take effect at t + 1. The read side (G3: the SRAM
read queue, the token free list, `readReturn`, `injected`, the read port's `injectedAt`) is a slot in the same unit (its
own member and its own phase in the step, before the injection), so G3 adds it without restructuring. Outputs: the
injection `Link(1)` to the router, the send-hold status `Reg` to W8's block. Invariants 5, 10, 17, 21 counted.

**3. C&C's model and the chip-level lines** (AUDIT 0 and 4; `CnC.java`, `Cnc1Peripheral.java`, `Endpoint.java`). One unit
for `CnC` and C&C 1's peripheral: a step program built at configuration from the harness's sequence (sends, `command`
with its parity and argument block, `loadWords`, `go` with `cnc_reaction` and `cnc_go_settle`, waits, bookkeeping marks),
one register action per cycle, a refused send retried; READY bits as `Cnc1Peripheral` (visible at the edge +
`ready_latency`, cleared by PULSE_GO's store, a pulse arriving in the clearing cycle kept); GO's net as 64 width-0 Links.
**Event-driven (D23):** it steps only when a step acts, a wait ends, or a READY / error event arrives; nothing scans the
tiles. READY and the error lines reach it through **8 row collectors** (one per mesh row): **the ready mask is 64 bits
per unit, so 64 READY plus 64 error inputs cannot be one unit's; M3's larger fan-ins will meet the same limit.** The
request bus is a stub with no steps (the sort sends no request) that writes its empty fingerprint; C&C 2 and 3 are small
drain units on tiles 1 and 2's to-controller outputs.

**4. The chip's assembly.** `tile_cycle(cfg, k)` adds a tile's units (mine, W8's, later G3's and G4's), wires them
inside and returns `TileIO` (four link ins and outs, GO in, the READY/error line out, on tiles 0-2 the fifth port);
`build_chip` adds 64 tiles, links them (`Chip.neighbour`, `arrivalPort`), C&C, the collectors and the endpoints; the start
skews from Java's `Random(skew_seed)` (checked against the `.fp` header). **The partition by tile groups** (form 2): a
domain per tile from a function (default: bands of rows; W10's load-balanced partitioner can plug in); C&C and the
collectors in tile 0's domain (its from-controller Hold must not cross), endpoint k in tile k's; every crossing a Link
of latency >= 3, asserted from `Sim::partition()` (interval 3). **The driver**: the images (`golden-m2/images`), the
C&C sequences of `Mach.boot`, `M2Prog` and `M2Sort`, the input-load stand-in, the run to the stop condition, the
fingerprints through W8's library against the golden `.fp` (its D12 header checked first).

**5. Checks**, each on one thread and on form 2 (2, 4, 8 domains, several threads): unit tests (routes, the broadcast
tree: one copy per tile, none to C&C; unicast latency 3 + 4h); the boot's network part (tile 0's router and the chip
units every cycle against `regs.fp` / `wfi.fp`, an eject sink standing in until W8's director exists); with W8: the whole
boot, `wfi`'s self-sends, then **`regs` and `wfi` with every unit of tile 0 every cycle**, and the whole chip every 10
cycles (`regs-chip`, `wfi-chip`, regenerated locally); a2a's network part once G3 lands; NetTest last.

## The owner's answers (2026-11-07)

- **Q1** JDK and clang: install them (apt) in the container.
- **Q2** NetTest: ref's per-chip digests can't match by construction (they hash ref's Java objects, AUDIT-M2 6.1).
  Compare its log facts exactly (latencies, delivery counts, queue peaks), plus the canonical fingerprint where it
  applies. Last, and optional if a2a and the sort cover the network.
- **Q3** The stop condition: the driver polls between cycles until W10's stop request lands (a unit asks the run to stop
  at the end of the cycle, exactly).
- The router's queues: inside the router as ref first; W10's held-depth Link later (above).
- The row collectors: fine; the mask's width forces them (above).

## Log

- 2026-11-07: read the prompt's list; plan proposed; approved. D27 (model ref's default order) noted: none of my units'
  couplings depends on the child order (Holds and Links only; READY and GO are registered).
- 2026-11-07: **stage 0, the port headers** (declarations; `tests/test_net_headers.cpp` checks they compile and meet the
  unit rules on gcc and clang): `src/units/net.h` (the link word as `LinkWord`, `NetConfig`, `Hops`, the mesh's
  neighbours; shared read-only, W8's director can use the header accessors), `router.h`, `inject.h` (the read side as
  G3's member `InjReadSide`), `cnc.h` (C&C, `CncProgram`, the row collectors, the endpoints), `chip.h` (`ControlIO`: what
  W8's tile part returns to the tile builder; `TileIO`, `ChipSpec`, `build_chip`, `row_bands`). JDK 21 and clang 18
  installed (Q1).
  **For W8, through the integrator** (details in the headers):
  1. *send-busy.* ref's STATUS bit is `!sendHold.isEmpty(t)` at endCycle(t), and `WordHold.isEmpty` is `!occ &&
     !pendPut`: occupied at the end of t **or a put made at t**. My Reg carries the first (I see the hold); the put of t
     is the core's (a send store or a retry), so W8's block ORs it in. Same Reg, just the exact meaning written down.
  2. *ROUTER_OVF.* `ControlIO.router_ovf` is a `CountIn` the router increments per FIFO overflow (TileControl reads the
     sum of every router FIFO's `overflowCount`). Not in the agreed list (it never fires in M2, but ERRORS reads it):
     proposed as an addition.
  3. *How W8's part plugs in.* `tile_cycle` calls a `ControlBuilder` (W8's function) that adds W8's units for tile k and
     returns `ControlIO`; G3/G4 the same way later.
- C&C details found while writing `cnc.h`: SortTest's `atAllReady` is a `waitUntil` step (one cycle of its own) then an
  instant call, so `go` after it sees all READY one cycle later: the program keeps ref's step granularity, since the
  queue's length is in the fingerprint. The sort's commands are queued after the boot has run (SortTest's boot ends on
  a harness condition): release points in the program; with nothing released and more to come, C&C steps every cycle
  doing nothing (exact, a few cycles), so no external wake is needed.
