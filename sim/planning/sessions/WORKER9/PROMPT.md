# Worker 9 (sim/, M2, group G2): the network, and the chip's assembly

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`), with v1.3's Java simulator
(`sim/ref/`) as the exactness oracle. Milestone 1 is closed: the framework, and one RV32 controller matching `ref`
instruction for instruction. **Milestone 2's target (owner, D25): the full-size sort, exact against `ref`, one chip split
over threads, in about 4 s against `ref`'s ~96 s** (`sim/planning/PLAN-M2.md`). WORKER7's audit (`sim/tests/AUDIT-M2.md`)
splits the units into groups G1-G4. **You are G2, the network**, and you own **the chip's assembly**: the tile builder,
the 64-tile chip, C&C's model and the chip-level lines, and the driver that runs `golden-m2`'s programs. Beside you: W8
(G1, the tile's control, and the fingerprint's C++ side) and W10 (framework speed). G3 and G4 follow.

READ FIRST: `sim/planning/PLAN-M2.md`, `sim/planning/DECISIONS.md` (D5, D12, D17, D21-D26; **D23**: chip-level work is
event-driven), **`sim/tests/AUDIT-M2.md` in full** (sections 0, 1.2, 2, 3.1, 3.2's send side, 4 and 5 are yours),
`sim/tests/golden-m2/` (README, `RUNS`, `HASHES`, `regen.sh`), `sim/planning/sessions/WORKER7-REPORT.md`. **The
framework:** `sim/src/sim.h`, `config.h`, `PORTS.md`, and WORKER3's and WORKER4's reports (`sessions/WORKER3-REPORT.md`:
links, levels, wakes; `WORKER4-REPORT.md`: form 2, partitions, boundary units: the chip must split over threads).
**Units already built:** W5's controller (`sim/src/units/`). **The oracle:** `Chip.java`, `Tile.java`, `Router.java`,
`InjectionPath.java` (its send side), `CnC.java`, `Hops.java`, `MeshHeader.java`, `LinkWord.java` in `sim/ref/meshsim/src/`,
and W7's drivers `sim/tests/ref/M2Sort.java`, `M2Prog.java`, `FpHooks.java` (how each program is set up and booted).

THE WORK
1. **The router and the links** (section 3.1): the mesh's links (latency, three-word link words, D17), the router's
   queues and arbitration exactly as `ref`, its C&C input and outputs; tiles 0-2's special routers.
2. **The injection point's send side and the eject path** (section 3.2's send side; the read side is G3's, in the same
   unit: design it so G3 adds the read side without restructuring), the eject path into W8's director.
3. **C&C's model and the chip-level lines** (section 0 and section 4): `ref`'s Java `CnC` sequencer as the sort uses it
   (the boot's broadcasts, commands, READY/GO), and chip-level work **event-driven** (D23): nothing scans all tiles each
   cycle. `RequestBus.runCycle`'s per-cycle scan is section 4's first case: the sort sends no request, so it must cost
   nothing.
4. **The chip's assembly**: a tile builder (a function adding a tile's units and returning its external ports, as W3's
   `sketch/tile_builder.cpp`), the 64-tile chip, and a **partition by tile groups** for form 2 (W4), every crossing a
   Link (so the barrier interval stays 3). The driver that boots and runs `golden-m2`'s programs (images, the harness's
   stand-ins: input load, verification) and compares fingerprints through W8's library.
5. **Checks:** the boot of any program (14,791 cycles of network and port-B traffic before any engine runs) matching
   `golden-m2` every cycle; `wfi`'s self-sends; then, with W8, **`regs` and `wfi` with every unit of tile 0 checked every
   cycle**; then NetTest and the all-to-all's network part. Every check also on form 2 over several threads.

COORDINATION WITH W8. W8 owns the tile's control (the register block, the director, port B) and the fingerprint
library. Your units meet W8's at the send holding register, the eject path into the director, the READY/GO lines into
the register block, and port B. **First, agree the ports between your units and W8's** (kinds, widths, latencies, from
AUDIT-M2 section 3): each of you publishes your units' headers early (declarations only), and any disagreement comes to
the integrator with the AUDIT's evidence. Don't edit each other's files.

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
- Branch `sim-w9`, in `sim/src/units/` (your units and the chip's assembly; W8 also works there: your own file names) and
  `sim/tests/` (the chip driver, your tests). The framework (`sim/src/*.h|cpp` outside `units/`) is W10's: if the API
  needs a change, write it up and tell the integrator.

DELIVERABLES: the router, links, injection send side and eject path; C&C's model and the chip-level lines, event-driven;
the tile builder, the 64-tile chip and its partition; the boot and `wfi` matching `golden-m2` every cycle, and `regs` and
`wfi` with W8; NetTest and the all-to-all's network part; all on one thread and on form 2; `WORKER9/REPORT.md`.
