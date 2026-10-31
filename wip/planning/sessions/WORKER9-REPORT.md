# WORKER9 report: the standalone tracer at current jolt-cpp (jolt-asic-planning PR #2)

**All three parts are done.** PR #2 is ready for review. v1.25's acceptance runs (WORKER11, Quest 083 and 084)
matched every row of both programs against these deliveries, so the deliveries and the writer are verified end to end.
What is left open is listed at the end.

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
store rows equal `capture_writes` output on both. **At 2^29 (Quest 069b, the 62 GB runner): `--validate` OK on all
three workloads**, and `make runs`' `.bin` byte-identical to the validate run's; counts identical to the sandbox runs:

| run | writes | validation hash | wall | peak RSS | `.bin` SHA-256 |
|---|---|---|---|---|---|
| EVM Zone `4e3451c1d8c5` trace-29 | 55,254,496 | `0x5094e2f40c3e2729` | 23 s | 29.2 GB | `a5568ea0...e77586ee` |
| Cyclone input/29 | 37,036,072 | `0xeecfc9850cd093b4` | 21 s | 30.7 GB | `734368ad...cc` |
| PR #1's EVM Zone trace-29, new tracer | 31,683,025 | `0x94a703b04e7b5b65` | 15 s | 20.7 GB | `65a3cacf...7eae` |

(Full SHA-256 in Quest `results/069b-w9-tracer-2p29-validate/sha256.txt`. Task 069 itself failed its three attempts on a
missing tarball before the owner's copy; 069b is its re-run.)

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
| ROM | **388 of 512 words**: every RV64IMAC template, **all atomics** (188 words), ECALL, EBREAK, MRET; v1.1 ~508 with atomics partly controller-fed |
| decode table | 306 ROM entries, 17 controller entries (15 inlines; CSRRW, CSRRS), 1,725 invalid (class 3) |
| controller-fed | 75 template numbers: 15 inlines, 60 CSR keys; 13,821 words |
| opcode table | 171 opcodes at 821e1bd, 88 executed row opcodes |

### The CSR instructions: controller-fed (owner, DECISIONS-v1.25 s6)

ECALL, EBREAK and MRET are exact words with their own decode entries and are in the ROM (9 words). **The CSR
instructions (CSRRW, CSRRS: 60 templates, 102 words) stay controller-fed, the one deliberate exception to the goal of
every expansion except the inlines and the advice generators in the ROM** (owner's option (a)). They would fit in the
124 free words, but the decode index sees CSRRW and CSRRS as one entry each, while jolt-cpp's expansion depends on the
CSR number (each CSR is a different virtual register, named literally in the rows) and on the register pattern
(rd = x0, rs1 = x0, rd = rs1, which changes the row count). The alternatives were a special SYSTEM decode layout with a
loaded CSR-id table and a computed pattern, or new replayer datapath; CSR instructions are rare, so the controller
feeding them costs almost nothing. So the controller feeds the inlines, the advice generators and the CSR
instructions, and nothing else.

### The template-index key (a format change)

Universal tables can't list every instruction word, so the index's key is no longer the word itself. The 16-byte
entry keeps its layout (bytes 0-3 the key, 4-7 the word offset into `templates.bin`, 8-11 the length, 12 `rd0`,
13 class, 14-15 spare); entries are sorted by key, ascending, and an entry's template number is its position. To serve
a presented code-stream word *w*, the controller computes **key(w)** and looks it up. key(w) keeps every bit of *w*
except the register fields:
- **class 1, inline** (*w*[6:0] = 0001011): rd [11:7], rs1 [19:15], rs2 [24:20] **cleared**; one entry per inline.
  The template's codes 129-131 resolve any registers correctly, x0 and aliasing included (checked).
- **class 2, CSRRW/CSRRS** (*w*[6:0] = 1110011, funct3 1 or 2): rd and rs1 **renamed by first appearance**: x0 stays 0,
  a non-zero rd becomes 1; rs1 becomes 0 if x0, 1 if it equals a non-zero rd or if rd is x0, else 2; the CSR field is
  kept. Five patterns per CSR and instruction: (rd, rs1) = (0,0), (0,1), (1,0), (1,1), (1,2). Entries exist for the six
  CSRs jolt-cpp supports; any other CSR has no entry, an error, as in jolt-cpp.
- Operand codes 129-131 still resolve against the presented word, not the key.

This replaces "one entry per distinct instruction word" in `REPLAY-INPUT-REQUIREMENTS.md` section 2 and changes the
Stage B controller's lookup (normalize, then search).

## Checks

- **Random**: every decode index and every key, random words with x0 and aliasing cases: **117,676 words over 108
  source opcodes, 0 failures** (`wip/tables/replayer/check.txt`).
- **Real programs**: every instruction of EVM Zone `4e3451c1d8c5` (616,231) and Cyclone (545,760), resolved from the
  universal tables and compared with jolt-cpp's own bytecode rows: **0 failures**.
- **Mutants**: 500 single-field changes in ROM and fed words (opcode, operand codes, `seq_remaining`, each flag, `rd0`,
  format, `sign_imm`, `offset_imm`, const, `hold`): **all 500 caught**. Changes the replayer can't see by the spec are
  not mutants and are skipped: `rd0` beyond a template's first word, or 0 vs 2 in a template with no temporary or
  source rd; `offset_imm` outside `ShiftData` or under format R; `virt` in a multi-row template; format or const where
  no code reads them; the immediate-zero codes (145, 132 under format R, 128 with const 0) among themselves; temporary
  *k* vs literal register 40 + *k* where the template's `rd0` isn't remap. The first run caught 497: the 3 misses were
  in rarely drawn decode indices, so the mutant check now uses every derivation sample plus its x0 variants plus up to
  60 fresh valid words per index.
- **Opcode appendix (Q3)**: meshsim's `tools/replayer-opcodes` regenerated at 821e1bd (`WORKER9/replayer-opcodes-821e1bd/`,
  with the differences for the spec editor): new vectors for `VirtualLoad*`/`VirtualStore*`, `ShiftData*` with offsets,
  and non-contiguous `PEXT` masks (signed: extends from the last bit placed).
- **Division family**: WORKER7's harness at 821e1bd, only the opcode numbers changed: identical result, 8,040,000 cases,
  0 failures (`WORKER9/tracer-div-821e1bd/`).

## The Tracer's tables (`wip/tables/tracer/`)

Built by `tracer_tables.py` (PR #2) from the replayer's universal tables. Same decode entries and template numbers
(`template_map.txt`: identity, both units have the same controller-fed set); **every traced word bit-identical to the
replayer's** (checked over all 2,048 decode entries and 75 templates; only the Tracer-only `advice_reg` differs).
Untraced words compute every advice value in bank 1 (results in 146-209, scratch above; at most 26 registers live).

- **ROM 404 of 512**: the replayer's 388 plus the division family's 16 untraced words (WORKER7's rows, renumbered).
- **The seven secp256k1 inlines** (controller-fed): multiply and square by product scanning and WORKER7's reduction
  (233-414 untraced rows); GLV (940 rows: k mod r, two rounded divisions by r, two signed 128-bit differences,
  branch-free); the two division inlines: reduce a and b, **Fermat inverse b^(m-2) as one loop body x 256** (square,
  multiply, select by the exponent's top bit, shift the exponent: the row sequence never depends on the data), then the
  output and the witness quotient. DIVQ: 123,979 untraced rows from 1,069 stored words; DIVR: 214,957 rows from 1,777.
- **Segments** (`segments.bin`, index byte 14): a template is its segments in order, each repeated as given.
- **Check**: every untraced program executed from the words, loops instantiated, against jolt-cpp's own advice
  (`advice_ref.cpp`: jolt-cpp's expansion executed by `execute_instruction`, the inline advice by `evaluate`):
  division family 8 x 200 cases with the edge cases, multiply/square 4 x 200, GLV 200, DIVQ and DIVR 10 each:
  **0 mismatches**.
- The inverse is larger than WORKER7's addition-chain estimate (53,000-110,000) because the loop body always
  multiplies; an addition chain (255 squarings, ~15 multiplies for q) would roughly halve DIVQ but needs several
  segments with different bodies (runs of squarings between multiplies). The segment format already allows it.
  **On hold** (v1.25 s6): WORKER12 is counting how often each inline runs in both 2^29 workloads; the single loop
  stays until the owner decides.
- **The untraced `LD`** (WORKER7 Q9, QUESTIONS Q9): **confirmed, on one condition: it reads through the Tracer's D-cache
  like any load**, so it sees the program's own earlier stores (an inline's operands may have just been written by the
  guest). It writes a bank-1 register and logs nothing.

## Still to do

Part 2 is complete; Q8 (loop vs addition chain) is on hold for WORKER12's counts. Part 3 (the delivery writer) is
under way (PLAN-PART3.md, approved).

---

# Part 3: the delivery writer

`tracer/delivery/delivery_writer.cpp` in PR #2 (README there): a functional model of the Tracer's output from
jolt-cpp 821e1bd's own trace, writing per window the reference rows, the three state-record variants (raw, LRU, the
Tracer's pick), the three streams raw and compressed, `end.bin` and a manifest with SHA-256, and **proving every window
by replaying it from the delivered files alone**: the streams decoded, code words resolved through the universal
tables exactly as the replayer does, rows executed with jolt-cpp's row semantics on the log-driven path, transfers
taking their next `instr_idx` from the read log; every record compared with `rows.bin`, every stream consumed exactly,
the end state equal to the next window's start. A flipped bit in any stream, in `rows.bin` or in a ROM word is caught.

| program | rows | windows | replay (3 variants) | wall (1 CPU) | peak RSS |
|---|---|---|---|---|---|
| PR #1's EVM Zone trace-23 | 4,278,784 | 128 | all OK | 13 s | |
| Cyclone input/22 | 2,705,819 | 64 | all OK | 7 s | |
| **EVM Zone `4e3451c1d8c5` trace-29** | 354,861,942 | 8,192 | **all OK** (1,064,585,826 rows) | 1,014 s | 1.44 GB |
| **Cyclone input/29** | 371,526,406 | 8,192 | **all OK** (1,114,579,218 rows) | 819 s | 1.63 GB |

So a full 2^29 program is generated and proved in 14-17 minutes on one CPU without keeping files, inside the runner's
45-minute limit (v1.25 s7's generate, replay, discard shards).

- **Selected windows** (12 EVM Zone, 13 Cyclone; README lists the categories; EVM Zone has no atomics, Cyclone's
  densest-atomics window has 104): written and proved, 137 MB, delivered to the owner as
  `w9-selected-windows-821e1bd.tgz` (SHA-256 `f4b11501...71bd641`), not committed.
- **Full-program deliveries**: Quest tasks 070 (EVM Zone) and 071 (Cyclone), held, input `w9-delivery.tgz` (SHA-256
  `4000c1ac...c3da1ce10`, the tracer tree at `121c271` plus the universal replayer tables and a libelf fallback). Each
  writes every window with `--keep=all` to `/tmp/w9-deliveries/` on the box (about 31 GB) and proves it; results get the
  log, size and the manifest's SHA-256.
- **Notes.** The row immediate is jolt-cpp's raw 64-bit `_imm`, pending WORKER10's ruling (v1.25 s7 Q1). A register the
  program hasn't touched before a window holds 0 in that window's image (its first observed value otherwise); no row
  can observe it. The read log's control-transfer rule is the spec's: a row with `next_from_value` whose new pc differs
  from its pc, which is jolt-cpp's `finish_instruction` test.

## Full-program deliveries on the runner (Quest 079, 080)

Both written with `--keep=all` and every window proved by the writer's functional replay in all three variants, with
`imm_signed` (word 0 [86]). The runner box's paths are the inputs for WORKER11's kit (a replay-delivery root: a
`manifest.json` listing the programs, one directory per program with the tables and the window files):

| | EVM Zone `4e3451c1d8c5` trace-29 (079) | Cyclone input/29 (080) |
|---|---|---|
| path on the box | `/tmp/w9-deliveries/evm-zone-29/` | `/tmp/w9-deliveries/cyclone-29/` |
| size | 48,368,983,728 bytes (46 GB) | 46,975,272,816 bytes (44 GB) |
| writer run time (16-thread box, one thread) | 424 s | 356 s |
| windows | 8,192, all replayed OK (1,064,585,826 rows over 3 variants) | 8,192, all OK (1,114,579,218 rows) |
| `<name>.manifest.json` SHA-256 | `7142e894c80a0c9d491f21c852a942885e0abd97cfe6aefd0101d716824e138f` | `f8a0622e144a944c51386b5b36f81101dcdeb16e5a8d950c1e9ca0e3097f8982` |

- **Delivery root for the kit: `/tmp/w9-deliveries`**, `manifest.json` (both programs) SHA-256
  `d80dac9ae327eab5a5522807696570282fd8cad82fc3cd0f4784a415ab3e5790`.
- **Selected windows: `/tmp/w9-deliveries-selected`** (same layout, 12 + 13 windows, all proved), packed as
  **`/tmp/w9-selected-windows.tgz`**, SHA-256 `c62de0229be30864655e68a1d8cc00bffba4c271027c764a22d7f1580e0c1ca1`.
  These carry `imm_signed`; `/tmp/w9-selected-windows-821e1bd.tgz` (copied by hand earlier) predates it and should
  not be used.
- Disk after both: 260 GB free.

- **End to end:** v1.25's acceptance runs (WORKER11, Quest 083 and 084) replayed every window of both deliveries in
  meshsim and matched every row.

---

# Closing: settled items and rebuild notes

**Settled while the work went on:**
- **T7 (was Q8), the division inlines' inverse: answered.** Keep the single Fermat loop as built (DIVQ 123,979 and
  DIVR 214,957 untraced rows), with host-supplied advice as the backup (DECISIONS-v1.3 s11).
- **The normalized template-index key: done.** WORKER11 updated `REPLAY-INPUT-REQUIREMENTS.md` and the controller's
  lookup, merged into main.
- **The opcode appendix: done.** WORKER10 updated `spec/replayer-opcodes.md` from the 821e1bd harness outputs
  (`WORKER9/replayer-opcodes-821e1bd/`).
- **The Tracer's formats and requirements are v1.3 Tracer work**, recorded in DECISIONS-v1.3: `segments.bin` and index
  byte 14, `seq_remaining` counting traced words only, `advice_reg` [61:56], and the untraced `LD` reading through the
  Tracer's D-cache.

**Still to keep in mind:**
- **PR #2** is stacked on PR #1's branch; retarget it to `main` if PR #1 merges first.
- **The deliveries live in `/tmp` on the runner box** (`/tmp/w9-deliveries`, `/tmp/w9-deliveries-selected`,
  `/tmp/w9-selected-windows.tgz`), so a reboot or clean-up loses them. Rebuild them from `w9-delivery.tgz` by re-running
  079 and 080 under new task numbers (claimed in Quest's `NUMBERS.md`), about 7 minutes each; the manifests' SHA-256
  above identify the same content.
- **A later jolt-cpp:** the tables and deliveries are tied to `821e1bd`. For a new commit: re-copy the tracer under
  PR #1's rules, rerun `build_tables` and `tracer_tables.py` (their checks run as part of the build), regenerate the
  opcode-appendix harness outputs, and regenerate the deliveries with `delivery_writer`, which proves each window as it
  writes it.
- A register a program hasn't touched before a window holds 0 in that window's image (no row can observe it); if the
  real Tracer captures the hardware value instead, the images differ only in such registers.
