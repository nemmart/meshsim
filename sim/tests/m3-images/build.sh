#!/bin/bash
# build.sh [--check] (WORKER16; W19: from sim/fw): M3's firmware images, prebuilt (D4), byte-identical to 110u's kit
# (sim/planning/sessions/WORKER13-tasks/IMAGES): cc1, cc23 (C&C 1-3), tracer_ctl (C&C 3), replay_ctl (the tiles), each
# .code.bin, .data.bin and .sym, built by sim/fw/build.sh (v1.3's build_cc.sh, replay/build.sh and tracer/build.sh,
# byte-identical: oracle/fw_crosscheck.sh); checked against IMAGES and MANIFEST. Also cc_ping (cc_ping.c here:
# test_cc_units' C&C 1 program), built as sw/tests' programs are (crt0.S, app.ld, -Os). --check compares with the
# committed files; without it the committed files are rewritten. The boot ROM is sim/fw/boot/bootrom.hex.
set -eu
export LC_ALL=C   # byte order for sort, ls and globs, and nm's symbol order: the outputs are the same in any locale
HERE=$(cd "$(dirname "$0")" && pwd); SIM=$(cd "$HERE/../.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
OUT=$T/fw bash "$SIM/fw/build.sh" --build-only > "$T/build.log" 2>&1 || { tail "$T/build.log"; exit 1; }
# IMAGES names v1.3's paths: hops.txt, sw/bootrom.hex, sw/build/<n>, sw/replay/build/<n>, sw/tracer/build/<n>
mkdir -p "$T/k/sw/build" "$T/k/sw/replay/build" "$T/k/sw/tracer/build"
cp "$SIM/data/hops.txt" "$T/k/"; cp "$SIM/fw/boot/bootrom.hex" "$T/k/sw/"
cp "$T"/fw/cc1.* "$T"/fw/cc23.* "$T/k/sw/build/"; cp "$T"/fw/replay_ctl.* "$T/k/sw/replay/build/"; cp "$T"/fw/tracer_ctl.* "$T/k/sw/tracer/build/"
(cd "$T/k" && sha256sum -c --quiet "$SIM/planning/sessions/WORKER13-tasks/IMAGES") || { echo "build.sh: the images differ from IMAGES (110u's)"; exit 1; }
mkdir -p "$T/out"
for n in cc1 cc23 replay_ctl tracer_ctl cc_ping; do
  for x in code.bin data.bin sym; do cp "$T/fw/$n.$x" "$T/out/"; done
done
(cd "$T/out" && sha256sum *.bin *.sym) > "$T/MANIFEST"
if [ "${1:-}" = --check ]; then
  cmp -s "$T/MANIFEST" "$HERE/MANIFEST" && (cd "$HERE" && sha256sum -c --quiet MANIFEST) && echo "m3-images: equal to a fresh build and to IMAGES" && exit 0
  echo "m3-images: differ from a fresh build"; diff "$HERE/MANIFEST" "$T/MANIFEST"; exit 1
fi
cp "$T"/out/* "$HERE/"; cp "$T/MANIFEST" "$HERE/MANIFEST"; echo "m3-images: written (riscv64-unknown-elf-gcc $(riscv64-unknown-elf-gcc -dumpversion))"
