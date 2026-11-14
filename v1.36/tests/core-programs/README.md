# core-programs: the RTL's own core programs

*For anyone who needs to trust the core model's function and timing: what these fixtures are, where each came from,
and what the test requires.*

The core model (`src/units/rv32.h`) is the RTL's core, Snarky One's `rv32_core`, cycle for cycle. This folder holds
the RTL's own evidence for that: the 49 self-checking programs the RTL's `program_test` runs on the core (riscv-tests'
rv32ui, rv32um and rv32uzicond, two firmware tests and CoreMark), CoreMark as the RTL's benchmark builds it, and what
the RTL's simulation measured on each. `tests/test_core_programs.cpp` runs every program on the model, on a bus that
is the RTL test's memory and devices with its ideal timing (`tests/core_programs.h`), and requires:

- each program's pass;
- the cycle of its result and the instructions retired by then, equal to the RTL's (`PROGRAMS`);
- every instruction retired and every trap taken in the same cycle as the RTL's, with the same address, instruction
  word, cause and `mtval`: each program's whole trace, by its hash (`RETIRE.sha256`), and four programs' line by line
  (`retire/`), so that a difference names its first cycle;
- CoreMark's own report on the benchmark run, equal to the RTL's baseline (`BENCH`).

## The files

| file | what | from |
|---|---|---|
| `elf/*.elf` | the 49 programs, and `coremark-bench.elf`, the benchmark run | `mise run core-images` (`build/core-tests/elf/`); the benchmark's from `mise run bench-core`'s build |
| `SHA256SUMS` | the ELF files' hashes (`cd elf && sha256sum -c ../SHA256SUMS`) | this folder |
| `PROGRAMS` | per program: the cycle of its result and the instructions retired | the RTL's `program_test`, instrumented to print them |
| `RETIRE.sha256` | per program: the hash of its retire trace | the same run |
| `retire/*.rtl` | four programs' retire traces in full | the same run |
| `BENCH` | CoreMark's report on the benchmark run | `rtl/blocks/core/baseline/bench.toml` |

A retire trace has one line per instruction retired, `ret: CYCLE PC INSN`, and one per trap taken, `trap: CYCLE CAUSE
PC MTVAL` (`CYCLE` decimal, the rest hexadecimal without leading zeros; `INSN` a 16-bit instruction's 16 bits), in
cycle order: the RTL's `rvfi_valid` records that are not `rvfi_trap`, and its `trap_valid` outputs.

## How they were made

Snarky One at commit `370e3ac` (`tile/base spec: GROUP in two SRAMs again, as the RTL has it`), default
configuration (32 KiB IMEM and DMEM), with its pinned tools (xPack `riscv-none-elf-gcc` 15.2, Verilator):

1. `mise run core-images` built the programs; `build/core-tests/programs.txt` lists them in the order of `PROGRAMS`.
2. `program_test` (`rtl/blocks/core/test/src/program_test.rs`) was changed, in a scratch worktree only, to print in its
   ideal-timing run (`PROGRAM_TEST_MODE=bench`) one line per `rvfi_valid` record (the loop's cycle index,
   `rvfi_pc_rdata`, `rvfi_insn`, `rvfi_trap`), one per `trap_valid` (the index, `trap_cause`, `trap_pc`,
   `trap_value`), and at each program's result the index and the count of records without `rvfi_trap`; run with
   `cargo test --release -p blocks_core -- --exact program_test::program_test --nocapture`.
3. `mise run bench-core` built the benchmark run and ran it on the RTL (its report: 3602722 ticks, 2746880
   instructions, the baseline's); `xtask/src/coremark.rs` was changed, in the same scratch worktree, to keep the ELF
   file it builds in a temporary copy; `program_test` as in step 2 then ran that file too (`PROGRAM_TEST_LIST`), for
   its line in `PROGRAMS` and its hash.

**The fixtures can be verified here but not regenerated:** that takes Snarky One, its tools and the two changes above.
