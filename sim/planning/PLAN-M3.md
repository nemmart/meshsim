# Milestone 3: v1.3 as it runs: the Tracer and the replayers

Draft, 2026-11-09, from the owner's target; refined when W13's audit lands. Decisions are in `DECISIONS.md` (D32 for
this milestone); M2's close-out and what it carried here are in `PLAN-M2.md`.

## The target (owner, D32)

**Get the simulator as it exists in v1.3 running on the new framework: the Tracer and the replayers.** The workload that
defines "running" is **a segment on the full chip** as `ref` runs it (`sim/ref/meshsim/test/TracerT2.java`, WORKER22's
T2): C&C 3 runs `tracer_ctl.c` with the Tracer as its peripheral on the one memory; its DMA sends each window into its
tile's replay region; every tile runs `replay_ctl.c` with its replayer peripheral on the window it was sent; C&C 1's
firmware boots C&C 2, C&C 3 and the tiles; the rows are compared with the delivery's `rows.bin`. W13 confirms the exact
run(s) and whether `ReplayBench`'s segment (the D8/D15 bar, on `sim-step-cost`) is the same workload.

- **Exact first** (as M1 and M2), with D30: very close is enough where exact would be costly, deliberate differences
  proposed and approved.
- **Then timed** against `ref` on the runner, one thread and split over threads (D26: one chip at a time). No speed aim
  is set yet; M2 ended at ~20× `ref` on the sort. **`ref`'s baseline** (W13, pinned, 3 runs): evm-zone-29 10 in
  46.2 s, cyclone-29 10 in 50.9 s per segment (setup ~9-10 s separate). T2 is serial on C&C 3's Tracer (the tiles mostly
  sleep), so **C&C 3's domain on one thread** decides the wall time, more than the split (AUDIT-M3 section 5).
- The large inputs (deliveries, WORKER26's initial image and map) live on the runner's data directory, with their hashes.

## What it needs (W13's audit establishes it)

New units, beyond M2's chip: **the Tracer** (`Tracer.java`, `RowCore.java`, `Rvc.java`, its two caches and fill bus to
`MemorySystem.java`/`BankedMemory.java`), **the replayer** (`Replayer.java`, `ReplayerAlu.java`) behind **the peripheral
slot** (`PeripheralSlot.java`, `Peripheral.java`), **the C&C cores** (`CcCores.java`: C&C 1-3 as RV32 cores running
`sw/cc/`'s firmware, in place of M2's Java sequencer), and whatever DMA, request-bus and register paths the sort never
exercised. The Tracer's and replayer's own suites (`TracerTest`, `TracerRig`, `TracerT1`, `ReplayerTest`,
`ReplayerUnitTests`, `ReplayDriver`, `StageB`) are unit-level references.

## Order

1. **W13, the reference side** (as W7 for M2): coverage of T2, `AUDIT-M3.md` (the new units' state, ports and couplings;
   unit groups), the fingerprint's `ref` side for the new units, the goldens, `ref`'s baseline on the runner.
2. **W14, the Tracer** and **W15, the replayer**, in parallel from the start: each self-contained machine built and
   checked against `ref` on its own (its suites, a conformance driver like RouterConf) while W13 audits; wired into the
   chip once the audit gives their ports.
3. **The C&C cores and the chip's integration** (a further worker, from the audit), then T2 end to end, exact.
4. **Timing**, one thread and split.

## Status

| worker | state |
|---|---|
| W13 | reference side: the reference runs are **evm-zone-29 segment 10** (T2's acceptance, 110u) and **cyclone-29 segment 10** (reaches what evm misses); `sim/ref` reproduces 110u (Quests 159-160); `ref` takes ~46 s and ~50 s per segment with idle-skip on (the baseline); AUDIT-M3 sections 1, 3.1-3.3 and 6.6 on main; next 3.4 (the C&C cores), sections 4-5, the goldens |
| W14 | the Tracer: the pure pieces and the Tracer class on main, equal to `ref` every cycle on TracerTest's cases (TracerConf); next the seeded stimulus, T1, the ports |
| W15 | the replayer: the ALU and the replayer on main, equal to `ref` every cycle (ReplayConf, 13 traces plus 36 sweeps); next ReplayerUnitTests, Stage A on W9's deliveries, the slot |
| W16 | G7, the C&C cores and the chip's integration, then T2 end to end and the first timing: prompt written 2026-11-09 (`sessions/WORKER16/PROMPT.md`) |
