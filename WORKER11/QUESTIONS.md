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
