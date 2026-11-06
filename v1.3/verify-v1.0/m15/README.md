# The mesh sorter's full-size measurements: P4, the shuffled check and the whole-sort sweep

Worker 8, 3 October 2026; rebuilt by worker 9 for **meshsim v4.0 M8** (the address generator without slot working
registers, `design_v2/spec/PROPOSAL-slot-iteration-counter.md`), by worker 10 for **M9** (the inputs check, below;
the simulator is M8's, cycle-identical), and by worker 11 for **M10** (the controller read port's results return as
mode-6 packets through the tile's loopback, spec 4.3 of 4 October: a few cycles more per search step in the split, nothing
else changes). The sort program, its images, the job list and the
options are M7's, unchanged (the images are byte-identical). Runs on any machine with **Java 17 or later; nothing else**
(no Python, no RISC-V toolchain).

**M8: the comparison with M7.** A full-size `./run.sh` ends by writing `results/COMPARE-M7.md`: every run against
M7's results (`ref-m7/`, = `design_v2/sort/results-p4`), step by step and phase by phase. M8 is expected to be
cycle-identical except for gains: about 3–4 cycles per phase that arms an address generator and polls `rd_ready`
(it comes sooner), LB's sorted and all-equal runs (no late groups), and the sweep's `ts_engine_latency` 2–4 points
(see `meshsim-v4.0-M8-RESULTS.md`). By hand: `java -cp meshsim.jar SortCompare results ref-m7 [out.md]`.
`cfg.agLookahead=N` sets the read-ahead depth (default 2·`ts_engine_latency` + 2).

**M9: the inputs check.** Two kits with different tile images once gave different results for the same run (DEV 100),
unnoticed. Now every run's log starts with an `inputs:` line, the SHA-256 of every file it reads (`meshsim.jar`,
`image/*`, `hops.txt`, the key set), and the kit ships `HASHES`, the reference set. `run.sh` checks the kit and the key
sets against `HASHES` before running (a warning, not a stop), and `SUMMARY.md` opens with an **Inputs** section that
flags any run whose files differ from `HASHES`, runs with no inputs line (older kits), and a results directory that
mixes kits. By hand: `java -cp meshsim.jar Inputs check HASHES`.

**M10.** Only the simulator changed; the images, the job list and the options are M7's. `HASHES` is regenerated (the jar
differs). The expected difference from M9 is confined to the SPLIT step: about +0.2K cycles on uniform, more on skewed
sets [estimate; measured at 64 × 2,000 by the verification kit, `meshsim-v4.0-M10-RESULTS.md`]. `COMPARE-M7.md` (a
full-size run) will show it on every run.

**M11 (worker 12, 8 October): the rename.** The lead's 8 October names (spec section 1: `CNT` → `HIST`, `SLOT` →
`STREAM`, `COUNTS` → `GROUP`, SRAM read/write queues, histogram digit) in the simulator and the sort program; no
behaviour change. The images are byte-identical to M10's in every code and data section; only `sort.sym` changes
(`SLOT_OF` is `STREAM_OF`). `HASHES` is regenerated (the jar and `sort.sym` differ). One persistent name changed: the
phase CSVs' column `peak_cntReq` is now `peak_histReq`; `SortCompare` reads both, so M10-era results (`ref-m7/`,
`design_v2/sort/results-p4`) still compare. Every result is expected to equal M10's.


**M12 (worker 13, 9 October): the 9 October modes** (`design_v2/spec/PROPOSAL-mode-renumbering.md`, merged in spec 3.1,
4.8, 4.9). The packet mode is 4 bits; augmenting is mode 4, chosen by the sender per stream (`MASTER_CONFIG`'s attach
flag is gone); controller-memory writes, mailbox writes and replies are modes 5, 6 and 7. The sort program sends its key
streams in mode 4 with 128-bit values (step 1's input pass, the keys all-to-all, and hop 2 of TH and BT). The images
change (`mesh.h` v7). No packet moves differently; the controllers run a few instructions more or fewer in some steps
and two overlays changed by one 8-byte packet, so totals move by tens of cycles (direct −6, LB 0, TH −56 to −114 at
64 × 2,000; BT at full size −882 and −295, M12 faster). `HASHES` is regenerated. `PhaseNeutral` (in the jar) compares two results directories phase by phase on
that basis: `java -cp meshsim.jar PhaseNeutral OLD_DIR NEW_DIR OLD_sort.sym NEW_sort.sym [out.md]`. `COMPARE-M7.md`
(a full-size run) will show the same small differences against M7.
**M13 (worker 14, 10 October): the wake timer** (spec 4.12, decided 10 October; `meshsim-v4.0-M13-RESULTS.md`). The
simulator gains `WAKE_TIMER` and the timer-late status bit; the sort doesn't use them. The images are byte-identical to
M12's (built against `mesh.h` v8, which only adds names); only the jar changes, so `HASHES` is regenerated. Every result
is expected to equal M12's exactly (checked on full-size and 64 × 2,000 runs in RESULTS).
**M14 (worker 16, 10 October): the status reorganisation** (spec 4.13, 8.3a, 4.12, 6.6; `meshsim-v4.0-M14-RESULTS.md`).
The error conditions move to a new read-only `MASTER_CONDITIONS`; drift replaces "late"; done early and the done gate;
the error line to C&C and REPORT. The sort reads none of the old status bits, so its images are byte-identical to
M13's (built against `mesh.h` v9); only the jar changes, so `HASHES` is regenerated. Every step is expected to equal
M13's to the cycle. One accounting column changes meaning: the phase CSVs' `agLate` counts only rotations delayed by
read-queue stalls (drift); read-ahead waits at group starts and zero-length groups are timetable since M14 and are no
longer counted there. Each run's log gains one line, the zero-length groups (and those with a gap) and the group-run
stall cycles.
## Setup

- **Java 17 or later** (`java -version`); the simulator is `meshsim.jar`, built with `--release 17`.
- **The key sets** are generated by `KeyGen` (in the jar): design v1's `data.py` generators ported to Java (numpy's
  SeedSequence, PCG64 and sampling routines), verified byte-identical with `data.py` on all 20 sets at 64 × 2,000 and
  64 × 65,536. They are made on the first run into `keys-65536/` (20 × 16 MB).
- Memory: 3 GB of heap per run by default; 12 at a time need about 40 GB.

## Run

```
./run.sh                      # everything: 92 runs, 12 at a time, 3 GB each, into results/
./run.sh -j 10 -m 3g -o results
./run.sh -n 2000 -o smoke -g p4 -f p4/uniform      # a smoke test: small key sets, a few minutes
./run.sh -s -o results        # summary only
```

Options: `-j` parallel runs (default 12), `-m` heap per JVM (default 3g), `-o` results directory, `-n` keys per tile
(default 65,536: N = 4M), `-g` groups (any of `p4,shuffle,sweep`), `-f` only ids containing a string, `-J` another
job file, `-s` the summary only.

**Smoke-tested** (3 October, here): `-n 2000` with every P4 run (43), every P4 run shuffled (43) and three sweep points,
one at a time: every check passing, every shuffled run identical to its fixed-order run, about 15 to 30 s per run.

- **Order:** `jobs.txt` lists the runs longest first (BT and the 128-bit format first), so the long ones start on
  the fast cores.
- **Restartable:** a run whose CSV exists is skipped. A run in progress writes `<id>.csv.part`, renamed at the end.
- **Time:** a full-size run takes about 3 to 7 minutes on one core here (1.3 to 2.6M simulated cycles at about 7K
  cycles/s); a laptop's performance cores are likely faster, its efficiency cores slower [estimate]. All 92 runs at
  `-j 12`: roughly 45 to 60 minutes [estimate].
- **Key sets** are generated on the first run, only those the selected runs need (seconds each).

## What runs (`jobs.txt`, made by `tools/make_jobs.py`)

Every run uses `ready_latency` 8, the physical default (lead, 3 October).

| group | runs | what |
|---|---|---|
| p4 | 43 | worker 5's 20-set catalogue (design v1's `data.py`, seed 0) in both formats (32-bit key + 128-bit value `k128`, + 32-bit value `kv32`), plus worker 6's three built-in sets (5 distinct keys, ragged sizes, shaped targets) |
| shuffle | 3 | uniform `k128`, trace hot 100 / 30 % `k128` (BT), reverse sorted `kv32` (TH), with the components' order shuffled every cycle (seed 4): each must equal its fixed-order run exactly |
| sweep | 46 | the whole sort, uniform and trace hot 100 / 30 %, `k128`, over M6's parameters one at a time plus the all-pessimistic corner: array read latency 1/3/4, area queue depth 4/16, `ts_engine_latency` 2/3/4, `ready_latency` 1/4, GO/RUN latency 4/8, link latency 2/4, start skew off and seeds 2-6, `cm_latency` 4 |

## Results

Per run, in the results directory (`/` in the id becomes `_`):
- `<id>.csv`: one row per step of the sort (L1, SPLIT, XB, RED, XC, TH, SWEEP, DESC, K, K1.x, K2.x, V, V1.x, V2.x,
  L2) with its cycles (GO to the next step's GO), overlay-load cycles and peaks, then a TOTAL row (L1's GO to L2's
  all READY);
- `<id>.phases.csv`: every C&C phase (PhaseLog): cycles, READY spread, instructions, address-generator requests,
  peaks of links, eject, write-engine input, area queues and more;
- `<id>.log`: (M9) the `inputs:` line, then the run's checks (the sorted output, every value with its key, counters, invariants) and its report.

`SortSummary` then writes `SUMMARY.md` (the P4 table, the shuffled identity check, each sweep with its band) and
`all.csv` (every CSV row). Please upload the whole results directory (or `SUMMARY.md`, `all.csv`, the
`*.phases.csv` and any log of a failed run).

A run's checks are in its log (`SortTest: n passed, 0 failed`). A run that failed a check still writes its CSV, with
the count in `checks_failed`; one that did not finish has a log and no CSV, and `SUMMARY.md` lists it.

## Contents

| path | what |
|---|---|
| `run.sh`, `tools/runjob.sh` | the runner |
| `jobs.txt`, `tools/make_jobs.py` | the run list and its generator |
| `meshsim.jar` | meshsim v4.0 M9 (`src/`, = M8's) with `SortTest`, `SortSummary`, `SortCompare`, `KeyGen` and `Inputs` |
| `HASHES` | (M9) the reference set: SHA-256 of the jar, the image, `hops.txt` and the 20 key sets at 64 × 65,536 and 64 × 2,000 |
| `ref-m7/` | M7's results (`design_v2/sort/results-p4`, CSVs only), the reference `SortCompare` compares a full-size run against |
| `hops.txt` | the simulator's route table (read from the working directory) |
| `image/` | the sort program built for the tiles: the resident loader, the kernel, the first overlay, the staged overlay images, data, symbols |
| `meshsim.jar`'s `KeyGen` | the key sets: design v1's generators, cut into 64 tiles in generation order (`java -cp meshsim.jar KeyGen DIR --n 65536 [--only set,...]`) |
| `tools/make_jobs.py` | how `jobs.txt` was made (not needed to run) |
