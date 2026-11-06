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

**The architecture (owner, 2026-11-05): one event queue per tile, then threads over tiles.** Each tile (and each C&C unit) keeps
a **timing wheel**: a ring of buckets indexed by cycle, holding the data in flight to its units with arrival times, and its
units' scheduled wakes. Every latency is small and bounded (a link 3 cycles, a fill 80), so insertion and removal are O(1) and a
cycle's entries sit together in memory. Each cycle a tile steps only the units its wheel names, plus units with work in
progress, which schedule their own next wake: an idle unit, or a whole sleeping tile, is never visited, and nothing walks the
object graph polling empty buses. A put at t onto a bus of latency L becomes an entry at t + L, the same semantics as now.
**Then parallelise over tiles:** threads own sets of tiles and each steps its own tiles' wheels for cycle t; everyone ticks off
the cycle at a barrier before t + 1. A put to another tile lands in that tile's wheel at t + L ≥ t + 1, through per-thread
outboxes merged at the barrier, so there are no locks in the cycle. Chip-wide work (the lines, the invariant checks, the
request bus) stays serial at the barrier. Links' 3-cycle latency may later let threads sync less often than every cycle.
The active flags above are the simplest form of the same idea; the wheel is where the wakes live.
**The architecture, final form (owner, 2026-11-05): deliver, run, merge; tasks grouped by cost.** Each cycle t: (1) **deliver**:
all data due at t is handed to its units' inputs, `deliverData(bus, value)`, from the timing wheel; (2) **run**: each unit with
something delivered or work pending runs `runCycle(out)`, reading only what was delivered and its own state, writing its outputs
into its task's own `DeliveryQueue`, each at arrival t + L ≥ t + 1; (3) **merge**: the tasks' queues go into the wheel. Units are
grouped into **tasks by cost**, e.g. 16 RV32 cores in a task, 64 routers in another, so a thread pool gets even pieces and each
kind's code stays hot; tasks run in parallel, one barrier per cycle. No races by construction, and idle-skipping falls out (an
idle unit is in no task this cycle). **The audit that comes first (step 2):** every direct read between units in the same cycle
(`canPut`, `ready`, status registers, any `peek` across units; "a FIFO is full by its count before the cycle's pop") must become
data delivered in phase 1 (a credit, an occupancy snapshot) or a read of last cycle's committed state. This supersedes the
per-tile wheels and threads above as the target; they remain its ingredients. **Performance is the question** (owner): measure
each stage's gain, and report where it gets to.

**Two more decisions (owner, 2026-11-05).** (1) **GO and READY at 8 cycles** (`go_latency`, `ready_latency`; the error line
follows READY), as the spec's own estimates assume (4.13), instead of meshsim's 1, which isn't physical: applied **first, as
its own commit**, with every baseline it moves (the sort's 1,229,008, recorded cycle counts) re-recorded and documented, so the
exactness proofs compare like with like. With every cross-tile coupling then at 3 cycles or more, threads could sync every 3
cycles; start with a barrier every cycle and adaptive one-or-many threads, and use the lookahead only if the barrier limits.
(2) **Partitioning by unit type, to measure against by tile:** for instance **4 threads running the RV32 cores** (the 67 cores,
about 17 each, keeping the interpreter's code hot in their caches) and the other threads the tiles' units (routers, SRAM,
engines, replayers, the Tracer); a core's couplings to its tile are 1 cycle, safe with a per-cycle barrier. The profile decides
stage 2's partitioning.

THE WORK
1. Profile a T2 segment and a Stage B run: where the cycles go, by unit and state, to confirm idle units dominate and estimate
   the gain.
2. List every unit's wake sources and pending-work conditions, and audit every same-cycle read between units, in
   WORKER28/NOTES.md, before any code.
3. Implement the per-tile wheels (stage 1, one thread), then the threads over tiles (stage 2), on a branch, sim-idle-skip, made from v1.3-tracer once B1 is merged into main (until then, profile and plan
   only), with flags to turn each stage off.
4. Prove it exact: identical logs, cycle for cycle, with each stage on and off, and at 1, 2, 8 and 16 threads, for the quick and full suites, the full-size sort
   (1,229,008 cycles), Stage B on its windows, and T2 segments of both workloads; and under shuffle seeds.
5. Then the acceptance: T2 on all 128 segments of both workloads at 2²⁹, raw and LRU-coded, on the Quest runner, each task
   well under 45 minutes, with a fail-fast on hung tiles.

RULES
- Propose a plan first and wait for the owner's go-ahead.
- Exactness is the requirement: any difference with skipping on is a bug, never a tolerance.
- Claim Quest numbers in NUMBERS.md first; long runs go to the runner; background jobs don't survive a turn.
- Keep the last tool calls of every turn for committing and pushing, and end each turn with a clean git status.

DELIVERABLES: the branch, the profile, the proof of exactness, T2 on every segment of both workloads, WORKER28/REPORT.md.
