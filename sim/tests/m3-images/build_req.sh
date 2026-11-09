#!/bin/bash
# build_req.sh [--check] (WORKER14): test_cc_req's two programs (cc_req.c on C&C 1, tile_req.c on tile 5), built as
# W16's build.sh builds cc_ping (sw/tests' way: crt0.S, app.ld, -Os) in a scratch copy of sim/ref (D5), with
# riscv64-unknown-elf-gcc. Writes cc_req.* and tile_req.* (code.bin, data.bin, sym) and REQ_MANIFEST, or with --check
# compares a fresh build with them.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd); SIM=$(cd "$HERE/../.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cp -r "$SIM/ref" "$T/ref"; M=$T/ref/meshsim
mkdir -p "$T/out" "$M/sw/build"
for p in cc_req tile_req; do
  (cd $M/sw && cp "$HERE/$p.c" tests/ && riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 -std=gnu11 -ffreestanding \
     -nostdlib -nostartfiles -static -fno-builtin -Wall -Wno-array-bounds -Wl,--no-relax -I. -Os -T app.ld crt0.S tests/$p.c \
     -o build/$p.elf && riscv64-unknown-elf-objcopy -O binary --only-section=.text build/$p.elf build/$p.code.bin \
     && riscv64-unknown-elf-objcopy -O binary --only-section=.data build/$p.elf build/$p.data.bin \
     && riscv64-unknown-elf-nm build/$p.elf > build/$p.sym) > "$T/build.log" 2>&1 || { cat "$T/build.log"; exit 1; }
  for x in code.bin data.bin sym; do cp "$M/sw/build/$p.$x" "$T/out/"; done
done
(cd "$T/out" && sha256sum *.bin *.sym) > "$T/REQ_MANIFEST"
if [ "${1:-}" = --check ]; then
  cmp -s "$T/REQ_MANIFEST" "$HERE/REQ_MANIFEST" && (cd "$HERE" && sha256sum -c --quiet REQ_MANIFEST) && echo "build_req: equal to a fresh build" && exit 0
  echo "build_req: differ from a fresh build"; diff "$HERE/REQ_MANIFEST" "$T/REQ_MANIFEST"; exit 1
fi
cp "$T"/out/* "$HERE/"; cp "$T/REQ_MANIFEST" "$HERE/REQ_MANIFEST"; echo "build_req: written (riscv64-unknown-elf-gcc $(riscv64-unknown-elf-gcc -dumpversion))"
