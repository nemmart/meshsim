# WORKER9 part 2 plan (proposed, awaiting the owner's go-ahead)

The expansion-table builder, in PR #2 next to the tracer (`tracer/tables/`), from jolt-cpp `821e1bd`.

## Builder (step 9)

- **A C++ tool, `build_tables`**, compiled against the PR #2 tracer: it `#include`s `isa/virtual.cpp` (as meshsim's
  `tools/replayer-opcodes/enum.cpp` and my expansion diff do) to call `expand_virt_seq`, `has_side_effects_beyond_rd`,
  `rd0_is_handled_internally` and `num_cycles` directly, and links the rest (decoder, ELF loader).
- **Templates are derived, not re-typed.** For each source opcode it expands several probe instructions with
  distinct operands and immediates and maps every row slot back to an operand code: register 129/130/131 (the
  source's rd/rs1/rs2), 132 (its immediate), 133-136 (the derived shift masks), 137-144 (temporaries 40+k), 128
  `const` / 145 (immediate 0), or a literal register (x0, the reservations 32/33, CSR registers). A slot that fits no
  code, or fits differently across probes, is an error, not a guess. Flags come from jolt-cpp's own predicates:
  `rd0` (the two rd = x0 functions; 3 for ADDI), `virt`, `next_from_value` (rows that can transfer control),
  `from_log` (LD, SD, `VirtualLoad*`, `VirtualStore*`, advice rows), `report_c` (last words), `imm0_pc0` (JAL),
  `format`, `seq_remaining`.
- **`shift_offset`** (new): in every current expansion it is either the store's immediate (SB/SH/SW) or 0 (SC.W,
  AMO*.W), and `ShiftData` rows have no immediate slot. Proposal for QUESTIONS.md: spare bit [63] becomes
  `offset_imm`: when set, the row's `shift_offset` (and, for `ShiftData*`, the row's immediate) is the source
  immediate (code 132's value); when clear, 0. No other field moves.
- **Per program** (EVM Zone `4e3451c1d8c5`, Cyclone): decode the program's bytecode as the tracer does
  (`apply_host_io_encoding` included), collect the distinct instruction words, and build
  `decode_table.bin` (2,048 entries, every entry written, immediate-bit replication, unused = controller class),
  `rom.bin` (512 words; words 0-1 the padding and shared NOP rows), `templates.bin` + `template_index.bin` (the
  controller-fed inlines and system instructions), and the opcode table (row opcode numbers in 821e1bd's order,
  which is renumbered). Exactly REPLAY-INPUT-REQUIREMENTS's names, layouts and SHA-256 identities.
- **ROM fill**: everything that isn't an inline or a system instruction, as in v1.1; if it doesn't fit, evict the
  least frequent by dynamic count to the controller. Expected about 389 of 512 words (v1.1 ~508; the new expansions
  save about 119 words, mostly in the atomics).
- **The Tracer's variant**: the same format and builder, plus WORKER7's untraced division-family rows
  (`wip/tracer/templates.py`, bank-1 registers 146-255, `advice_reg`, `untraced`), and the C&C 3-fed set. WORKER7's
  harness re-run at 821e1bd first (the division family is reported unchanged; the check confirms it).

## Check (step 10)

- **Instantiate and compare**: a resolver that turns template words into rows exactly as spec 4.19.3-4.19.4 say
  (operand codes, the rd = x0 policy and offset, the derived `is_first` / `is_virtual` / `pc_increment`), compared
  field by field with `expand_virt_seq` for: every instruction word in both programs; for every source opcode, random
  operands (1e5+) plus edge cases (rd/rs1/rs2 = x0, every register, immediates 0, ±1, extremes, shift amounts 0, 31, 32,
  63, every byte/half/word lane offset); both variants (the Tracer's with untraced rows removed must equal the
  replayer's).
- **Rows, not just words**: execute instantiated rows with jolt-cpp's `execute_instruction` against the tracer's own
  expansion on random register/memory states, so semantics (ShiftData's base + offset, the signed immediates) are
  checked too. PEXT/PEXT-signed vectors include non-contiguous masks (the owner's ruling: correct for any mask).
- **Mutants**: broken words (wrong code, flag, offset bit) that the check must catch, as WORKER7 did.
- Regenerate meshsim's `tools/replayer-opcodes` outputs (`enum.txt`, `vec.tsv`) at 821e1bd and list the differences
  for the spec editor (QUESTIONS Q3).

## For the spec / replayer (reported, not changed by me)

- The **signed-immediate set** is logic, not data (`trace_owner.hpp` `signed_immediate`): it now includes
  `VirtualLoad*`, `VirtualStore*` and `VirtualShiftData*`. The record's immediate rule (4.19.12) must follow, or it
  becomes a flag; to settle with the `shift_offset` encoding.
- `VirtualHostIO` decodes only with rd = 0 now; its template has no operands.
- Opcode numbers in the expansion word's [7:0] follow 821e1bd's enum.

## Delivery (step 11)

`wip/tables/` in meshsim: per program, `replayer/` and `tracer/` with the four files each, the opcode table, a
README (jolt-cpp commit, PR #2 commit, how to regenerate, checksums), and a report of ROM fill and template counts
against v1.1's (EVM Zone 239 / Cyclone 221 ROM words used, 32 / 23 controller-fed words).

## Then part 3 (delivery writer): plan to be proposed after part 2.
