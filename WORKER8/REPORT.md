# WORKER8 report: the sort on v1.2

Branch `v1.2-sort`, from `v1.2-meshsim` at `e7a2106` (phase 2's last commit; owner's choice). Status: the sort builds and
passes every suite `run_tests.sh` gives it on the v1.2 simulator (P1, the ten built-in sets in fixed and shuffled
child order, forced BT at 16,384 in both formats), and **all 43 full-size P4 runs on the Quest runner (task 070), each
on v1.1's path.** Per-step differences measured and explained (task 071), except `split_get_cuts`' own −1.3K to −1.9K (waiting
time, located but not itemised). Still open: `run_tests.sh`'s full-mode extras (WORKER4's suites) on this branch.

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
| `sort.ld`, `build.sh` | one image: program memory 0x01000000-0x01007EFF (below the interim loader), data from 0x10; `rv32i_zicsr_zmmul`, `gnu11`; `loader.S` deleted |
| `sort/skew/planner/skew.h`, `skew_th.c`, `skew_bt.c` | the machine section on mesh.h 1.2; **fixed: hop 1's streams hard-coded mode 2 (v1.1's write by source; v1.2's bucket write)**, now `SK_MODE_SRC` per build (the host shadow keeps 2) |
| `patches/planner.patch` | regenerated: `EMPTY_HI` uses `SK_MODE_SRC` |
| `sort/split/split.c`, `split_regs.h` | `split_regs.h` now includes the generated mesh.h (W3-REVIEW-2's advice); replies as mode 0 with no-increment on counter 3, read-port results on counter 2, `wait_for_command()` as the default barrier |
| `patches/split-barrier.patch` | regenerated as a unified diff against the ported split.c |
| `meshsim/sw/ext/` | copies of the above (check_layout.sh) and `sort/build.sh` |
| `test/SortTest.java` | boot as `Mach.boot` (interim loader, no staging); exit through `SIM_EXIT` with 0; `ERRORS` = 0; `PTR[64]` checks retired; counters 1-3 in the state dump |
| `test/Inputs.java` | the image is three files; the interim loader hashed as `image/loader.bin` |
| `run_tests.sh` | the sort's seven steps back |
| `test/SortTest.java` (later) | `-Dsort.prof=lo-hi` (tile 0's cycles in a pc range, debug); the interim loader read from the image directory when it has one (a kit's `image/`) |
| `WORKER8/runner/` | the v1.2 runner kit (v1.1's job 1 with the v1.2 jar, image and HASHES); Quest tasks 070 and 071 carry it |

**Stale, left unported (owner, 2026-10-31):** `sw/ext/split-bisect/splitdrv.c` and `test/SplitRun.java`. They were
already stale at v1.1 (`splitdrv.c` uses v1.0's register names) and aren't in `run_tests.sh`.

## Results against v1.1 (64 × ≤ 2,000, `run_tests.sh`'s sets, fixed child order)

| set | path (both) | v1.1 | v1.2 | change |
|---|---|---:|---:|---:|
| uniform k128 | direct | 258,194 | 253,353 | −4,841 |
| uniform kv32 | direct | 241,114 | 236,037 | −5,077 |
| few k128 | direct (stage 2 ran) | 230,226 | 227,557 | −2,669 |
| ragged kv32 | direct | 226,214 | 221,325 | −4,889 |
| sorted k128 | LB | 255,791 | 250,561 | −5,230 |
| equal kv32 | LB | 73,010 | 68,377 | −4,633 |
| equal k128 | LB | 88,997 | 84,595 | −4,402 |
| reverse k128 | TH (stage 2 ran) | 289,132 | 285,156 | −3,976 |
| reverse kv32 | TH (stage 2 ran) | 263,245 | 258,301 | −4,944 |
| ragged k128 shaped | direct | 241,907 | 237,320 | −4,587 |

P1 (the local sorts alone): L1 −1,708 and L2 −1,200 cycles on all six sets. **Checks:** 2 fewer per run than v1.1
(P1 42 vs 54, the sort 104 vs 124, BT 10 vs 12): the staging check and the `PTR[64]` check, both retired with the
staging area. **Shuffled child order (seed 4):** 104 passed, every set on the same path.

**BT, forced, trace hot 10 / 10 % at 64 × 16,384, 32-bit values:** passes on the BT path; 708,595 vs v1.1's 709,658
(−1,063). Per step: L1 −1,708, SPLIT −339, XB +385, RED −578 (v1.1's 552 overlay load), XC +87, TH −618 (597), SWEEP
+299 (expected: one `ROUTER_ADDR` store per token and per credit, about 254), DESC +879 (+1,297 net of v1.1's 418
overlay load: **to explain**), K1.0-K1.2 +193, +193, +432, K2.0-K2.2 +422, +497, +1 (**to explain**), L2 −1,200.

## Full size: 43 P4 runs at 64 × 65,536 (Quest task 070 against v1.1's task 065)

**All 43 pass** (every check: the global order, every value with its key, counters, invariants; inputs matching the
kit's HASHES), **every run on v1.1's path**, 1,097 s on the runner (v1.1 1,277 s). Per run and per step:
`WORKER8/results/070-p4-full/COMPARE-065.md`; the runner's outputs beside it. The kit was built before the
stepping-pointer change below, so these figures include `plan_transfer`'s +306.

| path | runs | change in TOTAL |
|---|---:|---|
| direct | 14 | −0.31 % to −0.72 % (−4.6K to −7.0K cycles) |
| LB | 6 | −0.31 % to −0.64 % |
| TH | 2 | −0.19 %, −0.39 % |
| BT | 21 | −0.11 % to +0.30 %: 32-bit values −1.4K to +2.2K, 128-bit values +0.1K to +5.9K |

Mean change per step, by path (v1.2 − v1.1, cycles):
- **every path:** L1 −1,706, L2 −1,200 (constant: no overlay loads, no `PTR[64]` restore, 1-cycle `HIST` loads);
  XB +348 to +358 (`xchg` +80, `plan_transfer` +306 before the fix: measured 7,839 vs v1.1's 7,828 after it).
- **SPLIT** depends on the data: −3,199 direct, −2,063 LB, −1,990 TH, **+466 BT**. It saves a constant ≈ 2,750
  (v1.1's 405-cycle overlay load and 2,349-cycle `zero_planner()`, both gone) and pays ≈ 6 cycles per read-port search
  step on the critical path (K = 8 against v1.1's landing at cm_latency 2: WORKER4's SearchBench, +97 per step band;
  spec 4.7 notes it as deliberate). The skewed sets that take BT need more search steps per round, so the step cost
  outweighs the constant there. **Measured (Quest task 071, `-Dsort.watch`, both versions, full size; mean cycles per
  tile):** uniform kv32 SPLIT 211,001 → 207,020 (−3,981) = the constant −2,754 + `split_get_cuts`' own −1,906 +
  `step_loop` +592 + `sort_barrier` +98 + `place` 0 (sum −3,970); trace100-30 kv32 SPLIT 364,198 → 364,695 (+497) =
  −2,754 − 1,341 + 3,569 (`step_loop`: many more search steps) + 1,033 (`sort_barrier`). **Not itemised:**
  `split_get_cuts`' own −1,341 to −1,906. Its register accesses are the same in both builds (7 event loads, the same
  send and counter stores), so the difference is time spent waiting inside it (its exchanges and its inlined read
  batches) rather than code; a per-phase pc histogram would locate it.
- **RED** −578 where stage 2 runs (v1.1's 552-cycle overlay load for TH's image), else −108 to +19.
- **TH (the command)** −617 on BT (v1.1's 597-cycle overlay load for BTPLAN).
- **BT:** SWEEP +298 (one `ROUTER_ADDR` store per token and per credit, 2 × 127 rounds + the header shifts), DESC
  +873, each layer +190 to +460, the last of each kind ≈ 0 (no next configure in its tail): the table-store cost in
  the configures (below). 128-bit values have twice the layers, hence their larger total.
- **TH's layers:** K1.0 +575, V1.0 +580, K2.0 +241 (the same configures in TH's planner functions).

### Differences explained (so far; per-step table to come)

- **L1 −1,708, L2 −1,200:** no overlay loads (v1.1: 715 in L1's tail, 200 at L2's start) and no `PTR[64]` restores; the
  remainder to be itemised.
- **SPLIT −2,620 (uniform):** its overlay load (405) and `zero_planner()` gone; read-port search steps are ~6 cycles
  slower (WORKER4's SearchBench: K = 8); to be itemised.
- **XB +385:** `xchg` +80 (`ROUTER_HDR`'s dst moved from [6:0] to [31:25]: one shift per send, 64; the last word's
  decrement at K = 8); `plan_transfer` +306, measured (`-Dsort.watch`, `-Dsort.prof`) and itemised from the
  disassembly: the `PTR` loop takes 12 instructions per entry against v1.1's 9 (+192 over 64: GCC indexes
  `tables->PTR[p]` from the window base 0xFFFF2000, a `lui`, a shift and an add, where v1.1's window at 0x2400 compiled
  to a stepped pointer), the stream loop 2 more per stream (+128: *d* now shifted into `STREAM_COUNT` [31:25], and the
  explicit zero `STREAM_DEST` store), and v1.1's `wait_tables()` poll gone (about −5); +315 predicted, +306 measured.
  The stream loop's 1,855 cycles on v1.2 equal its static count (64 × (25 instructions + 2 taken branches)).
- **RED +62:** `config_direct_lb` +83 (the same table-store code generation, 16 groups), `tree_reduce` −19.
- **BT's steps, measured (task 071; only functions whose v1.1 overlay is loaded in the step are comparable):**
  K1.0, K1.1 (and V1.0, V1.1) +193 = `bt_hop1_m` +190 (64 PTRs) + `bt_phase` +2; K1.2 +432 = `bt_hop2_d` +190 +
  `bt_hop2_m` +171 + `bt_phase` +68; K2.0 +422 and K2.1 +497 = the same three, +427 and +485; k128's K2.2 +926 =
  `bt_hop1_src` +750 (V1.0's streams) + `bt_hop1_m` +190; each kind's last layer +1 (no configure in its tail). DESC's
  +1,297 (net of v1.1's 418-cycle overlay load) isn't directly comparable (v1.1 swaps BTPLAN for BTRUN mid-step), but
  rebuilds from the measured per-call costs: `bt_hop1_src` +750, `bt_hop1_m` +190, `bt_first`'s 64 GROUP words
  (≈ +190) and the dst shift on 64 × (L + 1) descriptor sends (+256): ≈ +1,386. Logs: `WORKER8/results/071-watch/`.
- **BT's steps, the cause:** the same
  table-store code generation in the planner's table writers, which are 2-8 instructions longer on v1.2 (`bt_hop1_m`
  88 → 93, `bt_hop1_src` 176 → 182, `bt_hop2_d` 52 → 54, `th_hop2` 140 → 146; the PTR or STREAM store indexed from
  0xFFFF2000, plus *d*'s shift), about 2 cycles per PTR or stream written: each layer's tail configures the next
  (`bt_hop1_m`/`bt_hop2_d`, 64 PTRs), K2.2's tail runs `bt_hop1_src` for V1.0 (up to 256 streams), DESC's tail runs
  `bt_first` (64 GROUP words) and K1.0's configure, and DESC's 64 × (L + 1) sends each take the dst shift.
- **A model difference, not the program's:** in v1.1 a load through the table window (`HIST`, the prefix sums) took
  the register-load latency, 2 cycles; in v1.2 a table-window load takes 1 (`Controller.java`'s header, both
  versions). Every prefix sum (256 `HIST` loads per bucket pass, 4 passes in L1 and in L2) gains from it.
- **Recovered (owner's optional item, 2026-10-31):** `plan_transfer`'s PTR and stream loops, and `k_start`'s V PTR
  loop, now step explicit pointers over the table window, and the stream loop no longer stores `STREAM_DEST` (modes
  1-5 ignore it, spec 4.2). Before → after, uniform n=2000: kv32 `plan_transfer` 4,133 → 3,759 (v1.1 3,827), XB 8,213 →
  7,839 (v1.1 7,828), TOTAL 236,037 → 235,663; k128 XB 8,597 → 8,287 (v1.1 8,283), K 3,909 → 3,848, TOTAL 253,353 →
  252,982. The tables above are before the change; the logs committed after it are after. **Left as a known compiler
  effect:** the planner's table writers (`bt_hop1_m`, `bt_hop1_src`, `bt_hop2_d`, `th_hop*`, through worker 5's
  `HW_*` macros) and `config_direct_lb`'s 16-group loop (+83).

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
