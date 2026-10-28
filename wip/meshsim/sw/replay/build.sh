#!/bin/sh
# Builds the replay controller (Stage B) into build/: replay_ctl.code.bin (instruction memory from 0x0000),
# replay_ctl.data.bin (data memory from 0x4000), replay_ctl.sym. Same toolchain and flags as meshsim/sw/build.sh.
set -e
cd $(dirname $0); mkdir -p build
CC="riscv64-unknown-elf-gcc -march=rv32i_zmmul -mabi=ilp32 -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wall -Wextra -Wno-array-bounds -Wl,--no-relax -I. -Os"
$CC -T replay.ld crt0.S replay_ctl.c -o build/replay_ctl.elf
if riscv64-unknown-elf-nm build/replay_ctl.elf | grep -qE ' (__udivsi3|__umodsi3|__divsi3|__modsi3)$'; then echo "ERROR: software division"; exit 1; fi
riscv64-unknown-elf-objcopy -O binary --only-section=.text build/replay_ctl.elf build/replay_ctl.code.bin
riscv64-unknown-elf-objcopy -O binary --only-section=.data build/replay_ctl.elf build/replay_ctl.data.bin
riscv64-unknown-elf-nm build/replay_ctl.elf > build/replay_ctl.sym
END=$(sed -n 's/^\([0-9a-f]*\) . _end$/\1/p' build/replay_ctl.sym)
echo "replay_ctl: code $(stat -c %s build/replay_ctl.code.bin) B, data $(stat -c %s build/replay_ctl.data.bin) B, data+bss end 0x$END (stack below 0x5BC0)"
