# gold: Jolt's tracer in Java

A standalone emulator of the Jolt zkVM's tracer (DECISIONS-v1.3 section 17, work item W4): it reads a guest's ELF, sets
up Jolt's memory, runs the program and produces **Jolt's trace rows exactly as jolt-cpp's tracer does** (jolt-cpp
`821e1bd`). Self-contained: plain Java 21, no dependencies, no native code, nothing imported from the rest of meshsim.

Status: **stage 1** (a Tracer model), validated below. Stage 2 (computing all advice, writing section 16's segment
file) is next.

## Build and run

```sh
./build.sh                      # javac into build/, then the inline emit test (against test/inline-emit-digests.txt)
JOLT_CPP=<jolt-cpp> ./build.sh  # the same, but row for row against <jolt-cpp>/src/tracer/inlines/*.inc

java -cp build gold.Main compare <program dir> <input dir> <ref prefix> [--max-rows=N]
```

`<program dir>` holds `program.elf` and `program.json`; `<input dir>` holds `input.bin` and, if present,
`trusted_advice.bin` and `untrusted_advice.bin` (as jolt-cpp's tools take them). `<ref prefix>` is the output of
`row_dump` (jolt-asic-planning, branch `worker18-rows`, `tracer/rows/`): `compare` checks the layout and bytecode size,
then every row's record against `<ref>.rows.bin` when present (stopping at the first difference, with context), else
every 2^16-row window hash against `<ref>.hashes.csv`, then the row count, padded size and run hash. In stage 1 the
secp256k1 advice comes from `<ref>.advice.bin`; the division family computes its own.

## What it matches

| run | rows | checked | result |
|---|---|---|---|
| EVM Zone `trace-23-812ded164e49` | 5,833,069 | every record | match |
| Cyclone `input/22/input-default` | 2,705,819 | every record | match |
| EVM Zone `trace-29-4599adc50712` | 354,861,942 | every window hash, run hash | match |
| Cyclone `input/29/input-15961266` | 371,526,406 | every window hash, run hash | match |

All 15 inlines match jolt-cpp's emit lists row for row (InlineEmitTest). Speed: about 16.5 M rows/s warm on one CPU
(a 2^29 run in 22 s); memory is sparse 16 KB pages, so a 2^29 run fits in a few hundred MB.

## The row record

Ten little-endian u64 words per row, jolt-cpp's `RiscCycle` (see `Row.java`):
pc; opcode | rd_reg << 8 | rs1_reg << 16 | rs2_reg << 24 | inline_sequence_remaining << 32 | is_compressed << 48 |
virt_flags << 56; imm; rd; rs1; rs2; rd_prev; mem_access_addr; mem_pre_value; mem_post_value. Hash: WORKER16's mix
over the ten words, folded per 2^16-row window, the windows folded into a run hash.

## The parts

| file | what |
|---|---|
| `Elf.java`, `Program.java`, `Layout.java`, `Memory.java`, `Json.java` | ELF64 by hand; Jolt's memory layout; loading inputs; sparse RAM |
| `Decoder.java`, `Instruction.java`, `HostIo.java` | RV64IMAC and Jolt's custom opcodes, compressed included, as jolt-cpp decodes them |
| `Expander.java`, `Bytecode.java`, `VInstr.java` | Jolt's expansions per instruction family (virtual.cpp), the expanded program |
| `Machine.java`, `Bits.java`, `Isa.java`, `Op.java` | the interpreter, building each row |
| `InlineAsm.java` and `*Inline.java` | the inlines as readable loops: secp256k1 (7), BLAKE2b, BLAKE3 (2), Keccak (2), SHA-256 (2), BIGINT256_MUL |
| `Row.java`, `AdviceFile.java`, `Main.java` | the record and hash, stage 1's advice, the compare driver |
| `test/gold/InlineEmitTest.java` | each inline vs jolt-cpp's `.inc` emit list, as data |

Every rule cites jolt-cpp (file:line at `821e1bd`) or, for the inlines, upstream Jolt's builder (`f8fc214`, the
commit jolt-cpp pins); `WORKER18/NOTES.md` lists them.
