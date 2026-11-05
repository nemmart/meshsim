# a16z/jolt: the standard 32-bit `unimp` (0xC0001073) makes an ELF unprovable

**Where**: `crates/jolt-program/src/image/decode.rs:195-206` (`decode_system`: CSRRW decodes for any CSR) and the
expansion, which rejects CSR 0xC00 (`UnsupportedCsr(3072)`); at 17a9459 (unchanged at 7189216).

**What happens.** `unimp`, the canonical 32-bit illegal instruction (`csrrw x0, cycle, x0`, 0xC0001073), is what
assemblers and compilers emit for `unimp`/`__builtin_trap` paths and as padding after code that can't return. a16z
skips the compressed `c.unimp` (0x0000) at decode (`image/elf.rs:126-134`) but not the 32-bit form: anywhere in a Text
section, even never reached, it makes `build_jolt_program` fail with `Expansion(UnsupportedCsr(3072))`.

**Reproducer**: meshsim `wip/gold/test/guests/unimp32_unreached` (`li a0, 1; j .; unimp`): a16z refuses the ELF.

**Suggested fix**: treat 0xC0001073 like c.unimp: an unreachable marker (UNIMPL) that fails only if executed.
