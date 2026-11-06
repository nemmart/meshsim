# Worker 28: full-detail simulation at full scale (P1): exact idle-skipping in meshsim

meshsim steps every unit every cycle. On the full chip (T2), a segment is about 21 M cycles, of which most of the 64 tiles
spend most in WFI with every queue empty, so T2 costs about 20 minutes per segment and can only run a sample. Your job: make
meshsim skip idle units exactly, so T2 runs every segment of both workloads at 2²⁹ at full detail, as part of v1.3's acceptance.

READ FIRST: wip/meshsim/README.md (the model's conventions: timed buses, a put at t seen at t + 1, the shuffle seeds),
Chip.java's step loop, and every unit's step(); WORKER22's branch v1.3-tracer and WORKER22/NOTES.md (TracerT2, the Tracer, the
memory system); wip/planning/sessions/WORKER20-REPORT.md and WORKER25-REPORT.md (the replayer's acceptance).

THE MECHANISM (owner, 2026-11-05): an "active" flag per unit. It's set by anything that can change the unit's state: a put on
any of its input buses, a chip-wide line (GO, RUN, the error line), or its timer coming due. It's cleared by the unit itself
after a step when nothing is in progress (queues and FIFOs empty, engines idle, core halted or in WFI with no wake pending).
The chip steps only active units, and a skipped unit's counters (mcycle, WFI and stall counts) are caught up when it wakes, so
every log stays identical.
**Hierarchical (owner, 2026-11-05): wakes at the tile level too.** A whole tile sleeps when every unit in it is idle and its core is
in a sleep state (WFI or halted); then the chip doesn't step the tile at all. It wakes on anything crossing its boundary: a word
on any of its link inputs from a neighbour, its controller link, a chip-wide line (GO, READY, RUN, the error line), or its
timer coming due. Inside an awake tile, the per-unit flags skip its idle units. The same idea one level up if the profile
says so: a C&C unit, or the chip outside the busy units.

THE WORK
1. Profile a T2 segment and a Stage B run: where the cycles go, by unit and state, to confirm idle units dominate and estimate
   the gain.
2. List every unit's wake sources and pending-work conditions, in WORKER28/NOTES.md, before any code.
3. Implement it on a branch, sim-idle-skip, made from v1.3-tracer once B1 is merged into main (until then, profile and plan
   only), with a flag to turn skipping off.
4. Prove it exact: identical logs, cycle for cycle, with skipping on and off, for the quick and full suites, the full-size sort
   (1,229,008 cycles), Stage B on its windows, and T2 segments of both workloads; and under shuffle seeds.
5. Then the acceptance: T2 on all 128 segments of both workloads at 2²⁹, raw and LRU-coded, on the Quest runner, each task
   well under 45 minutes, with a fail-fast on hung tiles.

RULES
- Propose a plan first and wait for the owner's go-ahead.
- Exactness is the requirement: any difference with skipping on is a bug, never a tolerance.
- Claim Quest numbers in NUMBERS.md first; long runs go to the runner; background jobs don't survive a turn.
- Keep the last tool calls of every turn for committing and pushing, and end each turn with a clean git status.

DELIVERABLES: the branch, the profile, the proof of exactness, T2 on every segment of both workloads, WORKER28/REPORT.md.
