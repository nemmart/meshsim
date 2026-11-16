# fw/boot: the boot ROM

*For anyone asking where the simulator's boot ROM comes from.* The boot ROM of machine-spec 6.5 is the RTL's program,
`boot_rom.S` (Snarky One's `rtl/blocks/core/src/boot_rom.S`, unchanged): every tile has it at the reset PC,
0x0200_0000, and its handler is `mtvec`'s reset value (6.2).

`fw/build.sh` (its part `boot`) assembles it, links it at the ROM's base, and writes into `fw/images/boot/`:

| file | what |
|---|---|
| `boot_rom.elf` | the program, linked at the ROM's base: 176 bytes of code |
| `boot_rom.hex` | its `.text`, one 32-bit little-endian word a line (44 words): what the simulator loads (`tests/rtl_images.h`) |
| `boot_rom.sym` | `nm`'s lines for `_start`, `wait_load`, `wait_release`, `image`, `handler` and `park`: the simulator reads the handler's address from it |

`fw/build.sh --check` requires all three equal to the build's. The RTL holds the same words in its
`rtl/blocks/core/src/boot_rom_pkg.veryl`, which its `boot_rom_test` checks against its own build of this program;
`boot_rom.hex` and `boot_rom.sym` here are byte for byte those of Snarky One's build at its commit
370e3ac. v1.35's own boot ROM is not in v1.36.
