# Decisions toward v1.25

**v1.25 is an intermediate milestone between v1.2 and v1.3** (owner, 2026-10-31): **a working clone of current
jolt-cpp.** It is v1.2 with the replayer and the Tracer's tables brought up to jolt-cpp's current expansions, so v1.3
(the Tracer) starts from a machine that matches today's Jolt. Same style as the other logs: one entry per decision,
later entries may revise earlier ones. The analysis behind it is the v1.3 log's section 9.

---

## 1. Scope (2026-10-31)

Decided (O). Starts once v1.2 is frozen; ends with v1.25 frozen as `v1.25/`.

- **Pin a current jolt-cpp `main` commit** (not a moving branch), and diff its expansions against v1.2's reference
  (jolt-asic-planning `4f36b3d`) in full.
- **The replayer** (spec 4.19, meshsim's model): rebuild the opcode table, the ROM templates and the decode table,
  re-checking the template count and the ROM fill; **add `shift_offset` to the expansion word**; add the new opcodes
  (`VirtualLoadB/H/W`, `VirtualStoreB/H/W`) and the changed ones (`ShiftData*`'s operands, the narrow memory rows'
  immediate, `VirtualHostIO`, the AMO\*.W bodies, the renumbering; v1.3 section 9 as corrected by WORKER9); **PEXT correct for any
  mask** (v1.3 section 9: the spec requires it, the implementation chooses how, full speed promised only for
  contiguous masks; replacing v1.1's narrowing and invariant 16); drop `VirtualAlignAddr` and the
  alignment assertions; regenerate the replayer's test vectors.
- **The Tracer's tables: rebuilt too** (owner): its ROM and decode table (v1.3 section 7) and its advice templates (v1.3
  section 8, `wip/tracer/`), against the same pinned commit, though nothing executes them until the Tracer exists.
- **The spec** (4.19, the expansion word, the opcodes, the parameters) and **CHANGES** for v1.25.

## 2. Acceptance (2026-10-31)

Decided (O). **v1.25 matches the jolt-cpp build on our two 2²⁹-row runs:** **EVM Zone** (trace-29) and **Cyclone**
(input/29), the two workloads v1.1's replayer was measured on (planning notes, item 113's guests).

- **Every window of both**, replayed on meshsim's replayer from run logs made at the pinned jolt-cpp commit, compared
  **row for row** (by row hash) against that jolt-cpp build's trace. This is v1.1's planning to-do 7 ("full-program
  replay on the runner") and spec item 58's validation harness, at the new reference. Run on the Quest runner.
- The Tracer's rebuilt tables are checked the same way as far as possible without a Tracer: WORKER7's harness style,
  against the pinned jolt-cpp's own expansion and advice code.

## 3. Deliveries (2026-10-31)

Decided (O).

- **A delivery is the Tracer's output, made in software** until the hardware Tracer exists: per program the tables; per
  window the state record, the three streams (raw, compressed, the Tracer's pick) and jolt-cpp's reference rows
  (meshsim's `REPLAY-INPUT-REQUIREMENTS.md`). v1.1's deliveries 1-3 (240 windows, 15.7 M rows, all matched) were made by
  a v1.1-era session's software stand-in; **neither the tool nor the files were kept.**
- **v1.2 freezes without real-window runs:** v1.1's replayer matched the 240 windows, v1.2 changed its interface rather
  than its row logic, and WORKER4's StageB synthetic run checks the new interface against Stage A on 18 windows covering
  every compression and store-log combination, a controller-fed expansion and a mid-sequence start.
- **v1.25 rebuilds the delivery writer** (WORKER9's part 3, in jolt-asic-planning PR #2, next to the tracer and the table
  builder): a functional model of the Tracer's output, which v1.25's acceptance runs need and which gives v1.3 a head
  start. Deliveries are not committed to any repository.

## 4. The expansion word's new bits, and the tables (2026-11-01)

Decided (O). From WORKER9's part-2 plan (`WORKER9/PLAN-PART2.md`).

- **`offset_imm`** (WORKER9's proposal, spare bit [63]): when set, the row's `shift_offset` is the source instruction's
  immediate; when clear, 0. That covers every current use (the stores use their immediate; SC.W and the AMOs use 0).
- **`sign_imm`**, a second spare bit (position proposed by WORKER9 with the encoding): when set, the row's immediate is
  sign-extended; when clear, zero-extended. **It replaces the replayer's hard-wired list of signed-immediate opcodes**
  (spec 4.19.12). The table builder sets it from jolt-cpp's own `signed_immediate`, which at `821e1bd` adds `ShiftData*`,
  `VirtualLoad*` and `VirtualStore*` to the list. A future change to Jolt's list then means rerunning the builder, not
  changing the hardware (as for PEXT, v1.3 section 9). A bit rather than a new operand code, because the free operand
  codes (146-255) are the Tracer's untraced registers (v1.3 section 7).
- **The table builder is an offline tool** (`build_tables`, in jolt-asic-planning PR #2): run once per jolt-cpp version and
  per program; its outputs (`rom.bin`, `decode_table.bin`, `templates.bin` and its index, the opcode table) are committed
  to meshsim's `wip/tables/`. At run time a tile's controller copies the ROM and the decode table into the replayer
  (part 0) and keeps `templates.bin` to feed the rare templates.
- **Two sets of tables, one format** (v1.3 sections 7, 8, 10): **the replayer's** (traced rows only) and **the Tracer's**,
  whose traced rows are identical to the replayer's and which add untraced rows for **every advice** (the division family;
  `VirtualAdvice` in the secp256k1 inlines, the division inlines' modular inverse included). Each unit has its own ROM
  and fed set: the replayer's ROM is expected at about 389 of 512 words (v1.1 about 508), so the Tracer's 389 plus its 16
  division-advice words fit too (v1.3 Q11 likely resolved).
- **The mapping between the two numberings:** the Tracer writes each window's template bitmap in the **replayer's**
  numbering of controller-fed templates (v1.3 section 6), so the builder emits that mapping alongside the Tracer's tables.
- **Long data-independent expansions** (the modular inverse) are emitted as **a loop body plus a repeat count**, with any
  prologue and epilogue, for C&C 3 to generate (v1.3 section 10).

## 5. Universal tables, and the expansion word's last bits (2026-11-01)

Decided (O). From WORKER9's first delivery of the replayer's tables and its Q5-Q7.

- **The tables are universal, not per program** (owner): one ROM and one decode table covering **every RV64IMAC
  instruction**, and the controller-fed set covering **every inline jolt-cpp supports**, built once per jolt-cpp version
  and good for any guest program. Trimming the tables to what one program uses was rejected: a new guest would need the
  builder rerun before it could run. (WORKER9's first delivery was per program: EVM Zone 109 ROM words and 35 fed
  templates, Cyclone 186 and 23; every ROM-able template together fits in 379 of 512.) EVM Zone and Cyclone remain the
  real-program checks; every opcode is also checked against jolt-cpp with random operands.
- **Unused decode-table entries are invalid** (revises WORKER9's Q7, class 2): with universal tables they are encodings
  that aren't valid RV64IMAC instructions, which Jolt itself rejects, so they are an error, not a controller-fed template.
- **The goal: every expansion except the inlines and the advice generators fits in the ROM** (owner, 2026-11-01). The
  controller then feeds only what is inherently special; everything executed routinely, the atomics (.W and .D, LR and SC)
  and the system instructions (`ECALL` and the like) included, is in the ROM. In v1.1 the ROM was nearly full (about 508 of
  512), so the longest expansions were controller-fed; the new AMO\*.W bodies are 8-13 rows (from 18-24) and the ROM has
  room. **If it doesn't fit**, the builder reports how many words short it is and which expansions are largest, and the
  owner decides between a larger ROM (12 KB, say) and what to feed.
- **Q5, `sign_imm` at [62]** (WORKER9's proposal, accepted): the expansion word had one spare bit (taken by `offset_imm`),
  so the Tracer's `advice_reg` narrows from 7 bits to 6, [61:56]: a traced row reads its advice only from bank-1 registers
  146-209 (v1.3 section 7). The advice expansions put their results there and use 210-255 for scratch.
- **Q6, `seq_remaining` counts traced words only** (WORKER9's proposal, accepted; reverses WORKER7's Q4): it is 12 bits,
  too narrow for the modular inverse's 53,000-110,000 untraced words. Untraced words are marked, so the Tracer needs no count
  to recognise them, and the Tracer's traced words become bit-identical to the replayer's. Revises v1.3 section 10: the
  length C&C 3 sends up front counts traced words only.

## 6. What stays controller-fed (2026-11-01)

Decided (O). From WORKER9's part 2 (its REPORT, "system instructions in the ROM").

- **`ECALL`, `EBREAK` and `MRET` are in the ROM**, bringing it to 388 of 512 words.
- **The CSR instructions (`CSRRW`, `CSRRS`: 60 templates, 102 words) stay controller-fed**: the one deliberate exception to
  section 5's goal. They would fit, but jolt-cpp's expansion depends on **which CSR** and on **the register pattern**
  (rd = x0, rs1 = x0, rd = rs1), which a single decode entry can't see. Putting them in the ROM would need a special decode
  layout for the SYSTEM group (a loaded CSR-id table and a computed register pattern) or new replayer datapath; CSR
  instructions are rare in guest code, so the controller feeding them costs almost nothing. **Rare is good** (owner).
- So **the controller feeds the inlines, the advice generators and the CSR instructions**, and nothing else.

## 7. From meshsim's replayer at v1.25 (2026-11-01)

From WORKER11's questions; the integrator's answers, to review.

- **Q1, signedness in the reference rows: a flag in the row** (revised 2026-11-01, from WORKER10's finding). The row's
  immediate is already 64 bits; what `sign_imm` determines is how Jolt forms its **128-bit `imm()`** from it, so extending
  the stored immediate can't carry the information, and the row needs **a spare bit saying it**: **word 0 bit [86], `imm_signed`**,
  equal to the expansion word's `sign_imm` (confirmed by WORKER10 in the v1.25 spec: word 0 [127:86] was spare and the 80-byte
  row has no other spare bits). The replayer model and WORKER9's delivery writer follow exactly.
- **Q2, the write log's last-load entry (E[15]) is updated by every load row**, the new `VirtualLoad*` rows included. The
  delivery writer's encoder does the same.
- **Q3:** `offset_imm` is set only on `ShiftData*` rows (jolt-cpp records their immediate as the shift offset); meshsim flags it
  on any other opcode.
- **Q4:** a misaligned memory row (LD and SD included) is a violation meshsim flags, since jolt-cpp aborts on one.
- **The acceptance runs' deliveries** *(revised 2026-11-01, from WORKER9's part 3)*: generating any window needs the whole
  program traced from the start, but **the delivery writer produces and proves a whole 2²⁹ program in about 17 minutes on one
  CPU** (EVM Zone: all 8,192 windows, all three variants, 1,064,585,826 rows, peak 1.44 GB), inside the runner's 45-minute
  limit. So **one writer task per program produces its delivery on the runner box and keeps it there** for the acceptance runs,
  whose shards read their windows from it; it is discarded afterwards (about 31 GB per program; disk space on the box to check).
  Nothing is committed.

---

## Definition of done (owner, 2026-11-01)

**Both v1.2 and v1.25 end with a full spec and a fully running simulator**, each frozen as its own folder, and a delta
file for what changed between them.

**v1.2, frozen as `v1.2/`** (the old tables and expansions, jolt-asic-planning `4f36b3d`):
- the spec, `machine-spec.md` v1.2, and `CHANGES-v1.2.md` (written);
- meshsim fully running: WORKER4's phases 5-6 (the meshsim-only checks; the recipes as suites), the sort (WORKER8, done,
  branch `v1.2-sort`) and the replayer (StageB synthetic, section 3);
- `v1.2-meshsim` and `v1.2-sort` merged into `main`, a full `run_tests.sh` and the full-size sort on the runner, then the
  freeze.

**v1.25, frozen as `v1.25/`** (jolt-cpp `821e1bd`): **the spec, the simulator and the software** (owner, 2026-11-01).
- **The spec:** 4.19 updated (the six new opcodes and the changed ones, `offset_imm`, `sign_imm`, PEXT for any mask, the decode
  classes, universal tables, the normalized template-index key), the parameters, invariants and open items. *Done (WORKER10;
  on `main`).*
- **The tables:** the universal replayer and Tracer tables (WORKER9 part 2, `wip/tables/`). *Done.*
- **The simulator:** meshsim's replayer model updated to match and running the universal tables (WORKER11, `v1.25-meshsim`).
- **The software:**
  - the replayer's controller software, `replay_ctl.c`: the normalized lookup and the universal tables (WORKER11);
  - the recipes: the replayer recipe (26) and its test-side `template_lookup` for the normalized key; the rest of the book is
    unchanged (WORKER11's scope, since it owns the replayer's software);
  - the sort: unchanged, re-run on v1.25 to confirm;
  - the table builder and the delivery writer (WORKER9, jolt-asic-planning PR #2).
- **Acceptance** (section 2): every window of EVM Zone and Cyclone at 2²⁹, matched row for row against jolt-cpp, on the runner
  (WORKER9's deliveries, Quest 079-080; WORKER11's kit).
- **`CHANGES-v1.25.md`**, the delta from v1.2 (done), and **a change guide from v1.1 to v1.25** for the RTL, which is upgraded from
  v1.1 straight to v1.25 (the v1.2 waypoint stays frozen as `v1.2/`, useful in its own right): `CHANGES-v1.2.md` and
  `CHANGES-v1.25.md` folded together by spec section, so each unit's changes are in one place (integrator, once the spec is final).

## Work list

| # | task | from | status |
|---|---|---|---|
| W1 | **Bring the replayer and the Tracer's tables up to date with jolt-cpp's latest** (1), and pass the acceptance runs (2) | v1.3 log, section 9 and its former W1 | the tables (WORKER9 part 2) and the delivery writer (part 3) under way; the spec and meshsim's replayer after v1.2 is frozen |
| W2 | **`CHANGES-v1.25.md`**: the delta from v1.2 | owner | done (WORKER10) |
| W3 | **The change guide to v1.25, from v1.1 and from v1.2** (`wip/spec/CHANGES-to-v1.25.md`), for the RTL | owner | WORKER13 |
| W4 | **The replayer recipe** (26) and its `template_lookup` for the normalized key | owner | done (WORKER11, on `v1.25-meshsim`): keyed lookup, the book regenerated, the key cross-checked against the Java model on 20,000 words |
