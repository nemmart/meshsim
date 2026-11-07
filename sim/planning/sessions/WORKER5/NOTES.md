# WORKER5 notes: the RV32 controller on the framework

## The plan (for the go-ahead)

### What reading the oracle changed in the sketch and the spec

1. **Table-window loads take 1 cycle, not 2.** `Controller` waits a cycle only for `Registers.inRegs(ea)`, i.e. `ea >=
   0xFFFFF800` (the block and the peripheral windows); the table window (`0xFFFF2000-0xFFFF35FF`) is below it. AUDIT 6 and
   TRACE-FORMAT's table say 2. The golden agrees with the code (`faults-t6`: `lui` at 97, the PTR load's `TRAP` at 98). Not
   a golden error, a doc error: I follow the code; the two docs want a one-line fix (integrator's).
2. **A block or window load waits first, then checks.** `ref` takes the wait cycle for any `ea >= 0xFFFFF800` before the
   `f3` check (illegal), the alignment check (UB) and the decode (bus error). So a sub-word or reserved-`f3` load there
   costs its wait cycle before it traps. Sketch 2's `load_reg` bus-errors without waiting: I follow `ref`.
3. **The `REQUEST_SEND` refusal** is checked before the `f3 > 2` and alignment checks, and only for `sw` at that word.
4. **`violate` only counts in `ref`** (reserved words, an offset past 32 KB that aliases, a store into data memory's first
   16 bytes, reserved bits in a stored value, mret): the C++ counts them the same way and carries on; none is fatal.
5. **An error in a handler that halts (HALT 8) still raises ERRORS**: `busErrors++`/`illegals++` come before `trap`'s
   stop. So the core posts the error on `bus` in that case too. One `bus` put a cycle is enough: a faulting access has no
   other effect.
6. **`start_skew`** is `java.util.Random(1).nextInt(2)` per tile (`Chip`, `skewSeed` 1): the harness reproduces it (10
   lines) so the D12 check compares it like any key.
7. **Outside M1, fatal "not modelled in M1: <what>"**: ROUTER_HDR/ADDR/SET_W0-W2/SEND_*, the write counters, DMA_*,
   PULSE_DMA, SRAM_RD_*, REQUEST_GET_W0-W2 and REQUEST_RECEIVE (loads), peripheral 1's window (present, type 4), HIST loads
   and stores, stores to PTR/STREAM/GROUP. **Implemented exactly, being local and cheap**: reserved words (read 0, stores
   ignored, counted), the absent windows 2-3 (bus error), loads from PTR/STREAM/GROUP (bus error), EVENTS_REQUIRED (it is
   in the wake term), TRACE (counted), `mret`.

### The design: three layers

**(a) The logic, as plain code** (no framework):
- `Rv32`: `Controller.runCycle` and `execute` line for line, templated on its **environment**: the register view, the
  wake term, the interrupt term, `canPush`, the bus to the block, the trace sink. Two environments: a plain struct (unit
  tests, the direct driver) and the unit's adapter over `Ctx`. Both inline; two instantiations of genuinely different
  code, nothing templated on payloads (D16).
- `RegBlockState`: TileControl's M1 subset and RequestPort's count; `end_cycle(go, store, landed, freed, error,
  consume)` in `endCycle`'s order, then the view (STATUS, EVENTS, ERRORS, the wake and interrupt terms, request busy).
- The register map and decode (`Registers.java`'s subset: `loadAt`, `storeAt`, regions, windows, tables).

**(b) The three units** of sketch 2 on `sim.h`, thin wrappers of (a), with sketch 2's ports, wires and levels (bench and
block at level 0, core at level 1). The bench replays MEMW (a cycle's consecutive words as one put), GO, CMD and REQ-FREED,
nothing else.

**(c) The harness** (`sim/tests/`): parse a golden (header, replayed events, body, closing block); load the images and
`bootrom.hex`, checking their sha256 against the header; **refuse on any D12 difference, naming the key and both
values**; run; write the trace; compare line for line, reporting the first difference with 5 lines of context each side.

**Two drivers, the same trace.** A **direct driver** (~60 lines, no framework): per cycle, the bench's events, the core's
`run_cycle`, the block's `end_cycle`, in `ref`'s order. It matches all 67 goldens **before W3's engine exists**, and
stays as a second oracle: when the engine run differs, the direct run tells a unit bug from an engine bug. The **engine
driver** builds the three units with `Config` and runs them; its trace and closing block must equal both.

**The trace in the loop** (D6): a `TraceLog`, a fixed array of small records (cycle, rank, kind, up to 4 words) allocated
at configuration from the golden's line count plus a margin; full stops recording and is reported as a difference, never
grows. Formatting happens after the run. Order within a cycle by rank (MEMW, GO, CMD, the core's lines, HALT, REQ-FREED),
stable, as TRACE-FORMAT orders the file.

**The end of a run**: the harness runs to the golden's END cycle. The core records how it stopped (halt R, parked, UB) and
the cycle; a halted core stays asleep, its `mcycle` caught up from its last step (so step-all, which steps it anyway, gives
the same closing block). Not stopped by then: `END timeout`, a difference. No API change needed.

### Files

| file | what |
|---|---|
| `src/units/regmap.h` | the register map, the address decode |
| `src/units/rv32.h` | the core (templated on the environment), CSRs, memories, boot ROM, traps, timing |
| `src/units/regblock.h` | `RegBlockState` |
| `src/units/trace_log.h`, `.cpp` | the records; formatting (configuration regime) |
| `src/units/controller.h`, `.cpp` | `Core`, `RegBlock`, `TestBench`; `configure_tile()` |
| `tests/golden_run.h`, `.cpp` | parse, D12 check, load, the two drivers, compare |
| `tests/sha256.h`, `.cpp`; `tests/java_random.h` | the digests; the skew |
| `tests/test_rv32.cpp`, `test_regblock.cpp` | unit tests on (a) |
| `tests/test_golden.cpp` | all 67 in `make check` (direct now; engine, seeds, orders, step-all once W3 lands) |
| `tests/golden_main.cpp` | the `golden` command: `golden [--direct\|--engine] [--seed N] [--order instance] [--step-all] [names]` |

`sim/Makefile`: two small additions (the `src/units` objects in the test links; `make golden`). It is shared with W3's
stage 2, so I'll keep the diff minimal and merge `main` before touching it.

### Order of work, and how the core is tested before the engine

1. The register map, sha256, Java's Random; the golden parser, D12 check and image loader, with tests.
2. `Rv32` and its unit tests against a scripted environment: every instruction class (sign and zero extension, shifts,
   the three `mulh` against 64-bit arithmetic, `div`/`rem` illegal), the CSR rules (`mtime` write illegal, `csrr` not a
   write, `misa`, `mip`, MPP on read, `mepc`/`mtvec` low bits), traps (`mepc`, `mtval`, `mcause`, MPIE), the halts (2, 3,
   5, 6, 7, 8), parked, UB; the timing (branch +1, multiply 3, block load 2 with an interrupt preempting its first cycle,
   a refused `REQUEST_SEND` re-executed, the interrupt held off by `stall` but not in WFI); items 1-3 above.
3. `RegBlockState` and its tests (`endCycle`'s order: GO, store, landed; a consume beats a same-cycle set; the gate;
   request room on busy's fall; STATUS for t + 1).
4. The direct driver, `TraceLog`, compare: **all 67 matching**; `make golden`. Commit and push.
5. The three units on the API (they compile against `sim.h` now, as the sketches do).
6. W3's engine lands: merge `main`; all 67 on the engine; then shuffle seeds, both orders, step-all (its digest equal to the
   ready-driven run's) and the missed-wake detector; debug and release, gcc and clang.
7. W4's threads: N controllers on N threads, different programs, each matching.
8. The report: lines against `Controller.java` (444) and the `TileControl`/`RequestPort` parts replaced; instructions per
   second against `ref`, on the runner (clang release, pinned to a P-core), the number claimed in `NUMBERS.md` first.

The 67 goldens are at most ~1,000 lines and a few thousand cycles each, so all of them run in `make check` (every build).

### For the integrator

- Item 1's doc fix (AUDIT 6, TRACE-FORMAT): table-window loads are 1 cycle.
- Item 7's list of what is fatal and what is exact: OK?
- No API change needed so far.

## The go-ahead (owner, 2026-11-07)

Approved as planned, with: the doc fix for table-window loads (done on main, merged); follow `ref` for the block load's
order; the fatal/exact split as listed, each fatal message naming the register or window and saying "not modelled in M1"
(so M2 can grep for them); the direct driver kept small, a regression check once the engine path matches; my make rules in
`src/units/units.mk`, one include line in `sim/Makefile`.

## Progress

**Step 1-4 done (commit "W5: the controller's logic ...").** `src/units/`: `regmap.h` (the address map and register
table), `rv32.h` (the core), `regblock.h` (the block's subset), `trace_log.h/.cpp`, `controller.h/.cpp` (the three units,
compiled in every build, linked once the engine lands). `tests/`: `golden_run.h/.cpp` (the harness and the direct
driver, ~60 lines of loop, run under the allocation hook), `golden_main.cpp` (`make golden`), `sha256`, `java_random.h`,
`test_rv32.cpp` (34), `test_regblock.cpp` (7), `test_golden.cpp` (6).

- **All 67 goldens match with the direct driver**, the whole file: our own header (sha256 of the images and the boot ROM,
  `data_at` and `entry` from the `.sym` as TraceRun takes them, the skew from `java.util.Random(1)`, the schedule's steps),
  the body, and the closing block. 0.03 s for all 67 (release).
- The D12 check refuses with the key and both values (`refused (D12): config mul_latency: golden 4, ours 3`), or the
  header line otherwise (image, tile, boot ROM). Tested, as is a core that differs (a 4-cycle multiply) being caught.
- `make check` clean: gcc and clang, debug (ASan, UBSan) and release.
- Digests: each unit defines `digest()` (sim.h: a unit holding pointers must). The core folds the counters `catch_up`
  advances in relative to `last` (`mcycle - last`, stall and WFI cycles `- last`, the stall's end cycle), so a core that
  slept and one stepped every cycle digest the same (tested).

**Waiting for W3**: increment 2 (on `sim-w3`, not on `main`) has Link, Fifo and Hold; the controller needs `Wire` and
`RegFile` (increment 3). A trial merge of `sim-w3` into this branch (scratch worktree, not pushed) merges cleanly, the
Makefile included.

### For W3 (no change asked)

- **Catch-up counters and the step-all digest.** A unit that counts cycles lazily (`mcycle`) differs between the two
  runs until it next steps, so its bytes can't be compared; `digest()` has to fold those counters relative to the cycle it
  last ran. Worth a sentence in sim.h's step-all paragraph: "counts the same either way" holds only after catch-up.
- Struct padding: a unit digested by its bytes can differ in its padding between two machines; my units define
  `digest()` anyway, but the default might want a note.
