# fw/rtl: the RTL's firmware

*For anyone running the RTL's firmware on the simulator: what each file is, where it came from, and how to refresh it.*

v1.36's simulator models the RTL's hardware, so the RTL's own firmware images run on it unmodified. This folder holds
that firmware as Snarky One built it, sources and images, copied verbatim. **Nothing here is built by meshsim:** not
by the `Makefile`, not by `fw/build.sh`. The images are built in Snarky One, by its own pinned toolchain, and
`refresh.sh` copies them.

## Where it came from

| | |
|---|---|
| repository | Snarky One, the RTL (`snarkify-zero`) |
| commit | `370e3acae6254e9e94ba594dbce293e3fa948ed6` (also in `MANIFEST`'s header) |
| tasks | `mise run core-images` (the boot ROM and the tile programs, into `build/core-tests/`) and `mise run sort-images` (the chip's runs, into `build/sort/`) |
| C and assembly | xPack `riscv-none-elf-gcc` 15.2.0-1 (`mise.lock`), `-march=rv32ic_zicond_zicsr_zmmul -mabi=ilp32` |
| Rust | 1.96.0 (`rust-toolchain.toml`), target `riscv32i-unknown-none-elf`, the firmware's `Cargo.lock` |
| configuration | `Veryl.toml`'s `[properties]`: `MESH_SIZE` 8, `IMEM_BYTES` and `DMEM_BYTES` 32768, `SCRATCHPAD_BYTES` 4194304, `LPDDR_ADDRESS_BITS` 32, `RW_BUFFER_BYTES` 16384, `RW_QUEUE_BEATS` 16, `STREAM_BYTES` 4 |

The recipes, with every flag, are `src/xtask/src/core_tests.rs` (the boot ROM), `tile_tests.rs` (the tile programs)
and `sort_images.rs` (the runs); they are copied for reference and are not run here.

**The builds are reproducible but for one name.** Two builds of the same commit give the same bytes in every file
here, except that `boot/boot_rom.elf` and `sort/tile/sort.elf`, which keep their symbols, each hold the name of GCC's
temporary object file (a `FILE` symbol, `cc` and six random characters, `.o`). Their loaded segments are the same:
`sort.elf`'s are what every sort run's `master.elf` carries, and those files are identical between builds.

## The images

Each is an ELF file whose `PT_LOAD` segments are what the RTL's tests place or load: code in IMEM from `0x01000000`,
data in DMEM from `0x00000010` (the base tile's map, spec 5.1 and 6.3).

| file | bytes | what | how the RTL's tests use it |
|---|---|---|---|
| `boot/boot_rom.elf` | 5,116 | the boot ROM's program (spec 6.5), every tile's: 176 bytes at the reset PC, `0x02000000`; its handler, `mtvec`'s reset value, at `0x02000068` | `boot_rom_test` checks the RTL's ROM (`boot_rom_pkg.veryl`'s halfwords) equals it, word for word; the simulator's ROM must hold the same words |
| `tile/base_tile/programs.txt` | 82 | the tile programs: one a line, its kind and its ELF file | `base_tile_test`, as the master tile, boots each on one tile through the boot ROM: at the tile's `LOAD`, the program's segments by uncounted 128-bit `WRITE`s, a row each, then its arguments' two rows (argument 0 its kind), then a `COUNT` on counter 7; `tile_map.h` gives the protocol and the reports |
| `tile/base_tile/registers.elf`, `noc.elf`, `barrier.elf`, `errors.elf`, `replayer.elf` | 1,070, 2,242, 338, 1,600, 718 | kinds 0, 1, 2, 3 and 5: the tile window's registers; the tile's packets; the barrier; the errors; the replayer's window. Stripped, with no section headers | as above; each reports `REPORT_PASS` or `REPORT_FAIL` to the master tile |
| `tile/base_tile/special.elf` | 1,068 | kind 4: a special tile's base | `base_tile_special_test` places it in IMEM and DMEM before the reset ends (`boot_local`) |
| `sort/kv32/master.elf`, `sort/k128/master.elf` | 39,600 each | the master tile's firmware for a sort of 16 records a tile, with 32-bit and with 128-bit values, the run linked in: its run image (`sort_images.rs`, `run_image`: a header, then the tile image's IMEM and DMEM rows) in `.rodata` | `chip_test` places it in the master tile's IMEM and DMEM during the reset (`boot_local`), and reads its summary (the `.summary` section, DMEM's first data) after the run |
| `sort/kv32-64k/master.elf`, `sort/k128-64k/master.elf` | 39,600 each | the same at 65,536 records a tile | `chip_test`, in the release tier |
| `sort/tile/sort.elf` | 28,680 | the tile image: meshsim v1.25's sort, split and skew planner on the RTL's port layer (`PORTING.md`); the master loads it on every compute tile | the test reads only its layout table (`.sort_layout`: the buffers `BUF_IN`, `BUF_VIN`, `BUF_Y`, `BUF_VS`); its code and data reach the tiles through `master.elf` |
| `sort/memory-tile/memory.elf` | 5,904 | the memory tile's firmware: serves `READ` and `WRITE` requests (spec 11.5) with the RW engine | placed in the memory tile's IMEM and DMEM during the reset, in every chip test |
| `sort/memory/master.elf` | 22,944 | the master's firmware for the memory run: `READ`, `WRITE` and `READ` back, then 300 `WRITE`s of 0 beats | `chip_memory_test` |
| `sort/trace/master.elf` | 32,044 | the master's firmware for the trace run, with the segment's two images linked in | `chip_trace_test` |
| `sort/segment/replay.elf`, `sort/segment/tracer.elf` | 11,476, 17,112 | the segment's images, which the master loads: the compute tiles' replay image and the tracer tile's | through `sort/trace/master.elf`; `chip_trace_test` reads `tracer.elf` for the tracer tile's report |
| `sort/sort.keys` | 4,356 | the keys of the runs of 16 records a tile: SortTest's key file, little-endian 32-bit words: the magic `0x4B455953`, the 64 tiles' record counts, then the keys tile by tile | `chip_test` computes each tile's input and its expected result from them |
| `sort/sort.txt` | 1,772 | the images' sizes and headroom, and the buffers' addresses | a listing |
| `sort/sort-64k.keys.sha256` | | the sha256 of `sort-64k.keys`, the keys of the runs of 65,536 records a tile | |

**Not copied.** `sort-64k.keys` is 16,777,476 bytes (16 MiB), too large to commit: `mise run sort-images` writes it to
Snarky One's `build/sort/`. It is SortTest's built-in `uniform` set (`sort_images.rs`, `sort_test_keys`:
`java.util.Random` at the seed 11 + `"uniform".hashCode()`, 65,536 keys a tile), in SortTest's key file format, which
`tests/sort_input.h`'s `read_keys` reads. Check a copy with `sha256sum -c sort-64k.keys.sha256` beside it, and keep it
outside `fw/rtl/`, whose check lists every file. The core's own 49 programs (riscv-tests, the firmware test and
CoreMark, `build/core-tests/elf/`) are not copied either: nothing here runs them yet.

## The sources

Under `src/`, each at its path in Snarky One (`src/rtl/...` is Snarky One's `rtl/...`), as its git tracks them:

| path | what |
|---|---|
| `src/rtl/blocks/core/src/boot_rom.S` | the boot ROM's program; `boot_rom_pkg.veryl` beside it is the RTL's ROM, its halfwords written by hand, with the handler's offset, `HANDLER` = `0x68` |
| `src/rtl/tile/base/test/programs/base_tile/` | the tile programs: assembly, `start.S`, `link.ld`, `programs.toml` (the kinds) and `tile_map.h`, their addresses, fields and protocol |
| `src/rtl/chip/test/programs/sort/` | the tile's sort: `sw/`, `split-bisect/` and `skew-planner/`, vendored from meshsim v1.25, on the port layer `port/` (`mesh.h`, `crt0.S`, `port.c`, `sort.ld`); `PORTING.md` lists every change |
| `src/rtl/chip/test/programs/firmware/` | the chip's Rust firmware, a Cargo workspace: its programs `master.rs`, `memory.rs`, `replay.rs` and `tracer.rs` on its library, `lib.rs`, the base tile's registers and conventions; `link.ld` and `start.S` for the images placed before the reset, `loaded.ld` and `loaded_start.S` for the loaded ones |
| `src/xtask/src/core_tests.rs`, `tile_tests.rs`, `sort_images.rs` | the builds' recipes, for reference: the flags, the audits, the run image's format, the keys |

`tests/test_regs.cpp` reads `tile_map.h` and the firmware's `lib.rs`: the registers they define must be where `fw/include/mesh.h`
puts them.

## The check

`MANIFEST` lists the sha256 of every file under `src/`, `boot/`, `tile/` and `sort/`. `fw/check_images.sh rtl` (and
`fw/check_images.sh` with no part named) fails if a listed file is missing or changed, or if one of those folders
holds a file the list does not name.

## Refreshing

```sh
# in a Snarky One checkout, at the commit to take:
mise run core-images && mise run sort-images
# here:
fw/rtl/refresh.sh /path/to/snarky-one
fw/check_images.sh rtl
```

`refresh.sh` replaces `src/`, `boot/`, `tile/` and `sort/` and rewrites `MANIFEST` with the checkout's commit. It
refuses a checkout whose copied sources differ from its `HEAD`, and notes an image older than a source. Then update
the commit and the sizes in this file, and read `git diff`: a refresh at the same commit changes only the temporary
name in `boot/boot_rom.elf` and `sort/tile/sort.elf`.
