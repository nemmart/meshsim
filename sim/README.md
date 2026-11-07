# sim/: the next simulator

A clean-sheet simulation framework for the mesh ASIC: **very fast, parallel, and easy to build units in**, written in **C++
from the start** (minimal STL, our own low-level structures), built from components rather than migrated from the Java
simulator. The brief, the measurements behind it and the design direction are in `wip/planning/SIMULATOR-NEXT.md`.

| folder | what |
|---|---|
| `ref/` | **the reference: v1.3's Java simulator, the exactness oracle.** A frozen copy of `v1.3/`'s `meshsim/` with the siblings it reads (`sw/`, `sort/`, `tables/`, `gold/`), so it builds and runs here as it did there. Not developed further here (`wip/meshsim` carries on for v1.4). Wherever the new framework models a unit that `ref` models, they must agree cycle for cycle. |
| `src/` | **the C++ framework**: first the primitives, then an RV32 controller running real firmware on them |

**The first milestone** (owner, 2026-11-06): the framework's primitives, and **one RV32 controller running real firmware**
on them, checked against `ref` running the same program, cycle for cycle.

**The core loop** (owner, 2026-11-06): no bus or register objects to walk. Every cycle, per thread:
1. **Deliver.** All data in motion lives in **big delivery lists**, one bucket per arrival cycle. A put appends an entry
   (destination port, payload inline or a handle) to the bucket for t + L. Delivery walks bucket t front to back, writing
   each payload into its destination's slot (or onto its FIFO ring) and marking the destination unit ready.
2. **Run.** Ready units go on **run lists grouped by type** (one list, or a 64-bit mask per chip, per unit type), so one
   type's code runs over all its ready instances together. Only ready units run; an idle unit is never visited.
3. **Commit.** Staged register writes and pushes applied; unfinished work re-marks its unit for the next cycle.

**Thread-local work** (owner): each thread owns its partition (a set of tiles, or whole chips) with its own delivery lists,
run lists and free lists. Entries for another thread's units go into a per-destination outbox, merged at the barrier, with
no locks in the cycle.

**Memory** (owner): two kinds of object.
- **Permanent state**, allocated once at build time with `new`/`malloc`: an `RV32_core` with all its state, a big memory, a
  unit's own state. Grouped by type where possible.
- **Data in motion**, the messages: small and constantly created and dropped, so **never `malloc` in steady state**. Most are
  fixed-size values copied inline into delivery entries and ring slots; payloads too big to copy travel as handles into
  **thread-local free lists**.

**Build** (owner, 2026-11-06): **plain `make`**, not CMake.

**Discipline from the first commit:** sanitizers (ASan, UBSan) and deterministic tests.
