# Machine spec v1.2 → v1.25: what changed

`spec/machine-spec.md` is now v1.25, a milestone between v1.2 and v1.3: **v1.2 with the replayer and its tables
brought up to jolt-cpp `821e1bd`**, so the hardware matches today's Jolt. This file says what moved since v1.2,
section by section, so a v1.2 reader can find their way. Every item cites the decision behind it, as **V*n***, a
section of `planning/DECISIONS-v1.25.md`, or **O** for an owner's ruling given to this update (2026-11-01). Where a later
decision revised an earlier one, the citation names the later.

**Scope.** Only the replayer (4.19) and what follows from it change: the front matter, sections 9, 10 and 13, and the
opcode appendix `spec/replayer-opcodes.md`. The sort and the rest of the machine are v1.2's, and so is every section
not listed below.

**Numbering.** Section numbers are v1.2's; no section is added or removed. Invariants keep their numbers. Open items
1–136 keep their text; the ones v1.25 changes are listed in section 13's new status table, and v1.25's own items are
**137–141**.

**Still at v1.2, not touched by this update:** the figures (`diagrams/`), `sw/`, `spec/schedule.md`, meshsim (its
replayer is being brought to v1.25 by WORKER11 on `v1.25-meshsim`), the sort and the measured results. Measurements in
4.19 were taken at `4f36b3d`'s expansions and are kept, labelled, until the acceptance runs re-measure them (item 138).

## The replayer in one paragraph

The reference is jolt-cpp `821e1bd`. Jolt now loads and stores the aligned doubleword around every sub-word access and
32-bit atomic, through six new row opcodes; `ShiftData…` reads the base register plus a `shift_offset`, which the
expansion word carries as `offset_imm`; which immediates are signed is table data, `sign_imm`, reported in the row as
`imm_signed`; `VirtualPext` and `VirtualPextSigned` must be correct for any mask; and the tables are universal, one set
per jolt-cpp version, with every expansion but the inlines and the CSR instructions in the ROM.

## Section by section

| section | what changed | decisions |
|---|---|---|
| front matter | title v1.25; `CHANGES-v1.25.md`, `DECISIONS-v1.25.md` and `tables/` among the companion files; "v1.25 in brief" | V1 |
| 4.19 (intro) | "peripheral 1, **type 1**" corrected to type 4, as its heading and `PARAMS` say (an editorial fix of v1.2) | — |
| 4.19.1 | **the reference is jolt-cpp `821e1bd`** (PR #2, `tracer-standalone-main`), replacing PR #1 at `4f36b3d`; the workloads are EVM Zone `evm-zone-4e3451c1d8c5` (input `trace-29-4599adc50712`, replacing v1.2's EVM Zone program) and Cyclone, both 2²⁹ rows; `expand_compressed_chunk` re-cited; citations not marked 821e1bd were taken at `4f36b3d` (item 137); the expansion queue carries the inlines and the CSR instructions | V1, V2, O |
| 4.19.2 | the log rows are the **load and store rows**: LD and `VirtualLoadB/H/W` take the read log, SD and `VirtualStoreB/H/W` the write log; "includes store log" speaks of store rows; the measured densities are labelled as `4f36b3d`'s, expected to rise about a ninth (item 138) | V1 |
| 4.19.3 | the expansion word: **`offset_imm` [63]** (a `ShiftData…` row's `shift_offset` is the source immediate when set, else 0; set on no other row, meshsim flags it elsewhere), **`sign_imm` [62]** (the row's immediate is signed: Jolt's 128-bit `imm()`), **`advice_reg` narrowed to [61:56]**, register − 146 (bank-1 registers 146–209); opcode numbers 0–170 at `821e1bd`; `shift_offset` and `sign_imm` explained; a `ShiftData…` row's immediate is its `shift_offset`; codes 146–255 named as the Tracer's bank-1 registers; `sign_imm` and `offset_imm` added to the conventions kept as data | V4, V5, V7 (Q3) |
| 4.19.4 | the `lb` example rewritten (three rows: `VirtualLoadB`, `VirtualWindowMaskB`, `VirtualPextSigned`); **`VirtualHostIO` is no longer remapped** (it has no register operands at `821e1bd`; `rd0` = 0); JAL's and JALR's throwaway paths re-cited | V1 |
| 4.19.5 | **the tables are universal**: one decode table, ROM and controller-fed set for every RV64IMAC instruction and every inline, built once per jolt-cpp version (`tables/`); **unused entries are class 3, invalid**, raising the existing invalid expansion (v1.2: controller class); 306 ROM, 17 controller and 1,725 invalid entries; `VirtualHostIO`'s entry has no operands; ECALL's entry is a ROM template | V5, O |
| 4.19.6 | the ROM's goal (everything but the inlines and the advice generators in the ROM) and **its fill at `821e1bd`: 388 of 512 words**, every atomic and ECALL, EBREAK and MRET included (v1.2: about 508); the CSR instructions stay controller-fed, the one deliberate exception; what the builder does when templates don't fit | V5, V6 |
| 4.19.7 | the appendix regenerated at `821e1bd` (94 opcodes in rows, 252 vectors); **`VirtualPext` and `VirtualPextSigned` correct for any mask**, full speed promised for contiguous masks only, the method the implementation's, meshsim timing every mask alike and reporting non-contiguous ones (replaces v1.2's narrowing); **the memory rows** (the six narrow opcodes, the 3-row loads and 6-row stores, `ShiftData…`'s base register, misaligned accesses gone from traces, `VirtualHostIO` operand-free); the class table renumbered, class L adding the six narrow opcodes; **the replayer executes 88 opcodes** (v1.2: 85); the record's memory fields for every load and store row; `next_from_value`'s placement; "four departures" now three | V1, V7 (Q4), O |
| 4.19.9 | **controller-fed templates found by a normalized key**: the index sorted by key, a number its position; an inline's register fields cleared, a CSR instruction's renamed by first appearance; codes 129–131 still resolve against the presented word; a key with no entry an error; 75 numbers at `821e1bd` (v1.2: one per exact instruction word, per program); EBREAK's word in the ROM; the controller feeds only the inlines and the CSR instructions | V5, V6 |
| 4.19.11 | class 3 described as an encoding no instruction Jolt accepts; a duplicated sentence removed | V5, O |
| 4.19.12 | **word 0 [86] `imm_signed`**, the word's `sign_imm` (spare now [127:87]); `imm` is a `ShiftData…` row's `shift_offset`, extended to 128 bits by `imm_signed`; `mem_addr` and `mem_value` for every load and store row, the narrow rows' address rounded to the doubleword; `VirtualAdvice` is 126 | V7 (Q1), V1 |
| 4.19.14 | the ALU's PEXT for any mask | V1 |
| 4.19.15 | the write log's last-load entry E[15] is updated by **every load row**, the narrow ones included | V7 (Q2) |
| 9 (16) | the contiguous-PEXT clause withdrawn: a non-contiguous mask is reported as a statistic; added: `offset_imm` only on `ShiftData…` rows, no misaligned memory row; the rows equal jolt-cpp `821e1bd`'s | V1, V7 (Q3, Q4), O |
| 10 | the replayer's row: the reference, the universal tables' fill, 88 executed opcodes, the new expansion-word bits | V1, V5, V6 |
| 13 | a status table for items 33, 34, 36, 40, 43, 56, 58, 112, 113, 114; new items 137 (citations at `821e1bd`), 138 (v1.25's measurements), 139 (what the controller feeds), 140 (PEXT for any mask: the method), 141 (the Tracer's tables, deferred) | V1–V7, O |

## The opcode appendix (`spec/replayer-opcodes.md`)

| section | what changed | decisions |
|---|---|---|
| header | derived at `821e1bd` from WORKER9's harness outputs; which citations are at `821e1bd`; what v1.25 changed; one narrowing left (zero shift masks), the PEXT narrowing withdrawn | V1 |
| 1.2, 1.5 | immediates of the narrow loads (rs2), the narrow stores (rd), `VirtualHostIO` (all three), `ShiftData…` (none: its immediate is `shift_offset`); the signed-immediate set at `821e1bd`; the record's imm and memory fields | V1, V4 |
| 2 | **renumbered**; `VirtualLoadB/H/W` and `VirtualStoreB/H/W` added (G); `VirtualAlignAddr` and the two alignment assertions removed; `ShiftData…`, `VirtualPext`, `VirtualPextSigned`, `VirtualHostIO`, LD and SD rewritten; slot forms, last-row status and source lines regenerated at `821e1bd`; "the masks VirtualPext receives" rewritten | V1 |
| 3 | now **the load and store rows**: the eight opcodes, the narrow rows' address, alignment as a tracer abort, the last-load entry | V1, V7 (Q2) |
| 4, 5 | flags kept as found at `4f36b3d`, with v1.25 notes on F2, F4, F8, F9, F11 and on `VirtualHostIO`'s remap | V1, V6 |
| 6–9 | regenerated from the `821e1bd` harness outputs: producers and emit sites, row counts of the source-only opcodes (the changed expansions described), the never-occurring opcodes renumbered (130, 164, 168), and 252 test vectors, `ShiftData…` with its `shift_offset` | V1 |

## Removed, by name

`VirtualAlignAddr`, `VirtualAssertHalfwordAlignment`, `VirtualAssertWordAlignment` (no longer in jolt-cpp); the
contiguous-mask narrowing of `VirtualPext` and `VirtualPextSigned` (invariant 16, item 43); per-program tables and the
"unused entries are controller class" rule; the exact-word template key; `VirtualHostIO`'s remap and its write of rd;
EBREAK, ECALL and MRET as controller-fed expansions.
