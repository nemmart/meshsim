# The RV32 core: specification, v1.36

This is the contract of the RV32 processor in every tile, the **core**: its
ISA, its ports, its CSRs, its traps, its `WFI` and its lockstep regions.
Chapter 6 of `spec/machine-spec.md` states what the machine requires of it, and
this document is the core's own side. It is the core contract of the RTL
(Snarky One), which meshsim's core model follows.

How it is read:
- **Scope.** The core, and `rv32_core`: the core with its data-side address
  decoder (`data_decoder`) and its check of fetch addresses, the standalone
  core whose ports its tile serves. Memories (IMEM, DMEM and the boot ROM among
  them, the tile's `core` module's: `spec/machine-spec.md` 2.3 and 6.3), devices,
  the tile, the NoC and the accelerator are outside it. "The enclosing unit" is
  whatever design serves the core's ports: in the machine, the tile.
- **Section numbers.** A bare section number, as in "(7.3)", is
  `spec/machine-spec.md`'s. This document's own sections are cited by title, as
  in "Lockstep regions".
- **The core has no memory map.** Its windows are parameters, which the machine
  sets to its own map (5.1).
- **Cycle counts.** The contract states the core's timing where the machine
  relies on it. The costs the simulator reproduces are gathered in the last
  section, "The core's cycle costs".
- *Open* marks a subject still to be decided (`spec/machine-spec.md`, chapter 26).

## Core contract

| Area | Contract |
| --- | --- |
| ISA | RV32I + C + Zmmul + Zicsr + Zicond; firmware spelling `rv32ic_zicond_zicsr_zmmul`, ABI `ilp32`; custom-0 and custom-1 instructions go to a coprocessor (Coprocessor) |
| Privilege | Machine mode only; precise synchronous exceptions, machine interrupts, MRET |
| Pipeline | Four logical stages: fetch, decode, execute, writeback; single issue, in-order retirement |
| Registers | 32 architectural GPRs; x0 always reads zero; one architectural write port |
| Fetch | Separate read-only port; a fetch returns every word an instruction starting at its address can occupy: one at a word address, two at an odd halfword; fetch runs on in aligned words, so only a redirect's first fetch can be at an odd halfword; 16/32-bit instructions, including cross-word instructions |
| Control transfers | No branch prediction: fetch runs on sequentially until it is redirected, and nothing in the core remembers earlier control flow. Execute resolves branches and JALRs; a taken branch or any other JALR redirects fetch in the next cycle, and the instruction fetched behind it is cancelled. A JAL, and a JALR through x1 (a return) whose x1 is ready (committed: in its last cycle in decode, no instruction in execute or writeback writes x1), need no execute stage: decode computes the target, and fetch is redirected as the jump enters execute. With memory that answers the next cycle and no other stall, the next instruction retires 1 cycle after a branch that is not taken, 4 cycles after a taken branch or any other JALR, and 4 cycles after a JAL or ready return enters execute: 3 cycles after it retires, or fewer if it waited in execute, and at least 1, whatever the target's instruction and its alignment: a redirect's first fetch is at the target and returns every word the instruction there occupies, so none needs a second response. These costs depend on the instructions around the transfer, never on earlier control flow |
| Data | Separate byte-addressed 32-bit request/response port with four byte enables; one outstanding operation. The next request may be raised in the cycle the previous response arrives: with memory that answers the next cycle and no other stall, back-to-back loads and stores retire one per cycle, and an instruction that uses a load's result right after it waits one cycle |
| Coprocessor | When the `coprocessor_present` input is 1, every 32-bit instruction with the custom-0 (`0001011`) or custom-1 (`0101011`) opcode goes to a coprocessor through a separate request/response port; when it is 0, those instructions are illegal. The core reads them as R-type: it reads rs1 and rs2, sends the instruction word with their values, and writes the response's word to rd (x0 discards it). funct3, funct7 and the choice of opcode are the coprocessor's to define. The request is raised as the instruction enters writeback, and the instruction holds writeback until the response, as a load does; one operation is outstanding on the data and coprocessor ports together, and the next request on either may be raised in the cycle the response arrives. With a coprocessor that is always ready and answers the next cycle, and no other stall, back-to-back coprocessor instructions retire one per cycle, and an instruction that uses one's result right after it waits one cycle. An error response is an illegal-instruction exception |
| Memory ordering | No posted stores; completion response for both loads and stores; ordinary FENCE drains preceding transactions; data and coprocessor requests are raised in program order, each after the previous one's response |
| Alignment | Naturally aligned byte/halfword/word data accesses; trap misaligned halfword/word accesses without issuing a transaction |
| Multiply | All four Zmmul operations; two pipelined stages (execute: the four products of the operands' 16-bit halves, the high halves sign-extended for signed operands, each at most 17×17 signed; writeback: their sum, with no correction terms). Operands are not forwarded into it: a multiply whose operand the instruction in writeback is still producing waits one cycle, as does an instruction that uses a multiply's result right after it; other instructions are not delayed |
| Conditional zero | Zicond's CZERO.EQZ (rd = 0 if rs2 = 0, else rs1) and CZERO.NEZ (rd = 0 if rs2 ≠ 0, else rs1) are ALU instructions: one cycle in execute whatever the operand values, their result forwarded like any ALU result. They let a program make a data-dependent choice without a branch |
| Interrupts | Two: the machine external interrupt, `external_interrupt` (MEIP, bit 11 of `mip` and `mie`), which also releases lockstep barriers ([Lockstep regions](#lockstep-regions)), and `nmi`, the non-maskable interrupt (NMI): taken whatever `mstatus.MIE` and `mie` hold, and only by a core that has taken no trap since reset ([Traps](#traps)); both synchronous to the core clock; taken at precise instruction boundaries. The tile drives `external_interrupt` with MEIP, the tile's wake, and `nmi` with the tile's errors (6.2). There is no software, timer or platform interrupt |
| Reset | Rising-edge, synchronous active-low; reset PC and hart ID supplied by the enclosing tile, the hart ID stable through reset and the first cycle after it, the reset PC from the last cycle of reset through the first cycle after it; `mtvec`'s reset value a parameter, `RESET_TRAP_VECTOR` ([CSRs](#csrs)) |
| WFI | Retires, then the core executes nothing until an interrupt is pending and enabled in `mie` (`(mip & mie) != 0`), whatever `mstatus.MIE`, or the NMI can be taken; fetch stops once its buffer is full, and `wfi_sleeping` reports the wait so that the tile can gate the clock (see [Wait for interrupt](#wait-for-interrupt-wfi)) |
| Status | `core_status`, the machine's interface (3) to the tile control unit: `canonical_wait`, a level from a register, 1 in each cycle after a cycle of the canonical wait ([Lockstep regions](#lockstep-regions)), and `access_fault` and `illegal_instruction`, one-cycle pulses from registers, in the cycle after the core takes an access fault (instruction, load or store) or an illegal-instruction exception ([Core ports](#core-ports)) |
| Lockstep | Nothing in the core remembers earlier control flow or timing, and operand values reach timing only through branch outcomes and jump targets: cores that run the same code from a barrier released in the same cycle, under the region's rules, retire identically, cycle for cycle ([Lockstep regions](#lockstep-regions)) |
| Excluded | DIV/REM, A, floating point, vector, S/U modes, MMU, PMP, hardware debug, caches, Zifencei |

### Core ports

| Group | Signal | Direction | Meaning |
| --- | --- | --- | --- |
| Clock/reset | `clk`, `rst` | in | Rising edge; synchronous active-low reset |
| Configuration | `reset_pc[31:0]` | in | First fetch address after reset; bit 0 ignored. Fetch loads it in the first cycle after reset, so it holds its value from the last cycle of reset through that cycle; earlier in reset it may change, since no target accepts a request then |
| | `hart_id[31:0]` | in | Value of `mhartid` |
| | `coprocessor_present` | in | 1: custom-0 and custom-1 instructions go to the coprocessor port; 0: they are illegal instructions, and the port raises no request (Coprocessor) |
| Instruction request | `instruction_request_valid` | out | Core wants the words an instruction at the address can occupy |
| | `instruction_request_ready` | in | Memory accepts the request this cycle |
| | `instruction_request_address[31:0]` | out | Fetch address, bit 0 zero: the address's word, and at an odd halfword (bit 1 set) the next word too, the two a 32-bit instruction there spans. Only the first fetch after a redirect to an odd halfword is at one ([Memory and reset interface details](#memory-and-reset-interface-details)) |
| Instruction response | `instruction_response_valid` | in | Response to the one accepted fetch |
| | `instruction_response_data[63:0]` | in | The words, little-endian: [31:0] the address's word, [63:32] the next word at an odd halfword (ignored at a word address) |
| | `instruction_response_error[1:0]` | in | Per word, in the same order: bit 0 the address's word failed, bit 1 the next word failed (ignored at a word address). A failed word's data is ignored |
| Data request | `data_request_valid` | out | Core wants a load or store |
| | `data_request_ready` | in | Memory accepts the request this cycle |
| | `data_request_address[31:0]` | out | Byte address, naturally aligned for the access size |
| | `data_request_write` | out | 1 = store, 0 = load |
| | `data_request_byte_enable[3:0]` | out | Active byte lanes |
| | `data_request_write_data[31:0]` | out | Store data, already placed in its byte lanes |
| Data response | `data_response_valid` | in | Completion of the one accepted load or store |
| | `data_response_read_data[31:0]` | in | Full word for loads; the core extracts the lanes |
| | `data_response_error` | in | Access failed; a failed store has no effect at the target |
| Coprocessor request | `coprocessor_request_valid` | out | Core sends a coprocessor instruction |
| | `coprocessor_request_ready` | in | The coprocessor accepts the request this cycle |
| | `coprocessor_request_instruction[31:0]` | out | The instruction word |
| | `coprocessor_request_source1[31:0]` | out | Value of register rs1 |
| | `coprocessor_request_source2[31:0]` | out | Value of register rs2 |
| Coprocessor response | `coprocessor_response_valid` | in | Completion of the one accepted coprocessor instruction |
| | `coprocessor_response_data[31:0]` | in | The value written to rd |
| | `coprocessor_response_error` | in | The coprocessor rejected the instruction and did nothing for it: an illegal-instruction exception |
| Interrupts | `external_interrupt` | in | The machine external interrupt, MEIP (`mip`/`mie` bit 11): a level-sensitive pending input, synchronous to `clk` (the tile synchronizes it); it carries a tile's barrier release ([Lockstep regions](#lockstep-regions)) |
| | `nmi` | in | The NMI: level-sensitive, synchronous to `clk`; it has no bit in `mip` or `mie` ([Traps](#traps)) |
| Wait for interrupt | `wfi_sleeping` | out | From a register: 1 from the cycle after a WFI retires until the cycle after the wake condition first holds (`(mip & mie) != 0`, or the NMI can be taken), while the core waits ([Wait for interrupt](#wait-for-interrupt-wfi)); never 1 when that condition holds as the WFI retires. Meanwhile the data and coprocessor ports are idle and the instruction port makes at most the requests that fill fetch's buffer. For the tile's clock gating; the core gates nothing itself |
| Status | `core_status_canonical_wait` | out | From a register: 1 in the cycle after each cycle in which `wfi_sleeping` is 1, `instruction_request_valid` is 0 and no fetch is outstanding (one accepted at an earlier edge whose response has not arrived before that cycle): the canonical wait ([Lockstep regions](#lockstep-regions)) a cycle late. Since that wait lasts until the wake, it is 1 from the cycle after the wait begins with fetch idle through the first cycle in which `wfi_sleeping` is 0 again |
| | `core_status_access_fault` | out | From a register: 1 for the one cycle after a cycle in which the core takes an instruction, load or store access fault ([Traps](#traps)), 0 otherwise |
| | `core_status_illegal_instruction` | out | From a register: 1 for the one cycle after a cycle in which the core takes an illegal-instruction exception, 0 otherwise |
| Observation | `rvfi_*`, `trap_*` | out | For the RTL's testbenches and formal checks, and no part of the machine: an enclosing design leaves them unconnected. `rvfi_*` is the RISC-V Formal Interface: one record per instruction leaving writeback, completed or trapped with an exception (an instruction an interrupt is taken in place of has none); `trap_*` reports each trap taken, exceptions and interrupts |

The three ports use the same handshake:

- A request is accepted on a rising edge where `valid` and `ready` are both 1. The
  core holds `valid` and every request field stable until acceptance.
- Each accepted request receives exactly one response, no earlier than the cycle
  after acceptance. There is no response back-pressure: the core only issues a
  request when it can accept the response.
- At most one request is outstanding on the instruction port, and one on the data
  and coprocessor ports together. A redirect cannot cancel an accepted
  fetch; the core discards that response, and requests the new address no earlier
  than the cycle it arrives (in the redirect cycle itself if the port is free).
- Instruction requests are speculative: fetch runs ahead sequentially, and discards
  what a taken branch or jump, a trap or MRET later redirects away from.
- Data and coprocessor requests are never speculative. An older instruction that
  traps prevents any younger instruction from issuing a data or coprocessor
  request.

### Memory and reset interface details

- The core has no memory map: it does not bake IMEM/DMEM sizes, bases or
  permissions into its ports. Address decoding is in `data_decoder`, and the
  check of fetch addresses is in `rv32_core` itself (below), both outside the
  core. IMEM and the boot ROM serve fetches only: the core neither reads nor
  writes them as data, and every data access to either faults. The map is the
  enclosing unit's, given to `rv32_core` as its parameters `IMEM_BASE`,
  `BOOT_ROM_BASE` and `BOOT_ROM_BYTES`, `DMEM_BASE`, `SYSTEM_BASE` and
  `SYSTEM_BYTES`, with the configured `IMEM_BYTES` and `DMEM_BYTES` as IMEM's
  and DMEM's sizes. The machine gives its own map (5.1): DMEM, IMEM and the
  boot ROM in regions 0x00, 0x01 and 0x02, the boot ROM's first word the reset
  PC, and the system window region 0xFF whole, which `register_bus` decodes.
- Core reset also resets its CPU-facing transaction channels; it does not reset
  memory contents or the agents that write IMEM. A core-only reset with live
  channels requires prior quiescence/draining by the enclosing unit;
  accepted stores cannot be retroactively undone.
- IMEM may be written while the core runs, as when the master tile loads an
  image while the core waits in the boot ROM (6.5). The
  core has no instruction-coherence instruction: Zifencei is excluded, and
  FENCE.I raises an illegal-instruction exception. A redirect restarts fetch
  instead: a taken branch, JAL or JALR, an exception, an interrupt, MRET, or
  reset. Its redirect cycle, J, is the cycle in which the redirecting
  instruction leaves writeback (retiring, or trapping with an exception), the
  cycle in which an interrupt is taken, or the first cycle after reset; in the
  RTL's tests, `rvfi_valid` (the instruction's record) or `trap_valid` (an interrupt)
  marks it. A JAL, and a return whose x1 is ready (Control transfers), redirect
  earlier: their J is the cycle they enter execute, before their record and
  later than the record two before theirs. In J the core discards every
  instruction word fetched or requested before J other than the redirecting
  instruction's own: fetch's queue, the instructions in decode and execute, and
  the response to any earlier request, whenever that comes. Every instruction
  executed after the redirect comes from fetches raised in J or later: first
  the target's (`reset_pc`'s after reset), which returns every word an
  instruction there can occupy (the next word too when the target is an odd
  halfword), then each following word in order, one per fetch, aligned whatever
  the target. The boot, IMEM written while the core is held in reset, is this
  rule's reset case. What IMEM must guarantee in turn is under [Obligations of
  the enclosing unit](#obligations-of-the-enclosing-unit) (IMEM writes).
- Without a redirect the core executes the words it has already fetched: falling
  through into rewritten code, the wake from a WFI, the hold after a CSR write
  and an ordinary FENCE restart nothing. So a word fetched before a write to it
  may execute in its old form, and a 32-bit instruction that spans two words may
  combine halves of different ages; while the core waits after a WFI, fetch
  holds the words up to 16 bytes past the WFI's own ([Wait for
  interrupt](#wait-for-interrupt-wfi)).
- General-purpose registers x1–x31 are not reset; software must initialize them.

### Data-side address decoder

`data_decoder` connects the core's data port to two target ports, each with the
data handshake above: DMEM (reads and writes) and the system port (reads and
writes, any latency, for the tile's register bus, `register_bus`).
IMEM and the boot ROM have no data port: an access to either is in no window of
the decoder and faults. `rv32_core` is the core with this decoder and the check
of fetch addresses below: the standalone core, whose ports the enclosing unit
serves.

- Windows are a base and a size in bytes each. The decoder's two are its
  parameters, which `rv32_core` passes from its own, with IMEM's and the boot
  ROM's windows, which `rv32_core` checks fetches against: IMEM's and DMEM's
  windows, each of its configured size, the boot ROM's and the system window, at
  the bases the enclosing unit gives (in the machine, 5.1's). A size is a nonzero multiple of 4, at
  most 2^31 bytes, and need not be a power of two; the boot ROM's may also be
  0, for no boot ROM, a window that holds no address. A base is a multiple of its
  window's span, the smallest power of two not below its size; this also keeps
  the window inside the 32-bit address space. No two windows share a byte (their
  spans may overlap). `data_decoder` checks these rules for its windows, and `rv32_core` for the
  whole map: an invalid set is a configuration error, which no design is built
  from.
- An address is in a window when its bits above the span equal the base's and
  its offset in the span is below the size. The part of a span past the size does
  not belong to the window and never aliases its storage; an address there
  faults unless another window holds it.
- A request in a window goes to that target alone, with the core's address (the
  byte address, not an offset), write flag, byte enables and write data. An
  address in no window, IMEM's and the boot ROM's included, is accepted at once
  and answered with an
  error the next cycle without reaching any target; the core takes that as an
  access fault.
- Exactly one response per accepted request reaches the core: the one from the
  target the request went to, with its data and error. A target's response while
  it has no request of the decoder's outstanding never reaches the core.
- Nothing is buffered beyond the one outstanding access: the upstream ready is the
  ready of the target the request goes to, or 1 for an address in no window
  (above); a request may be accepted in the cycle the previous response
  arrives, so targets that are always ready and answer the next cycle give one
  access per cycle.
- Timing: address, write flag, byte enables and write data go unmuxed to every
  target that takes them; only each target's valid and the upstream ready depend
  on the decode. The response is a multiplexer selected by a register, written
  from the decode while the request is raised.

### Instruction-side windows

`rv32_core` passes the instruction port through unchanged, except the response's
error: the core sees an error, and so an instruction access fault, for every
fetched word outside both IMEM's window and the boot ROM's, whatever the target
returns: the address's word (error bit 0) and, at an odd halfword, the next (bit
1), which is outside when the address's word is its window's last. IMEM and the
boot ROM need not check fetch addresses, then: memories that take only their
word-address bits will do, and a fetch past the end of either, or in the part of
its span past its size, never runs aliased code. The instruction target (in the
tile's `core`, IMEM and the boot ROM, which both read every fetch) still receives
every fetch the core raises, in a window or not, and answers each one.

- Timing: the compares add no logic to `instruction_request_*`, among the core's
  latest outputs. Their results for the raised request's address are registered
  (enabled by valid, as the decoder's response target is) and join the errors of
  that request's response. The next word's compare needs no adder: it is in a
  window when the address is, and the address's offset in that window is below
  its size less 4.

### Obligations of the enclosing unit

The rules above bind the core and `rv32_core`. A tile, or any design around
them, must in turn guarantee the following.

- Handshake, from the target's side: accept a request only when able to answer
  it, answer each accepted request exactly once, no earlier than the cycle after
  acceptance, and raise no response otherwise. The data and coprocessor sides
  drop a response with nothing outstanding (`data_decoder`, the load/store unit
  and the coprocessor port), but the instruction port does not: fetch takes any
  response it has not marked stale as the answer to its request.
- Reset: reset is synchronous, so in its first cycle the ports still show the
  core's state from before it (a fetch at any address, or a pending load, store
  or coprocessor instruction); from its first edge on, fetch raises a request
  for `reset_pc`'s words in every cycle, and tracks none of them. So a
  target accepts no request while the core is in reset (it holds ready low, or
  is reset with the core), and when reset is released no response is
  outstanding on any port. A core-only reset follows a drain: the enclosing unit
  releases reset only once every port is idle, each accepted request answered.
  An accepted
  store or coprocessor instruction is not undone.
- Latency: any latency is legal, but the core cannot cancel an access, a load,
  store or coprocessor instruction holds writeback until its response, and an
  interrupt waits for it; a target that never answers stops the core. Whatever
  serves the system port bounds its latency and answers with an error an access
  it cannot complete. A lasting condition, such as a
  full queue behind the system port, is reported in a register the program
  reads (the read unit's `READ_FREE`), never by holding ready low.
- Coprocessor: with `coprocessor_present` = 1, the coprocessor port keeps the
  handshake above. Unlike a system port device, the coprocessor may hold ready low
  while it cannot take an instruction (a full queue): the core then waits with
  that instruction in writeback, and an interrupt waits with it, so ready rises
  again in bounded time. An error response means the coprocessor did nothing
  for that instruction; its data is ignored. With `coprocessor_present` = 0 the
  core raises no coprocessor request, and ready and the response inputs may be
  tied to 0. `coprocessor_present` is constant while the core runs. What each
  instruction does, what its response holds, and the coprocessor's latency and
  queueing are the accelerator's (*Open*: 2.2, 6.1).
- Timing at 1 GHz: responses feed the next request in the same cycle, which is
  what gives one access per cycle. A data response's valid or error, and each
  interrupt input, reach the instruction request and every request valid through
  the trap and fetch redirect logic, and a data or coprocessor response's valid
  reaches the data and coprocessor request valids through writeback and the
  load/store unit. So each `*_response_valid`,
  `*_response_error` and interrupt input comes straight from a flip-flop next to
  `rv32_core`, and a target's ready comes from its registered state, never from
  the same cycle's request: the request valids arrive too late in the cycle for
  that. A bridge to a protocol that forbids
  paths from inputs to outputs (OBI, AXI) needs a register slice, a cycle per
  access: acceptable for the system port, not for IMEM or DMEM.
- Instruction port: a design around the bare core, without `rv32_core`,
  faults fetches outside IMEM itself (above).
- IMEM writes, by the tile: the boot while the core is in reset, and kernels
  while it runs (J is the redirect cycle under [Memory and reset interface
  details](#memory-and-reset-interface-details)): the code a redirect
  enters, every word holding any part of an instruction the core executes from J
  until the next redirect, has its last write visible to every fetch accepted at
  the edge that ends J or at a later edge, and is not written from J until that
  next redirect. The first part holds if the tile releases reset only once the
  boot's writes are visible to fetches, and if, for code written while the core
  runs, the core learns that its writes are visible to fetches before J, from a
  notice its writer orders after them (in the tile, the load's `COUNT`, which
  lands after its writes: 6.5 and 14.4), polled or awaited (by the J of a JAL or
  ready return, every older instruction has left execute, so a branch on a polled
  word has its load's response). Any other word may be written at any time, and a fetch of
  a word being written may return any data: under these rules no such word is
  executed. Writes come through IMEM's write port, which has no handshake: a
  write presented in a cycle takes effect at the edge that ends it, visible to
  every fetch accepted at a later edge (the `noc_write` interface, 3; 6.3);
  so a writer knows its write is visible from that edge on.
- DMEM at boot: the core reads a program's constants and initial data only from
  DMEM, which starts dirty and is never cleared by hardware. So the boot
  writes the program's data into DMEM (its read-only and initialized data, and
  zeros over its zero-initialized data, the part of a loadable segment past its
  file bytes) as it writes its code into IMEM, while the core is held in reset,
  and the tile releases reset only once both are visible. In the machine, an
  image's load writes both, and the image is entered only after the load has
  landed (6.5).
- Interrupts: level-sensitive and synchronous to `clk`, `external_interrupt`
  held by its source until the program clears it there, and `nmi` until reset
  (the WFI wake relies on this); `mip` shows `external_interrupt` as MEIP, and
  ignores writes.
- Interrupt sources: the privileged specification asks these of the platform,
  not the hart, and the core takes each input as it comes. `external_interrupt`
  comes from the platform's interrupt controller ("Machine Interrupt
  Registers"). In the machine's tile, the tile control unit
  drives the two: `external_interrupt` is its MEIP, the wake of
  the tile's events, and `nmi` its errors, `ERRORS` non-zero, which only reset
  clears (6.2, 7.2 and 7.4).
- Configuration: `reset_pc` holding its value from the last cycle of reset through
  the first cycle after it (it may change earlier in reset);
  `hart_id` constant while the core runs, since `mhartid` reads it directly, and
  unique among the system's harts with one of them 0 (the specification's
  "Hart ID Register"; the tile id meets both).
- WFI clock gating (the core gates nothing itself): stop the core's clock only
  while `wfi_sleeping` is 1, `instruction_request_valid` is 0 and no fetch is
  outstanding. Fetch keeps filling its buffer after `wfi_sleeping` rises, and a
  request accepted while the clock is stopped would be lost. Restart the clock
  when `external_interrupt` or `nmi` rises: the tile cannot see `mie`, and the core evaluates
  the wake condition only on clock edges (if it does not hold, the core simply
  keeps waiting).
- Lockstep regions: the further obligations under [Lockstep
  regions](#lockstep-regions).

### CSRs

Checked against the machine-level specification (v20260120). Any CSR address not
listed raises an illegal-instruction exception. A write to a read-only address
(bits 11:10 = `11`) raises an illegal-instruction exception. CSRRS/CSRRC with
rs1 = x0 and CSRRSI/CSRRCI with a zero immediate do not write, so they never fault
for that reason. Unlisted bits read as zero and ignore writes.

| CSR | Address | Reset | Behavior |
| --- | --- | --- | --- |
| `mstatus` | 0x300 | MIE=0, MPIE=0 | MIE (bit 3) and MPIE (bit 7) writable. MPP (bits 12:11) reads `11`, writes ignored. All other fields read zero |
| `misa` | 0x301 | `0x4000_0104` | MXL=1 (RV32), I and C bits set. Writes ignored. Reports neither M nor A |
| `mie` | 0x304 | 0 | MEIE (bit 11) writable; every other bit reads zero and ignores writes |
| `mtvec` | 0x305 | `RESET_TRAP_VECTOR` | BASE (bits 31:2) writable; MODE (bits 1:0) reads `00` (direct only). The reset value is `rv32_core`'s parameter, a multiple of 4: in the tile, the boot ROM's handler (6.2) |
| `mstatush` | 0x310 | 0 | All zero (little-endian only) |
| `mcountinhibit` | 0x320 | 0 | Reads zero, ignores writes: the counters always count |
| `mhpmevent3`–`31` | 0x323–0x33F | 0 | Read zero, writes ignored |
| `mscratch` | 0x340 | 0 | Fully writable |
| `mepc` | 0x341 | 0 | Bits 31:1 writable; bit 0 reads zero |
| `mcause` | 0x342 | 0 | Interrupt (bit 31) and exception code (bits 4:0) writable |
| `mtval` | 0x343 | 0 | Fully writable |
| `mip` | 0x344 | — | MEIP (bit 11) shows `external_interrupt`; every other bit reads zero; writes ignored |
| `mcycle`, `mcycleh` | 0xB00, 0xB80 | 0 | 64-bit cycle counter; writable; counts every cycle |
| `minstret`, `minstreth` | 0xB02, 0xB82 | 0 | 64-bit retired-instruction counter; writable; counts every retired instruction |
| `mhpmcounter3`–`31`, high halves | 0xB03–0xB1F, 0xB83–0xB9F | 0 | Read zero, writes ignored |
| `mvendorid`, `marchid`, `mimpid` | 0xF11–0xF13 | 0 | Read-only zero |
| `mhartid` | 0xF14 | — | Read-only `hart_id` input |
| `mconfigptr` | 0xF15 | 0 | Read-only zero |

`mip` and `mie` have a bit only for the one interrupt the core has, MEIP and
MEIE; the bits of the interrupts it lacks read zero and ignore writes, as WARL
fields of an interrupt that cannot occur. The privileged specification ("Machine
Interrupt Registers") lets MSIP and MSIE both be read-only zero on a platform
that delivers interprocessor interrupts as external interrupts, as the tile
does through its events (7.2); the chip has no machine timer (6.1), so
none drives MTIP; and bits 16 and up are the platform's, which defines none.

Not present (accesses are illegal): `medeleg`, `mideleg`, `mcounteren`, `menvcfg`,
`mseccfg`, PMP, debug/trigger CSRs, and the user counters `cycle`, `time`, `instret`
(no Zicntr). A CSR write to `mcycle` or `minstret` replaces the value, and that
instruction's own increment is dropped. An instruction that traps is not retired and
does not increment `minstret`.

### Traps

| Cause | `mcause` | `mepc` | `mtval` |
| --- | --- | --- | --- |
| Instruction access fault | 1 | Instruction address | 0 |
| Illegal instruction | 2 | Instruction address | Original instruction bits: 16-bit encodings zero-extended, not their 32-bit expansion |
| Breakpoint (EBREAK, C.EBREAK) | 3 | Instruction address | Instruction address |
| Load address misaligned | 4 | Instruction address | Data address |
| Load access fault | 5 | Instruction address | Data address |
| Store address misaligned | 6 | Instruction address | Data address |
| Store access fault | 7 | Instruction address | Data address |
| Environment call (ECALL) | 11 | Instruction address | 0 |
| Machine external interrupt | 0x8000_000B | Next unexecuted instruction | 0 |
| NMI | 0x8000_0000 | Next unexecuted instruction | 0 |

A coprocessor instruction is an illegal instruction when `coprocessor_present`
is 0, found in decode with nothing sent, or when the coprocessor answers it with
an error, taken as the response arrives; `mtval` is the instruction word either
way. It has no other exception after decode.

`mtval` is 0 for an instruction access fault, whichever half of a 32-bit
instruction failed, and `mepc` names the instruction. The privileged specification
allows this: "When a trap is taken into M-mode, `mtval` is either set to zero or
written with exception-specific information to assist software in handling the
trap."

Instruction-address-misaligned exceptions cannot occur: with C, every jump and branch
target is halfword-aligned (JALR clears bit 0).

The NMI (6.2) is an interrupt of unknown cause, as
the privileged specification reserves `mcause` 0x8000_0000 for one, with no bit in
`mip` or `mie`. It can be taken while `nmi` is high and the core has taken no trap
since reset, whatever `mstatus.MIE` and `mie` hold; its own trap entry is one, so
it is taken at most once. Since no interrupt is taken while writeback holds an
instruction ([Pipeline event priority](#pipeline-event-priority)), a synchronous
exception always wins over it, and once taken ends its eligibility: the NMI that
an exception raises through the tile's errors is then never taken.

Trap entry: `mepc`, `mcause`, `mtval` are written; `mstatus.MPIE` ← `MIE`;
`mstatus.MIE` ← 0; the PC becomes `mtvec.BASE << 2`. The trapping instruction does
not retire and has no architectural effect.

MRET: `mstatus.MIE` ← `MPIE`; `MPIE` ← 1; the PC becomes `mepc`.

Synchronous exception priority for one instruction, highest first (spec Table 7):
instruction access fault; illegal instruction; ECALL/EBREAK; load/store address
misaligned (detected before any bus request); load/store access fault.
Simultaneous interrupts: the NMI, then the machine external interrupt; an NMI's
priority is above every interrupt's.

### Pipeline event priority

Highest first, when more than one applies in the same cycle:

1. Reset: returns every stage to empty and the PC to `reset_pc`, and ends a WFI
   wait.
2. Trap: the oldest instruction's exception, or an interrupt taken at the boundary
   before it. Flushes all younger instructions and redirects fetch to the trap vector.
3. MRET: at retirement, redirects fetch to `mepc` and flushes younger instructions.
4. Branch redirect: a taken branch or any other JALR leaving execute. The
   instruction behind it (fetched sequentially) still moves into execute at the
   same edge; in the next cycle fetch is redirected to the target (from
   registers), that instruction is cancelled in execute (it acts on nothing: no
   request, trap or interrupt), and decode is cleared. The branch or jump
   completes in writeback in that cycle: an instruction carrying an exception never transfers, and no
   interrupt is taken while writeback holds an instruction, so a trap or MRET
   never coincides with this redirect. A JAL, or a ready return (Control
   transfers), redirects as it enters execute instead: in its first execute cycle
   fetch is redirected to the target decode computed (from registers) and decode
   is cleared, while the jump stays in execute and completes as usual. That
   redirect can coincide with a trap or MRET of the older instruction in
   writeback, or with an interrupt taken in place of the jump: the trap or MRET
   wins and flushes the jump with everything younger. A JAL or ready return that
   enters execute behind a taken branch or JALR leaving it is on its wrong path:
   it is cancelled with the instruction it follows and redirects nothing.
5. Stall: holds a stage and everything behind it; the WFI wait (below) is one.

Interrupt eligibility: an interrupt is taken when it is pending in `mip`, enabled in
`mie`, and `mstatus.MIE` = 1, or, the NMI, when it can be taken ([Traps](#traps)),
at an instruction boundary where every older
instruction has fully completed and the next instruction has produced no effect (no
register write or bus request). `mepc` is that next instruction's
address. As the privileged specification requires ("Machine Interrupt
Registers": the conditions are evaluated immediately after an explicit write to a
CSR they depend on), a CSR write that enables or disables an interrupt takes
effect for the very next instruction: an interrupt it enables is taken with
`mepc` at that instruction. The core holds the instruction after any CSR write
for one cycle in execute to do this. After MRET, a pending enabled interrupt is
taken before the instruction at `mepc` executes. For WFI, see below.

### Wait for interrupt (WFI)

Checked against the privileged specification (v20260120), "Wait for Interrupt",
which has WFI inform "the implementation that the current hart can be stalled
until an interrupt might need servicing". The contract relies on these sentences:

- "If an enabled interrupt is present or later becomes present while the hart is
  stalled, the interrupt trap will be taken on the following instruction, i.e.,
  execution resumes in the trap handler and `mepc` = `pc` + 4."
- "The operation of WFI must be unaffected by the global interrupt bits in
  `mstatus` (MIE and SIE) and the delegation register `mideleg` (i.e., the hart
  must resume if a locally enabled interrupt becomes pending, even if it has been
  delegated to a less-privileged mode), but should honor the individual interrupt
  enables (e.g, MTIE) (i.e., implementations should avoid resuming the hart if the
  interrupt is pending but not individually enabled)."
- "If the event that causes the hart to resume execution does not cause an
  interrupt to be taken, execution will resume at `pc` + 4"
- "This instruction may raise an illegal-instruction exception when TW=1 in
  `mstatus`": TW reads zero here, so WFI raises no exception of its own.

The core:

- Retires the WFI normally: its RVFI record (next PC = its address + 4), a
  `minstret` increment. The record after it is the interrupt handler's first
  instruction or the instruction after the WFI, after the wait.
- Wakes on `(mip & mie) != 0` (MEIP and MEIE), whatever `mstatus.MIE`, and on the
  NMI while it can be taken ([Traps](#traps)). An interrupt pending but not
  enabled in `mie` does not wake it. Only this condition and reset end the wait.
- Holds the instruction after the WFI in decode, never in execute, from the
  cycle the WFI is in execute: while the WFI is in execute or writeback, and
  then while `wfi_sleeping` is set. The hold comes from registers only and acts
  only on decode's move into execute and on decode taking fetch's next
  instruction, which reach registers and no request output; so neither the WFI
  wait nor the wake condition has a path to the instruction or data request
  outputs. Execute and writeback are empty while the core
  waits.
- If the wake condition holds in the cycle the WFI retires, there is no wait:
  the instruction after it moves into execute in the next cycle and, with
  `mstatus.MIE` clear, retires three cycles after the WFI with nothing else
  stalling (two cycles more than after an ordinary instruction).
- Otherwise waits from the next cycle, with `wfi_sleeping` set: no instruction
  executes, traps or accesses memory. `wfi_sleeping` falls in the cycle after
  the first cycle in which the wake condition holds, the instruction after the
  WFI moves into execute then and is in execute in the cycle after that. With
  `mstatus.MIE` clear, and no NMI to take, it leaves execute in that cycle and
  retires in the next with nothing else stalling: execution resumes at the
  WFI's address + 4 without a trap, whether or not the interrupt input stays
  high.
- Takes an interrupt that wakes it the ordinary way, as the first sentence
  quoted above requires. Let w be the cycle the WFI retires in if the wake
  condition holds then, or else the first cycle of the wait in which it holds.
  The instruction after the WFI moves into execute at the end of cycle w + 1. If
  `mstatus.MIE` is set in cycle w, or the wake is the NMI's, the interrupt can
  still be taken in cycle w + 2: its source holds it until the program clears it,
  the NMI's until reset (Obligations of the enclosing unit), and no instruction
  runs in between to change `mstatus` or `mie` or take a trap. It is taken there like any other interrupt (above), in place of that
  instruction and before it has any effect, with `mepc` the WFI's address + 4,
  `mtval` 0 and the cause of the highest-priority interrupt pending in cycle
  w + 2. A request that falls before cycle w + 2, which the obligations rule
  out, is not taken: execution resumes at the WFI's address + 4, as the
  specification allows for an event that causes no interrupt. The cycles after
  the wake are the price of keeping the hold off the request paths; the
  specification sets no wake-up latency.
- Does not tell fetch about the wait. Decode holds the instruction after the
  WFI, and fetch fills its six-halfword queue behind it and stops, because it
  requests a word only with room reserved for it; then the instruction port
  raises no request until the wake, although a response to the last may still
  be outstanding when `wfi_sleeping` rises. That is at most four requests from
  the cycle the WFI retires (the words up to 16 bytes past the WFI's own). The
  data and coprocessor ports are idle: every older access completed before the
  WFI retired, and nothing younger starts.
- Gates nothing itself. When a tile may stop the core's clock, and when it must
  restart it, is under [Obligations of the enclosing
  unit](#obligations-of-the-enclosing-unit).

### Lockstep regions

A lockstep region lets a group of cores, up to every core of the mesh, run one
program in step: from a common barrier on, every core of the group retires the
same instruction at the same address in the same cycle, raises the same
instruction requests and coprocessor instruction words, and raises its data
requests in the same cycles to the same target ports; the values each core
computes are its own. The core provides
what is listed here; the tile, the NoC and the firmware provide the rest under
the obligations and rules below. In the machine the barrier is the
tile control unit's and the master tile's (7.3 and 17.6), its wake is MEIP, and
the group is every compute tile (7.3). *Open* in the RTL: how a tile joins a
smaller group, clock gating during a barrier, and detecting a core that drifts
out of step.

What the core provides:

- Timing from the instruction stream only. Every stall, trap and redirect is
  decided from instruction kinds, register numbers, instruction lengths and
  addresses, whether a branch is taken and a jump's target, a data access's
  alignment (a misaligned one traps without a request), the ports' timing and
  errors, and the interrupt inputs as `mie` and `mstatus.MIE` admit them
  (Control transfers, Data, Alignment, Multiply, the hold after a CSR write under
  [Pipeline event priority](#pipeline-event-priority), and the WFI wait); never
  from other operand values, and nothing in the core remembers earlier control
  flow or timing. There is no branch predictor, cache, divider or early-exit
  arithmetic.
- A canonical wait. A core waiting after a WFI (`wfi_sleeping` = 1) whose
  instruction port is idle (`instruction_request_valid` = 0, no fetch
  outstanding) stays so until the wake, in a state that depends only on the
  WFI's address and the code after it: decode holds the instruction after the
  WFI, fetch's queue holds the five or six halfwords after that instruction, up
  to a fetch-word boundary (every fetch after a redirect's first is an aligned
  word), and execute, writeback and the data and
  coprocessor ports are empty. It does not depend on when the core arrived,
  what it ran before, or its ports' earlier timing. `core_status_canonical_wait`
  reports it, a cycle late ([Core ports](#core-ports)): the tile's barrier
  waits for it (7.3).
- The release on `external_interrupt` (MEIP). With `mie` = 0x0000_0800 (MEIE
  alone) and `mstatus.MIE` clear it ends the wait and is never taken: execution
  resumes at the WFI's address + 4.

Barrier and release. A barrier is an arm write, then a WFI, at the same
addresses on every core of the group: a whole-word store with bit 0 set to the
tile's barrier register, which arms the barrier, then the barrier's WFI, with no
other WFI between them (`li t0, 1`, `sw t0` to the register, `wfi`). In the machine
the register is `ARRIVE` (7.3). What follows is the core's model of a barrier
device, which states what the core needs of one; the machine's device is the
tile control unit, and 7.3 is the authority on what it does. The tile's barrier device drives a line, the barrier line, which
is the core's `external_interrupt` from the arm write until the release (in the
machine's tile, the arrival gate makes the wake its release
alone: 7.3). The device:

- Lowers the barrier line on every write to the register, no later than the
  edge that ends the cycle in which the write's response reaches the core. The
  line that a release raised stays high until such a write, and the arm write
  lowers it before the barrier's WFI can retire, as it must: a line still high
  would let the WFI fall through. A release with the arrival counted wins over a
  write that takes effect at the same edge; only another requester than the core
  could write then, since an arrived core waits.
- Reports the tile's arrival on its output `barrier_arrived`, from a flip-flop
  that rises at the edge that ends a cycle in which the core is armed and waits
  in the barrier's WFI with its instruction port idle (the clock-gating
  condition under [Obligations of the enclosing
  unit](#obligations-of-the-enclosing-unit)), and stays high until the release:
  an arrival, once counted, is never withdrawn.
  The arming tells the barrier's WFI from any other, and keeps a core that still
  waits just after the release from arriving again. An arrived core keeps its
  canonical wait until the release, since only MEIE is enabled in `mie` and the
  barrier line stays low until then. The IMEM writes made while the
  cores wait, a region's kernel among them, are visible by the release under the
  rule for code written while the core runs (above), which the writer meets
  before it has the group released (in the chip, the master tile: 6.5, 17.6).
- On a release with the arrival counted, raises the barrier line and clears
  the arming and the arrival, at one edge. It ignores a release without an
  arrival counted, leaving the line as it is (low while the tile is armed), and
  flags it in the register: a fault of the release network then shows as a
  detectable hang, not as cores silently out of step.

The release network releases a group only when every member's `barrier_arrived`
is high, never on an arrival that its previous release has cleared (after each
release it waits out its own round trip), by raising the barrier line, and so
`external_interrupt`, on every core of the group in the same cycle, R: each input comes from a
flip-flop next to its `rv32_core`, reached from the release through registered
stages of equal depth, with no synchronizer.

Resume timing. In R the wake condition holds on every core (w = R under [Wait
for interrupt](#wait-for-interrupt-wfi)): `wfi_sleeping` is 0 from R + 1, and
the instruction after the WFI moves into execute at the end of R + 1 and, with
nothing else stalling, retires in R + 3. From R on, a core's retirements, traps,
instruction requests, data request valids and target ports, coprocessor request
valids and instruction words, and `wfi_sleeping` depend only on its code and its
ports' timing, so under the obligations and rules below they are the same on
every core, cycle for cycle. Register values, data addresses within DMEM,
loaded and stored data, coprocessor operands and results, and `rvfi_order` may
differ.

Obligations of the tile and NoC, from R until the group leaves the region:

- Code, from the barrier wait on, before R: every word that a core of the group
  may fetch from then until the group leaves the region is the same at the same
  address on every core, and is not written from the time a core of the group
  may fetch it until the group leaves the region. These words include those
  fetched ahead and discarded, and the barrier WFI's word and the 16 bytes after
  it, even those fetched before the wait. Writes during a region go only to
  words that no fetch of the region returns, every word an IMEM write writes
  among them: the tile's NoC port writes a packet's words
  (6.3).
- Instruction port: ready in every cycle, each response exactly one cycle after
  its request, never an error; IMEM writes never delay a fetch.
- DMEM: ready in every cycle, each response a fixed number of cycles after its
  request, the same on every tile, never an error; no other master (the tile's
  NoC port) delays a core access.
- Coprocessor: idle at R, holding nothing from before R that can change its
  timing; from R on, its ready and each response's timing depend only on the
  instructions it has received since R and the cycles they came in, the same on
  every tile, never on operand values; never an error.
- System port: only devices local to the tile, the barrier register among them,
  each ready in every cycle and answering after a fixed number of cycles, the
  same on every tile; no access goes over the NoC; no timeout, no error.
- Clock and reset: one clock for every core of the group and the release
  network, with no divider, synchronizer or clock-domain crossing; no core is
  reset. A tile that stops a waiting core's clock restarts it from the release
  itself (the flip-flop that drives `external_interrupt`, or the stage before
  it), so that the core sees the edge that ends R; `mcycle` then misses the
  stopped cycles, which the rules keep out of control flow.
- Interrupts: `external_interrupt` is the barrier line from each arm write
  until its release; at other times it is unconstrained, and with `mie` and
  `mstatus` as the rules require it reaches `mip` and nothing else, since the
  region has no WFI but its barriers. `nmi` is low: it is a tile's error, which a
  correct program on correct hardware never raises.

Firmware rules for the code of a region:

- Before the first barrier, set `mstatus.MIE` = 0, `mie` = 0x0000_0800 (MEIE) and the
  same `mtvec` on every core; none of them changes in the region. The region has
  no WFI but its barriers.
- Control flow is the same on every core: no branch condition, JALR target, trap
  or MRET depends on a value that can differ between cores (data, loaded values,
  coprocessor results, `mhartid`, `mcycle`, `minstret` or their high halves,
  `mip`, a register not written in the region). A data-dependent choice uses
  `czero.eqz`/`czero.nez` or arithmetic, and every loop runs the same number
  of times. Software division and remainder (libgcc's `__divsi3` family, Rust's
  compiler builtins) branch on their operands, so a region divides only values
  that are the same on every core. Polling a device is control flow on its
  data: a region does not poll.
- Loads and stores are naturally aligned and go to DMEM, at addresses that may
  differ between cores, or to the tile's local devices at the same address on every
  core. No access that faults.
- Coprocessor instructions are ones the coprocessor accepts, never answering
  with an error.
- `mhartid`, the counters and `mip` serve only as data.
- Kernel code, written while the core runs, is entered by a redirect ([Memory and reset interface
  details](#memory-and-reset-interface-details)), never by falling through from a
  barrier.

A branch on per-core data ends the region; the next barrier starts a new one.


## The core's cycle costs

The contract above fixes these costs. They are gathered here for the
simulator, whose core model reproduces them, so that a program takes the same
cycles on meshsim as on the RTL. Each holds with IMEM and DMEM as the machine
has them: ready in every cycle, each response the cycle after its request
(6.3, Lockstep regions), and with no other stall. "After" counts retirements:
an instruction with no cost retires one cycle after the one before it.

| Case | Cost | Where |
| --- | --- | --- |
| an ordinary instruction; a branch not taken | the next instruction retires 1 cycle after it | Control transfers |
| a taken branch; a JALR other than a ready return | the next instruction retires 4 cycles after it: 3 more | Control transfers |
| a JAL; a ready return (a JALR through x1 whose x1 is committed) | the next instruction retires 4 cycles after the jump enters execute: 3 after it retires, 2 more, or fewer if the jump waited in execute, and at least 1 | Control transfers |
| a redirect's target at an odd halfword, or a 32-bit instruction across two words | nothing more | Fetch, Control transfers |
| a load or store to DMEM | 1 cycle: back-to-back loads and stores retire one a cycle | Data |
| an instruction that uses a load's result right after it | waits 1 cycle | Data |
| a multiply | two pipelined stages, and no cycle of its own; a multiply whose operand the instruction in writeback is still producing waits 1 cycle, and an instruction that uses its result right after it waits 1 cycle | Multiply |
| `czero.eqz`, `czero.nez` | as any ALU instruction | Conditional zero |
| the instruction after any CSR write | held 1 cycle in execute | Pipeline event priority |
| a load from, or a store to, a window or a table | the machine's fixed latency, `WINDOW_LOAD_LATENCY` or `TABLE_LOAD_LATENCY` (6.4), and a stalling store its stall (9, 11.1, 17.6) | Obligations of the enclosing unit |
| a `WFI` whose wake condition holds as it retires | no wait: the next instruction retires 3 cycles after the `WFI`, 2 more | Wait for interrupt (WFI) |
| a `WFI` that waits, woken in cycle R | the next instruction retires in R + 3 | Lockstep regions |
| a coprocessor instruction | the coprocessor's: 1 cycle with one that is always ready and answers the next cycle | Coprocessor |

- **No other cost.** There is no branch predictor, cache, divider or
  early-exit arithmetic, so a cost depends on the instructions around it,
  never on earlier control flow or on operand values (Lockstep regions).
- **Overall.** On CoreMark the RTL's core takes 1.312 cycles per instruction,
  2.776 CoreMark/MHz `[meas: RTL]`, with memory answering the next cycle. It is
  an unofficial figure: the performance run alone, in simulation.
- **Traps.** Every trap is terminal in the machine (6.2), so no program's
  result or phase timing depends on a trap's entry cost, and this document
  states none.
