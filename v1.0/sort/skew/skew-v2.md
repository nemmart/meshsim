# Skew handling for design v2

Design note for the keys and values all-to-alls on skewed data, with the planner built as host-tested C (`planner/`, `harness/`) and measured in `RESULTS.md`. The spec, `../../spec/machine-spec.md`, is the source of truth; nothing here edits it.

**Markers.**
- **[meas]**: measured with the harness. Each planner function runs on the RV32I + Zmmul build in an emulator with spec 4.11's timing (1 cycle per instruction, +1 per taken branch or jump, 3 per multiply). Every chosen two-hop path is executed record by record.
- **[model]**: the walk-through's cost rules, applied to measured group maxima.
- **[estimate]**: everything else, including the exchange, reduction and barrier costs taken from the walk-through.

**The lead's rulings of 2 October in this note:**
- **A7.** The top 128 KB of A7 is sort scratch; images and overlays fit in the other 384 KB.
- **Planner overlay.** The planner is loaded as an overlay before the skew decision.
- **L2's slots.** L2 rewrites its local slots (192–196) just before use.
- **Fixed command sequence.** C&C runs a fixed maximum of layer phases, and tiles finish empty ones at once.
- **Faster hop-2 layout.** It is an optimisation for later.
- **Earlier rulings.** Keys staging is in A0 + A1, the off-word exchange is a stage-2 step (XC), and BT's tables and descriptors have a 2 KB budget.

---

## 0. Summary

**Four paths, chosen identically on every tile with no extra messages beyond the reductions:**

| path | what | used on |
|---|---|---|
| **direct** | the walk-through's all-to-all | uniform, Zipf ≤ 0.8 |
| **LB** | direct, with the loopback stream moved to its own one-slot group at full rate | sorted, all equal, 99 % one value |
| **TH** | design v1's window two-hop, used only when it needs one layer | reverse sorted |
| **BT** | the research note's balanced two-hop, layered | Zipf 1.0 (both forms), all nine trace sets |

**Results** [meas + model, K + V for 128-bit values, including all planning; `RESULTS.md` 2]:

| data set | path | K + V | direct |
|---|---|---|---|
| uniform | direct | 215.0K | 213.4K |
| Zipf s = 1.0 | BT | 778.6K | 915.6K |
| trace (hot 100, 30 %) | BT | 778.6K | 2,119.0K |
| all nine trace sets | BT | 772–786K | 1.76–3.00M |
| sorted, all equal | LB | 198.6K | 786.9K |
| reverse sorted | TH | 419.6K | 6,291.8K |
| 99 % one value | LB | 740.7K | 1,303.7K |

**What the planning costs.**
- **Uniform data costs 0.8K for the switch plus 0.75K of empty commands.** Everything else is skipped.
- **BT's planning is 120–136K, and its per-layer set-up about 124K.** This is 2–3× the research note's estimate.

**The switch is format-aware.** For 32-bit values it prices the keys all-to-all alone. Uniform then costs 72.1K, sorted 66.7K (LB), reverse sorted 149.0K (TH) and the trace sets 332–343K (BT at L = 3). Zipf 1.0 stays direct (`RESULTS.md` 2.1).

**BT's transfers cost 526.3K on every skewed set.** Every layer runs at R = w, so the cost no longer depends on the distribution.

**Recommendation: BT, with TH as its one-layer fast path, and LB.** Section 7 gives the reasons and the costs against TH.

**Sort totals** (the walk-through's L1, S and L2 plus the paths above; 128-bit values) [model + estimate]:

| data set | v2 with skew handling | walk-through's v2 baseline (no skew handling) | design v1 [meas] |
|---|---|---|---|
| uniform | 1.37M | 1.37M | 1.36M |
| trace | 2.00M | 3.34M | 2.99M |
| Zipf | 2.01M | 2.15M | 2.55M |
| sorted | 1.22M | 1.81M | 1.79M |
| reverse sorted | 1.45M | 7.32M | 1.79M |
| 99 % one value | 2.04M | 2.61M | 1.95M |

v2's split is the larger term on 99 % one value (307K).

---

## 1. What design v2 changes for a two-hop transfer

- **The receiver chooses where a stream lands.** Mode 2 lands each sender's stream at `PTR[src]` (spec 4.9).
  - So **one phase carries at most one contiguous run per (sender, receiver) pair at the receiver.**
  - Pieces of one pair that land in different places need different phases (layers), with `PTR` re-set between them.
  - Senders carry no destination address, so every receiver must learn where each incoming piece goes. Descriptors (section 3) carry that.
- **Hop 2 must rebuild the direct inbox exactly.** Each record goes to off[s][d] + its position in the cell.
  - Then attach on the keys' last hop (ordinal = position from `ATTACH_BASE_ADDRESS` = A4) gives the direct path's ordinals.
  - Count-on-arrival of sort 2's first digit is also on in every keys hop-2 layer, with `CNT` cleared once before the first.
  - Neither is on in hop 1.
  - L2 and the gathers are unchanged.
- **Keys and values are separate all-to-alls.** Keys are 64-bit, at period 4; values are 128-bit, at period 8, and arrive as two packets per value.
  - **Both use the same plan.** The values phases replay the keys phases with 128-bit slots and their own `PTR`.
  - So every transfer term triples (×1 keys, ×2 values) while planning is paid once. This is why BT pays on v2 where it was marginal on v1.
- **Sources need off[me][q]**, the position of their slice in q's inbox. Only q has it after XB.
  - **XC**, a one-word exchange from q to p (≈ 0.7K [estimate]), is the first step of stage 2.
  - So a sort that stops at stage 1 pays nothing for it.
- **No coherence** (spec 4.1). Within a phase no buffer is both read and written. Section 4 gives the placement, and the harness checks it per tile per phase.

## 2. The paths

### 2.1 Direct

As in the walk-through 5.5–5.6: 16 groups of 4, period 4 (keys) or 8 (values).

Cost per kind: Σ_g P·R_g plus late starts plus a tail of 60 plus a barrier of 25. A group shorter than the next group's 9-cycle prefetch delays the next start (ruling A-3).

### 2.2 LB: direct with the loopback stream at full rate

- **Group 15** {0, 4, 32, 60} keeps t = 4, with its loopback slot's count set to 0 so it acts as padding.
- **A 17th group** is one slot, d = 0, t = 1: period 1 for keys, 2 for values.
  - Its R is the largest loopback stream on any tile.
  - It uses no links, so it carries the loopback stream at one element per visit instead of one per four.
  - It runs in the same `COUNTS` run after group 15, so it adds no phase.
  - Prefetch is fine: group 15 lasts at least 6 cycles.
- **Cost:** Σ_{g<15} P·R_g + P·R'_15 + R_loop·(1 or 2) + late starts + tail + barrier, where R'_15 is group 15's maximum without the loopback.
- **RED carries two more words,** R'_15 and R_loop (≈ +0.1K on every sort).
- **On sorted and all-equal data** every record stays on its tile: K + V = 197.1K against 786.9K direct, matching the walk-through's ≈ 197K.

### 2.3 TH: windows, one layer

This is design v1's two-phase transfer.
- **Windows.** Destination q's inbox is cut into 64 windows of W_q = ⌈M_q/64⌉ ≤ 1,024 records. Window k is carried by m = (q + k) mod 64.
- **Pieces.** A source cuts each slice at window boundaries.
- **Layers.** A source with several pieces for one m needs several hop-1 layers.

**On v2, TH is used only when every source has at most one piece per intermediate,** which R2's word L reports. When L > 1, BT is cheaper on every data set measured (section 7), so multi-layer TH is not built.

- **Staging at m.** Window (q, k) is staged at record WCAP·q + (position in window), WCAP = 1,024. This is a fixed slot per destination, possible because M_q ≤ 65,536.
  - Keys go in A0 at A0 + 2·index.
  - Values for q < 32 go in A0 and q ≥ 32 in A3. The index splits at 32,768 exactly, so no window straddles the gap.
- **Hop 1.**
  - Source p puts its piece for m in the slot of offset m − p (K: slots 0–63; V: slots 64–127).
  - `COUNTS` R are the R2 layer-0 maxima, which are exact when L = 1.
  - Descriptor p → m: one word, staging index | size << 16, exchanged once and used by both kinds.
  - m sets `PTR[p]` from it, and `SRAM_REMAINING` to the sum of sizes (×2 for values).
- **Hop 2.**
  - m sends q its one window from WCAP·q, with R = W_max in every group, computed locally.
  - q sets `PTR[m]` = inbox + k·W_q, k = (m − q) mod 64, and `SRAM_REMAINING` = M_q (×2), with attach on for the keys.
  - Nothing is exchanged.

### 2.4 BT: the balanced two-hop

This is the research note's algorithm with v2's mechanics.

**Pieces.** Cell (s, d) is cut into k = ⌈S[s][d]/w⌉ pieces of w records, the last possibly shorter.
- This uses fixed w rather than the note's near-equal sizes, so there is no divide.
- `kc[d]` holds k − 1 in a byte, which requires w ≥ 256.

**The sweep** (the note's diagonal sweep) gives each piece an intermediate m.
- In round t of pass 1 (cells of ≥ 4 pieces) and then pass 2 (the rest), tile s places cell (s, s + t). It uses its own counts per intermediate and column s + t's counts.
- The column's counts are a **token** passed from tile s + 1 to tile s: 64 B as 8 mode-4 packets into data memory, counted by `CM_REMAINING`.
- The choice is the lowest max(r, c), then the lowest r + c, ties in rotated order from m = s + d.
- **On v2 the counts are kept as level masks** (two words per level, 8 levels): `rmask[v]` = {m : r[m] ≤ v} for the row and `cmask[v]` likewise in the token.
  - The lowest v with `rmask[v] & cmask[v]` nonempty, then the lowest u with `rmask[u] | cmask[u]` within it, gives the candidate set in a few word operations.
  - It is rotated by m0, and pieces are taken from it lowest bit first (de Bruijn count of trailing zeros).
  - This is the note's batch rule exactly, at ≈ 20–50 cycles per piece instead of a 64-entry scan [meas].
- 128 rounds run as a pipeline (one command, no C&C).
- Then a one-word reduction makes the step-1 layer count L1 global.

**Layers.**
- A piece's **step-1 layer** is its rank among (s, m)'s pieces in sweep order.
- Its **step-2 layer** is its rank among (m, d)'s pieces in (step-1 layer, s) order.
- Each layer is one `COUNTS` run with **one R for all 16 groups**: the layer maximum, which is w except in tail layers.

**Descriptors.**
- **s → m, one word per piece:** d (6 bits) | j (10) | size − 1 (10), where j is the piece's index in its cell.
  - Bits 26–28 later hold the step-2 layer, stamped by m.
  - L1 rounds of the one-word exchange (64 steps each) deliver them all before R3, into m's `held[layer][s]`.
- **m → d, one half-word per step-2 layer:** s | j << 6, from m's `out2` to d's `in2`.
  - d computes the landing, off[s][d] + j·w, and the size from its own column. m never needs off.
  - The exchange is re-sent for the values phases, since d keeps nothing between phases.

**Staging at m (values index space).** Pieces are laid out first fit over three segments, in step-1 order:
- [0, 32,768) in A0;
- [32,768, 65,536) in A3;
- [65,536, 73,728) in A7's top 128 KB.

Keys use the same index in A0–A1 (A0 + 2·index ≤ 0x24000). The measured high-water is 67,857–70,193 [meas], so 64 KB at A7's top would not have been enough (Zipf-raw needs 70,193).

**Step 1, at the source.**
- One scan of the own pieces writes the slots of up to four layers at once into four `SLOT` banks: layer l in slots 64(l mod 4) … with its `COUNTS` entries 16(l mod 4) …. Each layer phase is then a `COUNTS` run with start 16(l mod 4) and count 16.
- A fifth layer, if any, is written into bank 0 after the fourth layer's phase.
- Unused slots get only their count word cleared.

**Step 1, at m.** `PTR[s]` comes from `held[layer][s]` at the staging cursor, which carries across layers, plus `SRAM_REMAINING`.

**Step 2, at m, per layer.**
- A scan of `held` replays the staging layout and picks the pieces stamped with this layer.
- It writes bank 0's slots and `out2`, then the exchange runs.
- d sets `PTR[m]` and `SRAM_REMAINING`, with attach and counting on for the keys.

## 3. Descriptor and word formats

| item | format | from → to | when |
|---|---|---|---|
| TH descriptor | staging index (16) \| size (11) << 16 | p → m | one exchange, stage 2 |
| BT step-1 descriptor | d (6) \| j (10) << 6 \| (size − 1) (10) << 16; bits 26–28 the step-2 layer (m's stamp) | s → m | L1 exchanges, stage 3 |
| BT step-2 word | s (6) \| j (10) << 6, half-word | m → d | before each step-2 layer, keys and values |
| BT column token | 8 levels × 64-bit mask = 64 B, 8 mode-4 packets | tile s + 1 → s | each sweep round |
| "none" | 0xFFFFFFFF, or 0xFFFF for the half-word | | never a real descriptor: landing + size would exceed 65,536 |

## 4. Placement

| phase | reads | writes | notes |
|---|---|---|---|
| K1 (hop 1, keys) | A3: sort 1's output (key, j) | A0–A1: keys staging | attach off, no counting |
| K2 (hop 2, keys) | A0–A1 | A4: KR, at the direct positions | attach (base A4), count digit 1 |
| V1 (hop 1, values) | A5–A6: VS | A0, A3, A7 top 128 KB: values staging | A0 is free after K2; A3 after K1 |
| V2 (hop 2, values) | A0, A3, A7 top | A1–A2: VR | |

- **L1's and L2's buffers and settings are unchanged.**
- A1 is free during K, since VIN was consumed by gather 1 and VR is written only in V2.
- The order K1, K2, V1, V2 is forced. Keys staging (A0–A1) and values staging (A0, A3, A7) overlap in A0, and VR (A1–A2) overlaps keys staging.
- The harness checks, for every tile in every phase, that read and write areas are disjoint and inside these sets.

## 5. Commands, tables and counters

**The fixed command sequence (lead, 2 Oct).** After `RED`, whose tail runs stage 1, C&C runs:

`XC` · `TH` · `SWEEP` · `DESC` · `K` · K1.0 … K1.4 · K2.0 … K2.4 · `V` · V1.0 … V1.4 · V2.0 … V2.4 · `L2`

| command | does |
|---|---|
| `XC` | the off-word exchange |
| `TH` | the TH table, R2 and the stage-2 decision; on TH, also its descriptors |
| `SWEEP` | BT's 128 rounds and the L1 reduction |
| `DESC` | BT's descriptor exchanges, held statistics, R3 and the final decision |
| `K`, `V` | the direct or LB all-to-all |
| K1.x, K2.x, V1.x, V2.x | up to 5 step-1 and 5 step-2 layer phases per kind (5 = `LMAX`); TH uses phase 0 of each |

- **A tile finishes a command that is empty for its path at once,** at ≈ 30 cycles [estimate]:
  - direct or LB: 25 empty commands, 0.75K;
  - TH: 21, 0.63K;
  - BT at 4 + 4 layers: 6, 0.18K.
- **C&C's receive path is not used.** Every tile knows the path and the layer counts from the reductions.
- **K's configure stays in `RED`'s tail on the direct and LB paths** (walk-through 5.4). The empty commands between `RED` and `K` leave the configured run (its tables and `DMA_CONFIG`) untouched until K's start.

**SLOT and COUNTS.**
- BT's step-1 layers use all 256 `SLOT` entries (four banks) and all 64 `COUNTS` entries. Step 2 uses bank 0.
- TH uses slots 0–127 and `COUNTS` 0–31.
- The direct path's K and V slots and the probe slots are dead by then. **L2 writes its own slots 192–196 just before use (lead, 2 Oct).**

**`PTR`, `SRAM_REMAINING`, `MASTER_CONFIG`.**
- Set per phase by the receiver: `PTR[0..63]`, and `SRAM_REMAINING` = packets expected (records × 1 or 2).
- Attach and count digit 1 are on only in the keys' hop-2 phases. `CNT` is cleared once, before the first of them.
- Hop-1 phases run with attach and counting off. The harness checks `SRAM_REMAINING` against the packets delivered, exactly, for every tile and phase.

## 6. The switch

Every decision uses only reduced values and globals: n_t, M_q, the record format, and the RED, R2 and R3 vectors.

**The record format.** `nkind` is 2 for 128-bit values (keys and values all-to-alls) and 1 for 32-bit values (one all-to-all of 64-bit (key, value) records).
- Every cost term — D, LB, the floor, TH's and BT's exact costs, and BT's estimate — counts the values' runs and set-ups only when `nkind` = 2.
- With one kind, BT's planning has to pay for itself on the keys alone: Zipf 1.0 stays direct, and the trace sets take BT at L = 3.
- In the 32-bit format the keys' hop 2 runs with attach off (walk-through 6), and the command sequence has no `V` or V layer phases. So every tile makes the same choice with no extra messages, and the harness checks that every decision field is identical on all 64 tiles.

**Stage 1, every sort, in `RED`'s tail (0.8K [meas]: 0.7K of rule plus 0.1K for two RED words).**
- D (direct) and LB are computed from the RED words with the section 2 formulas.
- **The floor** is a lower bound on any two-hop path, TH or BT, including its least planning:

      floor = (1 + 2·[nkind = 2])·(n_max + M_max) + nkind·(2·60 + C_TH1 + C_TH2) + 2·XC + red(19) + C_TH_LOCAL

  - Hop 1 sends n_p records from a tile at no more than one record per cycle (keys) or per two (values).
  - Hop 2 lands M_q records at a tile at the same rate.
  - Each kind has at least one hop-1 and one hop-2 set-up. C_TH1 = 3,900 and C_TH2 = 3,800 [meas], and BT's per-kind set-ups are larger.
  - Any two-hop path pays XC, R2 and at least the cheapest TH table, C_TH_LOCAL = 5,000 [meas ≥ 5.5K].
  - On the reference size the floor is 416.9K (128-bit) and 146.9K (32-bit). Reverse sorted's TH comes in at 419.6K and 149.0K, so the floor is within 0.7 % and 1.4 %.
- **If min(D, LB) ≤ floor, no planning runs.** This is every uniform-like set, Zipf ≤ 0.8, sorted and all equal.

**Stage 2 (XC, the TH table, R2 of 19 words: 16 layer-0 maxima, L, z, Δ(W*)) — 12–16K [meas].**
- If L = 1, TH's cost is exact: transfers from R2 and W_max plus measured set-ups. TH is taken if it beats min(D, LB). Reverse sorted is taken here at 10.7K of planning.
- Otherwise BT's estimate for L = 1 … 4 is evaluated (version 1, section 6.1). Stage 3 runs only if the best estimate beats min(D, LB).

**Stage 3 (BT).**
- The sweep, the descriptors, the held statistics, then **R3**, 15 words: L1, L2, staging high-water, and the per-layer R of each step.
- The exact cost, transfers plus measured set-ups, is then compared with min(D, LB).
- BT is dropped if it is infeasible: more than 5 layers, staging past the third segment, more than 300 pieces in a row, or a count past the mask levels. That costs the planning spent; it never happened on these sets with 128 KB at A7.

**Improvement on design v1's floor.**
- v1 built its 30–93K piece table whenever D exceeded n_max + M_max, and on Zipf-raw built it for nothing (90K).
- On v2, the expensive planning (stage 3) runs only behind an exact TH test and a calibrated BT estimate, at a sunk 12–16K.
- No set lost a stage-3 plan. The only planning not used is 99 % one value's stage 2 (12.0K).
- There, BT forced costs 724.6K against LB's 740.7K all in (a 16.1K loss for the gate). The estimate (759.7K) kept LB, a reasonable call at a near tie.

### 6.1 Choosing (w, L) (the tuning)

BT's piece size is w = ⌈64·W*/(64L − z)⌉ (research note 3), and L is chosen from an estimate every tile can compute after R2.
- **Version 0** (the research note's formula, as the checkpoint used it) picks L = 7, w = 171 on every skewed set. That is infeasible on v2's budgets: it needs w ≥ 256, ≤ 300 pieces per row and ≤ 5 held layers.
- **Version 1** uses the measured set-up per layer (7.7K per kind), sweep round (560) and scan cost (22 per piece), counts the format's kinds, and allows only (w, L) that fit memory with room for one overflow layer.
- **For 128-bit values it picks L = 4, w = 342 on every skewed set.** For 32-bit values it picks L = 3, the measured best on trace. Measured against forced L (`RESULTS.md` 5):
  - it is the best L on Zipf and 99 % one value;
  - on two trace sets L = 3 would be 1.9K and 5.9K better (≈ 0.2–0.8 %).
- Its estimate, 759.7K, is within 4 % of the measured 772–786K.

## 7. BT or TH: the recommendation

**BT, with TH as its one-layer fast path.**

**Measured costs where both are possible.** TH figures are from the checkpoint's model with set-ups of 900 per layer, which favours TH; BT figures are measured.

| data set | TH (layers) | BT [meas] | direct |
|---|---|---|---|
| Zipf s = 1.0 | 902K (7) | 779K | 916K |
| trace hot 100, 30 % | 1,034K (7) | 779K | 2,119K |
| trace hot 10, 10 % | 1,164K (10) | 774K | 2,162K |
| 99 % one value | 627K (3) | 724K (forced) | 1,304K; LB 727K |
| reverse sorted | 410K + 10.7K (1), measured 419.6K | — | 6,292K |

**Why.**
1. **Balance is worth more on v2 than on v1.** The values triple the transfer volume, and BT's layers all run at R = w. Its transfers are 526.3K on every skewed set, whatever the distribution. TH's hop 1 grows with the number of windows a source touches per intermediate: 7–10 layers on Zipf and trace.
2. **Receiver-chosen landing costs TH as much as BT.** On v2 TH's multi-layer hop 1 also needs per-layer descriptors and `PTR` rewrites, so its old advantage, landing at computed offsets with no descriptors, is gone.
3. **TH stays where it is cheapest:** one layer, a 5.5K table, no exchanges beyond one descriptor round, and set-ups of 3.7–3.9K per phase. That is exactly reverse sorted and the one-tile cases.

**What BT costs on top of its transfers** [meas] (`RESULTS.md` 3):
- **Planning, 120–136K:**
  - stage 2: 15–16K;
  - sweep: 64–81K on the pipeline;
  - descriptors: 4 × (5.7K + exchange);
  - held statistics with R3: 9.6K.
- **Set-up, 121–126K over 16 phases:**
  - source slots: 11.6K per kind;
  - step-1 per layer at m: 2.2K;
  - step-2 per layer: 9.2K at m + exchange + 1.7K at d.

The research note estimated 55–62K of planning and 0.5–2K of set-up per layer. Most of the difference is controller work that the note's per-operation estimates under-counted (scans of the own pieces and of `held`), and the 2 KB budget, which forces re-scanning instead of tables.

## 8. Memory and code

**Data memory, BT working state: ≤ 2,048 B as declared** (`PMAX` = 300 pieces, `LMAX` = 5 layers), **1,709–1,724 B in use** [meas]:

| item | bytes |
|---|---|
| own pieces' intermediates, sweep order | ≤ 300 (226–232 used) |
| per-intermediate counter, `r` | 64 |
| pieces per cell, `kc` | 64 |
| row level masks during the sweep / cell order after | 64 |
| one 256-byte buffer: the token, then one layer's descriptors, then `out2` + `in2` | 256 |
| held descriptors, 5 layers × 64 | 1,280 (1,024 used at L1 = 4) |
| cursors and counts | 20 |

- **Step-1 descriptors are exchanged all at once,** since m needs them all for R3. Step-2 words go per layer.
- TH's state (≈ 1 KB with its descriptor arrays) is never live with BT's.
- The globals (n, M, the reduced vectors) and the row and column arrays exist anyway from XB.

**Code.**
- The RV32 image's text is **8.72 KB** at -O2 (8.1 KB at -Os): stage 1 and helpers 1.1 KB, TH 2.3 KB, BT 5.1 KB.
- The `GROUP_OFF`/`SLOT_OF`/de Bruijn tables add 160 B of data.
- It is an overlay (lead, 2 Oct). At ≈ 512 cycles per 1K words by mode-4 loopback (spec 4.2), loading it costs ≈ 1.1K [estimate].
- Splitting stage 1 (≈ 0.9 KB) into the resident image and loading the rest only when stage 2 runs would save that 1.1K on every unskewed sort.

## 9. Open items and optimisations (not in the numbers)

| item | effect | needs |
|---|---|---|
| **Faster hop-2 layout (lead: later):** build every step-2 layer's slots and `out2` in one scan, keeping `out2` for all layers (512 B) | ≈ −50K on BT sets | ≈ 2.3 KB working state |
| Fold each layer's set-up into the previous phase's tail ("configure then start", walk-through 4) | part of the ≈ 124K of set-up | the tables are free only after `rd_done`; needs care |
| Volume-aware tie-break in the sweep | lowers the staging high-water (70,193 values against 73,728) | a global per-m volume, not available on the ring; heuristic only |
| L = 3 when the estimate is within a few K of L = 4 | 1.9–5.9K on two trace sets | a better overflow prediction |
| Stage 1 resident, the rest of the planner an overlay loaded only when needed | ≈ −1.1K per unskewed sort | overlay layout |

**Done since the first package:** the switch is format-aware (lead, 2 Oct; section 6).

**Not executed by the harness:** the direct and LB paths, which are costed by formula. Their mechanics are the walk-through's, plus one extra group for LB.

**For the spec, no hardware change is needed.** What this design relies on:
- `COUNTS` runs naming their own slots, and four `SLOT` banks used as four layers;
- mode-4 neighbour packets into data memory counted by `CM_REMAINING`, for the sweep token;
- repeated one-word exchanges in mode 6, one round per layer;
- 18-bit counts;
- the one-slot 17th group, t = 1, for LB.
