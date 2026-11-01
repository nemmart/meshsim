# Machine spec v1.1 → v1.2: what changed

`spec/machine-spec.md` is now v1.2. It describes the machine as it is; this file says what moved since v1.1,
section by section, so a v1.1 reader can find their way. Every item cites the decision behind it, as
**D*n***, a section of `planning/DECISIONS-v1.2.md`, or **W*n*-*nn***, a finding in
`planning/reviews/`. Where a later decision revised an earlier one, the citation names the later.

**Numbering.** v1.1's section numbers are kept wherever the subject survives. A section whose subject v1.2
removes keeps its number as a one-line stub pointing here: **4.17.7** and **8.12**. New subjects take new
numbers at the end of their chapter: **2.2**, **8.14** and **8.15**. Invariants keep their numbers (7 is
withdrawn, 19–26 are new), and so do open items (1–116 keep their text with a *(v1.2: …)* note; 117–133 are
new).

**Still at v1.1, not touched by this update:** the figures (`diagrams/`), `sw/mesh.h` and
`sw/REGISTERS.md` (to be regenerated from section 8; a draft v1.2 header is `sw/mesh-v1.2-draft.h`, WORKER6),
`spec/schedule.md` (unchanged in substance), `spec/replayer-opcodes.md` (unchanged), meshsim, the sort and
the measured results.

## The machine in one paragraph

A 256-bit link word with a 32-bit byte address and a 3-bit counter field; one 32-bit map for packets and for
the controller; five modes; four write counters decremented K cycles after the traffic director; natural
alignment checked in hardware; up to three peripherals per unit, each with a region, a standard register
window, counted or live events and an optional DMA; errors that trap terminally; no HALT state, a "toast"
pin and an outside reset; a boot ROM in every controller; and three controllers that are tile instances with
their engines as peripherals.

## Section by section

| section | what changed | decisions |
|---|---|---|
| front matter | v1.2 summary; companion files marked stale; the *(Deferred)* marker; the hardware/meshsim split stated once (who checks what) | D27 |
| 1 | addresses: packets and the controller use 32-bit byte addresses, big-SRAM engine pointers stay 21-bit chunks; packet address of chunk *c* is `0x03000000 \| c << 2`; natural alignment defined; register names without `TILE_`; conditions renamed errors | D5, D14, D26, D29 |
| 2 | C&C 1–3 at endpoints 64–66 on tiles 0–2's fifth ports, each a tile instance with its engine as peripheral 1 (Ingress/Egress and the UART; the Updater/Binder/RW engine; the Tracer); nets: GO and READY into C&C 1's peripheral, one error line per unit by endpoint id, hard and soft reset inputs, the toast and `LED` pins; RUN/HALT net gone | D15, D23, D24, D25, D28, D35 |
| 2.1 | 65 and 66 are C&C 2 and C&C 3 (65 proposed with the endpoint map); 72–74 gone; 67–125 reserved | D25 |
| **2.2 (new)** | the 32-bit address map: data memory 0x00, program memory 0x01, boot ROM 0x02, big SRAM 0x03, peripherals 0x04–0x06, registers 0xFF; who reaches what; offsets in bytes; out of bounds unchecked; auto-advance past the end a software bug; data-memory word 0 reserved | D5, D9, D22, D26, D27 |
| 3.1 | 256-bit link word, 74 reserved bits stored in full; modes 0 write at address, 1 by source, 2 bucket, 3 augmented, 4 gather request, 5–7 discarded silently; the counter field (0 uncounted, 1–4, 5–7 treated as uncounted); uncounted traffic is commands and boot overlays only; the bad-packet table (alignment and region decode); v1.1's modes 0, 6–15 and their bad-packet rules gone | D2, D6, D17, D26, D30, D32, W5-15 |
| 3.2 | padding stages 256 flops | D2 |
| 3.3 | target 3 (the RW engine) deferred with the file system; the endpoint-assignment caveat removed | D25, D33 |
| 4 (intro) | unit table updated | — |
| 4.1 | natural alignment for every 64/128-bit access; spanning accesses removed; misaligned packet, write and read rules; out of range undefined; `mem_idle` gone; phase ordering by K | D26, D32, D36 (W2-38), D18 |
| 4.2 | `PULSE_DMA` starts a run and clears DMA run done; DMA run done (`EVENTS` [7]) after the last table read; new `STREAM` layout (gap after in `STREAM_SRC`, 32-bit destination address in `STREAM_DEST`, size/counter/mode/*d* in `STREAM_COUNT`), 17-bit count; *d* 64–66 the controllers; misaligned source read sends payload 0 | D4, D12, D17, D26, D30, D32 |
| 4.3 | `SRAM_RD_DEST_ADDR` R/W: data-memory byte offset, size, counter; results are mode-0 writes on a named counter; misaligned read sets [1] (not [7]) and still sends its packet; search loop rewritten | D13, D32, D36 |
| 4.4 | gather requests are mode 4, never counted, their counter field passed to the gather values (mode 1); record-aligned base | D6, D17 (C), D32 |
| 4.5 | read queue carries the counter; spanning part gone; two injection sources kept; controllers and DMAs don't use a tile's injection point | D10, D26 |
| 4.6, 4.7 | routing to and from 64–66 via tiles 0–2; 256-bit FIFO entries | D2, D25 |
| 4.8 | switch on mode and region; the K-stage delay line; K a parameter (default 8) with its formula; "command landed" when a write covering slot 0 lands in port B; HALT and `cm_idle` gone | D3, D6, D18, D32, D36 |
| 4.9 | modes renumbered; mode 0 at an explicit address replaces v1.1's mode 3; `PTR[64]` no longer special; `PTR` alignment, a write through a misaligned `PTR` dropped but counted | D6, D17, D26, D32 |
| 4.10 | table window at 0xFFFF2000 (proposed); waiting for the tables by events; guarantees A–C; `ts_busy` and `wait_tables()` gone; window access after done safe by K | D11, D17, D32 |
| 4.11 | the core's two inputs, CSRs legal, traps instead of halts; 32 KB + 32 KB memories at regions 0x00/0x01, program memory load-only; boot ROM; bus-error list; `ROUTER_HDR` and `ROUTER_ADDR` with the no-increment bit replace `ROUTER_SET_HDR`; resident loader gone; register-load latency extends to windows | D1, D5, D7, D10, D20, D22 |
| 4.12 | commands and replies are mode-0 writes; blocks at the top of 32 KB data memory; four write counters named by the packet, decremented at K; unexpected arrival at the decrement (accepted trade-off); `EVENTS` layout renumbered (counter 4 at [3], counters done [4], GO [5], command landed [6], DMA run done [7], request room [8], request pending [9], peripherals [12:10]); spurious wakes; HALT rules become reset rules | D3, D6, D14, D16, D30, W5-01 |
| 4.13 | `PARAMS`, `STATUS` and `ERRORS` replace `TILE_STATUS`/`TILE_CONDITIONS`; `STATUS` renumbered; `ERRORS` [1] "bad packet or misaligned access", [11]–[13] the peripherals (live); errors sticky until the reset; traps terminal; the handler's conventions | D1, D18, D20, D21, D27 |
| 4.14 | commit by K; uncounted writes only commands and overlays | D3, D17, W5-15 |
| 4.15 | the bucket write is mode 2 | D6 |
| 4.16 | connected at tiles 0–2 to C&C 1–3; 256-bit FIFOs; the memory system enters at tile 1 | D2, D25 |
| 4.17 | rewritten as **Peripherals**: up to three per unit, regions and 0x200-byte windows, the ten standard words, counted and live events, errors per peripheral, writes into a region as mode 0 counted by their counter field, the optional DMA on its own router port | D9, D10, D14 |
| 4.17.5 | "Mode 9, the peripheral write" becomes "Writes into a peripheral's region"; `DMA_PERIPH_REMAINING` gone | D6, D10 |
| **4.17.7 (stub)** | HALT, a halted controller and the empty slot: removed (no HALT; an absent peripheral is in 4.17.1) | D23 |
| 4.17.9 | the sender becomes the peripheral's DMA: `PULSE_DMA`, `DMA_HDR`, `DMA_ADDR`, 12-bit count | D10 |
| 4.18 | request pending is `EVENTS` [9]; the HALT paragraph becomes the reset | D16, D23 |
| 4.19 | the replayer as peripheral 1 with the standard window; **its low-water lines are counted credits** (depth 2, set at `PULSE_RUN` and at each new expansion, one returned per half queue consumed, unneeded bits masked by the loop); the controller loop rewritten; its map in byte offsets (parts at offset [22:20]); ownership stopped/running; errors in its own `ERRORS`; the stream-length bound corrected to about 4.3 Mbit | D9, D10, D36, W1-30, W3-24 |
| 4.20 | the UART is part of C&C 1's peripheral; line errors are status (`UART_STATUS`, W1C), never errors | D9, D21, D28 |
| 5 | rows for K, the error lines, peripherals, events and the reset; RUN/HALT removed | D3, D23, D32 |
| 6 (intro), 6.1 | "RUN/HALT and reset" becomes "Reset, errors and the toast pin": no HALT; an error end to end; C&C 1's handler; the toast pin; the one keep-alive watchdog; hard and soft reset from outside; what the reset does; no reset hold (the error lines' flops reset with the whole chip) | D21, D23, D34, D35, D36, W5-13 |
| 6.2 | the boot ROM, exactly; the boot sequence; C&C 1 booted from outside; C&C 2/3 start on their command and report "started"; overlays and first command of one kind; staging by address and always counted; `PTR[64]` gone | D22, D31, D36, W5-15 |
| 6.3 | READY into C&C 1's peripheral, tiles only; `PULSE_GO` in C&C 1's peripheral clears READY and drives GO; `done` no longer forced in HALT | D23, D28 |
| 6.4 | `mem_idle` gone from the end of a phase | D18 |
| 6.5 | block copy as a mode-0 stream on a write counter | D6 |
| 6.6 | "The C&C controller" becomes "The controllers and their registers": C&C 1–3 as tile instances; C&C 1's peripheral words; REPORT, the receive FIFO, the RUN/HALT and GO registers gone | D1, D15, D25, D28, D36 |
| 6.7 | the memory system is C&C 2's peripheral; requests name a 32-bit destination and their data and reply counters; replies are mode-0 writes; writes by tags (window protocol deferred); debug output as log files | D23, D25, D33, W5-17 |
| 7 | the memory subsystem is C&C 2's peripheral; LPDDR behind its AXI bus survives a soft reset | D25, D35 |
| 8.1 | region 0xFF's map; the controller block's new offsets; the common header layout; reserved words read 0; configuration registers; the C header | D9, D11, D12, D14, D30 |
| 8.2 | `PARAMS` and `STATUS` (was `TILE_STATUS`) | D18 |
| 8.3 | `ERRORS` (was `TILE_CONDITIONS`) | D18 |
| 8.4 | `ROUTER_HDR`, `ROUTER_ADDR` (was `ROUTER_SET_HDR`) | D7, D12 |
| 8.5 | `DMA_CONFIG` readable, at 0x858 | D11 |
| 8.6 | `PULSE_DONE`, `PULSE_DMA` (was `TILE_PULSE`) | D8 |
| 8.7 | `WRITE_COUNTER_1`–`4` (was the `DMA_*_REMAINING` counters) | D14, D30 |
| 8.8 | `SRAM_RD_DEST_ADDR` R/W, new layout | D13 |
| 8.9 | table window in region 0xFF; `STREAM` fields | D4, D11 |
| 8.10 | `EVENTS_*` (was `WAKE_*`) and the request port at 0x848–0x854 | D11, D14, D16 |
| 8.11 | "The peripheral registers" becomes the standard window and the replayer's | D10, W3-REVIEW-2 §5 |
| **8.12 (stub)** | the sender's registers: removed | D10 |
| 8.13 | the UART's words in C&C 1's peripheral | D9, D28 |
| **8.14 (new)** | C&C 1's peripheral: error lines, READY, `READY_EXPECT`, `PULSE_GO`, `WATCHDOG`, `LED`, the UART | D15, D24, D28, D36 |
| **8.15 (new)** | simulator-only words: the trace word and the exit word | D29, D32, D36 |
| 9 | every invariant marked as a meshsim check with its hardware counterpart; 7 withdrawn; 4, 5, 9, 10, 11, 14–18 restated; 19–26 new (bounds, alignment, reserved encodings, uncounted traffic, silent traps, K, boot order, ignored starts) | D27, D26, D32, D36 |
| 10 | K, `error_latency`, write counters, boot ROM, watchdog, addresses, peripherals; 256-bit word; memories; `run_latency` and the C&C receive FIFO gone | D2, D3, D5, D30, D32 |
| 11 | rescaled for the 256-bit word (≈ +0.065 mm²) and the 32 KB memories; sender and per-tile UART dropped; total ≈ 0.52–0.61 mm² | D2, D5 |
| 12 | the control island with 32 KB banks (≈ 0.17–0.2 mm², item 127) | D5 |
| 13 | status of items 1–116; new items 117–133 (the log's pending Q1 and Q9, S1, the deferred list of D33, and v1.2's proposals) | D33, Pending questions |
| 14 | mode numbers and counter names updated | D6, D14 |

## Removed, by name

`TILE_STATUS`, `TILE_CONDITIONS`, `TILE_PULSE`, `ROUTER_SET_HDR`, `DMA_SRAM_REMAINING`, `DMA_CM_REMAINING`,
`DMA_REPLY_REMAINING`, `DMA_PERIPH_REMAINING`, `WAKE_EVENTS`/`MASK`/`REQUIRED`/`PEEK` (renamed `EVENTS_*`),
`PERIPH_STATUS_0`–`7`, `PERIPH_RUN`, `PERIPH_SERVICE_1`–`6`, `periph_running`, `SENDER_SEND`, `SENDER_ADDR`,
`sender_busy`, sender idle, the tiles' `UART_*` registers and events, `ts_busy`, `mem_idle`, `cm_idle`,
`wait_tables()`, the RUN/HALT net and `run_latency`, the resident loader at 0x1F00, packed 14-bit
controller-memory words, v1.1's modes 0 and 5–15 (as numbered there), REPORT, C&C's receive FIFO and its
error flag, write tokens and mode-3 write streams to C&C, the spanning big-SRAM access.

## Round 2: decisions 37–42 (2026-10-29)

Applied after the first round. Each row names the spec sections it touched.

| decision | what changed | sections |
|---|---|---|
| D37.1, D41 (F13) | the boot ROM's tile branch jumps only if the load that consumed GO returned "command landed" (`EVENTS` [9], the event, not `STATUS` [5], which GO clears); otherwise it pulses `done` again and waits. The event stays pending after a unit's first command, and the ROM relies on it | 4.12, 6.2, 6.3 |
| D37.2 | interrupts stay off in the ROM; boot part 2 sets `mtvec` and enables `mie.MEIE` and `MIE` (a software convention). Until then an error raises the error line and the toast pin but takes no trap | 4.13, 6.2 |
| D37.3, D41 (F16) | the report set is defined exactly: 24 reports in a fixed order, three per peripheral slot with zeros for an absent one; the handler reads seven standard words per window (`EVENTS_PEEK`, never `EVENTS`; not the write-only pulses); reports are exempt from the per-tile limits | 4.13 |
| D37.4 | the first 16 bytes of data memory are reserved (0x00–0x0F); overlays start at 0x10 | 2.2, 3.1, 4.11, 9 (22) |
| D37.5 | the no-increment bit stays set until the program clears it | 4.11 |
| D37 (limitations) | C&C 1 sends the dump down LVDS when an FPGA is in the loop (which waits as long as it wants; 0.1 ms is a default), over the UART otherwise | 6.1 |
| D38 | the receive queues are in the request tree, at the root, one per target: 128, 256 and 128 for C&C 1, 2 and 3; credits equal the depths; a tile keeps at most 1, 3 and 1 requests outstanding; error reports excepted; tiles have no receive queue | 3.3, 4.18, 6.6, 6.7, 8.10, 9 (17), 10, 11, 12 |
| D39 | the replayer's status words: 32 `ROWS_RUN`, 33 `EXPANSION_OFFSET`, 34 `EXPANSION_OPCODE`, one field per word | 4.19.1, 4.19.5, 4.19.9, 8.11 |
| D40 | every request names its own reply address, anywhere in data memory; the reply block is the default for tile-to-tile replies; **seven counters**, codes 1–7 (none reserved), `WRITE_COUNTER_1`–`7` at 0x860–0x878, the read port at 0x87C and 0x880; **`EVENTS` renumbered**: counters [6:0], counters done [7], GO [8], command landed [9], DMA run done [10], request FIFO no longer full [11], request pending [12], peripherals [15:13]; the reset mask [8] \| [7] | 3.1, 4.2, 4.8, 4.10, 4.12, 4.13, 4.14, 4.17, 4.18, 4.19, 4.20, 5, 6.1–6.7, 8.1, 8.6, 8.7, 8.8, 8.10, 9, 10, 11 |
| D41 (F12) | the core implements `mcycle` and `mtime`; C&C 1 bounds its waits by cycle count | 4.11, 6.1 |
| D41 (F14) | a stream's *d* may be 126 (the broadcast) in hardware; its own copy takes a read token; only the controllers broadcast from streams, by convention; meshsim errors on a mesh tile | 4.2, 4.5, 6.2, 9 (17) |
| D41 (F6) | `rd_running` falls in the same cycle as DMA run done (guarantee B) | 4.2, 4.10 |
| D42 | port B takes one 128-bit write per cycle, built as the implementer likes (W5-05); W5-02, 03, 07, 08, 10, 11, 12, 14, 16, 18, 19, 20 accepted, their *(Proposed)* markers dropped; W5-04 and W5-21 are with Tony (item 134); S9's protected loopback marked required | 3.1, 4.3, 4.5, 4.7, 4.9, 4.11, 6.1, 6.7, 8.1, 8.13–8.15, 10, 11, 12, 13 |
| 13 | status rows for items 61, 64, 85, 92, 93, 99 updated; 121–123, 127, 131–133 updated or closed; new items 134 (Q16) and 135 (what "outstanding" means for a read) | 13 |

### From the recipe check (round 2)

| what | sections |
|---|---|
| a note on a unit's two injection sources, which aren't ordered by the spec; open item 136 (WORKER6's F17) | 3.2, 6.2, 13 |
| the replayer's controller loop waits on the refill counter **and** DMA run done, as its own table rule (4.10) and recipe 26 require | 4.19.9 |
| the `done` pulse clears no event but the counters'; a stale "[4]" corrected to [7] | 4.12 |

## Round 3: decisions 43–44 (2026-10-29)

| decision | what changed | sections |
|---|---|---|
| D43 | **mode 5 is count only:** it decrements its counter at the destination and writes nothing; any size, payload and address ignored; from a stream the source is read and the payload discarded; to the tile itself it takes a read token; counter 0 does nothing (meshsim flags it). Modes 6–7 stay reserved. The counter field counts modes 0–3 and 5 | front matter, 3.1, 4.2, 4.5, 4.8, 4.12, 9 (21) |
| D43 | **the fence:** a run that something must follow ends with a count-only packet to the tile itself on a spare counter; when that counter reaches 0, everything earlier in the run has been injected. DMA run done keeps its meaning (the last table read). Count-only packets also serve as "done" signals between tiles | 3.1, 3.2, 6.2 |
| D44 | **ordering per sending unit:** packets from one unit in one tile (the address generator, the send port, the GATHER engine, the read port, each sending peripheral's DMA), to one destination, of one kind, arrive in the order sent; between units there is none; a fence or putting the packet in the run imposes it. Round 2's note and item 136 are replaced | 3.2, 6.2, 13 (136 closed) |
| D44 (W5-23) | "outstanding" is a software convention: a request until its reply lands, a read until its data lands; in hardware a request holds its receive-queue slot only until the target pops it | 3.3, 4.12, 6.7, 13 (135 closed) |
| D44 (W5-24) | the 24-report set is decided as defined; its header's op and sequence fields come with C&C 1's request format | 4.13, 13 (122) |
| D44 (W5-26) | C&C 1 lands a tile's first command only when that tile should start at the next GO | 6.2 |
