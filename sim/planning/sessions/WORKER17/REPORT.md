# WORKER17 report: all 128 segments, back to back (v1.35)

Branch `sim-w17`. Plan and log: `NOTES.md`. Quests 172-174. No unit and no framework file changed (one `include` line
in `sim/Makefile` for `tests/chain.mk`).

## Result

**All 128 segments of evm-zone-29 and of cyclone-29 run back to back on the framework's chip, the memory carried
from each segment to the next, and every segment passes everything `ref`'s T2 checks; every segment's starting
memory equals `ref`'s byte for byte** (Quest 174, the i5-14400F, one thread per workload).

| workload | segments OK | start memory = ref's | segment cycles | segment time | µs/cycle | per segment (min-max) | rows |
|---|---|---|---|---|---|---|---|
| evm-zone-29 | **128 of 128** | 128 of 128 | 2,941,623,679 | **878.5 s** | 0.2987 | 6.86 s (5.39-7.70) | 536,870,912 |
| cyclone-29 | **128 of 128** | 128 of 128 | 2,817,819,737 | **849.7 s** | 0.3015 | 6.64 s (5.42-7.85) | 536,870,912 |

Outside the timed segments, per workload: setup 7.7 s / 13.3 s, boots 0.34 s, the checks (reading the delivery: the
rows alone are 43 GB a workload) 63.5 s / 49.7 s, the carry 0.25 s / 1.17 s; **in all 950.6 s / 914.5 s (15.8 / 15.2 min)**. The two
chains ran side by side, each pinned to its own physical P core (CPUs 0 and 2, machine.sh's P_ONE; D21's record in
`quest174/machine.txt`: governor powersave, clang 18.1.3 release).

Per segment (`quest174/{evm,cyclone}.csv` and `chain-*.log`): C&C 3's exit and the end (W16's labels), the segment's
cycles (the end less the boot's end, 7,222 every time), setup, boot and segment times, µs per cycle; the logs add
every check's count, the transfers, DMA packets, the Tracer's row and drain cycles, the replay maximum, the pages owned
and created, the dirty lines written back and the cells checked. Segment 10 on both workloads equals Quest 173's
figures, which equal golden-m3 at every point.

## What carries between segments (from `ref`)

`TracerT2` and `M3Seg` carry the one memory by T1's path and run each chip on a copy, so:

- **carried: the FileStore only** (the image, the map, every page created, `nextFree`);
- **at each start** (T1's harness): the ownership set from the segment's update list (checked equal to
  `seg/NNN.pages`); W13's snapshot is taken after it;
- **at each end** (T1's harness): the Tracer's dirty lines written back untimed. `ref`'s T2 chip never does it (its
  copy is thrown away), so the chain does, as T1;
- **fresh every segment**: the chip, its Tracer, a cold memory system over the store (cold again at every state-word
  landing, T2's `onStateWrite`), the landings, the boot, CC_SEGMENT. T1's other edits (cache cold, stall counters
  cleared) follow from the fresh Tracer and memory system;
- **not carried: the Tracer's record**: it is landed from the segment file's frame 2, and C&C 3 checks its end state
  against the next segment's frame 2 (status 1). Segment 127 has no next frame and writes nothing.

## The checks, every segment

- **The carry**: the start memory (after the ownership set) against `ref`'s T1 carry: the sha256 of W13's snapshot
  form, `nextFree`, the page count and the files sum, from `tests/golden-chain` (Quest 172); the landings of segments
  0, 1, 10, 126, 127 against `ref`'s; at each end, after the write-back, every cell of the segment's update list and
  every cell updated so far against the memory (T1's end checks).
- **T2's acceptance, everything `TracerT2.chip` checks** (Q3): C&C 3 exited, `tr_results` status 1 (64 windows), the
  Tracer not stopped; per tile the delivered state word and registers against `state.bin`, the delivered code, read
  log and write log against the delivery's files value for value, exit 1, the rows against `rows.bin` (count and the
  replayer's sink hash), the replayer clean; no violation in any core or register block; the request bus's
  `lag_cycles` 0. Reading the delivery is outside the timed part.
- **Segment 10** (Quest 173): its start and landings, made by the chain, equal W13's dumps byte for byte, and W16's
  `t2` on them equals golden-m3 at every point (evm 241 of 241, cyclone 285 of 285), one domain and the 6-domain
  split, C&C 3's exit and the end at W16's labels; its figures equal `ref`'s (Quest 159).

## Delivered

- **`sim/tests/ref/T1Carry.java`** (`ref`'s side, Q1): T1's carry through every segment, the start memory's sha256 and
  files sum, the landings' sha256 where asked; `M3Inputs` writes into any stream. **`sim/tests/golden-chain/`**: its
  output for both workloads (Quest 172), the oracle at every segment.
- **`sim/tests/chain.h/.cpp`**: WORKER26's inputs into a FileStore in `ref`'s allocation order, the segment file
  (frames 2-5, frame 5's LRU coding), the landings (the tiles' and C&C 3's tables built once), W13's two dump forms,
  the oracle's files, the manifest's windows, a minimal JSON reader. **`test_chain`** (synthetic inputs and the real
  replayer tables) in `make check`, gcc and clang, debug and release.
- **The driver, `tests/chain_main.cpp`** (`build/<cc>-<mode>/chain`): `--segments A-B` of one workload as a chain, a
  line per segment (every check, the cycles, the times) and the totals; `--oracle`, `--dump S` (W13's forms for `t2`).
  One domain (D26). The run goes in 250,000-cycle chunks under `ref`'s stop rules (no progress for 3 M cycles; a tile
  not exited 1 M cycles after C&C 3), 10,000-cycle chunks after C&C 3's exit, so the timed run ends within 10,000
  cycles of the end (1.8 M idle cycles over 128 segments, 0.06%).
- **Tasks**: `tasks/mk_ref_task.sh` (a precompiled `sim/ref` kit, without firmware images), `tasks/mk_task.sh` (the
  C++ kit, W16's with the replayer tables and golden-chain), the bodies of Quests 172-174; their results kept in
  `quest173/` and `quest174/`.

## Findings

1. **The exit and end cycles from the register blocks**: `sim_exit_at` is W16's label + 1 for C&C 3's exit and for
   the end (the latest of C&C 3's and the tiles'), on both workloads' segment 10 (Quest 173), so the chain needs no
   per-cycle loop.
2. **The landings are segment-independent but for C&C 3's record, advice, expected end state and header** (65 ×
   1.4 MB of tables and templates otherwise rebuilt each segment): built once, setup is 0.06-0.15 s a segment.
3. **Speed holds over the whole workload**: 0.25-0.32 µs per cycle on evm-zone-29 (median 0.31), 0.26-0.38 on
   cyclone-29 (median 0.30); segment 10's time in the chain (7.18 s / 7.80 s) is within 1% of Quest 171's (7.10 s /
   7.81 s, `t2 --time`). The fastest segments are near the end (122: 5.39 s; 126 and 127: 21.2 M cycles in 5.4 s).
4. **Segment 127** writes nothing and has no next frame 2: its header has no expected end state, as `ref`'s; it
   passes like every other.

## Open

- `ref`'s own time for the 128 segments wasn't measured: W13's baseline is segment 10 alone (46.2 s / 50.9 s, about
  6.5× the chain's 7.18 s / 7.80 s there). A `ref` run of all 128 under T2 would take about 1.6 h per workload on one
  core, if wanted.
- The full runs are one thread per workload (D26); T2 is serial on C&C 3's Tracer, so the split doesn't help (Quest 171).
