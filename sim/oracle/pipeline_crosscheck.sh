#!/bin/bash
# oracle/pipeline_crosscheck.sh (WORKER19, P1): pipeline/keys/EXPECTED.sha256 against v1.3's tools run now (sim/ref's
# KeyGen and SplitSets): every expected hash reproduced by v1.3, so pipeline/check.sh checks against v1.3's outputs.
# Also pipeline/FROM-V1.3.sha256 against the repository's v1.3/ (gold, jumpmap, the hard-cut lists).
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
TOP=$(cd "$SIM/.." && pwd)
grep -v '^#' "$SIM/pipeline/FROM-V1.3.sha256" | while read -r h p v; do
  [ "$(sha256sum < "$TOP/$v" | cut -c1-64)" = "$h" ] || { echo "pipeline_crosscheck: $v differs from FROM-V1.3.sha256"; exit 1; }
done
echo "pipeline_crosscheck: $(grep -vc '^#' "$SIM/pipeline/FROM-V1.3.sha256") files of v1.3/ equal to FROM-V1.3.sha256"
# inputs/src/ against the repository's history, and the snapshot build against v1.3's git-archive build (classes.sh)
grep -v '^#' "$SIM/pipeline/inputs/src/SNAPSHOTS.sha256" | while read -r h f o; do
  [ "$(git -C "$TOP" show "$o" | sha256sum | cut -c1-64)" = "$h" ] || { echo "pipeline_crosscheck: inputs/src/$f differs from $o"; exit 1; }
done
bash "$TOP/v1.3/inputs/lib/classes.sh" "$TOP" "$T/cls-v13" > "$T/cls-v13.log" 2>&1 || { tail "$T/cls-v13.log"; exit 1; }
bash "$SIM/pipeline/inputs/lib/classes.sh" "$TOP" "$T/cls-v135" > "$T/cls-v135.log" 2>&1 || { tail "$T/cls-v135.log"; exit 1; }
diff -r "$T/cls-v13" "$T/cls-v135" > /dev/null || { echo "pipeline_crosscheck: inputs' classes differ from v1.3's git-archive build"; exit 1; }
echo "pipeline_crosscheck: inputs/src equal to the pins' git archive; the classes (gold, jumpmap, gold v1.3, VERSION) equal v1.3's build"
