# Item 113: differential guest programs

These are small bare RV64 programs, one per potential difference between jolt-cpp and upstream Jolt at LayerZero-Research/jolt@735355e (spec item 113). Each file's header comment gives:
- **jolt-cpp:** the rows jolt-cpp is expected to produce, read from its source at 4f36b3d (the gold standard);
- **735355e:** what a reading of 735355e's source suggests, unconfirmed.

They are **written now, to run later**: through the Rust tracer at 735355e and through jolt-cpp, diffing the rows. The replayer comparison follows from jolt-cpp's rows.

| file | case | what it executes |
|---|---|---|
| `lrd_x0.S` | LR/SC with rd = x0 | `lr.d x0, (a0)` |
| `scd_x0.S` | LR/SC with rd = x0 | `lr.d t1, (a0)`, then `sc.d x0, a1, (a0)` (succeeds) |
| `nop_hint.S` | the shared NOP row's virtual flag | `add x0, a1, a2`; `slli x0, a1, 3`; `mul x0, a1, a2`; `addi x0, x0, 0`; `addi x0, a1, 5` |
| `jalr_self.S` | the self-jump rule | `self: jalr t0, 0(t0)` with t0 = self |
| `jalr_zero.S` | a new pc of 0 from a JALR (N6) | `jalr x0, 0(t0)` with t0 = 0 |

**Common to all of them:**
- **Load address:** linked at 0x80000000 (`guest.ld`), where both tracers load code. There are two segments: `.text` alone in the executable one, and data and stack in a separate, page-aligned, non-executable one, because jolt-cpp decodes the whole executable segment as code (as the tracer session's `guest-split.ld` does). Every guest has at least one data word, so no empty segment is emitted.
- **No jolt-sdk:** each program sets sp itself.
- **End:** each ends with `j .`, which both tracers treat as the end.
- **Markers:** the rows under test sit between `MARK(0x111)` and `MARK(0x222)`, two `addi t6, x0, n` rows. They can be found as rd_reg 31 with rd 0x111 and 0x222.

**Build:** `./build.sh` writes `build/<name>.elf` and `.dis` (gcc-riscv64-unknown-elf, `rv64imac`).

## A branch that wraps to 0: not constructible as a guest

A conditional branch reaches only ±4 KiB, and a JAL ±1 MiB. With code at 0x80000000, no branch target can be 0 or wrap to it. A guest would have to be linked within 4 KiB of address 0, or of 2⁶⁴. Neither tracer loads code there:
- jolt-cpp asserts every new pc lies in the code range, apart from 0 (`trace_owner.cpp:118`).
- In the Rust emulator, memory below DRAM_BASE is the I/O region.

So in practice only a JALR can produce a new pc of 0 (`jalr_zero.S`).

The branch rule can still be checked at unit level. jolt-cpp: a `vec.cpp`-style call of `execute_instruction` plus `finish_instruction`, with pc = 8 and a BEQ immediate of −8, where it is expected to end (pc 0, instr_idx 0). The Rust side: a `Cpu` unit test with `pc` set directly, which can't fetch there, so only the branch's own effect on pc is checkable. I suggest item 113 records this case as **not constructible in a guest**.

## Running them later

These are open (item 113, point 4):
1. **The Rust tracer at 735355e:** the `tracer` crate's binary, or `jolt_tracer::trace` on the ELF. Convert its `Cycle`s to rows via its bytecode preprocessing.
2. **jolt-cpp's tracer at 4f36b3d:** convert its `RiscCycle`s, with `instr_idx`, to the 4.19.12 layout, as for the reference rows.
3. **Diff** the rows between the markers, and the rows after them up to the padding.
