# The coupling audit: what the controller sees of its tile in M1

*(WORKER2, 2026-11-07; moved here from its session folder when it closed. W5's specification.)*

W5's specification for the controller's surroundings in M1: the register-block subset and the test-bench unit. Every
interaction between `Controller` and the rest of its tile that the M1 programs exercise (the 48 riscv-tests, `hello`,
`faults` 0-7, `misalign_ld`, `misalign_st`, and `sim/fw`'s `csr` (3 variants), `timers`, `irq`, `wfi_cmd`, `wfi_go`,
`reqfull`), with the cycle each takes effect, the same-cycle queries marked, and what M1 doesn't touch. References are to
`sim/ref/meshsim/src`. Trace lines and cycles are as in `sim/tests/TRACE-FORMAT.md`.

## 0. The cycle, as ref runs it

`Chip.runCycle(t)`: the request bus first (`reqBus.runCycle`), then every tile, then C&C. `Tile.runCycle(t)`: its units
(`cmem` the controller memories' port B, `core` the controller, and the rest; the order may be shuffled), then the
peripheral slot, then **`TileControl.endCycle(t)`**, which applies everything the controller did at t to the register
block. So:

- **A store to the register block at t takes effect at the end of t**: a load sampling at t + 1 sees it, the wake term at
  t + 1 uses it. (`TileControl.write` records it; `apply` runs in `endCycle(t)`.)
- **A register-block or window load issued at t samples at t + 1** (`reg_load_latency` 2: the first cycle returns early
  with nothing done) and returns the state as at the end of t, plus the live bits at t + 1. Its side effect (`EVENTS`'s
  consume) is at the end of t + 1.
- Memories (`SRAMMacro`): a read returns the contents as at the start of the cycle; writes commit at its end. The core's
  store at t is seen by its load at t + 1; a port-B write at t (`MEMW` C = t + 1) by a load at t + 1.

`Controller.runCycle(t)`, in order (W5 implements exactly this):
1. `mcycle++` (every cycle, halted included).
2. Halted: nothing more.
3. A refused router send (`pendingT == t - 1 && sendHold.refused(t)`): retry, stall, return. *(Not in M1.)*
4. **The interrupt**: if `interruptPending(t)` and `stall == 0`: leave WFI, trap (cause `0x8000000B`), return.
5. In WFI: if the wake term holds at t (`ctl.takeWake(t)`), leave WFI and go on in the same cycle; else return.
6. `stall > 0` (a multiply's or a taken branch's extra cycles): `stall--`, return.
7. `step(t)`: fetch, execute.

## 1. The interrupt term (`Controller.interruptPending`)

**`interruptPending(t) = (mstatus & MIE) != 0 && (mie & MEIE) != 0 && ctl.errorsAt(t) != 0`**, judged at step 4 above,
at the start of the controller's work in cycle t, before WFI's wake and before the stall count:
- `errorsAt(t) = conditions | slot.liveErrors(t) << 11`. `conditions` (TILE_CONDITIONS) is updated in `endCycle`
  (`conditionEvents`): a condition whose event count grew during t - 1 is set at the end of t - 1. **So an error raised by
  the core at t (a bus error, an illegal instruction) is in `errorsAt(t + 1)`**, and an interrupt can be taken at t + 1 at
  the earliest. The peripherals' live errors are 0 in M1.
- `mstatus` and `mie` are the values as the previous instruction left them: a `csrsi mstatus, 8` at t makes the
  interrupt pending at t + 1 (`irq`: MIE set at 77, the trap at 78).
- **Not taken while `stall > 0`**: after a multiply (2 cycles) or a taken branch/jump (1 cycle), the interrupt waits for
  the stall to end. It is **not** held off by the first cycle of a register-block load (that is not `stall`: the load is
  re-executed at t + 1, and an interrupt pending at t + 1 preempts it, `mepc` = the load), nor by a refused `REQUEST_SEND`
  (likewise re-executed each cycle).
- Taken in WFI: the core leaves WFI and traps (no `WAKE` line).
- Taken inside a trap handler: allowed (only a synchronous trap inside the handler stops the core, `HALT 8`).
- The trap (`Controller.trap`): `mepc` = the next instruction not yet executed (`pc`), `mcause = 0x8000000B`,
  `mtval = 0`, MPIE = MIE, MIE = 0, `pc = mtvec & ~3`, `stall = 0`.

**Covered in M1** by `irq` only: an interrupt taken inside a handler, one cycle after `csrsi` set MIE with ERRORS [9]
already set. **Not covered (carried to M2, PLAN-M1 "Carried to M2")**: an interrupt from ordinary code, and a pending
interrupt waiting for a multiply or branch stall. Neither can be provoked without an error source that doesn't trap, and
the only controller-local one, done early (ERRORS [8]), needs the write counters. W5 implements the rule above in full;
M2 tests it.

`mip` (CSR 0x344) reads `MEIE` (bit 11) when `errorsAt(t) != 0` at the CSR instruction's cycle t; writes are ignored.

## 2. The WFI wake term (`TileControl.takeWake`, `wakeTerm`)

At cycle t, both for the `wfi` instruction itself (step 7) and for each cycle asleep (step 5):
- `e = eventsAt(t) = (events | liveEvents(t)) & 0xFFFF`, where `events` is EVENTS' sticky bits **as at the end of t - 1**
  and the live bits (request pending [12]: never on a mesh tile; the peripherals' [15:13]: 0 in M1, their masks are 0) at t.
- **The done gate closed** (`gate`, from a `PULSE_DONE` until GO, `done_gate` true): wake = `e & EV_GO` ([8]).
- Otherwise: wake = `(e & mask & ~required) != 0 || (required != 0 && (e & required) == required)`, with `mask` =
  EVENTS_MASK (reset `0x180`: GO [8] and counters done [7]) and `required` = EVENTS_REQUIRED (0; not used in M1).
- A wake consumes nothing. If the wake term holds when `wfi` executes, it falls through (the instruction retires and the
  next one runs at t + 1); if not, the core sleeps from t + 1 and leaves at the first cycle the term holds, running the next
  instruction in that same cycle (`WAKE` line).
- `lastWake` (the `WAKE` line's `go`/`event`): `go` if EVENTS' sticky GO bit is set, else `event`.

So in M1: a `GO` line at C or a `CMD` line at C wakes a WFI at C (GO always under the gate and when the mask holds it;
CMD when the mask holds [9] and the gate is open: `wfi_cmd`).

## 3. The register-block subset

Addresses `0xFFFFF800 + offset`. Loads are 32-bit only (a sub-word load is a bus error), 2 cycles, sampling in the
second. Stores are 32-bit only (a sub-word store is a bus error), posted, effective at the end of their cycle.

| reg (offset) | M1 use | load returns (sampling at t) | store at t (effect at the end of t) |
|---|---|---|---|
| PARAMS (0x000) | boot ROM, `hello`, every `tile_index()` | constant: `periph_present` [2:0] = 1, the tile index [26:20], big-SRAM size [31:27] = `mem_chunks / 65536 - 1` = 15: tile k reads `0x78000001 | k << 20` | read-only: bus error |
| STATUS (0x004) | boot ROM handler (report 25), `wfi_cmd`, `wfi_go`, `timers`, `reqfull` | `status` computed at the end of t - 1: [4] done, [5] command landed, [6] request busy (the request FIFO full); [0] RD_RUNNING, [1] RD_DONE, [2] GATHER_BUSY, [3] SEND_BUSY are 0 in M1 | read-only: **bus error** (`faults` 0, `irq`) |
| ERRORS (0x008) | handlers (boot ROM's, `csr`'s, `irq`'s) | `errorsAt(t)` (section 1): [9] bus error, [10] illegal instruction in M1 | read-only: bus error |
| PULSE_DONE (0x00C) | boot ROM (tile branch), `wfi_go` | 0 (write-only) | bit 0 set: `done` = 1, **the done gate closes**, counters done [7] and the per-counter events [6:0] clear; READY rises on the rising edge of `done && landed` (to C&C: its effect returns as a replayed `GO`) |
| EVENTS (0x018) | boot ROM, `wfi_cmd`, `wfi_go` | `eventsAt(t)`; **consumes** the sticky bits it returned that are under EVENTS_MASK, at the end of t (a bit set during t survives) | read-only: bus error |
| EVENTS_PEEK (0x01C) | `wfi_cmd`, `wfi_go`, `reqfull` | `eventsAt(t)`, consuming nothing | bus error |
| EVENTS_MASK (0x020) | `wfi_cmd` | the mask | `mask = v & 0xFFFF` (the wake term from t + 1) |
| REQUEST_SET_W0-W2 (0x048-0x050) | boot ROM handler, `reqfull` | 0 (write-only) | the request port's words; cleared by every push |
| REQUEST_SEND (0x054) | boot ROM handler (2 reports), `reqfull` (190) | 0 | a push (section 4); **stalls while the FIFO can't take it** |
| SIM_EXIT (0x1F8) | crt0 (`main`'s return), every M1 program | 0 | **the core halts at t** (`HALT 5` after the store's retire line; `haltWith` is called inside the store) |

EVENTS bits seen in M1: [8] GO, [9] command landed, [7] counters done (never set in M1: no counter is written), [11]
request room (set at the end of a cycle in which the FIFO stops being full: `!busy(t) && busy(t - 1)`; never set in the M1
runs, `reqfull`'s FIFO is still full when it ends), [10] run done (never set in M1). Write-only words read 0; reserved
words read 0 and ignore stores (a violation in ref, not a behaviour).

**Not touched in M1**: EVENTS_REQUIRED, TRACE, PULSE_DMA, the write counters, ROUTER_HDR/ADDR/SET_W0-W2/SEND_32-128 (except
`faults` 2's sub-word store to ROUTER_SET_W1, a bus error before any effect), DMA_*, SRAM_RD_*, REQUEST_GET_*,
REQUEST_RECEIVE, the peripheral windows (beyond the address decode). M2 brings them.

**Sequencing in `endCycle(t)`** (what wins when two things land in the same cycle): 1. GO seen at t: `done`, `landed`,
`gate` cleared, EVENTS [8] set; 2. the controller's store of t (so a `PULSE_DONE` at the GO's cycle leaves `done` and the
gate set); 3. a command landing at t: `landed`, EVENTS [9] set (so it survives a GO in the same cycle); 4. new conditions
(ERRORS); READY's edge; 5. a consuming EVENTS load's clear; the edge events ([10], [11], [7:0]); a clear beats a set in the
same cycle; 6. STATUS for t + 1.

## 4. The request FIFO (REQUEST_SEND) and the bench's `REQ-FREED`

- **Depth 128** (`request_fifo_depth`). Each entry is one 128-bit request (W0-W2 and the stored word); the bus drains it
  as 8 beats of 16 bits, one a cycle at best, so a lone tile frees at most one entry per 8 cycles.
- **A `REQUEST_SEND` store at t counts as a push at the end of t** (`TileControl.apply` calls `RequestPort.send` in
  `endCycle(t)`); the bus can take its first beat at t + 1.
- **It is accepted at t iff `canPush(t)`: `size < 128 || freedAt == t`.** The bus runs before the tiles in every cycle, so
  an entry it frees at t (`REQ-FREED` C = t) is already gone when the store at t asks: **a free and a push in the same
  cycle both happen, and the push is allowed even if the FIFO was full at the end of t - 1** (`reqfull`: freed at 398,
  the store accepted at 398).
- Refused: the store does nothing (no retire line), writes `REQ-REFUSED`, and is re-executed at t + 1. It is not `stall`
  (section 1: an interrupt can preempt it).
- STATUS [6] (request busy) for a load sampling at t + 1 = `size >= 128` at the end of t, after t's free and t's push.
  EVENTS [11] (request room) sets at the end of t when busy falls (`!busy(t) && busy(t - 1)`).
- **The bench's model**: a count `n` (0 at reset). At cycle t: if `REQ-FREED` at t, `n--`; the core's `REQUEST_SEND` at t
  is accepted iff `n < 128`; at the end of t, an accepted push makes `n++`; then busy = `n >= 128`, room on its fall.
  Refusals, STATUS [6] and EVENTS [11] all follow; `REQ-REFUSED` is the check.
- In M1: the boot ROM handler's two reports (never refused) and `reqfull`: 118 refused cycles in 20 stalls (394-397, then
  6 each, 400-405 ... 544-549), one accepted send every 8 cycles once full.

## 5. Commands, GO and the command block

- A command is the director's mode-0 writes into data memory through port B: its arguments into the argument block (even
  commands slots 1-7, `0x7FC4`-`0x7FDC`; odd 8-15, `0x7FE0`-`0x7FFC`), then the command word into slot 0 (`0x7FC0`), last.
  Each write at t is a `MEMW` at t + 1: **a load the core executes at t + 1 sees it**.
- **Command landed**: the write covering slot 0 at t sets `landed` and EVENTS [9] at the end of t (`CMD` at t + 1), and
  STATUS [5] for t + 1. GO clears `landed` (not EVENTS [9]: the boot ROM's tile branch never consumes it, and a program
  that wants it must, `wfi_cmd`).
- **GO**: seen at t (C&C's store at s, `go_latency` + the tile's skew later) clears `done`, `landed` and the gate and sets
  EVENTS [8] at the end of t (`GO` at t + 1).
- **Replayed by the C++ test bench**: `MEMW`, `CMD`, `GO` (at the trace's cycles), and `REQ-FREED` (section 4). That is
  all that comes from outside the controller in M1. The bench need not model why they happen (C&C, READY, the mesh, the
  request tree); the latencies that set their cycles are in the trace's header only so a mismatched golden is refused.
- **Derived by the C++ controller and its register-block subset**: everything else: every retire line, `WAKE`, `TRAP`,
  `HALT`, `REQ-REFUSED`, the `UB` stop, STATUS, ERRORS, EVENTS and its consumes, the done gate, READY's edge (not
  traced), PARAMS, `mcycle`/`mtime`, the closing block.

## 6. The controller's own couplings (inside the controller, listed for completeness)

- **Reset** (`Controller.reset`, `TileControl.reset`): `pc = 0x02000000`, x1-x31 = 0, every CSR 0, `mcycle` 0; EVENTS 0,
  the mask `0x180`, `done`/`landed`/gate clear. The images are loaded before cycle 1 (the C++ side loads them directly).
- **Fetch**: program memory (`0x01xxxxxx`, word aligned) or the boot ROM (`0x02000000`, 64 words); anything else stops
  (`HALT 6`). No fetch timing beyond the instruction's.
- **Timing**: one cycle per instruction; a register-block, window or table-window load 2 (`reg_load_latency`, sampling in
  the second); a taken branch, `jal`, `jalr` +1 (`branch_penalty`); a multiply (`mul`, `mulh`, `mulhsu`, `mulhu`) 3 in all
  (`mul_latency`); data-memory and boot-ROM loads 1; stores 1, posted.
- **Counters**: `mcycle` is incremented at the start of every cycle, so a read at t returns t (no writes); a write at t sets
  it after that cycle's increment, so a read at t + 1 returns the value + 1 (`timers`); `mcycleh` the high half.
  `mtime = mcycle / ref_cycle` (40), `mtimeh` its high half: read-only, **a write to `mtime`/`mtimeh` is an illegal
  instruction** (`csrWrite` refuses them; `csrr`, i.e. `csrrs` with `rs1 = x0`, doesn't write). The CSRs: `mstatus`
  keeps MIE [3] and MPIE [7], reads add MPP 3 [12:11]; `mie` keeps MEIE [11]; `mtvec` direct only (low 2 bits 0); `mepc`
  low 2 bits 0; `mscratch`, `mcause`, `mtval` all bits; `misa` reads `0x40001100`, writes ignored; `mip` section 1. Any
  other CSR (e.g. `cycle` 0xC00, `faults` 4, `csr`) is an illegal instruction.
- **The address decode and bus errors** (each: ERRORS [9] at the end of t, a trap at t with `mtval` = the address,
  `mcause` 7 for a store, 5 for a load): a store to program memory (`faults` 1, `csr-store`) or the boot ROM; an unmapped
  region (`faults` 7, `csr-load`: `0x30000000`); a sub-word access to the register block (`faults` 2), a window or the
  table window; a store to a read-only register (`faults` 0, `irq`); a load from PTR, STREAM or GROUP (`faults` 6: the
  table window's decode, `0xFFFF2400` and up). **The table window in M1 is only that decode**: HIST loads and stores (the
  window's only legal use) and the window's busy checks are not exercised.
- **Illegal instructions** (ERRORS [10], `mcause` 2, `mtval` = the instruction word): the divide opcodes (riscv-tests
  `div`, `divu`, `rem`, `remu`; `faults` 3), an unimplemented CSR, a write to a read-only CSR that refuses it, anything
  else undecoded.
- **Traps**: terminal; to `mtvec` (the boot ROM's handler, `0x02000078`, unless a program sets its own); a synchronous trap
  inside a handler stops the core (`HALT 8`); `ecall` and `ebreak` stop it (`HALT 2`, `3`; `faults` 5); a misaligned jump
  target stops it (`HALT 7`); `mret` is legal (a violation in ref) and not used in M1.
- **Undefined behaviour**: a misaligned load or store stops the run (`UB` line; `misalign_ld` tile 9, `misalign_st` tile
  12), before any effect.
- **`parked`**: the trace ends when a handler executes `j .` (a `jal` to itself) or a WFI with no interrupt pending.

## 7. The `Config` values the controller's behaviour depends on (the D12 header)

| value | how |
|---|---|
| `reg_load_latency` (2), `branch_penalty` (1), `mul_latency` (3) | the controller's timing (section 6) |
| `ref_cycle` (40) | `mtime` |
| `done_gate` (true) | the wake term (section 2) |
| `request_fifo_depth` (128) | `canPush`, STATUS [6], EVENTS [11] (section 4) |
| `mem_chunks` (2^20), `periph_type` (4), the tile index | PARAMS |
| `go_latency` (8), `ready_latency` (8), `start_skew`, `cnc_reaction` (1), `cnc_go_settle` (true), `link_latency` (3), `intra_tile_latency` (1), `cm_latency` (2), `queue_depth` (16) | the cycles of the replayed `GO`, `CMD` and `MEMW` (not the controller's behaviour) |
| `req_hop_latency` (3), `cnc_drain_reports` (false) | the cycles of the replayed `REQ-FREED` |
| `error_latency` (8), `stage_delay` (64), `K` (8), `cc_cores` (false) | nothing in M1 (the error line to C&C, C&C's staging, the write counters' delay, C&C as cores); recorded per D12 |
| `idle_skip`, `shuffle_seed` | nothing: a correct trace is the same either way (checked for `idle_skip`) |

No other `Config` value reaches the controller or the subset above in M1.
