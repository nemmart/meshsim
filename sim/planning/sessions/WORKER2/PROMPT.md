# Worker 2 (sim/): the reference side: the observer, golden traces, the coupling audit, the M1 programs, the baseline

**The project.** A clean-sheet simulation framework in C++ for the mesh ASIC (`sim/src/`): very fast, parallel, and easy to
build units in, with v1.3's Java simulator (`sim/ref/`) as the exactness oracle. Milestone 1 is the primitives plus one RV32
controller running real firmware, **matching `ref` instruction for instruction**. You make that check possible: everything
the C++ side will be compared against, and the precise description of what a controller sees of its tile. You work in Java
and on the runner, beside W1 (the C++ build and structures). W5 builds the C++ controller from your audit and your traces.

READ FIRST: `sim/README.md`, `wip/planning/SIMULATOR-NEXT.md`, `sim/planning/PLAN-M1.md` (you are W2), `sim/planning/
DECISIONS.md` (D2, D3, D5, D8 especially). Then in `sim/ref/meshsim/`: `src/Controller.java` (the core, its timing, WFI,
traps, the send stall), `src/TileControl.java` (`takeWake`, `errorsAt`, `endCycle`, the registers), `src/Registers.java`,
`src/RegisterBlock.java`, `sw/bootrom.S`, `test/RiscvTest.java` (how a program is booted on tile 0 alone: command, then GO),
`test/CtrlTest.java` and `test/Mach.java` (how `sw/tests` programs are run), `sw/tests/{hello,faults,misalign_ld,
misalign_st}.c`. For the runner: the Quest repository's `PIPELINE.md` and `NUMBERS.md`, `wip/planning/INTEGRATOR-HANDOVER.md`'s
"How we work", and Quest's `tasks/120-w29-proof-c.sh` (an embedded kit, the inputs on the box, ReplayBench).

THE WORK
1. **The observer** (DECISIONS D5: the one permitted change to `sim/ref`). One class in `sim/ref/meshsim/src`, opt-in by a
   system property, off by default, with the fewest possible call sites. It writes, for one chosen tile's controller:
   - **a line per retired instruction:** the cycle, pc, the instruction word, rd and the value written (if any), the memory
     operation (load or store, address, size, value; register-block and window accesses included);
   - **the test-bench events**, at the cycle the controller can first observe them: a command landed, GO, the send holding
     register accepting or refusing; and halts, traps and WFI entry and wake if the retire lines don't already make them
     exact.
   **Proof that it changes nothing:** with the observer compiled in and off, `run_tests.sh` quick, `RiscvTest` and `CtrlTest`
   give logs identical to the pristine `sim/ref` (the old series' `prove_same.sh` in `wip/planning/sessions/WORKER29-tools/`
   may help); with it on, only the trace files differ. Record it in `sim/ref/README.md` (new: `sim/ref` frozen, this the one
   change, how to switch it on).
2. **The trace format**, `sim/tests/TRACE-FORMAT.md`: every field, and exactly what a line's cycle means for a multi-cycle
   instruction (a register load, a multiply, a taken branch), a send stall, WFI, a trap, a halt. Plain text, one line per
   event, easy to diff. The C++ side writes the same format.
3. **The coupling audit**, `WORKER2/AUDIT.md`: every interaction between the Controller and the rest of its tile in the M1
   programs, each with the cycle it takes effect and whether it is a same-cycle query (the registers read and written, the
   WFI wake term, the interrupt term, PARAMS, COMMAND, the send holding register's acceptance, the table window's decode, the
   counters `mcycle`/`mtime` and `refCycle`). This is W5's specification for the register-block subset and the test-bench unit,
   so be complete and precise; anything the M1 programs don't touch, say so.
4. **The M1 programs** (D2), in `sim/fw/`: small single-tile programs replacing `regs` and `wfi`, covering the trap CSRs
   (`mtvec`, `mscratch`, `mepc`, `mcause`, `mtval`, `mstatus`, `mie`), `mcycle`/`mtime` (and their high halves) around loops of
   known length, and **WFI woken by a command and by GO** (the test bench lands them at chosen cycles). Sources, a build script
   and the built images committed. Find where the riscv toolchain is available (locally, installable, or on the runner; W1 is
   probing the runner) and say which you used.
5. **The trace runner and golden traces.** A Java driver in `sim/tests/ref/` (compiled into `ref`'s default package against its
   classes; its classes are package-private) that boots one program on **the traced tile alone**, as `RiscvTest` does, lands
   the commands and GO a program expects, and writes the trace. Golden traces in `sim/tests/golden/` for: the 48 riscv-tests
   programs (`test/riscv/bin`), `hello`, `faults` on tiles 0-7 (one run each), `misalign_ld` on tile 9, `misalign_st` on tile
   12, and your M1 programs. A script that regenerates them all and checks they match what's committed.
6. **The baseline** (D8), one Quest task: ReplayBench on `sim-step-cost` (where it lives), segment 0 of evm-zone-29 (as task
   120), as **1 JVM alone, then 16 JVMs at once**: µs per chip-cycle per JVM, chip-cycles per second in aggregate, and the
   box's load. This is the bar the new framework is judged against.

RULES
- **Propose your plan first** (the observer's call sites, the trace's fields, how the driver boots and drives a program, the
  M1 programs' outline) in `WORKER2/NOTES.md`, and wait for the go-ahead before changing `sim/ref` or writing code.
- `sim/ref` is frozen apart from the observer: no other change there, however small. Everything else lives in `sim/tests/`,
  `sim/fw/` and your session folder.
- Work on branch `sim-w2`; the integrator merges. Keep your notes, questions and report in `sim/planning/sessions/WORKER2/`.
- Long runs go to the runner (claim plain Quest numbers in `NUMBERS.md` first; the meshsim repository is private, so tasks
  carry embedded kits); background jobs don't survive a turn.
- Never write a token into a file. Never `pkill -f` a pattern that matches your own command line.
- Commit and push before anything long in a turn, keep the last tool calls for committing, end each turn with a clean
  `git status`.
- Keep replies to the owner short.

DELIVERABLES: the branch with the observer and its proof, `TRACE-FORMAT.md`, `AUDIT.md`, the M1 programs and their images,
the trace runner and the golden traces; the baseline's numbers; `WORKER2/REPORT.md`.
