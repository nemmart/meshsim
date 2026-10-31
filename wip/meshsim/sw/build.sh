#!/bin/sh
# Builds the boot ROM and every C program in sw/tests into sw/build (meshsim v1.2).
#   bootrom.hex                           the boot ROM (6.2), committed: meshsim loads it into every controller
#   build/<name>.code.bin, <name>.data.bin   the image: program memory from 0x01000000, data memory from 0x00000010
# Left out: the v1.1 recipes (tests/rcp_*.c; phase 6 builds the v1.2 recipes in wip/sw/recipes/ instead).
# Toolchain: riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 (no divider: a program that pulls in a
# software divide fails the build unless ALLOW_SOFTDIV=1). A line "// build: <flags>" in a source replaces the default -Os.
set -e
HERE=$(cd $(dirname $0) && pwd); cd $HERE
OUT=build; mkdir -p $OUT
CC="riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 -std=gnu11 -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wall -Wno-array-bounds -Wl,--no-relax -I."
OBJCOPY=riscv64-unknown-elf-objcopy
NM=riscv64-unknown-elf-nm

# mesh.h is generated from the Java register table
if [ -d ../src ] && command -v javac >/dev/null 2>&1; then
  mkdir -p $OUT/cls && javac -d $OUT/cls ../src/*.java && java -cp $OUT/cls Registers mesh.h >/dev/null
fi

# the boot ROM (6.2): its image is committed as bootrom.hex, which meshsim loads
$CC -T bootrom.ld bootrom.S -o $OUT/bootrom.elf
$OBJCOPY -O binary $OUT/bootrom.elf $OUT/bootrom.bin
od -An -v -tx4 -w4 $OUT/bootrom.bin | sed 's/^ *//' > bootrom.hex
echo "boot ROM: $(wc -l < bootrom.hex) words"


FAILED=""
for src in tests/*.c; do
  n=$(basename $src .c)
  case $n in rcp_*) continue;; esac
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
