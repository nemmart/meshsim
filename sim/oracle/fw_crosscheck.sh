#!/bin/bash
# oracle/fw_crosscheck.sh (WORKER19, P1): v1.35's firmware build (fw/build.sh) against v1.3's own. Builds every image in
# a scratch copy of sim/ref with v1.3's scripts (meshsim/sw/build.sh, build_cc.sh, build_recipes.sh, replay/build.sh,
# tracer/build.sh, ext/sort/build.sh; the M1 programs and cc_ping, cc_req, tile_req as W2's, W16's and W14's scripts
# built them there), then fw/build.sh's, and compares them file by file. Also checks sim/COPIES.sha256 against sim/ref.
# Needs sim/ref, riscv64-unknown-elf-gcc and (for mesh.h's regeneration in v1.3's sw/build.sh) a JDK.
set -eu
SIM=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
# the copies
grep -v '^#' "$SIM/COPIES.sha256" | while read -r h s r; do
  [ "$(sha256sum < "$SIM/ref/$r" | cut -c1-64)" = "$h" ] || { echo "COPIES: $r differs from the manifest"; exit 1; }
done
echo "copies: $(grep -vc '^#' "$SIM/COPIES.sha256") files of sim/ref equal to the manifest"
# v1.3's build
cp -r "$SIM/ref" "$T/ref"; M=$T/ref/meshsim/sw
(cd "$M" && sh build.sh && sh build_cc.sh && sh build_recipes.sh && sh replay/build.sh && sh tracer/build.sh \
   && sh ext/sort/build.sh) > "$T/v13.log" 2>&1 || { tail "$T/v13.log"; exit 1; }
CC="riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32"
for src in "$SIM"/fw/m1/*.S; do       # W2's fw/build.sh
  n=$(basename "$src" .S)
  $CC -nostdlib -nostartfiles -static -Wl,--no-relax -I"$M" -T "$M/app.ld" "$src" -o "$M/build/$n.elf"
done
for n in cc_ping cc_req tile_req; do  # W16's m3-images/build.sh, W14's build_req.sh
  (cd "$M" && $CC -std=gnu11 -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wall -Wno-array-bounds \
     -Wl,--no-relax -I. -Os -T app.ld crt0.S "$SIM/tests/m3-images/$n.c" -o build/$n.elf)
done
V=$T/v13; mkdir -p "$V/log8k" "$V/oldmap"
for e in "$M"/build/*.elf "$M"/replay/build/replay_ctl.elf "$M"/tracer/build/tracer_ctl.elf "$M"/ext/sort/build/sort.elf; do
  n=$(basename "$e" .elf); case $n in bootrom) continue;; esac
  for x in code.bin data.bin sym; do cp "${e%.elf}.$x" "$V/" 2>/dev/null || true; done
  [ -f "$V/$n.sym" ] || riscv64-unknown-elf-nm "$e" > "$V/$n.sym"
  [ -f "$V/$n.code.bin" ] || riscv64-unknown-elf-objcopy -O binary --only-section=.text "$e" "$V/$n.code.bin"
  [ -f "$V/$n.data.bin" ] || riscv64-unknown-elf-objcopy -O binary --only-section=.data "$e" "$V/$n.data.bin"
done
for d in log8k oldmap; do cp "$M"/replay/build/$d/replay_ctl.{code.bin,data.bin,sym} "$V/$d/"; done
cp "$M/bootrom.hex" "$V/"
# v1.35's build
OUT=$T/v135 bash "$SIM/fw/build.sh" --check > "$T/v135.log" 2>&1 || { tail "$T/v135.log"; exit 1; }
# compare: the same set of files, every one equal
(cd "$V" && find . -type f | sort) > "$T/a"; (cd "$T/v135" && find . -type f | sort) > "$T/b"
diff "$T/a" "$T/b" || { echo "fw_crosscheck: the two builds made different sets of images"; exit 1; }
bad=0; while read -r f; do cmp -s "$V/$f" "$T/v135/$f" || { echo "DIFFERS: $f"; bad=1; }; done < "$T/a"
[ $bad = 0 ] || { echo "fw_crosscheck: FAILED"; exit 1; }
echo "fw_crosscheck: $(wc -l < "$T/a") image files, fw/build.sh's equal to v1.3's build byte for byte"
