# The change guide from v1.35 to v1.36

*For whoever brings the simulator and the firmware from v1.35 to v1.36, for the RTL engineer, and for the owner,
who has three reversals of recorded rulings and a list of new open items to rule on (sections 4 and 8).*

## 1. In brief

**v1.36 is the hardware the RTL (Snarky One) implements, in the RTL's terms.** The RTL was written against v1.3
and v1.35 with a specification of its own, which refined them chapter by chapter; building it closed questions
they left open, changed some things they decided, and found errors in them. v1.36 takes that specification as
meshsim's: `spec/machine-spec.md` chapters 1–19 are the RTL's base tile specification and `spec/core.md` its core
contract, with their numbering, names, register layouts and vocabulary, and `spec/jolt/replayer.md` and
`spec/jolt/tracer.md` are its 17.8 and 17.9. What v1.35's specification has beyond them is kept in the same terms:
the invariants (20), the timing summary (21), rules and figures only v1.35 stated (22), the deferred memory system
(23), the budgets (24, 25), the open items under their numbers (26) and the sort (27). **v1.36 is specification
and simulator:** this guide is what the simulator and the firmware are to change to, unit by unit, and the cycle
counts they are to reproduce. Nothing in `src/`, `fw/` or `tests/` has changed yet (`README.md`, Status).

**For the RTL: nothing changes**, except where this guide says the RTL's own documents are wrong or stale
(section 6, "On the RTL's side") and where the owner rules against a reversal (section 4).

**How to read a citation.** A bare section number is v1.36's (`spec/machine-spec.md`, with 17.8.x and 17.9.x in
`spec/jolt/`). "v1.35's 4.12" is a section of v1.35's documents, kept in `spec/history/`
(`machine-spec-v1.35.md`, `replayer-v1.35.md`, `tracer-v1.35.md`). Item and invariant numbers are the same in
both. The RTL's measurements are marked `[meas: RTL]`. In sections 5, 6 and 8, a sentence that quotes v1.35 after
"v1.35's" keeps to v1.35's numbers to its end, and "section 3.3" or "6.8" with the word "section" is this guide's own.

| # | section | for |
|---|---|---|
| 2 | what to change, unit by unit, in implementation order | the simulator and the firmware |
| 3 | the timing table: the RTL's figure against v1.35's simulator, path by path | the simulator |
| 4 | three reversals of recorded rulings | the owner |
| 5 | differences the first inventory lacked, and where the RTL's specification is silent | the owner, the simulator |
| 6 | errors and contradictions found in v1.35, and their resolution | everyone |
| 7 | the name table, v1.35 → v1.36 | the firmware, mechanically |
| 8 | where v1.35's specification went | everyone |

## 2. For the simulator and the firmware: what to change

Unit by unit, in the order to implement it. Each item ends with its section in v1.36, the section of v1.35 it
replaces, and a code such as A5.3 or B2.1: the item's number in the inventory of differences between the RTL and
v1.35 that this version was written from (kept with the project's planning, not in this tree; the codes are here
so that each change can be traced). "Verified" means both texts were read for the item. The stages are the
ones `README.md` lists. Stage 1 changes no cycle count by itself and is the widest; stages 2 to 6 move every
figure of the sort; 8 and 9 need 1 to 7.

### 2.1 Stage 1: the register map and the names

Section 7 is the name table: every register, bit, field and parameter, with its old and new place. The changes
behind it:

- Region 0xFF; windows are eight blocks of 16 words, one module a block; every tile-window offset moves but `PARAMS`, `STATUS`, `ERRORS` (full table: D3-names.md); 5.3, 5.5; v1.35's 8.1; A2.1. Verified.
- Tables; base 0xFFFF2000 → 0xFFFF_E000, order kept, window 11 unmapped; 5.2; v1.35's 8.1, 8.9; A2.2. Verified. Closes item 120.
- Tables; every table loadable by the core (v1.35: only `HIST`, the rest a bus error); 5.2, 8.6, 12.5; v1.35's 8.9; A2.3. Verified.
- Tables; store only their fields' bits: other bits ignored on a store, 0 on a load; 5.2; v1.35's 8.9; A2.4. Verified (v1.35's 8.9 gives fields but not storage).
- Decode; an undefined word or unused block is an access fault (a DMA-less peripheral's block 1 reads 0); v1.35: reserved words read 0, stores ignored, the simulator flags; 5.3, 17.1; v1.35's 8.1; A2.5. Verified.
- Simulator-only exit and trace words removed; a tile's result is a request to the master (the RTL chip's firmware uses `op` 2, `EXIT`: firmware's, not the spec's); 5.5; v1.35's 8.15; A2.6. Verified.
- Header registers; header block in [7:0], `dst` [14:8], own fields [31:15] (v1.35: [31:17] header, [16:0] own); 5.4; v1.35's 8.1; A2.7. Verified.
- `PARAMS`; straps only, new layout and five new straps, no memory size; 7.1; v1.35's 8.2; A2.9. Verified.
- `STATUS`; four bits; `rd_done`, `send_busy`, "command landed" gone; 7.1; v1.35's 8.2; A2.10. Verified.
- `EVENTS`/`EVENTS_MASK`; 14 bits renumbered, mask reset 0x5; "command landed" and "request FIFO no longer full" gone; 7.2; v1.35's 8.10; A2.11. Verified.
- `ERRORS`; 13 bits renumbered; v1.35's [4] and [5] gone; 7.4; v1.35's 8.3; A2.12. Verified.
- `EVENTS_REQUIRED` removed, and its term in the wake; 7.2; v1.35's 8.10; A2.13. Verified.
- Reset; `WRITE_W0`–`W2` and `REQUEST_OUT_W0`–`W2` are 0 at the reset (v1.35: undefined); 18; v1.35's 8.1; A2.14. Verified.
- Stores; a window store takes effect at the end of t + 1, a table store at the end of t (v1.35: every store at the end of its cycle, "posted"); 6.4; v1.35's 8.1; B1.1, A3.10. Verified.
- `DMA_CONFIG` split into `DMA_CONFIG` (+`gap`), `RECEIVE_CONFIG`, `GATHER_CONFIG`; 8.1, 12.1, 13; v1.35's 8.5, 8.7; A5.1, A5.2. Verified.
- Read unit; four registers, `READ_DEST_ADDR` a full address, `READ_FREE` 16 bits; 10; v1.35's 8.8; A5.10. Verified.
- Write unit; `no_increment` at `WRITE_HDR` [15]; 9; v1.35's 8.4; A5.11. Verified.
- Request unit; separate out and in words; 11.1; v1.35's 8.10; A7.1. Verified.
- Peripheral window; control block words 3–6 reordered, DMA in block 1; `DMA_HDR` layout; 17.1; v1.35's 8.11; A8.1, A8.2. Verified.
- IMEM and the boot ROM are fetch-only: a load is an access fault. 5.1; v1.35's 2.2. A2.8.
- A core access past IMEM's or DMEM's size faults; no aliasing. `spec/core.md`, 6.3; v1.35's 2.2. A2.15.
- Tables at 0xFFFF_E000–0xFFFF_F5FF; every table loadable; only fields' bits stored. 5.2, 8.6, 12.5; v1.35's 4.10, 8.9. A2.2–A2.4.
- `fw/include/mesh.h`, `fw/REGISTERS.md` and the register-table generator (`tools/regs/`) are regenerated from the
  new table; every firmware source is translated by section 7. Counter 7 is the boot ROM's load counter (6.5):
  v1.35's firmware used it as `CTR_FENCE`, which still works inside an image, but an image ends with every
  counter at 0.

### 2.2 Stage 2: the control protocol and the boot

- No command block, no "command landed", no uncounted command writes; the reply block is software's. 6.5; v1.35's 4.12; A4.1.
- Ready = `STATUS.arrived` and the core in the canonical wait; `BARRIER_ARRIVE` pulses the cycle after. The simulator's tile control needs the core's canonical-wait level. 7.3; v1.35's 4.12, 5, 6.3; A4.2.
- The arrival gate lasts through the cycle after the release. 7.3; v1.35's 4.12; A4.3.
- The gate does not stop counter events from being set; `ARRIVE` clears `counter_n_done` and `all_counters_done` and neither arms nor disarms them. 7.3, 7.2; v1.35's 4.12; A4.4.
- A counter's event: "a decrement that leaves the counter at 0 while armed"; a drain in the arming cycle sets it (12.2's next = count + N − dec reaching 0). 7.2, 12.2; v1.35's 4.12; A4.5, A6.11.
- The release wins over an `ARRIVE` in the same cycle (`STATUS.arrived` cleared). 7.3; v1.35's silent; A4.6.
- The one tie rule: `DMA_START`'s and `ARRIVE`'s clears win over a set of the same cycle; a consuming load never removes an event it did not return. 7.2; v1.35's 4.12 ("a set and a clear in the same cycle: the clear wins").
- Commands, the command block, "command landed", parity argument blocks, `wait_for_command()`'s read: removed; a phase is a step of the image's fixed schedule; 6.5, 7.3; v1.35's 6.2, 6.3, 6.6; A4.1, D5. Verified.
- Barrier; every compute tile takes part in every release, the ROM has no guard, a release before all have arrived is UB; 6.5, 7.3; v1.35's 6.1, 6.2; A4.7. Verified.
- Boot; the ROM branches on `PARAMS.boot_local`, arms counter 7, sends `LOAD`, waits, arrives (if `PARAMS.barrier`), jumps to IMEM's base; the master answers with uncounted `WRITE`s and one `COUNT`; 6.5, 11.5; v1.35's 6.2; A4.8. Verified.
- Images end by a jump to the reset PC; no overlays, no staging in the scratchpad; 6.5; v1.35's 6.2, 6.5; A4.9. Verified.
- Master and memory tiles start from placed images (`boot_local` 1); the tracer tile by `LOAD`; no host boot, no "started" requests; 6.5; v1.35's 6.2 (steps 3, 4); A4.10, D4. Verified. Open on both sides: boot over LVDS, a silicon memory tile's fill.
- Handler; ROM's reports 24 and 25; an image's set is its own, numbered from 0; `REPORT` op 1, number [25:18]; 6.5, 11.5; v1.35's 6.1 (step 4), 4.13; A4.11, A7.5. Verified (range 6.1 only).
- `stage_delay` removed; counted data follows a request (`LOAD`, `ARMED`, `STREAMS_ARMED`) or lockstep; 6.5, 17.9.10; v1.35's 6.2, 6.3; A10.11, D5. Verified in 6.2/6.3.
- Interrupt enables; the ROM sets `mie.MEIE`, `mstatus.MIE` stays 0, errors are an NMI; v1.35's image enabled `MEIE` and `MIE` in boot part 2; 6.2, 6.5; v1.35's 6.2; A3.2, A3.3. Verified.
- Reset; one reset input (hard/soft *Open*); 18; v1.35's 6.1; A1.12, D3, item 124. Verified.

### 2.3 Stage 3: the core

- A3.1 ISA: add C and Zicond (RV32I + C + Zmmul + Zicsr + Zicond); fetch handles 16/32-bit instructions on any halfword; `misa` reports I and C only. Build flags `rv32ic_zicond_zicsr_zmmul`. New: `spec/core.md` Core contract (ISA, Fetch, Conditional zero), machine-spec 6.1. Old: v1.35's 4.11 ("no C or A extensions").
- A3.2 Errors are the NMI: unmaskable (whatever `mstatus.MIE`/`mie`), taken from `WFI` too, at most once (a core that has taken any trap takes no NMI; a synchronous exception in the same cycle wins); entry `mcause` 0x8000_0000, `mepc` next instruction, `mtval` 0. New: core.md Traps, Interrupts; 6.2. Old: v1.35's 4.11 Inputs (gated by `mstatus.MIE` and `mie`), 4.13.
- A3.3 The wake is MEIP (`mip`/`mie` bit 11): `WFI` ends on `(mip & mie) != 0`; the boot ROM sets `mie.MEIE`; software keeps `mstatus.MIE` 0 so it never traps (with MIE 1 it traps, cause 0x8000_000B). `mip` shows only MEIP. New: core.md WFI, CSRs; 6.2, 6.5. Old: v1.35's 4.11 (separate wakeup input).
- A3.4 CSR list and reset values fixed: `mstatus` (MIE, MPIE; MPP reads 11), `misa` 0x4000_0104, `mie` (MEIE only), `mtvec` (reset: the boot ROM's handler; direct mode), `mstatush`, `mcountinhibit`, `mhpmevent*`/`mhpmcounter*` (zero), `mscratch`, `mepc`, `mcause`, `mtval`, `mip`, `mcycle[h]`, `minstret[h]`, `mvendorid`/`marchid`/`mimpid`/`mconfigptr` (0), `mhartid`. No `mtime`/`time`/`cycle`/`instret`. Any other CSR, or a write to a read-only one, is illegal. Closes item 121. New: core.md CSRs. Old: v1.35's 4.11 (`mcycle` and `mtime`; item 121). Simulator: `misa` currently reports M (confirmed in the simulator's CSR read); `mtvec` resets to 0 there.
- A3.5 Misaligned halfword/word load or store traps (causes 4/6, `mtval` the data address) with no bus request, and sets no `ERRORS` bit. New: core.md Alignment, Traps; 6.2. Old: v1.35's section 1 Alignment (UB).
- A3.6 No instruction-address-misaligned exception exists (with C). Remove the misaligned-jump trap. New: core.md Traps; 6.2. Old: v1.35's 4.11 Traps, invariant 23.
- A3.7 A fetch outside IMEM and the boot ROM is an instruction access fault (cause 1, `mtval` 0) and pulses `core_status.access_fault`, so it sets `ERRORS.access_fault`. New: core.md Instruction-side windows, Core ports; 6.2, 7.4. Old: v1.35's 4.11 ("the fetch and the jump set no `ERRORS` bit").
- A3.8 `FENCE.I` is illegal (no Zifencei); ordinary `FENCE` drains preceding transactions. New: core.md Memory and reset interface details. Old: none in v1.35's spec; the simulator treats opcode 0x0F with funct3 ≤ 1 as a no-op (confirmed).
- A3.9 Coprocessor port: custom-0/custom-1 go to it when `coprocessor_present` is 1; in the machine it is tied off until the accelerator is decided (*Open*, 2.2, 6.1), so they are illegal instructions. New: core.md Coprocessor. Old: none (v1.35's spec has no coprocessor or custom opcode: searched).
- A3.10 No posted stores: every load and store has a completion response; a window store takes the window's load latency and takes effect at the end of t + 1 (B1.1). New: core.md Memory ordering; 6.4. Old: v1.35's 4.11 Timing ("single-cycle and posted").
- Timing model: replace "one instruction a cycle, +1 taken branch, 3-cycle multiply, load-use 0" by core.md's "The core's cycle costs" table (A-timing.md). Lockstep: the canonical wait (asleep in `WFI`, instruction port idle) is reported by `core_status.canonical_wait`, one cycle late; resume at R + 3.
- IMEM and the boot ROM serve fetches only: any load or store to them is an access fault (inventory A2.8). An address in the part of a window's span past its size faults and never aliases (A2.15).
- `mhartid` reads the tile's `hart_id` input (see A-rulings: the machine-spec does not say it is the tile id).

### 2.4 Stage 4: the NoC, the topology and the requests

- Chip: attach tiles are 0, `MESH_SIZE` − 1 and (2·`MESH_SIZE` − 1) mod `TILE_COUNT` (0, 7, 15), not 0, 1, 2. 2.1, 16.1; v1.35's 2, 4.16. A1.1.
- Chip: special tiles have the full `SCRATCHPAD_BYTES` (4 MiB), not 2 MB. 2.2; v1.35's 2. A1.2.
- Chip: mesh side is a parameter, 1–8; tile ids, `TILE_ID_BITS`, routes and broadcast spans follow it. 4.1, 16.2, 19; v1.35's 2.1, 4.6. A1.3 (simulator: only if taken; equal at 8 × 8).
- Router: attach ports and their queues exist only on attach tiles. 16.1; v1.35's 4.7 ("every tile"). A1.4. Not observable.
- Router: a packet for a reserved tile id, or none as dst, is dropped where it enters, no error; the simulator may still flag it. 16.2; v1.35's 2.1. A1.8.
- Packet: only the defined bits are carried and stored (182 at 8 × 8). 4.1; v1.35's 3.1. A1.9. Area estimates only.
- Packet: header block in header registers at [7:0], `dst` [14:8], own fields [31:15]. 5.4; v1.35's 3.1, 8.1. A2.7.
- No start skew: remove the 0/+1 offset. 7.3, 18; v1.35's 3.2. A1.11.
- Reset: one reset input (*Open*: hard and soft). 18; v1.35's 2, 6.1. A1.12.
- Send and receive words separate: `REQUEST_OUT_W0`–`W2`, `REQUEST_SEND` at 0x100–0x10C; `REQUEST_IN_W0`–`W2`, `REQUEST_RECEIVE` at 0x110–0x11C. 11.1; v1.35's 4.18; A7.1.
- Request queue 4 deep; no room event. 11.1, 19; v1.35's 4.18; A7.2.
- Receive queues in the receiving tile's request unit, a `request_credit` wire to the root. 11.3; v1.35's 4.18, 3.3; A7.3.
- Targets 3–7 reserved, dropped at the root. 11.2; v1.35's 4.18; A7.4.
- Word 0: `op` [17:10], 0 reserved; `REPORT` 1 with `number` [25:18]; `LOAD` 3. 11.5, 6.5; v1.35's 4.13 (draft: sequence number [31:21]); A7.5.
- Replies of 32, 64 or 128 bits. 11.4; v1.35's 4.12 (32 or 64); A7.6.
- A no-reply request is outstanding until the target pops it. 11.3; v1.35's 3.3 (D1); A7.7.
- The request tree's timing: a cycle a node, links wires, a node's ready in the same cycle, a credit usable the
  cycle after its pop (section 3; 21.4). v1.35's `hop_latency` 3 and its credit latency go.

### 2.5 Stage 5: the DMA, write and read units

- `DMA_CONFIG`: kind, start, count as before; new `gap` [20:18]; digits → `RECEIVE_CONFIG`; *k* − 1 and the record base → `GATHER_CONFIG`. 8.1, 12.1, 13; v1.35's 4.2, 8.5, 8.7. A5.1.
- `STREAM`: per-stream gap removed, one per-transfer gap. `STREAM_SRC_ADDR` [20:0] only; `STREAM_HDR` = header block [7:0], `dst` [14:8], count [31:15]. 8.3; v1.35's 4.2. A5.2, A2.7.
- `GROUP`: word 0 = rotations [17:0], first_stream [25:18], streams − 1 [29:26]; word 1 = gap [7:0]. A pass's R store rewrites the whole of word 0. 8.4; v1.35's 4.2. A5.3.
- A special tile's stream `dst` below `TILE_COUNT` is absolute. 8.3; v1.35's 4.2 ("A controller's relative *d*"). A1.10.
- Group lead-in is 4·`TABLE_LATENCY` + 3 whatever the first entry holds. 8.2; v1.35's 4.2 (lines 467 against 471). A5.6.
- A refused read shifts every later time (no catch-up). 8.3, 8.4; v1.35's 4.2 says the same, its simulator differs. A5.7.
- An empty transfer is busy for one cycle; `dma_done` at its end. 8.2; v1.35's 8.5. A5.8.
- `dma_done` and `dma_busy` change in the cycle the last table read completes; no `rd_done` bit. 8.2; v1.35's 4.2. A2.10.
- The core may store `STREAM`/`GROUP` entries the running transfer does not read. 8.6; v1.35's 4.10. A5.5.
- A `DMA_CONFIG` store during a transfer: the RTL latches it at the start (Appendix A); the contract does not say. A5.9 — needs a ruling (D1-rulings).
- Four registers: `READ_HDR`, `READ_DEST_ADDR` (32 bits), `READ_SEND`, `READ_FREE`. 10; v1.35's 4.3, 8.8. A5.10.
- Write unit: `no_increment` is `WRITE_HDR` [15]; a send stalls only while the holding register is full and not
  being injected that cycle; the packet is injected in *t* + 2 at the earliest, for a send store issued in *t*.
  9, 14.3; v1.35's 8.4, 4.11. A5.11, B3.7.

### 2.6 Stage 6: the receive, gather and inject units and the scratchpad

- Write engine: a fixed pipeline, no input FIFO, no refusal, no `ERRORS` bit; writes win their bank. 12.4, 15; v1.35's 4.9. A6.3.
- `PTR`/`HIST`: no request FIFOs and no overflow error; a core access meeting the engine: the store is lost, the load undefined. 12.5; v1.35's 4.10. A6.4, A6.5.
- Counter: a store and a decrement in one cycle give count + N − decrement. 12.2; v1.35's 4.12 (D2's range). A6.11.
- No "command landed" decode at the director. 12.3; v1.35's 4.8. A4.1.
- Queue of `TOKEN_COUNT` = 64; overflow is a program error. 13, 7.4; v1.35's 4.4. A6.6.
- While `gather_busy`, the address generator takes no read. 14.1; v1.35's 4.4, 4.5. A6.7.
- `gather_busy` = queue non-empty or a request's last read not yet taken; reads taken are not included. 13; v1.35's 4.4 (same text; its simulator ORs in in-flight reads). A6.8.
- No bank request queues: a read is taken only if no write meets it at its bank and a token is free. 14.1, 15; v1.35's 4.1, 4.5. A6.1.
- Fixed `READ_LATENCY`; reads return in take order; one return order for all clients. 15, 14.2; v1.35's 4.1, 4.5. A6.2.
- A loopback packet's token is freed when the traffic director takes its copy. 14.2; v1.35's 4.5. A6.9 (simulator side not traced).
- `SCRATCHPAD_BYTES` is a power of two, 512 B to 8 MiB; no partial bank. 5.1, 15, 19; v1.35's 4.1. A6.10 (conflicts with item 107's 4.25 MB).

### 2.7 Stage 7: the peripherals, Ingress/Egress, the RW engine and the host interface

- Control block: `EVENTS` word 3, `EVENTS_PEEK` 4, `EVENTS_MASK` 5, `START` 6; DMA in block 1: `DMA_HDR` word 16, `DMA_DEST_ADDR` 17, `DMA_START` 18. 17.1, 5.5; v1.35's 4.17.4; A8.1.
- `DMA_HDR`: header block [7:0], `dst` [14:8], `count` [26:15], `queue` [28:27]. 17.1; v1.35's 4.17.9; A8.2.
- A non-`WRITE` DMA packet carries address 0 and leaves `DMA_DEST_ADDR`. 17.4, 4.1; v1.35's 4.17.9; A8.3.
- A peripheral-defined drop is still counted. 17.3, 12.3; v1.35's 4.17.5; A8.4.
- Landing within `PERIPHERAL_LATENCY` ≤ `LAND_LATENCY` − `WRITE_PORT_LATENCY`. 17.3, 19; v1.35's 4.17.5; A8.5.
- A peripheral's `DMA_START` does not clear its `dma_done` (only a load of its `EVENTS` consumes). 17.2; v1.35's 4.17.3 agrees ("no clear pulses").
- A count-0 DMA transfer is busy for one cycle, then `dma_done`. 17.4; v1.35's 4.17.9 ("does nothing, and DMA done is still posted").
- DMA src = the tile's own id; its router input has `ready` = room in the queue the packet enters. 16.4; v1.35's 4.16, 4.17.9; A1.6, A1.7.
- UART: divisor counts the chip's clock, resets to 115,200 baud; receiver and line defined; `UART_STATUS` at word 45 with occupancies [4:0], [12:8], `rx_overrun` [16], `framing_error` [17]. 17.6; v1.35's 4.20; A8.7.
- Host interface: words 64–72, `STATUS.stream_last`, `stream_in`, `stream_out`. 17.6; v1.35's 4.17.3 (deferred); A8.6.
- Ingress/Egress: `START` UB, no writes to its region, no errors of its own but `tile_error`. 17.6; A8.8.
- Ingress/Egress; names (`ARRIVALS`, `ARRIVE_EXPECT`, `RELEASE`, `tile_error`, `all_arrived`); host-interface words 64–72, `STATUS.stream_last`, `stream_in`/`stream_out` events; words 46–63 fault; 17.6; v1.35's 6.6, 8.14; A8.6, A8.8. Verified.
- UART; divisor counts the chip's clock, reset 115,200 baud; 17.6; v1.35's 8.13; A8.7. Verified (v1.35: reference oscillator, 57,600).
- Watchdog; store of N sets limit and count, expiry at the end of t + 1 + N; 17.6; v1.35's 6.1, 8.14; B4.4. Verified (v1.35 text gives no cycle).
- RW engine; words 32–36, one job at a time, `lpddr_error`; replaces the simulator's stub; 17.7; v1.35's 6.7, 7; A8.9. Verified (v1.35: "out of scope").
- Requests to the memory tile; `READ` op 1 and `WRITE` op 2 with fixed layouts; replies 128-bit; a `READ` may ask for a reply; 11.4, 11.5; v1.35's 6.7; A7.5, A7.6. Verified.
- Write buffer parts replace tag windows; the reply names part and counter; `EVENTS.done` frees it; 11.5, 17.7, 23.4; v1.35's 6.7; A8.10. Verified.
- `lpddr` an AXI4 master with stated rules; `fill` four channels; `replay_row`; chip-boundary ports; 3; v1.35's 6.7, 7; A8.11, A8.12. Verified.
- Memory tile's packets' src is its own id; 16.4; v1.35's 6.7 (deferred there), 4.17.9; A1.6. Verified in 6.7.
- Special tiles' scratchpads 4 MiB; 2.2; v1.35's 6.6, 8.2; A1.2. Verified.

### 2.8 Stage 8: the replayer

- **Window layout** (follows the peripheral's control block): `EVENTS` word 3, `EVENTS_PEEK` 4, `EVENTS_MASK` 5, `START` 6 (was `PULSE_RUN` 3, events 5–7); block 1 reads 0 (no DMA); words 32–35 keep their numbers and names. 17.8.1, 17.1; v1.35's 4.19.9, 8.11; A8.1.
- **`ERRORS` packed from bit 0**: `queue_overrun` 0, `invalid_expansion` 1, `code_underflow` 2, `read_log_underflow` 3, `write_log_underflow` 4, `leftover` 5 (each one lower; reserved [0] dropped). Events unchanged in position (done 0, credits 2–5, `new_expansion` 6). 17.8.1; v1.35's 4.19.11; A9.1 (closes item 123).
- **`EXPANSION_OFFSET` is latched at the post** of `new_expansion` and held until the next; it is not the live row offset. 17.8.1, 17.8.6; v1.35's 4.19.9; A9.2.
- **Underflow fires on the row that needs the value**, never on look-ahead: the code underflow at the first row of the instruction that lacks its value, not at decode; rows before it are pushed, the needing row is not. No read-log pre-peek: a `next_from_value` row that does not transfer takes no log word and raises no underflow. A pass that ends before the needing row sets `leftover` instead. 17.8.10, 17.8.12, 17.8.3; v1.35's 4.19.11, 4.19.15; A9.3 (RTL README "meshsim v1.35": "When an underflow is set", "A control transfer's word").
- **A new pc and the program's end only on an expansion's last row**; a row that is not its expansion's last keeps pc whatever its opcode. The shared ALU model must gate it (the Tracer's too). 17.8.7; v1.35's 4.19.7 (text agrees, the simulator did not); A9.4, E1.
- **Core-fed NOP policy with rd = x0**: the core sends the shared NOP row alone, carrying the template's `rd0`; the replayer takes that one word and runs ROM word 1, at any row offset. The simulator's discard of `seq_remaining` further words goes. 17.8.5, 17.8.13; v1.35's 4.19.4; A9.5.
- **Leftover** has three causes: words not all taken, values not all decoded, and more entries entered a queue than the stream's words (a third size counter, 17 bits, entries in, an overrun's dropped push not counted). It may be set in the cycle after `done`. 17.8.10, 17.8.12; v1.35's 4.19.11, 4.19.15; A9.6, E8.
- **Queue entries leave no later than** the use of their last bit, at most one pop a cycle from each queue (the simulator may pop two); credits return on entries left, the count restarting at the state write and, for the expansion queue, at each new expansion. A fill sets a credit to 2 whatever a consuming load takes that cycle. 17.8.8, 17.8.1; v1.35's 4.19.8; A9.7, E8.
- **A queue overrun's dropped push is still counted** by its packet's counter. 17.8.8, 12.3; v1.35's 4.19.8, 4.19.10 ("not promised"); A8.4.
- **Code decoder table is 16 × 32 bits**; a code miss with size bit 1 is UB. 17.8 (parts), 17.8.12; v1.35's 4.19.15; A9.8.
- **New UB the simulator stops on**: a word left in the expansion queue at a new expansion; `offset_imm` on a row that is not `VirtualShiftData…`; `from_log` with `next_from_value`; a reserved expansion-word bit set; `START` with no state write since the reset or since the previous pass's `START`; a row without `next_from_value` whose new pc differs from pc; a ROM word written in the cycle a row reads it; a code miss of 64 bits. Decoder tables and size counters are undefined after the reset until a state write. 17.8.4, 17.8.7, 17.8.8, 17.8.9, 17.8.10, 17.8.12, 17.8.14; v1.35's 4.19.3, 4.19.11; A9.9.
- **Reset** zeroes `STATUS`, `EVENTS`, `ERRORS`, `EVENTS_MASK`, `RUN_CONFIG`, `ROWS_RUN`, `EXPANSION_OFFSET`, `EXPANSION_OPCODE`, and empties the five queues. 17.8.14; v1.35's 8.11 (reset column) against its simulator; A9.10, E8.
- **A template past ROM word 511**: a word at 511 with `seq_remaining` > 0 runs, and the expansion's next row is the invalid one; `start` + row offset > 511 makes the first row invalid. 17.8.6; v1.35's 4.19.6 (which reads as the word at 511 being invalid). Not in the inventory.
- **`START` and a pass of 0 rows**: `STATUS.busy` is 1 for the cycle after the start takes effect (a window store takes effect at the end of t + 1, 6.4) and `EVENTS.done` is posted at its end. When a pass's first row starts is the implementation's. 17.8.10; v1.35's 4.19.9, 4.19.11 ("the first row starts at once"); B1.1.
- **Stall costs** (to reproduce the RTL's cycles): the shared NOP row of a ROM template +2; a load or advice row whose read-log word its decoder has not announced +2; `VirtualPext`/`VirtualPextSigned` with a mask of two or more runs, a cycle a mask bit up to the highest set bit, plus one (≤ 65 past the schedule); a core-fed expansion's first row waits for the core (4 cycles under an ideal controller); no read-log pre-peek. 17.8.16 (contract: 17.8.7 leaves stalls to the implementation); v1.35's 4.19.7; B5, item 140.
- **The row port**: `replay_row`, valid/ready, a row a cycle, ready tied to 1 on the chip until the materializer is specified (the simulator's sink). 3, 17.8.11; v1.35's 4.19.12; A8.12.
- **Firmware** (`fw/replay/replay_ctl.c`): the loop without `EVENTS_REQUIRED` (loop until both `counter_n_done` and `dma_done` are in hand); new names (`START`, `COUNTER_n`, `WRITE_*`, `DMA_START`, tile events `peripheral_1` bit 4, `counter_n_done` bit 6 + n, `dma_done` bit 1); `release` out of the mask during a pass; fetches from LPDDR are `READ` requests to the memory tile; the tables' delivery after the tiles' loads; the core-fed NOP sends one word. 17.8.21, 17.8.22, 17.8.13; v1.35's 4.19.9, 4.19.13; A2.11, A2.13, A4.8, A7.5.
- **`spec/jolt/replayer-opcodes.md`**: no content change; section numbers and "controller-fed" only.

### 2.9 Stage 9: the Tracer and the segment

- Tracer; `EVENTS` packed from bit 0: done 0, dma_done 1, advice_credit 2, expansion_credit 3, new_expansion 4, state/code/read_log/write_log_ready 5–8 (were 0, 1, 3, 5, 6, 7–10). `ERRORS` packed: queue_overrun 0 … misaligned 8 (were 1, 2, 4, 7–12), plus new write_back_error 9. New 17.9.1; old v1.35's 4.21.3, 4.21.8, 8.16. A10.1 (reverses item 144).
- Tracer; control block and DMA block at the peripherals' new words (`START` word 6, `EVENTS` 3–5, `DMA_HDR`/`DMA_DEST_ADDR`/`DMA_START` words 16–18; `DMA_HDR` queue [28:27], count [26:15]). New 17.1, 17.4; old v1.35's 8.16 words 3–9. A8.1, A8.2.
- Tracer; a write-back has a status: `write_response.status` 1 sets `ERRORS.write_back_error` (H), the line lost; stops the Tracer, at the next `START` if idle. New 17.9.5, 17.9.9, `fill` in 3; old v1.35's 4.21.5 "A write-back can't fail". A10.2.
- Tracer; the fill port is four handshaked channels; at most one read and one write outstanding together; the memory-system model must take both. New 3 (`fill`), 17.9.5, 17.9.14; old v1.35's 4.21.5 table. A10.3.
- Tracer; the one-line write-back buffer: place freed at the write's response; a requested write is always made; take-back of an unrequested line makes no write; after a state write a requested write still holds the place and no read of its line is requested until it is accepted. New 17.9.5, 17.9.3; old v1.35's 4.21.5 Ordering. A10.4 (E6: v1.35's simulator has no buffer).
- Tracer; `EXPANSION_OFFSET` is latched when the core-fed expansion is posted and held until the next. New 17.9.1; old v1.35's 4.21.3 (words 33–35). A10.5.
- Tracer; jump target: key = the 64-bit offset tested ≥ 2³² before the LRU, LRU untouched, `STAT_PROBES` not counted; LRU entry (key 32, base 32); a target at offset 0xFFFF_FFFF is UB. New 17.9.4; old v1.35's 4.21.4. A10.6 (E5, E21).
- Tracer; each log stream ends, and gets its tail credit, when the log scan has given it its own last value. New 17.9.6; old v1.35's 4.21.6. A10.8.
- Tracer; after a stopping error no counted event is set (`dma_done` included) and the live events are down, from the cycle after; counts already posted stay; an error while idle stops the next run at its `START`. New 17.9.9; old v1.35's 4.21.8. A10.9.
- Tracer; reset list explicit (17.9.12): zeroes `RUN_CONFIG`, `ROWS_RUN`, `EXPANSION_OFFSET`, `EXPANSION_OPCODE`, length words, hash constants, counters, latch, bitmap, `EVENTS_MASK`; `START` with no state write since reset is UB; `log_k` > 32 UB. New 17.9.12, 17.9.3, 17.9.2; old v1.35's 4.21.8 last paragraph. A10.10.
- Tracer; parts 1 and 3 of the region are unmapped (v1.35: "unused"); a write of another size or to an unmapped offset is UB as before. New 17.9.2.
- Tracer; statistics counters follow 17.9.8's attribution (stall cycle = no row in its schedule; fetch stalls include the redirect and a run's first instruction; data/map from the cycle after the schedule would have completed). New 17.9.8; old v1.35's 4.21.3 words 44–51, 4.21.9. C 157.
- Tracer; pipeline costs beyond the schedule (17.9.17's table): redirect +5, first instruction 6 after `START`'s effect, NOP policy +1, core-fed successor +1, instruction miss fills + ~2, data miss + 5, probe + 4, stream word 2 cycles after the rows' end, `done` ≤ 2 after the last pop. New 17.9.7, 17.9.17; old v1.35's 4.21.9. B6.
- Segment firmware (tracer tile, master tile, replay image); handshakes are requests to target 2: `ARMED` after arming the start and totals counters, `STREAMS_ARMED` after arming the stream counter; the tracer tile sends a tile's record pieces only after its `ARMED` (tile 0's window-0 start included) and its streams only after its `STREAMS_ARMED`; no `stage_delay`, no `COUNT` "armed" packet, no command to start the tracer tile (it is loaded by `LOAD`); three releases. New 17.9.10, 17.9.15, 6.5, 11.5; old v1.35's 4.21.10, 4.21.3 loop. A10.11.
- Segment firmware; the last run's 68 words go to the tracer tile itself with its own id as dst (was dst 66 by number; same thing at 8 × 8), words 1 and 67 forwarded to tile `TILE_COUNT` − 1. New 17.9.10.
- Simulator parameters: `tr_row_cycles`, `tr_mul_cycles`, `tr_probe_compare` become constants of the contract; `tr_probe_lines` → `MAP_PROBE_LINES`; `stage_delay` removed; `ms_line_hit`/`ms_line_miss` kept. New 17.9.18.

### 2.10 The simulator's configuration, the checker, the sort's firmware and the measurements

- Simulator configuration; every parameter renamed to chapter 19's name (table in 19.1); new 19, 19.1; old v1.35's section 10; inventory F.
- Simulator configuration; `ARRIVE_LATENCY`, `RELEASE_LATENCY`, `ERROR_LATENCY` independent, each from 0 up (lift the "< 3" refusal; untie `error_latency`); new 19.1, 18; old v1.35's section 10; B1.8, E28.
- Simulator configuration; removed parameters: area queue depth, write-engine input FIFO, `PTR`/`HIST` request FIFOs, `stage_delay`, start skew, the command block's address, `cc_link_latency`, `cc_dma_depth`; new 19.1; old v1.35's section 10; A6.1, A6.3, A6.4, A4.1, A1.11, A1.7.
- Simulator configuration; `GATHER_QUEUE_DEPTH` 64, `REQUEST_QUEUE_DEPTH` 4, `READ_LATENCY` 3, request tree a cycle a node; new 19, 19.1; old v1.35's section 10; A6.6, A7.2, B3.1, B2.1.
- Checker; the invariants keep their numbers. Messages and names move to v1.36's terms; invariant 1 loses the write-engine and table FIFOs; 8 and 11 keep their reply-block checks as checks of the reference firmware's convention (22.18), the block's address now the image's, and lose the command block; 21 loses the trace/exit words and expects an access fault on an unmapped word; 22's uncounted exception is the load, not commands; 23 covers every trap, the fetch fault now an `ERRORS` bit; 25 is the load's order; new 20; old v1.35's section 9.
- Checker; new checks, invariants 27–34: release before all arrived; `ARRIVE` without the barrier; core store to a transfer's `STREAM`/`GROUP` entries and core access to `PTR`/`HIST` under its engine; `GATHER_CONFIG` store while read; IMEM/DMEM same-word collision; counter sum past 2²⁰ − 1; image conventions (`mstatus.MIE`, ending at the reset PC with counters 0); Ingress/Egress and RW engine UB; new 20; none in v1.35.
- Checker; invariant 14's and 4's exceptions for command writes removed; new 20; old v1.35's section 9; A4.1.
- Sort firmware and its documents; `GROUP` entries: R is `rotations` in word 0 with `first_stream` and `streams`, so a per-pass R store rewrites all three; `spec/schedule.md` section 3; old schedule "only R, in the entry's low word"; A5.3.
- Sort firmware; the planner is no overlay; phases are the image's schedule; replies are `WRITE`s to DMEM addresses; new 27, 6.5; old v1.35's section 14; A4.1, A4.9.
- Measurements; every `[meas: v1.35]` and older figure re-measured; the full-size sort's reference (1,229,687 in v1.35) re-established; new 27, item 164; old v1.35's section 14.

### 2.11 Not built, and deferred or open as in v1.35

- D1 file system, Updater, Binder, tag pool, logs, flush: chapter 23.5 *(Deferred, item 119/60)*. Verified.
- D2 memory system behind `fill`: 23.6, 2.1 *Open*; the 6/80 model stays the simulator's. Verified.
- D3 hard and soft reset: 18 *Open*; 22 keeps v1.35's intent as item 124. Verified.
- D4 host boot over LVDS, the dump over LVDS: *Open* (6.5, 17.6). Verified.
- D5 commands, command block, parity blocks, `stage_delay`, one-at-a-time bring-up, overlays: removed. Verified.

## 3. The timing table

Every path whose cycle count differs between the RTL and v1.35's simulator, and the ones checked and found equal.
**"Contract"** means chapters 1–19 or `spec/core.md` state the figure, so every implementation has it. **"The
RTL's"** means the contract leaves it to the implementation and the figure is what the RTL (Snarky One) does,
from its documents or its measurements in simulation (`[meas: RTL]`). **v1.36's simulator is to reproduce both
kinds**, within what item 163 decides; chapter 21 of `spec/machine-spec.md` is the same table by path, without
the comparison. *t* is the cycle the core issues a store, *p* the cycle after, when the register interface
presents it; *L* is `TABLE_LATENCY`; *a* is the cycle the traffic director accepts a packet; R is the cycle a
wake reaches the core. None of the figures was produced for this guide: nothing was built or run. The v1.35
figures are from its documents and simulator sources; where one was not re-read, the row says so.

### 3.1 The core and the registers (inventory B1.1–B1.7)

| path | v1.36: the RTL's figure, and whether it is contract or the RTL's | v1.35 | sections; source |
|---|---|---|---|
| B1.1 window store's effect | end of t + 1 (a store takes the latency of a load from the same place: window 2, table 1 → effect end of t + L − 1); contract (6.4) | end of t ("single-cycle and posted") | 6.4; v1.35's 4.11 Timing |
| B1.2 load-use | the instruction using a load's result right after it waits 1 cycle; contract (core.md, Core contract: Data) | 0 ("usable by the next instruction") | core.md; v1.35's 4.11 |
| B1.3 taken branch; JALR other than a ready return | next instruction retires 4 cycles after it (+3); contract (Control transfers) | +1 (`branch_penalty`) | core.md; v1.35's 4.11 |
| B1.4 JAL; ready return (JALR through x1, x1 committed) | next retires 4 cycles after the jump enters execute: +2 after its retirement, fewer if it waited in execute, at least 1 cycle after; contract | +1 | core.md; v1.35's 4.11 |
| not-taken branch | +0; contract | +0 | core.md |
| B1.5 multiply | two pipelined stages, no cycle of its own; +1 if an operand is still being produced by the instruction in writeback; +1 for an immediate user of the product; contract (Multiply) | 3 cycles | core.md; v1.35's 4.11 |
| instruction after a CSR write | held 1 cycle in execute; contract (Pipeline event priority) | none | core.md |
| B1.6 wake → next instruction | wake in cycle R: next instruction retires in R + 3; `WFI` retiring with the wake already pending: next retires 3 cycles after the `WFI` (+2); contract (WFI, Lockstep regions) | resumes the same cycle; +1 with the wake pending (inventory; simulator side not re-read) | core.md; v1.35's 4.11, 6.3 |
| DMEM load/store | one a cycle, response the next cycle; contract (Data; machine 6.3) | one a cycle | core.md |
| window / table load | 2 / 1 cycles; contract (6.4) | 2 / 1 | no difference |
| B1.7 CoreMark | 1.312 cycles per instruction, 2.776 CoreMark/MHz; the RTL's measurement `[meas: RTL]`, unofficial (performance run only) | 1 instruction a cycle model, +1 a taken branch, 3 a multiply | core.md, The core's cycle costs |
| trap / NMI entry cost | not stated (every trap is terminal) | not stated | core.md; ruling list |

### 3.2 The nets, the barrier and the boot (B1.8–B1.10)

| path | v1.36: the RTL's figure, and whether it is contract or the RTL's | v1.35 | sections; source |
|---|---|---|---|
| nets (B1.8) | arrive 0 (wire), release 1, error 0; "the chip's", RTL placeholders | 8/8/8; < 3 refused (`chip.cpp:180`, confirmed) | 18, 19; chip README line 25 |
| empty phase (B1.9) | 35 release to release; RTL's measurement | 43 on v1.25 at latency 1; 57 at v1.35 defaults (not re-verified) | chip README line 177–179 |
| boot (B1.10) | reset → `REQUEST_SEND` 26; `COUNT`'s decrement → `ARRIVE` 9 (or → image 17 without barrier); release → image 13; reset → image with `boot_local` 27; ROM 44 words; RTL's measurement | none; 48 words | core README lines 202–208 |
| `RELEASE` store → `BARRIER_RELEASE` pulse | effect at end of t + 1, then `RELEASE_LATENCY` (the chip's; the RTL chip: one flip-flop) | `go_latency` after the store (default 8) | 17.6, 18; v1.35's 6.3 |
| Tile ready → `BARRIER_ARRIVE` at the master | pulse the cycle after ready, then `ARRIVE_LATENCY` (the chip's; the RTL chip: a wire) | `ready_latency` (default 8), from the edge of done & command landed | 7.3, 17.6; v1.35's 6.3 |
| Release → first read, stream transfer / group transfer | to be measured on v1.36's simulator; lead-in 2L + 2 / 4L + 3 from the start store's cycle is contract (8.2) | 12–13 / 20–21 [meas: v1.35], with 0/+1 skew | 22, 8.2; v1.35's 6.4 |
| Start skew | none: contract (7.3) | random 0 or +1 | 7.3; v1.35's 6.4, 3.2 |
| Earliest arrival vs arming | no figure: ordered by a request or by lockstep | `stage_delay` 64 cycles after the release store | 6.5, 17.9.10; v1.35's 6.2 |

### 3.3 The NoC and the requests (B2)

| path | v1.36: the RTL's figure, and whether it is contract or the RTL's | v1.35 | sections; source |
|---|---|---|---|
| Attach link (B2.4) | 3 stages, a hop of 4: contract (16.1, every link) | simulator `cc_link_latency` 1; spec gives no number for it though 3.2 says every link is 3 | 16.1; v1.35's 3.2, 4.16 |
| Router overflow (B2.5) | signalled in the cycle of the push: RTL (router README: `overflow` "in this cycle"); the `ERRORS` cycle not traced | counted at t + 1 (D28) | 16.3, 7.4; v1.35's 4.7 |
| Mesh link and hop (B2.6) | 3 and 4: contract (16.1) | same | 16.1; v1.35's 3.2 |
| Local input → eject, h hops, unloaded | t + 4h + 2: RTL (router README) | not stated | 16.1 |
| request store → loadable (B2.1) | 11 (compute), 10 (special): store t, pushed end of t + 1, beat 0 offered t + 2, at target t + 4 (t + 3), beat 7 moves t + 11 (t + 10), a window load issued in that cycle samples it; RTL's (logical minimum, no link stages) | 18 | request_unit README Timing, request_tree README Timing |
| tree back-pressure (B2.2) | each buffer's ready = room at the start of the cycle; RTL's | credit seen 3 late (`reqbus.h:61`, confirmed default 3) | request_tree README |
| receive-queue credit (B2.3) | pulsed in the pop's cycle, usable the next; RTL's | 3 (1 in mode 0) (`reqbus.h:532`) | request_unit README |

### 3.4 The tile's units (B3)

| path | v1.36: the RTL's figure, and whether it is contract or the RTL's | v1.35 | sections; source |
|---|---|---|---|
| Scratchpad read, take → row / injection (B3.1) | `READ_LATENCY` = 3: the parameter is contract as "the implementation's" (15, 19); 3 is the RTL's (scratchpad README) | 4 in the spec (*Open*), 5 in the simulator | 15, 14.3; v1.35's 4.1 |
| Read, take → router's local input (B3.2) | `READ_LATENCY` + 1 = 4: the +1 link register is contract (14.3) | 6 in the simulator | 14.3 |
| Address generator, offer → take (B3.3) | same cycle: contract (8.3–8.5: a visit is made when the arbiter takes its read; timetable times are takes) | one cycle later, through a holding register (simulator) | 8.3, 14.1; v1.35's 4.2 |
| Start store → first packet at the router, stream transfer (B3.4) | 2L + 2 (contract, 8.2) + `READ_LATENCY` + 1 = 2L + 6 at the RTL's 3: derived | 2L + 9 (derived from the simulator) | 8.2, 14.3 |
| Group lead-in, first group with R = 0 (B3.5) | 4L + 3: contract (8.2) | 2L + 2 in the simulator; spec has both (E13) | 8.2; v1.35's 4.2 |
| Stream lead-in | 2L + 2 after the start store: contract (8.2) | same | 8.2; v1.35's 4.2 |
| `dma_done` (B3.6) | set when the last table read has completed: contract (8.2); the cycle after the walk completes: RTL (DMA unit README) | three offsets by case (simulator): one later, one earlier, one earlier for an empty transfer | 8.2; v1.35's 4.2 |
| Empty transfer | busy for the one cycle after the start takes effect, `dma_done` at its end: contract (8.2) | "ends at once" | 8.2; v1.35's 8.5 |
| Read-unit store → read offered (B3.8) | t + 2: store's effect at the end of t + 1 is contract (6.4); offered from the next cycle is RTL (read unit README) | t + 1 (inferred from the simulator) | 10, 6.4; v1.35's 4.3 |
| Director acceptance a → scratchpad write (B3.9) | presented a + 2L + 2: RTL (receive unit README); contract only "within `LAND_LATENCY`" (12.3) | "by 2L + 4", into the area's queue | 12.3; v1.35's 4.8 |
| a → `PTR`/`HIST` updated (B3.10) | read end of a + L, written end of a + L + 1: RTL (receive unit README) | complete by L + 4 | 12.3, 12.5; v1.35's 4.8, 4.10 |
| a → `noc_write` to IMEM, DMEM, a peripheral | `WRITE_PORT_LATENCY` = 2: contract (12.3) | `cm_latency` 2, `rp_latency` 2 | 12.3; v1.35's 4.8 |
| `LAND_LATENCY` (B3.11) | 8: contract (12.3); the RTL's paths fit for L ≤ 3, 4 refused (receive unit README) | K = 8, formula covers L ≤ 2 | 12.3; v1.35's 4.8 |
| `GATHER` accepted a → first read taken (B3.12) | a + 2: RTL (gather unit README) | a + 2 | 13; v1.35's 4.4 |
| Gather rate (B3.13) | none stated; chip uniform-k128 gather steps +2.2 % / +2.5 % over v1.25's simulator: RTL's measurement (inventory; not re-checked) | ≈ 2.0 cycles a value at *k* = 2 | 13, 14.1; v1.35's 4.4 |
| Bank request queue (B3.14) | none | depth 8 | 15; v1.35's 4.1 |
| Table read round trip, `READ_AHEAD` (B3.15) | 2L + 1, 2L + 2: contract (8.5) | same | 8.5; v1.35's 4.2 |
| Loopback token freed (A6.9) | the cycle the director takes the copy: contract (14.2) | when it leaves the loopback FIFO (about a cycle earlier) | 14.2; v1.35's 4.5 |
| Send store → earliest injection (B3.7) | *t* + 2: the store's effect at the end of *t* + 1 is contract (6.4); injection the next cycle is the RTL's | *t* + 1 | 9, 14.3; v1.35's 4.11 |

### 3.5 The peripherals (B4)

| path | v1.36: the RTL's figure, and whether it is contract or the RTL's | v1.35 | sections; source |
|---|---|---|---|
| landing in a peripheral (B4.1) | end of the cycle presented = director + 2; bound is contract | `rp_latency` 2 | 17.3; receive_unit, rw_engine README |
| peripheral DMA start → first packet (B4.2) | offered t + 3 (presented p = t + 1, taken into the packet register p + 1, offered p + 2); RTL's | in the DMA FIFO from t + 2 | peripheral README Timing |
| DMA back-pressure (B4.3) | router queue room (16); contract (16.4) | 4-deep credit FIFO | 16.4 |
| watchdog (B4.4) | end of t + 1 + N; contract | t + N | 17.6; ingress_egress README |
| UART frame (B4.5) | 10 × divisor chip cycles (86,810 at 8681); contract | 10 × divisor × ref cycle | 17.6 |
| RW engine (B4.6) | AR/AW from p + 2 (= t + 3), W from p + 3, a beat a cycle; RTL's | none | rw_engine README Timing |
| Fill: hit / miss | Open (2.1); the simulator's model 6 / 80 | 6 / 80 | 23.6; v1.35's 7 |

### 3.6 The replayer (B5)

| path | v1.36: the RTL's figure, and whether it is contract or the RTL's | v1.35 | sections; source |
|---|---|---|---|
| A row's schedule | 3 cycles; 5 for class M: contract, unchanged | the same | 17.8.7; v1.35's 4.19.7 |
| A pass, block level, ideal controller, EVM Zone 128 windows (`evm-zone-23`) | 25,818,054 cycles, 3.0778 a row; schedules 25,814,298, stalls 3,756 [meas: RTL] | 25,832,928, 3.0795 a row; stalls 18,630 [meas: v1.26.1's Java model, Stage A, one-off run on the same windows; v1.35's C++ not re-run] | 17.8.16 |
| The same, Cyclone 64 windows (`cyclone-22`) | 13,010,346, 3.1019; schedules 13,006,362, stalls 3,984 [meas: RTL] | 13,015,958, 3.1032; stalls 9,596 [meas: v1.26.1] | 17.8.16 |
| RTL against the model | −0.06 % (EVM Zone), −0.04 % (Cyclone): all of it stalls | — | 17.8.16 |
| A pass on the RTL's chip, the replay firmware as the controller | 3.0875 a row (EVM Zone), 3.1249 (Cyclone) [meas: RTL] | Stage B 3.2116 / 3.1105 at 2²⁹ windows [meas: v1.26, other windows: not comparable] | 17.8.16 |
| First row of a core-fed expansion | +4 cycles under the ideal controller; 76.1 (EVM Zone) and 94.4 (Cyclone) on average on the chip with the firmware [meas: RTL] | +2 with a controller answering in the event's cycle | 17.8.16, 17.8.21 |
| A window's first row, queues empty at `START` | 1,066 cycles over 128 windows, 540 over 64 [meas: RTL] | 135 and 123 cycles a window of queue waits | 17.8.16 |
| The shared NOP row of a ROM template | +2 [meas: RTL; not in the recorded windows, met in the synthetic ones] | 0 | 17.8.16, 17.8.5 |
| A load or advice row whose read-log word its decoder has not announced | +2 (9 and 2 rows in the recorded windows) [meas: RTL] | 0 | 17.8.16 |
| A `next_from_value` row | no pre-peek: the word is taken only if the row transfers | a read-log peek before every such row, stalling until a value or the stream's end is known | 17.8.3, 17.8.16 |
| `VirtualPext`/`VirtualPextSigned`, mask of two or more runs | a cycle a mask bit up to the highest set bit, plus one: ≤ 65 past the schedule [the RTL's choice; contract: "as many as the implementation's"] | 3 cycles, reported as a statistic | 17.8.7, 17.8.16, 17.8.20 |
| LRU-coded streams against raw | +14 cycles over EVM Zone's 128 windows, +8 over Cyclone's 64 [meas: RTL] | not measured separately | 17.8.16 |
| `START` store → `STATUS.busy` | the store takes effect at the end of t + 1 (6.4); first row's start is the implementation's | busy from the end of the store's cycle; "the first row starts at once" | 17.8.10 |
| Entry release, credit return | no later than the use of the entry's last bit, at most one pop a cycle; the RTL releases an entry when fewer than 66 of its bits are left | when the stream's bits have passed the entry; may pop two in a cycle | 17.8.8 |

### 3.7 The Tracer (B6)

| path | v1.36: the RTL's figure, and whether it is contract or the RTL's | v1.35 | sections; source |
|---|---|---|---|
| Tracer row, class S or L / M | 4 / 6: contract | 4 / 6 (`tr_row_cycles`, `tr_mul_cycles`) | 17.9.7 |
| Row that sets its successor's pc (control transfer, branch taken or not, program's end) | successor fetched only after it completes: contract (17.9.7); +5 cycles, every access a hit: RTL's measurement (was +6 before RTL commit 3fb7a82; the RTL README's synthetic-comparison row still says +6, stale) | 0 | 17.9.7, 17.9.17 |
| Run's first instruction (not left inside an expansion) | starts 6 cycles after `START`'s effect: RTL's measurement | the cycle after `START` | 17.9.17 |
| NOP-policy row; core-fed instruction's successor | +1; +1: RTL's measurement | 0; 0 | 17.9.17 |
| Instruction miss / data miss | fills + about 2 / fill + 5: RTL's measurement | fill latency exactly | 17.9.17 |
| Map probe | 2 cycles a line after its response: contract; +4 around the reads: RTL's measurement | 2 a line (`tr_probe_compare`) | 17.9.7, 17.9.17 |
| Rows' end → stream word; length words and `state_ready` | 2 cycles after; the cycle after that: RTL's measurement (contract says "when the rows end") | `state_ready` the cycle after the last row, scans from the cycle after that | 17.9.6, 17.9.17 |
| Drain | one FIFO entry a cycle a scan, a packet a cycle: contract; about 54,650 / 53,240 cycles a window (EVM Zone / Cyclone): RTL's measurement | same rule; within 0.02 % | 17.9.7, 17.9.17 |
| `done` | at most 2 cycles after the DMA's last pop: RTL's measurement | the cycle after | 17.9.17 |
| Fill latency | the memory system's, Open; RTL tests use 1 (block) and 2–40 (chip) | model: hit 6, miss 80, depth + 1 accesses | 17.9.14 |
| Real workloads, block level, cycles a row (EVM Zone / Cyclone) | 4.4631 / 4.4168 at fill latency 1; 4.6506 / 4.7396 at v1.3's mean waits: RTL's measurement | 4.2697 / 4.4331 [meas: v1.3 Java, quoted by the RTL README; not re-run on v1.35's C++] | 17.9.17 |
| Chip level, first `START` to last `done` | 5.51 / 5.26 / 5.38 cycles a row; 23.1 M / 22.1 M / 22.6 M cycles a segment: RTL's measurement | about 21 M a segment [meas: v1.3] | 17.9.17 |

### 3.8 The sort, end to end (B7)

The RTL's sort was compared with **v1.25's simulator at net latencies of 1**, not with v1.35's: the sort's sources
are the same in v1.25 and v1.35, but v1.35's defaults are 8, 8 and 8, and its one committed full-size reference
is uniform-k128. The comparison is to be run again on v1.36's simulator (item 164). The RTL attributes the gap to
the core's timing: the steps the engines drive match within 0.2 %.

| path | v1.36: the RTL's figure, and whether it is contract or the RTL's | v1.35 | sections; source |
|---|---|---|---|
| the sort, 64 × 16, 32-bit values | 150,261 [meas: RTL] | 123,473 (v1.25 simulator, nets at 1) | 27; B7 |
| the sort, 64 × 16, 128-bit values | 153,013 [meas: RTL] | 125,980 (v1.25, nets at 1) | 27; B7 |
| the sort, 64 × 64K, 32-bit values | 952,154 [meas: RTL] | 894,163 (v1.25, nets at 1) | 27; B7 |
| the sort, 64 × 64K, 128-bit values | 1,306,907 [meas: RTL] | 1,229,959 (v1.25, nets at 1); 1,229,687 (v1.35 at its defaults, `RUNNING.md`) | 27; B7 |
| by step, 64K kv32 (RTL / v1.25) | L1 335,118 / 334,368; SPLIT 259,473 / 206,666; XB 9,856 / 7,571; RED 6,664 / 5,137; K 71,524 / 71,498; L2 268,921 / 268,321; 14 empty phases 598 / 602 [meas: RTL] | | the RTL chip's results, where it was verified |

## 4. Three reversals of recorded rulings, for the owner

v1.36 takes the RTL's hardware, and in three places that hardware goes against a ruling the owner recorded in
v1.3 or v1.35. v1.36's text states the RTL's rule in each, so that the specification describes one machine; each
is an open item (160, 161, 162) until the owner rules. If a ruling goes against the RTL, the RTL and this
specification change together.

### 4.1 A peripheral DMA's packets carry the tile's own id as src (item 160, against item 118)

- **The ruling.** v1.35's 4.17.9: a sending peripheral's DMA stamps src = the controller's id + 8 (72, 73, 74),
  "(owner, 2026-11-05, item 118)". A receiver could then tell a special tile's DMA from its core: an `APPEND`
  from the DMA lands through `PTR[74]`, one from the core through `PTR[66]`.
- **v1.36** (16.4): "Each sending peripheral's DMA enters the special tile's router on one of its unused link
  inputs, as a port of its own, and its packets have the special tile as src." And: "v1.35's src for a DMA, the
  tile's id + 8 (item 118), is not taken: its routing by dst, and its ejection of a packet for the tile itself,
  are this rule."
- **The RTL's reason,** as its specification gives it: what the separate id was for in the router is done by the
  input. The DMA has a router input of its own (16.4), packets are routed by dst alone (16.2), and a DMA's packet
  for its own tile ejects like any arrival and takes no token. A tile id is `TILE_ID_BITS` wide, with room for
  the compute tiles, the three special tiles, broadcast and none (4.1): ids 72 to 74 exist only at 8 × 8, and
  the mesh's side is now a parameter. The RTL's documents state no further reason.
- **What it costs.** At a receiver the DMA and the core of one special tile share `PTR[src]` and the order rules
  of one source: 14.4's "one sender per `PTR` entry" then forbids the two from feeding one receiver's `PTR`
  entry at once, where v1.35 let them. The Tracer's streams are `WRITE`s into the replayers' queues, which are
  ordered ports under the same rule (17.3). Firmware that set `PTR[74]` for the Tracer's deliveries sets
  `PTR[66]`.
- **To rule:** src = the tile's id (the RTL's, v1.36's text), or the id + 8 restored, which needs ids for the
  three DMAs at every mesh size and the RTL's router to stamp them.

### 4.2 The Tracer's `EVENTS` and `ERRORS` are packed from bit 0 (item 161, against item 144)

- **The ruling.** Item 144, accepted (owner, 2026-11-05): the Tracer keeps, for the bits it shares with the
  replayer, the replayer's positions, with gaps where it has no such bit (v1.35's 4.21.3 and 4.21.8).
- **v1.36** (17.9.1): "`EVENTS`, packed from bit 0 (5.4)" and "`ERRORS`, sticky until the reset, packed from bit
  0". The positions, old → new: `done` 0 → 0, `dma_done` 1 → 1, `advice_credit` 3 → 2, `expansion_credit` 5 → 3,
  `new_expansion` 6 → 4, `state_ready`, `code_ready`, `read_log_ready`, `write_log_ready` 7–10 → 5–8; the errors
  1, 2, 4 and 7–12 → 0–8, with `write_back_error` new at 9 (section 7.7 has each).
- **The RTL's reason** is its one rule for every register (5.4): "Fields are packed from bit 0, in the order
  their tables list them." The replayer's `ERRORS` moved down one by the same rule (17.8.1, which closes item
  123). The RTL's documents state no reason beyond the uniform rule.
- **What it costs.** Code shared between the replayer's and the Tracer's cores can no longer use one set of bit
  masks for both; `fw/include/mesh.h` gets two sets, and v1.35's `fw/tracer/tracer_ctl.c` is renumbered.
- **To rule:** packed (the RTL's, v1.36's text), or item 144's positions restored in the RTL and here.

### 4.3 The segment's handshakes are requests (item 162, against v1.35's 4.21.10)

- **The ruling.** v1.35's 4.21.10 (owner, 2026-11-05 and 2026-11-06): the segment is staged by time. The sender
  waits `stage_delay` = 64 cycles after its release store before it sends counted data, a tile's "armed" is a
  `COUNT` packet, and there are "no 'booted' packets and no request-tree barrier".
- **v1.36** (17.9.10): "**The handshakes** are requests to the tracer tile (target 2, 11.5), so no timing is
  assumed, as the load waits for its `LOAD` (6.5)": a compute tile, woken by the release, arms its counters and
  sends `ARMED`; at its window, once its totals have landed, it arms its stream counter and sends
  `STREAMS_ARMED`; and "the tracer tile sends a tile nothing its request has not allowed". The segment has
  three releases.
- **The RTL's reason,** as stated: "so no timing is assumed". A counted packet that beats its counter's arming
  is a counter underflow (12.2); v1.35 kept the arming ahead of the data by a software constant tuned at
  bring-up, which rests on the core's path from the release to the arming being shorter than the constant. With
  the RTL's core, whose cycle counts are its own (6.4), and with no commands, so that nothing "lands" to start
  the tracer tile (6.5), the RTL orders the two by a request, as its boot orders a load by `LOAD`. The request
  tree is the path built for it: flow-controlled, and the requester field names the tile (11.3).
- **What it costs.** Two requests a compute tile a segment, within the tracer tile's limit of 1 outstanding
  (11.3), and their trip on the tree, 11 cycles each unloaded (section 3.3). `stage_delay` goes, and with it
  the last use of item 125's window. The `op` codes are the tracer tile's firmware's (item 176): the RTL's
  firmware uses 1 and 2.
- **To rule:** requests (the RTL's, v1.36's text), or v1.35's staging by `stage_delay` restored, which needs a
  constant that covers the RTL's core.

### 4.4 Also decided by the RTL without a recorded ruling

Not reversals, but places where the RTL's hardware replaces a decision of v1.35's that the owner may want to
see: each is an open item of chapter 26 marked "for the owner to confirm" or "needs a decision". Items 64 (the
request queue of 4, with no room event), 107 (a scratchpad size must be a power of two, which 4.25 MiB is not),
116 (read-unit reads against arriving traffic: the RTL's specification states no rule, and v1.35's is kept), 123
(the replayer's `ERRORS` positions, closed as the RTL's), 163 (how exact the simulator must be), 168 (a core
access to `PTR` or `HIST` that meets its engine is undefined), 169 (the gather queue at 64 and the address
generator held off while the gather unit is busy), 170 (`READ_LATENCY` and "a write wins its bank" as contract
or implementation), 171 (no start skew), 172 (the UART on the chip's clock, at 115,200 baud) and 173 (bring-up
with every compute tile in every release, no overlays).

## 5. Beyond the inventory: further differences, and where the RTL's specification is silent

Writing v1.36 meant reading v1.35 and the RTL's specification side by side, section by section. This section
lists what that turned up beyond the inventory: **5.1**, every further difference between them (the RTL's
specification wins, and v1.36 states its rule); **5.2**, every place the RTL's specification is silent or
ambiguous where the simulator needs a ruling, every rule kept from v1.35 that the RTL's specification does not
state (each is flagged in the text where it stands), and the inventory's items that could not be confirmed.

### 5.1 Differences the inventory lacked

In these lists "S *n*" is section *n* of the RTL's base tile specification, which is v1.36's section *n*, and a
README is one of the RTL's (Snarky One's) partitions'.

**The core.**

Differences between the RTL's core spec and v1.35 that the inventory lacks:
1. Instruction after any CSR write is held one cycle in execute (core.md Pipeline event priority). v1.35's 4.11: one instruction a cycle, nothing for CSRs. (Inventory's closing table mentions "CSR hold"; B1 has no row.)
2. MEIP is a real interrupt: with `mstatus.MIE` = 1 and `mie.MEIE` = 1 it is taken as a trap, cause 0x8000_000B (core.md Traps). v1.35's 4.11: the wakeup "resumes the core from `WFI` without a trap" unconditionally.
3. `WFI` wakes only on `(mip & mie) != 0` or a takeable NMI: with `mie.MEIE` clear the wake term does not release `WFI` (core.md WFI). v1.35's wakeup input has no enable.
4. Trap CSR contents are fixed: `mtval` = the original instruction bits for an illegal instruction, the data address for load/store faults, 0 for an instruction access fault; `mepc` bit 0 reads 0 (core.md Traps, CSRs). v1.35's 4.11/4.13 do not state them (the 24-report handler read them unspecified).
5. `mcycle` and `minstret` are writable 64-bit counters; `minstret` exists (v1.35: `mcycle`, `mtime` only). Part of A3.4 but `minstret`'s behaviour on traps/writes is new.
6. General-purpose registers x1–x31 are not reset; software initialises them (core.md Memory and reset interface details). v1.35's spec is silent.
7. Fetch runs ahead and holds up to 16 bytes past a `WFI`; code a core may fetch must not be written until the next redirect; code written while the core runs is entered only by a redirect, after a notice ordered behind its writes (core.md Memory and reset interface details, Obligations: IMEM writes). v1.35's 4.11: "Every instruction is fetched as stored, so code written into program memory through port B runs the next time it is fetched", with no fetch-ahead window.
8. Illegal: a coprocessor instruction when `coprocessor_present` is 0; a CSR write to a read-only CSR; `FENCE.I`. (Second and third not in v1.35's text.)
9. An accepted store when a reset begins may still be written to DMEM (core.md Obligations: Reset; machine-spec 18 Kept). v1.35: silent.
10. Lockstep is a written contract with firmware rules (no data-dependent control flow, no polling, `czero` for data-dependent choices, software division branches on operands so a region divides only values equal on every core). v1.35's 6.3 has the synchronised start but none of these rules. (Inventory lists it as "unnumbered" under C; no A/B row.)

**The packet, the NoC and the tile's units (v1.35's sections 1 to 4.10).**

1. **Read unit's destination.** v1.35's 4.3: a 17-bit byte offset in data memory, "the destination stays in data memory". S 10: `READ_DEST_ADDR` is a full 32-bit address, "the next read's address in DMEM; any other is UB". (The inventory's A5.10 gives the register split, not that the region is now software's to keep.)
2. **Broadcast streams.** v1.35's 4.2: "meshsim errors loudly on a stream with *d* = 126 from a mesh tile", by convention only controllers broadcast from streams. S 8.3, 14.2: no such restriction; a compute tile's broadcast is an ordinary loopback packet. Kept as a convention, flagged.
3. **`GATHER` packet size.** v1.35's 3.1: "sent at 64 bits". S 4.2, 13: no size stated; the ordinal is payload bits [51:32].
4. **"One user per phase" of `PTR`** (v1.35's 4.9: `PTR[src]` or `PTR[t]`, never both): not in S 12.4/12.5. Kept, flagged.
5. **Mixed sizes within a group** (v1.35's 4.2, *(Proposed)*, item 27): S 8.4 is silent. Kept, flagged.
6. **`HIST` count past 17 bits:** v1.35's 4.10 "an error (invariant 6)"; S 12.5 "UB (1.3)". Same class in the new vocabulary, but v1.35's word "error" elsewhere means an `ERRORS` bit.
7. **A `PTR`/`HIST` core load meeting the engine:** beyond A6.5: S 12.5 also says "a program uses `PTR` or `HIST` only while its engine is idle", and makes `PTR` loadable; v1.35 forbade loading `PTR` at all.
8. **A core access past a memory's end.** v1.35's 2.2: high offset bits ignored, the access aliases. S 5.1: "An address past the end of its memory is UB"; the core specification faults it (A2.15 covers the core; the base spec's own sentence still says UB for a packet's).
9. **A sub-word access in region 0xFF is an access fault** (S 5.1); v1.35's 2.2 and 1 make misaligned or partial accesses UB and do not define sub-word register accesses (its 8.1: "one `lw` or `sw`").
10. **The reserved-id drop needs no error and no check** (A1.8) — additionally S drops "none as dst"? S 16.2 says only "a reserved tile id"; S 4.1 says none is "as src, an idle packet". A dst of none is not classified by S. See D1-rulings.
11. **`STREAM_HDR.dst` on a compute tile** is relative only "below `TILE_COUNT`"; otherwise "an absolute tile id, a special tile or broadcast" (S 8.3). v1.35 calls 127 "invalid" and 67–125 "reserved (no endpoint)": S makes both UB by 1.3's general rule only.
12. **Attach-output and eject arbitration with attach queues.** S 16.3 fixes the arbiters' queue order "local, west, east, north, south, then the attach ports (master, memory, tracer)"; v1.35's 4.7 gives the feeders' order but not the fifth port's place in the round-robin (its simulator's order is A1.5's subject).
13. **Request-tree arbitration order.** S 11.3: a row node's inputs in column order, the root's in row order, then the master, memory and tracer tiles; v1.35's 3.3 states round-robin "from the one after the input it last served" without fixing the root's input order.
14. **Cut-through** (S 11.3: a node passes each beat on as it arrives) is stated in S; v1.35's 3.3 says "once it has chosen an input it passes that request's 8 beats" and marks beat-0 routing *(Proposed)*.
15. **The requester field's width** follows `TILE_ID_BITS` (upper bits 0) in S 11.2; v1.35 fixes 7 bits.
16. **Receive-queue depth is a formula** in S 11.3 (a power of two ≥ `TILE_COUNT` × limit + 64); v1.35 gives the three numbers.
17. **`gather_overflow` sources.** S 13 names the write unit and a peripheral's DMA as senders that can fill the gather queue; v1.35's 4.4 argues the send port cannot outrun the engine and classes the overflow H (E23).
18. **`record_base` shared.** S 13: a store to `GATHER_CONFIG` while the gather unit is busy, or while an `AUGMENT` that reads it has not yet decremented its counter, is UB. v1.35 has no such rule for `DMA_RECORD_BASE_ADDRESS` (only "the receiver sets it for the run").
19. **A token freed in a cycle can be taken from the next; the read arbiter takes a token before the write unit** (S 14.2). v1.35's 4.5 does not order the two takers.
20. **Injection → router's local input takes a cycle** (S 14.3, a link register stage) as contract; v1.35 has `intra_tile_latency` as a parameter.
21. **Write-unit loopback tokens**: same rule both sides; but S 16.4 adds that a peripheral DMA's packets to its own tile take no read token.
22. **Count-0 streams.** S 8.3: with a gap of 0, or as the transfer's last stream, a count-0 stream takes no cycle, and every such stream whose entry is in passes in one cycle. v1.35's 4.2: "take no cycles while their entries are in; a run of them longer than the queue takes about one cycle each". Compatible; S is exact.
23. **`STREAM` reads cross at most one group boundary a cycle** (S 8.5) is new, exact text for v1.35's "one group per cycle".

**The core's place, events, errors, peripherals and requests (v1.35's 4.11 to 5).**

1. **`ARRIVE` clears the counter events but neither arms nor disarms them.** RTL spec 7.2 lists `ARRIVE` under "also cleared by"; RTL (tile_control_unit README, Decisions): a counter still counting at `ARRIVE` sets its events when it drains, under the gate too. v1.35's 4.12: the `done` pulse clears the events "with their qualifiers" (disarms), and no edge is taken while the gate is closed. (Inventory A4.4 says "does not disarm"; the "clears" half and the README's source are added here.)
2. **Silent traps now run the handler with no NMI ever after.** RTL 6.2: "a core that has taken any trap since reset takes no NMI". v1.35's 4.13: `MIE` cleared on entry, same effect, but before the image enables interrupts an error took no trap at all; v1.36 has no such window. A misaligned load/store is a new silent trap (A3.5 covers the trap, not that it sets no `ERRORS` bit: RTL 6.2 last paragraph).
3. **The ROM's handler is reachable for the whole life of a tile that never installs its own**, and `mtvec` is set again "on every entry" of the ROM (RTL 6.2, 6.5 step 1). v1.35's 4.13: unreachable once an image has installed its own.
4. **A peripheral's `STATUS.busy` / `dma_busy` rise at the store's effect, the end of t + 1** (RTL 17.2, 17.4, 6.4). v1.35's 4.17.2, 4.17.9: "from the end of the run store's cycle". Follows from B1.1 but is stated separately in v1.35.
5. **A count-0 peripheral DMA transfer is busy for one cycle** (RTL 17.4). v1.35's 4.17.9: "does nothing, and DMA done is still posted". (A5.8 covers only the tile's DMA.)
6. **A store to `DMA_HDR` or `DMA_DEST_ADDR` during a transfer is UB** (RTL 17.4). v1.35's 4.17.9: "undefined (meshsim flags it); a load returns the live, advancing values". The RTL's implementation latches `DMA_HDR` at the start and keeps `DMA_DEST_ADDR` live (peripheral README, Decisions); the RTL's spec does not say what a load returns mid-transfer (see rulings).
7. **A store to a DMA-less peripheral's block 1 is UB** (RTL 17.1); v1.35's 4.17.4: reserved words ignore stores, the simulator flags it. Same check, different class; and the DMA-less `PULSE_DMA` word (v1.35 word 4, "unused") is now inside block 1, which reads 0.
8. **A peripheral's `DMA_START` does not clear `EVENTS.dma_done`**, unlike the tile's (RTL 17.2, explicit). v1.35's 4.17.3 implies it ("no clear pulses") without saying so against 4.12's `PULSE_DMA` clear.
9. **`RUN_CONFIG` shared layout** (v1.35's 4.17.4 "words 32–35 of the running peripherals"): the RTL's spec has no shared paragraph; each of 17.8.1 and 17.9.1 defines its own.
10. **The live events reach the wake one cycle after a load can read them** (RTL tile_control_unit README: `meip` from flip-flops; RTL spec 17.2 allows the lag). v1.35's 4.12: "the hardware aims to make the bit current by the next instruction". Applies to `request_pending` too, which v1.35 does not mention.
11. **The arrival gate with an `ARRIVE` in the release's own cycle** lasts through the next cycle alone, and one in the cycle after the release starts a new gate (RTL README, Decisions; RTL spec 7.3 says "from `ARRIVE` until the cycle after the release" and "the release wins" for `STATUS.arrived`). v1.35: the simulator applies GO first, then the store.
12. **An `ARRIVE` on a tile without the barrier's nets is UB** (RTL 7.3). v1.35's 6.3 lets C&C 2 and 3 use `done`/"command landed" for their own commands; with commands gone the special tiles do not arrive.
13. **`EVENTS_MASK` bits exist for live events too** and the tile's `request_pending` mask bit resets to 0 (same in both); but v1.35's reset mask [8]|[7] included "counters done" which the boot ROM relies on in v1.36 (6.5), where v1.35's ROM relied on the pending "command landed". No inventory row names the ROM's dependence on the reset mask.
14. **Write-only words read 0, with no exception**: v1.35's 4.11 excepts the request port's write-only words, which read as the receive side. Follows A7.1.
15. **A handler that sends many reports now stalls**: request queue 4 (A7.2 notes it); in addition report numbers 24 and 25 are the ROM's by hardware (the ROM), and an image's own set "numbered from 0" may not exceed 255 (`number` is 8 bits, RTL 11.5) where v1.35's draft had 11 bits.
16. **UART `UART_STATUS` clears**: RTL 17.6: "an error raised in the store's cycle stays set"; v1.35's 4.20 is silent on the tie.
17. **UART: the start bit is not checked, a glitch starts a frame; a divisor of 0 is UB** (RTL 17.6). v1.35 has no receiver model and no rule.
18. **Toast pin's source**: RTL 17.6: the master tile's own error line or the watchdog, a flip-flop; v1.35's 6.1 (D3) raises it from C&C 1's `ERRORS`. A cycle's difference (ingress_egress README).
19. **Error raised in the reset's cycle**: v1.35's 4.13 "lost (the reset wins)"; the RTL's spec is silent (its tests check "events and errors of its cycles lost"). Kept in 22, flagged.
20. **Uncounted scratchpad writes**: v1.35's 4.14 requires scratchpad data to be sent counted, because its write path was slower than port B's. RTL 12.3: every write the director takes, the scratchpad's included, is in place `LAND_LATENCY` after acceptance, so an uncounted write followed by a `COUNT` from the same sending unit is vouched for like the load's (6.5). v1.35's rule is not kept.

**Control, the boot, the memory system and the registers (v1.35's 6 to 8).**

1. **An unservable request.** v1.35's 6.7 (Errors): a receiver that cannot serve a request (an operation or layout it does not define, an LPDDR range past the device) drops it, and the memory system reports it as one of its peripheral's errors. v1.36: an `op` the target does not define is UB (11.5); the RW engine knows nothing of requests (17.7); a non-OKAY LPDDR response sets `lpddr_error` and the beat completes; a job past LPDDR's end is UB (17.7).
2. **Order between reads.** v1.35's 6.7: the memory system may answer reads in any order and interleave the packets of reads to one tile. v1.36: the RW engine does one job at a time and sends reads in start order, one transfer at a time (17.7), so packets of two reads never interleave; 11.4 still lets a receiver answer requests in any order.
3. **Action registers.** v1.35's 8.1: "a store of 1 fires the action". v1.36's 1.2: a store acts when its bit 0 is 1, the other bits reserved (a store of 3 acts).
4. **`READ_FREE` is 16 bits** ([15:0]; `READ_QUEUE_DEPTH` ≤ 2¹⁶ − 1, 19); v1.35's 8.8: free entries [31:0].
5. **A counter sum past 2²⁰ − 1** is UB (12.2); v1.35's 8.7: wraps (the simulator flags). Same class, different words.
6. **`WATCHDOG` store of 0** stops it, and a store of N sets limit and count (17.6); v1.35's 6.1/8.14 only say "writing N > 0 starts it; a write … restarts it from the last N".
7. **Words 46–63 of Ingress/Egress and words 10–31 of every peripheral window** are access faults (5.3, 17.6); v1.35's 8.11/8.14: reserved, read 0. (An instance of A2.5, but it changes the "handler reads the same words on every peripheral" assumption: only block 0 words 0–6 and block 1 are safe.)
8. **A peripheral's `DMA_START` does not clear its `dma_done`** (17.2), unlike the tile's; v1.35's 8.6 says the tile's start clears DMA run done and 8.11 is silent for a window's. v1.35's 4.17.3 agrees ("no clear pulses").
9. **Reports are told apart by number, not by a sequence number** (6.5, 11.5: 24 and 25 the ROM's, an image's from 0); v1.35's 6.1 step 4: "by the first report's sequence number". Extends A7.5/A4.11.
10. **No "started" handshake and no boot order.** v1.35's 6.2: C&C 1 waits for C&C 2's and C&C 3's "started" requests before loading the tiles. v1.36: the master answers each `LOAD` (the tracer tile's included) and nothing states that the memory tile is up before it is used; the request tree holds requests until popped (11.3).
11. **`.rodata` in DMEM.** Follows A2.8 but changes every image's link script: v1.35's 6.7 calls program memory "load-only".
12. **The `READ` request's `data_counter` may be 0** in the layout (11.5, 3 bits), where v1.35's 6.7 says "everything it sends is counted". See rulings.
13. **The master tile's own `ERROR_LINES` bit** and bits past `TILE_COUNT` + 2 read 0, and its own line drives the toast pin (17.6): same as v1.35 in effect, but v1.35 raises the pin from "C&C 1's `ERRORS` non-zero"; identical since the line is `ERRORS` ≠ 0.
14. **`RELEASE` restarts the watchdog by setting the count to the limit** even when the limit is 0 (off stays off): same as v1.35. No difference; listed because the wording differs.
15. **Mesh-size scaling of Ingress/Egress's words**: `ERROR_LINES`, `ARRIVALS`, `ARRIVE_EXPECT` bits past the tiles read 0 / are reserved at smaller meshes (17.6); v1.35 fixes 64 (part of A1.3).

**The invariants, the parameters, the open items and the sort (v1.35's 9 to 14).**

Differences found in this range that the inventory lacks (or states less fully):
1. **Reply block.** v1.35 has a reply block (256 words below the command block, 2 per source) as the default landing of tile-to-tile replies, with `strict_replies` and invariant 8's "overwritten unread" statistic (v1.35's section 9 invariants 8, 11; section 10 "command and reply blocks"). The RTL's specification has no reply block: a reply lands at the address its request names (11.4), and tile-to-tile "replies" are plain `WRITE`s. The inventory's A4.1 names only the command block.
2. **`GATHER_CONFIG.record_base` alignment.** v1.35's invariant 20: `DMA_RECORD_BASE_ADDRESS` aligned to a record, and to 128 bits in a gather. RTL 13: aligned to a row (128 bits) whatever *k* is.
3. **Counter sum.** RTL 12.2: a sum past 2²⁰ − 1 is UB. v1.35's section 9 has no such invariant (new invariant 32).
4. **IMEM/DMEM collision.** RTL 6.3: a core access and a NoC write to the same word in the same cycle are UB. v1.35's section 9 has no such invariant (new invariant 31).
5. **Reserved tile ids.** v1.35's invariant 17: no stream has *d* = 67–125 or 127, no stream on a mesh tile has *d* = 126, no packet to 67–125 (the simulator errors). RTL 16.2: a packet for a reserved id is dropped where it enters, no error (A1.8 covers this), and RTL 8.3 allows `dst` = broadcast with no restriction to special tiles: whether a compute tile's stream may broadcast is not stated (kept v1.35's rule, flagged).
6. **Boot ROM words.** v1.35's section 10: 48 of 64 words used; the RTL's ROM is 44 words (inventory B1.10, CL; not re-verified here).
7. **Read queue entry.** v1.35: 43 bits; v1.36: 58 bits with the full 32-bit destination (A5.10 gives it; noted here because the budget line changes).
8. **Special tiles' logic budget.** v1.35's section 11 charges an attach (fifth) port to every tile, 0.04–0.07 mm²; v1.36 builds it on attach tiles only (A1.4), which removes the largest single optional line from 61 tiles.

**The replayer.**

1. **A template past ROM word 511.** RTL 17.8.6: a word at 511 with `seq_remaining` > 0 runs, and the expansion's next row is the invalid one. v1.35's 4.19.6: "a template that would run past word 511 (a word at 511 with `seq_remaining` > 0, …) is an invalid expansion", which reads as that word's row being the invalid one.
2. **A ROM word written in the cycle a row reads it is UB** (RTL 17.8.9's ownership table). v1.35's 4.19.6/4.19.10: the ROM is "written (second port)" while running, with no exception.
3. **A code miss with size bit 1 is UB** (RTL 17.8.12). v1.35's 4.19.15 says only that code values are "always 32-bit misses or hits".
4. **A row without `next_from_value` whose new pc differs from its pc is UB** (RTL 17.8.7). v1.35's 4.19.7: "`next_from_value` must be set", with no outcome.
5. **A credit's fill wins over a consuming load in the same cycle** ("a fill sets 2 whatever a consuming load takes", RTL 17.8.1). v1.35 is silent.
6. **The error stop's rows**: "that row is not pushed, and every row before it is" (RTL 17.8.10). v1.35's 4.19.11 says only that the pass stops "as if it had ended". (The simulator's dropped in-flight row is in A9.3; the rule itself is not in v1.35's text.)
7. **When the first row starts after `START` is the implementation's** (RTL 17.8.10); v1.35's 4.19.11: "the first row starts at once".
8. **A padding word and the shared NOP row are their expansion's first row whatever the row offset** (RTL 17.8.5, 17.8.6). v1.35 is silent on the row offset for them.
9. **The state word's [127:112] is one reserved field** (RTL 17.8.2); v1.35 splits it ([114:112], [116:115] reserved, [127:117] spare). No behaviour change.
10. **`format` 6 and 7** are an invalid expansion in the RTL's field table (17.8.4); v1.35's 4.19.3 table says "reserved" and its 4.19.11 makes them an invalid expansion: the same rule, stated once now.
11. **The "exact rows, not exact cycles" rule** (RTL 17.8.7: "The cycles of a pass are not meshsim's") is new text; v1.36 words it "the implementation's (17.8.16)", since the simulator is now to reproduce the RTL's.
12. **The handshake exception** (RTL 3): a write-log hit 15 held by the decoder resolves to E[15] as its row takes it. v1.35 has only the ordering rule (a load row's copy precedes a later row's write-log pop).
13. **The decode table and ROM port kinds**: v1.35 "dual-ported" ROM; the RTL's is two-port (one read port, one write port); the register file stays true dual-port. Implementation, no behaviour.

**The Tracer and the segment.**

Differences between the RTL's 17.9 and v1.35's 4.21 that the inventory lacks:
1. Parts 1 and 3 of the Tracer's region: v1.35's 4.21.2 "unused (zero length)"; RTL 17.9.2 "unmapped, parts 1 and 3 included". Same effect (a write is UB).
2. `RUN_CONFIG` [25:24]: v1.35's 4.21.3 "ignored"; RTL 17.9.1 "held and read back, with no effect here". Named `store_log`, `rd_prev`; [28:26] named `tracer_compression`.
3. `PULSE_RUN` in v1.35 also reset output queue 0's read pointer; RTL 17.9.3's `START` list does not name it (the rows' end sets the pointer to word 0, 17.9.6). Observable only by a transfer from queue 0 between `START` and the rows' end, which no credit covers (UB).
4. A fetch whose line the 35-bit `line` field cannot name (offset ≥ 2⁴² or below `lowest_address`): RTL 17.9.5 defines it (miss counted in `STAT_MISS_I`, no read requested, `page_missing`); v1.35 has no such rule.
5. An error while idle (a write-back's `error` after the run's end) stops the next run at its `START` (17.9.9): new with A10.2.
6. `ERRORS.map_miss` on a new pc of 0: v1.35's 4.21.4 "a JALR or branch whose new pc is 0"; RTL 17.9.4 derives it from the key rule (o ≥ 2⁶³) and also covers a `JAL` whose target wraps to 0 that is not the end row.
7. "Several matching entries in a map line: the lowest-numbered entry's base" (17.9.4): not in v1.35.
8. Order of causes in one cycle on the template latch/bitmap: the state write, `START`, a new expansion, the store (17.9.4): not in v1.35.
9. Encoders: a stream's set 1 and set 2 may share one table, zeroed again at the rows' end; the FIFOs may be single-port (17.9.6): implementation latitude v1.35 did not state.
10. Length words: v1.35 "fixed and exact from the rows' end"; the RTL's implementation raises them 3 cycles after the rows' end (README; 17.9.1's text still says "until the run's rows end").
11. `STATUS.busy` after a stopping error stays 1 until the reset: same in both; but RTL adds "later errors are still set" and "counts already posted stay until consumed".
12. v1.35's Tracer loop arms a counter on the tracer tile for tile n's "armed" `COUNT` packet; v1.36 pops a request instead, so the tracer tile needs no counter for it (follows A10.11).
13. The run's end in v1.35 comes "the cycle after" the last transfer; RTL: at most 2 cycles after the last pop.
14. RTL 17.9.4 cites the opcode appendix directly under spec, without its jolt folder; the file is `spec/jolt/replayer-opcodes.md` (path error in the RTL's spec, fixed here).

### 5.2 Rulings the simulator needs, rules kept from v1.35, and what was not confirmed

#### The core

Rulings a simulator implementer needs (core):
1. core.md "Lockstep regions" describes a generic barrier device (lowers its line on every write to the register; ignores a release with no arrival counted and "flags it in the register"; `barrier_arrived` from a flip-flop). Machine-spec 7.3 defines the tile's device differently in detail: the arrival gate, `EVENTS.release` counted, and a release before every tile has arrived is UB (a pending `EVENTS.release`), with no flag. core.md now says 7.3 is the authority; the owner should confirm, or the RTL's core spec should drop the generic device's "flags it in the register".
2. `mhartid`: core.md says the hart id is supplied by the tile and "the tile id meets both" requirements; machine-spec chapters 1–19 never state that `hart_id` = `PARAMS.tile_id` (and the master tile's id is `TILE_COUNT`, compute tile 0's is 0). The simulator needs the value: assumed the tile id.
3. Trap, NMI and MRET entry cost in cycles: not stated by the RTL's spec (every trap is terminal, so only the handler's report timing would see it). The simulator may choose; proposed: as a taken branch.
4. How exact the simulator's core must be: core.md's cost table gives per-kind costs with "nothing else stalling"; combinations (a JAL that waited in execute "or fewer", a redirect coinciding with a load-use wait, fetch-queue refill after a redirect at an odd halfword) are exact only in a pipeline model. Ruling needed: a per-kind cost table (close) or a four-stage pipeline model (exact). CoreMark 1.312 cycles/instruction is the check either way.
5. `coprocessor_present` in the machine: the RTL ties the port off (custom-0/1 illegal) until the accelerator is decided; machine-spec 6.1 says "Custom-0 and custom-1 instructions go to the coprocessor port" with what it serves *Open*. The simulator should treat them as illegal for now; confirm.
6. MEIP taken as a trap when `mstatus.MIE` = 1: software keeps MIE 0 (6.2); should the simulator model the trap (per core.md) or treat MIE = 1 as UB and stop? core.md is defined behaviour; proposed: model it.
7. core.md keeps "kernels written while the core runs" (the RTL's wording) though machine-spec 6.5 has only loads while the core waits in the boot ROM and no overlays (A4.9). Harmless, but the word "kernel" has no definition in machine-spec 1.1.
8. Kept RTL implementation "Timing:" bullets (decoder response mux, fetch-window compares, "Timing at 1 GHz") are how-level; the rule the machine relies on is only "a target's ready comes from registered state, never from the same cycle's request" (3, `reg`). Kept verbatim; the owner may prefer them in Appendix A.
Inventory items not confirmed by reading both sides:
- B1.6's v1.35 side ("same cycle; +1"): taken from the inventory; v1.35's 4.11 says only that `WFI` continues at the next instruction.
- A3.4's "`mtvec` = 0 at reset" in the simulator: not re-read (the `misa` M bit and the `fence` no-op were confirmed in the simulator's RV32 model).

#### The packet, the NoC and the tile's units (v1.35's sections 1 to 4.10)

**Kept from v1.35, not stated by the RTL's specification (flagged in the text)**
1. Read-unit reads against arriving traffic (item 116, invariant 14): the software rule, kept in chapter 22 (The read unit in use). With commands gone its exception is dropped. Ruling wanted: keep, or relax as v1.35's own measurement suggests.
2. Broadcast streams only from the master tile (simulator errors on one from a compute tile): convention kept in 22. The hardware (8.3, 14.2) supports them.
3. `GATHER` packets are 64-bit: kept in 22 (Gathers in use). Chapters 4.2 and 13 state no size.
4. One user of `PTR` per phase (`PTR[src]` or `PTR[t]`): kept in 22.
5. Mixed sizes within a group (item 27, proposed): kept in 22.
6. The simulator's checks that have no hardware counterpart (flags on an all-zero header, a `COUNT` with counter 0, a write to DMEM's first 16 bytes after the load, a reserved dst, out-of-bounds accesses): kept as "the simulator flags/errors".
**Silent or ambiguous in the RTL's specification**
7. 8.1/8.2: a store to `DMA_CONFIG` during a transfer. The RTL latches the configuration at the start (its README); the contract says only that a second `DMA_START` is UB. v1.35's simulator reads it live. Ruling: adopt the RTL's (config taken at the start) as contract, or make the store UB.
8. 8.2: the exact cycle of `dma_done`. Contract: "once the transfer's last table read has completed". The RTL sets it in the cycle after the walk completes. A simulator needs the RTL's figure to match cycles (Appendix A).
9. 15/19: `READ_LATENCY` is "the implementation's". The simulator needs a value: the RTL's 3 (Appendix A). Whether 3 becomes contract is open (inventory closing 2, item 15).
10. 10: when a queued read is first offered (the RTL: the cycle after its `READ_SEND` takes effect), and the order reads are offered in (the RTL: the order queued). The contract says only "may land in any order".
11. 13: the cycle of a request's first read (the RTL: a + 2) is not contract.
12. 12.3: the cycle each write is in place (a + 2L + 2 etc.) is not contract, only "within `LAND_LATENCY`"; a simulator that models a read taken between the RTL's write cycle and the decrement sees the RTL's value only if it copies the RTL's pipeline. Software may not rely on it (15, "writes before later reads").
13. 16.2: "a packet for a reserved tile id is dropped where it enters the NoC": a dst of **none** is not named (the inventory's A1.8 adds it). 4.1 defines none only as a src. Ruling: is dst = none dropped likewise, or UB?
14. 16.2/16.3: a compute tile's packet to the broadcast id from a special tile's router "spreads from its attach tile as that tile's own": whether the attach tile's own copy passes its loopback queue or an attach-input eject queue is not stated (it matters for the eject arbiter's order and for tokens: a special tile's broadcast has no copy for itself, so holds no token — 14.2 says "a compute tile's broadcast").
15. 14.2: the write unit's loopback packet on a special tile to the broadcast id: not a loopback packet by 14.2's definition ("a packet to the tile itself, or a compute tile's broadcast"), so it takes no token. Stated only by implication.
16. 11.3: a request for a reserved target "is dropped at the root": whether the simulator flags it (v1.35 did) is the simulator's; inventory C item 126 says nothing is recorded.
17. 12.5: "a core load returns an undefined value" when it meets the engine: the simulator must choose (stop, as UB, is consistent with 1.3; v1.35 returned the engine's value).
18. 5.1 against `spec/core.md`: 5.1 says an address past the end of its memory is UB; the core faults past IMEM's/DMEM's size. For packets it stays UB. The two sentences should be read together; v1.36's text keeps both.
19. Start skew: removed in the spec (7.3); the fan-in table's skewed column is kept as a margin only. Re-measuring fan-in on v1.36's simulator with attach tiles 0/7/15 is needed before the table is cited as current.
20. Scratchpad size: power of two only (A6.10) against item 107's 4.25 MB: chapter 26's item 107 records the conflict.
**Inventory items not confirmed**
- A6.9 (v1.35 simulator's loopback token timing), B2.5 (the `ERRORS` cycle), B3.13 (the RTL chip's +2.2 % / +2.5 %): taken from the inventory; only the RTL-side text read.
- B2.1's 11/10: the request-tree README gives a beat's t → t + 2 / t + 1; the store-to-loadable totals are the inventory's derivation.
- A5.4's reason (timing, 0.781 → 0.641 ns): the DMA unit README states the two SRAMs, not the reason.
- A5.7, A6.8, A5.6 simulator sides (`ag.h`, `inject.h`): not re-read; the RTL's DMA unit README describes the same simulator behaviour (its "meshsim" section), which agrees.
- All else in A1 (but A1.5–A1.7, in the next part), A2.4/2.7/2.8/2.15, A5, A6: confirmed against both specs' text.

#### The core's place, events, errors, peripherals and requests (v1.35's 4.11 to 5)

**Silent or ambiguous in the RTL's specification (a simulator implementer needs a ruling)**
1. **17.4: what a load of `DMA_HDR` / `DMA_DEST_ADDR` returns during a transfer.** The RTL implements `DMA_DEST_ADDR` live (advancing) and `DMA_HDR` as written (the transfer keeps its own copy). v1.35 said "live, advancing". The handler's reports read both.
2. **17.4: a store to `DMA_DEST_ADDR` mid-transfer** is UB by the spec; the RTL lets it move the next packets' addresses. The simulator should stop (UB), as v1.35 flagged.
3. **7.2/7.3: `ARRIVE`'s effect on arming.** The spec's table says `ARRIVE` clears `counter_n_done` and `all_counters_done`; whether it disarms is only in the RTL's README (it does not). Needs a sentence in 7.2 or the simulator's reading recorded.
4. **7.3: `ARRIVE` in the release's cycle and in the cycle after** (gate's extent): only in the RTL's README.
5. **7.3: `ARRIVE` on a special tile** is UB; the RTL sets `STATUS.arrived` and a gate nothing ends. The simulator should stop.
6. **7.4: an error raised in a cycle of the reset.** RTL's spec silent; kept from v1.35 ("lost").
7. **6.2: silent traps and the simulator.** v1.35's simulator "stops loudly" on `ecall`, `ebreak` and a trap inside the handler. The RTL's spec gives no simulator behaviour. Kept in 22: the simulator stops; needs confirming, since the ROM's handler now reports them (a test of the handler's reports on `ebreak` would otherwise be impossible).
8. **6.5: the reference image's handler.** A4.11 removes the fixed 24-report set from the protocol. 22 keeps it as the reference firmware's convention; the master tile's firmware then cannot count reports as a protocol constant. Owner: keep, or shrink the set (a 24-report burst stalls on the 4-deep request queue).
9. **The reply block's address.** It sat below the command block at the top of DMEM (0x7BC0). With the command block gone the address is free: 22 says "the image's". Needs a value for `fw/include/mesh.h`.
10. **11.1: `REQUEST_OUT_W0`–`W2` after the reset.** RTL 18 zeroes them; v1.35 called them undefined at reset. No conflict, recorded.
11. **17.2: how late the tile's `peripheral_n` bit may be.** "May lag a consuming load by a cycle": the RTL's is exactly one cycle at the wake, none at a load. The simulator should model exactly that (21 states it as the RTL's).
12. **21: which figures are contract.** Chapter 21 classes as contract only what chapters 1–19 or the core spec state; `READ_LATENCY` 3, same-cycle take, `dma_done`'s cycle, the request tree's hop, the credit's cycle, DMA first packet t + 3 are "the RTL's". The owner may want some promoted (inventory Closing 2, item 15).
13. **Net latencies.** The RTL chip's 0/1/0 are placeholders; v1.35's simulator refuses < 3. 21 lists the RTL's and marks item 8 open.
**Kept from v1.35, not stated by the RTL's specification (all in chapter 22, flagged in place)**
- Programs and the toolchain; the C header rules.
- Silent traps: the simulator stops; the watchdog is the backstop.
- The handler's conventions and the reference 24-report set.
- Waiting rules: `STATUS` diagnostic; keep `release` in the mask (item 79); the phase-end wait (invariant 4); count 0 arms nothing; the loop that replaces `EVENTS_REQUIRED`.
- What earns an error; an error in the reset's cycle is lost.
- The reply block (invariant 11).
- Requests: no acknowledgement for issuing.
- Counters: one per thing waited for; the stray's late detection and the simulator's diagnostic; no flush.
- Pacing the special tiles' traffic (the 12-of-16 figure is `[meas: v1.35]`, at tile 0 with v1.35's attach tiles 0, 1, 2; to be re-measured with 0, 7, 15).
- Peripherals: act on every bit a load returns; items (item 119).
**Inventory items not confirmed**
- B1.9: v1.35's 57 cycles for an empty phase at its defaults: not found in the spec; from its reference log (CL stays).
- B1.10: v1.35's ROM at 48 words: not checked in `fw/boot/bootrom.S`.
- B2.5: v1.35 counting a router overflow at t + 1 (D28): simulator not traced.
- E10 (C&C 1's and C&C 2's DMAs FATAL in the simulator): not found by grep in `src/units/periph_slot.cpp` or `cnc1_periph.h`; unconfirmed.
- E7 (a GO set in the cycle of a consuming load is lost): `regblock.h:119` applies the consume after the sets of the cycle; plausible, not traced end to end.
- A3.9: v1.35's 4.11 has no coprocessor port (grep "custom": no hit in the platform spec). Confirmed absent.

#### Control, the boot, the memory system and the registers (v1.35's 6 to 8)

**Silent or ambiguous in the RTL's specification**
1. **11.5 `READ.data_counter` = 0.** Is an uncounted read allowed? v1.35: all the memory system's data is counted. (23.3 assumes counted.)
2. **11.5/17.7: who checks an undefined `op` or a bad range.** v1.35 dropped and raised a peripheral error; the RTL makes it UB. The simulator should stop on it (UB) — confirm.
3. **`RECEIVE_CONFIG` stores while arrivals land** (12.1, 12.5): the RTL's spec states no rule (13 states one for `GATHER_CONFIG`). Kept from v1.35 in 22 as "static during a phase".
4. **`DMA_CONFIG` stored during a transfer** (8.1/8.2): the RTL's spec is silent; the RTL latches it at the start (inventory A5.9, its README). 22.5 cites Appendix A for it, which states it; the owner rules whether it is contract.
5. **Soft reset** (18 *Open*, item 124): v1.35's keep list is kept as the proposal in 22; 3's `lpddr`/`fill` rules ("answers nothing accepted before a reset after it") constrain it. Needs the owner.
6. **The memory tile being up before a tile uses it.** No "started" handshake (extra difference 10). With `boot_local` the memory tile runs from the reset; is a tile's early `READ` simply queued until served? Assumed yes (11.3).
7. **The master tile's dump path** (LVDS when a host is in the loop): the host interface's beats have no meaning yet (17.6 *Open*); 22 says UART, or the host interface once defined.
8. **Report burst vs `REQUEST_QUEUE_DEPTH` 4** (item 64, A7.2): a handler's reports stall its core; exempt from the per-tile limit (11.3), and the master's receive queue is sized TILE_COUNT + 64 — a burst of 24 reports from many tiles can fill it; the tree then back-pressures. Fine in hardware; say so? (22 mentions the stall.)
9. **A tile's result/exit.** v1.35's exit word is gone; the RTL chip's master firmware defines `op` 2 `EXIT` (firmware's). The simulator's tests need an end-of-test convention: ruling on whether meshsim adopts `EXIT` as firmware convention.
10. **Trace marks.** v1.35's trace word is gone and any undefined word faults: the simulator needs another marker facility (outside the map, e.g. by pc), or the owner keeps a simulator-only word (contradicting 5.3).
11. **Interleaving/cached reads** (23.3): whether a future cache may answer hits before misses is left *(Deferred, 119)*.
**Kept from v1.35, not stated by the RTL's specification (flagged in the text)**
- Configuration registers static during a phase (`RECEIVE_CONFIG`) — 22.
- No read-unit reads outstanding while deliveries arrive (invariant 14, item 116) — 23.9.
- No `READ` outstanding and no write-buffer write in flight across a group transfer — 23.9 (software; the RTL's 17.7 says only "one at a time, or paced").
- The source buffer may be overwritten after `dma_done` [derived] — 23.4.
- 128-bit image packets and the three padding words — 22 (the RTL's 6.5 says `WRITE`s, no size).
- The phase patterns (configure/start, arm-then-start) — 22 (the RTL chip's sort port follows them; not in its spec).
- The master handler's duties, 0.1 ms default, "no hold" — 22.
- No per-tile pacing of deliveries — 23.8.
**Inventory items not confirmed**
- B1.9 "57 at v1.35's defaults" and B1.10 (26 / 13 / 44 words): taken from the inventory and the RTL chip README (35 confirmed at its line 178); not re-derived.
- B4.4's simulator side (t + N) and D33: v1.35's text gives no expiry cycle.
- A4.11's "the same 24 reports" is in v1.35's 4.13; 6.1 confirms "24 from an image's handler, 2 from a unit still in its ROM".
- E10 confirmed in the simulator (`chip.cpp`: "its DMA leads nowhere"); E26's `CTR_FENCE 7` confirmed in `fw/include/mesh.h`; the two fw READMEs not read.

#### The invariants, the parameters, the open items and the sort (v1.35's 9 to 14)

**Silent or ambiguous in the RTL's specification, or needing the owner**
1. **Invariant 14 (item 116):** the RTL's specification (10) states no rule on read-unit reads overlapping incoming traffic. Kept from v1.35, flagged. Is it still a check?
2. **Invariant 17, "no `READ` outstanding and no write-buffer write in flight during a group transfer on any tile":** v1.35's rule; the RTL's specification is silent. Kept, flagged.
3. **Invariant 17, "no stream on a compute tile broadcasts":** RTL 8.3 allows `dst` = broadcast on any tile, and 14.2 handles a compute tile's broadcast as a loopback packet (written for the write unit; a stream's reads already hold tokens). Kept v1.35's rule, flagged. Should the simulator stop on it?
4. **Invariant 22, "`APPEND`, `BUCKET`, `AUGMENT` and every scratchpad write are always counted":** not stated by the RTL (4.2 says "by its counter field"). Kept, flagged.
5. **Invariant 23 and traps without an `ERRORS` bit:** `ecall`, `ebreak` and a misaligned load or store trap to the handler, which reports (24, 25) and parks, but raise no error line. The simulator: stop the run, or let the handler's reports go out as on hardware? v1.35 stopped the core on its "silent traps". Not stated.
6. **Invariant 8, reply statistic:** removed with the reply block. If the sort's port keeps a software reply area in DMEM, the "overwritten unread" statistic could be kept as a simulator option; nothing in the hardware defines it.
7. **`RECEIVE_CONFIG`, `DMA_CONFIG` stores while in use:** the RTL states UB only for `GATHER_CONFIG` (13) and says the RTL latches `DMA_CONFIG` at the start (its README; the spec text is silent, A5.9). A `RECEIVE_CONFIG` store with arrivals in flight is not ruled. GUIDELINES 4 keeps "configuration registers are static during a phase" as the general rule.
8. **Global nets in the simulator (19.1):** at `ARRIVE_LATENCY` 0 and `ERROR_LATENCY` 0 the simulator's partition rule "every crossing ≥ 3" fails (inventory closing 1, rank 2). Which default does v1.36's simulator ship with: the RTL chip's 0/1/0, or v1.35's 8/8/8 with the RTL's as an option?
9. **Item 107:** 4.25 MiB cannot be expressed with a power-of-two `SCRATCHPAD_BYTES`.
10. **Item 64:** request queue depth 4 with no room event: a handler's burst of reports (two in the ROM's set) stalls the core; v1.35's 24-report set would stall 20 times.
11. **Items 168–172:** each a place where the RTL's hardware replaces a v1.35 decision without an owner ruling (PTR/HIST collision, gather depth and AG gate, `READ_LATENCY`, start skew, UART clock).
Inventory items not confirmed by D4: B1.9's "57 at v1.35's defaults" (not found in a source read here); B1.10 (boot figures); B2.1's 11/10 (taken from the inventory; the request_tree README confirms "a cycle a node" only); C's "142 kept" in the RTL's receive unit (the spec's 4.2 states the check; the RTL was not read).
The directive's "invariant 12 changes with the attach tiles": invariant 12 is withdrawn in v1.35 (a stub); the attach tiles change invariants 1 and 17 instead.

#### The replayer

**Silent or ambiguous in the RTL's specification**
1. **How exact must the cycles be?** 17.8.7 leaves every stall's cost, the first row's start, the entries' release and the PEXT's extra cycles to the implementation. To reproduce the RTL's pass cycles (17.8.16) the simulator needs rules the spec does not state: when a decoder "announces" a read-log word (the +2 case is defined only in the RTL's README, `stream_decoder`'s Timing), the fetch's two-cycle NOP row, the 66-bit release rule, the first row's delay after `START`. Ruling: reproduce these (then they need writing down, e.g. in Appendix A), or keep "exact rows, schedules exact, stalls approximate".
2. **`leftover` "at the end or the cycle after it"** (17.8.10): which, in the simulator.
3. **The reset's undefined decoder state** (17.8.14): the simulator should treat `START` with no state write as UB (stop), not model an undefined table.
4. **Item 115, the core-fed set's broadcast**: decided, built nowhere; its proposed mechanics (a size-first command) went with the commands. Until ruled, every tile keeps the whole set (216 KiB).
5. **The tables' delivery at boot**: v1.35 had C&C 1 broadcast the decode table and ROM at boot; the RTL's spec leaves it to software (17.8.13). 17.8.19 says the master tile's firmware broadcasts them after the tiles' loads: that wording is v1.36's, and software's to settle (the RTL's replay image loads its own tables).
6. **The core's loop** (17.8.21) is v1.35's adapted to v1.36's events (no `EVENTS_REQUIRED`: an inner loop until both events). The RTL's firmware (its replay image) differs: set-up before the release, a template cache, `ARMED`/`STREAMS_ARMED` requests. Ruling: which one v1.36's `fw/replay/replay_ctl.c` follows.
7. **Items 35, 36, 38, 39, 42, 56, 57** are "proposed" in v1.35 and built as proposed in the RTL; the open-items table says so and leaves the status to the owner.
8. **Item 123** is marked closed (the `ERRORS` positions are the RTL's, by the decision to take the RTL's terms); confirm.
9. **Item 140** is marked closed by the RTL's bit-serial extraction; the contract still says "as many as the implementation's".
**Kept from v1.35, not stated by the RTL's specification** (each kept section opens with that flag): 17.8.15 ground truth and reference; 17.8.16 measurements; 17.8.17 the producer's requirements and the record's rationale; 17.8.18 Jolt's rules behind the expansion word, the table builder's rules; 17.8.19 the tables' contents, the key, the broadcast; 17.8.20 the opcode lists per class and Jolt's behaviour; 17.8.21 the loop, "a credit may be split", the reload practice; 17.8.22 the resident region; 17.8.23 cost; 17.8.24 the simulator's checks, among them the statistics for a misaligned memory row and a non-contiguous PEXT mask.
**Inventory items not confirmed**
- B5: "50 cycles on the cache-hit path, against meshsim's ~41–45": not found in the RTL's READMEs; left out of the spec. 76.1 / 94.4 and 3.0875 / 3.1249 are confirmed (the chip's README).
- B5: the model's figures are v1.26.1's Java model's, a one-off run (the RTL's replayer README says so); v1.35's C++ was not run.
- B5 "before 2c2c9d6 the RTL was 3.1871 / 3.2215": not checked, not used.
- E8 "`reset()` leaves three words": the simulator's reset was not traced line by line; `pend`, `low_water()` and `exp_discard_` are present in `src/units/replayer.h` (confirmed).
- A9.3/A9.5 simulator line numbers were not re-read; the RTL README's "meshsim v1.35" section describes the same behaviours.
- The +1/+2 inconsistency for the NOP row in the RTL's README: its Results say +2 (fetch reads the template's first word, then ROM word 1); 17.8.16 uses +2.

#### The Tracer and the segment

**Kept from v1.35, not stated by the RTL's specification**
3. A mid-segment state write is UB, and the simulator's flag (state write with the advice field above 0): 17.9.15. The RTL's 17.9.11 says only that a new segment waits for the last run's `done`.
4. The simulator's item-145 check (inline completing a row with no `EXPANSION_TEMPLATE` store): kept in 17.9.16; the RTL makes the store-before-first-word a UB rule (17.9.11) with no check.
**Silent or ambiguous in the RTL's specification: a simulator implementer needs a ruling**
5. 17.9.1/17.9.6: the length words, the stream word and `state_ready` change "when the rows end"; the RTL does it 2–3 cycles after (stream word +2, the others +3). Which cycle does the simulator use? (item 177)
6. 17.9.7 gives no number for the redirect, the first instruction, the NOP policy, the core-fed successor, the miss overheads or the probe's 4 cycles: they are the RTL's measurements (17.9.17). Whether the simulator must match cycle for cycle (item 175; inventory closing 2 point 6).
7. 17.9.10: "Their `op` codes are the tracer tile's firmware's" while inventory A7.5 lists `ARMED` 1, `STREAMS_ARMED` 2: confirmed only in the RTL's firmware (`OP_ARMED` 1, `OP_STREAMS_ARMED` 2), not in its spec (item 176).
8. `fill`: the model's behaviour with a read and a write outstanding together, and when the model answers a write (item 174). v1.35's memory-system model had one request outstanding.
9. Spare record bits (A10.7: "spare record bits read 0", the state word's [127:112], the files word's [127:103]): the RTL's README says the state word's [127:112] read 0 through output queue 0; 17.9.2 says only "spare, 0" for the files word. Not written into v1.36's text beyond the RTL's; the simulator keeps every bit written. Needs a sentence or a ruling.
10. 17.9.3: `START` does not list output queue 0's pointer (v1.35's `PULSE_RUN` reset it).
11. 17.9.9: "every other error stops the Tracer where it is": whether a DMA transfer in progress at the stop completes (RTL: "drains nothing more", and `dma_done` is not set) is not said packet by packet.
12. 17.9.8 says the counters are "as this implementation sees its stalls"; v1.36's text now says the simulator follows that attribution (item 157 closed that way, by v1.35's own "meshsim follows"). Confirm.
13. The tile-0 start: 17.9.10 says tile 0's record start comes "from the tracer tile's own copy"; that it too waits for tile 0's `ARMED` follows from "nothing its request has not allowed" and is how the RTL's firmware does it; written so in 17.9.15.
**Inventory items not confirmed**
- A10.9 (CL): confirmed on the RTL side (17.9.9 and the README's decision "The stop withdraws every event"); v1.35's `periph_slot.cpp` after a stop not read.
- A10.7: RTL README only; not in the RTL's spec (see 9).
- B6's meshsim figures are v1.3's Java runs quoted in the RTL's READMEs; not re-run. "Run's first instruction: 7 cycles after `START`" in the inventory is "6 cycles after START's effect" in the RTL README (the same thing counted from the store).
- B6 "rows' end → stream word +2; `done` ≤ 2 after the last pop": confirmed in the RTL README (lines 677–691).
- Stale RTL README line (~1317, synthetic comparison "+6 cycles"): confirmed stale; the README's own schedule section and acceptance figures say 5.
- Simulator-side claims of A10.2–A10.8 (tracer.cpp line numbers) not re-read.

## 6. Errors and contradictions found in v1.35, and their resolution in v1.36

Found while comparing v1.35's specification and simulator with the RTL. None is recorded in v1.35's `ERRATA.md` or
its guide unless the item says so. E*n* is the inventory's number; "new" items were found while writing v1.36.
**On the simulator's side, each is something stages 1 to 9 of section 2 must not carry over.**

### 6.1 The core

Errors and stale statements in v1.35 (core), with v1.36's resolution:
- E9 (confirmed): the simulator's `misa` reports M (bit 12) though the core has Zmmul only. v1.36: `misa` = 0x4000_0104, I and C (core.md CSRs).
- E20 (confirmed in v1.35's 4.11): a fetch fault "sets no `ERRORS` bit" (a silent trap) though every other bus error sets one. v1.36: an instruction access fault sets `ERRORS.access_fault` (6.2, 7.4).
- New: v1.35's 4.11 says "Loads from program memory and the boot ROM take one cycle too … (item 134)" and "program memory is readable, so [constants] may live there"; the simulator's `fence` handling accepts funct3 1 (`FENCE.I`) as a no-op though the spec lists no Zifencei. v1.36: IMEM and the ROM are fetch-only; `FENCE.I` is illegal.
- New: v1.35's 4.11 states both "A store at t updates the register at the end of t; … the owning unit at t+1 see[s] it" and the sampling rule with side effects at the end of t + L − 1 for loads only; stores and loads to a window therefore had different effect points. v1.36: one rule, a store takes effect as a load takes its side effect (6.4).
- New: v1.35's 4.11 gates the hard interrupt's release of `WFI` by `mstatus.MIE`, while 4.13 (per the inventory, v1.35's line 1045) says the image "takes no trap" until it enables it: an error raised before the image enables the interrupt leaves a core asleep in `WFI` for ever, reported only by the error line. v1.36: the NMI is unmaskable from reset on.

### 6.2 The packet, the NoC and the tile's units (v1.35's sections 1 to 4.10)

- **E2. A refused group read.** v1.35's 4.2 (Drift): "every later time shifts by the stall"; its simulator keeps absolute times and catches up (confirmed from the RTL's DMA unit README; simulator not re-read). v1.36: 8.3, 8.4: a refused read shifts every later time; the simulator is to follow.
- **E3. `gather_busy`.** v1.35's 4.4: "Reads issued but not yet landed are not included"; its simulator ORs in in-flight reads. v1.36: 13's definition (queue non-empty, or a request's last read not yet taken); the simulator is to follow.
- **E13. First-group lead-in.** v1.35's 4.2, Timetable: "S_0 is … 4·L_ts + 3 cycles after the start store", unconditional; Read-ahead: "A group starts when its `GROUP` entry is in and, if R > 0, its first `STREAM` entry", which for an R = 0 first group is 2·L + 2. v1.36: 8.2, 4·`TABLE_LATENCY` + 3 "whatever its first entry holds".
- **E14 (part). Table loads.** v1.35's 4.10: "Only `HIST` can be loaded; `PTR`, the `STREAM` arrays and `GROUP` take stores only", while its Ports table gives `PTR` no controller read and 4.8 says "of the two, only `HIST` is loadable" — consistent here; the contradiction is with v1.35's 4.11 list (D2's range). v1.36: 5.2, every table is loadable.
- **E15. A DMA's address.** v1.35's 3.1: "Used by mode 0; modes 1–5 ignore it, region included (senders put 0)", against its 4.17.9 stamping `DMA_ADDR` on every packet. v1.36: 4.1, the sending hardware puts 0 for every mode but `WRITE`, whatever its `_DEST_ADDR` holds; 17.4 agrees.
- **E17. Fifth-port queues.** v1.35's 4.7: "Every tile has a fifth-port feeder FIFO on each of these five outputs and a sixth output"; its 4.6: "tiles 0, 1 and 2 each have a sixth [route block]"; its 2: "every router has a fifth port, connected only where something is attached". v1.36: 16.1, attach ports only on attach tiles.
- **E18. The attach link's latency.** v1.35's 3.2: "Every link takes exactly 3 cycles"; nothing for the fifth port's link, and the simulator uses 1. v1.36: 16.1, every link, attach links included, is three stages. ("Down port" is in v1.35's 4.16, D2's range.)
- **E19. Request-bus latencies.** v1.35's 3.3 gives only `reqbus_latency` ("about 15–20", 17 measured); hop and credit latencies are the simulator's alone. v1.36: 11.3 states the tree's structure; the RTL's figures are Appendix A's.
- **E23. Gather overflow's class.** v1.35's 4.4: "the queue overflows only on a fault (… H)", though it concedes requests "from the send port". v1.36: 7.4, `gather_overflow` is P; 13 names the write unit and a peripheral's DMA as the senders that can cause it.
- **E24 (part). Stale.** v1.35's 3.1 "Size" still speaks of a peripheral's DMA choosing its packet size ("from its own choice (4.17.9)"), though a DMA's packets are all 128-bit. v1.36: 17.4, 5.4 (the size in `DMA_HDR` is the DMA's, ignored on a store).
- E21 and E22 are the Tracer's and the replayer's: 6.7 and 6.6.
**New**
- N1. v1.35's 4.1 states the unloaded read latency as "4 cycles" [meas] while v1.35's simulator injects 5 cycles after the take (the RTL's inject unit README, citing the simulator). v1.36: `READ_LATENCY` is the implementation's (15), the RTL's 3 (Appendix A).
- N2. v1.35's 4.1 has the same acceptance rule twice ("Room is judged before the pop" and "Acceptance"). Removed with the area queues.
- N3. v1.35's 4.3 calls the read unit's queue "the request queue", the name 4.4 also gives the gather unit's and 3.3/4.18 the request port's FIFO. v1.36: read queue, gather queue, request queue.
- N4. v1.35's 4.2: "`rd_running` = 1 … from the end of the start store's cycle" against "A start store in cycle *t* takes effect in cycle *t* + 1" in the same list. v1.36: 6.4, 8.2: the store takes effect at the end of *t* + 1, and a load issued after it sees it.
- N5. v1.35's 3.2 fan-in table is a v1.0 measurement "to be re-measured in v1.2", never re-measured; its skewed column assumes a start skew v1.36 does not have. Kept, marked `[meas: v1.0]`.
- N6. v1.35's 4.4 back-pressure bound ("≈ 10 … under 16 [estimate]") was an estimate for a 16-entry queue; v1.36's queue of `TOKEN_COUNT` is bounded by the tokens (13).
- N7. v1.35's 2: "attached to the fifth ports of tiles 0, 1 and 2" with "C&C 1's *d* = 0 is tile 0" (4.2): a controller's relative offsets counted from its attach tile, a rule the simulator needed a correction for (CL6). v1.36: a special tile's `dst` below `TILE_COUNT` is absolute (8.3).
- N8. "record" names two things in v1.36's vocabulary: the sort's 64-bit record (12.4, 13) and the replayer's/Tracer's 68-word record (1.1). 22.1 says so; 1.1, the RTL's text, defines only the second.

### 6.3 The core's place, events, errors, peripherals and requests (v1.35's 4.11 to 5)

Inventory E items in range:
- **E4** (counter store + decrement): v1.35's 4.12 states `next = count + N − dec`, underflow judged against count + N, and "if the count reaches 0 in the same cycle as an add of N, it ends at N and done doesn't fire". With count 0, N = 1 and a decrement, next = 0 and the rule gives no edge, so the event would never fire; the simulator judged without the store (D27). **v1.36:** 12.2 keeps the formula; 7.2 defines the event as a decrement that leaves the counter at 0 while armed, and "a counter at 0 in its arming cycle has ended the new wait": the event is set. Confirmed in both texts.
- **E7** (a GO set in the cycle of a consuming load is lost in the simulator, against v1.35's 4.12 "an event set after the sample point survives"): **v1.36:** 7.2, "a consuming load never removes an event it did not return". Simulator side not fully traced.
- **E9** (`misa` reports M): confirmed, `src/units/rv32.h:465` sets bit 12. **v1.36:** `spec/core.md`'s CSR list (`misa` = I and C).
- **E10** (C&C 1's and C&C 2's DMAs fatal in the simulator while the spec gives C&C 1–3 sending engines, v1.35's 4.17.9): not confirmed. **v1.36:** 17.6 Ingress/Egress has no DMA; 17.7 the RW engine has one; 17.9 the Tracer has one.
- **E11** ("GO wakes whatever the mask holds" against "the gate lasts until GO"): confirmed, both in v1.35's 4.12 (the gate ends at GO, yet GO must wake the core through the gate). **v1.36:** 7.3: the gate lasts until the cycle after the release, and under it the wake is `EVENTS.release` alone.
- **E12** (READY can pulse before the core is in `WFI`): confirmed: v1.35's 5 and 6.3 fire READY on "command landed" and `done`, with no core state. **v1.36:** 7.3: ready needs the canonical wait.
- **E16** (the memory system's reply "32- or 64-bit" from a DMA whose packets are all 128-bit): v1.35's 4.12 (reply 32 or 64 bits) against 4.17.9 (every DMA packet is 128 bits). Confirmed. **v1.36:** 11.4: a reply is 32, 64 or 128 bits, the memory tile's 128.
- **E19** (request-tree hop and credit latencies only in the simulator): confirmed: v1.35's 4.18 and 5 give only `reqbus_latency` ≈ 15–20 [estimate], 17 [meas]. **v1.36:** 21.4 states the RTL's figures; item 61 stays open for link stages.
- **E20** (a fetch fault a "silent trap"): confirmed, v1.35's 4.11 and 4.13. **v1.36:** 6.2, 5.1: it is an access fault and sets `ERRORS.access_fault`.
- **E23** (gather overflow classed H though software senders can cause it): confirmed, v1.35's 4.13 bit 6 "(H): mode 4 only comes from the tile itself" while the send port can send mode 4 (4.11: "the send port checks nothing"). **v1.36:** 7.4, 13: `gather_overflow` is P.
- **E24** (stale): v1.35's 4.17.4 table still says `DMA_ADDR` advances "by each packet's size" and 4.17.9 `DMA_ADDR` "by each packet's size in bytes" while every packet is 128 bits (advance 16): confirmed. **v1.36:** 17.4.
- **E15**: v1.35's 4.11 says modes 1–5 leave `ROUTER_ADDR` unchanged and ignore the address, while 4.17.9 stamps and advances `DMA_ADDR` on every packet whatever its mode. **v1.36:** 4.1, 17.4: only a `WRITE` carries and advances.
- **E17** (fifth-port FIFOs "every tile" against "tiles 0–2 only"): confirmed inside v1.35's 4.16 itself: its first sentence gives every router a fifth input and output; its cost bullet says "tiles 0–2 only". **v1.36:** 16.1: attach ports only on attach tiles.
- **E18** ("down port"): confirmed in v1.35's 4.16 and 4.17.9; no latency given for the link. **v1.36:** 16.1: an attach port, a link of three stages like any other.
New in range:
1. **v1.35's 4.11 lists two stores that stall, then a third**: "The only stores that stall are a send store …, a request store …, and a store to the UART's TX word", while v1.35's 5 lists the same three. Consistent; but 4.20 calls `UART_TX` "a third stall source beside the send and request stores (4.11)", and the host interface would add more. **v1.36:** 21.1 lists all five.
2. **v1.35's 4.12 "Timing": "a GO pulse at cycle t sets [8] at the end of t and wakes the core at t + 1"** while v1.35's 5 adds "a `PULSE_GO` store reaches every tile `go_latency` cycles later" with a random start skew in 3.2: the skew makes "every core at t + 1" untrue for half the tiles. **v1.36:** 7.3, 18: no skew.
3. **v1.35's 4.13: "A store to `ERRORS` is a bus error ([9])"** makes a store to `ERRORS` set `ERRORS`: intended, but it means the one register that cannot be written is changed by writing it. **v1.36:** same rule (5.1, a store to a read-only register), stated once.
4. **v1.35's 4.13 handler text: "It reads seven of each window's ten standard words"** but the report table carries seven words plus two zeros, and lists `DMA_ADDR` among them while 4.17.4 says a DMA-less peripheral's words 8 and 9 read 0 "and a load of them is not flagged; only a store is": the simulator-only flag is a third behaviour between "reserved" and "defined". **v1.36:** 17.1: block 1 of a peripheral without a DMA reads 0; a store is UB.
5. **v1.35's 4.17.5: "whether the dropped write is still counted is not promised"** contradicts the preamble's "there is no third kind" only in spirit, but leaves a counter's done undefined after a checked drop. **v1.36:** 17.3, 12.3: still counted.
6. **v1.35's 4.18 and 4.12 disagree on `REQUEST_SET_W*` at reset**: 4.18: "configuration registers: undefined at reset", yet the handler "writes every word" precisely because a half-written request may be pending; and "at the push they clear to 0 *(Proposed)*". **v1.36:** 18: zeroed at the reset; 11.1: cleared at the push.
7. **v1.35's 4.20: "about 174 µs at 57,600 baud"** for one byte time: 10 bits at 57,600 baud is 173.6 µs; correct, but stated as the stall bound "at most one byte time", which holds only when the transmitter is running. **v1.36:** 17.6 states the stall, no bound.
8. **v1.35's 5, "READY lines … readable from s + `ready_latency` + 2"** while 4.11's sampling rule would make a bit set at the end of s + `ready_latency` readable by a load issued in that cycle: the "+ 2" counts the load's latency. **v1.36:** 21.2 states the setting cycle, and 6.4 the load.
9. **v1.35's 4.16 cost bullet scales from "v1.1's 169-bit"** while 3.1 stores 256 bits a queue entry (item 117). **v1.36:** 4.1: the defined bits only (182 at 8 × 8).
10. **v1.35's 4.11 "Illegal: divide opcodes … set illegal instruction, `ERRORS` [10]"** but `misa` reports M (E9). Same root as E9.

### 6.4 Control, the boot, the memory system and the registers (v1.35's 6 to 8)

| # | v1.35 | Resolution in v1.36 |
|---|---|---|
| E10 | 6.6/6.7: C&C 1–3 "send through … like any unit", the memory system's packets enter "from its own router port (4.17.9)"; the simulator makes C&C 1's and C&C 2's DMA packets FATAL ("its DMA leads nowhere", `chip.cpp`). Verified | Ingress/Egress has no DMA (17.6); the RW engine has one, on a link input of the memory tile's router (17.7, 16.4): the simulator models it |
| E12 | 6.3: READY pulses when `done` and "command landed" are both 1; safety is argued from "GO cannot come back sooner than the READY round trip (≈ `ready_latency` + 3)", which fails with short nets. Verified | readiness requires the canonical wait (7.3); no timing argument |
| E14 | 8.9: only `HIST` can be loaded, the rest a bus error (while v1.35's 4.11 lists them as loadable, per the inventory). 8.9 verified | every table is loadable (5.2, 8.6, 12.5) |
| E16 | 6.7: a reply is "a 32- or 64-bit mode-0 write", yet the memory system sends from its DMA, whose packets are all 128-bit (v1.35's 4.17.9). Verified in 6.7 | replies may be 128-bit; the memory tile's always are (11.4, 11.5, 17.7; 23.2) |
| E24 | 6.7 still says "*(Deferred, item 118: the src its packets carry)*" though item 118 is "decided: sending peripherals' src and port". Verified | src is the tile's own id (16.4), a reversal of item 118 for the owner |
| E26 | 8.1 names `fw/include/mesh.h` "version 1.2" (the file says 1.2, `MESH_H_VERSION 0x0102`) in v1.35; `CTR_FENCE 7` is documented as "request slot 2's", and counter 7 is now the boot ROM's load counter (6.5). Verified | `mesh.h` is regenerated for v1.36; counter 7 is free to an image after the load (the ROM's use ends at the jump), but an image must end with it at 0 |
| E28 | 6.1/6.3 treat `ready_latency`, `go_latency`, `error_latency` as free parameters; the simulator refuses `ready_latency` < 3 and ties `error_latency` to it (inventory; not re-read) | `ARRIVE_LATENCY`, `RELEASE_LATENCY`, `ERROR_LATENCY` are the chip's (19); the simulator must take wire / 1 / wire |
| new 1 | 6.7: "anywhere in the requester's data memory (region 0x00; program memory is load-only)": "load-only" contradicts 2.2/4.11's "fetch, load" for the core and the fact that packets write it | IMEM is fetch-only for the core, written by packets (5.1); replies go to DMEM (11.4) |
| new 2 | 6.2 step 3 has the ROM test "command landed" so a tile ignores other tiles' GOs, yet 6.6 says C&C 1 must wait `go_latency` + 1 before a command or "READY is lost and the machine hangs": two mechanisms guarding one race, the second by timing | no commands; every compute tile takes part in every release (7.3) |
| new 3 | 6.1 step 4 tells report sets apart "by the first report's sequence number", a field item 122 leaves deferred | `REPORT`'s `number` (11.5) |
| new 4 | 8.2 calls `STATUS` "diagnostic only (4.10)": 4.10 is the table memories | 7.1; the convention is kept in 22 |
| new 5 | 8.1's reset list makes `ROUTER_SET_W0`–`W2` and `REQUEST_SET_W0`–`W2` undefined at the reset, while 8.4/8.10 say they "clear to 0 at every send" and a word never stored is sent as 0: a first packet after reset with an unstored word is undefined | 0 at the reset (18) |
| new 6 | 6.2: "meshsim's [ROM] was about 728 [bytes]" beside "48 words in meshsim's ROM" and a 256-byte limit: the first is v1.2 history left in normative text | dropped; 22 gives 44 words [meas: RTL] and 48 [meas: v1.35] |
| new 7 | 7: the fill model has "one request outstanding" and no failure, while the Tracer's text (v1.35's 4.21.5) has a write-back buffer | one read and one write outstanding, write status (3, 17.9.5; 23.6) |

### 6.5 The invariants, the parameters, the open items and the sort (v1.35's 9 to 14)

- **E24 (verified).** v1.35's section 13, item 84, lists "16 requests per target queue" as still proposed, while its 3.3 and section 10 give 128, 256 and 128. Resolution: item 84 closed, the depths `RECEIVE_QUEUE_DEPTH` (11.3, 26).
- **E24 (verified).** Item 118's row keeps "the DMA port's flow control" deferred and "src" decided, while v1.35's 6.7 still deferred the src (not re-read here; from the inventory). Resolution: 16.4 states both; item 160 for the reversal.
- **E25 (verified).** v1.35's section 14 prints v1.25's 1,229,008 as the measured full-size sort, while v1.35's own reference is 1,229,687 (`RUNNING.md` lines 84–85). Resolution: 27 lists each figure with the version it was measured on.
- **E28 (verified in section 10's text; the simulator's refusal taken from the inventory).** v1.35's section 10 calls the three nets' physical latencies *(Open)* while its simulator cannot run below 3 and ties `error_latency` to `ready_latency`. Resolution: 19.1 requires the three to be independent, from 0 up.
- **New.** v1.35's section 9, invariant 24, cites "item S1, 13": S1 is the decisions log's name; the item is 128. Resolution: 20 cites item 128.
- **New.** v1.35's section 9, invariant 1, names "tiles 0–2's fifth-port FIFOs" while its section 11 charges and builds the fifth port "in every tile" (E17, same contradiction seen from sections 9 and 11). Resolution: attach ports on attach tiles only (16.1), and 24 charges them there.
- **New.** v1.35's section 10 "events" row and section 11's control line still list "command landed" and the `done` qualifiers as hardware; consistent in v1.35, removed in v1.36 (7.2).
- **New.** v1.35's section 11 total says "with the replayer and the fifth port" while the fifth-port line sits below the total and is not obviously inside the 0.52–0.61 figure. Resolution: 24 keeps v1.35's figures as its estimates only, to be redone.
- **New.** `spec/schedule.md` cited "machine-spec 4.2" for "In v1.1 a 128-bit value is one packet" and carried "to be re-measured in v1.1" labels (item 24): kept as labels, the citation fixed.
- **New.** `spec/GUIDELINES.md` cited `machine-spec.md` "(v1.3)" though it shipped with v1.35. Resolution: v1.36.

### 6.6 The replayer

- **E1 (verified in `src/units/replayer_alu.h`: the branch and jump cases set `new_pc` with no last-row gate).** The simulator lets a row that is not its expansion's last branch, jump or end the program, against v1.35's 4.19.7's table. v1.36: 17.8.7's table rules; 17.8.20 records it; the shared ALU model is fixed.
- **E8.** In the simulator and not in v1.35's text: the leftover's third cause (entries pushed past the words), the restart of the credit count at the state write and at a new expansion, the core-fed NOP discard. v1.36: the first two are 17.8.10/17.8.12 and 17.8.8; the discard is replaced by "the core sends the shared NOP row alone" (17.8.5, 17.8.13). `reset()` leaving `ROWS_RUN`, `EXPANSION_OFFSET`, `EXPANSION_OPCODE` against 8.11's reset column: 17.8.14 lists them as zeroed. The dead `pend`/`low_water()` remnants (present in `src/units/replayer.h`) go: the credits are software's account.
- **E22a.** "Three conventions can't be table data" followed by four bullets (v1.35's 4.19.7). v1.36: four (17.8.7, 17.8.20).
- **E22b.** "The controller reads the queue occupancies to see which stream" (v1.35's 4.19.11, `ERRORS` [6]) with no word exposing them. v1.36: dropped; `leftover` is one bit.
- **E22c.** Two rules for when a code entry leaves (v1.35's 4.19.7 "when its expansion's last row completes" for the instruction; 4.19.15 "when its last bit has been consumed" for the entry). v1.36: one rule, "no later than" (17.8.7, 17.8.8).
- **E25.** v1.35's 4.19.1 rate paragraph leads with v1.25's figures. v1.36: 17.8.16 leads with the RTL's figures on the recorded windows and marks every older figure with its version.
- **E27.** v1.35's 4.19.9 and 4.19.13 state the core-fed set's broadcast as fact; no firmware implements it. v1.36: "decided, not built" in 17.8.13, 17.8.19, 17.8.22 and item 115.
- **New: `EXPANSION_OFFSET`.** v1.35's 4.19.9/8.11: "the row within the current expansion where the next row starts", live, yet also "where the controller begins sending". v1.36: latched at the post (17.8.1).
- **New: overrun counting.** v1.35's 4.19.8 and 4.19.10 leave "whether the dropped push is still counted" unpromised, against the guideline's "no third kind". v1.36: counted (17.8.8).
- **New: `format` 6, 7** are "reserved" in v1.35's 4.19.3 table and an invalid expansion in its 4.19.11. v1.36: an invalid expansion in both places.
- **New: the decoder tables' cost** counts three 16 × 64-bit tables (384 B) though code values are 32-bit. v1.36: 320 B (17.8.23).
- **Stale: 4.19.6 "Loading … broadcast by C&C 1 at boot (4.19.10)"**: 4.19.10 does not say so (it only notes boot loads find the replayer stopped). v1.36: 17.8.19 "Loading".
- **Stale in `replayer-opcodes.md`**: its sections 4 and 5 quote the spec as it stood before part F; kept as a record, their citations now "v1.35's 4.19.x".

### 6.7 The Tracer and the segment

Errors, contradictions and stale statements in v1.35's Tracer text (verified in `spec/history/tracer-v1.35.md`), with v1.36's resolution:
- E21a. `EXPANSION_OFFSET` (v1.35's 4.21.3): "the row within the current expansion where the next row starts … (non-zero only at a segment's start inside one…)" describes both a live value and a value held; 8.16 says "where the next row starts". v1.36: latched at the post, held until the next (17.9.1).
- E21b. "A free entry has key 0xFFFFFFFF, which no target can have (pcs are even)" (v1.35's 4.21.4): the key is pc − `lowest_address`, which is odd when `lowest_address` is odd (`io_size` is exact, any alignment). v1.36: a target at that offset is UB, the host places none (17.9.4).
- E21c. Jump LRU "64 entries of (pc, base)" and the cost line "64 × 96 bits" against a 32-bit key: v1.36: (key, base), 64 × (32 + 32) bits, the offset tested for ≥ 2³² before the LRU (17.9.4, 17.9's table).
- E5. `STAT_PROBES` "jump-LRU misses, each starting a map probe" (v1.35's 4.21.3) while the simulator counts a miss whose offset is too far and starts no probe. v1.36: no look-up, LRU untouched, not counted (17.9.4, 17.9.8).
- E6. The one-entry write-back buffer and its take-back (v1.35's 4.21.5) are not in v1.35's simulator. v1.36: the buffer's rules are complete (17.9.5); the simulator is to model it.
- "A write-back can't fail" (v1.35's 4.21.5): true of a correct memory system only; v1.36 gives the response a status and an error (17.9.5).
- v1.35's 4.21.8 "no event is posted" after a stop is ambiguous about the DMA's `dma_done` and the live flags; v1.36: none set, live ones down, from the cycle after (17.9.9).
- v1.35's 4.21.6: the three stream ends are not distinguished ("when its stream has ended") while one log scan feeds both logs; the simulator ends both logs together. v1.36: each ends on its own last value (17.9.6).
- v1.35's 4.21.9 memory estimate "about 850 KB" with the LRU at 96 bits an entry: v1.36: about 849 KiB (17.9's table).
- v1.35's 4.21.3 loop relies on `EVENTS & EVENTS_MASK` at the peripheral and a landed command from C&C 1 to start: there are no commands in v1.36 (17.9.15).
- On the RTL's side: its 17.9.4 cites the opcode appendix without its jolt folder (the path is `spec/jolt/replayer-opcodes.md`); its tracer README's synthetic comparison still says "+6 cycles" for a redirect (5 since 3fb7a82).


### 6.8 On the RTL's side

Found in the RTL's (Snarky One's) own documents while this version was written; none changes its hardware.
- **`GROUP`'s SRAMs.** The RTL's base tile specification's 8.6 said for a time that `GROUP` is one 64 × 38-bit
  SRAM; the RTL has two, 64 × 30 and 64 × 8, one for each word of an entry. The text v1.36 takes (8.6) states the
  two, and Appendix A repeats it.
- **The core specification's barrier device.** Its Lockstep regions describes a generic device that ignores a
  release with no arrival "and flags it in the register"; the tile's device is 7.3's, where such a release is
  UB and leaves a pending `EVENTS.release`. `spec/core.md` says 7.3 is the authority.
- **`mhartid`.** The core specification takes the hart id from the tile; no chapter says it is
  `PARAMS.tile_id`. v1.36's simulator assumes it is (section 5.2).
- **17.9.4 cited the opcode appendix without its jolt folder;** the path is `spec/jolt/replayer-opcodes.md`, as v1.36 has it.
- **The Tracer's README** still gives "+6 cycles" for a redirect in its synthetic comparison; its schedule and
  its acceptance figures say 5, the figure since the redirect's fetch moved into the cycle its row completes.
- **The replayer's README** gives the shared NOP row's stall as +1 in one place and +2 in its results; +2 is
  the figure (17.8.16).
- **The request unit's README** says a credit's timing is "as meshsim does": true only of v1.35's simulator
  at its credit latency 0, not at its default 3.
- **The chip's README** compares the sort with v1.25's simulator at net latencies of 1 (section 3.8), and lists
  the group lead-in as a difference without saying it is the R = 0 case alone.
- **Citations.** The RTL's READMEs' "meshsim v1.xx" sections cite v1.25, v1.26.1 and v1.3 in places; the pin is
  v1.35, whose hardware is v1.3's.

## 7. The name table: v1.35 → v1.36

Every term, register, bit, field and parameter of v1.35, and its name and place in v1.36, complete enough to
translate v1.35's firmware and simulator sources mechanically. `spec/machine-spec.md` 1.1 has the RTL's own list
in the other direction. A v1.35 address is the low 12 bits of 0xFFFFFxxx unless it says otherwise; a v1.36 place
is a block and word (5.3), an offset in the window, or an absolute address. "Removed" means the hardware no
longer has it, and section 2 says what replaces its use; "new" has no v1.35 counterpart.

### 7.1 Terms, the packet and the tables (v1.35's sections 1 to 4.10)

| v1.35 name | v1.36 name | note (address/bits old → new) |
|---|---|---|
| unit | tile | any of the `TILE_COUNT` + 3 |
| tile, mesh tile | compute tile | |
| tile idx, endpoint id | tile id | `TILE_ID_BITS` wide |
| C&C 1, C&C 2, C&C 3 (endpoints 64, 65, 66) | master, memory, tracer tile (`TILE_COUNT`, + 1, + 2) | |
| the controllers; non-tile endpoints | the special tiles | |
| controller; C&C controller | core | |
| program memory, data memory, controller memory | IMEM, DMEM | |
| big SRAM | scratchpad | region 0x03, unchanged |
| area | bank | |
| chunk; chunk address | word; word address | 21 bits, unchanged |
| network, mesh (the links) | NoC | |
| link word | packet | |
| right, left, down, up | east, west, south, north | |
| injection, eject, loopback | local (in), local (out)/eject, loopback | |
| fifth port; tiles 0, 1, 2 | attach port; attach tile (0, 7, 15 at 8 × 8) | |
| FIFO (router, engines) | queue | |
| selector | arbiter | |
| route block | (the router's routes, 16.2) | no separate name |
| request bus; requestor; request port | request tree; requester; request unit | |
| receive queue (at the root) | receive queue (in the receiving tile) | |
| register block | tile window | 0xFFFFF800, unchanged base |
| table window 0xFFFF2000–0xFFFF35FF | windows 0–10, 0xFFFF_E000–0xFFFF_F5FF | `HIST` 0xFFFF2000 → 0xFFFF_E000; `PTR` 2400 → E400; `STREAM_SRC` 2800 → `STREAM_SRC_ADDR` E800; `STREAM_DEST` 2C00 → `STREAM_DEST_ADDR` EC00; `STREAM_COUNT` 3000 → `STREAM_HDR` F000; `GROUP` 3400 → F400 |
| bus error | access fault | |
| address generator run; stream run; group run; engine run | transfer; stream transfer; group transfer | |
| send port; holding register | write unit; holding register | |
| controller read port; request queue | read unit; read queue | |
| GATHER engine; request queue (16) | gather unit; gather queue (`GATHER_QUEUE_DEPTH` = 64) | |
| gather request; gather value packet | `GATHER` packet; value (an `APPEND`) | |
| SRAM read queue | read arbiter | |
| SRAM write queue; DMA write engine | (none); write engine, in `receive_unit` | |
| traffic director | traffic director, in `receive_unit` | |
| port B (of controller memory) | the NoC port (`noc_write`) | |
| mode 0 write at address | `WRITE` | |
| mode 1 big-SRAM write by source | `APPEND` | |
| mode 2 bucket write | `BUCKET` | |
| mode 3 augmented write | `AUGMENT` | |
| mode 4 gather request | `GATHER` | |
| mode 5 count only | `COUNT` | |
| write counter; `WRITE_COUNTER_n` | counter; `COUNTER_n` | |
| unexpected arrival | counter underflow (`ERRORS.counter_underflow`) | |
| done early | early arrival (`ERRORS.early_arrival`) | |
| READY, GO; `done` | arrive, release (`BARRIER_ARRIVE`, `BARRIER_RELEASE`); arrive | |
| `PULSE_DMA` (tile) | `DMA_START` | |
| `PULSE_GO` | `RELEASE` | |
| `READY_LINES_0`–`1` | `ARRIVALS_0`–`1` | |
| "toast" pin | toast pin (`toast`) | |
| `rd_running` (`STATUS`) | `STATUS.dma_busy` | |
| `rd_done` (`STATUS`) | gone; `EVENTS.dma_done` | |
| DMA run done (`EVENTS` [10]) | `EVENTS.dma_done` ([1]) | |
| GATHER busy (`STATUS` [2]) | `STATUS.gather_busy` ([2]) | |
| router overflow `ERRORS` [0] | `ERRORS.router_overflow` [0] | |
| bad packet `ERRORS` [1] | `ERRORS.bad_packet` [1] | |
| drift `ERRORS` [3] | `ERRORS.drift` [3] | |
| write-engine input overflow `ERRORS` [4] | gone | no input FIFO |
| table request overflow `ERRORS` [5] | gone | no request FIFOs |
| GATHER queue overflow `ERRORS` [6] | `ERRORS.gather_overflow` [4] | now P |
| read-port error `ERRORS` [7] | `ERRORS.read_overflow` [5] | |
| `DMA_CONFIG` kind [0], start [8:1], count [17:9] | the same | |
| (per-stream gap after, `STREAM_SRC` [23:21]) | `DMA_CONFIG.gap` [20:18] | one per transfer |
| `DMA_CONFIG` histogram/bucket digits | `RECEIVE_CONFIG.bucket_digit` [2:0], `.histogram_digit` [5:3] | old positions: v1.35's 8.5 (D3) |
| `DMA_CONFIG` *k* − 1 [26:24] | `GATHER_CONFIG.value_size` [23:21] | |
| `DMA_RECORD_BASE_ADDRESS` | `GATHER_CONFIG.record_base` [20:0] | |
| `STREAM_SRC` source [20:0] | `STREAM_SRC_ADDR` [20:0] | |
| `STREAM_DEST` [31:0] | `STREAM_DEST_ADDR` [31:0] | |
| `STREAM_COUNT` count [16:0] | `STREAM_HDR.count` [31:15] | |
| `STREAM_COUNT` read size [18:17] | `STREAM_HDR` size [1:0] | header block |
| `STREAM_COUNT` counter [21:19] | `STREAM_HDR` counter [4:2] | |
| `STREAM_COUNT` mode [24:22] | `STREAM_HDR` mode [7:5] | |
| `STREAM_COUNT` destination *d* [31:25] | `STREAM_HDR.dst` [14:8] | broadcast 126, none 127 at 8 × 8: unchanged values |
| `GROUP` low word: R [17:0] | `GROUP` word 0 `rotations` [17:0] | |
| `GROUP` high word: start stream [7:0] | word 0 `first_stream` [25:18] | |
| `GROUP` high word: *t* − 1 [11:8] | word 0 `streams` [29:26] | |
| `GROUP` high word: gap [19:12] | word 1 `gap` [7:0] | |
| `SRAM_RD_DEST_ADDR` destination [16:0], size [18:17], counter [21:19] | `READ_DEST_ADDR` [31:0]; `READ_HDR` size [1:0], counter [4:2] | the destination is now a full byte address in DMEM, not an offset |
| `SRAM_RD_ADDR_QUEUE` (store: queue a read; load: free entries) | `READ_SEND` (store); `READ_FREE` (load) | |
| `ROUTER_HDR`, `ROUTER_ADDR`, `ROUTER_SEND_*` | `WRITE_HDR`, `WRITE_DEST_ADDR`, `WRITE_SEND_*` | D3 has the layouts |
| `DMA_HDR`, `DMA_ADDR` (peripheral) | `DMA_HDR`, `DMA_DEST_ADDR` | |
| K | `LAND_LATENCY` | 8 |
| `cm_latency` | `WRITE_PORT_LATENCY` | 2 |
| `rp_latency` | `PERIPHERAL_LATENCY` | a bound, ≤ `LAND_LATENCY` − `WRITE_PORT_LATENCY` |
| `ts_engine_latency`, L_ts | `TABLE_LATENCY` | 1 by default |
| `ag_lookahead` | `READ_AHEAD` | 2·`TABLE_LATENCY` + 2 |
| `queue_depth` | `ROUTER_QUEUE_DEPTH` | 16 |
| `link_latency` | (16.1: three register stages) | no parameter name |
| `mem_chunks` | `SCRATCHPAD_BYTES` | power of two |
| token count (64) | `TOKEN_COUNT` | |
| loopback FIFO depth | `LOOPBACK_QUEUE_DEPTH` | |
| read-port queue depth (≥ 64) | `READ_QUEUE_DEPTH` | |
| `reqbus_latency` | (none) | Appendix A's figure |
| `intra_tile_latency`, `ready_latency`, `error_latency` | (simulator's; `ARRIVE_LATENCY`, `ERROR_LATENCY` for the nets) | D4's 19.1 |
| KB, MB | KiB, MiB | 2 MB → 2 MiB, 512 KB → 512 KiB |

### 7.2 The registers, word by word (v1.35's section 8)

Addresses: v1.35 gives the low 12 bits of 0xFFFFFxxx; v1.36 gives the offset in the window and the full address.
The tile window stays at 0xFFFF_F800 and peripheral *n*'s at 0xFFFF_F800 + 0x200·*n*; only the words inside move.

**Region 0xFF (v1.35's 8.1, 8.9 → 5.2)**

| v1.35 name | v1.36 name | note (address/bits old → new) |
|---|---|---|
| the table window, 0xFFFF2000–0xFFFF35FF | windows 0–10, 0xFFFF_E000–0xFFFF_F5FF | base 0xFFFF2000 → 0xFFFF_E000; the tables' order and offsets inside are unchanged (closes item 120) |
| `HIST`, 0xFFFF2000 | `HIST`, 0xFFFF_E000 | count [16:0] unchanged; load and store, as before |
| `PTR`, 0xFFFF2400 | `PTR`, 0xFFFF_E400 | word address [20:0] unchanged; load was a bus error → loadable |
| `STREAM_SRC`, 0xFFFF2800 | `STREAM_SRC_ADDR`, 0xFFFF_E800 | source [20:0] unchanged; gap after [23:21] → removed (one gap per transfer, `DMA_CONFIG.gap`); loadable |
| `STREAM_DEST`, 0xFFFF2C00 | `STREAM_DEST_ADDR`, 0xFFFF_EC00 | [31:0] unchanged; loadable |
| `STREAM_COUNT`, 0xFFFF3000 | `STREAM_HDR`, 0xFFFF_F000 | count [16:0] → [31:15]; size [18:17] → [1:0]; counter [21:19] → [4:2]; mode [24:22] → [7:5]; *d* [31:25] → `dst` [14:8]; loadable. On a special tile a `dst` below `TILE_COUNT` is now absolute |
| `GROUP`, 0xFFFF3400, low word then high word | `GROUP`, 0xFFFF_F400, word 0 then word 1 | low: R [17:0] → word 0 `rotations` [17:0]; high: start stream [7:0] → word 0 `first_stream` [25:18]; high: *t* − 1 [11:8] → word 0 `streams` [29:26]; high: gap [19:12] → word 1 `gap` [7:0]; loadable. A store of R now rewrites `first_stream` and `streams` too |
| (unmapped) | window 11, 0xFFFF_F600 | unmapped: access fault |
| the controller's register block, 0xFFFFF800 | the tile window, 0xFFFF_F800 | eight blocks of 16 words (5.3) |
| peripheral *n*'s window | peripheral *n*'s window | same bases; block 0 the control block, block 1 the DMA, blocks 2–7 (words 32–127) its own |

**The tile window (v1.35's 8.1–8.8, 8.10 → 5.5, 7–13)**

| v1.35 name (offset) | v1.36 name (offset in window; address) | note |
|---|---|---|
| `PARAMS` (0x800) | `PARAMS` (0x000; 0xFFFF_F800) | peripherals [2:0] → `peripherals` [9:7]; endpoint id [26:20] → `tile_id` [6:0]; big-SRAM size [31:27] → removed (a build parameter, `SCRATCHPAD_BYTES`); new: `receiver` [10], `request_target` [13:11], `attach` [16:14], `barrier` [17], `boot_local` [18] |
| `STATUS` (0x804) | `STATUS` (0x004; 0xFFFF_F804) | `rd_running` [0] → `dma_busy` [1]; `rd_done` [1] → removed (`EVENTS.dma_done`); GATHER busy [2] → `gather_busy` [2]; `send_busy` [3] → removed; `done` [4] → `arrived` [0]; "command landed" [5] → removed; `request_busy` [6] → `request_full` [3] |
| `ERRORS` (0x808) | `ERRORS` (0x008; 0xFFFF_F808) | router overflow [0] → `router_overflow` [0]; bad packet [1] → `bad_packet` [1]; unexpected arrival [2] → `counter_underflow` [2]; drift [3] → `drift` [3]; write-engine input overflow [4] → removed; table request overflow [5] → removed; GATHER queue overflow [6] → `gather_overflow` [4]; read-port error [7] → `read_overflow` [5]; done early [8] → `early_arrival` [6]; bus error [9] → `access_fault` [7]; illegal instruction [10] → `illegal_instruction` [8]; peripherals 1–3 [11]–[13] → `peripheral_1`–`_3` [10]–[12]; receive underflow [14] → `receive_underflow` [9] |
| `PULSE_DONE` (0x80C) | `ARRIVE` (0x018; 0xFFFF_F818) | acts on a store with bit 0 set |
| `PULSE_DMA` (0x810) | `DMA_START` (0x044; 0xFFFF_F844) | block 1, word 1 |
| reserved (0x814) | — | removed; the word 0x014 is now `EVENTS_MASK` |
| `EVENTS` (0x818) | `EVENTS` (0x00C; 0xFFFF_F80C) | write counter *n* done [*n* − 1] → `counter_n_done` [6 + *n*]; counters done [7] → `all_counters_done` [2]; GO [8] → `release` [0]; command landed [9] → removed; DMA run done [10] → `dma_done` [1]; request FIFO no longer full [11] → removed; request pending [12] → `request_pending` [3]; peripherals 1–3 [15:13] → `peripheral_1`–`_3` [6:4] |
| `EVENTS_PEEK` (0x81C) | `EVENTS_PEEK` (0x010; 0xFFFF_F810) | bits as `EVENTS` |
| `EVENTS_MASK` (0x820) | `EVENTS_MASK` (0x014; 0xFFFF_F814) | bits as `EVENTS`; reset [8] \| [7] = 0x180 → `release` \| `all_counters_done` = 0x5 |
| `EVENTS_REQUIRED` (0x824) | — | removed: software loops on the events it waits for |
| `ROUTER_HDR` (0x828) | `WRITE_HDR` (0x080; 0xFFFF_F880) | no-increment [0] → `no_increment` [15]; size [18:17] (reserved) → [1:0] (ignored on a store, reads 0); counter [21:19] → [4:2]; mode [24:22] → [7:5]; dst [31:25] → `dst` [14:8] |
| `ROUTER_ADDR` (0x82C) | `WRITE_DEST_ADDR` (0x084; 0xFFFF_F884) | [31:0]; a packet of any mode but `WRITE` carries 0 |
| `ROUTER_SET_W0`–`W2` (0x830–0x838) | `WRITE_W0`–`W2` (0x088–0x090; 0xFFFF_F888–F890) | unchanged; 0 at the reset, where v1.35's were undefined |
| `ROUTER_SEND_32`, `_64`, `_128` (0x83C–0x844) | `WRITE_SEND_32`, `_64`, `_128` (0x094–0x09C; 0xFFFF_F894–F89C) | unchanged |
| `REQUEST_SET_W0`–`W2` (0x848–0x850, stores) | `REQUEST_OUT_W0`–`W2` (0x100–0x108; 0xFFFF_F900–F908) | send and receive no longer share addresses; 0 at the reset |
| `REQUEST_SEND` (0x854, store) | `REQUEST_SEND` (0x10C; 0xFFFF_F90C) | |
| `REQUEST_GET_W0`–`W2` (0x848–0x850, loads) | `REQUEST_IN_W0`–`W2` (0x110–0x118; 0xFFFF_F910–F918) | |
| `REQUEST_RECEIVE` (0x854, load) | `REQUEST_RECEIVE` (0x11C; 0xFFFF_F91C) | |
| `DMA_CONFIG` (0x858) | `DMA_CONFIG` (0x040; 0xFFFF_F840) | run kind [0] → `kind` [0]; run start [8:1] → `start` [8:1]; run count [17:9] → `count` [17:9]; new `gap` [20:18] (stream transfers); bucket digit [20:18] → `RECEIVE_CONFIG.bucket_digit` [2:0]; histogram digit [23:21] → `RECEIVE_CONFIG.histogram_digit` [5:3]; *k* − 1 [26:24] → `GATHER_CONFIG.value_size` [23:21] |
| — | `RECEIVE_CONFIG` (0x140; 0xFFFF_F940) | new: the two digits |
| `DMA_RECORD_BASE_ADDRESS` (0x85C) | `GATHER_CONFIG` (0x180; 0xFFFF_F980) | chunk address [20:0] → `record_base` [20:0]; with `value_size` [23:21] |
| `WRITE_COUNTER_n` (0x85C + 4·*n*: 0x860–0x878) | `COUNTER_n` (0x140 + 4·*n*: 0x144–0x15C; 0xFFFF_F944–F95C) | count [19:0] unchanged; a sum past 2²⁰ − 1 is UB (v1.35: wraps) |
| `SRAM_RD_ADDR_QUEUE` (0x87C), store | `READ_SEND` (0x0C8; 0xFFFF_F8C8) | word address [20:0] |
| `SRAM_RD_ADDR_QUEUE` (0x87C), load | `READ_FREE` (0x0CC; 0xFFFF_F8CC) | free entries [31:0] → [15:0] |
| `SRAM_RD_DEST_ADDR` (0x880) | `READ_HDR` (0x0C0; 0xFFFF_F8C0) and `READ_DEST_ADDR` (0x0C4; 0xFFFF_F8C4) | size [18:17] → `READ_HDR` [1:0]; counter [21:19] → `READ_HDR` [4:2]; destination [16:0], a byte offset in data memory → `READ_DEST_ADDR` [31:0], a full byte address in DMEM |
| reserved words (0x884–0x9F4): read 0, ignore stores | unmapped words and block 7 | an access fault |
| simulator-only exit word (0x9F8) | — | removed; a tile's result is a request to the master tile (its firmware's `op`) |
| simulator-only trace word (0x9FC) | — | removed |

**A peripheral's window (v1.35's 8.11 → 5.5, 17.1)**

Offsets are in the window; peripheral 1's base is 0xFFFF_FA00.

| v1.35 name (word, offset) | v1.36 name (block, word; offset) | note |
|---|---|---|
| `PARAMS` (0, 0x00) | `PARAMS` (0, 0; 0x000) | type [7:0], version [15:8]; constants [31:16] |
| `STATUS` (1, 0x04) | `STATUS` (0, 1; 0x004) | run in progress [0] → `busy`; DMA in progress [1] → `dma_busy` |
| `ERRORS` (2, 0x08) | `ERRORS` (0, 2; 0x008) | |
| `PULSE_RUN` (3, 0x0C) | `START` (0, 6; 0x018) | |
| `PULSE_DMA` (4, 0x10) | `DMA_START` (1, 2; 0x048) | |
| `EVENTS` (5, 0x14) | `EVENTS` (0, 3; 0x00C) | run done [0] → `done`; DMA done [1] → `dma_done` |
| `EVENTS_PEEK` (6, 0x18) | `EVENTS_PEEK` (0, 4; 0x010) | |
| `EVENTS_MASK` (7, 0x1C) | `EVENTS_MASK` (0, 5; 0x014) | reset 0, as before |
| `DMA_HDR` (8, 0x20) | `DMA_HDR` (1, 0; 0x040) | count [11:0] → `count` [26:15]; output queue [13:12] → `queue` [28:27]; size [18:17] → [1:0]; counter [21:19] → [4:2]; mode [24:22] → [7:5]; dst [31:25] → `dst` [14:8] |
| `DMA_ADDR` (9, 0x24) | `DMA_DEST_ADDR` (1, 1; 0x044) | advances only on a `WRITE` |
| reserved words 10–31: read 0 | block 0 words 7–15, block 1 words 3–15 | unmapped: an access fault. A peripheral without a DMA: every word of block 1 reads 0 |
| words 32–127, peripheral-defined | blocks 2–7, the same word numbers and offsets | a word the peripheral does not define is an access fault |

**Ingress/Egress (v1.35's 6.6, 8.13, 8.14 → 17.6)**

Words 32–45 keep their numbers and offsets (0x080–0x0B4 in the window; 0xFFFF_FA80–FAB4).

| v1.35 name (word) | v1.36 name (word) | note |
|---|---|---|
| `ERROR_LINES_0`–`2` (32–34) | `ERROR_LINES_0`–`2` (32–34) | unchanged; by tile id |
| `READY_LINES_0`–`1` (35, 36) | `ARRIVALS_0`–`1` (35, 36) | |
| `READY_EXPECT_0`–`1` (37, 38) | `ARRIVE_EXPECT_0`–`1` (37, 38) | |
| `PULSE_GO` (39) | `RELEASE` (39) | |
| `WATCHDOG` (40) | `WATCHDOG` (40) | a store of *N* sets the limit and the count; 0 stops it |
| `LED` (41) | `LED` (41) | |
| `UART_TX` (42) | `UART_TX` (42) | |
| `UART_RX` (43) | `UART_RX` (43) | |
| `UART_DIVISOR` (44) | `UART_DIVISOR` (44) | counts the chip's clock, not a reference oscillator's; reset 57,600 baud → 115,200 baud (`UART_RESET_DIVISOR`) |
| `UART_STATUS` (45) | `UART_STATUS` (45) | TX [4:0], RX [12:8], RX overrun [16] → `rx_overrun`, framing error [17] → `framing_error` |
| reserved (46–63) | unmapped | an access fault |
| host-interface words (64–127), deferred | `STREAM_OUT_W0`–`W2` (64–66), `STREAM_SEND` (67), `STREAM_SEND_LAST` (68), `STREAM_IN_W0`–`W2` (69–71), `STREAM_RECEIVE` (72) | new; the rest unmapped |
| `STATUS` (no own bits) | `STATUS.stream_last` [2] | new |
| `ERRORS` [0], the unit-error bit | `ERRORS.tile_error` [0] | live; Ingress/Egress has no errors of its own |
| `EVENTS` [2] all tiles READY | `EVENTS.all_arrived` [2] | live |
| `EVENTS` [3] watchdog expired | `EVENTS.watchdog` [3] | counted, depth 1 |
| `EVENTS` [4] UART RX not empty, [5] UART TX not full | `EVENTS.uart_rx` [4], `EVENTS.uart_tx` [5] | live |
| `EVENTS` [31:6], the host interface's, deferred | `EVENTS.stream_in` [6], `EVENTS.stream_out` [7] | new, live |

**The RW engine (new: 17.7; v1.35 had no words for it)**

| v1.35 name | v1.36 name | note |
|---|---|---|
| — | `RW_CONFIG` (32), `RW_LPDDR_ADDR` (33), `RW_BUFFER_ADDR` (34), `RW_REPLY_W0` (35), `RW_REPLY_SEND` (36); `ERRORS.lpddr_error` [0] | new |
| tag, tag window, tag pool | a part of the write buffer; the software's allotment | 11.5, 17.7, 23 |

**Terms and parameters in sections 6–8**

| v1.35 name | v1.36 name | note |
|---|---|---|
| command, command block, command word, argument blocks, "command landed" | — | removed: a fixed schedule in the image, arguments in DMEM (6.5) |
| `wait_for_command()` | the barrier: `ARRIVE`, `WFI`, a load of `EVENTS` | the sort's port calls it `arrive_and_wait()` |
| boot part 2; "started" request | — | removed |
| overlay; staging | — (image; the load) | removed |
| `stage_delay` | — | removed: `ARMED`/`STREAMS_ARMED`/`LOAD` requests |
| `ready_latency`, `go_latency`, `error_latency` | `ARRIVE_LATENCY`, `RELEASE_LATENCY`, `ERROR_LATENCY` | the chip's (19) |
| `reg_load_latency` (2 block/window, 1 table/memory) | `WINDOW_LOAD_LATENCY` 2, `TABLE_LOAD_LATENCY` 1 | 6.4 |
| pulse register | action (a register named for what its store does) | 1.2 |
| configuration register | the same | undefined at the reset, but `WRITE_W*`/`REQUEST_OUT_W*`, now 0 |
| hard interrupt; the image enabling `MEIE`/`MIE` | NMI; the ROM sets `mie.MEIE`, `mstatus.MIE` stays 0 | 6.2 |
| report sequence number | `REPORT`'s `number` (word 0 [25:18]) | 11.5 |
| the unit-error bit | `tile_error` | 17.6 |
| the keep-alive watchdog | the watchdog | 17.6 |
| the memory system (C&C 2's peripheral) | the RW engine (built, 17.7); the memory system (the translator and cache behind `fill`, open, 2.1) | |
| fill bus | fill port, `fill` | 3 |
| `f_ref`, reference oscillator | — | the UART counts the chip's clock |

### 7.3 The bits of `STATUS`, `EVENTS` and `ERRORS`, the peripherals' words, the requests (v1.35's 4.11 to 4.20)

| v1.35 name | v1.36 name | note (address/bits old → new) |
|---|---|---|
| controller | core | |
| hard interrupt | NMI (`nmi`) | maskable → non-maskable, at most once |
| wakeup input | MEIP (`meip`) | |
| bus error | access fault | |
| register-load latency (2 / 1) | `WINDOW_LOAD_LATENCY` / `TABLE_LOAD_LATENCY` | |
| port A / port B | the core port / the NoC port (`noc_write`) | |
| command block, "command landed" | — | removed |
| reply block | reply block | software convention; address the image's |
| `WRITE_COUNTER_n` | `COUNTER_n` | 0x85C + 4(n−1) → tile window 0x144 + 4(n−1) (block 5, word n) |
| unexpected arrival | counter underflow, `ERRORS.counter_underflow` | [2] → [2] |
| counters done, `EVENTS` [7] | `EVENTS.all_counters_done` | [7] → [2] |
| write counter n done, `EVENTS` [n−1] | `EVENTS.counter_n_done` | [n−1] → [6+n] |
| GO, `EVENTS` [8] | `EVENTS.release` | [8] → [0] |
| command landed, `EVENTS` [9] | — | removed |
| DMA run done, `EVENTS` [10] | `EVENTS.dma_done` | [10] → [1] |
| request FIFO no longer full, `EVENTS` [11] | — | removed |
| request pending, `EVENTS` [12] | `EVENTS.request_pending` | [12] → [3] |
| peripheral n, `EVENTS` [12+n] | `EVENTS.peripheral_n` | [13–15] → [4–6] |
| `EVENTS_MASK` reset [8] \| [7] (0x180) | `release` \| `all_counters_done` (0x5) | |
| `EVENTS_REQUIRED` | — | removed |
| `done` pulse, `PULSE_DONE` | `ARRIVE` | tile window word 6 (0x018) |
| `done` gate | arrival gate | lasts one cycle longer |
| `STATUS` `rd_running` | `STATUS.dma_busy` | → [1] |
| `STATUS` `rd_done` | — (`EVENTS.dma_done`) | removed |
| `STATUS` GATHER busy | `STATUS.gather_busy` | → [2] |
| `STATUS` `send_busy` | — | removed |
| `STATUS` `done` | `STATUS.arrived` | → [0] |
| `STATUS` "command landed" [5] | — | removed |
| `STATUS` `request_busy` [6] | `STATUS.request_full` | → [3] |
| `PARAMS` peripherals [2:0] | `PARAMS.peripherals` | → [9:7] |
| `PARAMS` endpoint id [26:20] | `PARAMS.tile_id` | → [6:0] |
| `PARAMS` big-SRAM size [31:27] | — | removed; new `receiver` [10], `request_target` [13:11], `attach` [16:14], `barrier` [17], `boot_local` [18] |
| `ERRORS` [0] router overflow | `router_overflow` | [0] |
| `ERRORS` [1] bad packet | `bad_packet` | [1] |
| `ERRORS` [2] unexpected arrival | `counter_underflow` | [2] |
| `ERRORS` [3] drift | `drift` | [3] |
| `ERRORS` [4] write-engine input overflow | — | removed |
| `ERRORS` [5] table request overflow | — | removed |
| `ERRORS` [6] GATHER queue overflow (H) | `gather_overflow` (P) | [6] → [4] |
| `ERRORS` [7] read-port error | `read_overflow` | [7] → [5] |
| `ERRORS` [8] done early | `early_arrival` | [8] → [6] |
| `ERRORS` [9] bus error | `access_fault` | [9] → [7] |
| `ERRORS` [10] illegal instruction | `illegal_instruction` | [10] → [8] |
| `ERRORS` [11–13] peripheral 1–3 | `peripheral_1`–`peripheral_3` | → [10–12] |
| `ERRORS` [14] receive underflow | `receive_underflow` | [14] → [9] |
| `ROUTER_HDR` | `WRITE_HDR` | no-increment [0] → `no_increment` [15]; size [18:17] → [1:0] (ignored); counter [21:19] → [4:2]; mode [24:22] → [7:5]; dst [31:25] → [14:8] |
| `ROUTER_ADDR` | `WRITE_DEST_ADDR` | |
| `ROUTER_SET_W0`–`W2` | `WRITE_W0`–`W2` | |
| `ROUTER_SEND_32`, `_64`, `_128` | `WRITE_SEND_32`, `_64`, `_128` | |
| send port; holding register | write unit; holding register | |
| request port; request FIFO (128) | request unit; request queue (`REQUEST_QUEUE_DEPTH` 4) | |
| `REQUEST_SET_W0`–`W2` | `REQUEST_OUT_W0`–`W2` | 0x100–0x108 |
| `REQUEST_SEND` | `REQUEST_SEND` | 0x10C |
| `REQUEST_GET_W0`–`W2` | `REQUEST_IN_W0`–`W2` | shared offsets → 0x110–0x118 |
| `REQUEST_RECEIVE` | `REQUEST_RECEIVE` | → 0x11C |
| requestor [6:0], target [9:7] | requester, target | same bits |
| report op, sequence number (draft [31:21]) | `op` [17:10] = 1 `REPORT`, `number` [25:18] | item 122 |
| `reqbus_latency` (17) | — | 11 / 10 in the RTL (21) |
| peripheral `PULSE_RUN` (word 3) | `START` | word 3 → block 0 word 6 |
| peripheral `PULSE_DMA` (word 4) | `DMA_START` | word 4 → block 1 word 2 (word 18) |
| peripheral `EVENTS` / `EVENTS_PEEK` / `EVENTS_MASK` (words 5, 6, 7) | the same | → words 3, 4, 5 |
| peripheral `DMA_HDR` (word 8) | `DMA_HDR` | → word 16; count [11:0] → `count` [26:15]; output queue [13:12] → `queue` [28:27]; size [18:17] → [1:0]; counter [21:19] → [4:2]; mode [24:22] → [7:5]; dst [31:25] → `dst` [14:8] |
| peripheral `DMA_ADDR` (word 9) | `DMA_DEST_ADDR` | → word 17 |
| `STATUS` [0] run in progress; [1] DMA in progress | `STATUS.busy`; `STATUS.dma_busy` | same bits |
| run done `EVENTS` [0]; DMA done [1] | `EVENTS.done`; `EVENTS.dma_done` | same bits |
| standard words (0–9); words 10–31 reserved | control block (block 0), DMA block (block 1) | words 32–127 keep their numbers (blocks 2–7) |
| `rp_latency` (2) | `PERIPHERAL_LATENCY` (≤ 6) | |
| `cm_latency` | `WRITE_PORT_LATENCY` | |
| K | `LAND_LATENCY` | |
| `ts_engine_latency` | `TABLE_LATENCY` | |
| `ready_latency`, `go_latency`, `error_latency` | `ARRIVE_LATENCY`, `RELEASE_LATENCY`, `ERROR_LATENCY` | |
| fifth port; "from/to the controller" FIFOs | attach port; its queues | only on attach tiles |
| down port (of a controller's router) | attach port (of a special tile's router) | |
| src = id + 8 (72–74) | src = the tile's id | reversal of item 118 |
| `UART_TX`, `UART_RX`, `UART_DIVISOR`, `UART_STATUS` (words 42–45; v1.35's 8.13 order TX, RX, divisor, status) | the same names, words 42, 43, 44, 45 | `UART_STATUS`: [4:0] TX bytes, [12:8] RX bytes, [16] `rx_overrun`, [17] `framing_error` (D3 has v1.35's 8.13 bit positions) |
| UART RX not empty / TX not full | `EVENTS.uart_rx` [4] / `EVENTS.uart_tx` [5] | D3 has v1.35's positions |
| RX overrun, framing error | `UART_STATUS.rx_overrun`, `.framing_error` | |
| `PULSE_GO` | `RELEASE` | word 39 |

### 7.4 The core

| v1.35 name | v1.36 name | note |
|---|---|---|
| controller (a unit's RV32 processor) | core | `spec/core.md`; machine-spec 6 |
| C&C controllers | the special tiles' cores | |
| hard interrupt | NMI (`nmi`) | unmaskable, taken at most once; `mcause` 0x8000_0000 (core.md Traps; 6.2) |
| wakeup input | MEIP (`external_interrupt`, `mip`/`mie` bit 11) | released from `WFI` by `(mip & mie) != 0`; the ROM sets `mie.MEIE`, software keeps `mstatus.MIE` 0 (6.2) |
| program memory / data memory / controller memory | IMEM / DMEM | IMEM and the boot ROM fetch-only (5.1) |
| bus error | access fault | `mcause` 1 (instruction), 5 (load), 7 (store) |
| `ERRORS` [9] bus error; `ERRORS` [10] illegal instruction | `ERRORS.access_fault` [7]; `ERRORS.illegal_instruction` [8] | 7.4; raised through `core_status` pulses (3) |
| `mtime` / `time` CSR | none | removed; `mcycle`/`mcycleh` are the time (6.1) |
| `-march=rv32i_zmmul_zicsr -mabi=ilp32` | `-march=rv32ic_zicond_zicsr_zmmul -mabi=ilp32` | core.md, Core contract, ISA |
| `branch_penalty` (1) | by kind: taken branch / other JALR +3; JAL / ready return +2 or fewer | core.md, The core's cycle costs |
| multiply latency 3 | two pipelined stages, +1 / +1 interlocks | core.md, Multiply |
| register-load latency (`reg_load_latency`), table-window load latency | `WINDOW_LOAD_LATENCY` (2), `TABLE_LOAD_LATENCY` (1) | values unchanged; stores now take the same latency (6.4) |
| send port's holding register | the write unit's holding register | 9 |
| done / READY / GO at the core | arm write (`ARRIVE`), canonical wait, release on MEIP | core.md Lockstep regions; 7.3 |

### 7.5 The parameters (v1.35's section 10; the full mapping is 19.1)

| v1.35 name | v1.36 name | note |
|---|---|---|
| K | `LAND_LATENCY` | 8 → 8 |
| `cm_latency` | `WRITE_PORT_LATENCY` | 2 |
| `rp_latency` | `PERIPHERAL_LATENCY` | fixed 2 → a bound, ≤ `LAND_LATENCY` − `WRITE_PORT_LATENCY`, measured from the write port |
| `ts_engine_latency` | `TABLE_LATENCY` | 1 |
| `ag_lookahead` | `READ_AHEAD` | 2·`TABLE_LATENCY` + 2 |
| `queue_depth` | `ROUTER_QUEUE_DEPTH` | 16 |
| (loopback FIFO depth) | `LOOPBACK_QUEUE_DEPTH` | ≥ `TOKEN_COUNT` |
| `reg_load_latency` | `WINDOW_LOAD_LATENCY`, `TABLE_LOAD_LATENCY` | 2, 1 |
| `ready_latency` | `ARRIVE_LATENCY` | 8 → the chip's |
| `go_latency` | `RELEASE_LATENCY` | 8 → the chip's |
| `error_latency` | `ERROR_LATENCY` | 8 → the chip's; no longer tied to `ARRIVE_LATENCY` |
| `tr_probe_lines` | `MAP_PROBE_LINES` | 3 |
| `mem_chunks` | `SCRATCHPAD_BYTES` | chunks → bytes; power of two |
| (area size limit) | `MAX_BANK_BYTES` | 512 KiB |
| `link_latency` | a link's three register stages (16.1) | no parameter name in chapter 19 |
| `mem_idle`, `cm_idle`, `intra_tile_latency`, `branch_penalty`, `hop_latency`, `cc_link_latency`, `cc_dma_depth`, `strict_replies`, `skew` | — | the simulator's own, or removed (19.1) |
| `stage_delay` | — | removed (19.1) |
| GATHER request queue (16) | `GATHER_QUEUE_DEPTH` | = `TOKEN_COUNT` |
| request FIFO (128) | `REQUEST_QUEUE_DEPTH` | 4 |
| receive queues (at the root) | `RECEIVE_QUEUE_DEPTH` | per target |
| read port's request queue | `READ_QUEUE_DEPTH` | ≥ 64 |
| write counters, 20 bits | `COUNTER_BITS` | 20 |
| invariant *n* (section 9) | invariant *n* (chapter 20) | numbers unchanged |
| item *n* (section 13) | item *n* (chapter 26) | numbers unchanged |

### 7.6 The replayer

| v1.35 name | v1.36 name | note |
|---|---|---|
| the replayer's window: `PULSE_RUN` (word 3) | `START` (block 0, word 6, offset 0x018) | acts on bit 0 |
| `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK` (words 5, 6, 7) | the same names, words 3, 4, 5 (0x00C, 0x010, 0x014) | control block layout |
| words 4, 8, 9 (reserved: no DMA) | block 1 (words 16–18) reads 0 | no DMA |
| `STATUS` [0] run in progress | `STATUS.busy` | |
| `EVENTS` [0] run done | `EVENTS.done` | counted, 1 |
| `EVENTS` [2] code credit, [3] read-log credit, [4] write-log credit, [5] expansion credit | `code_credit`, `read_log_credit`, `write_log_credit`, `expansion_credit` | same bits; counted, 2 |
| `EVENTS` [6] new expansion | `new_expansion` | same bit |
| `ERRORS` [0] reserved | — | dropped |
| `ERRORS` [1] queue overrun | `ERRORS.queue_overrun` [0] | |
| `ERRORS` [2] invalid expansion | `ERRORS.invalid_expansion` [1] | |
| `ERRORS` [3] code-stream underflow | `ERRORS.code_underflow` [2] | |
| `ERRORS` [4] read-log underflow | `ERRORS.read_log_underflow` [3] | |
| `ERRORS` [5] write-log underflow | `ERRORS.write_log_underflow` [4] | |
| `ERRORS` [6] done with input left over | `ERRORS.leftover` [5] | |
| `RUN_CONFIG` [23:0] rows to run, [24] use the store log, [25] output `rd_prev`, [28:26] the Tracer's compression enables | `RUN_CONFIG.rows`, `.store_log`, `.rd_prev`, `.tracer_compression` | word 32, 0x080, unchanged |
| `ROWS_RUN`, `EXPANSION_OFFSET`, `EXPANSION_OPCODE` | the same | words 33–35, unchanged; `EXPANSION_OFFSET` now latched at the post |
| the tile's `EVENTS` [13] (peripheral 1 live bit) | the tile's `EVENTS.peripheral_1` [4] | |
| the tile's `ERRORS` [11] | the tile's `ERRORS.peripheral_1` [10] | |
| state record; checkpoint | record | 68 words |
| state word: next offset | state word: `offset`, the row offset | [111:96] |
| stream word: words, values out, LRU-compressed flag | `code_words`, `read_log_words`, `write_log_words`; `code_values`, …; `code_compressed`, … | same bits |
| opcode stream; opcode word | code stream; code value | |
| decompressor; compressed stream | decoder; LRU-coded stream | "compressed" stays for an RVC instruction |
| controller-fed; controller (decode class 1, 2) | core-fed | |
| the controller loop | the core's loop | |
| queue port | a queue's part (of the region) | parts 1–4 |
| part 0 (as "the working state") | the record's words 0–65 | |
| stopped / running | idle / busy (`STATUS.busy`) | |
| run (of the replayer) | pass | |
| `rp_latency` | `PERIPHERAL_LATENCY` | a bound, not 2 |
| queue size 256 entries; output queue 32 rows | `REPLAY_QUEUE_ENTRIES`; `REPLAY_OUTPUT_ROWS` | parameters (19) |
| the output queue's ready/valid | the row port, `replay_row` | interface (3) |
| `ReplayerRegs` (C struct) | the firmware header's, to be regenerated | |

### 7.7 The Tracer

| v1.35 name | v1.36 name | note (address/bits old → new) |
|---|---|---|
| C&C 3; C&C 3's peripheral | tracer tile; the Tracer, its peripheral 1 | tile id 66 = `TILE_COUNT` + 2 |
| state record | record | 68 words; unchanged layout |
| word-0 write | state write | |
| fill bus | fill port, `fill` | four channels |
| compressors | encoders | |
| controller-fed | core-fed | |
| range comparator; misalignment test | the address checks | |
| `PULSE_RUN` (word 3) | `START` (word 6, 0x018) | |
| `PULSE_DMA` (word 4) | `DMA_START` (block 1 word 2, 0x048) | |
| `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK` (words 5–7) | same names (words 3–5, 0x00C–0x014) | |
| `DMA_HDR` (word 8) | `DMA_HDR` (block 1 word 0, 0x040) | count [11:0] → [26:15]; queue [13:12] → [28:27]; dst [31:25] → [14:8]; header block [7:0] |
| `DMA_ADDR` (word 9) | `DMA_DEST_ADDR` (0x044) | |
| `STATUS` [0] run in progress; [1] DMA in progress | `STATUS.busy`; `STATUS.dma_busy` | same bits |
| `EVENTS` [0] run done | `EVENTS.done` [0] | |
| `EVENTS` [1] DMA done | `EVENTS.dma_done` [1] | |
| `EVENTS` [3] advice credit | `EVENTS.advice_credit` [2] | |
| `EVENTS` [5] expansion credit | `EVENTS.expansion_credit` [3] | |
| `EVENTS` [6] new expansion | `EVENTS.new_expansion` [4] | |
| `EVENTS` [7] state queue needs service | `EVENTS.state_ready` [5] | |
| `EVENTS` [8] code queue needs service | `EVENTS.code_ready` [6] | |
| `EVENTS` [9] read-log queue needs service | `EVENTS.read_log_ready` [7] | |
| `EVENTS` [10] write-log queue needs service | `EVENTS.write_log_ready` [8] | |
| `ERRORS` [1] queue overrun | `ERRORS.queue_overrun` [0] | |
| `ERRORS` [2] invalid expansion | `ERRORS.invalid_expansion` [1] | |
| `ERRORS` [4] advice underflow | `ERRORS.advice_underflow` [2] | |
| `ERRORS` [7] jump target not in the map | `ERRORS.map_miss` [3] | |
| `ERRORS` [8] store where the copy can't write | `ERRORS.store_not_writable` [4] | |
| `ERRORS` [9] page doesn't exist | `ERRORS.page_missing` [5] | |
| `ERRORS` [10] outside the provable range | `ERRORS.out_of_range` [6] | |
| `ERRORS` [11] failed assertion | `ERRORS.failed_assertion` [7] | |
| `ERRORS` [12] misaligned access | `ERRORS.misaligned` [8] | |
| — | `ERRORS.write_back_error` [9] | new |
| `RUN_CONFIG` [23:0] rows; [24], [25]; [26] compress code, [27] read log, [28] write log | `RUN_CONFIG.rows`; `.store_log`, `.rd_prev`; `.tracer_compression` [28:26] | word 32, unchanged |
| words 33–51 (`ROWS_RUN` … `STAT_PROBES`) | same names, same words | unchanged |
| files word fields: memory root, memory depth, map root, map depth, log₂ K, I/O region's size | `memory_root`, `memory_depth`, `map_root`, `map_depth`, `log_k`, `io_size` | bits unchanged |
| log FIFO tags: load, other read log, write log | `load`, `other`, `write` | 0, 1, 2 |
| read status ok / hole / not owned / beyond the depth's capacity | `ok`, `hole`, `not_owned`, `beyond` | 0–3; write status 0 `ok`, 1 `error` is new |
| "armed" (a count-only packet to C&C 3) | `ARMED`, `STREAMS_ARMED` requests to target 2 | op codes the firmware's (RTL firmware: 1, 2) |
| state counter | start counter | software's name (RTL firmware: COUNTER_START) |
| `tr_probe_lines` | `MAP_PROBE_LINES` | 3 |
| `tr_row_cycles`, `tr_mul_cycles`, `tr_probe_compare` | (constants of 17.9.7) | 4, 6, 2 |
| `stage_delay` | (removed) | |
| `rp_latency` | `PERIPHERAL_LATENCY` | |

## 8. Where v1.35's specification went

Every numbered section and subsection of v1.35's `machine-spec.md`, `jolt/replayer.md` and `jolt/tracer.md`
(now `spec/history/machine-spec-v1.35.md`, `replayer-v1.35.md` and `tracer-v1.35.md`), with its disposition:
**replaced** by a chapter of the RTL's specification; **kept**, rewritten in v1.36's terms and corrected where
the hardware changed; or **removed**, because the hardware no longer has it. A long section has a row for each
part that went a different way. "22 (title)" names a section of chapter 22 by its title. The last column gives
the inventory's code or the reason.

**In numbers** (rows, which are finer than sections): 121 replaced, 91 kept, 12 removed. No numbered section of
the platform is removed whole except v1.35's 8.15, the simulator-only words (5.3 makes an undefined word an
access fault); 4.17.7 and 8.12 were already stubs. What is removed inside sections: commands and the command
block, "command landed", `EVENTS_REQUIRED`, the request queue's room event, the bank request queues and the
per-client return order, the write engine's input FIFO, the tables' request FIFOs, the start skew, 256-bit
storage, the DMA's src + 8, target 3, overlays and staging, `stage_delay`, the one-at-a-time bring-up, the host
boot's sequence, the maskable hard interrupt, `mtime`, loads from IMEM, and the fixed 24-report protocol.
**v1.35's numbering is not kept:** v1.36 takes the RTL's chapter numbers, so the rule "a moved section leaves a
stub" gives way, for this one version, to this table. Item and invariant numbers are unchanged.

### 8.1 The preamble and sections 1 to 4.10

| v1.35 section | its title | disposition | where in v1.36 | why: inventory item or note |
|---|---|---|---|---|
| preamble | title and purpose paragraph | replaced | `machine-spec.md` preamble | new document, new numbering |
| preamble | Companion material | replaced | `machine-spec.md` preamble; `spec/README.md` | paths change; the figures (`v1.3/diagrams/`, at v1.1) and `v1.3/results/` are outside `v1.36/`: cite as history or drop |
| preamble | Markers (*(Proposed)*, *(Open)*, `[meas: …]`) | kept | `machine-spec.md` preamble: the RTL's *Open* in chapters 1–19, v1.35's markers in 20–27; `[meas: v1.35]` etc. | S/CONVENTIONS.md |
| preamble | How the hardware treats software bugs | replaced | 1.3 (checked or UB); `spec/GUIDELINES.md` section 2 | same two kinds; the "(hardware)"/"(meshsim)" marks become "sets `ERRORS.x`"/"UB (1.3)" |
| preamble | what v1.35's meshsim does on a broken rule (fatal / silent traps / counted) | kept | 22 (How the simulator treats a broken rule) | "silent traps" removed: A3.2, A3.7 |
| preamble | How v1.3's text is meant | kept | `spec/GUIDELINES.md` section 1 (already there); the RTL's "How it is written" replaces it in chapters 1–19 | coordinator: GUIDELINES 1 now needs "the RTL is the truth about the hardware" |
| 1 | Conventions | replaced | 1.1 (terms), 5.1, 5.4 (addresses, word addresses); kept remainder in 22 (Conventions of the kept text) | F renames; chunk→word; 8 MB→8 MiB; scratchpad size a power of two (A6.10); misaligned load/store traps (A3.5), no misaligned jump target (A3.6) |
| 2 | System | replaced | 2.1, 2.2, 16.1 (tiles, links), 11 (request tree), 18 (nets, pins) | A1.1 attach tiles 0/7/15; A1.2 special tiles 4 MiB; A1.4 attach ports only on attach tiles; A1.12 one reset (*Open*); item 82 closed |
| 2.1 | Address space (7 bits) | replaced | 4.1 (tile ids), 16.2 (reserved ids) | A1.3 `TILE_ID_BITS`; A1.8 reserved dst dropped where it enters, no error |
| 2.2 | The 32-bit address map | replaced | 5.1, 6.3; kept remainder in 22 (Staying in bounds) | A2.8 IMEM/ROM fetch-only; A2.15 no aliasing; overlays removed (A4.9) |
| 3 | Links | replaced | 4, 16.1 | — |
| 3.1 | Link word (packet) | replaced | 4.1, 4.2, 12.2, 12.3; kept remainder in 22 (Counted and uncounted traffic) | A1.9 only defined bits stored (item 117 closed); A2.7 header layout; commands removed (A4.1); item 142 closed (region decode kept) |
| 3.2 | Latency, throughput and ordering | replaced | 16.1 (links), 14.4, 16.2 (order), 16.3 (no flow control); kept: fan-in table and rationale in 22 (The links and fan-in) | start skew removed (A1.11) |
| 3.3 | Request bus | replaced | 11.2, 11.3, 11.5; kept: fairness, throughput, depth arithmetic in 22 (The request tree: rates and the limits' arithmetic) | A7.3 receive queues in the tile; A7.4 target 3 reserved; A7.7; B2.1 latency; `reqbus_latency` 17 → 11/10 |
| 4 | Tile (intro, unit table, `intra_tile_latency`) | replaced | 2.3 (module hierarchy) | `intra_tile_latency` is no contract parameter: a simulator parameter (19.1) or removed |
| 4.1 | Big SRAM | replaced | 15, 14.1; kept: placement, what follows no-coherence in 22 (Scratchpad placement and coherence) | removed: area request queues, arrival-order service, depth 8, write-engine FIFO overflow, per-client read order, partial last area, `mem_idle` (A6.1, A6.2, A6.3, A6.10, D7; items 1–4) |
| 4.2 | Address generator | replaced | 8.1–8.6; kept: uses, conventions, one-word exchange, schedule figures in 22 (The DMA unit's transfers; Group transfers and the all-to-all schedule) | A5.1–A5.9, A1.10; `rd_done` removed (A2.10); per-stream gap removed (A5.2) |
| 4.3 | Controller read port | replaced | 10; kept: software rule (item 116), search loop, costs in 22 (The read unit in use) | A5.10; command exception removed (A4.1) |
| 4.4 | GATHER engine | replaced | 13, 14.1; kept: arming, rate, why local in 22 (Gathers in use) | A6.6 queue 64; A6.7 AG gated; A6.8; E23 overflow is P; A5.1 *k* and base in `GATHER_CONFIG` |
| 4.5 | SRAM read queue, return path and injection | replaced | 14.1–14.4, 16.2 | A6.2 one return order; A6.9 loopback token freed at the director; removed: per-client return queues, SRAM write queue as a named part |
| 4.6 | Route blocks | replaced | 16.2 | same routes at 8 × 8; A1.1, A1.3 (formula by `MESH_SIZE`), A1.5 special tile's router |
| 4.7 | Output queue sets and selectors | replaced | 16.3 | A1.4 no attach queues except on attach tiles; A1.9 |
| 4.8 | Traffic director | replaced | 12.3; kept: one `LAND_LATENCY` rationale in 22 (Scratchpad writes in use) | "command landed" removed (A4.1); K formula → B3.9–B3.11 (item 128 closed); `cm_idle`/`mem_idle` removed (D7) |
| 4.9 | DMA write engine | replaced | 12.4, 12.5; kept: uses of `PTR`, augmenting in 22 (Scratchpad writes in use) | A6.3 no input FIFO, no `ERRORS` [4]; pointer stage of 2L + 2 entries is implementation |
| 4.10 | Table memories | replaced | 5.2, 8.6, 12.5; kept: waiting for the tables in 22 (Scratchpad writes in use) | A2.2 addresses; A2.3 all loadable; A2.4 sizes; A5.4; A5.5; A6.4 no request FIFOs; A6.5 core collision undefined; `EVENTS_REQUIRED` removed (A2.13); item 5, 120 closed |

### 8.2 Sections 4.11 to 5

| v1.35 section | its title | disposition | where in v1.36 | why: inventory item or note |
|---|---|---|---|---|
| 4.11 | Controller (ISA, inputs) | replaced | 6.1, 6.2; `spec/core.md` (ISA, CSRs, Traps, Wait for interrupt) | A3.1–A3.8, A3.10. C and Zicond added; NMI and MEIP replace the maskable hard interrupt and the wakeup input; `mtime` gone; item 121 closed |
| 4.11 | — Programs (C, toolchain, `-march`) | kept | 22 "Programs and the toolchain" (supplements 6) | software; `-march` becomes `rv32ic_zmmul_zicsr_zicond` (A3.1) |
| 4.11 | — Timing (one instruction a cycle; load-use 0; branch +1; multiply 3) | replaced | `spec/core.md` (the core's cycle costs); 21 | B1.2–B1.7 |
| 4.11 | — Register-load latency, the sampling rule | replaced | 6.4 | same values (2 / 1); B3.15 |
| 4.11 | — Stores (single-cycle, posted) | replaced | 6.4; `spec/core.md` (no posted stores) | A3.10, B1.1: a window store takes effect at the end of *t* + 1 |
| 4.11 | — Memories (strict Harvard), port A / port B, bus errors | replaced | 5.1, 6.3 | A2.8 (IMEM and ROM fetch-only), A2.5 (undefined words fault), A2.15 (no aliasing: UB past the end, the core faults), A2.3 (tables loadable) |
| 4.11 | — the command block and reply block rows of the memory table | removed (command block); kept (reply block, as a software default) | 22 "The reply block" (supplements 11) | A4.1 |
| 4.11 | — `.rodata` may live in program memory | removed | — | A2.8: IMEM is not loadable; constants live in DMEM |
| 4.11 | — Controller-memory addresses | replaced | 5.1, 6.3 | — |
| 4.11 | — No big-SRAM port (copies, writes, reads) | replaced | 15 ("The core"), 10 | — |
| 4.11 | — Send port (`ROUTER_*`) | replaced | 9 | A2.7, A5.11, B3.7; names F |
| 4.11 | — No-increment's use for replies | kept | 22 "The reply block" (supplements 11) | software convention; commands' use removed (A4.1) |
| 4.11 | — the send port checks nothing | replaced | 9 ("No checks") | — |
| 4.11 | — The boot ROM | replaced | 6.5 | A4.8 |
| 4.12 | The command block | removed | — | A4.1: no commands; a phase is a fixed schedule in the image, arguments are DMEM data (6.5) |
| 4.12 | "Command landed" | removed | — | A4.1, A2.10, A2.11 |
| 4.12 | The reply block | kept (software default) | 22 "The reply block" (supplements 11) | no hardware; address is software's (was a silicon constant only for the command block). 128-bit replies added (A7.6) |
| 4.12 | The stack below them | kept | 22 "The reply block" | software convention |
| 4.12 | Seven write counters | replaced | 12.2, 12.3 | names: `COUNTER_n`; counter underflow |
| 4.12 | — usage rule (one counter per thing waited for); the accepted trade-off of a stray's late detection; the simulator's diagnostic at acceptance | kept | 22 "Counters: what a program keeps" (supplements 12) | not stated by the RTL's specification |
| 4.12 | — Writes are additive | replaced | 12.2 | A6.11 (the rule is the spec's; the simulator follows it), E4 |
| 4.12 | Counters done, `EVENTS` [7] | replaced | 7.2 (`all_counters_done`), 12.2 | A4.5: "armed and drained"; a drain in the arming cycle sets it |
| 4.12 | — polling and `WFI`, the end of a phase ("a phase that sets counters ends with a wait for `all_counters_done` before it arrives"; a count of 0 never drains) | kept | 22 "Waiting: what a program keeps" (supplements 7) | software rule; invariant 4 |
| 4.12 | Per-counter done events, `EVENTS` [6:0] | replaced | 7.2 (`counter_n_done`, bits 7–13) | A2.11 |
| 4.12 | The events (table, sticky/live) | replaced | 7.2 | A2.11 |
| 4.12 | — spurious wakes, software re-checks | replaced | 17.2 | — |
| 4.12 | — `EVENTS_MASK` | replaced | 7.2 | reset value now bits 0 and 2 (0x5) |
| 4.12 | — `EVENTS_REQUIRED`, the wake term's AND | removed | — | A2.13: software loops |
| 4.12 | — a load consumes under the mask; `EVENTS_PEEK`; a wake consumes nothing | replaced | 7.2 | — |
| 4.12 | — the wait helpers; "keep the release in the mask" (item 79) | kept | 22 "Waiting: what a program keeps" (supplements 7) | software |
| 4.12 | — The `done` gate | replaced | 7.3 (the arrival gate) | A4.3, A4.4; E11 |
| 4.12 | — Timing (event set at end of its cycle, wakes at t + 1) | replaced | 21; `spec/core.md` (Wait for interrupt, Lockstep regions) | B1.6 |
| 4.12 | — At the reset | replaced | 18 | — |
| 4.12 | Uses | kept (rewritten) | 22 "Waiting: what a program keeps" | `EVENTS_REQUIRED` and request-room uses removed |
| 4.13 | `PARAMS`, `STATUS` | replaced | 7.1 | A2.9, A2.10 |
| 4.13 | — "`STATUS` is diagnostic only" | kept | 22 "Waiting: what a program keeps" | not stated by the RTL's specification |
| 4.13 | Errors (`ERRORS` table, kinds) | replaced | 7.4 | A2.12; E23 |
| 4.13 | — sticky; raises the interrupt and the error line | replaced | 7.4, 6.2, 18 | A3.2 |
| 4.13 | — What earns an error | kept | 22 "What earns an error" (supplements 7) | design rule (also `spec/GUIDELINES.md`) |
| 4.13 | — Same-cycle rule and overflows | replaced (overflows) / kept (an error raised in the reset's cycle is lost) | 7.4; 22 "What earns an error" | reset-cycle rule not stated by the RTL's specification |
| 4.13 | Traps are terminal; no retrap; a second trap in the handler; engines carry on | replaced | 6.2, 17.5; `spec/core.md` (Traps) | A3.2: NMI at most once |
| 4.13 | — traps that set no `ERRORS` bit (silent traps) | replaced / kept | 6.2; 22 "Traps the hardware does not report" (supplements 6) | A3.6, A3.7: the fetch fault now sets `access_fault`; misaligned jump cannot occur; `ecall`, `ebreak`, a misaligned load/store and a trap in the handler remain silent |
| 4.13 | The handler: the 24-report set | removed as a fixed protocol; kept as the reference image's convention | 22 "The handler's conventions" (supplements 6) | A4.11: a loaded image's set is its own, numbered from 0 |
| 4.13 | — handler conventions (check `PARAMS.peripherals`, `EVENTS_PEEK`, write every word, exempt from limits, park in a jump) | kept | 22 "The handler's conventions" | software; item 133 closed (6.5) |
| 4.13 | — the boot ROM's two reports | replaced | 6.5 | item 122 closed: `REPORT` op 1, number [25:18] (11.5) |
| 4.13 | — interrupts off until the image enables them | removed | — | A3.2: the NMI is taken from reset on; software keeps `mstatus.MIE` 0 |
| 4.14 | Commit ordering | replaced | 12.3 (`LAND_LATENCY` list), 15 ("Writes before later reads", "No coherence") | — |
| 4.14 | — uncounted writes (commands, overlays) | removed / replaced | 6.5 (the load: uncounted `WRITE`s then a `COUNT`) | A4.1, A4.9 |
| 4.14 | — "there is no flush"; scratchpad data is always sent counted | kept | 22 "Counters: what a program keeps" | not stated by the RTL's specification |
| 4.15 | Digits (shared definition) | replaced | 12.5, 12.1 | A5.1: the digits are `RECEIVE_CONFIG`'s |
| 4.16 | The fifth router port | replaced | 2.1, 16.1–16.4 | A1.1, A1.4–A1.7, B2.4; E17, E18 |
| 4.16 | — cost | kept | 24 (D4) | figure moves: attach tiles 0, 7, 15 |
| 4.16 | — the memory system's traffic; pacing is policy | kept | 22 "Pacing the special tiles' traffic" (supplements 16) | measurement `[meas: v1.35]` at tile 0; attach tiles changed (A1.1) |
| 4.17 | Peripherals (intro, the parts table) | replaced | 17, 17.1 | A8.1 |
| 4.17.1 | The peripherals and their types | replaced | 17.1, 2.1 | — |
| 4.17.2 | Run: `PULSE_RUN` and `STATUS` [0] | replaced | 17.2 | `START`, `STATUS.busy` |
| 4.17.3 | Events: counted and live | replaced | 7.2, 17.2 | — |
| 4.17.3 | — software rule: act on every bit a load returns, or mask only what is serviced | kept | 22 "Peripherals: what a program keeps" (supplements 17) | — |
| 4.17.3 | — Items (deferred, item 119) | kept as open | 26 item 119 (D4) | the RW engine's `EVENTS.done` returns a write buffer part (17.7): informs |
| 4.17.4 | The standard window | replaced | 5.3, 5.5, 17.1 | A8.1, A8.2 |
| 4.17.4 | — words 32–35 of the running peripherals | replaced | 17.8.1, 17.9.1 (jolt files) | forks B, C |
| 4.17.4 | — access rules (reserved words read 0) | replaced | 5.3 | A2.5: an undefined word is an access fault |
| 4.17.4 | — a peripheral without a DMA | replaced | 17.1 | block 1 reads 0; a store is UB |
| 4.17.4 | — the C header | kept | 22 "Programs and the toolchain" | software; to be regenerated |
| 4.17.5 | Writes into a peripheral's region | replaced | 17.3, 12.3 | A8.4, A8.5 |
| 4.17.6 | Errors | replaced | 17.2, 7.4 | — |
| 4.17.7 | (removed) | removed (stub kept in the table only) | — | removed in v1.2 |
| 4.17.8 | What every peripheral must do | replaced | 17.5 | — |
| 4.17.9 | Peripheral output: the DMA | replaced | 17.4, 16.4 | A1.6, A1.7, A8.2, A8.3, B4.2, B4.3; E15 |
| 4.18 | Request port | replaced | 11.1–11.4 | A7.1–A7.7 |
| 4.18 | — "the depth is for debug messages"; no acknowledgement for issuing | kept | 22 "Requests: what a program keeps" (supplements 11) | depth now 4: a burst of reports stalls the core (A7.2) |
| 4.19 | The replayer (stub) | replaced | 17.8 stub; `spec/jolt/replayer.md` | |
| 4.20 | UART | replaced | 17.6 | A8.7; E (reference oscillator) |
| 4.20 | — debug output from other tiles goes to log files | kept (deferred) | 23 (D3) | file system deferred |
| 4.20 | — cost | kept | 24 (D4) | — |
| 4.21 | The Tracer (stub) | replaced | 17.9 stub; `spec/jolt/tracer.md` | |
| 5 | Timing summary | kept (rebuilt) | 21 | B1–B4 |

### 8.3 Sections 6 to 8

| v1.35 section | its title | replaced / kept / removed | where in v1.36 | why: inventory item or note |
|---|---|---|---|---|
| 6 (intro) | Control | replaced | 2.1, 7.3, 17.6 | the master tile sequences by arrivals and releases; "writes commands" removed (A4.1) |
| 6.1 | Reset, errors and the toast pin | replaced; part kept | 6.2 (NMI, traps terminal), 7.4, 17.6 (error lines, watchdog, toast pin, `LED`), 18 (reset); kept in 22 "An error, end to end, and the reset" | kept: the end-to-end story, the master's handler duties, hard/soft reset as *(Open)* item 124, "no hold". Removed inside it: one-at-a-time bring-up (A4.7), the 24-report set (A4.11), `mtime` (A3.4), the reset of `EVENTS_REQUIRED`/"command landed" (A2.13, A4.1) |
| 6.2 | Boot | replaced; part kept; part removed | 6.5 (the boot ROM, `LOAD`); kept in 22 "The load, as software sees it" | A4.8. Removed: the ROM's command test and guard (A4.7), boot from the host and C&C 1 loading C&C 2/3 with "started" requests (A4.10, D4), boot part 2 enabling interrupts (A3.2, A3.3), `stage_delay` staging (A10.11, D5), staged images and overlays (A4.9). Kept: 128-bit image packets and padding, one kind, counted data only after arming, item 125 |
| 6.3 | `done`, READY, GO and `WFI` | replaced; part kept; part removed | 7.2 (wake), 7.3 (barrier), 17.6 (master's side), 6.4 (lockstep), `spec/core.md` (WFI, lockstep regions); kept in 22 "The phase, as software sees it" | A4.2–A4.6. Removed: "command landed", the command convention, the command word, parity argument blocks (A4.1, D5), `EVENTS_REQUIRED` in the wake (A2.13). Kept: the three-instruction barrier, the one-barrier and configure/start phases, the cost |
| 6.4 | Phases that receive | kept | 22 "The phase, as software sees it" | software rule (invariant 4); v1.35's 12–13 / 20–21 figures marked [meas: v1.35], to be re-measured; start skew removed (A1.11) |
| 6.5 | Block copy into controller memory (overlays, tables) | kept, less its overlays | 22 "Block copy into DMEM or IMEM" | the stream-to-self recipe stands in the new names; overlays from C&C 1 removed (A4.9) |
| 6.6 | The controllers and their registers | replaced; part kept | 2.1, 2.2 (the special tiles are tiles), 17.6 (Ingress/Egress's words, bits, outputs), 17.9 (the Tracer), 16.2/16.4; kept in 22 "The master tile's sequencing" | Removed: 2 MB scratchpads (A1.2), `go_latency` + 1 wait and commands crossing a phase (A4.1), "started" requests (A4.10), one-at-a-time bring-up (A4.7), the simulator's behavioural C&C models (simulator's own docs). Kept: the phase loop, the watchdog convention, pacing |
| 6.7 | The memory system's contract | kept, corrected; parts replaced | chapter 23 (23.1–23.4, 23.7–23.9); 11.4 (answers), 11.5 (`READ`, `WRITE`), 17.7 (RW engine, write buffer), 3 (`lpddr`, `fill`) | A7.4–A7.6, A8.9–A8.12, A1.6, E16, E24 |
| 7 | Memory subsystem | kept, as deferred | chapter 23 (23.5, 23.6) | file system, Updater, Binder, translator, shared cache: deferred/open as in v1.35 (D1, D2); the 6/80 model kept as the simulator's |
| 8 (intro) | Registers | replaced | 5 | |
| 8.1 | Register map and general rules | replaced; part kept | 5.1–5.5, 6.4 (latencies), 18 (reset values); kept in 22 "Register conventions and the C header" | A2.1, A2.2, A2.5, A2.7, A2.14, B1.1. Kept: configuration registers static in a phase, start only, the C header. Removed: reserved words reading 0 |
| 8.2 | `PARAMS` and `STATUS` | replaced | 7.1 | A2.9, A2.10 |
| 8.3 | `ERRORS` | replaced | 7.4 | A2.12 |
| 8.4 | The send port: `ROUTER_*` | replaced | 9 | A5.11, renames |
| 8.5 | `DMA_CONFIG` | replaced | 8.1 (transfer, `gap`), 12.1 (`RECEIVE_CONFIG`), 13 (`GATHER_CONFIG`) | A5.1, A5.2, A5.8 |
| 8.6 | The pulse registers | replaced | 1.2 (actions), 7.3 (`ARRIVE`), 8.2 (`DMA_START`), 17.2 (`START`), 17.6 (`RELEASE`) | renames |
| 8.7 | The write counters and `DMA_RECORD_BASE_ADDRESS` | replaced | 12.2 (`COUNTER_n`), 13 (`GATHER_CONFIG.record_base`) | A5.1, A6.11 |
| 8.8 | The read port | replaced | 10 | A5.10 |
| 8.9 | The table window | replaced | 5.2, 8.3, 8.4, 8.6, 12.5 | A2.2, A2.3, A2.4; closes item 120 |
| 8.10 | The events and the request port | replaced; `EVENTS_REQUIRED` removed | 7.2 (events, wake), 11.1 (request unit) | A2.11, A2.13, A7.1, A7.2 |
| 8.11 | The peripheral windows: the standard words | replaced | 5.5, 17.1 | A8.1, A8.2 |
| 8.12 | (removed) | removed (already a stub in v1.35) | — | v1.1's sender registers |
| 8.13 | The UART | replaced | 17.6 | A8.7 |
| 8.14 | C&C 1's peripheral: Ingress/Egress and the UART | replaced | 17.6 | A8.6, A8.8 |
| 8.15 | Simulator-only words | removed | — (noted in 22 "Register conventions") | A2.6: block 7 is unmapped and an undefined word faults (A2.5); a tile's result is a request |
| 8.16 | The Tracer's window (stub) | replaced | `spec/jolt/tracer.md` 17.9.1 | |

### 8.4 Sections 9 to 14, the invariants and the open items

| v1.35 section | its title | replaced / kept / removed | where in v1.36 | why: inventory item or note |
|---|---|---|---|---|
| 9 | Invariants (checked by the simulator) | kept | 20 | invariant numbers unchanged; rewritten in v1.36's terms; 27–34 new |
| 9, invariant 1 | No FIFO overflow | kept, corrected | 20, invariant 1 | write-engine input FIFO and `PTR`/`HIST` request FIFOs gone (A6.3, A6.4); attach-port queues only on attach tiles (A1.1, A1.4); `ERRORS` names (A2.12) |
| 9, invariant 2 | At most one access per area per cycle | kept, corrected | 20, invariant 2 | the hardware keeps it itself: a write wins its bank (A6.1); the software half (no buffer read and written in one transfer) stays |
| 9, invariant 3 | The timetable holds | kept | 20, invariant 3 | terms |
| 9, invariant 4 | Every expected arrival lands first | kept, corrected | 20, invariant 4 | the command-write exception removed (A4.1) |
| 9, invariant 5 | No bad packets | kept | 20, invariant 5 | the bad-packet list is 4.2's (item 142) |
| 9, invariant 6 | No `HIST` count exceeds 17 bits | kept | 20, invariant 6 | |
| 9, invariant 7 | (withdrawn) | kept as a stub | 20, invariant 7 | |
| 9, invariant 8 | Overwritten-unread results counted; read queue never full | first half removed, second kept | 20, invariant 8 | no reply block (A4.1); `strict_replies` gone |
| 9, invariant 9 | No unexpected arrival | kept | 20, invariant 9 | renamed counter underflow (F) |
| 9, invariant 10 | Gathers are local | kept | 20, invariant 10 | |
| 9, invariant 11 | Controller-memory writes keep to their places | kept, reduced | 20, invariant 11 | command and reply blocks gone (A4.1); what is left is "a reply lands where its request named" |
| 9, invariants 12, 13 | (withdrawn) | kept as stubs | 20 | |
| 9, invariant 14 | Read-port results and incoming traffic do not overlap | kept, flagged | 20, invariant 14 | not stated by the RTL's specification (item 116); the command-write exception removed |
| 9, invariant 15 | No errors | kept | 20, invariant 15 | |
| 9, invariant 16 | The peripherals' and the replayer's invariants | kept, extended | 20, invariant 16 | block 1 of a DMA-less peripheral, queue capacity (17.1, 17.4) |
| 9, invariant 17 | The request contract is kept | kept, corrected | 20, invariant 17 | targets 0–2 (A7.4); tag windows → write-buffer parts (A8.10); stream `dst` ids (A1.8, A1.10); `op` (A7.5) |
| 9, invariant 18 | One source per order-placed destination | kept | 20, invariant 18 | 14.4 |
| 9, invariant 19 | Everything stays in bounds | kept | 20, invariant 19 | the machine's rule stays UB (5.1); the core's own decode is `spec/core.md`'s (A2.15) |
| 9, invariant 20 | Pointers and runs stay aligned | kept, corrected | 20, invariant 20 | `record_base` aligned to a row whatever *k* (13); the core's misaligned access traps (A3.5) |
| 9, invariant 21 | No reserved encodings | kept, corrected | 20, invariant 21 | an undefined word is an access fault (A2.5); the simulator-only words are gone (A2.6); digits in `RECEIVE_CONFIG` (A5.1) |
| 9, invariant 22 | Uncounted traffic is mode 0 only | kept, corrected | 20, invariant 22 | commands and overlays gone; the only uncounted `WRITE`s are a load's (A4.8); first half flagged "not stated by the RTL" |
| 9, invariant 23 | No silent traps | kept, rewritten | 20, invariant 23 | every trap reports through the handler; fetch fault sets `ERRORS.access_fault` (A3.7); no misaligned jump target (A3.6); misaligned load/store traps (A3.5) |
| 9, invariant 24 | K covers the write path | kept | 20, invariant 24 | `LAND_LATENCY` (item 128) |
| 9, invariant 25 | Boot keeps its order | kept, rewritten | 20, invariant 25 | the load's rules (6.5) replace overlays, "command landed" and `stage_delay` (A4.1, A4.8, A4.9) |
| 9, invariant 26 | No ignored start | kept | 20, invariant 26 | |
| (new) | invariants 27–34 | new | 20 | UB rules of 7.3, 8.6, 12.5, 13, 6.3, 12.2, 6.2/6.5, 17.6/17.7 |
| 10 | Parameter summary | replaced, and mapped | 19, 19.1 | 19 is the contract's table; 19.1 maps every v1.35 row and lists the removed and the simulator's |
| 11 | Logic budget (per tile, 12 nm estimates) | kept | 24 | lines for removed hardware dropped or marked; mm² are v1.35's estimates, to be redone |
| 12 | Physical design: the control island | kept | 25 | terms; receive queues in the receiving tile (A7.3) |
| 13 | Open items | kept | 26 | item numbers unchanged; 19 items closed by the RTL's hardware; 160–173 new |
| 13, items 2, 3, 4, 5, 6, 63, 67, 82, 83, 84, 117, 120, 121, 122, 126, 129, 133, 142 | | closed in v1.36 | 26, "Closed in v1.36" | inventory C |
| 13, item 118 | DMA src and port | deferred half closed, decided half reversed | 26, item 160 | A1.6, A1.7 |
| 13, items 1, 7, 8, 28, 48, 60, 61, 64, 107, 119, 124, 125, 128, 130 | | kept, status "informed" or "needs a decision" | 26 | inventory C |
| 13, items 10, 13, 15, 17–22, 24, 27, 116, 159 | | kept unchanged but for terms | 26 | |
| 14 | Context: the sort these units serve | kept | 27 | terms; E25 fixed; phases without commands; the RTL's end-to-end figures added |
| `spec/schedule.md` | the all-to-all's schedule | kept, edited | `spec/schedule.md` | names, directions, section numbers; `GROUP` word layout (A5.3) |
| `spec/GUIDELINES.md` | the rules | kept, edited | `spec/GUIDELINES.md` | names and section numbers; three rules whose hardware changed (tiles' differences, alignment, registers) |

### 8.5 The replayer: 4.19, its window, its invariant, its parameters and its open items

| v1.35 section | its title | disposition | where in v1.36 | why: inventory item or note |
|---|---|---|---|---|
| jolt/replayer.md header | "Part of the machine specification … moved here whole" | replaced | `spec/jolt/replayer.md` header | new numbering (17.8) |
| 4.19 | The replayer (peripheral type 4): intro | replaced | 17.8 (intro, `PARAMS`, the parts table) | RTL's 17.8; the "no figures yet" note is removed (no figure set in v1.36's spec) |
| 4.19 | Ground truth | kept | 17.8.15 (summary in 17.8 "The reference") | not in the RTL's spec beyond one bullet |
| 4.19.1 | Overview: what a pass is | replaced | 17.8, 17.8.2, 17.8.10 | "46 passes per Akita proof" kept in 17.8.16 |
| 4.19.1 | Overview: the parts | replaced | 17.8 parts table | A9.8: code LRU table 16 × 32 |
| 4.19.1 | Overview: its interfaces | replaced | 17.8.1, 17.8.9, 17.2 | A2.11, A8.1 (window layout), A9.1 |
| 4.19.1 | Overview: rate | kept | 17.8.16 | [meas: v1.25]; RTL figures added (B5) |
| 4.19.1 | Overview: the reference, status | kept | 17.8.15 | |
| 4.19.2 | The run log: the three streams, which rows take a log word | replaced | 17.8.3 | sizes per tile and densities kept in 17.8.16 |
| 4.19.2 | The densities at v1.25/v1.26 | kept | 17.8.16 | [meas], to re-measure |
| 4.19.2 | A requirement on our tracer engine | kept | 17.8.17 | producer's side |
| 4.19.2 | The checkpoint: state word, stream word, register image; the state record | replaced | 17.8.2 | "checkpoint"/"state record" → record; state word [127:112] one reserved field |
| 4.19.2 | The advice field; stream bounds; "the replayer needs only words 0–65"; "what the record is"; the descriptor | kept | 17.8.17 | rationale, not in the RTL's spec |
| 4.19.2 | Use the store log; output `rd_prev`; nothing in the record changes per pass; the working state and re-runs | replaced | 17.8.1, 17.8.2, 17.8.3, 17.8.11, 17.8.13 | version history (v1.25/v1.26 forms) dropped: it is in `spec/history/` |
| 4.19.2 | Mid-expansion start; inline scratch; end of trace | kept | 17.8.17 (end of trace also 17.8.3) | |
| 4.19.3 | The expansion word: the field table, operand codes, slot kind, row's immediate, derived fields | replaced | 17.8.4 | A9.9: reserved bit set, `from_log` with `next_from_value`, `offset_imm` off ShiftData are UB |
| 4.19.3 | `virt`, `shift_offset`, `sign_imm`, two immediate slots, "Jolt's conventions are data" | kept | 17.8.18 | Jolt's rules behind the fields; items 35, 56 |
| 4.19.4 | The rule (offset, remap, NOP, first word delivered, mid-sequence start) | replaced | 17.8.5 | A9.5 |
| 4.19.4 | What Jolt does; the example; which instructions remap; a16z's D13/D14; what the remap produces; temporaries in 40–47; every expansion word | kept | 17.8.18 | items 36, 113, 158 |
| 4.19.5 | Every instruction is a template; the index; the entry; the padding word | replaced | 17.8.6 | |
| 4.19.5 | The encodings separated; the tables are universal; the table builder's rules | kept | 17.8.19 | software contract |
| 4.19.6 | The ROM; how a template is read | replaced | 17.8.6 | extra difference: a word at ROM word 511 with `seq_remaining` > 0 runs, the next row is the invalid one |
| 4.19.6 | The fill at `821e1bd`; the CSR gap | kept | 17.8.19 | |
| 4.19.6 | Loading (broadcast by C&C 1 at boot) | kept, rewritten | 17.8.19 "Loading" | A4.8: the boot is a pull; delivery is software's (17.8.13) |
| 4.19.7 | Per row; the register file; the schedules; pc and `instr_idx` rules; stalls | replaced | 17.8.7 | A9.4, A9.7; B5 |
| 4.19.7 | "What a row computes"; PEXT; the memory rows; the classes' opcode lists; advice rows; the multiplier; edge cases; zero shift masks; a load row's memory fields; hardwired Jolt behaviour | kept | 17.8.20 | item 140 closed by the RTL; E22 (three/four conventions): 17.8.7 says four |
| 4.19.7 | Rate (multiply density) | kept | 17.8.16 | [meas: v1.25] |
| 4.19.8 | The queues; the ports; credits; overrun; the state write | replaced | 17.8.8, 17.8.9 | A8.4: a dropped push is still counted; A9.7; credit count restarts stated (E8) |
| 4.19.8 | The measured waits, 4 KiB against 8 KiB | kept | 17.8.16 | item 143; [meas: Quest 098–101] |
| 4.19.8 | "A credit is permission to send up to half"; two refills outstanding | kept | 17.8.21 | software |
| 4.19.8 | v1.1's low-water condition, items 50, 51 | removed | — | history only; the simulator's `pend`/`low_water` remnants go (E8) |
| 4.19.9 | The window table, the events, `RUN_CONFIG`, what the controller reads | replaced | 17.8.1 | A9.1, A9.2 (`EXPANSION_OFFSET` latched), A8.1 |
| 4.19.9 | The controller loop; between passes; short runs | kept, rewritten | 17.8.21 | A2.13: `EVENTS_REQUIRED` gone; new register and event names |
| 4.19.9 | Controller-fed templates and their numbers (the key); EBREAK's expansion; controller-fed expansions and the broadcast | kept | 17.8.19 | E27, item 115: the broadcast is decided, not built; its size-first command went with the commands (A4.1) |
| 4.19.10 | The map; one part per queue; a bad write is UB; landing; counting; ownership; `RUN_CONFIG` | replaced | 17.8.9, 17.3, 12.3, 17.8.1 | A8.5 (`PERIPHERAL_LATENCY` a bound); A8.4; A9.9 (ROM word read in the write's cycle) |
| 4.19.10 | How a controller loads part 0 | kept | 17.8.21 | new register names |
| 4.19.10 | Implementation note: the decode table's organization | kept | 17.8.23 | not contract |
| 4.19.11 | `PULSE_RUN`: a pass; `STATUS` [0]; done; after a trap; the errors | replaced | 17.8.10, 17.8.1, 17.8.12 | A9.1, A9.3, A9.6, A9.9; E22: "the controller reads the queue occupancies" is dropped, no word exposes them |
| 4.19.12 | The output row; the output queue | replaced | 17.8.11, 3 (`replay_row`) | A8.12; item 59 |
| 4.19.13 | The resident replay region (the software contract) | kept | 17.8.22 | the overlay sentence is rewritten (A4.9); fetches are `READ` requests (11.5) |
| 4.19.14 | Cost | kept | 17.8.23 | decoder tables 320 B (A9.8) |
| 4.19.15 | Stream decompression: bit order, raw, coded, the tables, the last-load entry, the sizes and checks | replaced | 17.8.12 | A9.3, A9.6; "decompressor" → decoder |
| 4.19.15 | The last-load entry's reason; the encoder rule; the cost | kept | 17.8.17, 17.8.23 | the encoder is the Tracer's (17.9.6) |
| 8.11 (replayer's part) | The replayer's window | replaced | 17.8.1, 17.1 | A8.1, A9.1; the C struct note is the firmware header's |
| section 9, invariant 16 (replayer's part) | the replayer's invariants | kept, rewritten | 17.8.24 | error bits by name; new UB rules (A9.9) added |
| section 10 (replayer's row) | parameter summary | kept, rewritten | 17.8.24 (and 19: `REPLAY_QUEUE_ENTRIES`, `REPLAY_OUTPUT_ROWS`) | A9.8 |
| Open items | items 137, 59, 113, 140, 35, 38, 39, 42, 56, 57, 36, 115, 123, 158, 139 | kept, updated | "Open items" of `spec/jolt/replayer.md` | 140 and 123 closed; 35, 36, 37, 38, 39, 42, 56, 57 "built as proposed in the RTL"; 115 decided, not built |

Counts (rows above): replaced 20, kept 24 (5 of them rewritten), removed 1. By numbered subsection (4.19, 4.19.1–4.19.15, 8.11, inv. 16, section 10, open items = 20): 14 are split between "replaced" (their contract) and "kept" (rationale, measurements, software); 4.19.11, 4.19.12 and 8.11 are replaced whole; 4.19.13, 4.19.14 and the three tail blocks are kept whole.

### 8.6 The Tracer: 4.21, 8.16, its invariant, its parameters and its open items

| v1.35 section | its title | disposition | where in v1.36 | why: inventory item or note |
|---|---|---|---|---|
| 4.21 (intro) | The Tracer (peripheral type 3) | replaced | 17.9 (`spec/jolt/tracer.md`) | the RTL's text; the decisions' citations (§n, B1 Qn) stay in history |
| 4.21.1 | Overview | kept | 17.9.13 (parts table replaced by 17.9's) | what it shares with the replayer, the segment in a proof, the budget; rewritten |
| 4.21.2 | Its region and the state record | replaced | 17.9.2 | parts 1 and 3 now simply unmapped; reasons kept in 17.9.16 |
| 4.21.3 | Control: the window, the pulses and the queues | replaced; the controller's loops kept | 17.9.1, 17.9.3; loop in 17.9.15 | A10.1 (bits packed), A10.5 (`EXPANSION_OFFSET` latched), A10.10 (`START` with no state write UB); loop rewritten without commands or `stage_delay` |
| 4.21.4 | Execution | replaced | 17.9.4; reasons in 17.9.16 | A10.6 (key, no probe counted, 0xFFFF_FFFF UB) |
| 4.21.5 | Memory | replaced; the simulator's model kept | 17.9.5; 17.9.14, 17.9.16 | A10.2, A10.3, A10.4; the memory system stays deferred (item 119) |
| 4.21.6 | Output: the FIFOs, the compressors, the queues and the drain | replaced | 17.9.6; reasons and the drain's bound in 17.9.16, 17.9.17 | A10.8 (each log stream ends on its own last value); "where the output lands… later" kept as deferred in 17.9.13 |
| 4.21.7 | Boundaries | replaced | 17.9.3, 17.9.11; a sentence in 17.9.13 | between runs / between segments / at a segment's end are 17.9.3's and 17.9.11's rules |
| 4.21.8 | Errors | replaced | 17.9.1, 17.9.9 | A10.1 (positions), A10.2 (`write_back_error`), A10.9 (no counted event after a stop) |
| 4.21.9 | Timing and cost | replaced (contract) and kept (model, figures) | 17.9.7, 17.9.8; 17.9.14, 17.9.17, 17.9.18 | B6; `tr_row_cycles`, `tr_mul_cycles`, `tr_probe_compare` become contract; `ms_line_*` stay the simulator's model |
| 4.21.10 | The segment on the chip: barriers and deliveries (the software contract) | replaced; `stage_delay` staging and the `COUNT` "armed" packet removed | 17.9.10; sequence in 17.9.15 | A10.11 (requests `ARMED`, `STREAMS_ARMED`; three releases): reverses an owner ruling |
| 8.16 | The Tracer's window (C&C 3's peripheral) | replaced | 17.9.1 (and 17.1 for the control block and block 1) | A8.1, A8.2, A10.1; words 32–51 keep their numbers |
| section 9, invariant 16 (the Tracer's part) | — | kept | 17.9.18 (for chapter 20) | rewritten; adds the new UB rules; `ERRORS` [0]–[9] |
| section 10 (the Tracer's row) | — | kept, mapped | 17.9.18 (for 19.1) | sizes are 17.9's table and 19 |
| Open items (153, 147, 148) | — | kept, updated | `spec/jolt/tracer.md` Open items | 153 part closed, 157 closed, 144 reversed, four new |

Counts (15 rows): replaced 9 (three of them with a kept part), kept 4, replaced-and-kept 1 (4.21.9), removed 0 whole sections; removed inside sections: `stage_delay` staging, the "armed" `COUNT` packet, the reserved gaps in `EVENTS`/`ERRORS`.
