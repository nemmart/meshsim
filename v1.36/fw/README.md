# fw: the firmware

*For firmware authors: where the register interface and its reference are, which firmware the simulator runs, how it
is built here, and what is still built outside.*

v1.36's hardware is the RTL's, so **the firmware of v1.36 is the RTL's firmware**: the programs Snarky One's tests run,
run here unmodified, byte for byte. **Every image that is C or assembly is built in this folder, from sources in this
folder** (`build.sh`), and each is proved equal to its committed copy. The chip's programs that are Rust today (the
master tile's, the memory tile's, the tracer tile's and the replay image) are not built here yet: they are committed
as Snarky One built them, in `rtl/`, until they are rewritten in C ("Built outside meshsim", below). meshsim's own
firmware of v1.35 (on v1.35's map and commands) is not carried over (`../README.md`, "What v1.36 does not carry
over").

| | what |
|---|---|
| `include/mesh.h` | the tile-program interface: every register and window as C structs and constants, in v1.36's names |
| `REGISTERS.md` | the programmer's register reference |
| `build.sh` | builds every C and assembly image from the sources below; `--check` proves each byte-equal to its committed copy |
| `check_images.sh` | checks every committed image against its hash list, without the toolchain |
| `boot/` | the boot ROM's program, `boot_rom.S` (6.5) |
| `tile/` | the tile programs: assembly (`registers`, `noc`, `barrier`, `errors`, `special`, `replayer`), with `start.S`, `link.ld`, `tile_map.h` and the list `programs.toml` |
| `sort/` | the sort's tile image: the owner's v1.25 sort, split and skew planner in C (`sw/`, `split-bisect/`, `skew-planner/`) on the RTL's port layer (`port/`); `sort/PORTING.md` lists every change |
| `core/` | the core's own programs: the start-up and link script of the RTL's test memory (`start.S`, `link.ld`), `isa_edges.S`, the four interrupt programs with `events.h`, and what the third-party programs are built with: riscv-tests' environment header and selection (`core/riscv-tests/`), CoreMark's port and flags (`core/coremark/`) |
| `images/` | the committed images `build.sh` builds, with their hash list, `MANIFEST`: `boot/` (`boot_rom.elf`, and what the simulator reads of it: `boot_rom.hex`, its words, and `boot_rom.sym`, its symbols), `tile/` (the tile programs and their list), `sort/` (the tile image, `sort.elf`, in its folder `tile/`; the 16-record runs' keys, `sort.keys`; and the sha256 of the 64K runs' key file) |
| `rtl/` | **built outside meshsim:** the chip's Rust firmware, sources and images, copied from Snarky One, with its hash list and `refresh.sh`. Its `README.md` says where each file came from |

The core's own programs' images are a test fixture, with the RTL's measurements of each: `tests/core-programs/elf/`
(`tests/core-programs/README.md`).

## Building

```sh
fw/build.sh --check       # build everything, and require each image byte-equal to its committed copy
fw/build.sh               # build everything, and rewrite fw/images/ and its MANIFEST
fw/check_images.sh        # no toolchain: the committed images against their hash lists
```

**The toolchain** is the one the committed images were built with: xPack `riscv-none-elf-gcc` 15.2.0-1 (GCC 15.2.0,
binutils 2.45), on `PATH` or named by `RISCV_PREFIX` (a prefix: the path of its `bin` folder, then
`riscv-none-elf-`). `build.sh` refuses another compiler, and says where to get this one. Every image is built for the
core's ISA, `-march=rv32ic_zicond_zicsr_zmmul -mabi=ilp32`, and for the RTL's default configuration: an 8 × 8 mesh,
32 KiB of IMEM and of DMEM, a 4 MiB scratchpad. The flags, start-up files and link scripts are the RTL's recipes',
step by step (Snarky One's `mise run core-images` and `mise run sort-images`); `build.sh`'s header lists the parts.

**Without the toolchain everything else builds and runs:** the images are committed, and `check_images.sh` checks them
against the hash lists beside them. The runner's step `fw-build` is `build.sh --check` when the toolchain is there and
is printed as SKIPPED, with the reason, when it is not; its step `fw-images` is `check_images.sh`, always.

**Two builds give the same bytes.** Every image is linked from named object files in the build's own folder. (The
RTL's recipes link straight from the sources, and GCC then names a temporary object file at random, which the linker
records in the symbol table: an ELF file that keeps its symbols differed from build to build in those eight
characters. The images committed here are linked from named objects; what a loader takes from each, its entry point
and its segments' addresses, sizes and bytes, is what Snarky One's build gives. `fwimage loaded` writes exactly that
from an ELF file, to compare two: `tools/README.md`.)

**The third-party programs** are built only from a checkout, as v1.35 built riscv-tests': `RISCV_TESTS=DIR` (a
riscv-tests checkout at the revision `core/riscv-tests/riscv-tests.toml` pins) rebuilds the 46 riscv-tests programs,
`COREMARK=DIR` (CoreMark at the revision `core/coremark/coremark.toml` pins) the two CoreMark runs. Without them
`build.sh` builds the rest and says which it left; their images are committed with the others.

## The images, and what runs them

| image | sources | `build.sh`'s part | what it does | run by |
|---|---|---|---|---|
| `images/boot/boot_rom.elf` | `boot/boot_rom.S` | `boot` | the boot ROM's program (6.5): the pull boot by `LOAD`, `boot_local`, the handler; 176 bytes at the reset PC, `0x02000000` | every tile of every test boots through it; `test_rtl_tile` and `test_core_programs` check its cycles |
| `images/tile/`: the six programs and `programs.txt` | `tile/` | `tile` | the RTL's tile programs, each with its kind: the tile window's registers, the tile's packets, the barrier, the errors, a special tile's base, the replayer's window (`tile/README.md`) | `test_rtl_tile` |
| `images/sort/tile/sort.elf` | `sort/` | `sort` | the sort: meshsim v1.25's sort, split and skew planner on the RTL's port layer; the master loads it on every compute tile | `test_rtl_sort`, through each run's master |
| `images/sort/sort.keys` | `tools/fwimage/` | `runs` | the keys of the runs of 16 records a tile: SortTest's `uniform` set, in SortTest's key file | `test_rtl_sort` |
| in `tests/core-programs/elf/`: `firmware-isa_edges.elf`, `irq_wfi.elf`, `nmi_wfi.elf`, `nmi_run.elf`, `trap_nmi.elf` | `core/` | `core` | the core's directed programs: the ISA's edges, and the interrupts and waits | `test_core_programs` |
| in `tests/core-programs/elf/`: the 46 riscv-tests programs | riscv-tests, with `core/riscv-tests/riscv_test.h` | `riscv`, with `RISCV_TESTS` | riscv-tests' rv32ui, rv32um and rv32uzicond | `test_core_programs` |
| in `tests/core-programs/elf/`: `coremark.elf`, `coremark-bench.elf` | CoreMark, with `core/coremark/` | `coremark`, with `COREMARK` | CoreMark, 1 iteration and 10 | `test_core_programs` |

**Which header a program takes its addresses from.** `include/mesh.h` is generated for a program written against
v1.36's map here. The RTL's programs keep their own headers, because the images must not change by a byte and neither
header can be replaced by `mesh.h` without rewriting the programs: the tile programs' `tile/tile_map.h` also holds
their protocol with the test and their assembler macros, and takes the configuration from the build, under names
`mesh.h` defines itself; the sort's `sort/port/mesh.h` is the port layer, meshsim v1.25's `mesh.h` names at the
RTL's addresses (`sort/PORTING.md`). `tests/test_regs.cpp` requires `tile_map.h`'s registers to be where `mesh.h`
puts them. The sources are Snarky One's bytes at the commit `rtl/MANIFEST` names, so their comments name its build
("cargo xtask", "Veryl.toml"): here that is `build.sh` and its configuration.

## Built outside meshsim, to be replaced by C

These images are the chip's firmware that is Rust today. They are copied from Snarky One's build (`rtl/README.md`),
checked against `rtl/MANIFEST`, and never built here. Each will be replaced by a C program built by `build.sh`.
The sources named are copied under `rtl/src/`, at their paths in Snarky One.

| image | its source in Snarky One | what it is | what the C rewrite must provide |
|---|---|---|---|
| `rtl/sort/<run>/master.elf`, for the runs `kv32`, `k128`, `kv32-64k`, `k128-64k`, `memory` and `trace` | the chip firmware's `master.rs`, on its library, `lib.rs`, with `start.S` and `link.ld` | the master tile's program, one build a run, the run image linked in: the load, the barriers' releases, the exits, the links' latency and the line checks of a sort run; the requests of the memory run; the trace run's load and three releases | `main` (which points `mtvec` at its own trap entry) and `trap`, neither returning, behind `start.S` (`_start` at IMEM's base); the summary as DMEM's first data (section `.summary` at `0x10`, 5,568 bytes: `Summary`, a header of four words, `done` first, then the run's own), which the tests read by word; the run image read at the symbol `RUN_IMAGE` ("The run image", below); no division helper. Its code is 4,916 bytes today |
| `rtl/sort/memory-tile/memory.elf` | the chip firmware's `memory.rs`, with `start.S` and `link.ld` | the memory tile's program: `READ` and `WRITE` requests served with the RW engine (11.5) | `main` behind `start.S`, placed before the reset ends (`boot_local`); `mtvec` left at the boot ROM's handler; no data. 514 bytes of code today |
| `rtl/sort/segment/replay.elf` | the chip firmware's `replay.rs`, on `lib.rs`, with `loaded_start.S` and `loaded.ld` | the compute tiles' replay image: the replayer's controller loop for one window (17.8.13, 17.9.10) | a loaded image: `_start` at IMEM's base, its loaded data from `0x20`, in whole rows; `.bss` zeroed by its start-up, the core-fed set's tables in `.noinit` (6 KiB); its end a jump to the reset PC. 3,326 bytes of code today |
| `rtl/sort/segment/tracer.elf` | the chip firmware's `tracer.rs`, on `lib.rs`, with `loaded_start.S` and `loaded.ld` | the tracer tile's image: feeds the Tracer and delivers each window (17.9.10) | as the replay image; and its report (section `.summary`, not loaded, 832 bytes), which `test_rtl_segment` reads from the tracer tile's DMEM. 5,750 bytes of code today |
| `tests/core-programs/elf/firmware-rust.elf` | the core firmware's `main.rs`, with `core/start.S` and `core/link.ld` | the core's Rust test program, one of the RTL's 49 | a `main` behind `core/start.S` that returns 0 for a pass. Its references (`PROGRAMS`, `RETIRE.sha256`) are the RTL's run of these bytes: a rewrite needs the RTL's run of the new program |

`rtl/src/xtask/src/sort_images.rs` and `core_tests.rs` are the recipes of these builds (the Rust target and
features, the link arguments, the audits). **A rewrite changes the images' instructions, and so every cycle figure of
the chip's tests** (`../README.md`, Status: the sort's, the segment's, the memory run's): those are the RTL's run of
the Rust images, and the RTL must run the C images for the new figures.

### The frame the C programs drop into

What a C program of the chip is built with is already here, and `build.sh`'s part `runs` builds the data it is linked
around:

- **The placed images** (the master's, the memory tile's): the chip firmware's `start.S` (the global pointer, the
  stack pointer, `call main`) and `link.ld`: code in IMEM from `0x01000000`, its first word `_start`; `.summary`,
  then `.rodata`, `.data`, `.bss` in DMEM from `0x10`; the stack down from DMEM's top; `IMEM_BYTES` and `DMEM_BYTES`
  given with `--defsym`. The tests place `.bss`'s zeros with the image.
- **The loaded images** (the replay image, the tracer tile's): `loaded_start.S` (which also zeroes `.bss`) and
  `loaded.ld`: code from `0x01000000`, `.rodata` and `.data` from `0x20` (past the 16 reserved bytes and the
  arguments' row), then `.bss`, `.noinit` and `.summary`, none of them landed.
- **The run's object**: `fw/build/out/runs/<run>/run.o`, the run image as read-only data, 16-byte aligned, with one
  symbol, `RUN_IMAGE`, at its start; a master's link takes it as one more object.

Those four files move beside the C sources when the first of them is written; until then they stay with the Rust
sources that include them.

### The run image

A master's image holds its run as data: the run image, at the symbol `RUN_IMAGE`. **meshsim makes it**
(`tools/fwimage/`, from the format below, which is `sort_images.rs`'s `run_image` and `trace_run_image`), and both
`build.sh --check` and `check_images.sh` require each committed master to hold, byte for byte, the run image made
here: so the tile image inside the four sort masters is the one built from `sort/`, and only the master's own code is
still from outside.

Little-endian 32-bit words:

| words | what |
|---|---|
| 0 | the magic, `0x5352554E` ("SRUN") |
| 1 | the kind: 0 a sort run, 1 the memory run, 2 the trace run |
| 2, 3 | a sort run's format (0 kv32, 1 k128) and its records a tile; else 0 |
| 4 | the address of the image's arguments' row (`0x10` for the sort's image, which gets the records and the format there), or 0 for none |
| 5 | the tile count, 64 |
| 6, 7, 8 | the image's IMEM rows, its DMEM rows, and its DMEM rows' load address (`0x20`) |
| then | the IMEM rows, then the DMEM rows, four words a row |
| the trace run only | the tracer tile's image likewise: its IMEM rows, its DMEM rows, their load address, then the rows |

An image's rows are its ELF file's loaded segments' file bytes, the code's from IMEM's base and the data's from
`0x20`, each padded with zeros to whole 16-byte rows. The memory run has no image: its run image is the nine words.
The sort runs' image is `images/sort/tile/sort.elf`; the trace run's are `rtl/sort/segment/replay.elf` and
`tracer.elf`.

**The keys** are SortTest's built-in `uniform` set: `java.util.Random`'s `nextInt()` from the seed
11 + `"uniform".hashCode()`, tile by tile, written as SortTest's key file (the magic `0x4B455953`, the 64 tiles'
record counts, the keys). `fwimage keys 16 FILE` is `images/sort/sort.keys`; `fwimage keys 65536 FILE` is the 64K
runs' file (16 MiB, not committed; its sha256 is `images/sort/sort-64k.keys.sha256`), which the runner's step
`rtl-sort-64k` builds into `build/`.

## The register interface

`include/mesh.h` and `REGISTERS.md` are generated from the register table (`tools/regs/`); edit the table, never the
files (`tools/README.md`). The specification is their source of truth (`spec/machine-spec.md` chapters 2, 4, 5, 7–13,
17 and 19, and `spec/jolt/` for the replayer's and the Tracer's windows): where they differ, the files are wrong.
`tests/test_regs.cpp` checks them against the generator, the simulator's decoding, the specification's tables and the
RTL firmware's own definitions (`tile/tile_map.h` and the Rust firmware's `lib.rs`).

## Running an image on a tile

An image is an ELF file whose loaded segments are IMEM's and DMEM's contents: code in IMEM from `0x01000000`, data in
DMEM from `0x00000010` (5.1, 6.3; `tests/rtl_images.h` reads one). The machine harness (`Mach`, `tests/mach.h`)
builds the chip; a test places an image in a tile's memories before the reset ends (the special tiles: `boot_local`)
or lets the master tile's program load it through the boot ROM's `LOAD` (the compute tiles and the tracer tile), as
the RTL's tests do. `tests/test_rtl_tile.cpp` (one tile) and `tests/test_rtl_sort.cpp` (the chip) are the examples.

Nothing in an image stops the simulator: a program reports to the master tile by requests (11.5), and a test reads
the master's summary from its DMEM.

## Changing a program, and taking a newer one from the RTL

Edit the source, run `fw/build.sh` (it rewrites `images/` and its `MANIFEST`), and commit both; a core program's
image is copied by hand into `tests/core-programs/elf/` with its line of `SHA256SUMS`, and needs the RTL's run of it
for its references (`tests/core-programs/README.md`). The tests' expectations are the RTL's run of these images:
a changed image needs the RTL's figures for it.

The Rust images are refreshed from a Snarky One checkout (`rtl/README.md`, "Refreshing"). `build.sh --check` then
fails if a master holds another tile image than the one built here.
