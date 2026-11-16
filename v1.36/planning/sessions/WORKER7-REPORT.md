# WORKER7 report: M2's reference side

Branch `sim-w7` (merged into main 2026-11-07, tests and docs only). The plan and its log are in `NOTES.md`. The
specification for the unit workers is **`sim/tests/AUDIT-M2.md`**; the references are **`sim/tests/golden-m2/`**.
`sim/ref` is unchanged (D5's observer only, as W2 left it).

## 1. What the sort exercises (prompt item 1; AUDIT-M2 sections 0-1)

- `p4/uniform-k128` = `SortTest -1 run keys=<KeyGen uniform, 64 x 65,536> fmt=k128 ready=8` on `Config.machine()`; the
  image runs the real split (no `ORACLE`); **C&C is the Java sequencer `CnC`** (`ccCores` false): the framework
  reproduces its step queue, `command`/`go` timing, READY bits and GO's net, not C&C's cores.
- Coverage from ref's own counters per sort step and three tiles' retire traces: what the sort uses unit by unit; the
  17 register-block words and six table-window regions it touches; what it never touches (the request port and bus,
  the peripheral slot, multiplies, traps, mode 5-7, any FIFO overflow or stall except **2.1 M refused puts on the AG's
  holding register**). The split for the unit groups: (A) the sort needs, (B) only regs/wfi/the mesh tests need (128-bit
  sends, counter wraps, the reads regs checks; NetTest/EngineTest's Java stimuli), (C) nothing in M2 needs.

## 2. The coupling audit (item 2; AUDIT-M2 sections 2-5)

- The cycle's order, and **evidence that the children's order doesn't matter for the sort**: the canonical state is the
  same at every fingerprint point and every cycle of a window with the children shuffled (seed 4) and with idle-skip off.
- The orders that do matter: the request bus first; the SRAM read queue and the injection point as one unit (tokens);
  `TileControl.endCycle` last; the chip's READY pass after the tiles. **The write counters' `pendingAdd`** is
  order-dependent in ref; section 3.3 gives an order-free model equal to ref's core-before-director (the counter
  arithmetic in the register block's shifted end of cycle, both inputs `Link(1)`), and the evidence that no M2 program
  reaches the case (`pendcheck`: 0 coincidences in the 16K sort, the shuffled 2K sort, regs, wfi, a2a).
- **The read queue's token free list is behaviour**: two read-port returns ready in one cycle leave in token order; the
  C++ reproduces the list's order exactly (3.2).
- Per unit: state, ports onto PORTS.md's kinds, same-cycle couplings with their cycles (3.1-3.8).
- Chip-level work and its events (D23, section 4): **`RequestBus.runCycle`, 33% of ref's idle-skipped profile, scans 64
  request FIFOs that stay empty for the whole sort**: D23's first concrete case.
- **Unit groups:** G1 the tile's control, G2 the network (in parallel; first check regs and wfi, tile 0 every cycle),
  G3 the memory path (a2a, then SPLIT), G4 the engines (L1), then the sort at 16K and full size (section 5).

## 3. The canonical state fingerprint (the owner's addition; AUDIT-M2 section 6)

- `Chip.digest()` hashes 20 statistics counters per tile, no memory, no queue: ref's "nothing changed" check, not a
  cross-implementation reference.
- **Defined** per unit (16 per tile, 4 chip-level) as a stream of 64-bit words: architectural state, memories (an
  order-free sum of `fmix64` terms, so the C++ keeps it incrementally), and every bus delivering into the unit, read as
  at the end of the cycle; hashed with `fmix64`. The normative streams are `sim/tests/ref/StateFp.java`.
- **ref's side built** (`StateFp`, `FpHooks`): every 10,000 cycles and at every GO and all-READY, every cycle in a window
  (the big SRAM kept incrementally and checked against a full scan), a per-unit dump with named words. No change to
  `sim/ref`: fields read by reflection, never a bus method (ref settles buses lazily).
- **Proofs:** ref's digest and every result unchanged with every hook on; identical fingerprints with idle-skip off and
  under shuffle; incremental equals full.
- **Cost** (64 tiles, 16K sort): ~88 ms a call, 82 ms of it the big SRAM's scan; ~6 ms for everything else. So the C++
  keeps memory sums incrementally and a fingerprint is microseconds.

## 4. The references (item 3; AUDIT-M2 section 7, `sim/tests/golden-m2/`)

`regen.sh [--check]` rebuilds ref and the drivers in a scratch copy of `sim/ref/meshsim` with the committed images,
runs `RUNS`, and checks every file's sha256 against `HASHES` (all SAME locally). Every `.fp` file carries the D12 header
(the run, its inputs' sha256, every `Config` field, the start skews).

| reference | facts | kept |
|---|---|---|
| regs / wfi / a2a (CtrlTest's runs) | end at 869 / 1,616 / 7,698 cycles; tile 0 fingerprinted every cycle (a2a every 10); traces of tiles 0, 27, 63 | git |
| NetTest -1 quick, EngineTest -1 | logs and ref's per-chip digests (171, 23 chips) | git |
| the sort, 64 x 16,384 | 496,619 cycles, verified; PhaseLog; coverage; fingerprints; traces of tiles 0, 27, 63 | facts in git, the rest by hash |
| the sort, 64 x 65,536 (`p4/uniform-k128`) | **1,229,687 cycles**, verified; PhaseLog; coverage; fingerprints; traces of tiles 0, 27, 63 (Quest 134; Quest 135 re-made them at fb6f21e with the `.fp` header: 10 files byte-identical, the fingerprints identical) | facts in git, the rest by hash; files in the runner's `$DATA/w7-golden-m2/fb6f21e` |

## 5. Localising a mismatch (item 4; AUDIT-M2 section 8)

The ladder: end facts → PhaseLog → interval fingerprints (first point, first (tile, unit)) → a per-cycle window (first
cycle) → the unit's dump (first named word) → the tile's retire trace. A unit's stream includes every bus delivering into
it, so a wrong put shows as its destination differing at the end of that cycle. **Enough to develop G1-G4 against
whole-program references.** The put/take trace of one tile's named buses **stays a proposal** (owner, 2026-11-07),
available if a unit ever needs testing before its neighbours exist: about 10 guarded call sites in 4 files, ~200 MB per
tile at 16K, Quest 123's proof again.

## 6. The baseline (item 5)

Quest 134 (`results/134-w7-m2-refs-baseline/` in Quest): `sim/ref` at main, one JVM (`-Xmx6g`, OpenJDK as installed on
the runner), **pinned to CPU 0** (the first P-core detected by W5's `machine.inc`, D21); each run alone on the box. The
machine: Intel Core i5-14400F, 6 P-cores (CPUs 0-11, 4.7 GHz max) and 4 E-cores (12-15, 3.5 GHz), 64 GB, governor
powersave, Linux 6.8 (`machine.txt` in the result). `M2Sort` runs SortTest's own pieces and times them; hooks off.

| run | setup (JVM start → the sort's first command) | of which the boot | the run (`runUntil`) | **the counted span** (L1 GO → L2 all READY) |
|---|---|---|---|---|
| M2Sort 1 | 2.084 s | 1.301 s (14,791 cycles) | 96.005 s, 1,229,773 cycles | **95.981 s, 1,229,687 cycles: 78.05 µs/cycle** |
| M2Sort 2 | 2.145 s | 1.332 s | 95.870 s | **95.843 s: 77.94 µs/cycle** |
| M2Sort 3 | 1.980 s | 1.232 s | 95.827 s | **95.802 s: 77.91 µs/cycle** |
| SortTest, the whole JVM (`date` around `java`) | | | | **97.70 s** in all |

Setup's parts (run 1): JVM start to `main` 0.005 s, the key file and the oracle 0.52 s, the chip 0.12 s, the boot 1.30 s,
the input load 0.13 s; verification after the run 0.14 s.

- **The figure to beat is ~95.9 s for the counted 1,229,687 cycles (78 µs per chip-cycle)**, pinned to one P-core.
  PLAN-M2's ~110 s (Quest 120, the same code) was the whole JVM unpinned; pinned, the whole JVM is 97.7 s. The ~4 s aim is
  ~3.3 µs per cycle: **~24× the counted span**.
- Run to run, the counted span varies by 0.2%. The boot (14,791 simulated cycles) takes ~1.3 s, outside the count.
- The counted span includes `SortTest.Machine.monitor()` between every two cycles (as SortTest's own run does): a scan
  of 64 tiles that the C++ bench need not reproduce. It is part of the baseline as measured.

## 7. Deviations and notes

- The prompt's "regen.sh --check" checks every file by sha256 (`HASHES`), so references too big for git are still
  checked; regenerating needs only a JDK.
- The mesh tests are referenced by their logs and ref's digests: their stimulus is Java code a C++ bench would have to
  port; the C-driven all-to-all (`a2a`) is the mesh reference with the canonical fingerprint.
- A `pkill -f` matching my own command line killed one shell (the prompt's rule); no harm; pgrep and PIDs since.
