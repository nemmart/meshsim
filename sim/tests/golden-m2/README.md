# golden-m2: M2's references (WORKER7)

What each run is, and how to compare against it: `AUDIT-M2.md` sections 6-8. In short:

- `RUNS`: every run (name, local or runner, kept in git or by hash, the driver command).
- the oracle's `golden-m2.sh [--check] [PATTERN]` (outside the frozen copy): rebuilds `sim/ref` and the oracle's drivers in a scratch copy of
  `sim/ref/meshsim` with `images/`, runs, and writes (or checks) every file's sha256 in `HASHES`. `ALL=1` adds the
  full-size sort (the runner: Quest 134 made it; its large files are under the runner's `$DATA/w7-golden-m2/`).
  `KEEP=DIR` keeps every file made, the hash-only ones included.
- Files: `NAME.out` (the run's facts: cycles, results, ref's digest; `timing:` lines removed), `NAME.fp` (the canonical
  state fingerprint: `cycle unit hash`), `NAME.trace` (a tile's retire trace, sim/tests/TRACE-FORMAT.md), `NAME.dig`
  (ref's per-chip digest), `NAME.cover.csv` (ref's counters per sort step), `NAME.phases.csv` (PhaseLog).
- The key sets are KeyGen's `uniform` (64 x 16,384 and 64 x 65,536), regenerated and checked against `HASHES`.
