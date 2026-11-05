# WORKER17 notes

Working notes, kept current; the report is `REPORT.md` (not yet written). Measured and read, not designed.

## Status

- 2026-11-01: plan agreed with the owner (go-ahead given). Owner's additions: (1) question 8 includes jolt-cpp's compressed
  trace and GPU expander, the interface the prover actually reads; their format goes here early, for exchange with WORKER16
  (reading the same expander for registers 40-47); (2) measurements are RAM-only; registers get the reading side only.
- Done: the compressed trace and both expanders, for memory; the layout (Q1) in both codebases with both workloads' values,
  measured; how ram_K is chosen and its value (owner's request, 2026-11-02); the RAM ra-virtualization limits, checked at the
  pin. Next: the rest of the question 5 table, then the per-row rules (Q2), then the probe.
- **2026-11-02 (later): DECISIONS-v1.3 sections 12, 14, 15.** A proof is the whole run (T = 2²⁹, log T = 29, **8-bit chunks**);
  K up to 2³², one per run; question 3 measures 16 KB pages and 16 MiB regions. Done this turn: the probe (`mem_probe`) and
  its measurements at 2²⁹ (below, "Q3"); the K-dense parts of jolt-cpp's RAM sumchecks at the pin (below, "Q5: what is dense
  in K"), which found a **2²⁴ limit at the pin in GPU read-write checking, independent of chunk width**. Upstream's K-dense
  parts are not yet read.
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

**A proof is the whole run (DECISIONS-v1.3 section 14): log T = 29, 8-bit chunks.**

| K | d = ceil(log K / 8) | ra virtualization degree |
|---|---|---|
| EVM Zone, 2²³ | 3 | 4 |
| Cyclone, 2²⁴ | 3 | 4 |
| the K limit, 2³² | 4 | 5 |

The chunk-count limits above (`MAX_D` 6, slot 2-6) allow log K up to 48 at 8-bit chunks, so they are no limit for us. *The
log K ≤ 24 figure in the table applies only to 4-bit chunks (proofs under 2²⁵ rows), which we don't make; the 4M-row figures
I gave before (d = 6, degree 7) assumed 4M-row proofs and are superseded.* **But see "Q5: what is dense in K": the pin's GPU
read-write checking refuses K above 2²⁴ for another reason.**

**Binding order.** jolt-cpp's Rust config says RAM and register read-write checking bind **address first**
(`ram_rw_phase1_num_rounds: 0`, `phase2 = log K`; the comment cites `zkvm/ram/cpu/read_write_checking.hpp`,
`zkvm/ram/gpu/rw_checking.cu`, `zkvm/registers/gpu/read_write_checking.cu`; `src/backend/cpp/src/config.rs:161-175`). To
confirm in the kernels and against upstream's default.

## Q5: what is dense in K (jolt-cpp at the pin; upstream to do)

"Dense in K" = an array, loop or allocation over all K cells. Container sizes are elements of 32-byte field elements unless
said. Paths under `src/jolt-core/zkvm/ram/`.

| sumcheck | CPU (`cpu/`) | CUDA (`gpu/`) |
|---|---|---|
| read-write checking | sparse matrix over the T accesses (`read_write_matrix.tpp:54-57`, `max_nnz`) **plus dense K**: `val_aux` 2 × K (`:64`), a loop over all K (`:182`), `col_seen(K)` and loops over K in two places (`:788-796`, `:895-900`); `inc` over T (`read_write_checking.hpp:163`); after the address rounds, 3 × T (`:451`) | sparse over T (`rw_sparse.tpp:112-153`) **plus dense K**: `val_final` K (`:129`), `col_prefix` K + 1 (`:148`), a fold blob of about 5.25 K field elements (`:280-320`), an init kernel looping over K (`rw_sparse_init_kernels.cu:241`). **Refuses K > `RW_MAX_ADDRESS_SPACE` = 2²⁴** (`rw_sparse.hpp:33`, checked `rw_sparse.tpp:93-97`). **HEAD: 2³⁰** (`rw_sparse.hpp:35`). After the address rounds: 3 × T/2 (`rw_checking.cu:831-843`) |
| raf evaluation | dense K: a container of K (`raf_evaluation.hpp:101-111`), ra(k) accumulated over the trace then summed over k | dense K: 2 × K (`raf_evaluation.cu:264, 312`) |
| output check | dense K: `NUM_POLYS` × K (`output_check.hpp:88-89`), sharded over cards in the sumcheck but allocated whole; a loop to K (`:238`) | per card, `NUM_POLYS` × K / cards (`output_check.cu:139-141`), **plus the initial and final RAM snapshots, each K × 8 bytes, on every card** (`:149, 189-197`; again `:810-813`); `ram_k` is a `uint32_t` (`:89`) |
| val check | over T: 3 × T (`val_check.hpp:63`; `val_check.cu:582, 728`), `lt` table over T | over T |
| ra virtualization | over T: d × T/8 (`ra_virtual.hpp:154`), d eq tables of 2^chunk | over T (`ra_virtual.cu:302`), d tables of 2^chunk (`:167`) |
| Hamming booleanity | over T (`hamming_booleanity.hpp:126`) | over T (`hamming_booleanity.cu:172, 220`) |
| program-image init (stage 4) | | over the image's words, at word index `remapped(program_start)`, checked to lie within K (`stage4_program_image_init.cu:168-185`) |
| untrusted-advice openings (stage 4) | | over the advice words, checked to lie within K (`stage4_untrusted_advice_openings.cu:162-172`) |

So, at the pin: **GPU read-write checking caps K at 2²⁴** (both workloads fit; Cyclone exactly), **independent of the chunk
width**; HEAD raises it to 2³⁰. Above that, three sumchecks hold arrays of K: read-write checking (about 7 K field elements on
the GPU), raf evaluation (2 K) and the output check (K/cards field elements per card plus **two K × 8-byte snapshots per
card**). At K = 2³² those are 32 GiB per snapshot and 128 GiB per K-sized field array. The ra virtualization, val check and
Hamming booleanity are sized by T. The initial RAM's evaluation (Val_init) is done by the Rust side
(`sumcheck/slots/ram_val_check.rs` uses `jolt_verifier::stages::stage4::ram_val_check`): not yet read, nor upstream's kernels
(first hits: `jolt-kernels/src/optimized/ram_read_write.rs:489` loops `0..ram_k`; `ram_trace.rs:426` allocates `vec![0; ram_k]`).

## The verifier's commit: a16z `7189216` (owner, 2026-11-03)

- **`7189216` is jolt-cpp HEAD's `third-party/jolt`** (`88dca85`). **The pin `821e1bd` carries `f8fc214`** (2026-10-28), 20
  commits earlier. Read at `7189216` as asked; differences from the pin's submodule and from `17a9459` noted.
- `f8fc214..7189216`: in RAM paths, the verifier's stage-4 val check (`ram_val_check.rs`) loses its gamma domain separator
  (transcript only; #2110/#2108 rework the transcript), and **#2190 "range-check direct RAM loads"** (`4e55c9a5f`) adds a range
  check on loaded doublewords where `VirtualPext[Signed]`/`ANDN` read them. No change to what RAM checking proves.
- `7189216..17a9459`: in RAM, register and bytecode paths only prover kernels change (`jolt-kernels/src/optimized/*`, incl.
  `ram_ra_virtualization.rs` +27); **no relation, geometry or verifier change**. So everything in these notes read at `17a9459`
  on the checking side holds at `7189216`.

## Val_init (initial RAM evaluation) and the verifier's K checks, at `7189216`

- **Val_init(r_address)** = a public part plus committed contributions (`jolt-verifier/src/stages/stage4/ram_val_check.rs:286-332,
  370-461`; `verify.rs:140-150`). The **public part** is `public_initial_ram_evaluation` (`verify.rs:244-269`): the input bytes
  and, in full program mode, the program image, as word segments, evaluated by `sparse_segments_mle_msb`: **sized by the
  segments' words, not by K**. Each segment must lie in `[0, ram_K)` (`:271-297`). In committed program mode the image is a
  staged opening (`ProgramImageInitContribution`), and advice (trusted, untrusted) is a committed polynomial placed with
  `block_selector_mle_msb` (`ram_val_check.rs:482-530`). So the verifier never touches all K.
- The output check's publics (`stage2/ram_output_check.rs:50-80`): `range_mask_mle_msb` over the I/O mask and
  `sparse_segments_mle_msb` over the public I/O words: also not K.
- **The verifier bounds K** (`verifier.rs:473-494`, again `:982-1003`): a power of two, ≥ `compute_min_ram_k` and **≤
  `compute_max_ram_k`**, next_pow2 of the whole layout (`heap_end − lowest`)/8. So a run's K can't exceed its layout's range:
  K = 2³² needs a layout of more than 2³¹ cells (16 GiB above `lowest`). For our layouts the maximum is 2²⁴ (EVM Zone) and
  2²⁶ (Cyclone).
- **Binding order:** upstream's own prover binds **cycle first** (`jolt-prover/src/config.rs:179-185`: phase 1 = log T, phase 2
  = log K, RAM and registers); jolt-cpp binds address first (phase 1 = 0); the verifier takes either from the proof's config
  (`verifier.rs:1711` builds an address-first config). The kernels' "address first" in jolt-cpp is still to confirm by reading
  them.

## Q5: what is dense in K, upstream's prover at `7189216`

`jolt-kernels/src/optimized/`:

| sumcheck | dense in K | sized by T |
|---|---|---|
| read-write checking (`ram_read_write.rs`) | `val_final` oracle table of K and `val_init` reconstructed to K (`:422-431`); refuses log T or log K > 32, "packs indices as u32" (`:408-413`) | addresses, increments |
| output check (`ram_output_check.rs:68-95`) | `val_io` (K, zero-filled, `unsafe_allocate_zero_vec`), `io_mask` (K), `val_final` (K), split eq over log K | |
| raf evaluation (`ram_raf_evaluation.rs:45-55`) | `ra_folded` (`ram_trace.rs:426`, `vec![0; ram_k]`) and `unmap` (K) | the fold over T |
| val check (`ram_val_check.rs:72-85`) | `eq_table(r_address)` = `EqPolynomial::evals` over log K (`support.rs:488`): **K entries** | addresses, increments |
| ra claim reduction (`ram_ra_claim_reduction.rs:76-100`) | `eq_address` over log K, `fold_addresses` (K) | |
| ra virtualization, Hamming booleanity | 2^chunk tables | T |

Versus jolt-cpp at the pin: jolt-cpp's **GPU val check** uses at most 4 chunk tables of at most 8 bits for eq(r_address)
(`gpu/val_check.cu:65-75`), so it is not dense in K (and caps log K at 32); its CPU val check builds `eq_poly` over log K
variables (`cpu/val_check.hpp:108-111`; whether it materializes 2^log K not yet checked).

## Q2: the per-row rules (jolt-cpp at the pin; `isa/virtual.cpp`)

Every RAM row is one of `LD`, `SD`, `VirtualLoadB/H/W`, `VirtualStoreB/H/W`, and reads or writes **one aligned doubleword**
(the cell). Expansions with RAM rows, and their RAM accesses:

| source | RAM rows | notes |
|---|---|---|
| LD / SD | 1 | |
| LB/LBU/LH/LHU/LW/LWU | 1 (`VirtualLoadX`) of 3 rows | `expand_load` `:238-246` |
| SB/SH/SW | 2 (`VirtualLoadX`, `VirtualStoreX`, same cell) of 6 rows | `expand_narrow_store` `:248-261` |
| LR.D / LR.W | 1 (`LD` / `VirtualLoadW` via `expand_load`), sets the reservation vreg | `:~630-643` |
| SC.D | `VirtualAssertLTE(0x8000_0000, rs1)`, then `LD` and `SD` **always** (on failure it stores the old value back) | `:645-668` |
| SC.W | the assertion, a narrow load, then a narrow store (load + store): 3 RAM rows, the store always | `:670-711` |
| AMOSWAP.D / AMOADD.D (and the other .D AMOs) | `LD`, `SD` | `:713-745` |
| AMO*.W | `VirtualLoadW`, `VirtualStoreW` (one read, one write) | `:721-735, 746-` |
| secp inlines | loads and stores inside the inline sequences; the JIT counts their mem_log words (`syscalls.cpp:236`) | |

- A row records: the address (rs1 + imm, floored for narrow ops, `isa/riscv.hpp:241-253`), the pre-value (load: the word
  read; store: the word before) and the post-value (load: the same; store: rs2's value). Exactly what `RiscCycle` holds
  (`trace_owner.hpp:19-69`) and what the compressed trace reconstructs (above).
- Misaligned narrow accesses keep their low bits and are rejected (`isa/riscv.hpp:215-268`); `SC` asserts a RAM address.
- **The spec's output row differs on stores** (spec 4.19.12, word 4): it puts a store's pre-value in `mem_value` and leaves
  `rd_prev`/`rd` 0 (immediate slot); jolt-cpp's SoA packs the pre-value into the `rd` and `rd_prev` lanes
  (`expand_trace.cu:490-497`). Same information, different place. *(Open, spec item 113: a load to x0. The compressed trace
  logs the word; the SoA's `rd` lane is then 0. Whether the prover gets the loaded word some other way for such rows: to check.)*

## Q3: measured, 2²⁹ (`mem_probe`)

`mem_probe` (jolt-asic-planning `worker17-memory` `62ddcd1`, `tracer/src/mem_probe.cpp`) runs PR #2's tracer unchanged
(compressed trace, CPU expansion in 2²² slices) and replays RAM: a shadow of the initial image (the tracer VM's loaded RAM,
then input and advice bytes copied as the tracer copies them) updated by every store. **Every load's value and every store's
pre-value matched the shadow on both runs (0 mismatches), no access was below `lowest_address`, and no other row touched
memory.** So the initial image plus the stores explain every value read. Units: a page is 16 KB (2¹¹ cells), a 16 MiB region
is 2¹⁰ pages, both counted from `lowest_address` (`0x7E00_0000`, 16 MiB aligned). A page belongs to the layout region of its
first byte (so EVM Zone's termination word shares the input's page). 19 s and 17 s per run. Data:
`data/mem_probe-{evm-zone,cyclone}-29.json` and `.segments.csv`.

**Whole run**

| | EVM Zone | Cyclone |
|---|---|---|
| cells touched | 54,328 | 1,380,356 |
| read only / written | 10,703 / 43,625 | 365,296 / 1,015,060 |
| **pages touched** | **69** | **693** |
| **read-only / written pages** | **11 / 58** | **194 / 499** |
| 16 MiB regions with a touched page (leaves) | **3** | **4** |
| initial image, nonzero cells | 251,369 | 737,988 |
| cells whose final value differs from the initial | 17,589 | 685,948 |

Cross-checks with PR #1 (`docs/write-stream-experiments.md` on `tracer-standalone`): Cyclone's 499 written pages, 1,015,060
written cells, 737,988 nonzero initial cells and 685,948 changed cells all equal PR #1's. **EVM Zone differs because PR #1
measured another input** (`trace-29-e23d2a1c5937`, 275 M rows, 182 written pages); ours is `trace-29-4599adc50712` (355 M rows).

**By layout region** (pages; loads / stores are row counts)

| region | EVM Zone: pages (RO/W) | loads / stores | Cyclone: pages (RO/W) | loads / stores |
|---|---|---|---|---|
| untrusted advice | 1 (1/0); 1,105 cells | 8,220 / 0 | **178 (178/0)**; 362,755 cells, the whole 2.9 MB input | 2,902,036 / 0 |
| trusted advice | 4 (4/0); all 8,192 cells, 16 nonzero | 65,536 / 0 | none | |
| input (+ output, flags on its page) | 1 (0/1) | 108 / 0 (+1 / 1 flags) | 1 (0/1) | 1,042 / 0; output 722 / 722 |
| program (`.text` to `.bss`) | 7 (6/1) | 371,460 / 2,096 | 18 (16/2) | 3,530,786 / 47,799 |
| stack | 4 (0/4) | 63.5 M / 55.1 M | 2 (0/2) | 31.0 M / 24.6 M |
| heap | 52 (0/52) | 304,192 / 138,509 | 494 (0/494) | 22.9 M / 12.4 M |

**16 MiB regions (leaves) and the touched address ranges**

- EVM Zone, 3 leaves: `0x7E00_0000` (1 page: the untrusted advice's start); `0x7F00_0000` (5: trusted advice, input, flags);
  `0x8000_0000` (63: program data at `0x801D_0000`-`0x801E_C000`, stack `0x809D_C000`-`0x809E_C000`, heap from `0x809F_0000`
  to `0x80B2_0000`). The heap is not one dense run: past `0x80AA_0000` it touches every other page (28 runs in all).
- Cyclone, 4 leaves: `0x7E00_0000` (178: advice, dense, `0x7E00_0000`-`0x7E2C_8000`); `0x7F00_0000` (1: input, output, flags);
  `0x8000_0000` (18: program data); `0x8400_0000` (496: **stack and heap as one dense run, `0x8432_4000`-`0x84AE_4000`**,
  7.75 MiB). 8 runs in all.
- So the two dense areas the owner expected are, for Cyclone, the heap-plus-stack run (they adjoin: the heap starts at the stack's
  top) and the advice; for EVM Zone, everything above `0x8000_0000` sits in one leaf. The heap used is far below the earlier
  estimate (Cyclone 7.7 MiB of 256, not about 29 MiB: that was ram_K's bound, not use).

**Per 2²² segment** (85 of 128 segments have rows for EVM Zone, 89 for Cyclone; the rest are padding with no accesses)

| per segment, segments with rows | EVM Zone: min / median / max (sum) | Cyclone: min / median / max (sum) |
|---|---|---|
| pages touched | 18 / 18 / 47 (1,590) | 15 / 94 / 400 (6,921) |
| **pages written (the head-of-proof counts)** | **10 / 10 / 39 (908)** | **3 / 19 / 266 (2,225)** |
| pages read only in the segment | 6 / 8 / 12 (682) | 8 / 69 / 180 (4,696) |
| pages first touched in the run | 0 / 0 / 32 (69) | 0 / 0 / 263 (693) |
| leaves touched | 1 / 1 / 3 | 2 / 2 / 4 |
| cells whose first access in the segment is a load (read from the start image) | 460 / 999 / 19,225 | 5,816 / 10,012 / 369,663 |

The 128 written-page counts are in the CSVs' `pages_written` column. EVM Zone: 17, 39, then 10 in every segment, 32 in the last.
Cyclone: 69, 14, 15, 15, 15, 134, ..., peaks of 189 and 266, a run of 3s, then 10-46. **Read-only pages are new data (loads
weren't traced in PR #1):** whole run 11 (EVM Zone) and 194 (Cyclone, 178 of them the advice); per segment up to 12 and 180.

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
- 2026-11-02 (later): pulled DECISIONS-v1.3 12/14/15. Wrote and ran `mem_probe` (both 2²⁹ runs, replay check clean); read the
  K-dense parts of jolt-cpp's RAM sumchecks at the pin (GPU RW checking: K ≤ 2²⁴ at the pin, 2³⁰ at HEAD).
- 2026-11-03: read a16z `7189216` (HEAD's submodule; the pin's is `f8fc214`): Val_init and the output check's publics are
  sparse; the verifier bounds K by the layout; upstream's prover is dense in K in five sumchecks. Q2 per-row rules. REPORT.md.
