# wip/tables: universal expansion tables from jolt-cpp 821e1bd

Built once per jolt-cpp version, good for any guest (DECISIONS-v1.25 s5), by `build_tables` (jolt-asic-planning PR #2,
`tracer/tables/`) from **snarkify/jolt-cpp@821e1bd**, using jolt-cpp's own `expand_virt_seq`. Formats: meshsim
`wip/meshsim/REPLAY-INPUT-REQUIREMENTS.md` section 2 and spec 4.19.3-4.19.6, with the changes below. `manifest.json`
has every file's size and SHA-256.

| path | content |
|---|---|
| `opcode_table.txt` | `code name class` for all 171 opcodes at 821e1bd (renumbered against 4f36b3d); S/M/L for the 88 row opcodes a template can emit, `-` otherwise (`ReplayerAlu`'s NAME/CLASS format) |
| `replayer/rom.bin` | 512 words: 0 padding, 1 shared NOP, then every ROM template (388 words used) |
| `replayer/decode_table.bin` | 2,048 entries: 306 ROM, 17 controller (15 inlines class 1; CSRRW and CSRRS class 2), **1,725 invalid (class 3)** |
| `replayer/templates.bin`, `template_index.bin` | the 75 controller-fed templates (13,821 words): 15 inlines and 60 CSR keys |
| `replayer/listing.txt` | ROM placement per decode entry, template numbers and keys |
| `replayer/check.txt` | the build's check output |
| `tracer/` | the Tracer's variant (below) |

**ROM fill: 388 of 512 words**: every RV64IMAC template, **all atomics** (188 words: LR/SC and every AMO, .W and .D),
and ECALL (7), EBREAK (1), MRET (1). v1.1: ~508 with the atomics partly controller-fed. Controller-fed: the 15 inlines,
and CSRRW/CSRRS (60 keys, 102 words): they would fit in the 124 free words, but the decode index can't reach them (see
REPORT, "The CSR instructions"); the owner decides.

**Changes to the formats**
- Expansion word: `offset_imm` [63], `sign_imm` [62], `advice_reg` [61:56] = bank-1 register - 146 (v1.25 s4, s5).
  Opcode numbers are 821e1bd's (`opcode_table.txt`).
- Decode-table entries for encodings that aren't valid instructions are **class 3** (invalid).
- **The template-index key** (replaces "one entry per distinct instruction word"; universal tables can't list every
  word). Each 16-byte entry keeps its layout (key u32, word offset u32, length u32, `rd0`, class, two spare bytes);
  only the meaning of bytes 0-3 changes. To find the template for a presented code-stream word *w*, the controller
  computes **key(w)** and looks it up; the entries are sorted by key, ascending, and an entry's template number is
  its position. key(w) keeps every bit of *w* except register fields:
  - **class 1, an inline** (*w*[6:0] = 0001011): rd [11:7], rs1 [19:15] and rs2 [24:20] are **cleared**. One entry per
    inline (15).
  - **class 2, CSRRW or CSRRS** (*w*[6:0] = 1110011, funct3 1 or 2): rd [11:7] and rs1 [19:15] are **renamed by first
    appearance**: x0 stays 0; a non-zero rd becomes 1; rs1 becomes 0 if x0, 1 if equal to a non-zero rd or if rd is x0,
    else 2. The CSR field [31:20] is kept. The five patterns per CSR (rd, rs1) = (0,0), (0,1), (1,0), (1,1), (1,2) are
    the cases jolt-cpp's expansion distinguishes (rd = x0, rs1 = x0, rd = rs1); entries exist for jolt-cpp's six CSRs
    (0x300 mstatus, 0x305 mtvec, 0x340 mscratch, 0x341 mepc, 0x342 mcause, 0x343 mtval) and both instructions: 60. A
    word whose key has no entry (another CSR) is an error, as jolt-cpp rejects it.
  - The template's operand codes 129-131 still resolve against the presented word *w*, not the key.

**Checks** (`build_tables build`): every decode index and every key on random words with x0 and aliasing cases,
resolved from these tables exactly as `Replayer.java` does and compared field by field with jolt-cpp's rows: 117,676
words over 108 source opcodes, 0 failures; 500 mutants (single-field changes, spec don't-cares excluded), all caught; and every instruction of EVM Zone `4e3451c1d8c5` (616,231) and Cyclone
(545,760) against jolt-cpp's bytecode rows: 0 failures.

Regenerate: build PR #2's tracer; `tracer/tables/build.sh <tracer> <build dir> build_tables`; then
`build_tables build <out>/replayer 400 <evm-zone dir> <cyclone dir>` and `build_tables opcodes <out>/opcode_table.txt`
(`vm.overcommit_memory=1` on small machines).

## The Tracer's tables (`tracer/`)

Built by `tracer_tables.py` (PR #2, `tracer/tables/`) from the replayer's tables above: the same format, the same
decode entries and template numbers, and **every traced word bit-identical to the replayer's** (checked over all 2,048
decode entries and 75 templates; only `advice_reg` differs, a Tracer-only field). Added: **untraced** words (bit 55)
that compute every advice value in bank 1 (v1.3 s7-8, s10; v1.25 s4-5).

| file | content |
|---|---|
| `rom.bin`, `decode_table.bin` | **404 of 512 words**: the replayer's 388 plus 16 untraced words in the division family (WORKER7's rows; `w7/templates.py`, renumbered to 821e1bd) |
| `templates.bin`, `template_index.bin` | 75 templates, 18,901 words: the seven secp256k1 inlines gain their untraced advice programs; index byte 14 = 1 marks a template with segments |
| `segments.bin` | per segmented template: u32 template number, u32 segment count, then (u32 word offset into the template, u32 length, u32 repeat) per segment |
| `template_map.txt` | replayer template number -> Tracer template number (identity at 821e1bd: both units have the same controller-fed set) |
| `listing.txt`, `check.txt` | sizes and the check |

**Untraced words.** `untraced` = 1; rd is a bank-1 register (146-255); sources are bank-1 registers, x0, the source
registers (130, 131) or the replayer's temporaries; `const` as usual. No `virt`, `report_c`, `from_log`. In Tracer words
**`seq_remaining` counts traced words only** (v1.25 s5, Q6): an untraced word carries the count of the next traced word,
so a template ends at the traced word with `seq_remaining` 0 and never at an untraced word. A template's first word
carries `rd0` whatever its kind. A traced advice row (`VirtualAdvice`, `from_log`) reads its value from bank-1 register
146 + `advice_reg`. Untraced `LD` (WORKER7 Q9) reads guest memory and logs nothing. Untraced rows use the replayer's ALU
opcodes plus RV64 M division (`DIV`, `DIVU`, `DIVUW`), and `SLT`.

**Segments** (v1.3 s10): a template is the concatenation of its segments, each emitted `repeat` times by C&C 3. Only
the two division inlines are segmented: prologue x1, Fermat loop body x256, epilogue x1, then the traced words x1.

| template | traced words | untraced rows | stored untraced words | segments (length x repeat) |
|---|---|---|---|---|
| `SECP256K1_MULQ` | 186 | 237 | 237 | |
| `SECP256K1_SQUAREQ` | 167 | 233 | 233 | |
| `SECP256K1_DIVQ` | 194 | 123,979 | 1,069 | 129x1, 482x256, 458x1 |
| `SECP256K1_MULR` | 232 | 414 | 414 | |
| `SECP256K1_SQUARER` | 213 | 410 | 410 | |
| `SECP256K1_DIVR` | 240 | 214,957 | 1,777 | 129x1, 836x256, 812x1 |
| `SECP256K1_GLVR_ADV` | 13 | 940 | 940 | |
| division family (8, ROM) | 7-14 | 1-4 | | |

The inverse is Fermat's b^(m-2) with a uniform body: square, multiply by b, select by the exponent's top bit (data
independent: the exponent is the constant m - 2, shifted left each pass), so the row sequence never depends on the data.
At most 26 bank-1 registers are live at once (210-229 hold the loop state).

**Check** (`check.txt`): every untraced program executed, loops instantiated, from these words, and each traced advice
row's value compared with jolt-cpp's own advice (`advice_ref`: the instruction expanded by jolt-cpp and executed by
`execute_instruction`, secp256k1 advice by `evaluate`): division family 8 x 200 cases with the edge cases (divide by
zero, overflow, INT32_MIN / -1), multiply/square 4 x 200, GLV 200, the two division inlines 10 each: **0 mismatches**.
