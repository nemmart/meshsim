# WORKER2 report: the reference side

Branch `sim-w2`. The plan is in `NOTES.md` (agreed 2026-11-07); W5's specification is `AUDIT.md`.

**Status: Quest 123 (the quick-suite proof and the baseline) pending; sections 1 and 6 are filled in from its results.**

## 1. The observer (D5)

`sim/ref/meshsim/src/Observer.java` and 7 one-line guarded call sites (`Controller` 5: retire, trap, halt, wake, request
refused; `ControllerMemories` 1: port-B writes; `TileControl.endCycle` 1: GO, command landed, request FIFO freed). Off by
default; `-Dmeshsim.observe=FILE,TILE` or `=on` with a driver. Recorded in `sim/ref/README.md`. The only diff of `sim/ref`
against main: those four files, +101 lines.

**The proof** (with the observer present and off, and on: logs, exit codes and every chip's fingerprint against main's
pristine `sim/ref`):
- locally: `RiscvTest` and `CtrlTest` seeds -1 and 4: identical, off and on;
- Quest 123, the quick suite's 39 Java steps (`sim/tests/ref/prove_observer.sh`): *pending*.

## 2. The trace format

`sim/tests/TRACE-FORMAT.md`: one rule for a line's cycle (the cycle in which ref's `step()` completes the instruction);
retire lines (`C PC INSN [xN=VAL] [ld|st ADDR SIZE VAL]`, operands as read); the events `MEMW`, `GO`, `CMD`, `REQ-FREED`
(replayed by the C++ bench), `REQ-REFUSED` (a check), `WAKE`, `TRAP`, `HALT`, `UB`; their order within a cycle; the D12
header (images' sha256, the boot ROM's, 25 `config` keys, the schedule); the closing block (registers, CSRs, `mcycle`,
the data memory's sha256). A `MEMW` at C is visible to a load the core executes at C.

## 3. The coupling audit

`AUDIT.md`: `Controller.runCycle`'s order; `interruptPending`'s condition and the cycle it is judged in (an error at t is
seen at t + 1; held off by `stall`, not by a register load's first cycle or a refused `REQUEST_SEND`); the wake term; the
register-block subset (PARAMS, STATUS, ERRORS, PULSE_DONE, EVENTS, EVENTS_PEEK, EVENTS_MASK, REQUEST_SET_W0-W2,
REQUEST_SEND, SIM_EXIT) with each load's and store's cycle; `endCycle`'s same-cycle sequencing; the request FIFO and the
bench's count; replayed against derived; what M1 doesn't touch; the `Config` values the controller depends on.

Carried to M2 (PLAN-M1): an interrupt from ordinary code, and a pending interrupt waiting for a multiply or branch stall
(no non-trapping error source without the write counters).

## 4. The M1 programs (`sim/fw/`)

Assembly, built by `sim/fw/build.sh` (riscv64-unknown-elf-gcc 13.2, Ubuntu 24.04's `gcc-riscv64-unknown-elf`, installed
here with apt; the runner has no riscv toolchain, so images are committed):

| program | covers | ends |
|---|---|---|
| `csr` (+ `csr-load.tb`, `csr-store.tb`) | every trap CSR's kept bits, misa/mip read-only, immediate and no-write forms; a trap to its own handler: illegal (`cycle`), load fault, store fault | SIM_EXIT 1 at 129/129/132 |
| `timers` | `mcycle`/`mtime` and high halves around loops: 300 cycles (100 taken branches), 91 (10 multiply pairs), 81 (20 register loads); a write to `mcycleh`/`mcycle` and the carry; `mtime` after it | SIM_EXIT 2 at 1176 |
| `irq` | a bus error to its own handler, MIE set again, the external interrupt one cycle later (mcause `8000000b`) | SIM_EXIT 5 at 93 |
| `wfi_cmd` | WFI woken by a command landing (308); a later WFI falling through on a pending command | SIM_EXIT 3 at 1241 |
| `wfi_go` | WFI under the done gate woken by GO (311); GO before the WFI (falls through); a command, READY, C&C's GO (1526) | SIM_EXIT 4 at 1540 |
| `reqfull` | the request FIFO refusing `REQUEST_SEND` | SIM_EXIT 6 at 561 |

**reqfull's stalls**: 190 requests, two stores each, one push per 2 cycles against one 8-beat drain per 8 cycles. The FIFO
fills at 393; **118 refused cycles in 20 stalls**: 394-397 (4), then 400-405, 408-413, 416-421, 424-429, 432-437, 440-445,
448-453, 456-461, 464-469, 472-477, 480-485, 488-493, 496-501, 504-509, 512-517, 520-525, 528-533, 536-541, 544-549 (6
each): from then on a send is accepted every 8 cycles, in the cycle the bus frees an entry (`REQ-FREED` and the accepting
store both at 398, 406, ...). STATUS reads `0x40` (request busy) after the burst.

## 5. The trace runner and the golden traces

`sim/tests/ref/TraceRun.java` boots one program on one tile alone as `RiscvTest` boots tile 0 (images poked; C&C 1 expects
only that tile; the boot command, GO), runs the program's schedule (`sim/fw/*.tb`: `at`, `cmd`, `go`, `readygo`, `boot`,
`max`) and writes the trace. `sim/tests/golden/`: **67 traces** (48 riscv-tests on tile 0, `hello`, `faults` on tiles 0-7,
`misalign_ld` on 9, `misalign_st` on 12, the 8 M1 runs), `images/` with `MANIFEST.md` (toolchain, build commands, every
image's sha256; `build_images.sh` rebuilds them), `RUNS`, and `regen.sh [--check]` (images against the manifest, then each
trace's header, then the whole trace). `--check` passes; with `JOPTS=-Dmeshsim.skip=false` every body is identical (only
the `idle_skip` header line differs).

## 6. The baseline (D8, D13)

Quest 123: ReplayBench (sim-step-cost 8fd1ecd, code = b7655df) on segment 0 of evm-zone-29, windows 0-63, 3 repeats
per JVM, 3 GB heaps; steady rate = the mean of repeats 2-3. *Pending.*

## 7. Deviations from the prompt, and why

- The test bench's events grew from three to four replayed (`MEMW`, `GO`, `CMD`, `REQ-FREED`) with `REQ-REFUSED` as a
  check: a command's words reach memory through port B, and STATUS [6] / EVENTS [11] need the FIFO's occupancy (owner,
  2026-11-07).
- The interrupt from `irq` is taken inside a handler (a store to a read-only register traps synchronously first).
- `reqfull` added (no golden run stalled on the request FIFO).
- The images traced from live in `sim/tests/golden/images/` (the M1 programs' too), built by `sim/fw/build.sh`.
- The quick-suite proof runs `run_tests.sh quick`'s Java steps with the firmware prebuilt (the runner has no riscv
  toolchain), as `tools/run_quick.sh` does.
