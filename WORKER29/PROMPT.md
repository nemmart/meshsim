# Worker 29: a faster simulation engine for busy workloads (P2)

Simulation is a roadblock for the project (owner): we'll be doing much more of it, so the engine is worth investing in now.
meshsim simulates the chip cycle by cycle on one thread. WORKER28 (P1) is making it skip idle units and tiles, which helps runs
where most tiles wait (the Tracer's full-chip T2: 19× faster), but does nothing when every tile is busy, as in the sort, the
replay after GO, or any real use of the mesh. Your job: make a busy cycle much cheaper on one thread, staying exact. **Stay in
Java** (owner): the JVM is fast, and the gains are in the design (data in compact arrays, no allocation in a cycle, only units with
work touched, calls the JIT can inline), not the language.

READ FIRST: wip/meshsim/README.md (the conventions: timed buses, a put at t seen at t + L, the shuffle seeds), Chip.java's step
loop, Tile and its units, Bus.java, Buses.java and WordBuses.java (the buses commit lazily), and WORKER28/PROMPT.md and NOTES.md
(P1's architecture: timing wheels, then threads; deliver, run, merge as the target).

THE WORK
1. Profile the sort (the full-size p4/uniform-k128 on the runner, and a smaller configuration that runs locally) and a busy
   full-chip phase: where a busy cycle's time goes, by unit, bus, allocation, dispatch and data layout.
2. Prototype the biggest wins on a scratch branch, measured locally on a small sort: for instance cheaper buses and commits,
   fewer objects touched per cycle, no allocation in the cycle, data laid out by kind; and the deliver-run-merge structure if
   the profile justifies it.
3. Agree with WORKER28 who changes what: it owns scheduling (which units step; the threads), you own a step's cost (the units'
   and buses' internals). Then build on its branch sim-idle-skip once its exactness proof passes.
4. Prove each change exact: identical logs, cycle for cycle, for the quick and full suites, the full-size sort (1,229,008
   cycles; after WORKER28's latency commit, its new baseline), and a T2 segment, under shuffle seeds too.
5. Report the sort's wall time and µs per cycle before and after, single-threaded, and what's left for P1's threads.

RULES
- Propose a plan first and wait for the owner's go-ahead.
- Exactness is the requirement: any difference is a bug, never a tolerance.
- Long runs go to the runner (claim plain Quest numbers: PIPELINE.md); background jobs don't survive a turn.
- Commit and push before anything long in a turn, and keep the last tool calls for committing; end each turn with a clean git
  status.

DELIVERABLES: the profile, the measured prototypes, the changes on a branch with their proof, WORKER29/REPORT.md.
