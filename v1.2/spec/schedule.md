# All-to-all schedule: sixteen groups of four

The schedule every digit all-to-all runs on: 16 groups of four relative offsets, run as a group run by the address generator (`spec/machine-spec.md` 4.2). It also fixes the routing ties in the offset table (`spec/machine-spec.md` 4.6).

## 1. Setting

- **The mesh.** 64 tiles, idx = 8·row + col, with links ±1 (the ring) and ±8 (the columns) (`spec/machine-spec.md` section 2).
- **Offsets and routes.** Offset k sends to tile (idx + k) mod 64. The packet goes `b_k` hops horizontally, then `a_k` hops vertically, with k ≡ 8·a_k + b_k (mod 64), |a_k|, |b_k| ≤ 4 and |a_k| + |b_k| minimal. Section 2 writes this as `+k [h, v]` = [b_k, a_k]. Positive h is right and positive v is down.
- **Ties.** The minimum is not unique only for |a_k| = 4. The ties go as follows: k = 29 … 32 go down (a_k = +4), and k = 33 … 35 go up (a_k = −4).
- **The offset ROM.** Section 2's table lists [b_k, a_k] for all 64 offsets, so it is also the contents of the offset ROM `hops[k]` (`spec/machine-spec.md` 4.6).
- **Runs.** An all-to-all runs as **16 groups** of four offsets, one after another. Each group has rotation period T = 4 (four visits, no padding) and R rotations, R being the group's largest stream over the whole mesh (`GROUP`'s R field). In rotation r, each stream with r < its count sends element r (`spec/machine-spec.md` 4.2).

## 2. The sixteen groups

| g | T | streams: +k [h, v] | RIGHT | LEFT | DOWN | UP | pairs (\|a\|, \|b\|) |
|---|---|---|---|---|---|---|---|
| 0 | 4 | +1 [1,0], +29 [−3,4], +35 [3,−4], +63 [−1,0] | 4 | 4 | 4 | 4 | (0,1) + (4,3) |
| 1 | 4 | +2 [2,0], +30 [−2,4], +34 [2,−4], +62 [−2,0] | 4 | 4 | 4 | 4 | (0,2) + (4,2) |
| 2 | 4 | +3 [3,0], +31 [−1,4], +33 [1,−4], +61 [−3,0] | 4 | 4 | 4 | 4 | (0,3) + (4,1) |
| 3 | 4 | +5 [−3,1], +23 [−1,3], +41 [1,−3], +59 [3,−1] | 4 | 4 | 4 | 4 | (1,3) + (3,1) |
| 4 | 4 | +6 [−2,1], +22 [−2,3], +42 [2,−3], +58 [2,−1] | 4 | 4 | 4 | 4 | (1,2) + (3,2) |
| 5 | 4 | +7 [−1,1], +21 [−3,3], +43 [3,−3], +57 [1,−1] | 4 | 4 | 4 | 4 | (1,1) + (3,3) |
| 6 | 4 | +8 [0,1], +28 [4,3], +36 [−4,−3], +56 [0,−1] | 4 | 4 | 4 | 4 | (1,0) + (3,4) |
| 7 | 4 | +9 [1,1], +27 [3,3], +37 [−3,−3], +55 [−1,−1] | 4 | 4 | 4 | 4 | (1,1) + (3,3) |
| 8 | 4 | +10 [2,1], +26 [2,3], +38 [−2,−3], +54 [−2,−1] | 4 | 4 | 4 | 4 | (1,2) + (3,2) |
| 9 | 4 | +11 [3,1], +25 [1,3], +39 [−1,−3], +53 [−3,−1] | 4 | 4 | 4 | 4 | (1,3) + (3,1) |
| 10 | 4 | +12 [4,1], +24 [0,3], +40 [0,−3], +52 [−4,−1] | 4 | 4 | 4 | 4 | (1,4) + (3,0) |
| 11 | 4 | +13 [−3,2], +15 [−1,2], +49 [1,−2], +51 [3,−2] | 4 | 4 | 4 | 4 | (2,3) + (2,1) |
| 12 | 4 | +14 [−2,2], +18 [2,2], +46 [−2,−2], +50 [2,−2] | 4 | 4 | 4 | 4 | (2,2) + (2,2) |
| 13 | 4 | +16 [0,2], +20 [4,2], +44 [−4,−2], +48 [0,−2] | 4 | 4 | 4 | 4 | (2,0) + (2,4) |
| 14 | 4 | +17 [1,2], +19 [3,2], +45 [−3,−2], +47 [−1,−2] | 4 | 4 | 4 | 4 | (2,1) + (2,3) |
| 15 | 4 | +0 (loopback), +4 [4,0], +32 [0,4], +60 [−4,0] | 4 | 4 | 4 | 0 | loopback, (4,0), (0,4) |

**Reading the table:**
- **The direction columns** count, for each of a tile's outgoing links, the cycles per rotation on which it carries data.
- **Coverage.** All 64 offsets appear once, and **Σ T = 64**, the lower bound (section 4).
- **No padding.** Every group is four streams.
- **128-bit values.** In v1.1 a 128-bit value is one packet (machine-spec 4.2), so each visit is one packet and 128-bit groups run on this schedule unchanged: period t = 4, loads ≤ 4. (v1.0 sent two packets per visit, at period 2t = 8 with loads ≤ 8 per period.)

## 3. Constants that go with it

```
GROUP_OFFSETS = [[1,29,35,63], [2,30,34,62], [3,31,33,61], [5,23,41,59], [6,22,42,58], [7,21,43,57],
                 [8,28,36,56], [9,27,37,55], [10,26,38,54], [11,25,39,53], [12,24,40,52], [13,15,49,51],
                 [14,18,46,50], [16,20,44,48], [17,19,45,47], [0,4,32,60]]
T   = [4] * 16      # one packet per visit, for 64-bit keys and 128-bit values alike (v1.1)
GAP = 0             # cycles after each group; 4 for phases that run every stream at the full
                    # window (the two-hop layers TH and BT), spec/machine-spec.md 4.2
```

**In the tables.** Each group is one `GROUP` entry with t − 1 = 3, gap = `GAP`, and start = the first of its four consecutive `STREAM` entries, one per offset with *d* = the offset (`spec/machine-spec.md` 4.2). Only R, in the entry's low word, changes from pass to pass.

## 4. Why these groups

- **The bound.** Total link demand with these ties is RIGHT 64, LEFT 64, DOWN 64 and UP 60: the seven ties split 4 down and 3 up. There are 64 streams, so Σ T ≥ 64.
  - With 16 groups of at most four streams, the bound is met only if every group has T = 4. That means RIGHT = LEFT = DOWN = 4 and UP ≤ 4 in every group, with the four UP slots of slack used once.
  - Such partitions exist. An exact-cover search found 1,587 admissible 4-sets and at least 20,000 covers (the count was stopped there). The table is one of them.
- **Groups are closed under negation** (k in K ⇒ −k in K). This is a property of the table, not a requirement. The link loads, the queue bound (section 5) and the routing (`spec/machine-spec.md` 4.2, 4.6) don't use it. It is what the complement-pairing argument and the count below rest on.
- **Complement pairing.** A closed 4-set is two negation pairs {k, −k}.
  - A pair of type (|a|, |b|) has loads (|b|, |b|, |a|, |a|): one member goes right and down (or right and up), the other the reverse.
  - So two pairs have loads (|b₁| + |b₂|, same, |a₁| + |a₂|, same), and T = 4 needs |a₁| + |a₂| = |b₁| + |b₂| = 4. **A pair of type (a, b) goes with a pair of the complementary type (4 − a, 4 − b).**
  - Two special cases follow. A class's four sign variants is the complement pairing of (2, 2) with itself (group 12); for any other class the loads (2b, 2b, 2a, 2a) are unbalanced. Two variants of a class with two of its transpose balance only when the transpose is the complement: (1, 3) with (3, 1) (groups 3 and 9).
- **The loopback and +32.** Both are their own negatives, so a closed partition must put them in the same group. +32 [0, 4] takes DOWN 4, so the other pair must have no vertical hops and |b| = 4: {4, 60}. Group 15 is forced, and it carries the four UP slots of slack.
- **Counting.** The pair types match their complements one to one:
  - one pair each: (0,1)–(4,3), (0,2)–(4,2), (0,3)–(4,1), (1,0)–(3,4), (2,0)–(2,4), (3,0)–(1,4);
  - two each: (1,1)–(3,3), (1,2)–(3,2), (1,3)–(3,1), (2,1)–(2,3);
  - (2,2): two, in one group.

  So there are exactly **2⁴ = 16 closed partitions with Σ T = 64** (confirmed by exhaustive exact cover), for each of the 16 negation-consistent tie choices and for no other.
- **Which of the 16.** The choice among them is immaterial (≤ 0.2 % on every data set). The table matches pairs of equal sign pattern and puts the loopback group last.
- **Memory.** Each tile reads one 64-bit record and receives one per cycle in every group. With the send and receive buffers in different areas, each area stays within one access per cycle (`spec/machine-spec.md` 4.1).

## 5. Queue bound (why 16 entries suffice)

- **Within a group.** Per 4-cycle period an output receives at most 4 packets. With at most four inputs they need at least one cycle to arrive, during which one leaves. So the total occupancy of an output's FIFOs never exceeds T − T/4 = **3**, and the periodic pattern does not accumulate.
- **At a group boundary.** The previous group's last packets are still in flight while the next group starts. Two overlapping groups give at most 8 packets per 4 cycles, and at most 6 in the queues.
- **Margin.** Against 16 entries, the margin is at least 10.
- **Measured** [meas: meshsim, full size, on uniform, Zipf-raw, trace and 99 % one value; v1.0 measurement; to be re-measured in v1.1]:
  - link FIFOs peak at 4 of 16, eject at 3–4, and the write queue at 4 (against 5 and 8 for a nine-group schedule);
  - no late rotations, stalls or overflows.
- **Gap 0.** With no gap between groups, the queue peaks are the same as with gap 4 on uniform and on all the skewed sets, with no drift [meas: meshsim] (`spec/machine-spec.md` 4.2).

## 6. Cost

**Timetable.** A group run takes Σ_g (max(R_g·T_g, 1) + gap), counted from S_0, the first-request cycle, 4·`ts_engine_latency` + 3 cycles after the start store (`spec/machine-spec.md` 4.2). R_g is the largest stream of group g over the whole mesh, so skew enters only through R_g. A large stream inflates a group of four visits.

**Measured** [meas: meshsim, `AllToAllTest measure`, 64 × 65,536, link seed 1; v1.0 measurement; to be re-measured in v1.1]:
- **Conditions.** These runs used an 8-cycle gap after each group, and the bounds include it.
- **Comparison.** The nine-group schedule they are compared against has Σ T = 66.

| data set | nine groups: bound / measured | sixteen groups: bound / measured | change |
|---|---|---|---|
| uniform | 73,816 / 73,904 | 71,268 / **71,345** | −2,559 (−3.5 %) |
| Zipf-raw s = 1.0 | 304,586 / 304,647 | 294,520 / **294,580** | −10,067 (−3.3 %) |
| trace hot = 100, frac = 0.30 | 800,146 / 800,218 | 706,408 / **706,470** | −93,748 (−11.7 %) |
| 99 % one value | 575,850 / 575,905 | 434,652 / **434,712** | −141,193 (−24.5 %) |

The measured pass is 55–88 cycles over the bound in every case.
