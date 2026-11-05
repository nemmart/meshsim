# WORKER5: the recipes checked against the spec (round 2)

**What was checked** (at a662df9, WORKER6's REPORT-2), against `wip/spec/machine-spec.md` v1.2 as updated for
decisions 37–42 in this round:
- `wip/sw/RECIPES.md`, `wip/sw/CONVENTIONS.md`;
- `wip/sw/mesh-v1.2-draft.h`;
- `WORKER6/check/` (`bootrom.S`, `handler.S`, `rcp.h`, `rcp_*.c`, `crt0.S`).

The areas covered were register offsets and bits, the boot ROM and boot sequence, the handler, counters and
events, requests and replies, and the replayer's loop. I also checked WORKER6's `REPORT.md` items 1–10 and
`REPORT-2.md` §3.

I didn't edit the recipes or the header. Where the spec was wrong or silent, I fixed or flagged the spec in this
round; that is noted against each item.

## Verdict

**The recipes, the header and the check sources agree with the spec on everything that matters.** That covers:
- every register offset, including the 93 reserved words and the trace and exit words at 0x9F8/0x9FC;
- the `PARAMS`, `STATUS`, `ERRORS` and `EVENTS` bits; `EVENTS_MASK`'s reset value is GO | counters done = 0x180;
- the common header, `STREAM`, `RD_DEST` and `DMA_HDR` layouts;
- the replayer's words 32–34, event bits and byte map;
- C&C 1's words 32–45 and its event and error bits;
- the boot ROM, instruction for instruction;
- the handler's 24 reports, report for report;
- seven counters, the request limits and the per-request reply address.

There is **one real spec gap** (M1, now open item 136). The spec's own replayer loop was **one wait weaker than its
rule** (M2; spec fixed). The rest are stale comments and labels in the recipe material.

## Mismatches

| # | where | the spec says | the recipe or header says | which is right |
|---|---|---|---|---|
| M1 | R22 walk, F14/F17; spec 3.2 and 6.2 | 3.2: packets from one source to one destination arrive in the order sent. 6.2: "overlays and command come from one source … so the command lands last" | the command can't safely follow a run from the send port: DMA run done says only that the last table read is done, so a send-port packet can overtake the run's read returns. R22 puts the command in the run | **The recipe is right; the spec was silent.** I added a note in 3.2, qualified 6.2 ("provided they leave by one injection source") and opened **item 136** for the owner (W5-25). Neither text claims an order the hardware may not give |
| M2 | R26 against spec 4.19.9 | the spec's loop squelched on the refill counter's done alone, then rewrote `STREAM` entries next time round, against its own rule in 4.10 that the tables are free only at the counter's done **and** DMA run done | R26 waits on `EV_CTR(CTR_PERIPH) \| EV_RUN_DONE` | **The recipe is right.** Spec 4.19.9's loop now waits on both through `EVENTS_REQUIRED`, as R26 does |
| M3 | `rcp.h`, `local_run` comment (and R0's code block) | DMA run done is `EVENTS` [10] (D40) | "DMA run done (EVENTS [7])" | **Spec.** A stale bit number in a comment; the code uses `EV_RUN_DONE`. R0's prose already says [10] |
| M4 | R25 walk, step 1 | the controller's live bit for peripheral *n* is `EVENTS` [12 + *n*] ([13]–[15]; 4.17.3) | "the tile's live bit [9 + n]" | **Spec.** Stale numbering in prose; the code uses `EV_PERIPH(n)` = 1 << (12 + n), which is right |
| M5 | R25 (relies-on line, `replayer_load_state` comment; `rcp_replay.c`) | the replayer's writable state is its **part 0**, at byte offsets 0x000000–0x003FFF of region 0x04 (4.19.10); "region" is reserved for the address map's regions (2.2) | "the replayer's region 0 is written while it is stopped" | **Spec**, for the word: "region 0" now reads as address region 0x00, data memory. The rule itself (written only while `STATUS` [0] = 0) agrees |
| M6 | header's data-memory comment; CONVENTIONS 1's memory-map heading; REPORT-2 §4 | the command block at 0x00007FC0 and the reply block at 0x00007BC0 are **decided** (owner, 2026-10-29: W5-01; 4.11, 4.12). The 16 reserved bytes and overlays from 0x10 are decided too (D37.4) | "(proposed: the log fixes the blocks' shape, not their addresses)" | **Spec.** The addresses are no longer proposals; the ROM reads `COMMAND_BASE`, so it is a silicon constant |
| M7 | header line 8, the build comment | `-march=rv32i_zmmul_zicsr` (4.11); CONVENTIONS 1 and `build.sh` agree | `riscv64-unknown-elf-gcc -march=rv32i_zmmul …` | **Spec.** A stale comment: the ROM, the handler and boot part 2 need `zicsr` |
| M8 | CONVENTIONS 7, "a running program always reads 0" in `ERRORS` | the first error traps only once boot part 2 has enabled interrupts; before that an error raises the error line and the toast pin but takes no trap (4.13, 6.2) | "A running program always reads 0 here, since the first error traps" | **Spec**, as a precision. R22's walk, step 7, says it correctly; CONVENTIONS 7 could add "after boot part 2" |

## WORKER6's REPORT.md items for the spec session

| # | item | spec v1.2 now |
|---|---|---|
| 1 | the ROM tests the event [9], not `STATUS` [5] | **agrees** (6.2, step 3; 4.12 says the event stays pending) |
| 2 | boot part 2 arms first, then `mtvec`, `mie.MEIE`, `MIE` | **agrees** (6.2). The spec gives no order inside the start path beyond 6.4's "arm the counters first"; R22's order fits it. Which `mie` bit is item 121 (Q1) |
| 3 | the report set: seven of ten words per window, `EVENTS_PEEK` not `EVENTS`, not the pulses; absent slots zeros | **agrees**, and the spec now defines the set as R24 sends it: 24 reports in R24's order (4.13). Only the header's field positions and op value stay proposed (item 122). The draft's placeholder (op [17:10], sequence [31:21], reply counter 0) fits |
| 4 | requests: queues in the tree, 1/3/1, the reply counter named, error reports excepted; F15 | **agrees** (3.3, 4.18, 6.7); F15 answered by D40's reply address per request |
| 5 | `rd_running` falls no earlier than [10] | **agrees**: in the same cycle (4.2, 4.10 guarantee B) |
| 6 | 16 reserved bytes; no-increment cleared by the program | **agrees** (2.2, 4.11) |
| 7 | `ROUTER_ADDR` advances for mode 0 only | **agrees**, decided (D42; 4.11) |
| 8 | invariant 14 with the protected loopback required | **unchanged**: the rule and invariant 14 stand; whether to retire them is open item 116 |
| 9 | a stream naming the broadcast; does `done` clear [10]? | **agrees** on the broadcast (4.2). On `done`: it doesn't; only `PULSE_DMA` and a consuming load clear DMA run done. The spec now says so in 4.12 |
| 10 | the counter convention is software's | **agrees**; the spec's examples (counter 2 for the read port, 4 for refills) match CONVENTIONS 5 |

**The handler note** (`EVENTS_PEEK`, never `EVENTS`; skip the write-only pulse words): `handler.S` reads words 0–2,
6–9 of each present window and nothing else, which is exactly spec 4.13's rule. It reads the controller's
`EVENTS_PEEK`, never `EVENTS`. Absent windows aren't touched (`PARAMS` [2:0] first), and their three reports
carry zeros. **Agrees.**

## Checked and agreeing

These were checked in detail; nothing to report:
- **The controller block:** every offset against 8.1, including `WRITE_COUNTER[7]` at 0x860–0x878, the read port
  at 0x87C/0x880, and `TileRegs` at 0x200 bytes.
- **The windows:** `PeripheralStd` words 0–9 and the 22 reserved standard words; `ReplayerRegs` 32–34;
  `Cnc1Regs` 32–45 in D28's order.
- **The bits:**
  - `ST_*`, `ERR_*`, `EV_*` (including `EV_RESET_MASK` = 0x180), `PEV_*`, `RP_EV_*`, `CC1_EV_*` and
    `CC1_ERR_UNIT`;
  - the common header (`HDR_*`), `STREAM_SRC_W`, `STREAM_COUNT_W` (17-bit count, *d* to 126), `RD_DEST`
    (17-bit offset) and `DMA_HDR_W`;
  - the replayer's byte map (`RP_*`, parts at offset [22:20]).
- **The ROM** (`bootrom.S`), step by step against 6.2:
  - `mtvec` first; the branch on `PARAMS` [26:20].
  - The tile loop pulses `done`, then `WFI` + `EVENTS` until GO; it tests [9] in that same load and loops back
    to the `done` pulse otherwise. Its mask stays the reset mask, so [9] isn't consumed.
  - The controllers' mask is [9] alone, restored to 0x180.
  - It reads slot 0, then slot 1, and jumps. It writes no data memory and leaves `MIE` off.
- **The handler** (`handler.S`): the 24 reports exactly as spec 4.13's table. Every request's four words are
  written; nothing goes to memory; it parks in a plain loop.
- **Requests:** `request_issue` arms the slot's counter for the reply (plus any data) before sending, and names
  the reply address in word 1. `drain_requests` answers at the address and counter each request names. Error
  reports ask for no reply. The 1/3/1 limits hold, since CONVENTIONS 6b's three slots keep a tile at three in all.
- **Replies and the no-increment bit** (R7): the reply address is `REPLY_WORD(me)` in every receiver. The bit is
  set for the exchange and cleared after (D37.5); `ROUTER_HDR`'s read-modify-write is legal, since it is R/W
  with size reading 0.
- **The read port** (R11): `RD_DEST` with offset, size and counter; one wait per step on the counter.
- **Tables by events** (R23, `local_run`): the counter's done **and** DMA run done, or DMA run done alone when
  nothing was armed.
- **The replayer's loop** (R26) against 4.19.8–4.19.9:
  - credits are set to 2 at `PULSE_RUN` and the expansion's at each new expansion;
  - a refill is pushed for every credit the load returns;
  - idle queues leave the mask; the expansion's bit joins at the first new expansion;
  - the squelch waits on counter 4 and [10];
  - the words are read as `EXPANSION_OPCODE` and `EXPANSION_OFFSET`.
- **C&C 1's phase** (R27):
  - "all tiles READY" is used as a live event, and the watchdog's event is consumed;
  - `go()` writes `WATCHDOG` and then `PULSE_GO`, and waits `go_latency` + 1 by `mcycle`;
  - commands go arguments-first with slot 0 last;
  - the toast path drains reports by `EVENTS_PEEK` [12] before each pop.

## Observations (not mismatches)

- **R27, the watchdog's expiry:** `wait_all_ready()` parks (`for (;;)`) when the watchdog fires. The toast pin is
  latched, but no error traps C&C 1 (the watchdog posts an event, not an error), so the dump never goes out. The
  spec leaves this to C&C 1's firmware (6.1). Calling the toast path instead would send what C&C 1 knows.
- **R27, `MAX_REPORTS`:** 64 × 24 reports assume only tiles fail. C&C 2 and C&C 3 send 24 each too (the set is
  every unit's, 4.13), so 66 × 24 would be exact. The bound only stops the drain early.
- **R22, C&C 1's rule for one-at-a-time starts:** land a tile's first command only when that tile should start
  at the next GO. That follows from the ROM's test, and the spec (6.2) implies but doesn't state it. Worth one
  sentence in 6.2 if the owner agrees.

## Spec changes made in this check

- **3.2:** a note on a unit's two injection sources, and **item 136** (M1).
- **6.2:** the ordering claim for overlays and the command qualified, citing recipe 22 (M1).
- **4.19.9:** the controller loop waits on the refill counter **and** DMA run done (M2).
- **4.12:** the `done` pulse clears no event but the counters' (REPORT item 9). A stale "[4]" in the same
  sentence is now [7].
