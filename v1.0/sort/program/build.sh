#!/bin/sh
# Builds the sort program (worker 6; overlays and BT: worker 8, M7) against this meshsim tree, which supplies mesh.h,
# crt0.S and the loader's linker script. Sources: sw/ (the program), the planner and the split as patched for the sort
# (patches/: meshsim/sw/ext/skew-planner and meshsim/sw/ext/split-bisect).
#   build.sh [MESHSIM]       default: ../../meshsim (the v1.0 layout)
# Everything at -O2 (lead, 3 Oct: the kernel and every image). Outputs in build/:
#   loader.bin; sort.code.bin (the kernel, from 0); sort.ovinit.bin (the LOCAL image, at _ov_base: loaded at boot);
#   sort.stage.bin (every image in load order: C&C stages it at stage_base()); sort.data.bin (from 0x4000);
#   sort.sym, sort.dis, sort.map; and the memory map on stdout.
set -e
HERE=$(cd $(dirname $0) && pwd)
M=$(cd ${1:-${MESHSIM:-$HERE/../../meshsim}} && pwd)
SK=$(cd ${SKEW:-$M/sw/ext/skew-planner} && pwd)
SP=$(cd ${SPLIT:-$M/sw/ext/split-bisect} && pwd)
OUT=$HERE/build; mkdir -p $OUT; rm -f $OUT/*.o
CC="riscv64-unknown-elf-gcc -march=rv32i_zmmul -mabi=ilp32 -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wall -Wno-array-bounds -Wl,--no-relax -I$HERE/sw -I$SK -I$M/sw"
OPT=${OPT:--O2}
# -fno-jump-tables: the command switch's jump table would be 448 B of resident .rodata (data memory is the tight one)
OPT="$OPT -fno-jump-tables $EXTRA"
OC=riscv64-unknown-elf-objcopy
$CC -T $M/sw/loader.ld $HERE/sw/loader.S -o $OUT/loader.elf
$OC -O binary $OUT/loader.elf $OUT/loader.bin
LW=$(( $(stat -c %s $OUT/loader.bin) / 4 )); [ $LW -le 64 ] || { echo "loader too big: $LW words"; exit 1; }
OBJS=""
for f in $HERE/sw/*.c $SK/skew_common.c $SK/skew_th.c $SK/skew_bt.c; do
  o=$OUT/$(basename $f .c).o
  $CC $OPT -ffunction-sections -fdata-sections -c $f -o $o; OBJS="$OBJS $o"
done
# the split: an object named split_*.o (its .bss goes to the shared region); its barriers go through wait_for_command()
$CC $OPT -ffunction-sections -fdata-sections -DSPLIT_BARRIER=sort_barrier -I$SP -c $SP/split.c -o $OUT/split_bisect.o
OBJS="$OBJS $OUT/split_bisect.o"
$CC -T $HERE/sw/sort.ld -Wl,--gc-sections -Wl,--orphan-handling=warn -Wl,--build-id=none -Wl,-Map=$OUT/sort.map $M/sw/crt0.S $OBJS -o $OUT/sort.elf
if riscv64-unknown-elf-nm $OUT/sort.elf | grep -qE ' (__udivsi3|__umodsi3|__divsi3|__modsi3|__mulsi3|__muldi3)$'; then echo "ERROR: library arithmetic"; exit 1; fi
$OC -O binary --only-section=.text $OUT/sort.elf $OUT/sort.code.bin
$OC -O binary --only-section=.data $OUT/sort.elf $OUT/sort.data.bin
$OC -O binary --only-section=.ov_local $OUT/sort.elf $OUT/sort.ovinit.bin
: > $OUT/sort.stage.bin
for s in local split xfer th btplan btrun; do
  $OC -O binary --only-section=.ov_$s $OUT/sort.elf $OUT/ov_$s.bin
  cat $OUT/ov_$s.bin >> $OUT/sort.stage.bin
done
riscv64-unknown-elf-nm -S $OUT/sort.elf > $OUT/sort.sym
riscv64-unknown-elf-objdump -d $OUT/sort.elf > $OUT/sort.dis
# the map: sizes, and the check that the images' load addresses are consecutive (sort.stage.bin's layout)
python3 - "$OUT" <<'PY'
import subprocess, sys
out = sys.argv[1]
secs = {}
for l in subprocess.run(["riscv64-unknown-elf-objdump", "-h", out + "/sort.elf"], capture_output=True, text=True).stdout.splitlines():
    f = l.split()
    if len(f) >= 6 and f[0].isdigit(): secs[f[1]] = (int(f[2], 16), int(f[3], 16), int(f[4], 16))
sym = {}
for l in open(out + "/sort.sym"):
    f = l.split()
    if len(f) == 3: sym[f[2]] = int(f[0], 16)
k = secs[".text"][0]; base = sym["_ov_base"]; region = 0x1F00 - base
print(f"sort image (M7): kernel {k} B (0x0000-0x{k:04x}); overlay region at 0x{base:04x}, {region} B")
lma = 0x10000000
for s in ["local", "split", "xfer", "th", "btplan", "btrun"]:
    size, vma, l = secs[".ov_" + s]
    assert l == lma, f".ov_{s}: load address 0x{l:x}, expected 0x{lma:x}"
    print(f"  .ov_{s:7s} {size:5d} B ({100 * size // region:3d}% of the region), {size // 8} packets, staged at +{(lma - 0x10000000) // 4:#x} chunks")
    lma += size
print(f"  staged in all: {lma - 0x10000000} B")
d = secs[".data"][0]; ss, se, be = sym["_shared_start"], sym["_shared_end"], sym["_end"]
print(f"data: .rodata+.data {d} B; shared region 0x{ss:04x}-0x{se:04x} ({se - ss} B: split {secs['.shared_split'][0]} B, "
      f"planner + RED buffers {secs['.shared_plan'][0]} B); .bss end 0x{be:04x}; {0x5c00 - be} B to the stack top")
PY
