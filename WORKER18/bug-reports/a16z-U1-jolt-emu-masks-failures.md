# a16z/jolt: jolt-emu reports some failing riscv-arch-test cases as passes

**Where**: `tracer/src/emulator/mod.rs`, `run_test` (`:117-148` at 17a9459, unchanged at 7189216); with
`tests/arch-tests/jolt/rvmodel_macros.h` (`RVMODEL_HALT_FAIL`, `:47-52`).

**What happens.** A failing ACT4 test writes `(gp << 1) | 1` to tohost, with gp unmasked. `run_test` treats a tohost
write as an exit only when the HTIF device byte (bits 63:56) is 0; otherwise it keeps running, reaches the halt loop
`j 1b`, sees the pc stall and returns 0 ("Program exited successfully"). So any failing test whose gp has a bit at 55
or above set is reported as passing, and CI's arch-tests job doesn't see it.

**Observed**: `Zalrsc-sc.d-00` (built by `make arch-tests-generate` at 17a9459): `jolt-emu -d` shows the BEQ at
0x8000b6ac not taken and the jump to `failedtest_x5_x4` (check 241); gp = 0x9cadd5ae9e2ac9c5, tohost =
0x395bab5d3c55938b (device 0x39); exit code 0.

**Suggested fix**: in `RVMODEL_HALT_FAIL`, write a payload that fits HTIF (e.g. `((gp & 0x7fffffffffff) << 1) | 1`, or
a fixed code with gp reported separately); and/or have `run_test` treat any nonzero tohost write other than the pass
code as a failure, and a stall without a tohost write as "no verdict" rather than a pass.
