# WORKER25 notes (P7b: Stage B and the replay acceptance kit)

Branch `sim-w25`, from main 67d42fb. Rows: E2, E3, E4, E5 (V135-INVENTORY), with H7's `stageb.profile`.

## The plan (agreed with the owner, 2026-11-11)

0. **E2 (TracerT1): no port.** D41 Q6: the 128-segment chain is TracerT1's equivalent. Recorded here and in the report.
1. **Fixtures from `ref`.** `oracle/ref/stageb.sh` runs v1.3's `StageB synthetic` in a scratch copy of `sim/ref` and
   writes the fixture (`tests/stageb/stageb.log`), regenerated rather than taken from `ref`'s committed
   `results/stageb.log` (which may be stale, as W20's F1 found for accounting). `--check` regenerates and compares. The
   guest-window steps (`DELIVERY=`: `stageb-<guest>`) get their references on the runner, where the delivery is.
2. **Stage B** (E3): `stageb` (`tests/stageb.{h,cpp}`, `stageb_main.cpp`), StageB.java line for line on W20's `Mach`:
   the loader (the window into tile 0's big SRAM per `replay_layout.h`), C&C 1 loading and starting `replay_ctl`, the
   run with the CL4 starvation watch (`--norow N`, ref's `-Dstageb.norow`), the rows against `rows.bin` (or Stage A),
   the streams consumed exactly, the end state, the replayer clean, the checker's violations, `rp_results`; the
   `synthetic` mode (16 program windows, the 2 controller-fed windows, the old-map CL4 case) from W15's
   `replay_window.h` and `replay_driver.h`; tile 0 alone (stage B, the 63 others parked and checked untouched) and the
   full chip (stage F); `--profile` (H7, ref's `-Dstageb.profile`). Log-equal to `ref`, plain and under the debug
   options (shuffle, both orders, step-all); the CL4 line's "in X s" is a timing figure and is masked.
3. **Stage A from the manifest** (E4): `replay_delivery` also takes `DELIVERY PROGRAM VARIANT [window ...]` and reads
   the manifest itself (`tools/delivery/json.h`), its lines unchanged; the window code factored so `replay_shard`
   reuses it.
4. **`replay_shard`** (ReplayShard.java): stages A, B, F; `--first/--count`, `--stride/--phase`, `--full-every`; every
   file a replay reads against the manifest's sha256 (CORRUPT); the tables against `data/tables/manifest.json`; CSV
   lines equal to v1.3's but for `wall_ms`.
5. **The kit** (E5), in `sim/tests/acceptance/` (owner; P8 may tidy the layout): `run_shard.sh`, `calibrate.sh`
   (`synthetic_delivery`, seed 11), `make_kit.sh`, `make_task.sh`, `make_acceptance_task.sh`. JVMs become processes,
   the `part-*.csv` files concatenated in the shell glob's order as v1.3's. `run_shard.sh`'s Python summary becomes a
   small C++ step (D40: no Python), writing `SUMMARY.txt` and `VERDICT.txt` as v1.3's. The kit carries source and
   builds with clang on the runner, as W10's and W17's kits do.
6. **Checks.** Locally: a small seed-11 synthetic delivery through v1.3 patched as D43 (as `oracle/synthetic_crosscheck.sh`)
   and through ours: `results.csv` (wall time masked) and `VERDICT.txt` byte-identical, `SUMMARY.txt`'s non-timing
   lines equal (owner); at `-j 1` and `-j 3`, stages ABF, `-x`, a planted corrupt file. On the runner: the full
   calibration (128 windows) on both kits, a real evm-zone-29 or cyclone-29 range on both, and the five guest windows.
7. **The suite runner**: `replay-build`, `stageb`, `stageb-<guest>` (with `DELIVERY=`) in W20's `sim/run_tests.sh`
   (not on main yet), through the integrator; `stageb` in `make check` if quick enough.

## The owner's answers (2026-11-11)

- The kit scripts: `sim/tests/acceptance/`.
- On the runner: the kit carries source and builds with clang.
- The item-113 delivery: look in WORKER13-NOTES (the runner's data paths) first; if it isn't there, list the runner's
  data directory with a small Quest task.
- `-q 512` (the 8 KB-log control): port it if it amounts to making the replayer's log-queue depth a configuration value
  (W15's replayer, now unowned: note the change); estimate first, and bring it to the owner if it is more.
- `SUMMARY.txt`: compare its non-timing lines only.

## Needs from W20 (asked through the integrator, 2026-11-11)

1. The replayer on tile 0 only, every other tile's peripheral 0 (ref's `Config.testPeripheral`, `periphType = 0`).
2. C&C 1's peripheral `expect = 1` (only tile 0 starts, 6.2).
3. Parked tiles (ref's `Chip.parked`) and `parkedUntouched`.
4. A row hook on the replayer (ref's `rowListener`), with the cycle.

Until they land: the fixtures, Stage A from the manifest, `replay_shard`'s stage A, the kit's scripts and summary step,
the `-q` estimate, the guest delivery's whereabouts.

## Findings

- **F1. `ref`'s committed `results/stageb.log` is stale** (2026-11-07): 18 windows, no CL4 case, and other cycle counts
  (program 0: 235 cycles, Stage A 5.720 cycles/row; today's v1.3: 236 and 3.400). The fixture is regenerated
  (`oracle/ref/stageb.sh`, 19 windows, deterministic: `--check` equal).
- **F2. The replayer's error texts aren't ref's.** StageB's detail lines (the CL4 case in `stageb.log`; any FAIL line in
  the kit's `results.csv`) print ref's Replayer lists verbatim: `head(r.violations)` (the first three, then "... (N in
  all)") and, in Stage A, `errorReport()` with every entry. The C++ replayer keeps the last 64 facts in a ring and
  formats them with its own texts ("@t queue 2 pushed past the credits taken for it: 1 entries", where ref writes "t0
  @t: queue 2 pushed past the credits taken for it: 1 entries, 0 credits (4.19.8; invariant 16)"; invalid expansions
  as numbers). Exactness needs ref's ~36 message texts in `fact_text` and ref's lists (the first entries per list, a
  capacity set at configuration, with the counts): about 200-250 lines in W15's replayer (unowned). Brought to the
  owner (beyond `-q`'s scope). The `meshsim: violations ...` part of the same lines is the checker's: the replayer's
  violations as chip violations (W20).
  **Owner (2026-11-11): go ahead**, in scope (it is the acceptance check); the replayer's tests, ReplayConf and T2
  unchanged. Boundary: W25 the replayer's own messages and lists; W20 the replayer's violations as chip violations.

## Environment

- The container had no clang or javac: installed `clang lld openjdk-21-jdk-headless` (clang 18.1.3, gcc 13.3.0,
  JDK 21.0.12). One CPU.

## Progress

- `oracle/ref/stageb.sh` and the fixture `tests/stageb/stageb.log` (F1).
- **`-q 512` (estimate: small, done).** The replayer's log-queue depth is a constructor argument (`Replayer(me,
  log_entries = 256)`, ref's `-Dreplayer.logq`), `PeriphSlotConfig::replayer_log_entries` passes it to a tile's
  replayer, `ReplayerStream::MAX_CAP` 256 → 512 (every stream's ring is a fixed array of MAX_CAP; 12 KB more per
  replayer). **A change in W15's replayer and slot (now unowned)**: `replayer.h`, `replayer.cpp`, `periph_slot.h`,
  `periph_slot.cpp`. ReplayerUnitTests' 8 KB half of queueSizes now ported (2 checks: 1,458 of ref's 1,473; 15 left).
  test_replayer, the slot tests (clang debug, gcc release), test_t2_harness, test_g1_replay, test_chain,
  test_tracer_slot (clang release): pass.
- **F2 done: ref's lists in the replayer** (a change in W15's unowned replayer: `replayer.h`, `replayer.cpp`).
  `Replayer(me, log_entries, list_capacity = 4)`: five lists (ref's violations, invalids, overruns, underflows,
  leftovers; badWrites is always empty, a bad write being FATAL), each entry ref's text, formatted when recorded by
  `snprintf` into a buffer allocated at construction (N_LISTS x capacity x 256 bytes; D24), the first `list_capacity`
  kept and every entry counted (`list_count`, `list_entry`). The fact codes, counts and ring (ReplayConf's
  observations, `fact_text`) are unchanged. Two recording details: V_CREDIT carries ref's credits (allowance / half)
  as a third argument, V_UNDEFINED the ALU's reason; the control transfer's underflow past the read log's values is a
  new cause, UF_TRANSFER_END (ref's text differs from the words case). `tests/replay_report.h`: `list_string`,
  `head` (StageB.head), `error_report` (Replayer.errorReport).
  **Checked against ref**: `oracle/ref/ReplayLists.java` + `replaylists.sh` replay every committed ReplayConf script
  on ref's Replayer and print every list whole (`tests/replayer/lists.txt`); `tests/test_replay_lists` does the same on
  the C++ replayer: equal (13 scripts: 240 credit violations, 340 overruns, 4 underflow kinds' texts, an invalid
  class 3, 4 leftovers across the three streams), and the kept-entries limit. **Not yet reached by a fixture**: the
  other violations (no flush, two sources, foreign source, offset_imm, log and nfv, undefined, end word, no nfv,
  misaligned), the ROM-end and invalid-word texts, the over-words underflows, leftover kinds 2 and 3: ported from
  ref's source line by line; a script provoking each would close it (to do if the budget allows).
  The replayer's tests, the slot tests, ReplayConf (gcc and clang, debug and release), test_t2_harness, test_g1_replay,
  test_chain, test_tracer_slot (clang release): pass, unchanged.
- **Shared file**: one line in the Makefile, `include tests/p7b.mk` (P7b's rules). Tell the integrator.
- **E4 done: Stage A from the manifest.** `tests/stage_a.h` (shared with `replay_shard` and Stage B): ref's
  ReplayDelivery.runWindow line for line (the line, the summary, every detail line of a failing window: the row
  count, the first mismatching row with ref's fields, describe and diag.bin lines and the two rows before it, the
  streams' consumption, the end state, `errorReport()` from F2's lists, the missing-template log), `tablesFor`, the
  manifest's delivered windows, `consumedExactly`. `replay_delivery DELIVERY VARIANT [PROGRAM ...] [--keep-going]`
  is ref's `java ReplayDelivery` (STOPPED line included); W15's list form is kept, its lines unchanged for passing
  windows (its failure details are now ref's). Checked locally against ref's ReplayDelivery (scratch build): a
  6-window seed-11 synthetic delivery (3,000 rows), all three variants, `cmp` equal; and the same with planted faults
  (a flipped row byte with a diag.bin, a changed end-state register, a truncated rows.bin, the manifest's rows +10
  and -10: an underflow and a row count) under --keep-going and stopping: equal, clang release and gcc debug. (The
  kit's crosscheck script will carry this check.)
- **`replay_shard`, stage A** (`tests/replay_shard_main.cpp`; ref's ReplayShard): the tables against their manifest
  (`tables` FAIL line), every file a replay reads against the window entry (CORRUPT: missing, no SHA-256, a hash,
  rows_sha256, a record not v1.26), the range, stride and phase, the variant (state-pick if present), `--full-every`,
  the CSV line (ref's but for wall_ms) and Stage A's stdout line. Stages B and F stop with a FATAL until Stage B lands.
  Checked locally against ref's ReplayShard (scratch build): the 6-window delivery (plain; state-lru over a strided
  range), the planted-fault delivery with the manifest's hashes refreshed (5 FAIL lines with ref's details), and a
  corrupted copy (a tables FAIL line, each CORRUPT kind, a hash given as a bare string): CSV with wall_ms masked, stdout
  and exit code all equal.
- **`shard_summary`** (`tests/acceptance/shard_summary.cpp`): v1.3's `run_shard.sh` inline Python in C++ (D40),
  writing SUMMARY.txt and VERDICT.txt and the exit code. Checked against v1.3's own Python, taken verbatim from
  `sim/ref/meshsim/acceptance/run_shard.sh` by `oracle/ref/shardsummary.sh` (python3 on the oracle side only), on six
  cases in `tests/acceptance/summary/` (OK lines of A, B, F with stall counts; a B/F cycle disagreement; FAIL,
  CORRUPT, tables and malformed lines; a blank line; a range; "all" from a manifest; -x adding F; an empty file; a
  passing shard): `test_shard_summary` (make check) equal on gcc and clang, debug and release; a planted difference in
  a fixture fails it.
- **The step replay-build** (`run_tests.sh`; `tests/stageb/replay_build.sh`): with the RISC-V toolchain, the three
  replay-controller images rebuilt from `fw/replay` with v1.3's commands and compared with the committed ones;
  without it, the committed images against their MANIFESTs; either way v1.3's line from the committed image. Its
  reference `tests/stageb/replay-build.log` is v1.3's own `sw/replay/build.sh` output (oracle/ref/stageb.sh, now
  also writing it; the toolchain installed here: gcc-riscv64-unknown-elf 13.2.0-11ubuntu1+12, fw/build.sh's).
  Both paths and a planted image difference (exit 1) tried; `./run_tests.sh quick --check`: no log differs.
- **F3. `ref`'s committed `results/replay-build.log` is stale too** (code 3256 B, end 0x11dc; v1.3's build today: 3728
  B, 0x29ec, the committed images'). Not used.
- **Q for the owner:** W20's runner lists the step `replayer` (ReplayerTest: the opcode table, the ALU against vec.tsv,
  ReplayerUnitTests) as pending P7b. It is inventory row C20 (P3's, dropped with D40-D41), not one of E2-E5.
- **The kit** (`tests/acceptance/`: run_shard.sh, calibrate.sh, build.sh, make_kit.sh, make_task.sh,
  make_acceptance_task.sh, README.md): v1.3's scripts on the C++ binaries (`replay_shard`, `shard_summary`,
  `synthetic_delivery`); processes in place of JVMs (v1.3's file names and SUMMARY wording kept, so its outputs compare);
  the kit is HEAD's sim/ sources (Makefile, src, tools, tests' top-level sources and makefiles, bench's makefiles), the
  tables, the boot ROM, hops and the replay controllers' images, 0.8 MB, built on the runner by `./build.sh` (clang;
  35 s here) after HASHES; the task records the machine (`tools/machine.sh`, D21). Differences from v1.3's kit in the
  README (no jar, Java or Python; D43's synthetic delivery; calibrate.sh's exit code is tail's, as in v1.3).
- **`oracle/acceptance_crosscheck.sh`**: v1.3's kit built in scratch (ref's classes in a jar, SyntheticDelivery patched
  as D43, v1.3's run_shard.sh and calibrate.sh unchanged) against v1.35's (made from HEAD and built), the same delivery
  through both: results.csv (wall column masked), VERDICT.txt, SUMMARY.txt's non-timing lines, the exit code and every
  jvm-K.log. **Stage A, all equal** (9 windows of 4,000 rows, seed 11): -j 1 with -m, -j 3, a range, -n all, a range
  past the manifest's end, a planted-fault delivery (FAIL details), a corrupted one (CORRUPT kinds). Stage B's cases
  (AB, ABF, -x, -q 512) and `--calibration` (the runner: 128 x 65,536 rows) come with Stage B.
- **The step `replayer`** (inventory C20, P3's row, the owner's 2026-11-11): `suite_replayer` is v1.3's ReplayerTest
  as run_tests.sh runs it, its output **byte-identical to v1.3's log** (2,187 checks; `tests/replayer/replayer.log`,
  from `oracle/ref/replayertest.sh`): W15's ALU report (`test_replayer_alu`'s alu_report) and ReplayerUnitTests groups
  (`test_replayer`), compiled in unchanged, each in a namespace of its own, plus the two ConvertDelivery groups W15
  left out (15 checks), here on D42's C++ converter (its record conversion in-process; the manifest's determinism over
  two processes and this one). In the runner (`--check`: equal) and in make check (`test_suite_replayer`; gcc and
  clang, debug and release). Changes in others' files: `test_replayer_alu`'s alu_report counts its passing checks
  (an optional out parameter) and counts the opcode-coverage check as ref does (F5); `convert_delivery.cpp` silences
  gcc 13's false `-Wmaybe-uninitialized` inside std::regex in the debug build (it failed to build there; make check
  never built it before).
- **F4. `ref`'s committed `results/replayer.log` is stale** (242 vector rows, 1,188 checks; v1.3 today: 252 rows, 2,187).
- **F5. ReplayerTest's coverage check runs once per uncovered opcode** (its `missing.append` lost in a comment makes the
  T.check the loop's body, as W15 found): a passing run counts none. W15's port checked every executed opcode (87
  more checks); it now fails once per uncovered opcode and counts none when all are covered, so the totals agree.
- **Stage B's harness (W20, on main): enough to start.** MachConfig `test_periph_tile` and `periph_type 0`
  (testPeripheral), `parked` and `Chip::parked_untouched`, `Cc::set_expect`, `Chip::on_row`; the replayer itself through
  `unit<PeriphSlot>("slot").s.replayer(1)` (stalls, passes, pass start and end, queues, lists, pc, registers);
  `Cc::load_words`, `command`, `go`; `run_until`; `violations()`, `violation_report()`. **Still to ask W20**: (a) the
  replayer's violations as chip violations (the CL4 case's "meshsim: violations: 256; ..." part; not recorded yet), (b)
  a MachConfig field for the replayer's log-queue depth (`-q 512`; PeriphSlotConfig has it, MachConfig doesn't pass it).
  Neither blocks the driver's start: the CL4 line and `-q 512` wait for them.
- **Quest 177** (the runner's data listed): **no item-113 guest delivery** anywhere under `$DATA`, the queue's data or
  `/tmp` (no name matching item113, a guest's name, replay-inputs or guest). v1.3's `stageb-<guest>` steps
  (`DELIVERY=`) have no input on the runner: making one needs the guests traced (jolt-cpp) into a delivery. To the owner.
- **Owner (2026-11-11), Quest 177**: the item-113 guest steps stay unrun, as in v1.3 (optional there, only with
  DELIVERY=; no input on the runner; tracing new guests is beyond a freeze). The runner keeps them, running when
  DELIVERY= is given, as v1.3's do (for REPORT.md too).
- **Stage B's driver** (E3; `tests/stageb.{h,cpp}`, `stageb_main.cpp`; binary `stageb`): StageB.java line for line on
  W20's harness: the loader, C&C 1's boot of replay_ctl with only tile 0 expected, the run with the CL4 watch (and the
  profile's sampling) in the condition, the rows, streams, end state, the replayer's lists (F2's texts, `head`),
  violations, the stop and starvation reports, parked tiles, rp_results; the window's line (Java's HALF_UP `%.3f`:
  `java_fixed`) and `out.summary` for replay_shard; `synthetic` (ReplayerUnitTests.programWindow and r() from W15's
  port, compiled in as in suite_replayer; the controller-fed windows; CL4 with the old map); `--profile` (Java's
  HashMap<Integer, Long> order for ties). **`stageb synthetic` equals v1.3's log line for line but for the CL4
  case's "meshsim: violations" line** (W20's recording of the replayer's violations, pending), in clang release and
  debug, and in the debug build plain, shuffled (seeds 3, 4), step-all and instance-major (ASan, UBSan, the missed-wake
  detector): all the same. `test_stageb` (make check) compares with the meshsim line left out while ours has none and
  says PENDING; strict once W20's recording lands. The runner's step `stageb` stays PENDING until then (it would
  differ by that line).
- **F6. A double free in W20's harness** (for W20): `Chip::on_row` sets the replayer's row buffer to the hook's
  vector, and `SlotCore::Ref::~Ref` deletes the replayer's row buffer (`delete[] r->row_buffer()`), so a machine with a
  row hook frees it twice at destruction (ASan: the hook's vector, then the slot). Worked around in `stageb.cpp`
  (the buffer detached before the machine is destroyed); the fix is W20's (mach.cpp, or the slot owning only buffers
  it allocated).
- **`replay_shard`'s stages B and F** (Stage B on tile 0 alone with tiles 1-63 parked, and on the full chip; `--norow`;
  the image from `--replay-build`). **The kit crosscheck's Stage B cases** (`oracle/acceptance_crosscheck.sh`, 9 windows
  of 4,000 rows): AB at -j 1 (with -m) and -j 3, ABF, AB with -x 3, B with -n all, AB on the corrupted delivery: **all
  equal to v1.3's kit** (results.csv with wall masked, VERDICT, SUMMARY's non-timing lines including the stage-B/F stall
  lines, exit codes, every jvm log). AB on the planted-fault delivery: equal but for one line, F7.
- Java's `%.3f` rounds the shortest decimal HALF_UP (12,946 / 4,000 = 3.2365 prints 3.237; printf gives 3.236): found
  by the crosscheck, `java_fixed` now as W21's `java_2f`.
- **F7 (for W20): a peripheral's ERRORS bit isn't a chip violation yet.** In the planted-fault window 5 (the manifest's
  rows + 10: the replayer underflows), v1.3's StageB adds "meshsim: violations: 1;   cycle 43074 tile 0: ERRORS
  [100000000000] set: peripheral 1 (invariant 15); " (the slot's ERRORS [11], the replayer's condition); ours has
  no meshsim line. W20's invariant-15 recording covers the register block's bits, not the slot's peripheral bits. Like
  F2's replayer violations, it is W20's (the boundary the owner set).
- **The runner**: `stageb` added to SUITES; the step `stageb` stays PENDING (the CL4 line, W20's); v1.3's guest steps
  kept as v1.3 has them (`DELIVERY=`: `stageb-<guest>`, `$S/stageb $DELIVERY state <guest>`, no reference log: none
  exists), unrun on the runner (owner's ruling). The guest step's command smoke-tested on a stand-in synthetic delivery
  named lrd_x0. `./run_tests.sh quick --check`: passes.
- **W20's F2 chip side merged (6c746b30): the CL4 line is one cycle off (F8).** In the suite and debug builds (the
  checker on) the CL4 case's "meshsim: violations: 256; ..." line now appears, every one of the 256 messages equal to
  ref's but its cycle: ref's says `cycle 23808` for the replayer's `t0 @23807` (ref's Chip.violation reports c.cycle
  while the replayer settles cycle t lazily in the next one, t + 1), ours `cycle 23807`. **F8 (for W20)**: a
  PERIPH_VIOLATION fact's cycle should be t + 1. The step `stageb` stays PENDING until then; `test_stageb` compares
  everything else strictly and reports the meshsim line on its own (PENDING when it differs or no checker is built).
- **F7 goes to W26** (owner, 2026-11-12: the checker's remaining kinds); **F6 stays with W20** (workaround kept).
- **Quest 180** (claimed after re-reading NUMBERS.md; task from `tasks/mk_acceptance_xcheck.sh` at sim-w25 HEAD):
  `oracle/acceptance_crosscheck.sh` on the runner with `--calibration` and three real cases on deliveries converted by
  v1.35's convert_delivery in scratch: W9's selected windows of evm-zone-29 and of cyclone-29 (AB, F on each), and
  evm-zone-29 windows 0-127 (AB, F every 32nd); processes = min(nproc, 16). The crosscheck gained `--real`,
  `--no-small`, `KIT135` (a prebuilt kit: no git in a task) and `KEEP` (the cases' outputs into the results). The
  bundle (6.5 MB) checked locally: unpacked, HASHES, the crosscheck run from it on a stand-in delivery (equal), and
  convert_delivery built from the kit's sources.
- **Quest 180: ALL PASS** (runner `godspeed`, 16 CPUs, 62 GB; JDK 21.0.13; clang 18.1.3; 06:35-06:37 UTC). v1.35's
  kit equal to v1.3's on every case: **the calibration** (both calibrate.sh, 128 windows of 65,536 rows, seed 11: the
  j1, j4, j8 (ABF) and j16 runs), **W9's selected windows** of evm-zone-29 (12) and cyclone-29 (13), AB with F on
  every window, and **evm-zone-29 windows 0-127** (8,388,608 rows; AB, F every 32nd): results.csv (wall masked),
  VERDICT, SUMMARY's non-timing lines (the stall lines included), exit codes and every per-process log. The
  converted selected manifests' sha256 equal Quest 164's (evm-zone-29 0a8c8f74...). All 25 + 128 real windows PASS
  in both kits. SUMMARY's rates, for the record only (not a timing claim): evm-zone-29 0-127 at 16 processes, Stage B
  0.14 s per window (v1.3: 1.37), Stage A 0.02 (0.16).
  One slip: the task ran `sh tools/machine.sh` (a bash script: "redirection unexpected"), so machine.txt is empty
  apart from the error; the run log names the host, CPUs, memory and versions. Fixed for later tasks (`bash`, in the
  maker and in the kit's make_task.sh).
- **F6 fixed by W20** (4dd260a9: the slot frees only its own row buffer): the workaround in `stageb.cpp` removed;
  `test_stageb` and a delivery run clean under ASan (clang debug).
