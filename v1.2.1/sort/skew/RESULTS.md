# Skew handling for design v2 — RESULTS

**2 October 2026, worker session 5.** Measurements of the planner (`planner/`) and the paths of `skew-v2.md`. Reference size: 64 tiles × 65,536 records, balanced rank targets, 128-bit values. The 20 data sets are `design_v1/python/split-point-model/data.py`'s catalogue, with ties ordered by (key, tile, position). **A7's top 128 KB is sort scratch** (lead, 2 Oct).

## 0. Summary

- **All 20 data sets pass every check in both record formats** (section 1).
  - The chosen two-hop path delivers every key, with its ordinal, and every value to exactly the place the direct all-to-all would.
  - The RV32 build and the host build reach byte-identical states after every one of the planner's calls.
- **128-bit values, K + V including all planning** [meas + model]:
  - uniform 215.0K (direct 213.4K: the switch costs 0.8K, the empty commands 0.75K);
  - sorted and all equal 198.6K (LB);
  - reverse sorted 419.6K (TH);
  - Zipf 1.0 and all nine trace sets 772–786K (BT), against 0.88–3.00M direct;
  - 99 % one value 740.7K (LB).
- **32-bit values, K only** (the switch is format-aware; section 2.1): uniform 72.1K, sorted 66.7K (LB), reverse sorted 149.0K (TH), the nine trace sets 332–343K (BT at L = 3, against 0.59–1.00M direct), Zipf 1.0 direct (BT would be 364.6K against 305.2K).
- **BT's transfers are 526.3K on every skewed set** (128-bit). Planning adds 120–136K and set-up 121–126K, 2–3× the research note's estimates.
- **The (w, L) tuning:**
  - before (the research note's formula), it picks L = 7, which is infeasible on v2's memory;
  - after, it picks L = 4 on every set, the measured best or within 1.9–5.9K of it.
- **Footprint and code:**
  - BT working state: ≤ 2,048 B declared, 1,709–1,724 B used;
  - planner code: 8.72 KB, an overlay;
  - staging high-water: 70,193 values, against 73,728 with A7's 128 KB (64 KB would not do: Zipf-raw needs 70,193 > 69,632).

## 1. How it is measured

`harness/harness.c` plays the 64 tiles from XB to the end of the values all-to-all (or the keys all-to-all, with `--fmt 32`), for each data set's stream matrix (`harness/data/*.s64`, exported by `model/export_mats.py`).

**The planner runs twice per call.**
- Each planner function runs natively on the tile's `Tile` state, and again on the **RV32I + Zmmul image** (`riscv64-unknown-elf-gcc` 13.2, `-O2 -march=rv32i_zmmul -mabi=ilp32`, freestanding).
- The image runs in `harness/rv32emu.c` with **spec 4.11's timing:** 1 cycle per instruction, +1 per taken branch or jump, 3 per `mul`/`mulh*`, loads and stores 1.
- The two resulting states must be byte-identical. **The emulator's cycles are the planner's measured cycles.**
- Within a phase the slowest tile counts.
- The build fails if the image calls a library routine or contains a divide.

**Exchanges, reductions and barriers are done by the harness** as the machine would do them. Their cycles are the walk-through's estimates:
- a 64-step one-word exchange: 661;
- a reduction of v words: 660 + 52v + 1.5v;
- a barrier: 25;
- an empty command: 30.

**The chosen two-hop path is executed record by record** on a model of all 64 big SRAMs (4 MB each), from the `SLOT`, `COUNTS` and `PTR` entries and `SRAM_REMAINING` the planner wrote.
- Mode-2 landing at `PTR[src]` with += 2.
- 128-bit values travel as two packets.
- Attach writes (target − `ATTACH_BASE_ADDRESS`)/2 into the high word.

**Checked on every tile in every phase:**
1. one run per (sender, receiver) pair;
2. no slot count above its group's R (records would be lost);
3. every non-empty slot carries its group's offset, mode 2 and the kind's read size;
4. R the same on every tile;
5. packets received = `SRAM_REMAINING`, exactly;
6. no chunk written twice, across all hop-1 layers of a kind and across all hop-2 layers;
7. reads and writes inside the allowed areas (`skew-v2.md` 4), and **no area both read and written**;
8. at the end, KR (A4) holds every key at its direct position with its ordinal, and VR (A1–A2) holds every value in the same order; in the 32-bit format, KR holds every (key, value) record at its direct position with the value untouched (attach off);
9. every decision field (path, costs, w, L, R2, R3) is identical on all 64 tiles.

**Not executed:** the direct and LB paths, which are costed by formula (`skew-v2.md` 2.1–2.2).

**Cost rules** (walk-through 5.5–5.6, spec 4.2):
- A `COUNTS` run costs Σ_g P·R_g plus late starts plus a tail of 60, with P = 4 (keys) or 8 (values) and gap 0.
- A group shorter than 9 cycles delays the next by the difference (prefetch, ruling A-3).
- Set-up per phase is the slowest tile's measured planner cycles, plus a barrier, plus its exchanges.

## 2. All data sets

| data set | path | **K + V, all in** | planning | transfers | set-up | empty commands | direct | LB |
|---|---|---|---|---|---|---|---|---|
| uniform | direct | **215.0K** | 0.8K | — | — | 0.72K | 213.4K | 216.6K |
| zipf s=0.5 | direct | **216.1K** | 0.8K | — | — | 0.72K | 214.6K | 217.8K |
| zipfraw s=0.5 | direct | **217.7K** | 0.8K | — | — | 0.72K | 216.1K | 219.5K |
| zipf s=0.8 | direct | **240.1K** | 0.8K | — | — | 0.72K | 238.5K | 241.9K |
| zipfraw s=0.8 | direct | **263.3K** | 0.8K | — | — | 0.72K | 261.7K | 265.8K |
| zipf s=1.0 | BT | **778.6K** | 128.2K | 526.3K | 124.0K | 0.18K | 915.6K | 929.2K |
| zipfraw s=1.0 | BT | **774.9K** | 124.8K | 526.3K | 123.7K | 0.18K | 883.3K | 897.0K |
| trace hot=10 frac=0.10 | BT | **773.8K** | 121.9K | 526.3K | 125.5K | 0.18K | 2161.5K | 2190.5K |
| trace hot=10 frac=0.30 | BT | **780.7K** | 129.2K | 526.3K | 125.0K | 0.18K | 2009.5K | 2036.5K |
| trace hot=10 frac=0.60 | BT | **779.2K** | 131.4K | 526.3K | 121.3K | 0.18K | 3004.8K | 3047.8K |
| trace hot=100 frac=0.10 | BT | **774.7K** | 123.1K | 526.3K | 125.1K | 0.18K | 2132.3K | 2167.2K |
| trace hot=100 frac=0.30 | BT | **778.6K** | 127.5K | 526.3K | 124.6K | 0.18K | 2119.0K | 2148.7K |
| trace hot=100 frac=0.60 | BT | **786.1K** | 136.0K | 526.3K | 123.6K | 0.18K | 1932.3K | 1952.1K |
| trace hot=10000 frac=0.10 | BT | **775.1K** | 123.2K | 526.3K | 125.4K | 0.18K | 2177.6K | 2168.9K |
| trace hot=10000 frac=0.30 | BT | **771.9K** | 120.3K | 526.3K | 125.1K | 0.18K | 2047.1K | 2066.9K |
| trace hot=10000 frac=0.60 | BT | **772.5K** | 121.3K | 526.3K | 124.7K | 0.18K | 1762.4K | 1781.9K |
| sorted | LB | **198.6K** | 0.8K | — | — | 0.72K | 786.9K | 197.1K |
| reverse sorted | TH | **419.6K** | 10.7K | 393.5K | 14.9K | 0.60K | 6291.8K | 6291.8K |
| all equal | LB | **198.6K** | 0.8K | — | — | 0.72K | 786.9K | 197.1K |
| 99% one value | LB | **740.7K** | 12.8K | — | — | 0.72K | 1303.7K | 727.1K |

- "Planning" is everything that decides the path: stage 1, 2 and 3.
- "Set-up" is the per-phase slot, `PTR` and counter writing of the executed phases, with their exchanges and barriers.
- BT ran at w = 342, L = 4, with 4 step-1 and 4 step-2 layers on every BT set: 16 phases.
- TH ran in 4 phases (one hop-1 and one hop-2 per kind).
- The "direct" and "LB" columns are the switch's formulas, as every tile computes them.
- **Model of the direct path vs the walk-through 7.2:** the K + V figures here are the walk-through's K + V at gap 0 (spec 4.2), with the same R_g.

### 2.1 32-bit values (`--fmt 32`): keys only

The format word `nkind` (1 for 32-bit values, 2 for 128-bit) is global, and the switch prices only what the format moves:
- D, LB, the floor, TH's and BT's exact costs and BT's estimate count the keys all-to-all alone.
- Planning is not shared with a values all-to-all, so BT has to win on the keys alone.
- The keys' hop 2 runs with attach off, since the high word is the value (walk-through 6).
- The command sequence drops `V` and the V layer phases.

| data set | path | **all in** | planning | transfers | set-up | empty commands | direct | LB |
|---|---|---|---|---|---|---|---|---|
| uniform | direct | **72.1K** | 0.5K | — | — | 0.42K | 71.2K | 72.2K |
| zipf s=0.5 | direct | **72.5K** | 0.5K | — | — | 0.42K | 71.5K | 72.6K |
| zipfraw s=0.5 | direct | **73.0K** | 0.5K | — | — | 0.42K | 72.1K | 73.2K |
| zipf s=0.8 | direct | **80.5K** | 0.5K | — | — | 0.42K | 79.5K | 80.7K |
| zipfraw s=0.8 | direct | **88.2K** | 0.5K | — | — | 0.42K | 87.3K | 88.6K |
| zipf s=1.0 | direct | **321.9K** | 16.3K | — | — | 0.42K | 305.2K | 309.8K |
| zipfraw s=1.0 | direct | **311.1K** | 16.2K | — | — | 0.42K | 294.5K | 299.0K |
| trace hot=10 frac=0.10 | BT (w 512 L 3) | **332.4K** | 93.2K | 197.0K | 42.1K | 0.15K | 720.5K | 730.2K |
| trace hot=10 frac=0.30 | BT (w 512 L 3) | **340.7K** | 101.8K | 197.0K | 41.8K | 0.15K | 669.8K | 678.9K |
| trace hot=10 frac=0.60 | BT (w 512 L 3) | **337.6K** | 100.0K | 197.0K | 40.5K | 0.15K | 1001.6K | 1016.0K |
| trace hot=100 frac=0.10 | BT (w 512 L 3) | **333.5K** | 94.1K | 197.0K | 42.2K | 0.15K | 710.8K | 722.4K |
| trace hot=100 frac=0.30 | BT (w 512 L 3) | **339.2K** | 100.0K | 197.0K | 42.1K | 0.15K | 706.4K | 716.3K |
| trace hot=100 frac=0.60 | BT (w 512 L 3) | **343.3K** | 104.4K | 197.0K | 41.8K | 0.15K | 644.1K | 650.7K |
| trace hot=10000 frac=0.10 | BT (w 512 L 3) | **334.4K** | 95.1K | 197.0K | 42.2K | 0.15K | 725.9K | 723.0K |
| trace hot=10000 frac=0.30 | BT (w 512 L 3) | **332.1K** | 92.6K | 197.0K | 42.4K | 0.15K | 682.4K | 689.0K |
| trace hot=10000 frac=0.60 | BT (w 512 L 3) | **334.0K** | 94.6K | 197.0K | 42.3K | 0.15K | 587.5K | 594.0K |
| sorted | LB | **66.7K** | 0.5K | — | — | 0.42K | 262.4K | 65.8K |
| reverse sorted | TH | **149.0K** | 10.2K | 131.2K | 7.3K | 0.33K | 2097.3K | 2097.3K |
| all equal | LB | **66.7K** | 0.5K | — | — | 0.42K | 262.4K | 65.8K |
| 99% one value | LB | **255.3K** | 12.5K | — | — | 0.42K | 434.6K | 242.4K |

**The switch's calls, checked against forced runs** [meas]:
- **Zipf s = 1.0 stays direct.** BT's best (L = 4) is 364.6K against 321.9K all in. The stage-2 test cost 15.7K (`harness/tune-fmt32.txt`).
- **99 % one value stays LB.** BT forced is 301.9K against 255.3K.
- **On the trace sets version 1 picks L = 3, the measured best** (339.2K, against 364.2K at L = 4 on hot 100, 30 %). With one kind, the set-up per layer weighs more than the thinner layers save.

## 3. Planning cycles

### 3.1 By stage, on the BT sets [meas]

| data set | stage 1 | stage 2 | stage 3: sweep (pipeline) | stage 3: sweep + descriptors, all | held stats, R3, decision | keys: transfers + set-up | BT working state (B) | staging high-water |
|---|---|---|---|---|---|---|---|---|
| zipf s=1.0 | 0.8K | 15.8K | 72.5K | 102.0K | 9.5K | 175.6K + 61.2K | 1718 | 69586 |
| zipfraw s=1.0 | 0.8K | 15.7K | 69.4K | 98.7K | 9.6K | 175.6K + 61.0K | 1716 | 70193 |
| trace hot=10 frac=0.10 | 0.8K | 16.0K | 65.4K | 95.4K | 9.7K | 175.6K + 61.9K | 1721 | 68501 |
| trace hot=10 frac=0.30 | 0.8K | 15.6K | 73.8K | 103.1K | 9.6K | 175.6K + 61.6K | 1718 | 68830 |
| trace hot=10 frac=0.60 | 0.8K | 14.6K | 79.3K | 106.6K | 9.4K | 175.6K + 59.8K | 1709 | 68483 |
| trace hot=100 frac=0.10 | 0.8K | 16.1K | 66.6K | 96.5K | 9.6K | 175.6K + 61.7K | 1720 | 67857 |
| trace hot=100 frac=0.30 | 0.8K | 15.9K | 71.4K | 101.1K | 9.6K | 175.6K + 61.5K | 1718 | 69002 |
| trace hot=100 frac=0.60 | 0.8K | 15.5K | 81.1K | 110.2K | 9.5K | 175.6K + 61.0K | 1717 | 68915 |
| trace hot=10000 frac=0.10 | 0.8K | 16.0K | 66.5K | 96.7K | 9.6K | 175.6K + 61.9K | 1724 | 69233 |
| trace hot=10000 frac=0.30 | 0.8K | 16.2K | 63.6K | 93.6K | 9.6K | 175.6K + 61.7K | 1721 | 68537 |
| trace hot=10000 frac=0.60 | 0.8K | 15.9K | 65.1K | 94.9K | 9.6K | 175.6K + 61.5K | 1720 | 69205 |

**What the stages are:**
- **Stage 1:** the rule (0.7K) and two RED words.
- **Stage 2:** XC (0.66K), the TH table (5.5–10.1K), R2 (1.7K) and the decision (3.5K).
- **Stage 3, sweep and descriptors:**
  - sweep start (1.4K);
  - the 128-round sweep as a pipeline: each round waits for its own previous round and for tile s + 1's token, 45 cycles of token trip [estimate];
  - sweep end (2.6K);
  - the L1 reduction (0.7K);
  - 4 × (descriptor scan 5.7K + exchange 0.66K).
- **Stage 3, held statistics:** 7.7K, plus R3 of 15 words (1.5K) and the decision (0.4K).

The **keys** column is the keys-only part of the transfers and set-up.

### 3.2 Per function [meas: trace hot 100, 30 %, the slowest tile; cycles per call]

| function | calls | max | mean |
|---|---|---|---|
| `sw_prepare` | 64 | 2,253 | 2,253 |
| `sw_stage1` | 64 | 732 | 732 |
| `th_build` | 64 | 10,121 | 9,758 |
| `sw_stage2` | 64 | 3,491 | 3,491 |
| `bt_sweep_init` | 64 | 1,392 | 1,374 |
| `bt_sweep_cell` | 8192 | 3,284 | 211 |
| `bt_sweep_done` | 64 | 2,643 | 2,625 |
| `bt_desc` | 256 | 5,657 | 5,476 |
| `bt_held_stats` | 64 | 7,736 | 7,597 |
| `bt_decide` | 64 | 422 | 422 |
| `bt_hop1_src` | 128 | 11,556 | 11,373 |
| `bt_hop1_m` | 512 | 2,169 | 1,703 |
| `bt_hop2_m` | 512 | 9,168 | 8,494 |
| `bt_hop2_d` | 512 | 1,689 | 1,595 |

TH, on reverse sorted:

| function | calls | max | mean |
|---|---|---|---|
| `sw_prepare` | 64 | 2,253 | 2,253 |
| `sw_stage1` | 64 | 732 | 732 |
| `th_build` | 64 | 5,531 | 5,530 |
| `sw_stage2` | 64 | 492 | 492 |
| `th_desc` | 64 | 841 | 841 |
| `th_hop1` | 128 | 3,849 | 3,801 |
| `th_hop2` | 128 | 3,702 | 3,589 |

- `sw_prepare` is XB's tail (prefixes and the tile's own group maxima, which the walk-through already has). It is not charged to skew handling.
- `bt_sweep_cell`'s mean includes the calls that find their cell in the other pass and return at once.

### 3.3 How the planner got here (same harness, trace hot 100, 30 %)

| version | sweep | BT set-up | BT planning | K + V |
|---|---|---|---|---|
| first build: a 64-entry scan per piece, re-scan of the own pieces per layer | 304.5K | 224.7K | 373.6K | 1,124.6K |
| level-mask sweep, step-1 slots for four layers in one scan (`SLOT` banks) | 87.3K | 125.2K | 156.4K | 807.9K |
| per-cell piece counts, cell order, pointer loops, first-fit staging, batch picks | 71.6K | 124.6K | 127.5K | **778.6K** |

**Tried and dropped: keeping each piece's step-2 layer equal to its step-1 layer** where (layer, d) is free. The aim was to make a step-2 layer one row of `held` plus a short list. About 40 % of each row collides on its destination, so the list overflowed (> 48 per tile) and layers ran out.

## 4. Set-up per phase [meas, slowest tile, plus exchange and barrier]

| phase | BT | TH |
|---|---|---|
| hop 1, the source's slots | 11.6K once per kind (four layers' banks in one scan) | in the next column |
| hop 1, per layer | 2.2K (`PTR` at m) + 25 | 3.85K (slots + `PTR`) + 25 |
| hop 2, per layer | 9.2K (m: slots, `out2`) + 0.66K exchange + 1.7K (d: `PTR`) + 25 | 3.7K + 25 |
| total per kind, BT at 4 + 4 layers | ≈ 61K (keys 59.8–61.9K measured) | 7.3–7.6K |

## 5. Tuning (w, L): before and after [meas, BT forced at each L]

| data set | L = 2 (w 1,024) | L = 3 (w 512) | L = 4 (w 342) | L = 5 (w 256) | version 0 picks | version 1 picks |
|---|---|---|---|---|---|---|
| Zipf s = 1.0 | infeasible (staging) | 990.9K (4/4 layers) | **778.6K** | 830.6K | L = 7, w = 171: infeasible | **L = 4: 778.6K** (best) |
| trace hot 100, 30 % | 911.6K | **776.7K** | 778.6K | 840.1K | L = 7: infeasible | L = 4: 778.6K (+1.9K) |
| trace hot 10, 60 % | 912.3K | **773.3K** | 779.2K | 840.9K | L = 7: infeasible | L = 4: 779.2K (+5.9K) |
| 99 % one value | 878.0K | 733.5K | **724.6K** | 773.6K | L = 7: infeasible | L = 4: 724.6K (best) |

**Version 0** (the research note's estimate, which the checkpoint's model used) prices set-up at 900 per layer and planning at the note's figures, so it favours many thin layers. L = 7 means w = 171 < 256 (the per-cell count `kc` is a byte), about 440 pieces per row (> 300) and 7–8 layers (> 5 held).

**Version 1** (`bt_estimate`, version 1):
- set-up 7.7K per layer per kind;
- 560 per sweep round;
- 22 per own piece per descriptor scan;
- 16K fixed;
- only (w, L) with L + 1 ≤ 5, pieces ≤ 300 and w ≥ 256.

Its estimate at L = 4 is 759.7K, against 772–786K measured. Larger L costs more set-up and planning than it saves in transfers. L = 3 wins narrowly where the sweep needs no overflow layer.

## 6. Model (checkpoint) against measured

| data set | checkpoint model: chosen path, K + V | measured |
|---|---|---|
| uniform | direct 213.4K | direct 215.0K |
| Zipf s = 1.0 | BT 627.2K (L 7, planning 70K, set-up 900 per layer) | BT 778.6K |
| trace hot 100, 30 % | BT 569.9K | BT 778.6K |
| sorted, all equal | LB 197.2K | LB 198.6K |
| reverse sorted | TH 404.1K | TH 419.6K |
| 99 % one value | BT 557.4K | LB 740.7K (BT forced: 724.6K) |

The transfers match the model; BT's are 526.3K measured at L = 4, against 484–550K modelled at L = 7. The difference is set-up, ≈ 124K against 16 × 900 = 14K, and planning, 120–136K against 64–78K.

## 7. Against design v1 [meas: v3.8 per step, v3.10 plan and totals]

v1's "values" step is the request transfer plus the gather. So the like-for-like v2 figure is K + V plus gather 2, which is 196.6K (walk-through 5.7).

| data set | v1: collect + values + plan | v2: K + V + planning + gather 2 | v1 sort total | v2 sort total [model + estimate] |
|---|---|---|---|---|
| uniform | 78.9K + 262.3K + 3.8K = 345.0K | 215.0K + 196.6K = 411.6K | 1,363,092 | 1.37M |
| trace hot 100, 30 % | 587.7K + 1,233.4K + 96.9K = 1,918.0K | 778.6K + 196.6K = 975.2K | 2,985,568 | 2.00M |
| Zipf-raw s = 1.0 | 309.6K + 1,130.5K + 93.5K = 1,533.6K | 774.9K + 196.6K = 971.5K | 2,548,504 | 2.00M |
| sorted | 159.6K + 646.1K + 33.3K = 839.0K | 198.6K + 196.6K = 395.2K | 1,791,180 | 1.22M |
| reverse sorted | 159.6K + 646.1K + 33.3K = 839.0K | 419.6K + 196.6K = 616.2K | 1,791,173 | 1.45M |
| all equal | 159.6K + 646.1K + 33.3K = 839.0K | 198.6K + 196.6K = 395.2K | 1,739,466 | 1.22M |
| 99 % one value | 273.2K + 738.5K + 35.8K = 1,047.5K | 740.7K + 196.6K = 937.3K | 1,953,865 | 2.04M |

- **v2 sort totals** are the walk-through's L1 (530.6K), L2 (464.7K) and C&C (0.5K), plus its split S for each set (7.2: 164K uniform, 226K trace, 235K Zipf, 30K sorted / reverse / all equal, 307K at 99 %), plus K + V from section 2.
- **Uniform is higher on v2** because v2 moves the 128-bit values over the network (V = 142K), where v1 moved 64-bit requests.
- **99 % one value's total is higher on v2** because of the split.

**32-bit values, against v1's measured 32-bit sort** [meas: kv32 patch, uniform 1,017,610]: the walk-through's 32-bit total is ≈ 0.84M on uniform, with K at 71.2K. With the switch, K costs 72.1K on uniform, 66.7K on sorted (LB) and 332–343K on trace (BT). Design v1 measured 32-bit totals only on uniform and the degenerate sets, not trace (README).

**Keys alone, against v1's measured two-phase collect:**
- trace: 237.1K + 127.5K planning on v2, against 587.7K + 96.9K on v1;
- reverse sorted: 138.5K + 10.7K, against 159.6K + 33.3K.

## 8. Memory and code

**BT working state** (data memory):
- declared: 8 + 300 + 64 + 64 + 64 + 256 + 1,280 + 12 = **2,048 B**;
- in use: **1,709–1,724 B** [meas] (`skew-v2.md` 8).

**RV32 planner:**

| part | text |
|---|---|
| stage 1 and helpers (`sw_prepare`, `sw_stage1`, cost formulas, `udiv32`) | 1.1 KB |
| TH (`th_build`, `sw_stage2`, `th_desc`, `th_hop1`, `th_hop2`) | 2.3 KB |
| BT (`bt_*`) | 5.1 KB |
| **total** | **8.72 KB** |

Plus 160 B of tables in data memory. It is an overlay, loaded before the skew decision (lead, 2 Oct): ≈ 1.1K [estimate]. At -Os it is 8.1 KB.

**Table memories:**
- BT uses all 256 `SLOT` entries and all 64 `COUNTS` entries; TH uses 128 and 32.
- L2 writes slots 192–196 just before use.

## 9. A7's third segment: 64 KB against 128 KB

**Values staging high-water** [meas, BT sets]: 67,857–70,193 values, against 69,632 with 64 KB and 73,728 with 128 KB.
- With 64 KB, **Zipf-raw s = 1.0 overflowed** (70,193). Its plan was dropped after the planning was spent, and it ran direct at ≈ 1.01M against 774.9K with BT.
- Four trace sets came within 120 values of the 64 KB limit.

The imbalance is inherent: the sweep balances piece counts per pair, not volume per intermediate (`skew-v2.md` 9).

## 10. Reproducing

```
cd design_v2/sort/skew/model && python3 mats.py && python3 export_mats.py   # stream matrices (numpy; ~1 min)
python3 skew_model.py --no-verify            # the checkpoint's cost model (optional; ~2 min with --verify)
cd ../harness && ./build.sh                  # needs gcc and riscv64-unknown-elf-gcc (Ubuntu: gcc-riscv64-unknown-elf)
./harness > results.txt                      # all sets, with record-level checks and RV32 cycle counts (~1 min)
./harness --fmt 32 > results-fmt32.txt       # the 32-bit-value format (keys only)
./harness --set "trace hot=100" -v           # one set, with the per-function profile
./harness --set "zipf s=1.0" --tune          # BT at every L, and both estimates' choices
./harness --force bt --set "99%"             # force a path (th or bt)
HOSTFLAGS=-DA7VALUES=4096u ./build.sh        # the 64 KB third segment
```

**Files:**
- `harness/results.txt`: section 2's source;
- `harness/results-fmt32.txt`, `harness/tune-fmt32.txt`: section 2.1;
- `harness/tune.txt`: section 5;
- `harness/profile-*.txt`: section 3.2;
- `model/model-results.json`: the checkpoint model.
