# Parked for WORKER26 (send now; remove once sent)

Go ahead, with these rulings (owner, 2026-11-05; DECISIONS-v1.3 section 18, 332a518):

**Access:** jolt-asic-planning is under Quarky93, not nemmart: `https://github.com/Quarky93/jolt-asic-planning.git`, with the
meshsim token from your preferences (it works; the integrator merged with it today). Branch `tracer-standalone-main`.

**One table per proof, built whole on the host** (replacing section 18's static table plus supplement): your static analysis
**plus the run's own targets** (from gold's row stream, as you plan), merged by the host into a single table (about 70K
entries), with k, p1 and p3 chosen for that table. **No supplement inserted on chip, so nobody inserts**: frame 6 carries the
whole table (its lines and its three constants). Not the run's targets alone: that would understate the cache footprint. So
the hash constants travel with the table in frame 6 (your open question): not frame 7, not a program sidecar. Keep the
static-only and run-only counts as measurements.

**Scope addition: each segment's start image** (your PROMPT.md, item 3b): gold's cycle-0 image (the ELF's sections and the
inputs) plus segments 0 to k−1's updates (frames 4 or 5), applied in order; owned = the pages segment k itself writes (its own
update list), every other page shared and not owned; **checked against gold's memory at segment k's first row**, every segment
of both workloads at 2²⁹; plus word 66's values (log₂ K, `lowest_address`), standing in for frame 7 until gold writes it.
Hand over per segment a list of pages (4 KB content and owned flag) with word 66's values; WORKER22's `FileStore` lays them out
as section 20 trees. WORKER22 will propose the formats for the map and the images: agree them with it.

**Where outputs live:** the small guests' tables and images on your branch under `wip/jumpmap/out/`, as test fixtures; **the
2²⁹ tables and start images in the runner's persistent data** (`~questrunner/data`), where WORKER22's tasks read the
deliveries, with their checksums in your report.

Everything else in your plan as proposed, including the acceptance (every jump of both runs through the table with the
Tracer's exact probe: zero map errors, every base equal to gold's next instr_idx).
