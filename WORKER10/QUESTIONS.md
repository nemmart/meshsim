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
