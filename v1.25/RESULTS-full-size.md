# The full-size runs on the runner machine (meshsim v1.1, spec 2026-10-26n / 26r)

Run on the runner machine from `runner.tgz` (this archive's `runner/`); its outputs are in `results/runner/`.

## Task 065: the sort's 43 full-size P4 runs (64 × 65,536 keys, N = 4M)

`runner/p4/run_p4.sh -j 8`: **all 43 runs pass every check** (the sorted output, every value with its key, counters,
invariants), every run read the reference inputs (`kit/HASHES`), 21 minutes at `-j 8`. Outputs:
`results/runner/065-p4-full/` (VERDICT.txt, SUMMARY.md, all.csv and every run's CSVs).

**Paths:** identical to v1.0 on all 43: BT chosen naturally on the 18 trace sets, few5-k128, zipf10-k128 and
zipfraw10-k128; TH on reverse; LB on equal, one99 and sorted; direct elsewhere.

**Against v1.0** (`results/full-size/`, the v1.0 kit at the same size), cycles from L1 GO to L2 all READY:

- **128-bit values (k128): 4.3 % to 13.4 % faster** (median 7.5 %). A 128-bit value now travels as one packet
  (v1.1), so every value step takes about half its v1.0 time: on uniform-k128 V goes 142,226 → 71,155; on
  trace100-30-k128 each of the eight V1.* and V2.* steps is about 21,900 shorter.
- **32-bit values (kv32): 0.4 % to 0.7 % slower** (median 0.55 %): the 2-cycle register loads (spec 4.11's reference)
  in the controller's loops; on uniform-kv32 L1 +1,055, SPLIT +1,185, L2 +1,060.
- The range is now 0.71M (equal-kv32) to 2.03M cycles (trace100-60-k128); v1.0's was 0.71M to 2.19M.

| set | v1.0 path | v1.0 total | 26n path | 26n total | change | % | checks failed |
|---|---|---:|---|---:|---:|---:|---:|
| equal-k128 | LB | 1,233,033 | LB | 1,170,711 | -62,322 | -5.05 % | 0 |
| equal-kv32 | LB | 706,831 | LB | 709,887 | +3,056 | +0.43 % | 0 |
| few5-k128 | BT | 1,924,816 | BT | 1,726,853 | -197,963 | -10.28 % | 0 |
| one99-k128 | LB | 1,778,625 | LB | 1,539,919 | -238,706 | -13.42 % | 0 |
| one99-kv32 | LB | 898,884 | LB | 902,331 | +3,447 | +0.38 % | 0 |
| ragged-kv32 | direct | 824,340 | direct | 828,605 | +4,265 | +0.52 % | 0 |
| reverse-k128 | TH | 1,655,602 | TH | 1,530,426 | -125,176 | -7.56 % | 0 |
| reverse-kv32 | TH | 990,757 | TH | 996,233 | +5,476 | +0.55 % | 0 |
| shaped-k128 | direct | 1,603,974 | direct | 1,493,642 | -110,332 | -6.88 % | 0 |
| sorted-k128 | LB | 1,431,047 | LB | 1,370,127 | -60,920 | -4.26 % | 0 |
| sorted-kv32 | LB | 904,845 | LB | 909,303 | +4,458 | +0.49 % | 0 |
| trace10-10-k128 | BT | 2,140,800 | BT | 1,978,129 | -162,671 | -7.60 % | 0 |
| trace10-10-kv32 | BT | 1,301,575 | BT | 1,310,565 | +8,990 | +0.69 % | 0 |
| trace10-30-k128 | BT | 2,146,115 | BT | 1,983,118 | -162,997 | -7.59 % | 0 |
| trace10-30-kv32 | BT | 1,306,859 | BT | 1,315,914 | +9,055 | +0.69 % | 0 |
| trace10-60-k128 | BT | 2,146,603 | BT | 1,984,311 | -162,292 | -7.56 % | 0 |
| trace10-60-kv32 | BT | 1,307,293 | BT | 1,316,987 | +9,694 | +0.74 % | 0 |
| trace100-10-k128 | BT | 2,135,722 | BT | 1,972,824 | -162,898 | -7.63 % | 0 |
| trace100-10-kv32 | BT | 1,295,837 | BT | 1,304,817 | +8,980 | +0.69 % | 0 |
| trace100-30-k128 | BT | 2,171,583 | BT | 2,008,961 | -162,622 | -7.49 % | 0 |
| trace100-30-kv32 | BT | 1,333,350 | BT | 1,342,823 | +9,473 | +0.71 % | 0 |
| trace100-60-k128 | BT | 2,191,506 | BT | 2,029,320 | -162,186 | -7.40 % | 0 |
| trace100-60-kv32 | BT | 1,351,635 | BT | 1,361,334 | +9,699 | +0.72 % | 0 |
| trace10000-10-k128 | BT | 2,136,637 | BT | 1,973,213 | -163,424 | -7.65 % | 0 |
| trace10000-10-kv32 | BT | 1,297,630 | BT | 1,306,054 | +8,424 | +0.65 % | 0 |
| trace10000-30-k128 | BT | 2,155,796 | BT | 1,993,061 | -162,735 | -7.55 % | 0 |
| trace10000-30-kv32 | BT | 1,318,447 | BT | 1,327,474 | +9,027 | +0.68 % | 0 |
| trace10000-60-k128 | BT | 2,180,956 | BT | 2,018,846 | -162,110 | -7.43 % | 0 |
| trace10000-60-kv32 | BT | 1,344,860 | BT | 1,354,432 | +9,572 | +0.71 % | 0 |
| uniform-k128 | direct | 1,433,804 | direct | 1,366,634 | -67,170 | -4.68 % | 0 |
| uniform-kv32 | direct | 896,537 | direct | 900,296 | +3,759 | +0.42 % | 0 |
| zipf05-k128 | direct | 1,494,189 | direct | 1,427,664 | -66,525 | -4.45 % | 0 |
| zipf05-kv32 | direct | 956,147 | direct | 960,930 | +4,783 | +0.50 % | 0 |
| zipf08-k128 | direct | 1,560,992 | direct | 1,487,276 | -73,716 | -4.72 % | 0 |
| zipf08-kv32 | direct | 1,006,997 | direct | 1,012,574 | +5,577 | +0.55 % | 0 |
| zipf10-k128 | BT | 2,160,842 | BT | 1,998,640 | -162,202 | -7.51 % | 0 |
| zipf10-kv32 | direct | 1,293,864 | direct | 1,300,738 | +6,874 | +0.53 % | 0 |
| zipfraw05-k128 | direct | 1,427,348 | direct | 1,360,410 | -66,938 | -4.69 % | 0 |
| zipfraw05-kv32 | direct | 888,287 | direct | 893,167 | +4,880 | +0.55 % | 0 |
| zipfraw08-k128 | direct | 1,467,449 | direct | 1,385,537 | -81,912 | -5.58 % | 0 |
| zipfraw08-kv32 | direct | 897,960 | direct | 903,080 | +5,120 | +0.57 % | 0 |
| zipfraw10-k128 | BT | 1,975,181 | BT | 1,811,921 | -163,260 | -8.27 % | 0 |
| zipfraw10-kv32 | direct | 1,101,224 | direct | 1,107,171 | +5,947 | +0.54 % | 0 |

## Tasks 066 and 067: the full-size suites

`runner/suites/run_suites.sh -j 4`. Task 066 failed because `runner.tgz` left out `meshsim/data/` (the one99, trace and
zipfraw data sets the all-to-all suites read); the package now includes it, and `run_suites.sh` stops at once if it is
missing. Task 067, with the files restored: **all four suites pass**, 19 minutes at `-j 4`. Outputs:
`results/runner/067-suites-full/` (and 066's in `066-suites-full/`).

| suite | result | as shipped before this session (`meshsim/results`, 26n tree as received) |
|---|---|---|
| CtrlTest full (the all-to-all from C on four data sets, and the controller suite) | 7,167 passed, 0 failed | 6,713 passed |
| EngineTest full (full-size all-to-alls on four data sets, the radix sort) | 383 passed, 0 failed | 383 passed |
| NetTest skew table (fan-in under the 0/+1 start skew, 40 patterns) | ran; a table, no checks | ran |
| Sweep full (the parameter sweep) | ran (its log's tail in `067-suites-full/tails/`) | ran |
