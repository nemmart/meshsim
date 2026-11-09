# Worker 16 (sim/, M3, group G7): the C&C cores and the chip's integration

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`), with v1.3's Java simulator
(`sim/ref/`) as the exactness oracle. **M1** built the framework and the RV32 controller; **M2** ran the full-size sort
exactly as `ref`, one chip split over threads, 20.9× faster (`sim/planning/PLAN-M2.md`). **M3's target (owner, D32): get
v1.3 running as it is, the Tracer and the replayers**: a segment on the full chip as `ref`'s `TracerT2` runs it
(`sim/planning/PLAN-M3.md`).
**You are G7** (`sim/tests/AUDIT-M3.md` section 5): **the C&C cores and the chip's integration**, then **T2 end to end**.
Beside you: W14 (G5, the Tracer) and W15 (G6, the slot and the replayer), whose units you wire in as they land. W13's
audit and references are on `main`.

READ FIRST: `sim/planning/PLAN-M3.md`, `sim/planning/DECISIONS.md` (D5, D12, D21, D23, D26-D32), **`sim/tests/AUDIT-M3.md`
in full** (sections 0, 2.2, **3.4**, 4, 5, 6.7-6.9, 7, 9 are yours), `sim/tests/golden-m3/` (README, RUNS, HASHES,
`regen.sh`), `sim/planning/sessions/WORKER13-REPORT.md` when it lands. **The chip you extend:** M2's units and assembly,
in `sim/src/units/` (`chip.h`, `cnc.h`, the tile builder), and the reports that explain them:
`sim/planning/sessions/WORKER9-REPORT.md` (the chip, the driver, C&C's Java model you now replace with cores),
`WORKER8-REPORT.md` (the controller's units, the fingerprint library), `WORKER12-REPORT.md` (the sort's driver),
`WORKER10-REPORT.md` (placement, the measurement tools). **The framework:** `sim/src/sim.h`, `config.h`, `PORTS.md`.
**The oracle:** `sim/ref/meshsim/src/CcCores.java`, `Chip.java`, `Cnc1Peripheral.java`, the request bus, the host port,
`test/TracerT2.java`, `CcCoresTest.java`, `CcBench.java`, `sw/cc/`, and W13's drivers `sim/tests/ref/M3Seg.java`,
`StateFpM3.java`.

THE WORK
1. **The C&C units**: units 64-66 as framework tiles built from M2's units with the controller variant of the router,
   the C&C links (latency 1, so C&C c shares a domain with tile c), the host port and its stand-in, the request bus's
   controller inputs and targets, C&C 1's peripheral (READY, GO, the error lines, the watchdog, the UART) and C&C 2's stub
   as type-1 and type-2 bodies inside W15's slot (W15 owns the slot and its dispatch; you own those two bodies), the chip
   lines as events (D23, section 4).
2. **The boot, exact: your first milestone.** `coresBooted` at cycle 7,222 and every unit's fingerprint at boot end
   (1,214 units) equal to `golden-m3`, with stubs for the Tracer and the replayers if theirs haven't landed.
3. **The T2 harness**: section 0's stand-ins, the memory snapshot at segment 10's start and the big-SRAM landings loaded
   from the runner's `$DATA/w13-golden-m3`, the rows observed and checked against `rows.bin`, the partition (C&C c with
   tile c, C&C 3 with tile 2, the replaying tiles spread).
4. **T2 end to end**, with W14's Tracer and W15's slot and replayer wired: evm-zone-29 segment 10 exact (fingerprints
   every 100,000 cycles localise a mismatch to a unit), then cyclone-29 segment 10.
5. **The first timing**: both runs, one thread and split, against `ref`'s baseline (46.2 s and 50.9 s per segment; W13).
   Per section 5, C&C 3's domain on one thread decides the wall time: report it separately.
C&C 1's peripheral applies stores in their own cycle where the slot's rule applies them one later (AUDIT 3.4's open item);
neither run reaches the difference, so keep it or propose it under D30.

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
- Branch `sim-w16`, in `sim/src/units/` and `sim/tests/`. W14's and W15's files are theirs: wire to them, and ask through
  the integrator for changes. The framework (`sim/src/*.h|cpp` outside `units/`) has no active owner: if the API needs a
  change, write it up and tell the integrator.

DELIVERABLES: the C&C units and the chip's integration; the boot exact; the T2 harness; both runs exact end to end; the
first timing; `WORKER16/REPORT.md`.
