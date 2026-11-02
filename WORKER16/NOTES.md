# WORKER16 notes (T2: the Tracer's starting state)

Running notes; the report is `REPORT.md`. Newest at the bottom.

## 2026-11-01, session 1

**Go-ahead** (owner, via the user): as PROMPT.md, plus: choose restart points deliberately as well as at random:
inside virtual and inline sequences (mid-expansion), just before and after Cyclone's traps, between an LR and its SC,
around syscalls and advice reads. Confirm the instrumented build's rows match the uninstrumented ones (a hash per
window) before trusting its numbers.

**Sources.** jolt-asic-planning `tracer-standalone-main` at `6c3aae0` (jolt-cpp `821e1bd`). Tool branch:
`worker16-state` (from `tracer-standalone-main`).

**Reading so far** (paths under `tracer/src/tracer/`):
- `vm.hpp:27` the JIT's `risc_regs[isa::NUM_REGS]`: x0-x31, `reservation_w`, `reservation_d`, mtvec, mscratch, mepc,
  mcause, mtval, mstatus, pc (`isa/riscv.hpp:26-68`) = registers 0-39 plus pc. Other `RiscV` fields (`host_regs`,
  `instr_count`, `next_checkpoint_at`, `exit_code`, `state`, `stubs`, `mem_log_idx`) are host bookkeeping.
- `isa/virtual.hpp:25-53`: 128 registers; 32-39 reserved (reservations + 6 CSRs), 40-47 allocatable *stateful*
  (temporaries of ordinary expansions, "previous values we need to track"), 48-127 inline temporaries, "zeroed at the
  end of each sequence that uses them".
- `trace_owner.cpp:24-102` `InterpreterState`: `regs[128]`, `dirty_inline_vregs_end`, `instr_idx`, `risc_pc`, `cycle`,
  `ram`/`mem_log`/`mem_log_idx`, **`secp_advice[8]` and `next_secp_word`** (filled by `prepare_advice` at the start of
  a secp256k1 inline sequence from RAM via two registers, consumed by `VirtualAdvice` rows: hidden state mid-sequence).
- `trace_owner.hpp:393-399` jolt-cpp's own restart record, `CompressedChunkHeader`: regs 0-47, pc, instr_idx,
  `skip_cycles`, `mem_log_start`. `trace_owner.cpp:704-731`: a chunk that would start mid-sequence with a non-zero
  inline register (48+) is **rewound to the sequence start** (`skip_cycles`), i.e. jolt-cpp itself avoids restarting
  mid-sequence with only 0-47.
- `trace_owner.cpp:1218-1240`: the JIT (40 tracked registers) can only resume at an instruction boundary; the
  snapshot code runs the interpreter to the end of a sequence first.
- `compressed_trace_gen.hpp:51-58` `MemorySnapshot`: cycle, risc_pc, regs[128], RAM: jolt-cpp's own "full state".
- Syscalls (`syscalls.cpp:57-94`): advice-write, print, cycle-track: no architectural effect. Inlines run on the
  host in the JIT (`syscalls.cpp:122-248`) and as virtual sequences in the interpreter.
- ECALL (`isa/virtual.cpp:1069-1084`): writes mepc, mcause, mtval, mstatus and jumps through mtvec (reg 34); MRET jumps
  through mepc (36); CSRRW/CSRRS on regs 34-39. LR/SC (`isa/virtual.cpp:610-700`) use regs 32/33; SC.W spills its
  success flag into reg 32 mid-sequence.

**Plan for the tool** (`tracer/state/` on `worker16-state`, includes `trace_owner.cpp` as `delivery_writer` does, so
jolt-cpp's own interpreter runs unchanged):
1. Reference: jolt-cpp's pipeline (`gen_compressed_trace` + `expand_trace`, per window of 2^16 rows), a hash per window.
2. Pass A: one RAM-driven interpreter run (the Tracer's model), rows hashed per window and compared with (1); counters
   for every candidate piece of state; reservoir-sampled restart points per category.
3. Pass B: the same run, `fork()` at each chosen point; the child rebuilds an interpreter from pc, instr_idx,
   regs 0-127 and RAM only, runs R rows, and its rows are compared row by row with the parent's. Control variants
   (zeroing 32-39, or 40-47) to show the check detects missing state.

**Tool built and first results** (`state_probe`, jolt-asic-planning `worker16-state` at `b277125`, `tracer/state/`;
local 1 CPU, 3 GB, `vm.overcommit_memory=1`, which resets between tool calls here, so every run sets it):
- Pass A rows = jolt-cpp's rows on every window: Cyclone input/22 42/42, EVM Zone trace-23 90/90.
- Restart (k = 16 + first 4 per category, R = 65,536 rows): **T2 matched at every point tested except mid-secp256k1
  sequences with advice pending** (EVM Zone trace-23: 0/20 mid-sequence with advice pending, 0/20 at an advice row, 1/20
  just after one; all match with `secp_advice` + `next_secp_word` added). Cyclone input/22 has no secp inline.
- Controls: zeroing 32-39 fails at traps, MRET, CSRs, between LR and SC; zeroing 40-47 fails almost everywhere (the next
  write's `rd_prev`); zeroing 48-127 fails only mid-inline. 48-127 were zero at every instruction start (0 exceptions).
- Counts: Cyclone input/22 51 ECALL, 51 MRET, 205 CSRRW, 306 CSRRS, 125 LR.W / SC.W, 1 AMO, 30 VirtualHostIO;
  EVM Zone trace-23 668 secp sequences, 3,710 `VirtualAdvice` rows, 3 division advice rows.

## 2026-11-01, session 2

**Context** (owner): DECISIONS-v1.3 section 13: the host supplies all advice in the read log, 128 streams per 2^29 run
(one per 2^22 rows), built by jolt-cpp's JIT; the secp buffer becomes "pending words at the head of the next slice's
stream". To measure on the 2^29 runs: (1) secp sequences: count, rows inside, longest; (2) 2^22 slice boundaries inside a
secp sequence with advice pending, and the pending words each carries; (3) advice words per slice (min, mean, max).
Report only. W2 (the JIT emitting the streams) may come next.

**2^29 runs done locally** (no Quest number needed; detached with `setsid`, since a plain background job dies with the
tool call): EVM Zone 3 min, Cyclone 4 min, peak about 1.6 GB per process. Tool at `ae1c262`. Rows equal jolt-cpp's on
every window (5,415 and 5,670); counts equal WORKER9's and WORKER12's. T2: Cyclone 1,096/1,096; EVM Zone 515/673, every
failure mid-secp with advice pending; T2 + buffer 673/673. Section 13: no 2^22 boundary falls inside a secp sequence
on either run; words per slice EVM Zone 1 / 44.5 / 3,676 (total 3,782), Cyclone 0 / 1,086 / 3,094 (total 96,687).
secp words per operation are `shape().advice_words` (4, 6 or 8), not always 8. Written up in `REPORT.md`.

## 2026-11-01, session 3: W2

**Asked** (owner, via the user): W2 in two parts on my own branch of jolt-asic-planning. (1) First, report how jolt-cpp
gets registers 40-47 right at a chunk's start, before building. (2) Then the advice streams: 128 per 2^29 run, one per 2^22
rows, words in row order (`shape(op).advice_words` per secp op, 1 per division-family instruction), a crossing
sequence's pending words at the head of the next stream; check each stream against the words the rows consume.
Plan first.

**(1) Registers 40-47** (read; jolt-cpp `821e1bd`, `trace_owner.cpp` identical in jolt-cpp and PR #2):
- Values come from the CPU compressed-trace generator's interpreter executing the expansions. Each thread starts from a JIT
  checkpoint that carries only registers 0-39 (`trace_owner.cpp:1039-1046`; `num_jit_tracked_regs`, `isa/virtual.hpp:47`),
  so 40-47 start at 0 in every thread.
- Per thread, `VregInfo` (`trace_owner.cpp:662-676`) records each register's first write (`:802-808`) and its values at the
  thread's end (`:848`). After all threads join, a sequential pass (`:1093-1135`) carries the values forward: every
  header before a thread's first write to a register (all of them if it never writes it) gets the previous threads' last
  value. Correct because 40-47 are never read before written within an instruction (WORKER16: 0 carried reads at 2^29)
  and each thread runs to exactly the row where the next starts.
- The compressed trace carries them as full 64-bit values in `CompressedChunkHeader::regs[0..47]`, one header per 512-row
  chunk (`trace_owner.hpp:393-399`). 48-127 are not carried: zero at a chunk's start, and a chunk that would start
  mid-sequence with a non-zero 48+ is rewound to the sequence's start (`skip_cycles`, `:700-732`).
- Both expanders only consume the header: CPU `expand_compressed_chunk` (`:857-863`), GPU `expand_kernel`
  (jolt-cpp `src/tracer/gpu/expand_trace.cu:453-460`, not in PR #2), 0-47 from the header, 48-127 zeroed. Snapshots replay
  from the headers too (`:1336-1366`). The "handling" is the CPU post-pass.
- A host-built slice start from the JIT has pc and registers 0-39 only. It needs 40-47 at the slice's first row: the last
  value written before it, possibly many slices back (Cyclone: registers 43-47 carried into a write only 2-4 times in the
  whole run). In jolt-cpp that value comes only from interpretation, as the patch chain or a replay from an earlier header.
  Mid-sequence starts also need 48-127 (or a rewind) and the pending advice.
- Division family: one advice row per instruction, on the source rs1/rs2: DIV→`VirtualAdviceDiv`, REM→`Rem`, DIVU and
  REMU→`DivU`, DIVW→`DivW`, REMW→`RemW`, DIVUW and REMUW→`DivUW` (`isa/virtual.cpp:285-436`).

**(2) Built and checked** (go-ahead, owner via the user, with the output format; plus: scope registers 40-47 from the same
pass, not built): branch `worker16-w2` at `38722ce`. Streams equal jolt-cpp's rows' advice words on every slice of both
2^29 runs (128/128 each), a 6,314-slice stress over EVM Zone's secp region (68 heads, up to 8 words) and an 8,192-slice
Cyclone run; default outputs unchanged. Registers 40-47: 8 families, 25 opcodes write them (4.03M / 30.32M instructions);
the interpreter tail misses 51 / 455 (boundary, register) pairs (up to 284.5M rows back), so the JIT would need helpers;
cheap; proposed as `start_regs_40_47` in the manifest. See `W2-REPORT.md`.
