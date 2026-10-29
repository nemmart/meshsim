# WORKER2 running notes (packet path review of DECISIONS-v1.2)

Scope: decisions 2, 3, 4, 5, 6, 7, 12, 13, 17; S1-S9; Q5, Q8-Q10. Line numbers: D = wip/planning/DECISIONS-v1.2.md,
S = wip/spec/machine-spec.md.

## Progress
- [x] Read DECISIONS-v1.2 in full (607 lines)
- [x] Spec 1-3 (S1-231), 4.1-4.2 (S232-449)
- [ ] Spec 4.3-4.10, 4.11-4.12, 4.13, 4.15-4.17, 6.7, 8, 9, 10, 11
- [ ] Planning notes
- [ ] Draft REVIEW.md

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
