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
