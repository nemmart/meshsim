# WORKER11 questions

Each with the proposed answer; the work carries on with it unless the owner says otherwise. Marked in the code with
"WORKER11 Qn".

**Q1. Where the row's signedness goes.** `sign_imm` [62] replaces the hard-wired list of signed-immediate opcodes, but a
row that carries Jolt's raw immediate still leaves its reader needing that list. *Proposed:* a flag in the row, record
word 0 bit [86] = the word's `sign_imm`. **Answered (DECISIONS-v1.25 s7):** preferred, the row carries the immediate
already extended to 64 bits if the 80-byte layout has room; otherwise the flag at [86]. WORKER10 decides in the spec;
meshsim models the flag ([86], `Replayer.REC_SIGN_IMM`) until the spec lands, then follows it exactly.

**Q2. The write log's last-load entry.** v1.2: every LD row's value goes into E[15]. At 821e1bd the narrow loads are
`VirtualLoadB/H/W`. *Proposed:* every load row (LD and `VirtualLoad*`). **Answered (s7): agreed;** the delivery writer's
encoder does the same.

**Q3. `offset_imm` outside ShiftData.** jolt-cpp records a ShiftData row's immediate as its `shift_offset`; the builder
sets `offset_imm` only there. *Proposed:* the record's `imm` is `shift_offset` for ShiftData rows, and meshsim flags
`offset_imm` on any other opcode. **Answered (s7): agreed.**

**Q4. Misaligned memory rows.** jolt-cpp aborts on one (`checked_memory_address`, LD and SD included), so it never
reaches a trace. *Proposed:* meshsim flags it as a violation; the row still replays (address rounded down as for the
narrow rows). **Answered (s7): agreed.**

## The model against the v1.25 spec (branch `v1.25` @ fcb09f9; WORKER10/REPORT.md's checklist), 2026-11-01

**No disagreement found; nothing for WORKER10 to fix.** Point by point (model: `Replayer`, `ReplayerAlu`; controller:
`sw/replay/replay_ctl.c`):
1. **Record:** word 0 [86] `imm_signed` = the word's `sign_imm` (`Replayer.REC_SIGN_IMM`, bit 22 of long 1; spare [127:87]
   checked 0 by the unit tests). `imm` of a ShiftData row is `shift_offset` (code 132's sign-extended value under
   `offset_imm`, else 0); an advice row's is 0 (its slots are x0 registers); `VirtualAdvice` is 126. Agrees.
2. **Memory fields:** narrow rows' `mem_addr` = a − (a & (8 − w)) (`ReplayerAlu.memAddr`); loads' `mem_value` = rd after;
   stores' = the log word, 0 without the store log. Agrees.
3. **Logs:** load rows pop the read log, store rows the write log; E[15] takes every load row's value. Agrees.
4. **ShiftData:** rs1 the value, rs2 the base, lane from rs2 + `shift_offset`; `offset_imm` elsewhere flagged and
   ignored. Agrees.
5. **PEXT for any mask**, timed as a 3-cycle S row. *Added now:* `Replayer.pextNonContiguous`, the statistic the
   checklist asks for, with a unit test (counted, not flagged).
6. **Misaligned memory rows** (all widths, LD and SD included) flagged. Agrees.
7. **Decode class 3** is an invalid expansion (condition 13, `ERRORS` [2]); the test windows' unused entries are class 3.
   Agrees.
8. **Lookup by key(w)** (`tkey` in C, `ReplayTables.key` in Java), index sorted by key, number = position, a missing key
   an error (status 2), codes 129-131 against the presented word. Agrees.
9. **`VirtualHostIO`:** the ROM word is all-immediate with `rd0` 0; nothing read or written. Agrees.
10. **88 executed opcodes** as `tables/opcode_table.txt` (ReplayerTest checks it). The appendix's section-3 table was
    also checked by script against the model: every executed opcode's number, name, class and cycles agree; the six
    DIV/REM advice opcodes it lists are "as VirtualAdvice", never executed, as in the model. `advice_reg` is ignored,
    `untraced` is an invalid expansion. Agrees.

Vectors: the appendix's are WORKER9's `vec.tsv`, which the model passes (`data/vec-821e1bd.tsv`: 225 match, 4 C++-UB
shift masks flagged, 23 not applicable).
