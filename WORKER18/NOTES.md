# WORKER18 notes: the Java emulator of Jolt's tracer (W4, DECISIONS-v1.3 section 17)

Citations are jolt-cpp at `821e1bd` (`src/tracer/...` unless noted) unless marked otherwise.

## Status

- jolt-asic-planning branch **`worker18-rows`** (from `tracer-standalone-main` `8d546e3`): `tracer/rows/row_dump.cpp`,
  `build.sh` (as WORKER16's `state/build.sh`). Built locally: 1 CPU, 3 GB, `vm.overcommit_memory=1`.
- `wip/gold/`: stage 1 Tracer model. **Matches jolt-cpp row for row on EVM Zone trace-23 and Cyclone input/22
  (every record), and on both workloads at 2^29 (every 2^16-row window hash and the run hash)** (2026-11-02).
  **All 15 inlines written and passing InlineEmitTest** (secp256k1 x7, BLAKE2b, BLAKE3 x2, Keccak x2, SHA-256 x2,
  BIGINT256_MUL). Stage 1 is complete; stage 2 next.

## The row record (agreed with the owner, 2026-11-02)

jolt-cpp's full CPU row, `RiscCycle` (`trace_owner.hpp:19-68`), as a fixed record of ten little-endian u64 words (not the
struct's memory, whose padding bytes can't be reproduced outside C++):

| word | contents |
|---|---|
| 0 | pc |
| 1 | opcode \| rd_reg << 8 \| rs1_reg << 16 \| rs2_reg << 24 \| (u16) inline_sequence_remaining << 32 \| is_compressed << 48 \| virt_flags << 56 (bit 0 is_virtual, bit 1 first in sequence) |
| 2 | `_imm` (i64) |
| 3-6 | rd, rs1, rs2, rd_prev (values) |
| 7-9 | mem_access_addr, mem_pre_value, mem_post_value |

Opcode numbers are jolt-cpp's enum order (`isa/riscv.inc`, NOP = 0). Covers the SoA columns (`trace_columns.inc`).
Stage 3 will map upstream's Cycle rows into the same record.

**Hash:** WORKER16's `mix` (jolt-asic-planning `worker16-state`, `tracer/state/state_probe.cpp:42`) over the record's words:
row hash = fold `mix` over w0..w9 from `0x243f6a8885a308d3`; window hash = fold `mix` over row hashes from 0 (windows of
2^16 rows); run hash = fold `mix` over window hashes from 0. Only the run's rows are hashed; padding rows (all zero) are
a count: the next power of two above rows + 1, at least 256 (`trace_owner.hpp:437-450`).

## row_dump

`row_dump <program dir> <input dir> <out> [--window=W] [--rows] [--max-rows=N]` runs jolt-cpp's pipeline unchanged
(`gen_compressed_trace`, `expand_trace` per window) and writes `<out>.hashes.csv`, `<out>.advice.bin` (u64 per row whose
**bytecode** opcode is `VirtualAdvice`, i.e. the secp buffer words, found by `get_pc(pc, inline_sequence_remaining)`),
`<out>.json` (rows, padded rows, run hash, ram_K, layout, host I/O encoding), and with `--rows` `<out>.rows.bin`.

## jolt-cpp's rules (for the emulator; each to be cited in the report)

### Loading and layout
- ELF: little-endian, `EM_RISCV`, `ET_EXEC`; entry nonzero, < 4 GiB, 2-aligned; PT_LOAD segments only, `p_align` must be
  4096, nonzero `p_memsz >= p_filesz`; exactly one executable segment, which is the code range; program range = min/max
  of the loaded segments, and must start at `0x8000_0000` (`loader.cpp:25-190`, `:197-205`).
- Memory config from program.json: `heap_size` **or** `memory_size` is the heap (`utils/elf_helper.cpp:99`); optional
  `host_io_encoding` (`:43-63`).
- Layout (`loader.cpp:228-300`): sizes aligned up to 8; advice sizes power of two or 0; io region = advice + input +
  output + 16 bytes, rounded up to a power of two **in words**; the larger advice region first at `program_start - io_bytes`
  (trusted first on a tie); input after the higher advice end; output; panic; termination; io_end. Stack from
  `program_end + 128` (canary), size `stack_size`; heap after the stack.
- Inputs copied unparsed into input, trusted and untrusted advice (`compressed_trace_gen.cpp`, `execute_fast`); missing or
  empty files are empty (`elf_helper.cpp`, `read_optional_input_file`).
- Initial state: all registers 0, pc = entry (`vm.cpp:56-63`).

### Host I/O encoding (`host_io_encoding.cpp`)
- Auto-detect: scan the code; if every instruction decodes, there is at least one raw ECALL, no `VirtualHostIO`, and no
  CSRRW/CSRRS/MRET, the encoding is LegacyRawEcall, else Standard. The scan stops at the first undecodable instruction
  (decode_failed means Standard).
- LegacyRawEcall turns ECALL into `VirtualHostIO` (all operands immediate 0) in the bytecode; RAM keeps the raw word.

### Decoding (`isa/decoder.cpp`)
- 16-bit units; compressed if the low two bits aren't `11`; compressed words are expanded to 32-bit encodings
  (`uncompress_instruction`, `:646-1167`), which return `0xffffffff` (invalid) or 0 (UNIMP) for reserved forms.
- Operand convention: `op_a, op_b, op_c` with immediate flags. R: rd, rs1, rs2. I: rd, rs1, imm. Shift-immediate: rd, rs1,
  shamt (6 bits). S and B: imm, rs1, rs2 (`imm_a`). LUI/AUIPC: rd, imm (bits 31:12 kept in place), 0. JAL: rd, imm, 0.
  CSRRW/CSRRS: rd, rs1, csr (only the six M-mode CSRs decode, `:598-644`). ECALL, EBREAK, MRET, FENCE, UNIMP: all
  immediate 0. Funct3 011/101/110/111 SYSTEM and non-zero MISC_MEM funct3 are UNIMP. LR ignores the rs2 field; aq/rl are
  ignored.
- Custom opcodes: 0x0B the inlines by funct7/funct3 (`:1210-1250`); 0x5B: rev8w (funct3 0, funct7 5, immediate 0),
  `VirtualAssertEQ` (funct3 1, B format), `VirtualHostIO` (funct3 2, rd 0) (`:1251-1268`).
- An invalid instruction (opcode 255) is still expanded in release builds (asserts off): one bytecode row, never run.

### Bytecode (`isa/virtual.cpp`)
- Entry 0 is a NOP; each source instruction's expansion follows; UNIMP is **removed** (no rows, its bytes skipped)
  (`:1357-1363`). `risc_pc_to_instr_idx` per 2 bytes (`virtual.hpp:159-171`). The last row of an expansion carries
  `pc_increment` = 2 or 4; others 0. Every row of an expansion carries the source pc.
- Row flags (`:174-189`): virtual iff the expansion has more than one row **or** the opcode changed;
  first-in-sequence iff virtual and row 0; `virt_seq_remaining` = length - 1 - index.
- Immediates are sign-extended to 64 bits ((i32) op then u64).
- **rd = x0** (`:15-128`, `:270-283`): unless the opcode handles rd = x0 itself (ECALL, MRET, EBREAK, CSRs, LR/SC, all
  inlines), an instruction with rd = x0 becomes one `ADDI x0, x0, 0` if it has no side effects beyond rd, else rd is
  remapped to the first temporary (register 40). Side-effecting: JAL, JALR, loads, LR/SC, AMOs, CSRs, `VirtualHostIO`,
  inlines.
- Temporaries are allocated from 40 upward per expansion, freed in stack order (`:148-158`).
- Per family: division (`:285-447`), MULH/MULHSU (`:220-232`, `:448-473`), narrow loads and stores (`:239-262`,
  `:474-527`), shifts (`:528-611`: SRAI/SRLI become a bitmask immediate; SLLI becomes `VirtualMULI` by 2^shamt; SLLIW
  `VirtualMULIW`; SRL/SRA/SLL/SLLW/SRLW/SRAW two rows), LR/SC (`:612-712`), AMOs (`:713-987`), FENCE and `VirtualHostIO`
  (`:1063-1067`), ECALL (`:1068-1085`, 7 rows to mtvec), CSRRW/CSRRS (`:1086-1127`, 1-3 rows by rd/rs1), MRET
  (`:1128-1133`, JALR through mepc), EBREAK (`:1134-1164`, `JAL vN, 0`), JAL/JALR with rd = 0 get a temporary rd
  (`:1165-1184`), default: one row with the source operands (`:1185-1187`).
- Lengths: `num_cycles` (`:1210-1306`); secp inlines from `inlines/secp256k1_shapes.hpp`.
- secp inlines put `advice_source` (operation, rs1, rs2 if two-input) on their first row (`:1194-1201`).

### Execution (`trace_owner.cpp`)
- Operands: a field marked immediate supplies its value, else the register's (`:78-83`). Writes to x0 are dropped
  (`:85-93`).
- **Next pc** (`finish_instruction`, `:111-126`): if the new pc is 0, halt; if it differs from the current pc, jump
  (instr_idx = the target's first row); **if it equals the current pc** (no jump, or a jump to itself) the pc advances by
  `pc_increment` and instr_idx by 1.
- **JAL with immediate 0 halts** (new pc 0) (`:277-283`); JALR targets `(rs1 + imm) & ~1`. The run ends when pc = 0.
- Branches compare and add the immediate to the current pc (`:247-276`).
- `VirtualAdviceRemU` returns the **quotient**, as `VirtualAdviceDivU` (REMU's expansion uses DivU anyway); `VirtualAdviceRem`
  returns |quotient|; `VirtualAdviceRemW` |quotient| (32-bit); `VirtualAdviceRemUW` the 32-bit quotient. Division by zero:
  all ones (Div, DivU), `UINT32_MAX` zero-extended (DivUW), -1 (DivW), 0 (the Rem ones). Overflow cases per `:536-618`.
- Shifts by bitmask use `trailing_zeros` of the mask (`:355-379`); `VirtualROTRIW` caps at 32; `VirtualShiftRightBitmaskW`
  = 2^32 - 2^shift.
- Narrow memory: the word address is floored (`isa/riscv.hpp:215-270`); a misaligned word address aborts.
- Window masks and shift-data (`:448-485`): masks by `(ea >> 2) & 1`, `ea & 7`, `ea & 6`; shift-data adds the row's
  `shift_offset` to the base register.
- Asserts (`VirtualAssert*`, `:620-639`) abort on failure; UNIMP aborts (`:648-652`).
- secp advice: `prepare_advice` at a sequence's first row (`:52-66`, called at `:787-791`) evaluates the operation on the
  32-byte inputs at rs1 (and rs2) and resets the buffer; each `VirtualAdvice` row pops the next word.
- **Inline registers 48-127 are never zeroed on the row path** (`expand_compressed_chunk` doesn't call
  `normalize_inline_vregs`; zeroing happens only at chunk headers and snapshots, `:128-139`). Rows stay consistent because
  every inline zeroes its own registers at its end (e.g. `inlines/secp256k1_mulq.inc` tail). So plain sequential
  execution matches.

### Rows (`expand_compressed_chunk`, `trace_owner.cpp:855-915`)
- pc, opcode, flags, `inline_sequence_remaining` from the bytecode row; `is_compressed` = `pc_increment == 2`.
- `_imm` = sum of the immediate fields (`trace_owner.hpp:168-177`), or `shift_offset` for shift-data, 0 for advice rows.
- reg numbers 0 where the field is immediate; rs1, rs2, rd_prev read before execution, rd after (0 where immediate).
- **Every division-family advice row is relabelled `VirtualAdvice`**, rs1/rs2 numbers and values and `_imm` zeroed.
- Stores: address from rs1 + imm (floored), pre = the word before, post = rs2's value. Loads: address, pre = post = rd.
- Padding rows are all zero (NOP = 0).

### Syscalls and the JIT
- `jolt_syscall_dispatch` (`syscalls.cpp:43-80`): advice-write, cycle-track, print: no architectural effect.
- The JIT runs threads to their start; the rows come from the interpreter (`trace_owner.cpp:783-845`).

## Inlines

The `.inc` files are generated straight-line emit code (`secp256k1_shapes.hpp`: "Generated by examples/inlines
--dump-inlines"): jolt-cpp's `examples/inlines/src/dump_inlines.rs` runs upstream Jolt's sequence builders (the
`third-party/jolt` submodule, `f8fc214`) and prints each row. The Java inlines follow those builders
(`jolt-inlines/<name>/src/sequence_builder.rs`) and the shared InlineExpansionBuilder
(`crates/jolt-program/src/expand/inline.rs`, `allocator.rs`):
- inline registers from 48 up, lowest free first; after the sequence, an `ADDI r, x0, 0` reset row for every register
  ever allocated, **in register order** (`allocator.rs`, `take_registers_for_reset`). *(Correction: last turn's
  report that Keccak's tail was unsorted was my misreading of two files' tails printed together; it is 48..84 in order.)*
- the constant-folding helpers (`inline.rs:290-414`): an operation on two known constants emits nothing, a constant
  first operand is swapped into the immediate slot. SHA256_INIT relies on them for its first four rounds.
- `jolt_asm!`'s `srli`, `slli`, `zextw` are source instructions, expanded as usual (VirtualSRLI with a mask, VirtualMULI,
  VirtualZeroExtendWord). The emulator writes them as readable loops; **a test that runs on every build** checks each generated
sequence row for row against the `.inc` emit lists, parsed as data (owner, 2026-11-02).

## Reference data (2026-11-02, row_dump `df1b231`, local runs)

`WORKER18/data/`: `<name>.json`, `<name>.hashes.csv` (and EVM Zone's `advice.bin`); the row files (80 B per row) stay
local (467 MB and 216 MB).

| run | rows | padded | windows (2^16) | run hash | secp advice words | bytecode rows |
|---|---|---|---|---|---|---|
| EVM Zone `trace-23-812ded164e49` (`evm23`) | 5,833,069 | 2^23 | 90 | `c6055e2211414a5a` | 3,710 | 790,242 |
| Cyclone `input/22/input-default` (`cyc22`) | 2,705,819 | 2^22 | 42 | `a3898b8a64eb2d62` | 0 | |

Opcode census: evm23 uses 57 opcodes, among them the secp256k1 inlines (`VirtualAdvice`, `VirtualAssertEQ`), no Keccak
(`VirtualXORROTL1` absent); cyc22 72, among them BLAKE2b (`VirtualXORROT63/32/24/16`) and BLAKE3 (`VirtualXORROTW16/12/8/7`),
no secp, 30 `VirtualHostIO` rows, and a division family (256 `VirtualAdvice` rows, all division advice).

Sandbox note: `vm.overcommit_memory` resets to 0 between sessions; set it to 1 before running jolt-cpp.

## Stage 1 results (2026-11-02, gold `12631db`; reference row_dump `df1b231`, jolt-cpp 821e1bd)

| run | rows | check | result | gold run time | jolt-cpp row_dump |
|---|---|---|---|---|---|
| EVM Zone trace-23 | 5,833,069 | every record (80 B) + 90 windows + run hash | **match** | 1.0 s (5.9 M rows/s, hashing) | |
| Cyclone input/22 | 2,705,819 | every record + 42 windows + run hash | **match** | 0.5 s (5 M rows/s) | |
| EVM Zone `trace-29-4599adc50712` | 354,861,942 (padded 2^29) | 5,415 window hashes + run hash `bcea2e34bc23ccec`; 3,674 secp advice words all used | **match** | 21.2 s, **16.7 M rows/s** | 29 s |
| Cyclone `input/29/input-15961266` | 371,526,406 (padded 2^29) | 5,670 window hashes + run hash `0a6e6fe67d902fb9` | **match** | 22.5 s, **16.5 M rows/s** | 27 s |

Speed: one CPU, Java 21, default heap; short runs include JIT warm-up (5-6 M rows/s); warm, about 16.5 M rows/s.
Loading and expanding the program takes 0.7 s. Comparing every record from a file costs about 30% more than hashing.
The 2^29 runs fit this 3 GB sandbox (memory is sparse 16 KB pages), so no runner was needed.

Advice in stage 1: the division family computes its own (as jolt-cpp's interpreter does); secp256k1 words come from
row_dump's `advice.bin` (stage 2 computes them).

## Stage 2: the segment file, as WORKER16's segment_writer writes it (jolt-asic-planning `worker16-w3` at `ca30b5e`)

Section 16's format **plus every choice the encoder makes**, read from `tracer/segments/segment_writer.cpp` and
`segment_common.hpp` at `ca30b5e`. Byte-identical files need all of them (owner, 2026-11-02). Frame 6 (an `instr_idx`
table) is left out: the tool doesn't write it and where it belongs is open (owner, 2026-11-02).

- **Cuts:** by default `k * 2^22` for k = 0..127 (`--segment-rows`, `--segments`); `--cuts=` gives an explicit
  increasing list starting at 0 (the hard-cut tests), and then the file has one page count per cut. Segment k runs
  rows [cut k, cut k+1), the last one until the program ends.
- **Frame order:** frame 1 once; then per segment frames 2, 3, then 4 or 5. Frame header: u32 length in 32-bit words
  including the 2 header words, u32 type; data zero-padded to a whole word (`put_frame`, `segment_common.hpp:129-137`).
- **Frame 1:** per segment a u32: the number of distinct 16 KB pages among its dirty cells (cell >> 11).
- **Frame 2:** 131 u64 taken before the cut row runs: pc; `instr_idx` (the row's bytecode index); the next offset =
  `instr_idx` - the first index of pc's expansion; then registers 0-127 (register 0's word is 0; 48-127 as they are,
  live mid-sequence). **A segment past the end of the run** (padding): pc 0, `instr_idx` 0, offset 0, and the
  registers **as the run left them** (writer `:170-178`).
- **Frame 3:** the advice words of the segment's advice rows, in row order: a secp row's word, a division row's
  result (`segment_common.hpp:89-103`). Words of a sequence that crosses a cut fall in the next segment.
- **Dirty cells:** every cell a store row writes during the segment (even with an unchanged value), cell =
  (word address - lowest_address) / 8, lowest_address = min(trusted, untrusted advice start).
- **Update words:** the dirty cells sorted, split into maximal runs of consecutive cells; per run a header word
  `start cell | count << 32` and the cells' values **at the segment's end** (read from memory then).
- **Frame 5's data:** u32 count of update words, then the codes, bit-packed least significant bit first in each byte;
  a 32-entry table, all zero at the start of each frame. Per word: if it equals an entry, **the lowest matching index**:
  bit 1, then the index in 5 bits; the entry moves to the front. Otherwise bit 0, then a size bit (1 if the word has
  any of bits 63:32 set, else 0), then 64 or 32 bits of the value; the word is pushed at the front and the last entry
  drops. The last byte's spare bits are 0 (`lru_encode`, writer `:29-57`).
- **Raw or LRU per segment** (writer `:204-211`): LRU size = frame 5's data rounded up to 4 bytes; raw size =
  8 x the word count. **Frame 5 iff the list is non-empty and the LRU size is strictly smaller**; on a tie, raw; an
  empty list is always frame 4 (with no data).

Reference files (regenerated locally from `ca30b5e`, SHA-256 equal to WORKER16's committed `WORKER16/data/w3/*.writer.json`):

| file | rows | segments | bytes | SHA-256 |
|---|---|---|---|---|
| `evm-zone-29` | 354,861,942 | 128 | 1,747,828 | `208520ec...8aa17d18` |
| `cyclone-29` | 371,526,406 | 128 | 10,334,580 | `f45f71b6...8aa64070` |
| `evm-zone-23-hardcuts` (cuts: `WORKER16/data/w3/*.cuts`) | 5,833,069 | 93 | 326,576 | `b33f9fc4...5b35c566` |
| `cyclone-22-hardcuts` | 2,705,819 | 111 | 339,884 | `a3858f5e...8cac81ec` |
