# WORKER12 report: what the two workloads execute, and what it costs the Tracer

All numbers are measured, from one pass over each run's trace in trace order. Nothing here is a decision.

## Summary

- **Cyclone (2^29)**: 371,526,406 traced rows and **96,687 untraced rows (0.026%)**, all from the division family
  (REMU 95,893, DIVU 791, DIVW 3; one untraced row each). It runs **no secp256k1 inline**. Per 2^16-row window: at
  most 129 untraced rows.
- **EVM Zone (2^29)**: 354,861,942 traced rows and **32,016,730 untraced rows (9.02% of traced)**. 98.4% come from
  **254 `SECP256K1_DIVQ`** executions (123,979 untraced rows each), 1.3% from 2 `SECP256K1_DIVR` (214,957 each).
- **They are not spread out.** Every secp256k1 inline of the EVM Zone run starts in **four consecutive windows, 33-36**
  (traced rows 2,162,688-2,424,831, the first 0.7% of the run). Window 34 holds 89 DIVQ: **11,066,935 untraced rows
  against 65,536 traced, 169x**. The four windows together carry 32.0M untraced rows against 262,144 traced. The other
  5,411 windows carry at most 6 untraced rows each.
- **The small EVM Zone input (trace-23)** has nearly the same secp256k1 work (257 DIVQ, 2 DIVR) in the same windows
  (33-36), on 5.8M traced rows: there the untraced rows are **5.55x the traced rows**. The inverse count does not
  scale with the run's length in these two inputs.
- **Controller-fed set**: Cyclone runs 28,034 inlines (BLAKE2, BLAKE3, BLAKE3_KEYED64) and **8,751 CSR instructions**
  (875 trap entries and returns, 10 CSR instructions each, plus one `mtvec` write); they fall in 489 windows, at most
  160 per window, 10,501 traced rows in all. EVM Zone runs 662 inlines (secp256k1 only) and **no CSR instructions**.
  Neither run executes a SHA-256, Keccak or BIGINT256_MUL inline.
- **The inverse (item 6)**: with libsecp256k1's addition chain for p - 2 (255 squarings, 15 multiplies), DIVQ drops from
  123,979 to **62,409** untraced rows (0.503x); DIVR with a generic sliding window (k = 5) from 214,957 to 128,823
  (0.599x; a hand-made chain would do somewhat better). On EVM Zone 2^29: 32.0M -> **16.2M** untraced rows (9.02% ->
  4.57% of traced), and the heaviest window 11.07M -> **5.59M**.

## Method

**Counting** (`tracer/tables/count_exec.cpp` on branch
[`worker12-counts`](https://github.com/Quarky93/jolt-asic-planning/tree/worker12-counts) of jolt-asic-planning, made
from `tracer-standalone-main`): the compressed trace is generated as jolt-cpp does and expanded on the CPU one slice
(2^20 rows) at a time, exactly as `capture_writes` does; the tracer is not changed. Rows are visited in trace order.
Every row is attributed to its source instruction by its pc; an execution starts at a non-virtual row or at the first
row of a virtual sequence. Executions are grouped by **template**, using `build_tables.cpp`'s own `decode_index`,
`natural_class` and `fed_key` (the file is `#include`d), so the grouping is the tables': ROM instructions by decode index,
inlines and CSR instructions by their controller-fed key (CSR number and register pattern). The trace is **cut every
2^16 traced rows** in the same pass, and each window records how many executions of each template start in it.

**Rows** (`tracer/tables/count_report.py`, same branch): for each template it reads the Tracer's tables in meshsim
`wip/tables/tracer/` (ROM via the decode table; controller-fed via `template_index.bin` and `segments.bin`), counts the
traced words and the untraced rows (untraced words, each segment times its repeat count), and multiplies by the
executions. A window's untraced rows are those of the executions that **start** in it (the Tracer computes an
instruction's advice before its traced rows); an execution that crosses a window boundary is counted in its first window.

**Checks**, all passing on all four runs:
- count_exec: counted rows = the run's cycle count; every row's pc is a program instruction; every execution is
  contiguous, exactly jolt-cpp's bytecode expansion length, with `inline_sequence_remaining` counting down.
- **Totals against WORKER9**: Cyclone 371,526,406 and EVM Zone 354,861,942 traced rows, both exact.
- count_report: for every template in each program, the tables' traced word count equals jolt-cpp's expansion length,
  and the tables' source opcode name equals jolt-cpp's; window sums equal the run totals; the DIVQ/DIVR untraced counts
  equal `tracer_tables.py`'s programs (123,979; 214,957).

**Runs**: local, 1 CPU, 3 GB, `vm.overcommit_memory=1` (no Quest runner needed). Cyclone 2^29: trace 7 s, count 13 s;
EVM Zone 2^29: 11 s and 13 s. Small inputs: Cyclone `input/22`, EVM Zone `trace-23-812ded164e49`.

**Rerun**:
```sh
cd tracer && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 && cmake --build build
bash tables/count.sh $PWD $PWD/build build/count_exec
build/count_exec cyclone cyclone/input/29/input-15961266 build/out/cyclone-29
build/count_exec evm-zone-4e3451c1d8c5 evm-zone-4e3451c1d8c5/inputs/trace-29-4599adc50712 build/out/evm-29
python3 tables/count_report.py <meshsim>/wip/tables <meshsim>/WORKER12/data cyclone-29=build/out/cyclone-29 evm-29=build/out/evm-29
python3 <meshsim>/WORKER12/report_tables.py      # the tables below
```
(`-DCMAKE_POLICY_VERSION_MINIMUM=3.5` is only needed with CMake 4, for nlohmann_json 3.10.5.)

## Data (`data/`)

Per run (`cyclone-29`, `evm-29`, `cyclone-22`, `evm-23`): `-opcodes.csv` (every source opcode the tables know, executed
or not), `-templates.csv` (every template of the program: decode index or key, executions, traced/untraced rows per
execution and in total), `-windows.csv` (per window: traced rows, executions, untraced rows, inline and CSR executions,
DIVQ, DIVR), `-fed.csv` (each controller-fed template: executions, windows with it, mean and max per window),
`-summary.json`. `inverse.csv`: item 6.

## Tables

### 1-2. Source instructions: executions, rows per execution (from the tables), 2^29 runs

| opcode | class | traced/exec | untraced/exec | Cyclone execs | Cyclone untraced | EVM Zone execs | EVM Zone untraced |
|---|---|---|---|---|---|---|---|
| ADD | rom | 1 | 0 | 34,996,907 | 0 | 78,068,755 | 0 |
| ADDI | rom | 1 | 0 | 28,239,068 | 0 | 8,995,715 | 0 |
| SUB | rom | 1 | 0 | 2,117,020 | 0 | 2,712,048 | 0 |
| XOR | rom | 1 | 0 | 9,268,094 | 0 | 7,707,106 | 0 |
| XORI | rom | 1 | 0 | 3,176,650 | 0 | 187,183 | 0 |
| OR | rom | 1 | 0 | 10,118,683 | 0 | 9,483,193 | 0 |
| ORI | rom | 1 | 0 | 22,024 | 0 | 1,107 | 0 |
| AND | rom | 1 | 0 | 5,892,781 | 0 | 11,969,161 | 0 |
| ANDI | rom | 1 | 0 | 3,922,238 | 0 | 1,265,936 | 0 |
| SLL | rom | 2 | 0 | 34,351 | 0 | 82,907 | 0 |
| SLLI | rom | 1 | 0 | 11,560,747 | 0 | 13,205,942 | 0 |
| SRL | rom | 2 | 0 | 358,664 | 0 | 82,044 | 0 |
| SRLI | rom | 1 | 0 | 10,281,958 | 0 | 14,163,718 | 0 |
| SRA | rom | 2 | 0 | 0 | 0 | 0 | 0 |
| SRAI | rom | 1 | 0 | 29,080 | 0 | 363,975 | 0 |
| SLT | rom | 1 | 0 | 283 | 0 | 436 | 0 |
| SLTI | rom | 1 | 0 | 2,548 | 0 | 0 | 0 |
| SLTU | rom | 1 | 0 | 6,493,411 | 0 | 21,408,601 | 0 |
| SLTIU | rom | 1 | 0 | 254,625 | 0 | 288,555 | 0 |
| LB | rom | 3 | 0 | 196,086 | 0 | 37,919 | 0 |
| LH | rom | 3 | 0 | 4,973 | 0 | 3,032 | 0 |
| LW | rom | 3 | 0 | 1,294,059 | 0 | 85,620 | 0 |
| LBU | rom | 3 | 0 | 16,092,710 | 0 | 1,800,118 | 0 |
| LHU | rom | 3 | 0 | 229,755 | 0 | 1,482 | 0 |
| SB | rom | 6 | 0 | 7,176,003 | 0 | 823,697 | 0 |
| SH | rom | 6 | 0 | 93,655 | 0 | 4,310 | 0 |
| SW | rom | 6 | 0 | 1,454,963 | 0 | 15,187 | 0 |
| BEQ | rom | 1 | 0 | 14,168,666 | 0 | 341,994 | 0 |
| BNE | rom | 1 | 0 | 6,212,605 | 0 | 693,735 | 0 |
| BLT | rom | 1 | 0 | 158,556 | 0 | 1,368 | 0 |
| BGE | rom | 1 | 0 | 298,954 | 0 | 706 | 0 |
| BLTU | rom | 1 | 0 | 3,386,424 | 0 | 1,586,395 | 0 |
| BGEU | rom | 1 | 0 | 1,738,412 | 0 | 171,291 | 0 |
| JAL | rom | 1 | 0 | 1,933,001 | 0 | 113,449 | 0 |
| JALR | rom | 1 | 0 | 2,603,298 | 0 | 1,883,734 | 0 |
| AUIPC | rom | 1 | 0 | 1,850,119 | 0 | 1,139,461 | 0 |
| LUI | rom | 1 | 0 | 1,603,939 | 0 | 9,552,453 | 0 |
| ECALL | rom | 7 | 0 | 875 | 0 | 0 | 0 |
| EBREAK | rom | 1 | 0 | 0 | 0 | 0 | 0 |
| MRET | rom | 1 | 0 | 875 | 0 | 0 | 0 |
| MUL | rom | 1 | 0 | 7,090,869 | 0 | 21,785,954 | 0 |
| MULH | rom | 7 | 0 | 0 | 0 | 0 | 0 |
| MULHU | rom | 1 | 0 | 6,736,668 | 0 | 20,441,609 | 0 |
| MULHSU | rom | 11 | 0 | 0 | 0 | 0 | 0 |
| DIV | rom | 12 | 1 | 0 | 0 | 0 | 0 |
| DIVU | rom | 8 | 1 | 791 | 791 | 108 | 108 |
| REM | rom | 9 | 4 | 0 | 0 | 0 | 0 |
| REMU | rom | 7 | 1 | 95,893 | 95,893 | 0 | 0 |
| FENCE | rom | 1 | 0 | 0 | 0 | 0 | 0 |
| SD | rom | 1 | 0 | 28,110,032 | 0 | 54,408,652 | 0 |
| LD | rom | 1 | 0 | 33,173,434 | 0 | 61,451,614 | 0 |
| LWU | rom | 3 | 0 | 41,749 | 0 | 1,931 | 0 |
| SLLW | rom | 2 | 0 | 142,896 | 0 | 0 | 0 |
| SLLIW | rom | 1 | 0 | 437,569 | 0 | 1 | 0 |
| SRLW | rom | 2 | 0 | 72,360 | 0 | 0 | 0 |
| SRLIW | rom | 1 | 0 | 955,649 | 0 | 161,720 | 0 |
| SRAW | rom | 2 | 0 | 0 | 0 | 0 | 0 |
| SRAIW | rom | 1 | 0 | 37,185 | 0 | 0 | 0 |
| SUBW | rom | 1 | 0 | 257,430 | 0 | 0 | 0 |
| ADDW | rom | 1 | 0 | 238,530 | 0 | 1 | 0 |
| ADDIW | rom | 1 | 0 | 838,408 | 0 | 6,028 | 0 |
| MULW | rom | 1 | 0 | 75,934 | 0 | 0 | 0 |
| DIVW | rom | 14 | 1 | 3 | 3 | 0 | 0 |
| DIVUW | rom | 11 | 2 | 0 | 0 | 0 | 0 |
| REMW | rom | 11 | 4 | 0 | 0 | 0 | 0 |
| REMUW | rom | 9 | 2 | 0 | 0 | 0 | 0 |
| VirtualRev8W | rom | 1 | 0 | 0 | 0 | 0 | 0 |
| VirtualAssertEQ | rom | 1 | 0 | 0 | 0 | 0 | 0 |
| VirtualHostIO | rom | 1 | 0 | 1,018 | 0 | 0 | 0 |
| LR_W | rom | 7 | 0 | 27,230 | 0 | 0 | 0 |
| LR_D | rom | 5 | 0 | 0 | 0 | 0 | 0 |
| SC_W | rom | 21 | 0 | 27,230 | 0 | 0 | 0 |
| SC_D | rom | 12 | 0 | 0 | 0 | 0 | 0 |
| AMOSWAP_W | rom | 8 | 0 | 1 | 0 | 0 | 0 |
| AMOSWAP_D | rom | 3 | 0 | 0 | 0 | 0 | 0 |
| AMOADD_W | rom | 9 | 0 | 0 | 0 | 0 | 0 |
| AMOADD_D | rom | 4 | 0 | 0 | 0 | 0 | 0 |
| AMOXOR_W | rom | 9 | 0 | 0 | 0 | 0 | 0 |
| AMOXOR_D | rom | 4 | 0 | 0 | 0 | 0 | 0 |
| AMOAND_W | rom | 9 | 0 | 0 | 0 | 0 | 0 |
| AMOAND_D | rom | 4 | 0 | 0 | 0 | 0 | 0 |
| AMOOR_W | rom | 9 | 0 | 0 | 0 | 0 | 0 |
| AMOOR_D | rom | 4 | 0 | 0 | 0 | 0 | 0 |
| AMOMIN_W | rom | 13 | 0 | 0 | 0 | 0 | 0 |
| AMOMIN_D | rom | 7 | 0 | 0 | 0 | 0 | 0 |
| AMOMAX_W | rom | 13 | 0 | 0 | 0 | 0 | 0 |
| AMOMAX_D | rom | 7 | 0 | 0 | 0 | 0 | 0 |
| AMOMINU_W | rom | 13 | 0 | 0 | 0 | 0 | 0 |
| AMOMINU_D | rom | 7 | 0 | 0 | 0 | 0 | 0 |
| AMOMAXU_W | rom | 13 | 0 | 0 | 0 | 0 | 0 |
| AMOMAXU_D | rom | 7 | 0 | 0 | 0 | 0 | 0 |
| SHA256_INLINE | inline | 1900 | 0 | 0 | 0 | 0 | 0 |
| SHA256_INIT_INLINE | inline | 1864 | 0 | 0 | 0 | 0 | 0 |
| KECCAK256 | inline | 3087 | 0 | 0 | 0 | 0 | 0 |
| KECCAK256_ABSORB_PERMUTE | inline | 3121 | 0 | 0 | 0 | 0 | 0 |
| BLAKE2_INLINE | inline | 1067 | 0 | 15,513 | 0 | 0 | 0 |
| BLAKE3_INLINE | inline | 648 | 0 | 1,409 | 0 | 0 | 0 |
| BLAKE3_KEYED64_INLINE | inline | 646 | 0 | 11,112 | 0 | 0 | 0 |
| BIGINT256_MUL_INLINE | inline | 141 | 0 | 0 | 0 | 0 | 0 |
| SECP256K1_MULQ_INLINE | inline | 186 | 237 | 0 | 0 | 139 | 32,943 |
| SECP256K1_SQUAREQ_INLINE | inline | 167 | 233 | 0 | 0 | 265 | 61,745 |
| SECP256K1_DIVQ_INLINE | inline | 194 | 123979 | 0 | 0 | 254 | 31,490,666 |
| SECP256K1_MULR_INLINE | inline | 232 | 414 | 0 | 0 | 1 | 414 |
| SECP256K1_SQUARER_INLINE | inline | 213 | 410 | 0 | 0 | 0 | 0 |
| SECP256K1_DIVR_INLINE | inline | 240 | 214957 | 0 | 0 | 2 | 429,914 |
| SECP256K1_GLVR_ADV_INLINE | inline | 13 | 940 | 0 | 0 | 1 | 940 |
| CSRRW | csr | 1-3 | 0 | 3,501 | 0 | 0 | 0 |
| CSRRS | csr | 1-3 | 0 | 5,250 | 0 | 0 | 0 |

### 1. CSR instructions by CSR and register pattern (executed keys), Cyclone 2^29 (EVM Zone executes none)

| key | instruction | traced/exec | executions |
|---|---|---|---|
| 300020f3 | CSRRS mstatus (rd,rs1)=(1,0) | 1 | 875 |
| 30009073 | CSRRW mstatus (rd,rs1)=(0,1) | 1 | 875 |
| 30509073 | CSRRW mtvec (rd,rs1)=(0,1) | 1 | 1 |
| 34001073 | CSRRW mscratch (rd,rs1)=(0,0) | 1 | 875 |
| 340020f3 | CSRRS mscratch (rd,rs1)=(1,0) | 1 | 1,750 |
| 340090f3 | CSRRW mscratch (rd,rs1)=(1,1) | 3 | 875 |
| 341020f3 | CSRRS mepc (rd,rs1)=(1,0) | 1 | 875 |
| 34109073 | CSRRW mepc (rd,rs1)=(0,1) | 1 | 875 |
| 342020f3 | CSRRS mcause (rd,rs1)=(1,0) | 1 | 875 |
| 343020f3 | CSRRS mtval (rd,rs1)=(1,0) | 1 | 875 |

### 3. Totals

| run | traced rows | source executions | untraced rows | untraced / traced | untraced share of all rows |
|---|---|---|---|---|---|
| Cyclone 2^29 | 371,526,406 | 265,664,724 | 96,687 | 0.026% | 0.026% |
| EVM Zone 2^29 | 354,861,942 | 346,500,613 | 32,016,730 | 9.022% | 8.276% |
| Cyclone 22 | 2,705,819 | 1,911,900 | 256 | 0.009% | 0.009% |
| EVM Zone trace-23 | 5,833,069 | 4,426,721 | 32,389,265 | 555.270% | 84.739% |

Top untraced contributors, Cyclone 2^29:

| opcode | executions | untraced rows | share |
|---|---|---|---|
| REMU | 95,893 | 95,893 | 99.18% |
| DIVU | 791 | 791 | 0.82% |
| DIVW | 3 | 3 | 0.00% |

Top untraced contributors, EVM Zone 2^29:

| opcode | executions | untraced rows | share |
|---|---|---|---|
| SECP256K1_DIVQ_INLINE | 254 | 31,490,666 | 98.36% |
| SECP256K1_DIVR_INLINE | 2 | 429,914 | 1.34% |
| SECP256K1_SQUAREQ_INLINE | 265 | 61,745 | 0.19% |
| SECP256K1_MULQ_INLINE | 139 | 32,943 | 0.10% |
| SECP256K1_GLVR_ADV_INLINE | 1 | 940 | 0.00% |
| SECP256K1_MULR_INLINE | 1 | 414 | 0.00% |
| DIVU | 108 | 108 | 0.00% |

### 4. Untraced rows per window of 2^16 traced rows

| run | windows | with untraced | mean | p50 | p90 | p99 | p99.9 | max | max / 2^16 |
|---|---|---|---|---|---|---|---|---|---|
| Cyclone 2^29 | 5,670 | 2,869 | 17.1 | 1 | 55 | 93 | 120 | 129 | 0.00 |
| EVM Zone 2^29 | 5,415 | 107 | 5,912.6 | 0 | 0 | 1 | 1 | 11,066,935 | 168.87 |
| Cyclone 22 | 42 | 20 | 6.1 | 0 | 7 | 73 | 73 | 73 | 0.00 |
| EVM Zone trace-23 | 90 | 7 | 359,880.7 | 0 | 0 | 11,066,469 | 11,066,469 | 11,066,469 | 168.86 |

Heaviest windows, Cyclone 2^29:

| window | first row | untraced rows | DIVQ | DIVR | inline execs |
|---|---|---|---|---|---|
| 5018 | 328,859,648 | 129 | 0 | 0 | 0 |
| 2392 | 156,762,112 | 121 | 0 | 0 | 0 |
| 2460 | 161,218,560 | 120 | 0 | 0 | 0 |
| 2512 | 164,626,432 | 120 | 0 | 0 | 0 |
| 3578 | 234,487,808 | 120 | 0 | 0 | 0 |
| 3973 | 260,374,528 | 120 | 0 | 0 | 0 |

Heaviest windows, EVM Zone 2^29:

| window | first row | untraced rows | DIVQ | DIVR | inline execs |
|---|---|---|---|---|---|
| 34 | 2,228,224 | 11,066,935 | 89 | 0 | 229 |
| 35 | 2,293,760 | 10,818,511 | 87 | 0 | 225 |
| 33 | 2,162,688 | 5,406,380 | 40 | 2 | 112 |
| 36 | 2,359,296 | 4,724,796 | 38 | 0 | 96 |
| 74 | 4,849,664 | 6 | 0 | 0 | 0 |
| 42 | 2,752,512 | 1 | 0 | 0 | 0 |

### 5. The controller-fed set, 2^29 runs

Cyclone 2^29: 28,034 inline executions (24,643,755 traced rows), 8,751 CSR executions (10,501 traced rows, in 489 windows); per window at most 57 inlines, 160 CSR.

| template | label | executions | windows with | mean / window | max / window (window) | traced rows | untraced rows |
|---|---|---|---|---|---|---|---|
| 4 | BLAKE2_INLINE | 15,513 | 692 | 2.736 | 52 (1234) | 16,552,371 | 0 |
| 5 | BLAKE3_INLINE | 1,409 | 26 | 0.249 | 57 (976) | 913,032 | 0 |
| 6 | BLAKE3_KEYED64_INLINE | 11,112 | 447 | 1.960 | 34 (5616) | 7,178,352 | 0 |
| 18 | CSRRS mstatus (rd,rs1)=(1,0) | 875 | 480 | 0.154 | 16 (5222) | 875 | 0 |
| 19 | CSRRW mstatus (rd,rs1)=(0,1) | 875 | 477 | 0.154 | 16 (5222) | 875 | 0 |
| 29 | CSRRW mtvec (rd,rs1)=(0,1) | 1 | 1 | 0.000 | 1 (0) | 1 | 0 |
| 35 | CSRRW mscratch (rd,rs1)=(0,0) | 875 | 480 | 0.154 | 16 (5222) | 875 | 0 |
| 38 | CSRRS mscratch (rd,rs1)=(1,0) | 1,750 | 480 | 0.309 | 32 (5222) | 1,750 | 0 |
| 40 | CSRRW mscratch (rd,rs1)=(1,1) | 875 | 480 | 0.154 | 16 (5222) | 2,625 | 0 |
| 48 | CSRRS mepc (rd,rs1)=(1,0) | 875 | 480 | 0.154 | 16 (5222) | 875 | 0 |
| 49 | CSRRW mepc (rd,rs1)=(0,1) | 875 | 477 | 0.154 | 16 (5222) | 875 | 0 |
| 58 | CSRRS mcause (rd,rs1)=(1,0) | 875 | 480 | 0.154 | 16 (5222) | 875 | 0 |
| 68 | CSRRS mtval (rd,rs1)=(1,0) | 875 | 480 | 0.154 | 16 (5222) | 875 | 0 |

EVM Zone 2^29: 662 inline executions (120,110 traced rows), 0 CSR executions (0 traced rows, in 0 windows); per window at most 229 inlines, 0 CSR.

| template | label | executions | windows with | mean / window | max / window (window) | traced rows | untraced rows |
|---|---|---|---|---|---|---|---|
| 8 | SECP256K1_MULQ_INLINE | 139 | 4 | 0.026 | 46 (35) | 25,854 | 32,943 |
| 9 | SECP256K1_SQUAREQ_INLINE | 265 | 4 | 0.049 | 94 (34) | 44,255 | 61,745 |
| 10 | SECP256K1_DIVQ_INLINE | 254 | 4 | 0.047 | 89 (34) | 49,276 | 31,490,666 |
| 11 | SECP256K1_MULR_INLINE | 1 | 1 | 0.000 | 1 (33) | 232 | 414 |
| 13 | SECP256K1_DIVR_INLINE | 2 | 1 | 0.000 | 2 (33) | 480 | 429,914 |
| 14 | SECP256K1_GLVR_ADV_INLINE | 1 | 1 | 0.000 | 1 (33) | 13 | 940 |

### 6. The inverse: Fermat as built against addition chains (untraced rows per execution)

| inline | variant | squarings | multiplies | rows / step | prologue | epilogue | untraced rows | vs Fermat |
|---|---|---|---|---|---|---|---|---|
| DIVQ | Fermat loop (as built) | 256 | 256 | 482 | 129 | 458 | 123,979 | 1.000 |
| DIVQ | libsecp256k1 chain | 255 | 15 | 229 | 121 | 458 | 62,409 | 0.503 |
| DIVQ | sliding window k=5 | 252 | 66 | 229 | 121 | 458 | 73,401 | 0.592 |
| DIVR | Fermat loop (as built) | 256 | 256 | 836 | 129 | 812 | 214,957 | 1.000 |
| DIVR | sliding window k=5 | 252 | 63 | 406 | 121 | 812 | 128,823 | 0.599 |

Per run, with the best chain above for each inline (other untraced rows unchanged):

| run | DIVQ | DIVR | untraced, Fermat | untraced, chain | untraced / traced, Fermat -> chain | max window, Fermat -> chain |
|---|---|---|---|---|---|---|
| Cyclone 2^29 | 0 | 0 | 96,687 | 96,687 | 0.03% -> 0.03% | 129 -> 129 |
| EVM Zone 2^29 | 254 | 2 | 32,016,730 | 16,205,682 | 9.02% -> 4.57% | 11,066,935 -> 5,587,205 |
| Cyclone 22 | 0 | 0 | 256 | 256 | 0.01% -> 0.01% | 73 -> 73 |
| EVM Zone trace-23 | 257 | 2 | 32,389,265 | 16,393,507 | 555.27% -> 281.04% | 11,066,469 -> 5,586,739 |

## Notes on item 6

- A chain step here is one `mulmod` (the loop body's own: 225 rows for q, 402 for r) plus 4 moves into fixed registers;
  squaring costs the same as multiplying in these programs (the inlines' own squarings save only 4 rows). The chain
  variants drop the select and the exponent shifting, and the prologue's exponent setup (8 rows); the epilogue is
  unchanged. The chains are checked by exponent arithmetic (each builds exactly m - 2); the rows are not executed.
- What a chain needs that the loop doesn't: several segments (a run of squarings, then a multiply by a stored power,
  about 2 per multiply: ~31 for libsecp256k1's p - 2 chain, ~140 for the k = 5 window) and live registers for the stored
  powers (6 values, 24 registers for the p - 2 chain; 16 odd powers, 64 registers for k = 5). The Tracer's bank 1 has 110
  (146-255); the Fermat loop uses 26 at peak. Not checked against the segment format's limits.
- DIVR's figure is a generic sliding window; libsecp256k1's older hand-made scalar chain for n - 2 should be a little
  better. I didn't reconstruct it.

## What the numbers imply (for the owner)

- On Cyclone, untraced work is negligible at every scale measured (at most 129 rows in any 2^16-row window).
- On EVM Zone the total (9%) understates the local effect: a Tracer that produces one row (traced or untraced) per
  cycle spends about 32M cycles in four windows that hold 262,144 traced rows, i.e. those windows take ~122x their traced
  length, unless the untraced work runs ahead or alongside. On the small input the secp256k1 work dominates the whole run.
- An addition chain halves DIVQ, and with it both the EVM Zone total and its worst window; it doesn't change where the
  work falls.
- The CSR instructions are 8,751 executions (0.003% of Cyclone's executions), in bursts of 10 at each trap, and absent
  from EVM Zone.
- Observations, not checked further: EVM Zone executes no Keccak inline (its Keccak is in software); the secp256k1 work
  (~256 inverses) is the same on both EVM Zone inputs and sits at the same point early in the run, which looks like a
  per-block cost (e.g. sender recovery) rather than a per-row one.
