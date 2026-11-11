#!/bin/sh
# pipeline/check.sh (WORKER19, P1): builds the pipeline (build.sh, which runs gold's own tests: the inline emit digests
# and upstream's inline vectors) and checks the tools' outputs against v1.3's:
#   - the copies: every file in FROM-V1.3.sha256 still v1.3's;
#   - keys: keys/EXPECTED.sha256 (the uniform key sets at 16,384 and 65,536 per tile, every key and split set at 2,000);
#   - gold and jumpmap: jumpmap's fixtures (test/fixtures.sh: gold's segment files and jumpmap's outputs for 11 guests)
#     regenerated in a scratch copy, equal byte for byte to the committed jumpmap/out/ (v1.3's).
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
echo "gold, jumpmap: the fixtures of $(ls -d "$P"/jumpmap/out/*/ | wc -l) guests equal to v1.3's"
echo "pipeline/check.sh: all passed"
