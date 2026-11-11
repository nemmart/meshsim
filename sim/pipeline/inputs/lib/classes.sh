#!/bin/bash
# lib/classes.sh MESHSIM DIR: compiles, for Java 21, into DIR/
#   gold/, jumpmap/   gold and jumpmap at GOLD_SRC (pins.sh): src/d97ecde/ (v1.35, W19: committed snapshots in
#                     place of v1.3's git archive of wip/gold and wip/jumpmap; src/SNAPSHOTS.sh)
#   gold-v13/         gold at GOLD_V13_SRC (the v1.3 segment file): src/eeb64b5/
#   meshsim/          v1.26.1's ConvertDelivery with meshsim's src/ and the replay harnesses (as v1.26.1's make_kit.sh)
set -eu
. "$(dirname "$0")/../pins.sh"; . "$(dirname "$0")/../src/SNAPSHOTS.sh"
SRC=$(cd "$(dirname "$0")/../src" && pwd)
M=$1; C=$2; S=$M/$MESHSIM_REL
rm -rf "$C"; mkdir -p "$C"/{gold,jumpmap,gold-v13,meshsim}
javac --release 21 -nowarn -d "$C/gold" $(find "$SRC/d97ecde/gold/src" -name '*.java')
javac --release 21 -nowarn -d "$C/jumpmap" -cp "$C/gold" $(find "$SRC/d97ecde/jumpmap/src" -name '*.java')
javac --release 21 -nowarn -d "$C/gold-v13" $(find "$SRC/eeb64b5/gold/src" -name '*.java')
javac --release 21 -nowarn -d "$C/meshsim" "$S"/src/*.java 2>&1 | grep -v '^Note:' || true
javac --release 21 -nowarn -d "$C/meshsim" -cp "$C/meshsim" "$S"/test/T.java "$S"/test/Replay*.java \
  "$S"/test/StageB.java "$S"/test/SyntheticDelivery.java "$S"/test/ConvertDelivery.java 2>&1 | grep -v '^Note:' || true
for c in gold/gold/Main.class jumpmap/jumpmap/Main.class gold-v13/gold/Main.class meshsim/ConvertDelivery.class; do
  [ -f "$C/$c" ] || { echo "classes: no $c"; exit 1; }
done
echo "gold ${GOLD_LAST:0:7}, jumpmap ${JUMPMAP_LAST:0:7}, gold v1.3 ${GOLD_V13_LAST:0:7}" > "$C/VERSION"
