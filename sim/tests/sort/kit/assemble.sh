#!/bin/bash
# assemble.sh OUTDIR [KEYDIR] -- the sort's measurement kit on meshsim v1.35 (WORKER24, P6, row D8): v1.3's kit
# (meshsim/kit) on the C++ binaries, no Java to run it (D40). Run from sim/ after a release build, or it builds one.
#   OUTDIR/sort_test, sort_summary     the simulator's sort driver (v1.3's SortTest) and SortSummary (clang release)
#   OUTDIR/image/sort.{code.bin,data.bin,sym}, fw/boot/bootrom.hex, data/hops.txt   what sort_test reads
#   OUTDIR/sw/bootrom.hex, hops.txt    links to them, under the names the inputs line gives them (v1.3's)
#   OUTDIR/run.sh, tools/runjob.sh, jobs.txt (v1.3's job list), README.md
#   OUTDIR/HASHES                      the reference set: sort_test, the boot ROM, the image, hops.txt and the 20 key sets
#                                      at 64 x 65,536 and 64 x 2,000 (their sha256 as v1.3's kit recorded them, KEYS.sha256)
#   OUTDIR/keys-N/                     with KEYDIR, its key sets copied in (KEYDIR/keys-N/<set>.keys), checked against HASHES
# The key sets are made by pipeline/keys' KeyGen (Java, D40): the kit takes them prebuilt; run.sh makes missing ones
# only where a JDK is present (owner's Q4).
set -eu
SIM=$(cd "$(dirname "$0")/../../.." && pwd); HERE=$SIM/tests/sort/kit
O=${1:?usage: assemble.sh OUTDIR [KEYDIR]}; KD=${2:-}
B=$SIM/build/clang-release
(cd "$SIM" && make -s CC_NAME=clang MODE=release "$B/sort_test" "$B/sort_summary")
mkdir -p "$O/tools" "$O/image" "$O/sw" "$O/fw/boot" "$O/data" "$O/pipeline/keys"
cp "$B/sort_test" "$B/sort_summary" "$O/"
cp "$HERE/run.sh" "$HERE/README.md" "$HERE/jobs.txt" "$O/"; cp "$HERE/tools/runjob.sh" "$O/tools/"
for f in sort.code.bin sort.data.bin sort.sym; do cp "$SIM/tests/golden-m2/images/$f" "$O/image/"; done
cp "$SIM/fw/boot/bootrom.hex" "$O/fw/boot/"; cp "$SIM/data/hops.txt" "$O/data/"
cp "$SIM/pipeline/keys/KeyGen.java" "$O/pipeline/keys/"
ln -sf ../fw/boot/bootrom.hex "$O/sw/bootrom.hex"; ln -sf data/hops.txt "$O/hops.txt"
( cd "$O"
  echo "# meshsim v1.35 kit: SHA-256 of every input a run reads (run.sh checks them with sha256sum)"
  sha256sum sort_test sw/bootrom.hex image/sort.code.bin image/sort.data.bin image/sort.sym hops.txt
  cat "$HERE/KEYS.sha256" ) > "$O/HASHES"
if [ -n "$KD" ]; then
  for d in "$KD"/keys-*; do [ -d "$d" ] && mkdir -p "$O/$(basename "$d")" && cp "$d"/*.keys "$O/$(basename "$d")/"; done
  (cd "$O" && grep '\.keys$' HASHES | while read -r h f; do [ -f "$f" ] && echo "$h  $f"; done | sha256sum -c --quiet) \
    || { echo "assemble.sh: a key set in $KD differs from KEYS.sha256"; exit 1; }
fi
chmod +x "$O/run.sh" "$O/tools/runjob.sh"
echo "kit assembled in $O: $(du -sh "$O" | cut -f1)"
