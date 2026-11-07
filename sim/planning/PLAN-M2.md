# Milestone 2: the sort on the new framework, multithreaded, against `ref`

Draft, 2026-11-07, from the owner's target; to be refined when W7's audit lands. Decisions are in `DECISIONS.md`;
M1's plan, and the items it carried here, are in `PLAN-M1.md`.

## The target (owner, 2026-11-07)

**The full-size sort** (`p4/uniform-k128`; `SortTest` in `sim/ref/meshsim/test/`, the program in `sim/ref/sort/`) runs on
the new framework, **exactly as in `ref`** (the same cycle count, 1,229,687 at GO/READY latency 8, and the same results
and fingerprints), **multithreaded** (M1's form 2: one chip over several threads), so we see how fast it is against
today's engine.

- **Today:** about **110 s** on one `ref` JVM on the runner (110.1 s on `main`, 92.2 s with `sim-step-cost`'s A + C;
  WORKER29, Quest 120): about 90 µs per chip-cycle.
- **The aim:** about **4 s**, excluding setup (build, image and input load): about **3.3 µs per chip-cycle**, ~27× today.
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
| W7 | prompt written 2026-11-07 (`sessions/WORKER7/PROMPT.md`) |
