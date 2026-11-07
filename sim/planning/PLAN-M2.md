# Milestone 2: the sort on the new framework, multithreaded, against `ref`

Draft, 2026-11-07, from the owner's target; to be refined when W7's audit lands. Decisions are in `DECISIONS.md`;
M1's plan, and the items it carried here, are in `PLAN-M1.md`.

## The target (owner, 2026-11-07)

**The full-size sort** (`p4/uniform-k128`; `SortTest` in `sim/ref/meshsim/test/`, the program in `sim/ref/sort/`) runs on
the new framework, **exactly as in `ref`** (the same cycle count, 1,229,687 at GO/READY latency 8, and the same results
and fingerprints), **multithreaded** (M1's form 2: one chip over several threads), so we see how fast it is against
today's engine.

- **Today** (W7, Quest 134, the runner pinned to one P-core, `ref` at `main`): the **counted span** (L1's GO to L2's
  all-READY, 1,229,687 cycles) takes **about 96 s** (95.80-95.98 s, ~78 µs per chip-cycle); setup before the first GO
  about 2 s, of which the boot (14,791 cycles) about 1.3 s; SortTest as a whole JVM 97.7 s. (The earlier "110 s" was a
  whole JVM in a different run, Quest 120.)
- **The aim:** about **4 s** for the counted span, excluding setup: about **3.25 µs per chip-cycle, ~24× today**.
  For scale: M1 measured ~4.5× unit for unit per thread (D22) and, for one chip split over threads (form 2, the only
  threading that counts: D26), ~3× on four threads so far (Quest 133), so the aim needs both pushed further, and chip-level work kept event-driven (D23).
- Measured on the owner's runner and recorded with its machine (D21); the same build runs anywhere.

## What it needs

Everything the sort exercises, which **W7's audit establishes** (`sim/tests/AUDIT-M2.md`). Likely: all 64 tile controllers
and their register blocks in full (write counters, events, DMA, sends), the router and injection path, the traffic
director and the controller memories' port B, the write engine, the SRAM read queue and big SRAM, the address generator,
the table store and HIST, the gather engine, the read port, the request port and request bus, C&C as `ref` runs it for the
sort (the Java sequencer `CnC`, unless the sort's suite uses the cores), and the harness's stand-ins (input load,
verification). Not needed unless the audit says so: the replayer, the Tracer, the peripherals.

Carried from M1 (`PLAN-M1.md`, "Carried to M2"): `regs.c` and `wfi.c`; the interrupt paths M1 couldn't test; a Link
whose consumer may hold up to a declared depth (W4's words held twice); the wheel's worst-case sizing; Wire chains within
one unit type; chip-level work event-driven (D23); the per-cycle dependency chain if it matters on busy workloads.

## Exactness

The oracle is `sim/ref`. A whole chip can't be checked by one controller's retire trace alone, so M2 compares, in this
order of preference: the cycle count and the sort's verified result; `ref`'s chip fingerprints and logs (as WORKER28's
and WORKER29's proofs did); per-tile controller retire traces (M1's observer, D5, any tile); and, only where those don't
localise a mismatch, a finer reference. A finer reference (e.g. a trace of puts and takes on a tile's named buses) would
extend D5's one observer, so it is the owner's decision, with W7's proposal and its cost.

## Order (to be refined after the audit)

1. **W7, the reference side** (starts now): the audit, the goldens, the `ref` baselines on the runner.
2. **Framework items** for the sort (from M1's carried list, as the audit says they're needed), by a framework worker.
3. **Units**, by several workers in parallel once M1 closes and the audit has landed, in groups the audit proposes, each
   checked against `ref` as it lands.
4. **One tile, then 64**: `regs`, `wfi`, the mesh tests (NetTest, all-to-all), the sort at n = 16,384, then full size.
5. **Speed**: one thread, then form 2 over 2-12 threads, against the 110 s, aiming at ~4 s. The two levers (D26): **speed
   per thread**, and **one chip split over threads** made cheaper (today +18% time and +52% memory at six parts on one
   thread, W4) and **balanced by load** rather than by rows (the mesh's 8 rows over 6 threads left two threads with twice the
   work). Form 1 is not a lever: it stays a thread-safety test.

## Status

| worker | state |
|---|---|
| W7 | **done**, merged 2026-11-07 (`sessions/WORKER7-REPORT.md`): `sim/tests/AUDIT-M2.md` (what the sort touches, the units, the order that matters, D23's chip-level events, unit groups G1-G4, the canonical state fingerprint), `sim/tests/golden-m2/` (references for regs, wfi, a2a, NetTest, EngineTest, the sort at 16K and full size), the baseline (Quest 134) |
| W8 | **done**, closed 2026-11-08 (`sessions/WORKER8-REPORT.md`): G1, the tile's control, and the fingerprint's C++ side |
| W9 | **done**, closed 2026-11-08 (`sessions/WORKER9-REPORT.md`): G2, the network and the 64-tile chip; `a2a`'s network part and NetTest pass to W11/W12; the sort's driver sequence to W12 |
| W10 | framework speed: the Link that holds words, the port observer, the stop request, the between-cycles wake, **the cheaper split** (64 tile-domains on one thread now cost the same as one: Quest 137) and **partitioning by load** (max/mean 1.01-1.07 at 2-12 threads), all on main; the mesh split over threads now **3.38× at 6 threads** (was 2.76×), flattening near 1.6 µs per mesh-cycle; next: Quest 138, then the drain |
| W11 | G3, the memory path (first `regs`' path from 825): prompt written 2026-11-08 (`sessions/WORKER11/PROMPT.md`); starts when W8's and W9's reports are merged |
| W12 | G4, the engines, and the sort end to end: prompt written 2026-11-08 (`sessions/WORKER12/PROMPT.md`); starts with W11 |

**Open at the end of 2026-11-07** (for the next integrator turn):
- **W8:** f3f344f (the ROUTER_OVF tile-level test) waits for its build-by-build `make check`, then merge. Next scope
  proposed to the owner, not yet agreed: G3 (the memory path), first `regs`' path from 825.
- **W9:** merged; idle. Next scope proposed, not yet agreed: G4 (AG, gather, the director's modes 2-4) on G3's read
  queue; meanwhile `a2a`'s network part waits on G3, NetTest last.
- **W10:** merged (bdceac0: direct delivery, the observer's per-window fix); next the placement objective, measured on
  W9's chip first.
