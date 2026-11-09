# Worker 14 (sim/, M3): the Tracer

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`), with v1.3's Java simulator
(`sim/ref/`) as the exactness oracle. **M1** built the framework and the RV32 controller; **M2** ran the full-size sort
exactly as `ref`, one chip split over threads, 20.9× faster (`sim/planning/PLAN-M2.md`). **M3's target (owner, D32): get
v1.3 running as it is, the Tracer and the replayers**: a segment on the full chip as `ref`'s `TracerT2` runs it
(`sim/planning/PLAN-M3.md`).
**You build the Tracer** on the framework: C&C 3's peripheral, the RV64 row machine that executes the guest as the rows of
Jolt's expanded trace and writes each window's run log, with its caches and fill bus to the memory system. Beside you:
W13 (the reference side and the audit: your ports come from it) and W15 (the replayer, which shares the row formats and
tables with you).

READ FIRST: `sim/README.md`; `sim/planning/PLAN-M3.md`; `sim/planning/DECISIONS.md` (D6, D17, D20-D31). **The framework:**
`sim/src/sim.h`, `config.h`, `PORTS.md` and `sim/planning/sessions/WORKER3-REPORT.md`; **how a unit is built and checked
here:** `WORKER9-REPORT.md` (RouterConf: a ref-side conformance driver, seeded scripts, fixtures in `make check`),
`WORKER11-REPORT.md` (MemConf), `WORKER8-REPORT.md` (plugging into the fingerprint). **The oracle:**
`sim/ref/meshsim/src/Tracer.java`, `RowCore.java`, `Rvc.java`, `MemorySystem.java`, `BankedMemory.java`, the parts of
`ReplayerAlu.java` it shares, and its suites `test/TracerTest.java`, `TracerRig.java`, `TracerT1.java`; `sw/tracer/`.

THE WORK
1. **The Tracer's machine on its own**: the row core, RVC, the decode table, ROM and controller-fed templates, the two
   128-line caches, the write-back buffer, the fill bus, the code and log FIFOs, the output path (set 2, packers, output
   queues and credits), ERRORS, and 4.21.9's timing, as `Tracer.java` models them. Unit state laid out for the cycle loop
   (D6).
2. **Checked against `ref` on its own, before any wiring**: port `TracerTest`'s cases, and write a conformance driver
   (`sim/tests/ref/TracerConf.java`, as RouterConf): `ref`'s Tracer driven by a seeded or recorded stimulus through its
   interface, the C++ one by the same, compared every cycle (outputs, run-log words, the fingerprint's streams). Then the
   T1 path (the Tracer with an ideal controller over a segment), if W13 confirms it's a useful reference.
3. **Wiring into the chip**, once W13 publishes the Tracer's ports in `AUDIT-M3.md`: C&C 3's peripheral slot, the memory
   system, the DMA.

SHARED CODE WITH W15. The replayer shares formats and tables with the Tracer (the record, the region map, the decode
table, templates, the expansion rules: `RowCore`, `ReplayerAlu`). Agree with W15 in one round which shared pieces live
in one file and who owns it; the other uses it read-only and asks for changes. Disagreements to the integrator.

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
- Branch `sim-w14`, in `sim/src/units/` (your own file names) and `sim/tests/`. The framework (`sim/src/*.h|cpp` outside
  `units/`) has no active owner: if the API needs a change, write it up and tell the integrator.

DELIVERABLES: the Tracer as framework units, equal to `ref` every cycle on its suites and TracerConf (fixtures in
`make check`); wired into the chip from the audit; `WORKER14/REPORT.md`.
