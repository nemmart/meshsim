# WORKER26 notes (P2's second half: the checker's remaining kinds, S1, BusTest)

Branch `sim-w26`, from main d5cd16c7. Rows: B19 (with B20's ERRORS side as W20 left it), C2.

## The plan (go-ahead from the integrator, 2026-11-12)

Commit and push after each piece; each kind checked line for line against `ref` through `oracle/ref/CheckSelf.java`
(fixture `tests/mach/checkself.out`), gcc and clang, debug and suite builds. Before and after each change: `test_chip`,
`test_sort`, `test_t2_harness`, `test_cc_units`, `test_golden` unchanged.

0. **Baseline.** Debug and suite builds, gcc and clang; the five protected tests' output kept as the reference.
1. **The remaining kinds.** Each: the fact recorded where `ref` counts it (counters unchanged; D24), its text in
   `tests/checker.cpp`, a CheckSelf case. Order (the integrator's: the waiting suites first):
   - **1a. The CTL slot, finished (W23's CtrlTest `registers`, `invariants`).** The core's own `ctl.violate` sites
     (Controller.java: terminal traps that set no ERRORS bit, the ecalls among them; mret; an access past data/program
     memory; a store into the reserved first 16 bytes; the table window: HIST, PTR, the AG running), the WFI woken by
     other than GO, the request port's push into a full FIFO, PeripheralSlot's four (reserved window word, DMA_HDR and
     DMA_ADDR mid-transfer, full posts: invariant 16), a peripheral's live ERRORS bits [13:11] in invariant 15 (W25's
     F7: Stage B's planted fault, "ERRORS [100000000000] set: peripheral 1"), the Tracer's violations as chip violations (W14's texts, as W20 did
     the replayer's: `PERIPH_VIOLATION`), and the counter wraps and invariants 20/21 checked against CtrlTest's 384.
   - **1b. The request bus (W22's W22-13), the endpoints, C&C's lost receive.** The root's drop at a reserved or
     deferred target, the receive queue's overflow; the endpoints' bad packet, size 11, reserved mode; `rxLost`.
   - **1c. The per-tile engines.** The AG (checkEntry's eight, starts while running, table wraps), the read port (size
     11, queue full, destination alignment), the read queue (past big SRAM), the controller memories (command unread,
     reply overwrite strict), the read-return refusal (once per tile), HIST over 17 bits (once), late rotations (once).
   - **1d. Big SRAM's disjoint buffers (invariant 2).** `BigSram::new_phase()` (the read and write bits cleared, the phase
     advanced), the tracking and the DISJOINT fact; W20 adds `Chip::new_phase()` between cycles (calls it on every tile,
     wakes it). If the unit side's name changes, W20 hears through the integrator.
   - **1e. C&C 1's peripheral and the cores.** cc1p's messages (slot CC1_PERIPH), CcCores.check (tiles 64-66, slot
     CORES, the ERRORS-on-change rule), the cores' FIFOs in the overflow scan.
2. **S1 (invariant 24).** ref's HashMap and ArrayDeques replaced by fixed per-tile structures sized at configuration
   (approved): the director's acceptances to the write engine (a ring), chunk -> acceptance until big SRAM's area takes
   it (a small table, Java's last-put-wins), one FIFO per PTR and HIST index (pooled). Active only under SIM_CHECKS with
   `measure_s1`. Then W20's overflow case runs with `measureS1` back on, on both sides.
3. **BusTest (C2).** On `suite.h`, ref's check names kept, log-equal ("BusTest: 154 passed, 0 failed"); `throwsISE`
   as `suite::throws`. Each Java primitive group mapped onto the framework's port kind; **OverflowFIFO, RegisterBlock
   and SRAMMacro, with no C++ counterpart, as small test-only shims reproducing ref's classes** (integrator). The
   runner's `bus` step off PENDING. Then `REPORT.md`.

## The integrator's answers (2026-11-12)

- The replayer's violations as chip violations: done by W20 (6c746b30, `PERIPH_VIOLATION`); its line in `checker.cpp`
  is mine now. The Tracer's: mine, W14's texts the same way.
- BusTest's three primitives without a counterpart: test-only shims (above).
- 1a and 1b first.
- `Chip::new_phase`: W20's form (above). S1's design: approved.

## Environment

- The container had no clang and no javac: installed `clang lld openjdk-21-jdk-headless` (clang 18.1.3, gcc 13.3.0,
  javac 21.0.13).

## Progress
- **1a, the core's own checks** (Controller.java's `ctl.violate` through the block): `CTL_HALT` (ecall, ebreak, a fetch
  outside the memories, a misaligned jump, a trap inside the handler; rv32's `stop_loudly` takes the text's detail),
  `CTL_MRET`, `CTL_ALIAS` (memWord: past data or program memory), `CTL_DMEM_RESERVED`. A test's host store
  (TileCore's harness-only path) sets the core's `ck_t` first, so its facts carry its own cycle. CheckSelf: the core
  case (a program per tile, poked, then C&C's CMD_JUMP and GO), 72 violations line for line (gcc and clang suite, clang
  debug). The five protected tests the same as main's (clang suite).
- Merged main (W20's requests 9 and 10): both cases kept in CheckSelf.java (mine first, as test_mach's merge put them),
  `checkself.out` regenerated from ref; test_mach line for line (clang and gcc suite).
- **The table-window checks: exactness (integrator, 2026-11-12)**: the stores and the AG publish their busy state on
  same-cycle Wires the core reads, only under SIM_CHECKS; the outputs and their cycles proposed before touching the units.
- **1a, the table window (Controller.window, 4.10)**: `CTL_TABLE_BUSY`, judged by the core on three same-cycle Wires,
  declared and wired only under SIM_CHECKS (add_g1): `t<k>.hist.busy_hist` and `t<k>.ts.busy_ptr` (t + 1 driven at the
  store's step at t when busy at t, bit 63 when busy at t - 1 too), `t<k>.ag.running` (t << 2 | before << 1 | after,
  driven at a step that changes `running`). An instruction at t is judged on t; a test's store (TileRef::store) on t - 1,
  as ref's harness store between cycles sees the flags. CheckSelf: CtrlTest's `inv` (the AG case, by instructions) and a
  Net case of a test's HIST and PTR stores during count-on-arrival (14 lines), line for line (clang and gcc suite, clang
  debug). Not covered against ref: HIST/PTR busy met by an instruction (same Wires, the t + 1 test), and a test's HIST
  load (TileRef::load's own path: no check yet).
  **Levels: the core moves from level 2 to 3** (t<k>.core and cc<n>.core; every other unit's level unchanged): before,
  it shared level 2 with the AG, the director, the write engine and the gather, run first among them (add order); now it
  runs after them. **Waiting for the integrator's ruling** (the go-ahead said to bring a reordering back). The five
  protected tests are the same as the merged main's, clang suite and clang release.
- **The checker's report order fixed** (W20's sort was slot-major): ref's checkInvariants is tile-major within the
  per-tile checks (tile 7's director message before tile 10's register-block message, cycle 150 of `inv`).
- Found for W20: ref's `Net.at` packets are fed only by `Net.step`/`runAll` (`c.run` doesn't feed them); the C++ Net
  feeds them under `c.run` too. CheckSelf's case uses `step` on both sides.
- **The core's level 3: accepted (integrator, 2026-11-12)**, on the condition that order is shown not to matter on the
  bigger workloads in both builds. Done, clang suite and clang release, the merged main plus the Wires:
  - test_sort's 16K sort, every golden point (sort16k-fp regenerated from ref: sha256 b15c5681... as HASHES; KeyGen's
    uniform keys 3db703f3...): plain, SORT_OPT=shuffle, stepall and instance, all equal;
  - T2's boot (test_t2_harness: one domain, the 6-domain split, shuffle seed 3, instance-major, and step-all added for
    this run only, not committed): equal to ref's boot trace in every case;
  - M1's goldens (test_golden: direct, engine, and shuffle seeds 1-3, instance-major, step-all): equal.
  So the table-window commit (94f1e9a9, "NOT FOR MERGE") is ready to merge; merged by the integrator with 09753248.
- **1a, invariant 8 and `unreadOverwrites`** (ControllerMemories' read tracking): `CmemTrack` (rv32.h), owned by the
  tile core (`TileCore::track`, every build, out of the digests): the unread-reply bitmap (1 KB), command slot 0's
  unread flag, **`track.overwritten_unread`** (ref's `cmem.overwrittenUnread`, PhaseLog's `unreadOverwrites`: W20/W24
  read it per tile) and `track.unread_commands`. Marked at the core's step from port B's write of t (`track_port_b`,
  ref's cycle: the write reaches the core at t + 1), whatever the core's state (ref's cmem writes after a halt too); the
  core's loads and stores clear (`loaded`, `stored`), and a word it touched at t ends up read, as ref's same-cycle order.
  Facts `CMEM_REPLY_OVERWRITE` (reported under strict_replies) and `CMEM_COMMAND_UNREAD` (check_command_read), the
  checker applying the switches. CheckSelf: inv's 16 overwritten-unread replies, and inv with strictReplies (16
  messages), line for line (clang and gcc suite, clang debug). Protected tests the same as merged main's (clang release
  and suite); the 16K sort equal (clang release). Not covered against ref yet: the command slot's fact, the same-cycle
  touch; a test's load (TileRef::load) doesn't clear yet (when a suite needs it).
- **1a, the rest** (F7 withdrawn by W25: the peripheral ERRORS bits were reported already):
  - `CTL_WFI_WOKEN` (TileControl.takeWake: a wake by another event than GO with done set, the done gate off), at the
    core's two wake sites (rv32's `woken_in_done`; the Env's `done_gate`).
  - `SLOT_VIOLATION` (PeripheralSlot.violate, through the block): SlotCore::violate records every slot flag (a reserved
    window word stored, DMA_HDR or DMA_ADDR changed mid-transfer, a post to a full counter with its depth), the core's
    load of a reserved window word (rv32's read path; the slot never sees it), and SlotCore::accept a write into C&C 1's
    peripheral region or a stub engine's (ref's host.violate; F_PERIPHERAL).
  - `TRACER_VIOLATION` (Tracer.violate through its slot): every Tracer fact, ref's (W14's) texts in
    `Tracer::violation_text` (V_UNDEFINED now carries the ALU's text as its second argument, as the replayer's does).
  - `CTL_REQ_FULL` (RequestPort.send into a full FIFO; a harness push only: the core checks canPush), dated at the send.
  CheckSelf: the slot case (window 1's words 8 and 22 stored, 10 loaded: 48 lines) and the WFI case (13 lines), line for
  line (clang and gcc suite, clang debug). Not checked against ref (no case reaches them yet): DMA_HDR/ADDR mid-transfer
  and the full post (controllers' DMA, credits), the C&C 1 and stub region writes (cc_cores), the Tracer's, the full
  request push. Protected tests the same (clang release, suite); the 16K sort equal; the slot, request-bus and Tracer
  tests pass (clang suite).
- **1b, the request bus, the endpoints, C&C's lost receive** (W22-13):
  - `REQBUS_NO_RECEIVER` (the root drops a request for target 3, "no receiver: deferred", or 4-7, "reserved"; at the
    root's choice), `REQBUS_QUEUE_OVERFLOW` (a receive queue past its depth: the root's credits prevent it), both chip
    facts (tile -1) with ref's texts.
  - `EP_BAD_PACKET`, `EP_SIZE_UNDEF`, `EP_RESERVED_MODE` (Endpoint.send): `units::endpoint_checks(t, address, packet)`
    (cnc.h), called by C&C 1's sends (Cnc::send_packet) and the endpoints' harness sends (Endpoint::step). The checker
    takes ref's text rule (the last violation of the highest endpoint that ever had one: Chip.checkInvariants' `el`).
  - `CNC_RX_LOST` (C&C's receive FIFO full).
  CheckSelf: the root's drops (programs sending to targets 3, 5, 7: 48 lines) and C&C's receive FIFO full (depth 4),
  line for line (clang and gcc suite, clang debug). Protected tests the same (clang release, suite); test_reqbus passes.
  **For W20 (the harness):** ref's Net feeds `cncSends` through `cnc.send`, i.e. Endpoint.send's checks; W20's
  `Net::step` puts them with `Sim::put_late` directly (and `Net::send` likewise), so their facts need one call to
  `units::endpoint_checks(c.cycle(), address, p)` there. Until then CheckSelf's endpoint sends are left out.
  Not checked against ref: the queue overflow (unreachable by design), a reserved mode (UB at the director).
- **Kinds recorded but no case reaches yet** (check each when a suite does; likeliest W23's CtrlTest and RecipeTest,
  W22's RequestBusTest): the command slot's CMEM_COMMAND_UNREAD; a port-B write as the core reads that word; a test's
  load clearing invariant 8's bit (TileRef::load); HIST/PTR table-window busy met by an instruction; a test's HIST load
  (no check yet); the slot's DMA_HDR/ADDR mid-transfer and full post; the C&C 1 and stub region writes; the Tracer's
  violations; the full request push; the receive queue overflow; the endpoints' checks (after W20's call above).
- **W22's F4 fixed (SearchBench's 3 extra command-slot violations)**: ref's TileControl.endCycle(t) at a GO clears
  `cmem.slot0Unread` when the core's pc is in the boot ROM (the ROM takes the boot command at GO; its own load of slot 0
  is incidental), so the first command queued behind the boot may land before the program reads slot 0. Here: a fourth
  SIM_CHECKS-only Wire, `t<k>.ctl.go_seen` (t + 1 driven at the block's step when it takes the GO of endCycle(t)),
  read by the core after port B's write of t (ref's order: cmem at t, then endCycle(t)); the core's pc at that step is
  ref's at the end of t. No level changes (the block is below the core already). CheckSelf: `search` booted with a
  command queued right behind (ref: 0 violations; without the rule the C++ gave F4's three lines), line for line (clang
  and gcc suite, clang debug); SearchBench 56 passed, 0 failed (clang suite); protected tests the same (clang release,
  suite). `track.unread_commands` follows the rule only in the check builds (the Wire's); `overwritten_unread` is
  unaffected.
- **For 1d**: the same endCycle also calls `tile.sram.newPhase()` at every GO: the disjoint-buffer phases advance at each
  GO, besides the harness's `Chip.newPhase`. So BigSram::new_phase needs the GO too (a Link from the block, or the GO
  Wire above); to agree with W20 and the integrator before 1d.
