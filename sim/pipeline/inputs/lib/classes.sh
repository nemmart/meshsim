#!/bin/bash
# lib/classes.sh MESHSIM DIR: compiles, for Java 21, into DIR/
#   gold/, jumpmap/   gold and jumpmap at GOLD_SRC (pins.sh): src/d97ecde/ (v1.35, W19: committed snapshots in
#                     place of v1.3's git archive of wip/gold and wip/jumpmap; src/SNAPSHOTS.sh)
#   gold-v13/         gold at GOLD_V13_SRC (the v1.3 segment file): src/eeb64b5/
# (v1.35, W19, D42: no meshsim/: ConvertDelivery is the C++ convert_delivery; MESHSIM is unused, kept for the callers)
set -eu
. "$(dirname "$0")/../pins.sh"; . "$(dirname "$0")/../src/SNAPSHOTS.sh"
SRC=$(cd "$(dirname "$0")/../src" && pwd)
C=$2
rm -rf "$C"; mkdir -p "$C"/{gold,jumpmap,gold-v13}
javac --release 21 -nowarn -d "$C/gold" $(find "$SRC/d97ecde/gold/src" -name '*.java')
javac --release 21 -nowarn -d "$C/jumpmap" -cp "$C/gold" $(find "$SRC/d97ecde/jumpmap/src" -name '*.java')
javac --release 21 -nowarn -d "$C/gold-v13" $(find "$SRC/eeb64b5/gold/src" -name '*.java')
for c in gold/gold/Main.class jumpmap/jumpmap/Main.class gold-v13/gold/Main.class; do
  [ -f "$C/$c" ] || { echo "classes: no $c"; exit 1; }
done
echo "gold ${GOLD_LAST:0:7}, jumpmap ${JUMPMAP_LAST:0:7}, gold v1.3 ${GOLD_V13_LAST:0:7}" > "$C/VERSION"
