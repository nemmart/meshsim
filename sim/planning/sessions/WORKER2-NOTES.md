# WORKER2 notes: the plan (agreed with the owner 2026-11-07)

## The observer (D5)

One class, `sim/ref/meshsim/src/Observer.java`, on with `-Dmeshsim.observe=<file>,<tile>`, off by default. The guard is a
`static final boolean` read from the property at class load. javac keeps the calls (not a compile-time constant); the JIT
folds the guard when it's off. The proof that it changes nothing is the identical logs, not that mechanism.

Call sites (about 6, each one guarded line):
- **retire**: the end of `Controller.execute`, with t, the pc, the instruction word, rs1 and rs2 as read (captured before
  writeback, so rd == rs1 or rs2 still logs the operands), and rd's value if written. The memory operation is decoded
  from those;
- **trap**: `Controller.trap` (cause, mtval, mepc, mtvec);
- **halt**: `Controller.haltWith` (reason). An `UndefinedBehavior` stop is caught and logged by the driver (no call site);
- **WFI wake**: `Controller.runCycle`, where a WFI is left (cause GO or event);
- **request refused**: the `REQUEST_SEND` stall branch in `execute`, once per stalled cycle. Acceptance shows in the
  store's own retire line;
- **command landed, GO**: `TileControl.endCycle`, at the cycle they're applied.

Proof: with the observer present and off, `run_tests.sh quick`, `RiscvTest` and `CtrlTest` logs identical to the pristine
copy (`prove_same.sh` from `wip/planning/sessions/WORKER29-tools/` as a base); on, only the trace files differ. Recorded
in `sim/ref/README.md`.

## The trace format (`sim/tests/TRACE-FORMAT.md`)

Plain text, hex, one line per event. A line's cycle is **the cycle in which ref's `step()` completes the instruction**;
the register load's second cycle, the issue cycle of a multiply or taken branch, and the accept cycle of a stalled store
all follow from that rule.
- the header (D12): the configuration (GO and READY latency, the register-load latency, `stage_delay`, `refCycle`, the rest
  of `Config`'s relevant values), the shuffle seed, the tile, the image's sha256;
- retire: `cycle pc insn [xN=val] [ld|st addr size val]`;
- events: `TRAP`, `HALT`, `WAKE go|event`, `CMD`, `GO`, `REQ-REFUSED`;
- a closing block: registers, CSRs, `mcycle`, a data-memory digest.

## The coupling audit (`WORKER2/AUDIT.md`)

Every Controller-tile interaction in the M1 programs, its cycle, same-cycle queries marked; which events the C++ test bench
replays (command landed, GO, request refused) and which the C++ controller must produce itself (everything else); which
`Config` values the controller's behaviour depends on (so the D12 header is complete).

## The trace runner (`sim/tests/ref/TraceRun.java`)

Builds the machine as `RiscvTest` does, for any tile k: pokes code and data into tile k, C&C 1 expects one tile, the boot
command to tile k, then GO; then lands the further commands and GOs at the cycles in the program's schedule. Runs to a halt,
the handler's park, or an `UndefinedBehavior` stop.

## The M1 programs (`sim/fw/`, assembly)

- `csr`: every trap CSR, its kept bits (WARL), the read-only ones, the immediate and no-write forms; one deliberate trap to
  its own handler, chosen by the command argument (illegal, load fault, store fault);
- `timers`: `mcycle`/`mtime` and the high halves around loops with known stalls; writes to `mcycle`/`mcycleh`, a carry;
- `wfi_cmd`: a WFI woken by a command landing;
- `wfi_go`: a WFI woken by GO; GO before the WFI falls through;
- `irq`: its own mtvec, MIE and MEIE set, then an ERRORS bit raised (a store to a read-only register); the trap's cause,
  mepc and cycle (`interruptPending` reads `errorsAt(t)` in the same cycle).

## Golden traces (`sim/tests/golden/`)

The 48 riscv-tests, `hello`, `faults` on tiles 0-7, `misalign_ld` on 9, `misalign_st` on 12, the M1 programs. The images
traced from in `sim/tests/golden/images/` with the toolchain (gcc-riscv64-unknown-elf 13.2, apt), the exact build command and
each image's sha256. One script regenerates every trace and checks it, header included, against what's committed.

## The baseline (D8)

One Quest task: ReplayBench (`sim-step-cost`) on segment 0 of evm-zone-29 as task 120, 1 JVM alone then 16 at once.

## Answers (owner, 2026-11-07)

1. The test-bench event is the request FIFO refusing a `REQUEST_SEND` store, once per stalled cycle (PLAN-M1 corrected).
2. Images committed under `sim/tests/golden/images/` with toolchain, build command and sha256.
3. The 0x600D restart is out of M1.
4. The interrupt term is covered (`irq`).
