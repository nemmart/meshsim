# WORKER24 notes (P6: the sort and its kit)

Branch `sim-w24`, from main 67d42fb. Rows: D1-D10, H4, H7 (V135-INVENTORY).

## The plan (agreed with the owner, 2026-11-11)

Commit and push after each step; each step checked against `ref`.

1. **The references, from `ref`, committed as fixtures** (drivers in `oracle/ref/`):
   - v1.3's logs of my steps: `sort-p1`, `sort`, `sort-shuf`, `sort-bt-kv32`, `sort-bt-k128`, `sweep-quick` (and its
     CSV), each run once in `ref`;
   - `SortFp`: SortTest's sequence for any run, a hash-only StateFp at every GO (as M2Sort): one run per path and format
     (direct, LB, TH with stage 2, shaped, p1; BT forced at 16K in both formats; PLAN if some v1.3 run takes it, Q6);
   - the kit at `-n 2000` (92 runs: the CSVs, phases CSVs, SUMMARY.md, all.csv), on the runner.
2. **The `sort` driver** (D1-D4, H4, H7): SortTest's arguments and output in C++, from `test_sort` and W20's `Mach`;
   Inputs (D5). Log-equal on p1, sort, sort-shuf, plain and under the debug options; fp at every GO.
3. **The CSVs**: the per-run CSV byte-identical (H4); the phases CSV through W20's PhaseLog (H3), not my own.
4. **Sweep** (D10): `sweep quick|full` on `Mach` with the a2a, gather and search images; `sweep-quick.log` and its CSV
   equal.
5. **The kit** (D8): `assemble.sh`, `run.sh` (the same options and jobs, restartable) on the C++ binaries; SortSummary
   (D6) and SortCompare (D7) in C++. The CSVs and SUMMARY byte-identical on the 2,000-key smoke, then a full-size sample
   on the runner.
6. **The steps join W20's runner** under v1.3's names; the quick ones in `make check`.
7. **REPORT.md**: D9's split driver is dead code, not ported.

## The owner's answers (2026-11-11)

- **Q1 (W20's pieces not on main: PhaseLog, Config fields, the runner).** Steps 1, 2 (without the peak lines) and 5's
  SortSummary and SortCompare first; the rest as W20's PhaseLog, Config fields and runner land.
- **Q2 (the `inputs:` line).** The binary's sha256 in place of `meshsim.jar=(classes)`; that one field masked in the
  log check. Tooling metadata, not the model; noted in REPORT.md.
- **Q3 (stack headroom, tile 0's profile, `sort.watch`).** Approved: a diagnostic-only lowest-SP record in `rv32.h`,
  taken at writes to x2, off by default and zero cost when off; idle-skipped spans attributed in bulk. Before and after:
  test_chip, test_sort, test_t2_harness, test_cc_units and M1's goldens unchanged. Noted here and in a comment.
- **Q4 (key sets).** The kit is a testing run, so no Java (D40): prebuilt key sets checked by hash by default;
  `pipeline/keys` called only if a JDK is present and the sets are missing.
- **Q5 (SortTest's ORACLE branch).** Left out and recorded like D9: no v1.3 build makes an image with `ORACLE`, so
  there is nothing to check against.
- **Q6 (PLAN).** Search the full-size results for a run that takes PLAN; if none exists, report it. Don't invent a case.

## Environment

- Container: 1 CPU, Ubuntu 24.04; installed `openjdk-21-jdk-headless`, `clang lld` (clang 18.1.3; gcc 13.3.0).

## Progress
