# WORKER9 report: the standalone tracer at current jolt-cpp (jolt-asic-planning PR #2)

Part 1 (steps 1-7) is done, except `--validate` at 2^29, which is queued on the Quest runner (task 069). Parts 2 and 3
are not started; their plans go to the owner first.

## Pinned commit

**`snarkify/jolt-cpp@821e1bd2f433d6ca62300afbbbba031db50e7dc9`** (main, 2026-10-30 23:31 UTC). PR #1 was copied from
`4ab349bb` (2026-10-19), 81 commits earlier. Between DECISIONS-v1.3 s9's reading point `52fbbab` and `821e1bd` the CPU
tracer is unchanged (only a GPU helper in `trace_owner.hpp` and `gpu_utils.cuh`).

## PR #2

**https://github.com/Quarky93/jolt-asic-planning/pull/2**, draft, branch `tracer-standalone-main`, based on PR #1's
branch `tracer-standalone` so the diff is exactly the change. Commits: the re-copy; `capture_writes`; the EVM Zone
program; the README; `source-provenance.json`.

- **Re-copy** by PR #1's rules (PR #1 itself checked byte-identical to `4ab349bb` apart from its one `#include`).
  New files: `secp256k1_inverse.c/.h` and jolt-cpp's vendored libsecp256k1 v0.7.0 subset (`tracer/third-party/secp256k1/`,
  MIT); the CMake project now enables C. Dependencies otherwise as jolt-cpp pins them (fmt 11.2.0, nlohmann_json
  3.10.5, xbyak `3ae3da66`), unchanged from PR #1.
- **`capture_writes`**: writes and loads are `isa::is_store` / `isa::is_load` rows (adding `VirtualStoreB/H/W`,
  `VirtualLoadB/H/W`); the JSON adds `num_writes_sd`, `num_loads_ld`. Nothing else.
- **Programs**: jolt-cpp `8a88527c4` (10-28) replaced `examples/evm-zone/` with its current release
  `examples/evm-zone-4e3451c1d8c5/`; PR #2 uses it (owner, 2026-10-31: v1.25's acceptance uses it), with input
  `trace-29-4599adc50712`. Cyclone is unchanged in jolt-cpp and here.

## What changed against PR #1

jolt-cpp commits on the copied paths: `e1167cb5d` (10-29, the memory-op port), `81fc7e35e` (10-24, operand-free
`VirtualHostIO`), `612a4184c` (10-22, secp256k1 inverse via libsecp256k1, same values), `981607779` (10-20,
`trace_elf --count-cycles-only`), `ca2c8533f`/`c0e1ed703` (10-30, GPU-only).

**Mechanical expansion diff** (`expansion-diff/`): every non-virtual source opcode (110 at both commits) through
`expand_virt_seq` on 32 operand sets, compared by name. **21 change, 89 are identical row for row.**

| source opcode | rows 4f36b3d -> 821e1bd |
|---|---|
| LB, LBU | 4 -> 3 |
| LH, LHU, LW, LWU | 5 -> 3 |
| SB | 8 -> 6 |
| SH, SW | 9 -> 6 |
| LR.W | 9 -> 7 |
| SC.W | 26 -> 21 |
| AMOSWAP.W | 18 -> 8 |
| AMOADD/AND/XOR/OR.W | 19 -> 9 |
| AMOMIN/MAX/MINU/MAXU.W | 24 -> 13 |
| VirtualHostIO | 1 -> 1 (operands only) |

This confirms the corrected DECISIONS-v1.3 s9 / v1.25 list in full:

- **New row opcodes: only `VirtualLoadB/H/W`, `VirtualStoreB/H/W`.** No longer emitted (and removed from the enum):
  `VirtualAlignAddr`, `VirtualAssertHalfwordAlignment`, `VirtualAssertWordAlignment`. `VirtualPext`/`PextSigned`,
  `WindowMask*`, `ShiftData*` exist and are emitted at both commits.
- **`ShiftData*` operands**: was (value, computed address); now (value, base register) with the offset in
  `shift_offset` (`VirtualInstruction` byte 12, formerly padding); lane from `(base + shift_offset)`.
- **Narrow memory rows carry the access's immediate**: `VirtualLoadX rd, rs1, imm` and `VirtualStoreX rs1, rs2, imm`,
  address `floor(rs1 + imm, 8)`; previously `LD`/`SD` with immediate 0 on an aligned temporary. In stores, `WindowMask`
  now reads (rs1, imm) too (was the computed address, imm 0).
- **`VirtualHostIO`**: rd = rs1 = rs2 = 0, all immediate; no temporary, no rd write; the decoder accepts it only with
  rd = 0.
- **AMO*.W bodies**: old value by `VirtualLoadW`/`WindowMaskW`/`VirtualPextSigned`; result `ADDI rd, old, 0` (was
  `VirtualSignExtendWord`); AMOMINU/MAXU.W sign-extend rs2 (`VirtualSignExtendWord`, was `VirtualZeroExtendWord` on
  both operands), which keeps unsigned order for 32-bit values.
- **Numbering**: 6 opcodes inserted before `VirtualSignExtendWord`, 3 removed; every later opcode number shifts.

**Beyond the expansions (row semantics, `trace_owner.hpp`/`.cpp`, `riscv.hpp`), for the table builder:**

- **Immediate signedness**: `RiscCycle::imm()` is signed for `isa::signed_immediate` opcodes, i.e. loads, stores
  (now including the six Virtual ones), conditional branches **and `VirtualShiftDataB/H/W`**; all others are the u64
  bit pattern. Before: loads, stores, branches only. A ShiftData row's immediate is its `shift_offset`.
- **Misalignment** of a narrow access is no longer a trace row: `checked_memory_address` aborts the tracer.
- No other execution change: `WindowMask*`, `Pext*` and every other row opcode execute as before.

## Runs

Local, 1 CPU, 3 GB, `vm.overcommit_memory=1`:

| run | rows | padded | writes | loads | SD / LD | `ram_K` | wall | RSS |
|---|---|---|---|---|---|---|---|---|
| EVM Zone `4e3451c1d8c5` trace-29-4599adc50712 | 354,861,942 | 2^29 | 55,254,496 | 64,229,138 | 54,411,302 / 61,455,842 | 2^23 | 34 s | 1.7 GB |
| Cyclone input/29 | 371,526,406 | 2^29 | 37,036,072 | 60,395,486 | 28,284,220 / 33,729,842 | 2^24 | 19 s | 1.8 GB |
| PR #1's EVM Zone trace-29-e23d2a1c5937 | 246,549,649 | **2^28** | 31,683,025 | 46,797,908 | 26,166,540 / 29,284,686 | 2^23 | 15 s | 1.5 GB |

Against PR #1, same programs and inputs:

| run | PR #1 rows | PR #2 rows | difference | writes, loads | write stream |
|---|---|---|---|---|---|
| Cyclone input/29 | 410,144,754 | 371,526,406 | -38,618,348 (-9.4%) | identical | identical (address, value) |
| PR #1's EVM Zone trace-29 | 275,366,761 | 246,549,649 | -28,817,112 (-10.5%) | identical | identical (address, value) |

**Why**: each narrow load/store and 32-bit atomic still makes exactly one memory access, so counts and values are
unchanged and only cycle numbers move. The row difference is the shorter expansions, counted from each run's
`VirtualLoad*`/`VirtualStore*` rows (a local histogram build of `capture_writes`, not committed): PR #1's EVM Zone
exactly (7,216,723 byte, 15,482 half and 4,764,532 word loads; 4,509,094 / 7,532 / 999,859 stores; no atomics);
Cyclone to within 7 rows, which is its single 32-bit AMO (10 rows shorter; the store accounting counts 3). For the new
EVM Zone the same count puts it at 358,589,994 rows at PR #1's expansions, **exactly the cycle count in its job
metadata**. PR #1's EVM Zone trace-29 now pads to 2^28, not 2^29.

**Validation**: `--validate` (default slice and 100,000) OK on both trace-23 inputs; `trace_elf --dump-trace-txt`'s
store rows equal `capture_writes` output on both. At 2^29 `--validate` needs ~30 GB: **Quest task 069**
(`tasks/hold/069-w9-tracer-2p29-validate.sh`, input `w9-tracer.tgz` sha256 `0d46b008...47f8df`, carrying commit
`502dc8a`'s `tracer/`, PR #1's EVM program and a libelf fallback) runs `make runs` and `--validate` on all three; the
owner copies the tarball and releases it. Results will be added here.

## The run logs (step 6)

v1.1's run logs, state records, reference rows and tables were deliveries 1-3 from a separate v1.1-era session
("Jolt-cpp tracer for replayer inputs and reference rows", planning notes 2026-10-27, sections 1.3 and 2: "kept outside
the package"; `REPLAY-INPUT-REQUIREMENTS.md` and an unkept `FORMAT.md` describe them). **No writer exists** in
jolt-asic-planning (all three branches), jolt-cpp (all 708 branches; searched for the delivery's file names and the
expansion-word flag names), meshsim (main, `v1.2-meshsim`, `v1.2-sort`) or quest. meshsim's
`wip/tools/replayer-opcodes/` (`enum.cpp`, `vec.cpp`) is the opcode appendix's harness, not a writer. So there is
nothing to port: the owner has made it part 3, a rebuild (DECISIONS-v1.25 s3). For the record, what a writer must do
differently at `821e1bd`: memory-log words for `VirtualLoad*`/`VirtualStore*` rows as for `LD`/`SD` (the same
`is_load`/`is_store` change as `capture_writes`), and the new tables; the opcode stream, advice and control-transfer
words are unaffected.

Related: spec item 112's opcode appendix (`replayer-opcodes.md`, derived at 4f36b3d with `tools/replayer-opcodes/`)
is now stale for the changed rows; see QUESTIONS.md Q3.

---

# Part 2: the expansion tables (in progress)

## The builder

`tracer/tables/build_tables.cpp` in PR #2 (with `build.sh`). It `#include`s the tracer's `decoder.cpp` and
`virtual.cpp` and calls jolt-cpp's own `expand_virt_seq`, `has_side_effects_beyond_rd`, `rd0_is_handled_internally`,
`signed_immediate` and `uncompress_instruction`. Templates are **derived**: sample instruction words are expanded and
every row slot is mapped to an operand code (129-131 only where the field varies, 132-136 for the source immediate and
its masks, 137-144 temporaries, 128/145 constants, literals otherwise), with `offset_imm` where `shift_offset` equals the
source immediate and `sign_imm` from `signed_immediate`; anything that fits no code is an error. A resolver turns
template words back into rows exactly as `Replayer.java` does (decode index, rd = x0 policy and offset, operand
codes, derived `is_first`/`is_virtual`), and the check compares them with jolt-cpp's rows field by field, flags
included (`next_from_value`, `imm0_pc0`, `report_c`, `from_log`, `hold`, `untraced`).

Expansion word (DECISIONS-v1.25 s4-s5): `offset_imm` [63], `sign_imm` [62], `advice_reg` [61:56] = register - 146.

## The replayer's tables: universal (v1.25 s5), delivered to `wip/tables/`

| | |
|---|---|
| ROM | **379 of 512 words**, every RV64IMAC template including **all atomics (188 words)**; v1.1 ~508 with atomics partly controller-fed |
| decode table | 303 ROM entries, 20 controller entries, 1,725 invalid (class 3) |
| controller-fed | 78 template numbers (15 inlines, 63 system keys: ECALL, EBREAK, MRET, CSRRW/CSRRS for the six supported CSRs by register pattern), 13,830 words |
| opcode table | 171 opcodes at 821e1bd, 88 executed row opcodes |

**New: the template-index key.** Universal tables can't list every instruction word, so an entry's key is the word with
register fields cleared (inlines: their templates resolve any registers, x0 and aliasing included, through codes
129-131) or renamed by first appearance (system: jolt-cpp's CSR expansions depend on rd = x0, rs1 = x0 and rd = rs1).
The controller normalizes the presented word the same way before the lookup. This changes the Stage B controller's
lookup and `REPLAY-INPUT-REQUIREMENTS.md`'s index description.

## Checks

- **Random**: every decode index and every key, random words with x0 and aliasing cases: **117,843 words over 107
  source opcodes, 0 failures** (`wip/tables/replayer/check.txt`).
- **Real programs**: every instruction of EVM Zone `4e3451c1d8c5` (616,231) and Cyclone (545,760), resolved from the
  universal tables and compared with jolt-cpp's own bytecode rows: **0 failures**.
- **Mutants**: 500 single-field changes in ROM and fed words (opcode, operand codes, `seq_remaining`, flags, `rd0`,
  format, `sign_imm`, `offset_imm`, const, `hold`), excluding the spec's don't-cares (e.g. `rd0` beyond a template's
  first word, `offset_imm` outside `ShiftData`): **497 caught**; the 3 not caught are in rarely-sampled decode indices
  (`VirtualRev8W` and two others) where the mutant run drew too few valid words.
- **Opcode appendix (Q3)**: meshsim's `tools/replayer-opcodes` regenerated at 821e1bd (`WORKER9/replayer-opcodes-821e1bd/`,
  with the differences for the spec editor): new vectors for `VirtualLoad*`/`VirtualStore*`, `ShiftData*` with offsets,
  and non-contiguous `PEXT` masks (signed: extends from the last bit placed).
- **Division family**: WORKER7's harness at 821e1bd, only the opcode numbers changed: identical result, 8,040,000 cases,
  0 failures (`WORKER9/tracer-div-821e1bd/`).

## Still to do

The Tracer's variant (division family re-encoded; all seven secp256k1 inlines' advice as untraced rows, the division
inlines' Fermat inverse as loop body + repeat count, GLV), the replayer-to-Tracer template-number mapping, and its checks.
