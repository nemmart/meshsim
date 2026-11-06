# Runner jobs (meshsim v1.1, simulator session, 2026-10-27)

Two independent jobs, each self-contained: Java only, no RISC-V toolchain, no network, no build step.

| job | directory | runs | time on the runner | memory | pass |
|---|---|---|---|---|---|
| 1 | `p4/` | the sort's 43 full-size P4 runs (64 × 65,536 keys) | 21 min at `-j 8` (task 065) | ≈ 28 GB at `-j 8` | `run_p4.sh` exits 0 |
| 2 | `suites/` | CtrlTest full, EngineTest full, the skew table, the full sweep | 19 min at `-j 4` (task 067) | ≈ 18 GB at `-j 4` | `run_suites.sh` exits 0 |

Each script takes an output directory and a parallelism, writes per-run logs and a `VERDICT.txt`, and exits non-zero
if any run fails or mismatches. Details in each directory's README.
