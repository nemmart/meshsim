#!/bin/bash
# build_req.sh [--check] (from sim/fw): test_cc_req's two programs (cc_req.c on C&C 1, tile_req.c on
# tile 5), built by sim/fw/build.sh as sw/tests' programs are (crt0.S, app.ld, -Os). Writes cc_req.* and tile_req.*
# (code.bin, data.bin, sym) and REQ_MANIFEST, or with --check compares a fresh build with them.
set -eu
export LC_ALL=C   # byte order for sort, ls and globs, and nm's symbol order: the outputs are the same in any locale
HERE=$(cd "$(dirname "$0")" && pwd); SIM=$(cd "$HERE/../.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
OUT=$T/fw bash "$SIM/fw/build.sh" --build-only > "$T/build.log" 2>&1 || { tail "$T/build.log"; exit 1; }
mkdir -p "$T/out"
for p in cc_req tile_req; do
  for x in code.bin data.bin sym; do cp "$T/fw/$p.$x" "$T/out/"; done
done
(cd "$T/out" && sha256sum *.bin *.sym) > "$T/REQ_MANIFEST"
if [ "${1:-}" = --check ]; then
  cmp -s "$T/REQ_MANIFEST" "$HERE/REQ_MANIFEST" && (cd "$HERE" && sha256sum -c --quiet REQ_MANIFEST) && echo "build_req: equal to a fresh build" && exit 0
  echo "build_req: differ from a fresh build"; diff "$HERE/REQ_MANIFEST" "$T/REQ_MANIFEST"; exit 1
fi
cp "$T"/out/* "$HERE/"; cp "$T/REQ_MANIFEST" "$HERE/REQ_MANIFEST"; echo "build_req: written (riscv64-unknown-elf-gcc $(riscv64-unknown-elf-gcc -dumpversion))"
