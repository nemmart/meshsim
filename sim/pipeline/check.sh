#!/bin/sh
# pipeline/check.sh (WORKER19, P1): builds the pipeline (build.sh) and checks each tool's output against v1.3's
# (keys/EXPECTED.sha256): the uniform key sets at 16,384 and 65,536 per tile, every key set and split set at 2,000.
set -e
P=$(cd "$(dirname "$0")" && pwd)
sh "$P/build.sh"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
J="java -cp $P/build/keys"
$J KeyGen "$T/keys-16384" --n 16384 --only uniform > /dev/null
$J KeyGen "$T/keys-65536" --n 65536 --only uniform > /dev/null
$J KeyGen "$T/n2000/keys" --n 2000 > /dev/null
$J SplitSets "$T/n2000/split" --n 2000 > /dev/null
(cd "$T" && grep -v '^#' "$P/keys/EXPECTED.sha256" | sha256sum -c --quiet -) || { echo "pipeline/check.sh: FAILED"; exit 1; }
echo "pipeline/check.sh: $(grep -vc '^#' "$P/keys/EXPECTED.sha256") outputs equal to v1.3's"
