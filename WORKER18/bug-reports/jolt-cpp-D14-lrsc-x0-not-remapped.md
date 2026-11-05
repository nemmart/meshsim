# jolt-cpp: LR/SC with rd = x0 keep x0, where a16z remaps them to a temporary (proofs fail verification)

**Component**: `src/tracer/isa/virtual.cpp:57-80` (`rd0_is_handled_internally`: LR.W, LR.D, SC.W, SC.D and the inlines
exempt from the rd = x0 remap, citing a16z PR #1439). jolt-cpp 88dca85.

**What happens.** a16z 7189216 exempts only ECALL, MRET, EBREAK, CSRRW and CSRRS (`crates/jolt-program/src/expand/
operands.rs:51-60`) and remaps every other side-effecting rd = x0 to a temporary (`expand/mod.rs:133-145`). So a16z's
`lr.d x0, (t0)` is `LD x40` (its own temporary at x41) and jolt-cpp's `LD x0`: different rows, and jolt-cpp's load row
of a nonzero word is unprovable (R1CS: RamRead = RdWrite) where a16z's is fine.

**Reproducers**: meshsim `wip/gold/test/guests/{lr_d_x0_nonzero, lr_w_x0_nonzero, lr_d_x0_zero, sc_d_x0, sc_w_x0}`:
jolt-cpp's rows differ from a16z's on all five (e.g. `lr_d_x0_nonzero` 8babe424… against 3154d2c9…).

**Impact**: every program with an LR or SC writing x0 (an SC whose success bit is ignored, an LR used only for its
reservation): its proof fails verification. None of the workloads or examples has one. The inlines' exemption, by
contrast, changes no row of a provable program: an inline that doesn't read rd (SHA-256, Keccak, BLAKE2, BLAKE3) gives
identical rows (guest `inline_sha256_rd0`), and one whose rd is the output address with rd = x0 writes to address 0,
unprovable either way (`inline_bigint_rd0`: a16z's tracer panics, jolt-cpp's traces it).

**Fix**: jolt-asic-planning `worker18-fix-d14-lrsc-x0` (`cdde868`, from `tracer-standalone-main` fa59d9a;
`WORKER18/fixes/jolt-cpp-d14-lrsc-x0.patch`): LR/SC removed from the exemption. With it all five reproducers give
a16z's rows; EVM Zone trace-23, Cyclone input/22 and `tracer-tests` keep their run hashes.
