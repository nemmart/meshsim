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
