# What is open in v1.36

*For whoever takes v1.36 further, and for anyone who needs to know how far its simulator can be trusted: where the
simulator still differs from the RTL, what of the RTL's tests has no counterpart here, what the simulator takes from
the RTL's sources where the specification is silent, what was retired, and which firmware is not built here yet.* This is the record at the freeze. What is
open in the design itself is `spec/machine-spec.md` chapter 26; what v1.35 had that v1.36 does not is `README.md`
("What v1.36 does not carry over").

The simulator models the RTL (Snarky One) at the register, the packet and the cycle. Every unit was brought to the
specification and to the RTL's block, and the chip runs the RTL's firmware in the RTL chip's cycles (`README.md`,
Status). A path below that begins `rtl/` is Snarky One's.

## (a) Where the simulator still differs from the RTL

| what | the RTL | the simulator | why it stays |
|---|---|---|---|
| an error line's way to the master tile | the error lines are wires: a tile's line reaches `ERROR_LINES`, `ERRORS.tile_error` and the master's NMI in the cycle it rises (7.3, 18; `rtl/chip`, `chip.veryl`) | a compute tile's, the memory tile's or the tracer tile's error line reaches them a cycle later: its level in cycle *t* is known at the tile control unit's step of *t* and crosses a link of 1 cycle. The master tile's own line, to the toast pin, is exact | the framework's limit: a wire cannot cross thread domains, and a wire only where both ends share a domain would make a run's cycles depend on how it is split over threads (`src/README.md`: one result whatever the thread count) |
| the tracer tile's memory system | *Open* behind the `fill` port (17.9.14); the RTL's chip tests place a model of it at the chip's boundary | the chip's tests place the same model on the Tracer's fill port (`Tracer::FillPort`; `tests/segment_bench.h`: the RTL test's `Fill`, draw for draw). The simulator's own `MemSystem` over a `FileStore` serves the block-level tests: one access at a time, a request taken while another is served starting when that one ends (item 174). A run with neither stops the simulator | the memory system's design (items 60, 119) |
| the coprocessor port | `rv32_core` has one; the tile ties it off (there is no accelerator) | custom-0 and custom-1 are illegal instructions, as on a core with `coprocessor_present` 0 | the field accelerator, when the RTL has one |
| a core port that holds ready low, or a fetch answered later than the next cycle | the RTL's core supports both, and its test runs each program a second time with randomly delayed ports | the model has neither: no memory of the tile does either (IMEM, DMEM, the boot ROM and `register_bus` accept in every cycle) | a unit that needs it |
| the row port's consumer | `replay_row` has a ready; the chip ties it to 1 | the replayer's sink takes a row every `sink_interval` cycles (1 on the chip); the chip's tests read every row out of it | the materializer, *Open* (17.8.11) |
| undefined behaviour the simulator resolves its own way | a store to a `STREAM` or `GROUP` entry the running transfer reads: the RTL's read takes the entry `TABLE_LATENCY` cycles after its issue (8.6). A core load of `PTR` or `HIST` in a cycle its engine reads it: undefined (12.5) | the read takes the entry at its issue (`src/units/ag.h`); the load **stops the simulator**, naming the tile and the table | undefined: a correct program does neither |
| checks chapter 20 promises of the simulator, which it does not make | (the hardware checks none of them) | no unit records: a stray counted packet at its acceptance (invariant 9: only the hardware's `ERRORS.counter_underflow`, at the decrement, is reported); an uncounted `APPEND`, `BUCKET` or `AUGMENT`, an uncounted write into the scratchpad, a write into DMEM's first 16 bytes (22); the reference firmware's reply-block convention (8, 11: `strict_replies`); a 1 stored in a reserved bit (21); a load's order (25); a `GATHER_CONFIG.record_base` that is not aligned (20). v1.35's checker made these on v1.35's director and register block; v1.36's units were written from the specification without them. Chapter 20's own list of exceptions (item 159) stands beside these | a pass over chapter 20, invariant by invariant, with a case of `test_checker` for each check added |

Nothing else is known to differ. The router, the peripheral's DMA under a router input that is not ready,
`ERRORS.drift`, the gather queue and the canonical wait, which earlier stages listed here, are the RTL's now and are
compared with it (`tests/routerconf/README.md`, `test_periph_slot`, `test_ag`, `test_gather`, `test_core_programs`).

## (b) What of the RTL's tests has no counterpart

| the RTL's test | what it checks that nothing here does | what would close it |
|---|---|---|
| `base_tile_special_test.rs` and its `special` program (kind 4; built here, `fw/images/tile/special.elf`, not run) | a special tile's base with every kind of peripheral interface: the test plays peripherals 1, 2 and 3 as bare `reg` interfaces that answer every word, the control and DMA blocks' among them, with constants. In the simulator a peripheral's control block and DMA are the slot's own (`src/units/periph_slot.h`), so no peripheral can answer those loads with the test's constants. What the program checks of the tile is checked piece by piece: the straps and `boot_local` (`test_rtl_sort`, `test_rtl_memory`, `test_rtl_stream`), the peripherals' windows through the bus with the stores that stall (`test_core_tile`, `test_periph_slot`, `test_ingress_egress`, `test_rw_engine`), the write port, a DMA's packets through the special tile's router (`test_special_router`, `test_rtl_memory`), a request received with its credit (`test_request_tree`) | a slot whose blocks 0 and 1 a test can replace by a bare interface |
| `memory_tile_test.rs` (the memory tile alone, a program of the test's) | a read under a stalled attach output, region `WRITE`s of 64 and 32 bits through the tile, an LPDDR error to `ERRORS.peripheral_1`, the NMI, the reports 24 and 25 and the error line, on one tile. Here the tile's composition is checked in the chip (`test_rtl_memory`) and those three at the unit (`test_rw_engine`) | a one-tile bench for a special tile (the same as above) |
| `master_tile_test.rs`, parts 0–5 and 8 | the master tile alone. Its parts are the tile's and the nets' (`test_rtl_tile`, `test_request_tree`, `test_ingress_egress`, `test_rtl_sort`'s errors); parts 6 and 7 (the UART and the stream through the core) are `test_rtl_stream`'s | the same bench |
| `chip_replay_test.rs`'s six smaller ranges (EVM Zone's windows 33–36 and 86–89, Cyclone's 30–33 and 40–42 among them) | a segment that starts inside a host segment: the RTL's test starts at any window by running its model of the Tracer's execution from row 0. The simulator's tests have no such model and take none from the simulator, so `tests/segment_real.h` starts at a host segment's start, which the recorded files give | a port of the RTL's `tracer_execution_model.rs` to the tests, or the RTL test's fixtures for those ranges |
| `program_test.rs`'s second pass (randomly delayed ports); the coprocessor's programs | (a)'s rows | (a)'s |
| the handshake monitors (`verif::Handshake`) on the request link's beats, the row port and the fill port | the simulator's request unit hands over whole requests, and the framework's ports have no ready or valid to break; the request tree's beats are modelled in `test_request_tree`, against a beat-by-beat model from the RTL's READMEs | nothing in the simulator's ports |
| the RTL's models with all three attach ports at every tile id | a simulated tile has the attach port of its place in the chip (tiles 0, 7, 15) | nothing: the chip's attach ports are checked at chip level |
| the RTL test's reports (`print_trace`, `print_replay`, `print_instructions`, the stalls by kind) | reports, not checks: the figures they are made from are compared | a port of the reports, if they are wanted |
| a conformance of the peripheral slot under random interleavings (v1.35 had one against v1.3's simulator, `test_slotconf`, deleted: its scripts and observations were of v1.35's control block and replayer) | scripts of random stores and loads on the control and DMA blocks replayed on the RTL's `peripheral_control` and `peripheral_dma`. Every rule of 17.1–17.4 is tested from the specification and the RTL's headers (`test_periph_slot`, `test_ingress_egress`), but not: a sticky error through the slot, the DMA waiting on an empty queue, a store to `DMA_DEST_ADDR` during a transfer (UB: the slot flags it) | scripts replayed on the RTL's blocks in a Snarky One scratch worktree, as `tests/replayer/README.md` and `tests/routerconf/README.md` do for their units |
| the core's rules one by one | 62 directed tests of the core from its contract's sentences were written in the core stage against an interface that is gone, and are not in the tree. What the RTL's programs do not reach and nothing here checks: a CSR write to `mcycle` or `minstret`, `minstret` on a trapped instruction, the store-misaligned cause, a data target slower than one cycle on a bus | directed programs for those four, run on the RTL for their reference, as `tests/core-programs/README.md` describes for the interrupt programs |
| a fingerprint of every unit's state along a chip run, and a ThreadSanitizer run of the segment | v1.35's T2 had both against v1.3. v1.36's chip runs are compared at the master's summary, the reports, every fill request and every row's cycle; the sort split over 2 and 8 threads is compared at every poll and runs under TSan (`make tsan-chip`), the segment split over threads without TSan | a reference from the RTL for unit state along a run, if one is wanted |

The messages of the checker (`tests/checker.cpp`) are v1.3's words, with v1.35's section numbers in them ("3.1");
`test_checker` compares them as they are.

## (c) What the simulator takes from the RTL where the specification is silent

The specification states what the hardware does observably and leaves cycles to the implementation in many places.
The simulator runs in the RTL's cycles, so each such cycle is the RTL's, taken from its sources or its blocks'
READMEs. They are listed here so that a reader of the specification knows what the simulator adds to it; the guide's
section 9 (`spec/CHANGES-v1.35-to-v1.36.md`) lists the same decisions and the corrections the specification took
from them. Where the simulator stages found the specification wrong, it is corrected (the guide's 9.1), and the note
says so.

### The replayer

The model's timing rules beyond 17.8.7 (a stall's cost, the first row's start, the entries' arrival) are the RTL's
`replayer_unit`'s, listed in `src/units/replayer.h`'s header ("WHAT IS THE RTL'S, NOT THE SPECIFICATION'S") and
compared with the RTL every cycle (`tests/replayer/README.md`).

### The Tracer

Notes 1, 2, 3 and 11 were differences from the specification's text; 17.9.3, 17.9.9, 17.9.17 and 17.9.2 now say what
the RTL does.


1. **The rows' end** is the cycle after the last row completes: the control sees `row_done` through a register
   (`tracer_unit.veryl`); 17.9.3 says "the cycle in which the run's last row completes".
2. **After a stopping error** the scans and a transfer in progress go on; only the events, the rows' end and the run's end
   are withheld (`tracer_control.veryl`, `tracer_run.veryl`); 17.9.9 says "drains nothing more".
3. **A NOP policy** costs 2 cycles before its row (`replayer_fetch.veryl`: the policy's read, then ROM word 1); the RTL's
   Tracer README and inventory B6 say 1.
4. **A core-fed instruction** costs a cycle before its first row; its successor's fetch follows its last word's pop, which
   for an expansion of several words comes rows ahead and costs nothing.
5. **A redirect to pc 0, and a run's first padding row**: the padding word is fetched from nowhere, a cycle sooner than a
   fetch's hit (`tracer_instruction.veryl`'s `issue_padding`): 4 past the schedule, and 5 after START's effect.
6. **A run's first row inside an expansion** starts 3 cycles after START's effect (the fetch holds its words).
7. **An instruction miss** costs its fill's latency + 3, **a data miss** + 5 (+ the wait for the write-back buffer's
   place), **a probe** of n lines n × (the map's latency + 4) + 2 (`tracer_fetch.veryl`, `tracer_data.veryl`,
   `tracer_fill.veryl`, `tracer_memory.veryl`, `tracer_jump_lookup.veryl`).
8. **The write-back register**: a row's rd lands in the register file a cycle late, on the port the next row's rs2 read
   uses, so a row whose word arrives exactly the cycle after its predecessor's completion starts a cycle later
   (`tracer_rows.veryl`, `write_back_pending`).
9. **`state_ready`** rises 3 cycles after the last row completes (the registered `row_done`, the output's input register,
   the choice); **done** comes 2 cycles after the DMA's last pop (`peripheral_dma.veryl`'s busy).
10. **A fetch's read waits** while a data access or a map read is presented or in progress, and the fill port's one read
    goes to the data access, then the map, then the fetch (`tracer_memory.veryl`, `tracer_fill.veryl`).
11. **The record's spare bits** are not held: the state word's [127:112], the stream word's [127:125], the files word's
    [127:103]; register 0 reads 0 (`tracer_record.veryl`, `tracer_rows.veryl`); 17.9.2 says "spare, 0" of the files word only.
12. **A row that moves pc without `next_from_value`** (UB, 17.8.7) runs on sequentially, as the RTL's ALU; meshsim flags it.
13. **A row that is not its expansion's last keeps its pc** whatever its opcode (inventory A9.4): gated in `tracer.cpp`,
    not in the shared `replayer_alu.h`.
14. **A non-contiguous `VirtualPext`** takes a cycle for each bit of its mask up to the highest set one, + 2
    (`replayer_alu.veryl`).
15. **A key of 0xFFFF_FFFF** matches a free entry and takes its base, as v1.35's probe did (`tracer_jump_lookup.veryl`).


### The segment on the chip


1. **The memory system's timing** on the chip is the RTL test's model's, not a design's: reads answered 2 to 24 (a
   code line), 32 (a data line) or 40 (a map line) cycles after their handshake, writes 2 to 30, each ready 0 one
   cycle in four. The trace's cycles depend on it, and are the RTL chip's only under it.
2. **The scratchpads' layout** of a segment (the header's addresses, the tables at word 0x40, the replay region at
   0x1_0000) is the RTL test's, "as the host would"; the firmware reads it from the header.
3. **A cycle** of the RTL's test is the simulator's less one (the framework counts from 1); the fill port's draws and
   the rows' cycles are compared in the RTL test's.
4. **The tracer tile's report** is compared in the words its image writes (0–4, 8–15, three a window from 16): the
   others are the RTL's memory model's start values.
5. **A real segment's advice** is the values its windows' advice rows take (host segment k's first), its loaded
   record's advice field their number: the tracer tile's image requires the last record's field 0 (`tracer.rs`,
   `Status::AdviceLeft`).


### The tile's units, the router and Ingress/Egress


1. **`READ_LATENCY` is 3**, and a read taken in *t* is injected in *t* + 3 and at the router's local input in *t* + 4
   (`rtl/blocks/scratchpad/src/scratchpad_pkg.veryl`; `rtl/blocks/inject_unit/README.md`, "The inject unit"). 15 leaves
   the figure to the implementation.
2. **The return queues are one line** of `READ_LATENCY` stages, not a queue per client (the same README, its decisions).
3. **The banks a write meets** are the aligned `MAX_BANK_BYTES` blocks, the whole scratchpad when it is smaller (the
   same README; `rtl/blocks/scratchpad/README.md`: `min(SCRATCHPAD_BYTES, MAX_BANK_BYTES)`).
4. **A write the director accepts in *a*** is in the scratchpad in *a* + 2L + 2, `PTR` and `HIST` read at the end of
   *a* + L and written at the end of *a* + L + 1 (`rtl/blocks/receive_unit/README.md`, "Timing"); 12.3 says only "within
   `LAND_LATENCY`". `TABLE_LATENCY` above 3 is refused, as that unit does.
5. **A core load of `PTR` or `HIST` in the cycle of the engine's write** reads the old value, and one in the cycle of
   the engine's read is undefined (the same README: "the engine's read-modify-write uses the read port in its read's
   cycle and the write port in the next"); 12.5 says only that the engine wins a port it uses.
6. **`STATUS.gather_busy`** rises in *a* + 1, the first read is offered in *a* + 2, and it falls the cycle after the last
   read's take (`rtl/blocks/gather_unit/README.md`, "Timing").
7. **The DMA unit**: `DMA_CONFIG` is taken at the start (8.2 does not say; the RTL's README, its decisions); the table
   reads begin in the cycle the start is presented in; a place is free for a read from the cycle after its visit is
   made; a count-0 stream that takes no cycle keeps its place until it passes, and passes when the generator looks for
   its next stream, with the first other stream's first element when that stream's entry is in, alone otherwise; the
   transfer ends the cycle after its walk completes (`rtl/blocks/dma_unit/README.md`, "A transfer";
   `address_generator.veryl`'s header).
8. **The write unit's stall** is `reg_ready = !holding_valid || packet_ready` (`rtl/blocks/write_unit/README.md`, "The
   stall"; `write_unit.veryl`).
9. **The live levels** (`EVENTS.request_pending`, a peripheral's event and error) reach `meip` and `nmi` a cycle after a
   load reads them (`rtl/blocks/tile_control_unit/README.md`, "Timing").
10. **Ingress/Egress's UART and host interface** (17.6 gives the frames, the FIFOs and the rules, and no cycle; all of
    these are `rtl/blocks/ingress_egress_unit`'s: `uart.veryl`, `ingress_egress_unit.veryl`, `README.md`,
    "Timing" and "Decisions"):
    - the transmitter's idle point is no frame or the last cycle of a stop bit; a byte is popped at the end of a cycle
      at an idle point with the TX FIFO not empty, and its start bit is on `uart_tx` from the next cycle: *p* + 2 for a
      store presented in *p* into an empty FIFO; frames are back to back, 10 *D* apart; `uart_tx` is a flip-flop;
    - each direction takes `UART_DIVISOR` at a frame's start, as it stands at the start of that cycle (the pop's cycle;
      the cycle the receiver sees the falling edge in); a divisor of 0, UB, wraps the counters (16 bits; the
      receiver's 17);
    - the receiver: two synchronising flip-flops and a third for the line's last value, so a falling edge on the pin in
      *c* starts a frame in *e* = *c* + 2; bit *k* is sampled in *e* + *k D* + ⌊*D* / 2⌋; the frame ends at the stop
      bit's sample, and the receiver is idle again from the next cycle; the start bit is not sampled;
    - "full" for `rx_overrun` and for both stalls is the FIFO's count at the start of the cycle, a pop in that cycle
      notwithstanding (the library's `queue`); a stalled store is taken in the first cycle at whose start its FIFO
      has room (`register_bus` presents it in every cycle; `lines_ready`, `stream_ready` from registers);
    - a line error raised at the end of a clearing store's cycle stays set; `UART_STATUS`'s TX count is the FIFO's
      alone (the byte on the line has left it);
    - a pop (`UART_RX`, `STREAM_RECEIVE`) is at the end of the load's cycle and only if the FIFO was not empty at its
      start: a byte or a beat entering at that cycle's end stays;
    - `lvds_out_tvalid` and `lvds_in_tready` are flip-flops loaded with the FIFO's next state: `tvalid` from *p* + 1
      for a send presented in *p* into an empty FIFO, the next beat (or 0) the cycle after a handshake; `tready` 0 in
      the first cycle after the reset, and the cycle after a handshake fills the FIFO, 1 the cycle after a pop;
      `lvds_out_tdata` and `_tlast` are the FIFO's head register; a beat in is read from the cycle after its handshake;
    - `STREAM_OUT_W0`–`W2` hold only the words below the beat's top; a store at or above the top, and the top word's
      bits past TDATA, change nothing; `STREAM_BYTES` is the chip's 4 (`Veryl.toml`), a setting of the model (1–16).
    In the simulator a load that pops is new to the slot: the peripheral's `load_pops` and `load_effect`, the window's
    static bits `Win::R_POPS` and the core's `CoreOp::LOAD` (`periph_slot.h`, `peripheral.h`, `tile_ctl.h`).
11. **The router** (`rtl/blocks/router`; `src/units/router.h`): a queue is full by its count at the start of the cycle,
    its push then lost even if the head leaves in that cycle, and `overflow` is in the push's cycle (16.3 says only
    that the push sets `ERRORS.router_overflow`); the eject's arbiter does not see the loopback queue; a packet for
    none as its dst is dropped like one for a reserved id, and a DMA input is always ready for either; on a special
    tile a packet from its attach port for another tile goes nowhere. A packet at a tile its route does not pass, which
    no link delivers, goes where the RTL's route functions send it (a unicast with no ring move east, with no column
    move south; a broadcast's ring copy by its rows): the conformance scripts hold such packets, the NoC never.
12. **A peripheral's DMA** (`rtl/blocks/peripheral`, `peripheral_dma.veryl`): a packet register between the queue and
    the router's DMA input; the transfer takes `DMA_HDR` at its start; it ends in the cycle its last packet leaves.
13. **`ERRORS.drift`** is set at the end of every cycle a group transfer's read is offered and not taken, whatever holds
    it back: a higher client, the gather unit busy, a write at its bank, no token (`address_generator.veryl`: `drift =
    group_valid && !read_request_ready`).
14. **The gather queue** (`rtl/blocks/gather_unit`, `gather_unit.veryl`): a request leaves it in the cycle the offer
    register is empty or its read is taken; the overflow's pulse is in the lost request's cycle, judged on the count at
    the start of that cycle.


### The RW engine

`src/units/rw_engine.h` is the RTL's `rw_engine_unit` (`rtl/blocks/rw_engine_unit/src/rw_engine_unit.veryl`, its
README's Timing and Decisions), on the slot's control block and DMA (`rtl/blocks/peripheral`). 17.7 leaves these to
the implementation, and the model takes the RTL's:

1. **The bursts** are the job's 64-byte groups: each burst runs from the job's next beat to the end of that beat's
   aligned group of 4 beats, or to the job's last beat (17.7: "their lengths are the implementation's").
2. **A read's room** (17.7: "only while the queue has room for every beat in flight"): a burst of *L* beats is
   raised only if the beats raised and not yet received, with *L*, fit in output queue 0's free entries at the start
   of the cycle; the entry the DMA takes in a cycle is still counted as held in that cycle.
3. **The cycles**: `START` presented in *p* takes the job's registers at the end of *p*; `STATUS.busy` from *p* + 1;
   the first burst raised at the end of *p* + 1, on AR or AW from *p* + 2, and a next burst at the edge after the
   address channel frees; a write's first beat on W in *p* + 3 and a beat a cycle while WREADY is 1 (the buffer's row
   read in every cycle, the pending one again until the W register takes it); a job of 0 beats ends in *p* + 1; a
   read ends in the cycle its last beat comes and a write in the cycle of its last response, `EVENTS.done` posted
   then. 21.5's row gives the first three of these.
4. **The write data may run ahead of its address**, and the writes outstanding have no bound of their own; AR and AW
   share their address and length register; RREADY and BREADY are 1 and RLAST is not read.
5. **The write port** lands a `WRITE` at the edge that ends the cycle the port presents it (the RTL's README counts
   that as 1 of `PERIPHERAL_LATENCY`'s 6); a write past the buffer's end lands at its row within the buffer, a misaligned
   one lands aligned, one of the reserved size writes nothing (all UB: the model also flags them).
6. **The reply**: `RW_REPLY_W0` is a register, 0 at the reset; a store to `RW_REPLY_SEND` pushes at the end of its
   cycle, into room at its start, and clears `RW_REPLY_W0`; a push to a full queue 1 (UB) is lost, `RW_REPLY_W0`
   cleared all the same (flagged). Queue 1 holds one entry.
7. **`lpddr_error`** is set by any response but OKAY, EXOKAY among them, at the end of its cycle.
8. **A `START` in the cycle a job ends** starts the next (the RTL's test does it); a `START` while busy otherwise is
   UB and stops the simulator. `RW_CONFIG`, `RW_LPDDR_ADDR` and `RW_BUFFER_ADDR` are 0 before their first store and
   keep their values through a reset (the RTL does not reset them: undefined, 17.7).
9. **The slot's DMA** (`peripheral_dma.veryl`): the packet register is the slot's (`SlotCore::Dma`: `full`, `pkt`), one
   packet in front of the router's DMA input, offered until the router is ready for it (16.4: the room of the queue it
   enters; `router.h`, A DMA INPUT). A transfer ends in the cycle its last packet leaves the register; with a count of 0
   it ends the cycle after `DMA_START`'s. So under a stalled router input a read's beats stay in output queue 0 but the
   one in the register, as the RTL's: `test_rw_engine` checks the engine with the slot's register against its reference
   under every stall (the bench gives `packet_ready`), and `test_rtl_memory` the chip, cycle for cycle.
10. **The LPDDR controller is not chip hardware**: the `lpddr` port is the simulator's boundary, a harness places an
    AXI4 slave on it (`LpddrSlave`), and a job that raises a burst with none stops the simulator. The reset is not
    the port's: a harness that resets the engine resets its slave (3: "the LPDDR controller, which shares the
    reset").

### Where the chip's reference figures came from

Most are in Snarky One's READMEs or were written by its tests as fixtures (`tests/segment/rtl/`, `tests/rwengine/`,
`tests/routerconf/rtl/`, `tests/core-programs/`: each folder's README says how). Three were read from runs of the
RTL's chip in a scratch worktree of Snarky One at 076e647, never committed there:
1. **The sort's start-up** (`test_rtl_sort`): the RTL's chip README gives 16,033 cycles from the load's release to
   every tile's arrival; the 16,036 to L1's release, and each phase's arrival and release (phase 0 arrived at 20,780
   and released at 20,784, phase 1 at 36,817 and 36,820, in all four runs), are the master's summary read from its
   DMEM after `chip_test`'s set-up.
2. **The stream run** (`test_rtl_stream`): every handshake's cycle and the done word's, from `chip_stream_test` with
   its `Bench::cycle` printing them (`cargo test -p chip chip_stream_test -- --nocapture`).
3. **The UART through the master's core** (`test_rtl_stream`): a scratch test of the RTL chip written beside
   `chip_stream_test`, the same program and line as the simulator's: the first start bit in cycle 34, the led pin's
   20 changes, `UART_STATUS` in DMEM after 643 cycles, the done word after 5,210.
A cycle of the RTL's tests is the simulator's less one (the framework counts from 1).

## (d) What was retired, and what replaced it

`README.md` ("What v1.36 does not carry over") is the list for a reader of v1.35. In short, by what replaced it:

| retired | replaced by |
|---|---|
| v1.35's core, its M1 bench and goldens | the RTL's core and its programs (`test_core_programs`, `test_core_tile`) |
| the conformance of v1.35's units against v1.3's Java simulator (the engines, the memory path, the slot, C&C 1's peripheral, the router, the Tracer's call logs, MemTest's and NetTest's logs) | tests from the specification and the RTL's blocks: `test_ag`, `test_receive`, `test_mem`, `test_gather`, `test_periph_slot`, `test_ingress_egress`, `test_router` and `test_special_router`, `test_tracer_rules` and `tracer_block`. A reference made by the RTL is never regenerated from the simulator |
| v1.3's suites on v1.35's firmware (the control, recipe, read-port, accounting, request-bus and C&C suites), `test_chip`, `test_sort`, the sort's driver, sweep and kit | the RTL's chip tests under the RTL's firmware: `test_rtl_tile`, `test_rtl_sort`, `test_rtl_memory`, `test_rtl_stream` |
| Stage B and F, T2 and the chains | `test_rtl_segment`. What nothing checks now: a segment's end memory carried into the next segment's start on one machine (the memory system between segments is *Open*, 17.9.14) |
| CheckSelf | `test_checker`, for the rules of the packet v1.36 keeps; (a)'s last row for the rest |
| meshsim's own firmware | the RTL's, built here from sources here but for (e)'s images (`fw/README.md`) |
| the settings of v1.35's hardware (`areaQueueDepth`, `writeCombining`, `fifoCountBeforePop`, `sramReadLatency`, `weInDepth`, `agLookahead`, `ccDmaDepth`, `doneGate`, `stageDelay`, `runLatency`, `strictReplies`, `checkCommandRead`, `checkUnexpected` and the others) | refused by name, each with its reason (`MachConfig::dropped()`, `tests/mach.cpp`) |

What is left of v1.35's machine in the tests, and why: the coreless machine with a behavioural master (`tests/mach.h`'s
`Net`, `tests/bench_master.h`), on which `test_mach` runs v1.3's `packets`, `reads` and `a2a` sequences through
v1.36's units and `test_checker` provokes the checker; `test_cnc` checks that behavioural master against v1.3's
(`tests/cncconf/`). It is a test bench, not chip hardware: the chip's master is the master tile (`test_rtl_sort`).
`shard_summary` still sums Stage B and F lines as v1.3's did (its test's cases hold them); nothing writes them.

## (e) Firmware that is not built here yet

Every image the simulator runs that is C or assembly is built in `fw/` from sources there, and `fw/build.sh --check`
proves each equal to its committed copy. What follows is not: it is committed as Snarky One built it, and checked
only against a hash list.

| what | where | why it stays | what closes it |
|---|---|---|---|
| the chip's Rust firmware: the master tile's program (six images, one a run), the memory tile's, the replay image and the tracer tile's image | `fw/rtl/sort/`, with its sources in `fw/rtl/src/` and its hash list `fw/rtl/MANIFEST` | it is Rust, built by Snarky One's toolchain and recipes; the owners decided it is to be C, built here. Until then meshsim makes what it can of each master: its run image, with the tile image built here, which every committed master must hold byte for byte (`fw/README.md`, "The run image") | each program rewritten in C in `fw/` and built by `fw/build.sh` (`fw/README.md`, "Built outside meshsim, to be replaced by C": what each must provide). The images' instructions change, so the chip's cycle figures (`README.md`, Status) must be taken again from the RTL's run of the C images |
| the core's Rust test program, `firmware-rust.elf`, one of the RTL's 49 | `tests/core-programs/elf/`; its source in `fw/rtl/src/` | the same | a C program in `fw/core/` in its place, with the RTL's run of it for its references, or its retirement in the RTL |
| riscv-tests' and CoreMark's images, when `fw/build.sh` is run without their checkouts | `tests/core-programs/elf/` | third-party sources, not copied into this repository | nothing: `RISCV_TESTS` and `COREMARK` name the checkouts, and `fw/build.sh --check` then rebuilds and compares all 48 |
