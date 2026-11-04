# WORKER9 questions

Each with a proposed answer; work carries on with the proposal unless the owner says otherwise.

**Q1. Which EVM Zone program does v1.25 use?** jolt-cpp replaced PR #1's EVM Zone with `evm-zone-4e3451c1d8c5`.
*Proposed:* jolt-cpp's new release. **Answered (owner, 2026-10-31):** the new release, input
`trace-29-4599adc50712`, with Cyclone as jolt-cpp has it; also run the old program on the new tracer.

**Q2. `--validate` at 2^29** needs ~30 GB, beyond this sandbox, and the runner can't fetch private repositories.
*Proposed:* held Quest task 069 with a hand-copied tarball, as task 065. **Answered (owner):** the owner copies
`w9-tracer.tgz` and releases 069.

**Q3. The opcode appendix (`spec/replayer-opcodes.md`, item 112) is derived at 4f36b3d.** The new rows
(`VirtualLoad*`, `VirtualStore*`), `ShiftData*`'s operands and immediate, the signed-immediate set and `VirtualHostIO`
change what it should say, and its test vectors (`vec.cpp`) should be regenerated at `821e1bd`.
*Proposed:* part 2 regenerates `enum.txt` and `vec.tsv` at `821e1bd` with meshsim's harness (as a check source for
the table builder) and reports the differences; editing the appendix itself stays with the spec editor.

**Q4. PR #2's base.** *Proposed:* stacked on PR #1's branch (`tracer-standalone`), so its diff is the change; if PR #1
merges first, retarget PR #2 to `main`.

**Q5. Where `sign_imm` goes.** The expansion word has one spare bit, [63], now `offset_imm`. *Proposed:* `sign_imm` at
[62], and `advice_reg` (Tracer only) narrowed from [62:56] to [61:56], holding bank-1 register - 146, so a traced advice
row can read registers 146-209 (WORKER7's encoding was register - 128). The builder asserts it. Alternative: drop
`report_c` (always "last word" today) and use [53].

**Q6. `seq_remaining` for long Tracer expansions.** It is 12 bits (max 4,095), but the modular inverse is 53k-110k
untraced words and WORKER7's Q4 counts untraced words in `seq_remaining`. *Proposed:* in Tracer words,
`seq_remaining` counts **traced** words remaining (untraced words carry the count of the next traced word), so a
Tracer template's traced words are bit-identical to the replayer's and the template still ends at the traced word with
0; the generated block's total length is sent separately (the loop's repeat count). Reverses WORKER7 Q4's counting.

**Q7. Unused decode-table entries.** *Proposed:* class 2 (controller, system), so an unknown instruction is a question
for software (spec 4.19.5).

**Q8. The division inlines' inverse: one loop or an addition chain?** One uniform body (square, multiply, select) x 256
gives 123,979 untraced rows for DIVQ and 214,957 for DIVR from 1,069 and 1,777 stored words. An addition chain (runs of
squarings between ~15 multiplies for q) would roughly halve the rows, as several segments with different bodies, which
`segments.bin` already supports. *Proposed:* keep the single loop (simplest for C&C 3; the division inlines are rare)
unless the Tracer's time on them matters. **On hold (owner, v1.25 s6)** until WORKER12's inline counts; single loop kept.

**Q9. The Tracer's row ALU for untraced rows.** Untraced rows use the replayer's ALU opcodes plus `DIV`, `DIVU`,
`DIVUW` (v1.3 s8), `SLT` (a sign test, already a replayer opcode) and an untraced `LD` (WORKER7 Q9: reads guest memory,
logs nothing). *Proposed:* confirm the untraced `LD`, the one new behaviour. **Answered (owner, v1.25 s6): confirmed, provided it reads
through the Tracer's D-cache like any load, so it sees the program's own earlier stores.**
