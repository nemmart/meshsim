# Mechanical expansion diff, jolt-asic-planning 4f36b3d (jolt-cpp 4ab349bb) vs jolt-cpp 821e1bd

`expdump.cpp` `#include`s the tracer's `isa/virtual.cpp` and runs `expand_virt_seq` on every non-virtual source opcode
(110 at both commits) over a grid of 32 operand sets: rd in {0, 5}, rs1 in {0, 6}, the third operand in
{7, -12, 0x800, 0}, its immediate flag off and on. It prints every emitted row (opcode, three operands with their
immediate flags, `shift_offset`, `is_first`, `seq_remaining`, `is_virtual`, the advice source) and both `num_cycles`.
`build.sh <tracer dir> <out> [-DHAS_SHIFT_OFFSET]` compiles it against a built tracer tree (the same way as meshsim's
`wip/tools/replayer-opcodes/enum.cpp`). `cmp.py` compares the two dumps by opcode *name* (the numbering changed).
The dumps themselves (~20 MB each) are not committed; rebuild them with the two commands below.

    bash build.sh <PR #1 tree>/tracer dump_old && ./dump_old > old.txt
    bash build.sh <PR #2 tree>/tracer dump_new -DHAS_SHIFT_OFFSET && ./dump_new > new.txt
    python3 cmp.py

Result (`cmp-output.txt`): 21 source opcodes change, 89 are identical row for row in all 32 operand sets.
`changed-expansions.txt` shows each changed one side by side.
