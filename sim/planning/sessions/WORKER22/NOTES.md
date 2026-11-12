# WORKER22 notes (P4: the network, memory and engine suites)

Branch `sim-w22`, from main 67d42fb. Rows: C3, C4, C5, C6, C7, C11, C17, G1.

## The steps (v1.3's `run_tests.sh` names)

| row | suite | steps |
|---|---|---|
| C3 | NetTest | `net` (quick: `-1 quick`; all: `-1`, the fan-in table differs), `net-shuf` (5 quick), `net-shuf2` (11 quick), `skewtable` (`-1 skewtable 40`, all only) |
| C4 | MemTest | `mem` (-1), `mem-shuf` (3), `mem-shuf2` (8) |
| C5 | EngineTest | `engine` (-1), `eng-shuf` (4), `engine-full` (`-1 full`, all only) |
| C6 | AgTest | `agtest` (-1), `agtest-shuf` (4) |
| C7 | ReadPortGatherTest | `rdport` (-1), `rdport-shuf` (4), `rdport-full` (`-1 full`, all only); boots `rdport`, `gather` |
| C11 | SearchBench | `searchbench`; boots `search` |
| C17 | RequestBusTest | `reqbus` (-1), `reqbus-shuf` (4); boots `req` |
| G1 | AgBench | `agbench` (all only) |

The images (rdport, gather, search, req) are built by `fw/build.sh` and committed (W19). Every config field these
suites set is in `MachConfig` already. ref's shuffled logs differ from the unshuffled ones only in the header line
(`shuffled child order, seed N`, where printed) and the summary line.

## The plan (agreed with the owner, 2026-11-11)

1. **Setup**: branch, clang and a JDK in the container, `make check` clean on main.
2. **Fixtures**: `oracle/ref/p4suites.sh` runs each step in a scratch copy of `ref` (quick and all), `--fixtures` writes
   the logs, `--check` regenerates and compares. Regenerated, not taken from `ref/results` (W20's F1: a stale log
   there). Fixtures in `tests/p4suites/` with a `PROVENANCE`. The long steps (`skewtable 40`, `engine-full`,
   `rdport-full`, `agbench`) go to the runner (a Quest task) if slow locally.
3. **Per suite**, in order NetTest, RequestBusTest, MemTest, AgTest, EngineTest, ReadPortGatherTest, SearchBench,
   AgBench:
   - map every Java access onto `tests/mach.h`, list the gaps;
   - harness operations and violation kinds missing: asked of W20 through the integrator; read-only accessors for a
     unit's existing state: added here, noted below and in a comment at the accessor;
   - `tests/suite_<name>.cpp` on `suite.h`, Java's arguments;
   - a make-check test: the quick logs byte for byte, plain and at the step's shuffle seed, plus step-all and both
     orders; fp only where the log can't localise (EngineTest's all-to-all).
4. **Wiring**: `tests/p4suites.mk`, one `include` line in the Makefile (integrator told when it's in); the steps join
   W20's `run_tests.sh` when it lands.
5. **Close**: the full steps on the runner; `REPORT.md`.

## The owner's answers (2026-11-11)

- HooksTest is W23's (C15, P5); the prompt named it here by mistake.
- I own ReadPortGatherTest (C7) and RequestBusTest (C17), so the rdport, gather and req programs' logs; W23 relies on
  them and doesn't port them again.
- Read-only accessors for a unit's existing state: added by me, each noted here and in a comment. Anything that changes
  a unit's behaviour or adds state goes to the owner first.
- `p4suites.mk` with one include line: fine; tell the owner when it's in (the Makefile is shared).

## Accessors added to units (read-only, existing state)

(none yet)

## Framework additions (approved by the owner)

- **The Fifo peak (ask 7, approved 2026-11-11)**: `FifoPort::peak` (src/engine.h), updated after each delivery into a
  Fifo or credit Fifo (ref's WordFIFO.highWater: the count after the commit at t + 1), and `Sim::fifo_peak`,
  `Sim::fifo_restart_peak` (src/config.h, .cpp; ref's resetStats: peak = count), all **under SIM_CHECKS only** (the
  debug and suite builds; engine.h turns SIM_CHECKS on under SIM_DEBUG as check.h does); in the timing builds the
  member and its update don't exist and the accessors FATAL. **The timing builds are unchanged**: clang release,
  test_chip and test_sort's outputs identical before and after; every rebuilt release object's code identical
  (disassembly, addresses masked) except config.o, whose only differences are the two new functions, the functions'
  shifted addresses and FATAL's line numbers. Used by NetTest's queue peaks ("write-engine input").

## Asks of W20 (through the integrator)

From NetTest's gap map (2026-11-11). NetTest's other groups (structure, unicast's packets, routes, the controllers'
sends, in-order, fan-in, the queue peaks) port onto what `mach.h` has now.

1. **`Cc::received()` on the coreless machine** (ref's `n.c.cnc.received`; FATAL today): broadcast, cncPort.
2. **`Chip::reset_stats()`** (ref's `resetStats`, the peaks restart; FATAL today, planned with step 4): unicast and
   broadcast call it before every offset, and the router peaks they read depend on it.
3. **A tile's send-port hold from the harness** (ref's `Tile.sendHold`: `put(t, payload, header, hi)`, `isEmpty(t)`,
   `refusals`): injection, loopback.
4. **The write engine's and the GATHER queue's arrival logs** (ref's `writer.record()`/`writer.log`,
   `gatherStub.record()`/`log`): director (which modes reach which unit, in order).
5. **K below the formula refused at build** (ref's `Chip` throws IllegalArgumentException "... below ..." when K <
   kMin()): today `mach::Chip` builds with any K. NetTest runs it under `suite::throws`.
6. **The checker kinds NetTest provokes, recorded**: the injection path's no-endpoint (a packet for 70), size 11 at
   the sender and the director, bad packets at both (7 in the director group), DIR_UNEXPECTED ("stray"), and the
   director's invariants 10, 11, 19, 21, 22 (W20's "next in step 2").
7. **The write engine's input peak** (ref's `weIn.highWater`, the last line of NetTest's queue peaks): the framework's
   Fifo (`src/engine.h` FifoPort) keeps no peak. Adds state to a framework structure, so **the owner's call** (a
   statistic updated at the put, as the router's own `Fifo.peak`). **Approved and done** (below).
8. **The debug options on the harness** (W20): `MachConfig` passes only the shuffle seed into `sim::Options`; step-all
   and the instance-major order (both orders) need fields there too, for the suites' debug runs.

## Progress

- 2026-11-11: plan agreed; branch made. Toolchain: clang 18.1.3, gcc 13.3.0, JDK 21.0.13 (apt). clang release on
  main: builds, every test passes.
- **Fixtures (step 2)**: `oracle/ref/p4suites.sh`; the 16 local steps regenerated from `sim/ref` into
  `tests/p4suites/`. **`ref/results` is stale for every one of my steps** (assertion counts differ: mem 171 not 177,
  engine 365 not 371, agtest 240 not 239, rdport 587 not 562, net 31,328 not 31,330), and its `net.log` is the quick
  run's output (`random*3` column `-`), not the all run's. Neither is used: the fixtures are ref's regeneration.
- **Quest 176** (claimed, pushed): the four long steps (skewtable, engine-full, rdport-full, agbench) on the runner, and
  the 16 local steps regenerated there and compared (the runner's JDK). Task copy in `tasks/`.
- **Quest 176 result** (attempt 1): the four long logs made (skewtable 187 s, engine-full 322 s, rdport-full 45 s,
  agbench 30 s on the runner), committed as fixtures (sha256 equal to the task's SHA256SUMS). Its check-16 verdict
  FAILED only because `--check` with no steps compared all 20, and the 4 long ones had no committed fixture yet; none
  of the 16 differed (the runner's JDK gives the same logs). Task fixed to check the 16 (for the runner's retry).
- **NetTest's gap map**: the asks above; everything else maps onto `mach.h` and the units' existing state (router
  FIFOs' peaks and overflows, `routed`, `injected`, the director's counters, the route tables).
- **NetTest, the first groups** (`tests/suite_net.cpp`, `tests/p4_java.h` (Random, shuffle), `tests/p4suites.mk`,
  `tests/p4_lines.sh` (a partial run's lines as one block of a fixture)). Each group's lines against the fixture, clang
  suite build, seeds -1, 5, 11 (`net`, `net-quick`, `net-shuf`, `net-shuf2`) and clang debug (ASan, UBSan):
  | group | lines | |
  |---|---|---|
  | structure | 1 | equal |
  | unicast | 1 | equal (reset_stats a no-op meanwhile: ask 2; the one check reading the peaks has the same outcome) |
  | routes | 1 | equal |
  | in-order random + queue peaks | 20 | equal but the last peak line (ask 7) |
  | in-order full rate | 10 | equal |
  | fan-in (quick; all, with random*3 and 40 seeds) | 9 | equal |
  | skewtable, 1 seed (ad hoc, ref run locally) | 15 | equal; 40 seeds (24 s a seed here): runner |
  Fan-in's "safe" covers every hardware FIFO as ref's: the router FIFOs directly, the others by their ERRORS bits
  (a non-router overflow first would be FATAL until W20's overflow scan names it; none occurs).
- **Ask 7 done**: the queue peaks' 21 lines equal (net, net-shuf), the write-engine line included.
