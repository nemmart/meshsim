#!/bin/bash
# build.sh [--check] (WORKER16): M3's firmware images, prebuilt (D4), byte-identical to 110u's kit
# (sim/planning/sessions/WORKER13-tasks/IMAGES): cc1, cc23 (C&C 1-3), tracer_ctl (C&C 3), replay_ctl (the tiles), each
# .code.bin, .data.bin and .sym. Builds a scratch copy of sim/ref (nothing in sim/ref is written: D5) with
# riscv64-unknown-elf-gcc (apt install gcc-riscv64-unknown-elf) and sw/build.sh, build_cc.sh, replay/build.sh,
# tracer/build.sh; checks the images against IMAGES and MANIFEST. Also cc_ping (cc_ping.c here: test_cc_units' C&C 1
# program), built as sw/build.sh builds sw/tests' programs (crt0.S, app.ld, -Os). --check compares with the committed files; without
# it the committed files are rewritten. The boot ROM is sim/ref/meshsim/sw/bootrom.hex (committed in ref).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd); SIM=$(cd "$HERE/../.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cp -r "$SIM/ref" "$T/ref"; M=$T/ref/meshsim
(cd $M/sw && sh build.sh && sh build_cc.sh && (cd replay && sh build.sh) && (cd tracer && sh build.sh)) > "$T/build.log" 2>&1 || { tail "$T/build.log"; exit 1; }
(cd $M/sw && cp "$HERE/cc_ping.c" tests/ && riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 -std=gnu11 -ffreestanding \
   -nostdlib -nostartfiles -static -fno-builtin -Wall -Wno-array-bounds -Wl,--no-relax -I. -Os -T app.ld crt0.S tests/cc_ping.c \
   -o build/cc_ping.elf && riscv64-unknown-elf-objcopy -O binary --only-section=.text build/cc_ping.elf build/cc_ping.code.bin \
   && riscv64-unknown-elf-objcopy -O binary --only-section=.data build/cc_ping.elf build/cc_ping.data.bin \
   && riscv64-unknown-elf-nm build/cc_ping.elf > build/cc_ping.sym) >> "$T/build.log" 2>&1 || { tail "$T/build.log"; exit 1; }
(cd $M && sha256sum -c --quiet "$SIM/planning/sessions/WORKER13-tasks/IMAGES") || { echo "build.sh: the images differ from IMAGES (110u's)"; exit 1; }
mkdir -p "$T/out"
for f in sw/build/cc1 sw/build/cc23 sw/replay/build/replay_ctl sw/tracer/build/tracer_ctl sw/build/cc_ping; do
  for x in code.bin data.bin sym; do cp "$M/$f.$x" "$T/out/"; done
done
(cd "$T/out" && sha256sum *.bin *.sym) > "$T/MANIFEST"
if [ "${1:-}" = --check ]; then
  cmp -s "$T/MANIFEST" "$HERE/MANIFEST" && (cd "$HERE" && sha256sum -c --quiet MANIFEST) && echo "m3-images: equal to a fresh build and to IMAGES" && exit 0
  echo "m3-images: differ from a fresh build"; diff "$HERE/MANIFEST" "$T/MANIFEST"; exit 1
fi
cp "$T"/out/* "$HERE/"; cp "$T/MANIFEST" "$HERE/MANIFEST"; echo "m3-images: written (riscv64-unknown-elf-gcc $(riscv64-unknown-elf-gcc -dumpversion))"
