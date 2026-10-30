#!/bin/sh
# Compile every recipe program against the draft header (no link: there is no v1.2 linker script or simulator yet).
cd "$(dirname "$0")" && mkdir -p out && ok=1
for f in *.c *.S; do
    [ -e "$f" ] || continue
    riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -c "$f" -o "out/${f%.*}.o" || ok=0
done
[ $ok = 1 ] && echo "all compiled" || { echo "FAILED"; exit 1; }
