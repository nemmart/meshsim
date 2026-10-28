import json,re,collections
exec(open('/tmp/h/doc/rows.py').read())
D=json.load(open('/tmp/h/doc/data.json')); src=D['src']; summ=D['summ']; sites=D['sites']; code=D['code']
names={v:k for k,v in code.items()}
def slots(n):
    if n=='NOP': return 'x0·I·I (all 0)'
    out=[]
    for f in summ[n]['forms']:
        m=re.match(r'rd=(\S+) rs1=(\S+) rs2=(\S+)',f)
        out.append('·'.join(x.replace('R(x0)','x0') for x in m.groups()))
    return '; '.join(out)
def last(n):
    if n=='NOP': return '— (padding)'
    return summ[n]['last']
def source(n):
    if n=='NOP': return '`trace_owner.hpp:161–163`; `trace_owner.cpp:881–883`'
    return src[n]
fam=[('A. Integer ALU',[0,1,2,3,4,5,6,7,8,9,159,16,17,18,19]),
 ('B. Word forms, extension and sign helpers',[63,64,62,110,111,127,128,107]),
 ('C. Multiply',[43,114,65,115,45]),
 ('D. Shifts, bitmasks and rotates',[116,121,117,146,118,119,129,120,147,148,149,150,131,132,158,133,134,135,136,137,138,139,140,143,144,145]),
 ('E. Sub-word memory helpers',[154,152,153,141,155,156,157,151,142]),
 ('F. Control flow and pc-relative',[28,29,30,31,32,33,34,35,36,37]),
 ('G. Memory, the log and advice',[54,53,122,160,162,163,164,166,167]),
 ('H. Assertions (never evaluated by the replayer)',[108,123,124,125,130,112,113]),
 ('I. Miscellaneous',[109,52])]
assert sum(len(x) for _,x in fam)==91
L=[]
w=L.append
w("# Appendix: Replayer opcode semantics\n")
w("*Draft for folding into `spec/machine-spec.md` (4.19.7 and items 34–37, 43, 56, 57). Derived from the jolt-cpp tracer, `github.com/quarky93/jolt-asic-planning`, branch `tracer-standalone`, commit 4f36b3d, against the spec of `mesh-sorter-v1.1-2026-10-26g`/`26h` (4.19.3, 4.19.4, 4.19.7 unchanged between them). Every file:line below is in `tracer/src/tracer/` at that commit.*\n")
w("This appendix defines, for every opcode that occurs in a replayer row, exactly what the row computes: the value written to rd, the control transfer, what it takes from the log and the memory fields of its record. An ALU designer or simulator author should be able to implement the replayer's datapath from it without reading the tracer. Where the tracer and the spec disagree, or the spec is incomplete, the point is flagged (F1–F12, section 4) rather than resolved.\n")
w("**How it was derived.** The opcode set and every operand form come from running the tracer's own expansion code (`expand_virt_seq`, `isa/virtual.cpp`) over every source opcode, and the semantics from `execute_instruction` (`trace_owner.cpp:145–669`), read line by line and exercised by a harness compiled against the tracer's code (section 9). Both harnesses live outside the repository, in `/tmp/h` (`enum.cpp`, `vec.cpp`), built against `libtracer.a` from the repository's own CMake build.\n")
w("## 1. Conventions\n")
w("### 1.1 Values and notation\n")
w("- Every register and every immediate slot holds a **64-bit** value, `RiscRegValue` = `u64`. Arithmetic is modulo 2⁶⁴ unless a formula says otherwise; nothing traps on overflow.\n- s64(x), s32(x): x read as a two's-complement signed integer of 64 or 32 bits. **sx32(x)**: bits [31:0] of x sign-extended to 64 bits. **zx32(x)**: bits [31:0] of x, zero-extended. x[i:j]: bits i down to j.\n- **<ᵤ, ≤ᵤ, ≥ᵤ**: unsigned compares; **<**, **≥** on s64(·): signed.\n- **tz(x)**: the number of trailing zero bits of x, `std::countr_zero`, so **tz(0) = 64** (`utils/math.hpp`).\n- **rotr64(x, k)**, **rotr32(x, k)**, **rotl64(x, k)**: rotations by k modulo the width (`std::rotr`; `rotate_left_64` masks k to 6 bits); a rotation by the full width is the identity.\n- **bswap32**: reverse the 4 bytes of a 32-bit word.\n- **pext(x, m)**: gather the bits of x at the positions of m's 1-bits, from bit 0 upward, into consecutive low bits of the result (`utils::pext<64>`).\n- Shift amounts are masked where the table says so; **6 bits** (mod 64) for 64-bit shifts and **5 bits** (mod 32) for word shifts.\n")
w("### 1.2 Slots and how a row reads them\n")
w("A row has three operand slots, rd, rs1 and rs2, each either a **register** (R) or an **immediate** (I) (`VirtualInstruction`, `isa/virtual.hpp`). `read_reg_vals` (`trace_owner.cpp:78–83`) gives each slot's value: a register slot reads its register (register 0 always reads 0, because writes to it are dropped), an immediate slot carries its own 64-bit value, set when the row was built (sign-extended from the source encoding, `isa/decoder.cpp`). In the tables, **imm** is whichever slot is the immediate for that opcode, and the Slots column gives the kinds as rd·rs1·rs2, with x0 where the register is always register 0 in that form.\n\nWhere each opcode keeps its immediate:\n\n| immediate in | opcodes |\n|---|---|\n| **rs2** (the I-type place) | ADDI, XORI, ORI, ANDI, SLTI, SLTIU, ADDIW, JALR, LD, `VirtualMULI`, `VirtualMULIW`, `VirtualSRLI`, `VirtualSRAI`, `VirtualSRLIW`, `VirtualSRAIW`, `VirtualROTRI`, `VirtualROTRIW`, the window masks, `VirtualAlignAddr`, the alignment assertions; and an unused 0 in `VirtualRev8W`, `VirtualSignExtendWord`, `VirtualZeroExtendWord`, `VirtualPow2`, `VirtualPow2W`, the shift-right bitmasks, `VirtualMovsign`, `VirtualHostIO` |\n| **rs1** | JAL, LUI, AUIPC (`emit_u`, `isa/virtual.cpp:205–215`; decoder `isa/decoder.cpp:1187–1197`), and `VirtualAdvice` (0, with rs2 also immediate 0) |\n| **rd** | the branches BEQ–BGEU, SD, `VirtualAssertEQ`, `VirtualAssertLTE`, `VirtualAssertValidDiv0`, `VirtualAssertValidUnsignedRemainder`, `VirtualAssertMulUNoOverflow` (`emit_b`, `emit_s`), and the alignment assertions (rd immediate 0, `emit_h`, `isa/virtual.cpp:196–198`) |\n| **all three** | FENCE (all 0, `isa/virtual.cpp:1093–1097`) |\n\n**A slot's kind never varies for an opcode.** Over every emission site the R/I pattern of each opcode is the same; forms differ only in whether a register is x0 (section 6). So operand decode can be a fixed per-opcode property, as the spec's operand codes already allow.\n")
w("### 1.3 Writing rd\n")
w("- `save_rd` (`trace_owner.cpp:85–93`) **drops a write to register 0**; every other register, virtual registers included, takes the value.\n- A row whose rd slot is an immediate (branches, SD, the assertions, FENCE) **writes nothing**: `save_rd` asserts rd is a register, and these opcodes never call it.\n- **All reads happen before the write.** Every case reads its slots first, so a row whose rs1 or rs2 is also its rd sees the old value (JALR computes its link before writing; vector in section 9).\n")
w("### 1.4 pc, `pc_increment` and `instr_idx`\n")
w("- **pc** (`risc_pc`) is the **source instruction's** address, the same for every row of its expansion.\n- **`pc_increment`** is the source instruction's size, **2 or 4, on its expansion's last row only, and 0 on every other row** (`isa/virtual.cpp:1413–1416`: only `instrs[num_instrs + virt_seq_len − 1].pc_increment` is set; `emit_instr` leaves it 0).\n- Each opcode case may set **new_pc** (it starts equal to pc). Then `finish_instruction` (`trace_owner.cpp:111–126`):\n  1. **new_pc = 0** → pc = 0 and `instr_idx` = 0: the program has ended. This is checked first, for **every** opcode.\n  2. **new_pc ≠ pc** → a control transfer: pc = new_pc and `instr_idx` = the index of new_pc (in the replayer, the row's log word).\n  3. **otherwise** → pc += `pc_increment` and `instr_idx` += 1. So a row that isn't its expansion's last leaves pc where it is, and **a branch or jump whose target equals its own pc is sequential** (the self-jump rule).\n- The Control column below gives only new_pc; this rule then applies.\n")
w("### 1.5 The row record\n")
w("Each row's record is formed around `execute_instruction` (`trace_owner.cpp:889–926`; the derived helpers in `trace_owner.hpp:165–235` agree):\n\n| field | value |\n|---|---|\n| opcode | the row's opcode, except that the six DIV/REM advice opcodes are reported as `VirtualAdvice` (`trace_owner.hpp:164–166`) |\n| rd_reg, rs1_reg, rs2_reg | the slot's register number, or 0 if the slot is an immediate; for advice rows rs1_reg = rs2_reg = 0 |\n| rd_prev | rd's value before the row, or 0 if rd is an immediate |\n| rs1, rs2 | the registers' values before the row, or 0 for an immediate slot; 0 for advice rows |\n| rd | rd's value **after** the row, or 0 if rd is an immediate |\n| imm (`_imm`) | the **sum** of the immediate slots' values (rd·[rd imm] + rs1·[rs1 imm] + rs2·[rs2 imm]); at most one is non-zero in any form; 0 for advice rows |\n| is_compressed | `pc_increment` = 2 (so only the last row of a compressed instruction's expansion has it) |\n| memory fields | LD and SD only (section 3); 0 for every other opcode |\n")
w("## 2. The row opcodes\n")
w("91 opcodes occur in rows; the replayer executes 85 of them (the six DIV/REM advice opcodes are written as `VirtualAdvice`, spec 4.19.7). **Class** and cycles are 4.19.7's; the source agrees with all of them (F9). **Last row** says whether the opcode is always, never or sometimes its expansion's last row, which decides whether `pc_increment` is 0 (section 6 lists, for \"sometimes\", which source instructions end with it). **Source** is the `execute_instruction` case.\n")
for title,codes in fam:
    w(f"### {title}\n")
    w("| code | name | class | slots rd·rs1·rs2 | writes rd | rd = | control (new pc) | log | last row | source |")
    w("|---|---|---|---|---|---|---|---|---|---|")
    for c in codes:
        n=names[c]; r=S[n]
        w(f"| {c} | `{n}` | {r['cls']} | {slots(n)} | {r['writes']} | {r['rd']} | {r['ctl']} | {r['log']} | {last(n)} | {source(n)} |")
    w("")
    if title.startswith('D.'):
        w("**Where the shift amounts come from.** The 64-bit shifts reach the replayer only as these opcodes (SLL–SRAI, `isa/virtual.cpp:565–640`): SLL → `VirtualPow2` + MUL; SLLI → `VirtualMULI` with imm = 2^(shamt mod 64); SRL/SRA → `VirtualShiftRightBitmask` + `VirtualSRL`/`VirtualSRA`; SRLI/SRAI → `VirtualSRLI`/`VirtualSRAI` with imm = `mask_trailing_zeros(shamt mod 64)` = 2⁶⁴ − 2^shamt. The word shifts likewise (SLLW → `VirtualPow2W` + MULW; SLLIW → `VirtualMULIW` with imm = 2^(shamt mod 32); SRLW/SRAW → `VirtualShiftRightBitmaskW` + `VirtualSRLW`/`VirtualSRAW`; SRLIW/SRAIW → imm = 2³² − 2^(shamt mod 32)). So a shift amount is always carried as a power of two (left) or as a mask whose tz is the amount (right), and **no shift mask is ever 0** (F6).\n")
    if title.startswith('E.'):
        w("**The masks VirtualPext receives.** `expand_load` (`isa/virtual.cpp:237–250`) always feeds VirtualPext and VirtualPextSigned the rd of the window-mask row just before them: `0xFF << 8k` (LB, LBU), `0xFFFF << 16k` (LH, LHU), or `0xFFFF_FFFF << 32k` (LW, LWU, LR.W, SC.W). Every mask is contiguous and non-zero, so the general pext reduces to (rs1 ∧ m) >> tz(m), and the signed form sign-extends from bit 8, 16 or 32 of the result (spec item 43). The general definition above is what the tracer computes for any mask.\n")
    if title.startswith('G.'):
        w("**The advice opcodes.** The tracer computes the six DIV/REM advice values from registers (rows `isa/virtual.cpp:322–480`), but the replayer never does: the template builder writes those rows as `VirtualAdvice rd, 0, 0` with `from_log` set (spec 4.19.7, item 56), and the value comes from the read log. Their formulas are given so the tracer's log content is defined; the replayer's datapath needs none of them.\n")
    if title.startswith('H.'):
        w("The replayer doesn't evaluate assertions (spec 4.19.7): the tracer aborts on a failing one (`release_assert`), so a traced program never contains one that fails. Each writes nothing and moves pc only as a sequential row. The checks are given for completeness and for a simulator that wants to re-check a trace.\n")
w("## 3. LD and SD: the memory fields\n")
w("LD and SD are the normalized ISA's only memory opcodes; every load and store, sub-word, LR/SC and AMO included, is built from them (`trace_owner.hpp:219–235`; `trace_owner.cpp:917–925`).\n\n| opcode | slots | address | pre-value | post-value | rd | log |\n|---|---|---|---|---|---|---|\n| LD 54 | rd R, rs1 R, **imm in rs2** | (rs1 + imm) mod 2⁶⁴ | rd's value **after** the row | the same | the log word | 1 read-log word |\n| SD 53 | **imm in rd**, rs1 R, rs2 R | (rs1 + imm) mod 2⁶⁴ | the log word (the value memory held) | rs2's value | none: rd_reg, rd, rd_prev all 0 | 1 write-log word |\n\n- **Alignment:** the address is always a multiple of 8 in a correct trace. The tracer asserts it only when it reads RAM (`trace_owner.cpp:304`, `314`); with the log it doesn't check. The replayer doesn't either.\n- **LD with rd = x0** (only in LR.D's expansion with rd = x0): the log word is consumed but dropped, so rd stays 0 and **the record's pre- and post-values are 0**, not the memory value (F4).\n- **SD's pre-value** is the log word in the record (`mem_log[mem_log_idx − 1]`); the derived helper in `trace_owner.hpp` reads it from \"the rd lane\", which carries the same value in the GPU layout.\n")
# ---------------- section 4: flags
w("## 4. Flags: where the tracer, the spec and a reimplementation can disagree\n")
F=[
("F1","**JAL with imm = 0 always ends the program.** `execute_instruction` sets new_pc = 0 whenever a JAL's immediate is 0 (`trace_owner.cpp:280–281`), with no flag. The spec (4.19.7's pc table, 4.19.3's `imm0_pc0`) makes it conditional on a template bit. They agree only if **every JAL template word sets `imm0_pc0`** (JAL's own template and EBREAK's, F2). Suggested spec text: \"JAL: new pc = pc + imm, or 0 if imm = 0. Every JAL row behaves this way; `imm0_pc0` is set in every JAL word\", or drop the flag."),
("F2","**EBREAK is one row, `JAL v40, 0`** (`isa/virtual.cpp:1164–1192`), so in the bytecode it ends the program (F1) and links pc + `pc_increment` into v40. At run time the JIT still aborts on EBREAK (`transcode.cpp`), and no current guest reaches one. The spec lists EBREAK as controller-fed; its template is this single JAL."),
("F3","**A new pc of 0 ends the program for every opcode,** not only JAL: `finish_instruction` tests new_pc = 0 before anything else (`trace_owner.cpp:117–119`), so a JALR whose target is 0, or a branch whose pc + imm wraps to 0, also sets pc = 0 and `instr_idx` = 0. 4.19.7 says \"a new pc of 0 has the log word 0\", which covers it; the pc table could say so for JALR and the branches too."),
("F4","**LD with rd = x0 reports zero memory values.** The record's pre- and post-values are taken from rd after the row (`trace_owner.cpp:922–925`), so an LD whose rd is x0 (only LR.D with rd = x0, an exempt instruction, section 5) consumes its read-log word but reports pre = post = 0 and rd = 0. The replayer must form them the same way: from the value written, not the log word. 4.19.7 and 4.19.12 don't say this."),
("F5","**\"The W forms sign-extend their low 32 bits\" (4.19.7) is too broad.** Sign-extending: ADDW, ADDIW, SUBW, MULW, `VirtualMULIW`, `VirtualSRLW`, `VirtualSRLIW` (a *logical* word shift whose result is then sign-extended), `VirtualSRAW`, `VirtualSRAIW`, `VirtualSignExtendWord`. **Zero-extending:** `VirtualPow2W`, `VirtualShiftRightBitmaskW`, `VirtualROTRIW`, all seven `VirtualXORROTW…`, `VirtualZeroExtendWord`, and among the advice values `VirtualAdviceDivUW`, `VirtualAdviceRemW` and `VirtualAdviceDivW`'s INT32_MIN / −1 case (0x0000_0000_8000_0000)."),
("F6","**tz(0) = 64 in the 64-bit right shifts is undefined in the tracer.** `VirtualSRL`, `VirtualSRLI`, `VirtualSRA` and `VirtualSRAI` shift by tz(rs2), and a shift by 64 is undefined behaviour in C++ (the harness, on x86, returns rs1 unchanged). It can't arise: every mask comes from `VirtualShiftRightBitmask` (never 0) or from the expansion (2⁶⁴ − 2^shamt, never 0). The word shifts mask tz to 5 bits, so they are defined for 0 (shift 0), and `VirtualROTRI`'s `std::rotr` by 64 is the identity. Suggest: the spec states the masks are never 0 and leaves the result for 0 undefined, and meshsim flags a zero mask."),
("F7","**`VirtualAdviceRem` and `VirtualAdviceRemW` compute the quotient's magnitude, not the remainder** (`trace_owner.cpp:574–584`, `598–610`), and the dead `VirtualAdviceRemU` case computes the quotient. That is what Jolt's REM expansions consume (the remainder is rebuilt from it), so the names mislead but the log content is right. The replayer is unaffected: it takes the log word."),
("F8","**Immediates live in different slots by opcode** (section 1.2): rs1 for JAL, LUI and AUIPC; rd for branches, SD and five assertions; rs2 for the rest. The kind of each slot is fixed per opcode across every emission site (checked for all 90 emitted opcodes), so no opcode's operand can be either a register or an immediate. Only *which* register varies, including x0 in nine forms (section 6)."),
("F9","**No class disagreements.** Every opcode's class in 4.19.7 matches what its case computes: the five M opcodes are the only ones that need a 64 × 64 product; `VirtualAssertMulUNoOverflow` multiplies only to check and is never evaluated, so S is right; the L opcodes are exactly those that pop a log word (LD, SD, `VirtualAdvice`, and the six advice opcodes written as `VirtualAdvice`). MULH and MULHSU never reach a row (`isa/virtual.cpp:485–503`), as 4.19.7 says."),
("F10","**NOP is never executed.** It has no `execute_instruction` case (it would reach `unreachable()`); padding rows are the zero-filled tail of the trace once pc reaches 0 (`trace_owner.cpp:881–883`; `trace_owner.hpp:161–163`). The rd = x0 rewrite produces `ADDI x0, x0, 0`, not opcode 0 (`isa/virtual.cpp:307–311`). The replayer's padding row (4.19.6) is therefore pure data."),
("F11","**`VirtualHostIO` writes rd's own value back** (`trace_owner.cpp:294–299`), so the register file doesn't change; rd_prev = rd in the record. Its source rd = x0 is remapped to v40 first (section 5), so the row never names x0."),
("F12","**AUIPC uses the source instruction's pc on every row,** including ECALL's AUIPC row, which is not its expansion's last (`trace_owner.cpp:156–160`; ECALL `isa/virtual.cpp:1098–1115`)."),
]
for k,t in F: w(f"- **{k}.** {t}")
w("")
# ---------------- section 5: rd = x0
w("## 5. The rd = x0 cross-check against spec 4.19.4\n")
w("**What the source does** (`isa/virtual.cpp:19–120`, `300–320`):\n")
w("1. `has_rd0_register` (`:104–106`): the source's rd field is a register (not an immediate) and is 0.\n2. If the opcode is in neither `has_side_effects_beyond_rd` (`:19–55`) nor `rd0_is_handled_internally` (`:57–102`), the instruction becomes **one row, `ADDI x0, x0, 0`** (`:307–311`), virtual unless the source is ADDI itself (`:183`).\n3. If it has side effects and isn't handled internally, **rd is remapped** before any per-opcode expansion (`:315–319`): `allocate_virtual_register()` returns the first free register, which is always **40** (allocation starts at 32 + 8 = 40, `:140`), and the template's own temporaries then start at 41.\n4. Opcodes in `rd0_is_handled_internally` (ECALL, MRET, EBREAK, CSRRW, CSRRS, LR/SC and the inlines) are expanded unchanged.\n")
w("**JAL, JALR and `VirtualHostIO` with rd = x0.** All three are in `has_side_effects_beyond_rd` and not handled internally, so step 3 applies: the source's rd becomes v40, and the rows are `JAL v40, imm`, `JALR v40, rs1, imm` and `VirtualHostIO v40, rs1, imm`. Each case also carries its own `if (src.op_a == 0)` branch that would allocate a throwaway register (`:1195–1226`), but by the time it runs, op_a is already 40, so **those branches are dead code**. Either path would give v40, so the result is the same. The harness confirms it: with rd = x0 these sources produce rows whose rd is a non-zero register.\n\nSo the link is **written, not dropped**: v40 receives pc + `pc_increment`, and the record shows rd_reg = 40, rd_prev = v40's old value and rd = the link. v40 is not an inline temporary (it is below `num_stateful_regs` = 48, so `normalize_inline_vregs` never zeroes it, `trace_owner.cpp:128–140`), so the value stays in v40 until the next instruction that writes it, and a later rd_prev of v40 shows it.\n")
w("**What 4.19.4 says.** It lists JAL, JALR, LD and `VirtualHostIO` among the remap instructions with one-word templates (\"rd becomes 40; e.g. `ret` becomes `JALR v40, ra, 0`\"), and notes that their own throwaway-register paths never arise after the generic remap and give rd = 40 anyway. **That matches the source exactly.** EBREAK is handled internally, but its one row is `JAL v40, 0` all the same (F2), because the case allocates v40 itself.\n")
w("**What 4.19.4 should add** (none of it changes the mechanism):\n")
w("- *\"The remapped rd is written like any register: a JAL or JALR with rd = x0 leaves its link in v40, which is not an inline temporary and keeps that value across instructions; the record shows rd = 40 and the link.\"* Today the text could be read as the write being thrown away.\n- *\"Exempt instructions may produce rows that name x0 as rd: `LD x0` (LR.D), `VirtualPextSigned x0` (LR.W) and `XORI x0` (SC.W, SC.D). Register 0 drops the write (4.19.7), and an LD to x0 reports zero memory values (F4).\"*\n- *\"EBREAK's one row is `JAL v40, 0`, which ends the program (F1).\"*\n- The line references in 4.19.4 still hold at 4f36b3d (`:19–55`, `:57–102`, `:104–106`, `:140`, `:148–153`, `:183`, `:307–319`, `:1195–1226`).\n")
# ---------------- section 6: producers
w("## 6. Slot forms and their producers\n")
w("For each row opcode: every slot form (rd·rs1·rs2, R register, I immediate, x0 when the register is always register 0), the source instructions whose expansion emits it (from running `expand_virt_seq` on every source opcode, with rd ≠ x0 and rd = x0, and for the immediate shifts the amounts 0, 3, 31, 32 and 63), the static emit sites, and, for opcodes that are sometimes their expansion's last row, the sources where they are. Inline sites are counted per file. This is the table item 34's enumeration can reuse.\n")
w("| code | name | form | produced by | emit sites | last row in |")
w("|---|---|---|---|---|---|")
def sitestr(n):
    s=sites.get(n,[])
    vc=[x.split(':')[1] for x in s if x.startswith('virtual.cpp')]
    files=collections.Counter(x.split(':')[0] for x in s if not x.startswith('virtual.cpp'))
    parts=[]
    if vc: parts.append('`virtual.cpp:'+','.join(vc)+'`')
    for f,c in sorted(files.items()): parts.append(f'`{f}` ×{c}')
    return '; '.join(parts) if parts else ''
passthru={'SLTI','BEQ','BNE','BLT','BGE','BLTU','BGEU','SUBW','ADDW','ADDIW','VirtualRev8W','VirtualAssertEQ','ADD','ADDI','SUB','XOR','XORI','OR','ORI','AND','ANDI','SLT','SLTU','SLTIU','AUIPC','LUI','MUL','MULHU','SD','LD','MULW'}
for c in sorted(names):
    n=names[c]
    if n not in summ: continue
    first=True
    for f,srcs in summ[n]['forms'].items():
        m=re.match(r'rd=(\S+) rs1=(\S+) rs2=(\S+)',f)
        form='·'.join(x.replace('R(x0)','x0') for x in m.groups())
        st=sitestr(n)
        if n in passthru: st=(st+'; ' if st else '')+'as itself: `virtual.cpp` default (`:1227–1229`)' if n in srcs else st
        li=summ[n]['lastin'] if summ[n]['last']=='sometimes' else summ[n]['last']
        w(f"| {c if first else ''} | {('`'+n+'`') if first else ''} | {form} | {', '.join(srcs)} | {st if first else ''} | {li if first else ''} |")
        first=False
w("\nNOP 0 has no producer: it is the zero-filled padding row (F10).\n")
# row counts per source (base variant)
cnt=collections.Counter()
cur=None
for l in open('/tmp/h/enum.txt'):
    if l.startswith('== '): continue
    m=re.match(r'\s+(.*?) \| src=(\S+)(.*?) \| (\w+)\s+x(\d+)',l)
    if m and m.group(3).strip()=='' : cnt[m.group(2)]+=int(m.group(5))
w("## 7. Opcodes that occur only as source instructions (74)\n")
w("Each is replaced by its expansion (a ROM template or, for the system instructions and inlines, controller-fed words) and never appears in a row. Rows: the expansion's length for a representative instance (rd ≠ x0), from the harness. Where `execute_instruction` has a case for the opcode, it is dead code for rows.\n")
so=[
(10,'SLL','2 rows: `VirtualPow2` + MUL (`virtual.cpp:602`); dead case `trace_owner.cpp:201–206`'),
(11,'SLLI','1 row: `VirtualMULI` by 2^(shamt mod 64) (`:596`); dead case `trace_owner.cpp:202–206`'),
(12,'SRL','2 rows: `VirtualShiftRightBitmask` + `VirtualSRL` (`:589`); dead case `trace_owner.cpp:207–212`'),
(13,'SRLI','1 row: `VirtualSRLI` with the mask 2⁶⁴ − 2^shamt (`:570`); dead case `trace_owner.cpp:208–212`'),
(14,'SRA','2 rows: `VirtualShiftRightBitmask` + `VirtualSRA` (`:635`)'),
(15,'SRAI','1 row: `VirtualSRAI` (`:565`)'),
(20,'LB','4 rows via `expand_load`: `VirtualAlignAddr`, LD, `VirtualWindowMaskB`, `VirtualPextSigned` (`:511`)'),
(21,'LH','5 rows: `VirtualAssertHalfwordAlignment` + `expand_load` with mask H, signed (`:521`)'),
(22,'LW','5 rows: `VirtualAssertWordAlignment` + `expand_load` with mask W, signed (`:533`)'),
(23,'LBU','4 rows: `expand_load` with mask B, `VirtualPext` (`:516`)'),
(24,'LHU','5 rows: alignment assertion + `expand_load`, unsigned (`:527`)'),
(25,'SB','`expand_narrow_store`: read-modify-write of the aligned doubleword through LD and SD (`:559`)'),
(26,'SH','as SB, with the halfword alignment assertion (`:552`)'),
(27,'SW','as SB, with the word alignment assertion (`:545`)'),
(38,'ECALL','7 controller-fed rows writing mepc/mcause/mtval/mstatus and a JALR through mtvec (`:1098`); dead case `trace_owner.cpp:291–293`'),
(39,'EBREAK','1 row, `JAL v40, 0` (`:1164`; F2)'),
(40,'CSRRW','CSR virtual-register moves (ADDI) (`:1116`)'),
(41,'CSRRS','CSR read and OR into the CSR virtual register (`:1136`)'),
(42,'MRET','1 row, a JALR through mepc (`:1158`)'),
(44,'MULH','7 rows: 2 × `VirtualMovsign`, 2 × MUL, MULHU, 2 × ADD (`:485`; F9)'),
(46,'MULHSU','through MULHU, MUL and sign fix-ups (`:490`)'),
(47,'DIV','advice (`VirtualAdviceDiv`) plus checks (`:322`)'),
(48,'DIVU','advice (`VirtualAdviceDivU`) plus checks (`:365`)'),
(49,'REM','advice (`VirtualAdviceRem`) plus checks (`:346`)'),
(50,'REMU','advice from `VirtualAdviceDivU` (the quotient) plus checks (`:381`)'),
(51,'UNIMP','**removed**, not replaced: `num_cycles` = 0 and the bytecode builder skips it (`virtual.cpp:1318`, `1400–1404`); executing it aborts (`trace_owner.cpp:657–661`)'),
(55,'LWU','5 rows: alignment assertion + `expand_load`, mask W, unsigned (`:539`)'),
(56,'SLLW','2 rows: `VirtualPow2W` + MULW (`:609`)'),
(57,'SLLIW','1 row: `VirtualMULIW` by 2^(shamt mod 32) (`:616`)'),
(58,'SRLW','2 rows: `VirtualShiftRightBitmaskW` + `VirtualSRLW` (`:621`)'),
(59,'SRLIW','1 row: `VirtualSRLIW` with mask 2³² − 2^shamt (`:629`)'),
(60,'SRAW','2 rows: `VirtualShiftRightBitmaskW` + `VirtualSRAW` (`:581`)'),
(61,'SRAIW','1 row: `VirtualSRAIW` (`:575`)'),
(66,'DIVW','advice (`VirtualAdviceDivW`) plus checks (`:434`)'),
(67,'DIVUW','advice (`VirtualAdviceDivUW`) plus checks (`:395`)'),
(68,'REMW','advice (`VirtualAdviceRemW`) plus checks (`:462`)'),
(69,'REMUW','advice from `VirtualAdviceDivUW` plus checks (`:416`)'),
(70,'LR_W','reservation in v32 and a word load (`:665`); exempt from the rd = x0 rewrite'),
(71,'LR_D','reservation in v33 and an LD (`:649`); exempt (its LD may name x0, F4)'),
(72,'SC_W','reservation check and a conditional word store (`:712`); exempt'),
(73,'SC_D','reservation check and a conditional SD (`:682`); exempt'),
]
amos=['AMOSWAP_W','AMOSWAP_D','AMOADD_W','AMOADD_D','AMOXOR_W','AMOXOR_D','AMOAND_W','AMOAND_D','AMOOR_W','AMOOR_D','AMOMIN_W','AMOMIN_D','AMOMAX_W','AMOMAX_D','AMOMINU_W','AMOMINU_D','AMOMAXU_W','AMOMAXU_D']
for a in amos: so.append((code[a],a,'load, operation, store through LD and SD (the word forms through the window helpers) (`virtual.cpp:750–1017`)'))
inl=['KECCAK256','KECCAK256_ABSORB_PERMUTE','SHA256_INLINE','SHA256_INIT_INLINE','BLAKE2_INLINE','BLAKE3_INLINE','BLAKE3_KEYED64_INLINE','BIGINT256_MUL_INLINE','SECP256K1_MULQ_INLINE','SECP256K1_SQUAREQ_INLINE','SECP256K1_DIVQ_INLINE','SECP256K1_MULR_INLINE','SECP256K1_SQUARER_INLINE','SECP256K1_DIVR_INLINE','SECP256K1_GLVR_ADV_INLINE']
for a in inl: so.append((code[a],a,'an inline: controller-fed template from `inlines/` (`virtual.cpp:1018–1092`)'))
assert len(so)==74, len(so)
w("| code | name | rows | reason |\n|---|---|---|---|")
for c,n,r in sorted(so):
    w(f"| {c} | {n} | {cnt.get(n,0)} | {r} |")
w("\n## 8. Opcodes that never occur (3)\n")
w("- **126 `VirtualMove`**: nothing emits it (no emit site in `isa/virtual.cpp` or `inlines/`); its `execute_instruction` case (`trace_owner.cpp:649–653`, rd = rs1) is dead.\n- **161 `VirtualAdviceRemU`**: REMU takes its advice from `VirtualAdviceDivU`, the quotient (`virtual.cpp:385`), so no remainder advice is emitted; the dead case (`trace_owner.cpp:554–562`) computes the quotient anyway (F7).\n- **165 `VirtualAdviceRemUW`**: REMUW likewise uses `VirtualAdviceDivUW` (`virtual.cpp:424`); dead case `trace_owner.cpp:620–628`.\n")
# ---------------- section 9: vectors
w("## 9. Test vectors\n")
w("**Every vector below came from the compiled harness** (`/tmp/h/vec.cpp`): it `#include`s `trace_owner.cpp`, builds an `InterpreterState` over a small bytecode, and calls the tracer's own `execute_instruction<false>` (the log-driven path the replayer mirrors) for one hand-built row, each in a forked child so a failing assertion is caught. None is transcribed. What is *not* covered by the harness, because it isn't computed by `execute_instruction`: NOP (never executed, F10) and the row record's memory fields (section 3, read from `trace_owner.cpp:889–926`).\n")
w("Setup: pc = 0x0000_0000_8000_0100; `pc_increment` = 4 unless the note says otherwise; registers rs1 = x6, rs2 = x7 and rd = x5 (initialised to 0xdead) unless an operand is an immediate. **next** is the `finish_instruction` outcome: *sequential* (pc += `pc_increment`, `instr_idx` + 1), *transfer* (pc = new pc, `instr_idx` from the log) or *end* (pc 0, `instr_idx` 0). **log** is the number of log words popped. *ABORT* means the tracer's assertion fails (the replayer never sees such a row).\n")
def fmt(v):
    v=v.replace('0x','')
    return '0x'+'_'.join(v[i:i+4] for i in range(0,16,4))
rows=collections.OrderedDict()
for l in open('/tmp/h/vec.tsv'):
    p=l.rstrip('\n').split('\t')
    op,a,b,res,note=p[0],p[1],p[2],p[3],p[4]; rdimm=p[5] if len(p)>5 else ''
    rdv,pc,nx,lg=res.split('|')
    def opnd(x):
        k,v=x.split(' '); return f"{k} {fmt(v)}"
    inp=f"{opnd(a)}, {opnd(b)}"
    if rdimm: inp+=f", rd-slot imm {fmt(rdimm.split()[-1])}"
    rows.setdefault(op,[]).append((inp, fmt(rdv) if rdv.startswith('0x') else rdv, fmt(pc) if pc.startswith('0x') else pc, nx, lg, note))
order=[names[c] for _,cs in fam for c in cs]+['VirtualAdviceRemU','VirtualAdviceRemUW']
for op in order:
    if op not in rows: continue
    w(f"**`{op}`**\n")
    w("| inputs | rd after | new pc | next | log | note |\n|---|---|---|---|---|---|")
    for r in rows[op]: w("| "+" | ".join(r)+" |")
    w("")
w("---\n\n*Harness sources: `/tmp/h/enum.cpp` (the opcode and slot-form enumeration, output `/tmp/h/enum.txt` and `/tmp/h/summary.txt`) and `/tmp/h/vec.cpp` (these vectors, output `/tmp/h/vec.tsv`). Both are built with the repository's CMake build in `/tmp/tb` (`-DCMAKE_POLICY_VERSION_MINIMUM=3.5`), then `g++ -std=c++20 -O1 -DNDEBUG … -Wl,--allow-multiple-definition /tmp/tb/libtracer.a /tmp/tb/_deps/fmt-build/libfmt.a -lelf -no-pie`. Nothing was written into the repository.*")
open('/mnt/user-data/outputs/replayer-opcode-semantics.md','w').write('\n'.join(L)+'\n')
print('written',len(L))
