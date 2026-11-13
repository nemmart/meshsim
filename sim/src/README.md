# src: the simulator

*For someone changing the simulator or adding a unit or a peripheral: how a cycle runs, how a machine is built, and the
checklist, example and tests for a new one.* The headers are the reference: `sim.h` (what a unit's author writes),
`config.h` (building a machine), `check.h` (the checker); `PORTS.md` states each port kind's rule. This page explains
and points.

## How the simulator works

**A unit is a plain struct** with its ports (handles), its own state, a constructor that declares the ports, and
`Wake step(Ctx&)`. The framework owns every port's storage, decides who runs when, and moves the words. Payloads are
untyped 64-bit words; anything over 128 bits travels as a handle. A unit never reads another unit's state or holds a
pointer to one.

**The cycle** (`sim.h`, THE CYCLE), per thread, cycles numbered from 1 as in `ref`:
1. *Deliver t:* every word due at t lands (a Link's after its latency, a Fifo's, Hold's or Reg's put of t − 1, a
   credit's return), and each receiver is marked ready. Nothing done during t changes what was delivered at t.
2. *Run t, level by level:* each ready unit steps, reading its own state and its delivered inputs; its puts go to
   t + L (L ≥ 1), never visible during t. **Wires and RegFiles** are the exception, read in the same cycle by units at
   higher levels, so levels come from them: a unit's level is its longest wire path; a combinational loop is rejected
   at build, its units named. An input that must be this cycle's value is a Wire; everything else is registered.
3. *End t:* each stepped unit's `Wake` is scheduled.

`ref`'s "end of cycle" work (a unit that reads what every other unit did at t) is written as a unit at level 0 of
t + 1, with registered inputs: the *shifted end of cycle* (`sim.h`).

**Wakes.** After cycle 1 a unit steps only when something is delivered to it, a Wire it reads changes, or it asked for
that cycle (`Wake::next()`, `Wake::at_cycle(t)`). Data waiting in an input does not by itself wake it again, so an idle
unit costs nothing and a long sleep costs one wake. A missed wake fails silently, so the debug build has a detector
(D20, `sim.h`): a unit asleep with a word waiting in an input, and nothing on its way to wake it, is fatal by default,
naming the unit and the port. The *step-all* mode, which steps every unit every cycle, must give the same digest as
the ready-driven run.

**Building a machine** (`config.h`) is four steps, each once: add units (`cfg.add<T>(…)`), wire them (`cfg.link`,
`cfg.connect`), build, run. **Everything is sized at build, never resized:** each port declares its capacity, the build
adds them up and allocates every ring, bucket and pool once, at its exact maximum, per thread (`sizes()` reports each
number). Needing more at run time is a bug, so it is fatal.

**Two coding regimes** (D6). Configuration (adding, wiring, sizing, loading) may use any C++ that makes it clear: the
STL, strings, lambdas. The cycle loop uses none of it: plain arrays, the framework's structures (`base/`), no
allocation: `base/alloc_guard.h` makes any allocation in the loop fatal. No exceptions and no RTTI anywhere: a fatal
condition prints its context and aborts (`base/fatal.h`), and its message doesn't allocate (D24).

**Threads and the partition** (`threads.h`, not for unit authors). A machine can run over several domains, one thread
each: every connection that crosses domains is cut at build, and the producer's put goes to an outbox that the
consumer's domain drains after a barrier, every `interval` cycles (the smallest crossing latency), so a word arrives at
t + L exactly as in one domain. A Wire across domains is fatal at build. One chip runs at a time, split across threads
(D26).

**Determinism and its checks.** A run is deterministic, whatever the thread count. The machine's digest (`digest.h`)
hashes every unit's state and every port's contents, independently of how the build numbered them, so one domain and
several give the same digest; tests and `bench/check.sh` compare them. Against v1.3, results are exact and cycle counts
exact except for the deliberate differences (D30; `spec/CHANGES-v1.3-to-v1.35.md`); `tests/README.md` says how that is
established.

**What the builds check** (`check.h`, `base/fatal.h`): undefined behaviour is fatal in every build; the silent traps
stop the core in every build; every other invariant is counted in every build and reported by name in the debug and
suite builds. The checks neither v1.3's simulator nor v1.35's has are listed in `spec/CHANGES-v1.3-to-v1.35.md`.

## Adding a unit

1. Write the struct (`units/<name>.h`, and `.cpp` if needed): ports as members, state, a constructor declaring each port
   with its capacity, `Wake step(Ctx&)`. Follow `sim.h`'s rules for what `step` may read and write.
2. Choose each input's kind by `PORTS.md`: a Wire only for a same-cycle value; registered otherwise.
3. Return the right `Wake` from every path that leaves work in an input.
4. If its state isn't plain bytes, give it `uint64_t digest() const`, usually one line with `hash_fields` (`sim.h`).
5. Count each violation of an invariant it owns, and record it with a `check.h` kind (`ck::record`); make undefined
   behaviour `FATAL`.
6. Add it to the chip (`units/chip.h`): `tile_cycle` adds a tile's units, `build_chip` the chip's; a setting it
   needs goes in `units/chip.h`'s `ChipSpec` and, for the machine's users, `MachConfig` (`tests/mach.h`).
7. Write its tests (below), and add its row to `units/README.md`.

## Adding a peripheral

A target's own units are peripherals: they plug into a tile's or controller's peripheral slot (spec 4.17,
`spec/OVERVIEW.md`), which gives them the region, the window, the events, the DMA and the request port. The contract is
`units/peripheral.h`; the slot is `units/periph_slot.h`.

1. Write a class deriving from `PeripheralBase<Self>` and define the methods of `peripheral.h` it needs; the base gives
   the others their defaults. No virtual calls: the slot dispatches on the type.
2. Give it a type number: `PT_*` in `peripheral.h` (spec 4.17.1).
3. In `units/periph_slot.cpp`, add a case for the type in each switch: the dispatch (`visit`), the destructor
   (`Ref::~Ref`) and the creation; add an accessor as the others have (`cc_stub(n)`).
4. Put it in a slot when the chip is built (`units/chip.cpp`), and wire its own ports there if it has any.
5. If it has state, declare `DIGEST_N` and `digest_words`; return its flags from `violation_count()`.
6. Tests: as a unit's, plus the slot's tests with it in place (`tests/test_periph_slot.cpp` and
   `tests/test_tile_slot.cpp` do this for the test peripheral).

**A worked example: C&C 2's engine stub** (`units/cnc1_periph.h`, `CcStub`, type 2). It is present and typed, never
runs, raises no events or errors, has no words beyond the standard ones, and flags any write into its region:

- `type()` and `params()` return 2 (3 when it stands for C&C 3's engine);
- its constructor gives `EventCounts` two events of depth 1;
- `accept` (a region write) counts a violation, and `violation_count()` returns the count, which the slot forwards to
  the tile;
- `running`, `errors`, `step`, `idle` and `reset` are the trivial ones; `consume_events` and `events` hand the slot its
  counts;
- it has the four switch cases of step 3 and `cc_stub(n)`, and `build_chip` puts it in C&C 2's slot.

For a peripheral with words, runs, a DMA and output queues, read `units/test_peripheral.h` (the suites' test
peripheral, type 15) next; then `cnc1_periph.h`'s `Cnc1Peripheral`, which has its own ports.

## The tests a unit must pass

- **A test of its own,** `tests/test_<name>.cpp`: `make check` builds and runs every `tests/test_*.cpp` in the debug and
  release builds with gcc and clang (`BUILDING.md`).
- **The debug build clean:** ASan, UBSan and the missed-wake detector; the allocation guard (any allocation in the
  cycle loop is fatal) holds in every build.
- **The same digest on one domain and several**, where it can sit on a domain boundary.
- **Against v1.3,** where it has a counterpart in `ref`: its results and cycles compared with a fixture from v1.3
  (`tests/README.md`).

## Files

`sim.h`, `config.h`, `check.h` and `PORTS.md` are the reference. `engine.*`, `config.cpp`, `config_impl.h`,
`internals.h` and `observe.cpp` are the framework's build and cycle loop; `threads.*`, `parallel.*`, `partition.cpp`,
`barrier.*` and `topology.*` its domains; `digest.*` the digest; `regions.cpp` the region map; `base/` the low-level
structures (`base/README.md`); `units/` the model of the chip (`units/README.md`).
