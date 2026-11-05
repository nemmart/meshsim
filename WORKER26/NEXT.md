# Parked for WORKER26 (send with the reply to its plan; remove once sent)

**Scope addition (owner, 2026-11-05):** besides the jump map, you now build **each segment's start image** for WORKER22, which
had it in its plan (your PROMPT.md, item 3b):
- the image: gold's cycle-0 image (the ELF's sections and the inputs) plus segments 0 to k−1's updates (frames 4 or 5), applied
  in order;
- ownership: the pages segment k itself writes (its own update list) are owned, every other page shared and not owned;
- **checked against gold's memory at segment k's first row**, for every segment of both workloads at 2²⁹;
- word 66's values (log₂ K, `lowest_address`) and the hash constants, standing in for frame 7 until gold writes it.
Hand them over as, per segment, a list of pages (4 KB content and owned flag) with word 66's values; WORKER22's `FileStore`
lays them out as section 20 trees. Agree the format with WORKER22 (`WORKER22/NOTES.md` or `PLAN.md`). Fold this into your plan.
