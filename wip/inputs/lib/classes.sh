#!/bin/bash
# lib/classes.sh MESHSIM DIR: compiles, for Java 21, into DIR/
#   gold/, jumpmap/   from meshsim at GOLD_SRC (pins.sh; git archive of wip/gold and wip/jumpmap)
#   gold-v13/         gold at GOLD_V13_SRC (the v1.3 segment file)
#   meshsim/          v1.26.1's ConvertDelivery with meshsim's src/ and the replay harnesses (as v1.26.1's make_kit.sh)
set -eu
. "$(dirname "$0")/../pins.sh"
M=$1; C=$2; S=$M/$MESHSIM_REL
rm -rf "$C"; mkdir -p "$C"/{gold,jumpmap,gold-v13,meshsim}
X=$(mktemp -d); trap 'rm -rf "$X"' EXIT
mkdir -p "$X/v12" "$X/v13"
git -C "$M" archive "$GOLD_SRC" wip/gold/src wip/jumpmap/src | tar -x -C "$X/v12"
git -C "$M" archive "$GOLD_V13_SRC" wip/gold/src | tar -x -C "$X/v13"
javac --release 21 -nowarn -d "$C/gold" $(find "$X/v12/wip/gold/src" -name '*.java')
javac --release 21 -nowarn -d "$C/jumpmap" -cp "$C/gold" $(find "$X/v12/wip/jumpmap/src" -name '*.java')
javac --release 21 -nowarn -d "$C/gold-v13" $(find "$X/v13/wip/gold/src" -name '*.java')
javac --release 21 -nowarn -d "$C/meshsim" "$S"/src/*.java 2>&1 | grep -v '^Note:' || true
javac --release 21 -nowarn -d "$C/meshsim" -cp "$C/meshsim" "$S"/test/T.java "$S"/test/Replay*.java \
  "$S"/test/StageB.java "$S"/test/SyntheticDelivery.java "$S"/test/ConvertDelivery.java 2>&1 | grep -v '^Note:' || true
for c in gold/gold/Main.class jumpmap/jumpmap/Main.class gold-v13/gold/Main.class meshsim/ConvertDelivery.class; do
  [ -f "$C/$c" ] || { echo "classes: no $c"; exit 1; }
done
echo "gold $(git -C "$M" log -1 --format=%h "$GOLD_SRC" -- wip/gold), jumpmap $(git -C "$M" log -1 --format=%h "$GOLD_SRC" -- wip/jumpmap), gold v1.3 $(git -C "$M" log -1 --format=%h "$GOLD_V13_SRC" -- wip/gold)" > "$C/VERSION"
