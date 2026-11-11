#!/bin/sh
# pipeline/check.sh (WORKER19, P1): builds the pipeline (build.sh, which runs gold's own tests: the inline emit digests
# and upstream's inline vectors) and checks the tools' outputs against v1.3's:
#   - the copies: every file in FROM-V1.3.sha256 still v1.3's;
#   - keys: keys/EXPECTED.sha256 (the uniform key sets at 16,384 and 65,536 per tile, every key and split set at 2,000);
#   - gold and jumpmap: jumpmap's fixtures (test/fixtures.sh: gold's segment files and jumpmap's outputs for 11 guests)
#     regenerated in a scratch copy, equal byte for byte to the committed jumpmap/out/ (v1.3's).
#   - inputs: the pinned gold and jumpmap sources (inputs/src/) intact and building; v1.35's convert_delivery (the
#     C++ tool inputs/ calls; CONVERT_DELIVERY, default ../build/clang-release/convert_delivery) on a small v1.25
#     delivery (inputs/test/v125-small) equal to v1.3's ConvertDelivery: the converted manifest's sha256, which holds
#     every converted record's.
# gold/check.sh (the four segment files at 2^29 and the hard cuts) needs jolt-asic-planning and the reference files:
# run it where they are.
set -e
P=$(cd "$(dirname "$0")" && pwd)
(cd "$P" && grep -v '^#' FROM-V1.3.sha256 | awk '{print $1 "  " $2}' | sha256sum -c --quiet -) || { echo "pipeline/check.sh: a copy differs from v1.3's"; exit 1; }
sh "$P/build.sh"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
J="java -cp $P/build/keys"
$J KeyGen "$T/keys-16384" --n 16384 --only uniform > /dev/null
$J KeyGen "$T/keys-65536" --n 65536 --only uniform > /dev/null
$J KeyGen "$T/n2000/keys" --n 2000 > /dev/null
$J SplitSets "$T/n2000/split" --n 2000 > /dev/null
(cd "$T" && grep -v '^#' "$P/keys/EXPECTED.sha256" | sha256sum -c --quiet -) || { echo "pipeline/check.sh: keys FAILED"; exit 1; }
echo "keys: $(grep -vc '^#' "$P/keys/EXPECTED.sha256") outputs equal to v1.3's"
mkdir -p "$T/p"; cp -r "$P/gold" "$P/jumpmap" "$T/p/"
(cd "$T/p/jumpmap" && sh test/fixtures.sh --write > "$T/fixtures.log" 2>&1) || { tail "$T/fixtures.log"; echo "pipeline/check.sh: jumpmap fixtures FAILED"; exit 1; }
diff -r "$P/jumpmap/out" "$T/p/jumpmap/out" > /dev/null || { echo "pipeline/check.sh: jumpmap's fixtures differ from v1.3's"; exit 1; }
(cd "$P/inputs/src" && grep -v '^#' SNAPSHOTS.sha256 | awk '{print $1 "  " $2}' | sha256sum -c --quiet -) || { echo "pipeline/check.sh: inputs/src differs from SNAPSHOTS.sha256"; exit 1; }
for d in d97ecde/gold d97ecde/jumpmap eeb64b5/gold; do
  cp=; [ $d = d97ecde/jumpmap ] && cp="-cp $T/c/d97ecde/gold"
  mkdir -p "$T/c/$d"; javac --release 21 -nowarn -d "$T/c/$d" $cp $(find "$P/inputs/src/$d/src" -name '*.java') 2>&1 | grep -v '^Note:' || true
  [ -n "$(find "$T/c/$d" -name Main.class)" ] || { echo "pipeline/check.sh: inputs/src/$d doesn't build"; exit 1; }
done
echo "inputs: the pinned gold and jumpmap sources intact ($(grep -vc '^#' "$P/inputs/src/SNAPSHOTS.sha256") files) and building"
CD=${CONVERT_DELIVERY:-$P/../build/clang-release/convert_delivery}
[ -x "$CD" ] || { echo "pipeline/check.sh: no convert_delivery at $CD (build v1.35, or CONVERT_DELIVERY=)"; exit 1; }
"$CD" "$P/inputs/test/v125-small" "$T/conv" evm-zone-29 > "$T/conv.log" || { cat "$T/conv.log"; echo "pipeline/check.sh: convert_delivery FAILED"; exit 1; }
(cd "$T/conv" && grep -v '^#' "$P/inputs/test/v125-small/EXPECTED" | sha256sum -c --quiet -) || { echo "pipeline/check.sh: the conversion differs from v1.3's"; exit 1; }
for f in "$T"/conv/evm-zone-29/window_*.bin; do [ -L "$f" ] && continue; grep -q "$(sha256sum < "$f" | cut -c1-64)" "$T/conv/evm-zone-29.manifest.json" || { echo "pipeline/check.sh: $f not as its manifest says"; exit 1; }; done
echo "inputs: convert_delivery on a v1.25 delivery equal to v1.3's ConvertDelivery"
echo "gold, jumpmap: the fixtures of $(ls -d "$P"/jumpmap/out/*/ | wc -l) guests equal to v1.3's"
echo "pipeline/check.sh: all passed"
