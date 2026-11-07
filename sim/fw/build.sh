#!/bin/sh
# Builds the M1 programs (sim/fw/*.S) into OUT (default sim/tests/golden/images): <n>.code.bin (program memory from
# 0x01000000), <n>.data.bin (data memory from 0x10), <n>.sym. Against sim/ref's generated mesh.h and its app.ld.
# Toolchain: riscv64-unknown-elf-gcc 13.2 (Ubuntu 24.04's gcc-riscv64-unknown-elf 13.2.0-11ubuntu1+12).
set -e
HERE=$(cd $(dirname $0) && pwd); SW=$HERE/../ref/meshsim/sw
OUT=${1:-$HERE/../tests/golden/images}; mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
T=$(mktemp -d); trap 'rm -rf $T' EXIT
CC="riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 -nostdlib -nostartfiles -static -Wl,--no-relax -I$SW"
for src in $HERE/*.S; do
  n=$(basename $src .S)
  $CC -T $SW/app.ld $src -o $T/$n.elf
  riscv64-unknown-elf-objcopy -O binary --only-section=.text $T/$n.elf $OUT/$n.code.bin
  riscv64-unknown-elf-objcopy -O binary --only-section=.data $T/$n.elf $OUT/$n.data.bin
  riscv64-unknown-elf-nm $T/$n.elf > $OUT/$n.sym
  echo "$n: code $(stat -c %s $OUT/$n.code.bin) B, data $(stat -c %s $OUT/$n.data.bin) B"
done
