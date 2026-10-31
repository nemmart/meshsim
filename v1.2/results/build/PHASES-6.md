# Phase-level comparison: cmp/v1.0 against cmp/m15

Overlays (packets): btplan 510, btrun 354 -> 349, local 121, split 645, th 475 -> 473, xfer 328 -> 323; allowed per-tile packet changes [-12, -10, -7, -5, -2, 0].

| run | path | phases (slowest-tile basis / mean basis / within the tiles' range) | data plane | overlay phases | TOTAL change | = controller cycles | + overlay packets | + residual (max per phase) | instr/tile | changed phases: dCycles = d(basis) + k ov (r) |
|---|---|---|---|---|---|---|---|---|---|---|
| p4_reverse-k128 | TH | 98 (97 / 1 / 0) | identical, explained | 2 | -35 | -52.00 | -7 | +24.00 (6.00) | -44.00 | SPLIT -5 = +0.00 busy(t3) -5 ov; RED -2 = +0.00 busy(t36) -2 ov; DESC -13 = -13.00 busy(t3) +0 ov; K1.0 -7 = -13.00 busy(t3) +0 ov +6.00 [lead-in: 1 run x 6]; K2.0 -7 = -13.00 busy(t10) +0 ov +6.00 [lead-in: 1 run x 6]; V1.0 -7 = -13.00 busy(t52) +0 ov +6.00 [lead-in: 1 run x 6]; V2.0 +6 = +0.00 busy(t1) +0 ov +6.00 [lead-in: 1 run x 6] |
| p4_reverse-kv32 | TH | 87 (86 / 1 / 0) | identical, explained | 2 | -21 | -26.00 | -7 | +12.00 (6.00) | -22.00 | SPLIT -5 = +0.00 busy(t3) -5 ov; RED -2 = +0.00 busy(t36) -2 ov; DESC -13 = -13.00 busy(t3) +0 ov; K1.0 -7 = -13.00 busy(t3) +0 ov +6.00 [lead-in: 1 run x 6]; K2.0 +6 = +0.00 busy(t10) +0 ov +6.00 [lead-in: 1 run x 6] |
| p4_sorted-k128 | LB | 98 (97 / 1 / 0) | identical, explained | 1 | -17 | -18.00 | -5 | +6.00 (3.00) | -16.00 | SPLIT -5 = +0.00 busy(t3) -5 ov; RED -9 = -9.00 busy(t36) +0 ov; K -6 = -9.00 busy(t0) +0 ov +3.00 [lead-in: 1 run x 6]; V +3 = +0.00 busy(t0) +0 ov +3.00 [lead-in: 1 run x 6] |
| p4_sorted-kv32 | LB | 87 (86 / 1 / 0) | identical, explained | 1 | -11 | -9.00 | -5 | +3.00 (3.00) | -8.00 | SPLIT -5 = +0.00 busy(t3) -5 ov; RED -9 = -9.00 busy(t36) +0 ov; K +3 = +0.00 busy(t0) +0 ov +3.00 [lead-in: 1 run x 6] |
| p4_uniform-k128 | direct | 98 (97 / 1 / 0) | identical, explained | 1 | -19 | -26.00 | -5 | +12.00 (6.00) | -22.00 | SPLIT -5 = +0.00 busy(t3) -5 ov; RED -13 = -13.00 busy(t36) +0 ov; K -7 = -13.00 busy(t61) +0 ov +6.00 [lead-in: 1 run x 6]; V +6 = +0.00 busy(t61) +0 ov +6.00 [lead-in: 1 run x 6] |
| p4_uniform-kv32 | direct | 87 (86 / 1 / 0) | identical, explained | 1 | -12 | -13.00 | -5 | +6.00 (6.00) | -11.00 | SPLIT -5 = +0.00 busy(t3) -5 ov; RED -13 = -13.00 busy(t36) +0 ov; K +6 = +0.00 busy(t61) +0 ov +6.00 [lead-in: 1 run x 6] |

6 runs compared, 6 explained (data plane identical; every phase's cycle change explained by the slowest tile's controller within 2 cycles, or within the range of all tiles' changes).
