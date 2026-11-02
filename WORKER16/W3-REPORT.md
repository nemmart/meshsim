# WORKER16: W3, the segment file tool (DECISIONS-v1.3 sections 13, 15, 16)

Built on jolt-asic-planning branch **`worker16-w3`** (from `tracer-standalone-main`, commit `ca30b5e`), in
`tracer/segments/`, with nothing in jolt-cpp changed. Runs were local: 1 CPU, `vm.overcommit_memory=1`.

## The tool (slow version)

**`segment_writer <program dir> <input dir> <out>`** runs the trace with jolt-cpp's own interpreter
(`execute_instruction<true>` against RAM, included from `trace_owner.cpp`, as in `state_probe`) and writes section 16's
file. Every value is exact, including registers 40-47 and a segment that starts mid-sequence.

- **Frame 1:** per segment, the distinct 16 KB pages (cell >> 11) among the cells it stores, where cell =
  (address − `lowest_address`) / 8 (section 12).
- **Frame 2:** pc, `instr_idx`, next offset, registers 0-127, taken at the cut row before it runs.
  - **The convention is `delivery_writer`'s** (`delivery_writer.cpp:360`): `instr_idx` is the row's own virtual-instruction
    index, and the next offset is `instr_idx` − the expansion's first index (rows already run).
  - **Padding segments** are pc 0, `instr_idx` 0, offset 0 and the final registers.
- **Frame 3:** the words the segment's advice rows consume, in row order: a secp row's buffer word, or a division row's
  result. A crossing sequence's pending words therefore head the next segment's log.
- **Frames 4 and 5:**
  - The update words are the segment's dirty cells (every cell stored, including unchanged values), as maximal runs in
    address order. Each run is a header word (start cell | count << 32) followed by the values, read from RAM at the
    segment's end.
  - Frame 5 is the 32-entry move-to-front LRU code: LSB first, table zeroed per frame, the lowest matching index on a hit.
  - The writer emits the smaller of the two, raw on a tie. An empty list is always frame 4 (the padding rule).
    `--force=raw|lru` is for testing.
- **Test cuts.** `--cuts=` takes an explicit list. `--cuts-auto=N` picks hard rows in a scan pass:
  - mid-secp with advice pending;
  - mid-inline with registers 48-127 live;
  - between LR and SC;
  - inside ECALL or MRET;
  - other mid-sequence rows;
  - random rows and instruction starts;
  - plus cuts at and past the end (padding).

  Such files carry one page count per segment, off-spec on purpose.
- **Side files:** `<out>.cuts` and `<out>.json`, the per-segment statistics.

**`segment_check <program dir> <input dir> <file> <out json>`** decodes the file with its own decoder and checks it
against jolt-cpp's reference rows (`gen_compressed_trace`, then `expand_trace`):
1. **Frame 2's header:** pc, `instr_idx` and offset against the segment's first reference row.
2. **Restart (as in T2).** The interpreter starts from frame 2 alone on the start image, in the Tracer's model: no
   `prepare_advice`, and advice rows take frame 3's words. It must:
   - reproduce every reference row of the segment byte for byte;
   - consume exactly frame 3;
   - halt exactly at the trace's end.
3. **Frame 3:** equal to the reference rows' advice words.
4. **Frames 4 and 5:** the runs must be maximal and ordered, and equal to the cells the reference rows store in the
   segment, with their last values.
5. **Frame 1:** the distinct pages of those cells.
6. **Images.**
   - After each segment, the restart's own stores are undone (its throwaway copy) and the decoded updates applied.
   - So before segment *k* the RAM is the initial image plus the updates of segments 0 to *k* − 1. It must equal the
     reference image on every cell stored so far, and at the end (the final image).
   - Every other cell is unchanged by construction: the update cells are checked equal to the stored cells.
7. **Padding segments:** pc 0, `instr_idx` 0, offset 0, the final registers, no advice, frame 4 with no words, page count 0.

**`segment_file.py <file> [--check <check json>]`** is an independent Python reader (framing, LRU decode, runs). It
prints per-segment SHA-256s of the state, advice and decoded update words, which must equal the C++ checker's.

## Results

| run | segments (real, starting mid-sequence) | result | Python hashes |
|---|---|---|---|
| EVM Zone `4e3451c1d8c5` trace-29, 128 × 2²² | 128 (85, 5) | **all checks pass**: 354,861,942 rows restarted, 3.6M image-cell checks | equal |
| Cyclone `input-15961266` input/29, 128 × 2²² | 128 (89, 29) | **all checks pass**: 371,526,406 rows restarted, 81.6M image-cell checks | equal |
| EVM Zone trace-23, auto hard cuts (auto, and `--force=lru`, and `--force=raw`) | 93 (91, 56; **19 start mid-secp with 1-8 pending words at the head**; 2 padding) | all pass | equal |
| Cyclone input/22, auto hard cuts | 111 (109, 59; 2 padding) | all pass | equal |

- **Frame 1 equals WORKER17's `pages_written` on all 128 segments of both workloads** (`WORKER17/data/mem_probe-*.segments.csv`:
  908 and 2,225 pages), and its row counts per segment agree too.
- **Negative tests** (EVM Zone trace-23), one bit flipped each time:

  | corrupted | checker reports |
  |---|---|
  | register 40 of a segment starting at a boundary | a row differs |
  | a live register 48+ of a mid-sequence start | a row differs |
  | an advice word | a row differs and the advice words differ |
  | an update value | updates differ, then the next segment's start image and rows |
  | a page count | the page count differs |

- **One bug found by the checks.** `--force=lru` wrote frame 5 for the padding segments, which the padding rule
  forbids. Fixed: an empty list is always frame 4.
- **Times.** The writer takes 10-11 s per 2²⁹ run and the checker 31-33 s.
- **Files.** The EVM Zone file is 1,747,828 bytes (SHA-256 `208520ec...`), the Cyclone file 10,334,580 bytes (`f45f71b6...`).
  They are not committed, since they rebuild in about 10 s. Statistics are in `data/w3/`.

## Frame sizes: raw against LRU-32

| | EVM Zone | Cyclone |
|---|---|---|
| update words (dirty cells, runs) | 323,176 (318,407, 4,769) | 2,208,186 (2,193,019, 15,167) |
| raw (frame 4 data) | 2,585,408 B | 17,665,488 B |
| LRU-32 (frame 5 data) | 1,580,008 B (**0.611**) | 9,423,504 B (**0.533**) |
| LRU smaller in | 85 of 85 real segments | 89 of 89 |
| per segment, LRU / raw | min 0.227, median 0.648, max 0.743 | min 0.124, median 0.488, max 0.969 |
| codes | hits 40.8%, 32-bit misses 7.6%, 64-bit misses 51.6% | hits 48.9%, 8.0%, 43.2% |
| largest segment, raw / LRU | 182,888 / 50,208 B (segment 0) | 4,255,864 / 2,338,448 B (segment 10) |
| advice (frame 3) | 3,782 words, 30 KB | 96,687 words, 774 KB |
| states (frame 2) | 128 × 1,056 B = 135 KB | 135 KB |
| **file** | **1.75 MB** | **10.33 MB** |

These are not comparable with PR #1's figures:
- PR #1 measured R-dirty with a different tracer revision, and with its own counting. Its R-dirty word counts (579,660
  and 2,570,687) differ from these dirty-cell totals (318,407 and 2,193,019).
- PR #1's LRU was 512 entries with a 9-bit index.

So: LRU-32 roughly halves the updates on both workloads, and never loses to raw on a real segment.

## Scoping: registers 40-47 in the fast version (not built)

**The setup.** The JIT tracks registers 0-47. For each instruction whose expansion writes 40-47, x86 code computes the
final values the expansion leaves there. From `expand_virt_seq` (`isa/virtual.cpp`):
- Temporaries are allocated upward from 40 (`:140-157`).
- **The rd = x0 remap shifts everything by one** (`:276-282`). A side-effecting instruction with rd = x0 gets rd = 40, its
  result lands in 40, and every temporary below moves up one register. The one AMOSWAP.W on Cyclone is such a case (it
  writes 40-43, not 40-42).
- Plain jumps are the same mechanism: JAL and JALR with rd = x0.
- Inline sequences use 48+ only; none wrote 40-47 on either workload.

**Notation.** EA = rs1 + imm, A = EA & ~7, sh = 8·(EA & 7), M[A] is the aligned doubleword, and W = 0xff, 0xffff or
0xffffffff for B, H or W. q is the division-family advice word. |x| is `VirtualNegateIf(x, x)`. All arithmetic is
mod 2⁶⁴.

| family (lines) | opcodes | writes | final values |
|---|---|---|---|
| narrow loads, `expand_load` (`:239-246`) | LB, LBU, LH, LHU, LW, LWU | 40, 41 | 40 = M[A]; 41 = W << sh |
| narrow stores, `expand_narrow_store` (`:249-262`) | SB, SH, SW | 40, 41 | 41 = (rs2 & W) << sh; 40 = (M[A] & ~(W << sh)) + 41 (the word stored) |
| jumps with rd = x0 | JAL, JALR (and EBREAK's JAL) | 40 | pc + pc_increment (the link) |
| register shifts (`:552-605`) | SLL; SLLW; SRL, SRA; SRLW, SRAW | 40 | 1 << (rs2 & 63); (u32)1 << (rs2 & 31); ~0 << (rs2 & 63); 2³² − 2^(rs2 & 31) |
| DIVU / REMU (`:328-357`) | DIVU; REMU | 40, 41; 40 | DIVU: 40 = q, 41 = rs1 − q·rs2. REMU: 40 = rs1 − q·rs2 |
| DIV / REM (`:285-327`) | DIV; REM | 40-45; 40-43 | DIV: 40 = q, 41 = \|rs1\|, 42 = \|rs2\|, 43 = rs1 ^ rs2, 44 = (43 < 0 ? −q : q), 45 = 41 − 44·42. REM: 40 = q (\|quotient\|), 41 = \|rs1\|, 42 = \|rs2\|, 43 = 41 − 40·42 |
| W division (`:358-447`) | DIVUW; REMUW; DIVW; REMW | 40-43; 40-42; 40-47; 40-45 | DIVUW: 40 = zext32(rs1), 41 = zext32(rs2), 42 = q, 43 = sext32(q). REMUW: 40, 41 as DIVUW, 42 = 40 − q·41. DIVW: 40 = sext32(rs1), 41 = sext32(rs2), 42 = q, 43 = \|40\|, 44 = \|41\|, 45 = 40 ^ 41, 46 = (45 < 0 ? −q : q), 47 = 43 − 46·44. REMW: 40, 41 as DIVW, 42 = q, 43 = \|40\|, 44 = \|41\|, 45 = 43 − 42·44 |
| MULHSU (`:453-473`) | MULHSU | 40-43 | s = (rs1 < 0 ? −1 : 0), a = (rs1 ^ s) + (s & 1), lo = a·rs2: 41 = s & 1, 42 = lo ^ s, 43 = mulhu(a, rs2) ^ s, 40 = ((42 + 41) <u 42) |
| LR (`:612-644`) | LR.W; LR.D | 40, 41; 40 | LR.W: 40 = M[A], 41 = 0xffffffff << sh (EA = rs1). LR.D: 40 = 0x80000000 |
| SC (`:645-712`) | SC.W; SC.D | 40-42; 40-42 | SC.W: 42 = 0xffffffff << sh (the load's mask), 41 = (v & 0xffffffff) << sh, 40 = (M[A] & ~42) + 41, where v = the word stored (rs2 if the reservation held, else the loaded word). SC.D: 40 = success (reservation_d == rs1), 41 = M[rs1], 42 = 41 + (rs2 − 41)·40 |
| AMO .D (`:713-...`) | AMOSWAP.D; ADD, AND, XOR, OR; MIN, MAX, MINU, MAXU | 40; 40, 41; 40-42 | SWAP: 40 = old. ADD..OR: 40 = op(old, rs2), 41 = old. MIN..MAXU: 40 = old, 41 = the value stored, 42 = (rs2 − old)·take |
| AMO .W (`:721-...`) | AMOSWAP.W; ADD, AND, XOR, OR; MIN, MAX, MINU, MAXU | 40-42; 40-43; 40-44 | 40 = the doubleword stored, 41 = (value & 0xffffffff) << sh, 42 = old (sext32 of the lane); ADD..OR: 43 = op(old, rs2); MIN..MAXU: 43 = the value stored, 44 = take |
| CSRRW / CSRRS with rd = rs1 (`:1085-1127`) | CSRRW, CSRRS | 40 | rs1 (its value before the instruction) |
| trap path (`:1068-1084`, `:1128-1132`) | ECALL, MRET | 40 | the JALR's link: pc + pc_increment |

**What this means for the fast version.**
- **Families.** 14 families, about 45 opcodes. The workloads exercise 25 opcodes in 8 of them (W2's table).
- **Inputs.** Every value comes from things the JIT already has: operands, the effective address, the aligned memory word
  (before or after the store), the advice word (division: the quotient the JIT computes natively), registers 32-33, and pc.
- **What to emit inline.** The four frequent families are about 99.5% of the writing instructions: narrow loads and
  stores, rd = x0 jumps, register shifts.
- **What to leave to a generic helper.** The rest (division, MULHSU, LR/SC, AMO, CSR, trap path) are rare, about 0.2M of
  30.3M on Cyclone. A helper running the instruction's own expansion in the interpreter is exact by construction.
- **Checking it.** This tool's checker would validate the fast version unchanged, since frame 2 carries 40-47.
