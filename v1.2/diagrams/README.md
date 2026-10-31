# v1.0 diagram set

Twenty-nine figures in eight parts, numbered part.figure (1.0–8.1), for explaining the machine to another engineer and for building it: parts 2–7 go down to RTL (registers, gates, muxes, state machines, cycle-level timing and interface tables). They are drawn from `../spec/machine-spec.md` v1.0; each figure's subtitle and source line name its sections, and the spec's sections name their figures. Every figure is a self-contained SVG with its caption and source inside it. Pages are 1300 px wide with body text at 11.5–13 px and the smallest labels at 10–10.5 px. That reads well on screen. Printed across landscape letter or A4, body text comes out at about 6.5 pt and the smallest labels at about 5.5 pt, so print on A3 or tabloid for comfortable reading.

**Folders.** `svg/` holds the figures (self-contained SVGs, each with its caption and source line), `png/` the same figures rendered at twice their size (for slides and viewers without SVG), and `src/` the Python generators. The file names in the tables below are in `svg/` (and `png/`, with `.png`).

**Part 1. System**

| # | file | figure | caption |
|---|---|---|---|
| 1.0 | `fig1_0_sort.svg` | The sort these units serve: six steps (32-bit key, value of k records) | Steps 1, 2, 5 and 6 run inside each tile as loopback streams; steps 3 and 4 use the mesh. The gather (figure 7.4) serves steps 2 and 6, augmenting and count-on-arrival (figure 1.3's write engine) serve steps 1, 4 and 5, and the controller read port serves step 3. |
| 1.1 | `fig1_1_system_cards.svg` | The system: 16 cards of 8 ASICs under a controller FPGA *(above the chip; not in the spec)* | A controller FPGA has its own link to each of 16 cards. Each card is a card FPGA with its own link to each of 8 ASICs, each figure 1.2's 64-tile chip with its own DDR4: 128 ASICs and 8,192 tiles in all. ASIC 0 is opened up to show its 8 × 8 mesh, C&C and memory subsystem. |
| 1.2 | `fig1_2_system.svg` | The system: 64 tiles on the circulant mesh, C&C and the global nets | The 64 tiles are identical except tile 0, whose router has a fifth port to C&C. C&C holds the tiles in HALT or RUN, writes commands into their mailboxes over the mesh, collects one READY pulse per tile and answers with GO. Each tile's error line (any condition set) is ORed into one sticky error bit at C&C, so C&C learns at once that something is wrong; REPORT then collects the tiles' conditions (§6.6). |
| 1.3 | `fig1_3_tile.svg` | The tile: the engines, the tables and registers that drive them, and the big SRAM | Packets enter through the traffic director and leave through the injection point. The GATHER engine, the address generator and the controller read port share one SRAM read queue at fixed priority, drawn right to left from highest to lowest. Every read return becomes a packet: a read-port result is a mode-7 packet to the tile itself, back through the loopback and the director. The write engine is the only writer. |
| 1.4 | `fig1_4_packets.svg` | The link word and the packet modes | Every packet carries a 4-bit mode, and the receiver's traffic director routes on it alone: mode 1 feeds the GATHER engine, 2–4 the write engine, 5–7 port B of the controller's memories; 0 and 8–15 are dropped with the bad-mode condition. Three down-counters count modes 2–4, 5 and 7. |

**Part 2. Control**

| # | file | figure | caption |
|---|---|---|---|
| 2.1 | `fig2_1_control.svg` | Control: done, command landed, READY, GO, WFI and the local wake | READY is automatic: a tile pulses it once when its done bit and "command landed" are both set. C&C answers 64 READY pulses with one GO, which only wakes cores in WFI. A tile's own arrivals wake it through the local wake, the latched edge of its three down-counters all reaching 0. From the done pulse until GO, only GO can wake the core. |
| 2.2 | `fig2_2_tile_edge.svg` | The tile's edge toward C&C: RUN/HALT, GO, done, command landed, READY and the error line *(RTL)* | RUN's two edges come from one register: HALT → RUN resets the core and clears done and command landed; RUN → HALT clears the conditions. READY is its own line, a one-cycle pulse when done and command landed are first both 1; done reads 1 throughout HALT. GO enters through go_q. The error line is the OR of the tile's conditions. |
| 2.3 | `fig2_3_wake.svg` | WFI and wake: the down-counters, the local wake and the done gate *(RTL)* | WFI wakes on GO or on the local wake: the latched edge of all three counters done, qualified by a counter having been non-zero. One wake consumes the latch. From the done pulse until GO the gate holds the latch's set term off, so only GO can wake wait_for_command(). Counter stores, consume, the done pulse and HALT clear the latch; a clear wins over a set in the same cycle. |
| 2.4 | `fig2_4_conditions.svg` | The conditions: where each TILE_CONDITIONS bit is set, the register, and the error line *(RTL)* | TILE_CONDITIONS holds 11 set-only bits, one per error a tile can detect; entering HALT clears them all, and the clear wins over a set in the same cycle. Their OR is the tile's error line to C&C. Five are overflow detectors that drop the arriving item; two (5 and 8) can only mean a hardware fault. The register is read-only: a store to it is itself a bus error. |
| 2.5 | `fig2_5_reset.svg` | Reset and boot: every state element at chip reset, in HALT and at the RUN/HALT edges *(RTL)* | Chip reset puts every tile in HALT with its queues empty and its configuration registers undefined. HALT holds the address generator, the GATHER engine, the read port and the token FIFO in reset, while the routers, the director and the write engine run. HALT → RUN resets only the core; RUN → HALT clears the conditions. Boot loads by mode 5, then one marker and RUN. |

**Part 3. The controller**

| # | file | figure | caption |
|---|---|---|---|
| 3.1 | `fig3_1_controller.svg` | The controller's memories: what it reads and writes, all in the control island | The controller reaches every memory here in one cycle, so all of them sit with it in the control island. It reads and writes data memory, the registers and HIST, and only writes PTR, STREAM and GROUP; the engines that use the tables pay ts_engine_latency instead. |
| 3.2 | `fig3_2_regdecode.svg` | Register decode: the data-bus address map, the 13 registers and their bit layouts *(RTL)* | Every load and store is decoded in one cycle into instruction memory (never a data access), the table window, data memory or one of 13 registers at 0xFFFFF800 + 4·i. Anything else, a misaligned or sub-word access to a register or the table window, a load from a table the controller only writes, or a store to a read-only register is a bus error. DMA_PULSE fires one-cycle pulses, start before done; write-only registers read 0. |
| 3.3 | `fig3_3_readport.svg` | The controller read port: the read port's queue, the in-flight count and the free count *(RTL)* | A store to SRAM_RD_ADDR_QUEUE queues a 32-bit read with the current SRAM_RD_DEST_ADDR, which then advances. The queue drains into the SRAM read queue at the lowest priority. An entry counts against the 64 until its result has been injected, so the free count a program reads never overstates the room; a store with nothing free is dropped and sets condition 3. |
| 3.4 | `fig3_4_sendport.svg` | The controller send port: ROUTER_SET_HDR, ROUTER_SET_HI, ROUTER_SEND_LO and the holding register *(RTL)* | A ROUTER_SEND_LO store captures the header, the high word and its own data into the one-packet holding register; the header's addr then steps by the words the packet writes, and ROUTER_SET_HI clears. The store stalls only while the register is full and not leaving this cycle, so sends run one a cycle unless read returns, which go first, hold the injection point. |

**Part 4. The network**

| # | file | figure | caption |
|---|---|---|---|
| 4.1 | `fig4_1_router_tile0.svg` | The router of tile 0: the router of tiles 1–63 plus a fifth port for C&C | Tile 0's router is the router of tiles 1–63 (figure 4.2) plus a sixth route block for packets from C&C, a from-C&C FIFO (orange) on each of the five outputs, so C&C gets a fair round-robin share of each, and a sixth output, to C&C, with five FIFOs of its own: ten extra FIFOs in all. |
| 4.2 | `fig4_2_router.svg` | The router of tiles 1–63: five route blocks, output queue sets and round-robin selectors | Each route block forwards an arriving packet in one cycle to output FIFOs, one FIFO per feeder; each output's round-robin selector sends one packet per cycle. Routes go horizontal first, then vertical, so to-right and to-left have two feeders each, to-up and to-down four, and eject five: 17 FIFOs. |
| 4.3 | `fig4_3_router.svg` | The router: the route decision, the output queues and round-robin selectors, and the link pipeline *(RTL)* | Each route block decides in one cycle from src, dst and me: horizontal first by the offset table, then vertical, then eject; a broadcast fans out by its arriving direction. Each output takes one packet a cycle from its FIFOs round-robin; a link adds three register stages, so a hop is four cycles. A push into a full FIFO loses the packet and sets router overflow. |

**Part 5. Arrivals**

| # | file | figure | caption |
|---|---|---|---|
| 5.1 | `fig5_1_director.svg` | The traffic director: routing by mode, and how each mode's address is formed *(RTL)* | Every packet is routed by its 4-bit mode: mode 1 to the GATHER engine, 2–4 to the write engine (which forms the address from PTR), 5–7 to port B (which forms memory, word and banks from addr); 0 and 8–15 are dropped with the bad-mode condition. Read-port results are mode-7 packets. Counting happens on acceptance. |
| 5.2 | `fig5_2_rmw.svg` | Incrementing PTR and HIST: read on port A, write back on port B one cycle later *(RTL)* | Each table does one read-modify-write per cycle: port A reads the entry on cycle t, and port B writes the incremented value back on t+1. Port B is shared with the controller, and the increment wins. When the same entry is incremented twice in a row, the read result is ignored and the last value is used. req starts the read and loads the last address register; req(t−1) drives the write. |

**Part 6. The address generator**

| # | file | figure | caption |
|---|---|---|---|
| 6.1 | `fig6_1_ag.svg` | The address generator: STREAM, GROUP and the run in DMA_CONFIG; stream runs and group runs | Every request goes to the SRAM read queue at priority 2, below gather values; a 64-entry token FIFO holds each request's token until its data returns, so packets trail the timetable by a constant while nothing stalls. A stream run walks streams one after another; a group run sends a fixed timetable of groups. |
| 6.2 | `fig6_2_ag_control.svg` | The address generator's control: the run state machine, the stream-run and group-run sequencers *(RTL)* | Start loads the run and begins the read-ahead; the first request goes out when its entries are in; the run ends once its last entry is walked. Streams run one after another; a group run follows a fixed timetable, S_g = S_{g−1} + max(R·P, 1) + gap. A missing table entry holds the timetable on every tile alike; a request the SRAM read queue refuses holds it on this tile alone, which is drift. |
| 6.3 | `fig6_3_ag_readahead.svg` | The address generator, reader side: GROUP and STREAM read ahead of the timetable *(RTL)* | Two readers walk the tables ahead of the timetable: GROUP entries in group order, STREAM entries in visit order (each group's streams, R times over). Two queues of ag_lookahead entries absorb the tables' round trip. The visit side (figure 6.4) pops them and returns a credit for each entry. |
| 6.4 | `fig6_4_ag_visit.svg` | The address generator, visit side: one STREAM entry and one counter make each request *(RTL)* | Each visit takes one STREAM entry and the counter e (the rotation r, or the element j): it sends element e if e < count, from source + (e << z), so the source steps by 1, 2 or 4 chunks per element for 32-, 64- or 128-bit reads. The token carries dst, mode and the mode-5 word to the packets. |

**Part 7. Big SRAM and the read path**

| # | file | figure | caption |
|---|---|---|---|
| 7.1 | `fig7_1_sram.svg` | The big SRAM: 8 areas, one access per area per cycle, no coherence | Each of the 8 contiguous areas serves one 128-bit access per cycle from its own queue, in arrival order. There is no coherence: correctness comes from never reading and writing one buffer in one engine run; ordering between runs comes from waiting for the writes (DMA_SRAM_REMAINING and mem_idle). |
| 7.2 | `fig7_2_readq.svg` | The SRAM read queue and the area interface: the readers' priority, steering, and each area's request queue *(RTL)* | The SRAM read queue grants the highest-priority valid reader, steers its request to the area chunk[19:17], and takes it if that area has room; a read that crosses a row boundary sends its second row the next cycle. Each area has one request queue for reads and writes in arrival order, two pushes a cycle with the write first, and serves one 128-bit row a cycle; writes use per-chunk enables, and reads return the row with its tag and part. |
| 7.3 | `fig7_3_returns.svg` | The return path: the token FIFO, the packet builder and the injection point *(RTL)* | Every read taken pushes a token whose tail index is the read's tag. A read may start at any chunk: its one or two rows return in any order and are merged into its slot in order; the head leaves when both parts are in, as one packet (two for 128 bits), so packets trail the requests in request order. The injection point takes a return packet whenever one is ready, and the send port's packet otherwise. |
| 7.4 | `fig7_4_gather.svg` | The local gather: mode-1 requests to the tile itself, gather values to PTR[me] | The address generator streams the sorted (key, ordinal) words to the tile itself as mode 1; the director hands each to the GATHER engine, whose value reads return as mode-2 gather values that land in order at PTR[me]. Mode 1 is only ever sent to the tile itself (invariant 10). |
| 7.5 | `fig7_5_gather_dp.svg` | The GATHER engine's datapath: the request queue, the start address, the k reads and their tokens *(RTL)* | Each mode-1 request queues only its 20-bit ordinal. At the head, the start DMA_RECORD_BASE_ADDRESS + 2k·ordinal is computed and loaded; the k reads follow, one offered per cycle at priority 1, each stepping the address by 2 and pushing a token that sends the value back to the tile as mode 2. A push at full drops the request and sets condition 8; GATHER busy covers the queue and the request in progress. |

**Part 8. Reference**

| # | file | figure | caption |
|---|---|---|---|
| 8.1 | `fig8_1_params.svg` | Parameters: every depth, width and latency, with its default | Every depth, width and latency the figures use, with its default. Open: the area queue depth, the array's read latency and the physical ready_latency are still to be fixed for the hardware; the token FIFO's 64 entries must absorb the area queueing a phase's placement allows (§4.2). |

Figures 4.1 and 4.2 draw the router at block level: one route block per input, one FIFO per feeder on each output (§4.7, §4.17); figure 4.3 is its RTL. Figure 1.3 hangs each table and configuration register over the engine it drives. Figure 3.1 draws one box per memory, the controller's access (r, w or r/w) on each bus tap, the unit on each memory's other side, and two zones: must be close (what the controller reads) and can be far (what it only writes).

Figures 6.3 and 6.4 show the address generator's read-ahead and visit (§4.2, verified in meshsim). Where the spec is silent, they show the design as worked out with the lead: the `STREAM` reader's pointer into the `GROUP` queue, the request offered on its visit, and the token's fields.

## Conventions

- **Streams:**
  - green solid: the SRAM read queue and its returns;
  - blue long-dash: the SRAM write queue;
  - purple: the controller's paths (port A, port B, the send port), with purple dotted for engine-to-table-store traffic;
  - orange short-dash: the global nets (RUN/HALT, GO, READY) and control;
  - red: conditions and the error line.

  Every line is also labelled, so the figures read in greyscale.
- **Status:** an unmarked item is decided.
  - purple pill, or purple dashed outline: *proposed*, not yet ruled on;
  - orange pill, or orange dotted outline: *open*;
  - blue pill: *likely to change* (the spec's own marking, for `STREAM`'s gap-after field).
- **Numbers** keep the spec's markers: [estimate], [model], [meas: meshsim] and [built].
- **RTL figures** draw clocked registers with a wedge on their bottom edge, junctions as dots (lines that cross without a dot don't connect), and end with an interface table.
- **Not to scale.** Figure 2.1 is schematic. Field widths in figures 1.4 and 6.1 are to scale; register bit positions follow `mesh.h` (figure 3.2).
- **Placeholder names.** Example placements are illustrative, not prescribed: areas 2 and 3 in figure 7.1, areas X, Y and Z in figure 7.4.
- **Close and far** (figures 1.3 and 3.1) follow §12: the control island holds the controller, the instruction and data memories, the table store, the counter store and the peripheral registers; the engines, the router's queues and the big SRAM are outside it, and the GATHER engine sits by the big SRAM.

## Regenerating

Run `src/build.sh`: every `src/figP_N_*.py` writes its SVG into `svg/` (one figure: `cd src && python3 fig1_3_tile.py`), then each SVG is rendered into `png/` with cairosvg. `fig4_2_router.py` reuses the drawing code in `fig4_1_router_tile0.py`. The generators need only Python 3's standard library; the PNGs need cairosvg (`pip install cairosvg`). `src/common.py` holds the drawing helpers, palette and markers; `src/rtl.py` holds the RTL helpers (gates, mux, register, table, waveform) that the later figures share.

## Open items drawn as open

- **Above the chip** (figure 1.1): the cards, the card and controller FPGAs, their links, and each chip's DDR4 (§13 item 10).
- **Big SRAM:** the areas' memory-compiler numbers and read latency (§13 item 1), the area queue depth for the hardware (§13 item 2; figures 7.2, 8.1), write combining (§13 item 3; figure 7.1), and queueing against the timetable (§13 item 4).
- **The address generator:** gapless one-word exchanges and their link-queue peaks (§13 item 7; figure 6.1).
- **The memory subsystem's link to C&C** (§13 item 9; figure 1.2). It hangs off C&C, with no mesh address (§7).
- **The physical ready_latency** (§13 item 8; figure 8.1).
