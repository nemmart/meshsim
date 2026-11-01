#!/bin/sh
# Builds worker 4's split (split.c, split.h, split_regs.h: his files, unchanged) with splitdrv.c for meshsim v4.0,
# into sw/build/split.{code,data}.bin and split.sym, with SPLIT_TRACE on (the simulator maps his trace register,
# 0xFFFFFFF0, when Controller.traceReg is set, as test/SplitRun does). Run from anywhere.
set -e
cd "$(dirname "$0")/../.."
mkdir -p build
CC="riscv64-unknown-elf-gcc -march=rv32i_zmmul -mabi=ilp32 -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wl,--no-relax -I. -msmall-data-limit=0"
$CC -O2 -DSPLIT_TRACE -DSPLIT_BARRIER=split_barrier -T app.ld crt0.S ext/split-bisect/splitdrv.c ext/split-bisect/split.c -o build/split.elf
riscv64-unknown-elf-objcopy -O binary --only-section=.text build/split.elf build/split.code.bin
riscv64-unknown-elf-objcopy -O binary --only-section=.data build/split.elf build/split.data.bin
riscv64-unknown-elf-nm build/split.elf > build/split.sym
riscv64-unknown-elf-nm -S build/split.elf | grep step_loop
