# Worker 13 (sim/, M3): the reference side: what a segment exercises, how the new units couple, what we check against

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`), with v1.3's Java simulator
(`sim/ref/`) as the exactness oracle. **M1** built the framework and the RV32 controller; **M2** ran the full-size sort
exactly as `ref`, one chip split over threads, 20.9× faster (`sim/planning/PLAN-M2.md`). **M3's target (owner, D32): get
v1.3 running as it is, the Tracer and the replayers**: a segment on the full chip as `ref`'s `TracerT2` runs it
(`sim/planning/PLAN-M3.md`).
You do for M3 what WORKER7 did for M2: establish what must be built, the precise behaviour each new piece must
reproduce, and the references to check it against. W14 (the Tracer) and W15 (the replayer) start beside you on their
machines' insides; their wiring into the chip, and the C&C cores, start from your audit.

READ FIRST: `sim/README.md`; `sim/planning/PLAN-M3.md`, `PLAN-M2.md` (the close-out, "Carried to M3");
`sim/planning/DECISIONS.md` (D5, D12, D21, D23, D26-D31). **M2's equivalents, your model:** `sim/tests/AUDIT-M2.md`,
`sim/tests/golden-m2/`, `sim/planning/sessions/WORKER7-REPORT.md`, and how the fingerprint plugs in
(`WORKER8-REPORT.md`; `sim/tests/ref/StateFp.java`, `FpHooks.java`). **The framework:** `sim/src/PORTS.md`, `sim.h`.
**The oracle:** `sim/ref/meshsim/test/TracerT2.java` (and `TracerT1`, `TracerRig`, `StageB`, `ReplayShard`,
`CcCoresTest`), `src/Tracer.java`, `RowCore.java`, `Rvc.java`, `MemorySystem.java`, `BankedMemory.java`,
`Replayer.java`, `ReplayerAlu.java`, `PeripheralSlot.java`, `Peripheral.java`, `CcCores.java`, `sw/cc/`, `sw/tracer/`,
`sw/replay/`, and `REPLAYER-NOTES.md`, `REPLAY-INPUT-REQUIREMENTS.md`. Also `wip/meshsim/test/ReplayBench.java` on branch
`sim-step-cost` (the D8/D15 bar's driver).

THE WORK
1. **What a segment exercises.** Run T2 in `ref` on a segment (locally if one fits, else on the runner) and establish
   every unit, bus, register, firmware path and chip-level mechanism it touches beyond M2's chip, and every harness
   stand-in (what T2 lands before the boot, C&C 1/2's roles). Say whether `ReplayBench`'s segment is the same workload, and
   propose the M3 run(s): which segment(s), how many windows, what's compared.
2. **`sim/tests/AUDIT-M3.md`**: for each new unit (the Tracer and its memory system, the replayer and the peripheral slot,
   the C&C cores, the DMA and request paths M2 never used), its state, its ports onto the framework's kinds, every
   same-cycle coupling with the cycle it takes effect, the order within `Chip`/`Tile` that matters (D27), chip-level work
   event-driven (D23), and **unit groups for parallel workers** with their order. W14's and W15's ports come from here:
   publish their section early.
3. **The fingerprint's `ref` side** for the new units (extend `StateFp`, the same canonical form), and its cost per unit.
4. **The references**: golden runs of the M3 run(s) (cycle counts, rows compared with `rows.bin`, fingerprints at
   intervals, per-unit dumps), each with a D12 header; a regenerate-and-check script; large files on the runner with
   their hashes.
5. **The baseline**: `ref`'s wall time on the M3 run(s), one JVM pinned, setup and run separated, the machine recorded
   (D21), three runs.

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
- Branch `sim-w13`. Your drivers live in `sim/tests/ref/` (compiled into `ref`'s default package); your references in
  `sim/tests/golden-m3/` or on the runner. `sim/ref` is frozen apart from D5's observer.

DELIVERABLES: `AUDIT-M3.md` with the unit groups (W14's and W15's sections early); the fingerprint's `ref` side; the
references and their check script; the baseline; `WORKER13/REPORT.md`.
