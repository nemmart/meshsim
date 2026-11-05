# meshsim v1.0: results

What was measured for the v1.0 code (meshsim v1.0, `mesh.h` 1.0, the sort, the kit), against meshsim M15 where a
comparison applies. Logs: `meshsim/results/` (the last full `run_tests.sh`). What changed: `DEVIATIONS.md`.

## 1. Every suite passes

`meshsim/run_tests.sh all` in this layout (it first checks that meshsim's build copies equal `sw/` and `sort/`):

| suite | result |
|---|---|
| bus primitives | BusTest: 154 passed, 0 failed |
| network, packets, director | NetTest: 30509 passed, 0 failed |
| shuffled child order (seed 5) | NetTest (shuffled, seed 5): 30509 passed, 0 failed |
| shuffled child order (seed 11) | NetTest (shuffled, seed 11): 30509 passed, 0 failed |
| big SRAM, read and write queues | MemTest: 160 passed, 0 failed |
| shuffled child order (seed 3) | MemTest (shuffled, seed 3): 160 passed, 0 failed |
| shuffled child order (seed 8) | MemTest (shuffled, seed 8): 160 passed, 0 failed |
| address generator, write engine, all-to-all (small) | EngineTest: 347 passed, 0 failed |
| shuffled child order (seed 4) | EngineTest (shuffled, seed 4): 347 passed, 0 failed |
| control and the controller | CtrlTest: 5599 passed, 0 failed |
| shuffled child order (seed 4) | CtrlTest (shuffled, seed 4): 5599 passed, 0 failed |
| riscv-tests rv32ui/rv32um on the controller | RiscvTest: 96 passed, 0 failed |
| read port, GATHER | ReadPortGatherTest: 364 passed, 0 failed |
| per-phase accounting | AccountingTest: 11 passed, 0 failed |
| search steps through the read port (microbenchmark) | SearchBench: 56 passed, 0 failed |
| shuffled child order (seed 4) | ReadPortGatherTest (shuffled, seed 4): 364 passed, 0 failed |
| address generator read-ahead, figure 12's PTR/HIST ports | AgTest: 164 passed, 0 failed |
| shuffled child order (seed 4) | AgTest (shuffled, seed 4): 164 passed, 0 failed |
| the recipe book (RECIPES.md, sw/tests/rcp_*) | RecipeTest: 2325 passed, 0 failed |
| shuffled child order (seed 4) | RecipeTest (shuffled, seed 4): 2325 passed, 0 failed |
| conditions, the done gate, the error line, C&C's receive FIFO, REPORT | CondTest: 390 passed, 0 failed |
| shuffled child order (seed 4) | CondTest (shuffled, seed 4): 390 passed, 0 failed |
| REGISTERS.md against the generator, mesh.h and the simulator | RegDocTest: 120 passed, 0 failed |
| the sort's local sorts (P1) | SortTest: 54 passed, 0 failed |
| the sort, every built-in set, 64 x <= 2,000 (about 8 minutes) | SortTest: 124 passed, 0 failed |
| the sort, shuffled child order (seed 4) | SortTest (shuffled, seed 4): 124 passed, 0 failed |
| the sort on the BT path: trace hot 10 / 10 %, 64 x 16,384, 32-bit and 128-bit values (forced BT; about 2 minutes) | SortTest: 12 and 13 passed, 0 failed; path BT in both |
| gather 1 at full size, Java and from C (a few minutes) | ReadPortGatherTest full: 369 passed, 0 failed |
| per-phase accounting, full-size all-to-all from C | AccountingTest full: 11 passed, 0 failed |
| read-ahead measurements: ag_lookahead, zero-length groups, start to first request (a few minutes) | measurements (section 3) |
| parameter sweep, quick (about 1 minute; 'java Sweep full' takes about 25) | measurements |
| the all-to-all from C on the four data sets (several minutes) | CtrlTest: 5608 passed, 0 failed |
| full-size all-to-alls (4 data sets) and radix sort (about 1.5 minutes) | EngineTest full: 374 passed, 0 failed |
| fan-in under 0/+1 skew, 40 skew patterns (several minutes) | measurements (section 4) |

## 2. The sort is exact on every data set; v1.0 against M15

**64 × ≤ 2,000, every built-in set** (`results/sort.log`; the shuffled-order run `sort-shuf.log` is identical): 124
checks passing, every set exact. Totals, M15 → v1.0:

| set | path | M15 | v1.0 | change |
|---|---|---|---|---|
| uniform-128-bit | direct | 257,108 | 257,089 | -19 |
| uniform-32-bit | direct | 237,066 | 237,054 | -12 |
| few-128-bit | direct | 239,148 | 239,122 | -26 |
| ragged-32-bit | direct | 222,245 | 222,233 | -12 |
| sorted-128-bit | LB | 253,793 | 253,776 | -17 |
| equal-32-bit | LB | 69,987 | 69,976 | -11 |
| equal-128-bit | LB | 87,822 | 87,805 | -17 |
| reverse-128-bit | TH | 287,973 | 287,938 | -35 |
| reverse-32-bit | TH | 258,393 | 258,372 | -21 |
| ragged-128-bit | direct | 241,927 | 241,908 | -19 |

**BT, forced, at 64 × 16,384** (`results/sort-bt.log`, new in `run_tests.sh`): path BT, exact, 701,028 cycles
(32-bit values) and 998,040 (128-bit). BT is never chosen at 64 × 2,000, so until now no suite reached it; the
full-size BT run is what found the `rd_running` bug (`DEVIATIONS.md`).

**Phase by phase** (`PhaseNeutral --v1.0`: the data plane identical in every phase, except that a run is counted in the
phase that starts it and an overlay that changed size moves its loading phase's packets by 64 per packet; every phase's
cycle change explained by the slowest tile's controller change, the overlay packets, and at most one lead-in, 4L + 2,
per run it starts). One run per path and format, with the kit's jobs:

| run | path | M15 | v1.0 | change | = controller | + overlay packets | + residual (lead-ins) | verdict |
|---|---|---|---|---|---|---|---|---|
| reverse-k128 | TH | 269,386 | 269,351 | -35 | -52.00 | -7 | +24.00 | identical, explained |
| reverse-kv32 | TH | 239,729 | 239,708 | -21 | -26.00 | -7 | +12.00 | identical, explained |
| sorted-k128 | LB | 235,198 | 235,181 | -17 | -18.00 | -5 | +6.00 | identical, explained |
| sorted-kv32 | LB | 217,286 | 217,275 | -11 | -9.00 | -5 | +3.00 | identical, explained |
| uniform-k128 | direct | 250,762 | 250,743 | -19 | -26.00 | -5 | +12.00 | identical, explained |
| uniform-kv32 | direct | 230,778 | 230,766 | -12 | -13.00 | -5 | +6.00 | identical, explained |
| trace10-10-k128 | BT | 2,140,927 | 2,140,800 | -127 | -164.66 | -12 | +49.66 | identical, explained |
| trace10-10-kv32 | BT | 1,301,627 | 1,301,575 | -52 | -62.00 | -12 | +22.00 | identical, explained |

Every change is the controllers running fewer instructions (no arm, no `rd_ready` polling: −8 to −165 cycles over a
run), overlays 5–12 packets smaller per tile, and a lead-in of 4L + 2 = 6 cycles in each phase that starts a run M15 had
armed in an earlier command (K and V on every path; TH's and BT's layer phases). Detail: `PHASES-6.md`, `PHASES-bt.md`
beside this file.

**The split:** exact inside every sort run above. SplitRun (64 × 65,536) on set 00 (uniform): exact on v1.0, S = 200,749; on set 21 (the laptop kit's smoke test): identical on M15 and v1.0. The 25 sets on both versions are in the laptop kit.

**The kit at 64 × 2,000:** 48 of the 92 runs were run here (an earlier v1.0 build with the same images, before the
`rd_running` fix, which cannot change a run that never polls status right after a start): all passing. The full 92 on
both versions are in the laptop kit.

## 3. The start: lead-in and synchronised start

**Lead-in** [meas: AgBench start], from the cycle the start takes effect to the first request (add 1 to count from
the start store):

| L = `ts_engine_latency` | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| group run (4L + 2) | 6 | 10 | 14 | 18 |
| stream run (2L + 1) | 3 | 5 | 7 | 9 |

Group 0's size (1, 4, 8, 16 streams) makes no difference.

**GO to every tile's first request** for a synchronised start (spec 6.4) [meas: RecipeTest], from C&C's GO store:

| start path | GO → start store | start store → first request | GO → first request |
|---|---|---|---|
| sync recipe: counter store, start (a stream run) | 5–6 | 4 | 9–10 |
| a2a recipe's start (a group run) | 10–11 | 7 | 17–18 |

Every tile's first request lands within the 0/+1 skew; the lead-in is identical on all 64 tiles.

## 4. The count-before-pop rule

Every FIFO is full by its count before the cycle's pop. In the sort it changes nothing: instrumented runs (every
built-in set at 64 × 2,000 and uniform at 64 × 65,536) found no push or request that the old rule took and the new one
refuses. In adversarial tests it does: the fan-in limits drop (`results/skewtable.log`; text for 3.2 in
`SPEC-TEXT-fan-in-and-area-queue.md`), and an area queue served every cycle holds at most depth − 1 (`MemTest`).

Fan-in to tile 27 [meas: NetTest skewtable 40], largest safe W per sender:

```
  senders  safe W     nearest    farthest   random     total        W with no skew (nearest)
  63       0          0          0          0          0            0
  48       1          1          1          1          48           1
  32       1          1          1          1          32           1
  24       2          2          2          2          48           2
  16       2          2          3          3          32           3
  12       3          5          5          3          36           5
  8        5          5          7          5          40           5
  6        7          10         8          7          42           10
  4        15         20         15         15         60           20
  3        15         23         15         16         45           23
  2        30         30         30         30         60           30
  1        >= 64      64         64         64         -            64
```

## 5. Section 13a against meshsim

Fixed: the count-before-pop rule (area queues, token FIFO, overflow FIFOs); HALT resets the engines; an out-of-range
read makes no area access (a zero token); a run ends after its last entry. Already as specified: the send port, the
router's round robin and hop, C&C's receive FIFO, the READY and RUN/HALT edges, a controller halted by an error, chip
reset, the parameters, the read-port result, and reads and writes from any chunk (two rows, merged in the return path;
a spanning read in a group run sets drift, now in 4.1).

## 6. Kits

- **`kit/`** (meshsim v1.0, `mesh.h` 1.0 images): `HASHES` regenerated; the images are the ones measured above.
- **`verify-v1.0/`**: M15 against v1.0, the 92 kit runs at 64 × 2,000, the split's 25 sets and the two full-size BT
  runs, `VERIFY.md` at the end. Smoke-tested here: one kit run (uniform 128-bit at 64 × 2,000) and the split on set 21, both versions, `VerifySummary` at the end: "Kit: as expected (phase-level neutral, TOTAL −19)"; set 21 identical on both (S 49,741, 11.86 cycles per step, invariant 8 64, no violations, inputs matching `HASHES-SPLIT`; it is a shaped set run balanced, so it is not exact on either version, as `VerifySummary` expects). The full run's verdict goes into the final results.
