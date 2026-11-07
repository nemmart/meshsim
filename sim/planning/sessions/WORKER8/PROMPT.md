# Worker 8 (sim/, M2, group G1): the tile's control, and the state fingerprint on the C++ side

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`), with v1.3's Java simulator
(`sim/ref/`) as the exactness oracle. Milestone 1 is closed: the framework, and one RV32 controller matching `ref`
instruction for instruction. **Milestone 2's target (owner, D25): the full-size sort, exact against `ref`, one chip split
over threads, in about 4 s against `ref`'s ~96 s** (`sim/planning/PLAN-M2.md`). WORKER7's audit (`sim/tests/AUDIT-M2.md`)
splits the units into groups G1-G4. **You are G1, the tile's control**, and you also own **the C++ side of the canonical
state fingerprint**, which every group checks against. Beside you: W9 (G2, the network and the chip's assembly) and W10
(framework speed). G3 (the memory path) starts once your register block exists.

READ FIRST: `sim/planning/PLAN-M2.md`, `sim/planning/DECISIONS.md` (D5, D12, D17, D20-D26), **`sim/tests/AUDIT-M2.md` in
full** (sections 1.2, 2, 3.3, 3.7 and 6 are yours; 5 gives the order), `sim/tests/golden-m2/` (README, `RUNS`, `HASHES`,
`regen.sh`) and `sim/planning/sessions/WORKER7-REPORT.md`. **What you extend:** W5's controller (`sim/src/units/`,
`sim/planning/sessions/WORKER5-REPORT.md`, `sim/tests/AUDIT-M1.md`). **The framework:** `sim/src/sim.h`, `config.h`,
`PORTS.md`, and WORKER3's and WORKER4's reports (`sessions/WORKER3-REPORT.md`, `WORKER4-REPORT.md`). **The oracle:**
`TileControl.java`, `Controller.java`, `TrafficDirector.java`, `ControllerMemories.java`, `Registers.java` in
`sim/ref/meshsim/src/`, and W7's `sim/tests/ref/StateFp.java` (the fingerprint's `ref` side).

THE WORK
1. **The fingerprint, C++ side** (AUDIT-M2 section 6): a small library that writes the canonical fingerprint as `StateFp`
   does (the same streams of 64-bit words per unit, the same hash, memories as order-free sums kept incrementally), reads
   a golden `.fp` file and checks its D12 header against the run's configuration (refusing on any difference, naming it),
   and reports **the first differing cycle, unit and named word**. Each unit contributes its own part through one small
   interface, so W9's and later groups' units plug in. Test it on what exists (W5's controller, its memories) before your
   new units.
2. **The register block in full** for the sort and `regs`/`wfi` (section 3.7, section 1.2 A and B): write counters,
   events, DMA, the send path (the controller's sends through the register block to the injection point: W9's side), done
   pulses, the table-window decode; still the shifted end of cycle (PORTS.md). M1's "not modelled in M1" fatals become
   real behaviour where M2 needs them; what M2 still doesn't need stays fatal ("not modelled in M2").
3. **The traffic director and the controller memories' port B** (section 3.3): modes 0 and 1 (commands, replies,
   read-port results, writes), the counters and the K-line; the director's `pendingAdd` modelled order-free as section 3.3
   says. Modes 2-4 come with G4: leave them a clean place to go.
4. **Checks:** M1's 67 goldens still pass (the controller must not regress); then, with W9's router, **`regs` and `wfi`
   with every unit of tile 0 checked every cycle** against `golden-m2`, under every debug option.

COORDINATION WITH W9. W9 owns the chip's assembly (the tile builder, the 64-tile chip, C&C's model, the READY/GO lines)
and the network. Your units meet W9's at the send holding register, the eject path into the director, the READY/GO lines
into the register block, and port B. **First, agree the ports between your units and W9's** (kinds, widths, latencies,
from AUDIT-M2 section 3): each of you publishes your units' headers early (declarations only), and any disagreement comes
to the integrator with the AUDIT's evidence. Don't edit each other's files.

RULES
- **Propose your plan first** in your `NOTES.md` and wait for the go-ahead before writing code.
- Work on your own branch; merge `main` in often (never rebase); the integrator merges into `main`. Keep notes, questions
  and report in your session folder.
- `sim/ref` is frozen (D5). Exactness is the requirement: a difference from a reference is a bug, never a tolerance; if
  you believe a reference is wrong, stop and report it with the evidence.
- The cycle-loop rules (D6, D24): no STL, no allocation, no virtual calls in any `step` or engine path, diagnostics
  included; untyped 64-bit words; three-word link words (D17). Sanitizers and deterministic tests from the first commit;
  `make check` clean on gcc and clang (run it build by build if the tool limit bites). Every new unit passes the debug
  checks: shuffle, both orders, step-all, the missed-wake detector.
- Long runs and all timing go to the runner. **Quest numbers:** pull Quest and re-read `NUMBERS.md` immediately before
  claiming, push the claim before writing the task. Tasks carry embedded kits. **No machine-specific values** (D21): CPU
  lists from `sim/tools/machine.sh`, every result with its machine. Background jobs don't survive a turn.
- Never write a token into a file. Never `pkill -f`; use `pgrep` and kill by PID.
- Commit and push after each completed piece, keep the last tool calls for committing, end each turn with a clean
  `git status`.
- Keep replies to the owner short.
- Branch `sim-w8`, in `sim/src/units/` (your units; W9 also works there: your own file names) and `sim/tests/` (the
  fingerprint library, your tests). The framework (`sim/src/*.h|cpp` outside `units/`) is W10's: if the API needs a
  change, write it up and tell the integrator.

DELIVERABLES: the fingerprint library with its tests; the register block, director and port B; M1's 67 goldens still
matching; `regs` and `wfi` matching `golden-m2` every cycle (with W9); `WORKER8/REPORT.md`.
