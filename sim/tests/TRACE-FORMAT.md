# The retire trace (M1)

The format both sides write for one tile's controller: `ref` (v1.3's simulator with the trace hooks, outside the frozen copy) through its observer (`Observer`,
driven by the oracle's `TraceRun`, outside the frozen copy) and the C++ controller with its test bench. M1 is accepted when the two traces of a
program are equal, line for line, from reset to the end (DECISIONS D3). The golden traces are in `tests/golden/`
(the oracle's golden script makes and checks them).

Plain text, one line per event, fields separated by one space. Numbers are hexadecimal, lowercase, 8 digits
(`%08x`), except cycles, register numbers and access sizes, which are decimal. A trace has three parts: the header (`#`
lines), the body (one line per event, in cycle order), and the closing block (`END`, `REGS`, `CSRS`, `DMEM`).

## Cycles

Cycles are `ref`'s chip cycles: the first cycle run is 1 (`Chip.step` runs `++cycle`). The controller's `mcycle` counts
every cycle from reset, so with no write to it `mcycle` read at cycle C is C.

**A line's cycle is the cycle in which ref's `step()` completes the instruction** (for a retire line), or the cycle the
event takes effect as defined below. The cases that follow from that one rule:

| case | the line's cycle |
|---|---|
| a one-cycle instruction | its cycle |
| a load from the register block or a peripheral window (`reg_load_latency` 2; addresses from `0xFFFFF800`, `Registers.inRegs`; a table-window load, `0xFFFF2000`-`0xFFFF35FF`, takes 1 cycle) | the **second** cycle, in which it samples; the first cycle writes nothing |
| a multiply (`mul_latency` 3) | its issue cycle; the next 2 cycles write nothing |
| a taken branch, `jal`, `jalr` (`branch_penalty` 1) | its issue cycle; the next cycle writes nothing |
| a `REQUEST_SEND` store refused by a full request FIFO | each refused cycle writes `REQ-REFUSED`; the accepting cycle writes the store's retire line |
| `wfi` | the cycle it executes (a retire line). If the wake term holds then, it falls straight through and nothing else is written; otherwise the core sleeps and the cycle it leaves is a `WAKE` line |
| a trap (synchronous) | the cycle the faulting instruction executes: a `TRAP` line and no retire line for it |
| a trap (the external interrupt) | the cycle it is taken, in place of anything the core would have done that cycle |
| a halt | the cycle of the halting instruction: `SIM_EXIT`'s store writes its retire line, then `HALT`; `ecall`, `ebreak`, a fetch fault, a misaligned jump and a trap inside the handler write only `HALT` |

Cycles in which the controller does nothing visible (a stall, the first cycle of a register load, a cycle asleep in WFI,
cycles after a halt) write no line.

## The body

**Retire**: `C PC INSN [xN=VAL] [ld|st ADDR SIZE VAL]`
- `PC`, `INSN`: the instruction's address and word.
- `xN=VAL`: present when the instruction writes a register other than `x0` (`N` decimal, 1-31; `VAL` the value written):
  loads, ALU operations, `lui`, `auipc`, the links of `jal`/`jalr`, CSR instructions (the CSR's old value).
- `ld ADDR SIZE VAL`: a load. `ADDR` is the effective address (`rs1 + imm`), `SIZE` 1, 2 or 4, `VAL` the bytes loaded,
  zero-extended (before `lb`/`lh` sign-extend them; the sign-extended value is in `xN=`). Present for every load, `rd = x0`
  included, from any region: data and program memory, the boot ROM, the register block, the windows.
- `st ADDR SIZE VAL`: a store; `VAL` is `rs2`'s low `SIZE` bytes. Every store, to any region, `SIM_EXIT` and
  `REQUEST_SEND` included.
- The operands are as read, before the instruction writes `rd`: `lw x5, 0(x5)` logs the address from the old `x5`.
- A CSR write is not a field: it shows in later reads and in the closing block.

**Events** (the test bench's, and the controller's own):

| line | meaning | replayed by the C++ test bench? |
|---|---|---|
| `C MEMW ADDR VAL` | the traffic director wrote word `VAL` at byte address `ADDR` of the tile's data memory (`0000xxxx`) or program memory (`0100xxxx`) through port B, at C - 1. **C is the first cycle the core can see it: a load the core executes at C returns `VAL`** (`ref`'s memories return the contents as of the start of the cycle and commit writes at its end). One line per word: a 64- or 128-bit packet writes 2 or 4 lines, lowest address first. In M1: the words of a command (its arguments, then slot 0, `0x7FC0`) | yes |
| `C GO` | GO reached the tile at C - 1 (`TileControl.endCycle`): from C, EVENTS [8] is set, `done`, `landed` and the done gate are clear, and a WFI or an `EVENTS` load sees it | yes |
| `C CMD` | a port-B write covering command slot 0 landed at C - 1 (command landed): from C, EVENTS [9] and STATUS [5] are set. The slot-0 word itself is the `MEMW` line of the same C | yes |
| `C REQ-FREED` | the request bus freed the request FIFO's head entry at C. The bus runs before the tile in a cycle, so a `REQUEST_SEND` store at C already finds the room | yes |
| `C REQ-REFUSED` | a `REQUEST_SEND` store at C found the request FIFO full with no entry freed at C; the core retries at C + 1 | no: a check (it follows from the pushes and `REQ-FREED`) |
| `C WAKE go` / `C WAKE event` | a WFI that slept is left at C: the wake term holds at C, from GO or from another event (`TileControl.lastWake`). The instruction after the WFI may complete in the same C | no |
| `C TRAP mcause=M mepc=E mtval=V pc=H` | a trap taken at C, with the CSRs as set and `H` the handler's first instruction (`mtvec & ~3`); `mcause` `8000000b` is the external interrupt | no |
| `C HALT R pc=P` | the core stops at C: `R` is `Controller`'s halt reason (2 `ecall`, 3 `ebreak`, 5 `SIM_EXIT`, 6 a fetch outside program memory and the boot ROM, 7 a misaligned jump target, 8 a trap inside the handler); `P` the halting instruction's address | no |
| `C UB UNIT: WHAT` | `ref` stopped at C on undefined behavior (`UndefinedBehavior`: in M1 a misaligned load or store), written by the driver; `WHAT` is ref's description (e.g. `a misaligned 4-byte load from 0x102 at pc 0x1000050`) | no |

**Order within a cycle C**: `MEMW` (in address order), `GO`, `CMD`, then `WAKE`, `REQ-REFUSED`, `TRAP` or the retire
line, then `HALT`, then `REQ-FREED`. `ref` writes the lines labelled C (`MEMW`, `GO`, `CMD`) at the end of C - 1, after every unit
of the tile has run C - 1, and `REQ-FREED` for C at the end of C; the order above is the order in the file, which the
C++ side writes the same way. A trace's cycles never decrease.

## The header (D12)

The configuration the trace was made under; the C++ test bench checks its own against it before comparing and refuses on
any difference. Lines in this order:

```
# meshsim retire trace v1 (sim/tests/TRACE-FORMAT.md)
# program NAME
# tile K
# image code SHA256 data SHA256|none data_at ADDR entry ADDR
# bootrom SHA256
# config KEY VALUE        (one line per key, the keys below, in this order)
# tb STEP                 (the test-bench schedule's steps, if any: informative; the bench replays the events, not these)
```

- `image`: the sha256 of the program-memory image (`.code.bin`, loaded from `0x01000000`) and the data-memory image
  (`.data.bin`, loaded at `data_at`); `entry` is the boot command's jump target (its slot 1).
- `bootrom`: the sha256 of the boot ROM's words (`fw/boot/bootrom.hex`, little-endian bytes).
- `config`: the values the controller's behaviour, or the cycles of what the test bench replays, depend on
  (`AUDIT-M1.md` says which matters how):

| key | ref (`Config`, `Controller`) | default |
|---|---|---|
| `reg_load_latency` | `regLoadLatency` | 2 |
| `branch_penalty` | `Controller.branchPenalty` | 1 |
| `mul_latency` | `Controller.mulLatency` | 3 |
| `ref_cycle` | `refCycle` (mtime = mcycle / ref_cycle) | 40 |
| `go_latency` | `goLatency` | 8 |
| `ready_latency` | `readyLatency` | 8 |
| `error_latency` | `errorLatency()` | 8 |
| `start_skew` | `Chip.startSkew[tile]` (GO arrives go_latency + skew after C&C's store) | 0 or 1 |
| `stage_delay` | `stageDelay` | 64 |
| `cnc_reaction` | `cncReaction` | 1 |
| `cnc_drain_reports` | `CnC.drainReports` (C&C 1 pops its request queue) | false |
| `cnc_go_settle` | `cncGoSettle` | true |
| `done_gate` | `doneGate` | true |
| `request_fifo_depth` | `requestFifoDepth` | 128 |
| `req_hop_latency` | `reqHopLatency` | 3 |
| `link_latency` | `linkLatency` | 3 |
| `intra_tile_latency` | `intraTileLatency` | 1 |
| `cm_latency` | `cmLatency` | 2 |
| `queue_depth` | `queueDepth` | 16 |
| `K` | `K` | 8 |
| `mem_chunks` | `memChunks` (PARAMS' size field) | 1048576 |
| `periph_type` | `periphTypeOf(tile)` (PARAMS' peripheral bits) | 4 |
| `cc_cores` | `ccCores` | false |
| `idle_skip` | `idleSkip` (`-Dmeshsim.skip`) | true |
| `shuffle_seed` | `shuffleSeed` | -1 |

`idle_skip` and `shuffle_seed` don't change a correct trace (every golden trace's body is the same with `idle_skip`
false; the oracle's golden script with `JOPTS=-Dmeshsim.skip=false`); they are recorded so that a difference can be told apart.

## The closing block

```
END HOW cycle C
REGS X1 X2 ... X31
CSRS pc PC mstatus V mie V mtvec V mscratch V mepc V mcause V mtval V mcycle V64
DMEM SHA256
```

- `HOW`: `halt R` (R as in `HALT`), `parked` (a trap handler reached its park loop, `j .` or a WFI with no interrupt
  pending: `Controller.parked`), `ub`, or `timeout` (the schedule's `max` cycles ran out). `C` is the last cycle run: the
  cycle of the halt, of the instruction that parked, or of the UB. Nothing runs after it.
- `REGS`: x1-x31 at the end.
- `CSRS`: the architectural values as stored (`mstatus` holds only MIE [3] and MPIE [7]; a read adds MPP = 3 at [12:11]),
  `pc` the next instruction's address, `mcycle` 64 bits (16 digits).
- `DMEM`: the sha256 of the whole 32 KB data memory, words from address 0, each little-endian.
