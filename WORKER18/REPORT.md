# WORKER18 report: a Java emulator of Jolt's tracer, now our gold (W4, DECISIONS-v1.3 section 17)

## Summary

`wip/gold/` is a standalone Java 21 emulator of jolt-cpp's tracer (pin `821e1bd`): it loads a guest's ELF (parsed by
hand), lays out Jolt's memory, decodes and expands the program into Jolt's bytecode, runs it, and produces **jolt-cpp's
trace rows exactly**; it computes all advice itself (the division family directly, secp256k1 in BigInteger) and writes
DECISIONS-v1.3 section 16's segment file **byte-identical to WORKER16's segment_writer** (jolt-asic-planning
`worker16-w3` at `ca30b5e`). No dependencies, no native code, no imports from meshsim's other classes; jolt-cpp was
used as data only (its rows, WORKER16's files). About 16.5 M rows/s on one CPU: a 2^29 run's rows in 22 s, its segment
file in 14-16 s.

## What was built

| part | files |
|---|---|
| loading | `Elf` (ELF64 by hand), `Program`, `Layout` (Jolt's layout), `Memory` (sparse 16 KB pages), `Json` |
| decoding | `Decoder` (RV64IMAC, compressed, Jolt's custom opcodes), `Instruction`, `HostIo` (encoding detection) |
| expansions | `Expander` (per instruction family, after `isa/virtual.cpp`), `Bytecode`, `VInstr` |
| execution | `Machine` (the interpreter, building each row), `Bits`, `Isa`, `Op` (jolt-cpp's opcode numbering) |
| inlines | `InlineAsm` (upstream's InlineExpansionBuilder: allocation, reset rows, constant folding), `Secp256k1Inline`, `Blake2Inline`, `Blake3Inline`, `KeccakInline`, `Sha256Inline`, `BigintInline` |
| advice | `SecpCompute` (stage 2), `AdviceFile` (stage 1) |
| output | `Row` (the record and hash), `SegmentWriter` (section 16), `FrameDiff`, `Main` (`compare`, `segments`) |
| tests | `InlineEmitTest` (every build), `check.sh` (stage 2's acceptance) |

Reference data tool: `row_dump` on jolt-asic-planning branch `worker18-rows` (`tracer/rows/`): jolt-cpp's own pipeline
(`gen_compressed_trace`, `expand_trace`) writing each `RiscCycle` as WORKER18's record, per-window hashes, the secp
advice words, and the layout.

**The row record** (agreed 2026-11-02): ten little-endian u64 words holding the full `RiscCycle`
(`trace_owner.hpp:19-68`): pc; opcode | rd_reg << 8 | rs1_reg << 16 | rs2_reg << 24 | inline_sequence_remaining << 32 |
is_compressed << 48 | virt_flags << 56; imm; rd; rs1; rs2; rd_prev; mem_access_addr; mem_pre; mem_post. Hash:
WORKER16's mix over the ten words, folded per 2^16-row window and over windows (the struct's own bytes include padding
Java can't reproduce).

## Validation

### Stage 1: rows (secp advice from jolt-cpp's rows)

| run | rows | checked | result |
|---|---|---|---|
| EVM Zone `trace-23-812ded164e49` | 5,833,069 | every record (80 B), 90 windows, run hash | identical |
| Cyclone `input/22/input-default` | 2,705,819 | every record, 42 windows, run hash | identical |
| EVM Zone `trace-29-4599adc50712` | 354,861,942 | 5,415 window hashes, run hash `bcea2e34bc23ccec` | identical |
| Cyclone `input/29/input-15961266` | 371,526,406 | 5,670 window hashes, run hash `0a6e6fe67d902fb9` | identical |

Also identical on each: the layout (all 20 fields), the bytecode size, the padded row count.

### Inlines

All 15 inlines' sequences equal jolt-cpp's `.inc` emit lists row for row under two operand bindings
(`InlineEmitTest`; on builds without jolt-cpp, against stored digests of the `.inc` files).

### Stage 2: gold

- **Computed secp256k1 advice**: rows identical on EVM Zone trace-23 (every record; 668 operations, six kinds) and at
  2^29 (every window, run hash; 662 operations), and on `inlines` below (all seven kinds, SQUARE_R included).
- **Segment files, byte for byte** against segment_writer `ca30b5e` (regenerated locally; SHA-256 equal to WORKER16's
  committed `WORKER16/data/w3/*.writer.json`):

| file | rows | segments | bytes | SHA-256 | gold time |
|---|---|---|---|---|---|
| EVM Zone trace-29 | 354,861,942 | 128 | 1,747,828 | `208520ece5475caf...8aa17d18` | 13.5 s |
| Cyclone input/29 | 371,526,406 | 128 | 10,334,580 | `f45f71b6e5359960...8aa64070` | 15.5 s |
| EVM Zone trace-23, hard cuts | 5,833,069 | 93 | 326,576 | `b33f9fc4549cf915...5b35c566` | 2.8 s |
| Cyclone input/22, hard cuts | 2,705,819 | 111 | 339,884 | `a3858f5eeb6ee94a...8cac81ec` | 1.6 s |

The hard cuts (WORKER16's) fall mid-secp with advice pending, mid-inline with registers 48-127 live, between LR and SC,
inside ECALL and MRET, at other mid-sequence rows, random rows, and at and past the end (padding segments).

### Stage 3: upstream Jolt

Upstream's own Rust tracer (a16z/jolt `17a9459`, through `upstream_rows` on jolt-asic-planning `worker18-upstream`,
its `Cycle` rows mapped into the same record; NOTES.md, "Stage 3") **gives the same rows as jolt-cpp and gold on every
run**: EVM Zone trace-23 and Cyclone input/22 (every record), the seven example guests (every record), and both
workloads at 2^29 (every window and the run hash). All 15 inline sequences are identical between upstream `17a9459`
and jolt-cpp's `.inc` files (from `f8fc214`). ST1 is answered (DECISIONS-v1.3 section 17).

The differences are outside the rows: ten, each cited on both sides, in `WORKER18/UPSTREAM-DIFFS.md` (D1 program image
from segments or sections; D2 the stack canary; D3 LegacyRawEcall; D4 UNIMP skipped with a drifting pc, or the ELF
rejected; D5 executing where no instruction was decoded; D6 misaligned access checks; D7 EBREAK; D8 the bytecode
count; D9 the stack bounds' names; D10 padding, to confirm). None is resolved; gold stays jolt-cpp-exact until the
owner rules.

### Test guests

Eleven hand-assembled ELFs (`wip/gold/test/guests/`, generator committed) reach the paths no workload or example did:
EBREAK, FENCE, KECCAK256 without absorb, the custom `VirtualAssertEQ` (passing and failing), LegacyRawEcall, misaligned
accesses, UNIMP, a jump into an instruction's middle, a stack-canary store. Where jolt-cpp and upstream both complete,
the three agree on every row; the rest are D2-D6 above. `check_guests.sh` pins gold's result for each.

### Beyond the two workloads

Seven of jolt-cpp's example guests (`tracer-tests`, `inlines`, `merkle-tree`, `fibonacci`, `sha2-chain-profile-15`,
`sha3-chain-profile`, `btreemap-profile`): every record identical, with computed advice. With them, every source
instruction kind is executed somewhere except EBREAK, FENCE, KECCAK256 (whose builder is the executed absorb form minus
the absorb), the custom `VirtualAssertEQ` and UNIMP (removed from bytecode); see NOTES.md for the coverage.

## The rules, with their sources

`WORKER18/NOTES.md` lists every rule with its jolt-cpp citation (file:line at `821e1bd`); the inline builders cite
upstream Jolt at `f8fc214` (the commit jolt-cpp's submodule pins, from which its `.inc` files are dumped). The
non-obvious ones, which a hardware Tracer must also follow:

- **A jump to its own pc is treated as no jump**: pc advances by the row's increment (`trace_owner.cpp:111-126`).
- **JAL with immediate 0 ends the run** (pc 0) (`:277-283`); the run ends when pc is 0.
- **rd = x0**: a side-effect-free instruction becomes `ADDI x0, x0, 0`; one with side effects gets rd remapped to
  register 40; ECALL, MRET, EBREAK, the CSRs, LR/SC and all inlines keep x0 (`virtual.cpp:15-128`, `:270-283`).
- **Every division-family advice row is traced as `VirtualAdvice`**, its rs1/rs2 numbers, values and imm zeroed
  (`trace_owner.cpp:899-904`); `VirtualAdviceRemU` would return the quotient (no expansion emits it).
- **Registers 48-127 are never zeroed on the row path**; rows stay right because every inline's last rows zero its
  registers (`ADDI r, x0, 0`, in register order) (`trace_owner.cpp:128-139`, `allocator.rs`).
- **Narrow accesses** work on the containing aligned word (floored address; misaligned word address aborts)
  (`isa/riscv.hpp:215-270`); C.BEQZ/C.BNEZ put the register in the rs2 field (`decoder.cpp`, `uncompress`).
- **UNIMP is removed from the bytecode**; an undecodable instruction still occupies one bytecode row (an `ADDI x0`
  by the rd = x0 rule) and only aborts if run.
- **Host I/O**: the encoding is auto-detected; in LegacyRawEcall mode ECALL is traced as `VirtualHostIO`
  (`host_io_encoding.cpp`).
- **Segment file encoder choices** (WORKER16's, part of the format's definition): the lowest matching LRU index;
  frame 5 only when non-empty and strictly smaller than raw; padding segments' state is pc 0, `instr_idx` 0, offset 0
  and the final registers; advice words are each advice row's rd value (NOTES.md, "Stage 2").

## Open questions

1. **Frame 6** (the `instr_idx` table) is left out, as the owner decided, until it is settled whether it belongs in the
   per-proof file or a program file; WORKER16's tool doesn't write it yet.
2. **The new pin (88dca85)**: WORKER16 is moving PR #2's tracer to it with files expected unchanged. When that lands,
   rerun `check.sh` and `compare` on the four runs; gold's rules were read at `821e1bd` and I haven't diffed HEAD's
   `src/tracer` (DECISIONS 14 (c) says `isa/` is unchanged at 88dca85).
3. ~~Untested paths~~ **closed by the test guests** (stage 3), except jolt-cpp's JIT path for EBREAK (D7) and aborts
   inside inlines.
4. **Where gold and jolt-cpp could differ by design**: gold throws where jolt-cpp aborts, and also on a few conditions
   jolt-cpp only asserts in debug builds (a jump to a pc where no expansion starts, an address outside the 4 GiB RAM).
   Neither happens on any validated run.
5. ~~Stage 3~~ **done**: upstream's rows equal jolt-cpp's; the differences outside the rows (UPSTREAM-DIFFS.md D1-D10)
   await the owner's rulings, one by one.
6. **W5**: upstream's own tests (the ACT4 architectural tests, the inline tests) through gold: next.
