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
