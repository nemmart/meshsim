#!/bin/bash
# oracle/pipeline_crosscheck.sh (WORKER19, P1): pipeline/keys/EXPECTED.sha256 against v1.3's tools run now (sim/ref's
# KeyGen and SplitSets): every expected hash reproduced by v1.3, so pipeline/check.sh checks against v1.3's outputs.
set -eu
SIM=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
javac -nowarn -d "$T/c" "$SIM/ref/meshsim/test/KeyGen.java" "$SIM/ref/meshsim/test/SplitSets.java" 2>&1 | grep -v '^Note' || true
J="java -cp $T/c"
$J KeyGen "$T/keys-16384" --n 16384 --only uniform > /dev/null
$J KeyGen "$T/keys-65536" --n 65536 --only uniform > /dev/null
$J KeyGen "$T/n2000/keys" --n 2000 > /dev/null
$J SplitSets "$T/n2000/split" --n 2000 > /dev/null
(cd "$T" && grep -v '^#' "$SIM/pipeline/keys/EXPECTED.sha256" | sha256sum -c --quiet -) || { echo "pipeline_crosscheck: FAILED"; exit 1; }
echo "pipeline_crosscheck: $(grep -vc '^#' "$SIM/pipeline/keys/EXPECTED.sha256") expected outputs reproduced by v1.3's KeyGen and SplitSets"
