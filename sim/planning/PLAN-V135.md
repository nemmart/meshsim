# v1.35: the fast simulator, frozen

Agreed with the owner, 2026-11-10 (D38). **v1.35 is meshsim's C++ implementation (`sim/`), frozen**: no other
improvements, only its docs, build scripts and cleanup, and **no Java**, provided v1.35 has an equivalent for
everything v1.3's Java meshsim had. The Java stays available as v1.3 (`v1.3/`, and `sim/ref` as the oracle).

## Steps

1. **The inventory** (W18): every capability of v1.3's Java meshsim (`run_tests.sh`'s suites, the test drivers, the
   recipes, the tools that make inputs, benches, options, outputs such as CSVs, phase logs and kits), each with its C++
   equivalent or a gap and a proposed disposition (port, or point to v1.3 for a reference-only tool). One table,
   `sim/planning/V135-INVENTORY.md`. **The owner rules on the gaps.**
2. **The gaps the owner chooses to port**, by one or two workers.
3. **Docs, build scripts and cleanup** (one worker): v1.35's README and BUILDING, how to run every suite and tool,
   stale paths, leftover benchmarks.
4. **The freeze check** on the runner (as v1.3's Quest 118): every suite, the full-size sort, T2 on both workloads,
   W17's 128-segment chains. Then `sim/` is copied, without `sim/ref`, to a frozen `v1.35/`, with its row in the
   top-level README.

## Status

| worker | state |
|---|---|
| W17 | all 128 segments back to back (segments 0-10 exact on both workloads; the full runs next): part of the freeze check |
| W18 | the inventory: prompt written 2026-11-10 (`sessions/WORKER18/PROMPT.md`) |
