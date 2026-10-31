# wip/tables: universal expansion tables from jolt-cpp 821e1bd

Built once per jolt-cpp version, good for any guest (DECISIONS-v1.25 s5), by `build_tables` (jolt-asic-planning PR #2,
`tracer/tables/`) from **snarkify/jolt-cpp@821e1bd**, using jolt-cpp's own `expand_virt_seq`. Formats: meshsim
`wip/meshsim/REPLAY-INPUT-REQUIREMENTS.md` section 2 and spec 4.19.3-4.19.6, with the changes below. `manifest.json`
has every file's size and SHA-256.

| path | content |
|---|---|
| `opcode_table.txt` | `code name class` for all 171 opcodes at 821e1bd (renumbered against 4f36b3d); S/M/L for the 88 row opcodes a template can emit, `-` otherwise (`ReplayerAlu`'s NAME/CLASS format) |
| `replayer/rom.bin` | 512 words: 0 padding, 1 shared NOP, then every ROM template (379 words used) |
| `replayer/decode_table.bin` | 2,048 entries: 303 ROM, 20 controller (class 1 inline, 2 system), **1,725 invalid (class 3)** |
| `replayer/templates.bin`, `template_index.bin` | the 78 controller-fed templates (13,830 words): 15 inlines and 63 system keys |
| `replayer/listing.txt` | ROM placement per decode entry, template numbers and keys |
| `replayer/check.txt` | the build's check output |
| `tracer/` | the Tracer's variant (to come) |

**ROM fill: 379 of 512 words**, every atomic included (188 words: LR/SC and all AMOs, .W and .D), against v1.1's ~508
with the atomics partly controller-fed. Only the inlines and the system instructions (ECALL, EBREAK, MRET, CSRRW,
CSRRS) are controller-fed.

**Changes to the formats**
- Expansion word: `offset_imm` [63], `sign_imm` [62], `advice_reg` [61:56] = bank-1 register - 146 (v1.25 s4, s5).
  Opcode numbers are 821e1bd's.
- **Template-index key** (new; universal tables can't list every word): the entry's instruction word is the word with
  its register fields **cleared** for an inline (class 1; the template resolves any registers through codes 129-131,
  x0 and aliasing included), or **renamed by first appearance** for a system instruction (class 2: rd, then rs1; x0
  stays 0, the first other register becomes 1, the next new one 2), because jolt-cpp's CSR expansions depend on
  rd = x0, rs1 = x0 and rd = rs1. The controller normalizes the presented word the same way before the lookup. CSR
  templates exist for jolt-cpp's six supported CSRs (mstatus, mtvec, mscratch, mepc, mcause, mtval); any other CSR
  has no entry, as jolt-cpp rejects it.

**Checks** (`build_tables build`): every decode index and every key on random words with x0 and aliasing cases,
resolved from these tables exactly as `Replayer.java` does and compared field by field with jolt-cpp's rows: 117,843
words over 107 source opcodes, 0 failures; and every instruction of EVM Zone `4e3451c1d8c5` (616,231) and Cyclone
(545,760) against jolt-cpp's bytecode rows: 0 failures.

Regenerate: build PR #2's tracer; `tracer/tables/build.sh <tracer> <build dir> build_tables`; then
`build_tables build <out>/replayer 400 <evm-zone dir> <cyclone dir>` and `build_tables opcodes <out>/opcode_table.txt`
(`vm.overcommit_memory=1` on small machines).
