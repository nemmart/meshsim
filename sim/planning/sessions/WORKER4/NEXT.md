# Parked for WORKER4 (from the integrator, 2026-11-07): send at its next stopping point

One more item for your re-merge of main: W3 fixed a detector gap (now on main, 9e126a1). When the machine goes idle, the
jump stops at the earliest missed-wake suspect's deadline (debug builds only; credit-blocked suspects don't cap it), so a
suspect with a threshold above 1 is still judged. Form 2's idle jump (decided by every thread after the barrier) needs the
same cap. Compute the earliest suspect deadline across all domains and include it in the shared status words, so every
thread caps the jump at the same cycle. Add a cross-domain version of W3's test: a consumer across domains with a
threshold of 5 misses a wake just before the machine goes idle, and is still reported.
