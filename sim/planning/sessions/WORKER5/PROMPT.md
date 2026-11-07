# Worker 5 (sim/): the RV32 controller on the framework, matching `ref` instruction for instruction

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`): very fast, parallel, and easy to
build units in, with v1.3's Java simulator (`sim/ref/`) as the exactness oracle. **Milestone 1 ends with your work**: one
tile controller, built as units on the framework, running the same firmware as `ref` and producing **the same retire trace,
line for line, from reset (the boot ROM included) to halt**, on all 67 golden traces. W3 is implementing the framework
(stage 2) beside you; its API is agreed and merged. You write the controller against that API now, test what can be tested
on its own, and integrate when W3's engine lands.

READ FIRST: `sim/README.md`; `sim/planning/PLAN-M1.md` (you are W5, item 8; "What M1 proves", "Carried to M2");
`sim/planning/DECISIONS.md` (D2, D3, D12, D17, D18, D20 especially). **The API:** `sim/src/sim.h`, `sim/src/config.h`,
`sim/src/PORTS.md`, and **`sim/src/sketch/controller.cpp`** (W3's sketch of exactly your three units: your starting point).
**Your specification:** `sim/tests/AUDIT-M1.md` (every coupling between the controller and its tile, with the cycle each
takes effect, the order of `Controller.runCycle`, the interrupt rule, the request FIFO, what is replayed and what you must
derive) and `sim/tests/TRACE-FORMAT.md` (the trace you write, its D12 header, the closing block). **The oracle:**
`sim/ref/meshsim/src/Controller.java`, `TileControl.java`, `Registers.java`, `ControllerMemories.java`, `RequestPort.java`,
`sw/bootrom.S` and `sw/bootrom.hex`. **The goldens:** `sim/tests/golden/` (traces, `images/` with `MANIFEST.md`,
`regen.sh --check`), and `sim/planning/sessions/WORKER2-REPORT.md` (how they were made, `reqfull`'s stalls).

THE WORK
1. **The core's own logic, as plain code** (no framework needed to test it): decode and execute RV32I + Zmmul (div and rem
   trap as illegal instructions, as in `ref`), the trap CSRs, `mcycle`/`mtime` (read-only `mtime`: writing it is illegal),
   traps and halts, WFI, data and program memory and the boot ROM, the address decode with its bus errors and UB stops
   (misaligned loads and stores), and `ref`'s timing: a register-block load takes 2 cycles sampling in the second, a taken
   branch +1, a multiply 3, stores posted, a `REQUEST_SEND` stalling while the FIFO is full. Unit tests on it directly.
2. **The three units** of sketch 2, on the API: the core, the register-block subset (TileControl's M1 part, as the shifted
   end of cycle), and the test bench, which replays exactly AUDIT section 5's lines (MEMW, GO, CMD, REQ-FREED) and nothing
   else. Everything else, REQ-REFUSED included, must come out of your units.
3. **The harness:** load a golden's images (and `bootrom.hex`); read its D12 header and **refuse, naming the difference, if
   your configuration differs**; run to halt; write the trace in TRACE-FORMAT; compare line for line, reporting the first
   difference with context. One command runs all 67 (`make golden` or similar), fast enough for `make check` if it can be.
4. **Integration** when W3's engine lands: all 67 goldens match, with the halt reason, registers, CSRs, `mcycle` and the
   data-memory digest equal. Then the debug checks: shuffle seeds, both evaluation orders, **step-all** (D20: its digest
   equal to the ready-driven run's), and the missed-wake detector clean.
5. **After W4's threads:** N controllers on N threads, running different programs, each still matching.
6. **Report:** lines of code against `Controller.java` (444) and the `TileControl`/`RequestPort` parts you replace;
   instructions per second against `ref`, on the runner, clang release (D18), pinned to a P-core (D13).

RULES
- **Propose your plan first** (files, the order of work, how you'll test the core before the engine exists, the harness)
  in `WORKER5/NOTES.md`, and wait for the go-ahead before writing code.
- Work on branch `sim-w5`, in `sim/src/units/` (your units) and `sim/tests/` (the harness, your tests); merge `main` in
  (never rebase) to pick up W3's work as it lands. W3 owns the rest of `sim/src/`: **if the API needs a change, write it up
  in your notes and tell the integrator; don't edit W3's files.**
- Implement `ref`'s rules in full, including the two interrupt paths M1 can't test (PLAN-M1 "Carried to M2"); anything
  outside M1 (the router's send holding register, the peripheral slot) is a fatal "not modelled in M1", never a silent no-op.
- The cycle-loop rules (D6): no STL, no allocation, no virtual calls in any `step`; untyped 64-bit words; 128-bit values
  inline as two words (D17). Sanitizers and deterministic tests from the first commit; `make check` clean on gcc and clang.
- Exactness is the requirement: a difference from a golden is a bug, never a tolerance. If you believe a golden is wrong,
  stop and report it with the evidence; don't change it.
- Long runs go to the runner (claim plain Quest numbers in `NUMBERS.md` first; tasks carry embedded kits).
- Never write a token into a file. Never `pkill -f` a pattern that matches your own command line.
- Commit and push after each completed piece, keep the last tool calls for committing, end each turn with a clean
  `git status`.
- Keep replies to the owner short.

DELIVERABLES: the controller's units and the golden harness on `sim-w5`; all 67 goldens matching with every debug check
clean; `WORKER5/REPORT.md` with the line counts and the speed.
