# fw/rtl/tile: the RTL's tile programs

*For anyone running or extending acceptance A (`tests/test_rtl_tile.cpp`).* These are the programs the RTL's own test of
its base tile boots on it (Snarky One's `rtl/tile/base/test/programs/base_tile/`, driven by
`rtl/tile/base/test/src/base_tile_test.rs`), vendored unchanged as the ELF files its build writes. The simulator's
test loads them as that test does: each loadable segment as 16-byte rows (`tests/rtl_images.h`).

| file | kind | what it checks (its source's header has the list) |
|---|---|---|
| `registers.elf` | 0 | every unit's registers and tables through the register bus |
| `noc.elf` | 1 | a round trip through the router for each sender and each receiver's path, and the events that end each wait |
| `barrier.elf` | 2 | a second barrier after the boot ROM's |
| `errors.elf` | 3 | one error of `ERRORS`, raised by its unit, reaching the core and the boot ROM's handler |
| `special.elf` | 4 | a special tile's base (the RTL's `base_tile_special_test`): not run yet |
| `replayer.elf` | 5 | a short pass through the compute tile's replayer |
| `programs.txt` | | each program's kind and file, as the RTL's build lists them |

**Made from** Snarky One at commit `370e3acae6254e9e94ba594dbce293e3fa948ed6`, by its task `mise run core-images`
(xPack `riscv-none-elf-gcc` 15.2.0; `-march=rv32ic_zicond_zicsr_zmmul`), which writes
`build/core-tests/tile/base_tile/`; copied from there.

SHA-256:

```
dd38bc3e685bdecb4126d1f3f5ec26705210b77148cbbc9a2b2471f88fd11d42  barrier.elf
a6dc8bf0d8ecb68991b68a9c4f31bbd467aa10f552b3e46651aa152435e20a0d  errors.elf
196b95cdd53ff1bef11a529da0d65c64488f79250d915e0c30b970f670bf9da3  noc.elf
f5f29798c7b3ac31c9e9c26b661a08fbfacb407c4804d2507dcaa8ba2a65196a  programs.txt
404b7be27743a897c1a37392c289a27c5641bd111e893e619b93f506889df829  registers.elf
2d42ab5463321f58c99884a094416fe933e0a13aafb3817c707f9aef752aef34  replayer.elf
1965f7dfbaed4cb01a373721ef6fdaa3f55bac95f59808645b5354daa9b9d0b6  special.elf
```
