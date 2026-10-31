# Worker 7: the Tracer's advice expansions

**Reference.** File:line references are to the jolt-cpp tracer copy the spec cites, jolt-asic-planning
`tracer-standalone` at **4f36b3d**, under `tracer/src/tracer/`, unless marked *main* (jolt-cpp `main`,
`52fbbab3b`) or *upstream* (Rust Jolt at `735355e`, the spec's ground-truth commit, now under
`LayerZero-Labs/jolt`; read through the GitHub API, not built or run). Questions are in `QUESTIONS.md`;
artefacts are in `wip/tracer/` (its README says how to rebuild them).

**Summary.**
- **The division family is done.** Jolt's advice equals RV64 M's result for DIVU and DIV in every case. DIVW,
  DIVUW and the two remainder advices differ from the plain operation in specific edge cases, each fixed by
  at most three extra rows. The eight templates add **16 untraced words** in all. They were checked against
  jolt-cpp's own compiled code on **8,040,000 cases with no failure**, and the checks caught all 11 deliberately
  broken templates.
- **The inlines' advice** (T5) comes only from the seven secp256k1 inlines.
  - Multiply and square: 200 to 381 untraced rows each, generated and checked against jolt-cpp.
  - Division (a modular inverse): about 50,000 to 110,000 straight-line rows, so C&C 3's software is the
    practical route.
  - GLV: about 700 rows [estimate] against 13 traced.
- **Three findings for the owner**, below first, because they bear on the ground truth rather than on the
  Tracer alone.

---

## Findings for the owner

### F1. jolt-cpp `main` has moved past the spec's reference commit

jolt-cpp `main` (`52fbbab3b`) is no longer the tracer the spec and `replayer-opcodes.md` were derived from
(jolt-asic-planning 4f36b3d, copied from jolt-cpp `4ab349bb`, 2026-10-19).
- **Opcode set.** `main` adds `VirtualLoadB/H/W` and `VirtualStoreB/H/W` (*main* `riscv.inc:139–144`), and drops
  `VirtualAlignAddr`, `VirtualAssertHalfwordAlignment` and `VirtualAssertWordAlignment`.
- **Expansions.** The sub-word load and store expansions are rewritten (*main* `isa/virtual.cpp:234–262`, the
  AMOs with them), and `VirtualInstruction` gains a field (`shift_offset`).
- **Effect.** The replayer's opcode table, its ROM templates and the template count of spec 4.19.6 are all
  affected.
- **This work is not:** the eight DIV/REM advice cases are **byte-identical** (4f36b3d `trace_owner.cpp:536–630`
  against *main* `527–621`). *main*'s secp256k1 modular inverse is libsecp256k1's `modinv_var`
  (`inlines/secp256k1_inverse.c`) instead of Fermat, but an inverse is unique, so the advice values are the
  same.

Which commit the hardware tracks is the owner's call (Q1). The tools in `wip/tools/replayer-opcodes/` and
`wip/tracer/harness/` rebuild against any commit.

### F2. Upstream Jolt has an advice tape that jolt-cpp doesn't implement

Upstream at 735355e has a second kind of advice: an **advice tape** the guest reads with five instructions.

| instruction | upstream source | value |
|---|---|---|
| `AdviceLB`, `AdviceLH`, `AdviceLW`, `AdviceLD` | `tracer/src/instruction/advice_l{b,h,w,d}.rs` | the next 1, 2, 4 or 8 bytes of the advice tape (`advice_tape_read`) |
| `VirtualAdviceLen` | `virtual_advice_len.rs` | the bytes left on the tape (`advice_tape_remaining`) |
| `VirtualAdviceLoad` (row opcode) | `virtual_advice_load.rs` | the tape read inside the expansions above, *imm* bytes |

- **Encoding.** They are custom-2 (opcode 0x5B), funct3 0 (`FUNCT3_VIRTUAL_R`), funct7 0x00–0x04, sharing
  funct3 0 with `VirtualRev8W` (funct7 0x05) (*upstream* `instruction/mod.rs:13–17`, `1372–1393`).
- **jolt-cpp support.** **jolt-cpp supports none of them** (neither commit has the opcodes). Its
  `JOLT_ADVICE_WRITE` syscall is accepted and ignored (`syscalls.cpp:63–69`). So a guest built against
  upstream's advice tape can't be traced by our reference today.
- **If it becomes needed,** the tape is a new source of read-log words. The Tracer would read the tape with
  untraced rows from wherever it lives, keeping a cursor; that design is for the owner.
- **The decode index can't separate these instructions.** Spec 4.19.5's general index uses
  {*w*[6:2], funct3, *w*[30], *w*[25]}. funct7 0x00, 0x02 and 0x04 map to one index, and 0x01, 0x03 and
  0x05 (`VirtualRev8W`) to another. They would need a special group like the AMOs' (Q8).

Our two measured programs (EVM Zone, Cyclone) trace with jolt-cpp, so they don't use the tape.

### F3. Jolt's assertions don't pin the advice in five divide-by-zero cases

Each DIV/REM expansion checks its advice with virtual assertions (`isa/virtual.cpp:322–480`). In five cases
those checks **accept more than one advice value**, and the final rd still comes out right.

| instruction | the case | why the checks pass | the value jolt-cpp logs |
|---|---|---|---|
| REM | rs2 = 0 | quotient × 0 = 0; `VirtualAssertValidUnsignedRemainder` passes for any remainder when the divisor is 0 | 0 |
| REMU | rs2 = 0 | the same | 2⁶⁴ − 1 |
| REMW | rs2[31:0] = 0 | the same | 0 |
| REMUW | rs2[31:0] = 0 | the same | 0x0000_0000_FFFF_FFFF |
| DIVUW | rs2[31:0] = 0, **upper 32 bits only** | `VirtualAssertValidDiv0` sees the advice after `VirtualSignExtendWord`, so only its low word is checked | 0x0000_0000_FFFF_FFFF |

**How this was found.** Shown by running jolt-cpp's own expansions with substituted advice
(`wip/tracer/results/mutants.txt`): for example, REM with advice 1 at divide-by-zero, REMU and REMUW with advice
0, and DIVUW with a sign-extended advice all pass every assertion in every case; the advice row's value is the
only difference. DIVU's divide-by-zero advice, by contrast, **is** pinned (`VirtualAssertValidDiv0` on the
advice itself fails 1,261 of 25,000 cases when it is wrong).

**Consequences:**
- The trace isn't unique in these cases, so the Tracer must reproduce the reference's choice exactly, and our
  validation must compare the advice words themselves, not just rd. The harness does (its check C1).
- **Upstream agrees with jolt-cpp here**, by reading the source: the advice values computed in
  `div.rs`, `divu.rs`, `divw.rs`, `divuw.rs`, `rem.rs`, `remu.rs`, `remw.rs` and `remuw.rs` (each file's
  `trace`) match jolt-cpp's formulas in every case, these five included. This is from source, not from running
  both; confirming it by running is ST1's job (Q7).
- **The same pattern appears in the inlines.** `SECP256K1_GLVR_ADV` only stores its advice (`inlines/secp256k1_glvr_adv.inc`: 12 advice and SD rows,
  no check), so whatever constrains the decomposition is in guest code, not the expansion; only jolt-cpp's
  values match the trace (not traced through the guest code here).

Whether an unpinned advice value is a soundness concern for Jolt itself is outside this study. Here it only
means the hardware must copy the reference's choice.

---

## 1. Inventory of advice sources

Every row that takes advice reaches the trace as `VirtualAdvice` with rs1, rs2 and imm 0
(`trace_owner.cpp:912–917`, `trace_owner.hpp:164–177`) and is a read-log word (spec 4.19.2).

### 1.1 The DIV/REM advice opcodes

Six opcodes are emitted, and two more are defined but never emitted. They are jolt-cpp's own: `riscv.inc:190`
says "not part of jolt". Upstream feeds the same values through `VirtualAdvice` from each instruction's
`trace` (F3).

| opcode (code) | emitted by | Jolt's value (`trace_owner.cpp`) | line |
|---|---|---|---|
| `VirtualAdviceDivU` (160) | DIVU `:370`, REMU `:385` | 2⁶⁴ − 1 if rs2 = 0, else ⌊rs1 / rs2⌋ unsigned | 545–553 |
| `VirtualAdviceDiv` (162) | DIV `:331` | 2⁶⁴ − 1 if rs2 = 0; rs1 if rs1 = INT64_MIN and rs2 = −1; else s64(rs1) / s64(rs2) truncated | 563–573 |
| `VirtualAdviceRem` (163) | REM `:353` | 0 if rs2 = 0; 2⁶³ if INT64_MIN / −1; else \|trunc(s64(rs1) / s64(rs2))\|: **the quotient's magnitude** | 574–584 |
| `VirtualAdviceDivUW` (164) | DIVUW `:404`, REMUW `:424` | 0x0000_0000_FFFF_FFFF if rs2[31:0] = 0, else zx32(rs1[31:0] / rs2[31:0]) unsigned | 611–619 |
| `VirtualAdviceDivW` (166) | DIVW `:447` | 2⁶⁴ − 1 if rs2[31:0] = 0; 0x0000_0000_8000_0000 (**zero-extended**) if INT32_MIN / −1; else sx32(trunc(s32 / s32)) | 585–597 |
| `VirtualAdviceRemW` (167) | REMW `:473` | 0 if rs2[31:0] = 0; 0x8000_0000 zero-extended if INT32_MIN / −1; else zx32(\|trunc(s32 / s32)\|) | 598–610 |
| `VirtualAdviceRemU` (161), `VirtualAdviceRemUW` (165) | never | dead cases (they compute the quotient) | 554–562, 620–628 |

### 1.2 `VirtualAdvice` in the inlines

Only the seven secp256k1 inlines take advice. BLAKE2b, BLAKE3 (both forms), Keccak-256 (both), SHA-256 (both)
and the bigint multiply take none (`grep emit_advice inlines/*.inc`).

| inline | traced rows | advice words | the advice (`inlines/secp256k1.cpp`) |
|---|---|---|---|
| `SECP256K1_MULQ_INLINE` | 186 | 4 | ⌊a·b / q⌋, the quotient (`multiply_field`, 120–129) |
| `SECP256K1_SQUAREQ_INLINE` | 167 | 4 | ⌊a² / q⌋ |
| `SECP256K1_MULR_INLINE` | 232 | 4 | ⌊a·b / r⌋ |
| `SECP256K1_SQUARER_INLINE` | 213 | 4 | ⌊a² / r⌋ |
| `SECP256K1_DIVQ_INLINE` | 194 | 8 | interleaved: out = a · b⁻¹ mod q, and the witness ⌊b·out / q⌋ (`divide_field`, 131–146) |
| `SECP256K1_DIVR_INLINE` | 240 | 8 | the same mod r |
| `SECP256K1_GLVR_ADV_INLINE` | 13 | 6 | the GLV split of k = a mod r: (sign, \|k1\| low, high, sign, \|k2\| low, high) (`decompose`, 166–175) |

Here q and r are secp256k1's field prime and group order, and a and b are the 256-bit values at the addresses
in the instruction's rs1 and rs2 (`isa/virtual.cpp:1238–1244`, `inlines/secp256k1_instruction.hpp`).

**How jolt-cpp computes them.** All the advice is computed once, **at the start of the inline, from memory**:
`prepare_advice` (`trace_owner.cpp:52–66`) reads 32 bytes at `regs[rs1]`, and at `regs[rs2]` for the two-input
forms. It calls `evaluate`, which aborts the trace on a zero divisor, a quotient that doesn't fit 256 bits, or a
GLV half longer than 128 bits (`release_assert`, `trace_owner.cpp:62`; `std::abort`, `secp256k1.cpp:161`). The advice
rows then pop the words in order (`trace_owner.cpp:780–787`, `847–854`).

**Why the untraced work needs its own loads.** The traced rows interleave loads and advice: in `mulq.inc` the
first advice row comes after only limb 0 of each operand is loaded. So the untraced work needs all 4 or 8 limbs
**before** the first advice row (Q9).

The word counts match the shapes table (`inlines/secp256k1_shapes.hpp`).

### 1.3 Not row advice (for completeness)

- **The trusted and untrusted advice regions** are program inputs in guest memory, read by ordinary LDs
  (`loader.hpp:25–30`). They are part of the memory image, not of the Tracer's computation.
- **The `JOLT_ADVICE_WRITE` syscall** is ignored by jolt-cpp (`syscalls.cpp:63–69`).
- **Upstream's advice tape:** F2.

---

## 2. The division family

### 2.1 Jolt's advice against RV64 M, every edge case

Run on jolt-cpp's compiled advice code over 25 × 25 edge operand pairs: 0, ±1, 2, 3, 7, INT64_MIN/MAX and
neighbours, the 32-bit boundaries, and words with garbage upper halves
(`wip/tracer/results/check_div.txt`). "Plain RV64 M" means the obvious native operation: DIVU, DIV, DIVUW, DIVW,
and for the two remainder advices (which are quotient magnitudes) \|DIV\| and \|DIVW\|.

| advice | plain RV64 M | differ (of 625) | where they differ |
|---|---|---|---|
| `VirtualAdviceDivU` | DIVU | 0 | nowhere: division by zero gives 2⁶⁴ − 1 in both |
| `VirtualAdviceDiv` | DIV | 0 | nowhere: by zero, −1 in both; −2⁶³ ÷ −1 = −2⁶³ in both |
| `VirtualAdviceRem` | \|DIV\| | 25 | **division by zero only:** Jolt 0, \|−1\| = 1 |
| `VirtualAdviceDivUW` | DIVUW | 124 | **division by zero** (Jolt 0x0000_0000_FFFF_FFFF, RV64 all ones) and **any quotient with bit 31 set**: Jolt zero-extends, RV64 sign-extends |
| `VirtualAdviceDivW` | DIVW | 15 | **INT32_MIN ÷ −1 only:** Jolt 0x0000_0000_8000_0000, RV64 0xFFFF_FFFF_8000_0000 |
| `VirtualAdviceRemW` | \|DIVW\| | 100 | **division by zero only:** Jolt 0, 1 |

So signed overflow needs a fix only in the W form, and division by zero only in the remainders and DIVUW. The
W forms' 32-bit extension is the whole of DIVUW's other difference.

### 2.2 The templates

Each Tracer template is the replayer's template unchanged, plus untraced rows **immediately before the advice
row**. The untraced rows compute the advice into bank-1 register 146 (`b0`; and 147, `b1`, as scratch). The
advice row is `VirtualAdvice rd, 0, 0` with `from_log` set and `advice_reg` = 146 − 128 = 18. Data:
`wip/tracer/templates.py`; words: `wip/tracer/out/div_templates.*`.

| instruction | untraced rows | why |
|---|---|---|
| DIVU, REMU | `DIVU b0, rs1, rs2` | equal in every case |
| DIV | `DIV b0, rs1, rs2` | equal in every case |
| DIVW | `DIV b0, t0, t1`: a **64-bit** DIV of the sign-extended operands, which Jolt's first two traced rows already hold in t0 and t1 | gives −1 on zero, +2³¹ (zero-extended) on overflow, and the sign-extended quotient otherwise: exactly Jolt's value, with no fix-up row |
| DIVUW, REMUW | `DIVUW b0, rs1, rs2`; `VirtualZeroExtendWord b0, b0, 0` | zero- instead of sign-extension; zx32(sx32(x)) = zx32(x) also fixes division by zero |
| REM | `DIV b0, rs1, rs2`; `VirtualNegateIf b0, b0, b0` (the magnitude); `SLTU b1, x0, rs2` (rs2 ≠ 0); `MUL b0, b0, b1` | magnitude of the quotient, forced to 0 on division by zero; −2⁶³ ÷ −1 gives 2⁶³ as Jolt does |
| REMW | as REM, on t0 and t1 (64-bit DIV of the sign-extended operands) | the 64-bit DIV gives +2³¹ on overflow, whose magnitude is Jolt's 0x8000_0000 |

| template | Tracer words | untraced | replayer words |
|---|---|---|---|
| DIVU | 9 | 1 | 8 |
| REMU | 8 | 1 | 7 |
| DIV | 13 | 1 | 12 |
| REM | 13 | 4 | 9 |
| DIVUW | 13 | 2 | 11 |
| REMUW | 11 | 2 | 9 |
| DIVW | 15 | 1 | 14 |
| REMW | 15 | 4 | 11 |
| **total** | **97** | **16** | **81** |

The replayer counts match `replayer-opcodes.md` section 7. Everything the untraced rows use is in the Tracer's
ALU: section 8's divider, plus SLTU, MUL, `VirtualNegateIf` and `VirtualZeroExtendWord` from the replayer's.

**Conventions the words follow.** These are my proposals (Q4–Q6), except `advice_reg`, which the owner has confirmed:
- `advice_reg` = register − 128 (confirmed);
- `seq_remaining` counts every word of the Tracer's template, untraced included;
- `rd0` is in the first word, whatever its kind;
- `virt`, `report_c` and `from_log` are on traced words only;
- a template never ends with an untraced row, so `pc_increment` stays on the last traced row;
- the untraced DIVW and REMW rows read bank-0 temporaries written earlier in the same expansion, which needs
  the Tracer to run whole expansions (Q5, Q6).

**ROM budget.** The 16 untraced words are new ROM pressure: the replayer's fill is already about 508 of 512
(spec 4.19.6). If the Tracer's ROM list includes the division family, something else must move to C&C 3, or the
division family itself, which spec 4.19.6 names first (Q11).

### 2.3 The check

`wip/tracer/harness/check_div.cpp` compiles against the reference tracer and runs both sides on every case.
- **Jolt's side:** jolt-cpp's `expand_virt_seq` and `execute_instruction`, so the advice comes from its own
  `VirtualAdvice*` cases.
- **The Tracer's side:** the generated 128-bit words, decoded and run. Untraced rows use an RV64 M model written
  from the RISC-V spec for the divider, and jolt-cpp's own code for every other opcode. Bank 1 starts with random
  garbage.

It checks four things:
- **C1:** the logged advice equals jolt-cpp's;
- **C2:** the traced words are exactly jolt-cpp's rows, with the advice row as `VirtualAdvice`;
- **C3:** every row's record values match, no assertion fails, and the final register file is identical;
- **C4:** rd equals RV64 M, which cross-checks the RV64 M model against Jolt's expansion.

**Cases.** 1,005,000 per template:
- the 625 edge pairs under 8 register assignments (distinct; rd = rs1; rd = rs2; rs1 = rs2; all three equal;
  rs1 = x0; rs2 = x0; high registers);
- 1,000,000 random pairs, drawn from wide, small, negative, 32-bit, sign-extended, garbage-upper-half and edge
  values;
- rd = x0, which Jolt turns into its single NOP row, as `rd0` = 1 says.

**Result: 8,040,000 cases, no failure** (84 s).

**Negative tests** (`harness/mutants.py`, `results/mutants.txt`). All 11 broken templates are caught. They
include each fix-up removed, DIVW done as a native DIVW, the wrong `advice_reg`, the untraced row after the
advice row, \|DIVU\| for \|DIV\|, a changed traced row, and wrong advice at divide-by-zero only. The last group
is where F3 came from.

---

## 3. The inlines' advice (T5)

C&C 3 feeds the inline templates (DECISIONS-v1.3 section 7), so for each inline the question is whether its
template carries untraced rows that compute the advice, or C&C 3's software computes it while supplying the
words.

**The software route needs no new row format.** C&C 3 can deliver the values as untraced `ADDI bk, x0, #value`
words (code 128 carries a 64-bit `const`), one per advice word. What it does need is the operands: 64 bytes of
guest memory that may be dirty in the Tracer's D-cache (Q10).

### 3.1 Sizes

The multiply and square advice was **generated as straight-line untraced rows** and run against jolt-cpp's
compiled `evaluate()` (`wip/tracer/inline_rows.py`, `harness/secp_advice.cpp`, `results/inline_rows.txt`).
- **Method:** a schoolbook 512-bit product, then the reduction m = 2²⁵⁶ − c, folding H·2²⁵⁶ into H·c (two rounds
  for q, three for r), then one compare.
- **Ops used:** the replayer's ADD, SUB, SLTU, OR, XORI, MUL, MULHU and ADDI-with-`const`, plus an untraced LD
  per operand limb.
- **Result:** 20,000 cases per inline, no mismatch. Dropping the final compare gives about 25 % mismatches, so the
  check bites.

| inline | untraced rows | of which multiplies | peak live bank-1 registers | traced rows | ratio |
|---|---|---|---|---|---|
| MULQ | **204** (checked) | 42 | 17 | 186 | 1.1 |
| SQUAREQ | **200** (checked) | 42 | 16 | 167 | 1.2 |
| MULR | **381** (checked) | 80 | 21 | 232 | 1.6 |
| SQUARER | **377** (checked) | 80 | 21 | 213 | 1.8 |
| GLVR_ADV | ~700 [estimate] | — | — | 13 | ~50 |
| DIVQ | ~53,000 [derived] | — | — | 194 | ~270 |
| DIVR | ~110,000 [derived] | — | — | 240 | ~460 |

- **Squares.** These use the general product; a dedicated square would save about 40 rows.
- **GLVR estimate.** From the same blocks: k = a mod r (a compare and conditional subtract, about 30 rows); two
  rounded divisions by r of a 384-bit product (about 240 each, remainder and rounding compare included); then
  four products of at most 2 × 3 limbs and two signed differences (about 200).
- **DIVQ and DIVR.** The inverse dominates. Straight-line means Fermat (bⁿ⁻² mod n): about 255 modular squarings
  plus an addition chain's 15 (q) or 40 (r) multiplies, each about 196 or 373 rows, plus a final multiply and the
  witness quotient. A constant-time safegcd (libsecp256k1's `modinv64`, 590 divsteps) would be about
  10,000–12,000 rows [estimate]. The variable-time versions jolt-cpp *main* uses branch, which a template can't.

**How often.** Spec 4.19.2 measures 0.0001 (EVM Zone) and 0.0002 (Cyclone) advice words per row [meas: design
notes]. Taking those as the inlines' advice (jolt-cpp logs only these), that is about 6.6 and 13 words per
65,536-row window on average, so one to three multiply-class inlines per window. In untraced rows that is under
1 % on average [derived]. But secp256k1 work is bursty: a window spent in one signature recovery is mostly these
inlines, and there untraced rows would roughly double the Tracer's work for the window.

### 3.2 Recommendations, one per inline (not decided)

| inline | recommended | why |
|---|---|---|
| MULQ, SQUAREQ | **untraced rows** in the C&C 3-fed template | about 200 rows, checked; about 1.1× the traced work; needs no C&C 3 computation, only untraced LDs (Q9) |
| MULR, SQUARER | **untraced rows** | about 380 rows, checked; 1.6–1.8× |
| DIVQ, DIVR | **C&C 3 software**, 8 `ADDI` words | the inverse is 50,000–110,000 rows straight-line: too long to feed per instance. Software computes it in microseconds; this route needs C&C 3 to read the operands (Q10) |
| GLVR_ADV | **C&C 3 software**, 6 `ADDI` words | about 700 rows against 13 traced, and once the division inlines need the software route, GLV can share it |

**Alternatives.**
- **All software:** if Q10's operand access is cheap, putting all seven in software keeps the Tracer to one
  mechanism, at the cost of a C&C 3 computation for every multiply (each already stalls for its words).
- **No software:** a modular-inverse unit in the Tracer would let even division be rows. That is hardware design,
  so it is only recorded (Q10).

**On any route**, jolt-cpp aborts the trace for a zero divisor, a quotient overflow, or an over-long GLV half. A
guest that hits one can't be proven, so the Tracer (or C&C 3) needs a defined error, not a guess (Q12).

---

## 4. Validation plan

1. **Unit, done.** `check_div` (8.04 M cases, C1–C4) and its mutants; `inline_rows.py` against `evaluate()`. Rerun
   both on every jolt-cpp commit the owner adopts (F1); `harness/build.sh` takes the tracer path.
2. **Real programs: advice extraction.** A capture tool beside jolt-asic-planning's `capture_writes` runs jolt-cpp
   on EVM Zone (`trace-29`) and Cyclone (`29`). It records every advice row in row order: cycle, pc, source
   opcode, the advice value, and its inputs (the register values for DIV/REM, the 32-byte operands for the
   inlines). It also counts instances per opcode and per edge class (division by zero, overflow, W forms with a
   non-zero upper half).
3. **Real programs: the Tracer model.** The same run with the Tracer's templates substituted for the
   division-family expansions (untraced rows on the ALU model) and, for the inlines, whichever route is chosen.
   It emits the **read log** (LD values, advice, next `instr_idx` of every control transfer), and that must equal
   the read log derived from jolt-cpp word for word. Comparing words, not just rd, is required by F3.
4. **Coverage the programs miss.** If step 2 shows an edge class never occurs (likely for divide-by-zero and
   overflow), small guest programs exercise every DIV/REM opcode in every edge class and every secp256k1 inline at
   its quotient corners. They are traced through steps 2–3, so each case runs through a real expansion and log.
5. **Window boundaries.** A window cut that falls on an advice row, and one between untraced and traced rows. The
   Tracer must compute the advice once and put the word in the window whose rows consume it, with the next offset
   counting traced rows only (Q4, Q5).
6. **End to end.** The replayer model replays the Tracer's run log; its rows must equal jolt-cpp's bit for bit
   (spec item 58's harness).
7. **Upstream (ST1).** For F3's five unpinned cases, run guests hitting them through Rust Jolt at 735355e and
   compare the advice words with jolt-cpp's.
