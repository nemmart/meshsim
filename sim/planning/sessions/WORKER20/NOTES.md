# WORKER20 notes (P2: the machine harness, the checker and the outputs)

Branch `sim-w20`, from main e90facf. Rows: C1 with B18, B19, B20, H1, H2, H3, H6, B25, C0's runner, C2.

## The plan (agreed with the owner, 2026-11-10)

Commit and push after each step; each step checked against `ref`.

0. **`sim/tests/suite.h`, alone and first** (owner: W21 builds its Tracer suites on it now). Java's `T` in C++: `group`,
   `pending`, `check`, `eq`, `eqo`, `throws` (Java's `throwsISE`), `ub`, `done`, every line and count as Java's `T`
   prints them. Checked by one driver on both sides (`oracle/ref/TSelf.java` against `tests/test_suite.cpp`), its output
   committed as a fixture.
1. **The machine harness** (C1, B18), header `sim/tests/mach.h` published before its body, for P4-P6:
   - the full machine (`Config.machine()`): boot an image through C&C's model or through the cores (`Mach.boot`,
     `bootCores`, `hostBoot`, `stageCc1`, `phase`, `mbox`), run until a condition, stop, peek and poke SRAM, the
     controller memories and the registers, exit values and the halt summary, images with their symbols;
   - the coreless machine (`Config.defaults()`, `Net`, `Tile.set*`): tiles in RUN without a controller, GATHER a drain,
     a harness feeder unit per tile driving the read-return hold, the read-queue clients and C&C's sends (`Net.at`,
     `rd`, `cncSends`), the director's arrival logs, the engines configured from the harness; `A2A`'s data.
   Checked on regs, wfi and a2a against golden-m2, and a small coreless run against a `ref` driver in `tests/ref`.
2. **The checker** (B19, B20): units record fixed-size facts (cycle, tile, check, arguments) in a list sized at startup
   (D24); formatted after the run into `ref`'s exact strings, in `Chip.checkInvariants`' scan order, `scanOverflows`
   every 16 cycles, `maxViolationMessages`, the per-check switches; the missing checks (disjoint buffers, S1, invariant 8
   strict, read-port overlap, overwritten command words, unexpected arrivals, ERRORS once per tile and bit). Chip reset
   (hard, soft): every port flushed, every unit to power-on, the memories kept. Checked by provoking each violation in
   C++ and in a Java driver in `oracle/ref/`, the messages compared.
3. **`Config`** (H1, B25, H2): `MachConfig` with the fields some v1.3 driver, kit or sweep sets (D41 Q4), the rest
   listed as dropped; one `cfg.<field>=` front end; StateFp as every driver's fingerprint (D41 Q5); the H2 options.
4. **The outputs**: PhaseLog's CSVs (H3) byte-identical on AccountingTest (quick here, full on the runner); the TRACE
   store log (H6).
5. **The runner** (C0): `sim/run_tests.sh [all|quick]`, v1.3's step and log names, PENDING for suites not yet ported,
   the FAIL summary, `--check` against v1.3's logs with timing masked; **BusTest** (C2) its first suite, on the
   framework's port kinds (`throwsISE` as a death check), "BusTest: 154 passed, 0 failed".

## Wave 2's requests (NEXT.md, 2026-11-11): the order taken (the integrator's)

1. `sim/run_tests.sh` (C0). 2. wait_until (W23, W24) and a core's loads and stores from a test (W23). 3. the replayer on
tile 0 only with expect = 1, parked tiles and parkedUntouched, a row hook on the replayer (W25). 4. PhaseLog and the
Config fields (W24). 5. the request bus from a test and C&C's queued soft reset (W23). 6. S1 and the checker's
remaining kinds, interleaved as the suites ask.

- **C0 done**: `sim/run_tests.sh [all|quick] [--check] [--no-build]`: v1.3's steps, logs and summary; the suites built
  with MODE=suite; `step LOG TAIL "what" REF -- cmd` and `pending LOG "what" "who"` for each worker's lines; --check
  compares each log with its reference, timing masked. W21's memsys, rvc and tracer run (equal under --check).
- **wait_until and a test's loads and stores done**: `Cc::wait_until(cond)` (W23's form: CncStep::WAIT_HOST, harness-
  only, released between cycles; C&C moves on the next cycle; test_mach checks the form); `TileRef::load(t, ea, width)`
  (synchronous, as ref's: the memories with port B's write of the cycle just run applied, the registers as the block
  publishes them for t, HIST) and `TileRef::store(t, ea, v, mask)` (ref's bus-error reasons synchronously; the store
  made by the core in its step at t through the instruction's path: TileCore's host stores, harness-only). NetSelf's
  core case (memory, PARAMS, STATUS, ERRORS, a counter, the boot ROM, the bus errors, a counter store through the bus,
  a send through ROUTER_*) line for line. Not yet: loads with a side effect (EVENTS, REQUEST_RECEIVE), the peripheral
  windows (fatal, asking).
- **W25's machine done**: `MachConfig::test_periph_tile` (ref's testPeripheral: the replayer on that tile only, periph_type
  0 elsewhere; `G1Config::periph_only`, harness-only, in add_g1), `Cc::set_expect` (cc1p.expect), `MachConfig::parked` and
  `Chip::parked_untouched` (ref's check; the parked tiles are simulated all the same: asleep in their ROMs they cost
  little, and the check says whether anything reached them; `Sim::in_flight_into`, harness-only), `Chip::on_row` (ref's
  rowListener: the replayer's row buffer drained between cycles, `Replayer::row_base`/`drained`, harness-only). MachSelf's
  StageB machine (lone and full) line for line, its six register-block violations included (the first CTL facts: reserved
  bits, record base, counter wraps; also done early, done with GO masked, reserved accesses, SRAM_RD_DEST size). The row
  hook is unchecked against ref yet (it needs a replay window): W25's StageB will.
- **W22's asks 1-6 and 8 done** (ask 7, the framework FIFOs' peak, is the owner's call):
  1. `Cc::received` / `Chip::received(cc)` on either machine (the endpoints' receive traces on every chip without
     cc_cores; an overflow of the trace is reported when it is read).
  2. `Chip::reset_stats` (ref's resetStats: the router's FIFO peaks to their occupancy and the units' counters ref
     zeroes; the framework's FIFOs keep no peak).
  3. `TileRef::send_put(t, ...)` (t = the cycle just run: put_late; the next: the core's host send), `send_empty`,
     `send_refusals` (`Sim::hold_refusals`, harness-only).
  4. `record_writer`/`writer_log`, `record_gather`/`gather_log` (harness-only logs in WriteEngine and the drain).
  5. K below kMin refused at build, ref's message (a FATAL: `suite::throws` catches it).
  6. the director's remaining checks as facts: strays at acceptance (invariant 9: the director reads the counters'
     view through `regs`, harness-only wiring `G1Config::stray_check`; ref's pendingAdd of the same cycle isn't seen),
     uncounted mode/region/source and the overlay after a command (22, 6.2), the reserved bytes, command and reply
     blocks (11, 22), past data/program memory and past big SRAM (19), the command after overlays of the other kind
     (25); the write engine's write past big SRAM (19). CheckSelf's director case line for line.
  8. `MachConfig::step_all`, `instance_major`.
- **The runner's riscv and periph steps**: `equiv LOG "what" "equivalent" -- $S/test_x` (pass or fail, the log names
  the equivalent): riscv by test_golden (every program's retire trace equal to ref's), periph by test_tileslotconf
  (W15's TileSlotConf). periph-shuf stays PENDING (no shuffled run of TileSlotConf yet).
- `Chip::reset_stats` restarts the framework FIFOs' peaks too (the write-engine input, the GATHER queue: W22's
  `Sim::fifo_restart_peak`, under SIM_CHECKS). The runner's mask: an `inputs:` line's first field (W24, approved).
- **Known gap, the stray check (invariant 9): a counter store of the same cycle as the acceptance** (ref's
  `ctl.pendingAdd(c, t)`) isn't seen: the core's store reaches the register block a cycle later, so the director's view
  lacks it and reports a stray ref doesn't. Not cheap: judging a cycle later (when the view has it) moves the stray's
  fact after the same packet's later facts (the command block, past memory), and ref reports the last one per cycle, so
  the whole packet's facts would have to wait with it. Recorded; the owner hears when a suite's log reaches it (then
  fixed, or a D30 case).
- **W25's F2, the chip side**: the replayer's violations (ref's `violate(t, m)`: its list and the slot's `host.violate`,
  which ref sends to the tile's register block) recorded as chip facts, `Kind::PERIPH_VIOLATION` in slot CTL; the text is
  the replayer's own (`Replayer::violation_text`, factored out of W25's `list_add`, the list's "t<me> @<t>: " left off).
  One case line in checker.cpp (W26 owns the kinds' texts from 2026-11-12: told). Not yet checked against ref: no
  replay window here; W25's StageB lines (`meshsim: violations ...`) are the check. The runner's quick --check equal.
- **W23's requests 9 and 10**: `TileRef::set_errors(bits)` (ref's `ctl.conditions |= bits` between cycles: in the
  register block's state from the next cycle on, its view, its error line and the checker at that cycle's end; ORed in
  after the step's facts and line, before the view: `TileCtl::host_cond`, harness-only); `Chip::set_check_errors`,
  `set_strict_replies`, `set_max_violation_messages` (from the next cycle on, the report through the cycle just run made
  first) and `clear_violations` (ref's `violations = 0` and the messages cleared). CheckSelf's case line for line.
  W23's stand-ins in tests/ctl/ctl.h can go.
- **W25's F6 fixed**: the slot freed whatever row buffer the replayer pointed at (the row hook's vector, after
  `Chip::on_row`): a double free. The slot now frees only the buffer it allocated (`SlotCore::Ref::rows`).
  test_mach's `mach_row_hook_destroyed_cleanly` fails under ASan without the fix. W25 can drop stageb.cpp's workaround.
- **W22's F2 fixed**: `TileRef::mem_idle` read big SRAM's area queues modulo the array's 16, not the configured
  depth (8): after a wrap it read a stale slot. Now `BigSram::at(a, j)`; test_mach's `mach_mem_idle_sees_a_wrapped_queue`
  fails with the old indexing.
- **`tests/java_format.h`** (the owner: one shared helper): `jfmt::fixed` (Java's %.nf: the shortest digits, HALF_UP)
  and `jfmt::grouped` (%,d), W24's code with W24's test values and W23's, W25's and PhaseLog's, each checked against Java
  (`test_java_format`). W24, W25 (and W23's ctl.h) switch to it in P8's cleanup.
- **PhaseLog (H3) done**: `tests/phase_log.{h,cpp}`, `Chip::phases()` (attached at the first call; runs cycle by cycle
  then; the hooks after each cycle: the READY pulses (TileCtl's ready_pulse_at, W23's), C&C's all-READY and GO stores),
  ref's counters (the cores' caught up as ref's syncCounters), peaks, report() and csv() (Java's formatting). New
  statistics, counters only: the read queue's latency sum and count per client (under SIM_CHECKS, beside W22's latMax),
  the table stores' request-FIFO peaks, the read port's peak. `unreadOverwrites` 0 until W26's tracking. The debug and
  suite builds only (its FIFO peaks and latencies are kept there; FATAL otherwise). MachSelf's PhaseLog case (the a2a
  with AccountingTest's namer: report and CSV, seeds -1 and 4) line for line, gcc and clang. accounting-full's CSV is
  W23's step (AccountingTest) on the runner.
- **Two fixes**: the register blocks of C&C 1-3 as cores take the test's writes too (`host_on`; W23's errorPath, C&C 2's
  ERRORS bit); a peripheral's violation is reported at t + 1, as v1.3's (W25's F8: ref's slot passes it to the block,
  whose counter the checker sees at the next cycle's end). (The text has no cycle: checker.cpp unchanged.)
- **`Cc::phase_log`** (ref's CnC.phaseLog: {all READY seen, GO} per GO; the Cnc unit writes it into the harness's
  array, harness-only) and `phases_logged()` (its size, for run conditions). MachSelf's a2a prints it, equal to ref's.
- Found on the way: `peek_local` now applies port B's write of the cycle just run (it reaches the C++ core a cycle
  after ref's memory has it), so memory read right after a run that stops at quiescence is ref's.

## The owner's answers (2026-11-10)

- **Q1.** The checker is a compile-time switch: on in debug and in the suite build, off in the timing builds (D6).
- **Q2.** The PhaseLog reference is regenerated from `sim/ref` and committed as the fixture (finding F1).
- **Q3.** The reset flush and the violation list are framework additions; tell the owner if either changes an
  existing operation.
- New Java drivers go in `sim/oracle/ref/` (W19 moves them to `sim/oracle/`).

## Findings

- **F1. `ref`'s committed `results/phases-a2a.csv` and `results/accounting.log` disagree**: the boot phase's GO is at
  cycle 98 in the CSV (cycles 930) and at 91 in the log (cycles 916). Today's `ref` puts the a2a boot's GO at 98
  (MachSelf: `runAt 98`), so the **log** is the stale one, not the CSV (corrected 2026-11-10; first written the other way
  round). Neither is used as a reference: the fixture is regenerated from `sim/ref`.

## Environment

- The container had no clang (D18 needs it): installed `clang lld` (clang 18.1.3; gcc 13.3.0); JDK 21.0.12.

## Progress

- **Step 0 done**: `tests/suite.h`, `suite.cpp`; `tests/test_suite.cpp` runs TSelf's sequence and compares with
  `tests/suite/tself.out`, written by `oracle/ref/tself.sh --fixtures` from v1.3's own `T.java` (every method, passing
  and failing: 7 passed, 13 failed, exit 1). Equal on gcc and clang, debug and release. A suite links `tests/suite.o`
  (the Makefile's `test_suite` rule shows how). For W21: `throws`/`ub` run their function in a forked child.
- **Step 1, the header published**: `tests/mach.h` (MachConfig, Chip, TileRef, Cc, Mach, Net, A2A, `fields`), with
  `tests/test_mach.cpp` pinning `mach::fields` to the values ref's `Fields.java` prints. The body comes next.

- **Step 1, the machine as specified**: `tests/mach.cpp` (MachConfig, Chip, TileRef's memories and controller, Cc,
  Mach with `boot` and `boot_cores`, A2A's data and `verify`), `tests/mach.mk`. Checked by `oracle/ref/MachSelf.java`
  (v1.3's own Mach, run in a scratch copy of ref/meshsim's layout with the committed images; `oracle/ref/machself.sh`)
  against `test_mach`: regs, wfi (with a `run(n)` then `run_until`), a2a unshuffled and shuffled (seed 4), and the
  boot with C&C 1-3 as cores, line for line (`tests/mach/machself.out`), gcc and clang, debug and release. The owner's
  protected tests (test_chip, test_sort, test_t2_harness, test_cc_units, test_golden) unchanged, clang release and debug.

- **Step 1, the coreless machine and Net**: MachConfig::defaults (coreless, the GATHER engine a drain), TileRef's
  set_*/pulse/remaining/mem_idle/ts_busy and the table and HIST pokes, Chip::quiescent, A2A::load and run, Net (at, rd
  with the test read clients, the directors' logs, late, run_all, max_router_peak). Checked by `oracle/ref/NetSelf.java`
  (`netself.sh`; fixture `tests/mach/netself.out`): fan-in, a stream, a broadcast, a self-send, the drain, the read
  clients, the coreless a2a (seeds -1 and 4, gaps 0 and 8), line for line, gcc and clang, debug and release. The
  protected tests the same as main's (e563634, a worktree) in clang release and debug; every clang-release test passes.
  Two findings on the way: the register block's state between cycles is ref's end of the cycle before (its endCycle of
  t runs at t + 1), so harness reads go through `block_at_end` and harness writes wait for that endCycle (TileCtl's
  HostWrites); and the router's FIFOs are its own state, not ports, so `quiescent` reads them.
- **Step 1, the controllers' paths**: Net::cnc_send (ref's cncSends, put by C&C 1's model at T - 1), Net::send (ref's
  `ccs[c].send(c.cycle, ...)`, exact through `Sim::put_late`), Net::received (ref's `Endpoint.received`), clear_logs and
  pending over the endpoints. NetSelf's controllers case (C&C 1's broadcast, a command and back-to-back sends, tiles to
  C&C 1-3, direct sends' cycles, C&C 2 to C&C 3, C&C 2's broadcast; seeds -1 and 3) line for line, gcc and clang, debug
  and release; the protected tests the same as main's; all 63 clang-release tests pass.
- **Not yet:** Cc::received and ready_bits (C&C 1 on the machine as specified), TileRef::quiescent (use Chip::quiescent).

- **Step 2, the checker: its fact interface published** (`src/check.h`, `src/check.cpp`; MODE=suite in the Makefile):
  the units record fixed-size facts (Kind, cycle, tile, four arguments) into the harness's list; the harness formats
  them into ref's strings, in ref's report order (check.h's Slot), between cycles.
- **Step 2, first kinds**: `tests/checker.{h,cpp}` (the formatter: ref's LinkWord.describe and whyBad, one message per
  slot per tile per cycle with the last fact's text, Chip.violation's count and cap, violation_report), wired into Chip
  (installed at every run; processed after each). Recorded so far: the injection path's six checks, the director's
  bad packet, size 11, count-only on counter 0 and read-port overlap. `oracle/ref/CheckSelf.java` (`checkself.sh`,
  fixture `tests/mach/checkself.out`): equal in the debug and suite builds (gcc, clang); the release builds skip it.
- **Step 2, rules**: ERRORS (invariant 15: the register blocks record their ERRORS and error line at ref's end of
  cycle; the harness applies ref's condSeen), ref's config switches (check_errors, check_unexpected, ...), the once-only
  rules, the report through the cycle just run (`finalize_checks`: the blocks' end-of-cycle view, so a run that stops
  on the cycle an error lands still reports it), invariant 1 (the harness reads the router's FIFOs and the framework's
  first overflow, `Sim::fifo_overflows`, a harness-only addition; ref's FIFO names and scan order). CheckSelf: ERRORS and
  overflow cases line for line.
- **Found missing in C++:** S1 (invariant 24: acceptance to the area queue, PTR and HIST write-backs, measureS1, on by
  default in ref): the overflow case turns it off until it is built.
- **Next in step 2**: S1; the remaining kinds (the director's uncounted/boot/command checks and unexpected arrivals, the
  register block, the controller's memories, the read port, the AG, the read queue, the write engine, big SRAM's
  disjoint buffers, HIST, the rotations, the request bus, C&C, the endpoints), ERRORS (invariant 15, condSeen), the
  overflow scan (ref's FIFO names), the config switches (check_unexpected, check_errors, ...), then reset.

## What the body needs outside `sim/tests/` (additions only; the owner hears if any changes an existing operation)

- **Framework (done):** `Sim::wake_all()` and `Sim::in_flight()` (config.h, config.cpp, threads.h/.cpp; marked
  "W20 (harness-only)"). `wake_all` is ref's `wakeAll` at the start of `run`/`runUntil`; safe by the step-all rule: an
  unasked step with nothing delivered changes nothing); `in_flight` is the data-in-motion half of ref's `quiescent`.
- **Units (done; approved 2026-11-10; each marked "W20 (harness-only)", off unless the harness sets it):**
  - `add_g1`: a coreless tile (ref's `core.enabled = !coreless`: the core never executes) and the GATHER engine as a
    drain (`realResponder` off);
  - the injection unit: a harness input into the read-return holding register (`Net.at`) and harness-fed read-queue
    clients (`Net.rd`, `testReadClients`);
  - the director: an arrival log sized at configuration (`dir.log`; fatal when exceeded);
  - `TileCtl`: the harness's register writes (DMA_CONFIG, the record base, TILE_PULSE's start, the write counters)
    applied in its step right after its endCycle, so the engines see them in the next cycle as ref's direct pokes do;
  - `TableStore`, `HistStore`: a harness poke of PTR or HIST republished on `ptr_view` / `out` at the next step.
- **Framework (done):** `Sim::put_late(unit, hold, words)`: a Hold put as of the cycle just run, into the next cycle's
  delivery bucket (ref's harness puts between cycles); one domain only. `Sim::in_flight(shifted, ignored)`: Link words
  into `ignored` types (C&C's row collectors and C&C: ref's READY and error lines, wires there) never count; unit types whose Link words due at now() are ref's previous cycle
  `shifted` as before (the register block; the core's port B); with both lists empty every word counts (the default).
- **Units (done), the endpoints:** `HarnessSends` in `Cnc` and `Endpoint` (cnc.h): the harness's scheduled sends and the
  receive trace, null on every other machine.
- **C&C's queue between cycles:** CncProgram's vectors reserved at build, steps appended between cycles behind a
  release point (cnc.h's mechanism), `call` and `at_all_ready` run from `Cnc::call_at` after the cycle they ran in.
- **Open for P5:** CnC's `waitUntil(predicate)` (a lambda evaluated inside C&C's step in ref) is not declared yet:
  CondTest and RecipeTest use it; its form is settled with the first suite that needs it.
