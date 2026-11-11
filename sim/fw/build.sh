#!/bin/bash
# fw/build.sh [--check | --build-only] (WORKER19, P1): builds every firmware image v1.35 runs, from fw/'s sources, exactly as v1.3's
# scripts built them (the same flags, start-up files, linker scripts; each step names the v1.3 script it reproduces),
# into fw/build/out/ (OUT=DIR to change). Then:
#   --check   every image equal, byte for byte, to its committed copy; exit 1 on any difference or missing copy.
#   (none)    images that live in a test's fixture folder are compared with it (never overwritten: those folders have
#             their own scripts, which take their images from this build); every other image is (re)written into
#             fw/images/, with fw/images/MANIFEST (sha256).
#   --build-only   only the build into OUT (the fixture folders' scripts take their images from it).
# The committed images: tests/golden/images (the M1 programs, hello, faults, misalign_ld/st), tests/golden-m2/images
# (regs, wfi, a2a, sort), tests/m3-images (cc1, cc23, replay_ctl, tracer_ctl, cc_ping, cc_req, tile_req),
# fw/boot/bootrom.hex, fw/images/ (the rest), fw/riscv-tests/bin (only with RISCV_TESTS, below).
# RISCV_TESTS=DIR (a riscv-tests checkout at 3df703c) also rebuilds the riscv-tests images (fw/riscv-tests/build.sh).
# Toolchain: riscv64-unknown-elf-gcc 13.2 (Ubuntu 24.04's gcc-riscv64-unknown-elf 13.2.0-11ubuntu1+12), the one v1.3's
# images were built with; patch(1). The runner has none: the images stay committed.
set -eu
FW=$(cd "$(dirname "$0")" && pwd); SIM=$(cd "$FW/.." && pwd)
MODE=write; case "${1:-}" in --check) MODE=check;; --build-only) MODE=build;; esac
command -v riscv64-unknown-elf-gcc >/dev/null || { echo "fw/build.sh: needs riscv64-unknown-elf-gcc (apt install gcc-riscv64-unknown-elf)"; exit 1; }
OUT=${OUT:-$FW/build/out}; rm -rf "$OUT"; mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
OC=riscv64-unknown-elf-objcopy; NM=riscv64-unknown-elf-nm
RV="riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32"
# meshsim/sw/build.sh's flags (sw/tests' programs and the boot ROM; cc_ping, cc_req, tile_req are built the same way)
SWCC="$RV -std=gnu11 -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wall -Wno-array-bounds -Wl,--no-relax -I$FW/include"
# meshsim/sw/build_cc.sh's and build_recipes.sh's (-include the generated mesh.h; the recipes' crt0.S and handler.S)
RCC="$RV -std=gnu11 -O2 -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wl,--no-relax -include $FW/include/mesh.h"
# meshsim/sw/replay/build.sh's and tracer/build.sh's
CTLCC="$RV -std=gnu11 -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wall -Wextra -Wno-array-bounds -Wl,--no-relax -I. -I$FW/include -Os"
SOFTDIV=' (__udivsi3|__umodsi3|__divsi3|__modsi3)$'

img() {   # img ELF NAME [nm flags]: NAME.code.bin, NAME.data.bin, NAME.sym in OUT
  mkdir -p "$(dirname "$OUT/$2")"
  $OC -O binary --only-section=.text "$1" "$OUT/$2.code.bin"
  $OC -O binary --only-section=.data "$1" "$OUT/$2.data.bin"
  $NM ${3:-} "$1" > "$OUT/$2.sym"
}
sizes() { echo "$1: code $(stat -c %s "$OUT/$1.code.bin") B, data $(stat -c %s "$OUT/$1.data.bin") B"; }
nodiv() { if $NM "$1" | grep -qE "$2"; then echo "ERROR: $(basename "$1" .elf): software division or library arithmetic"; exit 1; fi; }

# ---- the boot ROM (meshsim/sw/build.sh): bootrom.hex, one 32-bit word per line
cd "$FW/boot"
$SWCC -T bootrom.ld bootrom.S -o "$T/bootrom.elf"
$OC -O binary "$T/bootrom.elf" "$T/bootrom.bin"
od -An -v -tx4 -w4 "$T/bootrom.bin" | sed 's/^ *//' > "$OUT/bootrom.hex"
echo "boot ROM: $(wc -l < "$OUT/bootrom.hex") words"
cd "$FW"

# ---- sw/tests' programs (meshsim/sw/build.sh): every tests/*.c but the v1.1 recipes (rcp_*); a line
# "// build: <flags>" in a source replaces -Os; software division refused
for src in tests/*.c; do
  n=$(basename "$src" .c); case $n in rcp_*) continue;; esac
  OPT=$(sed -n 's|^// build: ||p' "$src" | head -1); [ -n "$OPT" ] || OPT=-Os
  $SWCC $OPT -T app.ld crt0.S "$src" -o "$T/$n.elf"
  nodiv "$T/$n.elf" "$SOFTDIV"; img "$T/$n.elf" $n; sizes $n
done

# ---- the M1 programs (m1/*.S; W2's former fw/build.sh)
for src in m1/*.S; do
  n=$(basename "$src" .S)
  $RV -nostdlib -nostartfiles -static -Wl,--no-relax -I"$FW/include" -T app.ld "$src" -o "$T/$n.elf"
  img "$T/$n.elf" $n; sizes $n
done

# ---- the test programs kept beside their fixtures in tests/m3-images (cc_ping: W16; cc_req, tile_req: W14), built
# as sw/tests' programs are (crt0.S, app.ld, -Os)
for n in cc_ping cc_req tile_req; do
  $SWCC -Os -T app.ld crt0.S "$SIM/tests/m3-images/$n.c" -o "$T/$n.elf"
  img "$T/$n.elf" $n; sizes $n
done

# ---- the controllers' firmware (meshsim/sw/build_cc.sh): cc1, cc23, cc_rcp, cc_bad; -Werror; the size limits
for n in cc1 cc23 cc_rcp cc_bad; do
  $RCC -Werror -T recipes/recipes.ld recipes/crt0.S recipes/handler.S cc/$n.c -o "$T/$n.elf"
  img "$T/$n.elf" $n
  end=$(awk '$3 == "_end" { print $1 }' "$OUT/$n.sym")
  [ $((0x$end)) -le $((0x1000)) ] || { echo "$n: data and .bss end at 0x$end, above 0x1000 (cc1's report buffer)"; exit 1; }
  if [ $n = cc_rcp ] && { [ $(stat -c %s "$OUT/$n.code.bin") -gt 1024 ] || [ $(stat -c %s "$OUT/$n.data.bin") -gt 256 ]; }; then
    echo "$n: more than rcp_cc1.c's 64 + 16 packets"; exit 1
  fi
  sizes $n
done

# ---- the v1.2 recipes (meshsim/sw/build_recipes.sh): every recipes/rcp_*.c with recipes_support.c
nr=0
for src in recipes/rcp_*.c; do
  n=$(basename "$src" .c)
  $RCC -T recipes/recipes.ld recipes/crt0.S recipes/handler.S "$src" recipes/recipes_support.c -o "$T/$n.elf"
  img "$T/$n.elf" $n; nr=$((nr + 1))
done
echo "recipes: $nr built"

# ---- the replay controller (meshsim/sw/replay/build.sh): replay_ctl, its 8 KB-log control (log8k/) and the old queue
# map (oldmap/, test only)
cd "$FW/replay"
$CTLCC -T ../app.ld ../crt0.S replay_ctl.c -o "$T/replay_ctl.elf"
nodiv "$T/replay_ctl.elf" "$SOFTDIV"; img "$T/replay_ctl.elf" replay_ctl; sizes replay_ctl
$CTLCC -DHALF_LOG=256 -T ../app.ld ../crt0.S replay_ctl.c -o "$T/log8k.elf"; img "$T/log8k.elf" log8k/replay_ctl
$CTLCC -T ../app.ld ../crt0.S replay_ctl_oldmap.c -o "$T/oldmap.elf"; img "$T/oldmap.elf" oldmap/replay_ctl

# ---- C&C 3's Tracer firmware (meshsim/sw/tracer/build.sh)
cd "$FW/tracer"
$CTLCC -T ../app.ld ../crt0.S tracer_ctl.c -o "$T/tracer_ctl.elf"
nodiv "$T/tracer_ctl.elf" "$SOFTDIV"; img "$T/tracer_ctl.elf" tracer_ctl; sizes tracer_ctl
cd "$FW"

# ---- the sort program (meshsim/sw/ext/sort/build.sh, the same as sort/program/build.sh): sort/program/sw, with the
# planner and the split as patched for the sort (sort/program/patches, applied as v1.3's tools/check_layout.sh did); -O2
mkdir -p "$T/p/sort/skew" "$T/p/sort/split"
cp -r sort/skew/planner "$T/p/sort/skew/"; cp sort/split/split.c sort/split/split.h sort/split/split_regs.h "$T/p/sort/split/"
patch -s -p0 -d "$T/p" < sort/program/patches/planner.patch
patch -s -p0 -d "$T/p" < sort/program/patches/split-barrier.patch
SK=$T/p/sort/skew/planner; SP=$T/p/sort/split; PS=$FW/sort/program/sw
SCC="$RV -std=gnu11 -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wall -Wno-array-bounds -Wl,--no-relax -I$PS -I$SK -I$FW/include"
SOPT="-O2 -fno-jump-tables"
OBJS=""
for f in "$PS"/*.c "$SK/skew_common.c" "$SK/skew_th.c" "$SK/skew_bt.c"; do
  o=$T/$(basename "$f" .c).o
  $SCC $SOPT -ffunction-sections -fdata-sections -c "$f" -o "$o"; OBJS="$OBJS $o"
done
$SCC $SOPT -ffunction-sections -fdata-sections -DSPLIT_BARRIER=sort_barrier -I"$SP" -c "$SP/split.c" -o "$T/split_bisect.o"
OBJS="$OBJS $T/split_bisect.o"
$SCC -T "$PS/sort.ld" -Wl,--gc-sections -Wl,--orphan-handling=warn -Wl,--build-id=none crt0.S $OBJS -o "$T/sort.elf"
nodiv "$T/sort.elf" ' (__udivsi3|__umodsi3|__divsi3|__modsi3|__mulsi3|__muldi3)$'; img "$T/sort.elf" sort -S; sizes sort

# ---- not built: the split on its own (sort/split/splitdrv.c; v1.3's meshsim/sw/ext/split-bisect/build.sh, for
# SplitRun). It doesn't compile against mesh.h 1.2 in v1.3 either (it names v1.1's registers), and run_tests.sh never
# built it: the source is kept as it was.

# ---- the riscv-tests images (meshsim/test/riscv/build.sh), only with a checkout
if [ -n "${RISCV_TESTS:-}" ]; then
  sh riscv-tests/build.sh "$RISCV_TESTS" "$T/riscv" > /dev/null
  mkdir -p "$OUT/riscv-tests"; cp "$T"/riscv/*.bin "$OUT/riscv-tests/"
  echo "riscv-tests: $(ls "$OUT"/riscv-tests/*.code.bin | wc -l) programs"
fi

[ $MODE = build ] && { echo "fw/build.sh: built into $OUT"; exit 0; }

# ---- compare with (or install) the committed images
FIX="tests/golden/images tests/golden-m2/images tests/m3-images"
bad=0; nfix=0; nfw=0
cd "$OUT"
for f in $(find . -type f | sed 's|^\./||' | sort); do
  where=; f2=$f
  case $f in
    bootrom.hex) where=fw/boot ;;
    riscv-tests/*) where=fw/riscv-tests/bin; f2=${f#riscv-tests/} ;;
    *) for d in $FIX; do [ -f "$SIM/$d/$f" ] && { where=$d; break; }; done ;;
  esac
  if [ -n "$where" ]; then
    nfix=$((nfix + 1))
    cmp -s "$f" "$SIM/$where/$f2" || { echo "DIFFERS: $f against $where/$f2"; bad=1; }
  elif [ $MODE = check ]; then
    nfw=$((nfw + 1))
    cmp -s "$f" "$FW/images/$f" || { echo "DIFFERS (or missing): $f against fw/images/$f"; bad=1; }
  else
    nfw=$((nfw + 1)); mkdir -p "$FW/images/$(dirname "$f")"; cp "$f" "$FW/images/$f"
  fi
done
if [ $MODE = write ]; then
  (cd "$FW/images" && find . -type f ! -name MANIFEST | sed 's|^\./||' | sort | xargs sha256sum) > "$FW/images/MANIFEST"
fi
[ $bad = 0 ] || { echo "fw/build.sh: FAILED"; exit 1; }
if [ $MODE = check ]; then echo "fw/build.sh --check: $nfix images equal to their fixture copies, $nfw to fw/images/"
else echo "fw/build.sh: $nfix images equal to their fixture copies; $nfw written to fw/images/"; fi
