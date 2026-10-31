#!/bin/sh
# Builds the sort program (worker 6; overlays and BT: worker 8, M7; v1.2: WORKER8) against a meshsim tree, which supplies
# mesh.h (generated, 1.2) and crt0.S. Sources: sw/ (the program), ../skew-planner (worker 5's planner as patched by
# worker 6, 8 and WORKER8), ../split-bisect/split.c (worker 4's split, ported by WORKER8, with the SPLIT_BARRIER patch).
#   build.sh [MESHSIM]       default: this tree (../../..)
# Everything at -O2. Outputs in build/: sort.code.bin (program memory, from 0x01000000), sort.data.bin (data memory,
# from _data_start = 0x10), sort.sym (nm -S), sort.dis, sort.map; and the memory map on stdout.
# Toolchain (CONVENTIONS 1): rv32i_zicsr_zmmul, gnu11.
set -e
HERE=$(cd $(dirname $0) && pwd)
M=$(cd ${1:-${MESHSIM:-$HERE/../../..}} && pwd)
SK=$(cd ${SKEW:-$HERE/../skew-planner} && pwd)
SP=$(cd ${SPLIT:-$HERE/../split-bisect} && pwd)
OUT=$HERE/build; mkdir -p $OUT; rm -f $OUT/*.o $OUT/*.bin
CC="riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 -std=gnu11 -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wall -Wno-array-bounds -Wl,--no-relax -I$HERE/sw -I$SK -I$M/sw"
OPT=${OPT:--O2}
# -fno-jump-tables: kept from v1.1 (where data memory was tight) so the command loop's code stays comparable
OPT="$OPT -fno-jump-tables $EXTRA"
OC=riscv64-unknown-elf-objcopy
OBJS=""
for f in $HERE/sw/*.c $SK/skew_common.c $SK/skew_th.c $SK/skew_bt.c; do
  o=$OUT/$(basename $f .c).o
  $CC $OPT -ffunction-sections -fdata-sections -c $f -o $o; OBJS="$OBJS $o"
done
# the split; its barriers go through wait_for_command() (sort_barrier, main.c)
$CC $OPT -ffunction-sections -fdata-sections -DSPLIT_BARRIER=sort_barrier -I$SP -c $SP/split.c -o $OUT/split_bisect.o
OBJS="$OBJS $OUT/split_bisect.o"
$CC -T $HERE/sw/sort.ld -Wl,--gc-sections -Wl,--orphan-handling=warn -Wl,--build-id=none -Wl,-Map=$OUT/sort.map $M/sw/crt0.S $OBJS -o $OUT/sort.elf
if riscv64-unknown-elf-nm $OUT/sort.elf | grep -qE ' (__udivsi3|__umodsi3|__divsi3|__modsi3|__mulsi3|__muldi3)$'; then echo "ERROR: library arithmetic"; exit 1; fi
$OC -O binary --only-section=.text $OUT/sort.elf $OUT/sort.code.bin
$OC -O binary --only-section=.data $OUT/sort.elf $OUT/sort.data.bin
riscv64-unknown-elf-nm -S $OUT/sort.elf > $OUT/sort.sym
riscv64-unknown-elf-objdump -d $OUT/sort.elf > $OUT/sort.dis
python3 - "$OUT" <<'PY'
import subprocess, sys
out = sys.argv[1]
secs = {}
for l in subprocess.run(["riscv64-unknown-elf-objdump", "-h", out + "/sort.elf"], capture_output=True, text=True).stdout.splitlines():
    f = l.split()
    if len(f) >= 6 and f[0].isdigit(): secs[f[1]] = (int(f[2], 16), int(f[3], 16))
sym = {}
for l in open(out + "/sort.sym"):
    f = l.split()
    if len(f) >= 3: sym[f[-1]] = int(f[0], 16)
t = secs[".text"][0]; d = secs[".data"][0]; be = sym["_end"]
print(f"sort image (v1.2): code {t} B of 0x8000; .rodata+.data {d} B; .bss end 0x{be:04x}; "
      f"{0x7bc0 - be} B to the stack top (0x7BC0)")
PY
