# WORKER3 review: the v1.2 decisions — registers, peripherals and the software impact

Scope: `wip/planning/DECISIONS-v1.2.md` sections 7-14, 16 and 18 (with 9, 10 and 15 where they define the
peripheral windows), pending Q11-Q12, spec 4.11, 4.17, 4.19 and 8, and the code under `wip/sw/`,
`wip/sort/` and `wip/meshsim/`. Citations: **D** *n* is a decision section and D l.*n* a line of the
decisions log; **S** *x.y* and S l.*n* are `wip/spec/machine-spec.md`; **PN** is
`wip/planning/PLANNING-NOTES-2026-10-27.md`. Running notes: `NOTES.md`.

## 1. Summary: the five findings that matter most

1. **W3-01 (blocker).** C&C 1's peripheral errors can never clear. C&C 1 can't be halted, so one UART framing error or RX overrun holds C&C 1's hard interrupt and the C&C pin high until chip reset.
2. **W3-02 (blocker).** The standard window's `EVENTS` holds sticky events only, but the replayer's four low-water lines are live with credit on claim. As decided, a low-water line fires once and never again, and the pass stalls. The UART's live events have the same problem.
3. **W3-03 (issue).** Replacing `wait_tables()` with a `WFI` on DMA run done changes a state poll into an edge wait. Several of the sort's 15 call sites then either hang (no run has happened since the event was last consumed) or pass early (a stale, never-consumed event).
4. **W3-04 (issue).** `ROUTER_ADDR` auto-advances, so v1.2 replies and per-destination exchanges need an extra store per destination. This contradicts D7's "a change of dst alone, the common case in replies, is still one store". The advance rule for the new modes is also undefined.
5. **W3-05 (issue).** v1.1's "mode 3 to C&C is write-stream data" is still assumed in 4.2, 4.11, 6.6, 6.7 and invariant 17. In v1.2, mode 3 is the augmented write, and nothing says how the memory system's write stream, or any packet to endpoint 64, is recognised.

## 2. Findings

### W3-01 — C&C 1's sticky peripheral errors are permanent (blocker)

*D10 (l.313), D15 (l.441-457), D1.5 (l.83-87); S 4.13 (l.919, l.941), 4.20 (l.1675, l.1683), 8.13 (l.2125).*

The decisions chain together as follows:

- D10 makes a peripheral's `ERRORS` "sticky until HALT".
- D15 puts the UART (and Ingress/Egress) in C&C 1's peripheral, whose own errors "take the other bits and stay sticky" (l.453).
- Bit 64 of `RUN_LINES` is unused, so "C&C 1 cannot halt itself" (l.450), and nothing else halts it.

The consequence is that any error of C&C 1's own peripheral is permanent:

- It sets C&C 1's per-peripheral tile error.
- That error raises C&C 1's hard interrupt (level, so it never stops pending).
- It also holds the C&C pin high, which the host must read as "C&C 1 has failed" (D1.5).

The UART's RX overrun and framing error are line events, not program errors. A noisy console cable or a host that types too fast would produce exactly that state. This breaks the principle of 4.13 (l.919: an error means the software did something wrong, or the hardware is faulty). It matters most at bring-up, since PN l.208 has the C&C 1 ROM booting "from LVDS or the UART".

In v1.1 these were status bits in `UART_STATUS`, cleared by writing 1 (S l.2125), and raised no condition.

*Suggested resolution:* keep the UART's line errors as write-1-to-clear status bits in peripheral-defined words, not in `ERRORS`. Also decide how any genuine error of C&C 1's own peripheral is cleared, since HALT never reaches it.

### W3-02 — Peripheral `EVENTS` can't carry the replayer's credit lines or the UART's live events (blocker)

*D9 (l.294-295), D10 (l.316, l.324-330); S 4.17.3 (l.1018-1030), 4.19.8 (l.1457-1465), 4.19.9 (l.1475-1506), 4.20 (l.1679), 4.12 (l.875-878).*

D10 defines word 5 as "sticky events; a load consumes those in `EVENTS_MASK`". v1.1's slot gave a peripheral three kinds of line (4.17.3), each with its own claim:

| kind | what a claim does |
|---|---|
| sticky event | clears the event |
| live queue condition | credits one unit of service |
| live item FIFO | pops the next item |

The replayer depends on the second kind. Each low-water line is true while capacity − occupancy − pending ≥ half, and claiming it adds half to pending (4.19.8, l.1459-1462).

As a sticky event, the line would be set once on the condition's rise and consumed by the controller's load. If the queue is still at least half free after the credit, the condition never falls and rises again, so the event never re-fires. Refills stop, the queue drains, and the pass stalls with no error. The UART's RX not empty and TX not full (4.20 l.1679) are live as well, and have no place in a sticky-only word.

D9 says what the bit means "is the peripheral's own business" (l.295), which suggests the intent was to allow this. However, D10's table and l.330 ("loading `EVENTS` consumes events, as on the tile") fix the semantics as sticky.

Two smaller points on the same words:

- With `EVENTS_REQUIRED` tile-only (l.329), a program can no longer require two of one peripheral's events together. 4.12 l.885 relied only on run done + peripheral done, which still works.
- The peripheral `EVENTS_MASK` reset value, and what HALT does to it, are not stated (see W3-14).

*Suggested resolution:* carry 4.17.3's three kinds into the standard window, defined per bit by each peripheral, with "a consuming load claims one unit" as the general rule. Then restate 4.19.9's loop in two loads: the tile's `EVENTS` for GO, then peripheral 1's `EVENTS` for the claims. The squelch becomes clearing the tile's `EVENTS_MASK` [9].

### W3-03 — `wait_tables()` → events: a state poll becomes an edge wait (issue)

*D17 (l.494-513), D16 [6] (l.477); S 4.10 (l.740), 4.12 (l.874, l.892), 6.7 (l.1880); `sw/mesh.h` l.414-418.*

`wait_tables()` polls `rd_running | ts_busy`, so it returns at once whenever the tables are free, however many times it is called. D17's replacement is `WFI` with `EVENTS_REQUIRED` = counter *n* done | DMA run done. DMA run done [6] is a sticky edge, and v1.1 cleared it only by a consuming load and HALT → RUN (S l.874, l.892) *(inferred: no other clear is listed; the `done` pulse clears only the counter events)*.

That gives two failure modes:

- **Hang.** Some sites have had no run since [6] was last consumed, or have had no run at all. Examples are `config.c:13` and `local.c:32, 46, 55, 95` at the start of a configure, and `main.c:30` before `ov_load`'s own run. A `WFI` there never wakes.
- **Early pass.** `wfi()`'s reset mask (GO | counters done) doesn't consume [6]. So a [6] left over from an earlier run satisfies the next wait while the current run is still reading `STREAM`.

The counter half has its own corner case. A tile that expects no arrivals (`if (S.n_recv) wfi();`, transfer.c l.98, l.110) has no armed counter, so "counter *n* done" never fires. D17's single recipe doesn't cover that case.

Guarantee B also changes when [6] fires (after the last table read), while `STATUS` [0] `rd_running` keeps v1.1's end (the last entry walked). After v1.2, the two disagree by up to `ts_engine_latency` + 1. 6.7 l.1880 ("the source buffer may be overwritten once `rd_done`") should then say which one it means.

*Suggested resolution:* give the software rule in full:

- put [6] in `EVENTS_MASK` as well as `EVENTS_REQUIRED`, so the wake's load consumes it;
- arm a counter only when arrivals are expected, and wait on [6] alone otherwise;
- before the first run, or right after a consumed [6], the tables are free without waiting.

Alternatively, have `PULSE_DMA` clear [6]. Either way, `mesh.h` needs a helper that encodes the rule.

### W3-04 — `ROUTER_ADDR` auto-advance costs a store per destination; the advance rule is undefined (issue)

*D7 (l.248-256), D6 (l.230-231), D5 (l.204); S 4.11 (l.788); `sort/program/sw/exchange.c` l.12-16, `bt_run.c` l.70-74, `bt_plan.c` l.93-105, `sort/split/split.c`, `meshsim/sw/tests/rcp.h` l.38.*

In v1.1 a reply (mode 8) ignored addr, so a one-word exchange cost one `ROUTER_SET_HDR` and one send per destination. The `MODE_CM` sends to word +`me` (bt_descs) were the same: the header store reset the address.

In v1.2 the reply is a mode-0 write to the reply block + 8·`me`, and `ROUTER_ADDR` advances at each send. Every destination therefore needs `ROUTER_HDR` (dst), then `ROUTER_ADDR` (reset), then the send: three stores instead of two. That is 64 extra stores per exchange, in the sort's `xchg`, `bt_phase` and both loops of `bt_descs`, and in the split's per-round reply sends (`split.c` l.308, l.345-363). D7's claim that a change of dst alone is still one store in replies (l.255) is wrong for exactly the case it names.

D7 also says the address "auto-advances at each send as today" (l.248), but today's rule is keyed to v1.1 modes: +size for 3, 6 and 9, +1 for 7, unchanged otherwise (S l.788). The new rule is left open on three points:

- the units of the advance (Q10);
- whether modes 1-4, which don't use the address, advance it;
- whether an offset that carries past 0xFFFFFF changes the region.

*Suggested resolution:* state the new rule (advance by the size in Q10's units for mode 0; no advance for modes 1-4; no carry into the region). Then either accept and record the extra store, or add a spare `ROUTER_HDR` bit, e.g. [17] "hold address", that suppresses the advance.

### W3-05 — v1.1's mode 3 to C&C has no v1.2 equivalent (issue)

*D6 (l.211-218); S 4.2 (l.354, l.370, l.381, l.386), 4.11 (l.792), 6.6 (l.1832), invariant 17 (l.2159); `meshsim/sw/tests/rcp.h` l.99.*

v1.1 identifies a tile's write stream to the memory system as "mode 3 to C&C", with *d* = 64 allowed only with mode 3. C&C's port ignores mode and address for everything else (6.6 l.1832). In v1.2, mode 3 is the augmented write (D6) and the explicit write merged into mode 0.

Nothing says how C&C's port now tells write-stream data from results and errors, or what a mode-0 packet to endpoint 64 means once C&C 1 "reuses the tile controller design" with a 32 KB region 0x00. It could land in its data memory, go to its receive FIFO, or go to Ingress/Egress at region 0x03. `rcp.h` sends results to C&C with `MODE_REPLY`, relying on "C&C's port ignores the mode and the address".

*Suggested resolution:* decide endpoint 64's map in v1.2 (which region or address range is the memory system's write stream, and which reaches the receive FIFO), and rewrite the four spec passages and invariant 17 to match. *(Also for WORKER2: packet path.)*

### W3-06 — The live tile bit has no defined latency against the peripheral's consuming load (issue)

*D10 (l.326-328), D16 (l.480-482, l.489-490); S 4.11 sampling rule (l.758), section 12 (event flops, cited in 6.4 l.1801).*

The tile's `EVENTS` [9]-[11] are "high while the peripheral's `EVENTS & EVENTS_MASK` is non-zero". A program claims with a load of the peripheral's `EVENTS`, whose side effect lands at the end of *t* + *L* − 1, and then executes `WFI`.

Nothing says when the tile bit falls. If it is registered at its source or crosses into the control island through a flop, as event pulses do (6.4 l.1801), the `WFI` sees the stale 1 and falls straight through. v1.1 had one register, so this window did not exist (4.12's "no stale wake").

*Suggested resolution:* require the tile bit to reflect the peripheral's state as of the consuming load's side effect by the next instruction. Alternatively, accept spurious wakes and say so, since the replayer loop then runs with `ev` = 0. Extend the sampling rule to the peripheral windows (W3-15).

### W3-07 — `STATUS` renumbers its fields, though D18 says positions are kept (issue)

*D18 (l.517, l.532-542); S 8.2 (l.1969-1980).*

D18 opens with "v1.1's positions kept wherever a field survives", but every surviving field after [2] moves:

| field | v1.1 | v1.2 |
|---|---|---|
| `send_busy` | [5] | [3] |
| `mem_idle` | [7] | [4] |
| `cm_idle` | [8] | [5] |
| `done` | [9] | [6] |
| command landed | [10] | [7] |
| `request_busy` | [11] | [8] |

`PARAMS` and `ERRORS` do keep their positions. The cost of either choice is small, since D17 makes `STATUS` diagnostic. However, the field macros, the tests that poll `STATUS` (`cond.c`, `regs.c`, `rdport.c`, `wfi.c`, `rcp_status.c`) and `replay_regs.h` all hard-code positions.

*Suggested resolution:* either keep the v1.1 positions, leaving [3], [4] and [6] spare, or strike the sentence.

### W3-08 — `STREAM_COUNT` [31:25] is a relative *d*, not a dst (issue)

*D4 (l.170), D12 (l.382-393); S 4.2 (l.367, l.385, l.437).*

D4 names `STREAM_COUNT` [31:25] *d*. D12 lists the same bits as "dst" and says every header register lays it out the same way. In v1.1, *d* is relative for 0-63, so a packet goes to (`me` + *d*) mod 64, and it is absolute only for 64-125. That is what makes every tile's stream table identical for the 16-group schedule (S l.385). `ROUTER_HDR` and `DMA_HDR` carry an absolute dst.

The field sits at the same bits, but the hardware must add `me` for streams. "One header layout" (D12) is true of position but not of meaning. If it were read as absolute, the schedule's tables would differ per tile.

*Suggested resolution:* name the field *d* in `STREAM_COUNT` (relative, as 4.2) and state the difference in D12. D2's 7-bit copy covers only size, counter and mode, so it isn't affected.

### W3-09 — Data memory at 0x00000000 loses the free NULL check, and the zero header lands there (issue)

*D5 (l.185), D6 (l.225-227); S 4.11 (l.765, l.773).*

In v1.1, address 0 is instruction memory, where any data access is a bus error (S l.773), so a NULL dereference in C stopped the controller. In v1.2 it is the first word of data memory and silently valid.

The same word is where D6's accepted consequence puts an all-zero header's write: a `STREAM` entry, `ROUTER_HDR` or `DMA_HDR` left at 0 overwrites data word 0 unseen. Whatever the linker places at 0x00000000 (`.rodata`, under the current `sort.ld` and `app.ld` layout moved to 0) is therefore the most exposed word on the tile. The command and reply blocks must also stay away from it, since a slot-0 write now sets command landed (D6).

*Suggested resolution:* reserve the first words of data memory in the linker scripts (a guard, never read), with meshsim flagging any access to them. Record that the command block stays at the top of data memory (PN l.258).

### W3-10 — Reset PC, loader and handler placement are undefined in the new map (issue)

*D5 follow-on (l.201-203), D1.4 (l.62-67); S 4.11 (l.765, l.794), 6.1 (l.1729, l.1732), 6.5 (l.1813); PN l.208.*

D5 lists the reset PC, the loader address, the linker script and `mesh.h` as changing, but gives no values. Four more consequences follow:

- **The handler must be resident.** D1.4's handler (reads about 50 words, sends about 17 reports) is resident code that must survive every overlay swap (6.5: "the code doing the swap must sit outside the region being overwritten"). The 64-word loader region (S l.794) can't hold it, so the resident region and the overlay base must be laid out together.
- **mtvec.** It must point into region 0x01, and its value before the loader sets it decides what an error during boot does *(the reset value: Q1, Tony)*.
- **C&C 1's start.** C&C 1 has no RUN line (D15), so its start PC is not HALT → RUN's.
- **C&C 1's boot ROM.** The 16 KB ROM copied into C&C 1 (PN l.208) must be linked at 0x01000000 and reach the UART through its peripheral window.

There is also a simplification. The sort's kernel (2,072 B) plus its overlays (about 19.7 KB, `kit/image/sort.sym`) fit in 32 KB, so the sort can drop its overlays, as PN to-do 2 (l.269) expects.

*Suggested resolution:* fix the loader's and handler's addresses at the top of region 0x01, the HALT → RUN PC, mtvec's convention, and C&C 1's start PC in one place (spec part N).

### W3-11 — Can the controller load or store region 0x01? (question)

*D5 (l.197); S 4.11 (l.749-751, l.761-773), 8.1; `REGISTERS.md` s.1.*

D5 says "the controller sees 0x00, 0x01 and 0xFF". v1.1 is strictly Harvard: instruction memory is fetch-only, and a load or store there is a bus error (S l.773). If "sees 0x01" means only fetch, that should be stated. If it means loads and stores, then:

- the Harvard text, the bus-error list and `REGISTERS.md`'s map change;
- `.rodata` could move to program memory;
- overlays could be copied by the controller itself;
- instruction memory gains a port-A data path.

*Suggested resolution:* state "fetch only; region 0x01 takes only packets and fetches" unless the owner wants the data path.

### W3-12 — 6.4's "wait for `mem_idle` before `done`" contradicts D3 and D17 (issue)

*D3 (l.135-136), D17 (l.513); S 6.4 (l.1803), 4.14 (l.951-957), invariant 4 (l.2138).*

6.4 requires a tile that only sends to wait for `mem_idle` before `done`, because its own loopback writes count. D3 drops `mem_idle` and `cm_idle` from the done rule, and D17 makes `STATUS` diagnostic only. In v1.2, loopback writes are counted like any other if the sender gives them a counter, or are uncounted with counter 0. Uncounted ones have nothing to wait on.

4.14's "Counters-ready: `ts_busy` → 0" and invariant 4's "with `mem_idle` and `cm_idle`" go with D3 and D17, but neither decision lists them.

*Suggested resolution:* restate 6.4: loopback writes a phase must see landed are sent counted. Drop the `mem_idle` wait, 4.14's counters-ready paragraph and invariant 4's idle clause.

### W3-13 — Bad packets and unreachable addresses: the new rules aren't complete (issue)

*D5 (l.191, l.200), D18 [1] (l.552), D10 (l.338); S 4.17.6-4.17.7 (l.1057, l.1066), 4.11 (l.777), 4.19.10 (l.1524-1543).*

D18's bad packet is "bad size, misaligned address, or a region it can't reach". The following cases need a rule:

- a region 0x03-0x05 whose peripheral is absent (`PARAMS` [2:0] clear);
- reserved regions 0x06-0xFE;
- an offset beyond a memory's size within its region (region 0x00 is 32 KB of a 16 MB offset space);
- a write that runs off a memory's end (v1.1 checked 128-bit packets word by word, S l.777);
- the address field of modes 1-4, which don't use it.

v1.1's bad peripheral write [11] covered a mode-9 packet to an empty slot, and those were *counted* at once (S l.1066). If an absent peripheral's region is now a bad packet, it is dropped uncounted: the receiver's counter never reaches 0, though the error does report it. The replayer's own bad writes (S l.1543) move to its `ERRORS`. Its map (addr[20:18] selects the window, S l.1524) must be restated in the region's offset units, since with byte offsets the window bits become [22:20].

*Suggested resolution:* list the cases in 4.8 or 18. The obvious choice is a bad packet for every one except an in-window error a present peripheral detects, which goes to that peripheral's `ERRORS`. State whether a bad packet is counted.

### W3-14 — The standard window leaves standard fields unassigned (issue)

*D10 (l.309-323, l.338), D11 (l.373); S 4.17.2, 4.17.7, 4.17.9 (l.1097-1099), 8.1 (l.1957-1962).*

Generic code (the handler's dump, a boot check, meshsim's slot model) needs the following, which D10 doesn't define:

- **Bit numbers:**
  - `STATUS`'s standard bits (run in progress, DMA in progress);
  - the type, version and size fields of `PARAMS`;
  - a standard `EVENTS` bit for run done and for DMA done (v1.1 had sender idle at [20], standard on every tile).
- **Busy and configuration rules:**
  - what a `PULSE_RUN` or `PULSE_DMA` store does while that unit is busy (v1.1 ignored it, with meshsim flagging it);
  - whether `DMA_HDR` and `DMA_ADDR` may change during a transfer, and what a load returns mid-transfer;
  - a count of 0.
- **Reset and HALT:**
  - the peripheral `EVENTS_MASK` reset value, and whether HALT or HALT → RUN resets it as the tile's is reset (S l.879).
- **Accesses outside the defined words:**
  - reserved words 10-31, peripheral-defined words a peripheral doesn't define, and words 4, 8 and 9 on a DMA-less peripheral;
  - a whole window of an absent peripheral;
  - the controller block's reserved 0x874-0x9FC. v1.1's map had no holes, unmapped meant bus error, and undefined `PERIPH_STATUS` read 0 (S l.1957-1959, l.1036).

*Suggested resolution:* add a small table of standard bit positions and an access rule. A natural rule: reserved and undefined words read 0 and ignore stores, meshsim flags them, and an absent window is a bus error.

### W3-15 — Spec rules that must extend to the peripheral windows (issue)

*D9-D10; S 4.11 (l.757-759), 4.17.5 (l.1045), 4.17.8 (l.1071); D3 (l.139).*

The following v1.1 rules are written for the tile's own register block and need restating for the windows:

- **Register-load latency** (l.757) is "fixed per register for a given implementation". It must also hold for every peripheral's words, since a synchronised start may load them. Its rationale names the UART and peripheral as reasons for the 2-cycle option.
- **The sampling rule** (l.758) lists the side effects `WAKE_EVENTS`, `REQUEST_RECEIVE` and `UART_RX`. The consuming load of a peripheral's `EVENTS` and the UART's RX pop are now window accesses.
- **The store-stall list** (l.759) loses `SENDER_SEND` and moves `UART_TX` into C&C 1's window.
- **The landing requirement.** 4.17.8's "lands mode 9 a fixed `rp_latency` after the director" becomes "lands within K of acceptance". D3 fixes the peripheral at 2 cycles; with up to three peripherals, every one must meet it.

*Suggested resolution:* restate the four rules for "the register block and every peripheral window".

### W3-16 — Names: one name per level needs a header scheme; "DMA" means two things (nit)

*D8 (l.273), D10 (l.331-335), D14 (l.413-429).*

`PARAMS`, `STATUS`, `ERRORS`, `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK` and `PULSE_DMA` now exist at the tile and in each window. `mesh.h` is flat, so it needs a scheme, for example `P1_EVENTS` or `PERIPH_REG(n, EVENTS)`, and bit names such as `ERRORS_BUS` need a peripheral form.

"DMA" also names two different engines:

- the tile's address-generator run (`PULSE_DMA`, `DMA_CONFIG`, `DMA_RECORD_BASE_ADDRESS`, DMA run done [6]);
- a peripheral's output engine (`PULSE_DMA`, `DMA_HDR`, `DMA_ADDR`).

A reader of a flat `mesh.h` will take `DMA_HDR` to belong with `DMA_CONFIG`.

*Suggested resolution:* settle the header naming scheme with the register-table generator (`meshsim/src/Registers.java`). Consider renaming one of the two DMAs.

### W3-17 — Two big-SRAM address formats, and Q10's software consequence (issue)

*D4 (l.175), D5 (l.181, l.204), D11 (l.369), D13 (l.402); Q9, Q10.*

Whatever Q10 decides, big SRAM is addressed in two formats:

- **21-bit chunk addresses:** `PTR`, `STREAM_SRC` (Q9), `SRAM_RD_ADDR_QUEUE` and `DMA_RECORD_BASE_ADDRESS`;
- **32-bit region + offset:** `STREAM_DEST`, `ROUTER_ADDR` and `DMA_ADDR`.

Software converts between them; with byte offsets, that is `0x02000000 | chunk << 2`.

For the controller memories, Q10 decides the software's shape. With byte offsets, a C pointer into data or program memory *is* its packet address, and `CM_WORD`, `PACKED_WORD` and `cmWord` all disappear. With word offsets, every one of those sites needs region | (address >> 2). The affected sites are:

- `sort/program/sw/exchange.c`, `bt_plan.c` and `main.c`;
- `sort/split/split_regs.h`;
- `meshsim/sw/replay/replay_regs.h`;
- `meshsim/src/CnC.java`.

The Q list doesn't record this.

*Suggested resolution:* add the software consequence to Q10, and list each register's address format in 8.

### W3-18 — C&C 1's register set is only partly placed (question)

*D15 (l.438-447), D11 (l.358); S 6.6 (l.1821-1840).*

D15 places `ERROR_LINES_0`-`2` and `RUN_LINES_0`-`2` in C&C 1's peripheral, and D11 gives every controller `PULSE_GO`. The rest of 6.6's C&C registers are unplaced:

- the 64 sticky READY bits and their reset store;
- the receive FIFO's words (payload, src, size, valid);
- the phase watchdog;
- the host-interface registers;
- C&C 1's wake events (all READY set, receive FIFO not empty, watchdog). These are now presumably the peripheral's `EVENTS` bits.

*Suggested resolution:* list them as peripheral-defined words of C&C 1's window, or as reserved words of its controller block, before meshsim's `CnC.java` is rewritten.

### W3-19 — Three counters, no headroom for the replayer (question)

*D3 (l.130-131), D14 (l.422); S 4.19.13 (l.1616), 6.7 (l.831, l.897-898).*

v1.1 had four counters. A replayer pass can use the following at once:

- an LPDDR fetch outstanding;
- a refill run;
- a request's reply, if the fetch has one (6.7 l.831).

That is all three v1.2 counters *(inferred)*. The sort uses at most two at once (`bt_run.c` l.64-65).

*Suggested resolution:* confirm three is enough for the replayer's loop and the memory system's contract.

### W3-20 — `UART_CONTROL` (nit)

*D9 l.297.* D9 lists `UART_CONTROL` among v1.1's UART registers. v1.1 has `UART_DIVISOR` (S 8.13 l.2126), as D11 l.376 correctly says.

### W3-21 — PN to-do 13's deprecated alias can't work (nit)

*PN l.288-301; D7, D11 (l.375).* To-do 13 keeps `ROUTER_SET_HDR` as a deprecated `mesh.h` alias for one revision, and makes `SENDER_ADDR` R/W. D7 splits the header into two registers with a different layout, and D10 removes the sender, so neither part survives. Mark the to-do as superseded.

### W3-22 — Simulator addresses at the top of region 0xFF now collide with peripheral 3 (nit)

*D9 (l.292); `sort/split/split_regs.h` l.19, `sort/skew/harness/rv32emu.h` l.7; Q12.*

The split's ISS-only `TRACE_REG` and the skew harness's sentinel use 0xFFFFFFF0. That address is now word 124 of peripheral 3's window. Both are host-ISS only, but Q12's "simulator-only print register" needs an address that is not inside any window. A reserved word of the controller block, for example 0x9FC, is a candidate.

### W3-23 — Q12's options have little migration cost (question)

*D9 (l.298), Q12; S 4.20 (l.1685).*

No program calls `uart_putc` or `uart_puts` (they are defined only in `mesh.h`). The per-tile console is exercised only by `regs.c`, `CtrlTest`, `HooksTest` and `RegDocTest`, so either option costs little to adopt. The options differ elsewhere:

| option | works on hardware | cost |
|---|---|---|
| simulator-only print register | no: a bus error on hardware | prints must be compiled out of hardware builds |
| request-bus debug messages (6.7) | yes | a request-FIFO entry per message; C&C 1 must collect them |

The choice is the owner's.

## 3. Register accounting

D11's block (0x800-0x870) has no overlaps, and the four windows of D9 tile 0xFFFFF800-0xFFFFFFFF exactly. All of them stay within `x0`-relative reach (−2048 to −4), so register accesses remain one instruction. The table window at 0xFFFF2000-0xFFFF35FF overlaps nothing; it needs a `lui`, as v1.1's 0x2000 did. The rest of region 0xFF should be stated as unmapped (bus error).

Every one of v1.1's 40 registers is accounted for:

| v1.1 | v1.2 |
|---|---|
| `TILE_STATUS` | split: `PARAMS` + `STATUS` (W3-07); peripheral type to each window's `PARAMS` |
| `TILE_CONDITIONS` | `ERRORS` ([13:11] per peripheral) |
| `TILE_PULSE` | `PULSE_DONE`, `PULSE_DMA`, `PULSE_GO`; [2] to each window's `PULSE_RUN` |
| `ROUTER_SET_HDR` | `ROUTER_HDR` + `ROUTER_ADDR` |
| `ROUTER_SET_W0`-`W2`, `ROUTER_SEND_32`/`64`/`128` | unchanged, renumbered |
| `DMA_CONFIG`, `DMA_RECORD_BASE_ADDRESS` | kept, now R/W |
| `DMA_SRAM_`/`CM_`/`REPLY_`/`PERIPH_REMAINING` | `WRITE_COUNTER_1`-`3` (four become three) |
| `SRAM_RD_ADDR_QUEUE`, `SRAM_RD_DEST_ADDR` | kept; the latter re-laid out (D13) |
| `WAKE_EVENTS`/`PEEK`/`MASK`/`REQUIRED` | `EVENTS`/`EVENTS_PEEK`/`EVENTS_MASK`/`EVENTS_REQUIRED` |
| `REQUEST_*` (4 offsets) | unchanged, renumbered |
| `SENDER_SEND`, `SENDER_ADDR` | gone: a peripheral's `PULSE_DMA`, `DMA_HDR`, `DMA_ADDR` |
| `PERIPH_STATUS_0`-`7` | gone: each window's peripheral-defined words |
| `UART_TX`/`RX`/`STATUS`/`DIVISOR` | gone: C&C 1's window (W3-01, W3-20) |

The replayer's v1.1 interface maps onto peripheral 1 as follows:

| v1.1 | v1.2 |
|---|---|
| `PERIPH_RUN`, `periph_running` | `PULSE_RUN` and `STATUS`'s run bit (position unassigned, W3-14) |
| `PERIPH_STATUS_0`/`1` | two peripheral-defined words |
| service lines [18:13] | its `EVENTS` (W3-02) |
| mode 9 | mode 0 to region 0x03 (map units, W3-13) |
| conditions 11-13 | its `ERRORS` |
| `DMA_PERIPH_REMAINING` done ([3]) | a write counter's own done event |

## 4. Spec sections to update

| section | change |
|---|---|
| 2.1 / 3.1 | the 32-bit address and region map (D5); mode table (D6) |
| 4.2 | `STREAM` layouts (D4), relative *d* (W3-08), the counter field, *d* = 64 and mode 3 (W3-05), DMA run done's timing (D17 B) |
| 4.3 | the read port's destination: data memory only, counter field, the new layout (D13) |
| 4.10 | controller access rule by events (D17); window address 0xFFFF2000 (D11) |
| 4.11 | memory table and region map (D5), Harvard text (W3-11), packed words gone, send port (D7, D12), the auto-advance rule (W3-04), load latency, sampling and stall rules for the windows (W3-15), C&C's port (W3-05), the loader's address (W3-10), "no big-SRAM port" modes |
| 4.12 | command and reply blocks (D6, location), counters (D3), `EVENTS` layout (D16), live event kinds (W3-02), the uses list |
| 4.13 | `PARAMS`, `STATUS`, `ERRORS` (D18), the live bits list, condition → error names (D14) |
| 4.14 | counters-ready via `ts_busy` and the idle bits go (W3-12) |
| 4.17 | rewritten as the standard window (D9, D10): type, run, running, events, status words, peripheral writes by region, errors, HALT, requirements (W3-15), the sender becomes the DMA |
| 4.18 | `request_busy` and [7]/[8] renumbered (D16, D18) |
| 4.19 (intro, 4.19.8-4.19.13) | the replayer as peripheral 1: its window, its `EVENTS` and credits (W3-02), the controller loop, the region-0x03 map and units, counting by write counter, its errors, `PULSE_RUN` |
| 4.20 | the UART as part of C&C 1's peripheral (D9, D15); its errors (W3-01); the console (Q12) |
| 6.1 | HALT → RUN PC (W3-10), reset lists (sender gone, three counters, `EVENTS_*`) |
| 6.2 | boot loads by mode 0 to regions 0x00/0x01 with counter 0; marker as mode 0; staging by mode 1 at `PTR[64]` (v1.1 mode 2); `PARAMS` |
| 6.3 | `PULSE_DONE`, `PULSE_GO`, command writes as mode 0, `EVENTS` bits; "start and run act before done" goes (D8) |
| 6.4 | counters, `mem_idle` rule (W3-12) |
| 6.5 | block copy by mode 0 to region 0x01/0x00, a write counter, `PULSE_DMA` |
| 6.6 | C&C registers (W3-18), error lines (D15), REPORT goes (D1.6), receive path (W3-05) |
| 6.7 | counter fields in requests (Q8), mode 3 write stream (W3-05), `rd_done` reference (W3-03) |
| 8 (all) | rewritten from D11: 8.1 map and rules (holes, reserved words, W3-14), 8.2 → `PARAMS`/`STATUS`, 8.3 → `ERRORS`, 8.4 send port, 8.5 R/W, 8.6 pulses, 8.7 counters, 8.8 read port, 8.9 window address, 8.10 events; 8.11-8.13 become the standard window and the replayer's and C&C 1's words |
| 9 | invariants 4, 5, 11, 14, 15, 16, 17 and 18 restated (modes, counters, regions, windows) |
| 10 | K; `rp_latency` replaced by "within K" (W3-15) |
| 13 | items 44-48 and 53 (replayer map and status) reopened in region-offset units; item 82 (endpoint map) gains C&C 1's window |

## 5. Questions for the owner

1. Is region 0x01 fetch-only for the controller, or does it gain loads and stores (W3-11)?
2. Should a peripheral's `EVENTS` bits keep 4.17.3's three kinds (sticky, live with credit, live with pop), each defined by the peripheral (W3-02)?
3. How are C&C 1's own peripheral errors cleared? Are the UART's line errors errors at all (W3-01)?
4. Keep `STATUS`'s v1.1 positions, or renumber as D18's table does (W3-07)?
5. Accept the extra store per destination in replies and exchanges, or add a "hold address" bit to `ROUTER_HDR` (W3-04)?
6. What does a packet to endpoint 64 mean in v1.2, and how is the memory system's write stream marked (W3-05)?
7. Where do the loader, the trap handler and C&C 1's start PC go, and what is mtvec's convention (W3-10)?
8. Reserve a guard at the bottom of data memory (W3-09)?
9. Are three write counters enough for the replayer's pass with LPDDR fetches and replies (W3-19)?
10. Rename one of the two "DMA"s (W3-16)?

## 6. Migration inventory

Sizes: **S** small (a few lines or constants), **M** medium (a function or several call sites), **L** large (rewrite or most of the file). Removed behaviour is flagged in bold.

`meshsim/sw/mesh.h` equals `sw/mesh.h`, and `meshsim/sw/ext/sort/` equals `sort/program/sw/`, as checked by `meshsim/tools/check_layout.sh`. Each copy follows its original and is not listed again. `meshsim/sw/ext/split-bisect/` is the split with its barrier patch; `splitdrv.c` is its own.

### `wip/sw/`

| file | why | size |
|---|---|---|
| `mesh.h` (generated) | Every address and field changes: **`TILE_PULSE`** → `PULSE_*`, **`ROUTER_SET_HDR`** → `ROUTER_HDR`/`ROUTER_ADDR`, **`DMA_*_REMAINING`** → `WRITE_COUNTER_1`-`3`, `WAKE_*` → `EVENTS_*` (new layout), **modes 6-9** and `CM_WORD` go, IMEM/DMEM/LOADER/TABLE/COMMAND/REPLY bases move. **`wait_tables()`** goes (W3-03). `CMD_REPORT` goes. **`UART_*`, `uart_putc`/`uart_puts`** go. `SENDER_*` and `PERIPH_STATUS` go; window macros are new (W3-16). `tile_index()`/`sram_chunks()` read `PARAMS`. `wait_for_command()`'s asm uses `PULSE_DONE`'s offset. | L |
| `REGISTERS.md` | Generated tables and the prose of sections 1-5 and 7, including 2.8.2's replayer example. | L |
| `CONVENTIONS.md` | s.2 addresses, s.3 boot, s.4 commands (mode 7), s.5 waiting, s.6/6a sending and modes, s.7 status and conditions, s.8 block copy (mode 6), s.9 read port. | M |
| `RECIPES.md` | Nearly every recipe sends with `ROUTER_SET_HDR`, arms a named counter or calls `wait_tables()`. s.12 (status), **s.20 REPORT** goes. Kept in step with `meshsim/sw/tests/rcp_*.c` by `tools/recipes_sync.py`. | L |
| `regdoc/src/*.py`, `regdoc/svg/*` | The five worked examples use named counters and v1.1 headers; regenerate the figures. | M |

### `wip/sort/`

| file | why | size |
|---|---|---|
| `program/sw/sort.ld` | MEMORY origins (0x01000000 and 0x00000000, 32 KB each), the loader's place, a data-memory guard (W3-09). The overlays can go (W3-10). | M |
| `program/sw/loader.S` | `COMMAND_BASE`, the size from `PARAMS`, `PTR`'s address, its own address. Its **`ebreak`** on an unknown command now traps. | S |
| `program/sw/main.c` | `ov_load`: **mode 6** → mode 0 to region 0x01, `CM_WORD`, **`DMA_CM_REMAINING`**, **`TILE_PULSE`** start, **`wait_tables()`**. `CMD_STAGE` arming needs a counter id agreed with C&C (Q8). Simpler if overlays go. | M |
| `program/sw/exchange.c` | `xchg`: **mode 8** → mode 0 to `REPLY` + 8·`me`, one more store per destination (W3-04). `send_vector`: **mode 6**, `CM_WORD`. Counters. | M |
| `program/sw/bt_plan.c` | Tokens and credits (**mode 6**, `CM_WORD`). `bt_descs`' two per-destination loops with a fixed address (W3-04). Counters. | M |
| `program/sw/bt_run.c` | **`wait_tables()`** ×3, the reply loop (W3-04), counters. | M |
| `program/sw/transfer.c` | **`wait_tables()`** ×3, including after `if (S.n_recv) wfi()` (W3-03). Counters. | M |
| `program/sw/local.c` | **`wait_tables()`** ×5, **`TILE_PULSE`** start, mode constants. | M |
| `program/sw/skew.c` | **`wait_tables()`** ×2, counters. | S |
| `program/sw/config.c` | **`wait_tables()`**, mode constants. | S |
| `program/sw/sort.h` | Comments on counters and `wait_tables()`. `KEY_MODE` still works with the new constants (1 and 3). The comment that mode 3 overwrites `PTR[64]` is stale (that is bucket, mode 2 in v1.2). | S |
| `program/build.sh`, `program/patches/*` | Link addresses and overlay concatenation; regenerate the patches. | S |
| `split/split.c` | 10 sends through **`ROUTER_SET_HDR`** (replies and **mode 6**), 9 named-counter uses, **`TILE_PULSE`** done in its barriers. | M |
| `split/split_regs.h` | Hard-codes v1.1 addresses (0xFFFFF800-0xFFFFF848), **modes 6 and 8**, `CM_WORD` against 0x4000, `SR_REPLY` at 0x5BC0, `ME()` from `TILE_STATUS`. `TRACE_REG` collides (W3-22). | L |
| `split/split.h`, `split/BINDING.md` | Text on `TILE_PULSE` and replies. | S |
| `skew/planner/skew.h` | One `wait_tables`/table reference. | S |
| `skew/harness/*` | Host ISS with its own map. Changes only if `split_regs.h`'s addresses change under it (sentinel, W3-22). | S |

### `wip/meshsim/` (software)

| file | why | size |
|---|---|---|
| `sw/crt0.S`, `sw/replay/crt0.S` | `STACK_TOP`. The **`ebreak`** exit (meshsim's end-of-program convention) now traps. | S |
| `sw/app.ld`, `sw/loader.ld`, `sw/loader.S`, `sw/build.sh` | New origins and the loader's address. | S |
| `sw/replay/replay_regs.h` | Hard-coded v1.1 offsets, **`TILE_PULSE`**, **modes 6 and 9**, `PACKED_WORD`, the service lines as tile `WAKE_*` bits, the replayer map in 21-bit words. Replace with `mesh.h`. | L |
| `sw/replay/replay_ctl.c` | The 4.19.9 loop: claims through peripheral 1's `EVENTS`, squelch on the tile's [9] (W3-02); **`DMA_PERIPH_REMAINING`** → a write counter; `PERIPH_STATUS_0`/`1` → window words; `PULSE_RUN`; mode 9 → region 0x03; `wait_ag_idle()` polls `rd_running` (W3-03); polls `TILE_CONDITIONS`. | L |
| `sw/replay/replay.ld`, `replay_layout.h`, `build.sh` | Origins. | S |
| `sw/ext/split-bisect/splitdrv.c` | Mode and counter use. | S |
| `sw/tests/cond.c` | Conditions → errors (now trapping), **REPORT**, modes, `STATUS` polls (`mem_idle`, `rd_done`). | M |
| `sw/tests/rcp_selftest.c`, `rcp_rules.c` | **REPORT** ×5, `wait_tables()` ×9, counters, headers. | M |
| `sw/tests/regs.c` | The register map itself, **the UART**, `TILE_PULSE`, counters, `STATUS` positions (W3-07). | M |
| `sw/tests/rcp.h` | **REPORT**, a reply to C&C (W3-05), **modes 6 and 8**, counters. | M |
| `sw/tests/rdport.c`, `inv.c`, `wfi.c`, `search.c`, `a2a.c` | Read-port setup (D13), counters, `TILE_PULSE`, `STATUS` polls, `ebreak`. | S each |
| `sw/tests/` other 18 files | Counter names, `wait_tables()`, header stores, map constants. | S each |

### `wip/meshsim/` (simulator and tests)

| file | why | size |
|---|---|---|
| `src/Registers.java` | The register table that generates `mesh.h` and `REGISTERS.md`. | L |
| `src/RegDoc.java` | `REGISTERS.md`'s generator. | L |
| `src/TileControl.java` | Register decode, **`TILE_PULSE`** (start and run before done, l.259-264), wake events, **the UART**, the sender, service lines, conditions. | L |
| `src/TrafficDirector.java` | Mode decode → region decode, the counter field and the K delay line, **HALT suppressing counts** (l.58-89), command landed by address, reserved modes discarded. | L |
| `src/Controller.java` | The address map (`inData`, the fetch check), the reset PC (`LOADER_BASE`), traps and CSRs (W1). | L |
| `src/ControllerMemories.java` | 32 KB + 32 KB, port B by region, packed words gone. | M |
| `src/PeripheralSlot.java`, `src/Peripheral.java` | One slot becomes up to three standard windows (W3-02, W3-14), live tile bits (W3-06). | L |
| `src/Sender.java` | Becomes the peripheral DMA (`DMA_HDR`, `DMA_ADDR`, 12-bit count). | M |
| `src/Replayer.java` | Window words, credit lines in its `EVENTS`, region-0x03 map, its `ERRORS`. | M |
| `src/Uart.java` | Moves into C&C 1's peripheral; per-tile console replacement (Q12). | M |
| `src/CnC.java` | **The REPORT flow** (l.27-61, l.191), `cmWord`, **mode 7** commands → mode 0, the error bit → `ERROR_LINES`/`RUN_LINES` and per-unit halt, staging by mode 1. | L |
| `src/AddressGenerator.java` | `STREAM` layouts (D4), *d*, the counter field, `PULSE_DMA`, [6]'s timing (D17 B). | M |
| `src/LinkWord.java`, `src/WriteEngine.java` | 256-bit word, mode codes (W2). | M |
| `src/ReadPort.java`, `src/GatherEngine.java`, `src/TableStore.java` | Destination format and counter (D13), counter field (D17 C), window base. | S each |
| `src/Tile.java`, `Chip.java`, `Fields.java`, `Conditions.java`, `Config.java` | Wiring, constants, names. | S each |
| `test/HooksTest.java`, `PeripheralSlotTest.java`, `SenderTest.java` | **The UART**, pulses, the slot, the sender (the last becomes a DMA test). | L |
| `test/CtrlTest.java`, `CondTest.java`, `RecipeTest.java`, `RegDocTest.java` | Pulses and **combined `TILE_PULSE` fields** (`CtrlTest` l.274), **REPORT**, generated-doc checks. | M each |
| `test/NetTest.java`, `AgTest.java`, `ReadPortGatherTest.java`, `MemTest.java`, `EngineTest.java` | Modes (`NetTest` ×20), map constants, pulses. | M each |
| `test/RiscvTest.java`, `test/riscv/link.ld`, `riscv_test.h`, `build.sh`, `bin/` (144 prebuilt) | Relink at the new map and rebuild; PASS/FAIL by **`ebreak`** (W1). | M |
| `test/` other files | Map constants and names. | S each |
| `kit/`, `verify/`, `tools/` | Rebuild only *(inferred)*. `wip/kit/` and `wip/runner/` images, outside this inventory, are rebuilt too. | S |

**Removed behaviour, where it is relied on:**

- **Combined `TILE_PULSE` stores:** none in any program; only meshsim's `TileControl.java` (l.259-264) and a field check in `CtrlTest.java` l.274.
- **REPORT:** `mesh.h` `CMD_REPORT`, `CnC.java`, `rcp.h`, `rcp_selftest.c`, `cond.c`, `CondTest`, `RecipeTest`, `RECIPES.md` s.20.
- **Modes 6-9:** the sort (`exchange.c`, `bt_plan.c`, `bt_run.c`, `main.c`), the split, `replay_ctl.c`, `rcp.h` and most tests.
- **`DMA_*_REMAINING`:** throughout (63 code files, docs aside).
- **`wait_tables()`:** 15 calls in the sort, plus `rcp_rules.c` and `replay_ctl.c`'s `wait_ag_idle()`.
- **HALT suppressing counts:** `TrafficDirector.java` l.58-89 and `Peripheral.accept`.
- **The per-tile UART console:** `Uart.java`, `TileControl`, `regs.c`, `CtrlTest`, `HooksTest`, `RegDocTest`. No program prints.

## 7. For other workers

- **W1:** a bus error or illegal instruction both takes its synchronous trap and sets `ERRORS` [9]/[10], which raises the hard interrupt again. The handler's `WFI` park then runs with an interrupt level always pending.
- **W1:** C&C 1's own interrupt is the recovery entry for other units' errors, so it can't follow D1.4's report-and-park handler; nothing halts C&C 1.
- **W1:** `ebreak` as a program exit (`meshsim/sw/crt0.S`, `test/riscv/riscv_test.h`) and as the loader's unknown-command stop (`loader.S`) now traps; meshsim needs another exit convention.
- **W1:** with per-unit HALT (D15) and counting no longer suppressed in HALT (D3), a counted packet arriving at a halted unit raises that unit's error line while it is halted. Entering HALT clears errors only on the edge.
- **W1:** the replayer's queue overrun and invalid expansion now trap mid-pass. Whether the replayer stops is Q3.
- **W2:** modes 1-4 don't use the address: does the receiver check its region, and what do senders put there (W3-13)?
- **W2:** besides a 64- or 128-bit write covering slot 0 (D6), any mode-0 writer, read-port results included, now sets "command landed" if it lands on slot 0.
- **W2:** peripherals must land within K of acceptance, replacing `rp_latency` (W3-15).
- **W2:** the memory system's write stream (v1.1 mode 3 to C&C, *d* = 64) has no v1.2 marking (W3-05).
