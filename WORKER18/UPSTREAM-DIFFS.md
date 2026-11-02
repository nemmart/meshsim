# Differences between jolt-cpp and upstream Jolt's tracers (stage 3, ST1)

jolt-cpp at `821e1bd` (`src/tracer/...` unless noted) against upstream a16z/jolt at `17a9459`. Found by running both on
the same ELFs and inputs (jolt-asic-planning `worker18-rows` `row_dump` and `worker18-upstream` `upstream_rows`) and
by WORKER18's hand-assembled guests (`wip/gold/test/guests/`). **None is resolved here**: the owner decides case by
case which the gold follows (by default upstream, the spec's ground truth, item 113). Gold stays jolt-cpp-exact until
then, so its byte-identical acceptance holds.

**The rows themselves agree everywhere tested**: EVM Zone trace-23 and Cyclone input/22 (every record), seven jolt-cpp
example guests (every record), both workloads at 2^29 (every 2^16-row window and the run hash), and the hand-assembled
guests both tools complete. All 15 inline sequences are identical between upstream `17a9459` and jolt-cpp's `.inc`
files (dumped from `f8fc214`). The differences below are in loading, in conventions around the rows, and in what each
does with faulty or unusual guests.

| # | difference | affects valid guests? |
|---|---|---|
| D1 | program image from segments (jolt-cpp) or sections (upstream) | yes, if they differ |
| D2 | upstream enforces the stack canary; jolt-cpp doesn't | no (overflowing guests only) |
| D3 | LegacyRawEcall host I/O exists only in jolt-cpp | yes, for legacy guests |
| D4 | UNIMP in the code: jolt-cpp skips it (and its pc drifts); upstream rejects the ELF | **ruled** (owner, 2026-11-02) |
| D5 | executing where no instruction was decoded statically | no (faulty guests only) |
| D6 | misaligned access: both abort, by different checks | no |
| D7 | EBREAK: upstream stalls (ends); jolt-cpp's interpreter ends the run, its JIT aborts | yes, if a guest reaches EBREAK |
| D8 | bytecode count: jolt-cpp counts the leading NOP | convention only |
| D9 | the layout's stack bounds are named differently | convention only |
| D10 | padding: jolt-cpp pads to the power of two above rows + 1 | to confirm on upstream's prover |
| D11 | upstream has inlines and row kinds jolt-cpp lacks (grumpkin, p256, NTT; four kinds) | yes, guests using them |

## D1. The program image: segments or sections

- **jolt-cpp** loads the ELF's PT_LOAD **segments** into RAM, and the code is the one executable segment
  (`loader.cpp:143` skips non-PT_LOAD, `:153` takes the PF_X segment as the code; `program_start`/`program_end` are the
  segments' extent). The bytecode is decoded from that whole segment (`compressed_trace_gen.cpp`,
  `VirtualBytecode::from_riscv_code`).
- **upstream** builds its image from **sections**: every section at or above 0x8000_0000 is copied, the code is the
  Text sections, and `program_end` is the highest section end (`crates/jolt-program/src/image/elf.rs:46-62`); its
  emulator also loads from section headers (`tracer/src/emulator/mod.rs:169-181`, `setup_program`).
- Effect: the two agree when the executable segment and the Text sections cover the same bytes, which holds for every
  ELF tested. They differ for an ELF with code outside .text sections, data inside the executable segment, or no
  section headers (jolt-cpp only), and `program_end` (hence the stack, heap and `lowest_address`-relative layout) can
  differ by padding between the last section and the segment end.

## D2. The stack canary

- **upstream** rejects any access to [stack_end, stack_end + 128), the canary between the program and the stack:
  "Stack overflow: attempted to ... in the stack canary region" (`tracer/src/emulator/mmu.rs:157-162`; layout:
  `common/src/jolt_device.rs:497-503`).
- **jolt-cpp** places the stack 128 bytes after the program (`loader.cpp:201`, `:278`) but checks nothing.
- Test guest `stack_canary_store`: jolt-cpp (and gold) 6 rows; upstream panics.

## D3. LegacyRawEcall host I/O

- **jolt-cpp** auto-detects guests that use a raw ECALL for host calls (no VirtualHostIO, no CSRRW/CSRRS/MRET) and
  traces every ECALL as `VirtualHostIO` (`host_io_encoding.cpp:47-57`, `:79`); program.json can force either mode.
- **upstream** has no such mode at 17a9459: ECALL always traps to mtvec (`tracer/src/instruction/ecall.rs:1-4`, `:39-50`).
- Test guest `legacy_raw_ecall` (a0 = 0, ECALL, no trap handler): jolt-cpp (and gold) 4 rows, the ECALL row
  `VirtualHostIO`; upstream traps to mtvec = 0 and panics decoding address 0 (`tracer/src/emulator/cpu.rs:1466`).

## D4. UNIMP in the code (ruled)

**Ruling** (owner, 2026-11-02, DECISIONS-v1.3 section 17): the gold loader loads only instructions we can prove. In
strict mode (the default), any instruction in the ELF's load stream that Jolt can't prove (CSRRC and the immediate CSR
forms, a CSR outside the six, FENCE.I, WFI, SRET and other privileged instructions, an undecodable word) throws at
load, as upstream rejects it at decode. The exception is the standard UNIMP encodings (c.unimp 0x0000, unimp
0xC0001073), which compilers emit after code that can't return and as padding: they load as an unreachable marker,
like upstream's UNIMPL, and throw only if executed. In ACT4's permissive mode the unprovable instructions also throw
only when executed.

**Gold** (`wip/gold` `fb5f579`, `Bytecode.LoadMode`): implemented so the bytecode keeps jolt-cpp's structure (a removed
UNIMP still occupies no row, an undecodable instruction one row), so instruction indices and the segment files don't
change: all four segment files stay byte-identical. A removed UNIMP's address is kept; reaching it (by a jump or by
falling through) throws, as does executing a row flagged unprovable. 13 guests cover it (`wip/gold/test/guests/`,
`unprovable_*`, `unimp_*`, `c_unimp_*`), checked strict and permissive by `check_guests.sh`. Both workloads and all
seven jolt-cpp examples load in strict mode, with unchanged rows.

What the three do with those guests:

| guest | jolt-cpp | upstream 17a9459 | gold (ruled) |
|---|---|---|---|
| CSRRC, CSRRWI, CSRRSI, CSRRCI, WFI, FENCE.I | loads; steps over it, pc drifting | rejects at decode ("unsupported system instruction", "invalid MISC-MEM funct3") | rejects at load |
| CSR outside the six (mhartid), SRET, SFENCE.VMA, undecodable word | its JIT refuses the ELF (`transcode.cpp:2546`) | rejects ("UnsupportedCsr(3860)", "unsupported system instruction", "unknown RV64 opcode") | rejects at load |
| unimp 0xC0001073 after the end, never reached | its JIT refuses the ELF (0xC00 isn't one of its CSRs: undecodable) | **rejects** at expansion ("UnsupportedCsr(3072)") | loads; runs normally |
| unimp 0xC0001073 executed | refuses the ELF | rejects | throws when executed |
| c.unimp 0x0000 executed (falling in, or a jump to it) | steps over it, pc drifting; a jump to it aborts | skips it at decode (`elf.rs:126-134`), panics at run time decoding it (`cpu.rs:1466`) | throws when executed |

So under the ruling gold departs from upstream on one point: 0xC0001073 loads (as the ruling's standard UNIMP), where
upstream 17a9459 rejects the ELF.

### The difference as found

- **jolt-cpp** decodes some words as UNIMP (MISC-MEM funct3 != 0 such as FENCE.I, SYSTEM funct3 3/5/6/7, WFI, a
  compressed 0x0000) and **removes them from the bytecode** (`isa/virtual.cpp:1357`). Execution then steps over them
  with the wrong pc: `finish_instruction` advances pc by the executed row's own size and the bytecode index by one
  (`trace_owner.cpp:111-126`), so after a removed UNIMP every row's pc is behind by the UNIMP's size until the next
  jump, and link values (JAL, JALR) are wrong by as much.
- **upstream** rejects the ELF: "invalid MISC-MEM funct3" (`crates/jolt-program/src/image/decode.rs:81`).
- Test guest `unimp_fence_i` (ADDI; FENCE.I; ADDI; JAL): jolt-cpp (and gold) 3 rows with pcs 0x...00, 0x...04 (the
  ADDI at 0x...08), 0x...08 (the JAL at 0x...0c, linking 0x...0c); upstream refuses to load it.

## D5. Executing where no instruction was decoded statically

- **jolt-cpp** maps a jump target to its bytecode index through a table of instruction starts
  (`isa/virtual.hpp:159-164`); a target inside an instruction maps to index 0 (the NOP), which its interpreter doesn't
  handle (`trace_owner.cpp:654-655`, `unreachable`); the release build was observed to abort with "guest program
  executed UNIMP instruction" (`:648`), i.e. undefined behaviour.
- **upstream** fetches and decodes at run time from memory (`tracer/src/emulator/cpu.rs:660-678`), so it executes
  whatever the bytes decode to there. In `jump_into_gap` it decoded the halfword 0x0002 (C.SLLI x0, a HINT), which
  upstream uncompresses to a real `SLLI x0, x0, shamt` (`crates/jolt-riscv/src/uncompress.rs`, test
  `uncompresses_c_slli_x0` at `:612-614`), traced after the rd = x0 rewrite as `ADDI x0, x0, 0`, and ran on; such a row
  isn't in the expanded bytecode, which upstream's prover rejects (`tracer/src/trace_row.rs:13-40`, MissingBytecodePc).
  jolt-cpp's decoder treats C.SLLI with rd = x0 as invalid (`isa/decoder.cpp`, uncompress case 2/0).
- Gold aborts ("jump to 0x8000000a, where no instruction starts").

## D6. Misaligned access

- Both abort, by different checks: **jolt-cpp** `checked_memory_address` (`isa/riscv.hpp:257-265`) for both a
  misaligned LD and a narrow access whose floored word address stays misaligned; **upstream** `assert_eq!(.., 0,
  "Unaligned load_doubleword")` (`tracer/src/emulator/mmu.rs:323`) and "RAM access is not word aligned"
  (`tracer/src/instruction/floor_access.rs:69`).
- Test guests `misaligned_ld`, `misaligned_lw`: all three abort.

## D7. EBREAK

- **Both bytecodes** hold `JAL vN, 0` for EBREAK (jolt-cpp `isa/virtual.cpp:1134-1164`, after upstream PR #1439).
- **upstream** executes EBREAK by not advancing pc; the emulator detects the stall and ends
  (`tracer/src/instruction/ebreak.rs:26-28`, `tracer/src/lib.rs:321-322`).
- **jolt-cpp's interpreter** ends the run on a JAL with immediate 0 (`trace_owner.cpp:277-283`); **its JIT** raises
  ERR_EBREAK and aborts (`transcode.cpp:2164-2165`), so a run long enough for the JIT to reach the EBREAK would abort.
- Test guest `ebreak`: jolt-cpp (interpreter only, too short for the JIT), upstream and gold give the same 2 rows.

## D8. The bytecode count

- **jolt-cpp** counts the leading NOP in `num_instrs` (`isa/virtual.cpp:1317`, `:1335`); **upstream's**
  `expanded_bytecode` excludes it, and preprocessing prepends it (`crates/jolt-program/src/preprocess/bytecode.rs:47`).
  Same bytecode, counts differ by one (790,242 against 790,241 for EVM Zone).

## D9. Stack bounds' names

- **jolt-cpp**: `stack_start = program_end + 128`, `stack_end = stack_start + stack_size` (`loader.cpp:278-279`).
- **upstream**: `stack_end = RAM_START + program_size` (the low end, the canary's start), the stack's top at
  `stack_end + 128 + stack_size` (`common/src/jolt_device.rs:497-503`).
- Same addresses (equal `heap_end` on both workloads); `stack_end` names opposite ends.

## D10. Padding

- **jolt-cpp** pads the trace to the next power of two above rows + 1, at least 256 (`trace_owner.hpp:437-447`).
- **upstream**: its prover's test support checks `(len + 1).next_power_of_two()`
  (`crates/jolt-prover/tests/support/mod.rs:112`), while `crates/jolt-prover/src/profile.rs:728` uses
  `len.next_power_of_two()`. To confirm in the prover proper before calling it the same.

## D11. Inlines and row kinds only upstream has

- **upstream** at 17a9459 registers three inline families jolt-cpp doesn't decode: grumpkin (custom-0 funct7 0x06,
  `jolt-inlines/grumpkin/src/lib.rs`), p256 (funct7 0x07, `jolt-inlines/p256/src/lib.rs`) and NTT (funct7 8,
  `jolt-inlines/ntt/src/lib.rs`); and four final row kinds with no jolt-cpp opcode: `VirtualAdviceLoad`,
  `VirtualPow2I`, `VirtualPow2IW`, `VirtualShiftRightBitmaskI` (`crates/jolt-riscv/src/kind.rs`, e.g. `:346`).
- **jolt-cpp** decodes custom-0 (0x0B) only for funct7 0-5 (`isa/decoder.cpp:1210-1250`); others are invalid
  instructions.
- No ELF tested uses them (upstream_rows maps the four kinds to 0xFE and reports none). Gold follows jolt-cpp. Their
  upstream tests (`jolt-inlines/{grumpkin,p256,ntt}/src/tests.rs`) are out of W5's scope (owner, 2026-11-02).
