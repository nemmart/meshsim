# sim/: the next simulator

A clean-sheet simulation framework for the mesh ASIC: **very fast, parallel, and easy to build units in**, written in **C++
from the start** (minimal STL, our own low-level structures), built from components rather than migrated from the Java
simulator. The brief, the measurements behind it and the design direction are in `wip/planning/SIMULATOR-NEXT.md`.

| folder | what |
|---|---|
| `ref/` | **the reference: v1.3's Java simulator, the exactness oracle.** A frozen copy of `v1.3/`'s `meshsim/` with the siblings it reads (`sw/`, `sort/`, `tables/`, `gold/`), so it builds and runs here as it did there. Not developed further here (`wip/meshsim` carries on for v1.4). Wherever the new framework models a unit that `ref` models, they must agree cycle for cycle. |
| `src/` | **the C++ framework**: first the primitives, then an RV32 controller running real firmware on them |
| `planning/` | the project's plan (`PLAN-M1.md`), decisions (`DECISIONS.md`) and its workers, numbered from WORKER1 (`sessions/`) |

**The first milestone** (owner, 2026-11-06): the framework's primitives, and **one RV32 controller running real firmware**
on them, checked against `ref` running the same program, cycle for cycle.

**The core loop** (owner, 2026-11-06): no bus or register objects to walk. Every cycle, per thread:
1. **Deliver.** All data in motion lives in **big delivery lists**, one bucket per arrival cycle. A put appends an entry
   (destination port, payload inline or a handle) to the bucket for t + L. Delivery walks bucket t front to back, writing
   each payload into its destination's slot (or onto its FIFO ring) and marking the destination unit ready.
2. **Run.** Ready units go on **run lists grouped by type** (one list, or a 64-bit mask per chip, per unit type), so one
   type's code runs over all its ready instances together. Only ready units run; an idle unit is never visited.
3. **Commit.** Staged register writes and pushes applied; unfinished work re-marks its unit for the next cycle.

**How many phases a cycle has is for the framework to settle** (owner, 2026-11-06): deliver, run and commit are the
minimum; units with same-cycle (combinational) dependences add sub-phases, such as levels of wires, or a request then grant
step for arbitration. There is **no target number**: as many as the units need, as long as each phase is defined by
what it may read and write.

**Thread-local work** (owner): each thread owns its partition (a set of tiles, or whole chips) with its own delivery lists,
run lists and free lists. Entries for another thread's units go into a per-destination outbox, merged at the barrier, with
no locks in the cycle.

**Crossing a thread domain is a different kind of port** (owner, 2026-11-06): the same port kinds come in a **local** and a
**cross-domain** form. A local put appends to the thread's own delivery bucket; a cross-domain put goes to the outbox for the
destination's thread, and at the merge any 128-bit payload index is replaced by a copy in the destination's own array. The
configuration phase knows the partition, so it chooses each port's form when it wires the chip; a unit's code is the same
either way. *(For the plan: chosen at configuration time, not by virtual calls in the cycle loop, e.g. a port's route resolved
once to "my bucket" or "this outbox".)*

**Memory** (owner): two kinds of object.
- **Permanent state**, allocated once at build time with `new`/`malloc`: an `RV32_core` with all its state, a big memory, a
  unit's own state. Grouped by type where possible.
- **Data in motion**, the messages: small and constantly created and dropped, so **never `malloc` in steady state**. Most are
  fixed-size values copied inline into delivery entries and ring slots; payloads too big to copy travel as handles into
  **thread-local free lists**.

**Payloads are 64-bit words, untyped** (owner, 2026-11-06): no templated transports or message types. Everything in motion is
a small group of `uint64_t` words whose interpretation is up to the destination; a transport (a delivery list, a ring, a
`Fifo`, a `Hold`, a `Link`) moves words and never looks at them. A large payload travels as a handle, which is itself one
64-bit word. **Inline up to 128 bits; anything bigger by pointer** (owner): the receiver knows what to do with it. **The mesh's link word**
(128-bit payload plus a 54-bit header, three words in `ref`) is the case to measure: (a) **two words, the routing header and
the data** (owner's proposal): routers read only the header word, never dereference; the second word is the data itself for a
32- or 64-bit payload, a pointer to the 128 bits otherwise, the header's size field saying which; against (b) **three words
inline**, no allocation or dereference, one more word copied per hop. The first benchmarks measure both on a busy mesh. **The pointer is an index** (owner, 2026-11-06): each thread keeps **one array of
128-bit values** with a free list of indices, so word 2 of a 128-bit link word is an index into the thread's own array:
allocation is popping an index, data stays in one dense array. **Only crossing threads changes that:** at the outbox merge the
payload is copied into the destination thread's array and the index rewritten, so no array is ever shared and nothing is
freed by a thread that doesn't own it.

**Everything is sized at startup, never resized** (owner, 2026-11-06). Construction has two steps: first every component
**declares** its ports and their capacities (a FIFO's depth, a link's latency and so its most words in flight, a hold's one
slot); then the framework **adds them up** and allocates every pool, ring and delivery bucket once, at its exact maximum, per
thread. Nobody counts by hand. The 128-bit value array is sized to the most values all its queues can hold at once, its free
slots tracked by **a bitmap** (one bit per slot). A delivery bucket's size follows from the ports' latencies (each port takes
at most one put per cycle). At run time, needing more than was declared is a bug, so it stops with a fatal error, never grows. **Two
coding regimes** (owner): the **configuration phase** (declaring, building, sizing, wiring, loading) may use whatever C++ makes
it clear: the STL, templates, lambdas, `std::vector`, maps, strings; **the cycle loop** (deliver, run, commit) uses none of
it: plain arrays, the framework's own structures, untyped 64-bit words, no allocation. (No RTTI in either.)

**Build** (owner, 2026-11-06): **plain `make`**, not CMake. **No exceptions** (`-fno-exceptions`; owner): a fatal condition (a broken
invariant, undefined behaviour, a capacity exceeded) prints its context to stderr with `fprintf` and calls `std::abort()`. **No RTTI anywhere** (`-fno-rtti`; owner): no `dynamic_cast` or `typeid`;
the framework gives each unit type its own small integer id, which the run lists are grouped by anyway.

**Discipline from the first commit:** sanitizers (ASan, UBSan) and deterministic tests.
