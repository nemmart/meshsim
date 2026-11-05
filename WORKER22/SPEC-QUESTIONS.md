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

## Open

(none)
