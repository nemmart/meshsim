# WORKER6 report, round 2: the recipes brought up to D39-D42

**Ready for WORKER5's check.** Everything is on `main`. The recipes, the conventions, the draft header and the
check sources follow the decisions log through section 42. The book is still generated from the compiled code:
`WORKER6/check/build.sh` compiles every file with no warnings (`-Werror`), and `check_book.py` confirms all 44
code blocks in `wip/sw/RECIPES.md` equal their regions. Nothing has run (no v1.2 simulator).

## 1. What changed, by section

**D39, the replayer's status words.** `ReplayerRegs` has `ROWS_RUN` (32), `EXPANSION_OFFSET` (33) and
`EXPANSION_OPCODE` (34), one field each. `RP_NEXT_OFFSET()` is gone. R26's loop passes `EXPANSION_OPCODE` and
`EXPANSION_OFFSET` to the program's template lookup; R25 lists the three words.

**D40, reply addresses and seven counters.**
- **Registers:** `WRITE_COUNTER[7]` at 0x860-0x878, `SRAM_RD_ADDR_QUEUE` at 0x87C, `SRAM_RD_DEST_ADDR` at
  0x880, reserved 0x884-0x9F4; `_Static_assert`s check the offsets.
- **`EVENTS` renumbered** in the header (`EV_CTR(1-7)` [6:0], `EV_CTRS_DONE` [7], `EV_GO` [8], `EV_COMMAND`
  [9], `EV_RUN_DONE` [10], `EV_REQ_ROOM` [11], `EV_REQ_PENDING` [12], `EV_PERIPH(1-3)` [15:13]). The code uses
  only the names, so it needed no change; every bit number in the book's prose and the sources' comments was
  renumbered, and CONVENTIONS 5 has the new table.
- **The counter convention, revisited for seven** (CONVENTIONS 5; the hardware treats them alike):

  | counter | use |
  |---|---|
  | 1 `CTR_SRAM` | big-SRAM writes: local runs, all-to-alls, staged data |
  | 2 `CTR_CM` | controller-memory writes: block copies, vectors, read-port results |
  | 3 `CTR_REPLY` | tile-to-tile replies (the reply block, now the default rather than a rule) |
  | 4 `CTR_PERIPH` | writes to the tile's own peripherals: the replayer's refills and state |
  | 5-7 `CTR_REQ(k)` | request slot *k* = 0-2: its reply, and any data it brings back on the same counter |

  So D40's replayer tile (an LPDDR fetch, a refill, replies, up to three file-system requests, each waited on
  separately) fits: the fetch is a request on a slot, the refill on 4, replies on 3.
- **Reply addresses: request slots** (R21's `request_issue`/`request_wait`; rule 11; CONVENTIONS 6b). Slot
  *k* owns counter 5 + *k* and its own 8-byte reply buffer, `req_reply[k]`, whose address goes in the request's
  word 1. Three slots cover C&C 2's limit of three (D38), so by convention a tile has at most three requests
  outstanding in all. This answers F15. `result_to_cc1` (the self-test) uses slot 0. C&C 1's `drain_requests`
  replies to the address and counter each request names.
- **The handler** reports counters 5-7 as one more request: **24 reports**. The peripheral reports are 15-23.

**D41, the remaining findings.**
- **F6:** R23 and CONVENTIONS 5 say `rd_running` falls in the same cycle as DMA run done, so the poll covers
  the local side exactly (and still nothing about arrivals).
- **F12:** `mcycle()` is in the draft header (`csrr mcycle`, the low 32 bits). C&C 1 times the GO settle,
  `stage_delay` and the toast path's drain with it.
- **F13:** the ROM's tile branch keeps the reset mask, so it never consumes "command landed" [9]. A comment
  in `bootrom.S` and R22's walk say why.
- **F14:** **R22's boot is rewritten.** C&C 1 streams every image from its big SRAM by its address generator,
  a packet per cycle: *d* = 126 to the tiles, *d* = 65 or 66 (absolute) to C&C 2 and C&C 3. Boot part 2's
  staged data is also streamed with *d* = 126, counted on counter 1, `stage_delay` after GO by `mcycle`.
  - **The first command rides in the same run**, as its last stream: one 128-bit packet at slot 0 holding the
    command word and, in slot 1, the entry address, prepared in C&C 1's big SRAM (`put_command`). A packet
    covering slot 0 sets "command landed" (D6). From one source, after every overlay packet (S2), it lands
    last.
  - **Sending it from the send port after the run would not be safe as specified** (new F17, below).
- **F9-F11, F16:** marked accepted or answered; R24's report-set heading says the spec defines the set.

**D42, WORKER5's questions.** The header follows them: `ROUTER_ADDR` advances for mode 0 only (the recipes
send only mode 0 from the send port); each read-port queue entry keeps the counter `SRAM_RD_DEST_ADDR` held
when it was queued; C&C 1's words 32-45 and `EVENTS` [2]-[5] as the draft already had them; the exit word at
0xFFFFF9F8; `WATCHDOG` in core cycles (R27's comment); the replayer's part-select at byte offset [22:20]. The
corresponding "(proposed)" marks in the header are gone.

## 2. Findings

`WORKER6/FINDINGS.md`: **F1-F16 are all answered** (D37, D38, D40, D41), each with its answer. **One new, open:**

- **F17 (low): a command from the send port isn't ordered after a stream's packets.** DMA run done says the
  run's last table read is complete, not that its packets have left, and the send port is the tile's other
  injection source. Spec 3.2's "in the order sent" per source and destination doesn't say whether one tile's
  two injectors are one source. R22 avoids the question by putting the command in the run. **Suggested:** one
  sentence in 3.2 either way.

## 3. What WORKER5 should check against the spec

1. **The controller block** (8.1): the header's offsets, the 93 reserved words, and `SIM_EXIT`/`TRACE` at
   0x9F8/0x9FC.
2. **`EVENTS`** (4.12): the header's `EV_*` and CONVENTIONS 5's table; `EVENTS_MASK`'s reset GO | counters done
   = 0x180.
3. **The replayer's words** (4.19.9, 8.11): `ROWS_RUN`, `EXPANSION_OFFSET`, `EXPANSION_OPCODE` at 32-34; the
   events [0] run done, [2]-[5] credits, [6] new expansion; R26's loop against 4.19.9's.
4. **Requests** (3.3, 4.18, 6.7): reply address and counter per request; 1/3/1 outstanding; error reports
   excepted. **The request layout below the target is still a placeholder** in the draft (`REQ_CC1`: op
   [17:10], reply counter [20:18], argument [31:21]; word 1 the reply address). The spec or C&C 1's firmware
   should define it; the recipes need only "the request names a reply address and counter".
5. **The handler's report set** (4.13, item 122): R24 lists the 24 in order, three data words each. Per slot
   it reads seven of the ten standard words (not `EVENTS`, which consumes, nor the write-only pulses).
6. **The boot** (6.2): D37.1's ROM (the [9] test in the load that consumed GO; `done` again otherwise); boot
   part 2 arming first, then `mtvec`, `mie.MEIE`, `MIE`; C&C 1's streams with *d* = 126 and the command in the
   same run (F17).
7. **The read port** (4.3, 8.8): a per-entry counter, data memory only, a misaligned read still counted.
8. **Rule 11 and CONVENTIONS 6b's slots** are software conventions; the spec need only not contradict them.

## 4. Unchanged from round 1

The data-memory layout (0x00-0x0F reserved, image from 0x10, reply block 0x7BC0, command block 0x7FC0) and
the table window at 0xFFFF2000 are still the draft's proposals. `DMA_CONFIG` and `GROUP` still have v1.1's
layouts. `REPORT.md` (round 1) still describes the recipes ported, added and retired; this report covers only
what changed.
