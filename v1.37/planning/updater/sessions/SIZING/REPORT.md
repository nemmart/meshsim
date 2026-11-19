# How big are the two controllers? (v1.37, SIZING)

The figures below are counted from the code on `main` (`v1.37_wip/src/units/`) by the scripts in this folder.
`COUNTS.md` and `FW.md` are their full output: every register, operator, control-word row and firmware path. To
redo any figure, change it in `count.py` (the costs in `COST`, the low and high assumptions in `ASSUME`) or in
`fw/*.S`, and run:

```
python3 count.py; python3 fw_cycles.py; yosys/run.sh   # the last needs yowasp-yosys (pip)
```

Nothing in `v1.37_wip/` or `spec/` was changed.

## The answer

| | states | control word | flip-flops | logic between them, kGE (low–high) | memory, bits |
|---|---|---|---|---|---|
| **the cache's controller** | 12 named (walker 4, miss handler 8) in 5 flip-flops; 40 flip-flops of state in all | 110 bits; 85 distinct words | **6,652** (spec 6,876; lean 4,157) | **17.8–46.6** | 4,304,896 (512 KiB data, 13.5 KiB tags) |
| **the Updater's controller** | 18 named in 5 flip-flops, 8 with the fault's level and file; 26 in all | 95 bits; 93 distinct words | **4,193** (spec 4,193; lean 4,017) | **18.8–34.9** | 40,960 (4 KiB page memory, 1 KiB free page queue) |
| the AXI arbiter | none named; 45 flip-flops of slot, list and turn state | 49 bits; 11 distinct words | **1,539** (spec 1,539; lean 1,155) | **3.8–8.2** | none |
| the tile's core | (a pipeline, not counted as states) | | **about 1,900–2,000** | **23.1–31.5** | 526,336 (IMEM, DMEM, boot ROM) |

At 5.67 GE a flip-flop, the flip-flops come to 23.6–37.7 kGE for the cache's controller, 22.8–23.8 for the
Updater, 6.5–8.7 for the arbiter and 10.9–11.4 for the core. **So the totals are 41–84 kGE for the cache's
controller, 42–59 for the Updater, 10–17 for the arbiter, and 34–43 for the core.** The published figure for an
Ibex RV32IMC with a one-cycle multiplier is 33–36 kGE.

| one item of work | firmware on the core, cycles | the controller, cycles | what it must keep up with |
|---|---|---|---|
| Updater: a raw value into the page held | 25 (general path); 5.5 (a long run, unrolled) | 1 | the link: 8 cycles a raw update word |
| Updater: an LRU-coded value (the fixtures' mix) | 51 | 1 | the link: 2.2 cycles a code |
| Updater: a header word | 15 raw; 72 LRU-coded | 1 | 8 cycles |
| Updater: one index level of a page fault, not counting LPDDR's waits | 7,459 | about 5 | |
| cache: a look-up of a depth-2 file, both lines cached | 105–214 | 6 | |
| cache: a fill read of a depth-3 file, all lines cached | 482 | 10 | |

- **What dominates.** In the cache's controller it is the 1,024-bit line width. Five line registers hold 5,120 of
  its 6,652 flip-flops, and the 1,024-bit multiplexers around them are most of its logic. In the Updater it is the
  LRU decoder: its 2,048-flip-flop table, the table's 32:1 read, and the bit buffer's two shifters make up about
  25–35 of its 42–59 kGE. Like for like, low with low and high with high, the cache's controller is 1.2 to 2.0
  cores and the Updater 1.2 to 1.4. The big item is
  memory, and that is the cache itself: a firmware cache needs the same 512 KiB.
- **How sure.** The flip-flop counts are exact from the code. The logic is a range: synthesizing the largest
  pieces with yosys gave figures 5% to 22% above my operator costs, which is inside the range. The core is the
  least sure, about ±30%: it is not modelled gate by gate, so its figure is an estimate by parts, set against
  Ibex.
- **Firmware.** A core keeps up only with raw update words in long runs, where it would be busy 0.69 of the link's
  time on top of the copying overhead measured today. Short runs need about 3 cores' worth of time and LRU-coded
  frames about 23. The cache in firmware is 17 to 48 times slower than the controller on hits.

---

## 1. How it was counted

**From the code, at the signals' widths, at `LPDDR_ADDRESS_BITS` 32.** Each register in `count.py`'s `REGS`
carries what it holds, where it comes from (its number of sources), and where my count and the specification's
differ. The parts that are not hardware are listed apart (`NOT_HW`, printed in `COUNTS.md`). These are the
statistics, the stale-line check, invariant 36's `pages_seen` (2²³ bits), the UB checks the simulator stops on, and
two wires the code keeps as members (the walker's tag row and set).

**The costs, in NAND2 equivalents (GE).** They are the Nangate 45 nm Open Cell Library's X1 cells over NAND2_X1's
0.798 µm². I checked DFF_X1's 4.522 µm² in its databook; the others are the same library's, from memory.

| primitive | GE | used for |
|---|---|---|
| flip-flop | 5.67 | every register bit (an enable is counted apart, as a mux bit) |
| full adder | 5.33 | an adder or subtractor bit |
| half adder | 3.33 | an incrementer or decrementer bit |
| comparator bit (equality) | 3.0 | an XNOR and its share of the AND tree |
| comparator bit (magnitude) | 4.0 | a carry chain without sums |
| multiplexer bit (2:1) | 2.33 | an *n*:1 multiplexer bit costs *n* − 1 of them |
| AND/OR bit; XOR bit; constant compare bit | 1.33; 2.0; 1.0 | gating; inversions; zero and all-ones detects |
| clock-gating cell | 7.0 | one for each clock-gated register (low only) |

**Low and high.** Each switch below is named in `count.py`'s `ASSUME`, and either figure can be redone with
others.

| | high (as coded) | low (lean) |
|---|---|---|
| holding a register | every enabled register holds through a mux bit | registers of 8 bits or more are clock-gated |
| the cache's look-up ports | four, as specified | one: v1.37's only client is on port 1 |
| the cache's line registers | five | three: the walker's line and the miss handler's buffer dropped |
| copies of other registers | kept | dropped (listed under each unit) |
| the LRU table's clear at `START` | an AND gate a bit | a valid bit an entry |
| the page memory's read register | 128 flip-flops | inside the SRAM macro |
| page address plus offset | an adder | a concatenation: pages are multiples of 32 units (17.10.9) |
| the arbiter's burst cutter | one in each of the eight slots | one a direction |
| the arbiter's R data register | one for each client (4 × 128) | one shared |
| next-state and output logic | 8 GE a control-word row | 4 GE a row |
| buffering the wide selects | 15% added to the logic | none |

**The next-state and output logic** is costed from the control-word tables of section 2. Each row is one path
through a machine's code in one cycle. It is costed at 4 or 8 GE for its decode, plus 1.33 GE for each signal bit
it asserts, plus 3 GE for each one-bit flag register. It is 1 to 1.5 kGE for each controller, a few percent of the
total, so a large error in it moves little.

**The cross-check** (`yosys/`, `crosscheck.md`). I synthesized three Verilog models with yosys (0.71, the yowasp-yosys build)
to plain gates and counted the cells at the same costs. Each model is a faithful copy of the code's logic.

| model | yosys, GE | count.py, GE |
|---|---|---|
| the Updater's decoder: bit buffer, refill, LRU table and its move to front (logic only) | 10,719 | 10,198 |
| the same decoder's flip-flops (2,249 in yosys, 2,250 here), with their enables and clears | 20,515 | 20,671 |
| the cache's set logic: hit compare, victim, true-LRU touch, tag row written | 2,820 | 2,321 |
| the cache's entry from a line, and an update's patch into it | 5,029 | 4,737 |

My operator costs come out 5% to 22% under yosys's. That is inside the low-to-high range, and it is why the high
figure adds 15% for buffering.

## 2. The cache's controller (`cache_ctl.h`, `cache_ctl.cpp`; 23.6.5)

### Its states

| machine | its states (code) | state flip-flops |
|---|---|---|
| **walker** | idle, compare, read (`W_DATA`), reply (`WState`) | 2, plus its level *k* (2) and the discard's line count (6) |
| **miss handler**, with the **AXI reader** | idle, read the victim, hand over, wait, fetch, fill, install, fail (`MState`). The reader is its fetch and fill: `arvalid`, the beat count | 3, plus the beats in (4), `arvalid`, the copy-made bit and the error bit |
| **AXI writer** and its B side | implicit: idle, or busy with the address taken or not and beat 0 to 8 | 6 (busy, the address taken, the beat) and the queue of kinds (9) |
| **update slot** | implicit: empty or full | 1 |
| **front end** | for each of six sources, implicit: empty, waiting, waiting for the miss handler, waiting for the slot, parked, behind a write (fill's read only) | 4 for each look-up port, 3 each for fill's read and write, the round-robin pointer (3), the miss's source (4): 29 |

That gives **12 named states in 5 flip-flops**, and 40 state flip-flops counting the implicit machines. The
specification's lists (23.6.5) match the code's: four walker states and eight miss-handler states.

### Its control word

These are the signals the state logic drives into the block each cycle. The `_op` fields are a register's hold,
load, clear or step. The `_sel` and `_src` fields are multiplexer selects. `_re` and `_we` are memory enables. The
handshake outputs are `rep_valid`, `frr_valid`, `fwr_valid`, `ar_op` (which is `arvalid`), `awvalid`, `wvalid`,
`wlast` and the readies.

- **walker, 51 bits:** `next` 2, `pick` 1, `k_op` 2, `dk_op` 2, `cur_sel` 2, `x_ld` 1, `x_base` 2, `x_off` 2,
  `way_ld` 1, `own_op` 2, `line_op` 2, `pend_ld` 1, `tag_re` 1, `tag_we` 1, `tag_src` 2, `data_re` 1, `data_we` 1,
  `data_wsrc` 1, `data_1lane` 1, `rep_valid` 1, `rep_pend` 1, `rep_status` 3, `rep_level_k` 1, `rep_entry_e` 1,
  `frr_valid` 1, `frr_status` 2, `frr_zero` 1, `fwr_valid` 1, `fwr_status` 1, `req_free` 1, `req_wait` 2,
  `req_park` 1, `req_failclr` 1, `mh_start` 1, `mh_alloc` 1, `wr_upd` 1, `slot_ld` 1, `err_set` 1.
- **miss handler, 24 bits:** `next` 3, `ld` 1, `data_re` 1, `copied_op` 2, `buf_src` 2, `got_op` 2, `err_op` 2,
  `ar_op` 2, `wr_wb` 1, `tag_we` 1, `tag_fail` 1, `data_we` 1, `guard_ld` 1, `fail_owner` 1, `wake` 1,
  `prio_set` 1, `err_set` 1.
- **AXI writer, B and the update slot, 21 bits:** `busy_op` 2, `aw_op` 2, `beat_op` 2, `buf_src` 2,
  `addr_src` 1, `kind_push` 1, `kind` 1, `kind_pop` 1, `slot_reply` 1, `slot_err` 1, `slot_clr` 1, `wake_slot` 1,
  `mh_b` 1, `mh_b_err` 1, `awvalid` 1, `wvalid` 1, `wlast` 1.
- **front end, 14 bits:** `ld` 6 (a request taken at each port), `ready` 6, `rr_ld` 1, `prio_clr` 1.

**In all 110 bits.** The rows are the code's paths through each state in a cycle (`COUNTS.md` lists every one).
The walker has 45 rows and 44 distinct words, 41 without the next state. The miss handler has 24 rows and 24 words.
The writer and slot have 13, and the front end has 4 for each of its six alike sources.

That is 85 distinct words. **A table would hold them** (the walker's would be 44 × 51 bits), but plain logic is
smaller. Each word is a handful of asserted bits under a decode of the state and two or three conditions, about
1.0 to 1.4 kGE for all of it. These machines run concurrently, so the block's control word is the four
concatenated, not a product of them.

### Its registers

| part | as coded | lean | the specification (23.6.5) |
|---|---|---|---|
| the look-up ports' request registers, 4 × 135 | 540 | 135 | 540 |
| fill's read register | 103 | 103 | 102 |
| fill's write register (with its 1,024-bit line) | 1,127 | 1,127 | 1,127 |
| front end | 7 | 7 | 7 |
| walker (its line register 1,024) | 1,141 | 117 | 1,358 |
| miss handler (its tag row 216, its buffer 1,024) | 1,321 | 264 | 1,321 |
| AXI writer (its line 1,024) | 1,064 | 1,064 | 1,070 |
| update slot | 80 | 71 | 81 |
| the tag-write guard, the error | 11 | 11 | 11 |
| the outputs (the fill read response's 1,024 bits of data among them) | 1,258 | 1,258 | 1,259 |
| **in all** | **6,652** | **4,157** | **6,876** |

**Where I differ from the specification's 6,876 (−224):**

- **−216: the walker's tag row is not a register.** The tag memory's output register holds the row until the next
  read, and the walker is its only reader (`tags_.read` is called only from `issue_tag_read` and `step_cmp`). The
  code copies it into `row_` in the same call that uses it.
- **−1: the walker's dirty bit (`wdirty_`) is only for the stale-line check,** the simulator's (23.6.11).
- **+1: fill's read register has `behind_write`,** which 23.6.4's ordering rule needs and 23.6.5's 102 leaves out.
- **−6: the AXI writer's line address needs 25 bits, not 31.** It only feeds AW's address, which 23.6.5 itself
  counts at 25.
- **−1: the slot's source is a look-up port, 2 bits.** **−1: the fill read response's status is 0 to 3, 2 bits.**

**What a chip could drop besides (the lean column):**

- the walker's line register and the miss handler's buffer, 2,048 bits. The update's patch can be made on the way
  to the AXI writer, and R beats written lane by lane into the data memory, which has 8 lane enables;
- three look-up ports, 405 bits: ports 0, 2 and 3 have no client in v1.37;
- copies: the miss handler's set (9) and its victim's tag, valid and dirty (24), which its tag row holds, and the
  slot's set (9);
- two entries of the queue of kinds, 4 bits: at most one update and one write-back are ever outstanding.

### Its datapath operators

These are all in `COUNTS.md`. The largest, high:

| operator | GE |
|---|---|
| the registers' input multiplexers and holds (9,611 mux bits; 8,192 of them around the four 1,024-bit line registers that hold) | 22,394 |
| the data memory's write data, 3:1 of 1,024 bits (a fill write's line, the patched line, the miss handler's line) | 4,772 |
| the update's patch, 32 bits into the line (1,024 mux bits) | 2,386 |
| the entry out of the line, 32:1 of 32 bits | 2,311 |
| the W beat, 8:1 of 128 bits | 2,088 |
| the fill read response gated to zeros, 1,024 bits | 1,362 |
| the tag compares (8 × 22), the victim, the true-LRU touch, the tag row written | about 2,300 |
| the next-state and output logic | 976–1,436 |

**The gates: flip-flops 23.6–37.7 kGE, logic 17.8–46.6 kGE.** The high figure is the code's structure with an
enable flop on every register. The low figure clock-gates the wide registers, keeps three line registers and one
look-up port, and adds no buffering. **The 1,024-bit width dominates both:** the line registers, their holds and the
1,024-bit operators are about 60–65 of the high figure's 84 kGE and about 30 of the low figure's 41.

## 3. The Updater's controller (`updater_ctl.h`, `updater_top.h`; 17.10)

### Its states

| machine | its states | state flip-flops |
|---|---|---|
| **the run** | idle, stopped, check, root read, root write, next, half read, fill, flush, flush read, flush write, flush update, look-up, level, load, take, new write, new update (`State`, 18) | 5, plus the **fault walk's** position: the level (2) and the file (1) |
| **decoder** | implicit: the bit count (8), a run of values open or not, the output register full or not | 10 |
| **AXI channels and the look-up port** | implicit: AR offered, a read in progress; AW offered, a row pending, W valid, a write awaiting B; a request offered, a reply awaited | 8 |

That gives **18 named states in 5 flip-flops**, 8 with the fault walk's position, and 26 in all. The
specification's table (17.10.5) lists the same 18.

### Its control word

- **the run, 70 bits:** `next` 5, `level_op` 2, `side_op` 2, `look_ld` 1, `src_ld` 2, `pm_ok_op` 2, `pm_src_ld` 1,
  `new_page_ld` 1, `fill_row_op` 2, `held_op` 2, `held_kind` 2, `held_page_ld` 1, `held_at_sel` 2, `have_op` 3,
  `dirty_op` 2, `rd_go` 1, `rd_addr` 2, `rd_half` 2, `rd_two` 1, `rd_xform` 2, `rd_catch` 1, `wr_go` 1,
  `wr_addr` 2, `wr_half` 2, `wr_two` 1, `wr_src0` 2, `wr_src1` 2, `lk_go` 1, `lk_op` 1, `lk_root` 1, `lk_page` 1,
  `lk_level` 2, `lk_entry` 1, `pm_patch` 1, `pm_fill` 1, `take` 1, `fp_pop` 1, `pc_dec` 1, `busy_clr` 1, `done` 1,
  `stop` 1, `err` 3, `stat` 2, `credit` 1.
- **decoder, 10 bits:** `dec` 1, `need` 2 (raw, a hit, a 34-bit or 66-bit miss), `hdr` 1, `val` 1, `lru_hit` 1,
  `lru_miss` 1, `refill` 1, `clear` 1, `ready` 1.
- **AXI channels and the look-up port, 15 bits:** `ar_clr`, `beat_in`, `rd_last`, `rd_err`, `catch`, `aw_clr`,
  `w_take`, `w_fetch`, `w_hold`, `w_valid`, `w_last`, `b_in`, `b_err`, `lk_taken`, `rp_ld`, 1 each.

**In all 95 bits.** The run has 73 rows and 60 distinct words, 47 without the next state. The decoder has 19 rows
and 19 words, and the channels have 14. That is **93 distinct words**. As in the cache, plain logic beats a table:
about 1.1 to 1.5 kGE in all.

### Its registers

| part | as coded | lean |
|---|---|---|
| words 32–37, `WORD_COUNT`, `PAGE_COUNT` | 167 | 167 |
| the statistics counters | 192 | 192 |
| the control block's | 8 | 8 |
| the input queue (4 × 128) | 518 | 518 |
| **the decoder: the LRU table 2,048, the bit buffer 194 and its count 8, the run 64, the output register 99** | **2,413** | 2,445 (a valid bit an LRU entry) |
| the free page queue's pointers | 24 | 24 |
| the state machine | 346 | 276 |
| the AXI port | 266 | 258 |
| the look-up port | 131 | 129 |
| the page memory's read register | 128 | 0 |
| **in all** | **4,193** | **4,017** |

**This is the specification's count exactly (17.10.7).** It is held as the code holds it, and the code holds more
than a chip needs:

- **70 bits never needed.** `look_status[2]` is written and never read, and so is `look_entry[TRACER]`.
  `look_entry[IMAGE]` equals `src[IMAGE]` wherever it is read: in look-up and level, when the image owns its page.
- **8: `w_pend_row` is always `wr_row`.** **2: the request's `depth` is `RUN_CONFIG`'s.**
- **36 constant bits:** AR's and AW's addresses always have their low 11 bits 0 (a half page at a multiple of
  2,048), and `len` is 127 or 255. The lean column keeps these 36.
- **128: the page memory's read register** is normally the SRAM macro's output latch.

### Its datapath operators

The largest, high:

| operator | GE |
|---|---|
| the registers' input multiplexers and holds (the LRU table's 2,048 holds among them, 4,772) | 9,816 |
| the LRU hit's entry, 32:1 of 64 bits | 4,623 |
| the LRU table cleared at `START`, bit by bit (high only) | 2,724 |
| a 128-bit word put into the bit buffer at bits 0 to 65 (a 7-level shifter) | 2,610 |
| the bit buffer shifted by 0, 6, 34, 64 or 66 | 1,808 |
| the next-state and output logic | 1,097–1,521 |
| the input queue's head word, 4:1 of 128 | 895 |
| the window's read, 12:1 of 32; the six statistics incrementers; the run's three 32-bit counters | 869; 639; 320 |

**The gates: flip-flops 22.8–23.8 kGE, logic 18.8–34.9 kGE.** **The LRU decoder dominates.** Its flip-flops come to
13.7 kGE and its operators to 11.0–13.6 kGE, with 4.8 kGE more for the table's holds in the high figure. With the
high figure's buffering that is about 25–35 of the 42–59 kGE. The owner's note that LRU "is about 2,000 flip-flops and halves the stream"
(DESIGN-NOTES-2026-11-16.md) holds for the flip-flops. The table's 32:1 read and its clear about double it in gates.

## 4. The AXI arbiter (`axi_arbiter.*`; 23.11)

**No named states.** Each client has its own small machines, each changed by its own handshake. Each client has an
AR slot and an AW slot, each empty or cutting a burst. Each client has a read list and a write list, each of 0 to 4
bursts. The write piece in turn is either none or 1 to 32 beats from its end. The W queue holds 0 to 2 beats. That
is 45 flip-flops of state: the slots' and the write piece's valid bits, the lists' heads and counts, the beats to
take, the W queue's head and count.

**Its control word, 49 bits:** `ar_turn` 1, `aw_turn` 1, `ar_served` 2, `aw_served` 2, `slot_ld` 8, `list_push` 8,
`r_beat` 1, `r_last_piece` 1, `b_piece` 1, `b_last` 1, `wq_push` 1, `wq_pop` 1, `wcur_done` 1, `readies` 12,
`rvalid` 4, `bvalid` 4. It has 11 distinct words, but they combine freely across the channels, so it is plain
logic.

**Its registers: 1,539, the specification's count exactly** (264 a client, 483 shared). The code's lists have
`MAX_LIST` 16 entries and a response field on the read lists. At `CLIENT_BURSTS` 4, only 4 entries are reachable,
and R beats carry their own `rresp`, so the 1,539 is what a chip needs. **A lean build shares one R data register
among the four clients' outputs, which saves 384** (1,155).

**The gates: flip-flops 6.5–8.7 kGE, logic 3.8–8.2 kGE.** The largest item is the W queue's input from the client
in turn, 4:1 of 129 bits (902 GE). Next are the cutters: eight in the high figure (about 1.1 kGE), two shared in the
low (about 0.9 kGE with their multiplexers).

## 5. The tile's core, beside them

**What it is** (`spec/core.md`; machine-spec 6, 24, 25): RV32I with C, Zmmul, Zicsr and Zicond, in machine mode.
Its pipeline has four stages (fetch, decode, execute, writeback) with no branch prediction. The multiply runs in two
pipelined stages: four 17 × 17 products in execute, their sum in writeback. It has 31 GPRs and the CSRs of core.md's
table: mstatus, mie, mtvec, mscratch, mepc, mcause, mtval, mcycle and minstret. It has IMEM and DMEM of 32 KiB
each, each with a NoC write port, and a 256-byte boot ROM.

**My estimate, by parts at the same costs** (`count.py`'s `CORE_REGS`, `CORE_OPS`):

| part | GE |
|---|---|
| flip-flops: GPRs 992, the pipeline's registers 585, the data and coprocessor ports' 168 (the coprocessor's 97 high only), CSRs 262, flags 7 (1,917–2,014) | 10,900–11,400 |
| the register file's two read ports (32:1 of 32 bits, twice) | 4,623 |
| the multiplier: four 17 × 17 signed products and their sum | 8,552 |
| ALU, branch and jump adders, PC increments, forwarding, immediates, fetch's alignment | 3,186 |
| decode (RV32I, Zicsr, Zmmul, Zicond) and RVC's expander | 2,900 (an allowance) |
| load and store alignment, byte enables | 431 |
| CSRs, traps, NMI, WFI, the pipeline's stalls, the windows' checks | 3,412 (in part an allowance) |
| high only: the GPRs' write enables as hold muxes (2,311), the pipeline registers' holds, 15% buffering | |
| **logic in all (low–high)** | **23,100–31,500** |
| **the core in all** | **34,000–43,000 GE** |

**What is published for cores of its kind** (lowRISC Ibex README, its configuration table):

- Ibex "small" (RV32IMC, two stages, 3-cycle multiplier): 26.06 kGE in Yosys, about 24 kGE commercial.
- Ibex "maxperf" (RV32IMC, 1-cycle multiplier, branch target ALU, writeback stage): 35.64 kGE in Yosys, about
  33 kGE commercial.

Both use a latch-based register file and include a divider. This core has a flip-flop register file, which costs
more, and no divider, which costs less, and its multiplier is somewhat larger. **The two agree: about 35–40 kGE,
±30%.** I am least sure of this figure: the core's insides are not modelled gate by gate in the simulator.

**Its memories: 526,336 bits** (64 KiB and the ROM's 2,048). These are most of the 0.15 mm² that 24 budgets.
Section 25 gives "0.14 mm² for the 64 KiB of IMEM and DMEM, 0.01 mm² of core logic", and 35–43 kGE is in keeping
with 0.01 mm² at 12 nm, as a rough check rather than a figure. **The controllers are the same order as the core:**
like for like, the cache's controller is 1.2 to 2.0 cores and the Updater 1.2 to 1.4; the arbiter is a third of
one.

## 6. What a core would take to do the same work

The outlines are `fw/updater_fw.S` and `fw/cache_fw.S`. Each **path** in them is the straight line of instructions
one case runs, and both files assemble with llvm-mc for `rv32ic_zicond_zicsr_zmmul`, so every instruction is real.
`fw_cycles.py` counts them at core.md's costs ("The core's cycle costs"): an instruction 1 cycle, a taken branch 3
more, a JAL 2 more, a load's result used at once 1 more, a window load 1 more (`WINDOW_LOAD_LATENCY` 2). Loads and
stores to DMEM retire one a cycle. **Memory waits are left out on both sides,** since the controller waits as long
for the same burst or reply. `FW.md` has every path.

### What the core would need that it does not have

- **For the Updater** (on the master tile): a DMA of its own between DMEM and LPDDR for pages and half pages, and a
  window onto the cache's look-up port. Both are assumed in the outlines at window latency. The page held fits in
  DMEM: 4 KiB of 32. The update words arrive in DMEM by Ingress's DMA, which exists (17.6.4).
- **For the cache** (on the memory tile): the cache's memories mapped as a table, read 32 bits at a time. That
  takes a 1,024-bit line 32 loads, so a fill read's line is 32 loads and 32 stores where the controller moves it in
  one cycle. It also needs the look-up ports' request registers and the reply bus as a window, the fill port's
  likewise, and a DMA of lines between the data memory and LPDDR. All of these are assumed.

### The Updater: an update word, decoded and placed

| update word | firmware, cycles | controller | the link's cycles for it (a byte a cycle) | cores needed |
|---|---|---|---|---|
| raw: a header word | 15 | 1 | 8 | 1.9 |
| raw: a value into the page held, general path | 25 | 1 | 8 | 3.1 |
| raw: a value, fast path (a stretch of a run inside the page, unrolled by 4; +40 a stretch) | 5.5 | 1 | 8 | 0.69 |
| LRU: a hit at index 0 | 41.8 | 1 | 0.75 | 56 |
| LRU: a hit at index 8 (a move to front of 8 entries) | 131.8 | 1 | 0.75 | 176 |
| LRU: a 34-bit miss; a 66-bit miss | 63.9; 81.9 | 1 | 4.25; 8.25 | 15; 9.9 |
| LRU: the fixtures' mix (4,469 codes: 80% hits, mean index 0.13; 17.3 bits a code) | 50.6 | 1 | 2.16 | 23 |

The LRU mix comes from `lru_stats.py`, which decodes the repository's frame files (`v1.37_wip/tests/updater/*.frames`,
gold's `small.frames`). Those are small test fixtures, not proof runs. On proof runs LRU-coded frames 2, 6 and 9
take 0.4 to 0.99 of the raw size (WORKER1-REPORT.md), so a code is about 26 to 64 bits, 3 to 8 cycles of the link.
With the fixtures' costs a code, that is still 10 to 17 times what a core can do.

| page work | firmware, cycles besides the waits | the waits (the controller's too) | the controller's own cycles |
|---|---|---|---|
| a value in another page: the page held flushed in place, the next looked up and held | 39 | the flush's write, the look-up | about 6 |
| a fault's step at an **index** level: source read, **1,024 entries' owned bits cleared**, page taken, written, entry updated | 7,459 | the load, the write, the update | about 5 |
| the same at the data level | 35 | the same | about 5 |

The controller clears owned bits as the beats stream in, at no cost. Firmware takes 7.25 cycles an entry, branch
free with `czero.eqz`, so at 7,459 cycles each index level of a fault costs about ten times its memory wait (about
740 cycles).

**The measured point.** With the master tile's firmware only copying the host's words to the Updater, by DMA, at
about 360 instructions for each 256-word transfer, it was awake 17 to 20% of the link's time. With the forward
count it is 2.4 to 7.0% (INGRESS-INTERFACE.md, "In v1.37"). That copying costs a few instructions a word. Decoding
and placing them costs 5.5 to 25 cycles a raw word, against 8 for the link to deliver it. **In the best case, raw
words in long runs, a core doing the Updater's work would be busy about 0.69 + 0.17–0.20 ≈ 0.86–0.89 of the link's
time with nothing else to do. In every other case it falls behind the link.** The program image, which barely
compresses and is sent raw in whole pages, is the best case. Frames 6 and 9, which are sent LRU-coded and in
shorter runs, are not.

### The cache: a request answered

| request | firmware, cycles | controller (23.6.7) |
|---|---|---|
| a look-up, a depth-2 file, both lines cached, hit at the first way probed; no replacement order kept | 105 | 6 |
| the same, true LRU kept (23.6.1) | 193 | 6 |
| the same, hit at the mean way (4.5 probes); no order kept / true LRU | 126 / 214 | 6 |
| a fill read, a depth-3 file, four lines cached, true LRU, the line's 32 words copied out | 482 | 10 |
| a line missing: probes, victim, fetch set-up, install, besides LPDDR's wait (about 80) | 80 | about 13 of its own |
| a dirty victim's write-back, besides its wait | 9 | about 3 |

Keeping true LRU in firmware costs 44 cycles a touch, because it reads, ages and rewrites eight 3-bit ages.
Without it a look-up still costs 105 cycles against the controller's 6, and a fill read, the Tracer's main traffic,
482 against 10. **On hits the firmware is 17 to 48 times slower.** On misses it adds about 80 cycles to LPDDR's
wait, about twice the controller's time.

## 7. The other side of the trade

**What is fixed in the Updater's logic, which firmware could have changed after the chip is made:**

- the update word's format: a header's start in [31:0] and its count in [63:32], then one value for each 64-bit
  word;
- the LRU code: a 6-bit hit, a 34- or 66-bit miss, a 32-entry true-LRU table cleared at every frame;
- the bit buffer's 194 bits and the 128-bit word port;
- the type rule that skips reading a whole page or half;
- the bursts: a page, or a half at a multiple of 2,048;
- the copy-on-write walk: the Tracer's copy before the image, a level at a time, a page taken from the queue at each
  level, the data written before the entry that names it;
- the index's format: a 31-bit address, bit 31 owned, all ones a hole;
- the reach: 2²³ pages;
- one burst in flight, and every error stopping the run.

**What is fixed in the cache's controller:**

- 128-byte lines, 8 ways by 512 sets, true LRU, and a victim never the update slot's line;
- write-back for the Tracer's writes, write-through for updates, one update in flight;
- no list of pending writes: a dirty victim is written back in its place first;
- the look-up's walk, stopping at the first entry not owned, against `fill`'s, which goes on through a shared page;
- the discard's invalidation without a write;
- hits under a miss, as long as they touch no line of the miss handler's set.

Item 179's early write-back is an example of a policy change that would need new logic.

**What stays in firmware today** (`spec/master-firmware.md`, 30.3–30.4):

- which runs are made, and in what order: the program image, the jump table, the data image, and the memory
  segments as three runs;
- each run's roots and `RUN_CONFIG`: root 0, 1 or 2, `continue`, `tracer_copy`, the counts;
- the free pages: the page map, `PAGE_COUNT`, the queue's pushes under its credit;
- what each frame means: its type, its forward or discard counts, the records and logs it routes;
- the hand-over to the tracer tile;
- every check of a frame's contents, and the stops.

## 8. Choices made where the brief left it open

- **The state count:** the code's enums are reported as the named states; the flags that hold the other machines'
  states are listed apart.
- **"As coded" counts a register the code keeps, even a redundant one;** the lean count drops what a chip need not
  keep. Wires the code keeps as members (the walker's tag row and set) and the simulator's checks are counted in
  neither.
- **Widths at `LPDDR_ADDRESS_BITS` 32,** as the specification counts.
- **The firmware is outlined against assumed DMA and windows,** so that what it costs, rather than what it lacks,
  can be counted. What it lacks is listed in section 6.
- **No figure here is in mm²** but the specification's own; the costs are in GE for another library to redo.

## Files

| file | what |
|---|---|
| `count.py` | the registers, operators, memories and control-word rows of the three units and the core; the costs and the low/high switches; writes `COUNTS.md` and `counts.json` |
| `COUNTS.md` | its full output: every register with its source, every operator, every control row |
| `fw/updater_fw.S`, `fw/cache_fw.S` | the firmware outlines, by path |
| `fw_cycles.py`, `FW.md` | the outlines counted, and set beside the controllers and the link; `fw.json` |
| `lru_stats.py` | the LRU code mix of a set of frame files |
| `yosys/` | the three Verilog models, `run.sh`, `ge.py`, `crosscheck.md` |
| `PROMPT.md` | the brief |
