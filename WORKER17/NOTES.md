# WORKER17 notes

Working notes, kept current; the report is `REPORT.md` (not yet written). Measured and read, not designed.

## Status

- 2026-11-01: plan agreed with the owner (go-ahead given). Owner's additions: (1) question 8 includes jolt-cpp's compressed
  trace and GPU expander, the interface the prover actually reads; their format goes here early, for exchange with WORKER16
  (reading the same expander for registers 40-47); (2) measurements are RAM-only; registers get the reading side only.
- Done: the compressed trace and both expanders, for memory (below). Next: the layout (Q1) in both codebases, then the
  per-row rules (Q2), then the checking side.

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

## Log

- 2026-11-01: cloned meshsim, jolt-cpp (`821e1bd`), jolt-asic-planning (`tracer-standalone-main` `8d546e3`; WORKER16 used
  `6c3aae0`, no changes under `tracer/src` since), upstream (`17a9459`). Read the compressed trace and both expanders.
