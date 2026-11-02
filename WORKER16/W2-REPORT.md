# WORKER16: W2, the advice streams (DECISIONS-v1.3 section 13)

Built on jolt-asic-planning branch **`worker16-w2`** (from `tracer-standalone-main`, commit `38722ce`). Paths are under
its `tracer/`. Part (1), how jolt-cpp gets registers 40-47 right at a chunk's start, is in `NOTES.md` (session 3).

## What was built

jolt-cpp's JIT emits the Tracer's advice streams:
- **One stream per slice**: 128 per 2²⁹ run, one per 2²² rows, by default.
- **Words in row order**: `shape(op).advice_words` per secp256k1 inline and 1 per division-family instruction.
- **Crossing sequences**: a sequence that crosses a slice boundary puts its remaining words at the head of the next stream.

The changes:
- **`src/tracer/advice_streams.hpp`** (new): the sink (`g_advice_sink`, thread-local, so `RiscV`'s layout is
  untouched), `division_advice_word`, and the `AdviceStreams` result.
- **`src/tracer/syscalls.cpp`**: the secp256k1 inline dispatch appends the first `shape(op).advice_words` words of
  `evaluate`'s result. A new `InlineID::DIV_ADVICE` appends a division word.
- **`src/tracer/transcode.{hpp,cpp}`**: `TranscodeParams::emit_advice`. Before each division-family instruction,
  `emit_division_advice` passes the source values to `DIV_ADVICE` through the existing inline-dispatch stub. Which advice
  opcode applies is taken from the instruction's own expansion (`division_advice_instruction`, `isa/virtual.cpp`, end),
  with a check that the advice row reads the instruction's own rs1 and rs2.
- **`src/tracer/trace_owner.cpp`** (end):
  - `division_advice_word` runs the advice row through jolt-cpp's interpreter (`execute_instruction<true>`), so the
    JIT's words have its exact semantics.
  - `CompressedTraceGen::gen_advice_streams` compiles a third code cache (checkpoints plus advice emission) on first use.
    It runs on the snapshot VM, which must not have run yet. Per slice, its steps are:
    1. Interpret the rest of a sequence begun in an earlier slice; those words are the head.
    2. Run the JIT to a checkpoint at or before the slice's end. It stops at least 4,096 rows short
       (`emit_checkpoint_check`), always between instructions.
    3. Interpret to exactly the slice's end.

    This is `compute_memory_snapshots`' JIT/interpreter handoff. The interpreter takes secp words from `prepare_advice`'s
    buffer as rows consume them, and division words from `division_advice_word`.
- **The default code caches and every existing output are unchanged.** `trace_elf --dump-trace-txt` on Cyclone input/22
  and EVM Zone trace-23 hashes identically before and after (`39ba08d6...`, `54f48fc4...`).
- **`advice/advice_streams.cpp`** (and `build.sh`): writes `advice_NNN.bin` (little-endian u64 words in row order) and
  `manifest.json` (per stream: first row, words, `head_pending_words`, SHA-256), and runs the check.

  `--extra=FIRST:LAST:STEP` adds boundaries for stress tests; `--slice-rows` and `--slices` change the grid.

## The check

The check is independent of the JIT:
1. Generate jolt-cpp's own trace (`gen_compressed_trace`, then `expand_trace`, on the other VMs).
2. Take every advice row's word (its `rd`; no advice row has rd = x0 on any run) and split them by slice.
3. Compare each stream word by word, and its head: the words from rows before the slice's first instruction start.

| run | slices | words (from the JIT) | heads | result | streams built in |
|---|---|---|---|---|---|
| EVM Zone `4e3451c1d8c5` trace-29 | 128 × 2²² | 3,782 (3,782) | 0 | **128/128 equal** | 0.7 s |
| Cyclone input/29 `input-15961266` | 128 × 2²² | 96,687 (96,576) | 0 | **128/128 equal** | 0.8 s |
| EVM Zone trace-29, stress: a boundary every 97 rows over its secp region (rows 2.0M-2.6M) | 6,314 | 3,782 (108) | **68, up to 8 words** | **6,314/6,314 equal, all heads equal** | 0.7 s |
| Cyclone input/29, 2¹⁶-row slices | 8,192 | 96,687 (90,898) | 0 | **8,192/8,192 equal** | 1.2 s |
| Cyclone input/22, 2¹⁶-row slices | 64 | 256 (254) | 0 | 64/64 equal | 0.5 s |
| EVM Zone trace-23, 2¹⁶-row slices | 90 | 3,713 (3,469) | 0 | 90/90 equal | 0.5 s |
| EVM Zone trace-23, stress as above | 6,276 | 3,713 (3) | 67, up to 8 words | 6,276/6,276 equal | 0.5 s |

- **The totals and the per-slice words** equal WORKER16's earlier counts (3,782 and 96,687; section 13's table).
- **The default 2²⁹ runs have no crossing sequence.** EVM Zone's secp work lies inside slice 0, so the JIT made every
  word.
- **The stress runs exercise the head rule.** Their words come mostly from the interpreter, since a slice shorter than
  about 5,000 rows leaves the JIT no room to run.
- **The default runs' manifests** are `data/w2/*-manifest.json`; the stress runs are summarized in
  `data/w2/stress-checks.json`. The 2²⁹ stream files (412 KB and 936 KB) are not committed. They rebuild in under a
  second; their SHA-256s are in the manifests.

## Scoping: registers 40-47 from the same pass (not built)

**Which instructions' expansions write 40-47.** Measured over every row of both 2²⁹ runs (the manifests'
`registers_40_47.by_source`), these fall into **8 families, 25 source opcodes**. Each family's final values, read from
`isa/virtual.cpp`, are:

| family | opcodes seen | writes | final values | EVM Zone | Cyclone |
|---|---|---|---|---|---|
| narrow loads (`expand_load`, `:239-246`) | LB, LBU, LH, LHU, LW, LWU | 40, 41 | 40 = the aligned doubleword loaded, 41 = the window mask | 1.93M | 17.86M |
| narrow stores (`expand_narrow_store`, `:249-262`) | SB, SH, SW | 40, 41 | 40 = the merged doubleword stored, 41 = the shifted data | 0.84M | 8.72M |
| jumps with rd = x0 (rd remapped to a temporary, `:276-282`) | JAL, JALR | 40 | the link value | 1.09M | 2.98M |
| register shifts (`:552-...`) | SLL, SRL, SLLW, SRLW | 40 | `VirtualPow2`/bitmask of rs2 | 0.16M | 0.61M |
| division family | DIVU, REMU, DIVW | 40-41; DIVW 40-47 | quotient/remainder temporaries | 108 | 96,687 |
| LR/SC | LR.W, SC.W | 40-41; SC.W 40-42 | RAM bound, loaded word, success flag, ... | 0 | 54,460 |
| AMO | AMOSWAP.W | 40-43 | its temporaries | 0 | 1 |
| trap path | ECALL, MRET, CSRRW (rd = rs1) | 40 | ECALL's and MRET's link temporary, CSRRW's spilled rs1 | 0 | 2,625 |
| **all** | | | | **4.03M instructions** | **30.32M instructions** |

That is 1.1% of EVM Zone's rows and 8.2% of Cyclone's.

**The interpreter tail alone does not cover them.**
- **How long the tail is:** at least 3,613 rows on EVM Zone and 3,642 on Cyclone (the JIT stops at least 4,096 rows
  short).
- **(boundary, register) pairs:** of the 127 boundaries × 8 registers, the last write before the boundary falls inside
  the tail for:
  - EVM Zone: 461, with 51 outside and 504 never written;
  - Cyclone: 485, with 455 outside and 76 never written.
- **How far back the misses go:** registers 40 and 41 at up to 68K rows (EVM Zone) and 1.36M rows (Cyclone). Registers
  42-47 at up to **284.5M rows**: on Cyclone, 43-47 are written only by DIVW (3 executions in the run) and AMOSWAP.W (1).
- **So the JIT itself would have to keep 40-47 current.**

**What helpers would cost.**
- **The four frequent families** (narrow loads and stores, rd = x0 jumps, register shifts) are 99.5% of the writing instructions on Cyclone and 99.997% on EVM Zone,
  and their final values are values the JIT already computes or has at hand:
  - the aligned word and the address-derived mask or shift;
  - the link value, a constant per instruction;
  - a power of two of rs2.

  Emitting them inline is 1-2 stores into a spill area for jumps and shifts, and about 5-8 x86 instructions for narrow
  loads and stores. At 30.3M instructions, that is roughly 0.1 s on a 2²⁹ run at a few ns each.
- **The four rare families** (division, LR/SC, AMO, trap path; about 0.2M on Cyclone) can use one generic C helper:
  run the instruction's own expansion through the interpreter (as `division_advice_word` does for one row) with
  registers 0-39 and RAM. That is exact by construction, at well under 0.1 s.
- **The handoff** would also copy 40-47 between the JIT and the interpreter.
- **Validation:** check each slice's 8 values against the reference rows' register history (as `state_probe` and
  `delivery_writer` track it).

**Proposal, if the owner wants it: a manifest field `start_regs_40_47`, 8 u64 per slice**: the values at the slice's
first row. It is cheap at run time; the work is writing and checking about 4 inline emitters and one generic helper.

**Caveats:**
- **Mid-sequence starts.** A slice that starts mid-sequence also needs registers 48-127 at its first row (or a rewind
  to the sequence's start). The interpreter has these at the boundary, since it is the one crossing it.
- **Robustness of the 4 inline emitters.** They must reproduce `expand_virt_seq`'s temporaries exactly, so a
  change to an expansion would need a matching change in them. The generic helper for everything avoids that, at
  roughly 1-2 s per 2²⁹ run at an estimated 50 ns a call.
