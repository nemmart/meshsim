# jolt-cpp: C.SLLI with rd = x0 (a HINT) is decoded as an invalid instruction

**Component**: `src/tracer/isa/decoder.cpp`, `uncompress_instruction`, C.SLLI (`:1022-1031`). jolt-cpp `88dca85` (same at `821e1bd`).

**What happens.** The RISC-V C extension makes C.SLLI with rd = x0 a HINT, a valid encoding to execute as a no-op.
jolt-cpp returns invalid for it ("r == 0 is reserved instruction?"), so its JIT refuses the program. a16z uncompresses
it to `slli x0, x0, shamt` (`crates/jolt-riscv/src/uncompress.rs`, test `uncompresses_c_slli_x0`), traced as the rd = x0
no-op. The riscv-arch-test case `Zca-c.slli-00` executes it (a16z's jolt-emu passes it).

**Reproducer**: meshsim `wip/gold/test/guests/d12_c_slli_x0` (`li a0, 1; .half 0x0006; .half 0x0001; addi a0, a0, 1;
j .`): jolt-cpp "failed to decode risc instruction at 0x80000004"; a16z traces 5 rows.

**Expected / fix**: jolt-asic-planning branch `worker18-fix-d12-c-slli` (`2964c0a`, from `fa59d9a`;
`wip/planning/sessions/WORKER18-fixes/jolt-cpp-d12-c-slli-x0.patch`): return
`slli x0, x0, shamt`. With it jolt-cpp traces the reproducer (5 rows); workloads and examples unchanged. Its rewritten
row still differs from a16z's in the virtual flags, a separate difference (D13, fixed on
`worker18-fix-d13-rd0-noop-not-virtual`; with both fixes the reproducer equals a16z's rows, `d571a0f62e10b264`).
