# WORKER16: PR #2's tracer moved to jolt-cpp 88dca85 (DECISIONS-v1.3 section 14 (c))

**Branches** (jolt-asic-planning):
- **`worker16-pin-88dca85`**, for merging into `tracer-standalone-main`. It was made from `8d546e3` and is at `a9b8d24`:
  - `763dbb4`: the re-sync;
  - `a9b8d24`: the README and provenance.
- **`worker16-w3-88dca85`** (`f2ae129`): the segment tools on the new pin. It was made from `763dbb4`, with `worker16-w3`'s
  two commits cherry-picked (`0672c61`, `842c8ba`) and one porting commit added. `worker16-w3` stays at `ca30b5e`.

Both branches were pushed as they stood before the comparisons ran.

## The re-sync

- **What jolt-cpp changed.** Between `821e1bd` and `88dca85`, only `15cd6c780` touches the copied paths: 7 tracer files
  (`compressed_trace_gen.{cpp,hpp}`, `loader.{cpp,hpp}`, `trace_owner.cpp`, `vm.{cpp,hpp}`) and `src/utils/files.hpp`.
  - `load_program` now returns a `loader::ProgramImage`: the layout plus a sealed memfd snapshot of the loaded ELF.
  - `RiscV::reset(image)` restores the VMs from that snapshot with copy-on-write mappings, instead of reloading the ELF.
  - `CompressedTraceGen::layouts[num_jits]` and `elf_file_key` become one `program_image`.
  - `files.hpp` loses `FileKey`.
- **Copied:** those 8 files, plus **`src/utils/chrome_tracer.hpp`**, newly included by `compressed_trace_gen.cpp`. It
  uses only the standard library, and `TRACE_SCOPE` is empty unless `TRACE_ENABLED`.
- **Every file under `tracer/src/{tracer,utils}` now equals 88dca85**, except PR #2's documented include edit in
  `syscalls.cpp` (unchanged).
- **Unchanged between the pins:** **`src/tracer/isa/` is identical**, as are `src/benches/trace_elf.cpp`,
  `zklib-to-be-removed/bn`, `third-party/secp256k1` and both examples.
- **Ported** (`layouts[0]` → `program_image.layout`, 4 lines): `capture_writes.cpp`, `delivery/delivery_writer.cpp`,
  `tables/build_tables.cpp`. Every VM had the same layout, so their behavior is unchanged. The segment tools need the
  same change, on their own branch.
- **Build:** clean. The one warning (an unused variable) is in jolt-cpp's own `vm.hpp:154`.

## Checks

Old = the 821e1bd build (`tracer/src` unchanged on `tracer-standalone-main` since `6c3aae0`). New = the 88dca85 build.
Both use the same workloads: EVM Zone `4e3451c1d8c5` trace-29 and Cyclone input/29 `input-15961266`.

| check | 821e1bd | 88dca85 | |
|---|---|---|---|
| row hashes, SHA-256 per 2¹⁶-row window of jolt-cpp's rows (`gen_compressed_trace` + `expand_trace`; `data/pin/window_hashes.cpp`) | EVM Zone 5,415 windows, Cyclone 5,670 | same | **all identical** (whole-trace `89f4b7a0…`, `f34e83c1…`) |
| `segment_writer`, EVM Zone 2²⁹ | `208520ec…` | `208520ec…` | **byte-identical** |
| `segment_writer`, Cyclone 2²⁹ | `f45f71b6…` | `f45f71b6…` | **byte-identical** |
| `segment_writer`, EVM Zone trace-23, auto hard cuts (93 segments) | `b33f9fc4…` | `b33f9fc4…` | **byte-identical**, same cuts |
| `segment_writer`, Cyclone input/22, auto hard cuts (111 segments) | `a3858f5e…` | `a3858f5e…` | **byte-identical**, same cuts |
| `capture_writes` `.bin`, EVM Zone 2²⁹ | `a5568ea0…` | `a5568ea0…` | **byte-identical** |
| `capture_writes` `.bin`, Cyclone 2²⁹ | `734368ad…` | `734368ad…` | **byte-identical** |

- **The baseline's segment files** equal those W3 reported at `ca30b5e` (`208520ec…`, `f45f71b6…`).
- **`segment_check`** (88dca85 build) passes on all four new files: every row restarted (354,861,942 and 371,526,406
  at 2²⁹), and every image check passes.
- **The Python reader's hashes** equal the checker's on all four.
- **Nothing differed.**
- **Data:** `data/pin/` holds the window hashes (identical for both pins), every SHA-256, and the hash tool's source.
