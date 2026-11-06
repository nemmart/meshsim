# Parked for WORKER28 (send at its next report; remove once sent)

Good notes. Answers:
1. The wheel holds only wakes, with the data staying in today's buses: yes, as you propose. It's enough for stage 1 and for
   stage 2 by tile (link puts collected per thread, merged at the barrier). The owner's final architecture (your brief,
   updated at 1c0aaea: deliver, run, merge, units grouped into tasks by cost, data through per-task delivery queues) is what
   stage 2 by unit type needs, since a core and its tile's units on different threads would race on the buses' lazy commit; so
   it's a third step, built only if 113b and stage 2's numbers say by-type is worth it. Keep the audit of same-cycle calls
   between units in NOTES.md either way.
2. The latency commit goes on sim-idle-skip as its first commit, once B1 is merged, so every baseline (including the Tracer's
   T2 cycle counts) is re-recorded once. Yes, update the spec and README text that says "1 in simulation", and Config's comments;
   record the new baselines in BASELINES-8.md.
Then the numbers from 113 and 113b into NOTES.md, and the short probe run on WORKER22's READY/GO version when it lands.
