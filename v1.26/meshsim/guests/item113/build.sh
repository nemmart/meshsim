#!/bin/sh
# Assembles the item 113 guests into build/<name>.elf (+ .dis), RV64IMAC, linked at 0x80000000.
set -e
cd $(dirname $0); mkdir -p build
for s in *.S; do
  n=$(basename $s .S)
  riscv64-unknown-elf-gcc -march=rv64imac -mabi=lp64 -nostdlib -nostartfiles -static -Wl,--no-relax -T guest.ld $s -o build/$n.elf
  riscv64-unknown-elf-objdump -d build/$n.elf > build/$n.dis
  echo "$n: $(riscv64-unknown-elf-size -A build/$n.elf | awk '/\.text/{print $2}') B of code"
done
