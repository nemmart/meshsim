# Worker 21 (sim/, v1.35, P7a): the Tracer's rig and directed cases

**The project.** meshsim is the simulator for a parallel compute mesh (part of an ASIC for proving the Jolt zkVM).
**meshsim v1.35 is its C++ implementation in `sim/`** (D37), exact against v1.3's Java (kept as the oracle in
`sim/ref/`) and much faster: the controller's programs, the full-size sort (20.9×), and v1.3's segment test T2 on all 128
segments of both workloads (6.5×). **v1.35 is being frozen** (D38-D41, `sim/planning/PLAN-V135.md`): **the simulator
and everything that runs it off Java, every capability of v1.3's Java meshsim with an equivalent, and v1.35 a
stand-alone directory with all the source needed to build and run everything**; the offline input tools (`gold`,
jumpmap, KeyGen, SplitSets, ConvertDelivery, SyntheticDelivery) stay Java in v1.35's own `pipeline/` folder. W18's
inventory (`sim/planning/V135-INVENTORY.md`) is the porting plan: rows by capability, pieces P1-P8, waves.

**You are P7's first part** (inventory rows C21, C22, C23): v1.3's Tracer suites that the C++ doesn't run yet: TracerRig with its directed cases (TracerTest), MemorySystemTest and RvcTest, ported as C++ suites that run without Java or `ref`. W14 built the C++ Tracer and checked it by replaying `ref`'s recorded calls (TracerConf); these suites make the same checks stand-alone, in v1.35. The rest of P7 (Stage B, the acceptance kit) comes in wave 2. W19 (P1) and W20 (P2) work beside you.

READ FIRST: the top-level `README.md`, `sim/README.md`, `sim/planning/PLAN-V135.md`, **`sim/planning/V135-INVENTORY.md`
in full** (your piece's rows in detail), `sim/planning/DECISIONS.md` (D6, D21, D30, D37-D41), and
`sim/planning/sessions/WORKER18-REPORT.md`. How this codebase builds and checks things: `sim/planning/sessions/README.md`
(every worker's report), especially the ones your rows point to.
Also: `sim/planning/sessions/WORKER14-REPORT.md` (the Tracer, TracerParts, TracerConf, the generated TracerConfRig and
TracerConfCases), `sim/ref/meshsim/test/TracerRig.java`, `TracerTest.java`, `MemorySystemTest.java`, `RvcTest.java`.

THE WORK
1. **TracerRig in C++**: the ideal controller `ref`'s TracerTest drives the Tracer with, on the C++ Tracer, as a test
   harness in `sim/tests/` (W14's TracerConf generated it from `ref` by sed; this is a real port, no generation).
2. **TracerTest's directed cases**, every one, with `ref`'s assertions (180 of them, per W14), passing stand-alone.
3. **MemorySystemTest and RvcTest**: their cases and expected values, stand-alone (W14's TracerParts covered parts of
   them by recorded streams; these are the suites themselves).
4. **Check against `ref`** the way W14 did: every assertion passes, and a planted bug in each area fails; the recorded
   streams (TracerConf) stay as the oracle's deeper check in `sim/oracle/` (W19 moves them).
5. Add them to `make check`, and to W20's suite runner when it lands.

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
- Branch `sim-w21`, in `sim/tests/`. The Tracer's code is W14's (closed): if a suite exposes a bug in it, fix it,
  note it, and tell the integrator.

DELIVERABLES: TracerRig, TracerTest's cases, MemorySystemTest and RvcTest in C++, in `make check`, stand-alone;
`WORKER21/REPORT.md`.
