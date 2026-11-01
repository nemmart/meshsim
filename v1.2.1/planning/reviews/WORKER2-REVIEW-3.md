# WORKER2 review, round 3: sign-off on the packet path

References: **D***n* is line *n* of `wip/planning/DECISIONS-v1.2.md` at `50f6f46` (1,283 lines); **W4** is
`WORKER4/PROMPT.md`.

## 1. Verdict

**Ready to implement, with two small fixes.** The packet path holds together:

- the 256-bit word, the byte-addressed map with natural alignment, the modes and the four counters;
- K as a formula over a now fixed-length write path;
- the bad-packet table and the hardware/meshsim split.

WORKER4 can build phases 1-3 as asked. Two things would make it build the wrong thing:

- **W2-37:** 31's step 5, as written, makes every tile raise "done early" when it loads big SRAM at boot.
- **W2-38:** 32's "dropped, still counted" doesn't say how a dropped *read* is counted.

Both have one-line fixes. The deferred items (33) don't block the sort or the replayer: meshsim models no
memory system and no sending peripheral today, so there is nothing to port that depends on them.

## 2. Status of the round-2 findings

| finding | status | evidence |
|---|---|---|
| W2-24 stream count | **closed** | 17 bits, [16:0] (D1107-D1110); 4's table matches. |
| W2-25 K | **closed** | The formula is in 32 (D1157). Section 3 still says "K = 8 for now" (D148); see the short list. |
| W2-26 table window after done | **closed** | Guaranteed by K, with a meshsim check (D1158). |
| W2-27 big-SRAM images | **closed** | Counted, after the first command (D1128-D1130). The new ordering in step 5 raises W2-37. |
| W2-28 tag-window fan-in | **open, deferred** | Recorded as a constraint on the file system (D967), deferred by 33 (D1175). Not needed for the sort or the replayer. |
| W2-29, W2-30, W2-05 | **open, deferred** | 33 (D1175-D1178). |
| W2-31 sending peripherals' ports | **open, deferred** | 33 (D1179). Only C&C 1-3's engines send, and none of them is modelled yet. |
| W2-32 misaligned engine accesses | **partly closed** | 32 says dropped, still counted, [1] (D1160). How a dropped read is counted is undefined: W2-38. |
| W2-33 big SRAM out of range | **closed** | Undefined; meshsim errors (D1161). |
| W2-34 alignment of pointers and runs | **closed** | Stated per register in the spec update (D1162). |
| W2-35 counter codes 5-7 | **closed** | Treated as uncounted, and meshsim errors (D1163). 30 still says "undefined in hardware" (D1101); see the short list. |
| W2-36 stale text | **closed** | Fixed in place. One leftover is in S1 (short list). |
| Round 1 still partly open in round 2 (W2-01, -06, -11, -17, -20) | **closed or deferred** | W2-01 by 32's K; W2-06 by 32; W2-11 and W2-17 by 31's counted boot data. W2-20's remainder is W2-31, deferred. |

## 3. Blocking items

### W2-37: 31's step 5 makes every tile raise "done early"

**Decisions:** 31 (D1128-D1130), 18 (`ERRORS` [8], D702). **Spec:** 4.12, 6.2, 6.4.

Step 5 reads: "C&C 1 broadcasts how many words it will send and lands a command; each tile arms its counters
and signals READY; C&C 1 sends the data counted". A tile signals READY by pulsing `done`, with READY firing on
`done` and "command landed" together. If the tile arms its counters before that pulse, `done` comes with a
counter non-zero, which is the **done early** error (`ERRORS` [8]). That error raises the hard interrupt, and
under 35 it makes the chip toast. If the tile arms after `done` instead, it is inside the `done` gate and can't
run until GO.

v1.1 already had the working order, for announced staging (6.2, 6.4):

1. the command announces the data;
2. READY;
3. GO;
4. each tile arms its counters in the start path;
5. C&C 1 sends no sooner than `stage_delay` after its GO store;
6. each tile waits on `WFI` for counters done, then pulses `done`.

**Suggested resolution:** rewrite step 5 in that order, citing 6.2's `stage_delay`. W4 phase 4 then builds it
unchanged.

### W2-38: how a dropped misaligned read is "still counted"

**Decisions:** 32 (D1160), 3 (one decrement per cycle, at the director).

For a write through a misaligned `PTR` the rule is clear. The packet passed the director, its decrement is
already scheduled, and only the write is dropped. For a misaligned *read*, nothing reaches the counter that is
waiting. Three engines do such reads:

- the address generator, with a `STREAM_SRC` not aligned to its read size;
- the GATHER engine, with an odd `DMA_RECORD_BASE_ADDRESS`;
- the read port.

The counter that waits is at the receiver, or at the tile itself for the read port and gather values.
"Still counted" can only mean one of two things:

- a packet is still sent, or
- the engine decrements a counter directly. That would be a second decrement source, which 3 rules out.

**Suggested resolution:** a misaligned read is not performed, but its packet still goes out as usual with
payload 0, so the receiver's director counts it. `ERRORS` [1] is set at the tile that issued the read. This
keeps one decrement point and keeps lockstep timing.

## 4. Short list (non-blocking)

- **K's value (D148 vs D1157).** Section 3 says "K = 8 for now (a parameter)"; 32 says K is the formula, which
  gives 5 at the default L = 1. *Proposed:* K is a parameter with default 8. meshsim errors if it is set below
  the formula.
- **S1 (D1251)** still says "(K = 8 after the director)" and counts "the controller's reaction, at least 4
  cycles". After 32 the guarantee rests on K's formula alone.
- **Counter codes 5-7 (D1101).** 30 says "undefined in hardware"; 32 says "treated as uncounted". The later
  section wins, but 30 isn't marked superseded.
- **4's count of used bits (D182).** It says "87 used"; with a 17-bit count it is 88.
- **The boot ROM's jump address (D1144-D1145).** "The reset selects command block 0" names something the design
  doesn't define: there is one command block, with parity-selected argument slots. Say which slot holds the
  32-bit address. *Proposed:* slot 0 holds the address itself, since the ROM doesn't decode a command word.
  "Uses registers only, never data memory" should read "never stores to data memory", since the ROM must load
  that address from the command block.
- **Gaps in W4's prompt, in my area.**
  - Phase 1 doesn't name section **4** (the `STREAM` layout: *d* moves to `STREAM_COUNT`, the gap to
    `STREAM_SRC`, the 17-bit count) or **17**'s rules (uncounted traffic is mode 0; `PTR[64]` no longer
    special).
  - Phase 5's meshsim checks omit **32's table-window-after-done check** and **27's "uncounted traffic is
    mode 0"**.
  - Phase 1 should also name **32's misaligned-engine rule** (W2-38) and counter codes 5-7 as uncounted.
- **Outside my area, for WORKER1/3.** C&C 1 also boots through the ROM after a whole-chip reset (34, 35), but
  31's ROM branches only for tiles and for C&C 2/3. C&C 1's own image path (W1-27) is listed both as deferred
  (D1173) and as dissolved (34). W4 will need a harness rule, such as "meshsim preloads C&C 1's image, as the
  host would".
