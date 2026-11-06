# Results (v1.0, and v1.1 on spec 26n)

**v1.1 (spec 2026-10-26n / 26r):** `runner/` holds the full-size runs made on the runner machine (task 065: the 43 P4
runs, all passing; tasks 066-067: the full-size suites), summarised with the v1.0 comparison in `../RESULTS-full-size.md`;
`kit-n2000/` the kit's 92 runs at 64 × 2,000 (`../RESULTS-n2000.md`). The rest of this page is v1.0's.

Measured on meshsim v1.0 with the kit (`../kit/`), on the lead's laptop. The sort orders 64 × 65,536 records (32-bit keys with 128-bit or 32-bit values); cycles run from the first step's GO to the last step's READY, everything included. At 1 GHz, 1M cycles is 1 ms.

| folder | what |
|---|---|
| `runner/` | v1.1: the runner machine's outputs (065 the P4 runs, 066-067 the full-size suites) |
| `kit-n2000/` | v1.1: the kit's 92 runs at 64 × 2,000 |
| `full-size/` | v1.0: every data set in both formats at full size, three shuffled-order repeats and the sensitivity sweeps: `SUMMARY.md` (the tables), `all.csv`, and each run's CSV, per-phase CSV and log |
| `verify/` | the verification of v1.0 against the previous build (`../verify-v1.0/`): `VERIFY.md` (the verdict), the comparisons and the phase-by-phase reports |
| `build/` | measurements made while building v1.0: the suites, the lead-in and synchronised-start timings, the fan-in table, eight phase-by-phase comparisons |

## Headline numbers (from `full-size/SUMMARY.md`)

| data set | path | 128-bit values | 32-bit values |
|---|---|--:|--:|
| uniform | direct | 1,433,804 | 896,537 |
| Zipf 0.8 | direct | 1,560,992 | 1,006,997 |
| Zipf 1.0 | BT (128-bit), direct (32-bit) | 2,160,842 | 1,293,864 |
| trace (nine sets) | BT | 2,135,722 – 2,191,506 | 1,295,837 – 1,351,635 |
| sorted | LB | 1,431,047 | 904,845 |
| reverse sorted | TH | 1,655,602 | 990,757 |
| all equal | LB | 1,233,033 | 706,831 |
| 99 % one value | LB | 1,778,625 | 898,884 |

- **Every run passes every check** (92 of 92), and the three shuffled-order repeats equal their fixed-order runs exactly.
- **The uncertain hardware timing barely matters:** sweeping every uncertain parameter (memory read latency, area queue depth, table latency, GO/RUN, READY, link and controller-memory latencies, start skew) moves the totals by **−0.05 % to +0.22 %** on trace data and −0.05 % to +0.15 % on uniform data.
- **Where the time goes:** the two local sorts (with their gathers) are fixed at about 997K cycles with 128-bit values and 604K with 32-bit; the split costs 25K–376K depending on the data; the transfers, planning included, 0.2–0.8M.

## Verification (from `verify/VERIFY.md`)

v1.0 against the previous build, on the lead's laptop: **all 92 kit runs phase-level neutral** (identical data plane; every cycle change explained; totals 11–35 cycles faster on v1.0), **both full-size BT runs as expected**, and **the split identical on all 25 sets**. Sets 21 and 23 fail one exactness check on both builds alike: they are shaped sets that the split's test driver runs as balanced. The split's "inputs" flags mean only that its logs carry no input-hash line; the inputs were checked against `HASHES-SPLIT` before the runs.
