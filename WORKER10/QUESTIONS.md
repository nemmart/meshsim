# WORKER10 questions (the v1.25 spec)

Each with a proposed answer, written into the spec marked *(Proposed)*; work carries on with the proposal unless the
owner says otherwise.

**Q1. The system instructions are controller-fed, against DECISIONS-v1.25 s5's goal** (everything but the inlines and
the advice generators in the ROM). **Answered (owner, 2026-11-01):** the spec follows `wip/tables/` as delivered (ECALL,
EBREAK, MRET, CSRRW and CSRRS controller-fed, 63 keys) and notes the gap; WORKER9 is trying to fit them into the ROM, and
4.19.5-4.19.6 are updated if the tables change.

**Q2. Which error an invalid decode entry raises.** **Answered (owner, 2026-11-01):** the replayer's existing invalid
expansion, `ERRORS` [2] (4.19.11), which v1.2 already raises for class 3; no new bit.

**Q3. The Tracer's part of the expansion word.** **Answered (owner, 2026-11-01):** v1.25 describes only its fields
(`untraced` [55], `advice_reg` [61:56]); the Tracer's tables and the rest of its design are v1.3's.
