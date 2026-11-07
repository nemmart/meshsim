# Worker 12 (sim/, M2, group G4): the engines, and the sort end to end

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`), with v1.3's Java simulator
(`sim/ref/`) as the exactness oracle. **Milestone 2's target (owner, D25): the full-size sort, exact against `ref`, one
chip split over threads, in about 4 s against `ref`'s ~96 s** (`sim/planning/PLAN-M2.md`). WORKER7's audit
(`sim/tests/AUDIT-M2.md`) splits the units into groups. **G1 (WORKER8) and G2 (WORKER9) are done**: the 64-tile chip
matches `golden-m2` every cycle on `wfi` and on `regs` through 824. W11 is building G3, the memory path. **You are G4,
the engines** (the address generator, the GATHER engine, the director's modes 2-4), and you take **the sort end to end**:
at 16K, then full size, exact, on one thread and split. Beside you: W11 (G3) and W10 (framework speed).

READ FIRST: `sim/planning/PLAN-M2.md`, `sim/planning/DECISIONS.md` (D5, D12, D17, D20-D29), **`sim/tests/AUDIT-M2.md`**
(sections 0, 1, 2, **3.3 (modes 2-4), 3.6**, 4, 5, 6), `sim/tests/golden-m2/` (the sort's 16K references; full size on
the runner), `sim/planning/sessions/WORKER7-REPORT.md` (the sort's coverage: the AG's 2.1 M hold refusals; the
baseline), and the reports of the workers whose code you extend: `WORKER8-REPORT.md` (the director's switch slot for
modes 2-4, **the configuration parts of the AG and gather engine** in your future units' files, the fingerprint library)
and `WORKER9-REPORT.md` (**the chip's driver: how a program is booted, released (`Sim::wake`), and run; how far its
`M2Sort` sequence goes**, the partition). **The framework:** `sim/src/sim.h`, `config.h`, `PORTS.md`,
`WORKER3-REPORT.md`, and `WORKER10`'s notes on `sim-w10` (stop request, placement by load). **The oracle:**
`AddressGenerator.java`, `GatherEngine.java`, `TrafficDirector.java` (modes 2-4) in `sim/ref/meshsim/src/`,
`sim/ref/meshsim/test/SortTest.java`, `sim/ref/sort/`, and W7's `sim/tests/ref/M2Sort.java`.

THE WORK
1. **First, agree the ports with W11** (your engines read through its read queue and write through its write engine;
   AUDIT 3.2, 3.5, 3.6): declarations-only headers from each of you, early. Then plan in detail while W11 builds the
   read queue.
2. **The address generator** (3.6: group and stream runs, its Hold refused and retried under load, exact), **the GATHER
   engine**, and **the director's modes 2-4** (in W8's switch slot). Extend the files W8 started; never replace them.
3. **The sort's driver end to end**: from W9's driver (boot, release by `Sim::wake`, the stop request), the sort's
   sequence as `M2Sort` runs it, the input-load stand-in, verification.
4. **Checks:** with W11, `a2a`; then **the sort's L1** (AG, gather, HIST and buckets at once); then **the sort at 16K**
   against `golden-m2` (fingerprints at intervals, then the cycle count and the verified result); then **full size**
   against the runner's references (cycle count 1,229,687, result, fingerprints). All on one thread and split, under the
   debug options at 16K.
5. **The first timing** of the full sort (setup excluded, as W7's baseline), one thread and split over threads, on the
   runner with the machine recorded: the number M2 is judged by. W10 takes it from there.

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
- Branch `sim-w12`, in `sim/src/units/` and `sim/tests/`. The framework (`sim/src/*.h|cpp` outside `units/`) is W10's:
  if the API needs a change, write it up and tell the integrator.

DELIVERABLES: the AG, gather and modes 2-4, each checked against `ref` every cycle; the sort's driver; with W11, `a2a`;
the sort at 16K and full size, exact, one thread and split; the first timing; `WORKER12/REPORT.md`.
