# Differences between jolt-cpp and upstream Jolt's tracers (stage 3, ST1)

jolt-cpp at `821e1bd` (`src/tracer/...` unless noted) against upstream a16z/jolt at `17a9459`. Found by running both on
the same ELFs and inputs (jolt-asic-planning `worker18-rows` `row_dump` and `worker18-upstream` `upstream_rows`) and
by WORKER18's hand-assembled guests (`wip/gold/test/guests/`). The owner rules case by case which the gold follows.
**Why a16z's side matters** (owner, 2026-11-03, DECISIONS-v1.3 section 17, `c111f7c`): jolt-cpp has no verifier of
its own; it proves `jolt_verifier::JoltProof` against a16z's `jolt_program` preprocessing from its submodule
`third-party/jolt` at `7189216`, so a16z's bytecode is what every proof is checked against, and where jolt-cpp builds a
different bytecode or image its proofs fail verification.

**Pins**: `7189216` is an ancestor of `17a9459` (17 commits). Every a16z file D1, D3 and D12 rest on has the same git
blob at both: `crates/jolt-program/src/image/{elf,decode,mod}.rs`, `crates/jolt-riscv/src/uncompress.rs`,
`tracer/src/instruction/ecall.rs`, `tracer/src/emulator/{cpu,mod}.rs`, `crates/jolt-program/src/preprocess/bytecode.rs`,
`crates/jolt-program/src/expand/mod.rs`, `common/src/jolt_device.rs`.

**Status** (2026-11-03): D1, D3, D4, D5, D12 ruled (below); D2, D6-D11 and D13 deferred.

**The rows themselves agree everywhere tested**: EVM Zone trace-23 and Cyclone input/22 (every record), seven jolt-cpp
example guests (every record), both workloads at 2^29 (every 2^16-row window and the run hash), and the hand-assembled
guests both tools complete. All 15 inline sequences are identical between upstream `17a9459` and jolt-cpp's `.inc`
files (dumped from `f8fc214`). The differences below are in loading, in conventions around the rows, and in what each
does with faulty or unusual guests.

| # | difference | affects valid guests? |
|---|---|---|
| D1 | program image from segments (jolt-cpp) or sections (upstream) | **ruled: follow a16z** |
| D2 | upstream enforces the stack canary; jolt-cpp doesn't | no (overflowing guests only) |
| D3 | LegacyRawEcall host I/O exists only in jolt-cpp | **ruled: follow a16z** |
| D4 | UNIMP in the code: jolt-cpp skips it (and its pc drifts); upstream rejects the ELF | **ruled** (owner, 2026-11-02) |
| D5 | executing where no instruction was decoded statically | **ruled: the host tracer rejects it** |
| D6 | misaligned access: both abort, by different checks | no |
| D7 | EBREAK: upstream stalls (ends); jolt-cpp's interpreter ends the run, its JIT aborts | yes, if a guest reaches EBREAK |
| D8 | bytecode count: jolt-cpp counts the leading NOP | convention only |
| D9 | the layout's stack bounds are named differently | convention only |
| D10 | padding: jolt-cpp pads to the power of two above rows + 1 | to confirm on upstream's prover |
| D11 | upstream has inlines and row kinds jolt-cpp lacks (grumpkin, p256, NTT; four kinds) | yes, guests using them |
| D12 | C.SLLI with rd = x0 (a HINT): jolt-cpp rejects it as invalid, upstream runs it as a NOP | **ruled: follow a16z** |
| D13 | an rd = x0 rewrite row (ADDI x0, x0, 0) is flagged virtual in jolt-cpp, not in a16z | yes (any non-ADDI rd = x0) |

## D1. The program image: segments or sections (ruled: follow a16z)

**Ruling** (owner, 2026-11-03): follow a16z, the image and the code by sections. **Gold** (`c65bb37`): every section
at or above 0x8000_0000 goes into memory, `program_end` is the highest section end, the code is the merged Text
sections decoded in address order (gaps unmapped); program headers aren't used. All four segment files stay
byte-identical; both workloads' layout and bytecode equal jolt-cpp's and their rows a16z's, as do all seven examples.
**Reproducers** (`wip/gold/test/guests/`): `d1_rodata_in_text` (`.rodata` of all-ones bytes right after `.text`, in
one executable PT_LOAD): jolt-cpp's JIT fails at load ("failed to decode risc instruction at 0x80000010"), a16z and
gold give the same 4 rows; `d1_decodable_gap` (`.text`, a jump over a `.rodata` of four `nop` words, `.text.b`, in one
executable PT_LOAD): all three give the same rows, but a16z's bytecode puts `.text.b` (0x80000018) at index 3 of 7 and
jolt-cpp's at 7 of 11, since jolt-cpp decodes the four words as code; a proof using jolt-cpp's indices would fail
verification against a16z's preprocessing without any error before it. (`row_dump --index-of`, `upstream_rows
--index-of`.) **jolt-cpp fix**: to come (`worker18-fix-d1-sections`), covering the initial memory, `program_end` and
the code ranges.

### The difference as found

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

## D3. LegacyRawEcall host I/O (ruled: follow a16z)

**Ruling** (owner, 2026-11-03): no rewriting; a raw ECALL traps through mtvec as in a16z, with no special detection; a
program relying on legacy raw-ECALL host I/O fails as it would in a16z (a clearer error is the host tracer's job, as in
D5). **Gold** (`65289d9`): the host I/O detection is gone (`HostIo` removed); ECALL always expands to the trap. Guest
`legacy_raw_ecall` now traps to mtvec = 0 and stops with "jump target 0x0 is not in the bytecode map" (the end is only a
JAL with immediate 0, DECISIONS-v1.3 section 25). **jolt-cpp fix** (`WORKER18/fixes/jolt-cpp-d3-no-legacy-ecall.patch`,
against 88dca85): `resolve_host_io_encoding` always resolves to Standard and an explicit legacy request aborts; tested
on PR #2's standalone tracer: the workloads (trace-23, input/22 and both at 2^29) and the examples keep their run hashes;
`legacy_raw_ecall` is no longer rewritten (it traps and, through jolt-cpp's pc-0 sentinel, ends quietly after 8 rows,
where a16z and gold fail: the sentinel itself is DECISIONS section 25's subject).

### The difference as found

- **jolt-cpp** auto-detects guests that use a raw ECALL for host calls (no VirtualHostIO, no CSRRW/CSRRS/MRET) and
  traces every ECALL as `VirtualHostIO` (`host_io_encoding.cpp:47-57`, `:79`); program.json can force either mode.
- **upstream** has no such mode at 17a9459: ECALL always traps to mtvec (`tracer/src/instruction/ecall.rs:1-4`, `:39-50`).
- Test guest `legacy_raw_ecall` (a0 = 0, ECALL, no trap handler): jolt-cpp (and gold) 4 rows, the ECALL row
  `VirtualHostIO`; upstream traps to mtvec = 0 and panics decoding address 0 (`tracer/src/emulator/cpu.rs:1466`).

## D4. UNIMP in the code (ruled)

**Follow-up** (owner, 2026-11-03): 0xC0001073 (the standard 32-bit unimp, `csrrw x0, cycle, x0`) is rejected at load,
as a16z rejects it (`UnsupportedCsr(3072)`); c.unimp stays the only unreachable marker. Gold `65289d9`; guests
`unimp32_unreached` and `unimp32_executed` are rejected at load, `unimp_unreached` (c.unimp only) loads and runs. See
U3 for the a16z side.

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

## D5. Executing where no instruction was decoded statically (ruled)

**Ruling** (owner, 2026-11-03): rejecting it is the host tracer's job (its JIT maps pcs to bytecode entries), so such a
run never reaches the chip; if one did, the Tracer's "jump target not in the map" check catches it. Gold, as the host
tracer's reference, rejects it the same way: `jump_into_gap` stops with "jump target 0x8000000a is not in the bytecode
map" (gold `65289d9`; the same error for a JALR or branch to 0, DECISIONS section 25).

### The difference as found

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

## D12. C.SLLI with rd = x0, a HINT (ruled: follow a16z)

**Ruling** (owner, 2026-11-03): gold decodes c.slli x0 exactly as a16z (`SLLI x0, x0, n`, a no-op). **Gold**
(`65289d9`): `Zca-c.slli-00` now passes in Act4Runner. Guest `d12_c_slli_x0` (`.half 0x0006`): gold and a16z agree
except in one field of the rewritten row, the virtual flags, which is D13, a separate jolt-cpp-versus-a16z difference
(gold follows jolt-cpp there until ruled). **jolt-cpp fix** (`WORKER18/fixes/jolt-cpp-d12-c-slli-x0.patch`, against
88dca85; `isa/decoder.cpp` is the same at 821e1bd): C.SLLI with rd = x0 returns `slli x0, x0, shamt`; with it jolt-cpp
loads the guest and gives gold's rows; workloads and examples unchanged.

### The difference as found

- The RISC-V C extension reserves C.SLLI with rd = x0 as a HINT (a valid encoding that must execute as a no-op).
- **jolt-cpp** expands C.SLLI only for r != 0; r = 0 falls through to "reserved instruction?" and returns invalid
  (`isa/decoder.cpp:1022-1031`), unlike the other compressed HINTs it turns into NOPs (C.LI, C.MV, C.ADD with rd = 0,
  `:808-820`, `:1088`, `:1110`). Gold follows it; under the D4 ruling an invalid instruction can't be proven, so strict
  loading rejects it and the ACT4 runner throws on it.
- **upstream** uncompresses it to `SLLI x0, x0, shamt` (`crates/jolt-riscv/src/uncompress.rs:612-614`), which the
  rd = x0 rewrite turns into `ADDI x0, x0, 0`.
- Found by ACT4 `Zca-c.slli-00` (it executes C.SLLI x0 at 0x80003910): jolt-emu passes, gold's ACT4 runner aborts.

## Upstream findings that aren't jolt-cpp-versus-upstream differences

### U1. jolt-emu reports some failing ACT4 tests as passes

- ACT4's failure exit writes `(gp << 1) | 1` to tohost (`tests/arch-tests/jolt/rvmodel_macros.h:47-52`), with gp
  unmasked. `run_test` treats a tohost write as an exit only when the HTIF device byte (bits 63:56) is 0
  (`tracer/src/emulator/mod.rs:126-148`); otherwise it keeps running, reaches the `j 1b` stall and reports "exited
  successfully (code 0)" (`:117-120`). So any failing test whose gp has a bit above bit 54 set passes in upstream's CI.
- Seen in `Zalrsc-sc.d-00`: its check 241 fails (jolt-emu takes the BEQ-not-taken path to `failedtest_x5_x4` at
  0x8000b6b0, its `-d` trace shows it), gp = 0x9cadd5ae9e2ac9c5, tohost = 0x395bab5d3c55938b (device 0x39), and
  jolt-emu exits 0. Gold's ACT4 runner reads tohost directly and reports FAIL.

### U2. Upstream's ACT4 configuration contradicts its LR/SC rule

- The failing check is "cp_custom_sc_lrsc; bin: prev_lr_lr.w": `lr.w zero, (t4)` then `sc.d t6, s2, (t4)` at the same
  address. Sail, configured by upstream's `tests/arch-tests/jolt/jolt-rv64imac.yaml` with
  `LRSC_FAIL_ON_NON_EXACT_LRSC: false`, lets the SC.D succeed (the expected signature is rd = 0).
- Upstream's tracer fails it: an SC.D needs a doubleword reservation and LR.W makes a word one
  (`tracer/src/emulator/cpu.rs:535-547`, asserted by its own test `reservation_covers_follows_the_lr_sc_width_rules`,
  `:1677-1695`). jolt-cpp's SC.D expansion does the same (`isa/virtual.cpp`, SC_D: success iff the doubleword
  reservation equals the address; LR.W clears it). Gold follows jolt-cpp: rd = 1, the check fails.
- So jolt-cpp, upstream's tracer and gold agree; the ACT4 configuration (or the semantics) is what disagrees, and U1 hides it.

## D13. The rd = x0 rewrite row's virtual flags (deferred)

- **jolt-cpp** rewrites a side-effect-free instruction with rd = x0 to one `ADDI x0, x0, 0` row, and marks a row
  virtual whenever its opcode differs from the source's (`isa/virtual.cpp:174-189`, `:270-274`), so the rewrite of
  anything but ADDI is virtual and first-in-sequence (record word 1 = `0x0300000000000002`).
- **a16z** emits the rewrite with no virtual sequence (`virtual_sequence_remaining` None; word 1 = `0x2`).
- Guest `rd0_add` (`add x0, a0, a0`): jolt-cpp (and gold) `914cba73ca81d34f`, a16z `77c53666d3e73b90`, differing only in
  that row. Since a16z's bytecode is what proofs are checked against, any guest with such an instruction would fail
  verification under jolt-cpp; none of the workloads or examples has one (their rows match a16z's). Found with D12's
  guest. Not ruled; gold follows jolt-cpp.
