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

- **The Fifo peak (W22-7, approved 2026-11-11)**: `FifoPort::peak` (src/engine.h), updated after each delivery into a
  Fifo or credit Fifo (ref's WordFIFO.highWater: the count after the commit at t + 1), and `Sim::fifo_peak`,
  `Sim::fifo_restart_peak` (src/config.h, .cpp; ref's resetStats: peak = count), all **under SIM_CHECKS only** (the
  debug and suite builds; engine.h turns SIM_CHECKS on under SIM_DEBUG as check.h does); in the timing builds the
  member and its update don't exist and the accessors FATAL. **The timing builds are unchanged**: clang release,
  test_chip and test_sort's outputs identical before and after; every rebuilt release object's code identical
  (disassembly, addresses masked) except config.o, whose only differences are the two new functions, the functions'
  shifted addresses and FATAL's line numbers. Used by NetTest's queue peaks ("write-engine input").
- **MemTest's statistics (approved 2026-11-12)**, under SIM_CHECKS only, each where ref updates it: BigSram's
  `reads_a[16]`, `writes_a[16]`, `combined_a[16]` (at the serve and the merge) and `queue_peak[16]` (the count after an
  enqueue) (src/units/big_sram.h; ref's BigSRAM.reads, writes, combined, queuePeak); the SRAM read queue's
  `lat_max[3]` (issue to injection, at the injection) and `first_issue_t[3]`, `last_issue_t[3]` (at the issue)
  (src/units/inject.h; ref's SRAMReadQueue.latMax, firstIssueT, lastIssueT). **Timing builds unchanged**: clang
  release, test_chip and test_sort's outputs identical; of the rebuilt release objects, the normalized code (addresses,
  jump targets masked) differs only in 6 objects and only in `mov $imm,%esi`: FATAL's `__LINE__` argument to
  `fatal_at` (e.g. 0x117 -> 0x11d, the 6 lines inserted above it in inject.h).

## Changes to units I don't own (approved by the owner)

- **F1, the read queue's `window_full_cycles` (owner 2026-11-12: fix, in every build; v1.3 parity)**:
  src/units/inject.h (W11's read side, W20's harness path alike). `InjReadSide::wf_from` and `window_waiting`: at the
  end of Injection's step, if no token will be free at t + 1 and a client has a request (a client hold or the read-port
  FIFO ready; on the harness's machine a visible ring entry), the wait is counted from t + 1; the read side's next step
  adds the cycles asleep (`window_full_cycles += t - wf_from`) before counting its own cycle as before. Injection's
  `token_wait_cycles` / `wait_from` pattern; no wake added, no timing changed. **Checked**: MemTest's token window
  line now ref's (80 cycles; seeds -1, 8); test_chip, test_sort and test_memconf (which compares
  `window_full_cycles` with ref's MemConf fixtures) print the same before and after (clang release); test_mem,
  test_memconf, MemTest's groups in clang debug; NetTest's ported groups unchanged.

- **The write engine's stage peak (approved 2026-11-12)**: `WriteEngine::stage_peak` (src/units/write_engine.h), under
  SIM_CHECKS only, the stage's count after a take (ref's stageHighWater, WriteEngine.java:109).
- **F3, the write engine's UB stop names the PTR (approved 2026-11-12)**: "a write through PTR[src N] = chunk 0x..."
  (`PTR[digit]` for mode 2), as ref's; text only, in the FATAL path (behaviour unchanged).
  Both checked: test_chip and test_sort print the same before and after (clang release); EngineTest's write engine
  group equal to ref's (its line, peaks 3, 5, 7, 9; its `ub` names "PTR[src 9]").

- **F5, the traffic director's UB stops (approved 2026-11-12)**: src/units/director.h:124, 136 name the unit
  "traffic director (tile N): ..." as ref's (TrafficDirector.java:88, 98); text only, in the FATAL path. AgTest's
  `ub` on the misaligned destination now passes; test_chip, test_sort, test_g1_tile unchanged (clang release).

- **The write engine's writes by size (approved 2026-11-12)**: `WriteEngine::written_by_size[3]`, under SIM_CHECKS
  only, counted where a write is put to big SRAM (ref's writtenBySize, WriteEngine.java:81). ReadPortGatherTest's
  gather group: 20 of 20. Timing builds: test_chip and test_sort print the same; of the 30 release objects that include
  write_engine.h, 2 differ, only in FATAL's `__LINE__` argument (`mov $imm,%esi`).

## Asks of W20 (through the integrator; named W22-n, so they don't collide with other workers' numbers)

From NetTest's gap map (2026-11-11). NetTest's other groups (structure, unicast's packets, routes, the controllers'
sends, in-order, fan-in, the queue peaks) port onto what `mach.h` has now.

W22-1. **`Cc::received()` on the coreless machine** (ref's `n.c.cnc.received`; FATAL today): broadcast, cncPort.
W22-2. **`Chip::reset_stats()`** (ref's `resetStats`, the peaks restart; FATAL today, planned with step 4): unicast and
   broadcast call it before every offset, and the router peaks they read depend on it.
W22-3. **A tile's send-port hold from the harness** (ref's `Tile.sendHold`: `put(t, payload, header, hi)`, `isEmpty(t)`,
   `refusals`): injection, loopback.
W22-4. **The write engine's and the GATHER queue's arrival logs** (ref's `writer.record()`/`writer.log`,
   `gatherStub.record()`/`log`): director (which modes reach which unit, in order).
W22-5. **K below the formula refused at build** (ref's `Chip` throws IllegalArgumentException "... below ..." when K <
   kMin()): today `mach::Chip` builds with any K. NetTest runs it under `suite::throws`.
W22-6. **The checker kinds NetTest provokes, recorded**: the injection path's no-endpoint (a packet for 70), size 11 at
   the sender and the director, bad packets at both (7 in the director group), DIR_UNEXPECTED ("stray"), and the
   director's invariants 10, 11, 19, 21, 22 (W20's "next in step 2").
W22-7. **The write engine's input peak** (ref's `weIn.highWater`, the last line of NetTest's queue peaks): the framework's
   Fifo (`src/engine.h` FifoPort) keeps no peak. Adds state to a framework structure, so **the owner's call** (a
   statistic updated at the put, as the router's own `Fifo.peak`). **Approved and done** (below).
W22-8. **The debug options on the harness** (W20): `MachConfig` passes only the shuffle seed into `sim::Options`; step-all
   and the instance-major order (both orders) need fields there too, for the suites' debug runs.

### RequestBusTest's needs (2026-11-11): blocked on the harness

RequestBusTest drives ref's `RequestBus` from Java as C&C's model would, on `Net(Config.machine())` (cores booted into
the boot ROM's WFI). The C++ machine without `cc_cores` builds **no receive queues** (chip.cpp `add_request_bus`: they
come only with the controller cores, which pop them over `pop`), and `mach::Net` is coreless only. So:

W22-9. **The machine as specified under Net** (ref's `new Net(Config.machine())` and `settle`), or the same through Mach.
W22-10. **The receive queues on the Java-C&C machine, popped from the harness** (ref's `RequestBus.get(target, t, word)`,
    `pending`, `receive` (a pop, its credit returned to the root), `queue[target].size()`, `credits[]`, `underflows`,
    `delivered` (with `deliveredAt`, `srcTile`), `dropped`), and **`controllerSend`** (a controller's request entering at
    the root). Some are read-only (credits, sizes, counts) and could be accessors; the queues themselves, the pops and
    the delivered log are new harness units or state.
W22-11. **A tile's request port from the harness** (ref's `req.setW(i, v)`, `req.send(t, w3)`: a REQUEST_SEND without the
    core), and `req.canPush(t)` (the core's `can_push` exists).
W22-12. **The core's data port from the harness** (ref's `core.store(t, addr, v)`, `core.load(t, addr, 4)` on a tile
    waiting in the boot ROM's WFI: REQUEST_SET_W0-2, REQUEST_SEND, EVENTS_MASK, EVENTS, STATUS, EVENTS_PEEK,
    REQUEST_GET_W0, REQUEST_RECEIVE).
W22-13. **The request bus's checker kinds**: a reserved target and target 3 ("reserved", "no receiver"), dropped at the
    root.
The C program part (`fromC`: `req` on the machine as specified, C&C 2 draining its queue from the harness every 64
cycles) needs 10 too. `stall_request` (rv32.h) and the ERRORS bits exist.

### MemTest's needs (2026-11-12)

MemTest maps onto `mach.h` (Net.rd, Net.at, set_ptr, the SRAM pokes, set_remaining, mem_idle, dmem) and the units'
existing counters (the read queue's `issued[3]`, `stall_cycles`, `window_full_cycles`, `in_flight`; the write engine's
`taken`, `stall_cycles`; BigSram's aggregate `read_stall_cycles`, `write_stall_cycles`; the Fifo peak and overflows), and
its unit level (ref's `U`: BigSRAM alone at its holds) is test-side code here. What it reads that no unit keeps:

- **Statistics, new state (the owner's call)**: BigSram's per-area `reads[16]`, `writes[16]`, `combined[16]`,
  `queuePeak[16]` (C++ keeps only the totals); the SRAM read queue's per-client `latMax[3]`, `firstIssueT[3]`,
  `lastIssueT[3]`. Proposed as the Fifo peak was: under SIM_CHECKS only, updated where ref updates them, the timing
  builds unchanged (shown as for W22-7).
W22-14. (owner: `new_phase` to W20, big SRAM's recording to W26) **Chip::new_phase and the disjoint check recorded** (ref's `Chip.newPhase`, BigSRAM's phase and
    `disjointViolations`; the DISJOINT kind is declared and formatted, not yet recorded by big SRAM): disjoint.
- W22-4 (the write engine's arrival log): on main (`record_writer`, `writer_log`).
- Chip.memReport: suite-side formatting from the counters above (no ask).

### AgTest's needs (2026-11-12)

AgTest prints no lines (its log is the summary: 240 assertions). On today's main it uses W20's new `TileRef::store`,
`load`, `send_put`, the AG's statistics (`stream_reads`, `group_reads`, `requests`, the queue peaks), TableStore's and
the HIST store's `rmws` and `pipe.ctrl_writes_lost`. What's missing:

W22-15. **ref's `EngineTest.phaseDone` / `runLocal`** (AgTest's four engine groups and every EngineTest group stop on
    it): `!ag.running && ctl.countersZero() && dir.inFlight() == 0 && memIdle(t) && dir.cmIdle(t) && !tsBusy(t)`. The
    C++ director forwards each counted packet's counter over its `kline` Link(K + 1) and mode-0 writes over `port_b`, so
    `inFlight` (accepted, not yet decremented) and `cmIdle` (nothing on port B's link) are words in flight on those
    ports: a harness accessor (e.g. `TileRef::phase_done()`, or `dir_in_flight()` and `cm_idle()`).
W22-16. **The write engine's RMW requests from the harness** (figure 12: ref's `p.weTsReq.put(t, op, idx, add)` and
    `p.weHistReq.put(t, 0, idx, add)`, an RMW on the TableStore's or HIST's request line as the write engine would put
    it, so the test can aim a controller store or load at its write-back cycle).
- W22-14 (`new_phase`): rulings. W26: the AG's stream-entry checks ("stream", read size 11, d = 70, d = 126, mode-4,
  mode-3 at 128, counter 0) and the injection's "dst 70".
- ref's `ag.runningForStatus()` / `doneForStatus()` (rulings): the AG's `status` Reg as the block reads it at t + 1;
  read through `TileRef::block()` or an accessor (to settle when that group is written).

**Finding F2 (for W20): `TileRef::mem_idle` indexes the area queues modulo `MAX_DEPTH`** (tests/mach.cpp:739,
`m.q[a][(m.head[a] + j) % units::BigSram::MAX_DEPTH]`), while BigSram wraps them modulo `cfg.area_queue_depth`
(big_sram.h `at`, `enqueue`, `serve`). With the default depth 8, once head + j reaches 8 it reads a stale slot, so a
queued write can be missed (mem_idle true too early) or a served one seen. `m.at(a, j)` is the fix. MemTest's mem_idle
group passes either way (its queues never wrap while it asks).

### SearchBench and EngineTest (2026-11-12)

W22-17. **SearchBench needs the TRACE store log** (ref's `p.ctl.traceHook = (t, v) -> ...`: the cycle of every TRACE
    store per tile, search.c's `mark()`), W20's step 4 (H6, `MachConfig::log_trace`, not on main yet). Everything else
    it uses is there (Mach::boot, Cc's command/go/busy, run, word by symbol, halt_summary, the block's `dones`, the read
    port's FIFO overflows through `Sim::fifo_overflows`); Java's `%,d`, `%.1f`, `%.2f` (HALF_UP on the shortest
    digits, as W21's `java_2f`) are suite-side.
- **EngineTest**: writeengine, alltoall, drift, inv6 ported (`tests/suite_engine.cpp`); localcopy, streamruns, exchange,
  radix, lategroups and fence stop on ref's `runLocal` (W22-15). Against engine.log: **all-to-all equal** (5 lines:
  the table, the bound, the queue peaks, agReport's late/drift figures); the rest wait on:
  - **the write engine's stage peak** (ref's `writer.stageHighWater`, the count after a take, WriteEngine.java:109;
    printed by the write engine group's line): a statistic the C++ write engine doesn't keep: **the owner's call**
    (proposed as the others: under SIM_CHECKS, where ref updates it);
  - **finding F3: the write engine's UB stop doesn't name the PTR** (src/units/write_engine.h:123): ref says "a write
    through PTR[src 9] = chunk 0x50009 ..." (or `PTR[digit]` for mode 2), the C++ "a write through PTR = chunk
    0x50009". EngineTest's `ub` requires "PTR[src 9]". Diagnostic text only (the FATAL path): **the owner's call**;
  - W26: drift's "invariant 3" (AG drift) and inv6's "invariant 6" (HIST over 17 bits) aren't recorded yet.

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
- **NetTest's gap map**: the asks W22-1 to W22-6 below; everything else maps onto `mach.h` and the units' existing state (router
  FIFOs' peaks and overflows, `routed`, `injected`, the director's counters, the route tables).
- **NetTest, the first groups** (`tests/suite_net.cpp`, `tests/p4_java.h` (Random, shuffle), `tests/p4suites.mk`,
  `tests/p4_lines.sh` (a partial run's lines as one block of a fixture)). Each group's lines against the fixture, clang
  suite build, seeds -1, 5, 11 (`net`, `net-quick`, `net-shuf`, `net-shuf2`) and clang debug (ASan, UBSan):
  | group | lines | |
  |---|---|---|
  | structure | 1 | equal |
  | unicast | 1 | equal (reset_stats a no-op meanwhile: W22-2; the one check reading the peaks has the same outcome) |
  | routes | 1 | equal |
  | in-order random + queue peaks | 20 | equal but the last peak line (W22-7) |
  | in-order full rate | 10 | equal |
  | fan-in (quick; all, with random*3 and 40 seeds) | 9 | equal |
  | skewtable, 1 seed (ad hoc, ref run locally) | 15 | equal; 40 seeds (24 s a seed here): runner |
  Fan-in's "safe" covers every hardware FIFO as ref's: the router FIFOs directly, the others by their ERRORS bits
  (a non-router overflow first would be FATAL until W20's overflow scan names it; none occurs).
- **W22-7 done**: the queue peaks' 21 lines equal (net, net-shuf), the write-engine line included.
- **RequestBusTest**: gap map done (W22-9 to W22-13 above): it needs the receive queues on the machine without controller
  cores, popped from the harness, and the core's and request port's harness sides; nothing of it ports onto today's
  harness alone. Next instead: MemTest (coreless, Net.rd and the engines' setters: what mach.h has).
- **Quest 179** (claimed, pushed): skewtable 40 on the C++ against v1.3's log, and the ported groups against the four
  net logs, on the runner.
- **Quest 179**: every verdict PASS: skewtable 40 on the C++ byte for byte equal to v1.3's log (225 s on the runner,
  clang suite build), and the ported groups against net, net-quick, net-shuf and net-shuf2.
- **For the integrator**: main's `run_tests.sh` marks `searchbench` and `agbench` PENDING "P6" and `reqbus`,
  `reqbus-shuf` "P5"; they are P4's (C11, G1, C17).
- **MemTest**: gap map done (above): per-area and per-client statistics (owner's call), W22-14, W22-4.
- **run_tests.sh**: searchbench, agbench, reqbus, reqbus-shuf relabelled P4 (pushed to main's file, owner told).
- **MemTest** (`tests/suite_mem.cpp`; ref's `U` as a sim of BigSram and a driver, its puts through `Sim::put_late`,
  refusals from `hold_refusals`). Each group's lines against mem, mem-shuf (3), mem-shuf2 (8): clang suite, clang and
  gcc debug. 128 + 34 of the 171 assertions, every printed line but two:
  | group | lines | |
  |---|---|---|
  | decode, partial, wrap | 0 | pass |
  | stall, combining, latency | 1 each | equal |
  | throughput | 7 | equal |
  | priority, return order | 1 each | equal (packets: 0, pass; its 3 UB stops name tile, client, chunk) |
  | mem_idle | 0 | pass |
  | oversubscription + memReport | 10 | equal |
  | token window | 1 | equal after F1's fix (was 9 cycles, ref 80) |
  | disjoint | 1 | waits on W22-14 |
- **Finding F1: the read queue's `window_full_cycles` undercounts** (src/units/inject.h, W11's unit and W20's harness
  path alike). ref counts every cycle the issue phase finds no token free while a client has a request; the C++ unit is
  event-driven and sleeps while the window is full (`wake`: a client with no free token doesn't wake it; a freed token
  does), so the cycles asleep aren't counted: 9 here, ref 80. A statistic only (no timing or behaviour), but the value
  is printed by MemTest. Fix proposed (the owner's call: it changes a unit): account the wait lazily, as Injection's
  `token_wait_cycles` / `wait_from` already does: the cycle the wait began, the span added when the unit next steps.
  Under SIM_CHECKS only, or in every build (the existing counter is wrong in every build).
- **F1 fixed** (above): MemTest equal on every printed line but the disjoint group's (W22-14).
- **AgTest**: gap map (W22-15, W22-16 above); finding F2 (mem_idle's queue index) for W20.
- **NetTest complete** (W20's reset_stats, Cc::received, send_put/send_empty/send_refusals, record_writer/gather, the
  K check; the checker's kinds already recorded): broadcast, C&C 1-3 ports, director, injection, loopback added. **The
  whole suite byte for byte equal to all four logs**: net (all), net-quick, net-shuf (5), net-shuf2 (11), clang suite
  build; skewtable by Quest 179. **One interim**: the director group's `cm_idle` (ref's `dir.cmIdle(t)`, W22-15) reads
  "never idle" until W20's accessor lands, so its one check ("cm_idle low while the write is in the port-B pipe")
  passes without testing anything; replaced when W22-15 is on main.
- **run_tests.sh** (shared; owner told): `suite_net` in SUITES; steps net (ref net-quick.log in quick mode, net.log in
  all), net-shuf, net-shuf2, skewtable (all only). `./run_tests.sh quick --check --no-build`: the three net steps pass
  with no LOG DIFFERS.
- **make check**: `tests/test_p4suites.cpp` runs net-quick, net-shuf, net-shuf2 byte for byte (debug builds; skipped in
  release, where the Fifo peaks aren't kept); clang debug 48 s, all pass. `suite_mem` builds in the timing builds as a
  stub that says it needs SIM_CHECKS (its statistics don't exist there).
- **make check (partial, 2026-11-12)**: copies 322 equal; gcc debug 72 of 72 run tests pass (test_ctl_suites.cpp, W23's,
  doesn't compile in gcc debug: -Werror=maybe-uninitialized inside std::function; owner told W23); gcc release 73 of
  73; clang debug and release not finished. EngineTest pushed as work in progress (owner: nothing only in the tree).
- **NetTest's port-B placeholder replaced** (W22-15 on main): `cm_idle` reads the core's `port_b` Link through
  `Sim::observe` exactly as W20's `phase_done` does (nothing arriving after the next cycle). Probed: low at the accept
  and the cycle after, idle from accept + cm_latency (ref: written at t + cm_latency); net.log still byte for byte.
- **EngineTest's write engine group equal** (stage peak, F3).
- **EngineTest: every printed line equal to engine.log and eng-shuf.log** (clang suite build): write engine, local copy
  (8 lines), stream runs, one-word exchange (5), radix sort, late groups (2), all-to-all (5), fence; drift's line too.
  363 of 365 assertions: the 2 left are W26's kinds ("invariant 3", "invariant 6"). The six runLocal groups on
  W20's `phase_done`; `new_phase` is a local no-op until W20's Chip::new_phase lands (nothing records the
  disjoint-buffer check yet, W26, so it changes nothing meanwhile). The radix sort's Net sizes the directors' logs for
  its 5 passes (`log_capacity` 5N + 64: ref's log is a list). engine-full (4 data sets at full size): the runner, later.
- **make check, the four builds on 6e0d9e2d (2026-11-12)**: copies 322 equal; clang debug 74 of 74, clang release 74 of
  74, gcc release 74 of 74, gcc debug 73 of 73 (test_ctl_suites.cpp, W23's, doesn't compile in gcc debug: owner told
  W23). gcc caught a `-Wdangling-reference` in suite_net and suite_engine (a TileRef temporary), fixed in 6e0d9e2d.
  (test_suite_replayer failed once in gcc debug only because an interrupted build had deleted suite_replayer; rebuilt,
  it passes.)
- **SearchBench** (`tests/suite_search.cpp`; Java's `%.Nf` and `%,d` in `tests/p4_java.cpp`): on W20's TRACE log
  (record_trace). **Every printed line equal to searchbench.log** (both N, every cycles-per-step row, both fits, the
  63-search totals): 54 of 56 assertions. The 2 left are \"no violations\" for each N: **finding F4 (for W20)**: the C++
  checker reports 3 violations ref doesn't: \"a command landed in command slot 0 before the core read the previous
  one (6.3)\" at cycle 139 on tile 0 and 143 on tiles 8 and 56, src 64, dst 126, payload 0x01800001 (the first
  broadcast command after the boot). Recorded by TileCore's port-B tracking (src/units/tile_ctl.h, CMEM_COMMAND_UNREAD;
  W20's). The machine's timing is ref's (every measured figure equal), so the check's bookkeeping of the boot ROM's
  slot-0 read differs from ControllerMemories': the three tiles nearest C&C 1, where the command lands soonest after
  the boot. Reproduce: `build/clang-suite/suite_search` (MODE=suite).
- **AgTest** (`tests/suite_ag.cpp`; ref's runLocal on `phase_done`; rd_running and rd_done read through STATUS with
  `TileRef::load`): worked example 26, queue bounds 64, mixed sizes 8, rulings 13 (\"dst 70\" included): all pass;
  computed fields 86 of 95. 197 of the 206 ported pass (seeds -1 and 4); figure 12's 34 wait on W22-16. The 9 left:
  - W26: the AG's stream-entry flags (read size 11, d = 70, d = 126, mode-4, mode-3 at 128, the misaligned
    destination and source, counter 0): 8 checks;
  - **finding F5 (the owner's call, as F3)**: the director's UB stops say \"director (tile N): ...\"
    (src/units/director.h:124, 136); ref names the unit \"traffic director\" (TrafficDirector.java:88, 98), and AgTest's
    `ub` requires it. Text only, in the FATAL path: \"traffic director (tile N): ...\".
  - In `computed`, a UB stop runs in suite::ub's forked child, while ref keeps the facts recorded before its stop in
    the machine; the parent runs to the cycle before the stop's (named in its message) so the flags checked after it
    can be there.
- **F5 done**: AgTest 198 of the 206 ported (seeds -1, 4); the 8 left are W26's stream-entry flags (its 1c). F4 is
  W26's (invariant 8's command-slot check), not W20's (owner).

### ReadPortGatherTest (2026-11-12)

`tests/suite_rdport.cpp` (prints as it goes, as Java's `out()`; `set_record_base` for ref's `gather.base`). Ported: read
port from C (rdport.c), reserved streams, gather (k = 1, 2, 3, 4, 8), gather counting and alignment, gather from C
(gather.c, C&C's phase log). **Every printed line ported is equal to rdport.log** (from C, the gather table's 6 lines,
gather from C's 64 cycles). Reserved streams (9) and counting (21) pass. What's left:
W22-18. **The read port's host queue (for W20)**: ref's `readPort.hostQueue(addr, dest)` (a read-port read queued from
    the harness, as a store to SRAM_RD_ADDR_QUEUE would), `readPort.idle(t)` and `readPort.allAccepted()`: the
    all-to-all under read-port traffic, the tolerance runs and the overlap check (three groups).
- **The write engine's writes by size** (ref's `writer.writtenBySize[n]`, counted where a write goes to big SRAM,
  WriteEngine.java:81): a statistic the C++ write engine doesn't keep: **the owner's call** (as the stage peak). The
  gather group's 5 \"value packets\" checks read it; a placeholder fails them meanwhile.
- W26: from C's \"the overflow (invariant 8)\" (the 65th store into a full SRAM_RD_ADDR_QUEUE: not recorded yet), and
  gather from C's \"no invariant violations\": the same 3 command-slot violations as F4 (cycle 77 on tile 0, 81 on 8
  and 56; payload 0x01801000), W26's invariant-8 command check.
- W22-14: the disjoint-buffer group.
- **run_tests.sh** (shared; owner told): the P4 steps still PENDING now name what each waits on (W22-14, W22-16,
  W22-18, W22-9 to W22-13, W26's kinds; agbench: not yet ported). Only NetTest is byte for byte, so it stays the only
  P4 suite in the runner and in make check (test_p4suites); each other suite joins both when its log is equal.
- **Quest 184** (claimed after re-reading NUMBERS.md, pushed): engine-full and rdport-full's ported groups on the C++
  against v1.3's logs (printed lines; the FAIL lines, W26's, listed). Task copy in `tasks/`.
- **SearchBench complete** (W26's F4 fix on main): byte for byte equal to searchbench.log, 56 of 56 (clang suite build;
  clang debug too, 17 s). In run_tests.sh (step searchbench, `suite_search` in SUITES; `quick --check`: no LOG
  DIFFERS) and in make check (test_p4suites).
- **AgBench complete** (`tests/suite_agbench.cpp`, G1): byte for byte equal to agbench.log (145 lines: the lookahead
  tables with and without stalls, the zero-length groups, start to the first request), 14 s on the clang suite build
  (1,500 machines). In run_tests.sh's full block (step agbench, `suite_agbench` in SUITES); not in make check (a
  full-only step in v1.3).
- **Quest 184 result**: build PASS; rdport-full's ported groups all equal (from C 1 line, the gather table's 7 with the
  65,536-value row, gather from C 1), the FAILs W26's (invariant 8; F4, fixed since). engine-full aborted: the Net's
  directors' logs (fixed size, 16,384) overflowed on the full-size all-to-alls (ref's logs are lists). Fixed: the
  all-to-all group runs on the plain chip (ref only uses the Net's chip there); engine and eng-shuf still equal.
  engine-full then checked locally (clang suite build): every printed line equal to engine-full.log in three blocks
  (lines 1-18; the all-to-all at full size, 15 lines, 85 s; drift and fence), the 2 FAILs W26's invariants 3 and 6.
- **Spec items 62 and 70 (Quest 185, for the docs session)**: `tests/p4_drift.cpp` over engine-full's data sets, on the
  runner (Intel Core i5-14400F, 6 P + 4 E cores, 64 GB, Linux 6.8.0, clang 18.1.3; machine.sh's record in Quest's
  results/185-w22-drift/machine.txt). **No drift on any run** (ERRORS [3] on 0 tiles, 0 AG drift events), the control
  (EngineTest's drift case) detected (tile 7, first at cycle 106, 158 events). **Send-port token waits 0** on every
  run, and the read side's window-full cycles 0: these runs send nothing through the send port (every packet is an AG
  read return), and no read ever waited for a token. Run cycles equal engine-full.log's (71,238 ... 82,025). Attempt 1
  printed the whole table (exit 0) but its last check matched the table's padding wrongly; fixed for the retry.
- **On main (2026-11-12): W22-9, W22-10 (W20), W22-13 and invariants 3, 6, 8, the AG stream checks (W26).**
  - **EngineTest complete**: engine, eng-shuf and engine-full byte for byte (365 of 365; engine-full 92 s locally).
    Live in run_tests.sh (engine-full in the all block); engine and eng-shuf in make check.
  - AgTest 206 of 206 ported (figure 12: W22-16). ReadPortGatherTest from C 516 of 516. MemTest: W22-14.
  - **RequestBusTest** (`tests/suite_reqbus.cpp`, on W20's ReqBus): from C (req.c, C&C 2 draining from the test) 6 of
    6. The rest needs:
W22-11. **A tile's request port from the test** (still open): ref's `req.setW(i, v)`, `req.send(t, w3)` (a REQUEST_SEND
    without the core, \"as a store at the current cycle would\") and `req.canPush(t)`: fairness, credits,
    back-pressure, the receive side, drops.
W22-19. **The delivered requests' records** (new): ref's `RequestBus.delivered` list, each with `deliveredAt` and
    `srcTile` (ReqBus::delivered() is the count): basics (the unloaded latency, `q.deliveredAt - ts`), fairness (the
    span of deliveredAt), credits (srcTile 10, 20).
- Quest 185's three attempts ran before the check's fix: its results removed for a re-run (Quest's commit 70b44a3).
- **On main (18617c56): W22-11, W22-12, W22-18 (W20).**
  - **ReadPortGatherTest**: traffic (3 lines: 3,120 both ways, the 9,527 overlaps), tolerance (quick 1 line; full's
    23-line table), overlap check (1 line) all equal, seeds -1 and 4; `rp_host_queue`, `rp_idle`,
    `rp_all_accepted`; overlap's C&C 1 sends through `Net::send` (ref's c.cnc.send at the cycle just run). Every group
    now but disjoint (W22-14).
  - **RequestBusTest**: the receive side 8 of 8, drops 4 of 4 (W26's root drops). Back-pressure **aborts**:
W22-20. **(for W20) `TileRef::req_send` takes one push a cycle** (a `Sim::put_late` on the core's `req_push` Link):
    ref's `RequestPort.send(t, w3)` appends to the FIFO directly, any number between two cycles (and flags \"a request
    pushed into a full request FIFO\" past its depth). RequestBusTest pushes 128 at once (back-pressure), 100 and 29
    (credits), 4 per tile (fairness); the second push of a cycle is FATAL (\"the unit put on this Hold in cycle 12
    already\"). Basics, fairness and credits wait on W22-19 too.
