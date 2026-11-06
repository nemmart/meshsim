# WORKER29 — notes (P2: a faster engine for busy workloads, then the framework design)

Branch `sim-step-cost` (from `sim-idle-skip`, main merged forward at 2f49a12). Benchmarks: `test/ReplayBench.java` (all 64
tiles replaying at once; real windows on the runner, a synthetic window locally), the sort at n=16384 locally, the full-size
sort on the runner. Proofs: `WORKER29/tools/prove_same.sh` (base build against new: masked logs, every chip's fingerprint),
`WORKER29/tasks/make_proof_task.sh` (the same on the runner, with the full-size sort, T2 and ReplayBench).

## 1. The profile (step 1)

**Local** (one slow core, JFR, sim-idle-skip with stage 1 on). The sort at n=16384 (516K cycles, 62 s): L1 120 µs/cycle,
SPLIT 49 (tiles idle, skipped), L2 102, K and V (the all-to-alls) 185-225. Where a busy cycle went: S1's boxed bookkeeping
~21% of the wall time; the buses' lazy commit ~45% with S1 off (2,708 bus entries per cycle, 443 with a put to commit);
status polling ~6%; `BigSRAM.newPhase` 4-10%; the units' own logic spread thin.

**Quest 116** (the runner, eight JVMs on 16 threads, so wall times inflated): the full-size sort 1,229,008 cycles in 161 s;
**ReplayBench on real windows** (segment 0, windows 0-63, 4.19 M rows, every tile's rows matching rows.bin): replay
210-236 K cycles at 66-98 µs/cycle. The real replay's cycle: the buses' entry 17%, the replayer's own work ~18%,
`Tile.eventCounts` 8.6% (every router FIFO's overflow count summed every cycle), `TileControl.advance` 5.4%,
`checkInvariants` 3.3%, `newPhase` only 2.5% (the synthetic window overstates it: there it is a third). T2 with stage 1 on:
the request bus, `PeripheralSlot.liveErrors`, `scanOverflows` (chip-level loops: WORKER28's side).

## 2. A: cheap S1 and checks (3908ba1; merged forward 2f49a12)

S1 on primitive collections (`src/Prim.java`), its message built only when reported; `newPhase` clears only the bitmap words
set since the last phase. Exact on all 39 quick-suite steps, the sort at n=16384 and ReplayBench, locally. **About -20%
locally** (the sort at n=16384 62 → 50 s; the synthetic replay bench median 57 → 46 µs/cycle). **Quest 119** (A merged
forward, 7606a9e, against main 2d8d9c1): the quick suite 39 of 39 the same, and the full-size sort, T2 segment 0 and
ReplayBench of both workloads 5 of 5 the same (fingerprints, logs, T2's CSVs). The full-size sort's baseline with GO/READY at 8
is now **1,229,687 cycles**. Speed, each alone on the box: the full-size sort 109.1 → 100.5 s (-8%); **the real replay
unchanged** (evm-zone-29, 3 repeats: 49.8 → 49.6 µs/cycle): its cost was never in S1 or newPhase.

## 3. B: eager bus commit — dropped (owner, 2026-11-06); the key finding for the design

Measured first step (branch `sim-step-cost-b1-measured`): a query of a clean bus (nothing to commit, no take counted) writes
and calls nothing. **No gain** (the sort at n=16384 63.5 → 66 s; the replay bench the same within noise). JFR moves the time
from `enterSlow` to `enter`, unchanged in size: **the cost is the cache miss of reading each bus object a busy cycle polls
(~2,700 of them), not the commit.** The dirty-list form would hit the same wall (the consumer's next read of `count` takes the
same miss). Supporting: `TreeSet.isEmpty` in the SRAM read queue 4-14% (a pointer chase); transparent huge pages help ~10%;
the Parallel collector (another object layout) is ~40% slower than G1. **A busy cycle is bound by memory latency over
scattered objects, so data layout is the centre of the framework's design** (ports' state in compact arrays by kind, a
tile-local occupancy mask so a unit learns an input is empty without touching it; the same layout a GPU backend needs).

## 4. C: no per-cycle reach into scattered objects; direct calls

- e29997d: the hardware FIFOs' overflows counted into the tile's event-count slots as they happen (`WordFIFO.sinkOverflows`),
  so `Tile.eventCounts` reads one word per condition; the GO queue primitive with its head cached (`TileControl.advance`
  touches nothing when no GO is due); the read port's ready set's earliest time cached (no TreeSet walk when empty).
  Locally: the sort at n=16384 62 → 54 s against A.
- 0ab09b8: `Tile.runCycle` steps its children by direct calls (the shuffle permutes their indices with the same draws).
  Locally ~2% on the sort, ~5% on the synthetic replay bench (noisy).
- Proof against A, locally: the 30 faster quick-suite steps SAME (the rest, the sorts and the big runs: on the runner).
