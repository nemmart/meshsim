# WORKER20 report: P2, the machine harness, the checker's first half and the outputs

P2 (V135-INVENTORY rows C1 with B19/B20, H1, H2, H3, H6, B25, C0's runner): the harness every v1.35 suite is written on,
`tests/mach.h`, in C++ and line for line with v1.3's Java harness classes (`Mach`, `Net`, `Chip`'s test surface, `CnC`'s
queue, `PhaseLog`), checked against `sim/ref` itself. Branch `sim-w20`, everything merged into main (last: 5cefc440).
The working log, with every finding and figure, is `NOTES.md`; this report is the summary.

## How it was checked

Every piece of the harness is checked against ref by a Java twin in `sim/oracle/ref/` that drives ref's own classes
through the same sequence, and a C++ case in `tests/test_mach.cpp` that must print the same lines:

| oracle driver | fixture | what it covers |
|---|---|---|
| `MachSelf.java` (`machself.sh`) | `tests/mach/machself.out` | the machine: boots, the a2a (plain, shuffled, kit-configured), StageB's machine, PhaseLog, StateFp, the TRACE log, the reset |
| `NetSelf.java` (`netself.sh`) | `tests/mach/netself.out` | the coreless machine and Net on the machine as specified: packets, reads, controllers, the request bus, hooks, the read port, figure 12, the UB stop |
| `CheckSelf.java` (`checkself.sh`) | `tests/mach/checkself.out` | the checker: its messages, counts and caps, the switches mid-run |
| `TSelf.java` (`tself.sh`) | `tests/suite/tself.out` | `suite.h`, Java's `T` in C++ |

`<driver>.sh --fixtures` regenerates a fixture from ref; `--check` compares. `test_mach` passes on gcc and clang, debug,
release and the suite build (MODE=suite: release flags with the checker's facts). In the release builds the checker's
facts are compiled out, so the comparisons leave the violation counts and reports out of both sides there. The five
protected tests (test_chip, test_sort, test_t2_harness, test_cc_units, test_golden) were compared with main's baseline
after every change to a unit or the framework: unchanged throughout.

## The harness (`tests/mach.h`)

- **`MachConfig`**: ref's `Config` (`defaults()`, `machine()`), with the harness's own fields (`coreless`,
  `real_responder`, `test_read_clients`, `test_periph_tile`, `test_peripheral_tile`, `parked`, `reqbus_host`,
  `log_trace`, `step_all`, `instance_major`, `shuffle_seed`, the log capacities).
- **`Chip`**: the machine. Runs (`step`, `run`, `run_until`: cycle by cycle while a hook, a wait, PhaseLog or the drain
  is attached), `quiescent`, `all_stopped`, the violation report and switches, `reset_stats`, `new_phase`, `reset`,
  `phases()`, `reqbus()`, `received`, `digest`, `log`, `ag_report`, `on_row`, `parked_untouched`.
- **`TileRef`**: a tile. Memories and tables (pokes, peeks, `sram_*`, `ptr`, `hist_*`, `peek_local`/`poke_local`), the
  register block as ref has it at the end of the cycle just run (`block()`), loads and stores through the core with
  ref's results and side effects, the request port (`req_set_w`, `req_send`, `req_can_push`), the read port's host
  queue, the send hold, the write engine's request lines, sticky EVENTS and ERRORS bits, a scheduled GO, the logs
  (writer, gather drain, TRACE), `mem_idle`, `phase_done`, `quiescent`.
- **`Cc`**: C&C 1's Java model's queue (`load_words`, `command`, `send`, `go`, `run_when_ready`, `at_all_ready`,
  `wait_cycles`, `call`, `wait_until`, `reset`), `set_expect`, `phase_log`, `drain_reports` with `reports`,
  `reports_of`, `requests`, `results`.
- **`Net`**: ref's Net on any machine but the one with C&C 1-3 as cores: `at`, `rd`, `cnc_send`, `send`, `step`,
  `run_all`, the arrival logs, `late`, `pending`. Its schedules are fed by `Net::step` and `run_all` only, as ref's.
- **`ReqBus`**: ref's RequestBus from a test (`pending`, `get`, `peek_request`, `receive`, `controller_send`, the queue
  sizes, credits, underflows, dropped, delivered and `delivered_log`).
- **`Mach`**, **`A2A`**: ref's Mach and A2A (images, boot, the a2a's data and checks).
- **`suite.h`** (Java's `T`: groups, checks, `throws` and `ub` in a forked child), **`java_format.h`** (Java's `%.nf`
  half up on the shortest digits, and `%,d`; the one shared helper).

## PhaseLog (H3)

`tests/phase_log.{h,cpp}`, `Chip::phases()`: ref's per-phase accounting, its counters, peaks, `report()` and `csv()`,
with Java's number formatting. Equal to ref on the a2a (AccountingTest's namer) at seeds -1 and 4, and on search.c at
SRAM read latency 4 (the read port's peak by ref's rule, W24's F6). It runs in the debug and suite builds only, since
its FIFO peaks and latencies are kept there. `unreadOverwrites` is W26's counter, corrected so an overwrite landing in a
GO's cycle counts in that phase; W24's 84 kit CSVs are its check.

## The Config front end (H1, D41 Q4)

`MachConfig::set(field, value)` and `set_args` take SortTest's `cfg.<field>=value` with ref's names and Java's parsing.
`MachConfig::dropped()` lists ref's fields no v1.35 driver, kit or sweep sets, each with its reason: idleSkip,
startSkew, receiveQueueDepth, periphTypes, testPeripheral, hops, errorLatency. Fields no ref unit reads (runLatency,
stageDelay, counterStoreClearsLatch; rpLatency only in kMin) are inert here too. Checked: a kit job's ten `cfg.`
fields give the same a2a as ref. `replayerLogEntries` is W25's (the kit's `-q 512`).

## The reset (B20)

`Chip::reset(hard)` is ref's `Chip.doReset` at the end of the cycle just run: the register block's and port B's
end-of-cycle effects first, then every port flushed (`Sim::flush_ports`), then every unit as ref's resets leave it,
then the checker's error lines, ref's log line and a wake. `Cc::reset(hard)` is the queued form. Checked: the a2a cut
by `cc.reset(false)` mid-phase, ref's StateFp equal at the reset, the next cycle, the queue's end and 20,000 cycles
into a reboot, seeds -1 and 4. Not yet: the machine with C&C 1-3 as cores (FATAL).

## The checker's first half (`src/check.h`, `tests/checker.{h,cpp}`)

Units record fixed-size facts (`sim::check::record`, compiled in with SIM_CHECKS: the debug and suite builds); the
harness formats them after each run into ref's violation messages, one per check per tile per cycle with the last
fact's text, ref's count and message cap, and ref's ERRORS checks (invariant 15). Recorded here: the injection path,
the director (every check, the strays at acceptance included), the register block, the core's register-block checks,
the FIFO overflows, the write engine's write past big SRAM, the replayer's violations (F2). The switches can change
mid-run (W23's requests 9, 10). W26 took the rest of the kinds, S1 and BusTest, and owns the texts in checker.cpp.

## The outputs

- **`MESHSIM_DIGEST=FILE`** (B25, D41 Q5): every chip's canonical state fingerprint (StateFp) at its last cycle, in
  creation order, in place of ref's digest hash; ref's totals in clear. Equal to ref's StateFp on the a2a.
- **The TRACE store log** (H6): `record_trace`/`trace_log`, `log_trace` and `Chip::log()`. Equal to ref on search.c.
- **The runner**: `run_tests.sh`, v1.3's steps and log names, `--check` against the reference logs with timing and the
  simulator's name masked, the `equiv` steps (riscv by test_golden, periph by test_tileslotconf).

## The framework's and units' harness-only additions

All marked "W20 (harness-only)", off by default, the protected tests unchanged with them (approved 2026-11-10):

- framework: `Sim::wake_all`, `in_flight` (with shifted and ignored types), `in_flight_into`, `put_late` (a Hold or a
  Link, as of the cycle just run), `fifo_overflows`, `hold_refusals`, `has_unit`, `regfile_value`, `flush_ports`;
- units: the coreless core; the gather drain; the injection unit's harness feed; the director's arrival log and stray
  check wiring; the table stores' republish of a poke; the register block's test writes, ERRORS and EVENTS bits,
  scheduled GO, TRACE log and the engines' copies of DMA_CONFIG and the record base; the core's host stores, loads'
  side effects, host reads, host send and UB stop; the write engine's log and request lines; the slot freeing only its
  own row buffer; the replayer's drainable row buffer; C&C's phase log and reply queue; the request bus's host unit
  and delivery log; `G1Config`'s `periph_only`, `test_periph_tile`, `stray_check`, `replayer_log_entries`.

## Differences from ref found and matched

Each was found by a twin's comparison and is now equal to ref:

- the register block and port B run a cycle behind ref's view: reads, quiescence, the reset and PhaseLog take that in;
- router FIFOs are router state, not framework ports; C&C's READY and error lines are delayed wires (quiescence
  ignores them); ref's quiescence leaves the request bus out entirely;
- ref feeds Net's schedules only in `Net.step`/`runAll`, and puts `cncSends` between cycles at the cycle just run;
- a peripheral's violation is reported at t + 1; ERRORS bits set from a test count from the next cycle on;
- PhaseLog's read-port peak counts the same cycle's injection out; ReadPort.idle frees an entry at t + 1;
- ref's reset also resets C&C 1's peripheral (READY_EXPECT 0) under the Java model, and keeps the engines' own copies
  of DMA_CONFIG and the record base while clearing the registers;
- `mem_idle` read big SRAM's area queues at the array's depth, not the configured one (W22's F2, a harness bug).

## Known differences, recorded

- The stray check (invariant 9) doesn't see a counter store made in the same cycle as the acceptance (ref's
  `pendingAdd`): fixing it means delaying all of that packet's facts. No suite reaches it so far.
- ref doesn't report a push into a full request FIFO while its tile sleeps (idle skip); ours records it at once.
  RequestBusTest never does it.
- Two `controller_send`s to one controller within 8 cycles stop with a FATAL: their beats would overlap in one Link.
- The AG report is checked only on the a2a, where its figures are 0; EngineTest's drift line is the real check.

## What's left

- the reset and the fingerprint option on the machine with C&C 1-3 as cores (both FATAL today; `CcCores.reset`,
  statefp_m3's registry), when a suite needs them;
- anything RecipeTest's boot group, CondTest or wave 2's last suites turn up through the integrator.
