# WORKER6 report: the v1.2 recipes

**Deliverables** (all on `main`):

| file | what |
|---|---|
| `wip/sw/RECIPES.md` | the v1.2 recipe book: 21 v1.1 recipes ported or retired, 6 new, each with a walk-through |
| `wip/sw/CONVENTIONS.md` | the v1.2 control conventions, rewritten section by section |
| `wip/sw/mesh-v1.2-draft.h` | the draft header the recipes assume, per D14 |
| `WORKER6/FINDINGS.md` | 16 findings, most important first, each with the log's answer (D37, D38) or open |
| `WORKER6/check/` | every recipe's code as compiled files; `build.sh`, `gen_book.py`, `check_book.py` |

**How the code is checked.** Every code block in the book is generated from a `/* RECIPE */` region of
`WORKER6/check/`, and `check_book.py` confirms all 43 match. `build.sh` compiles every file against the draft
header with `-Wall -Wextra -Werror` (RV32I + Zicsr + Zmmul, GCC 13, `-O2`). Nothing has run: there is no v1.2
simulator, linker script or ROM model. When meshsim v1.2 exists, these files are the starting point for its
`rcp_*.c` tests, as v1.1's were for `RecipeTest`.

## 1. Recipes

v1.1's numbers are kept, so the two books diff section by section; the new recipes are 22-27.

**Ported** (what changed beyond names, struct access and the counter field):

| # | recipe | the v1.2 changes |
|---|---|---|
| 0 | helpers | `wait_tables()` gone; `wait_all()` (D17's `EVENTS_REQUIRED` wait); `local_run()` waits for its counter **and** DMA run done, and handles n = 0 |
| 1 | the command loop | the boot moved to 22; `sim_exit()` replaces returning to `ebreak` |
| 2 | local copy | mode 1; tables free by rule 6, no wait before touching them |
| 3 | block copy into controller memory | mode 0 at an address in region 0x00 or 0x01; 128-bit, 16-byte-aligned destinations; counter 2 |
| 4 | histogram, prefix sum, bucket passes | mode 2; the `PTR[64]` restore goes |
| 5 | augmented transfers | mode 3; the start waits for [7] too |
| 6 | gather | mode 4; the stream's counter field is the values' counter |
| 7 | one-word exchange, words to one tile | replies are mode-0 writes to `REPLY_WORD(me)` with no-increment, cleared when done (D37.5); `send_words` by byte address |
| 8 | vector exchange | mode 0, 8-byte-aligned landing buffers |
| 9 | all-to-all | the start waits for [7], so a tile receiving nothing doesn't pulse `done` with its run going |
| 10 | mesh reduction | mode 0, aligned buffers |
| 11 | read port | `RD_DEST(offset, size, counter)`; a misaligned read still counts (D36) |
| 12 | parameters, status, errors | `PARAMS`/`STATUS`/`ERRORS`; v1.1's "what a phase caused" pattern retired: errors trap |
| 13 | synchronised start | counter 1 by the struct; the compiler's start path checked (below) |
| 14 | cycles | every figure "to measure", except the lead-ins and *K*'s formula [derived] |
| 15 | the rules that bite | rules 1-3 kept; 4 rewritten (alignment is hardware-checked); 5 retired; 6-11 new (tables by events, no wait on an unarmed counter, consuming loads, GO in the mask, the no-increment bit, request limits) |
| 16 | gaps and awkwardness | v1.1's three: two gone, one kept; the walks' findings summarised |
| 17 | write a block into big SRAM | mode 0 by address to region 0x03: no `PTR`, no table touched |
| 18 | fill | as 17; `selftest.h` unchanged |
| 19 | hash | the read port on counter 2 |
| 21 | the self-test | signatures over the request bus, one outstanding, waiting for C&C 1's reply (D38), instead of REPORT's paced replies |

**Added:**

| # | recipe | from |
|---|---|---|
| 22 | the boot ROM (assembly), the image's start-up, boot part 2, C&C 1's side of the boot | D22, D31, D36, D37.1, D37.2, D37.4 |
| 23 | waiting for the tables by events, both orders walked | D17 |
| 24 | the error handler (assembly): 23 reports, no memory, no scratch register, then park | D20, D21, D37.3, D38 |
| 25 | a peripheral window through its struct: presence, the replayer's state words, a peripheral DMA | D9, D10, D14 |
| 26 | the replayer's refill loop on counted credits | D10, W3-24, spec 4.19.8-4.19.10 |
| 27 | C&C 1's side of a phase: commands, READY/GO, the watchdog, answering requests, the toast path | D28, D35-D38 |

**Retired:** 20, REPORT (D1.6), with `paced_to_cnc()` and C&C's REPORT sequence. All HALT material goes with it
(D23): halting before an uncoordinated write, HALT clearing counters and conditions, rule 5's `PTR[64]`
staging, and v1.1's gap "a counter can't be given up on without HALT". Its replacements are named in R20.

## 2. What the spec session should check

The recipes follow the log; these are the points where the spec text must agree with them, or where the log
left something the recipes had to assume.

1. **The boot ROM** (6.2) with D37.1: after GO the tile branch jumps only if the **event** [6] was returned
   by the load that consumed GO, not `STATUS` [5] (which GO clears); otherwise it pulses `done` again (R22).
   WORKER5's report (item 6) describes D36's ROM, before D37.1.
2. **Boot part 2** (D37.2): the first command arms counter 1 first, then sets `mtvec` and enables `mie.MEIE`
   and `MIE` (R22). Which `mie` bit is Q1's.
3. **The handler's report set** (4.13, item 122): R24 is a full proposal of the order: 23 requests (F16 against
   "about 17"). Per slot it reads seven of the ten standard words: `PULSE_RUN` and `PULSE_DMA` are write-only,
   and `EVENTS` consumes. WORKER5's report (item 7) says "words 0-9", which would read `EVENTS`: it should be
   `EVENTS_PEEK`, and the write-only pulses read 0 at best. Absent slots send zeros (D37.3).
4. **Requests** (3.3, 4.18, 6.7) with D38: receive queues in the tree (128/256/128), 1/3/1 outstanding per
   tile, the reply's counter named in the request, error reports excepted. And **F15**: three replies from
   C&C 2 can't share one reply-block pair.
5. **`rd_running`** (4.2, 8.2): it should fall no earlier than [7] (guarantee B), if D17's poll is to be
   sound (F6).
6. **Data memory's first 16 bytes reserved** (D37.4) and the no-increment bit cleared by the program (D37.5).
7. **`ROUTER_ADDR` advancing only in mode 0** (W5-03): the recipes send only mode 0 through the send port, so
   they rely on neither reading. The draft header's comment now says mode 0.
8. **Invariant 14** (read-port reads not overlapping arrivals): kept as a rule in R11 and CONVENTIONS 9; S9's
   protected loopback is now required, so the spec may be able to retire it.
9. **A stream on a controller naming the broadcast** (F14), and whether the done pulse clears [7] (the recipes
   don't rely on it).
10. **The counter convention** (1 big SRAM, 2 controller memory, 3 replies, 4 peripheral) is the recipes'
    choice, not hardware; the spec's examples happen to match (WORKER5's report, item 4).

## 3. For WORKER4: the draft header

WORKER5 compared the draft with the spec and found it agreeing on the controller block's offsets, the exit
and trace words, the `PARAMS`/`STATUS`/`ERRORS`/`EVENTS` bits, the common header, `STREAM`, C&C 1's words and
event bits, and the replayer's events and byte map. The replayer's words 32 and 33 now carry the spec's names,
`REPLAYER_STATUS` and `REPLAYER_OPCODE` (W5-09).

**Choices the draft made that the log leaves open** *(proposed)*:
- **The data-memory layout:** reserved 0x00-0x0F, image from 0x10, stack down from 0x7BC0, the reply block
  0x7BC0-0x7FBF, the command block 0x7FC0-0x7FFF. The ROM reads `COMMAND_BASE`, so the address becomes a
  silicon constant.
- **The table window** at 0xFFFF2000 (D11's proposal).
- **C&C 1's request format:** op [17:10], reply counter [20:18], argument [31:21]; ops REPORT 1, STARTED 2,
  RESULT 3. Placeholders until C&C 1's firmware defines them.
- **Qualifier macros** `MESH_RO`, `MESH_WO`, `MESH_RW`, not W3-27's `R`, `W` and `RW`. `rcp_reduce`'s
  `#define RW 16` collides with the latter.
- **The counter is a required argument** of every header macro (`ROUTER_HDR_W`, `STREAM_COUNT_W`, `RD_DEST`,
  `DMA_HDR_W`), as W3-REVIEW-2 section 6 advised: no send is uncounted by default.
- **`DMA_CONFIG` and `GROUP`** keep v1.1's bit layouts (the log doesn't change them).

**Things the compile showed:**
- **W3-27's extra instruction doesn't appear.** With `#define tile ((volatile TileRegs *)0xFFFFF800u)`, GCC 13
  at `-O2` emits every register access as a single x0-relative `lw`/`sw`, as v1.1's constants did: no base
  address is loaded in any recipe's object code. The start path from `WFI` to `PULSE_DMA` has the same shape
  as v1.1's. Worth re-checking at the build's actual flags and compiler version.
- **The build needs `zicsr`** (`-march=rv32i_zicsr_zmmul`): the ROM, the handler and boot part 2 use CSR
  instructions, which v1.1's `rv32i_zmmul` rejects. And `-std=gnu11` (or `c11`) for the request port's
  anonymous unions.
- **Assembly includes the header:** the ROM and the handler use its offsets, so constants used from assembly
  carry no `u` suffixes or casts (`X0REL(off)`, `OFF_*`, `EV_*`, `COMMAND_BASE`).
- **Write-only words are `MESH_WO`, which still lets a load compile;** only `MESH_RO` (const) blocks a store.
  A generated header could make write-only members unreadable by other means if wanted.

## 4. Findings in brief

Of the 16 findings, D37 and D38 answer F1-F5, F7 and F8. **Open:** F15 (C&C 2's three replies share one
reply-block pair: medium), F6 (`rd_running` against guarantee B), F16 (the report count), and the six
awkwardnesses F9-F14. Details and steps are in `FINDINGS.md`.
