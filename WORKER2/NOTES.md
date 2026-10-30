# WORKER2 running notes (packet path review of DECISIONS-v1.2)

Scope: decisions 2, 3, 4, 5, 6, 7, 12, 13, 17; S1-S9; Q5, Q8-Q10. Line numbers: D = wip/planning/DECISIONS-v1.2.md,
S = wip/spec/machine-spec.md.

## Progress
- [x] Read DECISIONS-v1.2 in full (607 lines)
- [x] Spec 1-3 (S1-231), 4.1-4.2 (S232-449)
- [x] Spec 4.3-4.10 (S450-740), 4.11-4.16 (S740-1000), 4.17-4.18 (S1000-1135), 6 (S1714-1900), 8.1-8.9, 9-12
- [x] Planning notes (section 5: C&C 1-3 are tile instances with 2 MB big SRAM on tiles 0-2's fifth links;
      engines 72-74 on each controller's +8 link; routing d>=64 via tile d[2:0])
- [x] sw: wait_tables() in mesh.h:416 polls rd_running|ts_busy; ~25 calls in RECIPES.md, also CONVENTIONS, REGISTERS
- [x] REVIEW.md written and pushed (W2-01..W2-23); citations checked against the files

## Candidate findings (raw, to verify)
1. Counting is not mode-independent: D 3 says "Counting no longer depends on the mode" but mode 4 (gather
   request) must not count (else request + values double count) and reserved 5-7 are "uncounted" (D 6).
   Define: counter field honoured for modes 0-3 only; mode 4's counter field = the counter its values use?
2. Two decrement sources per cycle: D 3 "at most one packet per cycle leaves the director, so a counter takes
   at most one decrement per cycle", but D 17 C puts gather values' delay at write-engine entry (after the
   director); read-port results (D 13, "always to the tile itself") - check whether they pass the director.
3. K vs write-engine input FIFO: S 4.1 (S~306) "write engine's 4-entry input FIFO". K from director acceptance
   must cover FIFO wait + pointer stage + area-queue acceptance. Area queue full -> write waits (writes-first
   only vs reads). If the area queue is full the FIFO backs up -> unbounded wait vs fixed K. Check 4.9.
4. Command landed timing: event decoded at director; must not post before port B lands slot 0 (and args).
5. Command slot 0 by 64/128-bit writes: recommend "any write whose covered words include slot 0".
6. Address increment carry: STREAM_DEST, ROUTER_ADDR, DMA_ADDR, SRAM_RD_DEST_ADDR advance by size - does it
   wrap in the 24-bit offset or carry into the region? v1.1 wrapped mod 2^21 (S 4.1 wrap, 4.2).
7. Q10 consequence: D 5 "one map for packet addresses and the controller's own loads and stores"; the
   controller's map is bytes (0xFFFFF800 etc.), so word offsets would give one address two meanings.
   Also unit mismatch with 21-bit chunk addresses kept in STREAM_SRC, DMA_RECORD_BASE_ADDRESS, SRAM_RD_ADDR_QUEUE (Q9).
8. "Bad size" in D 18 ERRORS[1] vs S 3.1 "Size 11 is undefined behaviour, not a bad packet" - which?
   Bad-packet list for v1.2 not written: mode 3 (augmented) at 128, misalignment per region, out-of-range offset,
   absent peripheral, reserved regions, mode 4 from a remote source.
9. Out-of-range offset within a valid region (0x00 beyond 32 KB, 0x02 beyond the tile's size - v1.1: reads 0,
   writes dropped, no condition; S 4.1 "Wrap and out-of-range").
10. STREAM_COUNT [31:25] is *d* (relative 0-63, absolute 64-125); D 12 calls [31:25] "dst". Same bits, different
    meaning - nit, but the "copy as one block" claim covers only [24:18].
11. DMA run done is sticky and not cleared by PULSE_DMA (D 16, 17): a stale [6] from an earlier run satisfies
    D 17's EVENTS_REQUIRED wait. Counters got additive arming to fix exactly this; [6] did not.
12. Write combining (S7) - a write held for combining hasn't entered its area queue; K/S1 must cover it.
13. Reserved modes discarded silently with counter != 0: the counter never completes -> silent hang (v1.1 had
    an error). Accepted for counter 0; consequence for counted ones not noted.
14. Endpoints 72-74 as "their controllers' peripherals" (D 15) vs having endpoint ids of their own: is a packet for
    Ingress/Egress dst 72, or dst 64 region 0x03? 2.1 still says 65 = memory system.
15. Stream to C&C: v1.1 mode 3 d=64 "addr the chunk offset within the write" (6.7). v1.2: mode 0 + which region?
16. HALT counting + live error lines: counted packet into a halted tile sets unexpected arrival -> error line
    -> C&C 1 interrupt during HALT (WORKER1).
17. meshsim checks unexpected arrival at acceptance (D 3): ERRORS timing differs from the hardware; DEVIATIONS.

## Facts gathered
- S 4.1: area queue depth 8 (meshsim), two pushes/cycle (one read, one write), write first; room judged before pop.
- S 4.1: write engine input FIFO 4 entries; director never stalls; overflow -> write-engine-input-overflow.
- S 4.1: mem_idle covers director->write engine, input FIFO, pointer stage, every area queue.
- S 4.1: alignment none required except v1.1 mode 3 (explicit address) - never spans.
- S 4.2: STREAM_DEST v1.1: dest word [20:0], d [27:21]; STREAM_COUNT mode [21:18], size [23:22], gap [26:24].
  v1.2 moves gap to STREAM_SRC [23:21] and d to STREAM_COUNT [31:25].

## Verified (after reading the spec)
- #2 confirmed: gather values DO pass the director in v1.1: S514 (mode-2 packet to the tile itself), S540
  ("Everything that leaves a tile ... is a big-SRAM read return turned into a packet"), S530 (k+1 cycles through
  the one injection point and the one eject). So D 17 C (D511) contradicts the spec. Read-port results too: S466.
- #3 confirmed and worse: write path = director -> 4-entry input FIFO (S667) -> pointer stage, PTR round trip
  2*L_ts+1 (S685; L_ts = ts_engine_latency 1-4) -> area queue. HIST RMW and PTR RMW each behind a 16-entry request
  FIFO (S738). At L_ts = 4 the pointer round trip alone is 9 > K = 8. cm_latency 2 [built], rp_latency 2 (Proposed,
  item 48; replayer placement open, item 59) - K must be tied to all of them.
- Area queue: ready_w = count < depth and count <= depth-1 after every cycle (S285-288), so a write is never refused
  by an area except part 1 of a spanning write (S280-283). FIFO backs up only behind spanning writes / pointer stage.
- v1.1 port B accepts 1/2/4 words at ANY word address (S771) - no alignment. D 18 ERRORS[1] "misaligned address"
  would newly reject unaligned controller-memory writes (and read-port results, mode 6 to self).
- v1.1 big SRAM: no alignment except mode 3 (S277); out-of-range reads 0 / writes dropped, no condition (S305-309).
- ROUTER_SET_HDR auto-increment: +1/2/4 for modes 3, 6, 9; +1 for mode 7; unchanged for others incl. replies (S788,
  8.4). v1.2: replies and commands are mode 0, so ROUTER_ADDR advances after every reply -> D 7's "change of dst
  alone ... is still one store" (D255) fails for replies.
- 6.7 write path: write stream = mode 3 to d=64 with addr = chunk offset within the write; C&C's port diverts mode 3
  to the memory system (S792, S1882). v1.2 has no equivalent: mode 3 is now augmented, mode 0 has no "write data"
  meaning at a controller. Planning notes: LPDDR owned by engine 73.
- Section 11 cost: link FIFOs 17 x 16 x 169 = 46 Kbit -> ~0.08 mm2 (S2223); fifth port 10 x 16 x 169 = 27 Kbit
  (S2238); loopback +48 entries (S2231). At 256: +87 bits x (272 + 160 + 48 entries) = 41.8 Kbit; by the spec's own
  v1.0->v1.1 slope (~1.6 um2/bit) ~ +0.065 mm2; plus padding stages (S153), holding registers, write-engine input
  FIFO, return-queue/read-port metadata (32-bit addresses).
- ts_busy (S957): OR of engine table request within L_ts+1 cycles, PTR/HIST request queued/arriving, or last RMW too
  recent to be readable. Covers uncounted arrivals too; D 17 covers counted ones only.
- DMA run done ([6], v1.1 [11]) is set on rd_done's rise (S342), sticky, not cleared by start: stale-event risk.
- 6.7 source-buffer rule (S1880) relies on area arrival-order service -> Q5 (writes ahead of reads) breaks it.
- 4.17.9 (S1080): mesh tiles' peripherals have no output; v1.2's standard DMA on any peripheral could inject at
  the tile -> S9's token rule must cover a DMA packet to the tile itself or broadcast.

## Plan for REVIEW.md
Top five: (1) K can't be guaranteed as a constant; (2) 17 C contradicts the gather path / one decrement per cycle;
(3) the LPDDR write stream and the meaning of regions at endpoints 64-74; (4) bad-packet/alignment/range rules for
the new map are undefined (incl. Q10 units); (5) DMA run done is stale-prone and event-only waits lose the
idempotent "tables free" test. Then ROUTER_ADDR advance, counting vs mode, command landed timing, address carry,
cost, Q5, S7, S9, invariants, nits.

# Round 2 (PROMPT-2.md)
Read: decisions overview, 2-7, 9-13, 16-30, S list, Q list (log now 1,096 lines; regions renumbered:
0x02 boot ROM, 0x03 big SRAM, 0x04-0x06 peripherals).

Raw round-2 points (to verify):
- Natural alignment removes spanning -> the write path may become fixed-length (area queue never refuses a write:
  count <= depth-1 after every cycle, S286-S290), so K can be a formula 2*L_ts+3(+combining) *(inferred)*.
- S1 table-window case: no read path in the margin; and a late PTR/HIST increment after a controller STORE
  overwrites it (increment wins, 4.10) - WAW, not just stale read. "Reaction >= 4 cycles" assumes the helper's
  EVENTS load; polling a counter register is faster.
- Boot (22) / staging: marker lands in port B; big-SRAM image writes (region 0x03) use the write-engine path, so
  in-order arrival doesn't put them "in place" when the marker lands.
- 17 "no uncounted writes except commands" vs 25's tag-window writes from running tiles (counter 0 at C&C 2).
- Tag windows: several tiles writing windows at once fan in at tile 1's fifth-port output with no flow control
  (v1.1's single write token prevented this, S1882 area / 4.16). Window length/completion, tile-side ack,
  read direction undefined.
- Sending peripheral port: src = unit id (shares reply pair / PTR[src] with the controller); controller routers
  are "normal tile instances" but not on the torus; DMA-to-self eject FIFO has no flow control.
- 26: misaligned ENGINE reads (STREAM_SRC, GATHER base) - what is done with the read? Read port: [7] or [1]?
- 26: "high offset bits ignored, aliases" - big SRAM sizes are not powers of two (4.25 MB, partial areas).
- 26: "mixing sizes breaks it" - descending sizes from an aligned start are fine; but DMA remainders leave the
  next transfer misaligned.
- 30: 16-bit stream count vs sort sizes (65,536 keys per tile?) - check section 14.
- Nits: overview "2-bit counter"; 17 "[6]" stale; 16 stale text; 13 "units per Q10"; 4 "88 used"; 5's table split
  by the italic note; 26 "GROUP sources", "augmented ordinal" in chunks; 7 "[0] proposed" vs layout.

Round 2 done: WORKER2/REVIEW-2.md pushed (status table W2-01..23; new W2-24..W2-36). Verified: the sort writes stream
counts of 65,536 (local.c:33,63,64,79; spec item 25), so the 16-bit count truncates to 0. meshsim WriteEngine
stalls only on spanning or area refusal; area never refuses a write (count <= depth-1), so without spanning the
write path is 2L+3 fixed *(inferred)*.

# Round 3 (PROMPT-3.md)
Read: overview, 31-35, 32's calls, 4, 25, 30, S1, WORKER4/PROMPT.md. Verdict: ready with two fixes.
- W2-37: 31 step 5 (arm, then READY = done pulse) -> done early [8]; use v1.1's announced-staging order (6.2/6.4).
- W2-38: "dropped, still counted" for misaligned READS: send the packet with payload 0 (one decrement point).
meshsim v1.1 models no LPDDR/memory system, so 33's deferred items don't block WORKER4.
