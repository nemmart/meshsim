#!/bin/sh
# Builds the resident loader and every C program in sw/tests into sw/build (meshsim v4.0).
#   build/loader.bin                      the loader, at 0x1F00
#   build/<name>.code.bin, <name>.data.bin   the image: instruction memory from 0x0000, data memory from 0x4000
# Toolchain: riscv64-unknown-elf-gcc -march=rv32i_zmmul -mabi=ilp32 (no divider: a program that pulls in a software
# divide fails the build unless ALLOW_SOFTDIV=1). A line "// build: <flags>" in a source replaces the default -Os.
set -e
HERE=$(cd $(dirname $0) && pwd); cd $HERE
OUT=build; mkdir -p $OUT
CC="riscv64-unknown-elf-gcc -march=rv32i_zmmul -mabi=ilp32 -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wall -Wno-array-bounds -Wl,--no-relax -I."
OBJCOPY=riscv64-unknown-elf-objcopy
NM=riscv64-unknown-elf-nm

# mesh.h is generated from the Java register table
if [ -d ../src ] && command -v javac >/dev/null 2>&1; then
  mkdir -p $OUT/cls && javac -d $OUT/cls ../src/*.java && java -cp $OUT/cls Registers mesh.h >/dev/null
fi

$CC -T loader.ld loader.S -o $OUT/loader.elf
$OBJCOPY -O binary $OUT/loader.elf $OUT/loader.bin
LW=$(( $(stat -c %s $OUT/loader.bin) / 4 ))
[ $LW -le 64 ] || { echo "loader too big: $LW words"; exit 1; }
echo "loader: $LW words"

FAILED=""
for src in tests/*.c; do
  n=$(basename $src .c)
  OPT=$(sed -n 's|^// build: ||p' $src | head -1); [ -n "$OPT" ] || OPT=-Os
  rm -f $OUT/$n.code.bin $OUT/$n.data.bin
  if ! $CC $OPT -T app.ld crt0.S $src -o $OUT/$n.elf; then FAILED="$FAILED $n"; continue; fi
  if [ "$ALLOW_SOFTDIV" != "1" ] && $NM $OUT/$n.elf | grep -qE ' (__udivsi3|__umodsi3|__divsi3|__modsi3)$'; then
    echo "ERROR: $n uses software division"; FAILED="$FAILED $n"; continue
  fi
  $OBJCOPY -O binary --only-section=.text $OUT/$n.elf $OUT/$n.code.bin
  $OBJCOPY -O binary --only-section=.data $OUT/$n.elf $OUT/$n.data.bin
  $NM $OUT/$n.elf > $OUT/$n.sym
  END=$($NM $OUT/$n.elf | sed -n 's/^\([0-9a-f]*\) . _end$/\1/p')
  echo "$n: code $(stat -c %s $OUT/$n.code.bin) B, data $(stat -c %s $OUT/$n.data.bin) B, data+bss end 0x$END"
done
[ -z "$FAILED" ] || { echo "FAILED:$FAILED"; exit 1; }
