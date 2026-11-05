# Parked for WORKER26 (send when it next reports; remove once sent)

**If it already had the reply to its plan** (with 128 start images), send only this change:

> Change to the scope addition (owner): not 128 start images. WORKER22 runs the segments in order on one memory, so build (1)
> the initial image: gold's cycle-0 image as a page list (4 KB content per page), plus word 66's values, standing in for frame
> 7; (2) per segment, the list of pages it writes (from its update list): the harness creates them, if absent, and marks them
> owned at the segment's start; (3) a check that applying segments 0 to k−1's updates to the initial image gives gold's memory
> at segment k's first row, every segment of both workloads at 2²⁹. Use WORKER22's format in WORKER22/NOTES.md, or agree
> changes with it. Outputs at 2²⁹ go to the runner's persistent data.

**Otherwise, the full reply:**

Go ahead, with these rulings (owner, 2026-11-05; DECISIONS-v1.3 section 18, 332a518):

**Access:** jolt-asic-planning is under Quarky93, not nemmart: `https://github.com/Quarky93/jolt-asic-planning.git`, with the
meshsim token from your preferences (it works; the integrator merged with it today). Branch `tracer-standalone-main`.

**One table per proof, built whole on the host** (replacing section 18's static table plus supplement): your static analysis
**plus the run's own targets** (from gold's row stream, as you plan), merged by the host into a single table (about 70K
entries), with k, p1 and p3 chosen for that table. **No supplement inserted on chip, so nobody inserts**: frame 6 carries the
whole table (its lines and its three constants). Not the run's targets alone: that would understate the cache footprint. So
the hash constants travel with the table in frame 6 (your open question): not frame 7, not a program sidecar. Keep the
static-only and run-only counts as measurements.

**Scope addition: the initial image and the pages each segment writes** (owner, 2026-11-05; DECISIONS-v1.3, B1's acceptance;
this replaces your PROMPT.md item 3b's 128 start images): WORKER22 runs the segments in order on one memory, so you build
- **the initial image**: gold's cycle-0 image (the ELF's sections and the inputs), as a page list (4 KB content per page), plus
  word 66's values (log₂ K, `lowest_address`), standing in for frame 7 until gold writes it;
- **per segment, the list of pages it writes** (from its update list, frames 4 or 5): the harness creates them, if absent, and
  marks them owned at the segment's start, every other page not owned;
- **a check:** applying segments 0 to k−1's updates to the initial image gives gold's memory at segment k's first row, for
  every segment of both workloads at 2²⁹ (so the updates WORKER22 checks against are right).
WORKER22's format is in `WORKER22/NOTES.md` (per run, the map's lines plus a JSON with k, p1, p3 and word 66's values; pages as
offset, owned flag and 4 KB content, absent pages holes): use it, or agree changes with WORKER22.

**Where outputs live:** the small guests' tables and images on your branch under `wip/jumpmap/out/`, as test fixtures; **the
2²⁹ tables and start images in the runner's persistent data** (`~questrunner/data`), where WORKER22's tasks read the
deliveries, with their checksums in your report.

Everything else in your plan as proposed, including the acceptance (every jump of both runs through the table with the
Tracer's exact probe: zero map errors, every base equal to gold's next instr_idx).
