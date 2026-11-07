# A simulation framework for meshsim — design (WORKER29, P2 step 2)

Status: draft for the owner's review, 2026-11-06. Nothing here is built yet; the prototype (section 11) tests it. The
measurements it rests on are in `WORKER29/NOTES.md` (Quests 116, 119, 120 and the local runs).

## 1. What the measurements say

**The main benchmark is the busy replay** (`test/ReplayBench.java`): all 64 tiles replaying their windows at once, as a
proof's sumchecks replay each tile's window about 50 times. On the runner, segment 0 of evm-zone-29 (64 windows, 4.19 M
rows, 236 K replay cycles) runs at **41.7 µs per chip-cycle** on main with A and C (49.7 before). A proof's replay is about
128 segments × 50 replays × 236 K cycles ≈ **1.5 × 10⁹ chip-cycles**: 17.5 hours on one thread today.

Four results shape the design:

1. **A busy cycle is bound by memory latency over scattered objects, not by the units' logic.** Making the buses' commit
   free changed nothing (B): the time moved from `enterSlow` to `enter`, because the cost is the cache miss of reading each
   of the ~2,700 bus objects a busy cycle polls. What did pay was removing per-cycle reaches into other objects (C: the
   overflow counts, the GO queue, the read port's set: −16% on the replay and the sort). Supporting: the Parallel collector,
   which places objects differently, is ~40% slower than G1; transparent huge pages help ~10%.
2. **The state is small but scattered.** A tile is 2,251 objects and 236 KB, 77 buses among them; the chip ~144 K objects
   and ~15 MB, interleaved with large arrays (memories, bitmaps). A busy cycle touches a subset spread over all of it.
3. **The units' own work is a minority.** On the real replay (Quest 116, before C): the replayer 18%, the SRAM read queue
   16%, `TileControl.endCycle` 16.5% (status, events, conditions: reading other units), the controller 9.6%, the router
   7.7%; bus entry alone 17%.
4. **Idle-skipping (WORKER28's stage 1) and the busy cycle are the same problem from two sides**: knowing which units have
   something to do without asking each one. Stage 1 answers it for sleeping tiles with a wake ring; a busy tile still
   polls every input of every unit.

So the framework's job is to make **the data a cycle needs dense and local, and the knowledge of who has work explicit**,
while a unit's author writes ordinary step code against declared ports.

## 2. The model of a cycle

Every cycle t has three phases:

1. **Deliver.** Everything due at t (words put at t − L on a link of latency L, FIFO entries put at t − 1, register writes
   of t − 1, scheduled wakes) becomes visible: the framework moves it into the consumer's input storage and sets the
   consumer's **ready bits** (section 5). Nothing a unit does at t can change what was delivered at t.
2. **Run.** Units with a ready bit set or work pending step, **level by level** (section 4). A unit reads its inputs (what
   was delivered, or a combinational wire written earlier in the same cycle) and its own state, and writes its outputs.
   Outputs other than wires are staged; they cannot be seen during t.
3. **Commit.** Staged register writes and FIFO pushes are applied (with the overflow rule), link words are placed in the
   timing wheel at t + L, and each unit that said it has work left is scheduled.

This is today's convention (a put at t seen at t + L; status computed at t read at t + 1; a FIFO full by its count before
the cycle's pop) made structural instead of being kept by lazy commits and a careful stepping order.

## 3. Port kinds and their exact semantics

A unit declares its ports once; the framework owns their storage. Each kind's semantics is today's bus class's, so the
current engine stays the oracle.

| kind | semantics | today |
|---|---|---|
| `Link(L)` | a put at t is visible from t + L, in order; one put and one take per cycle; bounded (L + 2), the consumer must take | `WordDelayLine` (links 3, intra-tile stages 1, director→port B 2, engine↔table 1) |
| `Fifo(N)` | a put at t visible from t + 1; full is judged by the count at the start of t (before this cycle's pop); a put that doesn't fit is dropped and counted into the owner's event slot | `WordFIFO` / `OverflowFIFO` (router FIFOs 16, write-engine input 4, GATHER 16) |
| `Fifo.credit(N)` | as `Fifo`, but the producer sees start-of-cycle room and a put without room is a bug | `WordBackpressureFIFO`, `BackpressureFIFO` |
| `Hold` | a holding register: a put at t visible from t + 1; if the old word isn't taken at t, the new one is refused and the producer sees `refused` at t + 1 | `WordHold` (injection priority, SRAM read/write holding, the send register) |
| `Reg` | a registered value: a write at t is visible from t + 1 and stays; many readers | `WordLatch`, status words, GO/READY as lines (`Reg` behind a `Link(8)`) |
| `RegFile` | registers with merge rules (core write wins, unit writes OR or masked, W1C, the rule table) committed at the end of t | `RegisterBlock` |
| `Mem(ports)` | a memory macro: a read returns the stored value in the same cycle, writes are applied at the end of t, a clash is counted | `SRAMMacro`, `BankedMemory` (controller memories, table stores) |
| `Wire` | combinational: written by its one producer during t, read during t by consumers at a later level | today's same-cycle direct calls between units |
| `Count` | a monotonic event counter a unit increments; readers see the sum (one word per condition) | the condition counts (C's overflow sinks are this) |

**Producer-side reads** are of start-of-cycle state only: `Fifo.canPut` reads the consumer's count at the start of t,
`Hold.canPut` its start-of-cycle occupancy. They are values of t − 1, so they add no ordering, and they read one word in the
port pool, not the consumer.

**Mapping today's same-cycle couplings** (WORKER28's audit, NOTES.md section 2): each one becomes a port, and the
choice decides the ordering.

| coupling (today: a direct call within the cycle) | port | effect on ordering |
|---|---|---|
| router → token pool (`loopbackLeft`) | `Wire` router → SRAM read queue | the queue after the router (as now) |
| injection point ↔ SRAM read queue (`takeForSend`, `injected`, `loops`) | none: they stay **one unit** (they share the token pool) | — |
| injection point → read port (`injectedAt`); director → read port (`acceptedAt`, `outstanding`) | `Wire`s into the read port | the read port after both |
| director → control (`pendingAdd`) | `Wire` | control at the last level (as now) |
| director → slot (`periph.accept`: a landing) | `Link(rp_latency)` into the peripheral (the landing is already timed) | none |
| gather engine → SRAM read queue (`inFlightOf`) | `Reg` (start-of-cycle count) | none |
| core → control registers, read-port setup, request port | `RegFile` / `Reg` (a store at t acts from t + 1) | none |
| core ↔ controller memories (fetch, load, store; director writes port B) | `Mem(2)` | none (port B's landing is a `Link(2)`) |
| core → send holding register | `Hold` | none |
| `TileControl.endCycle` reads the director's counters, the AG's done, the gather engine's busy, the read port, the request port, the slot's lines | each unit publishes a **status word** (`Wire`, written at its step) into the tile's status array; control reads one array | control at the last level |
| the slot ↔ its peripheral and DMA; the Tracer ↔ its memory system | one unit each (as now) | — |
| links, C&C links, GO, READY, the error lines | `Link(3)`, `Link(1)`, `Link(8)` into `Reg` | none |
| the request bus (runs first, reads and writes every port) | a chip-level unit at level 0 with `Link`s to the ports | — |

The rule a unit's author follows is short: **an input that must be this cycle's value is a `Wire`; anything else is
registered**, and the framework checks the wires form no loop.

## 4. Ordering

The framework builds a graph from the `Wire`s when the chip is built, assigns each unit a **level** (its longest wire
path from a unit with no wire inputs), and rejects a combinational loop with the cycle's units named. Today's fixed child
order (router, read queue and injection, director, …, core; then slot, control) is one valid topological order, which is why
the shuffle seeds have always passed: the framework makes that property explicit and checked.

**Evaluation order:** for each level, for each unit type in the level, for each instance (every tile of every chip in the
batch). This is legal because wires never cross tiles (every cross-tile coupling is a `Link` of 3 cycles or more, or a
line of 8), and it gives the order that suits both the CPU and a GPU: one type's code stays hot while it streams through
that type's state for all tiles. Inside a level, instances and types can run in any order; the debug mode varies it (section
8).

## 5. Wakes: who has work, without asking

Every unit instance has a **ready mask**, a word in a dense array: one bit per input port, set when delivery makes data
visible there, cleared when the unit takes the last of it. A unit's step tests its mask first and touches only the ports
whose bits are set: **an empty input is never read**, which is exactly the cost B found. A unit with no bit set and no work
pending is not stepped at all: idle-skipping for free, at the unit level, inside busy tiles too. A step returns its next
wake: `NONE`, `NEXT` (work in progress), or a cycle (a core's stall, the director's next decrement, the watchdog), which
the framework puts in the wheel.

**The timing wheel** holds what will become visible: per cycle bucket, the (port, word) deliveries and (unit) wakes due.
Every latency is small and bounded (the largest, a memory-system fill, 80), so a ring of 128 buckets with a small heap
beyond suffices, as in WORKER28's stage 1. Delivering bucket t is a sequential walk over a dense list: it writes the words
into the consumer's input storage and ORs the ready bits.

This subsumes stage 1: a sleeping tile is a tile none of whose units has a bit or a wake; the per-cycle counters stage 1
catches up (`mcycle`, `cyclesWfi`, …) are caught up the same way, by the unit, from the cycle it last stepped. WORKER28's
three conditions on any bus become properties of delivery: the wake is the delivery itself (1); a sleep check at the end of
t sees t's staged puts, because staging is explicit (2); queries are pure reads of delivered state (3).

## 6. Data layout: the centre of the design

Everything a busy cycle touches lives in **pools by kind**, allocated once when the chip (or the batch) is built:

- **Ports.** Each kind has one pool: parallel primitive arrays indexed by port id (`count[]`, `head[]`, `countAtStart[]`,
  `refusedAt[]`; the words `a[]`, `b[]`, `c[]` in fixed-size slots per port). Ports are numbered **consumer type-major,
  then instance**, so the inputs of all 64 routers sit together, and a router's inputs sit together.
- **Ready masks and status words**: one `long[]` per type, indexed by instance; the status array per tile is one small
  `int[]` row.
- **Unit state.** In the first form, ordinary Java objects with fields (authoring stays as today), but **allocated
  type-contiguously** when the chip is built (all routers, then all read queues, …), with every large array (big SRAM,
  memories, bitmaps, tables) allocated separately so it never sits between them. The prototype measures how much of the
  gain this form keeps against the second.
- **The second form, for a GPU backend:** unit state as structure-of-arrays, `[type][field][instance]`, with `instance =
  chip × 64 + tile`, so a warp's lanes (adjacent instances) read adjacent words. A unit that opts in writes its state as
  primitive fields and its step as a method over (state, instance); a generator (or a hand lowering) produces the array
  form. Nothing in the first form prevents it: no references in hot state, no allocation in a cycle, steps that touch only
  their own state and their ports.

What it buys, by estimate: a busy tile's hot data shrinks from a few hundred scattered cache lines to a few dense runs (its
ready masks, its status row, the ports with data, its units' state), and the polls disappear. The prototype measures it.

## 7. Threads

**Throughput first: independent chips on independent threads.** A proof's 128 segments are independent ASICs, and each
window's replays are independent runs, so the main lever for "many replays at once" needs no synchronisation at all: N
threads, each its own chip (or its own batch of chips), limited by memory bandwidth, not by barriers. Today's engine can do
this already (TracerT2 gives each segment a thread); the framework makes each chip smaller and denser, so more of them fit
in cache at once.

**Latency second: one chip over several threads.** Partition by tile groups (C&C units with tiles 0-2, their links being
1 cycle); every cross-group coupling is a `Link` of 3 or a line of 8, so the groups need to agree only every 3 cycles: a
barrier every 3 cycles, with cross-group link words exchanged through per-thread outboxes merged at the barrier (no locks
in the cycle). Partitioning by unit type (the owner's "16 cores in a task") is also legal under the type-major order, but
needs a barrier per level, several per cycle; the prototype measures both.

## 8. Debug checks

- **Order shuffle within a level** (types and instances), replacing today's child shuffle; any difference is an undeclared
  dependence, located by bisecting the level.
- **Wire discipline**: a wire read before its producer ran in the cycle, or written twice, throws.
- **Port discipline**: a unit holds only its port handles and its own state; there is no path to another unit's object.
- **Two evaluation orders**: type-major against tile-major, digests compared.
- Today's checks stay: two puts in a cycle, reading what isn't visible, the invariants (as a chip-level unit at the last
  level, run over the units that stepped).

## 9. Authoring

A router today is 222 lines over buses it polls; in the framework its step reads like the hardware: for each output, pick
among the inputs whose ready bits are set, move the head word. A sketch of a FIFO consumer:

```java
final class WriteEngine extends Unit {
    final In weIn = fifo(4);                  // declared once: kind and depth; storage is the framework's
    final Out toSram = hold();
    final Out tsReq = link(1);
    long step(Ctx c) {                         // c.t, c.ready(port), c.peek / take / put
        if (c.ready(weIn) && room()) { accept(c.peek(weIn)); c.take(weIn); }
        if (have() && c.canPut(toSram)) c.put(toSram, ...);
        return busy() ? NEXT : NONE;
    }
}
```

No commits, no stepping order, no `t` threaded through every call, no wake hooks: the declarations carry them. The
prototype counts the lines of a real unit both ways.

## 10. Migration (each step proved against today's engine: identical logs and fingerprints)

1. **M0, the framework core and an adapter.** A whole tile (today's `Tile`) as one framework unit, its links and C&C links
   as `Link` ports, GO/READY/errors as lines. Gives the wheel at tile granularity (stage 1's tile sleeping, structurally)
   and the tile-group threads. The rest of the tile unchanged inside.
2. **M1, inside the tile, unit by unit**, hottest first: the router and its FIFOs, the SRAM read queue and injection point,
   the director, the status array for `TileControl`, then the engines, the core, the slot. Each unit converted is proved
   before the next, as A and C were.
3. **M2, the layout**: type-contiguous allocation, type-major evaluation, large arrays apart.
4. **M3, threads**: independent chips (immediate), then tile groups.
5. **M4 (only if the owner decides): the SoA lowering and a GPU backend.**

WORKER28's P1 work in today's engine (stage 1, threads over tiles) carries on meanwhile; the framework subsumes it at M0 and
M3, and its proofs and fingerprints carry over.

## 11. The prototype (step 3) and how it will be judged

On a scratch branch (`sim-framework-proto`): the framework core (port pools, wheel, levels, scheduler, debug shuffle;
target under ~600 lines) and three units: **a FIFO-fed consumer, the router, and a small core** (today's RV32 interpreter
behind ports), with a traffic source.

- **Exactness:** an 8 × 8 mesh of prototype routers running NetTest's all-to-all traffic delivers every word at the same
  cycle as today's mesh (logs compared).
- **Speed per cycle:** that mesh, busy, against today's engine on the same traffic: µs per cycle single-threaded; then
  128 meshes at once on 1, 8 and 16 threads; and the two unit-state forms of section 6.
- **Authoring cost:** the router's and the consumer's lines and concepts, both ways.

**A GPU assessment** in the report, from the prototype's layout: per-cycle work per type, divergence by type (routers,
FIFOs and queues uniform; the replayer data-dependent, one lane's opcode against another's; the cores the hardest, eased
when a batch runs the same firmware in near lockstep), and what a persistent kernel with a grid sync every 3 cycles would
cost against kernel launches per level.

## 12. Expected speed, with the uncertainty stated

Estimates for the busy replay, to be replaced by the prototype's numbers:

- **Single thread, one chip:** today 41.7 µs per chip-cycle. Polls of empty ports, reaches into other units and scattered
  state are roughly half of today's cycle by the profiles (bus entry 17-25%, control and status 15-17%, the read port's and
  queues' structures several %); removing them and densifying the rest suggests **3-4×, about 10-15 µs**. The units' own
  work (the replayer, the interpreter) sets the floor.
- **Many chips at once:** with independent chips per thread, close to linear in cores until memory bandwidth binds. On
  the 16-thread runner, today's engine gives at best ~1.1 hours for a proof's 1.5 × 10⁹ chip-cycles (17.5 h / 16; more if
  the threads share cores or bandwidth binds: to be measured); the framework, at 3-4× per thread, **about 20-40 minutes**.
- **A GPU** (128 × 64 instances per type): unknown until assessed; the layout keeps it possible.

## 13. Questions for the owner

1. Are a window's ~50 replays identical in control flow (same firmware, same window, different values), or different
   windows? If identical, batching them in lockstep makes a SIMD or GPU backend far easier (no divergence between lanes).
2. Is the first unit-state form (objects allocated type-contiguously) acceptable for authoring, with the SoA form as an
   opt-in lowering for a GPU later? Or should SoA be the only form from the start?
3. Should the prototype measure the independent-chips throughput on today's engine too (one Quest task: 16 ReplayBench
   JVMs at once), so the framework's gain is judged against the best of today, not one thread?
