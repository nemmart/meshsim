# WORKER22: questions on spec 4.21 (the Tracer)

The review of 4.21 that B1 makes as it builds it. Each entry gives the section, the question, my proposal and the owner's
answer. **Answered** entries are for the spec to be updated to match. The model follows the answer, or the proposal while
an entry is open. Spec and DECISIONS-v1.3 read up to 6990b94.

## Answered (owner, 2026-11-05)

**Q1. 4.21.5: what "not owned" means.** The read status table bases "not owned" on the page's leaf entry's owned bit. After
`cow_open` (§20), however, a shared index page's entries keep the owned bits they had in the source, so a leaf entry can read
owned = 1 inside a page this file doesn't own.
*Proposed:* ok only if every entry on the path from the root is owned.
**Answered: yes.** Status 0, ok, only if every entry on the path from the root is owned.

**Q2. 4.21.5: status 3, "beyond the file's length".** A request carries only (root, depth, line), and §20's memory system
holds no per-file state, so it can't know a file's length.
*Proposed:* status 3 means beyond the depth's capacity. A dense file's unused entries are holes anyway.
**Answered: yes.** Status 3 means a line beyond the depth's capacity, line ≥ 2^(5 + 10·depth).

**Q3. 4.21.3, the controller's loop: window 0's start.** The state service sends words 0 and 2–66 to tile *n* + 1, so
nothing sends tile 0 its record's start.
*Proposed:* C&C 3 sends it from its own copy of the record.
**Answered: yes.** C&C 3 sends window 0's start (words 0 and 2–66) to tile 0 from its own copy of the record.

**Q4. 4.21.3, the loop at n = 63.** All 68 words go to C&C 3's own memory, so tile 63 never gets words 1 and 67 (WORKER21's
report, finding 7, says C&C 3 forwards them; the text doesn't).
*Proposed:* C&C 3 forwards them.
**Answered: yes.** C&C 3 forwards window 63's words 1 and 67 to tile 63.

**Q5. 4.17.9 and item 118: the DMA's router port.** It has to be modelled for the Tracer's output to reach the tiles.
**Answered by the item 118 ruling (owner, 2026-11-05; spec 2.x, 4.17.9, item 118, ba64af1):**
- DMA packets carry src = the controller's id + 8, so the Tracer's are src 74;
- they enter the controller's router as an extra input and leave through its down port;
- 72–74 are sources only.

Still deferred: the rest of the controllers' routing and the DMA port's flow control. How meshsim models them is in
`PLAN.md` (ruling C), and Q11 below covers one case.

**Q6. 4.21.8: several errors in one row.** When a row is both misaligned and out of range, which bits are set?
*Proposed:* [10] and [12] together, and [8] and [9] only if the access goes ahead.
**Answered: yes.**

**Q7. 4.21.6: a zero-row run that starts inside an expansion.** The code FIFO holds "the first one again if the window
starts inside it", so does a zero-row run write that code value?
*Proposed:* no.
**Answered: yes, a zero-row run writes no code value.**

**Q8. 4.21.9: does a write-back make its line warm?**
**Answered: moot.** The memory system is now a shared cache (below), and a write-back puts its line in the shared cache like
any access.

**Q9. Item 149: the range comparator's upper bound.**
*Proposed:* use 8·K meanwhile.
**Answered: yes,** 8·K, until item 149 is settled.

**Q10. DECISIONS-v1.3 section 16, frame 6's row.** It still carried the old code map's description beside the (pc offset,
base) pairs.
**Answered: fixed** (ba64af1, 6990b94): the map's (key, base) pairs; frame 6's encoding is left to the host tools.

**The memory system's timing (owner, 2026-11-05), replacing 4.21.9's `ms_line_hit` and `ms_line_miss` definitions.** The
spec should follow. What 4.21.9 has now (6 cycles for a line requested since the last word-0 write, 80 for its first
request) becomes:
- the shared cache: 512 KB in 128 B lines, 8-way × 512 sets, LRU, write-back, addressed after translation by LPDDR address,
  holding data and index lines alike;
- no TLB (§21 (b)): translation is the walk through the §20 trees, each index read a shared-cache access;
- a hit in 6 cycles, a miss filled from LPDDR in 80;
- dirty evictions through a write buffer, off the requester's path unless it is full;
- one request outstanding, and cold at each segment's start.

**Q11. 4.21.3 and item 118: the 64th state service's transfers to C&C 3's own memory.** The DMA's packets "leave through the
down port", but these four transfers are addressed to C&C 3 itself.
*Proposed:* a packet whose dst is the controller itself is ejected there.
**Answered (owner, 2026-11-05; DECISIONS-v1.3 section 21, 8561c7c): routing by dst, with no special case.** Each transfer names
its destination: `DMA_HDR`'s dst 66, with `DMA_ADDR` in C&C 3's memory. A packet whose dst is its router's own unit is ejected
there like any arrival; every other packet leaves through the down port.

**Q12. The shared cache's write-back policy and its write buffer.**
*Proposed:* a 4-line buffer draining at one line per 80 cycles, a requester waiting only to evict into a full buffer.
**Answered (owner, 2026-11-05; DECISIONS-v1.3 section 21 (b), c190a09):**
- **The next victim in every set is kept free or clean.** When a set's LRU line is dirty, it is evicted at once, in the
  background, into the write buffer, leaving its way empty.
- A clean LRU line stays, and can still be hit. It is dropped at no cost when a fill needs its way.
- So a fill never waits for a write-back.
- The write buffer holds 4 lines and drains to LPDDR at one line per 80 cycles (parameters).
- **A read that misses the cache waits until the write buffer is empty**, so the buffer is never searched.
- Reported per segment: early evictions, write-backs and read waits. If the waits matter, a 4-entry address compare in the
  buffer is the cheap fix.

**Q13. A Tracer write-back that misses the shared cache.**
**Answered: yes, it allocates without an LPDDR read** (8561c7c). The Tracer writes whole lines, so the line is taken into a
free or clean way, marked dirty, and comes back from there on its next access.

**`CC_LINK_OUT` as the down port** (owner, 2026-11-05): fine. meshsim's controller link stays on the controller router's
`Router.CC_LINK_OUT` (W14-01), which is the spec's down port.

**Q15. 4.21.6 and 3.1: how C&C 3's deliveries into a tile's big SRAM are counted** (found building T2).

The rules involved:
- Big-SRAM writes are always counted (3.1, 6.2; invariant 22).
- A decrement of an unarmed counter is an unexpected arrival ([2], 4.12), so a receiver arms its counter, with the exact
  count, before the packets arrive.
- 4.21.6 sends tile *n*'s window straight into its memory: the state words (66 packets, from run *n* − 1, or from C&C 3's own
  copy for tile 0, Q3), the totals (words 1 and 67, from run *n*), and its three streams.

But a tile can't know its streams' sizes until C&C 3 has traced its window (words 37–39 at the rows' end), and window 0's
start goes out before the tiles have booted. The spec gives no arming protocol.

*Options:*
- **(a) Tell the tile, then wait for its "armed".** When run *n*'s rows end, C&C 3 sends tile *n* an uncounted command
  carrying its packet count (2 + the three streams' words), and waits for the tile's "armed" (a request, or a count-only
  packet back) before the first transfer to it. The start state's 66 packets (to tile *n* + 1) are known in advance, so the
  tile arms for them at its start, and C&C 3 sends them only once the tiles are up. It costs one round trip per window,
  during the drain.
- **(b) A margin.** The tile arms *M* at its start; C&C 3 tells it each transfer's size and finishes with count-only
  packets for the rest of *M*. This needs no round trip but costs packets.
- **(c) Land in C&C 3's memory and pull.** The later design of 4.21.6 sends to C&C 3's memory or LPDDR, and the tile
  fetches by its own requests, counted on its own counter. That needs no new protocol, but takes a second pass of the data.

**Answered (owner, 2026-11-05; DECISIONS-v1.3 section 24, 058a926): option (a), refined, as an intermediate step.**
- The record's pieces are fixed (66 words of state to tile *n* + 1, 2 words of totals to tile *n*), so each tile arms for
  them at boot.
- For the streams, a handshake: C&C 3 sends tile *n* its totals first; the tile reads the stream sizes from word 1, arms
  its counter for the streams and replies "armed"; only then does C&C 3 send the streams. That is one round trip per
  window, negligible against the drain, with the scans waiting on full queues meanwhile.
- The later design lands the windows in C&C 3's memory and LPDDR files instead; candidate C2 (signed counters) would remove
  the handshake.
- The interim exemption below is removed once the handshake works.

*The interim, until then (meshsim only):* T2 lets exactly these deliveries through uncounted (`Config.tracerDeliveriesUncounted`,
off by default, used only by `TracerT2`): the Tracer's DMA, src 74, and C&C 3's own Q3/Q4 packets into tiles' big SRAM.
Nothing else is exempt, and the rows check is unaffected.

**Q14. 4.21.3: a segment's start while C&C 3 was feeding an expansion** (found at 2²⁹, Quest 110, Cyclone segment 16,
which starts inside an inline).
- A run can end inside a controller-fed expansion, with C&C 3 still holding its template's next words.
- The next segment's word-0 write empties the expansion queue and sets the expansion credit to 2 (4.21.3). The Tracer then
  presents the expansion again, with `EXPANSION_OFFSET` at the new segment's next offset (4.21.4).
- If C&C 3 answers those fresh credits with the old template's next chunk before the new expansion is presented, stale words
  enter the queue ahead of the new ones, and the Tracer executes them as the expansion's rows. That is wrong execution with
  no error bit, or a queue overrun ([1]) when the stale chunk and the new expansion's two exceed 256 entries.

The spec's controller loop (4.21.3) doesn't say what C&C 3 does with an expansion in progress when it loads a record.

**Answered (owner, 2026-11-05): the rule.** **On loading a segment's record, C&C 3 drops any expansion it was feeding** (its template, position and pending
chunks), since the word-0 write resets the credits and the Tracer presents the expansion again. The firmware loop in 4.21.3
gains that line. meshsim's ideal controller follows it (37fb37e), and so will `tracer_ctl.c`.

**Q15, the barriers (owner, 2026-11-06; DECISIONS-v1.3 section 24, 9544e8e): the segment's two barriers are C&C 1's
READY/GO,** since C&C 1 handles global barriers and starts. This was ruled after the fan-in hang: 64 direct "booted"
packets to C&C 3 overflowed tile 2's port and dropped two (3.2 already rules that out).
- **(1) Before tracing:** each tile boots, arms its counters for the record's fixed pieces and raises READY; C&C 1 waits for
  all READY, then tells C&C 3 to start tracing. Tiles send nothing to C&C 3: "booted" is dropped altogether (the request-tree
  version, d6b4d8d, is superseded).
- **(2) After the last delivery:** C&C 3 reports done to C&C 1, which issues GO, and all 64 tiles start replaying together.
- The per-window "armed" stays a direct packet, one tile to C&C 3 at a time.
- How (1) meets 4.13's done rule is Q17 below.

**Q16. 4.21.3, 4.19.13: when a tile may start replaying its window** (from T2, with Q15's handshake).

4.21.3's controller loop doesn't say when the tiles start. The plan had the harness start all 64 at the segment's last
done, by a command from C&C 1.

With Q15's counters a tile knows by itself when its window is complete:
- its state counter (66 words from window *n* − 1, or Q3's for tile 0);
- its totals counter (words 1 and 67 from window *n*);
- its stream counter, armed from word 1.

When all three are at 0, everything it replays from has landed.

*Proposed (and as built, b64d9d3):* **a tile starts replaying its window as soon as its three counters are done**, while C&C 3
traces the later windows. Nothing else gates it: no command, and no wait for the segment's end. The tile's replay doesn't
touch C&C 3 or the other tiles' regions, and its controller's start-up (the tables, the templates) can precede it.

**Answered (owner, 2026-11-05): accepted**, as the departure from the plan. **Reversed (owner, 2026-11-06; 9544e8e): all 64
tiles start replaying together, at C&C 1's GO after C&C 3's last delivery** (Q15's barrier (2)); the replay's ~236K cycles
then follow the trace, negligible against its ~21 M.

The other departure, queue 0 sent in queue order so that the totals reach a tile before its streams but after the state
words, was accepted with Q15's ruling.

**Q17. Q15's barrier (1) against 4.13's done rule: READY with the record's counters still armed.**

A tile raises READY with its done pulse (`wait_for_command`, 6.3). But two rules stand in the way:
- **Done early:** 4.12 and 4.13 (invariant 4) make a done pulse with a write counter non-zero done early, `ERRORS` [8], a
  terminal error. The record's counters, once armed at boot, stay non-zero until window *n* − 1's state and window *n*'s
  totals have landed, which is long after the barrier.
- **The done gate:** from the done pulse until GO nothing but GO wakes `WFI`, and counter events can't be set (4.13). A tile
  that has raised READY therefore can't take its totals, arm its streams and send "armed" during the trace.

*Proposed:* keep the two barriers and the "nothing to C&C 3 but armed" of the ruling, and order each tile's steps so its
done pulses come with its counters at 0:
1. **Boot, READY.** The tile boots and raises READY at once, with nothing armed.
2. **First barrier.** C&C 1 waits for all READY and issues GO (a first phase), then tells C&C 3 to start tracing.
3. **Arm.** On that GO the tile arms its record's counters. C&C 3's first delivery (Q3, to tile 0) comes long after: C&C 1
   must first tell it to start, and C&C 3 then loads its tables and record. So arming wins by timing, as every phase's
   window between GO and arming already does (4.12, the owner's rule of 2026-10-29; signed counters, item 125, would remove
   the window).
4. **During the trace,** awake, the tile takes its totals, arms its streams, sends "armed", and waits for its counters done.
5. **READY again,** now with every counter at 0, which is legal.
6. **Second barrier.** C&C 3 reports done to C&C 1, which waits for that and for all READY, then issues GO, and all 64
   tiles replay together.

In the harness, standing in for C&C 1's sequencing, the host commands C&C 1's phase and GO and starts C&C 3 directly.
cc1.c gains no new command yet.

**Answered (owner, 2026-11-06): yes, this order,** with one guard:
- READY at boot with nothing armed.
- GO, after which each tile arms its counters for the record's pieces and C&C 1 starts C&C 3.
- The deliveries, with the per-window handshake.
- READY again when a tile's counters reach 0 (its window has landed); this replaces C&C 3 reporting done.
- C&C 1's final GO, for the replay.
- **The guard:** tile 0's first delivery (window 0's start, Q3) can come before it has armed after the GO, so C&C 3 waits
  for a single "armed" from tile 0 (for its record) before sending it. Every other tile's first delivery is a window later.

*As built* (v1.3-tracer, after 515c11a): C&C 3 arms the guard's counter at its own boot, long before C&C 1 boots the
tiles, and waits for its start as a landed command, with no done pulse, since that counter is armed. In T2 the host
sequences C&C 1's two phases and C&C 3's start.

## Open

(none)
