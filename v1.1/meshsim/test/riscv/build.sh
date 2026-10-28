#!/bin/sh
# Builds the riscv-tests rv32ui / rv32um programs for meshsim v4.0: code at 0x0000 (instruction memory), data at
# 0x4000 (data memory), as two flat binaries per program. usage: build.sh <riscv-tests dir> <out dir>
# Excluded: ma_data (a misaligned access is a bus error here) and fence_i (it stores into its own code, impossible on
# the strict Harvard core).
set -e
RT=$1; OUT=$2; HERE=$(cd $(dirname $0) && pwd)
mkdir -p $OUT
for f in $RT/isa/rv32ui/*.S $RT/isa/rv32um/*.S; do
  n=$(basename $f .S)
  case $n in ma_data|fence_i) continue;; esac
  riscv64-unknown-elf-gcc -march=rv32im_zifencei -mabi=ilp32 -nostdlib -nostartfiles -static -Wl,--no-relax \
     -I$HERE -I$RT/isa/macros/scalar -I$RT/isa/rv32ui -I$RT/isa/rv32um -T $HERE/link.ld $f -o $OUT/$n.elf || { echo "FAILED $n"; continue; }
  riscv64-unknown-elf-objcopy -O binary --only-section=.text $OUT/$n.elf $OUT/$n.code.bin
  riscv64-unknown-elf-objcopy -O binary --only-section=.data $OUT/$n.elf $OUT/$n.data.bin
done
ls $OUT/*.code.bin | wc -l
