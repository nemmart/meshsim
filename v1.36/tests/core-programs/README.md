| `elf/*.elf` | the 48 programs, `coremark-bench.elf`, the benchmark run, and the four interrupt programs | built here by `fw/build.sh` from `fw/core/` (riscv-tests' and CoreMark's from their checkouts). `fw/build.sh --check` requires each equal to its build |# core-programs: the RTL's own core programs

*For anyone who needs to trust the core model's function and timing: what these fixtures are, where each came from,
and what the test requires.*

The core model (`src/units/rv32.h`) is the RTL's core, Snarky One's `rv32_core`, cycle for cycle. This folder holds
the RTL's own evidence for that: 48 of the 49 self-checking programs the RTL's `program_test` runs on the core
(riscv-tests' rv32ui, rv32um and rv32uzicond, the firmware test `isa_edges` and CoreMark; its 49th, the Rust test
program `firmware-rust`, is not carried: meshsim holds no image built outside it), CoreMark as the RTL's benchmark builds it, four
programs of ours for what those never reach (the interrupt programs, below), and what the RTL's simulation measured
on each. `tests/test_core_programs.cpp` runs every program on the model, on a bus that
is the RTL test's memory and devices with its ideal timing (`tests/core_programs.h`), and requires:

- each program's pass;
- the cycle of its result and the instructions retired by then, equal to the RTL's (`PROGRAMS`);
- every change of `wfi_sleeping` and of `core_status_canonical_wait` in the interrupt programs in the same cycle as the
  RTL's (`WAITS`);
- every instruction retired and every trap taken in the same cycle as the RTL's, with the same address, instruction
  word, cause and `mtval`: each program's whole trace, by its hash (`RETIRE.sha256`), and eight programs' line by line
  (`retire/`), so that a difference names its first cycle;
- CoreMark's own report on the benchmark run, equal to the RTL's baseline (`BENCH`);
- the RTL's boot ROM, on the core in the tile's map behind the register bus's timing (a window load answered two
  cycles after it is raised, a window store the next cycle, in the cycle it is made, `REQUEST_SEND`'s the cycle after
  it is made), in the RTL's cycles (`BOOT`): from the reset to its `LOAD` request and to an image, and from its two
  waits, woken by MEIP, to the `ARRIVE` and to the image.

## The files

| file | what | from |
|---|---|---|
| `elf/*.elf` | the 48 programs, `coremark-bench.elf`, the benchmark run, and the four interrupt programs | built here by `fw/build.sh` from `fw/core/` (riscv-tests' and CoreMark's from their checkouts). `fw/build.sh --check` requires each equal to its build |
| `SHA256SUMS` | the ELF files' hashes (`cd elf && sha256sum -c ../SHA256SUMS`) | this folder |
| `PROGRAMS` | per program: the cycle of its result and the instructions retired | the RTL's `program_test`, instrumented to print them |
| `RETIRE.sha256` | per program: the hash of its retire trace | the same run |
| `retire/*.rtl` | eight programs' retire traces in full | the same run |
| `WAITS` | the interrupt programs' waits: each cycle `wfi_sleeping` or `core_status_canonical_wait` changes in, with both | the RTL's `program_test`, instrumented to print them (step 5) |
| `BENCH` | CoreMark's report on the benchmark run | `rtl/blocks/core/baseline/bench.toml` |
| `fw/images/boot/boot_rom.elf` | the RTL's boot ROM, not in this folder | built by `fw/build.sh` from `fw/boot/boot_rom.S` |
| `BOOT` | the boot ROM's cycles on the RTL's block `core` | `rtl/blocks/core/README.md`, "The boot ROM's program" (`boot_rom_program_test`'s figures) |

A retire trace has one line per instruction retired, `ret: CYCLE PC INSN`, and one per trap taken, `trap: CYCLE CAUSE
PC MTVAL` (`CYCLE` decimal, the rest hexadecimal without leading zeros; `INSN` a 16-bit instruction's 16 bits), in
cycle order: the RTL's `rvfi_valid` records that are not `rvfi_trap`, and its `trap_valid` outputs.

## The interrupt programs

The RTL's programs take exceptions but no interrupt, and never wait: its test ties the core's interrupt inputs
low. `fw/core/irq_wfi.S`, `nmi_wfi.S`, `nmi_run.S` and `trap_nmi.S` cover that: `WFI` falling through and waiting (the
canonical wait among them), the machine external interrupt taken from a wait and in running code at every offset of
a mixed sequence, the interrupt a CSR write enables, the NMI from a wait and in running code, and the NMI that is
never taken after an exception; each header says what it checks, and each checks itself.

They need an interrupt source the program can time. The test memory's barrier line is the core's MEIP, and only the
test can raise it, so the test is given an **event trigger**: a store of `KIND << 8 | DELAY` to the side-effect write
device (0x2000_0014) raises an input `DELAY` cycles after the cycle the test sees that store counted in: `KIND` 0 the
barrier's release (`barrier_release` for that cycle, so MEIP is high from the next), `KIND` 1 the core's `nmi` input,
high from that cycle on. The RTL's `program_test` was given the trigger in the scratch worktree (and its tie of
`external_interrupt` to 0 removed, so that the barrier line reaches the core as the test memory drives it);
`tests/core_programs.h` has the same. The programs' expectations are the RTL's run of them, as for the 48.

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

4. The interrupt programs were built from their sources (now `fw/core/`) with the pinned compiler (`riscv-none-elf-gcc
   -march=rv32ic_zicond_zicsr_zmmul -mabi=ilp32 -nostdlib -nostartfiles -static -Wl,--build-id=none
   -Wl,--defsym=IMEM_BYTES=0x8000 -Wl,--defsym=DMEM_BYTES=0x8000 -T rtl/blocks/core/test/programs/firmware/link.ld`)
   and run by `program_test` as in step 2, with the event trigger.

5. `WAITS` (2026-11-15, Snarky One at `076e647`): `program_test`, changed as in steps 2 and 4 and to print, in every
   cycle `wfi_sleeping` or `core_status_canonical_wait` differs from the cycle before, the program, the loop's cycle
   index and both (`PROGRAM_TEST_WAITS`), ran the four interrupt programs in its ideal-timing run. `nmi_run` never
   waits. In all 31 waits the level rises the cycle after `wfi_sleeping`: at the tile's timing no fetch is outstanding
   when the wait begins.

**The images are rebuilt here; their references can be verified here but not regenerated:** that takes Snarky One, its
tools and the two changes above.


