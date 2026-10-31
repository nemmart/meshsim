# Appendix: Replayer opcode semantics

*Part of the mesh-sorter v1.1 specification, linked from `machine-spec.md` 4.19 (4.19.7, item 112). Derived from the jolt-cpp tracer, `github.com/quarky93/jolt-asic-planning`, branch `tracer-standalone`, **commit 4f36b3d**; every file:line below is in `tracer/src/tracer/` at that commit.*

**Status** (owner, 2026-10-26, part F):
- **Sections 1–5 are normative for what each row computes:** its slots, its rd value, its control transfer, its log word and LD's and SD's memory fields. `machine-spec.md` 4.19.7 keeps the classes, the schedules and the pc and `instr_idx` rules, and 4.19.12 the record layout.
- **Sections 6–9 are informative:** the producers of each slot form, the source-only and never-occurring opcodes, and the test vectors (for meshsim's ALU tests).
- **Two deliberate narrowings** (4.19.7, invariant 16): the masks reaching `VirtualPext` and `VirtualPextSigned` are contiguous and non-zero, and those reaching `VirtualSRL`, `VirtualSRLI`, `VirtualSRA` and `VirtualSRAI` are non-zero. The replayer builds only those cases; any other mask gives an undefined result, and meshsim flags it. The general definitions below are what the tracer computes.
- **Flags F1–F6 (section 4) are resolved by part F;** the flag text records the state before it. F1 (JAL with imm 0): `imm0_pc0` is set in every JAL word, ROM or controller-fed (4.19.3, 4.19.7's pc table, item 56). F2 (EBREAK): its one controller-fed word is `JAL v40, 0` with `imm0_pc0` and `next_from_value` (4.19.5, 4.19.4). F3 (a new pc of 0): ends the program from any row (4.19.7's pc table, item 57) *(pending the upstream check, item 113)*. F4 (LD to x0): LD's memory value comes from rd (4.19.7, 4.19.12); it arises only from LR.D with rd = x0, which depends on item 113's LR/SC question. F5 (the W forms): 4.19.7 defers to this file's per-opcode extension. F6 (zero shift masks): undefined, flagged by meshsim (4.19.7, invariant 16). F7–F12 need no change outside this file.
- **Ground truth** (parts G and H, `machine-spec.md` item 113): the trace is upstream Jolt's, meaning `LayerZero-Research/jolt` at `735355e43026e9ef392b36b4ab5ed0f5e680127f` (the version vendored by jolt-cpp at `309b4c4`); later upstream versions are future compatibility. **For now jolt-cpp is the gold standard:** the replayer, its tests and the reference rows match jolt-cpp, from which this file is derived; upstream at 735355e overrides jolt-cpp only once a difference is confirmed by running both, and then this file (and if need be jolt-cpp) is corrected.
- **The harness** that produced sections 6 and 9 (cited below as `/tmp/h/…`) is in the package at **`tools/replayer-opcodes/`**, with instructions for regenerating the tables against a later tracer commit.

This appendix defines, for every opcode that occurs in a replayer row, exactly what the row computes: the value written to rd, the control transfer, what it takes from the log and the memory fields of its record. An ALU designer or simulator author should be able to implement the replayer's datapath from it without reading the tracer. Where the tracer and the spec disagree, or the spec is incomplete, the point is flagged (F1–F12, section 4) rather than resolved.

**How it was derived.** The opcode set and every operand form come from running the tracer's own expansion code (`expand_virt_seq`, `isa/virtual.cpp`) over every source opcode, and the semantics from `execute_instruction` (`trace_owner.cpp:145–669`), read line by line and exercised by a harness compiled against the tracer's code (section 9). Both harnesses live outside the repository, in `/tmp/h` (`enum.cpp`, `vec.cpp`), built against `libtracer.a` from the repository's own CMake build.

## 1. Conventions

### 1.1 Values and notation

- Every register and every immediate slot holds a **64-bit** value, `RiscRegValue` = `u64`. Arithmetic is modulo 2⁶⁴ unless a formula says otherwise; nothing traps on overflow.
- s64(x), s32(x): x read as a two's-complement signed integer of 64 or 32 bits. **sx32(x)**: bits [31:0] of x sign-extended to 64 bits. **zx32(x)**: bits [31:0] of x, zero-extended. x[i:j]: bits i down to j.
- **<ᵤ, ≤ᵤ, ≥ᵤ**: unsigned compares; **<**, **≥** on s64(·): signed.
- **tz(x)**: the number of trailing zero bits of x, `std::countr_zero`, so **tz(0) = 64** (`utils/math.hpp`).
- **rotr64(x, k)**, **rotr32(x, k)**, **rotl64(x, k)**: rotations by k modulo the width (`std::rotr`; `rotate_left_64` masks k to 6 bits); a rotation by the full width is the identity.
- **bswap32**: reverse the 4 bytes of a 32-bit word.
- **pext(x, m)**: gather the bits of x at the positions of m's 1-bits, from bit 0 upward, into consecutive low bits of the result (`utils::pext<64>`).
- Shift amounts are masked where the table says so; **6 bits** (mod 64) for 64-bit shifts and **5 bits** (mod 32) for word shifts.

### 1.2 Slots and how a row reads them

A row has three operand slots, rd, rs1 and rs2, each either a **register** (R) or an **immediate** (I) (`VirtualInstruction`, `isa/virtual.hpp`). `read_reg_vals` (`trace_owner.cpp:78–83`) gives each slot's value: a register slot reads its register (register 0 always reads 0, because writes to it are dropped), an immediate slot carries its own 64-bit value, set when the row was built (sign-extended from the source encoding, `isa/decoder.cpp`). In the tables, **imm** is whichever slot is the immediate for that opcode, and the Slots column gives the kinds as rd·rs1·rs2, with x0 where the register is always register 0 in that form.

Where each opcode keeps its immediate:

| immediate in | opcodes |
|---|---|
| **rs2** (the I-type place) | ADDI, XORI, ORI, ANDI, SLTI, SLTIU, ADDIW, JALR, LD, `VirtualMULI`, `VirtualMULIW`, `VirtualSRLI`, `VirtualSRAI`, `VirtualSRLIW`, `VirtualSRAIW`, `VirtualROTRI`, `VirtualROTRIW`, the window masks, `VirtualAlignAddr`, the alignment assertions; and an unused 0 in `VirtualRev8W`, `VirtualSignExtendWord`, `VirtualZeroExtendWord`, `VirtualPow2`, `VirtualPow2W`, the shift-right bitmasks, `VirtualMovsign`, `VirtualHostIO` |
| **rs1** | JAL, LUI, AUIPC (`emit_u`, `isa/virtual.cpp:205–215`; decoder `isa/decoder.cpp:1187–1197`), and `VirtualAdvice` (0, with rs2 also immediate 0) |
| **rd** | the branches BEQ–BGEU, SD, `VirtualAssertEQ`, `VirtualAssertLTE`, `VirtualAssertValidDiv0`, `VirtualAssertValidUnsignedRemainder`, `VirtualAssertMulUNoOverflow` (`emit_b`, `emit_s`), and the alignment assertions (rd immediate 0, `emit_h`, `isa/virtual.cpp:196–198`) |
| **all three** | FENCE (all 0, `isa/virtual.cpp:1093–1097`) |

**A slot's kind never varies for an opcode.** Over every emission site the R/I pattern of each opcode is the same; forms differ only in whether a register is x0 (section 6). So operand decode can be a fixed per-opcode property, as the spec's operand codes already allow.

### 1.3 Writing rd

- `save_rd` (`trace_owner.cpp:85–93`) **drops a write to register 0**; every other register, virtual registers included, takes the value.
- A row whose rd slot is an immediate (branches, SD, the assertions, FENCE) **writes nothing**: `save_rd` asserts rd is a register, and these opcodes never call it.
- **All reads happen before the write.** Every case reads its slots first, so a row whose rs1 or rs2 is also its rd sees the old value (JALR computes its link before writing; vector in section 9).

### 1.4 pc, `pc_increment` and `instr_idx`

- **pc** (`risc_pc`) is the **source instruction's** address, the same for every row of its expansion.
- **`pc_increment`** is the source instruction's size, **2 or 4, on its expansion's last row only, and 0 on every other row** (`isa/virtual.cpp:1413–1416`: only `instrs[num_instrs + virt_seq_len − 1].pc_increment` is set; `emit_instr` leaves it 0).
- Each opcode case may set **new_pc** (it starts equal to pc). Then `finish_instruction` (`trace_owner.cpp:111–126`):
  1. **new_pc = 0** → pc = 0 and `instr_idx` = 0: the program has ended. This is checked first, for **every** opcode.
  2. **new_pc ≠ pc** → a control transfer: pc = new_pc and `instr_idx` = the index of new_pc (in the replayer, the row's log word).
  3. **otherwise** → pc += `pc_increment` and `instr_idx` += 1. So a row that isn't its expansion's last leaves pc where it is, and **a branch or jump whose target equals its own pc is sequential** (the self-jump rule).
- The Control column below gives only new_pc; this rule then applies.

### 1.5 The row record

Each row's record is formed around `execute_instruction` (`trace_owner.cpp:889–926`; the derived helpers in `trace_owner.hpp:165–235` agree):

| field | value |
|---|---|
| opcode | the row's opcode, except that the six DIV/REM advice opcodes are reported as `VirtualAdvice` (`trace_owner.hpp:164–166`) |
| rd_reg, rs1_reg, rs2_reg | the slot's register number, or 0 if the slot is an immediate; for advice rows rs1_reg = rs2_reg = 0 |
| rd_prev | rd's value before the row, or 0 if rd is an immediate |
| rs1, rs2 | the registers' values before the row, or 0 for an immediate slot; 0 for advice rows |
| rd | rd's value **after** the row, or 0 if rd is an immediate |
| imm (`_imm`) | the **sum** of the immediate slots' values (rd·[rd imm] + rs1·[rs1 imm] + rs2·[rs2 imm]); at most one is non-zero in any form; 0 for advice rows |
| is_compressed | `pc_increment` = 2 (so only the last row of a compressed instruction's expansion has it) |
| memory fields | LD and SD only (section 3); 0 for every other opcode |

## 2. The row opcodes

91 opcodes occur in rows; the replayer executes 85 of them (the six DIV/REM advice opcodes are written as `VirtualAdvice`, spec 4.19.7). **Class** and cycles are 4.19.7's; the source agrees with all of them (F9). **Last row** says whether the opcode is always, never or sometimes its expansion's last row, which decides whether `pc_increment` is 0 (section 6 lists, for "sometimes", which source instructions end with it). **Source** is the `execute_instruction` case.

### A. Integer ALU

| code | name | class | slots rd·rs1·rs2 | writes rd | rd = | control (new pc) | log | last row | source |
|---|---|---|---|---|---|---|---|---|---|
| 0 | `NOP` | S 3 | x0·I·I (all 0) | no (rd slot is register x0) | nothing: a padding row is never interpreted (no `execute_instruction` case; it would reach `default:` → `unreachable()`) | pc and `instr_idx` held (`hold`, spec 4.19.6); the record is all zeros | — | — (padding) | `trace_owner.hpp:161–163`; `trace_owner.cpp:881–883` |
| 1 | `ADD` | S 3 | R·R·R | yes | (rs1 + rs2) mod 2⁶⁴ | — | — | sometimes | `trace_owner.cpp:161–166` |
| 2 | `ADDI` | S 3 | R·R·I; R·x0·I; x0·x0·I | yes (the NOP form writes x0: dropped) | (rs1 + imm) mod 2⁶⁴; imm is the slot's 64-bit value (sign-extended when the row was built). Load-immediate form `ADDI rd, x0, imm`: rd = imm. NOP form `ADDI x0, x0, 0`: nothing | — | — | sometimes | `trace_owner.cpp:161–166` |
| 3 | `SUB` | S 3 | R·R·R; R·x0·R | yes | (rs1 − rs2) mod 2⁶⁴; with rs1 = x0 (BLAKE2): (0 − rs2) mod 2⁶⁴ | — | — | sometimes | `trace_owner.cpp:173–177` |
| 4 | `XOR` | S 3 | R·R·R | yes | rs1 ⊕ rs2 | — | — | sometimes | `trace_owner.cpp:189–194` |
| 5 | `XORI` | S 3 | R·R·I; x0·R·I | yes (dropped when rd = x0, as in SC) | rs1 ⊕ imm | — | — | sometimes | `trace_owner.cpp:189–194` |
| 6 | `OR` | S 3 | R·R·R | yes | rs1 ∨ rs2 | — | — | sometimes | `trace_owner.cpp:195–200` |
| 7 | `ORI` | S 3 | R·R·I; R·x0·I | yes | rs1 ∨ imm; load-immediate form `ORI rd, x0, imm`: rd = imm | — | — | sometimes | `trace_owner.cpp:195–200` |
| 8 | `AND` | S 3 | R·R·R | yes | rs1 ∧ rs2 | — | — | sometimes | `trace_owner.cpp:183–188` |
| 9 | `ANDI` | S 3 | R·R·I | yes | rs1 ∧ imm | — | — | sometimes | `trace_owner.cpp:183–188` |
| 159 | `ANDN` | S 3 | R·R·R | yes | rs1 ∧ ¬rs2 | — | — | never | `trace_owner.cpp:439–443` |
| 16 | `SLT` | S 3 | R·R·R | yes | 1 if s64(rs1) < s64(rs2), else 0 | — | — | sometimes | `trace_owner.cpp:240–246` |
| 17 | `SLTI` | S 3 | R·R·I | yes | 1 if s64(rs1) < s64(imm), else 0 | — | — | always | `trace_owner.cpp:240–246` |
| 18 | `SLTU` | S 3 | R·R·R | yes | 1 if rs1 <ᵤ rs2, else 0 | — | — | sometimes | `trace_owner.cpp:233–239` |
| 19 | `SLTIU` | S 3 | R·R·I | yes | 1 if rs1 <ᵤ imm, else 0; imm is the sign-extended immediate compared as unsigned (imm −1 is 2⁶⁴ − 1) | — | — | sometimes | `trace_owner.cpp:233–239` |

### B. Word forms, extension and sign helpers

| code | name | class | slots rd·rs1·rs2 | writes rd | rd = | control (new pc) | log | last row | source |
|---|---|---|---|---|---|---|---|---|---|
| 63 | `ADDW` | S 3 | R·R·R | yes | sx32((rs1 + rs2) mod 2³²): the 64-bit sum wraps, then bits [31:0] are sign-extended | — | — | always | `trace_owner.cpp:167–172` |
| 64 | `ADDIW` | S 3 | R·R·I | yes | sx32((rs1 + imm) mod 2³²) | — | — | always | `trace_owner.cpp:167–172` |
| 62 | `SUBW` | S 3 | R·R·R | yes | sx32((rs1 − rs2) mod 2³²) | — | — | always | `trace_owner.cpp:178–182` |
| 110 | `VirtualSignExtendWord` | S 3 | R·R·I | yes | sx32(rs1[31:0]); the rs2 slot (immediate 0) is ignored | — | — | sometimes | `trace_owner.cpp:322–328` |
| 111 | `VirtualZeroExtendWord` | S 3 | R·R·I | yes | rs1 ∧ 0xFFFF_FFFF; rs2 slot ignored | — | — | never | `trace_owner.cpp:329–333` |
| 127 | `VirtualNegateIf` | S 3 | R·R·R | yes | (0 − rs2) mod 2⁶⁴ if s64(rs1) < 0 (bit 63 of rs1 set), else rs2. −INT64_MIN wraps to INT64_MIN | — | — | sometimes | `trace_owner.cpp:522–526` |
| 128 | `VirtualMovsign` | S 3 | R·R·I | yes | 2⁶⁴ − 1 if rs1[63] = 1, else 0; rs2 slot (immediate 0) ignored | — | — | never | `trace_owner.cpp:527–535` |
| 107 | `VirtualRev8W` | S 3 | R·R·I | yes | bswap32(rs1[31:0]) in [31:0] and bswap32(rs1[63:32]) in [63:32]: each 32-bit half byte-reversed in place, the halves not swapped; rs2 slot (immediate 0) ignored | — | — | sometimes | `trace_owner.cpp:387–393` |

### C. Multiply

| code | name | class | slots rd·rs1·rs2 | writes rd | rd = | control (new pc) | log | last row | source |
|---|---|---|---|---|---|---|---|---|---|
| 43 | `MUL` | M 5 | R·R·R | yes | (rs1 · rs2) mod 2⁶⁴, the low half of the 128-bit product (signedness doesn't matter) | — | — | sometimes | `trace_owner.cpp:213–218` |
| 114 | `VirtualMULI` | M 5 | R·R·I | yes | (rs1 · imm) mod 2⁶⁴; for SLLI the immediate is 2^(shamt mod 64), so this is rs1 << shamt | — | — | sometimes | `trace_owner.cpp:213–218` |
| 65 | `MULW` | M 5 | R·R·R | yes | sx32((rs1 · rs2) mod 2³²): only the low 32 bits of the product matter, sign-extended | — | — | always | `trace_owner.cpp:219–226` |
| 115 | `VirtualMULIW` | M 5 | R·R·I | yes | sx32((rs1 · imm) mod 2³²); for SLLIW imm = 2^(shamt mod 32), zero-extended from 32 bits | — | — | always | `trace_owner.cpp:219–226` |
| 45 | `MULHU` | M 5 | R·R·R | yes | ⌊rs1 · rs2 / 2⁶⁴⌋, both unsigned: the high half of the unsigned 128-bit product. Maximum (2⁶⁴−1)² gives 2⁶⁴ − 2 | — | — | sometimes | `trace_owner.cpp:227–232` |

### D. Shifts, bitmasks and rotates

| code | name | class | slots rd·rs1·rs2 | writes rd | rd = | control (new pc) | log | last row | source |
|---|---|---|---|---|---|---|---|---|---|
| 116 | `VirtualPow2` | S 3 | R·R·I | yes | 2^(rs1 mod 64) (shift amount masked to 6 bits); rs2 slot (immediate 0) ignored | — | — | never | `trace_owner.cpp:352–356` |
| 121 | `VirtualPow2W` | S 3 | R·R·I | yes | zx32(2^(rs1 mod 32)): masked to 5 bits and **zero-extended** (amount 31 gives 0x0000_0000_8000_0000, not sign-extended) | — | — | never | `trace_owner.cpp:357–361` |
| 117 | `VirtualShiftRightBitmask` | S 3 | R·R·I | yes | with s = rs1 mod 64: 2⁶⁴ − 2^s, i.e. bits [63:s] set (s = 0 gives all ones). This is the mask whose tz is s, for VirtualSRL/SRA | — | — | never | `trace_owner.cpp:444–448` |
| 146 | `VirtualShiftRightBitmaskW` | S 3 | R·R·I | yes | with s = rs1 mod 32: 2³² − 2^s, i.e. bits [31:s] set and [63:32] clear (zero-extended) | — | — | never | `trace_owner.cpp:449–454` |
| 118 | `VirtualSRL` | S 3 | R·R·R | yes | rs1 >> tz(rs2), logical, with rs2 a bitmask from VirtualShiftRightBitmask (never 0). tz(0) = 64 is undefined behaviour in the tracer (see F6) | — | — | sometimes | `trace_owner.cpp:369–375` |
| 119 | `VirtualSRLI` | S 3 | R·R·I | yes | rs1 >> tz(imm), logical; imm = 2⁶⁴ − 2^(shamt mod 64) from the expansion (never 0), so tz(imm) = shamt mod 64 | — | — | sometimes | `trace_owner.cpp:369–375` |
| 129 | `VirtualSRA` | S 3 | R·R·R | yes | s64(rs1) >> tz(rs2), arithmetic (sign bits shifted in); rs2 a bitmask, never 0 | — | — | always | `trace_owner.cpp:362–368` |
| 120 | `VirtualSRAI` | S 3 | R·R·I | yes | s64(rs1) >> tz(imm), arithmetic; imm as for VirtualSRLI | — | — | always | `trace_owner.cpp:362–368` |
| 147 | `VirtualSRLW` | S 3 | R·R·R | yes | sx32(rs1[31:0] >> (tz(rs2) mod 32)): a logical shift of the low word, then the 32-bit result is **sign-extended** (a shift of 0 sign-extends rs1[31:0]); rs2 from VirtualShiftRightBitmaskW | — | — | always | `trace_owner.cpp:510–515` |
| 148 | `VirtualSRLIW` | S 3 | R·R·I | yes | sx32(rs1[31:0] >> (tz(imm) mod 32)); imm = 2³² − 2^(shamt mod 32) from the expansion | — | — | sometimes | `trace_owner.cpp:510–515` |
| 149 | `VirtualSRAW` | S 3 | R·R·R | yes | sx32(s32(rs1[31:0]) >> (tz(rs2) mod 32)), arithmetic on the low word | — | — | always | `trace_owner.cpp:516–521` |
| 150 | `VirtualSRAIW` | S 3 | R·R·I | yes | sx32(s32(rs1[31:0]) >> (tz(imm) mod 32)); imm as for VirtualSRLIW | — | — | always | `trace_owner.cpp:516–521` |
| 131 | `VirtualROTRI` | S 3 | R·R·I | yes | rotr64(rs1, tz(imm) mod 64); the rotation is encoded as the immediate's trailing zeros (keccak uses masks such as 0xFFFF_FFFF_FFF0_0000). tz(0) = 64 rotates by 64, the identity (`std::rotr` is defined for any count) | — | — | never | `trace_owner.cpp:376–380` |
| 132 | `VirtualROTRIW` | S 3 | R·R·I | yes | zx32(rotr32(rs1[31:0], min(32, tz(imm)))): rs1's upper half is dropped, the result is **zero-extended**, and a rotation of 32 (tz ≥ 32, including imm = 0) is the identity on the low word | — | — | never | `trace_owner.cpp:381–386` |
| 158 | `VirtualXORROTL1` | S 3 | R·R·R | yes | rs1 ⊕ rotl64(rs2, 1) (note: rs2 is rotated, not the XOR) | — | — | never | `trace_owner.cpp:495–499` |
| 133 | `VirtualXORROT63` | S 3 | R·R·R | yes | rotr64(rs1 ⊕ rs2, 63) | — | — | never | `trace_owner.cpp:394–399` |
| 134 | `VirtualXORROT32` | S 3 | R·R·R | yes | rotr64(rs1 ⊕ rs2, 32) | — | — | never | `trace_owner.cpp:400–405` |
| 135 | `VirtualXORROT24` | S 3 | R·R·R | yes | rotr64(rs1 ⊕ rs2, 24) | — | — | never | `trace_owner.cpp:406–411` |
| 136 | `VirtualXORROT16` | S 3 | R·R·R | yes | rotr64(rs1 ⊕ rs2, 16) | — | — | never | `trace_owner.cpp:412–417` |
| 137 | `VirtualXORROTW16` | S 3 | R·R·R | yes | zx32(rotr32(rs1[31:0] ⊕ rs2[31:0], 16)): upper halves dropped, result **zero-extended** | — | — | never | `trace_owner.cpp:418–420` |
| 138 | `VirtualXORROTW12` | S 3 | R·R·R | yes | zx32(rotr32(rs1[31:0] ⊕ rs2[31:0], 12)): upper halves dropped, result **zero-extended** | — | — | never | `trace_owner.cpp:421–423` |
| 139 | `VirtualXORROTW8` | S 3 | R·R·R | yes | zx32(rotr32(rs1[31:0] ⊕ rs2[31:0], 8)): upper halves dropped, result **zero-extended** | — | — | never | `trace_owner.cpp:424–426` |
| 140 | `VirtualXORROTW7` | S 3 | R·R·R | yes | zx32(rotr32(rs1[31:0] ⊕ rs2[31:0], 7)): upper halves dropped, result **zero-extended** | — | — | never | `trace_owner.cpp:427–429` |
| 143 | `VirtualXORROTW22` | S 3 | R·R·R | yes | zx32(rotr32(rs1[31:0] ⊕ rs2[31:0], 22)): upper halves dropped, result **zero-extended** | — | — | never | `trace_owner.cpp:430–432` |
| 144 | `VirtualXORROTW19` | S 3 | R·R·R | yes | zx32(rotr32(rs1[31:0] ⊕ rs2[31:0], 19)): upper halves dropped, result **zero-extended** | — | — | never | `trace_owner.cpp:433–435` |
| 145 | `VirtualXORROTW6` | S 3 | R·R·R | yes | zx32(rotr32(rs1[31:0] ⊕ rs2[31:0], 6)): upper halves dropped, result **zero-extended** | — | — | never | `trace_owner.cpp:436–438` |

**Where the shift amounts come from.** The 64-bit shifts reach the replayer only as these opcodes (SLL–SRAI, `isa/virtual.cpp:565–640`): SLL → `VirtualPow2` + MUL; SLLI → `VirtualMULI` with imm = 2^(shamt mod 64); SRL/SRA → `VirtualShiftRightBitmask` + `VirtualSRL`/`VirtualSRA`; SRLI/SRAI → `VirtualSRLI`/`VirtualSRAI` with imm = `mask_trailing_zeros(shamt mod 64)` = 2⁶⁴ − 2^shamt. The word shifts likewise (SLLW → `VirtualPow2W` + MULW; SLLIW → `VirtualMULIW` with imm = 2^(shamt mod 32); SRLW/SRAW → `VirtualShiftRightBitmaskW` + `VirtualSRLW`/`VirtualSRAW`; SRLIW/SRAIW → imm = 2³² − 2^(shamt mod 32)). So a shift amount is always carried as a power of two (left) or as a mask whose tz is the amount (right), and **no shift mask is ever 0** (F6).

### E. Sub-word memory helpers

| code | name | class | slots rd·rs1·rs2 | writes rd | rd = | control (new pc) | log | last row | source |
|---|---|---|---|---|---|---|---|---|---|
| 154 | `VirtualAlignAddr` | S 3 | R·R·I | yes | (rs1 + imm) mod 2⁶⁴ with bits [2:0] cleared | — | — | never | `trace_owner.cpp:475–479` |
| 152 | `VirtualWindowMaskB` | S 3 | R·R·I | yes | 0xFF << 8·(a mod 8), a = (rs1 + imm) mod 2⁶⁴ | — | — | never | `trace_owner.cpp:463–468` |
| 153 | `VirtualWindowMaskH` | S 3 | R·R·I | yes | 0xFFFF << 8·(a ∧ 6), a = rs1 + imm (bit 0 of a ignored) | — | — | never | `trace_owner.cpp:469–474` |
| 141 | `VirtualWindowMaskW` | S 3 | R·R·I | yes | 0xFFFF_FFFF << 32·a[2], a = rs1 + imm (bits [1:0] of a ignored) | — | — | never | `trace_owner.cpp:455–462` |
| 155 | `VirtualShiftDataB` | S 3 | R·R·R | yes | (rs1 ∧ 0xFF) << 8·(rs2 mod 8) | — | — | never | `trace_owner.cpp:480–484` |
| 156 | `VirtualShiftDataH` | S 3 | R·R·R | yes | (rs1 ∧ 0xFFFF) << 8·(rs2 ∧ 6) | — | — | never | `trace_owner.cpp:485–489` |
| 157 | `VirtualShiftDataW` | S 3 | R·R·R | yes | (rs1 ∧ 0xFFFF_FFFF) << 8·(rs2 ∧ 4), i.e. by 0 or 32 | — | — | never | `trace_owner.cpp:490–494` |
| 151 | `VirtualPext` | S 3 | R·R·R | yes | pext(rs1, rs2): the bits of rs1 at the 1-bits of rs2, packed into rd from bit 0 up, in ascending bit order; rs2 = 0 gives 0. With the contiguous masks it receives (always a VirtualWindowMask row): (rs1 ∧ rs2) >> tz(rs2) | — | — | always | `trace_owner.cpp:500–504` |
| 142 | `VirtualPextSigned` | S 3 | R·R·R; x0·R·R | yes (dropped when rd = x0, as in LR.W) | as VirtualPext, giving n = popcount(rs2) bits; then if the bit of rs1 at the highest set bit of rs2 is 1 and n < 64, bits [63:n] are set (sign extension from the window's top bit); rs2 = 0 gives 0 | — | — | sometimes | `trace_owner.cpp:505–509` |

**The masks VirtualPext receives.** `expand_load` (`isa/virtual.cpp:237–250`) always feeds VirtualPext and VirtualPextSigned the rd of the window-mask row just before them: `0xFF << 8k` (LB, LBU), `0xFFFF << 16k` (LH, LHU), or `0xFFFF_FFFF << 32k` (LW, LWU, LR.W, SC.W). Every mask is contiguous and non-zero, so the general pext reduces to (rs1 ∧ m) >> tz(m), and the signed form sign-extends from bit 8, 16 or 32 of the result (spec item 43). The general definition above is what the tracer computes for any mask.

### F. Control flow and pc-relative

| code | name | class | slots rd·rs1·rs2 | writes rd | rd = | control (new pc) | log | last row | source |
|---|---|---|---|---|---|---|---|---|---|
| 28 | `BEQ` | S 3 | I·R·R | no (rd slot is the immediate) | — | if rs1 = rs2: new pc = (pc + imm) mod 2⁶⁴, imm from the **rd** slot; else new pc = pc. Then the common rule (§1.4): a target equal to pc (imm = 0) is sequential; a target of 0 ends the program | — | always | `trace_owner.cpp:247–251` |
| 29 | `BNE` | S 3 | I·R·R | no (rd slot is the immediate) | — | if rs1 ≠ rs2: new pc = (pc + imm) mod 2⁶⁴, imm from the **rd** slot; else new pc = pc. Then the common rule (§1.4): a target equal to pc (imm = 0) is sequential; a target of 0 ends the program | — | always | `trace_owner.cpp:252–256` |
| 30 | `BLT` | S 3 | I·R·R | no (rd slot is the immediate) | — | if s64(rs1) < s64(rs2): new pc = (pc + imm) mod 2⁶⁴, imm from the **rd** slot; else new pc = pc. Then the common rule (§1.4): a target equal to pc (imm = 0) is sequential; a target of 0 ends the program | — | always | `trace_owner.cpp:267–271` |
| 31 | `BGE` | S 3 | I·R·R | no (rd slot is the immediate) | — | if s64(rs1) ≥ s64(rs2): new pc = (pc + imm) mod 2⁶⁴, imm from the **rd** slot; else new pc = pc. Then the common rule (§1.4): a target equal to pc (imm = 0) is sequential; a target of 0 ends the program | — | always | `trace_owner.cpp:262–266` |
| 32 | `BLTU` | S 3 | I·R·R | no (rd slot is the immediate) | — | if rs1 <ᵤ rs2: new pc = (pc + imm) mod 2⁶⁴, imm from the **rd** slot; else new pc = pc. Then the common rule (§1.4): a target equal to pc (imm = 0) is sequential; a target of 0 ends the program | — | always | `trace_owner.cpp:272–276` |
| 33 | `BGEU` | S 3 | I·R·R | no (rd slot is the immediate) | — | if rs1 ≥ᵤ rs2: new pc = (pc + imm) mod 2⁶⁴, imm from the **rd** slot; else new pc = pc. Then the common rule (§1.4): a target equal to pc (imm = 0) is sequential; a target of 0 ends the program | — | always | `trace_owner.cpp:257–261` |
| 34 | `JAL` | S 3 | R·I·I | yes | (pc + pc_increment) mod 2⁶⁴, the link; pc_increment is 2 or 4 (a JAL row is always its expansion's last) | new pc = (pc + imm) mod 2⁶⁴, imm from the **rs1** slot; **if imm = 0, new pc = 0 (program end), unconditionally in the tracer** (F1) | — | always | `trace_owner.cpp:277–283` |
| 35 | `JALR` | S 3 | R·R·I | yes | (pc + pc_increment) mod 2⁶⁴, computed before rd is written, so rd = rs1 reads the old rs1 | new pc = ((rs1 + imm) mod 2⁶⁴) ∧ ¬1, imm from the **rs2** slot; a target equal to pc is sequential, a target of 0 ends the program | — | always | `trace_owner.cpp:284–290` |
| 36 | `AUIPC` | S 3 | R·I·I | yes | (pc + imm) mod 2⁶⁴, imm from the **rs1** slot and pc the **source instruction's** pc (`risc_pc`, the same for every row of an expansion, including ECALL's AUIPC row) | — | — | sometimes | `trace_owner.cpp:156–160` |
| 37 | `LUI` | S 3 | R·I·I | yes | imm, from the **rs1** slot: the already shifted and sign-extended U-immediate (e.g. 0xFFFF_FFFF_8000_0000) | — | — | sometimes | `trace_owner.cpp:151–155` |

### G. Memory, the log and advice

| code | name | class | slots rd·rs1·rs2 | writes rd | rd = | control (new pc) | log | last row | source |
|---|---|---|---|---|---|---|---|---|---|
| 54 | `LD` | L 3 | R·R·I; R·x0·I; x0·R·I | yes (dropped when rd = x0, only in LR.D with rd = x0) | the log word (the 64-bit memory value at rs1 + imm; the tracer with RAM reads the aligned 8-byte word there) | — | 1 read-log word | sometimes | `trace_owner.cpp:311–321` |
| 53 | `SD` | L 3 | I·R·R; I·x0·R | no (rd slot is the immediate) | — | — | 1 write-log word: the memory pre-value | sometimes | `trace_owner.cpp:301–310` |
| 122 | `VirtualAdvice` | L 3 | R·I·I | yes | the log word; rs1 and rs2 slots are immediates 0 | — | 1 read-log word | never | `trace_owner.cpp:536–544` |
| 160 | `VirtualAdviceDivU` | L 3 | R·R·R | yes (as VirtualAdvice) | tracer: 2⁶⁴ − 1 if rs2 = 0, else ⌊rs1 / rs2⌋ unsigned. Replayer: the log word (written as `VirtualAdvice rd, 0, 0`) | — | 1 read-log word | never | `trace_owner.cpp:545–553` |
| 162 | `VirtualAdviceDiv` | L 3 | R·R·R | yes (as VirtualAdvice) | tracer: 2⁶⁴ − 1 if rs2 = 0; INT64_MIN if rs1 = INT64_MIN and rs2 = −1; else s64(rs1) / s64(rs2) truncated toward 0. Replayer: the log word | — | 1 read-log word | never | `trace_owner.cpp:563–573` |
| 163 | `VirtualAdviceRem` | L 3 | R·R·R | yes (as VirtualAdvice) | tracer: 0 if rs2 = 0; 2⁶³ if rs1 = INT64_MIN and rs2 = −1; else \|trunc(s64(rs1) / s64(rs2))\|, the **magnitude of the quotient**, not the remainder (F7). Replayer: the log word | — | 1 read-log word | never | `trace_owner.cpp:574–584` |
| 164 | `VirtualAdviceDivUW` | L 3 | R·R·R | yes (as VirtualAdvice) | tracer: 0x0000_0000_FFFF_FFFF if rs2[31:0] = 0, else zx32(rs1[31:0] / rs2[31:0]) unsigned. Replayer: the log word | — | 1 read-log word | never | `trace_owner.cpp:611–619` |
| 166 | `VirtualAdviceDivW` | L 3 | R·R·R | yes (as VirtualAdvice) | tracer, on the low words: 2⁶⁴ − 1 if rs2[31:0] = 0; 0x0000_0000_8000_0000 (**zero-extended**) if INT32_MIN / −1; else sx32(trunc(s32 / s32)). Replayer: the log word | — | 1 read-log word | never | `trace_owner.cpp:585–597` |
| 167 | `VirtualAdviceRemW` | L 3 | R·R·R | yes (as VirtualAdvice) | tracer, on the low words: 0 if rs2[31:0] = 0; 0x8000_0000 zero-extended if INT32_MIN / −1; else zx32(\|trunc(s32 / s32)\|), the quotient's magnitude. Replayer: the log word | — | 1 read-log word | never | `trace_owner.cpp:598–610` |

**The advice opcodes.** The tracer computes the six DIV/REM advice values from registers (rows `isa/virtual.cpp:322–480`), but the replayer never does: the template builder writes those rows as `VirtualAdvice rd, 0, 0` with `from_log` set (spec 4.19.7, item 56), and the value comes from the read log. Their formulas are given so the tracer's log content is defined; the replayer's datapath needs none of them.

### H. Assertions (never evaluated by the replayer)

| code | name | class | slots rd·rs1·rs2 | writes rd | rd = | control (new pc) | log | last row | source |
|---|---|---|---|---|---|---|---|---|---|
| 108 | `VirtualAssertEQ` | S 3 | I·R·R | no | —; checks rs1 = rs2 | — | — | sometimes | `trace_owner.cpp:629–633` |
| 123 | `VirtualAssertLTE` | S 3 | I·R·R | no | —; checks rs1 ≤ᵤ rs2 (unsigned) | — | — | never | `trace_owner.cpp:634–638` |
| 124 | `VirtualAssertValidDiv0` | S 3 | I·R·R | no | —; rs1 is the divisor, rs2 the quotient: checks rs1 ≠ 0 or rs2 = 2⁶⁴ − 1 | — | — | never | `trace_owner.cpp:639–643` |
| 125 | `VirtualAssertValidUnsignedRemainder` | S 3 | I·R·R | no | —; rs1 the remainder, rs2 the divisor: checks rs2 = 0 or rs1 <ᵤ rs2 | — | — | never | `trace_owner.cpp:644–648` |
| 130 | `VirtualAssertMulUNoOverflow` | S 3 | I·R·R | no | —; checks rs1 · rs2 < 2⁶⁴ (unsigned, `__builtin_mul_overflow`) | — | — | never | `trace_owner.cpp:334–341` |
| 112 | `VirtualAssertHalfwordAlignment` | S 3 | I·R·I | no | —; checks (rs1 + imm) mod 2 = 0, imm in the rs2 slot | — | — | never | `trace_owner.cpp:342–346` |
| 113 | `VirtualAssertWordAlignment` | S 3 | I·R·I | no | —; checks (rs1 + imm) mod 4 = 0, imm in the rs2 slot | — | — | never | `trace_owner.cpp:347–351` |

The replayer doesn't evaluate assertions (spec 4.19.7): the tracer aborts on a failing one (`release_assert`), so a traced program never contains one that fails. Each writes nothing and moves pc only as a sequential row. The checks are given for completeness and for a simulator that wants to re-check a trace.

### I. Miscellaneous

| code | name | class | slots rd·rs1·rs2 | writes rd | rd = | control (new pc) | log | last row | source |
|---|---|---|---|---|---|---|---|---|---|
| 109 | `VirtualHostIO` | S 3 | R·R·I | yes, its own value: rd ← rd (no change) | rd's current value (`save_rd(regs[rd])`); rs1 and the rs2 immediate are not used by the row. Its host-side effects happen outside the trace (JIT, `transcode.cpp`) | — | — | always | `trace_owner.cpp:294–300` |
| 52 | `FENCE` | S 3 | I·I·I | no (all three slots are immediates 0) | —: does nothing | — | — | always | `trace_owner.cpp:654–656` |

## 3. LD and SD: the memory fields

LD and SD are the normalized ISA's only memory opcodes; every load and store, sub-word, LR/SC and AMO included, is built from them (`trace_owner.hpp:219–235`; `trace_owner.cpp:917–925`).

| opcode | slots | address | pre-value | post-value | rd | log |
|---|---|---|---|---|---|---|
| LD 54 | rd R, rs1 R, **imm in rs2** | (rs1 + imm) mod 2⁶⁴ | rd's value **after** the row | the same | the log word | 1 read-log word |
| SD 53 | **imm in rd**, rs1 R, rs2 R | (rs1 + imm) mod 2⁶⁴ | the log word (the value memory held) | rs2's value | none: rd_reg, rd, rd_prev all 0 | 1 write-log word |

- **Alignment:** the address is always a multiple of 8 in a correct trace. The tracer asserts it only when it reads RAM (`trace_owner.cpp:304`, `314`); with the log it doesn't check. The replayer doesn't either.
- **LD with rd = x0** (only in LR.D's expansion with rd = x0): the log word is consumed but dropped, so rd stays 0 and **the record's pre- and post-values are 0**, not the memory value (F4).
- **SD's pre-value** is the log word in the record (`mem_log[mem_log_idx − 1]`); the derived helper in `trace_owner.hpp` reads it from "the rd lane", which carries the same value in the GPU layout.

## 4. Flags: where the tracer, the spec and a reimplementation can disagree

- **F1.** **JAL with imm = 0 always ends the program.** `execute_instruction` sets new_pc = 0 whenever a JAL's immediate is 0 (`trace_owner.cpp:280–281`), with no flag. The spec (4.19.7's pc table, 4.19.3's `imm0_pc0`) makes it conditional on a template bit. They agree only if **every JAL template word sets `imm0_pc0`** (JAL's own template and EBREAK's, F2). Suggested spec text: "JAL: new pc = pc + imm, or 0 if imm = 0. Every JAL row behaves this way; `imm0_pc0` is set in every JAL word", or drop the flag.
- **F2.** **EBREAK is one row, `JAL v40, 0`** (`isa/virtual.cpp:1164–1192`), so in the bytecode it ends the program (F1) and links pc + `pc_increment` into v40. At run time the JIT still aborts on EBREAK (`transcode.cpp`), and no current guest reaches one. The spec lists EBREAK as controller-fed; its template is this single JAL.
- **F3.** **A new pc of 0 ends the program for every opcode,** not only JAL: `finish_instruction` tests new_pc = 0 before anything else (`trace_owner.cpp:117–119`), so a JALR whose target is 0, or a branch whose pc + imm wraps to 0, also sets pc = 0 and `instr_idx` = 0. 4.19.7 says "a new pc of 0 has the log word 0", which covers it; the pc table could say so for JALR and the branches too.
- **F4.** **LD with rd = x0 reports zero memory values.** The record's pre- and post-values are taken from rd after the row (`trace_owner.cpp:922–925`), so an LD whose rd is x0 (only LR.D with rd = x0, an exempt instruction, section 5) consumes its read-log word but reports pre = post = 0 and rd = 0. The replayer must form them the same way: from the value written, not the log word. 4.19.7 and 4.19.12 don't say this.
- **F5.** **"The W forms sign-extend their low 32 bits" (4.19.7) is too broad.** Sign-extending: ADDW, ADDIW, SUBW, MULW, `VirtualMULIW`, `VirtualSRLW`, `VirtualSRLIW` (a *logical* word shift whose result is then sign-extended), `VirtualSRAW`, `VirtualSRAIW`, `VirtualSignExtendWord`. **Zero-extending:** `VirtualPow2W`, `VirtualShiftRightBitmaskW`, `VirtualROTRIW`, all seven `VirtualXORROTW…`, `VirtualZeroExtendWord`, and among the advice values `VirtualAdviceDivUW`, `VirtualAdviceRemW` and `VirtualAdviceDivW`'s INT32_MIN / −1 case (0x0000_0000_8000_0000).
- **F6.** **tz(0) = 64 in the 64-bit right shifts is undefined in the tracer.** `VirtualSRL`, `VirtualSRLI`, `VirtualSRA` and `VirtualSRAI` shift by tz(rs2), and a shift by 64 is undefined behaviour in C++ (the harness, on x86, returns rs1 unchanged). It can't arise: every mask comes from `VirtualShiftRightBitmask` (never 0) or from the expansion (2⁶⁴ − 2^shamt, never 0). The word shifts mask tz to 5 bits, so they are defined for 0 (shift 0), and `VirtualROTRI`'s `std::rotr` by 64 is the identity. Suggest: the spec states the masks are never 0 and leaves the result for 0 undefined, and meshsim flags a zero mask.
- **F7.** **`VirtualAdviceRem` and `VirtualAdviceRemW` compute the quotient's magnitude, not the remainder** (`trace_owner.cpp:574–584`, `598–610`), and the dead `VirtualAdviceRemU` case computes the quotient. That is what Jolt's REM expansions consume (the remainder is rebuilt from it), so the names mislead but the log content is right. The replayer is unaffected: it takes the log word.
- **F8.** **Immediates live in different slots by opcode** (section 1.2): rs1 for JAL, LUI and AUIPC; rd for branches, SD and five assertions; rs2 for the rest. The kind of each slot is fixed per opcode across every emission site (checked for all 90 emitted opcodes), so no opcode's operand can be either a register or an immediate. Only *which* register varies, including x0 in nine forms (section 6).
- **F9.** **No class disagreements.** Every opcode's class in 4.19.7 matches what its case computes: the five M opcodes are the only ones that need a 64 × 64 product; `VirtualAssertMulUNoOverflow` multiplies only to check and is never evaluated, so S is right; the L opcodes are exactly those that pop a log word (LD, SD, `VirtualAdvice`, and the six advice opcodes written as `VirtualAdvice`). MULH and MULHSU never reach a row (`isa/virtual.cpp:485–503`), as 4.19.7 says.
- **F10.** **NOP is never executed.** It has no `execute_instruction` case (it would reach `unreachable()`); padding rows are the zero-filled tail of the trace once pc reaches 0 (`trace_owner.cpp:881–883`; `trace_owner.hpp:161–163`). The rd = x0 rewrite produces `ADDI x0, x0, 0`, not opcode 0 (`isa/virtual.cpp:307–311`). The replayer's padding row (4.19.6) is therefore pure data.
- **F11.** **`VirtualHostIO` writes rd's own value back** (`trace_owner.cpp:294–299`), so the register file doesn't change; rd_prev = rd in the record. Its source rd = x0 is remapped to v40 first (section 5), so the row never names x0.
- **F12.** **AUIPC uses the source instruction's pc on every row,** including ECALL's AUIPC row, which is not its expansion's last (`trace_owner.cpp:156–160`; ECALL `isa/virtual.cpp:1098–1115`).

## 5. The rd = x0 cross-check against spec 4.19.4

**What the source does** (`isa/virtual.cpp:19–120`, `300–320`):

1. `has_rd0_register` (`:104–106`): the source's rd field is a register (not an immediate) and is 0.
2. If the opcode is in neither `has_side_effects_beyond_rd` (`:19–55`) nor `rd0_is_handled_internally` (`:57–102`), the instruction becomes **one row, `ADDI x0, x0, 0`** (`:307–311`), virtual unless the source is ADDI itself (`:183`).
3. If it has side effects and isn't handled internally, **rd is remapped** before any per-opcode expansion (`:315–319`): `allocate_virtual_register()` returns the first free register, which is always **40** (allocation starts at 32 + 8 = 40, `:140`), and the template's own temporaries then start at 41.
4. Opcodes in `rd0_is_handled_internally` (ECALL, MRET, EBREAK, CSRRW, CSRRS, LR/SC and the inlines) are expanded unchanged.

**JAL, JALR and `VirtualHostIO` with rd = x0.** All three are in `has_side_effects_beyond_rd` and not handled internally, so step 3 applies: the source's rd becomes v40, and the rows are `JAL v40, imm`, `JALR v40, rs1, imm` and `VirtualHostIO v40, rs1, imm`. Each case also carries its own `if (src.op_a == 0)` branch that would allocate a throwaway register (`:1195–1226`), but by the time it runs, op_a is already 40, so **those branches are dead code**. Either path would give v40, so the result is the same. The harness confirms it: with rd = x0 these sources produce rows whose rd is a non-zero register.

So the link is **written, not dropped**: v40 receives pc + `pc_increment`, and the record shows rd_reg = 40, rd_prev = v40's old value and rd = the link. v40 is not an inline temporary (it is below `num_stateful_regs` = 48, so `normalize_inline_vregs` never zeroes it, `trace_owner.cpp:128–140`), so the value stays in v40 until the next instruction that writes it, and a later rd_prev of v40 shows it.

**What 4.19.4 says.** It lists JAL, JALR, LD and `VirtualHostIO` among the remap instructions with one-word templates ("rd becomes 40; e.g. `ret` becomes `JALR v40, ra, 0`"), and notes that their own throwaway-register paths never arise after the generic remap and give rd = 40 anyway. **That matches the source exactly.** EBREAK is handled internally, but its one row is `JAL v40, 0` all the same (F2), because the case allocates v40 itself.

**What 4.19.4 should add** (none of it changes the mechanism):

- *"The remapped rd is written like any register: a JAL or JALR with rd = x0 leaves its link in v40, which is not an inline temporary and keeps that value across instructions; the record shows rd = 40 and the link."* Today the text could be read as the write being thrown away.
- *"Exempt instructions may produce rows that name x0 as rd: `LD x0` (LR.D), `VirtualPextSigned x0` (LR.W) and `XORI x0` (SC.W, SC.D). Register 0 drops the write (4.19.7), and an LD to x0 reports zero memory values (F4)."*
- *"EBREAK's one row is `JAL v40, 0`, which ends the program (F1)."*
- The line references in 4.19.4 still hold at 4f36b3d (`:19–55`, `:57–102`, `:104–106`, `:140`, `:148–153`, `:183`, `:307–319`, `:1195–1226`).

## 6. Slot forms and their producers

For each row opcode: every slot form (rd·rs1·rs2, R register, I immediate, x0 when the register is always register 0), the source instructions whose expansion emits it (from running `expand_virt_seq` on every source opcode, with rd ≠ x0 and rd = x0, and for the immediate shifts the amounts 0, 3, 31, 32 and 63), the static emit sites, and, for opcodes that are sometimes their expansion's last row, the sources where they are. Inline sites are counted per file. This is the table item 34's enumeration can reuse.

| code | name | form | produced by | emit sites | last row in |
|---|---|---|---|---|---|
| 1 | `ADD` | R·R·R | ADD, AMOADD_D, AMOADD_W, AMOMAXU_D, AMOMAXU_W, AMOMAX_D, AMOMAX_W, AMOMINU_D, AMOMINU_W, AMOMIN_D, AMOMIN_W, BIGINT256_MUL_INLINE, BLAKE2_INLINE, BLAKE3_INLINE, BLAKE3_KEYED64_INLINE, MULH, MULHSU, SB, SC_D, SC_W, SECP256K1_DIVQ_INLINE, SECP256K1_DIVR_INLINE, SECP256K1_MULQ_INLINE, SECP256K1_MULR_INLINE, SECP256K1_SQUAREQ_INLINE, SECP256K1_SQUARER_INLINE, SH, SHA256_INIT_INLINE, SHA256_INLINE, SW | `virtual.cpp:232,233,270,500,505,507,700,735,790,803,886,907,921,942,956,976,990,1011`; `bigint_multiply.inc` ×53; `blake2b.inc` ×576; `blake3.inc` ×336; `blake3_keyed64.inc` ×336; `secp256k1_divq.inc` ×70; `secp256k1_divr.inc` ×94; `secp256k1_mulq.inc` ×70; `secp256k1_mulr.inc` ×94; `secp256k1_squareq.inc` ×70; `secp256k1_squarer.inc` ×94; `sha256.inc` ×536; `sha256_init.inc` ×516; as itself: `virtual.cpp` default (`:1227–1229`) | ADD, MULH, MULHSU |
| 2 | `ADDI` | R·R·I | ADDI, AMOADD_D, AMOAND_D, AMOMAXU_D, AMOMAX_D, AMOMINU_D, AMOMIN_D, AMOOR_D, AMOSWAP_D, AMOXOR_D, CSRRS, CSRRW, DIV, DIVU, DIVUW, ECALL, LR_D, LR_W, REMU, SB, SC_W, SH, SHA256_INIT_INLINE, SHA256_INLINE, SW | `virtual.cpp:208,264,308,342,377,391,412,660,661,676,677,706,707,725,738,746,747,755,792,815,838,861,888,923,958,992,1103,1105,1106,1108,1123,1126,1127,1128,1131,1132,1143,1148,1149,1153`; `bigint_multiply.inc` ×11; `blake2b.inc` ×40; `blake3.inc` ×32; `blake3_keyed64.inc` ×32; `keccak256.inc` ×37; `keccak256_absorb_permute.inc` ×37; `secp256k1_divq.inc` ×16; `secp256k1_divr.inc` ×17; `secp256k1_glvr_adv.inc` ×1; `secp256k1_mulq.inc` ×16; `secp256k1_mulr.inc` ×17; `secp256k1_squareq.inc` ×13; `secp256k1_squarer.inc` ×14; `sha256.inc` ×100; `sha256_init.inc` ×105; as itself: `virtual.cpp` default (`:1227–1229`) | ADD, ADDI, ADDIW, ADDW, AMOADD_D, AMOAND_D, AMOMAXU_D, AMOMAX_D, AMOMINU_D, AMOMIN_D, AMOOR_D, AMOSWAP_D, AMOXOR_D, AND, ANDI, AUIPC, BIGINT256_MUL_INLINE, BLAKE2_INLINE, BLAKE3_INLINE, BLAKE3_KEYED64_INLINE, CSRRW, DIV, DIVU, DIVUW, DIVW, KECCAK256, KECCAK256_ABSORB_PERMUTE, LUI, MUL, MULH, MULHSU, MULHU, MULW, OR, ORI, REM, REMU, REMUW, REMW, SC_W, SECP256K1_DIVQ_INLINE, SECP256K1_DIVR_INLINE, SECP256K1_GLVR_ADV_INLINE, SECP256K1_MULQ_INLINE, SECP256K1_MULR_INLINE, SECP256K1_SQUAREQ_INLINE, SECP256K1_SQUARER_INLINE, SHA256_INIT_INLINE, SHA256_INLINE, SLL, SLLI, SLLIW, SLLW, SLT, SLTI, SLTIU, SLTU, SRA, SRAI, SRAIW, SRAW, SRL, SRLI, SRLIW, SRLW, SUB, SUBW, VirtualRev8W, XOR, XORI |
|  |  | R·x0·I | BIGINT256_MUL_INLINE, BLAKE2_INLINE, BLAKE3_INLINE, BLAKE3_KEYED64_INLINE, ECALL, KECCAK256, KECCAK256_ABSORB_PERMUTE, LR_W, SC_D, SC_W, SECP256K1_DIVQ_INLINE, SECP256K1_DIVR_INLINE, SECP256K1_GLVR_ADV_INLINE, SECP256K1_MULQ_INLINE, SECP256K1_MULR_INLINE, SECP256K1_SQUAREQ_INLINE, SECP256K1_SQUARER_INLINE, SHA256_INIT_INLINE, SHA256_INLINE |  |  |
|  |  | x0·x0·I | ADD, ADDI, ADDIW, ADDW, AND, ANDI, AUIPC, DIV, DIVU, DIVUW, DIVW, LUI, MUL, MULH, MULHSU, MULHU, MULW, OR, ORI, REM, REMU, REMUW, REMW, SLL, SLLI, SLLIW, SLLW, SLT, SLTI, SLTIU, SLTU, SRA, SRAI, SRAIW, SRAW, SRL, SRLI, SRLIW, SRLW, SUB, SUBW, VirtualRev8W, XOR, XORI |  |  |
| 3 | `SUB` | R·R·R | AMOMAXU_D, AMOMAXU_W, AMOMAX_D, AMOMAX_W, AMOMINU_D, AMOMINU_W, AMOMIN_D, AMOMIN_W, DIV, DIVU, DIVUW, DIVW, REM, REMU, REMUW, REMW, SC_D, SC_W, SUB | `virtual.cpp:340,359,375,389,408,428,456,479,698,733,884,905,919,940,954,974,988,1009`; `blake2b.inc` ×1; as itself: `virtual.cpp` default (`:1227–1229`) | SUB |
|  |  | R·x0·R | BLAKE2_INLINE |  |  |
| 4 | `XOR` | R·R·R | AMOADD_W, AMOAND_W, AMOMAXU_W, AMOMAX_W, AMOMINU_W, AMOMIN_W, AMOOR_W, AMOSWAP_W, AMOXOR_D, AMOXOR_W, BLAKE2_INLINE, BLAKE3_INLINE, BLAKE3_KEYED64_INLINE, DIV, DIVW, KECCAK256, KECCAK256_ABSORB_PERMUTE, MULHSU, SC_D, SC_W, SHA256_INIT_INLINE, SHA256_INLINE, XOR | `virtual.cpp:294,296,335,451,499,503,504,691,721,777,779,836,849`; `blake2b.inc` ×16; `blake3.inc` ×8; `blake3_keyed64.inc` ×8; `keccak256.inc` ×1680; `keccak256_absorb_permute.inc` ×1697; `sha256.inc` ×288; `sha256_init.inc` ×282; as itself: `virtual.cpp` default (`:1227–1229`) | XOR |
| 5 | `XORI` | R·R·I | BLAKE2_INLINE, KECCAK256, KECCAK256_ABSORB_PERMUTE, SC_D, SC_W, SHA256_INIT_INLINE, XORI | `virtual.cpp:708,745`; `blake2b.inc` ×10; `keccak256.inc` ×24; `keccak256_absorb_permute.inc` ×24; `sha256_init.inc` ×4; as itself: `virtual.cpp` default (`:1227–1229`) | SC_D, XORI |
|  |  | x0·R·I | SC_D, SC_W |  |  |
| 6 | `OR` | R·R·R | AMOOR_D, AMOOR_W, BLAKE3_INLINE, BLAKE3_KEYED64_INLINE, CSRRS, OR, SHA256_INIT_INLINE, SHA256_INLINE | `virtual.cpp:859,872,1145,1150,1154`; `blake3.inc` ×4; `blake3_keyed64.inc` ×4; `sha256.inc` ×4; `sha256_init.inc` ×4; as itself: `virtual.cpp` default (`:1227–1229`) | CSRRS, OR |
| 7 | `ORI` | R·R·I | ORI | `virtual.cpp:208,288,771`; as itself: `virtual.cpp` default (`:1227–1229`) | ORI |
|  |  | R·x0·I | AMOADD_W, AMOAND_W, AMOMAXU_W, AMOMAX_W, AMOMINU_W, AMOMIN_W, AMOOR_W, AMOSWAP_W, AMOXOR_W |  |  |
| 8 | `AND` | R·R·R | AMOADD_W, AMOAND_D, AMOAND_W, AMOMAXU_W, AMOMAX_W, AMOMINU_W, AMOMIN_W, AMOOR_W, AMOSWAP_W, AMOXOR_W, AND, SHA256_INIT_INLINE, SHA256_INLINE | `virtual.cpp:295,778,813,826`; `sha256.inc` ×192; `sha256_init.inc` ×185; as itself: `virtual.cpp` default (`:1227–1229`) | AND |
| 9 | `ANDI` | R·R·I | AMOADD_W, AMOAND_W, AMOMAXU_W, AMOMAX_W, AMOMINU_W, AMOMIN_W, AMOOR_W, AMOSWAP_W, AMOXOR_W, ANDI, MULHSU, SB, SC_W, SH, SHA256_INIT_INLINE, SW | `virtual.cpp:265,278,297,498,766,780`; `sha256_init.inc` ×5; as itself: `virtual.cpp` default (`:1227–1229`) | ANDI |
| 16 | `SLT` | R·R·R | AMOMAX_D, AMOMAX_W, AMOMIN_D, AMOMIN_W, SLT | `virtual.cpp:883,904,918,939`; as itself: `virtual.cpp` default (`:1227–1229`) | SLT |
| 17 | `SLTI` | R·R·I | SLTI | as itself: `virtual.cpp` default (`:1227–1229`) | always |
| 18 | `SLTU` | R·R·R | AMOMAXU_D, AMOMAXU_W, AMOMINU_D, AMOMINU_W, BIGINT256_MUL_INLINE, MULHSU, SECP256K1_DIVQ_INLINE, SECP256K1_DIVR_INLINE, SECP256K1_MULQ_INLINE, SECP256K1_MULR_INLINE, SECP256K1_SQUAREQ_INLINE, SECP256K1_SQUARER_INLINE, SLTU | `virtual.cpp:506,953,973,987,1008`; `bigint_multiply.inc` ×29; `secp256k1_divq.inc` ×38; `secp256k1_divr.inc` ×50; `secp256k1_mulq.inc` ×38; `secp256k1_mulr.inc` ×50; `secp256k1_squareq.inc` ×38; `secp256k1_squarer.inc` ×50; as itself: `virtual.cpp` default (`:1227–1229`) | SLTU |
| 19 | `SLTIU` | R·R·I | SC_D, SC_W, SLTIU | `virtual.cpp:692,722`; as itself: `virtual.cpp` default (`:1227–1229`) | SLTIU |
| 28 | `BEQ` | I·R·R | BEQ | as itself: `virtual.cpp` default (`:1227–1229`) | always |
| 29 | `BNE` | I·R·R | BNE | as itself: `virtual.cpp` default (`:1227–1229`) | always |
| 30 | `BLT` | I·R·R | BLT | as itself: `virtual.cpp` default (`:1227–1229`) | always |
| 31 | `BGE` | I·R·R | BGE | as itself: `virtual.cpp` default (`:1227–1229`) | always |
| 32 | `BLTU` | I·R·R | BLTU | as itself: `virtual.cpp` default (`:1227–1229`) | always |
| 33 | `BGEU` | I·R·R | BGEU | as itself: `virtual.cpp` default (`:1227–1229`) | always |
| 34 | `JAL` | R·I·I | EBREAK, JAL | `virtual.cpp:1174,1190,1198,1201` | always |
| 35 | `JALR` | R·R·I | ECALL, JALR, MRET | `virtual.cpp:1112,1160,1208,1211` | always |
| 36 | `AUIPC` | R·I·I | AUIPC, ECALL | `virtual.cpp:1102`; as itself: `virtual.cpp` default (`:1227–1229`) | AUIPC |
| 37 | `LUI` | R·I·I | BLAKE2_INLINE, BLAKE3_INLINE, BLAKE3_KEYED64_INLINE, LR_D, LR_W, LUI, SC_D, SC_W, SECP256K1_DIVQ_INLINE, SECP256K1_DIVR_INLINE, SECP256K1_MULQ_INLINE, SECP256K1_MULR_INLINE, SECP256K1_SQUAREQ_INLINE, SECP256K1_SQUARER_INLINE | `virtual.cpp:657,673,689,719`; `blake2b.inc` ×6; `blake3.inc` ×4; `blake3_keyed64.inc` ×6; `secp256k1_divq.inc` ×1; `secp256k1_divr.inc` ×2; `secp256k1_mulq.inc` ×1; `secp256k1_mulr.inc` ×2; `secp256k1_squareq.inc` ×1; `secp256k1_squarer.inc` ×2; as itself: `virtual.cpp` default (`:1227–1229`) | LUI |
| 43 | `MUL` | R·R·R | AMOADD_W, AMOAND_W, AMOMAXU_D, AMOMAXU_W, AMOMAX_D, AMOMAX_W, AMOMINU_D, AMOMINU_W, AMOMIN_D, AMOMIN_W, AMOOR_W, AMOSWAP_W, AMOXOR_W, BIGINT256_MUL_INLINE, DIV, DIVU, DIVUW, DIVW, MUL, MULH, MULHSU, REM, REMU, REMUW, REMW, SC_D, SC_W, SECP256K1_DIVQ_INLINE, SECP256K1_DIVR_INLINE, SECP256K1_MULQ_INLINE, SECP256K1_MULR_INLINE, SECP256K1_SQUAREQ_INLINE, SECP256K1_SQUARER_INLINE, SLL | `virtual.cpp:229,230,291,293,338,357,373,387,406,426,454,477,502,606,699,734,774,776,885,906,920,941,955,975,989,1010`; `bigint_multiply.inc` ×16; `secp256k1_divq.inc` ×20; `secp256k1_divr.inc` ×24; `secp256k1_mulq.inc` ×20; `secp256k1_mulr.inc` ×24; `secp256k1_squareq.inc` ×14; `secp256k1_squarer.inc` ×18; as itself: `virtual.cpp` default (`:1227–1229`) | MUL, SLL |
| 45 | `MULHU` | R·R·R | BIGINT256_MUL_INLINE, MULH, MULHSU, MULHU, SECP256K1_DIVQ_INLINE, SECP256K1_DIVR_INLINE, SECP256K1_MULQ_INLINE, SECP256K1_MULR_INLINE, SECP256K1_SQUAREQ_INLINE, SECP256K1_SQUARER_INLINE | `virtual.cpp:231,501`; `bigint_multiply.inc` ×16; `secp256k1_divq.inc` ×20; `secp256k1_divr.inc` ×24; `secp256k1_mulq.inc` ×20; `secp256k1_mulr.inc` ×24; `secp256k1_squareq.inc` ×14; `secp256k1_squarer.inc` ×18; as itself: `virtual.cpp` default (`:1227–1229`) | MULHU |
| 52 | `FENCE` | I·I·I | FENCE | `virtual.cpp:1096` | always |
| 53 | `SD` | I·R·R | AMOADD_D, AMOADD_W, AMOAND_D, AMOAND_W, AMOMAXU_D, AMOMAXU_W, AMOMAX_D, AMOMAX_W, AMOMINU_D, AMOMINU_W, AMOMIN_D, AMOMIN_W, AMOOR_D, AMOOR_W, AMOSWAP_D, AMOSWAP_W, AMOXOR_D, AMOXOR_W, BIGINT256_MUL_INLINE, BLAKE2_INLINE, BLAKE3_INLINE, BLAKE3_KEYED64_INLINE, KECCAK256, KECCAK256_ABSORB_PERMUTE, SB, SC_D, SC_W, SD, SECP256K1_DIVQ_INLINE, SECP256K1_DIVR_INLINE, SECP256K1_GLVR_ADV_INLINE, SECP256K1_MULQ_INLINE, SECP256K1_MULR_INLINE, SECP256K1_SQUAREQ_INLINE, SECP256K1_SQUARER_INLINE, SH, SHA256_INIT_INLINE, SHA256_INLINE, SW | `virtual.cpp:271,298,703,754,781,791,814,837,860,887,922,957,991`; `bigint_multiply.inc` ×8; `blake2b.inc` ×8; `blake3.inc` ×4; `blake3_keyed64.inc` ×4; `keccak256.inc` ×25; `keccak256_absorb_permute.inc` ×25; `secp256k1_divq.inc` ×4; `secp256k1_divr.inc` ×4; `secp256k1_glvr_adv.inc` ×6; `secp256k1_mulq.inc` ×4; `secp256k1_mulr.inc` ×4; `secp256k1_squareq.inc` ×4; `secp256k1_squarer.inc` ×4; `sha256.inc` ×4; `sha256_init.inc` ×4; as itself: `virtual.cpp` default (`:1227–1229`) | SB, SD, SH, SW |
|  |  | I·x0·R | BIGINT256_MUL_INLINE, BLAKE3_KEYED64_INLINE, SECP256K1_DIVQ_INLINE, SECP256K1_DIVR_INLINE, SECP256K1_GLVR_ADV_INLINE, SECP256K1_MULQ_INLINE, SECP256K1_MULR_INLINE, SECP256K1_SQUAREQ_INLINE, SECP256K1_SQUARER_INLINE |  |  |
| 54 | `LD` | R·R·I | AMOADD_D, AMOADD_W, AMOAND_D, AMOAND_W, AMOMAXU_D, AMOMAXU_W, AMOMAX_D, AMOMAX_W, AMOMINU_D, AMOMINU_W, AMOMIN_D, AMOMIN_W, AMOOR_D, AMOOR_W, AMOSWAP_D, AMOSWAP_W, AMOXOR_D, AMOXOR_W, BIGINT256_MUL_INLINE, BLAKE2_INLINE, BLAKE3_INLINE, BLAKE3_KEYED64_INLINE, KECCAK256, KECCAK256_ABSORB_PERMUTE, LB, LBU, LD, LH, LHU, LR_D, LR_W, LW, LWU, SB, SC_D, SC_W, SECP256K1_DIVQ_INLINE, SECP256K1_DIVR_INLINE, SECP256K1_MULQ_INLINE, SECP256K1_MULR_INLINE, SECP256K1_SQUAREQ_INLINE, SECP256K1_SQUARER_INLINE, SH, SHA256_INIT_INLINE, SHA256_INLINE, SW | `virtual.cpp:245,266,279,662,695,753,767,789,812,835,858,882,917,952,986`; `bigint_multiply.inc` ×8; `blake2b.inc` ×26; `blake3.inc` ×14; `blake3_keyed64.inc` ×12; `keccak256.inc` ×25; `keccak256_absorb_permute.inc` ×42; `secp256k1_divq.inc` ×8; `secp256k1_divr.inc` ×8; `secp256k1_mulq.inc` ×8; `secp256k1_mulr.inc` ×8; `secp256k1_squareq.inc` ×4; `secp256k1_squarer.inc` ×4; `sha256.inc` ×12; `sha256_init.inc` ×8; as itself: `virtual.cpp` default (`:1227–1229`) | LD, LR_D |
|  |  | R·x0·I | BLAKE3_KEYED64_INLINE |  |  |
|  |  | x0·R·I | LR_D |  |  |
| 62 | `SUBW` | R·R·R | SUBW | as itself: `virtual.cpp` default (`:1227–1229`) | always |
| 63 | `ADDW` | R·R·R | ADDW | as itself: `virtual.cpp` default (`:1227–1229`) | always |
| 64 | `ADDIW` | R·R·I | ADDIW | as itself: `virtual.cpp` default (`:1227–1229`) | always |
| 65 | `MULW` | R·R·R | MULW, SLLW | `virtual.cpp:613`; as itself: `virtual.cpp` default (`:1227–1229`) | always |
| 107 | `VirtualRev8W` | R·R·I | SHA256_INIT_INLINE, SHA256_INLINE, VirtualRev8W | `sha256.inc` ×8; `sha256_init.inc` ×8; as itself: `virtual.cpp` default (`:1227–1229`) | VirtualRev8W |
| 108 | `VirtualAssertEQ` | I·R·R | SECP256K1_DIVQ_INLINE, SECP256K1_DIVR_INLINE, SECP256K1_MULQ_INLINE, SECP256K1_MULR_INLINE, SECP256K1_SQUAREQ_INLINE, SECP256K1_SQUARER_INLINE, VirtualAssertEQ | `secp256k1_divq.inc` ×8; `secp256k1_divr.inc` ×8; `secp256k1_mulq.inc` ×4; `secp256k1_mulr.inc` ×4; `secp256k1_squareq.inc` ×4; `secp256k1_squarer.inc` ×4; as itself: `virtual.cpp` default (`:1227–1229`) | VirtualAssertEQ |
| 109 | `VirtualHostIO` | R·R·I | VirtualHostIO | `virtual.cpp:1218,1222` | always |
| 110 | `VirtualSignExtendWord` | R·R·I | AMOADD_W, AMOAND_W, AMOMAXU_W, AMOMAX_W, AMOMINU_W, AMOMIN_W, AMOOR_W, AMOSWAP_W, AMOXOR_W, DIVUW, DIVW, REMUW, REMW | `virtual.cpp:299,410,430,445,446,458,471,472,782,902,903,937,938` | AMOADD_W, AMOAND_W, AMOMAXU_W, AMOMAX_W, AMOMINU_W, AMOMIN_W, AMOOR_W, AMOSWAP_W, AMOXOR_W, DIVW, REMUW |
| 111 | `VirtualZeroExtendWord` | R·R·I | AMOMAXU_W, AMOMINU_W, BLAKE3_INLINE, BLAKE3_KEYED64_INLINE, DIVUW, REMUW, SHA256_INIT_INLINE, SHA256_INLINE | `virtual.cpp:402,403,422,423,971,972,1006,1007`; `blake3.inc` ×4; `blake3_keyed64.inc` ×4; `sha256.inc` ×4; `sha256_init.inc` ×4 | never |
| 112 | `VirtualAssertHalfwordAlignment` | I·R·I | LH, LHU, SH |  | never |
| 113 | `VirtualAssertWordAlignment` | I·R·I | AMOADD_W, AMOAND_W, AMOMAXU_W, AMOMAX_W, AMOMINU_W, AMOMIN_W, AMOOR_W, AMOSWAP_W, AMOXOR_W, LR_W, LW, LWU, SC_W, SW | `virtual.cpp:277,765` | never |
| 114 | `VirtualMULI` | R·R·I | AMOADD_W, AMOAND_W, AMOMAXU_W, AMOMAX_W, AMOMINU_W, AMOMIN_W, AMOOR_W, AMOSWAP_W, AMOXOR_W, BLAKE3_INLINE, BLAKE3_KEYED64_INLINE, ECALL, SHA256_INIT_INLINE, SHA256_INLINE, SLLI | `virtual.cpp:280,599,768,1109`; `blake3.inc` ×4; `blake3_keyed64.inc` ×4; `sha256.inc` ×4; `sha256_init.inc` ×4 | SLLI |
| 115 | `VirtualMULIW` | R·R·I | SLLIW | `virtual.cpp:618` | always |
| 116 | `VirtualPow2` | R·R·I | AMOADD_W, AMOAND_W, AMOMAXU_W, AMOMAX_W, AMOMINU_W, AMOMIN_W, AMOOR_W, AMOSWAP_W, AMOXOR_W, SLL | `virtual.cpp:290,292,605,773,775` | never |
| 117 | `VirtualShiftRightBitmask` | R·R·I | AMOADD_W, AMOAND_W, AMOMAXU_W, AMOMAX_W, AMOMINU_W, AMOMIN_W, AMOOR_W, AMOSWAP_W, AMOXOR_W, SRA, SRL | `virtual.cpp:281,592,638,769` | never |
| 118 | `VirtualSRL` | R·R·R | AMOADD_W, AMOAND_W, AMOMAXU_W, AMOMAX_W, AMOMINU_W, AMOMIN_W, AMOOR_W, AMOSWAP_W, AMOXOR_W, SRL | `virtual.cpp:282,593,770` | SRL |
| 119 | `VirtualSRLI` | R·R·I | AMOADD_W, AMOAND_W, AMOMAXU_W, AMOMAX_W, AMOMINU_W, AMOMIN_W, AMOOR_W, AMOSWAP_W, AMOXOR_W, BLAKE3_INLINE, BLAKE3_KEYED64_INLINE, SHA256_INIT_INLINE, SHA256_INLINE, SRLI | `virtual.cpp:289,572,772`; `blake3.inc` ×14; `blake3_keyed64.inc` ×12; `sha256.inc` ×12; `sha256_init.inc` ×8 | SRLI |
| 120 | `VirtualSRAI` | R·R·I | SRAI | `virtual.cpp:567` | always |
| 121 | `VirtualPow2W` | R·R·I | SLLW | `virtual.cpp:612` | never |
| 122 | `VirtualAdvice` | R·I·I | SECP256K1_DIVQ_INLINE, SECP256K1_DIVR_INLINE, SECP256K1_GLVR_ADV_INLINE, SECP256K1_MULQ_INLINE, SECP256K1_MULR_INLINE, SECP256K1_SQUAREQ_INLINE, SECP256K1_SQUARER_INLINE | `virtual.cpp:217` | never |
| 123 | `VirtualAssertLTE` | I·R·R | DIV, DIVU, DIVUW, DIVW, LR_D, LR_W, REM, REMU, REMUW, REMW, SC_D, SC_W, SECP256K1_DIVQ_INLINE, SECP256K1_DIVR_INLINE, SECP256K1_MULQ_INLINE, SECP256K1_MULR_INLINE, SECP256K1_SQUAREQ_INLINE, SECP256K1_SQUARER_INLINE | `virtual.cpp:339,358,374,388,407,427,455,478,658,674,690,720`; `secp256k1_divq.inc` ×1; `secp256k1_divr.inc` ×1; `secp256k1_mulq.inc` ×1; `secp256k1_mulr.inc` ×1; `secp256k1_squareq.inc` ×1; `secp256k1_squarer.inc` ×1 | never |
| 124 | `VirtualAssertValidDiv0` | I·R·R | DIV, DIVU, DIVUW, DIVW | `virtual.cpp:332,371,411,448` | never |
| 125 | `VirtualAssertValidUnsignedRemainder` | I·R·R | DIV, DIVU, DIVUW, DIVW, REM, REMU, REMUW, REMW | `virtual.cpp:341,360,376,390,409,429,457,480` | never |
| 127 | `VirtualNegateIf` | R·R·R | DIV, DIVW, REM, REMW | `virtual.cpp:333,334,336,354,355,361,449,450,452,474,475,481` | REM, REMW |
| 128 | `VirtualMovsign` | R·R·I | MULH, MULHSU | `virtual.cpp:227,228,497` | never |
| 129 | `VirtualSRA` | R·R·R | SRA | `virtual.cpp:639` | always |
| 130 | `VirtualAssertMulUNoOverflow` | I·R·R | DIV, DIVU, DIVUW, DIVW, REM, REMU, REMUW, REMW | `virtual.cpp:337,356,372,386,405,425,453,476` | never |
| 131 | `VirtualROTRI` | R·R·I | KECCAK256, KECCAK256_ABSORB_PERMUTE | `keccak256.inc` ×576; `keccak256_absorb_permute.inc` ×576 | never |
| 132 | `VirtualROTRIW` | R·R·I | SHA256_INIT_INLINE, SHA256_INLINE | `sha256.inc` ×224; `sha256_init.inc` ×222 | never |
| 133 | `VirtualXORROT63` | R·R·R | BLAKE2_INLINE | `blake2b.inc` ×96 | never |
| 134 | `VirtualXORROT32` | R·R·R | BLAKE2_INLINE | `blake2b.inc` ×96 | never |
| 135 | `VirtualXORROT24` | R·R·R | BLAKE2_INLINE | `blake2b.inc` ×96 | never |
| 136 | `VirtualXORROT16` | R·R·R | BLAKE2_INLINE | `blake2b.inc` ×96 | never |
| 137 | `VirtualXORROTW16` | R·R·R | BLAKE3_INLINE, BLAKE3_KEYED64_INLINE | `blake3.inc` ×56; `blake3_keyed64.inc` ×56 | never |
| 138 | `VirtualXORROTW12` | R·R·R | BLAKE3_INLINE, BLAKE3_KEYED64_INLINE, SHA256_INIT_INLINE, SHA256_INLINE | `blake3.inc` ×56; `blake3_keyed64.inc` ×56; `sha256.inc` ×64; `sha256_init.inc` ×63 | never |
| 139 | `VirtualXORROTW8` | R·R·R | BLAKE3_INLINE, BLAKE3_KEYED64_INLINE | `blake3.inc` ×56; `blake3_keyed64.inc` ×56 | never |
| 140 | `VirtualXORROTW7` | R·R·R | BLAKE3_INLINE, BLAKE3_KEYED64_INLINE, SHA256_INIT_INLINE, SHA256_INLINE | `blake3.inc` ×56; `blake3_keyed64.inc` ×56; `sha256.inc` ×48; `sha256_init.inc` ×48 | never |
| 141 | `VirtualWindowMaskW` | R·R·I | LR_W, LW, LWU, SC_W, SW | `virtual.cpp:535,541,547,678,729` | never |
| 142 | `VirtualPextSigned` | R·R·R | LB, LH, LR_W, LW, SC_W | `virtual.cpp:247` | LB, LH, LR_W, LW |
|  |  | x0·R·R | LR_W |  |  |
| 143 | `VirtualXORROTW22` | R·R·R | SHA256_INIT_INLINE, SHA256_INLINE | `sha256.inc` ×64; `sha256_init.inc` ×63 | never |
| 144 | `VirtualXORROTW19` | R·R·R | SHA256_INIT_INLINE, SHA256_INLINE | `sha256.inc` ×112; `sha256_init.inc` ×111 | never |
| 145 | `VirtualXORROTW6` | R·R·R | SHA256_INIT_INLINE, SHA256_INLINE | `sha256.inc` ×64; `sha256_init.inc` ×63 | never |
| 146 | `VirtualShiftRightBitmaskW` | R·R·I | SRAW, SRLW | `virtual.cpp:584,624` | never |
| 147 | `VirtualSRLW` | R·R·R | SRLW | `virtual.cpp:625` | always |
| 148 | `VirtualSRLIW` | R·R·I | SHA256_INIT_INLINE, SHA256_INLINE, SRLIW | `virtual.cpp:632`; `sha256.inc` ×96; `sha256_init.inc` ×96 | SRLIW |
| 149 | `VirtualSRAW` | R·R·R | SRAW | `virtual.cpp:585` | always |
| 150 | `VirtualSRAIW` | R·R·I | SRAIW | `virtual.cpp:578` | always |
| 151 | `VirtualPext` | R·R·R | LBU, LHU, LWU | `virtual.cpp:247` | always |
| 152 | `VirtualWindowMaskB` | R·R·I | LB, LBU, SB | `virtual.cpp:513,518,561` | never |
| 153 | `VirtualWindowMaskH` | R·R·I | LH, LHU, SH | `virtual.cpp:523,529,554` | never |
| 154 | `VirtualAlignAddr` | R·R·I | LB, LBU, LH, LHU, LR_W, LW, LWU, SC_W | `virtual.cpp:244` | never |
| 155 | `VirtualShiftDataB` | R·R·R | SB |  | never |
| 156 | `VirtualShiftDataH` | R·R·R | SH |  | never |
| 157 | `VirtualShiftDataW` | R·R·R | SC_W, SW |  | never |
| 158 | `VirtualXORROTL1` | R·R·R | KECCAK256, KECCAK256_ABSORB_PERMUTE | `keccak256.inc` ×120; `keccak256_absorb_permute.inc` ×120 | never |
| 159 | `ANDN` | R·R·R | KECCAK256, KECCAK256_ABSORB_PERMUTE, SB, SC_W, SH, SHA256_INIT_INLINE, SHA256_INLINE, SW | `virtual.cpp:268`; `keccak256.inc` ×600; `keccak256_absorb_permute.inc` ×600; `sha256.inc` ×64; `sha256_init.inc` ×61 | never |
| 160 | `VirtualAdviceDivU` | R·R·R | DIVU, REMU | `virtual.cpp:370,385` | never |
| 162 | `VirtualAdviceDiv` | R·R·R | DIV | `virtual.cpp:331` | never |
| 163 | `VirtualAdviceRem` | R·R·R | REM | `virtual.cpp:353` | never |
| 164 | `VirtualAdviceDivUW` | R·R·R | DIVUW, REMUW | `virtual.cpp:404,424` | never |
| 166 | `VirtualAdviceDivW` | R·R·R | DIVW | `virtual.cpp:447` | never |
| 167 | `VirtualAdviceRemW` | R·R·R | REMW | `virtual.cpp:473` | never |

NOP 0 has no producer: it is the zero-filled padding row (F10).

## 7. Opcodes that occur only as source instructions (74)

Each is replaced by its expansion (a ROM template or, for the system instructions and inlines, controller-fed words) and never appears in a row. Rows: the expansion's length for a representative instance (rd ≠ x0), from the harness. Where `execute_instruction` has a case for the opcode, it is dead code for rows.

| code | name | rows | reason |
|---|---|---|---|
| 10 | SLL | 2 | 2 rows: `VirtualPow2` + MUL (`virtual.cpp:602`); dead case `trace_owner.cpp:201–206` |
| 11 | SLLI | 1 | 1 row: `VirtualMULI` by 2^(shamt mod 64) (`:596`); dead case `trace_owner.cpp:202–206` |
| 12 | SRL | 2 | 2 rows: `VirtualShiftRightBitmask` + `VirtualSRL` (`:589`); dead case `trace_owner.cpp:207–212` |
| 13 | SRLI | 1 | 1 row: `VirtualSRLI` with the mask 2⁶⁴ − 2^shamt (`:570`); dead case `trace_owner.cpp:208–212` |
| 14 | SRA | 2 | 2 rows: `VirtualShiftRightBitmask` + `VirtualSRA` (`:635`) |
| 15 | SRAI | 1 | 1 row: `VirtualSRAI` (`:565`) |
| 20 | LB | 4 | 4 rows via `expand_load`: `VirtualAlignAddr`, LD, `VirtualWindowMaskB`, `VirtualPextSigned` (`:511`) |
| 21 | LH | 5 | 5 rows: `VirtualAssertHalfwordAlignment` + `expand_load` with mask H, signed (`:521`) |
| 22 | LW | 5 | 5 rows: `VirtualAssertWordAlignment` + `expand_load` with mask W, signed (`:533`) |
| 23 | LBU | 4 | 4 rows: `expand_load` with mask B, `VirtualPext` (`:516`) |
| 24 | LHU | 5 | 5 rows: alignment assertion + `expand_load`, unsigned (`:527`) |
| 25 | SB | 8 | `expand_narrow_store`: read-modify-write of the aligned doubleword through LD and SD (`:559`) |
| 26 | SH | 9 | as SB, with the halfword alignment assertion (`:552`) |
| 27 | SW | 9 | as SB, with the word alignment assertion (`:545`) |
| 38 | ECALL | 7 | 7 controller-fed rows writing mepc/mcause/mtval/mstatus and a JALR through mtvec (`:1098`); dead case `trace_owner.cpp:291–293` |
| 39 | EBREAK | 1 | 1 row, `JAL v40, 0` (`:1164`; F2) |
| 40 | CSRRW | 2 | CSR virtual-register moves (ADDI) (`:1116`) |
| 41 | CSRRS | 2 | CSR read and OR into the CSR virtual register (`:1136`) |
| 42 | MRET | 1 | 1 row, a JALR through mepc (`:1158`) |
| 44 | MULH | 7 | 7 rows: 2 × `VirtualMovsign`, 2 × MUL, MULHU, 2 × ADD (`:485`; F9) |
| 46 | MULHSU | 11 | through MULHU, MUL and sign fix-ups (`:490`) |
| 47 | DIV | 12 | advice (`VirtualAdviceDiv`) plus checks (`:322`) |
| 48 | DIVU | 8 | advice (`VirtualAdviceDivU`) plus checks (`:365`) |
| 49 | REM | 9 | advice (`VirtualAdviceRem`) plus checks (`:346`) |
| 50 | REMU | 7 | advice from `VirtualAdviceDivU` (the quotient) plus checks (`:381`) |
| 51 | UNIMP | 0 | **removed**, not replaced: `num_cycles` = 0 and the bytecode builder skips it (`virtual.cpp:1318`, `1400–1404`); executing it aborts (`trace_owner.cpp:657–661`) |
| 55 | LWU | 5 | 5 rows: alignment assertion + `expand_load`, mask W, unsigned (`:539`) |
| 56 | SLLW | 2 | 2 rows: `VirtualPow2W` + MULW (`:609`) |
| 57 | SLLIW | 1 | 1 row: `VirtualMULIW` by 2^(shamt mod 32) (`:616`) |
| 58 | SRLW | 2 | 2 rows: `VirtualShiftRightBitmaskW` + `VirtualSRLW` (`:621`) |
| 59 | SRLIW | 1 | 1 row: `VirtualSRLIW` with mask 2³² − 2^shamt (`:629`) |
| 60 | SRAW | 2 | 2 rows: `VirtualShiftRightBitmaskW` + `VirtualSRAW` (`:581`) |
| 61 | SRAIW | 1 | 1 row: `VirtualSRAIW` (`:575`) |
| 66 | DIVW | 14 | advice (`VirtualAdviceDivW`) plus checks (`:434`) |
| 67 | DIVUW | 11 | advice (`VirtualAdviceDivUW`) plus checks (`:395`) |
| 68 | REMW | 11 | advice (`VirtualAdviceRemW`) plus checks (`:462`) |
| 69 | REMUW | 9 | advice from `VirtualAdviceDivUW` plus checks (`:416`) |
| 70 | LR_W | 9 | reservation in v32 and a word load (`:665`); exempt from the rd = x0 rewrite |
| 71 | LR_D | 5 | reservation in v33 and an LD (`:649`); exempt (its LD may name x0, F4) |
| 72 | SC_W | 26 | reservation check and a conditional word store (`:712`); exempt |
| 73 | SC_D | 12 | reservation check and a conditional SD (`:682`); exempt |
| 74 | AMOSWAP_W | 18 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 75 | AMOSWAP_D | 3 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 76 | AMOADD_W | 19 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 77 | AMOADD_D | 4 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 78 | AMOXOR_W | 19 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 79 | AMOXOR_D | 4 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 80 | AMOAND_W | 19 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 81 | AMOAND_D | 4 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 82 | AMOOR_W | 19 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 83 | AMOOR_D | 4 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 84 | AMOMIN_W | 24 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 85 | AMOMIN_D | 7 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 86 | AMOMAX_W | 24 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 87 | AMOMAX_D | 7 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 88 | AMOMINU_W | 24 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 89 | AMOMINU_D | 7 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 90 | AMOMAXU_W | 24 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 91 | AMOMAXU_D | 7 | load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`) |
| 92 | KECCAK256 | 3087 | an inline: controller-fed template from `inlines/` (`virtual.cpp:1018–1092`) |
| 93 | KECCAK256_ABSORB_PERMUTE | 3121 | an inline: controller-fed template from `inlines/` (`virtual.cpp:1018–1092`) |
| 94 | SHA256_INLINE | 1900 | an inline: controller-fed template from `inlines/` (`virtual.cpp:1018–1092`) |
| 95 | SHA256_INIT_INLINE | 1864 | an inline: controller-fed template from `inlines/` (`virtual.cpp:1018–1092`) |
| 96 | BLAKE2_INLINE | 1067 | an inline: controller-fed template from `inlines/` (`virtual.cpp:1018–1092`) |
| 97 | BLAKE3_INLINE | 648 | an inline: controller-fed template from `inlines/` (`virtual.cpp:1018–1092`) |
| 98 | BLAKE3_KEYED64_INLINE | 646 | an inline: controller-fed template from `inlines/` (`virtual.cpp:1018–1092`) |
| 99 | BIGINT256_MUL_INLINE | 141 | an inline: controller-fed template from `inlines/` (`virtual.cpp:1018–1092`) |
| 100 | SECP256K1_MULQ_INLINE | 186 | an inline: controller-fed template from `inlines/` (`virtual.cpp:1018–1092`) |
| 101 | SECP256K1_SQUAREQ_INLINE | 167 | an inline: controller-fed template from `inlines/` (`virtual.cpp:1018–1092`) |
| 102 | SECP256K1_DIVQ_INLINE | 194 | an inline: controller-fed template from `inlines/` (`virtual.cpp:1018–1092`) |
| 103 | SECP256K1_MULR_INLINE | 232 | an inline: controller-fed template from `inlines/` (`virtual.cpp:1018–1092`) |
| 104 | SECP256K1_SQUARER_INLINE | 213 | an inline: controller-fed template from `inlines/` (`virtual.cpp:1018–1092`) |
| 105 | SECP256K1_DIVR_INLINE | 240 | an inline: controller-fed template from `inlines/` (`virtual.cpp:1018–1092`) |
| 106 | SECP256K1_GLVR_ADV_INLINE | 13 | an inline: controller-fed template from `inlines/` (`virtual.cpp:1018–1092`) |

## 8. Opcodes that never occur (3)

- **126 `VirtualMove`**: nothing emits it (no emit site in `isa/virtual.cpp` or `inlines/`); its `execute_instruction` case (`trace_owner.cpp:649–653`, rd = rs1) is dead.
- **161 `VirtualAdviceRemU`**: REMU takes its advice from `VirtualAdviceDivU`, the quotient (`virtual.cpp:385`), so no remainder advice is emitted; the dead case (`trace_owner.cpp:554–562`) computes the quotient anyway (F7).
- **165 `VirtualAdviceRemUW`**: REMUW likewise uses `VirtualAdviceDivUW` (`virtual.cpp:424`); dead case `trace_owner.cpp:620–628`.

## 9. Test vectors

**Every vector below came from the compiled harness** (`/tmp/h/vec.cpp`): it `#include`s `trace_owner.cpp`, builds an `InterpreterState` over a small bytecode, and calls the tracer's own `execute_instruction<false>` (the log-driven path the replayer mirrors) for one hand-built row, each in a forked child so a failing assertion is caught. None is transcribed. What is *not* covered by the harness, because it isn't computed by `execute_instruction`: NOP (never executed, F10) and the row record's memory fields (section 3, read from `trace_owner.cpp:889–926`).

Setup: pc = 0x0000_0000_8000_0100; `pc_increment` = 4 unless the note says otherwise; registers rs1 = x6, rs2 = x7 and rd = x5 (initialised to 0xdead) unless an operand is an immediate. **next** is the `finish_instruction` outcome: *sequential* (pc += `pc_increment`, `instr_idx` + 1), *transfer* (pc = new pc, `instr_idx` from the log) or *end* (pc 0, `instr_idx` 0). **log** is the number of log words popped. *ABORT* means the tracer's assertion fails (the replayer never sees such a row).

**`ADD`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x7fff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0001 | 0x8000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 | wraps |
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0001 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0001, rs2 0x0000_0000_0000_0002 | 0x0000_0000_0000_0003 | 0x0000_0000_8000_0100 | sequential | 0 | not the last row: pc_increment 0 |

**`ADDI`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x8000_0000_0000_0000, imm 0xffff_ffff_ffff_ffff | 0x7fff_ffff_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 | imm -1 |

**`SUB`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0000, rs2 0x0000_0000_0000_0001 | 0xffff_ffff_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0000, rs2 0x0000_0000_0000_0001 | 0x7fff_ffff_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 | wraps |

**`XOR`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xf0f0_f0f0_f0f0_f0f0, rs2 0xffff_ffff_ffff_ffff | 0x0f0f_0f0f_0f0f_0f0f | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`XORI`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0005, imm 0xffff_ffff_ffff_ffff | 0xffff_ffff_ffff_fffa | 0x0000_0000_8000_0104 | sequential | 0 | imm -1 |

**`OR`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_00f0, rs2 0x0000_0000_0000_000f | 0x0000_0000_0000_00ff | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`ORI`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0000, imm 0xffff_ffff_ffff_f800 | 0xffff_ffff_ffff_f800 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`AND`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_ffff, rs2 0x8000_0000_0000_0001 | 0x8000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`ANDI`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_ffff, imm 0x0000_0000_0000_07ff | 0x0000_0000_0000_07ff | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`ANDN`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_00ff | 0xffff_ffff_ffff_ff00 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0000, rs2 0xffff_ffff_ffff_ffff | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`SLT`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0000, rs2 0xffff_ffff_ffff_ffff | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0000, rs2 0x7fff_ffff_ffff_ffff | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0005 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`SLTI`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_ffff, imm 0x0000_0000_0000_0000 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`SLTU`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0000, rs2 0xffff_ffff_ffff_ffff | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0001, rs2 0x0000_0000_0000_0001 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`SLTIU`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0000, imm 0x0000_0000_0000_0001 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | seqz idiom |
| rs1 0x0000_0000_0000_0005, imm 0xffff_ffff_ffff_ffff | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | imm -1 = 2^64-1 unsigned |

**`ADDW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_7fff_ffff, rs2 0x0000_0000_0000_0001 | 0xffff_ffff_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 | overflow to INT32_MIN |
| rs1 0x0000_0000_ffff_ffff, rs2 0x0000_0000_0000_0001 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0001_0000_0005, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0005 | 0x0000_0000_8000_0104 | sequential | 0 | upper bits dropped |

**`ADDIW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x8000_0000_0000_0000, imm 0x0000_0000_0000_0000 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`SUBW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_8000_0000, rs2 0x0000_0000_0000_0001 | 0x0000_0000_7fff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0000, rs2 0x0000_0000_0000_0001 | 0xffff_ffff_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_8000_0000, rs2 0x0000_0001_0000_0000 | 0xffff_ffff_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualSignExtendWord`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_8000_0000, rs2 0x0000_0000_0000_0000 | 0xffff_ffff_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0001_7fff_ffff, rs2 0x0000_0000_0000_0000 | 0x0000_0000_7fff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualZeroExtendWord`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0000 | 0x0000_0000_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0xffff_ffff_8000_0000, rs2 0x0000_0000_0000_0000 | 0x0000_0000_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualNegateIf`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0005 | 0xffff_ffff_ffff_fffb | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0000, rs2 0x0000_0000_0000_0005 | 0x0000_0000_0000_0005 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0000, rs2 0x8000_0000_0000_0000 | 0x8000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0000, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualMovsign`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x8000_0000_0000_0000, rs2 0x0000_0000_0000_0000 | 0xffff_ffff_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x7fff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0000, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualRev8W`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0102_0304_0506_0708, rs2 0x0000_0000_0000_0000 | 0x0403_0201_0807_0605 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0102_0304_0506_0708, imm 0x0000_0000_0000_0000 | 0x0403_0201_0807_0605 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`MUL`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x8000_0000_0000_0000, rs2 0xffff_ffff_ffff_ffff | 0x8000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0xffff_ffff_ffff_ffff, rs2 0xffff_ffff_ffff_ffff | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0001_0000_0000, rs2 0x0000_0001_0000_0000 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualMULI`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0003, imm 0x8000_0000_0000_0000 | 0x8000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 | SLLI 63 form |

**`MULW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0001_0000, rs2 0x0000_0000_0001_0000 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_8000, rs2 0x0000_0000_0001_0000 | 0xffff_ffff_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0xffff_ffff_ffff_ffff, rs2 0xffff_ffff_ffff_ffff | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0000, rs2 0x0000_0000_0000_0002 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualMULIW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_7fff_ffff, imm 0x0000_0000_0000_0002 | 0xffff_ffff_ffff_fffe | 0x0000_0000_8000_0104 | sequential | 0 | SLLIW 1 form |

**`MULHU`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_ffff, rs2 0xffff_ffff_ffff_ffff | 0xffff_ffff_ffff_fffe | 0x0000_0000_8000_0104 | sequential | 0 | top of range |
| rs1 0x8000_0000_0000_0000, rs2 0x0000_0000_0000_0002 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0001_0000_0000, rs2 0x0000_0001_0000_0000 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0001 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0000, rs2 0x8000_0000_0000_0000 | 0x4000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualPow2`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0000, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | rs1&63=0 |
| rs1 0x0000_0000_0000_0001, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0002 | 0x0000_0000_8000_0104 | sequential | 0 | rs1&63=1 |
| rs1 0x0000_0000_0000_001f, rs2 0x0000_0000_0000_0000 | 0x0000_0000_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 | rs1&63=31 |
| rs1 0x0000_0000_0000_0020, rs2 0x0000_0000_0000_0000 | 0x0000_0001_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 | rs1&63=32 |
| rs1 0x0000_0000_0000_003f, rs2 0x0000_0000_0000_0000 | 0x8000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 | rs1&63=63 |
| rs1 0x0000_0000_0000_0040, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | rs1&63=0 |
| rs1 0x0000_0000_0000_0041, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0002 | 0x0000_0000_8000_0104 | sequential | 0 | rs1&63=1 |

**`VirtualPow2W`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0000, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | zero-extended |
| rs1 0x0000_0000_0000_001f, rs2 0x0000_0000_0000_0000 | 0x0000_0000_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 | zero-extended |
| rs1 0x0000_0000_0000_0020, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | zero-extended |
| rs1 0x0000_0000_0000_003f, rs2 0x0000_0000_0000_0000 | 0x0000_0000_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 | zero-extended |

**`VirtualShiftRightBitmask`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0000, rs2 0x0000_0000_0000_0000 | 0xffff_ffff_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0001, rs2 0x0000_0000_0000_0000 | 0xffff_ffff_ffff_fffe | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_001f, rs2 0x0000_0000_0000_0000 | 0xffff_ffff_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0020, rs2 0x0000_0000_0000_0000 | 0xffff_ffff_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_003f, rs2 0x0000_0000_0000_0000 | 0x8000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0040, rs2 0x0000_0000_0000_0000 | 0xffff_ffff_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualShiftRightBitmaskW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0000, rs2 0x0000_0000_0000_0000 | 0x0000_0000_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0001, rs2 0x0000_0000_0000_0000 | 0x0000_0000_ffff_fffe | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_001f, rs2 0x0000_0000_0000_0000 | 0x0000_0000_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0020, rs2 0x0000_0000_0000_0000 | 0x0000_0000_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_003f, rs2 0x0000_0000_0000_0000 | 0x0000_0000_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualSRL`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x8000_0000_0000_0001, rs2 0x0000_0000_0000_0001 | 0x8000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, rs2 0x0000_0000_8000_0000 | 0x0000_0001_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, rs2 0x0000_0001_0000_0000 | 0x0000_0000_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, rs2 0x8000_0000_0000_0000 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, rs2 0xffff_ffff_ffff_fff0 | 0x0800_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, rs2 0x0000_0000_0000_0000 | 0x8000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | tz(0)=64: C++ UB |

**`VirtualSRLI`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x8000_0000_0000_0001, imm 0x0000_0000_0000_0001 | 0x8000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, imm 0x0000_0000_8000_0000 | 0x0000_0001_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, imm 0x0000_0001_0000_0000 | 0x0000_0000_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, imm 0x8000_0000_0000_0000 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, imm 0xffff_ffff_ffff_fff0 | 0x0800_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, imm 0x0000_0000_0000_0000 | 0x8000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | tz(0)=64: C++ UB |

**`VirtualSRA`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x8000_0000_0000_0001, rs2 0x0000_0000_0000_0001 | 0x8000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, rs2 0x0000_0000_8000_0000 | 0xffff_ffff_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, rs2 0x0000_0001_0000_0000 | 0xffff_ffff_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, rs2 0x8000_0000_0000_0000 | 0xffff_ffff_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, rs2 0x0000_0000_0000_0000 | 0x8000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | tz(0)=64: C++ UB |

**`VirtualSRAI`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x8000_0000_0000_0001, imm 0x0000_0000_0000_0001 | 0x8000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, imm 0x0000_0000_8000_0000 | 0xffff_ffff_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, imm 0x0000_0001_0000_0000 | 0xffff_ffff_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, imm 0x8000_0000_0000_0000 | 0xffff_ffff_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x8000_0000_0000_0001, imm 0x0000_0000_0000_0000 | 0x8000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | tz(0)=64: C++ UB |

**`VirtualSRLW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_8000_0001, rs2 0x0000_0000_0000_0001 | 0xffff_ffff_8000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | tz&31; result sign-extended |
| rs1 0xffff_ffff_8000_0001, rs2 0x0000_0000_8000_0000 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | tz&31; result sign-extended |
| rs1 0xffff_ffff_8000_0001, rs2 0x0000_0001_0000_0000 | 0xffff_ffff_8000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | tz&31; result sign-extended |
| rs1 0xffff_ffff_8000_0001, rs2 0x0000_0000_0000_0000 | 0xffff_ffff_8000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | tz&31; result sign-extended |

**`VirtualSRLIW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0001_8000_0001, imm 0x0000_0000_0000_0001 | 0xffff_ffff_8000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0001_8000_0001, imm 0x0000_0000_8000_0000 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0001_8000_0001, imm 0x0000_0001_0000_0000 | 0xffff_ffff_8000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0001_8000_0001, imm 0x0000_0000_0000_0000 | 0xffff_ffff_8000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualSRAW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_8000_0001, rs2 0x0000_0000_0000_0001 | 0xffff_ffff_8000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | tz&31 |
| rs1 0x0000_0000_8000_0001, rs2 0x0000_0000_8000_0000 | 0xffff_ffff_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 | tz&31 |
| rs1 0x0000_0000_8000_0001, rs2 0x0000_0001_0000_0000 | 0xffff_ffff_8000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | tz&31 |
| rs1 0x0000_0000_8000_0001, rs2 0x0000_0000_0000_0000 | 0xffff_ffff_8000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | tz&31 |

**`VirtualSRAIW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_7fff_ffff, imm 0x0000_0000_0000_0001 | 0x0000_0000_7fff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_7fff_ffff, imm 0x0000_0000_8000_0000 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_7fff_ffff, imm 0x0000_0001_0000_0000 | 0x0000_0000_7fff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_7fff_ffff, imm 0x0000_0000_0000_0000 | 0x0000_0000_7fff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualROTRI`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0001, imm 0x0000_0000_0000_0002 | 0x8000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0001, imm 0x8000_0000_0000_0000 | 0x0000_0000_0000_0002 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0001, imm 0x0000_0001_0000_0000 | 0x0000_0001_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0001, imm 0x0000_0000_0000_0000 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | tz(0)=64: rotr by 64 = identity |

**`VirtualROTRIW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xabcd_0000_0000_0001, imm 0x0000_0000_0000_0002 | 0x0000_0000_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 | min(32,tz); zero-extended |
| rs1 0xabcd_0000_0000_0001, imm 0x0000_0000_8000_0000 | 0x0000_0000_0000_0002 | 0x0000_0000_8000_0104 | sequential | 0 | min(32,tz); zero-extended |
| rs1 0xabcd_0000_0000_0001, imm 0x0000_0001_0000_0000 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | min(32,tz); zero-extended |
| rs1 0xabcd_0000_0000_0001, imm 0x0000_0100_0000_0000 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | min(32,tz); zero-extended |
| rs1 0xabcd_0000_0000_0001, imm 0x0000_0000_0000_0000 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 | min(32,tz); zero-extended |

**`VirtualXORROTL1`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0000, rs2 0x8000_0000_0000_0000 | 0x0000_0000_0000_0001 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0001 | 0xffff_ffff_ffff_fffd | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualXORROT63`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0001, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0002 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualXORROT32`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0001, rs2 0x0000_0000_0000_0000 | 0x0000_0001_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualXORROT24`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0001, rs2 0x0000_0000_0000_0000 | 0x0000_0100_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualXORROT16`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_00ff, rs2 0x0000_0000_0000_00ff | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 | xor cancels |

**`VirtualXORROTW16`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_0000_0001, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0001_0000 | 0x0000_0000_8000_0104 | sequential | 0 | upper bits dropped; zero-extended |

**`VirtualXORROTW12`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0001, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0010_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualXORROTW8`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0001, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0100_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualXORROTW7`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0001, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0200_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualXORROTW22`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0001, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0400 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualXORROTW19`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0001, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_2000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualXORROTW6`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0001, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0400_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualAlignAddr`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_000d, imm 0x0000_0000_0000_0002 | 0x0000_0000_0000_0008 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0xffff_ffff_ffff_ffff, imm 0x0000_0000_0000_0000 | 0xffff_ffff_ffff_fff8 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualWindowMaskB`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0000, imm 0x0000_0000_0000_0000 | 0x0000_0000_0000_00ff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0005, imm 0x0000_0000_0000_0000 | 0x0000_ff00_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0007, imm 0x0000_0000_0000_0000 | 0xff00_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0008, imm 0x0000_0000_0000_0000 | 0x0000_0000_0000_00ff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0xffff_ffff_ffff_ffff, imm 0x0000_0000_0000_0006 | 0x0000_ff00_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 | rs1+imm wraps to 5 |

**`VirtualWindowMaskH`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0000, imm 0x0000_0000_0000_0000 | 0x0000_0000_0000_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0006, imm 0x0000_0000_0000_0000 | 0xffff_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0007, imm 0x0000_0000_0000_0000 | 0xffff_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualWindowMaskW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0000, imm 0x0000_0000_0000_0000 | 0x0000_0000_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0003, imm 0x0000_0000_0000_0000 | 0x0000_0000_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0004, imm 0x0000_0000_0000_0000 | 0xffff_ffff_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0007, imm 0x0000_0000_0000_0000 | 0xffff_ffff_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualShiftDataB`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_01ff, rs2 0x0000_0000_0000_0005 | 0x0000_ff00_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_01ff, rs2 0x0000_0000_0000_000f | 0xff00_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 | rs2&7=7 |

**`VirtualShiftDataH`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0001_ffff, rs2 0x0000_0000_0000_0007 | 0xffff_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 | rs2&6=6 |
| rs1 0x0000_0000_0001_ffff, rs2 0x0000_0000_0000_0001 | 0x0000_0000_0000_ffff | 0x0000_0000_8000_0104 | sequential | 0 | rs2&6=0 |

**`VirtualShiftDataW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0001_ffff_ffff, rs2 0x0000_0000_0000_0004 | 0xffff_ffff_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0001_ffff_ffff, rs2 0x0000_0000_0000_0003 | 0x0000_0000_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 | rs2&4=0 |

**`VirtualPext`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x1234_5678_90ab_cdef, rs2 0x0000_0000_0000_ff00 | 0x0000_0000_0000_00cd | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x1234_5678_90ab_cdef, rs2 0xffff_ffff_0000_0000 | 0x0000_0000_1234_5678 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_000a | 0x0000_0000_0000_0003 | 0x0000_0000_8000_0104 | sequential | 0 | non-contiguous mask (never emitted) |
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 | mask 0 |

**`VirtualPextSigned`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_8000, rs2 0x0000_0000_0000_ff00 | 0xffff_ffff_ffff_ff80 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_7f00, rs2 0x0000_0000_0000_ff00 | 0x0000_0000_0000_007f | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_8000_0000, rs2 0x0000_0000_ffff_ffff | 0xffff_ffff_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0xffff_0000_0000_0000, rs2 0xffff_0000_0000_0000 | 0xffff_ffff_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 | top window: no extension needed |
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 | mask 0 |

**`BEQ`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0000, rd-slot imm 0x0000_0000_0000_0010 | — | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0005, rd-slot imm 0xffff_ffff_ffff_fff0 | — | 0x0000_0000_8000_00f0 | transfer | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0005, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 | imm 0: target = pc |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0005, rd-slot imm 0x0000_0000_0000_0010 | — | 0x0000_0000_8000_0110 | transfer | 0 | pc_increment 4 |

**`BNE`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0000, rd-slot imm 0x0000_0000_0000_0010 | — | 0x0000_0000_8000_0110 | transfer | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0005, rd-slot imm 0xffff_ffff_ffff_fff0 | — | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0005, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 | imm 0: target = pc |

**`BLT`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0000, rd-slot imm 0x0000_0000_0000_0010 | — | 0x0000_0000_8000_0110 | transfer | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0005, rd-slot imm 0xffff_ffff_ffff_fff0 | — | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0005, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 | imm 0: target = pc |

**`BGE`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0000, rd-slot imm 0x0000_0000_0000_0010 | — | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0005, rd-slot imm 0xffff_ffff_ffff_fff0 | — | 0x0000_0000_8000_00f0 | transfer | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0005, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 | imm 0: target = pc |

**`BLTU`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0000, rd-slot imm 0x0000_0000_0000_0010 | — | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0005, rd-slot imm 0xffff_ffff_ffff_fff0 | — | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0005, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 | imm 0: target = pc |

**`BGEU`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0000, rd-slot imm 0x0000_0000_0000_0010 | — | 0x0000_0000_8000_0110 | transfer | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0005, rd-slot imm 0xffff_ffff_ffff_fff0 | — | 0x0000_0000_8000_00f0 | transfer | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0005, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 | imm 0: target = pc |

**`JAL`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| imm 0x0000_0000_0000_0010, imm 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | 0x0000_0000_8000_0110 | transfer | 0 |  |
| imm 0xffff_ffff_ffff_fff0, imm 0x0000_0000_0000_0000 | 0x0000_0000_8000_0102 | 0x0000_0000_8000_00f0 | transfer | 0 | compressed: link pc+2 |
| imm 0x0000_0000_0000_0000, imm 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | 0x0000_0000_0000_0000 | end: pc 0, instr_idx 0 | 0 | imm 0: program end |
| imm 0x0000_0000_0000_0000, imm 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | 0x0000_0000_0000_0000 | end: pc 0, instr_idx 0 | 0 | EBREAK's JAL v40, 0: program end |

**`JALR`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_8000_0121, imm 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | 0x0000_0000_8000_0120 | transfer | 0 | bit 0 cleared |
| rs1 0x0000_0000_8000_0100, imm 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | 0x0000_0000_8000_0104 | sequential | 0 | target = pc: sequential |
| rs1 0x0000_0000_8000_0120, imm 0xffff_ffff_ffff_ffe0 | 0x0000_0000_8000_0102 | 0x0000_0000_8000_0102 | sequential | 0 | rs1+imm = pc, compressed |
| rs1 0x0000_0000_8000_0140, imm 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | 0x0000_0000_8000_0140 | transfer | 0 | rd = rs1: reads old rs1 |
| rs1 0x0000_0000_0000_0000, imm 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | 0x0000_0000_0000_0000 | end: pc 0, instr_idx 0 | 0 | target 0: program end |

**`AUIPC`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| imm 0x0000_0000_0000_1000, imm 0x0000_0000_0000_0000 | 0x0000_0000_8000_1100 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| imm 0xffff_ffff_ffff_f000, imm 0x0000_0000_0000_0000 | 0x0000_0000_7fff_f100 | 0x0000_0000_8000_0104 | sequential | 0 | negative |

**`LUI`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| imm 0xffff_ffff_8000_0000, imm 0x0000_0000_0000_0000 | 0xffff_ffff_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| imm 0x0000_0000_1234_5000, imm 0x0000_0000_0000_0000 | 0x0000_0000_1234_5000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`LD`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_1000, imm 0x0000_0000_0000_0008 | 0x1122_3344_5566_7788 | 0x0000_0000_8000_0104 | sequential | 1 | rd = log word |
| rs1 0x0000_0000_0000_1000, imm 0x0000_0000_0000_0008 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 1 | rd = x0: dropped |

**`SD`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_1000, rs2 0x0000_0000_0000_0abc, rd-slot imm 0x0000_0000_0000_0008 | — | 0x0000_0000_8000_0104 | sequential | 1 | pops the log; writes nothing |

**`VirtualAdvice`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| imm 0x0000_0000_0000_0000, imm 0x0000_0000_0000_0000 | 0x0000_0000_0000_0042 | 0x0000_0000_8000_0104 | sequential | 1 | rd = log word |

**`VirtualAdviceDivU`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0000 | 0xffff_ffff_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0002 | 0x7fff_ffff_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualAdviceDiv`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x8000_0000_0000_0000, rs2 0xffff_ffff_ffff_ffff | 0x8000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0xffff_ffff_ffff_fff9, rs2 0x0000_0000_0000_0002 | 0xffff_ffff_ffff_fffd | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0000 | 0xffff_ffff_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualAdviceRem`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_fff9, rs2 0x0000_0000_0000_0002 | 0x0000_0000_0000_0003 | 0x0000_0000_8000_0104 | sequential | 0 | quotient magnitude, not the remainder |
| rs1 0x8000_0000_0000_0000, rs2 0xffff_ffff_ffff_ffff | 0x8000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualAdviceDivUW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0000 | 0x0000_0000_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0001_ffff_ffff, rs2 0x0000_0001_0000_0002 | 0x0000_0000_7fff_ffff | 0x0000_0000_8000_0104 | sequential | 0 | low words only |

**`VirtualAdviceDivW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_8000_0000, rs2 0xffff_ffff_ffff_ffff | 0x0000_0000_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 | zero-extended INT32_MIN |
| rs1 0xffff_ffff_ffff_fff9, rs2 0x0000_0000_0000_0002 | 0xffff_ffff_ffff_fffd | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0000 | 0xffff_ffff_ffff_ffff | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualAdviceRemW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0xffff_ffff_ffff_fff9, rs2 0x0000_0000_0000_0002 | 0x0000_0000_0000_0003 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0xffff_ffff_8000_0000, rs2 0xffff_ffff_ffff_ffff | 0x0000_0000_8000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0000 | 0x0000_0000_0000_0000 | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualAssertEQ`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0001, rs2 0x0000_0000_0000_0001, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0001, rs2 0x0000_0000_0000_0002, rd-slot imm 0x0000_0000_0000_0000 | ABORT | — | — | — |  |

**`VirtualAssertLTE`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0000, rs2 0xffff_ffff_ffff_ffff, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0xffff_ffff_ffff_ffff, rs2 0x0000_0000_0000_0000, rd-slot imm 0x0000_0000_0000_0000 | ABORT | — | — | — | unsigned compare |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0005, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualAssertValidDiv0`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0000, rs2 0xffff_ffff_ffff_ffff, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0000, rs2 0x0000_0000_0000_0005, rd-slot imm 0x0000_0000_0000_0000 | ABORT | — | — | — |  |
| rs1 0x0000_0000_0000_0003, rs2 0x0000_0000_0000_0005, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualAssertValidUnsignedRemainder`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0002, rs2 0x0000_0000_0000_0003, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0003, rd-slot imm 0x0000_0000_0000_0000 | ABORT | — | — | — |  |
| rs1 0x0000_0000_0000_0005, rs2 0x0000_0000_0000_0000, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 | divisor 0: always passes |

**`VirtualAssertMulUNoOverflow`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0001_0000_0000, rs2 0x0000_0001_0000_0000, rd-slot imm 0x0000_0000_0000_0000 | ABORT | — | — | — |  |
| rs1 0x0000_0000_ffff_ffff, rs2 0x0000_0001_0000_0001, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 | product 2^64-1 |

**`VirtualAssertHalfwordAlignment`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0001, imm 0x0000_0000_0000_0001, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0001, imm 0x0000_0000_0000_0000, rd-slot imm 0x0000_0000_0000_0000 | ABORT | — | — | — |  |

**`VirtualAssertWordAlignment`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0002, imm 0x0000_0000_0000_0002, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 |  |
| rs1 0x0000_0000_0000_0002, imm 0x0000_0000_0000_0001, rd-slot imm 0x0000_0000_0000_0000 | ABORT | — | — | — |  |

**`VirtualHostIO`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0005, imm 0x0000_0000_0000_0001 | 0x0000_0000_0000_1234 | 0x0000_0000_8000_0104 | sequential | 0 | rd keeps its own value |

**`FENCE`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| imm 0x0000_0000_0000_0000, imm 0x0000_0000_0000_0000, rd-slot imm 0x0000_0000_0000_0000 | — | 0x0000_0000_8000_0104 | sequential | 0 |  |

**`VirtualAdviceRemU`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0007, rs2 0x0000_0000_0000_0002 | 0x0000_0000_0000_0003 | 0x0000_0000_8000_0104 | sequential | 0 | dead case: computes the quotient |

**`VirtualAdviceRemUW`**

| inputs | rd after | new pc | next | log | note |
|---|---|---|---|---|---|
| rs1 0x0000_0000_0000_0007, rs2 0x0000_0000_0000_0002 | 0x0000_0000_0000_0003 | 0x0000_0000_8000_0104 | sequential | 0 | dead case |

---

*Harness sources: `/tmp/h/enum.cpp` (the opcode and slot-form enumeration, output `/tmp/h/enum.txt` and `/tmp/h/summary.txt`) and `/tmp/h/vec.cpp` (these vectors, output `/tmp/h/vec.tsv`). Both are built with the repository's CMake build in `/tmp/tb` (`-DCMAKE_POLICY_VERSION_MINIMUM=3.5`), then `g++ -std=c++20 -O1 -DNDEBUG … -Wl,--allow-multiple-definition /tmp/tb/libtracer.a /tmp/tb/_deps/fmt-build/libfmt.a -lelf -no-pie`. Nothing was written into the repository.*
