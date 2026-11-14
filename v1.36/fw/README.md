# fw: the firmware

*For firmware authors: where the programs, headers and docs are, how to build an image, and how one runs on a tile.*

## v1.36: what is on which map

v1.36's hardware is the RTL's, and its registers moved (the specification's guide to the changes from v1.35 has the
name table). This folder holds two sets of firmware:

| | what | map |
|---|---|---|
| `rtl/` | **the RTL's firmware**, sources and built images, copied verbatim from Snarky One and never built here: the boot ROM, the tile programs, the sort on its port layer, the chip's Rust firmware (the master tile's, the memory tile's, the segment's). `rtl/README.md` says which file is which, the commit and the toolchain; `fw/check_images.sh rtl` checks every file against `rtl/MANIFEST` | v1.36 |
| `include/mesh.h`, `REGISTERS.md` | the register interface and its reference, generated from the register table (`tools/regs/`) | v1.36 |
| everything else: `boot/`, `cc/`, `m1/`, `recipes/`, `replay/`, `sort/`, `tests/`, `tracer/`, `riscv-tests/`, `images/`, `app.ld`, `crt0.S`, `CONVENTIONS.md`, `RECIPES.md`, `regdoc/` | meshsim's own firmware and its documents, v1.35's, **not yet ported**: the sources name v1.35's registers and commands, and the committed images are v1.35's | v1.35 |

Until it is ported, meshsim's own firmware does not compile against `include/mesh.h`: its sources were written against
v1.35's header, which is `v1.35/fw/include/mesh.h` beside this version, and `fw/build.sh`, which includes
`include/mesh.h`, cannot rebuild its images. `fw/check_images.sh` still checks the committed images against their hash
lists.
`mesh-v1.2-draft.h` is the draft that v1.35's header was compared with (`tools/regs/header_diff.sh`); v1.36's header
is not its successor and is not compared with it.

## The docs

| file | what |
|---|---|
| `include/mesh.h` | the tile-program interface: every register and window as C structs and constants, in v1.36's names |
| `REGISTERS.md` | the programmer's register reference, v1.36's |
| `CONVENTIONS.md` | the control conventions a tile program follows (v1.35's) |
| `RECIPES.md` | the recipe book: short C for the common operations, each compiled from `recipes/` (v1.35's) |

`include/mesh.h` and `REGISTERS.md` are generated from the register table (`tools/regs/`); edit the table, never the
files (`tools/README.md`). The spec is their source of truth (`spec/machine-spec.md` chapters 2, 4, 5, 7–13, 17 and
19, and `spec/jolt/` for the replayer's and the Tracer's windows): where they differ, the files are wrong.
`tests/test_regs.cpp` checks them against the generator, the simulator's decoding, the specification's tables and
the RTL firmware's own definitions (`rtl/src/`).

## Building an image

The images are committed, so nothing here needs building to run the simulator. This section is meshsim's own firmware's
(v1.35's map, above); the RTL's images are built in Snarky One (`rtl/README.md`). To rebuild them you need the RISC-V
toolchain (`BUILDING.md`); without it, `fw/check_images.sh` checks the committed images against their hash lists.

- `fw/build.sh --check` rebuilds every image into `fw/build/out/` and checks each against its committed copy,
  byte for byte;
- `fw/build.sh` rewrites `fw/images/` (with its `MANIFEST`) and compares the images that live in a test's
  fixture folder, never overwriting them;
- `PARTS="cc recipes"` (any of `boot tests m1 m3 cc recipes replay tracer sort riscv`) limits the build to those parts;
  `RISCV_TESTS=DIR` (a riscv-tests checkout at 3df703c) also rebuilds the riscv-tests images.

An image is three files: `NAME.code.bin`, `NAME.data.bin` and `NAME.sym`.

## Running an image on a tile

The machine harness (`Mach`, `tests/mach.h`) loads images by name from `fw/images/` and the fixture folders
(`MachConfig::image_dirs`) and boots the tiles with them; the tests and suites in `tests/` are the examples
(`tests/test_golden.cpp` runs each M1 program on one tile, `src/units/README.md` lists the machine's settings).
A program ends the run by storing to the exit word (spec 8.15).

## The folders

| folder | what |
|---|---|
| `boot/` | the boot ROM: source, linker script and `bootrom.hex` |
| `cc/` | the controllers' programs: C&C 1 (`cc1.c`), C&C 2 and 3 (`cc23.c`), and two test variants |
| `images/` | the committed images not kept in a test fixture folder, and their `MANIFEST` (sha256); `log8k/` and `oldmap/` hold the replay controller's two variants, for the 8 KB log and (tests only) the old queue map |
| `include/` | `mesh.h` |
| `m1/` | M1's assembly programs and their test-bench schedules (`*.tb`) |
| `recipes/` | the recipes as compiled code, the source of `RECIPES.md` |
| `regdoc/` | `REGISTERS.md`'s worked-example diagrams |
| `replay/` | the replay controller (its `README.md`) |
| `riscv-tests/` | the riscv-tests images (`bin/`) and their build |
| `rtl/` | the RTL's firmware, v1.36's: sources (`src/`), the boot ROM (`boot/`), the tile programs (`tile/`), the chip's runs (`sort/`), their hash list and `refresh.sh` (its `README.md`) |
| `sort/` | the sort: the program (`program/`, its README), the split (`split/`: its sources and `BINDING.md`, the split's binding to the sort) and the skew planner (`skew/`, its README) |
| `tests/` | the tile test programs (`hello`, `faults`, `a2a`, `gather` and others) |
| `tracer/` | the Tracer's controller |

`app.ld` and `crt0.S` are the tile programs' linker script and start-up file; `build.sh` the build.
