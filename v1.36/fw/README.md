# fw: the firmware

*For firmware authors: where the register interface and its reference are, which firmware the simulator runs, and how
an image runs on a tile.*

v1.36's hardware is the RTL's, so **the firmware of v1.36 is the RTL's firmware**: Snarky One's boot ROM, tile
programs, sort and chip firmware, vendored here as it builds them and run by the simulator unmodified. meshsim's own
firmware of v1.35 (its C programs, recipes and conventions, on v1.35's map and commands) is not carried over
(`../README.md`, "What v1.36 does not carry over"); no firmware is written or built in this folder.

| | what |
|---|---|
| `include/mesh.h` | the tile-program interface: every register and window as C structs and constants, in v1.36's names |
| `REGISTERS.md` | the programmer's register reference |
| `rtl/` | **the RTL's firmware**, sources and built images, copied verbatim from Snarky One: the boot ROM (`boot/`), the tile programs (`tile/`), the chip's runs (`sort/`: the sort's four runs, the memory run, the trace run and the segment's images), their sources (`src/`), the hash list (`MANIFEST`) and `refresh.sh`. Its `README.md` says which file is which, the commit and the toolchain |
| `check_images.sh` | checks every file of `rtl/` against `rtl/MANIFEST` (the runner's step `rtl-images`) |

## The register interface

`include/mesh.h` and `REGISTERS.md` are generated from the register table (`tools/regs/`); edit the table, never the
files (`tools/README.md`). The specification is their source of truth (`spec/machine-spec.md` chapters 2, 4, 5, 7–13,
17 and 19, and `spec/jolt/` for the replayer's and the Tracer's windows): where they differ, the files are wrong.
`tests/test_regs.cpp` checks them against the generator, the simulator's decoding, the specification's tables and the
RTL firmware's own definitions (`rtl/src/`: `tile_map.h` and the Rust firmware's `lib.rs`).

The RTL's firmware does not include `mesh.h`: its C sources have the RTL's port header and its Rust firmware its own
library. `mesh.h` is for a program written against v1.36's map here; none is in this folder.

## What the RTL's firmware is, and what runs it

| the firmware (in `fw/`) | what it does | run by |
|---|---|---|
| `rtl/boot/boot_rom.elf` | the boot ROM's program (6.5): the pull boot by `LOAD`, `boot_local`, the handler | every tile of every test boots through it; `test_rtl_tile` and `test_core_programs` check its cycles |
| `rtl/tile/base_tile/` (`registers`, `noc`, `barrier`, `errors`, `replayer`) | the RTL's tile programs: the tile window's registers, the tile's packets, the barrier, the errors, the replayer's window | `test_rtl_tile` |
| `rtl/sort/tile/sort.elf`, inside each run's `master.elf` | the sort: meshsim v1.25's sort, split and skew planner, on the RTL's port layer (`rtl/src/rtl/chip/test/programs/sort/PORTING.md`) | `test_rtl_sort` |
| `rtl/sort/<run>/master.elf` | the master tile's program: the load, the barriers, the errors, the links' latency | `test_rtl_sort`, `test_rtl_memory`, `test_rtl_segment` |
| `rtl/sort/memory-tile/memory.elf` | the memory tile's program: `READ` and `WRITE` requests served with the RW engine (11.5) | `test_rtl_memory`, and every chip test |
| `rtl/sort/segment/tracer.elf`, `replay.elf`, with `rtl/sort/trace/master.elf` | the segment: the tracer tile's image feeds the Tracer and delivers each window; the replay image replays it on a compute tile | `test_rtl_segment` |

The core's own programs (riscv-tests, the firmware tests, CoreMark) are with their references, in
`tests/core-programs/`.

## Running an image on a tile

An image is an ELF file whose loaded segments are IMEM's and DMEM's contents (`rtl/README.md`, "The images";
`tests/rtl_images.h` reads one). The machine harness (`Mach`, `tests/mach.h`) builds the chip; a test places an image
in a tile's memories before the reset ends (the special tiles: `boot_local`) or lets the master tile's program load
it through the boot ROM's `LOAD` (the compute tiles and the tracer tile), as the RTL's tests do.
`tests/test_rtl_tile.cpp` (one tile) and `tests/test_rtl_sort.cpp` (the chip) are the examples.

Nothing in an image stops the simulator: a program reports to the master tile by requests (11.5), and a test reads
the master's summary from its DMEM.

## Refreshing the RTL's firmware

`rtl/README.md`, "Refreshing": build in a Snarky One checkout (`mise run core-images`, `mise run sort-images`), then
`fw/rtl/refresh.sh /path/to/snarky-one` and `fw/check_images.sh`.
