#!/bin/sh
# Build the host harness and the RV32 planner image (cycle counts).
#   ./build.sh            then   ./harness [options]   (see harness.c)
set -e
cd "$(dirname "$0")"
P=../planner
# the planner writes its tables into the shadow HW (skew.h; the tile build writes the table window instead)
SHADOW=-DSKEW_HW_SHADOW
riscv64-unknown-elf-gcc -O2 -std=c99 $SHADOW $HOSTFLAGS -Wall -Wextra -march=rv32i_zmmul -mabi=ilp32 \
  -ffreestanding -nostdlib -fno-builtin -fno-tree-loop-distribute-patterns -msmall-data-limit=0 \
  -T link.ld rv_entry.c $P/skew_common.c $P/skew_th.c $P/skew_bt.c -o planner_rv.elf
riscv64-unknown-elf-objcopy -O binary planner_rv.elf planner_rv.bin
riscv64-unknown-elf-nm -S planner_rv.elf | awk '{print $1, ($4 == "" ? "0" : $2), ($4 == "" ? $3 : $4)}' > planner_rv.syms
riscv64-unknown-elf-objdump -d planner_rv.elf > planner_rv.dis
# no library calls, no divides in the image
if grep -E 'call|jal.*<(__|mem)' planner_rv.dis | grep -E '<(__|mem)' ; then echo "library call in RV32 image" >&2; exit 1; fi
if grep -E '\s(div|divu|rem|remu)\s' planner_rv.dis ; then echo "divide in RV32 image" >&2; exit 1; fi
echo "RV32 image: $(stat -c %s planner_rv.bin) bytes; text $(riscv64-unknown-elf-size planner_rv.elf | awk 'NR==2{print $1}') bytes"
gcc -O2 -std=c99 $SHADOW $HOSTFLAGS -Wall -Wextra -o harness harness.c rv32emu.c $P/skew_common.c $P/skew_th.c $P/skew_bt.c
echo "harness built"
