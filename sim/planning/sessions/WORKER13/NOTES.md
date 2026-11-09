# WORKER13 notes (M3, the reference side)

Branch `sim-w13`. Prompt: `PROMPT.md`. Plan agreed with the owner 2026-11-09 (go-ahead with four points, below).

## The plan

1. **Coverage.** A driver in `sim/tests/ref/` (`M3Seg.java`) that runs `TracerT2.chip`'s sequence exactly, with
   read-only hooks between cycles (as W7's `M2Sort`/`FpHooks`): which units step, bus and DMA traffic, registers
   touched, firmware functions retired (C&C 1, C&C 3, the tiles), the harness's stand-ins. Small runs locally (a few
   windows, the suites); the 64-window runs on the runner.
2. **`sim/tests/AUDIT-M3.md`.** W14's section (the Tracer, `RowCore`, `Rvc`, its caches and fill bus to
   `MemorySystem`/`BankedMemory`, C&C 3's slot) and W15's (the replayer, `ReplayerAlu`, `PeripheralSlot`, its DMA)
   published first, each as soon as it is solid; then the C&C cores, the DMA and request paths M2 never used, the order
   within `Chip`/`Tile` (D27), D23's events, the unit groups and their order.
3. **The fingerprint's `ref` side** for the new units (`StateFp`, the same canonical form), and its cost per unit.
4. **References** in `sim/tests/golden-m3/` with D12 headers: cycles, rows against `rows.bin`, fingerprints at
   intervals, per-unit dumps; `regen.sh` to regenerate and check; large files on the runner with their hashes.
5. **The baseline**: `ref` on the M3 run(s), one pinned JVM, setup and run separated, the machine recorded (D21), three
   runs.

## The owner's go-ahead (2026-11-09)

1. **The reference segment** is the one T2's own acceptance used, for continuity with the run `ref` was proven on. If
   coverage shows it misses Tracer or replay paths another segment reaches, add that one as a second run; don't switch.
2. **Java locally** (openjdk-21-jdk-headless) for small `ref` runs: a few windows, the suites, coverage on a short run.
   The runner for the 64-window runs, the baseline, and anything big.
3. **W14's and W15's sections early**, each as soon as it is solid, before the rest.
4. **ReplayBench**: confirm from coverage whether it is T2's replay phase only; if so, say so in the audit and give the
   bar's relation to T2's own numbers, so M3's timing compares like with like.

## Log

- 2026-11-09: T2's acceptance (`v1.3/planning/sessions/WORKER22-REPORT.md` section 3): **Quest 110u**, segment 10 of
  each workload (evm-zone-29 and cyclone-29), 64 windows, raw, C&C 1's `CC_SEGMENT` running the barriers: every unit's
  real firmware. `sim/ref`'s `TracerT2` is that code (`cc1Command(0x54, ...)`). Reference run: **evm-zone-29 segment
  10**; cyclone-29 segment 10 (110u's other run, the heaviest segment of either workload) is the candidate second run,
  added if coverage shows paths evm-zone 10 doesn't reach. Inputs on the runner: `$DATA/w9-deliveries`,
  `$DATA/w26-jumpmap` (hashes in `wip/planning/sessions/WORKER22-tasks/make_t1_task.sh`).
- ReplayBench's bar (`sim/tests/ref/baseline.sh`, D8) is **evm-zone-29 segment 0**, windows 0-63, preloaded: a different
  segment from the reference, and (from its source) the replay phase only. To confirm by coverage.
- T2 reaches segment 10 by carrying the one memory through segments 0-9 on T1's path (the model and the ideal
  controller): that is setup, kept apart from the run in the baseline.
