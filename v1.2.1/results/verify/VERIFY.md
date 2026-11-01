# v1.0 verification: meshsim v1.0 against M15

Made by `VerifySummary` from `results`. Detail: `PHASES-kit.md` and `PHASES-bt.md` (phase by phase), `COMPARE-kit.md` and `COMPARE-bt.md` (step by step), `m15/kit/SUMMARY.md` and `v1.0/kit/SUMMARY.md` (each kit's runs and its inputs check).

## Verdict

- Split: **check the table** (25 of 25 on both; exact 25, within band 25, inv. 8 unchanged 25, v1.0 clean 25; inputs 50 flags).
- Kit: **as expected** (92 runs on both; checks 92, path 92, inv. 8 92; phase-level neutral 92, largest residual 18.00; TOTAL -35 to -11).
- BT at full size: **as expected** (2 of 2 on both; 2 as expected).
- Inputs of the kit runs: see the Inputs section of `m15/kit/SUMMARY.md` and `v1.0/kit/SUMMARY.md`.

## The split: SplitRun on the 25 sets

Expected: exact on every balanced set on both (the two shaped sets, 21 and 23, are run as balanced splits by SplitRun and are not exact on either version); S, search steps and cycles per step identical (band 0.0 %); invariant 8's count unchanged; no violations, and no read-port overlaps on v1.0.

| set | label | exact M15 / v1.0 | S M15 | S v1.0 | change | % | cyc/step M15 | v1.0 | inv. 8 M15 | v1.0 | violations M15 / v1.0 | overlaps v1.0 | flags |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 00 | uniform (balanced) | yes / yes | 200,749 | 200,749 | +0 | +0.000 | 13.06 | 13.06 | 15,296 | 15,296 | 0 / 0 | 0 |  |
| 01 | zipf s=0.5 (balanced) | yes / yes | 258,275 | 258,275 | +0 | +0.000 | 13.82 | 13.82 | 18,176 | 18,176 | 0 / 0 | 0 |  |
| 02 | zipfraw s=0.5 (balanced) | yes / yes | 192,035 | 192,035 | +0 | +0.000 | 13.67 | 13.67 | 5,888 | 5,888 | 0 / 0 | 0 |  |
| 03 | zipf s=0.8 (balanced) | yes / yes | 300,846 | 300,846 | +0 | +0.000 | 16.61 | 16.61 | 11,904 | 11,904 | 0 / 0 | 0 |  |
| 04 | zipfraw s=0.8 (balanced) | yes / yes | 186,657 | 186,657 | +0 | +0.000 | 13.39 | 13.39 | 18,048 | 18,048 | 0 / 0 | 0 |  |
| 05 | zipf s=1.0 (balanced) | yes / yes | 342,438 | 342,438 | +0 | +0.000 | 15.31 | 15.31 | 43,072 | 43,072 | 0 / 0 | 0 |  |
| 06 | zipfraw s=1.0 (balanced) | yes / yes | 164,023 | 164,023 | +0 | +0.000 | 13.41 | 13.41 | 29,056 | 29,056 | 0 / 0 | 0 |  |
| 07 | trace hot=10 frac=0.10 (balanced) | yes / yes | 320,880 | 320,880 | +0 | +0.000 | 17.70 | 17.70 | 16,320 | 16,320 | 0 / 0 | 0 |  |
| 08 | trace hot=10 frac=0.30 (balanced) | yes / yes | 317,417 | 317,417 | +0 | +0.000 | 17.09 | 17.09 | 13,824 | 13,824 | 0 / 0 | 0 |  |
| 09 | trace hot=10 frac=0.60 (balanced) | yes / yes | 319,392 | 319,392 | +0 | +0.000 | 16.89 | 16.89 | 11,072 | 11,072 | 0 / 0 | 0 |  |
| 10 | trace hot=100 frac=0.10 (balanced) | yes / yes | 313,265 | 313,265 | +0 | +0.000 | 17.39 | 17.39 | 20,992 | 20,992 | 0 / 0 | 0 |  |
| 11 | trace hot=100 frac=0.30 (balanced) | yes / yes | 346,482 | 346,482 | +0 | +0.000 | 16.10 | 16.10 | 13,056 | 13,056 | 0 / 0 | 0 |  |
| 12 | trace hot=100 frac=0.60 (balanced) | yes / yes | 360,532 | 360,532 | +0 | +0.000 | 15.59 | 15.59 | 10,880 | 10,880 | 0 / 0 | 0 |  |
| 13 | trace hot=10000 frac=0.10 (balanced) | yes / yes | 313,663 | 313,663 | +0 | +0.000 | 17.29 | 17.29 | 16,768 | 16,768 | 0 / 0 | 0 |  |
| 14 | trace hot=10000 frac=0.30 (balanced) | yes / yes | 338,742 | 338,742 | +0 | +0.000 | 16.69 | 16.69 | 13,312 | 13,312 | 0 / 0 | 0 |  |
| 15 | trace hot=10000 frac=0.60 (balanced) | yes / yes | 361,911 | 361,911 | +0 | +0.000 | 15.59 | 15.59 | 8,320 | 8,320 | 0 / 0 | 0 |  |
| 16 | sorted (balanced) | yes / yes | 211,285 | 211,285 | +0 | +0.000 | 57.92 | 57.92 | 20,800 | 20,800 | 0 / 0 | 0 |  |
| 17 | reverse sorted (balanced) | yes / yes | 211,277 | 211,277 | +0 | +0.000 | 57.92 | 57.92 | 20,800 | 20,800 | 0 / 0 | 0 |  |
| 18 | all equal (balanced) | yes / yes | 20,617 | 20,617 | +0 | +0.000 | 71.15 | 71.15 | 64 | 64 | 0 / 0 | 0 |  |
| 19 | 99% one value (balanced) | yes / yes | 20,627 | 20,627 | +0 | +0.000 | 71.20 | 71.20 | 64 | 64 | 0 / 0 | 0 |  |
| 20 | one tile at X (balanced) | yes / yes | 49,741 | 49,741 | +0 | +0.000 | 11.86 | 11.86 | 64 | 64 | 0 / 0 | 0 |  |
| 21 | one tile at X (shaped) | **no** / **no** | 49,741 | 49,741 | +0 | +0.000 | 11.86 | 11.86 | 64 | 64 | 0 / 0 | 0 | **shaped set run balanced: not exact on either** |
| 22 | ragged at X (balanced) | yes / yes | 251,993 | 251,993 | +0 | +0.000 | 14.06 | 14.06 | 35,840 | 35,840 | 0 / 0 | 0 |  |
| 23 | ragged at X (shaped) | **no** / **no** | 251,993 | 251,993 | +0 | +0.000 | 14.06 | 14.06 | 35,840 | 35,840 | 0 / 0 | 0 | **shaped set run balanced: not exact on either** |
| 24 | lowkeys 3000 (balanced) | yes / yes | 170,152 | 170,152 | +0 | +0.000 | 13.55 | 13.55 | 21,760 | 21,760 | 0 / 0 | 0 |  |

25 of 25 sets on both: exact 25; S within 0.0 % 25 (change +0.000 % to +0.000 %, mean +0.000 %; summed S 5,874,733 -> 5,874,733, +0); invariant 8 unchanged 25; v1.0 clean (no violation, no overlap) 25.

**Split inputs** (each log's `inputs:` line against `HASHES-SPLIT`: the split jar and image of each version, `hops.txt`, the set): 

- m15/split/00: no inputs line
- v1.0/split/00: no inputs line
- m15/split/01: no inputs line
- v1.0/split/01: no inputs line
- m15/split/02: no inputs line
- v1.0/split/02: no inputs line
- m15/split/03: no inputs line
- v1.0/split/03: no inputs line
- m15/split/04: no inputs line
- v1.0/split/04: no inputs line
- m15/split/05: no inputs line
- v1.0/split/05: no inputs line
- m15/split/06: no inputs line
- v1.0/split/06: no inputs line
- m15/split/07: no inputs line
- v1.0/split/07: no inputs line
- m15/split/08: no inputs line
- v1.0/split/08: no inputs line
- m15/split/09: no inputs line
- v1.0/split/09: no inputs line
- m15/split/10: no inputs line
- v1.0/split/10: no inputs line
- m15/split/11: no inputs line
- v1.0/split/11: no inputs line
- m15/split/12: no inputs line
- v1.0/split/12: no inputs line
- m15/split/13: no inputs line
- v1.0/split/13: no inputs line
- m15/split/14: no inputs line
- v1.0/split/14: no inputs line
- m15/split/15: no inputs line
- v1.0/split/15: no inputs line
- m15/split/16: no inputs line
- v1.0/split/16: no inputs line
- m15/split/17: no inputs line
- v1.0/split/17: no inputs line
- m15/split/18: no inputs line
- v1.0/split/18: no inputs line
- m15/split/19: no inputs line
- v1.0/split/19: no inputs line
- m15/split/20: no inputs line
- v1.0/split/20: no inputs line
- m15/split/21: no inputs line
- v1.0/split/21: no inputs line
- m15/split/22: no inputs line
- v1.0/split/22: no inputs line
- m15/split/23: no inputs line
- v1.0/split/23: no inputs line
- m15/split/24: no inputs line
- v1.0/split/24: no inputs line

## The sort kit's runs at 64 × 2,000: v1.0 against M15, phase by phase

Expected: every check passing on both; the same path; the **data plane identical in every phase** (address-generator requests, stalls, gather requests, read-port reads, injected and ejected packets, read latencies, send stalls, invariant 8, every peak), except that a run is counted in the phase that starts it (v1.0: start loads the run; M15 counted it where it was armed), so `agRuns` and `peak_agTable` may move between phases with equal run totals, and the one phase loading an overlay whose size changed moves agReq, injected, ejected and latNA together by exactly 64 per packet; and **every phase's cycle change explained**: dCycles = the change in the slowest tile's busy controller cycles + k (overlay packets) + r, with |r| <= 2, or 0 <= r <= one lead-in (4 ts_engine_latency + 2) per run the phase starts (M15 pre-armed such runs, so their first request went out at the start). TOTALs move by tens of cycles and are reported, not required equal. Overlays (packets, M15 -> v1.0): btplan 510, btrun 354 -> 349, local 121, split 645, th 475 -> 473, xfer 328 -> 323. Phase detail: `PHASES-kit.md`; step detail: `COMPARE-kit.md`.

| run | path M15 / v1.0 | checks M15 | checks v1.0 | TOTAL M15 | TOTAL v1.0 | change | = controller cycles | + overlay | + residual (max) | inv. 8 M15 / v1.0 | flags |
|---|---|---|---|---|---|---|---|---|---|---|---|
| p4_equal-k128 | LB | 13 passed, 0 failed | 13 passed, 0 failed | 88,501 | 88,484 | -17 | -18.00 | -5 | +6.00 (3.00) | 64 / 64 |  |
| p4_equal-kv32 | LB | 12 passed, 0 failed | 12 passed, 0 failed | 70,589 | 70,578 | -11 | -9.00 | -5 | +3.00 (3.00) | 64 / 64 |  |
| p4_few5-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 294,708 | 294,682 | -26 | -26.00 | -12 | +12.00 (6.00) | 73,728 / 73,728 |  |
| p4_one99-k128 | LB | 13 passed, 0 failed | 13 passed, 0 failed | 106,303 | 106,284 | -19 | -26.00 | -5 | +12.00 (6.00) | 64 / 64 |  |
| p4_one99-kv32 | LB | 12 passed, 0 failed | 12 passed, 0 failed | 76,524 | 76,512 | -12 | -13.00 | -5 | +6.00 (6.00) | 64 / 64 |  |
| p4_ragged-kv32 | direct | 12 passed, 0 failed | 12 passed, 0 failed | 222,847 | 222,835 | -12 | -13.00 | -5 | +6.00 (6.00) | 29,952 / 29,952 |  |
| p4_reverse-k128 | TH | 12 passed, 0 failed | 12 passed, 0 failed | 269,386 | 269,351 | -35 | -52.00 | -7 | +24.00 (6.00) | 17,856 / 17,856 |  |
| p4_reverse-kv32 | TH | 11 passed, 0 failed | 11 passed, 0 failed | 239,729 | 239,708 | -21 | -26.00 | -7 | +12.00 (6.00) | 17,856 / 17,856 |  |
| p4_shaped-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 242,599 | 242,580 | -19 | -26.00 | -5 | +12.00 (6.00) | 20,096 / 20,096 |  |
| p4_sorted-k128 | LB | 13 passed, 0 failed | 13 passed, 0 failed | 235,198 | 235,181 | -17 | -18.00 | -5 | +6.00 (3.00) | 17,856 / 17,856 |  |
| p4_sorted-kv32 | LB | 12 passed, 0 failed | 12 passed, 0 failed | 217,286 | 217,275 | -11 | -9.00 | -5 | +3.00 (3.00) | 17,856 / 17,856 |  |
| p4_trace10-10-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 501,943 | 501,917 | -26 | -26.00 | -12 | +12.00 (6.00) | 24,704 / 24,704 |  |
| p4_trace10-10-kv32 | direct | 12 passed, 0 failed | 12 passed, 0 failed | 371,148 | 371,129 | -19 | -13.00 | -12 | +6.00 (6.00) | 24,704 / 24,704 |  |
| p4_trace10-30-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 463,697 | 463,671 | -26 | -26.00 | -12 | +12.00 (6.00) | 19,008 / 19,008 |  |
| p4_trace10-30-kv32 | direct | 12 passed, 0 failed | 12 passed, 0 failed | 360,714 | 360,695 | -19 | -13.00 | -12 | +6.00 (6.00) | 19,008 / 19,008 |  |
| p4_trace10-60-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 483,522 | 483,496 | -26 | -26.00 | -12 | +12.00 (6.00) | 12,608 / 12,608 |  |
| p4_trace10-60-kv32 | direct | 12 passed, 0 failed | 12 passed, 0 failed | 366,952 | 366,933 | -19 | -13.00 | -12 | +6.00 (6.00) | 12,608 / 12,608 |  |
| p4_trace100-10-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 535,684 | 535,658 | -26 | -26.00 | -12 | +12.00 (6.00) | 26,304 / 26,304 |  |
| p4_trace100-10-kv32 | direct | 12 passed, 0 failed | 12 passed, 0 failed | 384,531 | 384,512 | -19 | -13.00 | -12 | +6.00 (6.00) | 26,304 / 26,304 |  |
| p4_trace100-30-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,349 | 503,323 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| p4_trace100-30-kv32 | direct | 12 passed, 0 failed | 12 passed, 0 failed | 382,624 | 382,605 | -19 | -13.00 | -12 | +6.00 (6.00) | 22,144 / 22,144 |  |
| p4_trace100-60-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 471,244 | 471,218 | -26 | -26.00 | -12 | +12.00 (6.00) | 13,760 / 13,760 |  |
| p4_trace100-60-kv32 | direct | 12 passed, 0 failed | 12 passed, 0 failed | 389,332 | 389,313 | -19 | -13.00 | -12 | +6.00 (6.00) | 13,760 / 13,760 |  |
| p4_trace10000-10-k128 | LB | 13 passed, 0 failed | 13 passed, 0 failed | 490,528 | 490,502 | -26 | -26.00 | -12 | +12.00 (6.00) | 24,256 / 24,256 |  |
| p4_trace10000-10-kv32 | LB | 12 passed, 0 failed | 12 passed, 0 failed | 364,572 | 364,553 | -19 | -13.00 | -12 | +6.00 (6.00) | 24,256 / 24,256 |  |
| p4_trace10000-30-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 497,686 | 497,660 | -26 | -26.00 | -12 | +12.00 (6.00) | 20,992 / 20,992 |  |
| p4_trace10000-30-kv32 | direct | 12 passed, 0 failed | 12 passed, 0 failed | 383,774 | 383,755 | -19 | -13.00 | -12 | +6.00 (6.00) | 20,992 / 20,992 |  |
| p4_trace10000-60-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 459,862 | 459,836 | -26 | -26.00 | -12 | +12.00 (6.00) | 10,112 / 10,112 |  |
| p4_trace10000-60-kv32 | direct | 12 passed, 0 failed | 12 passed, 0 failed | 391,189 | 391,170 | -19 | -13.00 | -12 | +6.00 (6.00) | 10,112 / 10,112 |  |
| p4_uniform-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,762 | 250,743 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| p4_uniform-kv32 | direct | 12 passed, 0 failed | 12 passed, 0 failed | 230,778 | 230,766 | -12 | -13.00 | -5 | +6.00 (6.00) | 18,752 / 18,752 |  |
| p4_zipf05-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 280,909 | 280,890 | -19 | -26.00 | -5 | +12.00 (6.00) | 50,112 / 50,112 |  |
| p4_zipf05-kv32 | direct | 12 passed, 0 failed | 12 passed, 0 failed | 260,863 | 260,851 | -12 | -13.00 | -5 | +6.00 (6.00) | 50,112 / 50,112 |  |
| p4_zipf08-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 315,307 | 315,288 | -19 | -26.00 | -5 | +12.00 (6.00) | 36,800 / 36,800 |  |
| p4_zipf08-kv32 | direct | 12 passed, 0 failed | 12 passed, 0 failed | 295,302 | 295,290 | -12 | -13.00 | -5 | +6.00 (6.00) | 36,800 / 36,800 |  |
| p4_zipf10-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 386,182 | 386,163 | -19 | -26.00 | -5 | +12.00 (6.00) | 31,104 / 31,104 |  |
| p4_zipf10-kv32 | direct | 12 passed, 0 failed | 12 passed, 0 failed | 352,435 | 352,423 | -12 | -13.00 | -5 | +6.00 (6.00) | 31,104 / 31,104 |  |
| p4_zipfraw05-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 239,856 | 239,837 | -19 | -26.00 | -5 | +12.00 (6.00) | 9,792 / 9,792 |  |
| p4_zipfraw05-kv32 | direct | 12 passed, 0 failed | 12 passed, 0 failed | 219,946 | 219,934 | -12 | -13.00 | -5 | +6.00 (6.00) | 9,792 / 9,792 |  |
| p4_zipfraw08-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 240,554 | 240,535 | -19 | -26.00 | -5 | +12.00 (6.00) | 20,288 / 20,288 |  |
| p4_zipfraw08-kv32 | direct | 12 passed, 0 failed | 12 passed, 0 failed | 220,479 | 220,467 | -12 | -13.00 | -5 | +6.00 (6.00) | 20,288 / 20,288 |  |
| p4_zipfraw10-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 234,568 | 234,549 | -19 | -26.00 | -5 | +12.00 (6.00) | 29,440 / 29,440 |  |
| p4_zipfraw10-kv32 | direct | 12 passed, 0 failed | 12 passed, 0 failed | 201,128 | 201,116 | -12 | -13.00 | -5 | +6.00 (6.00) | 29,440 / 29,440 |  |
| shuf_reverse-kv32 | TH | 11 passed, 0 failed | 11 passed, 0 failed | 239,729 | 239,708 | -21 | -26.00 | -7 | +12.00 (6.00) | 17,856 / 17,856 |  |
| shuf_trace100-30-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,349 | 503,323 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| shuf_uniform-k128 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,762 | 250,743 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_trace100-30-k128_area-queue-16 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,349 | 503,323 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_area-queue-4 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,349 | 503,323 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_cm-4 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 504,421 | 504,395 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_corner | direct | 13 passed, 0 failed | 13 passed, 0 failed | 506,683 | 506,657 | -26 | -50.00 | -12 | +36.00 (18.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_default | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,349 | 503,323 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_go-run-4 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,640 | 503,614 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_go-run-8 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 504,028 | 504,002 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_link-2 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 502,854 | 502,828 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_link-4 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,844 | 503,818 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_ready-1 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 502,670 | 502,644 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_ready-4 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 502,961 | 502,935 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_skew-off | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,298 | 503,272 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_skew-seed-2 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,346 | 503,320 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_skew-seed-3 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,342 | 503,316 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_skew-seed-4 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,352 | 503,326 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_skew-seed-5 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,332 | 503,306 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_skew-seed-6 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,344 | 503,318 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_sram-read-1 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 502,924 | 502,898 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_sram-read-3 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,794 | 503,768 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_sram-read-4 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 504,239 | 504,213 | -26 | -26.00 | -12 | +12.00 (6.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_ts-latency-2 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,415 | 503,389 | -26 | -34.00 | -12 | +20.00 (10.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_ts-latency-3 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,481 | 503,455 | -26 | -42.00 | -12 | +28.00 (14.00) | 22,144 / 22,144 |  |
| sweep_trace100-30-k128_ts-latency-4 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 503,547 | 503,521 | -26 | -50.00 | -12 | +36.00 (18.00) | 22,144 / 22,144 |  |
| sweep_uniform-k128_area-queue-16 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,762 | 250,743 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_area-queue-4 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,762 | 250,743 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_cm-4 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 251,216 | 251,197 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_corner | direct | 13 passed, 0 failed | 13 passed, 0 failed | 252,930 | 252,911 | -19 | -50.00 | -5 | +36.00 (18.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_default | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,762 | 250,743 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_go-run-4 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 251,053 | 251,034 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_go-run-8 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 251,441 | 251,422 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_link-2 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,197 | 250,178 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_link-4 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 251,325 | 251,306 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_ready-1 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,083 | 250,064 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_ready-4 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,374 | 250,355 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_skew-off | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,730 | 250,711 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_skew-seed-2 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,774 | 250,755 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_skew-seed-3 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,774 | 250,755 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_skew-seed-4 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,771 | 250,752 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_skew-seed-5 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,767 | 250,748 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_skew-seed-6 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,766 | 250,747 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_sram-read-1 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,637 | 250,618 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_sram-read-3 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,905 | 250,886 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_sram-read-4 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 251,048 | 251,029 | -19 | -26.00 | -5 | +12.00 (6.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_ts-latency-2 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,824 | 250,805 | -19 | -34.00 | -5 | +20.00 (10.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_ts-latency-3 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,886 | 250,867 | -19 | -42.00 | -5 | +28.00 (14.00) | 18,752 / 18,752 |  |
| sweep_uniform-k128_ts-latency-4 | direct | 13 passed, 0 failed | 13 passed, 0 failed | 250,948 | 250,929 | -19 | -50.00 | -5 | +36.00 (18.00) | 18,752 / 18,752 |  |

92 runs on both: checks passing on both 92; same path 92; data plane identical and every phase explained 92 (largest residual 18.00 cycles); TOTAL change -35 to -11 (mean -20.9); invariant 8 unchanged 92.

## The full-size BT runs (64 × 65,536, trace hot=10 frac=0.10): v1.0 against M15, phase by phase

BT is infeasible at 64 × 2,000, so these two runs are the only ones that execute it. Expected: path BT on both, checks passing, and the same phase-level test as the kit. Detail: `PHASES-bt.md`, `COMPARE-bt.md`.

| run | path M15 / v1.0 | checks M15 | checks v1.0 | TOTAL M15 | TOTAL v1.0 | change | = controller cycles | + overlay | + residual (max) | flags |
|---|---|---|---|---|---|---|---|---|---|---|
| p4_trace10-10-k128 | BT / BT | 12 passed, 0 failed | 12 passed, 0 failed | 2,140,927 | 2,140,800 | -127 | -164.66 | -12 | +49.66 (24.00) |  |
| p4_trace10-10-kv32 | BT / BT | 11 passed, 0 failed | 11 passed, 0 failed | 1,301,627 | 1,301,575 | -52 | -62.00 | -12 | +22.00 (6.00) |  |
