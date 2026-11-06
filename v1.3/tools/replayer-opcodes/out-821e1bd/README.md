# The opcode appendix's harness outputs at jolt-cpp 821e1bd (QUESTIONS Q3)

meshsim's `wip/tools/replayer-opcodes/` harness (`enum.cpp`, `vec.cpp`, `summarize.py`), rebuilt against
jolt-asic-planning PR #2's tracer (jolt-cpp `821e1bd`). `enum.cpp` compiled unchanged; `vec.cpp` needed the changes
below (the copy here is the updated one). For the spec editor: what `spec/replayer-opcodes.md` should now say. The
harness directory itself is not changed (not mine to change).

| file | |
|---|---|
| `enum.txt`, `summary.txt`, `sites.json` | regenerated at 821e1bd |
| `vec.tsv` | regenerated with the updated `vec.cpp` (252 vectors; 5 ABORTs, the failing-assertion cases as before) |
| `vec.cpp` | the 4f36b3d harness plus the edits below |
| `vec.diff`, `summary.diff` | against the 4f36b3d outputs in `wip/tools/replayer-opcodes/` |

**Edits to `vec.cpp`**: removed `VirtualAlignAddr` and the two alignment asserts (gone from jolt-cpp); `ShiftData*`
vectors now set `shift_offset` (a global applied to the next vector) and pass the *base* in rs2, with zero, positive
and negative offsets; added `VirtualLoadB/H/W` (rd = the log word, the doubleword; rd = x0 dropped) and
`VirtualStoreB/H/W` (one log word, nothing written), as `LD`/`SD` on the log-driven path; added non-contiguous `PEXT`
masks (8 runs, 32 runs, two single bits) and `PextSigned` with two runs, top bit set and clear: it sign-extends from the
last bit placed, which is what the owner's "correct for any mask" ruling requires the replayer to compute;
`VirtualHostIO` is operand-free (all immediate slots, no register read or write).

**What changed for the appendix** (from `summary.diff`, `vec.diff` and WORKER9's expansion diff):
- Row opcodes: 88 at 821e1bd (`wip/tables/opcode_table.txt`), against 85 executed at 4f36b3d: added `VirtualLoadB/H/W`
  (class L, imm in rs2 like LD) and `VirtualStoreB/H/W` (class L, imm in rd like SD); removed `VirtualAlignAddr`,
  `VirtualAssertHalfwordAlignment`, `VirtualAssertWordAlignment`. **Every opcode number from `VirtualLoadB` (110) on
  shifts**; `VirtualAdvice` is now 126 (was 122).
- `VirtualShiftDataB/H/W`: lane from `(rs2 + shift_offset) & 7` (`& 6`, `& 4`), rs2 now the base register; the row's
  immediate is `shift_offset`, signed.
- Signed immediates (`sign_imm`): loads, stores (the six Virtual ones included), branches and `ShiftData*`.
- `VirtualPext`/`PextSigned`: general for any mask (v1.25 ruling); the "contiguous masks only" text in 4.19.7 goes.
- `VirtualHostIO`: no operands; it neither reads nor writes rd (was "writes rd's own value back").
- Producers: `VirtualSignExtendWord` no longer ends the AMO*.W bodies (`ADDI rd, old, 0` does);
  `VirtualZeroExtendWord` is no longer used by AMOMINU/MAXU.W; ORI is now always a last row; SD is no longer produced
  by SB/SH/SW.
