# fw: the firmware

*For firmware authors: where the programs, headers and docs are, how to build an image, and how one runs on a tile.*

## The docs

| file | what |
|---|---|
| `include/mesh.h` | the tile-program interface: every register and window as C structs and constants |
| `REGISTERS.md` | the programmer's register reference |
| `CONVENTIONS.md` | the control conventions a tile program follows |
| `RECIPES.md` | the recipe book: short C for the common operations, each compiled from `recipes/` |

`include/mesh.h` and `REGISTERS.md` are generated from the register table (`tools/regs/`); edit the table, never the
files (`tools/README.md`). The spec is their source of truth (sections 2.2, 3.1, 4.11–4.13 and 8, and `spec/jolt/` for
the replayer's and the Tracer's windows): where they differ, the files are wrong.

## Building an image

The images are committed, so nothing here needs building to run the simulator. To rebuild them you need the RISC-V
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
| `images/` | the committed images not kept in a test fixture folder, and their `MANIFEST` (sha256) |
| `include/` | `mesh.h` |
| `m1/` | M1's assembly programs and their test-bench schedules (`*.tb`) |
| `recipes/` | the recipes as compiled code, the source of `RECIPES.md` |
| `regdoc/` | `REGISTERS.md`'s worked-example diagrams |
| `replay/` | the replay controller (its `README.md`) |
| `riscv-tests/` | the riscv-tests images (`bin/`) and their build |
| `sort/` | the sort: the program (`program/`), the split (`split/`) and the skew planner (`skew/`), each with its README |
| `tests/` | the tile test programs (`hello`, `faults`, `a2a`, `gather` and others) |
| `tracer/` | the Tracer's controller |

`app.ld` and `crt0.S` are the tile programs' linker script and start-up file; `build.sh` the build.
