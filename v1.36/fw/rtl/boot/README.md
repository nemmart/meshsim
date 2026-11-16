# fw/rtl/boot: the RTL's boot ROM

*For anyone asking where the simulator's boot ROM comes from.* The boot ROM of machine-spec 6.5 is the RTL's, vendored
unchanged: every tile has it at the reset PC, 0x0200_0000, and its handler is `mtvec`'s reset value (6.2).

| file | what |
|---|---|
| `boot_rom.elf` | Snarky One's `build/core-tests/boot_rom.elf`: `rtl/blocks/core/src/boot_rom.S` assembled and linked at the ROM's base |
| `boot_rom.hex` | its `.text`, one 32-bit little-endian word a line (44 words, 176 bytes): what the simulator loads (`tests/rtl_images.h`) |
| `boot_rom.sym` | `nm`'s lines for `_start`, `wait_load`, `wait_release`, `image`, `handler` and `park`: the simulator reads the handler's address from it |

**Made from** Snarky One at commit `370e3acae6254e9e94ba594dbce293e3fa948ed6`, by its task `mise run core-images`
(xPack `riscv-none-elf-gcc` 15.2.0, binutils 2.45), then, in its checkout:

```sh
riscv-none-elf-objcopy -O binary -j .text build/core-tests/boot_rom.elf boot_rom.bin
od -An -v -tx4 -w4 boot_rom.bin | sed 's/^ *//' > boot_rom.hex
riscv-none-elf-nm build/core-tests/boot_rom.elf | grep -E ' (handler|image|park|wait_load|wait_release|_start)$' | sort > boot_rom.sym
```

The RTL holds the same words in `rtl/blocks/core/src/boot_rom_pkg.veryl`, which its `boot_rom_test` checks against this
ELF file. v1.35's own boot ROM is not in v1.36.

SHA-256:

```
eb78f6c13bb3e93253963ab69c4d7d0fb332bd8250de482b4b5251ecfea8cf09  boot_rom.elf
7b54d8368de1f8ff17cf16cedffd66debaf1d0425e406a41bacea9b389a26ac8  boot_rom.hex
54658dfd7ca20f4e609fe8f588774de49a47bd6230accb3d8ddd1df809d2abe7  boot_rom.sym
```
