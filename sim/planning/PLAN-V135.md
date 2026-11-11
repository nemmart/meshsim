# v1.35: the fast simulator, frozen

Agreed with the owner, 2026-11-10 (D38). **v1.35 is meshsim's C++ implementation (`sim/`), frozen**: no other
improvements, only its docs, build scripts and cleanup, and **no Java**, provided v1.35 has an equivalent for
everything v1.3's Java meshsim had. The Java stays available as v1.3 (`v1.3/`, and `sim/ref` as the oracle).

**Refined by D39 (owner, 2026-11-10): everything off Java, even if it takes several workers, and v1.35 a stand-alone
directory with all the source needed to build and run everything.** Every gap is ported (no "point to v1.3"); each port
is checked against v1.3 as M1-M3's units were.

## Steps

1. **The inventory** (W18): every capability of v1.3's Java meshsim (`run_tests.sh`'s suites, the test drivers, the
   recipes, the tools that make inputs, benches, options, outputs such as CSVs, phase logs and kits), each with its C++
   equivalent or a gap. One table, `sim/planning/V135-INVENTORY.md`. Under D39 every gap is ported: the inventory
   becomes the porting plan (estimates, worker-sized groups, the order, how each port is checked against v1.3).
2. **The ports**, by several workers in parallel, from the inventory's groups.
3. **Docs, build scripts and cleanup** (one worker): v1.35's README and BUILDING, how to run every suite and tool,
   stale paths, leftover benchmarks.
4. **The freeze check** on the runner (as v1.3's Quest 118): every suite, the full-size sort, T2 on both workloads,
   W17's 128-segment chains. Then `sim/` is copied, without `sim/ref`, to a frozen `v1.35/`, with its row in the
   top-level README.

## Status

| worker | state |
|---|---|
| W17 | **done**, closed 2026-11-10 (`sessions/WORKER17-REPORT.md`): all 128 segments of both workloads pass, back to back (Quest 174: evm 878.5 s, cyclone 849.7 s of simulation, 0.30 µs per cycle); part of the freeze check |
| W18 | the inventory: prompt written 2026-11-10 (`sessions/WORKER18/PROMPT.md`) |
