#!/bin/sh
# Builds the controllers' firmware (WORKER14, Config.ccCores) into sw/build/<name>.{code,data}.bin and .sym: cc1 (C&C 1)
# and cc23 (C&C 2 and C&C 3), against the generated mesh.h (-include), with the recipes' crt0.S (recipe 22) and
# handler.S (recipe 24) and meshsim's recipes.ld (code from 0x01000000, data from 0x10).
set -e
HERE=$(cd $(dirname $0) && pwd); cd $HERE
R=../../sw/recipes; OUT=build; mkdir -p $OUT
CC="riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wl,--no-relax -include $HERE/mesh.h"
for n in cc1 cc23; do
  $CC -T recipes.ld $R/crt0.S $R/handler.S cc/$n.c -o $OUT/$n.elf
  riscv64-unknown-elf-objcopy -O binary --only-section=.text $OUT/$n.elf $OUT/$n.code.bin
  riscv64-unknown-elf-objcopy -O binary --only-section=.data $OUT/$n.elf $OUT/$n.data.bin
  riscv64-unknown-elf-nm $OUT/$n.elf > $OUT/$n.sym
  end=$(awk '$3 == "_end" { print $1 }' $OUT/$n.sym)
  [ $((0x$end)) -le $((0x1000)) ] || { echo "$n: data and .bss end at 0x$end, above 0x1000 (cc1's report buffer)"; exit 1; }
  echo "$n: code $(stat -c %s $OUT/$n.code.bin) B, data $(stat -c %s $OUT/$n.data.bin) B"
done
