# WORKER17 notes

Working notes, kept current; the report is `REPORT.md` (not yet written). Measured and read, not designed.

## Status

- 2026-11-01: plan agreed with the owner (go-ahead given). Owner's additions: (1) question 8 includes jolt-cpp's compressed
  trace and GPU expander, the interface the prover actually reads; their format goes here early, for exchange with WORKER16
  (reading the same expander for registers 40-47); (2) measurements are RAM-only; registers get the reading side only.
- Done: the compressed trace and both expanders, for memory; the layout (Q1) in both codebases with both workloads' values,
  measured; how ram_K is chosen and its value (owner's request, 2026-11-02); the RAM ra-virtualization limits, checked at the
  pin. Next: the rest of the question 5 table, then the per-row rules (Q2), then the probe.
- **Owner's correction (2026-11-02), checked at the pin:** jolt-cpp commits 4-bit chunks when log T < 25, so a 4M-row proof
  has d = ceil(log K / 4). The owner cited HEAD `88dca85`; at the pin `821e1bd` the limit is **lower** (below).

## Pins

- jolt-cpp `821e1bd` (snarkify/jolt-cpp). The standalone copy in jolt-asic-planning `tracer-standalone-main` (`8d546e3`)
  is byte-identical for every tracer file cited here (`trace_owner.{hpp,cpp}`, `trace_columns.inc`, `isa/riscv.hpp`,
  `isa/virtual.{hpp,cpp}`, `loader.{hpp,cpp}`, `transcode.cpp`; `syscalls.cpp` differs only in one `#include`). It does
  **not** include the GPU expander, `src/tracer/gpu/expand_trace.cu`; that is read from jolt-cpp itself.
- Upstream Jolt `17a9459` (a16z/jolt, 2026-10-31), as in DECISIONS-v1.3 section 12. Material changes at HEAD noted separately.
- Paths below are jolt-cpp's, `src/tracer/...`, unless marked otherwise.

## The compressed trace and its expanders, for memory (for WORKER16)

### What the compressed trace is

`CompressedTraceOwner` (`trace_owner.hpp:401-419`): an array of **chunk headers** and one **mem_log** of 64-bit words.

- **Chunk header** (`trace_owner.hpp:393-399`): `regs[num_stateful_regs]` (registers 0-47: 32 RISC-V, 32-39 reserved
  CSR/reservation vregs, 40-47 allocatable stateful vregs; `isa/virtual.hpp:28-53`), `pc`, `instr_idx`, `skip_cycles`
  (u16), `mem_log_start` (u64). One header per `compressed_chunk_size` rows: **512** for both workloads (`program.json`).
  About 408 bytes per header, so about 0.8 B per row.
- **mem_log**: one word per **load row** (the 64-bit word read), per **store row** (the word's value **before** the
  write), and per **`VirtualAdvice` row** (the secp advice word), in row order. Nothing else. The division family's
  advice (`VirtualAdviceDiv*`, `*Rem*`) is **not** logged: both expanders recompute it from rs1 and rs2
  (`expand_trace.cu:330-405`). A store's written value is **not** logged: it is the rs2 register's value, already in the
  replayed register file.
- Generation (`gen_compressed_chunks_and_mem_log`, `trace_owner.cpp:678-851`): loads `811-821` (log the word, write rd),
  stores `823-832` (log `ram[addr]`, then write `regs[rs2]` to RAM), advice `834-842`. The same three cases appear in the
  catch-up loop before the traced range, `746-777`.
- **Mid-sequence chunk starts** (`emit_new_chunk`, `trace_owner.cpp:700-733`): if a chunk boundary falls inside a virtual
  sequence and any inline vreg (48+) is non-zero, the header is **rewound to the sequence start**: `instr_idx` and
  `mem_log_start` are the sequence start's and `skip_cycles` is the number of rows to replay without recording. So a
  chunk's mem_log words can begin before its first row, and slices widen their mem_log range to cover that
  (`CompressedTraceRef::slice`, `trace_owner.hpp:488-503`). Note: the header's `regs` are still the boundary's, copied
  **before** the rewind (`705`), not the sequence start's. *(To check: replaying `skip_cycles` rows from the boundary
  registers; whether rows in the skipped part can change registers 0-47 that the skipped replay reads. This touches
  WORKER16's 40-47 question.)*
- **Registers 40-47 in headers are back-patched** after the parallel threads finish (`trace_owner.cpp:1093-1129`): each
  thread starts with unknown 40-47 values; headers before a thread's first write to each vreg get the previous thread's
  final value (`vreg_info`, `locs`, `first_valid_chunk`; recorded at `803-809`). For WORKER16.
- The JIT only **counts** mem_log words so threads know their starting index: 1 per 8-byte store or load, **2 per narrow
  store** (`transcode.cpp:817-822`; a narrow store becomes a word load and a word store, below), and `log_words` per secp
  inline (`syscalls.cpp:236`).
- `ram_K` (`trace_owner.cpp:1134-1140`): `next_pow2((max address accessed by any traced load/store − lowest_address)/8 + 1)`.
  It comes from the run's accesses, not from the layout.

### What the expanders produce

Two producers of the same columns: the CPU path (`expand_compressed_chunk`, `trace_owner.cpp:855-915`, AoS `RiscCycle`, then
`TraceExpander::copy_aos_to_soa`) and the GPU path (`expand_kernel`, `expand_trace.cu:427-562`, writes the SoA directly).

- **Per thread**, one chunk: registers 0-47 from the header, 48-127 zeroed (`expand_trace.cu:454-459`); replay `skip_cycles`
  rows unrecorded (`462-464`); then record up to 512 rows (`473-559`), each load/store/advice consuming the next mem_log word
  (`212-214`, `326-328`).
- **The expanded SoA** (`RiscCycleSOA`, `trace_owner.hpp:88-377`; columns `trace_columns.inc:53-76`): `instr_idx` (u32),
  `rd`, `rs1`, `rs2`, `rd_prev` (u64), `rd_imm_bits` (1 bit/row, device only), `rd_diff` (u64, device only), `rd_code` (u8,
  device only), `mem_waddr_p1` (u32). **No memory columns as such**; they are derived (`trace_owner.hpp:71-87, 199-218`):
  - **address**: `rs1 + imm`, imm from the bytecode (rd slot for stores, rs2 slot for loads), floored for narrow ops
    (`isa/riscv.hpp:241-253`). Also precomputed as **`mem_waddr_p1 = (addr >> 3) + 1`**, 0 for non-memory rows
    (`trace_owner.hpp:133-136`; GPU `expand_trace.cu:509-511`). It is the **absolute** address, **not** remapped by
    `lowest_address`; the prover subtracts `(lowest_address >> 3) + 1` (`zkvm/ram/gpu/val_check.hpp:77`,
    `ra_virtual.hpp:63`). u32, so addresses up to about 2³⁵.
  - **load**: pre = post = the `rd` lane (the loaded word).
  - **store**: the pre-value is **packed into both `rd` and `rd_prev`** lanes (`expand_trace.cu:490-497`), so a store row's
    `rd - rd_prev` is 0; post = the `rs2` lane. Register consumers must mask with `rd_value()`/`rd_prev_value()`
    (`trace_owner.hpp:183-189`), since stores have `rd_is_imm`.
  - **advice rows**: `rs1 = rs2 = 0` (`expand_trace.cu:487-489`).
- Lanes are written only where non-zero; the whole capacity is zeroed first (`expand_trace.cu:601-631`). Padding rows are
  `instr_idx` 0, the bytecode's reserved all-zero NOP (`trace_owner.hpp:141-143`).

### Memory rows, as the tracer emits them

- `is_load`/`is_store` (`isa/riscv.hpp:174-208`) list the RISC-V narrow ops too, but after expansion only **`LD`, `SD`,
  `VirtualLoadB/H/W`, `VirtualStoreB/H/W`** occur (the README of the standalone tracer: no other memory row on any run).
  Every one reads or writes one aligned 64-bit word; narrow ops address the word containing the access: address minus
  (address & 7/6/4) for B/H/W, which keeps a misaligned access's low bits so it is rejected
  (`isa/riscv.hpp:215-268`).
- **Narrow load** (`expand_load`, `isa/virtual.cpp:238-246`): `VirtualLoadX` (one RAM read of the word), `VirtualWindowMaskX`,
  `VirtualPext[Signed]`: 3 rows, 1 RAM access.
- **Narrow store** (`expand_narrow_store`, `isa/virtual.cpp:248-261`): `VirtualLoadX`, `VirtualWindowMaskX`, `ANDN`,
  `VirtualShiftDataX`, `ADD`, `VirtualStoreX`: 6 rows, **2 RAM accesses** to the same word (read, then write the merged
  word).
- Still to read: the LR/SC and AMO expansions (`isa/virtual.cpp:~640-910`), and secp inline memory rows.

## Upstream at the pin has no `jolt-core`

At `17a9459` jolt-core has been split up (`072998fb9`, "Strip jolt-core into jolt-prover-legacy (#1632)"; no
`jolt-prover-legacy` remains in `crates/`). The sumchecks are defined symbolically in
`crates/jolt-claims/src/protocols/jolt/relations/{ram,registers,bytecode}/` with shapes in `.../geometry/`, proved by
`crates/jolt-kernels/src/optimized/` (and `reference/`), verified in `crates/jolt-verifier`. RAM preprocessing (initial image,
`compute_min_ram_k`) is `crates/jolt-program/src/preprocess/ram.rs`. The prompt's `jolt-core` reads as these crates.

## Q1: the layout

Same construction in both (jolt-cpp `loader.cpp:190-308`; upstream `common/src/jolt_device.rs:381-533`):

- I/O region below `RAM_START` = `0x8000_0000`: sizes rounded up to 8 B; advice sizes must be 0 or a power of two
  (cpp `:238-245`, up `:404-414`); `io_bytes` = next_pow2((advice + advice + input + output + 16) / 8) words × 8 (cpp
  `:247-251`, up `:416-433`); the region starts at `0x8000_0000 − io_bytes`, the **larger** advice first, then the other
  advice, input, output, panic word, termination word, `io_end` (cpp `:253-276`, up `:444-495`). The padding leaves a gap
  between `io_end` and `0x8000_0000`.
- Above it: program (ELF image, `.text` to `.bss`), a 128 B stack canary, the stack (grows down), the heap (grows up).
  Same addresses, **opposite names**: cpp `stack_start = program_end + 128`, `stack_end = stack_start + stack_size`, heap from
  `stack_end` (`:278-282`); upstream `stack_end = RAM_START + program_size`, `stack_start = stack_end + 128 + stack_size`, heap
  from `stack_start` (`:497-509`). So the canary is `[program_end, program_end+128)` in both.
- Program size: cpp from the ELF's PT_LOAD extent, and the first segment **must** load at `0x8000_0000` (`loader.cpp:165-209`);
  upstream `program_size = program_end − RAM_START` from the host (`crates/jolt-host/src/program.rs:358,484-492`).
- `lowest_address` = min(trusted, untrusted advice start) (cpp `loader.hpp:61-63`, up `jolt_device.rs:535-537`); cell index
  = `(address − lowest_address) / 8` (up `:539-553`; address 0 = no access, below lowest = error). jolt-cpp's prover:
  `mem_waddr_p1 − ((lowest >> 3) + 1)`, with `w >= lowest_wp1` as its validity test (`zkvm/ram/gpu/ra_virtual.hpp:60-64`).
  *(To check: what a below-lowest access does in jolt-cpp; whether it can only be "no access".)*
- Heap size key: `heap_size` or the legacy `memory_size` (`utils/elf_helper.cpp:99`).

**Values, measured** (`capture_writes` at 2²⁹, PR #2's build; `data/capture_writes-*-29.json`; equal to my hand computation
from `program.json` and the ELF headers):

| region | EVM Zone | Cyclone |
|---|---|---|
| untrusted advice | `0x7E00_0000`-`0x7F00_0000` (16 MiB) | `0x7E00_0000`-`0x7F00_0000` (16 MiB) |
| trusted advice | `0x7F00_0000`-`0x7F01_0000` (64 KiB) | empty, at `0x7F00_0000` |
| input | `0x7F01_0000`-`0x7F01_0070` (112 B) | `0x7F00_0000`-`0x7F00_1000` (4 KiB) |
| output | empty, at `0x7F01_0070` | `0x7F00_1000`-`0x7F00_3000` (8 KiB) |
| panic, termination | `0x7F01_0070`, `0x7F01_0078` | `0x7F00_3000`, `0x7F00_3008` |
| I/O padding to `0x8000_0000` | about 16 MiB | about 16 MiB |
| program | `0x8000_0000`-`0x801E_AD28` | `0x8000_0000`-`0x8032_89E0` |
| canary | 128 B | 128 B |
| stack | `0x801E_ADA8`-`0x809E_ADA8` (8 MiB) | `0x8032_8A60`-`0x8432_8A60` (64 MiB) |
| heap | `0x809E_ADA8`-`0x849E_ADA8` (64 MiB) | `0x8432_8A60`-`0x9432_8A60` (256 MiB) |
| `lowest_address` | `0x7E00_0000` | `0x7E00_0000` |
| whole layout, cells | 13.9 M (`heap_end`: next_pow2 = 2²⁴) | 46.5 M (`heap_end`: next_pow2 = 2²⁶) |

## How ram_K is chosen (jolt-cpp at the pin), and its values

Three steps; the result is a power of two:

1. **The tracer** (`trace_owner.cpp:1134-1140`): `next_pow2((highest address any traced load or store touched −
   lowest_address)/8 + 1)`. From the run, not the layout.
2. **The program-image floor** (`prover/node_context.cpp:318-371`, inline in proof setup at the pin; the owner's
   `compute_min_ram_k` name and `319-368` are HEAD's, where the only change is that the layout comes from
   `compressed_trace_gen.program_image.layout`): `next_pow2(remapped(program_start) + W + 1)`, W the program-image words
   (`program_end` + 3 rounded up to 8, in words, `:322-327`). With committed program hints, W becomes max(next_pow2(W),
   2^sigma_main, the hints' `program_image_num_words`), sigma_main = (log_k_chunk + log T + 1)/2 with T the padded trace,
   bytecode or image size, whichever is largest (`:329-357`). ram_K is raised to this floor if it is below it (`:368-371`).
3. **The Rust config's check** (`src/backend/cpp/src/config.rs:102-107, 133-138`): fails unless ram_K ≥ upstream's
   `compute_min_ram_k` (`crates/jolt-program/src/preprocess/ram.rs:96-114`): next_pow2(max(remapped(min init address) + image
   words, remapped(`RAM_START`))). Upstream also has `compute_max_ram_k`, next_pow2 of the whole layout (`:116-129`).

| | EVM Zone | Cyclone |
|---|---|---|
| step 1, from the run | **2²³** | **2²⁴** |
| step 2 floor: remapped(program_start) = 2²², image words | 251,303 (2¹⁸ padded) | 414,014 (2¹⁹ padded) |
| step 2 floor, with hints at log T = 29 (8-bit chunks, sigma = 19) | 2²³ | 2²³ |
| step 2 floor, without hints, or with hints at log T = 22 (4-bit, sigma = 13) | 2²³ | 2²³ |
| step 3 minimum | 2²³ | 2²³ |
| **ram_K** | **2²³** | **2²⁴** |
| the layout's whole range (upstream's `compute_max_ram_k`) | 2²⁴ | 2²⁶ |

So EVM Zone touches nothing above about `0x8200_0000` (2²³ cells above `lowest`), well inside its 64 MiB heap, and Cyclone
nothing above `0x8600_0000`: its stack top (`0x8432_8A60`, cell 13.0 M) alone needs 2²⁴, and it uses at most about 29 MiB
of its 256 MiB heap. ram_K follows the run, so another input of the same program can change it. *(Per-region extents come
with the probe.)* The floor's T is the whole run's padded trace in jolt-cpp; at the Tracer's 4M-row proofs log T = 22,
which leaves the floor at 2²³ for both.

## Question 5, first entries: the RAM one-hot geometry (jolt-cpp)

| item | at the pin `821e1bd` | at HEAD `88dca85` |
|---|---|---|
| committed chunk width | 4 bits if log T < 25, else 8 (`zkvm/config.hpp:121-127`, `get_log_k_chunk`; applied `:176` unless overridden) | same |
| d, committed RAM ra polynomials | ceil(log K / chunk) | same |
| ra virtualization degree | d + 1 (upstream `relations/ram/ra_virtualization.rs:60-62`) | same |
| GPU ra virtualization, max d | **`MAX_D = 6`** (`zkvm/ram/gpu/ra_virtual.hpp:28`) | 8 (`:28-29`, "a 1 GiB guest heap") |
| Rust slot, accepted | **2-6** chunks of 4 or 8 bits (`sumcheck/slots/ram_ra_virtualization.rs:42-46`) | 2-8 (`:42`) |
| so log K at log T < 25 | **≤ 24** | ≤ 32 |
| GPU output check's `ram_k` | `uint32_t` (`zkvm/ram/gpu/output_check.cu:89`), so K < 2³² | same |

At the Tracer's 4M-row proofs (log T = 22, 4-bit chunks): **EVM Zone, K = 2²³: d = 6, degree 7; Cyclone, K = 2²⁴: d = 6,
degree 7.** Cyclone sits exactly at the pin's limit; a run reaching 2²⁵ cells (a few MiB more of stack or heap use above
`0x8600_0000`) needs d = 7, which the pin refuses and HEAD accepts. jolt-cpp's own whole-run proofs at 2²⁹ use 8-bit chunks:
d = 3 for both. The layout's whole range (2²⁶ for Cyclone) would need d = 7 at 4-bit chunks.

**Binding order.** jolt-cpp's Rust config says RAM and register read-write checking bind **address first**
(`ram_rw_phase1_num_rounds: 0`, `phase2 = log K`; the comment cites `zkvm/ram/cpu/read_write_checking.hpp`,
`zkvm/ram/gpu/rw_checking.cu`, `zkvm/registers/gpu/read_write_checking.cu`; `src/backend/cpp/src/config.rs:161-175`). To
confirm in the kernels and against upstream's default.

## Build and runs (local)

- PR #2's tracer builds here with CMake 4 given `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (nlohmann_json's old minimum); libelf-dev
  installed. `vm.overcommit_memory` resets between turns: set it to 1 before each run.
- `capture_writes` at 2²⁹: EVM Zone 29 s, Cyclone 23 s, 1 CPU. Rows, loads and stores equal WORKER9's and WORKER16's.
  The runner is not needed.

## Log

- 2026-11-01: cloned meshsim, jolt-cpp (`821e1bd`), jolt-asic-planning (`tracer-standalone-main` `8d546e3`; WORKER16 used
  `6c3aae0`, no changes under `tracer/src` since), upstream (`17a9459`). Read the compressed trace and both expanders.
- 2026-11-02: layout in both codebases; built PR #2's tracer, ran `capture_writes` on both 2²⁹ inputs (layout, ram_K);
  ram_K's selection; the owner's ra-virtualization limits checked at the pin (MAX_D 6 and slot 2-6 there; 8 at HEAD).
