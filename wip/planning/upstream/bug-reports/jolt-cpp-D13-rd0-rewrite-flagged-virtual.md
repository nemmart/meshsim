# jolt-cpp: the rd = x0 rewrite row is flagged virtual; a16z's isn't (proofs fail verification)

**Component**: `src/tracer/isa/virtual.cpp:174-189` (`emit_instr`: a row is virtual iff the expansion has more than one
row or its opcode differs from the source's) and `:270-274` (`should_nop_for_rd0`: a side-effect-free instruction with
rd = x0 becomes one `ADDI x0, x0, 0`). jolt-cpp 88dca85 (same at 821e1bd).

**What happens.** For any such instruction other than ADDI the rewrite changes the opcode, so jolt-cpp flags the row
virtual and first-in-sequence (`virtual_sequence_remaining` Some(0)). a16z returns the no-op directly, with no virtual
sequence (`crates/jolt-program/src/expand/mod.rs:144-145`, `noop_for`; 7189216). The bytecode rows differ, and a16z's is
what jolt-cpp's proofs are verified against.

**Reproducer**: meshsim `wip/gold/test/guests/rd0_add` (`li a0, 1; add x0, a0, a0; addi a0, a0, 1; j .`): jolt-cpp
`914cba73ca81d34f`, a16z `77c53666d3e73b90`, differing only in that row's word 1 (`0x0300000000000002` against `0x2`).

**Impact**: every program with a non-ADDI, side-effect-free instruction writing x0 (HINT encodings such as `add x0, …`,
`lui x0, …`, `slli x0, …`, including the C.SLLI x0 of D12): its proof fails verification. None of EVM Zone, Cyclone or
jolt-cpp's examples has one.

**Fix**: jolt-asic-planning branch `worker18-fix-d13-rd0-noop-not-virtual` (`a852cd3`, from `tracer-standalone-main` fa59d9a;
`wip/planning/sessions/WORKER18-fixes/jolt-cpp-d13-rd0-noop-not-virtual.patch`): after the no-op's `emit_instr`, `is_virtual` and
`is_first_in_sequence` are cleared. With it `rd0_add` gives a16z's rows (`77c53666d3e73b90`); EVM Zone trace-23,
Cyclone input/22, `tracer-tests`, `inlines` and `fibonacci` keep their run hashes.
