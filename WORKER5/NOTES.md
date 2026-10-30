# WORKER5 notes (spec v1.2)

Working notes, kept so the work survives a context compaction. Newest status at the top.

## Status

- All passes done; CHANGES-v1.2.md, QUESTIONS.md and REPORT.md written (2026-10-29). Next: round 2, check WORKER6's recipes against the spec.

## Owner's adjustments to the plan (2026-10-29)

1. **Numbering:** keep v1.1's numbers; a removed section keeps a one-line stub pointing to
   `CHANGES-v1.2.md`; new subjects take new numbers at the end of their chapter (for subsections: at the end
   of their section).
2. **The replayer's low-water lines become counted credits** (log 10, W3-24), written into 4.19, beyond
   moving it onto the new map and window.
3. 35 wins over 21 and 34; anything open goes in as *(Proposed)* with a question in `QUESTIONS.md`.

## Decisions digest (the log, as it finally stands)

- **Link word** 256 bits: payload [127:0], address [159:128] (region [31:24], byte offset [23:0]), size
  [161:160], counter [164:162], mode [167:165], src [174:168], dst [181:175], reserved [255:182] (sent 0,
  ignored, stored in full). (2, 30)
- **Modes:** 0 write at address; 1 big-SRAM by source at `PTR[src]`; 2 bucket `PTR[digit]`; 3 augmented
  `PTR[src]`; 4 gather request (local; never counted; counter passed to its values); 5-7 reserved, discarded
  at the destination, no error, no decrement; meshsim flags. Modes 1-4 ignore the address. (6, 26)
- **Counters:** field 0 uncounted, 1-4 `WRITE_COUNTER_1`-`4`, 5-7 accepted as uncounted (meshsim errors).
  Counted for modes 0-3. Decrement K cycles after the director accepts (delay line). K a parameter, default
  8; must be >= max(2L+3, L+4, `cm_latency`, `rp_latency`) (meshsim errors below). Additive arming; arming
  clears the done event; unexpected arrival at the decrement; meshsim also reports the stray at acceptance.
  (3, 30, 32, 36)
- **Map:** 0x00 data memory 32 KB; 0x01 program memory 32 KB (fetch and load; store = bus error; port B
  writes); 0x02 boot ROM (fetch and load; reset vector 0x02000000); 0x03 big SRAM; 0x04-0x06 peripherals
  1-3; 0x07-0xFE reserved; 0xFF controller only (registers 0xFFFFF800-0x9FF, windows FA00/FC00/FE00, table
  window 0xFFFF2000-0x35FF proposed). Offsets in bytes; big SRAM engines keep 21-bit chunk addresses
  (`STREAM_SRC`, `PTR`, `SRAM_RD_ADDR_QUEUE`, `DMA_RECORD_BASE_ADDRESS`); packet address of a chunk =
  0x03000000 | chunk << 2. Controller can't load/store big SRAM or peripherals. (5, 9, 11, 22, 29)
- **Alignment:** every 64/128-bit access naturally aligned, every region, checked in hardware (bad packet,
  `ERRORS` [1]). Spanning gone. Out of bounds unchecked (aliases; big SRAM undefined); meshsim errors.
  Misaligned engine access: dropped, still counted, [1]. Misaligned read-port read: not performed, packet
  still sent with payload 0, [1] at the issuer. Bad-packet table in 26. (26, 32, 36)
- **Hardware vs meshsim** (27): HW checks overflows, unexpected arrival, done early, drift, alignment,
  region decode. meshsim: out of bounds, reserved modes, counter codes 5-7, command/reply conventions,
  uncounted = mode 0, invariants, word 0 of data memory, traps with no `ERRORS` bit (ecall, ebreak, fetch
  outside PM/ROM, misaligned jump, trap in handler), K below formula, window access after done (32), credit
  to full counter, pulses while busy, DMA regs changed mid-transfer, reserved register words.
- **Registers:** controller block (11 + 30): PARAMS 800, STATUS 804, ERRORS 808, PULSE_DONE 80C, PULSE_DMA
  810, 814 reserved (was PULSE_GO), EVENTS 818, EVENTS_PEEK 81C, EVENTS_MASK 820, EVENTS_REQUIRED 824,
  ROUTER_HDR 828, ROUTER_ADDR 82C, ROUTER_SET_W0-2 830-838, ROUTER_SEND_32/64/128 83C-844, REQUEST 848-854,
  DMA_CONFIG 858, DMA_RECORD_BASE_ADDRESS 85C, WRITE_COUNTER_1-4 860-86C, SRAM_RD_ADDR_QUEUE 870,
  SRAM_RD_DEST_ADDR 874, 878-9FC reserved; 9FC trace word (sim). NOTE 11's table lists 848-854 for both
  GET and SET sides: GET_W0-2+RECEIVE are loads and SET_W0-2+SEND stores at the same four addresses
  (as v1.1's 0x858-0x864 pairing). Check v1.1 8.10.
- **PARAMS** [2:0] present mask, [26:20] endpoint id, [31:27] size-1 in 256 KB. **STATUS** [0] rd_running
  [1] rd_done [2] GATHER busy [3] send_busy [4] done [5] command landed [6] request_busy. **ERRORS** [0]-[10]
  as v1.1, [11]-[13] peripherals 1-3 (live), [14] receive underflow. (18)
- **EVENTS** [0]-[3] counter 1-4 done, [4] counters done, [5] GO, [6] command landed, [7] DMA run done, [8]
  request FIFO no longer full, [9] request pending (live), [10]-[12] peripherals (live). Mask resets to
  [5]|[4]. (16, 30)
- **Standard window** words 0-9 (10); C&C 1's words from 32 (28).
- **Traps terminal** (20); handler report set fixed (21); park in a plain loop.
- **Errors -> toast** (35, 36): toast = C&C 1's ERRORS != 0 or watchdog expired, latched until reset; hard
  and soft reset inputs; nothing recovers on chip. LED software pin.
- **Boot** (31, 36): ROM exact; tiles via READY/GO; C&C 2/3 on command landed, report "started"; C&C 1 takes
  the C&C 2/3 branch, outside world lands its overlays and first command.

## Section map (v1.1 -> v1.2)

See the checklist below; R = rewrite, U = update, K = keep, X = removed (stub), N = new number.

- 0 front matter: R (v1.2 summary, pointer to CHANGES-v1.2.md)
- 1 conventions: U (bytes, chunks, names, errors)
- 2 system: R (C&C 1-3 tile instances at 64-66, engines as peripherals, nets: GO, READY, error lines, reset
  inputs, toast and LED pins); 2.1 endpoints U; **N 2.2 the 32-bit address map**
- 3.1 R; 3.2 U (256 flops); 3.3 U (targets; endpoint 73 gone)
- 4.1 U (spanning, alignment, out-of-range, mem_idle); 4.2 U (STREAM layout, modes, d); 4.3 U; 4.4 U; 4.5 U;
  4.6/4.7 U; 4.8 R; 4.9 U; 4.10 U; 4.11 R; 4.12 R; 4.13 R; 4.14 U; 4.15 K; 4.16 U; 4.17 R (subsections:
  4.17.5 mode 9 -> writes to a peripheral's region; 4.17.7 X; 4.17.9 the DMA); 4.18 U; 4.19 U (+credits);
  4.20 U (UART now a peripheral)
- 5 U; 6.1 R (reset); 6.2 R (boot ROM, boot sequence); 6.3 U; 6.4-6.5 U; 6.6 R (C&C 1-3, C&C 1's peripheral);
  6.7 U; **N 6.8 errors and the toast pin** (if 6.1 gets too long)
- 7 U; 8 R (complete tables; 8.12 X; N 8.14 C&C 1's peripheral; N 8.15 simulator-only words)
- 9 R per 27; 10 U; 11 U; 12 K; 13 R; 14 K

## Checklist

- [x] Pass 1: front matter, 1, 2, 3
- [x] Pass 2: 4.1-4.16
- [x] Pass 3: 4.17-4.20
- [x] Pass 4: 5, 6, 7, 8
- [x] Pass 5: 9-14 (13: status table for 1-116, new items 117-133)
- [x] CHANGES-v1.2.md, QUESTIONS.md, REPORT.md

## New open-item numbers (section 13 continues from 116)

- 117 storage of the reserved link bits (trim later, power) (3.1)
- 118 sending peripherals' router ports (W2-31, deferred) (4.17.9)
- 119 file system: tag windows, window protocol, request target 3, request layout (deferred) (3.3, 6.7)
- 120 table window address 0xFFFF2000 (proposed, log 11) (4.10, 8.9)
- 121 Q1: CSR list and reset values, mtvec (Tony) (4.11)
- 122 handler report set: ops, order, count (4.13)
- 123 the replayer's window: STATUS/ERRORS/EVENTS bits, words 32-33 names (REPLAYER_STATUS, REPLAYER_OPCODE) (4.19.9, 4.19.11)
- 124 soft reset's keep list (Tony) (6.1)
- 125 candidate: signed counters (6.2)
- 126 requests dropped at the root: hardware record? (6.7)
- 127 controller memories at 32 KB: macros, area (~0.15 mm2 est), one-cycle access (11, 12)
- superseded: items 50, 51 (credits), 54 (HALT row), 87 (sender), 48 stays (rp_latency)

## Owner's rulings, round 2 (2026-10-29)

- W5-01, W5-06: as written. W5-15, W5-17: decided in the log (dd6c651); spec updated, (Proposed) dropped.
- W5-13: no reset hold; error-line flops reset with the whole chip; spec says why (6.1).
- Log also: stream bound slip (4.19.15 fixed: 4.3 Mbit); 1x4 test chip added to 33's deferred list (13).

## Questions raised so far (mirror of QUESTIONS.md)

- W5-01 command and reply blocks at the top of the 32 KB data memory (0x7FC0, 0x7BC0) (4.11, 4.12)
- W5-02 a write through a misaligned PTR: does the pointer still advance? proposed yes (4.9)
- W5-03 ROUTER_ADDR advances for mode 0 only (as v1.1 for modes using addr) (4.11)
- W5-04 loads from program memory and the boot ROM are single-cycle (4.11)
- W5-05 32 KB memories as four 2K x 32 banks (4.11)
- W5-06 write-engine input overflow stays P (W2-REVIEW-2 said it becomes H; not in the log) (4.13)
- W5-07 read-port request-queue entries carry the counter (setup register may change between reads) (4.3)
- W5-09 replayer: bit/word assignments (W3's table) and the names REPLAYER_STATUS/REPLAYER_OPCODE kept from v1.1 (4.19.9)
- W5-10 replayer: the "part" bits are byte-offset [22:20] (mechanical from addr[20:18]) (4.19.10)
- W5-11 4.16: a controller takes one packet a cycle from tile c's fifth port; which port of its router is deferred (item 118)
- W5-12 watchdog counts core cycles (6.1)
- W5-13 reset hold >= error_latency (from log 21; still needed with outside reset?) (6.1)
- W5-14 LED resets to 0 (6.1)
- W5-15 big-SRAM staging is always counted (31/W2-27) though 17 lists "boot and staging images for big SRAM" among uncounted traffic (6.2)
- W5-16 memory system's errors: a peripheral error of C&C 2 -> toast (6.7)
- W5-17 requests name the data and reply counters (25 leaves open; deferred item 119) (6.7)
- W5-18 C&C 1's peripheral word numbers and EVENTS bits (8.14); WATCHDOG load returns the count
- W5-19 exit word at 0xFFFFF9F8 (8.15)
- W5-20 controller block addresses (log 11 says proposed) (8.1)
- W5-21 budget: controller memories 32+32 KB estimated ~0.15 mm2; total 0.52-0.61 mm2 (11)
- W5-22 (merged into W5-09) WORKER6 names words 32/33 RUN_STATUS/PRESENTED
- W5-08 region decode for bad packets, size 11, modes 1-4 ignore address, mode 3 at 128: log 26's (proposed) rows kept proposed (3.1)
