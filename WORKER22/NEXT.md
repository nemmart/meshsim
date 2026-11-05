# Parked for WORKER22 (send when it next reports; remove once sent)

The 2²⁹ acceptance plan (owner, 2026-11-05; DECISIONS-v1.3, after the work list, 9cf1b52): one initial image (from WORKER26),
the segments run in order on that same memory. At each segment's start, create the pages it writes (WORKER26's per-segment
list), as zero pages if absent, and mark them owned; every other page is not owned, so [8] still fires on a stray store. Caches
cold at each segment's start: the word-0 write clears the Tracer's, and flush the shared cache at the boundary. At each
boundary, check the Tracer's state against the next segment's frame 2, and the pages it wrote against that segment's update
list. Use T1 to carry memory through the segments T2 doesn't run. Fold it into PLAN.md.
