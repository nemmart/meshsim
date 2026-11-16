# fw: the firmware

*For firmware authors: where the register interface and its reference are, which firmware the simulator runs, how it
is built here, and what changed from v1.35's.*

**Every image v1.36 runs is built in this folder, from sources in this folder** (`build.sh`), and each is proved
equal to its committed copy. Two kinds of firmware are here:

- **v1.35's firmware, upgraded to v1.36**: C&C 1's program on the master tile, the replay controller on the compute
  tiles, the Tracer's controller on the tracer tile, the sort, with v1.35's start-up file and link script, under
  their v1.35 names and in v1.35's layout, each changed only where v1.36's hardware forces it ("What changed",
  below). They reach v1.36's registers through one port header, `port/mesh.h`, which gives v1.35's names at v1.36's
  addresses, so that `diff -r ../../v1.35/fw .` shows what the hardware change requires;
- **the RTL's own programs, which are hardware content or its tests**: the boot ROM's program, the tile programs and
  the core's programs, Snarky One's bytes, built as its recipes build them.

| | what |
|---|---|
| `include/mesh.h` | the tile-program interface: every register and window as C structs and constants, in v1.36's names |
| `REGISTERS.md` | the programmer's register reference |
| `port/mesh.h` | the port layer: v1.35's firmware interface (v1.35's `include/mesh.h` names, the sort's v1.25 names) on v1.36's registers |
| `build.sh` | builds every image from the sources below; `--check` proves each byte-equal to its committed copy |
| `check_images.sh` | checks every committed image against its hash list, without the toolchain |
| `crt0.S`, `app.ld` | v1.35's start-up file and image layout, for `cc/`, `replay/` and `tracer/` |
| `cc/` | the controllers' programs: C&C 1's, `cc1.c`, the master tile's; and, new in v1.36, the memory tile's, `memory.c`, and the memory run's master, `cc_mem.c` |
| `replay/` | the replay controller, `replay_ctl.c`, with its scratchpad layout, `replay_layout.h` (its `README.md` is v1.35's history) |
| `tracer/` | the Tracer's controller, `tracer_ctl.c`, the tracer tile's program, with its layout, `tracer_layout.h` |
| `sort/` | the sort, v1.35's: the program (`program/sw/`), the skew planner (`skew/`), the split (`split/`), their patches (`program/patches/`), on the port layer's start-up, layout and input stand-in (`port/`; `PORTING.md` lists every change) |
| `boot/` | the boot ROM's program, `boot_rom.S` (6.5), the RTL's: hardware content |
| `tile/` | the RTL's tile programs: assembly (`registers`, `noc`, `barrier`, `errors`, `special`, `replayer`), with `start.S`, `link.ld`, `tile_map.h` and the list `programs.toml` |
| `core/` | the core's own programs: the start-up and link script of the RTL's test memory (`start.S`, `link.ld`), `isa_edges.S`, the four interrupt programs with `events.h`, and what the third-party programs are built with: riscv-tests' environment header and selection (`core/riscv-tests/`), CoreMark's port and flags (`core/coremark/`) |
| `images/` | the committed images `build.sh` builds, with their hash list, `MANIFEST` |

The core's own programs' images are a test fixture, with the RTL's measurements of each: `tests/core-programs/elf/`
(`tests/core-programs/README.md`).

## What changed from v1.35, and what was cut

Each change is marked "v1.36" in its source (the header's v1.36 paragraph, or the changed line).

| program | changed for v1.36's hardware |
|---|---|
| all | `port/mesh.h` in place of v1.35's `mesh.h`: v1.35's names at v1.36's addresses, bits and layouts (the tile window, the tables, the header block, `DMA_HDR`, the events, the peripherals' windows). v1.35's `wait_for_command` is the barrier, arrive and release (7.3); `EVENTS_REQUIRED` is gone, so a wait for several events gathers them over its wakes; v1.35's exit word is gone, so `crt0.S` sends `main`'s return value to the master as a request (`REQ_OP_RESULT`), waits for its reply and ends the image with a jump to the reset PC (6.5) |
| `cc/cc1.c` | no commands, GO or command block (6.5, 7.3): the harness's commands come from `boot_desc.script`, placed with the image (the host port no longer carries them); the boot waits for every `LOAD` (the compute tiles' and the tracer tile's), loads the tracer tile's image if it has one (the memory tile, C&C 2's place, starts from its placed image), and ends each load with a `COUNT` on counter 7 in place of the first command; no tile command is sent, so `CC_PHASE` and `CC_SEGMENT` keep their barriers alone; no "started" request; a result's reply goes where its word 0 names; an error is the NMI and `mstatus.MIE` stays 0 (6.2); a report's number is word 0 [25:18]. New: `CC_WAIT`, the wait for the tiles' results, and `CC_FAULT`, a bad packet to every compute tile and the memory tile (the chip test's error path) |
| `replay/replay_ctl.c` | the handshake with the tracer tile is two requests (17.9.10): `ARMED` once the record's counters are armed, `STREAMS_ARMED` once the streams' is, in place of a count-only "armed" |
| `tracer/tracer_ctl.c` | the handshakes are the tiles' requests, popped as they come: window 0's start waits for tile 0's `ARMED`, output queue 0's pieces for the `ARMED` of the tile each goes to, the streams for `STREAMS_ARMED`; no "started" request, no start command, no `stage_delay` |
| `sort/` | the port layer's (`sort/PORTING.md`): no commands, so `main.c` runs the phases as a fixed schedule ending at the barrier; `DMA_CONFIG`'s split into three registers, `GROUP`'s new words, the read unit's four registers, through port helpers; the image's arguments row in place of SortTest's poke |

Cut, each because v1.36's hardware cannot do it or nothing on v1.36 runs it (`../README.md`, "What v1.36 does not
carry over"); each is in `../../v1.35/fw/`: C&C 2's and 3's program, `cc23.c`, as cores serving nothing, and C&C 1's
two test variants, `cc_bad.c` and `cc_rcp.c` (commands); the replay controller's old-map and 8 KB-log builds (v1.35's
queue map and sizes); the tile test programs (`tests/`), the recipes (`recipes/`), the recipe book and the page of
control conventions (v1.2's draft header, commands, GO and the command block; their suites ran on v1.35's
command-driven machine); the M1 programs (`m1/`: v1.35's core, with its goldens); v1.35's build of riscv-tests (the
RTL's are in `core/`); v1.35's boot ROM (the boot ROM is the RTL's hardware content); the v1.2 draft header and the
register reference's diagrams (`regdoc/`).

## Building

```sh
fw/build.sh --check       # build everything, and require each image byte-equal to its committed copy
fw/build.sh               # build everything, and rewrite fw/images/ and its MANIFEST
fw/check_images.sh        # no toolchain: the committed images against their hash lists
```

**The toolchain** is the one the committed images were built with: xPack `riscv-none-elf-gcc` 15.2.0-1 (GCC 15.2.0,
binutils 2.45), on `PATH` or named by `RISCV_PREFIX` (a prefix: the path of its `bin` folder, then
`riscv-none-elf-`). `build.sh` refuses another compiler, and says where to get this one. (v1.35's images were built
with Ubuntu 24.04's `riscv64-unknown-elf-gcc` 13.2.) The RTL's programs are built for the core's ISA,
`-march=rv32ic_zicond_zicsr_zmmul -mabi=ilp32`, with the RTL's recipes' flags; v1.35's programs with v1.35's flags and
`-march=rv32i_zicsr_zmmul`, a subset of the core's ISA, so that their code is v1.35's code. Every image is built for
the RTL's default configuration: an 8 × 8 mesh, 32 KiB of IMEM and of DMEM, a 4 MiB scratchpad.

**Without the toolchain everything else builds and runs:** the images are committed, and `check_images.sh` checks them
against the hash lists beside them. The runner's step `fw-build` is `build.sh --check` when the toolchain is there and
is printed as SKIPPED, with the reason, when it is not; its step `fw-images` is `check_images.sh`, always.

**Two builds give the same bytes.** Every image is linked from named object files in the build's own folder, each
compiled from its own folder.

**The third-party programs** are built only from a checkout, as v1.35 built riscv-tests': `RISCV_TESTS=DIR` (a
riscv-tests checkout at the revision `core/riscv-tests/riscv-tests.toml` pins) rebuilds the 46 riscv-tests programs,
`COREMARK=DIR` (CoreMark at the revision `core/coremark/coremark.toml` pins) the two CoreMark runs. Without them
`build.sh` builds the rest and says which it left; their images are committed with the others.

## The images, and what runs them

| image | sources | `build.sh`'s part | what runs it |
|---|---|---|---|
| `images/cc/cc1.elf` | `cc/cc1.c`, `crt0.S`, `app.ld` | `cc` | the master tile in `test_sort` and `test_segment`, placed with its `boot_desc` and commands |
| `images/cc/memory.elf`, `images/cc/cc_mem.elf` | `cc/memory.c`, `cc/cc_mem.c` | `cc` | the memory tile in every chip test; the master in `test_memory` |
| `images/replay/replay_ctl.elf` | `replay/` | `replay` | every compute tile in `test_segment`, loaded by C&C 1 |
| `images/tracer/tracer_ctl.elf` | `tracer/` | `tracer` | the tracer tile in `test_segment`, loaded by C&C 1 |
| `images/sort/tile/sort.elf` | `sort/` | `sort` | every compute tile in `test_sort`, loaded by C&C 1 |
| `images/sort/sort.keys` | `tools/fwimage/` | `runs` | the keys of the runs of 16 records a tile: SortTest's `uniform` set, in SortTest's key file (`fwimage keys 65536 FILE` makes the 64K runs', 16 MiB, not committed; its sha256 is `images/sort/sort-64k.keys.sha256`) |
| `images/boot/boot_rom.elf`, `.hex`, `.sym` | `boot/boot_rom.S` | `boot` | every tile boots through it; `test_rtl_tile` and `test_core_programs` check its cycles |
| `images/tile/`: the six programs and `programs.txt` | `tile/` | `tile` | `test_rtl_tile` |
| in `tests/core-programs/elf/`: `firmware-isa_edges.elf` and the four interrupt programs; the 46 riscv-tests programs; the two CoreMark runs | `core/`; riscv-tests, CoreMark | `core`, `riscv`, `coremark` | `test_core_programs` |

**How a chip test runs this firmware** (`tests/test_sort.cpp`, `tests/test_segment.cpp`, `tests/test_memory.cpp`), as
v1.35's harness did: C&C 1's image and the memory tile's are placed in their IMEM and DMEM before the reset ends
(boot_local, 6.5); the images C&C 1 loads are placed in the master's scratchpad (each one's `.text`, and its `.data`
behind the sort's arguments row), and `cc1.c`'s `boot_desc` names them, with the commands to run in
`boot_desc.script`; the replay controller's and the tracer tile's layouts are placed in their scratchpads. Nothing in
an image stops the simulator: a test reads the programs' results in place (`cc1.c`'s `mbox` and `toast_info`,
`tracer_ctl.c`'s `tr_results`, `replay_ctl.c`'s `rp_results`, `sim_exit_word`), by their symbols.

**Which header a program takes its addresses from.** `include/mesh.h` is generated, for a program written against
v1.36 (`cc/memory.c`, `cc/cc_mem.c`). v1.35's programs take `port/mesh.h`. The RTL's programs keep their own:
`tile/tile_map.h` also holds their protocol with the test and their assembler macros; `tests/test_regs.cpp` requires
its registers to be where `include/mesh.h` puts them. The RTL's sources are Snarky One's bytes, so their comments
name its build ("cargo xtask", "Veryl.toml"): here that is `build.sh` and its configuration.

## The register interface

`include/mesh.h` and `REGISTERS.md` are generated from the register table (`tools/regs/`); edit the table, never the
files (`tools/README.md`). The specification is their source of truth (`spec/machine-spec.md` chapters 2, 4, 5, 7–13,
17 and 19, and `spec/jolt/` for the replayer's and the Tracer's windows): where they differ, the files are wrong.
`tests/test_regs.cpp` checks them against the generator, the simulator's decoding, the specification's tables and the
RTL firmware's own definitions (`tile/tile_map.h`).

## Changing a program

Edit the source, run `fw/build.sh` (it rewrites `images/` and its `MANIFEST`), and commit both; `FW_IMAGES=fw/build/out`
runs a test on a build not yet committed. A core program's image is copied by hand into `tests/core-programs/elf/` with
its line of `SHA256SUMS`, and needs the RTL's run of it for its references (`tests/core-programs/README.md`).
`tests/fw_teeth.sh` plants bugs in the firmware and requires the chip's tests to catch each.
