# sim/src: the port kinds and their exact semantics

WORKER3, stage 1 (draft for review). Every rule below is `ref`'s (`sim/ref/meshsim`, v1.3, frozen): the class and lines it
comes from, then how the framework realises it with its cycle (`sim.h`). Where WORKER29's draft
(`wip/planning/sessions/WORKER29-DESIGN.md` section 3) or a generic class differs from **the class the machine actually
runs**, that class wins and the difference is stated. Paths below are relative to `sim/ref/meshsim/src/`.

## 0. Which classes the machine runs

Every `new` of a bus class in `src/` (grep, 2026-11-07):

| class | instantiated by the machine | used for |
|---|---|---|
| `WordDelayLine` | yes | the mesh links (`Chip.java:78`), the C&C links (`CcCores.java:48-49`), the intra-tile stages (`Tile.java:69-71`, `82-84`), the big SRAM's read returns (`BigSRAM.java:63`) |
| `WordFIFO` | yes | the router's FIFOs (`Router.java:85`), `we-in`, `gather-q` (`Tile.java:72-73`), the read-queue clients (`Tile.java:94`), the read port (`ReadPort.java:32`), the table stores (`TableStore.java:110`, `176`), the fifth port and host FIFOs (`Endpoint.java:22`, `CcCores.java:50`, `58`) |
| `WordHold` | yes | the send register, the read return, the AG's output, the SRAM read and write holds, the gather output (`Tile.java:67-98`), the fifth port's send (`Endpoint.java:21`) |
| `WordLatch`, `WordBackpressureFIFO`, `WordBackpressureDelayLine` | **no** (`test/BusTest.java` only) | — |
| `Latch`, `DelayLine`, `TimedFIFO`/`OverflowFIFO`/`BackpressureFIFO` (generic, `Buses.java`) | **no** (`BusTest`, the replayer's tests) | — |
| `RegisterBlock` | **no** (`BusTest` only) | `TileControl` keeps its registers as fields with its own timing (section 6) |

So `Link`, `Fifo` and `Hold` have a class to follow; `Reg`, `RegFile`, `Wire` and `Count` are derived from the timing rules
`TileControl`, `EventCounts`, `RequestPort` and `Chip` implement by hand (sections 5-8). `BusTest.java` exercises every
class; the conformance driver (section 10) drives the three the machine runs, and `WordLatch` for `Reg`.

## 1. What every bus shares (`Bus.java`)

- **Time** (`Bus.java:38-49`): a bus settles (commits the previous cycle's put) on its first touch at a later cycle; calls
  within a cycle may come in any order. Equivalent to committing at the end of the cycle, since nothing observes the bus in
  between. **Framework:** a put at t is an entry in the delivery bucket of its arrival cycle; nothing is committed lazily.
- **At most one put and one take per bus per cycle** (`Bus.java:51-60`): two of either is a simulator bug (throws).
  **Framework:** fatal in every build for a put (one predicted branch per put, on the port's last-put cycle); a second take
  is a DCHECK (debug), since only the consumer takes.
- **Reading what isn't visible throws** (each class's `check`). **Framework:** DCHECK; a unit tests `ready` first.

## 2. `Link(L)`: `WordDelayLine` (`WordBuses.java:40-74`)

| | rule | lines |
|---|---|---|
| put | `canPut` always true; a put at t is visible from **t + L** (L ≥ 1), in order | 60-61 |
| visible | `ready(t)`: the head's arrival ≤ t; `peek` the head; `take` removes it | 62-72 |
| capacity | L + 2 entries in flight or visible (default); exceeding it throws: **the consumer must take** each word as it arrives (a link delivers one word per cycle) | 44-58 |
| side query | `inFlight(t)`: committed + pending; used only by quiescence checks between cycles (`Chip.java:53`, `Tile.java:216`, `250`) | 73 |

**Framework.** The put appends `{port, words}` to bucket t + L; delivery at t + L appends the words to the port's visible
ring (depth L + 2 by default, or as declared) and sets the consumer's ready bit for the port. In flight and visible are
counted together against the capacity, fatal when exceeded (`link 'name' full: capacity 5`), as `ref` throws.
`in_flight` is an observer query (configuration and tests), not a unit operation.

WORKER29 agrees, except that its "one take per cycle" needs the take check above, and its "bounded (L + 2)" is a default
that `WordDelayLine(name, L, capacity)` lets a caller change (no caller does).

### 2a. The reserved `Link`: `WordBackpressureDelayLine` (`WordBuses.java:237-269`), not in WORKER29's set

A delay line whose producer reserves the landing before it issues: `canPut(t)` = entries committed (in flight or visible)
plus this cycle's takes **below the capacity**, judged at the start of t (257); a put without room throws (258-261);
visibility as `Link(L)`. Not instantiated in v1.3 (the memory port's landing in M2 is its use). **Framework:** `Link(L)`
with a producer-side credit count (section 4's mechanism: a take returns a credit visible at t + 1). Documented now;
implemented in stage 2 only because it is the credit `Fifo`'s mechanism on a `Link` (cheap), otherwise left for M2.

## 3. `Fifo(N)`: `WordFIFO` (`WordBuses.java:81-139`)

| | rule | lines |
|---|---|---|
| put | `canPut` always true (no backpressure); a put at t is visible from **t + 1** | 113-114 |
| full | judged at the settle **against the occupancy at the start of t**: `count + takes ≥ N` (`countBeforePop`, the default since V1.0 item 5). A FIFO full at the start of t refuses t's put **even if it is popped in t** | 100-104 |
| overflow | the put is dropped; `overflowCount++`; sticky `overflow` with the cycle of the first (`overflowCycle`) | 104-107 |
| visible | `ready(t)`: count > 0; `peek` the head; `take` pops it; a take is seen at once by the consumer (count is decremented) | 115-126 |
| side queries | `size(t)` (after this cycle's takes), `sizeAtStart(t)` = count + takes (used: `TableStore.java:124`, `187`); `peekAAt` (unused); `occupancyNow`, `isEmpty` (between cycles, quiescence); `clear()` (a unit held in reset, 13a) | 129-138 |

**Divergence (the class the machine runs wins).** The generic `TimedFIFO` (`Buses.java:79-130`, its `commit` at 96-101) judges full **after**
the cycle's pop (`count == capacity` at the settle); `WordFIFO` with `fifo.afterPop` unset judges it **before**. The rule is
`WordFIFO`'s: full by the count at the start of t. WORKER29 states it correctly.

**When the overflow is counted.** `ref` judges it at the settle, which is the FIFO's first touch after t (lazy). Every FIFO
the machine runs is touched by its consumer every cycle it has entries or a put arrives (a put wakes the consumer,
`WordBuses.java:114`, and the consumer polls `ready`), so the overflow is counted at **t + 1**, before `TileControl.endCycle
(t + 1)` reads the counts (`Tile.java:139-147`). **Framework:** delivery at t + 1 judges the put of t and counts the
overflow at t + 1. The conformance driver touches every port every cycle, so the comparison checks exactly this.

**Framework.** The put appends to bucket t + 1. Delivery at t + 1 judges full as `count + (last_take == t) ≥ N`, where
`count` is the occupancy after t's takes and `last_take` the cycle of the FIFO's last take: the occupancy at the start of t,
with no per-cycle reset of any counter. A refused put increments the FIFO's overflow `Count` (section 8) and its first
cycle; an accepted one is pushed and sets the consumer's ready bit. `size()` and `size_at_start()` (= count + (last_take ==
t)) are the consumer's queries; the FIFO's sticky overflow and first cycle are observer queries.

## 4. Credit `Fifo(N)`

**What `ref` has.** Two credited shapes, neither of them WORKER29's mapping:
- `WordBackpressureFIFO` (`WordBuses.java:194-230`): `canPut(t)` = start-of-cycle occupancy below N (`count + takes < N`,
  215); a put without room throws (216-219); overflowing at the settle throws (208-213). **Not instantiated** (its own
  comment: "none in the v3.4 design; kept, tested"). WORKER29 maps the credit `Fifo` to it.
- **The request tree's receive queues** (`RequestBus.java:64-88`): the root forwards a request only with a credit for its
  target; a receive pops the head at t and returns the credit at **t + 1** (`returnCredit`, 87; credits applied at the
  start of `runCycle`, 89-90). This is the machine's credit FIFO (M2).

Both are the same rule seen from the producer: **room as of the start of t; a take at t frees room from t + 1; a put
without room is a bug.** **Framework:** a credit `Fifo` keeps a credit count on the producer's side, initialised to N; a
put spends one (fatal at 0); a take returns one through the wheel at t + 1 (an entry for the producer's credit port, which
also wakes it). The producer never reads the consumer's state. Visibility, order and the consumer's view are `Fifo(N)`'s;
overflow cannot happen.

### 4a. The request FIFO: full judged after a same-cycle pop (`RequestPort.java:25-31`, `Chip.java:215`)

`canPush(t)` = `size < depth || freedAt == t`: a `REQUEST_SEND` store at t completes if the FIFO has room **or the request
bus freed an entry in t**, and the bus runs first in the chip's cycle ("a request store at t sees the entry it frees",
`Chip.java:215`). That is the after-pop rule, made order-dependent on purpose. **Framework:** not a FIFO rule but a `Wire`:
the request bus (a chip-level unit at a lower level) drives "freed this cycle", the controller reads it. In M1 the test bench
drives that wire from `ref`'s trace (PLAN-M1 item 8). WORKER29's table has no entry for it.

## 5. `Hold`: `WordHold` (`WordBuses.java:153-186`)

| | rule | lines |
|---|---|---|
| put | a put at t is visible from **t + 1** | 168 |
| refusal | judged at the settle: if a word is still held at the end of t (nobody took the old one at t), t's put is **refused**: `refusedAt = t`, `refusals++`; not an error | 162-165 |
| producer | `refused(t)`: the put of t - 1 was refused; the producer presents the same word again (`retry`, 172-175) | 170-175 |
| `canPut(t)` | **empty at the start of t**: `!occ && takes == 0` (order independent; "not busy") | 167 |
| visible | `ready(t)`: occupied; `take` empties it at once | 176, 185 |
| side queries | `heldAtStart(t)` = `occ || takes > 0` (used: `AddressGenerator.java:152`); `isEmpty` (quiescence) | 179-180 |

Producers put without asking and react to `refused` (`WriteEngine.java:68`, `GatherEngine.java:35`, `SRAMReadQueue.java:136`,
`Controller.java:110`, `CnC.java:110`, `AddressGenerator.java:146`); `canPut` is read only by the C&C host model
(`CcCores.java:106`).

**Framework.** The put appends to bucket t + 1. Delivery at t + 1 accepts it if the hold is empty (state after t's takes,
before any of t + 1's), setting the consumer's ready bit; otherwise records `refused_at = t`, counts the refusal and
**sets the producer's ready bit**, so a producer that sleeps still sees its refusal at t + 1 (`ref`'s producers poll every
cycle they have work; the wake makes polling unnecessary). `refused()` = `refused_at == now - 1`; `can_put()` = `!occ &&
last_take != now`; `held_at_start()` = `occ || last_take == now`. The producer keeps its own copy of the word to retry
(`ref`'s `retry` re-puts the pending words: the same thing). WORKER29 agrees.

## 6. `Reg`: a registered value (`WordLatch`, `WordBuses.java:16-37`; and `TileControl`'s registers)

`WordLatch`: a put at t is visible from **t + 1** and stays until the next put replaces it (23-24); `take` is
non-destructive, many readers (31-36); `ready` is false until a first value has landed. **Re-putting the registered value
with nothing pending is a no-op** (26-29): no put is noted, so two such puts in one cycle don't throw.

`TileControl` keeps its registers as fields with the same rule, stated in its header (`TileControl.java:8-13`): a store at
t updates its register at the end of t and the owning unit acts from t + 1; status computed at t is read at t + 1.

**The no-op, and the "driven twice" check (owner's note 1).** No unit in the machine puts on a `WordLatch` (section 0), so
**no unit relies on re-putting the same value**. Behaviour equals `ref`'s either way (the visible value is the same), so
the framework's `Reg` is **stricter in debug**: a second put in a cycle is fatal whatever its value. It keeps the no-op's
other half as an optimisation: a put whose value equals the registered one, with nothing pending, wakes no reader.

**Framework.** The put appends to bucket t + 1; delivery replaces the value and sets each declared reader's ready bit
(only if the value changed). Readers read the delivered value at any level.

**GO, READY and the error lines** are not `Reg`s in `ref` but pulses: `TileControl.scheduleGo(t + goLatency + skew)`
(`CnC.java:84`, `TileControl.java:88-97`) makes GO visible to the tile **only at that cycle** (`goVisible(t)` = `goAt == t`),
and `endCycle` latches it into EVENTS [8] at the end of that cycle. **Framework:** a `Link(L)` with an empty payload (an
event), latency `goLatency + skew[tile]`, into the register block. WORKER29's "`Reg` behind a `Link(8)`" is this `Link`
plus the register block's own state.

## 7. `RegFile`: a unit's register block, read by other units

`RegisterBlock` (`RegisterBlock.java`) is not instantiated; `TileControl` implements what M1 needs by hand. Its timing
(`TileControl.java:8-26`, `100-147`, `read` at 136):
- **core stores**: one per cycle, applied at the end of t (step 2 of `endCycle`), the owner acting from t + 1;
- **core loads**: a load of the block takes `regLoadLatency` (2) and **samples in its last cycle** (`Controller.java`
  header; `TileControl.read`): issued at t, it returns the state **as at the end of t** plus the live bits at t + 1; a
  consuming load (EVENTS, REQUEST_RECEIVE) takes its side effect at the end of its sampling cycle;
- **sticky state and live bits**: EVENTS reads the sticky events as at the end of t - 1 and the live events at t
  (`eventsAt`, 110); ERRORS reads `conditions` (from `endCycle(t - 1)`) and the slot's live errors at t (`errorsAt`, 393);
- **the wake term and the interrupt term** are computed from those at t (`wakeTerm`, 112-117; `Controller.interruptPending`).

`RegisterBlock`'s merge rules (unit writes masked or OR-merged, W1C, core wins, the rule table) are recorded for M2; M1's
subset needs none of them beyond "core store at t, effect from t + 1".

**Framework, minimal `RegFile`.** An array of 32-bit registers **owned by one unit** (the register block), written only in
its step, read by declared readers at **later levels** in the same cycle (the read is a `Wire` read: section 9's discipline
applies), so a reader sees the owner's values for this cycle. Writes from other units (the core's stores, a consuming
load's clear) reach the owner as `Link(1)` entries: the owner applies them in its step at t + 1, at level 0, **before** the
readers, which is `ref`'s "end of t, visible from t + 1". The owner's "end of cycle t" work (`endCycle(t)`) is done the same
way: at level 0 of t + 1, from inputs delivered at t + 1 (section 11 of `sim.h`'s comment: the shifted end of cycle).

## 8. `Count`: an event counter (`Tile.eventCounts`, `TileControl.endCycle`; `EventCounts.java`)

`ref`: the condition counts are monotonic counters in their units (a FIFO's `overflowCount`, the AG's drift events, the
director's unexpected packets); `TileControl.endCycle(t)` reads their sums (`Tile.java:139-147`) and sets a condition bit for
each that rose since the last cycle (`TileControl.java:367-372`). `EventCounts` (a peripheral's counted events) adds 1 per
post at t, visible from t + 1 (`value(t)` excludes posts made at t, `EventCounts.java:34-38`).

**Framework.** A `Count` is one word in a pool, incremented by its producers (a unit's step, or the framework when a `Fifo`
refuses a put); several producers may share one (`ROUTER_OVF` sums all router FIFOs). Each increment is stamped with its
cycle. A reader reads either `now()` (increments up to this point of the cycle: a reader at a later level than every
producer, a `Wire` read) or `through(t - 1)` (as at the end of the previous cycle), which is what the register block reads
at level 0 of t + 1 for `endCycle(t)`: the increments of t's steps and of t's delivery, not t + 1's delivery. (`EventCounts`'
credits are M2.)

**A Count wakes nobody.** Reading it is the reader's business, whenever it runs; an increment sets no ready bit. That suits
M1 (the conditions are read by the register block when something else steps it). **Where a count's change must step a
unit, it is modelled as an event, not a Count**: the producer puts on a width-0 `Link` (or a `Reg` holding the value) to
that unit, which is what wakes it. Otherwise the reader would have to step every cycle to notice. The first case is M2's:
the write counters feed `endCycle`'s status and events (done, the per-counter done events), so their decrements reach the
register block as events.

## 9. `Wire`: this cycle's value (`ref`'s same-cycle direct calls)

`ref` has no wire class: same-cycle couplings are direct calls, correct because of the fixed child order
(`Tile.java:119-134`: the router, the read queue with the injection point, ..., the core; then the slot and `endCycle`;
`Chip.java:215`: the request bus first). WORKER28's and W2's audits list them; WORKER29 section 3 maps each to a port.

**Framework.** A `Wire` has one producer and declared readers; its value is the producer's **latest write** (a level that
persists while the producer is not stepped, so idle producers need not run). The producer's level is lower than every
reader's (levels from the wires; a loop is rejected with its units named). A write that **changes** the value sets each
reader's ready bit for this cycle (readers at later levels run in this cycle). Debug checks: a write after a reader read the
wire in the same cycle (an ordering bug), or two writes in one cycle, is fatal. Rule for authors (WORKER29): **an input
that must be this cycle's value is a `Wire`; anything else is registered.** A unit whose wire output changes with time
alone (not by its step) must ask for a wake to drive it.

**The phases a reader may see**, for all kinds together: during the run of cycle t at level k, a unit reads its inputs'
delivered state at t (minus its own takes), `Wire`/`RegFile`/`Count.now` values written at levels below k during t, and its
outputs' producer-side start-of-cycle state (`can_put`, `refused`, credits). Nothing it puts is visible during t.

## 10. The conformance script (stage 2's driver and test share it)

One seeded script per (kind, depth or latency), the same text read by the Java driver (`sim/tests/ref/PortConfDriver.java`,
compiled into `ref`'s default package, D5) and the C++ test. A line per cycle:

```
<cycle> <op> [<op> ...]
op: P<a>,<b>,<c>   put (three words; Link, Fifo, Hold, Reg)
    T              take (Link, Fifo, Hold; Reg: read)
    K              peek: observe the head (or the value) without taking
    Q              query: observe every side query of the kind (ready, size, size_at_start, can_put, refused,
                   held_at_start, overflow count and first cycle, refusals)
```

The script generator respects each kind's contract (one put and one take per cycle; a `Link`'s consumer takes every
arrival; a credit `Fifo`'s producer puts only with room; a `Hold`'s producer retries a refused word) and otherwise draws
puts and takes at random with a seeded rate. Both sides write one observation line per cycle (`Q` always issued); the two
outputs must be identical over at least 10⁵ cycles at several depths and latencies (PLAN-M1, W3). `Reg` is checked against
`WordLatch` (the no-op case included: the script repeats values).
