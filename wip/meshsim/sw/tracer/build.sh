#!/bin/sh
# Builds C&C 3's Tracer firmware (B1, WORKER22) into build/: tracer_ctl.code.bin (program memory from 0x01000000),
# tracer_ctl.data.bin (data memory from 0x10), tracer_ctl.sym. replay/build.sh's flags, start-up and layout.
set -e
cd $(dirname $0); mkdir -p build
CC="riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 -std=gnu11 -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wall -Wextra -Wno-array-bounds -Wl,--no-relax -I. -I.. -Os"
$CC -T ../app.ld ../crt0.S tracer_ctl.c -o build/tracer_ctl.elf
if riscv64-unknown-elf-nm build/tracer_ctl.elf | grep -qE ' (__udivsi3|__umodsi3|__divsi3|__modsi3)$'; then echo "ERROR: software division"; exit 1; fi
riscv64-unknown-elf-objcopy -O binary --only-section=.text build/tracer_ctl.elf build/tracer_ctl.code.bin
riscv64-unknown-elf-objcopy -O binary --only-section=.data build/tracer_ctl.elf build/tracer_ctl.data.bin
riscv64-unknown-elf-nm build/tracer_ctl.elf > build/tracer_ctl.sym
END=$(sed -n 's/^\([0-9a-f]*\) . _end$/\1/p' build/tracer_ctl.sym)
echo "tracer_ctl: code $(stat -c %s build/tracer_ctl.code.bin) B, data $(stat -c %s build/tracer_ctl.data.bin) B, data+bss end 0x$END (stack below 0x7BC0)"
