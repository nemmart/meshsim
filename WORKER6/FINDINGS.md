# WORKER6 findings: what writing out the v1.2 recipes found

Each finding was found by walking a recipe: what each tile and C&C 1 execute, in order, and what can arrive
when. Most important first. **Status** says how the decisions log answered it (D37, D38, D40, D41). The recipes
(`wip/sw/RECIPES.md`, R*n*) follow every answer and mark each finding where it bites as **[F*n*]**.
F1-F14 are the first round's, F15-F16 came from writing to D38, and F17 from round 2's boot by streams.
**All of F1-F16 are answered; F17 is open.** Bit numbers in the bodies below are as they were found; D40
renumbered `EVENTS` (counters done [7], GO [8], command landed [9], DMA run done [10]).

| # | finding | severity | status |
|---|---|---|---|
| F1 | GO reaches tiles still in the boot ROM, which jumped on any GO | high | **answered, D37.1** |
| F2 | Nothing enabled the hard interrupt | high | **answered, D37.2** |
| F15 | Three outstanding requests to C&C 2 can't all be answered in the reply block | medium | **answered, D40** |
| F3 | The fixed report set against per-tile peripherals | medium | **answered, D37.3** |
| F5 | Reports could wait on the request tree behind a stopped target | medium | **fixed, D38** |
| F4 | C&C 1's UART dump looked too slow for an FPGA reset | medium | **not a limitation, D37** |
| F6 | Polling `rd_running` is only as good as guarantee B | medium | **answered, D17 B (D41)** |
| F17 | A command from the send port isn't ordered after a stream's packets | low | **open** |
| F7 | A 128-bit overlay at address 0 writes the reserved word | low | **answered, D37.4** |
| F8 | A forgotten no-increment bit is a silent pile-up | low | **answered, D37.5** |
| F16 | The report count: 23 by D20's list, "about 17" in D1.4 and D38 | low | **answered, D41** (the spec defines the set; 24 with seven counters) |
| F9 | To claim one peripheral event, narrow the mask around the load | awkwardness | **accepted, D41** |
| F10 | `wait_all`'s cost, and the stale counters done it leaves | awkwardness | **accepted, D41** |
| F11 | One extra wake per expansion in the replayer loop | awkwardness | **accepted, D41** |
| F12 | C&C 1 has no timer for bounded waits | awkwardness | **answered, D41** (`mcycle`) |
| F13 | "Command landed" stays pending for ever in a tile | awkwardness | **answered, D41** (the spec says so; the ROM mustn't consume it) |
| F14 | A stream can't name the broadcast, so C&C 1 boots through its send port | awkwardness | **answered, D41** (*d* = 126) |

---|---|---|---|
| F1 | GO reaches tiles still in the boot ROM, which jumped on any GO | high | **answered, D37.1** |
| F2 | Nothing enabled the hard interrupt | high | **answered, D37.2** |
| F15 | Three outstanding requests to C&C 2 can't all be answered in the reply block | medium | **open** |
| F3 | The fixed report set against per-tile peripherals | medium | **answered, D37.3** |
| F5 | Reports could wait on the request tree behind a stopped target | medium | **fixed, D38** |
| F4 | C&C 1's UART dump looked too slow for an FPGA reset | medium | **not a limitation, D37** |
| F6 | Polling `rd_running` is only as good as guarantee B | medium | **open** |
| F7 | A 128-bit overlay at address 0 writes the reserved word | low | **answered, D37.4** |
| F8 | A forgotten no-increment bit is a silent pile-up | low | **answered, D37.5** |
| F16 | The report count: 23 by D20's list, "about 17" in D1.4 and D38 | low | **open** |
| F9 | To claim one peripheral event, narrow the mask around the load | awkwardness | open (accepted by D10) |
| F10 | `wait_all`'s cost, and the stale counters done it leaves | awkwardness | open |
| F11 | One extra wake per expansion in the replayer loop | awkwardness | open |
| F12 | C&C 1 has no timer for bounded waits | awkwardness | open (Q1) |
| F13 | "Command landed" [6] stays pending for ever in a tile | awkwardness | open; D37.1 now relies on it |
| F14 | A stream can't name the broadcast, so C&C 1 boots through its send port | awkwardness | open |

---

## F1. GO reaches tiles still in the boot ROM (high) — answered by D37.1

**What.** GO is one global net. D36's ROM, in its tile branch, pulsed `done`, waited for GO, and jumped to
slot 1. A tile whose overlays and first command haven't been sent yet is asleep in that same loop, so the next
GO meant for other tiles wakes it too. It consumes GO, reads slots 0 and 1 (data memory isn't cleared by the
reset) and jumps into garbage. The trap that follows sets no `ERRORS` bit (D27), so nothing reports it.

**Steps (R22).** One-at-a-time bring-up (D23), with `READY_EXPECT` = tile 5 only:
1. C&C 1 loads tile 5 and lands its command; tile 5 is READY; "all tiles READY" rises.
2. C&C 1 stores `PULSE_GO`.
3. Tiles 0-4 and 6-63, asleep in the ROM, wake on GO and jump to slot 1's stale value.

**Answer (D37.1).** After GO a tile's ROM jumps only if a command has landed; otherwise it pulses `done`
again and keeps waiting. R22's ROM tests the event [6] in the load that returned GO. It must be the event,
not `STATUS` [5]: GO clears the status bit, while [6] stays pending because the reset mask doesn't consume
it. **Rule that remains for C&C 1:** it lands a tile's first command only when the tile is to start at the
next GO, since a tile with a landed command jumps at the next GO whatever `READY_EXPECT` says (R22, R27).

## F2. Nothing enabled the hard interrupt (high) — answered by D37.2

**What.** `mstatus.MIE` is 0 at reset (W1-03), and the hard interrupt is gated by it and by its `mie` bit
(D20). D36's ROM doesn't set them, and nothing else in the log did. So an error would raise the tile's
error line (hardware, D1.5) but never trap: the handler wouldn't run, there would be no reports, and the
tile would carry on running a program that had already gone wrong.

**Steps (R22, R24).** Reset, ROM, jump, start-up, `main`. Then a counter is armed one too small; the extra
arrival sets `ERRORS` [2]; the hard interrupt rises, but `MIE` = 0, so no trap.

**Answer (D37.2).** Enabling interrupts is the image's job. Boot part 2, the first command (which receives
the big-SRAM data's size and loads it), sets `mtvec` to the image's handler and enables `mie.MEIE` and `MIE`.
R22's `boot_part2()` does this after its counter store, so the start path stays counter store first. Until
then an error still raises the error line and reaches C&C 1 at once. Which `mie` bit is the hard interrupt's
waits on Q1 (Tony's CSR list); the draft assumes MEIE, bit 11.

## F15. Three outstanding requests to C&C 2 can't all be answered in the reply block (medium) — answered by D40

**What.** D38 lets a tile keep up to 3 requests outstanding to the file system (C&C 2), each waiting for its
reply. A reply in the reply-block convention is a write to the receiver's `REPLY_WORD(src)`, one pair per
source (D6). All three of C&C 2's replies to one tile would land in the same pair, `REPLY[2·65]`: the second
overwrites the first unread. They can't share one counter either, if the tile is to know which request has
been answered. This found nothing wrong for C&C 1 and C&C 3: there, one outstanding request means one reply
in flight.

**Steps.**
1. A tile opens three log streams' tags: requests A, B and C to C&C 2, replies on counter 3.
2. C&C 2 answers A and B close together; both write `REPLY[130]`.
3. The tile sees counter 3 move by 2 but reads only B's answer.

**Suggested fix.** No new design needed: D25 already has requests carry "the destination as a full 32-bit
address" and their counters. Say in the file system's request format that **each outstanding request names
its own reply address** (for example, one of three fixed slots in the tile's data memory) and, if the tile
must tell them apart, its own counter. The reply-block pair remains for the one-outstanding targets. The
recipes don't send C&C 2 requests yet (the file system's format is deferred, D33), so nothing is marked.

**Answer (D40).** Every request names its own reply address, anywhere in the requester's data memory, beside its
reply's counter; the reply block becomes the default for tile-to-tile replies only. D40 also makes the counter
field's codes 1-7 all write counters. The recipes give each outstanding request a **slot** (R21's `request_issue`
and `request_wait`): slot *k* owns counter 5 + *k* and its own reply buffer. Three slots cover C&C 2's limit
(rule 11, CONVENTIONS 6b).

## F3. The fixed report set against per-tile peripherals (medium) — answered by D37.3

**What.** D20: the handler reports "for each present peripheral its window's standard words". D21: "the
report set is fixed", a constant of the protocol, counted by C&C 1 against it. Tiles differ in their
peripherals (the replayer only on some), so a set that depends on the peripherals present isn't fixed, and
C&C 1 couldn't count a tile's reports without knowing its peripherals.

**Answer (D37.3).** Every peripheral slot's words are sent, with zeros for an absent peripheral. R24's set is
23 requests: x1-x31, `mepc`, `mcause`, `mtval`, `PARAMS`, `STATUS`, `ERRORS`, `EVENTS_PEEK`, the four
counters, then three per slot. Of a window's ten standard words the handler reads seven: `PULSE_RUN` and
`PULSE_DMA` are write-only, and `EVENTS` consumes, so it reads `EVENTS_PEEK` instead.

## F5. Reports could wait on the request tree behind a stopped target (medium) — fixed by D38

**What.** Spec 3.3 (2026-10-26) accepted head-of-line blocking within a row: a request waiting for a target
with no credit holds its row node's link, and a tile's requests stay in order. D23 makes logging through the
file system the debugging path, so tiles send C&C 2 requests routinely.

**Steps.** C&C 2 stops popping (it failed, or is busy flushing) and its 16 credits run out. A tile then
fails with a log request queued ahead of its reports. Its reports can't pass that request, and every tile in
its row is blocked too. C&C 1's drain times out with those rows missing.

**Answer (D38).** The receive queues move into the request tree, 128, 256 and 128 deep, and a tile keeps at
most 1, 3 and 1 requests outstanding, each waiting for its reply. Normal traffic then can't exhaust a
target's credits, the root never skips, and no row stalls. Error reports are the exception and can fill C&C
1's queue in a mass failure (accepted). R21 and R27 follow the rule, R24 notes the exception, and rule 11 in
R15 states it.

## F4. C&C 1's UART dump looked too slow for an FPGA reset (medium) — not a limitation (D37)

**What.** D35's FPGA monitor resets about 0.1 ms after the toast pin rises. At 57,600 baud one report line
(36 characters) takes about 6 ms, so C&C 1's dump would be cut short.

**Answer (D37).** Not a limitation: with an FPGA in the loop, C&C 1 sends the dump down LVDS and the FPGA
waits as long as it needs (0.1 ms is a default); with no FPGA, a person resets slowly enough for the UART. R27
still drains the reports into data memory first, so C&C 1's 128-entry queue doesn't fill and stall rows
(D38). The code shows the UART because the host-interface words are deferred.

## F6. Polling `rd_running` is only as good as guarantee B (medium) — answered in D17 B (D41)

**What.** D17 lets a helper that can't know what ran before "poll `STATUS.rd_running`, a level", instead of
waiting for DMA run done, [7]. Guarantee B says [7] "fires only after the run's last table read has
completed, not when the last entry is walked". Nothing says the same of `rd_running`. v1.1's spec defines
`rd_running` as "until it has walked the run's last entry", which is exactly the earlier point B rules out
for [7].

**Steps (R23).** A run walks its last entry; `rd_running` falls; its last `STREAM` read is still in the
table pipeline. A helper polling `rd_running` sees 0 and rewrites `STREAM[0]`; the run's last element reads
the new entry.

**Suggested fix.** State in the spec (4.2, 8.2) that `rd_running` falls no earlier than [7] is set, i.e.
guarantee B for both. R23's poll is marked as valid only with that, and only when nothing counted can still
be arriving (the poll says nothing about `PTR`/`HIST` updates from arrivals).

**Answer (D17 B, D41).** `STATUS.rd_running` falls in the same cycle as DMA run done, so polling it means
exactly what the event means. R23 says so.

## F7. A 128-bit overlay at address 0 writes the reserved word (low) — answered by D37.4

**What.** D27 reserved data-memory word 0 and had meshsim flag any write to it. Boot overlays are 128-bit
packets (R22), so a data image laid from address 0 writes word 0 with its first packet.

**Answer (D37.4).** The first 16 bytes are reserved. Overlays start at 0x10, and meshsim flags any write to
0x00-0x0F after boot. R22 and CONVENTIONS 1 use 0x10.

## F8. A forgotten no-increment bit is a silent pile-up (low) — answered by D37.5

**What.** `ROUTER_HDR` [0] keeps `ROUTER_ADDR` from advancing (D7). Every later send lands at one address
until the next header store. A run of words sent after a reply exchange, with the bit left on, lands every
word on the first, counted and without error.

**Answer (D37.5).** The bit stays set until the program clears it, and the program clears it when done with a
fixed address. R7's `xchg` and R15's `self_reply` clear it (`tile->ROUTER_HDR &= ~ROUTER_NOINC`), and rule
10 says so.

## F16. The report count: 23 by D20's list, "about 17" in D1.4 and D38 (low) — answered by D41

**What.** D1.4 estimated "about 50 words, about 17 reports", and D38 sizes C&C 1's queue against "about 17
per failed tile". D20's list (tile registers, the counters, the trap CSRs, x1-x31) with three words per report
is 14 requests, and D37.3's three slots of peripheral words add 9. That makes 23 in R24's set: 1,472 for 64
failing tiles, against C&C 1's 128 entries.

**Suggested fix.** Fix the count when the spec defines the set (D21: "the spec defines the set"). The
difference doesn't change D38's conclusion (a mass failure fills the queue either way, accepted), but C&C 1
counts against the exact number.

**Answer (D41).** The spec defines the exact set, and C&C 1 counts against it; D38's conclusion stands. With
D40's seven counters R24's set is 24 requests (one more for counters 5-7).

## F9. To claim one peripheral event, narrow the mask around the load (awkwardness) — accepted (D41)

A load of a peripheral's `EVENTS` takes one count from every set, masked bit (D10). To consume one event
(DMA done, R25) without claiming another bit's credit, a program saves the window's mask, narrows it, loads,
and restores: three extra accesses. The replayer loop instead acts on every bit returned (R26), which is the
cheaper pattern where it fits. Recorded as the cost of D10's rule (W3-25's item kind would avoid it).

## F10. `wait_all`'s cost, and the stale counters done it leaves (awkwardness) — accepted (D41)

D17's wait for "counter *n* done and DMA run done" sets `EVENTS_MASK` and `EVENTS_REQUIRED` and restores
both: four stores around every such wait (R0). Counters done [4] has to leave the mask, since it would wake
alone. It is set anyway when the waited counter was the last at 0, and stays pending until the next arming or
`done` clears it. That is harmless under the rules (a count of 0 never fires; arming clears it), but a program
that later waits on [4] without arming would fall through. A cheaper alternative needing no
`EVENTS_REQUIRED` is a loop that ORs what each wake consumed. The helper follows D17's letter. D41 accepts the cost and allows the loop form in a helper.

## F11. One extra wake per expansion in the replayer loop (awkwardness) — accepted (D41)

When a new expansion is presented, its credits are set to 2 in the same cycle, but the expansion queue's bit
was out of the mask (its last expansion had nothing more behind its spare credit, D10). The load that
consumes "new expansion" therefore claims no expansion credit; the loop adds the bit to the mask and claims
the credit on the next wake, which is immediate. About 13 extra wakes a 64K-row window (spec 4.19.9), a few
cycles each (R26, step 6).

## F12. C&C 1 has no timer for bounded waits (awkwardness) — answered by D41

C&C 1's toast path drains reports "with timeouts" (D21), but its only timer is the watchdog, whose expiry
latches toast. R27 bounds the drain by a loop count. If Tony's CSR list (Q1) includes `mcycle`, a cycle bound is
cleaner.

**Answer (D41).** The core implements `mcycle` and `mtime`. R27 times the GO settle, `stage_delay` and the
toast path's drain by `mcycle` (the draft header's `mcycle()`).

## F13. "Command landed" stays pending for ever in a tile (awkwardness) — answered by D41

The reset mask (GO | counters done) never consumes [6], so after the first command it is always pending,
and every handler report's `EVENTS_PEEK` shows it. D37.1's ROM now relies on exactly this: the load that
returns GO also returns [6] unconsumed. Harmless; worth one line in the spec so nobody "fixes" it by
consuming [6] in the ROM's tile branch.

**Answer (D41).** The spec says so, and the ROM's tile branch must not consume it. R22's ROM keeps the reset
mask in its tile branch (and "command landed" is [9] since D40).

## F14. A stream can't name the broadcast, so C&C 1 boots through its send port (awkwardness) — answered by D41

C&C 1 is a tile instance with an address generator and 2 MB of big SRAM, so it could stream images at a
packet per cycle. But a `STREAM_COUNT` *d* is relative for 0-63 and absolute for 64-125, and can't name the
broadcast (126), and C&C 1's own id (64) makes relative *d* meaningless. So R22 sends overlays from C&C 1's
send port, four stores per 128-bit packet. That is acceptable for boot (a 64 KB image is 4K packets); worth
stating whether a stream on a controller may name 126.

**Answer (D41).** In hardware a stream's *d* may be 126, with no special case. By convention only the
controllers broadcast from streams, and meshsim errors on a mesh tile's. R22 now streams every image from C&C
1's big SRAM, a packet per cycle; the first command rides in the same run (F17).

## F17. A command from the send port isn't ordered after a stream's packets (low) — open

**What.** With D41, C&C 1 boots by streaming the overlays from its big SRAM. The natural next step is to
send the first command from the send port once the run is done, but nothing orders the two. DMA run done
(D17 B) says the run's last *table read* is complete, not that its packets have left: the last overlay
reads may still be in big SRAM's pipeline and the return path. The send port is the tile's other injection
source (D10: "the tile's injection point keeps its two sources"). Spec 3.2's in-order rule is stated per
source and destination, and doesn't say whether one tile's two injectors count as one source.

**Steps (R22).**
1. C&C 1 starts a broadcast stream run of the image; [10] fires when its last table read is done.
2. C&C 1 sends `CMD_JUMP` to slot 0 from its send port.
3. The command is injected ahead of the run's last data packets, lands, and raises READY.
4. GO: a tile jumps into an image whose last 16 bytes haven't landed.

**What the recipe does.** It puts the command in the same run, as its last stream: one 128-bit packet at slot
0 from C&C 1's big SRAM, carrying the command word and the entry in slot 1. The packet comes from the same
source as the overlays, and the run's reads return in order (S2). It needs no new rule.

**Suggested fix.** One sentence in 3.2 saying whether a tile's address generator and send port are one source
for ordering. If they are, the send-port form also works; if not, the spec states the same-run form as the
rule for boot (and for any command that must follow a stream's data).
