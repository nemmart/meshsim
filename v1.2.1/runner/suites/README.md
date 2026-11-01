# Runner job 2: meshsim v1.1's full-size suites (spec 2026-10-26n / 26r)

**What it runs.** The four long suites of meshsim's `run_tests.sh all` that don't fit the simulator session's turns,
from prebuilt classes (`meshsim/build/`, Java 21 target), prebuilt RV32 tile images (`meshsim/sw/build/`) and the
all-to-all data sets (`meshsim/data/`: one99, trace, zipfraw): no RISC-V toolchain or build step. The script stops at
once if any of these is missing (task 066 failed for want of `data/`; `meshsim/kit/assemble_runner.sh` refreshes the
payload from the tree). They are independent and run in parallel.

| suite | what | checks |
|---|---|---|
| `ctrl-full` | `CtrlTest -1 full`: the 16-group all-to-all from C (`sw/tests/a2a.c`) at full size on the four data sets, plus the controller suite | yes |
| `engine-full` | `EngineTest -1 full`: full-size all-to-alls on four data sets and the radix sort, from Java | yes |
| `skewtable` | `NetTest -1 skewtable 40`: fan-in under the 0/+1 start skew, 40 skew patterns | a table (its last line reads `0 passed, 0 failed`) |
| `sweep-full` | `Sweep full`: the parameter sweep (array latency, area queue depth, `ts_engine_latency`, `ready_latency`, GO/RUN latency, link latency, start skew, `cm_latency`) over the all-to-all, gather, the read-port searches and a command round trip; writes `sweep-full.csv` | a table, no checks |

**Run** (from a fresh unpack, e.g. in /tmp):

    cd runner/suites
    ./run_suites.sh -j 4 /tmp/suite-results                 # all four
    ./run_suites.sh -l ctrl-full,skewtable /tmp/suite-results
    ./run_suites.sh -l smoke /tmp/suite-smoke              # seconds: checks the setup

**Output:** `<suite>.log` per suite, `sweep-full.csv`, and `VERDICT.txt`.

**Pass:** exit status 0, meaning every selected suite exited 0 and, except `sweep-full`, its log's last line reads
`... n passed, 0 failed`.

**Time:** measured on the runner (task 067): **19 minutes** at `-j 4`, set by `sweep-full`.

**Memory:** 4 GB of heap per JVM (`-m 4g`): about 18 GB at `-j 4`.

**Send back:** the whole output directory.
