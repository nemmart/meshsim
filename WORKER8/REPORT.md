# WORKER8 report: the sort on v1.2 (draft, in progress)

Branch `v1.2-sort`, from `v1.2-meshsim` at `e7a2106` (phase 2's last commit; owner's choice). Status: the sort builds
and passes its quick suites on the v1.2 simulator; the full-mode BT pair, the shuffled suite and the full-size runs on
the Quest runner are in progress (NOTES.md).

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
| `transfer.c`, `config.c` | `STREAM`/`GROUP`/`PTR` through `tables->`; *d* in `STREAM_COUNT`; every stream counted on counter 1; K and V end with `wait_run` |
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

Not ported: `sw/ext/split-bisect/splitdrv.c` and `test/SplitRun.java`, already stale at v1.1 (v1.0 names) and not in
`run_tests.sh` (REQUESTS R3).

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

P1 (the local sorts alone): L1 −1,708 and L2 −1,200 cycles on all six sets.

### Differences explained (so far; per-step table to come)

- **L1 −1,708, L2 −1,200:** no overlay loads (v1.1: 715 in L1's tail, 200 at L2's start) and no `PTR[64]` restores; the
  remainder to be itemised.
- **SPLIT −2,620 (uniform):** its overlay load (405) and `zero_planner()` gone; read-port search steps are ~6 cycles
  slower (WORKER4's SearchBench: K = 8); to be itemised.
- **XB +385:** `xchg` +80 (`ROUTER_HDR`'s dst moved from [6:0] to [31:25]: one shift per send, 64; the last word's
  decrement at K), `plan_transfer` +306 (to confirm: *d* moved into `STREAM_COUNT`).
- **RED +62:** `config_direct_lb` +83 (as plan_transfer), `tree_reduce` −19.

## What the recipes didn't cover

- Waiting for counters done with the reset mask (`wfi()` until [7]) rather than `wait_all`: v1.1's form, used where only
  counters are pending, for the cost (QUESTIONS Q3).
- Rewriting one table (`STREAM`) while arrivals still update another (`PTR`): `bt_phase` (QUESTIONS Q1).
- Polling a landed word in data memory for completion (BT's token sequence word and DESC's markers) (QUESTIONS Q2).
