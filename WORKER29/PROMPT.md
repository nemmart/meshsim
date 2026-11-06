# Worker 29: a simulation framework that is very fast, parallel, and easy to build units in (P2)

**Why** (owner, 2026-11-06): simulation is a roadblock, and every version after v1.3 (v1.4, v1.5, v1.6 ...) gets bigger, with runs
of many replays (50 at a time). We need full-detail simulation to run blazingly fast, without making new units harder to write.
This doesn't have to land in v1.3; it's the foundation for what follows.

**Where meshsim is today.** A Java, cycle-level model: every unit stepped every cycle; timed buses that commit lazily; inside a
tile, units call each other directly in a fixed stepping order. WORKER28 (P1) has added exact idle-skipping (the full-chip T2 is
19× faster: a segment in 54 s instead of 1,030) and is planning threads over tiles. A busy chip (the sort, the replay after GO)
gains nothing from skipping: its cost is the step itself, and walking a large graph of small objects every cycle.

**The idea: the framework carries the rules, so a unit's author writes ordinary code.** Prior art to borrow from: Verilog's
sensitivity lists, Verilator's ordering of combinational logic, SystemC's delta cycles, gem5's ports and events, Sparta.
- **Ports, declared once**, of a few framework kinds: `Reg<T>` (read gives last cycle's value, a write is visible next cycle),
  `Wire<T>` (combinational: this cycle's value), `Fifo<T>` (with "full before this cycle's pop" built in), `Link<T>` (a latency).
  A unit's `step()` reads its inputs and writes its outputs as plain code: no commits, no stepping order, no reaching into another
  unit (it holds only ports).
- **The framework, from those declarations:** orders units by their combinational wires (a combinational loop rejected when the
  chip is built); double-buffers registers and FIFOs (no lazy-commit race); steps only units whose inputs changed or that have
  work pending (idle-skipping everywhere, for free); owns the ports' storage as compact arrays by kind (no allocation in a cycle);
  partitions across threads along registered ports and links (units joined by wires stay together); and has a debug mode that
  varies the order within a level to catch any undeclared dependence.
- **Migration is gradual:** first a whole tile as one unit with its links as ports, then inside it. The current simulator is the
  exactness oracle throughout: identical logs, cycle for cycle.

READ FIRST: wip/meshsim/README.md (the conventions: timed buses, a put at t seen at t + L, the shuffle seeds), Chip.java, Tile
and its units, Bus.java, Buses.java and WordBuses.java; WORKER28/PROMPT.md and NOTES.md on branch sim-idle-skip (its profile, its
audit of same-cycle calls between units, stage 1 as built).

THE WORK
1. **Profile** a busy chip (the sort: the full-size p4/uniform-k128 on the runner, a smaller configuration locally) and a T2
   segment with stage 1 on: where a cycle's time goes.
2. **Design** the framework: the port kinds and their exact semantics (including how today's same-cycle couplings map onto
   them: WORKER28's audit is the list), ordering, wakes, threads, data layout, the debug checks, and the migration path. In
   WORKER29/DESIGN.md, before code.
3. **Prototype** it on a scratch branch with two or three units (a FIFO, a router, a small core), and measure **speed per cycle**
   (single-threaded and threaded) and **authoring cost** (the code a unit needs, against today's).
4. **Recommend:** whether to migrate meshsim onto it, how, in what order, and the expected speed for a busy chip and for many
   replays at once.

RULES
- Propose a plan first and wait for the owner's go-ahead. The owner decides on migration after step 4.
- Stay in Java (owner: the JVM is fast; the gains are in the design).
- Exactness is the requirement for anything that replaces existing behaviour: any difference is a bug, never a tolerance.
- Coordinate with WORKER28, which owns P1 (idle-skipping and threads in today's engine); don't change the engine's main line.
- Long runs go to the runner (claim plain Quest numbers: PIPELINE.md); background jobs don't survive a turn.
- Commit and push before anything long in a turn, and keep the last tool calls for committing; end each turn with a clean git
  status.

DELIVERABLES: the profile, WORKER29/DESIGN.md, the prototype branch with its measurements, WORKER29/REPORT.md with the
recommendation.
