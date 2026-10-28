# meshsim v1.0 verification kit: v1.0 against M15

Runs the same work on meshsim M15 (`m15/`, the last milestone before v1.0) and on meshsim v1.0 (`v1.0/`), one
independent JVM per run, then compares them. **Java 17 or later; nothing else.**

| group | runs | expected |
|---|---|---|
| `bt` | the two full-size BT runs (64 × 65,536, trace hot 10 / 10 %, 32-bit and 128-bit values), on each version | path BT, checks passing, phase-level neutral (below) |
| `split` | SplitRun on the split's 25 sets (made by `SplitSets` into `sets-65536/`, 350 MB), on each version | identical: exact (the shaped sets 21 and 23 are run balanced and are not exact on either), S, search steps, cycles per step, invariant 8, no violations |
| `kit` | the sort kit's 92 runs at 64 × 2,000, on each version | checks passing, same path, phase-level neutral |

**Phase-level neutral** (`PhaseNeutral` with its v1.0 start rule): the data plane identical in every phase (requests,
stalls, gathers, read-port reads, packets, latencies, invariant 8, peaks), except that a run is counted in the phase that
starts it (v1.0: start loads the run; M15 counted it where it was armed: `agRuns` and `peak_agTable` may move between
phases, run totals equal) and that an overlay that changed size moves its loading phase's packet counts by exactly 64 per
packet; every phase's cycle change explained by the slowest tile's controller change plus the overlay packets, within 2
cycles, or within at most one lead-in (4 `ts_engine_latency` + 2) per run the phase starts. TOTALs differ by tens of
cycles and are reported, not required equal.

```
./run.sh                    # everything, 12 at a time, 3 GB per JVM, into results/
./run.sh -j 10 -m 3g -o results -g bt,split,kit
./run.sh -s                 # the summaries only
```

At the end: each version's `SUMMARY.md`, `COMPARE-kit.md` and `COMPARE-bt.md` (step by step), `PHASES-kit.md` and
`PHASES-bt.md` (phase by phase) and **`VERIFY.md`** (the verdicts). Restartable: a finished run is skipped. Please upload
`results/` (or at least `VERIFY.md`, the `PHASES-*.md` and `COMPARE-*.md` files and any failed run's log).

Checked here before packaging: the 92 kit runs at 64 × 2,000 and the split's 25 sets on both versions, one at a time
(`../RESULTS.md`); the two full-size BT runs are for the laptop.
