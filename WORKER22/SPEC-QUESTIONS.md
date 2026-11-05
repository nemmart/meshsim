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

## Open

**Q11. 4.21.3 and item 118: the 64th state service's transfers to C&C 3's own memory.** The DMA's packets "leave through the
down port", but these four transfers are addressed to C&C 3 itself. 4.17.9 also says a DMA packet to its own unit reaches
the ejector through the router like any arrival.
*Proposed (as modelled):* a DMA packet whose dst is the controller itself is ejected in the controller's own router and never
goes down to tile 2 and back. Every other dst goes down.

**Q12. The shared cache's write buffer (the ruling above).** Its depth and drain rate aren't given.
*Proposed (as modelled, parameters):* 4 lines, drained to LPDDR at one line per 80 cycles while no read is waiting on LPDDR.
A requester waits only when it must evict into a full buffer.

**Q13. The shared cache: a Tracer write-back that misses.** The Tracer always writes a whole 128 B line.
*Proposed (as modelled):* such a write allocates the line without a fill from LPDDR. The walk that translates it is the same
as a read's, so its index lines are ordinary accesses.
