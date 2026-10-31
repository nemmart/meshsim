# WORKER9 notes

Working log for PROMPT.md (the standalone tracer at current jolt-cpp, jolt-asic-planning PR #2).

## Pins

- jolt-cpp `main` pinned at **`821e1bd2f433d6ca62300afbbbba031db50e7dc9`** (2026-10-30 23:31 UTC, #1523), owner-approved.
- PR #1: jolt-asic-planning `tracer-standalone` at `4f36b3d`, copied from jolt-cpp `4ab349bb` (81 commits behind).
- Branch for PR #2: `tracer-standalone-main`, from `4f36b3d`.

## Source changes 4ab349bb -> 821e1bd in the copied paths

Commits that touch `src/tracer`, `src/utils`, `src/benches/trace_elf.cpp`:

| commit | date | effect on the CPU tracer |
|---|---|---|
| `981607779` | 10-20 | `trace_elf --count-cycles-only` |
| `612a4184c` | 10-22 | secp256k1 inline advice: modular inverse via vendored libsecp256k1 `modinv64_var` (new C file + `third-party/secp256k1/`); same values, faster |
| `81fc7e35e` | 10-24 | **`VirtualHostIO` is operand-free** (port of a16z/jolt#2101): no virtual rd, no register reads/writes; decoder accepts it only with rd = 0 |
| `e1167cb5d` | 10-29 | **the memory-op port**: `VirtualLoadB/H/W`, `VirtualStoreB/H/W`; shorter LB..SW, LR.W/SC.W, AMO*.W; `shift_offset`; alignment asserts and `VirtualAlignAddr` gone |
| `ca2c8533f`, `c0e1ed703` | 10-30 | GPU-only helpers (`rd_inc_magnitude`, `gpu_utils.cuh`); no CPU behaviour change |

`52fbbab` (DECISIONS-v1.3 s9's reference) .. `821e1bd`: only `trace_owner.hpp` (a helper) and `gpu_utils.cuh`. So s9's
analysis base and our pin agree on expansions; the extra items below were already there at `52fbbab`.

## Findings beyond DECISIONS-v1.3 s9 (for REPORT.md)

1. **`VirtualPext`/`PextSigned`, `VirtualWindowMaskB/H/W`, `VirtualShiftDataB/H/W` are not new.** They exist at
   `4ab349bb` and the old LB..SW expansions already used them. Truly new opcodes: only `VirtualLoadB/H/W`,
   `VirtualStoreB/H/W`. Removed: `VirtualAlignAddr`, `VirtualAssertHalfwordAlignment`, `VirtualAssertWordAlignment`.
2. **`VirtualShiftData*` changed semantics**: operand 2 was the computed address (register); now it is the *base*
   register and the offset is `shift_offset` (lane = (base + shift_offset) & 7). Its row `imm` is `shift_offset`,
   signed.
3. **Signed-immediate set**: `imm()` is signed for loads, stores (now incl. the Virtual ones), branches and
   `VirtualShiftData*`; everything else is the u64 bit pattern. (Old set: loads/stores/branches only.)
4. **Narrow memory rows carry the access's immediate**: `VirtualLoadX rd, rs1, imm` / `VirtualStoreX rs1, rs2, imm`,
   address = floor((rs1 + imm), 8). Old: `LD`/`SD` with imm 0 on an aligned virtual register.
5. **Misalignment is now a tracer abort** (`checked_memory_address` -> `release_assert`), not a trace row.
6. **`VirtualHostIO`** (commit `81fc7e35e`): one row as before, but operand-free (rd = rs1 = rs2 = 0, all imm),
   no virtual register allocated, no rd write. Row *content* change, not count.
7. **AMO*.W bodies changed beyond length**: old value from `VirtualPextSigned` (sign-extended); result via
   `ADDI rd, old, 0` (was `VirtualSignExtendWord`); AMOMAXU/MINU.W now sign-extend rs2 with `VirtualSignExtendWord`
   (was `VirtualZeroExtendWord` on both); unsigned order is preserved by sign-extension from 32 bits.
8. **Opcode enum renumbered**: 6 inserted before `VirtualSignExtendWord`, 3 removed. Any table keyed by jolt-cpp's
   numeric opcode shifts.
9. `VirtualInstruction` gains `i32 shift_offset` at offset 12 (former padding); RiscCycle stays 80 B.

To do: a mechanical check (dump every opcode's expansion at both commits and diff), so the list is complete.

## Programs

- **Cyclone**: unchanged in jolt-cpp (`examples/cyclone/`).
- **EVM Zone**: jolt-cpp replaced `examples/evm-zone/` with `examples/evm-zone-4e3451c1d8c5/` (commit `8a88527c4`,
  10-28: new guest release, new inputs; 2^29 input is `trace-29-4599adc50712`, 358,589,994 cycles in the job
  metadata). PR #1's `trace-29-e23d2a1c5937` no longer exists in jolt-cpp.

## Build

- Needs C as well as C++ now (`secp256k1_inverse.c`), and jolt-cpp's vendored `third-party/secp256k1/`
  (bitcoin-core v0.7.0 subset, MIT) as a private system include. Copied verbatim to `tracer/third-party/`.
- Built clean in the 1-CPU sandbox (~2 min). `vm.overcommit_memory=1` can be set here (root).

## capture_writes

- Was: writes = `SD` rows, loads = `LD` rows, anything else counted as an error. Now: `isa::is_store` / `isa::is_load`
  (which include the Virtual narrow ops); JSON adds `num_writes_sd`, `num_loads_ld`.
- Expectation: write and load *counts* and the (address, value) stream unchanged against PR #1 for the same program
  (each narrow store still writes one merged doubleword); only cycle indices shift.

## Turn 2 (2026-10-31)

- Pushed `tracer-standalone-main` (3 commits on `4f36b3d`: re-copy, `capture_writes`, EVM Zone program).
- Owner: v1.25 acceptance uses `evm-zone-4e3451c1d8c5` / `trace-29-4599adc50712` and Cyclone as jolt-cpp has it; also
  run the old EVM program on the new tracer to isolate the expansion change.
- Owner: PEXT must be correct for any mask (full speed only for contiguous) - for part 2.
- Small check, old EVM trace-23-f26e97fcfdbb on the new build: 4,775,361 -> 4,278,784 rows; writes 543,845 (same);
  `--validate` OK. 438,993 of the writes are SD, 374,110 of 715,431 loads are LD.

## Turn 3 (2026-10-31)

- Pushed README (3a71bda) and `expansion-diff/`; `source-provenance.json` (83f83c8); **draft PR #2 opened:
  https://github.com/Quarky93/jolt-asic-planning/pull/2** (base `tracer-standalone`).
- REPORT.md (part 1) and QUESTIONS.md (Q1-Q4) pushed. Task 069 to be released by the owner.
- Prompt now has part 3 (delivery writer; a rebuild). Owner: note `trace_owner.hpp`'s signedness change for the
  table builder.
- PLAN-PART2.md: proposed plan, awaiting go-ahead.

## Turn 4 (2026-11-01): part 2 started

- Owner approved part 2 (DECISIONS-v1.25 s4, v1.3 s10): `offset_imm` [63]; `sign_imm` as a bit (position mine);
  Tracer computes all advice (division family + all seven secp256k1 inlines' VirtualAdvice, incl. the modular inverse
  as loop body + repeat count, Fermat p-2); emit the replayer<->Tracer template-number mapping; Q3 yes; Q4 fine.
- The expansion word has only one spare bit ([63]). Proposal: `sign_imm` at [62], `advice_reg` narrowed to [61:56] =
  register - 146 (bank-1 registers 146-209). To record in QUESTIONS.
- `tracer/tables/build_tables.cpp` (pushed): derives templates by expanding sampled words through `expand_virt_seq`
  and mapping slots to codes; a resolver mirroring Replayer.java checks them. **Universal check: every decode index,
  85,363 random words (rd/rs = x0 edge cases included), 0 failures.** ROM if every ROM-able template were resident:
  379 of 512 words (89 distinct templates over 303 decode entries); 105 per-word controller templates; 87 row opcodes.
- Found and fixed in my code: S-format immediate zero-extension (u32 promotion); CSR templates are per word (their
  expansion depends on rs1 = x0), so controller-fed templates are derived from the exact word.
- Open: seq_remaining is 12 bits, but the Tracer's inverse expansion is 53k-110k words and WORKER7 Q4 counts untraced
  words in seq_remaining. Needs a ruling (to QUESTIONS).
- Per-program replayer tables built and delivered to `wip/tables/` (f05585f): EVM Zone 109 ROM words / 35 fed,
  Cyclone 186 / 23; every instruction of both programs (616,231 and 545,760) resolved from the files and compared
  with jolt-cpp's bytecode rows: 0 failures. Opcode table: 88 row opcodes of 171. QUESTIONS Q5 (sign_imm [62],
  advice_reg [61:56]), Q6 (seq_remaining counts traced words), Q7 (unused entries class 2).
- Next: mutants; regenerate tools/replayer-opcodes (enum/vec) at 821e1bd; the Tracer's variant (division family
  re-run, secp256k1 advice incl. the Fermat inverse as loop + count, the numbering map); REPORT part 2.

## Turn 5 (2026-11-01)

- Owner (v1.25 s5): universal tables; atomics in ROM; unused entries invalid; Q5/Q6 accepted.
- Universal replayer tables delivered (a70b9e7): ROM 379/512 (atomics 188), 78 fed templates (key normalization:
  inlines registers cleared, system renamed by first appearance), 1,725 invalid entries; 117,843 random words + both
  programs, 0 failures. Mutants 497/500 caught. Appendix outputs at 821e1bd (Q3). WORKER7's div harness at 821e1bd:
  identical. REPORT part 2 (in progress) written.
- Next: Tracer variant (secp256k1 advice incl. Fermat loop and GLV), mapping, checks.

## Turn 6 (2026-11-01)

- ECALL/EBREAK/MRET into the ROM (388/512). CSR: 60 keys, 102 words: fit in size, but the decode index can't select
  them (CSR number and register pattern): options in REPORT, owner decides.
- Mutants 500/500 (dense sampling + x0 variants + the spec's equivalences).
- Template-index key described precisely (README, REPORT).
- Tracer tables delivered (fe39fda): ROM 404/512; secp256k1 advice as untraced programs; DIVQ/DIVR Fermat loop as
  segments; template map; all checked against jolt-cpp's advice (0 mismatches); traced words identical.
- QUESTIONS Q8 (loop vs addition chain), Q9 (untraced LD). Next: part 3 plan.

## Turn 7-8 (2026-10-31/11-01): part 3

- delivery_writer (PR #2 121c271, README 61996f8): all windows of both small inputs and both 2^29 programs replay in
  all three variants (EVM 1,014 s, Cyclone 819 s on 1 CPU). Corruption test hook catches stream, rows and ROM flips.
- Selected windows written and proved; tarball to the owner (not committed). Quest 070/071 held for the full
  deliveries (need /tmp/w9-delivery.tgz). Task 069 still held.

## Final turn (2026-10-31)

- 069b (--validate at 2^29): all three OK. 079/080: full deliveries on the box, all windows proved; WORKER11's
  acceptance (083/084) matched every row. PR #2 retitled, description rewritten, marked ready for review.
- REPORT: complete, with "Left open". Quest numbers used: 069 (failed, no tarball), 069b, 079, 080, 081, 082, 082b.
