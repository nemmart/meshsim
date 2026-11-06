# jolt-cpp: legacy raw-ECALL host I/O rewrites ECALL, which a16z doesn't (proofs can fail verification)

**Component**: `src/tracer/host_io_encoding.cpp` (`resolve_host_io_encoding`, `apply_host_io_encoding`), used by
`compressed_trace_gen.cpp`, `isa/virtual.cpp` (`from_riscv_code`), `trace_owner.cpp`, `transcode.cpp`. jolt-cpp `88dca85`.

**What happens.** When a guest's code has a raw ECALL and no VirtualHostIO or trap runtime, jolt-cpp auto-detects
"LegacyRawEcall" and turns every ECALL into `VirtualHostIO` in the bytecode (one row). a16z Jolt has no such mode:
ECALL always expands to the trap through mtvec (`tracer/src/instruction/ecall.rs`), and its bytecode is what jolt-cpp's
proofs are checked against, so the two bytecodes differ for any such guest.

**Reproducer**: meshsim `wip/gold/test/guests/legacy_raw_ecall` (`li a0, 0; ecall; li a1, 3; j .`): jolt-cpp 4 rows with
the ECALL as `VirtualHostIO`; a16z traps to mtvec = 0 and fails decoding address 0.

**Expected**: no detection and no rewriting; a program relying on raw-ECALL host I/O fails as under a16z.
**Fix**: jolt-asic-planning branch `worker18-fix-d3-no-legacy-ecall` (`443e09e`, from `fa59d9a`;
`wip/planning/sessions/WORKER18-fixes/jolt-cpp-d3-no-legacy-ecall.patch`): `resolve_host_io_encoding` always resolves to Standard; an explicit
`legacy-raw-ecall` request aborts. Tested on PR #2's standalone tracer: EVM Zone and Cyclone (small inputs and 2^29)
and seven example guests keep their run hashes; the reproducer is no longer rewritten. (It then traps and ends at
jolt-cpp's internal pc-0 sentinel after 8 rows instead of failing; ending only at `j .` is a separate change.)
