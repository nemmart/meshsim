# Worker 20 (sim/, v1.35, P2): the machine harness, the checker and the outputs

**The project.** meshsim is the simulator for a parallel compute mesh (part of an ASIC for proving the Jolt zkVM).
**meshsim v1.35 is its C++ implementation in `sim/`** (D37), exact against v1.3's Java (kept as the oracle in
`sim/ref/`) and much faster: the controller's programs, the full-size sort (20.9×), and v1.3's segment test T2 on all 128
segments of both workloads (6.5×). **v1.35 is being frozen** (D38-D41, `sim/planning/PLAN-V135.md`): **the simulator
and everything that runs it off Java, every capability of v1.3's Java meshsim with an equivalent, and v1.35 a
stand-alone directory with all the source needed to build and run everything**; the offline input tools (`gold`,
jumpmap, KeyGen, SplitSets, ConvertDelivery, SyntheticDelivery) stay Java in v1.35's own `pipeline/` folder. W18's
inventory (`sim/planning/V135-INVENTORY.md`) is the porting plan: rows by capability, pieces P1-P8, waves.

**You are P2** (inventory rows C1 with B18, B19, B20, H1, H2, H3, H6, B25, C0's runner, C2 as the first suite): the foundation wave 2 stands on. Every Java-driven suite of v1.3 (NetTest, MemTest, EngineTest, CtrlTest, RecipeTest and the rest) drives `ref`'s machine through a Java harness; you build the C++ harness they will port onto, the per-cycle invariant checker, the outputs users read, and the suite runner. P4-P6 and P7b start when your harness and checker land, so they come first. W19 (P1) and W21 (P7a) work beside you.

READ FIRST: the top-level `README.md`, `sim/README.md`, `sim/planning/PLAN-V135.md`, **`sim/planning/V135-INVENTORY.md`
in full** (your piece's rows in detail), `sim/planning/DECISIONS.md` (D6, D21, D30, D37-D41), and
`sim/planning/sessions/WORKER18-REPORT.md`. How this codebase builds and checks things: `sim/planning/sessions/README.md`
(every worker's report), especially the ones your rows point to.
Also: `sim/ref/meshsim/run_tests.sh`, `test/Mach.java` and the harness classes your rows name, `src/Chip.java`'s checks,
`PhaseLog.java`, `Config.java`, and the framework (`sim/src/sim.h`, `config.h`, `PORTS.md`).

THE WORK, in this order (the harness and the checker first: wave 2 waits on them)
1. **The machine harness** (C1, B18): what `ref`'s Mach gives a Java suite (build a chip or a tile, load programs, poke
   and peek, step, run until, stop, read registers and memories), in C++ on the framework, general enough for every
   Java-driven suite. Publish its header early so P4-P6's workers can plan against it.
2. **The per-cycle invariant checker** (B19, B20): every invariant `ref`'s checker enforces, with `ref`'s messages, as
   debug-mode checks on the framework (no cost in release, D6/D24); checked by provoking each violation and comparing the
   message with `ref`'s.
3. **Reset and `Config`** (B25; D41 Q4): reset as `ref` has it; `Config`'s fields that some suite, kit, sweep or
   documented option uses, the rest listed as dropped.
4. **The outputs** (H1, H2, H3, H6): PhaseLog's CSVs byte-identical on AccountingTest's runs, and the other outputs your
   rows name.
5. **The suite runner** (C0): `run_tests.sh`'s equivalent for v1.35 (quick and full), and **C2 as its first suite**
   (BusTest log-equal), so wave 2's workers add their suites to it.

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
- Branch `sim-w20`, in `sim/tests/` (the harness, the suites) and the framework only for the checker's hooks (the
  framework has no active owner: additions are fine, changes to existing operations come to the integrator first).
  Work on `ref`'s data paths until W19's P1 lands, then merge main.

DELIVERABLES: the harness (header early), the checker, reset and `Config`, the outputs, the runner with BusTest; each
checked against `ref`; `WORKER20/REPORT.md`.
