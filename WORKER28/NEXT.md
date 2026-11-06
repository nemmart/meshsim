# Parked for WORKER28 (send at its next report; remove once sent)

Put 113 straight into tasks/; push the branch and 113 before anything else this turn.
The owner has set P1's architecture (your brief, updated: read its new sections): each cycle deliver, run, merge, with units
grouped into tasks by cost and run in parallel, one barrier per cycle, built on per-tile timing wheels; GO and READY at 8 cycles, applied first as its own commit with the baselines re-recorded; and stage
2's partitioning measured by tile against by unit type (for instance 4 threads for the 67 RV32 cores, about 17 each). Your early
numbers point the same way: the gain isn't only skipping idle tiles (your ~2×) but skipping the idle units inside the active
one. C&C 3's tile costs 11-25 µs a cycle, while T1 does the same Tracer work at about 0.4 µs, so have the profile break C&C 3's
tile down by unit, and measure the per-cycle cost of polling empty buses and the interpreter's share. Then write NOTES.md: the
wake sources, the audit of same-cycle reads between units (canPut, ready, cross-unit peeks), and plans for the latency change, stage 1 (the wheels, one thread) and stage 2 (the threads, by tile or by type).
