# WORKER17 report: RAM in jolt-cpp and upstream Jolt, and what our two workloads do with it

Measured and read; no design. Working notes, with every file:line, are `NOTES.md`; data in `data/`. Pins: jolt-cpp `821e1bd`
(v1.25's pin; its `third-party/jolt` is a16z `f8fc214`); the verifier at a16z `7189216` (jolt-cpp `88dca85`'s submodule, v1.3's
new pin), which on the checking side equals `17a9459` (DECISIONS-v1.3 section 12's read); `f8fc214` differs from it only in the
transcript and in #2190's range check on loaded words. Where jolt-cpp `88dca85` differs materially, it is said. Tools: jolt-asic-planning `worker17-memory` (`mem_probe`, on PR #2's tracer, unchanged). All runs local, 1 CPU, 2²⁹.

## Answers in brief

1. **Layout (Q1).** Same construction in jolt-cpp and upstream: a power-of-two I/O region ending at `0x8000_0000` (larger advice
   first, then the other advice, input, output, panic, termination), then program, 128 B canary, stack, heap; cell =
   `(address − lowest_address)/8`, `lowest_address` = the bottom of the advice. Both workloads: `lowest_address` =
   `0x7E00_0000`, a 32 MiB I/O region.
2. **ram_K.** The next power of two above the highest cell touched, floored by the program image (2²³ here), checked by the
   verifier to lie between `compute_min_ram_k` and `compute_max_ram_k` (the whole layout). **EVM Zone 2²³, Cyclone 2²⁴.**
3. **Pages (Q3).** At 16 KB pages: **EVM Zone 69 pages (11 read-only, 58 written) in 3 leaves of 16 MiB; Cyclone 693 (194
   read-only, 499 written) in 4 leaves.** Per 2²² segment, pages written: EVM Zone 10 / 10 / 39, Cyclone 3 / 19 / 266 (min /
   median / max). Every load and store pre-value is explained by the initial image plus the stores (replay check: 0
   mismatches).
4. **The pin's K limit.** jolt-cpp's GPU read-write checking refuses K > 2²⁴ at `821e1bd` (`RW_MAX_ADDRESS_SPACE`), whatever
   the chunk width; HEAD allows 2³⁰. At log T = 29 (8-bit chunks) the RAM ra virtualization is d = 3, degree 4 for both
   workloads, d = 4, degree 5 at 2³².
5. **Dense in K (Q5).** The verifier is not (Val_init and the I/O are sparse segment evaluations). Both provers are, in
   read-write checking, raf evaluation and the output check; upstream's also in the val check and the ra claim reduction
   (eq tables over all K); jolt-cpp's output check holds two K × 8-byte RAM snapshots per card.

## Q1: the layout

| region | EVM Zone | Cyclone |
|---|---|---|
| untrusted advice | `0x7E00_0000`-`0x7F00_0000` (16 MiB) | same |
| trusted advice | `0x7F00_0000`-`0x7F01_0000` (64 KiB) | empty |
| input / output | 112 B / empty | 4 KiB / 8 KiB |
| program | `0x8000_0000`-`0x801E_AD28` | `0x8000_0000`-`0x8032_89E0` |
| stack | 8 MiB, to `0x809E_ADA8` | 64 MiB, to `0x8432_8A60` |
| heap | 64 MiB, to `0x849E_ADA8` | 256 MiB, to `0x9432_8A60` |

Differences between the codebases are naming (stack start and end swapped), how program size is found (jolt-cpp: the ELF's
PT_LOAD extent, first segment must load at `0x8000_0000`; upstream: from the host), and jolt-cpp choosing ram_K from the run.
Measured with `capture_writes` and equal to a hand computation from `program.json` and the ELF headers.

**ram_K** (jolt-cpp `trace_owner.cpp:1134-1140`, `node_context.cpp:318-371`; upstream `jolt-program/src/preprocess/ram.rs:96-129`;
verifier `verifier.rs:473-494`):

| | EVM Zone | Cyclone |
|---|---|---|
| from the run | 2²³ | 2²⁴ (the stack top alone needs it) |
| program-image floor / `compute_min_ram_k` | 2²³ | 2²³ |
| `compute_max_ram_k` (the verifier's upper bound) | 2²⁴ | 2²⁶ |

## Q2: the per-row rules

Every RAM row is `LD`, `SD` or a narrow `VirtualLoad/StoreB/H/W`, and touches **one aligned doubleword**; narrow ops address the
word containing the access. A narrow load is 3 rows with 1 RAM read; a narrow store 6 rows with a read and a write of the same
cell; SC always stores (the old value on failure) after asserting a RAM address; AMOs are a read and a write. A row's memory
facts are the address (rs1 + imm), the pre-value and the post-value (load: the word read; store: rs2). Table in NOTES.

## Q3: the workloads' memory, 2²⁹, 16 KB pages and 16 MiB leaves

| | EVM Zone | Cyclone |
|---|---|---|
| cells touched (read-only / written) | 54,328 (10,703 / 43,625) | 1,380,356 (365,296 / 1,015,060) |
| pages touched (read-only / written) | **69 (11 / 58)** | **693 (194 / 499)** |
| leaves (16 MiB regions with a touched page) | **3**: `0x7E…`, `0x7F…`, `0x80…` | **4**: `0x7E…`, `0x7F…`, `0x80…`, `0x84…` |
| initial image, nonzero cells | 251,369 | 737,988 |
| cells changed by the run | 17,589 | 685,948 |

- **The dense areas.** Cyclone: the untrusted advice (178 read-only pages, the whole 2.9 MB read) and **one run of stack plus
  heap, `0x8432_4000`-`0x84AE_4000` (496 pages, 7.75 MiB)**: the heap starts at the stack's top, so they adjoin. EVM Zone:
  everything above `0x8000_0000` in one leaf (program data 7 pages, stack 4, heap 52, the heap touching every other page past
  `0x80AA_0000`). Cyclone uses 7.7 MiB of its 256 MiB heap.
- **Read-only pages (new: PR #1 traced no loads).** Whole run: 11 and 194. They are advice (EVM Zone reads all 64 KiB of its
  trusted advice, 16 cells of it nonzero), input, and program data (6 and 16 pages).
- **Per 2²² segment** (85 and 89 of 128 segments have rows; the rest are padding with no accesses):

| min / median / max | EVM Zone | Cyclone |
|---|---|---|
| pages touched | 18 / 18 / 47 | 15 / 94 / 400 |
| **pages written** | **10 / 10 / 39** | **3 / 19 / 266** |
| pages read only in the segment | 6 / 8 / 12 | 8 / 69 / 180 |
| leaves touched | 1 / 1 / 3 | 2 / 2 / 4 |

  The 128 written-page counts per workload are the `pages_written` column of `data/mem_probe-*-29.segments.csv`.
- **Cross-checks.** Rows, loads and stores equal WORKER9/16's. Cyclone's 499 written pages, written cells, nonzero initial and
  changed cells equal PR #1's exactly; EVM Zone's PR #1 figures (182 written pages) are for another input (275 M rows).

## Q4: the initial image

At the start: the ELF image (`.text` to `.bss` as loaded), the input bytes, and both advice blobs, each at its region's start;
the output, panic and termination words, stack and heap are zero. The probe built it that way and every value read matched.
The verifier evaluates its public part (inputs, and the program image in full program mode) as sparse word segments
(`stage4/verify.rs:244-269`); in committed program mode the image, and the advice always, enter as committed polynomials.

## Q5: the RAM sumchecks, sized by K or by T

Degrees (upstream relations, `jolt-claims/.../relations/ram/`): read-write checking 3, val check 3, output check 3, raf
evaluation 2, ra claim reduction 2, Hamming booleanity 3, ra virtualization d + 1. Registers' read-write checking 3; bytecode
read-raf d + 1. At log T = 29, 8-bit chunks: **K = 2²³ or 2²⁴: d = 3, degree 4; K = 2³²: d = 4, degree 5.**

| sumcheck | jolt-cpp at the pin: dense in K | upstream prover at `7189216`: dense in K |
|---|---|---|
| read-write checking | CPU `val_aux` 2 K and loops over K; GPU `val_final`, `col_prefix`, a fold blob (about 7 K field elements); **GPU refuses K > 2²⁴** (HEAD: 2³⁰) | `val_final`, `val_init` (K); log K ≤ 32 |
| val check | no: GPU ≤ 4 eq tables of ≤ 8 bits; CPU evaluates eq(r_address, cell) per access from tau (`cpu/val_check.hpp:242-244`) | eq table over K |
| output check | K/cards per card, **plus initial and final RAM snapshots, K × 8 B each, per card**; `uint32_t ram_k` | `val_io`, `io_mask`, `val_final` (K) |
| raf evaluation | K (CPU), 2 K (GPU) | `ra_folded`, `unmap` (K) |
| ra claim reduction | not read | eq over K |
| ra virtualization, Hamming booleanity | no (T, 2^chunk tables) | no |
| **verifier** | | **no**: sparse segments, range masks, block selectors |

At K = 2³² a K-sized field array is 128 GiB and a snapshot 32 GiB. The program-image and advice contributions are sized by
their words.

**Binding order.** jolt-cpp binds RAM and register read-write checking **address first**, configured (`config.rs:161-175`) and
confirmed in the kernels (RAM CPU `read_write_checking.hpp:131, 444`; RAM GPU `rw_checking.cu:68, 785-788`; registers GPU
`read_write_checking.cu:59`, CPU `:159-174, 442`); upstream's prover defaults to cycle first (`jolt-prover/src/config.rs:179-185`);
the verifier follows the proof's config.

## Q6: what each relation proves (upstream `7189216`)

| relation | proves | degree |
|---|---|---|
| R1CS (Spartan) | load/store: RamAddress = rs1 + imm − (narrow floor); other rows: RamAddress = 0; load: RamRead = RamWrite = RdWrite; store: RamWrite = rs2 (`composed/r1cs/rv64.rs:248-275`) | |
| raf evaluation | the RamAddress claim = Σ_k ra(k)·(8k + lowest) | 2 |
| read-write checking | each row's read and write values = Σ_k ra(k)·val(k) (write side + inc) | 3 |
| output check | Val_final = Val_io on `[input_start, 0x8000_0000)` | 3 |
| val check | val(k, t) = Val_init(k) + Σ_{j<t} inc(j)·wa(k, j) | 3 |
| ra claim reduction | raf's, RW's and val's `ra` openings are one | 2 |
| ra virtualization | ra = Π of the d committed chunks | d + 1 |
| Hamming booleanity | each row has 0 or 1 accesses | 3 |
| registers RW / val evaluation | the same for registers 0-127 | 3 / 2 |
| bytecode read-raf | each row's instruction and pc are its `instr_idx`'s bytecode entry | d + 1 |

Together: every load/store row touches exactly the cell `(rs1 + imm − lowest)/8` < K, reads the cell's current value, and
writes rs2 (store) or the same value (load); other rows touch nothing; cells start at Val_init and change only by stores.

## Q7: what isn't checked, and the edge cases

- **Final values outside `[input_start, 0x8000_0000)`** (program, stack, heap, advice) are unconstrained; the I/O range must end
  as the public I/O (input, output, panic, termination = 1) with its padding 0.
- **No region permissions:** stores to the program image, the advice or the canary are provable; any cell in `[0, K)` is RAM,
  including the I/O padding. Alignment comes from raf's `8k + lowest` and the narrow sequences' assertions.
- **Initial values** are the public segments plus the advice blocks, zero elsewhere.
- **A load to x0** (spec item 113): R1CS forces RamRead = RdWrite on every load, so jolt-cpp taking the word from the `rd` lane
  (`rw_sparse_init_kernels.cu:188-190`, `val_check_kernels.cu:96-98`) loses nothing. Loads with rd = x0 get a temporary rd
  (`isa/virtual.cpp:15-118, 276-281`, mirroring upstream); only `LR.D x0`/`LR.W x0` keep x0, and then the word must be 0 or the
  run can't be proven, in either codebase. **Measured: none** on either workload.
- **Below `lowest_address`:** jolt-cpp's prover silently treats it as no access (`jolt-utils/ram.hpp:16-30`; GPU also drops
  cells ≥ K, `rw_sparse_init_kernels.cu:93`), upstream's rejects it; it is unprovable either way (R1CS's RamAddress against
  raf's Σ ra·(8k + lowest) = 0). The tracer doesn't check it. **Measured: none.** Unchanged at `88dca85`.

## Q8: the interface the prover reads

- **jolt-cpp's compressed trace** carries memory as: chunk headers every 512 rows (registers 0-47, pc, `instr_idx`,
  `skip_cycles`, `mem_log_start`) and one mem_log word per load (the word read), per store (the pre-value) and per
  `VirtualAdvice`. The store's post-value is rs2's, the address rs1 + imm: nothing else is sent.
- **The GPU expander** rebuilds, per row: `mem_waddr_p1` = absolute address >> 3, plus 1 (the prover subtracts
  `lowest_address`), and puts the load word in `rd`, a store's pre-value in **both `rd` and `rd_prev`**, its post-value in
  `rs2`. No separate memory columns.
- A store's pre-value is the `rd` lane, its post-value `rs2`; a load's word is the `rd` lane, which R1CS makes equal to
  RdWriteValue anyway (Q7).
- **What the checking needs per row:** the cell index (for `ra` and raf), the read value and the write value (or the increment,
  post − pre); and per cell, Val_init (from the images) and Val_final (a dense table in both provers). All four per-row facts
  are in jolt-cpp's row and in spec 4.19.12's row (`mem_addr`, `mem_value`, `rs2`); the spec carries a store's pre-value in
  `mem_value` where jolt-cpp uses the `rd` lanes. **Not in the row:** Val_init and Val_final, which come from the images.

## Open

- Registers' and bytecode's reading side beyond the table above (owner: reading side only).
- WORKER16's thread: a rewound chunk's header holds the boundary's registers, not the sequence start's (`trace_owner.cpp:705`).
- For v1.3's pin (`88dca85`): RW checking's K limit 2³⁰ and the 31-bit packed column (`rw_sparse.hpp:35`,
  `rw_sparse_init_kernels.cu`); the ra virtualization's `MAX_D` 8; the output check's `uint32_t ram_k`.
