# WORKER10 notes (survive a reset)

Branch `v1.25` from `main` (79fae51). Go-ahead: owner, 2026-11-01 (plan in chat, below).

## Plan (approved)
1. Branch, NOTES, QUESTIONS.
2. Spec 4.19: 4.19 intro/4.19.1 reference (jolt-cpp 821e1bd, PR #2; EVM Zone `evm-zone-4e3451c1d8c5`, Cyclone);
   4.19.2 log rows (VirtualLoad*/VirtualStore*); 4.19.3 expansion word (`offset_imm` [63], `sign_imm` [62],
   `advice_reg` [61:56], `shift_offset`); 4.19.4 remap table (VirtualHostIO now rd0 = 0; example rewritten);
   4.19.5-6 decode classes (class 3 invalid), universal tables, ROM fill 379/512, normalized template-index key;
   4.19.7 PEXT any mask, opcode renumbering, 88 executed opcodes; 4.19.9 controller lookup by normalized key;
   4.19.11 invalid decode entry = existing invalid expansion; 4.19.12 record (imm, memory fields of the new rows).
3. Sections 9 (invariant 16), 10 (replayer row), 13 (items 34, 36, 40, 43, 56, 112, 113, 114 + new items from 137).
4. `replayer-opcodes.md` regenerated from `WORKER9/replayer-opcodes-821e1bd/`.
5. `wip/spec/CHANGES-v1.25.md` (CHANGES-v1.2 style, cite DECISIONS-v1.25 sN as **Vn**), `WORKER10/REPORT.md`.

## Owner's answers to my three points (2026-11-01)
- System instructions: follow `wip/tables/` as delivered (controller-fed), note the gap with DECISIONS s5's goal;
  WORKER9 is trying to fit them in the ROM: **update 4.19.5-6 if the tables change**.
- Invalid decode entries: the existing invalid-expansion error (4.19.11), no new bit.
- Tracer: only its fields of the expansion word (untraced, advice_reg).

## Facts gathered (from wip/tables at a70b9e7-ish, main 79fae51)
- Opcode table 171 opcodes (0-170); row opcodes 88 = S 74, M 5, L 9 (LD 54, SD 53, VirtualLoadB/H/W 110-112,
  VirtualStoreB/H/W 113-115, VirtualAdvice 126). Advice DIV/REM opcodes 163-170 now '-' (written as VirtualAdvice).
  VirtualMove 130 '-'. Multiply: MUL 43, MULHU 45, MULW 65, VirtualMULI 118, VirtualMULIW 119.
- ROM 379/512: 2 fixed + 42 one-word + 147 multi-word (non-atomic) + 188 atomic; 89 templates over 303 decode entries.
- Decode table: 303 ROM, 20 controller (15 inline + 5 system indices), 1,725 invalid (class 3).
- Fed: 78 numbers (15 inlines, 63 system keys), 13,830 words. Key normalization: inline register fields cleared;
  system renamed by first appearance (rd then rs1; x0 stays 0).
- rd0 per template: 0 = branches, FENCE, LR/SC, stores, VirtualAssertEQ, **VirtualHostIO (was remap)**;
  2 = AMOs, JAL, JALR, LB..LWU, LD; 3 = ADDI; 1 = the rest.
- Load template: VirtualLoadX t0, RS1, IMM (log, sign) / VirtualWindowMaskX t1, RS1, IMM / VirtualPextSigned|Pext RD, t0, t1.
- Store template: VirtualLoadX t0,RS1,IMM / WindowMaskX t1,RS1,IMM / ANDN t0,t0,t1 / ShiftDataX t1, RS2, RS1 (offset_imm, sign)
  / ADD t0,t0,t1 / VirtualStoreX IMM, RS1, t0 (log, sign).
- ShiftData row: all three slots registers; its immediate (record) = shift_offset = source imm if offset_imm, else 0.

## Status
See bottom: per-turn log.

## Turn log
- T1 (2026-11-01): read inputs, plan approved; branch created; NOTES/QUESTIONS started.
- T2 (2026-11-01): committed T1's WIP; merged main (V6: CSR controller-fed, V7: Q1-Q4). Owner answered Q4-Q7.
  Done: 4.19.7-4.19.15, sections 9, 10, 13 (items 137-141), appendix regenerated (ed scripts were in /home/claude/ed,
  not kept: section 2 rows rebuilt from WORKER9 summary.txt + case ranges in PR #2's trace_owner.cpp, sections 6-9 from
  WORKER9 outputs), CHANGES-v1.25.md, REPORT.md. Remaining: item 137's re-citation; follow WORKER11's findings and
  WORKER9's Tracer tables if the replayer's tables change.
- T2: merged main's K change (DECISIONS-v1.2 s49) cleanly; v1.25's text equals v1.2's there, so no CHANGES-v1.25 entry (WORKER10/NEXT.md done).
- T3 (2026-11-01): on main now (v1.25 branch done). Fixed from WORKER13: 4.17.1 types (D46), the renumbering stated exactly (appendix 2.0, 61 row opcodes), 8.1 load timing (D45); CHANGES-v1.25 lists them (two as v1.2 errata).
