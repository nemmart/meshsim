# WORKER16 report: the Tracer's starting state (T2), and section 13's advice streams

Measured and read, not designed: what follows is for the owner to decide on.

**Sources.** jolt-cpp `821e1bd` as copied in jolt-asic-planning `tracer-standalone-main` (`6c3aae0`); paths below are under
its `tracer/src/tracer/`. The tool is `tracer/state/state_probe.cpp` on branch **`worker16-state`** (commit `ae1c262`),
built by `tracer/state/build.sh` like `delivery_writer`. Runs: local, 1 CPU, 3 GB, `vm.overcommit_memory=1`, about 3 and
4 minutes each; the Quest runner was not needed.

## Answer

**At an instruction boundary (next offset 0), pc, `instr_idx` and registers 0-127, with RAM, are the whole of jolt-cpp's
row-affecting state**: on both 2²⁹ workloads every restart from them alone reproduced jolt-cpp's next 65,536 rows exactly.
**Mid-expansion, one more piece exists: the secp256k1 advice buffer** (`secp_advice[8]` and `next_secp_word`,
`trace_owner.cpp:49-50`), live inside a secp256k1 inline sequence from its first row to its last `VirtualAdvice` row.
Restarts there fail without it and all match with it. DECISIONS-v1.3 section 13 already turns it into the head of the next
slice's read-log stream. Every other candidate is either inside registers 0-127, derivable from `instr_idx`, static
configuration, or bookkeeping that never reaches a row.

## Inventory

| state | where | in 0-127? | live? (2²⁹ runs) | the Tracer needs |
|---|---|---|---|---|
| pc | `InterpreterState::risc_pc`, `trace_owner.cpp:38`; JIT `risc_regs[pc]`, `vm.hpp:27`, `isa/riscv.hpp:67` | no | always | the state word (has it) |
| `instr_idx` | `trace_owner.cpp:34` | no | always | the state word (has it); with the next offset it locates a mid-sequence start |
| x1-x31 | `regs[1..31]`, `trace_owner.cpp:26` | yes | always | - |
| LR/SC reservations | regs 32 (`reservation_w`), 33 (`reservation_d`), `isa/virtual.hpp:34-35`; set and cleared in `isa/virtual.cpp:610-700` (JIT: `transcode.cpp:2192-2251`); SC.W also spills its success flag into reg 32 mid-sequence | yes | Cyclone: 27,230 LR.W/SC.W pairs, at most 2 rows apart, reg 32 read across an instruction 27,230 times; EVM Zone: none | - |
| CSRs mtvec, mscratch, mepc, mcause, mtval, mstatus | regs 34-39, `isa/virtual.hpp:36-41`, `csr_to_vreg` `:56-66`; written by ECALL's expansion `isa/virtual.cpp:1069-1084`, read by MRET `:1127-1131` and CSRRW/CSRRS `:1085-1126` | yes | Cyclone: 875 traps; values read across instructions: mtvec 875, mscratch 2,625, mepc 1,750, mcause 875, mtval 875 (always 0), mstatus 875; EVM Zone: none | - |
| allocatable temporaries | regs 40-47, `isa/virtual.hpp:44-53` (allocator `isa/virtual.cpp:140-157`) | yes | **never read** across an instruction (0 on both runs), but **their value is the next write's `rd_prev`**: non-zero `rd_prev` carried over: EVM Zone reg 40 3.58M, reg 41 2.25M; Cyclone reg 40 24.4M, reg 41 22.9M, reg 42 27,233, regs 43-47 2 each | - (see note 1) |
| inline temporaries | regs 48-127 (`isa/virtual.hpp:25-29`) | yes | **zero at every instruction start** on both runs (0 exceptions); highest written: 64 (EVM Zone), 87 (Cyclone); live only mid-sequence | - |
| **secp256k1 advice buffer** | `secp_advice[8]`, `next_secp_word`, `trace_owner.cpp:49-50`; filled by `prepare_advice` `:52-65` at a sequence's first row from RAM through two registers (`advice_source`, `isa/virtual.hpp:80-84`); consumed in order by `VirtualAdvice`, `:527-531` | **no** | EVM Zone: 662 sequences, 6,221 rows mid-sequence with words pending; Cyclone: none | mid-sequence only: the unconsumed words (section 13: head of the next slice's stream) |
| inline-register high-water mark | `dirty_inline_vregs_end`, `trace_owner.cpp:31` | no | bookkeeping for `normalize_inline_vregs` (`:125-137`) | nothing: derivable (the check set it to 128) |
| "previous row's sequence count" | `prev_virt_seq_remaining`, `trace_owner.cpp:697` (decides when `prepare_advice` runs) | no | - | nothing: derivable from `instr_idx` (inside a sequence and not its first row) |
| row and cycle counters | `cycle`, `trace_owner.cpp:41`; JIT `instr_count`, `vm.hpp:35` | no | positions only | nothing (the Tracer's own row count) |
| replay log position | `mem_log`, `mem_log_idx`, `trace_owner.cpp:46-47`; JIT `mem_log_idx`, `vm.hpp:76` | no | not used by the RAM-driven path | nothing |
| JIT bookkeeping | `host_regs`, `next_checkpoint_at`, `exit_code`, `state`, `stubs`, `vm.hpp:31-74` | no | never reaches a row | nothing |
| guest memory, I/O region included | RAM; inputs, trusted and untrusted advice, outputs, panic and termination are words at fixed addresses (`loader.hpp:26-58`) | no | the guest reads them by ordinary loads (EVM Zone: 65,536 loads from trusted and 8,220 from untrusted advice; Cyclone: 2,902,036 from untrusted advice; neither stores into the input or advice regions) | the memory image (section 12's page table); no separate input/output or advice pointers exist |
| program configuration | the expanded bytecode (`VirtualBytecode`, `isa/virtual.hpp:132-175`), the host I/O encoding (`compressed_trace_gen.cpp:72`; both workloads `standard`), the memory layout | no | static per program | the decode table, ROM and page table (section 12) |
| syscalls | `syscalls.cpp:57-94`: advice-write, print, cycle-track | - | no architectural effect | nothing |
| halt | pc 0 (`JAL` with imm 0, `trace_owner.cpp:281`; `:1075`) | - | - | nothing |

**Note 1, registers 40-47.** jolt-cpp's JIT tracks only registers 0-39 (`num_jit_tracked_regs`, `isa/virtual.hpp:47`);
40-47 are not RISC-V state but trace history. jolt-cpp itself starts each thread with them zero
(`trace_owner.cpp:1039-1046`) and patches the chunk headers afterwards (`:1104-1135`). A start state built from a RISC-V
emulator's registers therefore has to take 40-47 from the trace's history. Zeroing them broke 1,060 of 1,096 Cyclone restarts.

**Note 2, jolt-cpp's own restart record.** `CompressedChunkHeader` (`trace_owner.hpp:393-399`) holds registers 0-47, pc,
`instr_idx`, `skip_cycles` and `mem_log_start`. A chunk that would start mid-sequence with a non-zero register 48+ is rewound to the
sequence's start (`trace_owner.cpp:704-731`). Its replay path takes advice from the log (`execute_instruction<false>`,
`:527-533`), so the buffer never appears there. The JIT can only resume at an instruction boundary (`:1229-1240`).

## Method

`state_probe` includes `trace_owner.cpp` (as `delivery_writer` does) and runs jolt-cpp's interpreter unchanged:

1. **Reference.** jolt-cpp's pipeline (`gen_compressed_trace`, then `expand_trace` per window of 2¹⁶ rows), one 64-bit hash per window.
2. **Pass A.** One RAM-driven run, the Tracer's model: `execute_instruction<true>`, advice computed by
   `prepare_advice`, in a child forked before any execution. Rows are laid out as `expand_compressed_chunk` lays them out,
   hashed per window and **compared with the reference**. Counters cover every candidate above. Restart points are
   reservoir-sampled per category (48 per category plus the first 4, seed 1).
3. **Pass B.** At each point, fork a reference child (the state as is) and one child per variant, the state rebuilt from:
   - **T2:** pc, `instr_idx`, registers 0-127, RAM;
   - **T2 without registers 32-39**, **without 40-47**, **without 48-127** (controls);
   - **T2 + the advice buffer.**

   Each child runs 65,536 rows into shared memory; the rows are compared byte for byte.

**Categories** (as asked):
- random rows, random instruction starts, window starts;
- mid-expansion in inline sequences, mid-expansion in other virtual sequences, mid-secp with advice pending;
- the first row of an ECALL, inside it, and the row after; the same around MRET and CSR instructions;
- instruction starts between an LR and its SC; inside SC; inside the AMO;
- around `VirtualHostIO` (syscalls);
- at and after secp `VirtualAdvice` rows; at and after division advice rows; at and after loads from the advice regions;
- loads from the input region; program start.

**Checks:**
- **Instrumented vs. uninstrumented:** the instrumented rows equal jolt-cpp's on **every window of both runs** (EVM Zone
  5,415/5,415, Cyclone 5,670/5,670), so its counts are of jolt-cpp's own rows.
- **WORKER9:**
  - rows: 354,861,942 and 371,526,406;
  - stores: 55,254,496 (SD 54,411,302) and 37,036,072 (SD 28,284,220);
  - loads: 64,229,138 (LD 61,455,842) and 60,395,486 (LD 33,729,842).
- **WORKER12:**
  - EVM Zone's 662 secp inlines (254 DIVQ, 265 SQUAREQ, 139 MULQ, 2 DIVR, 1 MULR, 1 GLVR);
  - Cyclone's 875 traps and returns;
  - Cyclone's 8,751 CSR instructions (10 per trap plus one mtvec write);
  - Cyclone's 96,687 division advice rows (REMU 95,893 + DIVU 791 + DIVW 3).

  All identical.
- **The controls show the check sees missing state:**
  - without 32-39: fails at every trap, MRET, CSR, LR-to-SC and SC point of Cyclone (573/1,096 match overall);
  - without 40-47: fails almost everywhere (36/1,096 and 94/673 match);
  - without 48-127: fails only mid-inline (1,043/1,096 and 465/673 match).

## Restart results (2²⁹)

| workload | points | T2 matched | T2 + advice buffer matched |
|---|---|---|---|
| EVM Zone `4e3451c1d8c5` trace-29-4599adc50712 | 673 | 515 | **673** |
| Cyclone input/29 `input-15961266` | 1,096 | **1,096** | 1,096 |

**All 158 EVM Zone failures are mid-secp with advice pending:**
- mid-sequence with words pending: 52/52 failed;
- at a `VirtualAdvice` row: 52/52 failed;
- just after one: 48/52 failed (the 4 that matched had nothing left to read);
- elsewhere mid-inline: 6/52 failed, all inside secp sequences.

With zero advice, the sequence's own `VirtualAssertEQ` aborts the run (`trace_owner.cpp:620-624`). Every other category,
including traps, CSRs, LR to SC and syscalls, matched at every point. Per-point results are in `data/*-state_probe.json`.

## Section 13 measurements (2²⁹, slices of 2²² rows)

**(1) secp256k1 sequences.** EVM Zone: **662 sequences, 120,110 rows inside them (0.034% of rows), the longest 240 rows**
(DIVR); 3,674 advice words. Cyclone: none.

| operation | sequences | rows | longest | words each |
|---|---|---|---|---|
| DIVQ | 254 | 49,276 | 194 | 8 |
| SQUAREQ | 265 | 44,255 | 167 | 4 |
| MULQ | 139 | 25,854 | 186 | 4 |
| DIVR | 2 | 480 | 240 | 8 |
| MULR | 1 | 232 | 232 | 4 |
| GLVR_ADV | 1 | 13 | 13 | 6 |

The words consumed per operation equal jolt-cpp's `shape(operation).advice_words` (`inlines/secp256k1_shapes.hpp:10-16`).
`evaluate`'s result array has 8 slots (`inlines/secp256k1.hpp:20`), but only DIVQ and DIVR use all 8.

**(2) Slice boundaries.**

| workload | boundaries | mid-sequence | in a secp sequence | with advice pending |
|---|---|---|---|---|
| EVM Zone | 84 | 5 (LBU 3, SB 2) | **0** | **0** |
| Cyclone | 88 | 29 (LBU 12, SB 10, BLAKE2 4, BLAKE3_KEYED64 1, LW 1, SH 1) | **0** | **0** |

So the head-of-stream rule is not exercised by either 2²⁹ run. EVM Zone's secp work all lies in slice 0, rows of about
2.1M-2.4M (WORKER12's windows 33-36), far from the boundary at 4,194,304. No division advice was pending at any boundary either.

Two things bear on the rule:
- **Exposure.** A boundary would carry pending words only if it fell on one of EVM Zone's 6,221 mid-sequence rows with
  words pending. A crossing sequence carries at most its own words: 8 (DIVQ, DIVR), 6 (GLVR) or 4 (the rest).
- **Order.** The buffer is read strictly in order (`next_secp_word++`, `trace_owner.cpp:530`), and each sequence fills
  it afresh at its first row. So the words a restart needs are exactly the unconsumed ones, in row order. The "T2 +
  advice buffer" restarts at the 52 mid-sequence points (and the 52 at advice rows) matched with nothing else added.

**(3) Advice words per slice** (secp + division; per-slice data in `data/*-slices.csv`):

| workload | slices with rows | min | mean | max | total | mean over 128 slices | section 13's estimate |
|---|---|---|---|---|---|---|---|
| EVM Zone | 85 | 1 | 44.5 | 3,676 (slice 0) | **3,782** (3,674 secp + 108 division) | 29.5 | about 5.4K |
| Cyclone | 89 | 0 | 1,086 | 3,094 (slice 48) | **96,687** (all division) | 755 (6.0 KB) | about 97K, about 6 KB per slice |

Cyclone matches the estimate. EVM Zone is about 30% below it: the estimate used 8 words per secp inline, while MULQ,
SQUAREQ and MULR use 4 and GLVR 6. It also has 108 division words the estimate didn't include. The largest slice is
3,676 words (about 29 KB, EVM Zone's slice 0) and 3,094 words (about 24 KB) for Cyclone. Slices past each program's
end (43 and 39 of the 128) carry no words.

## Files

- `data/evm-zone-29-state_probe.json`, `data/cyclone-29-state_probe.json`: the full output of
  `state_probe <program dir> <input dir> <out json>` with defaults (window 2¹⁶, run 2¹⁶ rows, k = 48, first = 4, seed 1,
  slice 2²²). It holds the counts, sequences by source, per-register carried reads and `rd_prev`, secp sequences, slices,
  restart summary and per-point results (T2 at every point, every failing variant).
- `data/evm-zone-29-slices.csv`, `data/cyclone-29-slices.csv`: per slice: rows, secp and division words, and what its
  boundary cuts.
