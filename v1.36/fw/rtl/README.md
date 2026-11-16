# fw/rtl: the chip's Rust firmware, built outside meshsim

*For anyone asking which firmware images meshsim does not build yet, where they came from, and how to refresh them.*

Everything the simulator runs that is C or assembly is built in `fw/` from sources there (`../README.md`). **This
folder holds the rest: the chip's firmware that is Rust today**, as Snarky One built it, sources and images, copied
verbatim. Nothing here is built by meshsim. Each image is to be replaced by a C program built by `fw/build.sh`
(`../README.md`, "Built outside meshsim, to be replaced by C", has the table: each image, its source, and what the C
rewrite must provide), and this folder goes when the last of them is.

## Where it came from

| | |
|---|---|
| repository | Snarky One, the RTL (`snarkify-zero`) |
| commit | `370e3acae6254e9e94ba594dbce293e3fa948ed6` (also in `MANIFEST`'s header) |
| task | `mise run sort-images` (the chip's runs, into `build/sort/`) |
| Rust | 1.96.0 (`rust-toolchain.toml`), target `riscv32i-unknown-none-elf` with `+c,+zmmul,+zicond,+relax`, the firmware's `Cargo.lock` |
| configuration | `Veryl.toml`'s `[properties]`: `MESH_SIZE` 8, `IMEM_BYTES` and `DMEM_BYTES` 32768, `SCRATCHPAD_BYTES` 4194304 |

The recipes, with every flag, are `src/xtask/src/sort_images.rs` (the runs) and `core_tests.rs` (the Rust target and
features, and the core's Rust program); they are copied for reference and are not run here.

## The images

Each is an ELF file whose `PT_LOAD` segments are what the RTL's tests place or load: code in IMEM from `0x01000000`,
data in DMEM from `0x00000010` (the base tile's map, spec 5.1 and 6.3).

| file | bytes | what | how the tests use it |
|---|---|---|---|
| `sort/kv32/master.elf`, `sort/k128/master.elf` | 39,600 each | the master tile's firmware for a sort of 16 records a tile, with 32-bit and with 128-bit values, the run linked in: its run image (`../README.md`, "The run image") in `.rodata`, at the symbol `RUN_IMAGE` | placed in the master tile's IMEM and DMEM before the reset ends (`boot_local`); its summary (the `.summary` section, DMEM's first data) read after the run |
| `sort/kv32-64k/master.elf`, `sort/k128-64k/master.elf` | 39,600 each | the same at 65,536 records a tile | `test_rtl_sort`'s 64K runs |
| `sort/memory-tile/memory.elf` | 5,904 | the memory tile's firmware: serves `READ` and `WRITE` requests (spec 11.5) with the RW engine | placed in the memory tile's IMEM and DMEM before the reset ends, in every chip test |
| `sort/memory/master.elf` | 22,944 | the master's firmware for the memory run: `READ`, `WRITE` and `READ` back, then 300 `WRITE`s of 0 beats | `test_rtl_memory` |
| `sort/trace/master.elf` | 32,044 | the master's firmware for the trace run, with the segment's two images linked in | `test_rtl_segment` |
| `sort/segment/replay.elf`, `sort/segment/tracer.elf` | 11,476, 17,112 | the segment's images, which the master loads: the compute tiles' replay image and the tracer tile's | through `sort/trace/master.elf`; `test_rtl_segment` reads `tracer.elf` for the tracer tile's report |
| `sort/sort.txt` | 1,772 | Snarky One's listing of its build: the images' sizes and headroom, and the buffers' addresses | a listing |

**What of these meshsim already makes.** The run image inside each `master.elf` is made here (`tools/fwimage/`), from
the tile image built here for the sort runs, and `fw/build.sh --check` and `fw/check_images.sh` require each master to
hold it byte for byte. So of a sort run's master only the master's own code and summary are from outside.

The core's Rust test program, `firmware-rust.elf`, is with the core's other programs and the RTL's measurements of
them, in `tests/core-programs/elf/`; its source is `src/rtl/blocks/core/test/programs/firmware/` (it includes
`fw/core/start.S` and links by `fw/core/link.ld`, which are that folder's `start.S` and `link.ld` in Snarky One).

## The sources

Under `src/`, each at its path in Snarky One (`src/rtl/...` is Snarky One's `rtl/...`), as its git tracks them:

| path | what |
|---|---|
| `src/rtl/chip/test/programs/firmware/` | the chip's Rust firmware, a Cargo workspace: its programs `master.rs`, `memory.rs`, `replay.rs` and `tracer.rs` on its library, `lib.rs`, the base tile's registers and conventions; `link.ld` and `start.S` for the images placed before the reset, `loaded.ld` and `loaded_start.S` for the loaded ones |
| `src/rtl/blocks/core/test/programs/firmware/` | the core's Rust test program: `main.rs` and its Cargo files |
| `src/xtask/src/sort_images.rs`, `core_tests.rs` | the builds' recipes, for reference |

`tests/test_regs.cpp` reads the chip firmware's `lib.rs`: the registers it defines must be where
`fw/include/mesh.h` puts them.

## The check

`MANIFEST` lists the sha256 of every file under `src/` and `sort/`. `fw/check_images.sh rtl` (and
`fw/check_images.sh` with no part named) fails if a listed file is missing or changed, or if one of those folders
holds a file the list does not name.

## Refreshing

```sh
# in a Snarky One checkout, at the commit to take:
mise run sort-images
# here:
fw/rtl/refresh.sh /path/to/snarky-one
fw/check_images.sh
fw/build.sh --check
```

`refresh.sh` replaces `src/` and `sort/` and rewrites `MANIFEST` with the checkout's commit. It refuses a checkout
whose copied sources differ from its `HEAD`, and notes an image older than a source. Then update the commit and the
sizes in this file. `fw/build.sh --check` fails if a master's run image is no longer the one made here: the tile
image, the replay image or the tracer tile's image changed in Snarky One and not in `fw/`. The simulator's tests hold
the RTL's cycle figures for these images: new images need the RTL's new figures.
