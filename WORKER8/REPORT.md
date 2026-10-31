# WORKER8 report: the sort on v1.2

Branch `v1.2-sort`, from `v1.2-meshsim` at `e7a2106` (phase 2's last commit; owner's choice). Merged since: `v1.2-meshsim`
(WORKER4's phases 3 and 4: the boot ROM) and `main` (DECISIONS-v1.2 47 and 48).

**Status: done.** The sort builds and passes every suite `run_tests.sh` gives it on the v1.2 simulator (P1; the ten
built-in sets in fixed and shuffled child order; forced BT at 16,384 in both formats), with every set on v1.1's path;
**all 43 full-size P4 runs pass on the Quest runner (task 070), each on v1.1's path**; every per-step difference from
v1.1 is measured and explained (task 071), one of them to the function rather than the instruction (SPLIT, below).
`run_tests.sh`'s other suites pass on this branch too (NOTES.md, step 7 and after).

## Ownership (agreed with the owner, 2026-10-31)

**`test/SortTest.java`, `Inputs.java`, `SortSummary.java` and `VerifySummary.java` are ported and owned by WORKER8 on
this branch;** WORKER4 leaves them alone (they are off `test/PENDING` here). So are the sort's lines in
`run_tests.sh` (layout, sort-build, sort-p1, sort, sort-shuf, sort-bt-kv32, sort-bt-k128) and `tools/check_layout.sh`'s
use.

## Counters and fences

CONVENTIONS 5's assignment, unchanged: **1 `CTR_SRAM`** every big-SRAM arrival (local passes, gathers, K, V, TH's and
BT's layers); **2 `CTR_CM`** controller-memory writes (RED's and the other tree reductions' vectors, BT's sweep tokens,
credits, descriptors and markers, the split's read-port results); **3 `CTR_REPLY`** the one-word exchanges (XB, XC,
TH's descriptors, the split's five exchanges) and BT's step-2 half-words. **4-7 unused.**

**No fence: the sort needs none, so none of 5-7 is used** (7, `CTR_FENCE`, stays the convention's). A fence orders a
send-port packet after a run's packets (RECIPES 28). In the sort every send-port packet that follows a run (BT's
half-words after a layer's start) goes to a different counter at the receiver, which waits for both, so no order
between the two units is needed; every other polled-or-counted dependency is within one sending unit (D44).

## What changed, file by file

| file | change |
|---|---|
| `sort/program/sw/sort.h` | v1.2 header; staging and `STR_OV` gone; `KEY_MODE` gives 1 or 3; `wait_counters()` (counters done under the reset mask, v1.1's `wfi()` wait) and `wait_run()` (counters and DMA run done, `wait_all`); counter assignment documented |
| `main.c` | no overlays (`ov_load`, the images, the shared region) and so no `zero_planner()`; `CMD_STAGE` gone (nothing to stage); `SK` 8-byte aligned in `.bss` |
| `local.c` | `run_pass` arms counter 1, starts, waits for it and DMA run done (RECIPES 0); `wait_tables()` ×5 and `restore_stage_ptr()` gone; streams in `STREAM_COUNT_W` form; gather's stream names counter 1 for its values |
| `exchange.c` | `xchg`: mode 0 to `REPLY_WORD(me)`, `ROUTER_ADDR` once, no-increment, counter 3; `send_vector`: mode 0 by byte address on counter 2; RED's buffers `aligned(8)` (were in the shared region) |
| `transfer.c`, `config.c` | `STREAM`/`GROUP`/`PTR` through `tables->`; *d* in `STREAM_COUNT`; every stream counted on counter 1; K and V end with `wait_run`; `plan_transfer`'s and `k_start`'s table loops step explicit pointers, and the direct path's streams leave `STREAM_DEST` unwritten (modes 1-5 ignore it) |
| `skew.c`, `bt_plan.c`, `bt_run.c` | counters by register; waits by events; tokens, credits and descriptors as mode-0 writes (one `ROUTER_ADDR` store per token and per credit, once per descriptor layer); `bt_phase`'s step-2 configure waits for DMA run done alone before rewriting bank 0's streams (QUESTIONS Q1) |
| `sort.ld`, `build.sh` | one image: program memory 0x01000000-0x01007FFF, entered from the boot ROM's jump; data from 0x10; `rv32i_zicsr_zmmul`, `gnu11`; `loader.S` deleted |
| `sort/skew/planner/skew.h`, `skew_th.c`, `skew_bt.c` | the machine section on mesh.h 1.2; **fixed: hop 1's streams hard-coded mode 2 (v1.1's write by source; v1.2's bucket write)**, now `SK_MODE_SRC` per build (the host shadow keeps 2) |
| `patches/planner.patch` | regenerated: `EMPTY_HI` uses `SK_MODE_SRC` |
| `sort/split/split.c`, `split_regs.h` | `split_regs.h` now includes the generated mesh.h (W3-REVIEW-2's advice); replies as mode 0 with no-increment on counter 3, read-port results on counter 2, `wait_for_command()` as the default barrier |
| `patches/split-barrier.patch` | regenerated as a unified diff against the ported split.c |
| `meshsim/sw/ext/` | copies of the above (check_layout.sh) and `sort/build.sh` |
| `test/SortTest.java` | boot as `Mach.boot` (the boot ROM since WORKER4's phase 4: code and data broadcast, `CMD_JUMP`, GO; no staging); exit through `SIM_EXIT` with 0; `ERRORS` = 0; `PTR[64]` checks retired; counters 1-3 in the state dump |
| `test/Inputs.java` | the image is three files; the boot ROM's image hashed as `sw/bootrom.hex` |
| `run_tests.sh` | the sort's seven steps back |
| `test/SortTest.java` (later) | `-Dsort.prof=lo-hi` (tile 0's cycles in a pc range, debug) |
| `WORKER8/runner/` | the v1.2 runner kit as tasks 070 and 071 ran it (before the boot ROM: interim loader); a later use needs the jar rebuilt and `sw/bootrom.hex` added (README there) |
| `WORKER8/results/` | the runner's outputs: 070 (the 43 P4 runs, `COMPARE-065.md`), 071 (the watches, both versions), 072 (ctrl-full, engine-full, skewtable) |

**Stale, left unported (owner, 2026-10-31):** `sw/ext/split-bisect/splitdrv.c` and `test/SplitRun.java`. They were
already stale at v1.1 (`splitdrv.c` uses v1.0's register names) and aren't in `run_tests.sh`.

## Results against v1.1

All v1.2 figures below are the branch's final code (with the stepping-pointer change, last section) unless marked.

### 64 × ≤ 2,000, `run_tests.sh`'s sets, fixed child order (`results/sort.log`)

| set | path (both) | v1.1 | v1.2 | change |
|---|---|---:|---:|---:|
| uniform k128 | direct | 258,194 | 252,982 | −5,212 (−2.02 %) |
| uniform kv32 | direct | 241,114 | 235,663 | −5,451 (−2.26 %) |
| few k128 | direct (stage 2 ran) | 230,226 | 227,186 | −3,040 (−1.32 %) |
| ragged kv32 | direct | 226,214 | 220,951 | −5,263 (−2.33 %) |
| sorted k128 | LB | 255,791 | 250,190 | −5,601 (−2.19 %) |
| equal kv32 | LB | 73,010 | 68,003 | −5,007 (−6.86 %) |
| equal k128 | LB | 88,997 | 84,224 | −4,773 (−5.36 %) |
| reverse k128 | TH (stage 2 ran) | 289,132 | 284,846 | −4,286 (−1.48 %) |
| reverse kv32 | TH (stage 2 ran) | 263,245 | 257,927 | −5,318 (−2.02 %) |
| ragged k128 shaped | direct | 241,907 | 236,949 | −4,958 (−2.05 %) |

**Shuffled child order (seed 4):** 104 checks pass, every set on the same path (`results/sort-shuf.log`). **P1** (the
local sorts alone, six sets): L1 −1,708 and L2 −1,200 cycles on every set. **Checks:** 2 fewer per run than v1.1 (P1 42
vs 54, the sort 104 vs 124, BT 10-11 vs 12-13): the staging check and the `PTR[64]` check, retired with the staging
area.

### Forced BT, trace hot 10 / 10 %, 64 × 16,384

| values | path | v1.1 | v1.2 | change |
|---|---|---:|---:|---:|
| 32-bit | BT | 709,658 | 708,221 | −1,437 |
| 128-bit | BT | 927,893 | 929,115 | +1,222 |

(Task 071's v1.2 runs, the final code; `results/sort-bt-*.log` are the same runs before the stepping-pointer change,
708,595 and 929,425.)

### Full size: 43 P4 runs at 64 × 65,536 (Quest task 070 against v1.1's task 065)

**All 43 pass** (every check: the global order, every value with its key, counters, invariants; inputs matching the
kit's HASHES), **every run on v1.1's path**, 1,097 s on the runner (v1.1 1,277 s). Per run and per step:
`WORKER8/results/070-p4-full/COMPARE-065.md`. *These runs predate the stepping-pointer change*, so they include
`plan_transfer`'s +306 (about +374 per run with 32-bit values and +371 with 128-bit, in XB and K).

| path | runs | change in TOTAL |
|---|---:|---|
| direct | 14 | −0.31 % to −0.72 % (−4.6K to −7.0K cycles) |
| LB | 6 | −0.31 % to −0.64 % |
| TH | 2 | −0.19 %, −0.39 % |
| BT | 21 | −0.11 % to +0.30 %: 32-bit values −1.4K to +2.2K, 128-bit values +0.1K to +5.9K |

## Every difference, explained

Mean change per step by path at full size (task 070), and where each comes from. Function-level figures are task 071's
`-Dsort.watch` (mean cycles per tile, both versions); a v1.1 function is comparable only in a step where its overlay is
loaded.

- **L1 −1,706, L2 −1,200 (every path, constant).** v1.1's overlay loads (715 in L1's tail, 200 at L2's start); the
  prefix sums' 256 `HIST` loads per bucket pass at 1 cycle instead of 2 (−1,024 per four passes: **a model
  difference**: v1.1 charged a table-window load the 2-cycle register latency, v1.2 charges 1; `Controller.java`'s
  header, both versions); no `PTR[64]` restore; against that, `wait_all`'s mask and required stores, about +8 per
  pass. L1: −715 − 1,024 − restore + 5 × 8 ≈ −1,709; L2: −200 − 1,024 + 4 × 8 ≈ −1,192.
- **SPLIT: −3,199 direct, −2,063 LB, −1,990 TH, +466 BT.** A constant −2,754: v1.1's 405-cycle overlay load and its
  2,349-cycle `zero_planner()` (no shared region now). Against it, ≈ 6 cycles per read-port search step (`step_loop`;
  K = 8 against v1.1's landing at cm_latency 2: WORKER4's SearchBench; spec 4.7 notes it as deliberate), so the
  skewed sets with many more search steps come out slower. Measured, uniform kv32: 211,001 → 207,020 (−3,981) = −2,754
  + `split_get_cuts`' own −1,906 + `step_loop` +592 + `sort_barrier` +98 (−3,970); trace100-30 kv32: 364,198 →
  364,695 (+497) = −2,754 − 1,341 + 3,569 + 1,033. **Located, not itemised:** the −1.3K to −1.9K is inside
  `split_get_cuts` itself (with its inlined `count_round` and `tie`), and it is waiting time, not code: the function's
  register accesses are the same in both builds (7 event loads, the same send and counter stores), so it is in how
  long its exchanges and inlined read batches wait for their counters.
- **XB +348 to +358 at full size, +11 with the final code.** `xchg` +80: `ROUTER_HDR`'s dst moved from [6:0] to
  [31:25], one shift per send (64), and the last word decrements at K = 8. `plan_transfer` +306 before the change:
  its PTR loop took 12 instructions per entry against 9 (+192: GCC indexes `tables->PTR[p]` from the window base
  0xFFFF2000, where v1.1's window at 0x2400 compiled to a stepped pointer), its stream loop 2 more per stream (+128:
  *d* shifted into `STREAM_COUNT`, and a zero `STREAM_DEST` store), and v1.1's `wait_tables()` poll gone (≈ −5): +315
  predicted, +306 measured. With the change, `plan_transfer` is 3,759 against v1.1's 3,827.
- **RED −578 where stage 2 runs** (v1.1's 552-cycle overlay load for TH's image), else −108 to +19 (`config_direct_lb`
  +83, the same table-store code generation over 16 groups; `tree_reduce` −19).
- **XC +12 to +87:** `xchg`'s dst shift and K, as XB.
- **TH (the command) −617 on BT:** v1.1's 597-cycle overlay load for BTPLAN.
- **SWEEP +298:** one `ROUTER_ADDR` store per token and per credit (tokens and credits are mode-0 writes now, where
  v1.1's header carried the address), 2 × 127 rounds, and the dst shifts.
- **BT's layers, +190 to +500 each, the last of each kind ≈ 0:** each layer's tail configures the next, and the
  planner's table writers cost ≈ 2-3 cycles more per PTR or stream written (the same indexed store from 0xFFFF2000,
  and *d*'s shift; `bt_hop1_m` 88 → 93 instructions, `bt_hop1_src` 176 → 182, `bt_hop2_d` 52 → 54). Measured: K1.0,
  K1.1, V1.0, V1.1 +193 = `bt_hop1_m` +190 + `bt_phase` +2; K1.2 +432 = `bt_hop2_d` +190 + `bt_hop2_m` +171 +
  `bt_phase` +68; K2.0 +422 and K2.1 +497 = the same three, +427 and +485; k128's K2.2 +926 = `bt_hop1_src` +750 (V1.0's
  streams) + `bt_hop1_m` +190. 128-bit values have twice the layers, hence their positive totals.
- **DESC +873 (+1,297 net of v1.1's 418-cycle overlay load):** not directly comparable (v1.1 swaps BTPLAN for BTRUN
  mid-step), but it rebuilds from the measured per-call costs: `bt_hop1_src` +750 and `bt_hop1_m` +190 (K1.0's
  configure), `bt_first`'s 64 GROUP words ≈ +190, and the dst shift on 64 × (L + 1) descriptor sends +256: ≈ +1,386.
- **TH's layers** (the two TH sets): K1.0 +575, V1.0 +580, K2.0 +241: the same configures, in TH's planner functions.
- **V +0 to −5, K +85 to +98** on direct and LB at full size: K's start path (`wait_all`'s stores) and, before the
  change, `k_start`'s V PTR loop (−61 with it).

**Kept as a known compiler effect:** the planner's table writers (worker 5's code, through its `HW_*` macros) and
`config_direct_lb`'s 16-group loop. Stepping pointers there would recover most of BT's per-layer cost; the planner is
shared with the host harness, so that is a change for its owner.

## The optional item: stepping pointers in `plan_transfer` (owner, 2026-10-31)

`plan_transfer`'s PTR and stream loops and `k_start`'s V PTR loop step explicit pointers over the table window, and
the direct path's streams leave `STREAM_DEST` unwritten (modes 1-5 ignore it, spec 4.2). Before → after, uniform
n = 2,000: kv32 `plan_transfer` 4,133 → 3,759 (v1.1 3,827), XB 8,213 → 7,839 (v1.1 7,828), TOTAL 236,037 → 235,663;
k128 XB 8,597 → 8,287 (v1.1 8,283), K 3,909 → 3,848, TOTAL 253,353 → 252,982. Code 21,856 → 21,872 B.

## What the recipes didn't cover

All three now answered by DECISIONS-v1.2 47, with recipe 23 and CONVENTIONS updated:
- Waiting for counters done with the reset mask (`wfi()` until [7]) rather than `wait_all` (Q3): sanctioned when no
  other counter is armed. Every site in the sort arms one counter, except `bt_phase`'s step-2 case, which arms 1 and 3
  and wants both (noted in `sort.h`, `bt_run.c`, `split_regs.h`).
- Rewriting one table (`STREAM`) while arrivals still update another (`PTR`) (Q1): the tables are free per memory;
  `bt_phase` is fine as written.
- Polling a landed word in data memory (Q2): valid only when everything it vouches for lands in controller memory.
  Checked: BT's tokens and sequence words, DESC's descriptors and markers are all mode-0 writes into data memory (`SK`);
  none goes to big SRAM (noted in `bt_plan.c`).
