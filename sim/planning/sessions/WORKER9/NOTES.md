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
- 2026-11-07, owner: stage 0 good; make check build by build before any merge. The three W8 points passed on
  (ROUTER_OVF as a `CountIn` is fine: same tile, same domain; the block reads it `through_last`). **No polling for the
  release (D23)**: find the cycle ref releases at. Found: SortTest's boot ends at the first cycle with C&C idle and
  every core in WFI (`allWaiting`), a harness condition on state that no line reports, so the driver polls it between
  cycles (as ref) and then wakes C&C for the next cycle: **a between-cycles wake asked of W10 through the owner**
  (`cnc.h` updated; C&C never steps while waiting). M2Prog's runs queue everything up front: no release.
- make check before stage 1 (stage 0's tree): gcc and clang, debug and release, 24 binaries each (252 / 221 tests
  passed), TSan (threads 39, units 2) on both: clean.
- 2026-11-07: **stage 1, the router** (`router.h/.cpp`, `net.cpp`, `net.mk`, one `include` line in `sim/Makefile`).
  Checked against ref's `Router` itself, not only by reasoning:
  - **Routing**: `route_unicast`/`route_broadcast` for every (tile, src 0-127, dst 0-127) and (tile, port, src), hashed
    per tile, equal to ref's (`tests/routerconf/tables.txt` from `RouterConf table`); the broadcast tree from every
    source reaches every tile exactly once and never a controller.
  - **Conformance, cycle by cycle** (`tests/ref/RouterConf.java`, `routerconf.sh`, `tests/test_router.cpp`): one of
    ref's routers alone, driven by a seeded script (four links, the injection, C&C's hold; rates in phases so FIFOs
    fill and overflow; every destination kind, random modes and simulator-only bits), observing every output word,
    every `loopbackLeft`, and **the router's canonical stream (rr and every FIFO, StateFp's encoding) every cycle**,
    the overflow total, route and eject errors. The C++ router on the engine gives identical lines: 5 fixtures (tiles
    0, 1, 5, 27, 63; 2,000 cycles) in make check, also under shuffle, instance-major and step-all; and 15 runs of
    10^5 cycles (seeds 1-3) locally (`ROUTERCONF_LONG=DIR test_router long`). `routerconf.sh --check` regenerates the
    fixtures from ref.
  - **A finding (for the owner): when ref counts a router FIFO's overflow.** ref judges a put at the FIFO's first touch
    after it (`Bus.enter`, lazy): for a router FIFO that is when its selector next *reaches* it (the selector stops at
    the first ready FIFO in round-robin order), when a put lands in it, or when idle-skip's quiescence scan reads it.
    So `overflowCount`, which `TileControl.endCycle` reads into ERRORS' ROUTER_OVF, can rise one or more cycles after
    t + 1, depending on the round-robin position and on idle-skip. PORTS.md 3 assumes every FIFO is touched every
    cycle (true of the FIFOs it lists, not of the router's). The router counts it **at t + 1** (PORTS.md 3's rule: ref
    with every FIFO touched each cycle; the Count incremented in the step of t + 1, so endCycle(t + 1) is the first to
    see it; tested). No M2 program overflows a router FIFO (AUDIT-M2 1.1), and the fingerprint has no counts, so no
    reference is affected. To record for the spec (D27's list), unless the owner wants ref's lazy timing reproduced.
  - make check after stage 1: gcc and clang, debug and release, 25 binaries (257 / 226 tests), TSan (41 + 2): clean.
- 2026-11-07, owner: stage 1 merged. **D28** (ref's lazy overflow count isn't self-consistent: the router counts at
  t + 1, PORTS.md's rule; references avoid the case), cited in `router.h`. **The release**: the driver's check between
  cycles, then a wake of C&C; W10's between-cycles wake (the driver names a unit, stepped at the next cycle in its
  domain) is asked by the owner. **Until it is on main, the driver keeps its own check** and the sort's release waits
  for it (nothing before the sort needs it). **W10's holding Link** (PORTS.md 2b on sim-w10) is one in-order queue per
  input, head-only take, fatal when full; ref's router takes every arrival at once into per-(output, feeder) FIFOs that
  drop when full: not the same shape, so the router keeps its own FIFOs (W4's "words held twice" stays a speed item).
  W10's stop request (`Ctx::stop`) is for the sort's timing runs, once on main.
- W8's branch (read, not merged): `linkword.h` extends `net.h`'s `lw`; `add_g1` is a `ControlBuilder` returning
  `ControlIO`; the fingerprint library `tests/statefp.h` (`FpRegistry`: one writer per unit name, `FpCtx` with the
  `Sim` for the port observer). My units will register writers there (the router's own part is `Router::fp_own`).
- 2026-11-07: **stage 2, the injection unit's send side and the loopback tokens** (`inject.h/.cpp`). The step:
  `loop_left`'s free of t - 1 applied, the start-of-cycle count fixed (`SRAMReadQueue.advance`), G3's phase, then a
  read return before a send; a send for the tile itself or a broadcast takes a token if `start + taken < cap` (else it
  waits, woken by the router's next `loop_left`), `TAG_TOKEN` set; InjectionPath's invariants counted; then STATUS's
  send-busy Reg (put only when it changes). **The token pool is a `Tokens` struct** shared with G3's `InjReadSide`
  (reads, loopback-held, start, taken): G3's issue takes a token from the same count, its `injected` frees a read's
  token (or moves it to the loopback), so G3 adds the read side without restructuring. A send that loses to a read
  return is judged again next cycle (`next()`); a token wait sleeps (no polling) and has its own missed-wake threshold
  in the tile builder, since the free it waits for may not be on its way yet (the loopback FIFO drains when the eject
  selector reaches it).
  **Checked against ref**: `RouterConf ... send` puts ref's InjectionPath with SRAMReadQueue's tokens behind the router
  (Tile's order: the router, then the read queue and the injection point), fed by a core that retries a refused send
  and stalls; the observation adds STATUS's send-busy (`!sendHold.isEmpty(t)`, so the core's put of t included, as
  agreed with W8), StateFp's "inj" stream (both holds), the tokens held as at the start of t + 1, token waits, the
  send hold's refusals, the violations. Tokens run out in these scripts (64 held, hundreds of waits and refusals).
  Equal to ref every cycle: 3 new fixtures (tiles 0, 5, 63) in make check with the router's 5, also under shuffle,
  instance-major and step-all; 24 runs of 10^5 cycles locally. One statistic needed care: ref counts a token wait every
  cycle the send waits, while the unit sleeps through it; it keeps when the wait began and counts the cycles when it
  ends (`token_waits(t)` is ref's count at the end of t).
- make check after stage 2: gcc and clang, debug and release (25 binaries; 257 / 226 tests), TSan (41 + 2): clean.
  (The whole make check in one call exceeds the tool's 5-minute limit here: run build by build.)
- 2026-11-07: **stage 3, C&C's model, the row collectors, the endpoints** (`cnc.h/.cpp`), and **G2's chip-level
  fingerprint writers** (`tests/statefp_g2.*`: "cnc", "cc1p", "reqbus", "ep", registered in W8's `FpRegistry`).
  C&C is one unit (CnC with Cnc1Peripheral), its program built at configuration with ref's step granularity (sends one
  packet a cycle with the refused one retried, commands with their parity, GO with cnc_reaction and the settle, waits,
  SortTest's atAllReady, calls), READY bits as Cnc1Peripheral keeps them (cleared by PULSE_GO, a pulse arriving the
  next cycle kept), the tiles' error lines, their OR and the error bit, the receive FIFO. Event-driven (D23): stepped
  only when a step acts, a wait ends, a READY or error word arrives, a send is refused, a packet reaches its FIFO.
  Line words as W8 puts them (bit 0 READY rose, bit 1 the level); the row collectors forward a row word (edges,
  present, levels). The writers rebuild ref's pending queues (readyAt pending, errChanges, CnC.errLine) from the line
  words in flight (W10's observer) and, for the end of cycle c itself, from `G2Chip::edge` (the block's preview).
  **Checked against ref** (`tests/ref/CncConf.java`, `cncconf.sh`, `tests/test_cnc.cpp`): ref's CnC, peripheral and
  endpoint in a real Chip whose tiles the driver plays (READY at boot and answering every GO with seeded delays, error
  lines up and down, packets into the receive FIFO, tile 0's router stalling one cycle in six); every cycle the packet
  taken, GO, the calls, and the "cnc" and "cc1p" streams are equal: 3 fixtures (6,000 cycles; 37-42 GOs, 9-24
  refusals, 40 receive packets, the error bit set, every step done; C&C stepped ~1,600 of 6,000 cycles), also under
  shuffle, instance-major and step-all; "ep" and "reqbus" at rest equal.
  **Found:** a second READY pulse while a tile's first is visible (not cleared) makes ref's bit read 0 until the second
  lands (readyAt is one cycle: max(old, edge + ready_latency)); C&C can't know of it before it lands (a shorter link
  would break form 2). The model keeps the spec's sticky bit and counts these (`ready_repulses`); the generator makes
  none. For the owner.
- make check after stage 3: gcc and clang, debug and release (33 binaries; 303 / 272 tests), TSan (60 + 2): clean.
