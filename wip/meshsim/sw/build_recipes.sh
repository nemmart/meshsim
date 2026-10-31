#!/bin/sh
# Builds the v1.2 recipe programs (wip/sw/recipes/rcp_*.c, unedited) into sw/build/<name>.{code,data}.bin and .sym:
# against the generated mesh.h (-include: the draft header's guard keeps it out), with the recipes' own crt0.S (recipe
# 22) and handler.S (recipe 24), meshsim's recipes.ld, and recipes_support.c (what a recipe leaves to its program).
set -e
HERE=$(cd $(dirname $0) && pwd); cd $HERE
R=../../sw/recipes; OUT=build; mkdir -p $OUT
CC="riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 -std=gnu11 -O2 -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wl,--no-relax -include $HERE/mesh.h"
FAILED=""
for src in $R/rcp_*.c; do
  n=$(basename $src .c)
  if $CC -T recipes.ld $R/crt0.S $R/handler.S $src recipes_support.c -o $OUT/$n.elf 2> $OUT/$n.err; then
    riscv64-unknown-elf-objcopy -O binary --only-section=.text $OUT/$n.elf $OUT/$n.code.bin
    riscv64-unknown-elf-objcopy -O binary --only-section=.data $OUT/$n.elf $OUT/$n.data.bin
    riscv64-unknown-elf-nm $OUT/$n.elf > $OUT/$n.sym
  else FAILED="$FAILED $n"; cat $OUT/$n.err; fi
done
echo "recipes: $(ls $OUT/rcp_*.code.bin | wc -l) built${FAILED:+; FAILED:$FAILED}"
[ -z "$FAILED" ]
