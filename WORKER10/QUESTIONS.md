# WORKER10 questions (the v1.25 spec)

Each with a proposed answer, written into the spec marked *(Proposed)*; work carries on with the proposal unless the
owner says otherwise.

**Q1. The system instructions are controller-fed, against DECISIONS-v1.25 s5's goal.** **Answered (owner, 2026-11-01;
DECISIONS-v1.25 s6):** ECALL, EBREAK and MRET are in the ROM (388 of 512); CSRRW and CSRRS stay controller-fed, the one
deliberate exception (option a). Spec 4.19.6, 4.19.9, item 139.

**Q2. Which error an invalid decode entry raises.** **Answered (owner, 2026-11-01):** the replayer's existing invalid
expansion, `ERRORS` [2] (4.19.11), which v1.2 already raises for class 3; no new bit.

**Q3. The Tracer's part of the expansion word.** **Answered (owner, 2026-11-01):** v1.25 describes only its fields
(`untraced` [55], `advice_reg` [61:56]); the Tracer's tables are v1.3's (item 141).

**Q4. Where `sign_imm` reaches the row.** It doesn't change the 64-bit immediate; it decides how Jolt forms its 128-bit
`imm()`. *Proposed:* a flag in the row. **Answered (owner, 2026-11-01; DECISIONS-v1.25 s7 Q1):** a flag; word 0 bit
[86] unless the layout gives a reason not to. **Kept at [86]** (`imm_signed`, 4.19.12; reasons in REPORT.md).

**Q5. The write log's last-load entry and the narrow loads.** *Proposed:* every load row updates E[15].
**Answered (DECISIONS-v1.25 s7 Q2): agreed** (4.19.15).

**Q6. Non-contiguous PEXT masks in meshsim.** *Proposed:* timed like any other row, reported as a statistic.
**Answered (owner, 2026-11-01): agreed** (4.19.7, invariant 16, item 140).

**Q7. Citations moved between `4f36b3d` and `821e1bd`.** *Proposed:* cite at `821e1bd` what v1.25 touches, mark the rest as
taken at `4f36b3d`, and re-cite later. **Answered (owner, 2026-11-01): fine** (item 137).

## DECISIONS-v1.25 section 8: candidates under 8a's test (2026-11-01)

These rules prescribe an outcome for a mistake. They are listed here and **not changed**, as the owner asked (8a: "rules elsewhere
in the spec that prescribe an outcome for a mistake are candidates to revisit under this test"). Each has my reading of the
test: would simulation catch this on our firmware?

**Q8. The replayer's bad write and queue overrun** (4.19.10, 4.19.11 [0] and [1]): a packet to an unmapped offset, of a
size its part doesn't take, or to a single-ported memory while a pass runs is **dropped, still counted**, and sets [0]; a
queue overrun is dropped and counted too. This is 8a's own example of a third kind. *Proposed:* UB for the first three
(a static mistake in the controller's software, which simulation catches). The queue overrun depends on the controller's
credit loop under real timing, which may argue for keeping it checked.

**Q9. Reserved digit values** (8.5, `DMA_CONFIG` [20:18], [23:21]): 5–7 are "reserved, treated as 0 (meshsim flags them)".
*Proposed:* UB; the "treated as 0" substitute is a prescribed outcome.

**Q10. Stream and group numbers past the table's end** (8.5, the run count): they "wrap within the table (meshsim flags
them)". *Proposed:* UB. The wrap probably comes free from the index width, in which case the spec could just stop
promising it.

**Q11. Reserved modes 6–7** (3.1, 4.8): "discarded at the destination, with no error and no decrement". *Proposed:* UB;
the discard is free, but the promise of no decrement is an outcome software might lean on.

**Q12. Reserved words in the register block and the windows** (4.11, 4.17.4): "read 0 and ignore stores (meshsim flags
the access)". *Proposed:* keep only if it is free in the RTL (it usually is); otherwise UB. D48's DMA words 8–9 are not a
mistake case: they are defined to read 0.

**Q13. Misuses the hardware "ignores"** (4.13, "what earns an error"): a start while its engine is running, a pulse while
a peripheral is busy. *Proposed:* UB, since meshsim already flags them; "ignores" is a prescribed outcome if it costs any
logic.

**Q14. Mode 3 at 128 bits and the region decode** (3.1, 4.13 [1], invariant 5): open as spec item 142, for the owner and
Tony. These are listed for completeness.

Not candidates, because 8a allows what comes for free: an overflow drops the arriving item (4.13), and an offset past the
end of data or program memory aliases (2.2, 4.11).

**Answered (owner, 2026-11-01), into v1.25 at once:** Q8: the bad write becomes UB (`ERRORS` [0] reserved); the queue
overrun stays checked, and its "still counted" is dropped from the spec. Q9, Q10, Q11 and Q13: UB. **For Tony:** Q12 (reserved
words "read 0, ignore stores": keep if free in the RTL) and whether the overrun's dropped push is still counted (free if
counting stays at the director). Q14 stays as spec item 142.
