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
