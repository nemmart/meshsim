# Runner job 1: the sort's 43 full-size P4 runs (meshsim v1.1, spec 2026-10-26n / 26r)

**What it runs.** The whole sort (L1 to L2) on meshsim v1.1 with the sort program's prebuilt tile images (`kit/image/`,
built against `mesh.h` 1.1), at 64 tiles × 65,536 keys (N = 4M): the 20-set catalogue in both formats (32-bit key with
a 128-bit value, `k128`, and with a 32-bit value, `kv32`) plus three built-in sets (5 distinct keys, ragged sizes,
shaped targets): 43 runs, `ready_latency` 8. Every run checks its sorted output, every value against its key, the
counters and the invariants. The key sets are generated on first use by `KeyGen` (in the jar) into `kit/keys-65536/`
(20 × 16 MB, a few seconds each). Nothing else is needed: Java 17 or later, no RISC-V toolchain.

**Run** (from a fresh unpack, e.g. in /tmp):

    cd runner/p4
    ./run_p4.sh -j 8 /tmp/p4-results              # all 43
    ./run_p4.sh -j 8 -l mylist.txt /tmp/p4-results   # only the ids listed (e.g. p4/uniform-k128), one per line
    ./run_p4.sh -j 8 -n 2000 /tmp/p4-smoke           # a smoke test: 64 x 2,000 keys, a few minutes

The script is restartable: a run whose CSV exists in the output directory is skipped.

**Output** (in the output directory): per run `<id>.log` (its `inputs:` line with the SHA-256 of every file it read, its
checks, its report), `<id>.csv` (one row per sort step and a TOTAL row) and `<id>.phases.csv` (every C&C phase);
`SUMMARY.md` and `all.csv` (SortSummary: the P4 table, the Inputs check); `VERDICT.txt` (one line per run).

**Pass:** exit status 0, meaning every selected run finished (has its CSV), its log reads `SortTest: n passed, 0
failed`, and SUMMARY.md flags no run under Inputs (every file matched `kit/HASHES`). `VERDICT.txt` names any failure.

**Time:** measured on the runner (task 065): **21 minutes** for all 43 at `-j 8` (0.7 to 2.0M simulated cycles a run). If a submission is cut off, resubmit the same command with the same output directory:
finished runs are kept.

**Memory:** 3 GB of heap per JVM (`-m 3g`), about 3.5 GB resident each: about 28 GB at `-j 8`; plus 320 MB of key
sets on disk.

**Send back:** the whole output directory (or `VERDICT.txt`, `SUMMARY.md`, `all.csv`, the `*.phases.csv` and any
failed run's log).
