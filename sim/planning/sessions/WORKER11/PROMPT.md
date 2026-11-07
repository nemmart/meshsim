# Worker 11 (sim/, M2, group G3): the memory path

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`), with v1.3's Java simulator
(`sim/ref/`) as the exactness oracle. **Milestone 2's target (owner, D25): the full-size sort, exact against `ref`, one
chip split over threads, in about 4 s against `ref`'s ~96 s** (`sim/planning/PLAN-M2.md`). WORKER7's audit
(`sim/tests/AUDIT-M2.md`) splits the units into groups. **G1 (the tile's control, WORKER8) and G2 (the network and the
64-tile chip, WORKER9) are done**: the whole chip matches `golden-m2` every cycle on `wfi` and on `regs` through cycle
824, on one thread and split over threads. **You are G3, the memory path.** Beside you: W12 (G4, the address generator,
gather and the director's modes 2-4, built on your read queue) and W10 (framework speed).

READ FIRST: `sim/planning/PLAN-M2.md`, `sim/planning/DECISIONS.md` (D5, D12, D17, D20-D29: **D27** model `ref` as it
runs, **D28/D29** where it can't be), **`sim/tests/AUDIT-M2.md`** (sections 1, 2, **3.2, 3.4, 3.5**, the read port, 5, 6),
`sim/tests/golden-m2/`, and the reports of the workers whose code you extend:
`sim/planning/sessions/WORKER8-REPORT.md` (G1: the register block, the director, port B, **the configuration parts of
the write engine and table store it already built in your future units' files**, the fingerprint library and how units
plug in) and `WORKER9-REPORT.md` (G2: the chip's assembly and driver, **the injection unit's read-side slot
`InjReadSide` and the shared `Tokens`**, the partition). **The framework:** `sim/src/sim.h`, `config.h`, `PORTS.md`
(incl. 2b, the Link that holds words) and `WORKER3-REPORT.md`. **The oracle:** `BigSRAM.java`, `SRAMReadQueue.java`,
`InjectionPath.java` (read side), `WriteEngine.java`, `TableStore.java`, the read port's sources, in
`sim/ref/meshsim/src/`, and W7's `StateFp.java`.

THE WORK, in this order
1. **`regs`' path from cycle 825 first** (PLAN-M2): mode-1 self-sends through the write engine, PTR's read-modify-write,
   big SRAM writes. Then lift W8's named limit (`fp::kRegsCheckedThrough`) in the same commit, and **`regs` matches
   `golden-m2` to its end, every unit of tile 0 every cycle, and every tile every 10 cycles**.
2. **The rest of the memory path:** the big SRAM (3.4: its contents' fingerprint kept incrementally, as W8's rule says),
   the read queue and tokens with the injection unit's read side (3.2: one unit with W9's send side; fill `InjReadSide`,
   share `Tokens`; **the token free list's order is exact behaviour**, AUDIT 3.2), the write engine (3.5) in full, the
   table store and HIST, the read port. Extend the files W8 started ("configuration part by W8 (G1)"); never replace
   them. Overflows that feed ERRORS are events (D29).
3. **Checks:** each unit's fingerprint stream equal to `ref`'s every cycle; then, with W12's AG, `a2a`; then the sort's
   SPLIT (the read port, replies). Every check on one thread and split, under every debug option.

COORDINATION WITH W12. W12's address generator and gather engine read through your read queue and write through your
write engine. **First, agree the ports between your units and W12's** (kinds, widths, latencies, from AUDIT-M2 sections
3.2, 3.5, 3.6): each of you publishes declarations-only headers early; disagreements come to the integrator with the
AUDIT's evidence. Don't edit each other's files. W8's and W9's files you extend are yours now; note each change in your
NOTES.md.

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
- Branch `sim-w11`, in `sim/src/units/` and `sim/tests/`. The framework (`sim/src/*.h|cpp` outside `units/`) is W10's:
  if the API needs a change, write it up and tell the integrator.

DELIVERABLES: `regs` matching to its end; the memory path's units, each checked against `ref` every cycle; with W12,
`a2a` and the sort's SPLIT; `WORKER11/REPORT.md`.
