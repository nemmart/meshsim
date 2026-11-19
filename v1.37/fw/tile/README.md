# fw/tile: the tile programs

*For anyone running or extending acceptance A (`tests/test_rtl_tile.cpp`).* These are the programs the RTL's own test of
its base tile boots on it (Snarky One's `rtl/tile/base/test/programs/base_tile/`, unchanged; driven there by
`rtl/tile/base/test/src/base_tile_test.rs`). `fw/build.sh` (its part `tile`) builds each from `start.S` and its own
source, by `link.ld`, into `fw/images/tile/`; the simulator's test loads them as the RTL's test does: each loadable
segment as 16-byte rows (`tests/rtl_images.h`).

| source | image | kind | what it checks (its header has the list) |
|---|---|---|---|
| `registers.S` | `registers.elf` | 0 | every unit's registers and tables through the register bus |
| `noc.S` | `noc.elf` | 1 | a round trip through the router for each sender and each receiver's path, and the events that end each wait |
| `barrier.S` | `barrier.elf` | 2 | a second barrier after the boot ROM's |
| `errors.S` | `errors.elf` | 3 | one error of `ERRORS`, raised by its unit, reaching the core and the boot ROM's handler |
| `special.S` | `special.elf` | 4 | a special tile's base (the RTL's `base_tile_special_test`): not run yet (`OPEN-v1.36.md`) |
| `replayer.S` | `replayer.elf` | 5 | a short pass through the compute tile's replayer |

`programs.toml` lists the programs in kind order; the build writes `programs.txt` from it, each program's kind and
file, which the test reads. `tile_map.h` holds the programs' addresses (the specification's map and registers), their
protocol with the test (the boot, the arguments, the reports) and their assembler macros; `start.S` is their
start-up and report; `link.ld`, through the C preprocessor, their layout: code in IMEM from its base, data in DMEM
past the reserved 16 bytes and the arguments' two rows, each segment starting a 16-byte row.

A built program is stripped of its symbols and cut after its last loaded byte, with no section headers, so the file
is small and the same on every build: a loader's view of it is all there is.
