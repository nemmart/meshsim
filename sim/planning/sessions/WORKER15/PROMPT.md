# Worker 15 (sim/, M3): the replayer and the peripheral slot

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`), with v1.3's Java simulator
(`sim/ref/`) as the exactness oracle. **M1** built the framework and the RV32 controller; **M2** ran the full-size sort
exactly as `ref`, one chip split over threads, 20.9× faster (`sim/planning/PLAN-M2.md`). **M3's target (owner, D32): get
v1.3 running as it is, the Tracer and the replayers**: a segment on the full chip as `ref`'s `TracerT2` runs it
(`sim/planning/PLAN-M3.md`).
**You build the replayer** on the framework: each tile's type-1 peripheral, which replays a window's run log into rows,
and **the peripheral slot** it sits behind (the tile's interface to any peripheral: the register paths, mode-9 packets,
the run pulse, the write port, the condition events). Beside you: W13 (the reference side and the audit: your ports come
from it) and W14 (the Tracer, which shares the row formats and tables with you).

READ FIRST: `sim/README.md`; `sim/planning/PLAN-M3.md`; `sim/planning/DECISIONS.md` (D6, D17, D20-D31). **The framework:**
`sim/src/sim.h`, `config.h`, `PORTS.md` and `sim/planning/sessions/WORKER3-REPORT.md`; **how a unit is built and checked
here:** `WORKER9-REPORT.md` (RouterConf), `WORKER11-REPORT.md` (MemConf), `WORKER8-REPORT.md` (the register block, the
director, the fingerprint). **The oracle:** `sim/ref/meshsim/REPLAYER-NOTES.md` ("Wiring it into the tile", the model
choices M1-M6), `src/Replayer.java`, `ReplayerAlu.java`, `PeripheralSlot.java`, `Peripheral.java`, and the suites
`test/ReplayerTest.java` (with `tools/replayer-opcodes/vec.tsv`), `ReplayerUnitTests.java`, `ReplayDriver.java`,
`ReplayDelivery.java`, `StageB.java`, `PeripheralSlotTest.java`; `sw/replay/`.

THE WORK
1. **The replayer on its own**: the ALU against `vec.tsv` first, then the machine (front end, expansion, the code, log and
   read-log streams, the output queue and sink, the image copy, errors) as `Replayer.java` models it, with REPLAYER-NOTES'
   model choices M1-M6. Unit state laid out for the cycle loop (D6).
2. **Checked against `ref` on its own, before any wiring**: port `ReplayerTest` and `ReplayerUnitTests`; a conformance
   driver (`sim/tests/ref/ReplayConf.java`, as RouterConf) comparing `ref`'s replayer and yours every cycle through the
   `Peripheral` interface; then Stage A's deliveries (`ReplayDriver`/`ReplayDelivery`: rows byte for byte against
   `rows.bin`, on the runner's data).
3. **The peripheral slot**, and the register block's and director's peripheral paths (`TILE_PULSE` [2], `periph_running`,
   the `WAKE_*` loads, `DMA_PERIPH_REMAINING`, conditions [11]-[13], mode 9), extending W8's, W11's and W12's files (all
   closed: note each change). Then wiring into the tile, once W13 publishes the ports, and Stage B (`replay_ctl` on a tile).

SHARED CODE WITH W14. The Tracer shares formats and tables with the replayer (the record, the region map, the decode
table, templates, the expansion rules: `RowCore`, `ReplayerAlu`). Agree with W14 in one round which shared pieces live in
one file and who owns it; the other uses it read-only and asks for changes. Disagreements to the integrator.

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
- Branch `sim-w15`, in `sim/src/units/` (your own file names) and `sim/tests/`. The framework (`sim/src/*.h|cpp` outside
  `units/`) has no active owner: if the API needs a change, write it up and tell the integrator.

DELIVERABLES: the replayer and the peripheral slot as framework units, equal to `ref` every cycle on their suites,
ReplayConf (fixtures in `make check`) and Stage A's deliveries; Stage B; wired into the tile; `WORKER15/REPORT.md`.
