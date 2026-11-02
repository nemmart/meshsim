# WORKER10 report: the spec at v1.25

Branch `v1.25`. Delivered: `wip/spec/machine-spec.md` and `wip/spec/replayer-opcodes.md` at v1.25, and
`wip/spec/CHANGES-v1.25.md` (the delta from v1.2, section by section, each item citing DECISIONS-v1.25). `main` is merged in
up to DECISIONS-v1.25 section 7.

## What changed

The full list is `CHANGES-v1.25.md`. In short:
- **4.19.1**: the reference is jolt-cpp `821e1bd` (PR #2); the workloads are EVM Zone `evm-zone-4e3451c1d8c5` and Cyclone.
- **4.19.3**: the expansion word gains `offset_imm` [63] and `sign_imm` [62]; `advice_reg` narrows to [61:56].
- **4.19.4–4.19.6, 4.19.9**: the tables are universal; unused decode entries are class 3, an invalid expansion; the ROM
  holds 388 of 512 words; the controller feeds only the inlines and CSRRW/CSRRS, found by a normalized key.
- **4.19.7**: the memory rows (`VirtualLoad…`, `VirtualStore…`, `ShiftData…` reading base + `shift_offset`, misaligned
  accesses gone from traces); PEXT correct for any mask; renumbering; 88 executed opcodes.
- **4.19.12**: the row carries `imm_signed` at word 0 bit [86]; the memory fields cover all eight memory opcodes.
- **4.19.15**: E[15] is updated by every load row.
- **9, 10, 13**: invariant 16, the replayer's parameter row, a status table for earlier items, and new items 137–141.
- **The appendix**: tables regenerated at `821e1bd` from WORKER9's harness outputs (sections 2, 6–9). Sections 1.2, 1.5
  and 3 are rewritten; flags F1–F12 are kept as found at `4f36b3d`, with v1.25 notes.

## The row's sign flag: word 0 bit [86] (for WORKER11 and WORKER9's delivery writer)

**Kept at [86], named `imm_signed`, equal to the expansion word's `sign_imm`.** It needs a bit of its own: the row's
`imm` is already the full 64-bit `_imm`, and what `sign_imm` decides is how Jolt widens it to the 128-bit `imm()`, so no
extension of the stored value can carry it. I found no reason against [86]:
- word 0 [127:86] was all spare, and [86] sits next to the other per-row flags ([80] `is_compressed`, [81]
  `is_virtual`, [82] `is_first_in_sequence`, [85:83] the slot kinds);
- the 80-byte layout has no other spare bits;
- nothing else claims [86].

Spare is now [127:87], 0. The model and the delivery writer must set [86] exactly from the word's `sign_imm`. That is,
from jolt-cpp's `signed_immediate`: loads, stores (the six narrow ones included), conditional branches and
`ShiftData…`. Every other row has 0.

## Left *(Proposed)* or open

Nothing new is left *(Proposed)*; every v1.25 question has been answered (`QUESTIONS.md`). Open:
- **Item 137**: re-cite the `4f36b3d` file:line references at `821e1bd`. They are marked; the normative functions and
  everything touched by v1.25 are already cited at `821e1bd`.
- **Item 138**: re-measure the log densities, controller-fed expansions per window, multiply density and row rate on the
  acceptance runs.
- **Item 140**: the PEXT method and its cycles for non-contiguous masks (with Tony).
- **Item 141**: the Tracer's tables, deferred to v1.3. v1.25 specifies only `untraced` [55] and `advice_reg` [61:56].

Earlier *(Proposed)* markers in 4.19 that v1.25 didn't touch are unchanged.

## What WORKER11 should check against the model

1. **The record:** word 0 [86] `imm_signed` = `sign_imm`. `imm` for a `ShiftData…` row is `shift_offset` sign-extended
   to 64 bits; for an advice row it is 0. `VirtualAdvice` is reported as 126.
2. **The memory fields** for LD, SD and the six narrow opcodes. The narrow rows' `mem_addr` is *a* − (*a* & (8 − *w*)),
   *a* = rs1 + imm. A load's `mem_value` is rd after the row; a store's is the log word (0 without the store log).
3. **The logs:** load rows pop the read log, store rows the write log. **E[15] takes the value of every load row**, the
   narrow ones included.
4. **`ShiftData…`:** rs1 is the value and rs2 the base register; the lane comes from rs2 + `shift_offset`.
   `shift_offset` is the source immediate when `offset_imm` is set, else 0. meshsim flags `offset_imm` on any other
   opcode.
5. **PEXT for any mask** (the appendix's 252 vectors include non-contiguous masks). Time every mask like any other row
   and report non-contiguous ones as a statistic.
6. **Misaligned memory rows** (LD and SD included) are flagged.
7. **Decode class 3** is an invalid expansion (`ERRORS` [2]); there is no controller class for unknown words.
8. **The controller's lookup** is by key(*w*): an inline's register fields cleared; CSRRW/CSRRS's rd and rs1 renamed by
   first appearance, CSR field kept. The index is sorted by key and a number is its position. A key with no entry is an
   error. Codes 129–131 resolve against the presented word.
9. **`VirtualHostIO`** has no operands, `rd0` 0 and no register write.
10. **88 executed opcodes**, numbered as `tables/opcode_table.txt`; `advice_reg` is ignored, and an `untraced` word is an
    invalid expansion.

## Notes

- I fixed one v1.2 erratum, "peripheral 1, type 1" in 4.19's intro (type 4 elsewhere), and removed a duplicated sentence
  in 4.19.11.
- The appendix's section 2 source column and section 6 emit sites were regenerated against PR #2's source. Its row
  semantics (rd formulas, control, log) were kept from v1.2 except for the rows `821e1bd` changed, which I checked against
  `execute_instruction` at `821e1bd`.
